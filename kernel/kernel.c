#include "kernel/console.h"
#include "kernel/idt.h"
#include "kernel/log.h"
#include "kernel/mem.h"
#include "kernel/panic.h"
#include "kernel/mmu.h"

#define VGA_PHYS 0xB8000ULL
#define VGA_HIGHER_HALF (HIGHER_HALF_BASE + VGA_PHYS)
#define MULTIBOOT2_MAGIC 0x36D76289U

#define ENABLE_NX_TEST 0

extern uint64_t pml4_table[];

#define ENABLE_FAULT_TEST 0

static inline void drop_identity_map(void)
{
    uint64_t phys_pml4 = (uint64_t)pml4_table;
    uint64_t *pml4_high = (uint64_t *)phys_to_higher_half(phys_pml4);
    pml4_high[0] = 0;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
}

static void trigger_page_fault_test(void)
{
#if ENABLE_FAULT_TEST
    log_info("Triggering page fault test (expect page fault panic)...");
    volatile uint64_t *ptr = (uint64_t *)0x1000; /* unmapped after dropping identity map */
    *ptr = 0xDEADBEEFCAFEBABEULL;
    (void)*ptr;
    log_warn("Page fault test unexpectedly did not fault.");
#endif
}

void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    if (magic != MULTIBOOT2_MAGIC) {
        panic("Invalid multiboot2 magic", magic);
    }

    const uint64_t multiboot_info_phys = (uint64_t)multiboot_info;
    volatile uint32_t multiboot_size = *(volatile uint32_t *)multiboot_info_phys; /* identity-mapped early */
    (void)multiboot_size;

    console_clear(0x1F); /* white on blue */
    log_init();
    log_set_level(LOG_LEVEL_DEBUG);
    log_info("Booting 64-bit kernel...");
    log_debug("Multiboot info validated.");
    log_debug_hex("Multiboot2 info size", multiboot_size);
    log_debug_hex("Multiboot2 info phys", multiboot_info_phys);

    log_info("Initializing IDT...");
    idt_init();
    log_info("IDT initialized.");

    log_info("Initializing physical memory manager...");
    mem_init(multiboot_info_phys);
    log_info("Physical memory manager initialized.");
    uint64_t max_phys = pmm_max_phys_addr();
    log_info_hex("Maximum managed physical address", max_phys);

    log_info("Extending higher-half direct map...");
    mmu_map_hhdm_2m(0, max_phys);
    log_info("Higher-half direct map updated.");
    log_info("Applying kernel section protections...");
    mmu_protect_kernel_sections();
    log_info("Kernel sections protected.");
    #if ENABLE_NX_TEST
    log_info("Running NX self-test (expect page fault)...");
    uint64_t nx_page = pmm_alloc_page();
    uint8_t *nx_virt = (uint8_t *)mmu_kmap(nx_page, MMU_FLAG_WRITE | MMU_FLAG_NOEXEC);
    nx_virt[0] = 0xC3; /* ret */
    __asm__ volatile("call *%0" : : "r"(nx_virt) : "memory"); /* should fault on instruction fetch */
    #endif
    log_debug_hex("PMM total bytes", pmm_total_bytes());
    log_debug_hex("PMM used bytes", pmm_used_bytes());

    /* allocator smoke test */
    log_debug("Running allocator self-test...");
    uint64_t before = pmm_used_bytes();
    uint64_t p1 = pmm_alloc_page();
    uint64_t p2 = pmm_alloc_page();
    log_debug("Allocated two pages:");
    console_write_hex(p1);
    console_write(" ");
    console_write_hex(p2);
    console_write("\n");
    log_debug("Freeing pages");
    pmm_free_page(p1);
    pmm_free_page(p2);
    uint64_t after = pmm_used_bytes();
    if (after != before) {
        panic("Allocator leak detected", after);
    }
    log_info("Allocator self-test passed.");

    log_info("Dropping identity map; switching to higher-half only.");
    drop_identity_map();
    log_info("Dropped identity map; higher-half only.");

    trigger_page_fault_test();

    
    for (;;) {
        __asm__ volatile("hlt");
    }
}
