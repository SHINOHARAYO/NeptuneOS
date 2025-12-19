#include "kernel/idt.h"
#include "kernel/panic.h"
#include "kernel/console.h"
#include "kernel/serial.h"
#include "kernel/mmu.h"

#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t rip;
    uint16_t cs;
    uint16_t _pad0;
    uint32_t _pad1;
    uint64_t rflags;
    uint64_t rsp;
    uint16_t ss;
    uint16_t _pad2;
    uint32_t _pad3;
} __attribute__((packed));

static struct idt_entry idt[256] __attribute__((aligned(16)));

static void set_gate(uint8_t vec, uint64_t handler)
{
    const uint64_t addr = handler;
    idt[vec].offset_low = addr & 0xFFFF;
    idt[vec].selector = 0x08;
    idt[vec].ist = 0;
    idt[vec].type_attr = 0x8E;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero = 0;
}

static void idt_load(void)
{
    const struct idt_ptr ptr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)&idt[0],
    };
    __asm__ volatile("lidt %0" : : "m"(ptr));
}

static inline uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void write_both(const char *label, uint64_t value)
{
    console_write(label);
    console_write_hex(value);
    console_write("\n");
    serial_write(label);
    serial_write_hex(value);
    serial_write("\r\n");
}

static void log_page_fault_details(uint64_t cr2, uint64_t err)
{
    console_set_color(0x4F);
    console_write("PAGE FAULT @");
    console_write_hex(cr2);
    console_write(" ERR=");
    console_write_hex(err);
    console_write(" [");
    console_write((err & 1) ? "P" : "NP");
    console_write((err & (1 << 1)) ? " W" : " R");
    console_write((err & (1 << 2)) ? " U" : " S");
    if (err & (1 << 4)) {
        console_write(" IX");
    } else {
        console_write(" DATA");
    }
    if (err & (1 << 3)) {
        console_write(" RSVD");
    }
    if (err & (1 << 6)) {
        console_write(" SS");
    }
    console_write(" ]\n");

    serial_write("PAGE FAULT @");
    serial_write_hex(cr2);
    serial_write(" ERR=");
    serial_write_hex(err);
    serial_write(" [");
    serial_write((err & 1) ? "P" : "NP");
    serial_write((err & (1 << 1)) ? " W" : " R");
    serial_write((err & (1 << 2)) ? " U" : " S");
    if (err & (1 << 4)) {
        serial_write(" IX");
    } else {
        serial_write(" DATA");
    }
    if (err & (1 << 3)) {
        serial_write(" RSVD");
    }
    if (err & (1 << 6)) {
        serial_write(" SS");
    }
    serial_write(" ]\r\n");

    const uint16_t pml4_index = (cr2 >> 39) & 0x1FF;
    const uint16_t pdpt_index = (cr2 >> 30) & 0x1FF;
    const uint16_t pd_index = (cr2 >> 21) & 0x1FF;
    const uint16_t pt_index = (cr2 >> 12) & 0x1FF;

    extern uint64_t pml4_table[];
    uint64_t *pml4 = (uint64_t *)phys_to_hhdm((uint64_t)pml4_table);
    uint64_t pml4e = pml4[pml4_index];
    write_both("PML4E=", pml4e);

    if (pml4e & 1) {
        uint64_t *pdpt = (uint64_t *)phys_to_hhdm(pml4e & ~0xFFFULL);
        uint64_t pdpte = pdpt[pdpt_index];
        write_both("PDPTE=", pdpte);

        if (pdpte & 1) {
            if (pdpte & (1ULL << 7)) {
                write_both("PDPE 1G=", pdpte);
            } else {
                uint64_t *pd = (uint64_t *)phys_to_hhdm(pdpte & ~0xFFFULL);
                uint64_t pde = pd[pd_index];
                write_both("PDE=", pde);
                if (pde & 1) {
                    if (pde & (1ULL << 7)) {
                        write_both("PDE 2M=", pde);
                    } else {
                        uint64_t *pt = (uint64_t *)phys_to_hhdm(pde & ~0xFFFULL);
                        uint64_t pte = pt[pt_index];
                        write_both("PTE=", pte);
                    }
                }
            }
        }
    }
}

