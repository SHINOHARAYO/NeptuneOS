#pragma once

#include <stdint.h>

#define HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define HHDM_BASE 0xFFFF800000000000ULL

#define MMU_FLAG_WRITE 0x2ULL
#define MMU_FLAG_USER  0x4ULL
#define MMU_FLAG_NOEXEC (1ULL << 63)
#define MMU_FLAG_GLOBAL (1ULL << 8)
#define MMU_FLAG_COW (1ULL << 9)

static inline uint64_t phys_to_higher_half(uint64_t phys)
{
    return phys + HIGHER_HALF_BASE;
}

static inline uint64_t higher_half_to_phys(uint64_t virt)
{
    return virt - HIGHER_HALF_BASE;
}

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)phys_to_higher_half(phys);
}

static inline uint64_t virt_to_phys(const void *virt)
{
    return higher_half_to_phys((uint64_t)virt);
}

static inline uint64_t phys_to_hhdm(uint64_t phys)
{
    return phys + HHDM_BASE;
}

static inline uint64_t hhdm_to_phys(uint64_t virt)
{
    return virt - HHDM_BASE;
}

/* Map physical range into the HHDM using 2 MiB pages. */
void mmu_map_hhdm_2m(uint64_t phys_start, uint64_t phys_end);

/* Map/unmap a single 4 KiB page. Flags use MMU_FLAG_* above (present is implied). */
void mmu_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void mmu_unmap_page(uint64_t virt);

/* Reload CR3 to flush TLB entries after page table changes. */
void mmu_reload_cr3(void);

/* Create a new PML4 with kernel mappings copied into the higher half. */
uint64_t mmu_create_user_pml4(void);

/* Apply proper permissions to kernel sections (text RX, rodata RO/NX, data/bss RW/NX). */
void mmu_protect_kernel_sections(void);

/* Map a 4 KiB page in a specific PML4 (used for user address spaces). */
int mmu_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/* Map a kernel linear address in the higher half for a given phys; allocates tables as needed. */
static inline void *mmu_kmap(uint64_t phys, uint64_t flags)
{
    void *virt = (void *)phys_to_higher_half(phys);
    mmu_map_page((uint64_t)virt, phys, flags);
    return virt;
}
