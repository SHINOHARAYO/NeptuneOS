#pragma once

#include <stdint.h>

uint64_t tty_read(char *buf, uint64_t len);
uint64_t tty_write(const char *buf, uint64_t len);
