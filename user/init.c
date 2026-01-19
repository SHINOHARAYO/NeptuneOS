#include "libc.h"

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

void _start(void)
{
    static const char *argv[] = { "/bin/shell", 0 };
    static const char *envp[] = { "TERM=neptune", "USER=guest", 0 };

    write_str("Neptune init\n");

    write_str("Neptune init: spawning /bin/shell...\n");

    for (;;) {
        long pid = sys_spawn("/bin/shell", argv, envp);
        if (pid < 0) {
            write_str("init: spawn failed\n");
            sys_yield();
            continue;
        }
        int status = 0;
        sys_wait(&status);
        write_str("init: shell exited\n");
    }
}
