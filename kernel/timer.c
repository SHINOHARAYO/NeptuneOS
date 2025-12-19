#include "kernel/timer.h"

#define MAX_TIMER_CALLBACKS 8

/* Very small tick-based callback list driven by the PIT IRQ. */
struct timer_cb {
    timer_callback_t cb;
    void *user;
};

static struct timer_cb callbacks[MAX_TIMER_CALLBACKS];
static uint64_t timer_ticks = 0;

void timer_on_tick(void)
{
    ++timer_ticks;
    for (int i = 0; i < MAX_TIMER_CALLBACKS; ++i) {
        if (callbacks[i].cb) {
            callbacks[i].cb(timer_ticks, callbacks[i].user);
        }
    }
}

int timer_register_callback(timer_callback_t cb, void *user)
{
    if (!cb) {
        return -1;
    }
    for (int i = 0; i < MAX_TIMER_CALLBACKS; ++i) {
        if (!callbacks[i].cb) {
            callbacks[i].cb = cb;
            callbacks[i].user = user;
            return 0;
        }
    }
    return -1;
}

uint64_t timer_get_ticks(void)
{
    return timer_ticks;
}
