#pragma once

#include <stddef.h>

#include "syscall.h"

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);
