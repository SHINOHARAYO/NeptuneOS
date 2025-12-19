#pragma once

#include <stdint.h>

typedef void (*timer_callback_t)(uint64_t ticks, void *user);

/* Called from the PIT IRQ handler to advance time and run callbacks. */
void timer_on_tick(void);

/* Register a callback invoked every tick; returns 0 on success, -1 if full. */
int timer_register_callback(timer_callback_t cb, void *user);

/* Read the global tick count. */
uint64_t timer_get_ticks(void);
