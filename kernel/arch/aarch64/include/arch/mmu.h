#pragma once
#include <stdint.h>

#define ARCH_HIGHER_HALF_BASE 0x0000000000000000ULL
#define ARCH_HHDM_BASE        0x0000000000000000ULL

static inline void arch_mmu_flush_tlb(void) {
    __asm__ volatile("tlbi vmalle1is");
}
static inline void arch_mmu_set_aspace(uint64_t phys) {
    (void)phys;
}
static inline void arch_invlpg(uint64_t virt) {
    (void)virt;
}
