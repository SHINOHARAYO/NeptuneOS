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
};

static inline long syscall3(long num, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
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
    return syscall3(SYSCALL_SPAWN, (long)path, (long)argv, (long)envp);
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
