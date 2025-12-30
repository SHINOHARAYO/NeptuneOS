#include <stdint.h>
#include "syscall.h"

#define BUF_LEN 128

static long str_len(const char *s)
{
    long len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

static int streq(const char *a, const char *b)
{
    long i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static void strip_newline(char *buf)
{
    long len = str_len(buf);
    while (len > 0) {
        char c = buf[len - 1];
        if (c == '\n' || c == '\r') {
            buf[len - 1] = '\0';
            --len;
        } else {
            break;
        }
    }
}

static void write_str(const char *s)
{
    sys_write(1, s, str_len(s));
}

void _start(void)
{
    static const char *envp[] = { "TERM=neptune", "USER=guest", 0 };
    char buf[BUF_LEN];

    write_str("Neptune user shell\n");

    for (;;) {
        write_str("user> ");
        long n = sys_read(0, buf, BUF_LEN - 1);
        if (n <= 0) {
            sys_yield();
            continue;
        }
        buf[n] = '\0';
        strip_newline(buf);

        if (buf[0] == '\0') {
            continue;
        }
        if (streq(buf, "help")) {
            write_str("Commands: help, hello, exec, exit\n");
            continue;
        }
        if (streq(buf, "exit")) {
            sys_exit(0);
        }
        if (streq(buf, "hello")) {
            static const char *argv[] = { "/bin/hello", 0 };
            sys_spawn("/bin/hello", argv, envp);
            continue;
        }
        if (streq(buf, "exec")) {
            static const char *argv[] = { "/bin/hello", "from-shell", 0 };
            sys_exec("/bin/hello", argv, envp);
            sys_exit(1);
        }

        write_str("Unknown command: ");
        write_str(buf);
        write_str("\n");
    }
}
