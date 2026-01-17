#pragma once

#include <stdint.h>

struct interrupt_frame;

enum {
    SYSCALL_EXIT = 1,
    SYSCALL_YIELD = 2,
    SYSCALL_READ = 3,
    SYSCALL_WRITE = 4,
    SYSCALL_OPEN = 5,
    SYSCALL_CLOSE = 6,
    SYSCALL_SPAWN = 7,
    SYSCALL_EXEC = 8,
    SYSCALL_GETPID = 9,
    SYSCALL_WAIT = 10,
};

enum syscall_error {
    SYSCALL_OK = 0,
    SYSCALL_EINVAL = 1,
    SYSCALL_ENOENT = 2,
    SYSCALL_EBADF = 3,
    SYSCALL_E2BIG = 4,
    SYSCALL_ENOMEM = 5,
    SYSCALL_EIO = 6,
};

struct syscall_regs {
#ifdef __aarch64__
    uint64_t rdi; // x0
    uint64_t rsi; // x1
    uint64_t rdx; // x2
    uint64_t r10; // x3
    uint64_t r8;  // x4
    uint64_t r9;  // x5
    uint64_t r11; // x6
    uint64_t r12; // x7
    uint64_t rax; // x8 (syscall num)
    uint64_t rbx; // x9
    // ... map others if needed, but these are args + num
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t rbp; // x29 (fp)
    uint64_t r15; // x30 (lr)
    uint64_t elr; // elr_el1
    uint64_t spsr; // spsr_el1
#else
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
#endif
};

uint64_t syscall_handle(struct syscall_regs *regs, struct interrupt_frame *frame);
void syscall_cleanup_handles_for_pid(int pid);
