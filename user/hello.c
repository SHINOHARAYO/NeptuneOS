#include "libc.h"

void _start(void)
{
    const char msg[] = "Hello from user program\n";
    sys_write(1, msg, strlen(msg));
    sys_exit(0);
    for (;;) {
#ifdef __aarch64__
        __asm__ volatile("wfi");
#else
        __asm__ volatile("hlt");
#endif
    }
}
