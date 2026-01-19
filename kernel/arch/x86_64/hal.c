#include <kernel/hal.h>
#include <kernel/mem.h>
#include <kernel/mmu.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <stdint.h>

/* Multiboot 2 definitions (local to x86 HAL) */
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

/* Defined in linker script */
extern char _kernel_phys_start;
extern char _kernel_phys_end;

extern uint64_t pml4_table[];

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

void arch_init_platform(void)
{
    /* x86 specific platform init if needed (e.g. GDT/IDT is done early in kernel.c) */
    /* Could move PIC/PIT init here later, but for now they are in kernel.c */
}

void arch_mem_init(uint64_t info_phys)
{
    if (info_phys == 0) {
        panic("Multiboot info missing", 0);
    }

    const uint64_t kernel_start_addr = (uint64_t)&_kernel_phys_start;
    const uint64_t kernel_end_addr = (uint64_t)&_kernel_phys_end;
    const uint8_t *info = (const uint8_t *)phys_to_virt(info_phys);

    uint32_t total_size = *(const uint32_t *)info;
    const uint8_t *tag_ptr = info + 8; /* skip total_size + reserved */
    const uint8_t *info_end = info + align_up(total_size, 8);

    uint64_t max_phys_end = 0;

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
                if (entry->type == 1) { /* Available RAM */
                    uint64_t start = entry->addr;
                    uint64_t end = entry->addr + entry->len;

                    /* Kernel intersection check */
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
                        pmm_add_region(start, end);
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
    
    /* We don't expose max_phys_end directly via API, pmm detects it via regions */
}

void arch_drop_identity_map(void)
{
    uint64_t phys_pml4 = (uint64_t)pml4_table;
    uint64_t *pml4_high = (uint64_t *)phys_to_higher_half(phys_pml4);
    pml4_high[0] = 0;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
}

int arch_log_should_mirror_to_serial(void)
{
    return 1; /* x86 uses VGA, so mirror to serial for debug logging */
}
