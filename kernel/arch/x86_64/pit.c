#include "kernel/pit.h"
#include "kernel/io.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43

void pit_init(uint32_t frequency_hz)
{
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }
    uint32_t divisor = 1193182u / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }

    /* channel 0, lobyte/hibyte, mode 3 square wave, binary */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}
