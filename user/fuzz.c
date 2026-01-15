#include <stdint.h>
#include "libc.h"

#define USER_BASE 0x0000000000400000ULL
#define USER_STACK_TOP 0x0000007FFFFFF000ULL
#define ITERATIONS 200

static uint32_t lcg_state = 0xC0FFEEU;

static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

static uint64_t rand_u64(void)
{
    return ((uint64_t)lcg_next() << 32) | lcg_next();
}

static uintptr_t rand_ptr(void)
{
    if (lcg_next() & 1u) {
        return (uintptr_t)(lcg_next() & 0xFFFFu);
    }
    uint64_t span = USER_STACK_TOP - USER_BASE - 0x1000;
    if (span == 0) {
        return USER_BASE;
    }
    return (uintptr_t)(USER_BASE + (rand_u64() % span));
}

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

void _start(void)
{
    char buf[64];
    char path[] = "/dev/tty";
    for (int i = 0; i < (int)sizeof(buf); ++i) {
        buf[i] = (char)('A' + (i % 26));
    }

    for (int i = 0; i < ITERATIONS; ++i) {
        uint32_t op = lcg_next() % 7u;
        uint64_t len = (lcg_next() % 8u) + 1u;

        switch (op) {
        case 0:
            sys_write(1, buf, len);
            break;
        case 1:
            sys_write(1, (void *)rand_ptr(), len);
            break;
        case 2: {
            long fd = sys_open(path);
            if (fd >= 0) {
                sys_close(fd);
            }
            break;
        }
        case 3:
            (void)sys_open((const char *)rand_ptr());
            break;
        case 4:
            (void)sys_close(3 + (long)(lcg_next() & 0x1Fu));
            break;
        case 5:
            (void)sys_read(3 + (long)(lcg_next() & 0x1Fu), (void *)rand_ptr(), len);
            break;
        case 6:
            if ((lcg_next() & 0x1Fu) == 0) {
                static const char *argv[] = { "/bin/echo", "fuzz", 0 };
                long pid = sys_spawn("/bin/echo", argv, 0);
                if (pid >= 0) {
                    (void)sys_wait(0);
                }
            }
            break;
        }

        if ((i & 31) == 0) {
            sys_yield();
        }
    }

    write_str("fuzz: ok\n");
    sys_exit(0);
}
