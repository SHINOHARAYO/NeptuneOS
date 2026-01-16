#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GICD_BASE 0x08000000
#define GICC_BASE 0x08010000

#define GICD_CTLR       (GICD_BASE + 0x000)
#define GICD_ISENABLER  (GICD_BASE + 0x100)
#define GICD_ITARGETSR  (GICD_BASE + 0x800)

#define GICC_CTLR       (GICC_BASE + 0x000)
#define GICC_PMR        (GICC_BASE + 0x004)
#define GICC_IAR        (GICC_BASE + 0x00C)
#define GICC_EOIR       (GICC_BASE + 0x010)

void gic_init(void);
void gic_enable_irq(uint32_t irq);
uint32_t gic_acknowledge_irq(void);
void gic_end_irq(uint32_t irq);
