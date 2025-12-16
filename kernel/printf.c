#include "kernel/printf.h"
#include "kernel/console.h"
#include "kernel/serial.h"

#include <stdbool.h>
#include <stddef.h>

static void put_char(char c)
{
    console_write((char[2]){c, '\0'});
    serial_write((char[2]){c, '\0'});
}

static void put_str(const char *s)
{
    console_write(s);
    serial_write(s);
}

static void put_uint(uint64_t value, unsigned base, bool uppercase)
{
    char buf[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value && i < sizeof(buf)) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }
    while (i > 0) {
        put_char(buf[--i]);
    }
}

static void put_int(int64_t value)
{
    uint64_t mag = (value < 0) ? (uint64_t)(-value) : (uint64_t)value;
    if (value < 0) {
        put_char('-');
    }
    put_uint(mag, 10, false);
}

void kvprintf(const char *fmt, va_list args)
{
    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            put_char(fmt[i]);
            continue;
        }
        ++i;
        char spec = fmt[i];
        switch (spec) {
        case '%':
            put_char('%');
            break;
        case 's': {
            const char *s = va_arg(args, const char *);
            put_str(s ? s : "(null)");
            break;
        }
        case 'c': {
            int c = va_arg(args, int);
            put_char((char)c);
            break;
        }
        case 'd':
        case 'i': {
            int val = va_arg(args, int);
            put_int(val);
            break;
        }
        case 'u': {
            unsigned int val = va_arg(args, unsigned int);
            put_uint(val, 10, false);
            break;
        }
        case 'x':
        case 'X': {
            unsigned int val = va_arg(args, unsigned int);
            put_uint(val, 16, spec == 'X');
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)va_arg(args, void *);
            put_str("0x");
            put_uint(val, 16, false);
            break;
        }
        default:
            put_char('%');
            put_char(spec);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
