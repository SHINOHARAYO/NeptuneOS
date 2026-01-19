#include "libc.h"
#include "syscall.h"

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

int strcmp(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : (a ? 1 : -1);
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    if (!dst || !src) {
        return dst;
    }
    char *out = dst;
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return out;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    if (!dst || !src || len == 0) {
        return dst;
    }
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int value, size_t len)
{
    if (!dst || len == 0) {
        return dst;
    }
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < len; ++i) {
        d[i] = (unsigned char)value;
    }
    return dst;
}

int sys_dup2(int oldfd, int newfd)
{
    return (int)syscall2(SYSCALL_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}

int sys_pipe(int pipefd[2])
{
    return (int)syscall1(SYSCALL_PIPE, (uint64_t)pipefd);
}

int sys_spawn2(const char *path, const char *const *argv, const char *const *envp, const int *fd_map)
{
    return (int)sys_spawn2_inline(path, argv, envp, fd_map);
}

int sys_chdir(const char *path)
{
    return (int)syscall1(SYSCALL_CHDIR, (uint64_t)path);
}

int sys_getcwd(char *buf, size_t size)
{
    return (int)syscall2(SYSCALL_GETCWD, (uint64_t)buf, size);
}
