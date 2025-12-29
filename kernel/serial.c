#include "kernel/serial.h"

#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

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
    while (!tx_empty()) {
        /* busy wait */
    }
    outb(COM1_PORT, (uint8_t)c);
}

void serial_write(const char *msg)
{
    for (size_t i = 0; msg[i] != '\0'; ++i) {
        if (msg[i] == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(msg[i]);
    }
}

void serial_write_len(const char *msg, uint64_t len)
{
    if (!msg || len == 0) {
        return;
    }
    for (uint64_t i = 0; i < len; ++i) {
        if (msg[i] == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(msg[i]);
    }
}

static char hex_digit(uint8_t value)
{
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void serial_write_hex(uint64_t value)
{
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (value >> shift) & 0xF;
        serial_write_char(hex_digit(nibble));
    }
}
