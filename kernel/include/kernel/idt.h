#pragma once

#include <stdint.h>

struct interrupt_frame;

void idt_init(void);
void idt_relocate_heap(void);
uint64_t idt_get_timer_ticks(void);
void dump_registers(struct interrupt_frame *frame, uint64_t cr2);
