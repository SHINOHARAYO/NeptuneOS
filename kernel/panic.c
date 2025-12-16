#include "kernel/panic.h"
#include "kernel/console.h"
#include "kernel/serial.h"

#include <stdint.h>

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

    for (;;) {
        __asm__ volatile("hlt");
    }
}
