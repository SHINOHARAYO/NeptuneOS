
#include "kernel/console.h"
#include "kernel/spinlock.h"
#include "kernel/hal.h"
#include <stddef.h>

static spinlock_t console_lock;

void console_backspace(void)
{
    spinlock_acquire_irqsave(&console_lock);
    arch_console_backspace();
    spinlock_release_irqrestore(&console_lock);
}

void console_clear(uint8_t color)
{
    spinlock_acquire_irqsave(&console_lock);
    arch_console_clear(color);
    spinlock_release_irqrestore(&console_lock);
}

void console_set_color(uint8_t color)
{
    spinlock_acquire_irqsave(&console_lock);
    arch_console_set_color(color);
    spinlock_release_irqrestore(&console_lock);
}

void console_write(const char *msg)
{
    size_t len = 0;
    while (msg[len]) len++;
    
    spinlock_acquire_irqsave(&console_lock);
    arch_console_write(msg, len);
    spinlock_release_irqrestore(&console_lock);
}

void console_write_len(const char *msg, uint64_t len)
{
    if (!msg || len == 0) return;
    spinlock_acquire_irqsave(&console_lock);
    arch_console_write(msg, (size_t)len);
    spinlock_release_irqrestore(&console_lock);
}

static char hex_digit(uint8_t value)
{
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void console_write_hex(uint64_t value)
{
    char buf[18];
    buf[0] = '0';
    buf[1] = 'x';
    int pos = 2;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (value >> shift) & 0xF;
        buf[pos++] = hex_digit(nibble);
    }
    
    spinlock_acquire_irqsave(&console_lock);
    arch_console_write(buf, pos);
    spinlock_release_irqrestore(&console_lock);
}

