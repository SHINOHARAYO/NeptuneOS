#include "kernel/sched.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_THREADS 8
#define STACK_SIZE 16384

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_DEAD,
};

struct thread {
    struct context ctx;
    void (*entry)(void *);
    void *arg;
    uint8_t *stack;
    enum thread_state state;
};

static struct thread threads[MAX_THREADS];
static size_t thread_count = 0;
static size_t current_index = 0;
static struct thread *current_thread = NULL;
static volatile uint64_t sched_ticks = 0;
static volatile uint8_t need_resched = 0;
static uint64_t last_switch_tick = 0;
static uint64_t time_slice_ticks = 5;
static int sched_ready = 0;

static void sched_exit(void);

static void sched_exit(void)
{
    if (current_thread) {
        current_thread->state = THREAD_DEAD;
    }
    sched_yield();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void thread_trampoline(void)
{
    struct thread *thread = current_thread;
    if (thread && thread->entry) {
        thread->entry(thread->arg);
    }
    sched_exit();
}

static int find_next_runnable(size_t start)
{
    if (thread_count == 0) {
        return -1;
    }
    for (size_t offset = 1; offset <= thread_count; ++offset) {
        size_t idx = (start + offset) % thread_count;
        if (threads[idx].state == THREAD_RUNNABLE) {
            return (int)idx;
        }
    }
    return -1;
}

void sched_init(void)
{
    thread_count = 1;
    current_index = 0;
    current_thread = &threads[0];
    current_thread->state = THREAD_RUNNING;
    sched_ticks = 0;
    need_resched = 0;
    last_switch_tick = 0;
    sched_ready = 1;
}

int sched_create(void (*entry)(void *), void *arg)
{
    if (!entry || thread_count >= MAX_THREADS) {
        return -1;
    }

    struct thread *thread = &threads[thread_count];
    *thread = (struct thread){0};
    thread->entry = entry;
    thread->arg = arg;
    thread->state = THREAD_RUNNABLE;

    thread->stack = (uint8_t *)kalloc_zero(STACK_SIZE, 16);
    if (!thread->stack) {
        log_error("sched_create: stack alloc failed");
        return -1;
    }

    uint64_t stack_top = (uint64_t)thread->stack + STACK_SIZE;
    stack_top = (stack_top & ~0xFULL) - 8;
    *(uint64_t *)stack_top = 0;
    thread->ctx.rsp = stack_top;
    thread->ctx.rip = (uint64_t)thread_trampoline;

    ++thread_count;
    return 0;
}

void sched_yield(void)
{
    if (!sched_ready) {
        return;
    }
    if (!current_thread) {
        return;
    }

    int next_idx = find_next_runnable(current_index);
    if (next_idx < 0) {
        return;
    }

    struct thread *next = &threads[next_idx];
    struct thread *prev = current_thread;
    if (next == prev) {
        return;
    }

    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_RUNNABLE;
    }
    next->state = THREAD_RUNNING;
    current_index = (size_t)next_idx;
    current_thread = next;
    last_switch_tick = sched_ticks;
    need_resched = 0;
    context_switch(&prev->ctx, &next->ctx);
}

void sched_start(void)
{
    sched_exit();
}

void sched_on_tick(void)
{
    if (!sched_ready) {
        return;
    }
    ++sched_ticks;
    if ((sched_ticks - last_switch_tick) >= time_slice_ticks) {
        need_resched = 1;
    }
}

void sched_maybe_preempt(void)
{
    if (!sched_ready) {
        return;
    }
    if (need_resched) {
        sched_yield();
    }
}
