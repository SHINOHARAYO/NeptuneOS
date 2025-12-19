#include "kernel/printf.h"
#include "kernel/console.h"
#include "kernel/serial.h"
#include "kernel/heap.h"

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

static size_t format_uint(uint64_t value, unsigned base, bool uppercase, char *buf, size_t buf_size)
{
    if (buf_size == 0) {
        return 0;
    }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value && i < buf_size) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }
    return i; /* length in reverse order */
}

static void put_uint_printed(uint64_t value, unsigned base, bool uppercase, char *buf, size_t buf_size)
{
    size_t len = format_uint(value, base, uppercase, buf, buf_size);
    while (len > 0) {
        put_char(buf[--len]);
    }
}

static void put_int(int64_t value, char *buf, size_t buf_size)
{
    uint64_t mag = (value < 0) ? (uint64_t)(-value) : (uint64_t)value;
    if (value < 0) {
        put_char('-');
    }
    put_uint_printed(mag, 10, false, buf, buf_size);
}

void kvprintf(const char *fmt, va_list args)
{
    /* prefer heap buffer to exercise allocator; fallback to stack */
    const size_t buf_size = 64;
    char stack_buf[buf_size];
    char *num_buf = stack_buf;
    int heap_used = 0;
    if (kheap_ready()) {
        char *tmp = (char *)kalloc_zero(buf_size, 16);
        if (tmp) {
            num_buf = tmp;
            heap_used = 1;
        }
    }

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
            put_int(val, num_buf, buf_size);
            break;
        }
        case 'u': {
            unsigned int val = va_arg(args, unsigned int);
            put_uint_printed(val, 10, false, num_buf, buf_size);
            break;
        }
        case 'x':
        case 'X': {
            unsigned int val = va_arg(args, unsigned int);
            put_uint_printed(val, 16, spec == 'X', num_buf, buf_size);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)va_arg(args, void *);
            put_str("0x");
            put_uint_printed(val, 16, false, num_buf, buf_size);
            break;
        }
        default:
            put_char('%');
            put_char(spec);
            break;
        }
    }

    if (heap_used) {
        kfree(num_buf);
    }
}

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
