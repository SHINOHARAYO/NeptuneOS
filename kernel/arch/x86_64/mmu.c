#include "kernel/mmu.h"
#include "kernel/log.h"
#include "kernel/panic.h"
#include "kernel/mem.h"

#include <stdint.h>
#include <stddef.h>

extern uint64_t pml4_table[];

#define PTE_PRESENT 0x1ULL
#define PTE_RW 0x2ULL
#define PTE_USER 0x4ULL
#define PTE_PS 0x80ULL

#define PAGE_SIZE_2M (1ULL << 21)
#define HHDM_PML4_INDEX ((HHDM_BASE >> 39) & 0x1FF)

static inline void *table_ptr(uint64_t phys)
{
    return (void *)phys_to_higher_half(phys);
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
    return (uint64_t *)table_ptr((uint64_t)pml4_table);
}

static void zero_page(uint64_t phys)
{
    uint64_t *ptr = (uint64_t *)table_ptr(phys);
    for (size_t i = 0; i < 512; ++i) {
        ptr[i] = 0;
    }
}

static uint64_t *ensure_hhdm_pdpt(uint64_t **out_phys)
{
    uint64_t *pml4 = pml4_high();
    uint64_t entry = pml4[HHDM_PML4_INDEX];
    uint64_t phys;

    if (!(entry & PTE_PRESENT)) {
        phys = pmm_alloc_page();
        zero_page(phys);
        pml4[HHDM_PML4_INDEX] = phys | PTE_PRESENT | PTE_RW;
    } else {
        phys = entry & ~0xFFFULL;
    }

    if (out_phys) {
        *out_phys = (uint64_t *)phys;
    }
    return (uint64_t *)table_ptr(phys);
}

static uint64_t *ensure_hhdm_pd(uint64_t *pdpt, uint16_t pdpt_index)
{
    uint64_t entry = pdpt[pdpt_index];
    uint64_t phys;
    if (!(entry & PTE_PRESENT)) {
        phys = pmm_alloc_page();
        zero_page(phys);
        pdpt[pdpt_index] = phys | PTE_PRESENT | PTE_RW;
    } else {
        phys = entry & ~0xFFFULL;
    }
    return (uint64_t *)table_ptr(phys);
}

static uint64_t *ensure_table(uint64_t *parent, uint16_t index, uint64_t flags)
{
    uint64_t entry = parent[index];
    uint64_t phys;
    if (entry & PTE_PS) {
        panic("Cannot split huge page", entry);
    }
    if (!(entry & PTE_PRESENT)) {
        phys = pmm_alloc_page();
        zero_page(phys);
        uint64_t new_entry = phys | PTE_PRESENT | PTE_RW;
        if (flags & MMU_FLAG_USER) {
            new_entry |= PTE_USER;
        }
        parent[index] = new_entry;
    } else {
        phys = entry & ~0xFFFULL;
        if ((flags & MMU_FLAG_USER) && !(entry & PTE_USER)) {
            parent[index] |= PTE_USER;
        }
    }
    return (uint64_t *)table_ptr(phys);
}

static inline void invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

void mmu_reload_cr3(void)
{
    uint64_t phys = (uint64_t)pml4_table;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

void mmu_map_hhdm_2m(uint64_t phys_start, uint64_t phys_end)
{
    uint64_t start = align_down(phys_start, PAGE_SIZE_2M);
    uint64_t end = align_up(phys_end, PAGE_SIZE_2M);

    if (start >= end) {
        return;
    }

    log_info_hex("HHDM map begin phys", start);
    log_info_hex("HHDM map end phys", end);

    uint64_t *pdpt = ensure_hhdm_pdpt(NULL);

    for (uint64_t phys = start; phys < end; phys += PAGE_SIZE_2M) {
        uint64_t virt = phys_to_hhdm(phys);
        uint16_t pdpt_index = (virt >> 30) & 0x1FF;
        uint16_t pd_index = (virt >> 21) & 0x1FF;

        uint64_t *pd = ensure_hhdm_pd(pdpt, pdpt_index);

        uint64_t entry = align_down(phys, PAGE_SIZE_2M) | PTE_PRESENT | PTE_RW | PTE_PS;
        pd[pd_index] = entry;
    }

    mmu_reload_cr3(); /* flush after updates */
}

void mmu_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if ((virt & 0xFFF) || (phys & 0xFFF)) {
        panic("mmu_map_page: unaligned", virt | phys);
    }

    uint64_t *pml4 = pml4_high();
    uint16_t pml4_index = (virt >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt >> 30) & 0x1FF;
    uint16_t pd_index = (virt >> 21) & 0x1FF;
    uint16_t pt_index = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = ensure_table(pml4, pml4_index, flags);
    uint64_t *pd = ensure_table(pdpt, pdpt_index, flags);
    uint64_t *pt = ensure_table(pd, pd_index, flags);

    uint64_t existing = pt[pt_index];
    if (existing & PTE_PRESENT) {
        panic("mmu_map_page: already mapped", existing);
    }

    uint64_t entry = (phys & ~0xFFFULL) | PTE_PRESENT;
    if (flags & MMU_FLAG_WRITE) {
        entry |= PTE_RW;
    }
    if (flags & MMU_FLAG_USER) {
        entry |= PTE_USER;
    }
    /* NX bit intentionally ignored until EFER.NXE is enabled */

    pt[pt_index] = entry;
    invlpg(virt);
}

void mmu_unmap_page(uint64_t virt)
{
    if (virt & 0xFFF) {
        panic("mmu_unmap_page: unaligned", virt);
    }

    uint64_t *pml4 = pml4_high();
    uint16_t pml4_index = (virt >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt >> 30) & 0x1FF;
    uint16_t pd_index = (virt >> 21) & 0x1FF;
    uint16_t pt_index = (virt >> 12) & 0x1FF;

    uint64_t pml4_entry = pml4[pml4_index];
    if (!(pml4_entry & PTE_PRESENT)) {
        panic("mmu_unmap_page: missing pml4 entry", virt);
    }
    uint64_t *pdpt = (uint64_t *)table_ptr(pml4_entry & ~0xFFFULL);

    uint64_t pdpt_entry = pdpt[pdpt_index];
    if (!(pdpt_entry & PTE_PRESENT) || (pdpt_entry & PTE_PS)) {
        panic("mmu_unmap_page: missing pdpt entry", virt);
    }
    uint64_t *pd = (uint64_t *)table_ptr(pdpt_entry & ~0xFFFULL);

    uint64_t pd_entry = pd[pd_index];
    if (!(pd_entry & PTE_PRESENT) || (pd_entry & PTE_PS)) {
        panic("mmu_unmap_page: missing pd entry", virt);
    }
    uint64_t *pt = (uint64_t *)table_ptr(pd_entry & ~0xFFFULL);

    uint64_t pte = pt[pt_index];
    if (!(pte & PTE_PRESENT)) {
        panic("mmu_unmap_page: not mapped", virt);
    }

    pt[pt_index] = 0;
    invlpg(virt);
}
