#include "kernel/serial.h"
#include "kernel/spinlock.h"

#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

static spinlock_t serial_lock;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int tx_empty(void)
{
    return inb(COM1_PORT + 5) & 0x20;
}

static int serial_write_char_unlocked(char c)
{
    for (uint32_t i = 0; i < 100000; ++i) {
        if (tx_empty()) {
            outb(COM1_PORT, (uint8_t)c);
            return 1;
        }
    }
    return 0;
}

void serial_init(void)
{
    outb(COM1_PORT + 1, 0x00);    /* disable interrupts */
    outb(COM1_PORT + 3, 0x80);    /* enable DLAB */
    outb(COM1_PORT + 0, 0x03);    /* divisor low  (38400 baud) */
    outb(COM1_PORT + 1, 0x00);    /* divisor high */
    outb(COM1_PORT + 3, 0x03);    /* 8 bits, no parity, one stop */
    outb(COM1_PORT + 2, 0xC7);    /* enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B);    /* IRQs enabled, RTS/DSR set (OUT2 enables IRQ line) */
    outb(COM1_PORT + 1, 0x01);    /* enable received data available interrupt */
}

void serial_write_char(char c)
{
    spinlock_acquire_irqsave(&serial_lock);
    (void)serial_write_char_unlocked(c);
    spinlock_release_irqrestore(&serial_lock);
}

void serial_write(const char *msg)
{
    spinlock_acquire_irqsave(&serial_lock);
    for (size_t i = 0; msg[i] != '\0'; ++i) {
        if (msg[i] == '\n') {
            serial_write_char_unlocked('\r');
        }
        serial_write_char_unlocked(msg[i]);
    }
    spinlock_release_irqrestore(&serial_lock);
}

void serial_write_len(const char *msg, uint64_t len)
{
    if (!msg || len == 0) {
        return;
    }
    spinlock_acquire_irqsave(&serial_lock);
    for (uint64_t i = 0; i < len; ++i) {
        if (msg[i] == '\n') {
            serial_write_char_unlocked('\r');
        }
        serial_write_char_unlocked(msg[i]);
    }
    spinlock_release_irqrestore(&serial_lock);
}

static char hex_digit(uint8_t value)
{
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void serial_write_hex(uint64_t value)
{
    spinlock_acquire_irqsave(&serial_lock);
    serial_write_char_unlocked('0');
    serial_write_char_unlocked('x');
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (value >> shift) & 0xF;
        serial_write_char_unlocked(hex_digit(nibble));
    }
    spinlock_release_irqrestore(&serial_lock);
}
