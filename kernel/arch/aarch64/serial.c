#include <kernel/console.h>
#include <stdint.h>

/* ARM64 PL011 UART Stub */
/* Base address for Virt machine is 0x09000000 */
#define UART0_BASE 0x09000000
volatile uint32_t * const UART0_DR = (uint32_t *)UART0_BASE;
volatile uint32_t * const UART0_FR = (uint32_t *)(UART0_BASE + 0x18);

void serial_init(void) {}

void serial_write(char c) {
    *UART0_DR = c;
}

void serial_write_str(const char *s) {
    while (*s) serial_write(*s++);
}

void serial_write_len(const char *s, uint64_t len) {
    for (uint64_t i = 0; i < len; ++i) serial_write(s[i]);
}

static char hex_digit(uint8_t value) {
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void serial_write_hex(uint64_t value) {
    serial_write('0'); serial_write('x');
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_write(hex_digit((value >> shift) & 0xF));
    }
}

/* Console interface shim */
void console_clear(uint8_t color) { (void)color; }
void console_write(const char *s) { serial_write_str(s); }
void console_write_char(char c) { serial_write(c); }
void console_set_color(uint8_t color) { (void)color; }
void console_backspace(void) { serial_write_str("\b \b"); }
void console_write_len(const char *s, uint64_t len) { serial_write_len(s, len); }
void console_write_hex(uint64_t n) { (void)n; } 
void console_put_char_at(char c, uint8_t color, int x, int y) { (void)c; (void)color; (void)x; (void)y; }
