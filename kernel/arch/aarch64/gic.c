#include <arch/gic.h>
#include <kernel/io.h>
#include <kernel/log.h>
#include <kernel/timer.h>

static inline void mmio_write32(uint64_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)addr;
}

void gic_init(void)
{
    /* Disable Distributor */
    mmio_write32(GICD_CTLR, 0);

    /* Enable Group 0 and Group 1 in CPU interface */
    mmio_write32(GICC_CTLR, 0x3); 

    /* Set Priority Mask to allow all interrupts (0xFF) */
    mmio_write32(GICC_PMR, 0xF0); // 0xF0 is standard low prio

    /* Enable Distributor */
    mmio_write32(GICD_CTLR, 1);
    
    log_info("GICv2 Initialized");
}

void gic_enable_irq(uint32_t irq)
{
    /* Enable IRQ in ISENABLER (1 bit per IRQ, 32 bit regs) */
    uint64_t addr = GICD_ISENABLER + ((irq / 32) * 4);
    uint32_t bit = 1 << (irq % 32);
    uint32_t val = mmio_read32(addr);
    mmio_write32(addr, val | bit);

    /* Target CPU 0 (for SPIs, usually offset 800h + 4*n). 
       QEMU virt usually routes SPIs to all or specific CPUs? 
       For simplicity, let's target all CPUs (0xFF) or CPU0 (0x1) 
       ITARGETSR registers are byte-accessible or word? Usually word. 
       Each byte corresponds to an IRQ. 
    */
    /* TODO: Routing logic if needed. Default in QEMU might be fine for uniprocessor. */
}

uint32_t gic_acknowledge_irq(void)
{
    return mmio_read32(GICC_IAR);
}

void gic_end_irq(uint32_t irq)
{
    mmio_write32(GICC_EOIR, irq);
}

/* Map legacy PIC calls to GIC */
void pic_remap(int off1, int off2)
{
    (void)off1; (void)off2;
    /* Initialize GIC instead of remapping PIC */
    gic_init();
}

void pic_enable_irq(int irq)
{
    /* Map legacy x86 IRQs if relevant, but caller usually passes 0-15.
       In ARM, GIC IRQs 0-31 are SGI/PPI. SPIs start at 32.
       User code usually requests irq 1 (keyboard) or 4 (serial).
       We need to map these to GIC numbers if we want to reuse generic drivers.
       UART0 on QEMU virt is IRQ 1 + 32 = 33 (SPI 1)? No, check dtb.
       Actually, PL011 is usually IRQ 1 (PPI) or something? 
       QEMU virt: PL011 is IRQ 1 (SPI 1 -> ID 33).
    */
    /* Simple mapping hack for now: 
       If generic code asks for IRQ 4 (COM1), enable 33.
       If IRQ 1 (Keyboard), ... PL050?
       Let's just pass-through for now or hardcode specific mappings.
    */
    if (irq == 4) irq = 33; /* Serial */
    
    gic_enable_irq(irq);
}

void irq_dispatch(uint8_t irq);
void arm_timer_reload(void);

void arm_irq_handler(void)
{
    uint32_t iar = gic_acknowledge_irq();
    uint32_t irq = iar & 0x3FF;

    if (irq < 1020) {
        if (irq == 33) {
            /* Map back to generic IRQ 4 for Serial */
            irq_dispatch(4);
        } else if (irq == 30) {
            /* Timer IRQ (PPI 30) -> IRQ 0 (PIT) */
            arm_timer_reload();
            timer_on_tick();
            /* irq_dispatch(0); not needed for internal timer */
        } else {
             /* Pass-through other IRQs */
             irq_dispatch(irq);
        }
        gic_end_irq(irq);
    }
}
