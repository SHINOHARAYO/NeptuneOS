#include "kernel/heap.h"
#include "kernel/mem.h"
#include "kernel/mmu.h"
#include "kernel/log.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HEAP_BASE 0xFFFF900000000000ULL
#define HEAP_FLAGS (MMU_FLAG_WRITE | MMU_FLAG_GLOBAL | MMU_FLAG_NOEXEC)

struct alloc_hdr {
    uint64_t class_idx;
    uint64_t size;
    uint64_t align;
};

#define HEAP_HDR_SIZE (sizeof(struct alloc_hdr))
#define HEAP_PAYLOAD_OFFSET ((HEAP_HDR_SIZE + 15) & ~15ULL) /* align header to 16-byte boundary */
#define LARGE_CLASS UINT64_MAX

static const size_t slab_classes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
static const size_t slab_count = sizeof(slab_classes) / sizeof(slab_classes[0]);

static uint64_t heap_cur = HEAP_BASE;
static uint64_t heap_end = HEAP_BASE;
static bool heap_ready = false;
static bool frees_enabled = false;
struct free_node { struct free_node *next; uint64_t size; uint64_t align; };
static struct free_node *free_lists[KHEAP_MAX_SLAB_CLASSES] = {0};
static struct free_node *free_large = NULL;
static uint64_t total_allocs = 0;
static uint64_t total_frees = 0;
static uint64_t slab_allocs[KHEAP_MAX_SLAB_CLASSES] = {0};
static uint64_t slab_reuses[KHEAP_MAX_SLAB_CLASSES] = {0};
static uint64_t large_allocs = 0;
static uint64_t large_reuses = 0;

static void map_next_page(void)
{
    uint64_t phys = pmm_alloc_page();
    mmu_map_page(heap_end, phys, HEAP_FLAGS);
    heap_end += 4096;
}

