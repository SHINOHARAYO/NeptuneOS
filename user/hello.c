#include <stdint.h>
#include "syscall.h"

static long str_len(const char *s)
{
    long len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

void _start(void)
{
    const char msg[] = "Hello from user program\n";
    sys_write(1, msg, str_len(msg));
    sys_exit(0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
