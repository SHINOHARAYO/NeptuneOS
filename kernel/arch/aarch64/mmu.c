#include <kernel/mmu.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/mem.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Defined in start.s */
extern uint64_t boot_pml4[];

#define PAGE_SIZE_2M (1ULL << 21)
#define HHDM_PML4_INDEX ((ARCH_HHDM_BASE >> 39) & 0x1FF)

static bool hhdm_ready = false;

static inline void *table_ptr(uint64_t phys)
{
    return (void *)phys_to_hhdm(phys);
}

static inline uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static inline uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static inline uint64_t *pml4_high(void)
{
    /* Access boot_pml4 via HHDM to avoid unmapping issues during section protection */
    /* boot_pml4 is in .pgtables, which might be unmapped temporarily when splitting blocks */
    /* virt_to_phys works because it uses arithmetic relative to HIGHER_HALF_BASE */
    return (uint64_t *)phys_to_hhdm(virt_to_phys(boot_pml4));
}

static void zero_page(uint64_t phys)
{
    uint64_t *ptr = (uint64_t *)table_ptr(phys);
    for (size_t i = 0; i < 512; ++i) {
        ptr[i] = 0;
    }
}

static uint64_t *ensure_table(uint64_t *parent, uint16_t index)
{
    uint64_t entry = parent[index];
    uint64_t phys;
    
    if (!(entry & ARCH_PTE_VALID)) {
        phys = pmm_alloc_page();
        if (!phys) panic("mmu: OOM in ensure_table", 0);
        zero_page(phys);
        /* L0-L2 Table Descriptor: Valid | Table */
        parent[index] = phys | ARCH_PTE_VALID | ARCH_PTE_TABLE;
    } else {
        /* Check if it's a Block (Huge Page) - sanity check */
        /* Table has bit 1 set. Block has bit 1 clear. */
        if (!(entry & ARCH_PTE_TABLE)) {
             /* It is valid but not a table -> Block. */
             return NULL;
        }
        phys = entry & ~0xFFFULL;
    }
    return (uint64_t *)table_ptr(phys);
}

static uint64_t flags_to_desc(uint64_t phys, uint64_t flags)
{
    uint64_t desc = (phys & ~0xFFFULL) | ARCH_PTE_VALID | ARCH_PTE_PAGE | ARCH_PTE_AF | ARCH_PTE_SH_INNER | ARCH_PTE_ATTR_NORMAL;

    if (flags & MMU_FLAG_WRITE) {
        if (flags & MMU_FLAG_USER) desc |= ARCH_PTE_AP_RW_USER;
        else desc |= ARCH_PTE_AP_RW_EL1;
    } else {
        /* Read Only */
        if (flags & MMU_FLAG_USER) desc |= ARCH_PTE_AP_RO_USER;
        else desc |= ARCH_PTE_AP_RO_EL1;
    }
    
    if (flags & MMU_FLAG_NOEXEC) {
        desc |= ARCH_PTE_UXN | ARCH_PTE_PXN;
    } else {
        /* Executable */
        desc |= ARCH_PTE_UXN; /* Default: Kernel code, User XN */
        if (flags & MMU_FLAG_USER) {
            desc &= ~ARCH_PTE_UXN;
            desc |= ARCH_PTE_PXN; /* User code, Kernel XN */
        }
    }

    if (!(flags & MMU_FLAG_GLOBAL)) {
       desc |= ARCH_PTE_NG;
    }
    
    return desc;
}

void mmu_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* Always map into Kernel TTBR1 (boot_pml4) for High Addresses */
    /* This function is typically used for Kernel Heap, IO, etc. */
    
    if ((virt & 0xFFF) || (phys & 0xFFF)) {
        panic("mmu_map_page: unaligned", virt | phys);
    }
    
    uint64_t *pml4 = pml4_high(); /* boot_pml4 */
    
    /* Indices */
    uint16_t idx0 = (virt >> 39) & 0x1FF;
    uint16_t idx1 = (virt >> 30) & 0x1FF;
    uint16_t idx2 = (virt >> 21) & 0x1FF;
    uint16_t idx3 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = ensure_table(pml4, idx0);
    if (!pdpt) panic("mmu_map_page: hit L0 block", virt);
    uint64_t *pd   = ensure_table(pdpt, idx1);
    if (!pd) panic("mmu_map_page: hit L1 block", virt);
    uint64_t *pt   = ensure_table(pd,   idx2);
    if (!pt) panic("mmu_map_page: hit L2 block", virt);
    
    pt[idx3] = flags_to_desc(phys, flags);
    
    arch_invlpg(virt);
}

