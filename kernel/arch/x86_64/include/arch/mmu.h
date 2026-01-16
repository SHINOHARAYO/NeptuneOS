#pragma once

#include <stdint.h>

#define ARCH_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define ARCH_HHDM_BASE 0xFFFF800000000000ULL

static inline void arch_mmu_flush_tlb(void)
{
    uint64_t phys;
    __asm__ volatile("mov %%cr3, %0" : "=r"(phys));
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static inline void arch_mmu_set_aspace(uint64_t phys)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static inline void arch_invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
}
