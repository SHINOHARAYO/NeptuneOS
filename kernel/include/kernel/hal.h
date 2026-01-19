#pragma once

#include <stdint.h>
#include <stddef.h>

/* Platform Initialization */
/* Initializes platform-specific hardware (VGA, Serial, GIC, etc.) */
void arch_init_platform(void);

/* Console Abstraction */
/* Low-level character output (e.g., VGA buffer or UART) */
void arch_console_write_char(char c);
void arch_console_write(const char *msg, size_t len);
void arch_console_clear(uint8_t color);
void arch_console_set_color(uint8_t color);
void arch_console_backspace(void);

/* Memory Initialization */
/* Parses bootloader info (Multiboot2 or FDT) and calls pmm_add_region() */
void arch_mem_init(uint64_t info_phys);

/* Identity Map Management */
/* Drops the lower-half identity map (CR3 switch on x86, TLB flushes on ARM) */
void arch_drop_identity_map(void);

/* Logging Policy */
/* Returns true if log messages should be explicitly written to serial port */
/* (On some arches like AArch64, console IS serial, so return false to avoid dup) */
int arch_log_should_mirror_to_serial(void);
