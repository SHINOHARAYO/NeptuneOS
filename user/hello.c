#include "libc.h"

void _start(void)
{
    const char msg[] = "Hello from user program\n";
    sys_write(1, msg, strlen(msg));
    sys_exit(0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
