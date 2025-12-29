#pragma once

#include <stdint.h>

struct interrupt_frame;

enum {
    SYSCALL_EXIT = 1,
    SYSCALL_YIELD = 2,
    SYSCALL_READ = 3,
    SYSCALL_WRITE = 4,
};

struct syscall_regs {
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
};

uint64_t syscall_handle(struct syscall_regs *regs, struct interrupt_frame *frame);
