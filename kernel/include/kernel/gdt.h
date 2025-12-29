#pragma once

#include <stdint.h>

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x18
#define GDT_USER_DATA 0x20
#define GDT_TSS 0x28

/* Relocate static GDT from gdt.s to heap-backed storage. */
void gdt_relocate_heap(void);
void gdt_set_kernel_stack(uint64_t rsp0);
