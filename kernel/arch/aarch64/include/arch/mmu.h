#pragma once

#include <stdint.h>

#define ARCH_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define ARCH_HHDM_BASE        0xFFFF800000000000ULL

#define ARCH_PTE_PRESENT 0x1ULL
#define ARCH_PTE_USER    (1ULL << 6) /* AP[0]=1 means User Accessible */

/* AArch64: Block (Huge) if Bit 1 == 0 (and Valid). Table if Bit 1 == 1. */
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
