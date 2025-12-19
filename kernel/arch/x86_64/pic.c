#include "kernel/pic.h"
#include "kernel/io.h"
#include "kernel/log.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

void pic_remap(uint8_t offset1, uint8_t offset2)
{
    /* Save current IRQ masks so we can restore after remap. */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* start init */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    /* set offsets */
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);

    /* tell master about slave at IRQ2, and slave cascade identity */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* 8086 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* restore masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    log_info("PIC remapped");
}

void pic_enable_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t value = inb(port) & ~(1u << (irq & 7));
    outb(port, value);
}

void pic_disable_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t value = inb(port) | (1u << (irq & 7));
    outb(port, value);
}

void pic_send_eoi(uint8_t irq)
{
    /* Send EOI to slave first if the IRQ originated there. */
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}
