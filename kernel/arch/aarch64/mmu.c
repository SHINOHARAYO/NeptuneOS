#include <kernel/mmu.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/user.h>

#include <stdint.h>
#include <stddef.h>

/* Defined in start.s */
extern uint64_t boot_pml4[];

/* Also we need to know where page tables end to allocate new ones safely?
   Or use pmm_alloc_page() for new tables?
   pmm assumes heap/mem initialized.
   Early mapping (before pmm) requires static allocator or careful ordering.
   `mmu_map_hhdm_2m` is called BEFORE `kheap_init`, but AFTER `pmm_init`.
   So `pmm_alloc_page` is available.
*/

/* Helper to get phys address of boot_pml4. 
   Since kernel is mapped high, boot_pml4 is high.
   We need its physical address to put in CR3 (TTBR).
   But here we just want to EDIT it.
   It's mapped in High Higher Half (Kernel).
   So we can access it directly via pointer.
*/

/* Function definitions moved to end of file */

/* AArch64 PTE Bits */
#define PTE_VALID      (1ULL << 0)
#define PTE_PAGE       (1ULL << 1)
#define PTE_USER       (1ULL << 6) /* AP[1] = 1 (EL0/EL1) -> Actually AP is [7:6]. 01=RW/User. 11=RO/User. Bit 6 is set in both. */
#define PTE_RO         (1ULL << 7) /* AP[2] = 1 (Read Only) */
#define PTE_SH_INNER   (3ULL << 8)
#define PTE_AF         (1ULL << 10)
#define PTE_NX         (1ULL << 54) /* UXN */
#define PTE_PXN        (1ULL << 53)

/* Software bits: 55-58 are reserved for software use */
#define PTE_COW        (1ULL << 55) 

#define PTE_RW         0 /* RW means bit 7 is clear */

static uint64_t *pte_lookup(uint64_t pml4_phys, uint64_t virt)
{
    /* AArch64 4-level walk (assumed 48-bit VA, 4KB granularity) */
    uint64_t *pml4 = (uint64_t *)phys_to_hhdm(pml4_phys);
    uint16_t l0_idx = (virt >> 39) & 0x1FF;
    uint16_t l1_idx = (virt >> 30) & 0x1FF;
    uint16_t l2_idx = (virt >> 21) & 0x1FF;
    uint16_t l3_idx = (virt >> 12) & 0x1FF;

    uint64_t l0e = pml4[l0_idx];
    if (!(l0e & PTE_VALID)) return NULL;
    
    uint64_t *l1 = (uint64_t *)phys_to_hhdm(l0e & ~0xFFFULL);
    uint64_t l1e = l1[l1_idx];
    if (!(l1e & PTE_VALID)) return NULL; /* TODO: Handle Block mappings if any */

    uint64_t *l2 = (uint64_t *)phys_to_hhdm(l1e & ~0xFFFULL);
    uint64_t l2e = l2[l2_idx];
    if (!(l2e & PTE_VALID)) return NULL;

    uint64_t *l3 = (uint64_t *)phys_to_hhdm(l2e & ~0xFFFULL);
    return &l3[l3_idx];
}

static inline void invlpg_page(uint64_t virt)
{
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> 12) : "memory");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

static int handle_user_cow_fault(uint64_t aspace, uint64_t page)
{
    uint64_t *pte = pte_lookup(aspace, page);
    if (!pte) return 0;
    
    uint64_t entry = *pte;
    if (!(entry & PTE_VALID) || !(entry & PTE_COW)) return 0;
    
    /* Allocate new page */
    uint64_t old_phys = entry & ~0xFFFULL; // Mask software bits too? No, usually fine.
    // Actually we need to mask attribute bits carefully. 0000FFFFFFFFF000 usually.
    old_phys &= 0x0000FFFFFFFFF000ULL;

    uint64_t new_phys = pmm_alloc_page();
    if (!new_phys) return 0;
    
    /* Copy Content */
    uint8_t *dst = (uint8_t *)phys_to_hhdm(new_phys);
    uint8_t *src = (uint8_t *)phys_to_hhdm(old_phys);
    for (int i = 0; i < 4096; ++i) dst[i] = src[i];
    
    /* Update PTE: Set RW (clear RO), clear COW */
    uint64_t new_entry = (entry & ~PTE_COW) & ~PTE_RO;
    new_entry = (new_entry & ~0x0000FFFFFFFFF000ULL) | new_phys;
    
    *pte = new_entry;
    invlpg_page(page);
    return 1;
}

