#include <stdint.h>
#include "libc.h"

#define BUF_LEN 128
#define MAX_TOKENS 4
#define READ_CHUNK 64

static void strip_newline(char *buf)
{
    long len = (long)strlen(buf);
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
    sys_write(1, s, strlen(s));
}

static void spawn_and_wait(const char *path, const char *const *argv, const char *const *envp)
{
    long pid = sys_spawn(path, argv, envp);
    if (pid < 0) {
        write_str("spawn: failed\n");
        return;
    }
    (void)sys_wait(0);
}

static int tokenize(char *buf, char *out[], int max)
{
    int count = 0;
    char *p = buf;
    while (*p && count < max) {
        while (*p == ' ') {
            ++p;
        }
        if (!*p) {
            break;
        }
        out[count++] = p;
        while (*p && *p != ' ') {
            ++p;
        }
        if (*p) {
            *p = '\0';
            ++p;
        }
    }
    return count;
}

static void cmd_help(void)
{
    write_str("Commands: help, hello, exec, exit, ls [path], mkdir <path>, cat <path>, write <path> <text>, append <path> <text>, <prog>\n");
}

static void cmd_cat(const char *path)
{
    if (!path) {
        write_str("cat: missing path\n");
        return;
    }
    long fd = sys_open(path);
    if (fd < 0) {
        write_str("cat: open failed\n");
        return;
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
}

static void cmd_write(const char *path, const char *text, int append)
{
    if (!path || !text) {
        write_str("write: missing args\n");
        return;
    }
    long fd = sys_open(path);
    if (fd < 0) {
        write_str("write: open failed\n");
        return;
    }
    if (append) {
        char scratch[READ_CHUNK];
        while (sys_read(fd, scratch, READ_CHUNK) > 0) {
            /* drain to EOF */
        }
    }
    sys_write(fd, text, strlen(text));
    sys_write(fd, "\n", 1);
    sys_close(fd);
}

static void cmd_ls(const char *path)
{
    char list_path[BUF_LEN];
    const char *open_path = "/dev/ls";
    if (path && path[0] != '\0') {
        if (path[0] != '/') {
            write_str("ls: absolute path required\n");
            return;
        }
        long path_len = (long)strlen(path);
        if (path_len + 7 >= BUF_LEN) {
            write_str("ls: path too long\n");
            return;
        }
        list_path[0] = '/';
        list_path[1] = 'd';
        list_path[2] = 'e';
        list_path[3] = 'v';
        list_path[4] = '/';
        list_path[5] = 'l';
        list_path[6] = 's';
        for (long i = 0; i <= path_len; ++i) {
            list_path[7 + i] = path[i];
        }
        open_path = list_path;
    }
    long fd = sys_open(open_path);
    if (fd < 0) {
        write_str("ls: open failed\n");
        return;
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
}

static void cmd_mkdir(const char *path)
{
    if (!path) {
        write_str("mkdir: missing path\n");
        return;
    }
    if (path[0] != '/') {
        write_str("mkdir: absolute path required\n");
        return;
    }
    char full[BUF_LEN];
    long len = (long)strlen(path);
    if (len + 2 >= BUF_LEN) {
        write_str("mkdir: path too long\n");
        return;
    }
    for (long i = 0; i < len; ++i) {
        full[i] = path[i];
    }
    if (len == 0 || full[len - 1] != '/') {
        full[len++] = '/';
    }
    full[len] = '\0';
    long fd = sys_open(full);
    if (fd < 0) {
        write_str("mkdir: failed\n");
        return;
    }
    sys_close(fd);
}

static void cmd_spawn(char *args[], int argc, const char *const *envp)
{
    if (!args || argc == 0) {
        return;
    }
    char path_buf[BUF_LEN];
    const char *path = args[0];
    if (path[0] != '/') {
        long name_len = (long)strlen(path);
        if (name_len + 6 >= BUF_LEN) {
            write_str("spawn: path too long\n");
            return;
        }
        path_buf[0] = '/';
        path_buf[1] = 'b';
        path_buf[2] = 'i';
        path_buf[3] = 'n';
        path_buf[4] = '/';
        for (long i = 0; i <= name_len; ++i) {
            path_buf[5 + i] = path[i];
        }
        path = path_buf;
    }
    const char *argv[MAX_TOKENS + 1];
    for (int i = 0; i < argc && i < MAX_TOKENS; ++i) {
        argv[i] = args[i];
    }
    argv[argc] = 0;
    spawn_and_wait(path, argv, envp);
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

        char *args[MAX_TOKENS] = {0};
        int argc = tokenize(buf, args, MAX_TOKENS);
        if (argc == 0) {
            continue;
        }

        if (strcmp(args[0], "help") == 0) {
            cmd_help();
            continue;
        }
        if (strcmp(args[0], "exit") == 0) {
            sys_exit(0);
        }
        if (strcmp(args[0], "hello") == 0) {
            static const char *argv[] = { "/bin/hello", 0 };
            spawn_and_wait("/bin/hello", argv, envp);
            continue;
        }
        if (strcmp(args[0], "exec") == 0) {
            static const char *argv[] = { "/bin/hello", "from-shell", 0 };
            sys_exec("/bin/hello", argv, envp);
            sys_exit(1);
        }
        if (strcmp(args[0], "ls") == 0) {
            cmd_ls(args[1]);
            continue;
        }
        if (strcmp(args[0], "mkdir") == 0) {
            cmd_mkdir(args[1]);
            continue;
        }
        if (strcmp(args[0], "cat") == 0) {
            cmd_cat(args[1]);
            continue;
        }
        if (strcmp(args[0], "write") == 0) {
            cmd_write(args[1], args[2], 0);
            continue;
        }
        if (strcmp(args[0], "append") == 0) {
            cmd_write(args[1], args[2], 1);
            continue;
        }
        cmd_spawn(args, argc, envp);
    }
}
