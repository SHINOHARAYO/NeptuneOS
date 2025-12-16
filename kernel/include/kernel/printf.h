#pragma once

#include <stdint.h>
#include <stdarg.h>

/* Minimal printf-like helpers for kernel debugging. Supports %s, %c, %d, %u, %x/%X, %p. */
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list args);
