#include "kernel/mem.h"
#include "kernel/console.h"
#include "kernel/log.h"
#include "kernel/panic.h"
#include "kernel/serial.h"
#include "kernel/mmu.h"
#include "kernel/spinlock.h"
#ifdef __aarch64__
#include "kernel/fdt.h"
#endif

#include <stdint.h>
#include <stddef.h>

extern char _kernel_end;
extern char _kernel_start;
extern char _kernel_phys_start;
extern char _kernel_phys_end;

#ifdef __aarch64__
#define META_REGION_LIMIT (16ULL << 30) /* 16 GiB */
#else
#define META_REGION_LIMIT (1ULL << 30) /* prefer metadata below 1 GiB */
#endif

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
};

struct pmm_region {
    uint64_t phys_start;
    uint64_t phys_end;
    uint64_t bitmap_phys;
    uint64_t bitmap_bytes;
    uint64_t total_pages;
    uint64_t reserved_pages;
};

#define MAX_PMM_REGIONS 32

static struct pmm_region regions[MAX_PMM_REGIONS];
static uint32_t region_count = 0;
static uint64_t managed_pages = 0;      /* allocatable pages (excludes metadata) */
static uint64_t reserved_pages = 0;     /* pages consumed by allocator metadata */
static uint64_t used_pages = 0;         /* includes reserved + allocations */
static uint64_t max_phys_end = 0;       /* highest address of any managed region */
static spinlock_t pmm_lock;

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

#ifndef __aarch64__
static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}
#endif

static inline uint8_t *bitmap_virt(const struct pmm_region *region)
{
    return (uint8_t *)phys_to_virt(region->bitmap_phys);
}

static void set_bit(struct pmm_region *region, uint64_t idx)
{
    uint8_t *bitmap = bitmap_virt(region);
    bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

static void clear_bit(struct pmm_region *region, uint64_t idx)
{
    uint8_t *bitmap = bitmap_virt(region);
    bitmap[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static int test_bit(const struct pmm_region *region, uint64_t idx)
{
    uint8_t *bitmap = bitmap_virt(region);
    return (bitmap[idx / 8] >> (idx % 8)) & 1u;
}

static void add_region(uint64_t start, uint64_t end)
{
    if (region_count >= MAX_PMM_REGIONS) {
        panic("Too many memory regions", region_count);
    }

    struct pmm_region *region = &regions[region_count++];
    region->phys_start = start;
    region->phys_end = end;
    region->bitmap_phys = 0;
    region->bitmap_bytes = 0;
    region->reserved_pages = 0;
    region->total_pages = (end - start) / 4096;
}

#ifndef __aarch64__
static void choose_regions(uint64_t info_addr)
{
    region_count = 0;
    managed_pages = 0;
    reserved_pages = 0;
    used_pages = 0;
    max_phys_end = 0;

    const uint64_t kernel_start_addr = (uint64_t)&_kernel_phys_start;
    const uint64_t kernel_end_addr = (uint64_t)&_kernel_phys_end;
    const uint8_t *info = (const uint8_t *)info_addr;

    uint32_t total_size = *(const uint32_t *)info;
    const uint8_t *tag_ptr = info + 8; /* skip total_size + reserved */
    const uint8_t *info_end = info + align_up(total_size, 8);

    while (tag_ptr < info_end) {
        const struct multiboot_tag *tag = (const struct multiboot_tag *)tag_ptr;
        if (tag->type == 0) {
            break;
        }

        if (tag->type == 6) { /* memory map */
            const struct multiboot_tag_mmap *mmap = (const struct multiboot_tag_mmap *)tag;
            const uint8_t *entry_ptr = (const uint8_t *)(mmap + 1);
            const uint8_t *mmap_end = tag_ptr + mmap->size;
            while (entry_ptr + mmap->entry_size <= mmap_end) {
                const struct multiboot_mmap_entry *entry = (const struct multiboot_mmap_entry *)entry_ptr;
                if (entry->type == 1) {
                    uint64_t start = entry->addr;
                    uint64_t end = entry->addr + entry->len;

                    if (end <= kernel_start_addr) {
                        entry_ptr += mmap->entry_size;
                        continue;
                    }
                    if (start < kernel_end_addr && end > kernel_start_addr) {
                        start = kernel_end_addr;
                    }

                    start = align_up(start, 4096);
                    end = align_down(end, 4096);
                    if (end > start) {
                        add_region(start, end);
                        if (end > max_phys_end) {
                            max_phys_end = end;
                        }
                    }
                }
                entry_ptr += mmap->entry_size;
            }
        }

        tag_ptr = (const uint8_t *)align_up((uint64_t)tag_ptr + tag->size, 8);
    }

    if (region_count == 0) {
        panic("No available memory region for allocator", 0);
    }
}
#endif

static void setup_bitmaps(void)
{
    managed_pages = 0;
    reserved_pages = 0;
    used_pages = 0;

    /* first pass: total bitmap space required */
    uint64_t total_bitmap_bytes = 0;
    for (uint32_t i = 0; i < region_count; ++i) {
        const struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }
        total_bitmap_bytes += align_up(region->total_pages, 8) / 8;
    }

    /* choose a low region already covered by the initial 1GiB direct map */
    struct pmm_region *meta_region = NULL;
    for (uint32_t i = 0; i < region_count; ++i) {
        struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }
        if (region->phys_start >= META_REGION_LIMIT) {
            continue;
        }
        uint64_t aligned_start = align_up(region->phys_start, 4096);
        uint64_t usable_bytes = region->phys_end - aligned_start;
        if (usable_bytes < total_bitmap_bytes) {
            continue;
        }
        if (!meta_region || region->phys_start < meta_region->phys_start) {
            meta_region = region;
        }
    }

    if (!meta_region) {
        panic("No low memory for allocator metadata", total_bitmap_bytes);
    }

    uint64_t meta_cursor = align_up(meta_region->phys_start, 4096);
    uint64_t meta_limit = meta_region->phys_end;
    if (meta_cursor + total_bitmap_bytes > meta_limit) {
        panic("Insufficient low memory for allocator metadata", total_bitmap_bytes);
    }
    log_debug("Selected PMM metadata region in low memory");
    log_debug_hex("Metadata region start", meta_region->phys_start);
    log_debug_hex("Metadata region end", meta_region->phys_end);
    log_debug_hex("Metadata bytes needed", total_bitmap_bytes);

    for (uint32_t i = 0; i < region_count; ++i) {
        struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }

        region->bitmap_phys = meta_cursor;
        region->bitmap_bytes = align_up(region->total_pages, 8) / 8;
        region->reserved_pages = 0; /* metadata lives in meta_region */
        meta_cursor += region->bitmap_bytes;

        uint8_t *bitmap = bitmap_virt(region);
        for (uint64_t b = 0; b < region->bitmap_bytes; ++b) {
            bitmap[b] = 0;
        }
    }

    /* reserve pages consumed by the shared metadata pool */
    uint64_t metadata_bytes = align_up(meta_cursor - meta_region->phys_start, 4096);
    meta_region->reserved_pages = metadata_bytes / 4096;
    log_debug_hex("Metadata bytes reserved", metadata_bytes);
    log_debug_hex("Metadata pages reserved", meta_region->reserved_pages);

    for (uint32_t i = 0; i < region_count; ++i) {
        struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }

        for (uint64_t page = 0; page < region->reserved_pages; ++page) {
            set_bit(region, page);
        }

        reserved_pages += region->reserved_pages;
        used_pages += region->reserved_pages;
        managed_pages += (region->total_pages - region->reserved_pages);
    }

    if (managed_pages == 0) {
        panic("No allocatable memory after metadata reservation", 0);
    }
}

