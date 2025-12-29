#pragma once

#include <stdint.h>

void console_clear(uint8_t color);
void console_set_color(uint8_t color);
void console_write(const char *msg);
void console_write_len(const char *msg, uint64_t len);
void console_write_hex(uint64_t value);
void console_backspace(void);
