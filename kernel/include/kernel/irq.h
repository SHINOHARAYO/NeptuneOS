#pragma once

#include <stdint.h>

void irq_dispatch(uint8_t irq);

#define IRQ_KEYBOARD 1
#define IRQ_SERIAL_COM1 4

int irq_kb_pop(uint8_t *scancode);
int irq_com_pop(uint8_t *ch);
