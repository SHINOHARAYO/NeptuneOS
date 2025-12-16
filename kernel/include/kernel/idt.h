#pragma once

#include <stdint.h>

struct interrupt_frame;

void idt_init(void);
void dump_registers(struct interrupt_frame *frame, uint64_t cr2);
