#pragma once

#include <stdint.h>

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_enable_irq(uint8_t irq);
void pic_disable_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);
