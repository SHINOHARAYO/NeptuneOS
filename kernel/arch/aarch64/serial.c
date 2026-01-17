#include <kernel/console.h>
#include <kernel/serial.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ARM64 PL011 UART Stub */
/* Base address for Virt machine is 0x09000000 */
#define UART0_BASE 0x09000000
volatile uint32_t * const UART0_DR = (uint32_t *)UART0_BASE;
volatile uint32_t * const UART0_FR = (uint32_t *)(UART0_BASE + 0x18);

static spinlock_t serial_lock;

#include <kernel/irq.h>

void serial_init(void) {
    spinlock_init(&serial_lock);
    /* Enable RX Interrupt: UARTIMSC (0x038) Bit 4 (RXIM) */
    volatile uint32_t * const UART0_IMSC = (uint32_t *)(UART0_BASE + 0x038);
    volatile uint32_t * const UART0_CR = (uint32_t *)(UART0_BASE + 0x030);
    
    *UART0_IMSC |= (1 << 4); /* Enable RX IRQ */
    
    /* Ensure UART is enabled (RXE=1, TXE=1, UAE=1). QEMU handles this but good to be safe. */
    /* Bit 9: RXE, Bit 8: TXE, Bit 0: UARTEN */
    *UART0_CR |= (1 << 9) | (1 << 8) | (1 << 0);
}

void serial_handler(void) {
    /* Read until RX FIFO empty (FR Bit 4: RXFE) */
    /* Wait, FR Bit 4 is RXFE = 1 if empty. */
    while (!(*UART0_FR & (1 << 4))) {
        uint32_t dr = *UART0_DR;
        /* Push to kernel input buffer */
        if (!(dr & 0xF00)) { /* No error bits populated */
            irq_com_push((uint8_t)(dr & 0xFF));
        }
    }
}

static void serial_check_tx(void) {
    /* Wait for TX FIFO to be not full? Bit 5 (TXFF) of FR? 
       Actually FR Bit 5 is TXFF (Transmitter FIFO Full). 
       We should wait while it is set. 
    */
    while (*UART0_FR & (1 << 5)) {
        __asm__ volatile("nop");
    }
}

void serial_write_char(char c) {
    spinlock_acquire_irqsave(&serial_lock);
    serial_check_tx();
    *UART0_DR = c;
    spinlock_release_irqrestore(&serial_lock);
}

void serial_write(const char *s) {
    spinlock_acquire_irqsave(&serial_lock);
    while (*s) {
        serial_check_tx();
        if (*s == '\n') {
            *UART0_DR = '\r';
            serial_check_tx();
        }
        *UART0_DR = *s++;
    }
    spinlock_release_irqrestore(&serial_lock);
}

void serial_write_len(const char *s, uint64_t len) {
    spinlock_acquire_irqsave(&serial_lock);
    for (uint64_t i = 0; i < len; ++i) {
        serial_check_tx();
        if (s[i] == '\n') {
            *UART0_DR = '\r';
            serial_check_tx();
        }
        *UART0_DR = s[i];
    }
    spinlock_release_irqrestore(&serial_lock);
}

static char hex_digit(uint8_t value) {
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void serial_write_hex(uint64_t value) {
    spinlock_acquire_irqsave(&serial_lock);
    serial_check_tx(); *UART0_DR = '0';
    serial_check_tx(); *UART0_DR = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_check_tx();
        *UART0_DR = hex_digit((value >> shift) & 0xF);
    }
    spinlock_release_irqrestore(&serial_lock);
}

/* Console interface shim */
/* To avoid double-printing when log.c is used, we can make console_write NO-OPs 
   if we rely on serial_write. 
   BUT, printf() uses console_write_char / console_write. 
   So we MUST implement them. The double print in log.c is a log.c issue.
*/

void console_clear(uint8_t color) { (void)color; }
void console_write(const char *s) { 
    /* We reuse the locked serial_write */
    serial_write(s);
}
void console_write_char(char c) {
    serial_write_char(c);
}
void console_set_color(uint8_t color) { (void)color; }
void console_backspace(void) { 
    serial_write("\b \b"); 
}
void console_write_len(const char *s, uint64_t len) { 
    serial_write_len(s, len); 
}
void console_write_hex(uint64_t n) { 
    serial_write_hex(n); 
} 
void console_put_char_at(char c, uint8_t color, int x, int y) { (void)c; (void)color; (void)x; (void)y; }
