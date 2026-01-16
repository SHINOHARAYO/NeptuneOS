#include "kernel/sched.h"
#include "kernel/heap.h"
#include "kernel/idt.h"
#include "kernel/log.h"
#include "kernel/mmu.h"
#include "kernel/spinlock.h"
#include "kernel/heap.h"
#include <stddef.h>
#include <stdint.h>

#define STACK_SIZE 16384

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD,
};

struct thread {
    struct thread *next;
    struct thread *prev;
    struct thread *wait_next;
    struct context ctx;
    void (*entry)(void *);
    void *arg;
    uint8_t *stack;
    enum thread_state state;
    uint64_t aspace;
    uint8_t exit_to_kernel;
    int pid;
    int ppid;
    int exit_code;
    uint8_t is_user;
    uint8_t reaped;
};

static struct thread *threads_head = NULL;
static struct thread *threads_tail = NULL;
static size_t thread_count = 0;
// current_thread is already defined below but we need valid pointer or NULL
static struct thread *current_thread = NULL;
static volatile uint64_t sched_ticks = 0;
static volatile uint8_t need_resched = 0;
static uint64_t last_switch_tick = 0;
static uint64_t time_slice_ticks = 5;
static int sched_ready = 0;
volatile uint8_t sched_preempt_pending = 0;
uint64_t sched_preempt_target = 0;
static int next_pid = 1;
static spinlock_t sched_lock;

static void list_append(struct thread *t)
{
    if (!t) return;
    t->next = NULL;
    t->prev = threads_tail;
    if (threads_tail) {
        threads_tail->next = t;
    } else {
        threads_head = t;
    }
    threads_tail = t;
    thread_count++;
}

static void list_remove(struct thread *t)
{
    if (!t) return;
    if (t->prev) {
        t->prev->next = t->next;
    } else {
        threads_head = t->next;
    }
    if (t->next) {
        t->next->prev = t->prev;
    } else {
        threads_tail = t->prev;
    }
    thread_count--;
    t->next = NULL;
    t->prev = NULL;
}

static struct thread *thread_alloc(void)
{
    // Ensure we can lock heap
    // Caller holds sched_lock.
    struct thread *t = (struct thread *)kalloc_zero(sizeof(struct thread), 16);
    if (t) {
        list_append(t);
    }
    return t;
}

static void sched_exit(void);

static void thread_trampoline(void)
{
    spinlock_release_irqrestore(&sched_lock);
    struct thread *thread = current_thread;
    if (thread && thread->entry) {
        thread->entry(thread->arg);
    }
    sched_exit();
}

static inline void load_cr3(uint64_t phys)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static void sched_resched_locked(void);

void sched_init(void)
{
    thread_count = 0;
    threads_head = NULL;
    threads_tail = NULL;
    /* Create a dummy thread struct for the bootstrap "idle" thread (kernel_main) */
    struct thread *boot = thread_alloc();
    if (!boot) {
        return;
    }
    boot->state = THREAD_RUNNING;
    boot->reaped = 1; /* Dummy, don't care */
    current_thread = boot;
    
    sched_ticks = 0;
    need_resched = 0;
    last_switch_tick = 0;
    sched_ready = 1;
}

int sched_create(void (*entry)(void *), void *arg)
{
    if (!entry) {
        return -1;
    }

    spinlock_acquire_irqsave(&sched_lock);
    struct thread *thread = thread_alloc();
    if (!thread) {
        spinlock_release_irqrestore(&sched_lock);
        return -1;
    }
    // thread is already zeroed by kalloc_zero and appended to list
    thread->entry = entry;
    thread->arg = arg;
    thread->state = THREAD_RUNNABLE;
    thread->aspace = 0;
    thread->exit_to_kernel = 0;
    thread->pid = 0;
    thread->ppid = 0;
    thread->exit_code = 0;
    thread->is_user = 0;
    thread->reaped = 1;

    thread->stack = (uint8_t *)kalloc_zero(STACK_SIZE, 16);
    if (!thread->stack) {
        log_error("sched_create: stack alloc failed");
        list_remove(thread);
        kfree(thread);
        spinlock_release_irqrestore(&sched_lock);
        return -1;
    }

    uint64_t stack_top = (uint64_t)thread->stack + STACK_SIZE;
    stack_top = (stack_top & ~0xFULL) - 8;
    *(uint64_t *)stack_top = 0;
    thread->ctx.rsp = stack_top;
    thread->ctx.rip = (uint64_t)thread_trampoline;
    thread->ctx.rflags = 0x2; /* IF=0, MUST enable in trampoline release */

    spinlock_release_irqrestore(&sched_lock);
    return 0;
}

