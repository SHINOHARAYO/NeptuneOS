#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t arch_flags_t;

static inline void arch_cpu_relax(void) { __asm__ volatile("yield"); }
static inline void arch_halt(void) { __asm__ volatile("wfi"); }
static inline void arch_irq_disable(void) { __asm__ volatile("msr daifset, #2"); }
static inline void arch_irq_enable(void) { __asm__ volatile("msr daifclr, #2"); }
static inline arch_flags_t arch_irq_save(void) { 
    uint64_t flags;
    __asm__ volatile("mrs %0, daif" : "=r"(flags));
    arch_irq_disable();
    return flags;
}
static inline void arch_irq_restore(arch_flags_t flags) {
    __asm__ volatile("msr daif, %0" : : "r"(flags));
}
static inline bool arch_irq_is_enabled(arch_flags_t flags) {
    return (flags & 0x80) == 0; /* IF bit in DAIF is bit 7 (I), 1=masked */
}
#include <arch/psci.h>

static inline void arch_reboot(void)
{
    psci_system_reset();
    for (;;) {
        arch_halt();
    }
}

static inline void arch_shutdown(void)
{
    psci_system_off();
    for (;;) {
        arch_halt();
    }
}

static inline void arch_icode_sync(void *addr, size_t len)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + len;
    /* Clean D-cache by VA to PoU */
    /* Assume min cache line 64 bytes */
    uintptr_t d_start = (uintptr_t)addr & ~63ULL;
    for (uintptr_t p = d_start; p < end; p += 64) {
        __asm__ volatile("dc cvau, %0" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish"); /* Ensure D-cache clean completes */
    
    /* Invalidate I-cache (All Inner Shareable) */
    /* Safer than ivau for VIPT caches across mappings */
    __asm__ volatile("ic ialluis");
    
    __asm__ volatile("dsb ish"); /* Ensure I-cache invalidation completes */
    __asm__ volatile("isb");     /* Context synchronization */
}