static uint64_t get_zero_page(void)
{
    static uint64_t zero_phys = 0;
    if (zero_phys) return zero_phys;
    
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    
    uint8_t *dst = (uint8_t *)phys_to_hhdm(phys);
    for (int i=0; i<4096; ++i) dst[i] = 0;
    
    zero_phys = phys;
    return zero_phys;
}

/* Need to implement mmu_map_page_in for AArch64 */
/* Simplified version that only handles L3 mapping, assumes tables exist or we allocate them */
/* We can reuse existing mmu_map_page helper logic or reimplement */
/* But 'mmu_map_page' in include/kernel/mmu.h maps to CURRENT pml4. */

static inline void *table_ptr(uint64_t phys)
{
    return (void *)phys_to_hhdm(phys);
}

static void zero_page(uint64_t phys)
{
    uint64_t *ptr = (uint64_t *)table_ptr(phys);
    for (size_t i = 0; i < 512; ++i) ptr[i] = 0;
}

static uint64_t *ensure_table(uint64_t *parent, uint16_t index, uint64_t flags)
{
    (void)flags;
    uint64_t entry = parent[index];
    uint64_t phys;
    /* flags usage: if user needed, we set user bit on table too */
    /* AArch64: Table descriptors also have effect on permissions? */
    /* Yes, PXNTable, UXNTable, APTable. Default 0 is permissive. */
    
    if (!(entry & PTE_VALID)) {
        phys = pmm_alloc_page();
        zero_page(phys);
        /* L0/L1/L2 table descriptor: Bit 1=1 (Table), Bit 0=1 (Valid) */
        uint64_t new_entry = phys | PTE_VALID | PTE_PAGE; 
        parent[index] = new_entry;
    } else {
        phys = entry & 0x0000FFFFFFFFF000ULL;
    }
    return (uint64_t *)table_ptr(phys);
}

int mmu_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* flags are generic MMU_FLAG_* from mmu.h */
    
    uint64_t *pml4 = (uint64_t *)table_ptr(pml4_phys);
    uint16_t l0 = (virt >> 39) & 0x1FF;
    uint16_t l1 = (virt >> 30) & 0x1FF;
    uint16_t l2 = (virt >> 21) & 0x1FF;
    uint16_t l3 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = ensure_table(pml4, l0, flags);
    /* Check L1 (1GB Block) */
    uint64_t l1e = pdpt[l1];
    if ((l1e & PTE_VALID) && !(l1e & PTE_PAGE)) {
        /* It's a block (1GB) */
        uint64_t block_phys = l1e & 0x0000FFFFFFFFF000ULL; // Mask attributes?
        /* Actually block address is 30 bits aligned */
        block_phys &= ~((1ULL << 30) - 1);
        uint64_t offset = virt & ((1ULL << 30) - 1);
        
        if ((block_phys + offset) == phys) {
            /* Already mapped correctly */
            return 0;
        } else {
            /* Mapped to something else! */
            /* TODO: Handle remap/split? Panic for now */
            // panic("MMU: L1 Block conflict", l1e);
            return -1;
        }
    }

    uint64_t *pd   = ensure_table(pdpt, l1, flags);
    
    /* Check L2 (2MB Block) */
    uint64_t l2e = pd[l2];
    if ((l2e & PTE_VALID) && !(l2e & PTE_PAGE)) {
        /* It's a block (2MB) */
        uint64_t block_phys = l2e & 0x0000FFFFFFFFF000ULL;
        block_phys &= ~((1ULL << 21) - 1);
        uint64_t offset = virt & ((1ULL << 21) - 1);
        
        if ((block_phys + offset) == phys) {
            return 0;
        } else {
            // panic("MMU: L2 Block conflict", l2e);
            return -1;
        }
    }

    uint64_t *pt   = ensure_table(pd,   l2, flags);
    
    /* L3 Entry */
    /* Convert generic flags to AArch64 PTE bits */
    /* By default use Attr 1 (Normal Memory) -> Index 1 in MAIR (Bits 4:2 = 1) -> 0x4  */
    /* If MMU_FLAG_DEVICE is set, use Attr 0 (Device) -> Index 0 in MAIR (Bits 4:2 = 0) -> 0x0 */
    
    uint64_t attr_idx = 1; /* Normal by default */
    if (flags & MMU_FLAG_DEVICE) {
        attr_idx = 0; /* Device */
    }
    
    uint64_t entry = (phys & 0x0000FFFFFFFFF000ULL) | PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER;
    entry |= (attr_idx << 2);
    
    if (flags & MMU_FLAG_USER) entry |= PTE_USER;
    // Wait, MMU_FLAG_WRITE (0x2) means Writable.
    if (!(flags & MMU_FLAG_WRITE)) entry |= PTE_RO;
    
    if (flags & MMU_FLAG_NOEXEC) entry |= PTE_NX | PTE_PXN;
    if (flags & MMU_FLAG_COW) entry |= PTE_COW;
    
    pt[l3] = entry;
    // No invlpg needed if we are modifying non-current or fresh mapping? 
    // If it's current aspace, we should invalid. mmu_map_page_in is generic.
    return 0;
}

