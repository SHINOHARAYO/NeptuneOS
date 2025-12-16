#pragma once

#include <stdint.h>

__attribute__((noreturn)) void panic(const char *message, uint64_t code);