static uint64_t align_up_uint(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static inline bool is_canonical(uint64_t addr)
{
    uint64_t high = addr >> 47;
    return high == 0 || high == 0x1FFFF;
}

static void insert_large_node(struct free_node *node)
{
    if (!node || !is_canonical((uint64_t)node)) {
        log_error("insert_large_node: bad node");
        return;
    }

    node->align = 16;
    struct free_node *prev = NULL;
    struct free_node *cur = free_large;
    uint64_t addr = (uint64_t)node;

    while (cur && (uint64_t)cur < addr) {
        prev = cur;
        cur = cur->next;
    }

    node->next = cur;
    if (prev) {
        prev->next = node;
    } else {
        free_large = node;
    }

    /* coalesce with next */
    if (cur) {
        uint64_t node_end = addr + node->size;
        if (node_end == (uint64_t)cur) {
            node->size += cur->size;
            node->next = cur->next;
        }
    }

    /* coalesce with previous */
    if (prev) {
        uint64_t prev_end = (uint64_t)prev + prev->size;
        if (prev_end == addr) {
            prev->size += node->size;
            prev->next = node->next;
            node = prev;
            /* try coalesce forward again */
            if (node->next) {
                uint64_t node_end2 = (uint64_t)node + node->size;
                if (node_end2 == (uint64_t)node->next) {
                    node->size += node->next->size;
                    node->next = node->next->next;
                }
            }
        }
    }
}

void kheap_init(void)
{
    heap_cur = HEAP_BASE;
    heap_end = HEAP_BASE;
    map_next_page();
    log_info("Kernel heap initialized.");
    heap_ready = true;
}

static int pick_slab_class(size_t size, size_t align)
{
    size_t need = size + HEAP_HDR_SIZE;
    size_t req_align = align < 16 ? 16 : align;
    for (size_t i = 0; i < slab_count; ++i) {
        if (slab_classes[i] >= need && slab_classes[i] >= req_align) {
            return (int)i;
        }
    }
    return -1;
}

static void ensure_space(uint64_t aligned_start, size_t need)
{
    while (aligned_start + need > heap_end) {
        map_next_page();
    }
}

static void track_free_bytes(uint64_t *free_slab_bytes, uint64_t *free_large_bytes)
{
    const uint64_t max_walk = 65536; /* simple guard against corrupted loops */
    uint64_t slab_bytes = 0;
    uint64_t large_bytes = 0;

    for (size_t i = 0; i < slab_count; ++i) {
        const uint64_t class_size = slab_classes[i];
        struct free_node *node = free_lists[i];
        uint64_t seen = 0;
        while (node) {
            if (!is_canonical((uint64_t)node)) {
                log_error("track_free_bytes: non-canonical slab free node");
                break;
            }
            slab_bytes += class_size;
            node = node->next;
            if (++seen > max_walk) {
                log_error("track_free_bytes: slab free list too long/looping");
                break;
            }
        }
    }

    {
        struct free_node *node = free_large;
        uint64_t seen = 0;
        while (node) {
            if (!is_canonical((uint64_t)node)) {
                log_error("track_free_bytes: non-canonical large free node");
                break;
            }
            large_bytes += node->size;
            node = node->next;
            if (++seen > max_walk) {
                log_error("track_free_bytes: large free list too long/looping");
                break;
            }
        }
    }

    if (free_slab_bytes) {
        *free_slab_bytes = slab_bytes;
    }
    if (free_large_bytes) {
        *free_large_bytes = large_bytes;
    }
}

void *kalloc(size_t size, size_t align)
{
    if (align == 0) {
        align = 8;
    }
    if (size == 0) {
        return NULL;
    }

    uint64_t req_align = align < 16 ? 16 : align;
    size_t total_need = size + HEAP_PAYLOAD_OFFSET;

    /* try large free list first if alignment is big or size is big */
    struct free_node **prev = &free_large;
    struct free_node *node = free_large;
    while (node) {
        if (node->size >= total_need && node->align >= req_align) {
            *prev = node->next;
            uint8_t *block = (uint8_t *)node;
            struct alloc_hdr *hdr = (struct alloc_hdr *)block;
            hdr->class_idx = LARGE_CLASS;
            hdr->size = total_need;
            hdr->align = req_align;

            /* split leftover into a new free node if big enough */
            uint64_t leftover_start = align_up_uint((uint64_t)block + total_need, 16);
            if (leftover_start < (uint64_t)block + node->size) {
                uint64_t leftover_size = (uint64_t)block + node->size - leftover_start;
                if (leftover_size >= HEAP_PAYLOAD_OFFSET + 32) {
                    struct free_node *left = (struct free_node *)leftover_start;
                    if (!is_canonical((uint64_t)left)) {
                        log_error("Non-canonical leftover split node");
                    } else {
                    left->size = leftover_size;
                    left->align = 16;
                    left->next = free_large;
                    free_large = left;
                    }
                }
            }

            ++total_allocs;
            ++large_reuses;
            return (void *)(block + HEAP_PAYLOAD_OFFSET);
        }
        prev = &node->next;
        node = node->next;
    }

    int slab_idx = pick_slab_class(size, align);
    if (slab_idx >= 0) {
        struct free_node *node = free_lists[slab_idx];
        if (node) {
            free_lists[slab_idx] = node->next;
            ++total_allocs;
            ++slab_reuses[slab_idx];
            return (void *)((uint8_t *)node + HEAP_PAYLOAD_OFFSET);
        }

        heap_cur = align_up_uint(heap_cur, req_align);
        ensure_space(heap_cur, slab_classes[slab_idx]);
        uint8_t *block = (uint8_t *)heap_cur;
        heap_cur += slab_classes[slab_idx];
        /* store class index in header */
        struct alloc_hdr *hdr = (struct alloc_hdr *)block;
        hdr->class_idx = (uint64_t)slab_idx;
        hdr->size = slab_classes[slab_idx];
        hdr->align = req_align;
        ++total_allocs;
        ++slab_allocs[slab_idx];
        return (void *)(block + HEAP_PAYLOAD_OFFSET);
    }

    /* big allocation: bump only, no free support yet */
    heap_cur = align_up_uint(heap_cur + HEAP_PAYLOAD_OFFSET, req_align) - HEAP_PAYLOAD_OFFSET;
    ensure_space(heap_cur, total_need);
    struct alloc_hdr *hdr = (struct alloc_hdr *)heap_cur;
    hdr->class_idx = LARGE_CLASS;
    hdr->size = total_need;
    hdr->align = req_align;
    void *ptr = (void *)(heap_cur + HEAP_PAYLOAD_OFFSET);
    heap_cur += total_need;
    ++total_allocs;
    ++large_allocs;
    return ptr;
}

void *kalloc_zero(size_t size, size_t align)
{
    uint8_t *ptr = (uint8_t *)kalloc(size, align);
    if (!ptr) {
        return NULL;
    }
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = 0;
    }
    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr || !frees_enabled) {
        return;
    }
    uint8_t *block = (uint8_t *)ptr - HEAP_PAYLOAD_OFFSET;
    struct alloc_hdr *hdr = (struct alloc_hdr *)block;
    uint64_t idx = hdr->class_idx;
    if (idx == LARGE_CLASS) {
        struct free_node *node = (struct free_node *)block;
        node->size = hdr->size;
        node->align = hdr->align;
        if (!is_canonical((uint64_t)node)) {
            log_error("kfree large: non-canonical");
            return;
        }
        insert_large_node(node);
        ++total_frees;
        return;
    }
    if (idx >= slab_count) {
        return; /* unknown class */
    }
    struct free_node *node = (struct free_node *)block;
    if (!is_canonical((uint64_t)node)) {
        log_error("kfree slab: non-canonical");
        return;
    }
    node->next = free_lists[idx];
    free_lists[idx] = node;
    ++total_frees;
}

