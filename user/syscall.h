#pragma once

#include <stdint.h>

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
    SYSCALL_DUP2 = 11,
    SYSCALL_PIPE = 12,
    SYSCALL_CHDIR = 13,
    SYSCALL_GETCWD = 14,
};

static inline long syscall1(long num, long a1)
{
    long ret;
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    __asm__ volatile("svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0)
        : "memory", "cc");
    ret = x0;
#else
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "rcx", "r11", "memory");
#endif
    return ret;
}

static inline long syscall2(long num, long a1, long a2)
{
    long ret;
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    __asm__ volatile("svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0), "r"(x1)
        : "memory", "cc");
    ret = x0;
#else
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
#endif
    return ret;
}
static inline long syscall3(long num, long a1, long a2, long a3)
{
    long ret;
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    __asm__ volatile("svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0), "r"(x1), "r"(x2)
        : "memory", "cc");
    ret = x0;
#else
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
#endif
    return ret;
}

static inline long syscall4(long num, long a1, long a2, long a3, long a4)
{
    long ret;
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    __asm__ volatile("svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3)
        : "memory", "cc");
    ret = x0;
#else
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
#endif
    return ret;
}

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    register long x5 __asm__("x5") = a6;
    __asm__ volatile("svc #0"
        : "=r"(x0)
        : "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory", "cc");
    ret = x0;
#else
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
#endif
    return ret;
}

static inline long sys_exit(long code)
{
    return syscall3(SYSCALL_EXIT, code, 0, 0);
}

static inline long sys_yield(void)
{
    return syscall3(SYSCALL_YIELD, 0, 0, 0);
}

static inline long sys_read(long fd, void *buf, long len)
{
    return syscall3(SYSCALL_READ, fd, (long)buf, len);
}

static inline long sys_write(long fd, const void *buf, long len)
{
    return syscall3(SYSCALL_WRITE, fd, (long)buf, len);
}

static inline long sys_open(const char *path)
{
    return syscall3(SYSCALL_OPEN, (long)path, 0, 0);
}

static inline long sys_close(long fd)
{
    return syscall3(SYSCALL_CLOSE, fd, 0, 0);
}

static inline long sys_spawn(const char *path, const char *const *argv, const char *const *envp)
{
    return syscall4(SYSCALL_SPAWN, (long)path, (long)argv, (long)envp, 0);
}

static inline long sys_spawn2_inline(const char *path, const char *const *argv, const char *const *envp, const int *fd_map)
{
    return syscall4(SYSCALL_SPAWN, (long)path, (long)argv, (long)envp, (long)fd_map);
}

static inline long sys_exec(const char *path, const char *const *argv, const char *const *envp)
{
    return syscall3(SYSCALL_EXEC, (long)path, (long)argv, (long)envp);
}

static inline long sys_getpid(void)
{
    return syscall3(SYSCALL_GETPID, 0, 0, 0);
}

static inline long sys_wait(int *status)
{
    return syscall3(SYSCALL_WAIT, (long)status, 0, 0);
}