static void dump_regs(struct interrupt_frame *frame, uint64_t err, uint64_t cr2, uint8_t vec, uint8_t has_err)
{
    if (!frame) {
        return;
    }

    console_set_color(0x4F);
    console_write("Vector=");
    console_write_hex(vec);
    console_write(" RIP=");
    console_write_hex(frame->rip);
    console_write(" RSP=");
    console_write_hex(frame->rsp);
    console_write("\nCS=");
    console_write_hex(frame->cs);
    console_write(" RFLAGS=");
    console_write_hex(frame->rflags);
    console_write("\nSS=");
    console_write_hex(frame->ss);
    console_write("\n");

    serial_write("Vector=");
    serial_write_hex(vec);
    serial_write(" RIP=");
    serial_write_hex(frame->rip);
    serial_write(" RSP=");
    serial_write_hex(frame->rsp);
    serial_write(" CS=");
    serial_write_hex(frame->cs);
    serial_write(" RFLAGS=");
    serial_write_hex(frame->rflags);
    serial_write(" SS=");
    serial_write_hex(frame->ss);
    serial_write("\r\n");

    if (has_err) {
        console_write("ERR=");
        console_write_hex(err);
        console_write("\n");
        serial_write("ERR=");
        serial_write_hex(err);
        serial_write("\r\n");
    }

    if (vec == 14) {
        console_write("CR2=");
        console_write_hex(cr2);
        console_write("\n");
        serial_write("CR2=");
        serial_write_hex(cr2);
        serial_write("\r\n");
    }
}

static void log_exception(const char *label, uint8_t vec, uint64_t err, uint8_t has_err, uint64_t rip)
{
    console_set_color(0x4F);
    console_write("Exception: ");
    console_write(label);
    console_write(" (#");
    console_write_hex(vec);
    console_write(") RIP=");
    console_write_hex(rip);
    if (has_err) {
        console_write(" ERR=");
        console_write_hex(err);
    }
    console_write("\n");

    serial_write("Exception: ");
    serial_write(label);
    serial_write(" (#");
    serial_write_hex(vec);
    serial_write(") RIP=");
    serial_write_hex(rip);
    if (has_err) {
        serial_write(" ERR=");
        serial_write_hex(err);
    }
    serial_write("\r\n");
}

static void exception_handler(const char *label, uint8_t vec, uint64_t err, uint8_t has_err, struct interrupt_frame *frame)
{
    uint64_t code = has_err ? err : 0;
    uint64_t rip = frame ? frame->rip : 0;

    log_exception(label, vec, err, has_err, rip);

    uint64_t cr2 = (vec == 14) ? read_cr2() : 0;
    if (vec == 14) {
        log_page_fault_details(cr2, err);
    }
    dump_regs(frame, err, cr2, vec, has_err);

    panic(label, code ? code : cr2);
}

#define EXC_NOERR(name, vec) \
    __attribute__((interrupt)) static void name(struct interrupt_frame *frame) { \
        exception_handler(#name, vec, 0, 0, frame); }

#define EXC_ERR(name, vec) \
    __attribute__((interrupt)) static void name(struct interrupt_frame *frame, uint64_t err) { \
        exception_handler(#name, vec, err, 1, frame); }

EXC_NOERR(isr_divide_error, 0)
EXC_NOERR(isr_debug, 1)
EXC_NOERR(isr_nmi, 2)
EXC_NOERR(isr_breakpoint, 3)
EXC_NOERR(isr_overflow, 4)
EXC_NOERR(isr_bound_range, 5)
EXC_NOERR(isr_invalid_opcode, 6)
EXC_NOERR(isr_device_not_available, 7)
EXC_ERR(isr_double_fault, 8)
EXC_ERR(isr_invalid_tss, 10)
EXC_ERR(isr_segment_not_present, 11)
EXC_ERR(isr_stack_segment_fault, 12)
EXC_ERR(isr_general_protection, 13)
EXC_ERR(isr_page_fault, 14)
EXC_NOERR(isr_x87_fpu_error, 16)
EXC_ERR(isr_alignment_check, 17)
EXC_NOERR(isr_machine_check, 18)
EXC_NOERR(isr_simd, 19)
EXC_NOERR(isr_virtualization, 20)

EXC_NOERR(isr_default, 255)

void idt_init(void)
{
    for (uint16_t i = 0; i < 256; ++i) {
        set_gate((uint8_t)i, (uint64_t)isr_default);
    }

    set_gate(0, (uint64_t)isr_divide_error);
    set_gate(1, (uint64_t)isr_debug);
    set_gate(2, (uint64_t)isr_nmi);
    set_gate(3, (uint64_t)isr_breakpoint);
    set_gate(4, (uint64_t)isr_overflow);
    set_gate(5, (uint64_t)isr_bound_range);
    set_gate(6, (uint64_t)isr_invalid_opcode);
    set_gate(7, (uint64_t)isr_device_not_available);
    set_gate(8, (uint64_t)isr_double_fault);
    set_gate(10, (uint64_t)isr_invalid_tss);
    set_gate(11, (uint64_t)isr_segment_not_present);
    set_gate(12, (uint64_t)isr_stack_segment_fault);
    set_gate(13, (uint64_t)isr_general_protection);
    set_gate(14, (uint64_t)isr_page_fault);
    set_gate(16, (uint64_t)isr_x87_fpu_error);
    set_gate(17, (uint64_t)isr_alignment_check);
    set_gate(18, (uint64_t)isr_machine_check);
    set_gate(19, (uint64_t)isr_simd);
    set_gate(20, (uint64_t)isr_virtualization);

    idt_load();
}
