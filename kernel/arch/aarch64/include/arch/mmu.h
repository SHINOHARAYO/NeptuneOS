#pragma once

#include <stdint.h>

#define ARCH_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define ARCH_HHDM_BASE        0xFFFF800000000000ULL

/* Page Table Entry Bits */
#define ARCH_PTE_VALID      (1ULL << 0)
#define ARCH_PTE_TABLE      (1ULL << 1) /* For L0-L2 */
#define ARCH_PTE_PAGE       (1ULL << 1) /* For L3 */

/* MAIR Attributes (Matches start.s: 0=Device, 1=Normal) */
#define ARCH_PTE_ATTR_DEVICE (0ULL << 2)
#define ARCH_PTE_ATTR_NORMAL (1ULL << 2)

/* Access Permissions (AP[2:1]) */
#define ARCH_PTE_AP_RW_EL1  (0ULL << 6)
#define ARCH_PTE_AP_RW_USER (1ULL << 6)
#define ARCH_PTE_AP_RO_EL1  (2ULL << 6)
#define ARCH_PTE_AP_RO_USER (3ULL << 6)

/* Shareability (SH) */
#define ARCH_PTE_SH_NONE    (0ULL << 8)
#define ARCH_PTE_SH_OUTER   (2ULL << 8)
#define ARCH_PTE_SH_INNER   (3ULL << 8)

/* Access Flag */
#define ARCH_PTE_AF         (1ULL << 10)

/* Not Global (ASID specific) */
#define ARCH_PTE_NG         (1ULL << 11)

/* Execute Never */
#define ARCH_PTE_PXN        (1ULL << 53) /* Privileged XN */
#define ARCH_PTE_UXN        (1ULL << 54) /* User XN */

/* Mapping to Common MMU Flags */
#define ARCH_PTE_USER       (1ULL << 6) 
#define ARCH_PTE_PRESENT    ARCH_PTE_VALID

/* Helpers */
#define ARCH_PTE_IS_HUGE(x) ( ((x) & 1) && !((x) & 2) )

static inline void arch_mmu_flush_tlb(void)
{
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

static inline void arch_mmu_set_aspace(uint64_t phys)
{
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(phys) : "memory");
    __asm__ volatile("tlbi vmalle1is"); 
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

static inline void arch_invlpg(uint64_t virt)
{
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> 12) : "memory");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}
#define ARCH_PHYS_BASE 0x40000000ULL
