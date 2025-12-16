#include "kernel/mem.h"
#include "kernel/console.h"
#include "kernel/log.h"
#include "kernel/panic.h"
#include "kernel/serial.h"

#include <stdint.h>
#include <stddef.h>

extern char _kernel_end;
extern char _kernel_start;
extern char _kernel_phys_start;
extern char _kernel_phys_end;

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

static uint64_t managed_start = 0;
static uint64_t managed_end = 0;
static uint64_t bitmap_base = 0;
static uint64_t managed_pages = 0;
static uint64_t reserved_pages = 0;
static uint64_t used_pages = 0;
static uint64_t total_usable_bytes = 0;

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static void set_bit(uint64_t idx)
{
    uint8_t *bitmap = (uint8_t *)bitmap_base;
    bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

static void clear_bit(uint64_t idx)
{
    uint8_t *bitmap = (uint8_t *)bitmap_base;
    bitmap[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static int test_bit(uint64_t idx)
{
    uint8_t *bitmap = (uint8_t *)bitmap_base;
    return (bitmap[idx / 8] >> (idx % 8)) & 1u;
}

static void choose_region(uint64_t info_addr)
{
    const uint64_t kernel_start_addr = (uint64_t)&_kernel_phys_start;
    const uint64_t kernel_end_addr = (uint64_t)&_kernel_phys_end;
    const uint8_t *info = (const uint8_t *)info_addr;

    uint32_t total_size = *(const uint32_t *)info;
    const uint8_t *tag_ptr = info + 8; /* skip total_size + reserved */
    const uint8_t *info_end = info + align_up(total_size, 8);

    uint64_t best_start = 0;
    uint64_t best_end = 0;
    total_usable_bytes = 0;

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
                    total_usable_bytes += entry->len;
                    uint64_t start = entry->addr;
                    uint64_t end = entry->addr + entry->len;
                    if (end <= kernel_end_addr) {
                        entry_ptr += mmap->entry_size;
                        continue;
                    }
                    if (start < kernel_start_addr) {
                        start = kernel_start_addr;
                    }
                    if (start < kernel_end_addr) {
                        start = kernel_end_addr;
                    }
                    start = align_up(start, 4096);
                    end &= ~(uint64_t)(4095);
                    if (end > start && (end - start) > (best_end - best_start)) {
                        best_start = start;
                        best_end = end;
                    }
                }
                entry_ptr += mmap->entry_size;
            }
        }

        tag_ptr = (const uint8_t *)align_up((uint64_t)tag_ptr + tag->size, 8);
    }

    if (best_start == 0 || best_end == 0) {
        panic("No available memory region for allocator", 0);
    }

    managed_start = best_start;
    managed_end = best_end;
    managed_pages = (managed_end - managed_start) / 4096;
}

static void setup_bitmap(void)
{
    uint64_t bitmap_bytes = align_up(managed_pages, 8) / 8;
    bitmap_base = managed_start;
    uint64_t bitmap_end = align_up(bitmap_base + bitmap_bytes, 4096);

    /* zero bitmap */
    for (uint64_t i = 0; i < bitmap_bytes; ++i) {
        ((uint8_t *)bitmap_base)[i] = 0;
    }

    /* mark bitmap storage itself as used */
    reserved_pages = (bitmap_end - managed_start) / 4096;
    for (uint64_t i = 0; i < reserved_pages; ++i) {
        set_bit(i);
    }
    used_pages = reserved_pages;
}

void mem_init(uint64_t multiboot_info)
{
    choose_region(multiboot_info);
    setup_bitmap();

    console_write("PMM region ");
    console_write_hex(managed_start);
    console_write(" - ");
    console_write_hex(managed_end);
    console_write("\nTotal usable: ");
    console_write_hex(total_usable_bytes);
    console_write(" bytes\n");

    serial_write("PMM region ");
    serial_write_hex(managed_start);
    serial_write(" - ");
    serial_write_hex(managed_end);
    serial_write("\r\nTotal usable: ");
    serial_write_hex(total_usable_bytes);
    serial_write(" bytes\r\n");

    log_info("Physical memory allocator ready.");
}

uint64_t pmm_alloc_page(void)
{
    for (uint64_t i = reserved_pages; i < managed_pages; ++i) {
        if (!test_bit(i)) {
            set_bit(i);
            ++used_pages;
            return managed_start + (i * 4096);
        }
    }
    panic("Out of physical memory", 0);
}

void pmm_free_page(uint64_t addr)
{
    if (addr < managed_start || addr >= managed_end) {
        panic("Attempt to free non-managed page", addr);
    }
    uint64_t idx = (addr - managed_start) / 4096;
    if (idx < reserved_pages) {
        panic("Attempt to free allocator metadata page", addr);
    }
    if (!test_bit(idx)) {
        panic("Double free detected", addr);
    }
    clear_bit(idx);
    --used_pages;
}

uint64_t pmm_total_bytes(void)
{
    return total_usable_bytes;
}

uint64_t pmm_used_bytes(void)
{
    return used_pages * 4096;
}
