#pragma once

#include <stdint.h>

struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

void idt_init(void);
void idt_relocate_heap(void);
uint64_t idt_get_timer_ticks(void);
void dump_registers(struct interrupt_frame *frame, uint64_t cr2);
void idt_expect_page_fault(uint64_t addr, uint64_t resume_rip);
int idt_complete_expected_page_fault(void);
