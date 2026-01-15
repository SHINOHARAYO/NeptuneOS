#include "kernel/panic.h"
#include "kernel/console.h"
#include "kernel/io.h"
#include "kernel/serial.h"

#include <stdint.h>

static void panic_reboot(void)
{
    /* Try keyboard controller reset. */
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
    }
    outb(0x64, 0xFE);

    /* Fall back to triple fault if reset is ignored. */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idt = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(idt));
    __asm__ volatile("int3");
}

__attribute__((noreturn)) void panic(const char *message, uint64_t code)
{
    __asm__ volatile("cli");

    serial_init();

    console_set_color(0x4F); /* white on red */
    console_clear(0x4F);
    console_write("KERNEL PANIC\n");
    console_write(message);
    console_write("\nCODE: ");
    console_write_hex(code);

    serial_write("KERNEL PANIC\r\n");
    serial_write(message);
    serial_write("\r\nCODE: ");
    serial_write_hex(code);
    serial_write("\r\n");

    console_write("\nRebooting...\n");
    serial_write("\r\nRebooting...\r\n");
    panic_reboot();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
