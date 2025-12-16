#pragma once

#include <stdint.h>

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *msg);
void serial_write_hex(uint64_t value);
