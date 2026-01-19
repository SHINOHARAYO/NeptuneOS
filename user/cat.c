#include <stdint.h>
#include "libc.h"

#define READ_CHUNK 64

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

__attribute__((used)) static void start_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    const char **argv = (const char **)&sp[1];

    if (argc < 2 || !argv[1] || argv[1][0] == '\0') {
        write_str("cat: missing path\n");
        sys_exit(1);
    }

    long fd = sys_open(argv[1]);
    if (fd < 0) {
        write_str("cat: open failed\n");
        sys_exit(1);
    }

    char buf[READ_CHUNK];
    for (;;) {
        long n = sys_read(fd, buf, READ_CHUNK);
        if (n <= 0) {
            break;
        }
        sys_write(1, buf, n);
    }
    sys_close(fd);
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
