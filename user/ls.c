#include <stdint.h>
#include "libc.h"

#define BUF_LEN 128
#define READ_CHUNK 64

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

__attribute__((used)) static void start_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    const char **argv = (const char **)&sp[1];

    const char *path = (argc > 1) ? argv[1] : NULL;
    char list_path[BUF_LEN];
    const char *open_path = "/dev/ls";

    if (path && path[0] != '\0') {
        if (path[0] != '/') {
            write_str("ls: absolute path required\n");
            sys_exit(1);
        }
        size_t path_len = strlen(path);
        if (path_len + 7 >= BUF_LEN) {
            write_str("ls: path too long\n");
            sys_exit(1);
        }
        list_path[0] = '/';
        list_path[1] = 'd';
        list_path[2] = 'e';
        list_path[3] = 'v';
        list_path[4] = '/';
        list_path[5] = 'l';
        list_path[6] = 's';
        for (size_t i = 0; i <= path_len; ++i) {
            list_path[7 + i] = path[i];
        }
        open_path = list_path;
    }

    long fd = sys_open(open_path);
    if (fd < 0) {
        write_str("ls: open failed\n");
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