int sched_create_user(void (*entry)(void *), void *arg, int parent_pid, int *out_pid)
{
    if (!entry) {
        return -1;
    }

    spinlock_acquire_irqsave(&sched_lock);
    struct thread *thread = thread_alloc();
    if (!thread) {
        spinlock_release_irqrestore(&sched_lock);
        return -1;
    }
    // thread is already appended
    thread->entry = entry;
    thread->arg = arg;
    thread->state = THREAD_RUNNABLE;
    thread->aspace = 0;
    thread->exit_to_kernel = 0;
    thread->pid = next_pid++;
    thread->ppid = parent_pid;
    thread->exit_code = 0;
    thread->is_user = 1;
    thread->reaped = 0;

    thread->stack = (uint8_t *)kalloc_zero(STACK_SIZE, 16);
    if (!thread->stack) {
        log_error("sched_create_user: stack alloc failed");
        list_remove(thread);
        kfree(thread);
        spinlock_release_irqrestore(&sched_lock);
        return -1;
    }

    uint64_t stack_top = (uint64_t)thread->stack + STACK_SIZE;
    stack_top = (stack_top & ~0xFULL) - 8;
    *(uint64_t *)stack_top = 0;
    thread->ctx.rsp = stack_top;
    thread->ctx.rip = (uint64_t)thread_trampoline;
    thread->ctx.rflags = 0x2; /* IF=0 */

    if (out_pid) {
        *out_pid = thread->pid;
    }
    spinlock_release_irqrestore(&sched_lock);
    return 0;
}

static void sched_resched_locked(void)
{
    if (!sched_ready || !current_thread) {
        return;
    }

    /* Find next runnable thread */
    struct thread *next_thread = NULL;
    struct thread *start = current_thread->next;
    if (!start) {
        start = threads_head;
    }
    
    struct thread *t = start;
    if (t) {
        do {
            if (t->state == THREAD_RUNNABLE) {
                next_thread = t;
                break;
            }
            t = t->next;
            if (!t) {
                t = threads_head;
            }
        } while (t != start);
    }
    
    if (!next_thread) {
        if (current_thread->state == THREAD_RUNNING) {
            return;
        }
        if (current_thread->state == THREAD_DEAD) {
             spinlock_release_irqrestore(&sched_lock);
             for (;;) __asm__ volatile("hlt");
        }
        return;
    }

    struct thread *prev = current_thread;
    if (next_thread == prev) {
         return; 
    }

    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_RUNNABLE;
    }
    next_thread->state = THREAD_RUNNING;
    
    current_thread = next_thread;
    last_switch_tick = sched_ticks;
    need_resched = 0;
    
    if (next_thread->aspace) {
        load_cr3(next_thread->aspace);
    } else {
        mmu_reload_cr3();
    }
    context_switch(&prev->ctx, &next_thread->ctx);
}

void sched_yield(void)
{
    spinlock_acquire_irqsave(&sched_lock);
    sched_resched_locked();
    spinlock_release_irqrestore(&sched_lock);
}

void sched_start(void)
{
    sched_exit();
}

