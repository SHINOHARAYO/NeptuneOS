#pragma once

#include <stdint.h>

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Relocate static GDT from gdt.s to heap-backed storage. */
void gdt_relocate_heap(void);
