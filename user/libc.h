#pragma once

#include <stddef.h>

#include "syscall.h"

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);

int sys_dup2(int oldfd, int newfd);
int sys_pipe(int pipefd[2]);
int sys_spawn2(const char *path, const char *const *argv, const char *const *envp, const int *fd_map);
int sys_chdir(const char *path);
int sys_getcwd(char *buf, size_t size);
