#include <kernel/sched.h>
#include <kernel/idt.h>
#include <arch/processor.h>
#include <stddef.h>

/* Stubs for x86-specific calls in kernel_main */

void idt_init(void) {}
void idt_relocate_heap(void) {}
#include <kernel/timer.h>
uint64_t idt_get_timer_ticks(void) { return timer_get_ticks(); }
void idt_expect_page_fault(uint64_t addr, uint64_t rip) { (void)addr; (void)rip; }
int idt_complete_expected_page_fault(void) { return 0; }

void gdt_relocate_heap(void) {}
void gdt_set_kernel_stack(uint64_t stack) { (void)stack; }





void cpu_init(void) {}
void syscall_enable(void) {}



void pci_init(void) {}
void pci_dump(void) {}

/* ACPI stub */
void acpi_init(void) {}
void acpi_dump(void) {}




/* Memset stub */
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

/* Block/FAT stub (if needed) */
/* Block/FAT stub (if needed) */
/* Block/FAT stub (if needed) */
struct block_device *ata_init(void) { return NULL; }