void kalloc_enable_frees(void)
{
    frees_enabled = true;
}

void kheap_get_stats(struct kheap_stats *out)
{
    if (!out) {
        return;
    }
    out->total_allocs = total_allocs;
    out->total_frees = total_frees;
    out->large_allocs = large_allocs;
    out->large_reuses = large_reuses;
    out->free_slab_bytes = 0;
    out->free_large_bytes = 0;

    for (size_t i = 0; i < KHEAP_MAX_SLAB_CLASSES; ++i) {
        if (i < slab_count) {
            out->slab_allocs[i] = slab_allocs[i];
            out->slab_reuses[i] = slab_reuses[i];
        } else {
            out->slab_allocs[i] = 0;
            out->slab_reuses[i] = 0;
        }
    }

    track_free_bytes(&out->free_slab_bytes, &out->free_large_bytes);
}

void kheap_dump_stats(void)
{
    struct kheap_stats stats;
    kheap_get_stats(&stats);
    log_info_hex("Heap allocs", stats.total_allocs);
    log_info_hex("Heap frees", stats.total_frees);
    log_info_hex("Heap large allocs", stats.large_allocs);
    log_info_hex("Heap large reuses", stats.large_reuses);
    for (size_t i = 0; i < slab_count; ++i) {
        log_debug_hex("Heap slab allocs", stats.slab_allocs[i]);
        log_debug_hex("Heap slab reuses", stats.slab_reuses[i]);
    }
    log_info_hex("Heap free slab bytes", stats.free_slab_bytes);
    log_info_hex("Heap free large bytes", stats.free_large_bytes);
}

int kheap_ready(void)
{
    return heap_ready ? 1 : 0;
}

int kheap_verify(void)
{
    if (!heap_ready) {
        return -1;
    }

    /* check slab free lists for canonical addresses and sane sizes */
    for (size_t i = 0; i < slab_count; ++i) {
        const uint64_t class_size = slab_classes[i];
        struct free_node *node = free_lists[i];
        uint64_t seen = 0;
        while (node) {
            if (!is_canonical((uint64_t)node)) {
                log_error("kheap_verify: non-canonical slab node");
                return -2;
            }
            /* ensure node is within heap range and aligned */
            uint64_t addr = (uint64_t)node;
            if (addr < HEAP_BASE || addr >= heap_end || (addr % 16) != 0) {
                log_error("kheap_verify: slab node out of range");
                return -3;
            }
            /* avoid excessive walk to catch loops */
            if (++seen > 65536) {
                log_error("kheap_verify: slab list too long/looping");
                return -4;
            }
            node = node->next;
        }
        /* class_size sanity */
        if (class_size < HEAP_PAYLOAD_OFFSET + 8) {
            log_error("kheap_verify: slab class too small");
            return -5;
        }
    }

    /* check large free list ordering and coalescing */
    struct free_node *prev = NULL;
    struct free_node *cur = free_large;
    uint64_t walks = 0;
    while (cur) {
        uint64_t addr = (uint64_t)cur;
        if (!is_canonical(addr)) {
            log_error("kheap_verify: non-canonical large node");
            return -6;
        }
        if (addr < HEAP_BASE || addr >= heap_end || (addr % 16) != 0) {
            log_error("kheap_verify: large node out of range/alignment");
            return -7;
        }
        if (prev && addr <= (uint64_t)prev) {
            log_error("kheap_verify: large list not strictly increasing");
            return -8;
        }
        uint64_t end = addr + cur->size;
        if (prev) {
            uint64_t prev_end = (uint64_t)prev + prev->size;
            if (prev_end == addr) {
                log_error("kheap_verify: adjacent large nodes not coalesced");
                return -9;
            }
        }
        prev = cur;
        cur = cur->next;
        if (++walks > 65536) {
            log_error("kheap_verify: large list too long/looping");
            return -10;
        }
        if (end > heap_end) {
            log_error("kheap_verify: large node extends past heap");
            return -11;
        }
    }

    return 0;
}
