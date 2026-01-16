#pragma once
#include <stdint.h>
#include <stdbool.h>

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
static inline void arch_reboot(void)
{
    /* TODO: PSCI reset */
    for (;;) {
        arch_halt();
    }
}
