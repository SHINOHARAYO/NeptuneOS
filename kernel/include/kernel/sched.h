#pragma once

#include <stdint.h>

#include <arch/context.h>
#include <stddef.h>

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
uint64_t sched_current_aspace(void);
int sched_current_pid(void);
int sched_get_ppid(int pid);
void sched_set_current_exit_code(int code);
void sched_get_cwd(char *buf, size_t size);
void sched_set_cwd(const char *buf);
int sched_get_fd(int fd);
void sched_set_fd(int fd, int global_handle);
int sched_allocate_fd(int global_handle);

/* Wait queue support */
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
    char cwd[256];
    int fds[16];
};
typedef struct wait_queue {
    struct thread *head;
    struct thread *tail;
} wait_queue_t;

void wait_queue_init(wait_queue_t *wq);
void sched_sleep(wait_queue_t *wq);
void sched_sleep_cond(wait_queue_t *wq, int (*cond)(void));
void sched_wake_one(wait_queue_t *wq);
void sched_wake_all(wait_queue_t *wq);
int sched_wait_child(int parent_pid, int *out_code);

void context_switch(struct context *old_ctx, struct context *new_ctx);
void arch_thread_setup(struct thread *thread, void (*trampoline)(void));
void arch_thread_switch(struct thread *next);
void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t pml4_phys);

#define STACK_SIZE 65536