int mmu_map_page_in(uint64_t root_phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* Used for User Mappings (TTBR0) */
    if (!root_phys || (virt & 0xFFF) || (phys & 0xFFF)) return -1;

    uint64_t *pml4 = (uint64_t *)table_ptr(root_phys);
    
    uint16_t idx0 = (virt >> 39) & 0x1FF;
    uint16_t idx1 = (virt >> 30) & 0x1FF;
    uint16_t idx2 = (virt >> 21) & 0x1FF;
    uint16_t idx3 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = ensure_table(pml4, idx0);
    uint64_t *pd   = ensure_table(pdpt, idx1);
    uint64_t *pt   = ensure_table(pd,   idx2);
    
    pt[idx3] = flags_to_desc(phys, flags);
    return 0;
}

uint64_t mmu_create_user_pml4(void)
{
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    zero_page(phys);
    return phys;
}

void mmu_unmap_page(uint64_t virt)
{
    /* Kernel unmap */
    uint64_t *pml4 = pml4_high();
    uint16_t idx0 = (virt >> 39) & 0x1FF;
    uint16_t idx1 = (virt >> 30) & 0x1FF;
    uint16_t idx2 = (virt >> 21) & 0x1FF;
    uint16_t idx3 = (virt >> 12) & 0x1FF;
    
    if (!(pml4[idx0] & ARCH_PTE_VALID)) return;
    uint64_t *pdpt = (uint64_t *)table_ptr(pml4[idx0] & ~0xFFFULL);
    
    if (!(pdpt[idx1] & ARCH_PTE_VALID)) return;
    uint64_t *pd = (uint64_t *)table_ptr(pdpt[idx1] & ~0xFFFULL);
    
    if (!(pd[idx2] & ARCH_PTE_VALID)) return;
    uint64_t *pt = (uint64_t *)table_ptr(pd[idx2] & ~0xFFFULL);
    
    pt[idx3] = 0;
    arch_invlpg(virt);
}

void mmu_map_hhdm_2m(uint64_t phys_start, uint64_t phys_end)
{
    uint64_t start = align_down(phys_start, PAGE_SIZE_2M);
    uint64_t end = align_up(phys_end, PAGE_SIZE_2M);
    
    if (start >= end) return;
    
    log_info_hex("HHDM map begin", start);
    log_info_hex("HHDM map end", end);
    
    /* HHDM is at ARCH_HHDM_BASE in TTBR1 */
    /* ARCH_HHDM_BASE is typically Index 256 or similar in TTBR1 */
    /* The caller expects us to map [phys_start, phys_end] to HHDM+phys_start */
    
    /* We need to ensure PGD[HHDM_INDEX] exists */
    uint64_t *pml4 = pml4_high();
    
    /* We iterate phys addresses */
    for (uint64_t p = start; p < end; p += PAGE_SIZE_2M) {
        uint64_t v = phys_to_hhdm(p);
        
        uint16_t idx0 = (v >> 39) & 0x1FF;
        uint16_t idx1 = (v >> 30) & 0x1FF;
        uint16_t idx2 = (v >> 21) & 0x1FF;
        
        uint64_t *pdpt = ensure_table(pml4, idx0);
        if (!pdpt) {
            /* L0 Block? Valid but weird. */
            p = (p & ~((1ULL << 39) - 1)) + (1ULL << 39) - PAGE_SIZE_2M;
            continue;
        }
        
        uint64_t *pd = ensure_table(pdpt, idx1);
        if (!pd) {
            /* L1 Block (1GB). Already mapped. Skip. */
            /* Align p to next 1GB boundary */
            /* (p | 1GB_MASK) + 1 -> Next 1GB */
            /* But we are in a loop p += 2MB */
            /* So set p to (Current 1GB End) - 2MB */
            uint64_t current_1gb_end = (p & ~((1ULL << 30) - 1)) + (1ULL << 30);
            p = current_1gb_end - PAGE_SIZE_2M; 
            continue;
        }
        
        /* L2 Block Descriptor */
        /* Valid | AF | SH_INNER | ATTR_NORMAL | RW_EL1 | UXN | PXN */
        /* Bit 1 (Table) must be 0 for Block */
        uint64_t desc = (p & ~0xFFFULL) | ARCH_PTE_VALID | ARCH_PTE_AF | ARCH_PTE_SH_INNER | ARCH_PTE_ATTR_NORMAL;
        desc |= ARCH_PTE_AP_RW_EL1; 
        desc |= ARCH_PTE_UXN | ARCH_PTE_PXN; /* No exec data */
        
        /* Ensure Block bit (bit 1) is 0. ARCH_PTE_VALID is 1. */
        /* So desc & 3 == 1. Correct. */
        pd[idx2] = desc;
    }
    
    arch_mmu_flush_tlb();
    hhdm_ready = true;
}

