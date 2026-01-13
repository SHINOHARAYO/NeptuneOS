#include <stdint.h>
#include "libc.h"

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

void _start(void)
{
    uint64_t *sp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    uint64_t argc = sp[0];
    const char **argv = (const char **)&sp[1];

    for (uint64_t i = 1; i < argc; ++i) {
        if (i > 1) {
            sys_write(1, " ", 1);
        }
        write_str(argv[i]);
    }
    sys_write(1, "\n", 1);
    sys_exit(0);
}