int mmu_handle_fault(uint64_t far, int flags)
{
    /* flags: MMU_FAULT_PROTECT(1), WRITE(2), USER(4), EXEC(8) */
    uint64_t page = far & ~0xFFFULL;
    uint64_t aspace = sched_current_aspace();
    if (!aspace) return 0;
    
    /* COW Handling */
    /* If Write Fault (flags & 2) and Present (implied calls this?) */
    /* Vectors.c logic: if (esr & (1<<6)) -> Write. */
    /* If we are here, we have a fault. */
    
    /* We need to check if it's a COW fault. */
    /* AArch64 Permission Fault happens if we write to RO page. */
    if (flags & MMU_FAULT_WRITE) {
        if (handle_user_cow_fault(aspace, page)) {
            return 1;
        }
    }
    
    /* Start on Demand / Stack Growth */
    /* Check bounds */
    uint64_t stack_low = USER_STACK_TOP - (USER_STACK_MAX_PAGES * 4096ULL);
    if (page < stack_low || page >= USER_STACK_TOP) return 0; // Not in stack range (simplify)
    
    /* Check RSP (User SP) if available? 
       We don't have user SP easily here unless passed.
       vectors.c passed 'regs', but didn't pass SP. 
       Actually SP_EL0 is available if we read it? 
       But we are in EL1.
    */
    /* Simplify: Allow stack growth within range regardless of SP for now */
    
    uint64_t phys = get_zero_page();
    if (!phys) return 0;
    
    if (mmu_map_page_in(aspace, page, phys, MMU_FLAG_USER | MMU_FLAG_WRITE | MMU_FLAG_NOEXEC) != 0) {
        return 0;
    }
    invlpg_page(page);
    return 1;
}

uint64_t mmu_create_user_pml4(void)
{
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    zero_page(phys);

    /* 
       AArch64 Kernel uses TTBR1 for Higher Half. 
       User Process uses TTBR0.
       We don't need to copy kernel mappings into User PML4 (TTBR0).
       We just return an empty (zeroed) PML4.
    */
    return phys;
}

void mmu_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_phys = virt_to_phys(boot_pml4);
    mmu_map_page_in(pml4_phys, virt, phys, flags);
}

void mmu_map_hhdm_2m(uint64_t phys_start, uint64_t phys_end)
{
    uint64_t pml4_phys = virt_to_phys(boot_pml4);
    
    phys_start &= ~0x1FFFFFULL;
    phys_end = (phys_end + 0x1FFFFF) & ~0x1FFFFFULL;
    
    for (uint64_t phys = phys_start; phys < phys_end; phys += 0x200000) {
        uint64_t virt = phys_to_hhdm(phys);
        
        uint16_t l0 = (virt >> 39) & 0x1FF;
        uint16_t l1 = (virt >> 30) & 0x1FF;
        uint16_t l2 = (virt >> 21) & 0x1FF;
        
        uint64_t *pml4 = (uint64_t *)phys_to_hhdm(pml4_phys);
        uint64_t *pdpt = ensure_table(pml4, l0, MMU_FLAG_WRITE|MMU_FLAG_GLOBAL);
        
        if ((pdpt[l1] & PTE_VALID) && !(pdpt[l1] & PTE_PAGE)) {
            continue;
        }
        
        uint64_t *pd = ensure_table(pdpt, l1, MMU_FLAG_WRITE|MMU_FLAG_GLOBAL);
        
        if (pd[l2] & PTE_VALID) {
            continue;
        }
        
        /* Map 2MB Block */
        uint64_t entry = (phys & 0x0000FFFFFFFFF000ULL) | PTE_VALID | PTE_AF | PTE_SH_INNER | (1ULL << 2);
        entry |= PTE_NX | PTE_PXN;
        pd[l2] = entry;
    }
}

void mmu_unmap_page(uint64_t virt)
{
    (void)virt;
}

void mmu_protect_kernel_sections(void)
{
    /* Pending implementation */
}