static struct pmm_region *find_region(uint64_t addr)
{
    for (uint32_t i = 0; i < region_count; ++i) {
        struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }
        if (addr >= region->phys_start && addr < region->phys_end) {
            return region;
        }
    }
    return NULL;
}

void mem_init(uint64_t multiboot_info)
{
#ifdef __aarch64__
    (void)multiboot_info;
    log_info("Initializing AArch64 memory...");
    
    uint64_t ram_start = 0;
    uint64_t ram_size = 0;
    
    if (multiboot_info == 0) {
        log_warn("FDT Address is 0. Skipping scan to avoid crash. Fallback enabled.");
        /* Scanning caused crashes on some setups. Rely on fallback. */
    }
    
    if (multiboot_info && fdt_get_memory(multiboot_info, &ram_start, &ram_size)) {
        log_info("FDT Memory Detection Successful");
        log_info_hex("RAM Start", ram_start);
        log_info_hex("RAM Size", ram_size);
    } else {
        log_info_hex("FDT Address", multiboot_info);
        log_warn("FDT Memory Detection Failed. Fallback to 4GB.");
        ram_start = 0x40000000;
        ram_size = 0x100000000ULL; /* 4GB */
    }

    uint64_t k_end = (uint64_t)&_kernel_phys_end;
    k_end = align_up(k_end, 4096);
    
    /* Ensure we don't map kernel memory as free */
    if (k_end > ram_start) {
        /* If kernel is inside this region, start after kernel */
        /* Assuming one big region for now (QEMU virt usually gives one contiguous block) */
        /* If multiple regions, we'd need loop. */
        uint64_t region_end = ram_start + ram_size;
        if (k_end < region_end) {
            add_region(k_end, region_end);
            max_phys_end = region_end;
        }
    } else {
        add_region(ram_start, ram_start + ram_size);
        max_phys_end = ram_start + ram_size;
    }
    
    /* We must populate region_count etc manually or via add_region */
    /* add_region increments region_count */
#else
    choose_regions(multiboot_info);
#endif
    setup_bitmaps();

    console_write("PMM regions: ");
    console_write_hex(region_count);
    console_write("\nTotal managed bytes: ");
    console_write_hex(managed_pages * 4096);
    console_write("\n");

    serial_write("PMM regions: ");
    serial_write_hex(region_count);
    serial_write("\r\nTotal managed bytes: ");
    serial_write_hex(managed_pages * 4096);
    serial_write("\r\n");

    for (uint32_t i = 0; i < region_count; ++i) {
        const struct pmm_region *region = &regions[i];
        if (region->total_pages == 0) {
            continue;
        }

        console_write("Region ");
        console_write_hex(i);
        console_write(": ");
        console_write_hex(region->phys_start);
        console_write(" - ");
        console_write_hex(region->phys_end);
        console_write(" pages=");
        console_write_hex(region->total_pages - region->reserved_pages);
        console_write("\n");

        serial_write("Region ");
        serial_write_hex(i);
        serial_write(": ");
        serial_write_hex(region->phys_start);
        serial_write(" - ");
        serial_write_hex(region->phys_end);
        serial_write(" pages=");
        serial_write_hex(region->total_pages - region->reserved_pages);
        serial_write("\r\n");
    }

    log_info("Physical memory allocator ready.");
}

