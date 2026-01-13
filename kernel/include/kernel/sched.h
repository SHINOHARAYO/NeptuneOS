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
    uint64_t rflags;
};

struct interrupt_frame;

void sched_init(void);
int sched_create(void (*entry)(void *), void *arg);
int sched_create_user(void (*entry)(void *), void *arg, int parent_pid, int *out_pid);
void sched_yield(void);
void sched_start(void);
void sched_on_tick(void);
void sched_maybe_preempt(void);
int sched_request_preempt(struct interrupt_frame *frame);
void sched_preempt_trampoline(void);
void sched_exit_current(void) __attribute__((noreturn));
void sched_set_current_aspace(uint64_t pml4_phys);
void sched_set_current_exit_to_kernel(int enable);
int sched_current_exit_to_kernel(void);
void sched_kill_user_threads(void);
uint64_t sched_current_aspace(void);
int sched_current_pid(void);
void sched_set_current_exit_code(int code);
int sched_wait_child(int parent_pid, int *out_code);

void context_switch(struct context *old_ctx, struct context *new_ctx);