static inline uint64_t align_down_4k(uint64_t value) { return value & ~0xFFFULL; }
static inline uint64_t align_up_4k(uint64_t value) { return (value + 0xFFFULL) & ~0xFFFULL; }

void mmu_protect_kernel_sections(void)
{
    /* Remap .text as RX, .rodata as R, .data/.bss as RW */
    extern char _text_start, _text_end;
    extern char _rodata_start, _rodata_end;
    extern char _data_start, _data_end;
    extern char _bss_start, _bss_end;
    
    const uint64_t text_start = virt_to_phys(&_text_start);
    const uint64_t text_end = virt_to_phys(&_text_end);
    const uint64_t rodata_start = virt_to_phys(&_rodata_start);
    const uint64_t rodata_end = virt_to_phys(&_rodata_end);
    const uint64_t data_start = virt_to_phys(&_data_start);
    const uint64_t data_end = virt_to_phys(&_data_end);
    const uint64_t bss_start = virt_to_phys(&_bss_start);
    const uint64_t bss_end = virt_to_phys(&_bss_end);
    
    uint64_t text_flags = MMU_FLAG_GLOBAL; /* Exec, RO */
    uint64_t ro_flags = MMU_FLAG_GLOBAL | MMU_FLAG_NOEXEC; /* RO, NX */
    uint64_t data_flags = MMU_FLAG_GLOBAL | MMU_FLAG_WRITE | MMU_FLAG_NOEXEC; /* RW, NX */
    
    for (uint64_t p = align_down_4k(text_start); p < align_up_4k(text_end); p += 4096) {
        mmu_map_page(phys_to_higher_half(p), p, text_flags);
    }
    for (uint64_t p = align_down_4k(rodata_start); p < align_up_4k(rodata_end); p += 4096) {
        mmu_map_page(phys_to_higher_half(p), p, ro_flags);
    }
    for (uint64_t p = align_down_4k(data_start); p < align_up_4k(data_end); p += 4096) {
        mmu_map_page(phys_to_higher_half(p), p, data_flags);
    }
    for (uint64_t p = align_down_4k(bss_start); p < align_up_4k(bss_end); p += 4096) {
        mmu_map_page(phys_to_higher_half(p), p, data_flags);
    }
    
    /* Map everything else up to _kernel_end (pgtables, stack) as RW/NX */
    extern char _kernel_end;
    const uint64_t kernel_end = virt_to_phys(&_kernel_end);
    for (uint64_t p = align_up_4k(bss_end); p < align_up_4k(kernel_end); p += 4096) {
        mmu_map_page(phys_to_higher_half(p), p, data_flags);
    }
    
    arch_mmu_flush_tlb();
    log_info("Kernel sections protected (AArch64)");
}




void arch_flush_cache(const void *virt, uint64_t size)
{
    uint64_t addr = (uint64_t)virt;
    uint64_t end = addr + size;
    
    /* Read Cache Type Register to get line sizes */
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    
    /* D-Cache Line Size: bits [19:16] is LOG2(words). Words = 4 bytes.
       Line size = 4 << (ctr >> 16 & 0xF) */
    uint32_t d_line_size = 4 << ((ctr >> 16) & 0xF);
    
    /* Clean Data Cache by VA to PoU */
    uint64_t p = addr & ~(d_line_size - 1);
    while (p < end) {
        __asm__ volatile("dc cvau, %0" : : "r"(p));
        p += d_line_size;
    }
    
    __asm__ volatile("dsb ish");
    
    /* Invalidate I-Cache (All) - simpler than iterating by VA and safer for VIPT/PIPT nuances with aliases */
    __asm__ volatile("ic ialluis");
    
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

int mmu_handle_fault(uint64_t addr, int flags)
{
    log_info_hex("MMU Fault Address", addr);
    log_info_hex("MMU Fault Flags", (uint64_t)flags);
    log_error("Page Fault Detected");
    panic("Page Fault", addr);
    return 0;
}