static uint32_t pmm_cursor_region = 0;
static uint64_t pmm_cursor_page_idx = 0;

uint64_t pmm_alloc_page(void)
{
    spinlock_acquire_irqsave(&pmm_lock);
    
    uint32_t start_r = pmm_cursor_region;
    uint64_t start_p = pmm_cursor_page_idx;
    
    for (uint32_t i = 0; i < region_count; ++i) {
        uint32_t r = (start_r + i) % region_count;
        struct pmm_region *region = &regions[r];
        
        if (region->total_pages <= region->reserved_pages) {
            continue;
        }

        /* Determine start page for this region */
        uint64_t p_current = (r == start_r) ? start_p : region->reserved_pages;
        
        /* Sanity check */
        if (p_current >= region->total_pages || p_current < region->reserved_pages) {
            p_current = region->reserved_pages;
        }

        /* Forward search from current position */
        for (uint64_t page = p_current; page < region->total_pages; ++page) {
            if (!test_bit(region, page)) {
                set_bit(region, page);
                ++used_pages;
                
                pmm_cursor_region = r;
                pmm_cursor_page_idx = page + 1;
                
                spinlock_release_irqrestore(&pmm_lock);
                return region->phys_start + (page * 4096);
            }
        }
        
        /* Wrap around: if we started in the middle, search the beginning */
        if (r == start_r && p_current > region->reserved_pages) {
             for (uint64_t page = region->reserved_pages; page < p_current; ++page) {
                if (!test_bit(region, page)) {
                    set_bit(region, page);
                    ++used_pages;
                    
                    pmm_cursor_region = r;
                    pmm_cursor_page_idx = page + 1;
                    
                    spinlock_release_irqrestore(&pmm_lock);
                    return region->phys_start + (page * 4096);
                }
             }
        }
    }
    
    spinlock_release_irqrestore(&pmm_lock);
    panic("Out of physical memory", 0);
}

void pmm_free_page(uint64_t addr)
{
    spinlock_acquire_irqsave(&pmm_lock);
    struct pmm_region *region = find_region(addr);
    if (!region) {
        spinlock_release_irqrestore(&pmm_lock);
        panic("Attempt to free non-managed page", addr);
    }

    uint64_t idx = (addr - region->phys_start) / 4096;
    if (idx >= region->total_pages) {
        spinlock_release_irqrestore(&pmm_lock);
        panic("Attempt to free outside region bounds", addr);
    }
    if (idx < region->reserved_pages) {
        spinlock_release_irqrestore(&pmm_lock);
        panic("Attempt to free allocator metadata page", addr);
    }
    if (!test_bit(region, idx)) {
        spinlock_release_irqrestore(&pmm_lock);
        panic("Double free detected", addr);
    }
    clear_bit(region, idx);
    --used_pages;
    spinlock_release_irqrestore(&pmm_lock);
}

uint64_t pmm_total_bytes(void)
{
    return managed_pages * 4096;
}

uint64_t pmm_used_bytes(void)
{
    return used_pages * 4096;
}

uint64_t pmm_max_phys_addr(void)
{
    return max_phys_end;
}
