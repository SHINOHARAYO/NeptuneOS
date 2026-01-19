#include <stdint.h>
#include "libc.h"

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

__attribute__((used)) static void start_main(uint64_t *sp)
{
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

__attribute__((naked)) void _start(void)
{
#ifdef __aarch64__
    __asm__ volatile(
        "mov x0, sp\n"
        "bl start_main\n"
        "b .\n");
#else
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "andq $-16, %rsp\n"
        "call start_main\n"
        "hlt\n");
#endif
}
