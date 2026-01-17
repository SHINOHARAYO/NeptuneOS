#pragma once

#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t arch_flags_t;

static inline void arch_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static inline void arch_halt(void)
{
    __asm__ volatile("hlt");
}

static inline void arch_irq_disable(void)
{
    __asm__ volatile("cli");
}

static inline void arch_irq_enable(void)
{
    __asm__ volatile("sti");
}

static inline arch_flags_t arch_irq_save(void)
{
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    arch_irq_disable();
    return rflags;
}

static inline void arch_irq_restore(arch_flags_t flags)
{
    // Check if IF bit (bit 9) was set in saved flags
    if (flags & 0x200) {
        arch_irq_enable();
    }
}

static inline void arch_wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

static inline uint64_t arch_rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_SFMASK 0xC0000084

#include <kernel/io.h>

static inline void arch_reboot(void)
{
    /* Try keyboard controller reset. */
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
    }
    outb(0x64, 0xFE);

    /* Fall back to triple fault if reset is ignored. */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idt = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(idt));
    __asm__ volatile("int3");
}

static inline void arch_shutdown(void)
{
    /* QEMU shutdown hack for x86 (ACPI S5 style via qemu specific port) */
    outw(0x604, 0x2000);
    for (;;) {
        arch_halt();
    }
}

static inline void arch_icode_sync(void *addr, size_t len)
{
    (void)addr;
    (void)len;
    /* x86 is coherent */
}

