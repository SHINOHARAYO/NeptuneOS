#include "kernel/gdt.h"
#include "kernel/heap.h"
#include "kernel/log.h"

#include <stdint.h>
#include <stddef.h>

/* Minimal 64-bit TSS with kernel stack (rsp0) and IST slots. */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed, aligned(16)));

static struct tss64 tss_kernel;

static inline void lgdt(const struct gdt_descriptor *desc)
{
    __asm__ volatile("lgdt %0" : : "m"(*desc));
}

static inline void ltr(uint16_t selector)
{
    __asm__ volatile("ltr %0" : : "r"(selector));
}

void gdt_relocate_heap(void)
{
    /* Build a fresh GDT with code, data, and TSS entries on the heap. */
    const size_t entries = 7; /* null, kcode, kdata, ucode, udata, tss low, tss high */
    const size_t gdt_bytes = entries * sizeof(uint64_t);
    struct gdt_descriptor new_desc;
    uint64_t *new_table = (uint64_t *)kalloc_zero(gdt_bytes, 16);
    if (!new_table) {
        log_error("Failed to allocate heap-backed GDT");
        return;
    }

    /* Base descriptors (match gdt.s) */
    new_table[0] = 0x0000000000000000ULL;         /* null */
    new_table[1] = 0x00af9a000000ffffULL;         /* code */
    new_table[2] = 0x00af92000000ffffULL;         /* data */
    new_table[3] = 0x00aff2000000ffffULL;         /* user data (0x18) for sysret SS */
    new_table[4] = 0x00affa000000ffffULL;         /* user code (0x20) for sysret CS */

    /* Set up TSS for kernel stack */
    uint64_t stack_ptr;
    __asm__ volatile("mov %%rsp, %0" : "=r"(stack_ptr));
    tss_kernel.rsp0 = stack_ptr;
    tss_kernel.iomap_base = sizeof(struct tss64);
    /* allocate dedicated IST stacks for IRQs */
    const size_t ist_stack_size = 4096;
    uint8_t *ist1 = (uint8_t *)kalloc_zero(ist_stack_size, 16);
    uint8_t *ist2 = (uint8_t *)kalloc_zero(ist_stack_size, 16);
    if (!ist1 || !ist2) {
        log_error("Failed to allocate IST stacks");
    } else {
        tss_kernel.ist1 = (uint64_t)(ist1 + ist_stack_size);
        tss_kernel.ist2 = (uint64_t)(ist2 + ist_stack_size);
    }

    uint64_t tss_base = (uint64_t)&tss_kernel;
    uint32_t tss_limit = (uint32_t)(sizeof(struct tss64) - 1);
    uint64_t tss_low = 0;
    tss_low |= (tss_limit & 0xFFFFULL);
    tss_low |= (tss_base & 0xFFFFFFULL) << 16;
    tss_low |= (0x89ULL) << 40; /* present, type 64-bit TSS (available) */
    tss_low |= ((uint64_t)(tss_limit >> 16) & 0xF) << 48;
    tss_low |= ((tss_base >> 24) & 0xFFULL) << 56;
    uint64_t tss_high = tss_base >> 32;

    new_table[5] = tss_low;
    new_table[6] = tss_high;

    new_desc.limit = (uint16_t)(gdt_bytes - 1);
    new_desc.base = (uint64_t)new_table;
    lgdt(&new_desc);
    ltr(GDT_TSS); /* TSS selector */
    log_info("GDT relocated to heap");
}

void gdt_set_kernel_stack(uint64_t rsp0)
{
    tss_kernel.rsp0 = rsp0;
}
