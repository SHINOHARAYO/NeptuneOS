#include "kernel/panic.h"
#include "kernel/console.h"
#include "kernel/io.h"
#include "kernel/serial.h"
#include <arch/processor.h>

#include <stdint.h>

// panic_reboot logic moved to arch_reboot

__attribute__((noreturn)) void panic(const char *message, uint64_t code)
{
    arch_irq_disable();

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
    arch_reboot();

    for (;;) {
        arch_halt();
    }
}