static void sched_exit(void)
{
    spinlock_acquire_irqsave(&sched_lock);
    if (current_thread) {
        current_thread->state = THREAD_DEAD;
    }
    sched_resched_locked();
    /* Should not return if we successfully switched away from a DEAD thread */
    /* If we returned, it means no other thread was found.
       We release lock and loop? */
    spinlock_release_irqrestore(&sched_lock);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

__attribute__((noreturn)) void sched_exit_current(void)
{
    sched_exit();
    __builtin_unreachable();
}

void sched_set_current_aspace(uint64_t pml4_phys)
{
    if (current_thread) {
        current_thread->aspace = pml4_phys;
    }
}

void sched_set_current_exit_to_kernel(int enable)
{
    if (current_thread) {
        current_thread->exit_to_kernel = enable ? 1 : 0;
    }
}

int sched_current_exit_to_kernel(void)
{
    if (!current_thread) {
        return 0;
    }
    return current_thread->exit_to_kernel ? 1 : 0;
}

uint64_t sched_current_aspace(void)
{
    if (!current_thread) {
        return 0;
    }
    return current_thread->aspace;
}

int sched_current_pid(void)
{
    if (!current_thread) {
        return 0;
    }
    return current_thread->pid;
}

void sched_set_current_exit_code(int code)
{
    if (current_thread) {
        current_thread->exit_code = code;
    }
}

int sched_wait_child(int parent_pid, int *out_code)
{
    if (parent_pid < 0) {
        return -1;
    }
    for (;;) {
        int has_child = 0;
        spinlock_acquire_irqsave(&sched_lock);
        
        struct thread *t = threads_head;
        while (t) {
            struct thread *next = t->next; // save next in case we remove
            if (t->is_user && t->ppid == parent_pid) {
                has_child = 1;
                if (t->state == THREAD_DEAD && !t->reaped) {
                    t->reaped = 1;
                    int pid = t->pid;
                    if (out_code) {
                        *out_code = t->exit_code;
                    }
                    /* Remove and free */
                    list_remove(t);
                    if (t->stack) {
                       kfree(t->stack);
                    }
                    kfree(t);
                    spinlock_release_irqrestore(&sched_lock);
                    return pid;
                }
            }
            t = next;
        }
        
        spinlock_release_irqrestore(&sched_lock);

        if (!has_child) {
            return -1;
        }
        sched_yield();
    }
}

void sched_kill_user_threads(void)
{
    spinlock_acquire_irqsave(&sched_lock);
    struct thread *t = threads_head;
    while (t) {
        if (t->aspace) {
            t->state = THREAD_DEAD;
            t->aspace = 0;
            t->reaped = 1;
        }
        t = t->next;
    }
    spinlock_release_irqrestore(&sched_lock);
}

void sched_on_tick(void)
{
    sched_ticks++;
    if (!sched_ready) {
        return;
    }
    if (sched_ticks - last_switch_tick >= time_slice_ticks) {
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

int sched_request_preempt(struct interrupt_frame *frame)
{
    if (!sched_ready || !need_resched || !frame) {
        return 0;
    }
    if ((frame->cs & 0x3) != 0) {
        return 0;
    }
    if (current_thread && current_thread->aspace) {
        return 0;
    }
    if (sched_preempt_pending) {
        return 0;
    }
    sched_preempt_pending = 1;
    sched_preempt_target = frame->rip;
    frame->rip = (uint64_t)sched_preempt_trampoline;
    return 1;
}

void wait_queue_init(wait_queue_t *wq)
{
    if (wq) {
        wq->head = NULL;
        wq->tail = NULL;
    }
}

void sched_sleep(wait_queue_t *wq)
{
    if (!wq) return;
    
    spinlock_acquire_irqsave(&sched_lock);
    if (current_thread) {
        current_thread->state = THREAD_BLOCKED;
        current_thread->wait_next = NULL;
        
        if (wq->tail) {
            wq->tail->wait_next = current_thread;
        } else {
            wq->head = current_thread;
        }
        wq->tail = current_thread;
        
        sched_resched_locked();
    }
    spinlock_release_irqrestore(&sched_lock);
}

void sched_sleep_cond(wait_queue_t *wq, int (*cond)(void))
{
    if (!wq) return;
    
    spinlock_acquire_irqsave(&sched_lock);
    
    if (cond && cond()) {
        spinlock_release_irqrestore(&sched_lock);
        return;
    }

    if (current_thread) {
        current_thread->state = THREAD_BLOCKED;
        current_thread->wait_next = NULL;
        
        if (wq->tail) {
            wq->tail->wait_next = current_thread;
        } else {
            wq->head = current_thread;
        }
        wq->tail = current_thread;
        
        sched_resched_locked();
    }
    spinlock_release_irqrestore(&sched_lock);
}

void sched_wake_one(wait_queue_t *wq)
{
    if (!wq) return;
    
    spinlock_acquire_irqsave(&sched_lock);
    struct thread *t = wq->head;
    if (t) {
        wq->head = t->wait_next;
        if (!wq->head) {
            wq->tail = NULL;
        }
        t->wait_next = NULL;
        t->state = THREAD_RUNNABLE;
    }
    spinlock_release_irqrestore(&sched_lock);
}

void sched_wake_all(wait_queue_t *wq)
{
    if (!wq) return;
    
    spinlock_acquire_irqsave(&sched_lock);
    struct thread *t = wq->head;
    while (t) {
        struct thread *next = t->wait_next;
        t->wait_next = NULL;
        t->state = THREAD_RUNNABLE;
        t = next;
    }
    wq->head = NULL;
    wq->tail = NULL;
    spinlock_release_irqrestore(&sched_lock);
}
