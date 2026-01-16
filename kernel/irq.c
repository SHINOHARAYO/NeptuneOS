#include "kernel/irq.h"
#include "kernel/log.h"
#include "kernel/io.h"

#define KB_DATA_PORT 0x60
#define COM1_PORT 0x3F8

#define KB_BUF_SIZE 64
#define COM_BUF_SIZE 128

static uint8_t kb_buf[KB_BUF_SIZE];
static uint32_t kb_head = 0, kb_tail = 0;

static uint8_t com_buf[COM_BUF_SIZE];
static uint32_t com_head = 0, com_tail = 0;

#include "kernel/sched.h"

static wait_queue_t input_wq;
static int wq_init_done = 0;

static void ensure_wq_init(void)
{
    if (!wq_init_done) {
        wait_queue_init(&input_wq);
        wq_init_done = 1;
    }
}

static void kb_push(uint8_t sc)
{
    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = sc;
        kb_head = next;
        ensure_wq_init();
        sched_wake_one(&input_wq);
    }
}

static void com_push(uint8_t ch)
{
    uint32_t next = (com_head + 1) % COM_BUF_SIZE;
    if (next != com_tail) {
        com_buf[com_head] = ch;
        com_head = next;
        ensure_wq_init();
        sched_wake_one(&input_wq);
    }
}

void irq_dispatch(uint8_t irq)
{
    switch (irq) {
    case IRQ_KEYBOARD: {
        uint8_t sc = inb(KB_DATA_PORT);
        kb_push(sc);
        break;
    }
    case IRQ_SERIAL_COM1: {
        /* Check Line Status Register: bit 0 == data ready */
        uint8_t lsr = inb(COM1_PORT + 5);
        if (lsr & 0x01) {
            uint8_t ch = inb(COM1_PORT);
            com_push(ch);
        } else {
            /* read IIR to clear pending */
            (void)inb(COM1_PORT + 2);
        }
        break;
    }
    default:
        break;
    }
}

int irq_kb_pop(uint8_t *scancode)
{
    if (!scancode) {
        return 0;
    }
    if (kb_tail == kb_head) {
        return 0;
    }
    *scancode = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return 1;
}

int irq_com_pop(uint8_t *ch)
{
    if (!ch) {
        return 0;
    }
    if (com_tail == com_head) {
        return 0;
    }
    *ch = com_buf[com_tail];
    com_tail = (com_tail + 1) % COM_BUF_SIZE;
    return 1;
}

static int irq_has_input(void)
{
    if (kb_head != kb_tail) return 1;
    if (com_head != com_tail) return 1;
    return 0;
}

void irq_wait_input(void)
{
    ensure_wq_init();
    sched_sleep_cond(&input_wq, irq_has_input);
}
