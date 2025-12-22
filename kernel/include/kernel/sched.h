#pragma once

#include <stdint.h>

struct context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t rip;
};

void sched_init(void);
int sched_create(void (*entry)(void *), void *arg);
void sched_yield(void);
void sched_start(void);
void sched_on_tick(void);
void sched_maybe_preempt(void);

void context_switch(struct context *old_ctx, struct context *new_ctx);
