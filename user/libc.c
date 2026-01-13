#include "libc.h"

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

int strcmp(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : (a ? 1 : -1);
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    if (!dst || !src) {
        return dst;
    }
    char *out = dst;
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return out;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    if (!dst || !src || len == 0) {
        return dst;
    }
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int value, size_t len)
{
    if (!dst || len == 0) {
        return dst;
    }
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < len; ++i) {
        d[i] = (unsigned char)value;
    }
    return dst;
}
