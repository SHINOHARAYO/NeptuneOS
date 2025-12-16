#pragma once

#include <stdint.h>

void console_clear(uint8_t color);
void console_set_color(uint8_t color);
void console_write(const char *msg);
void console_write_hex(uint64_t value);
