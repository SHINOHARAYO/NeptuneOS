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
    long fd = 0; /* default stdin */
    int close_fd = 0;

    if (path) {
        fd = sys_open(path);
        if (fd < 0) {
            write_str("cat: open failed\n");
            return;
        }
        close_fd = 1;
    }

    char buf[READ_CHUNK];
    for (;;) {
        long n = sys_read(fd, buf, READ_CHUNK);
        if (n < 0) {
            write_str("cat: read error\n");
            break;
        }
        if (n == 0) {
            break;
        }
        sys_write(1, buf, n);
    }
    if (close_fd) {
        sys_close(fd);
    }
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

static void cmd_cd(const char *path) {
    if (!path) {
        write_str("cd: missing path\n");
        return;
    }
    if (sys_chdir(path) != 0) {
        write_str("cd: failed\n");
    }
}

static void cmd_pwd(void) {
    char buf[128];
    if (sys_getcwd(buf, sizeof(buf)) == 0) {
        write_str(buf);
        write_str("\n");
    } else {
        write_str("pwd: failed\n");
    }
}

static void resolve_cmd(const char *cmd, char *out) {
    if (cmd[0] == '/') {
        strcpy(out, cmd);
    } else {
        strcpy(out, "/bin/");
        char *p = out;
        while(*p) p++;
        const char *s = cmd;
        while(*s) *p++ = *s++;
        *p = '\0';
    }
}

static void cmd_spawn(char *args[], int argc, const char *const *envp)
{
    if (!args || argc == 0) {
        return;
    }
    char path_buf[BUF_LEN];
    resolve_cmd(args[0], path_buf);
    
    const char *argv[MAX_TOKENS + 1];
    for (int i = 0; i < argc && i < MAX_TOKENS; ++i) {
        argv[i] = args[i];
    }
    argv[argc] = 0;
    spawn_and_wait(path_buf, argv, envp);
}

void _start(void)
{
    static const char *envp[] = { "TERM=neptune", "USER=guest", 0 };
    char buf[BUF_LEN];

    write_str("Neptune user shell\n");

    for (;;) {
        char cwd[64];
        if (sys_getcwd(cwd, sizeof(cwd)) == 0) {
            write_str(cwd);
        }
        write_str("> ");
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

        if (strcmp(args[0], "cd") == 0) {
            cmd_cd(args[1]);
            continue;
        }
        if (strcmp(args[0], "pwd") == 0) {
            cmd_pwd();
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
        /* Check for pipe or redirect */
        int pipe_idx = -1;
        int redir_idx = -1;
        for (int i = 0; i < argc; ++i) {
            if (strcmp(args[i], "|") == 0) {
                pipe_idx = i;
                break;
            }
            if (strcmp(args[i], ">") == 0) {
                redir_idx = i;
                break;
            }
        }

        if (pipe_idx > 0) {
             /* cmd1 | cmd2 */
             /* Split args */
             args[pipe_idx] = 0;
             char **cmd1 = args;
             char **cmd2 = &args[pipe_idx + 1];
             if (!cmd2[0]) {
                 write_str("syntax error: pipe\n");
                 continue;
             }
             
             int p[2];
             if (sys_pipe(p) != 0) {
                 write_str("pipe failed\n");
                 continue;
             }
             
             int map1[3] = { -1, p[1], -1 }; /* stdout -> pipe write */
             
             char path1[BUF_LEN];
             resolve_cmd(cmd1[0], path1);
             int pid1 = sys_spawn2(path1, (const char *const*)cmd1, envp, map1);
             if (pid1 < 0) { 
                  write_str("pipe: cmd1 spawn failed\n");
                  sys_close(p[0]); sys_close(p[1]);
                  continue;
             }
             
             int map2[3] = { p[0], -1, -1 }; /* stdin -> pipe read */
             char path2[BUF_LEN];
             resolve_cmd(cmd2[0], path2);
             int pid2 = sys_spawn2(path2, (const char *const*)cmd2, envp, map2);
             
             /* Close pipe ends in parent */
             sys_close(p[0]);
             sys_close(p[1]);
             
             if (pid2 < 0) {
                 write_str("pipe: cmd2 spawn failed\n");
             }
             
             sys_wait(0); /* Wait for one */
             sys_wait(0); /* Wait for other */
             continue;
        } else if (redir_idx > 0) {
             /* cmd > file */
             args[redir_idx] = 0;
             char **cmd = args;
             char *file = args[redir_idx + 1];
             if (!file) {
                 write_str("syntax error: redirect\n");
                 continue;
             }
             
             /* Open file for writing creates/truncates? 
                sys_open currently VFS opens. 
                VFS fat open usually implies RW. 
                We lack O_CREAT/O_TRUNC flags in sys_open interface yet.
                Assuming sys_open returns a valid handle we can write to.
                If it doesn't exist, does it create? FAT driver dependent.
                Let's assume it works for existing files or we implemented create.
                (We didn't explicit implement create in open syscalls, but maybe FAT does).
                
                Actually `cmd_write` uses `sys_open`.
                If `sys_open` fails, we might need `sys_creat`? 
                Let's stick to `sys_open` and hope.
             */
             int fd = sys_open(file);
             if (fd < 0) {
                 /* Try create? No sys_creat. 
                    Trigger write to create? 
                    This is a gap. 
                    Let's assume sys_open handles it or we modify open later.
                 */
                 write_str("redirect: open failed\n");
                 continue;
             }
             
             int map[3] = { -1, fd, -1 };
             char path[BUF_LEN];
             resolve_cmd(cmd[0], path);
             int pid = sys_spawn2(path, (const char *const*)cmd, envp, map);
             
             sys_close(fd); /* Parent closes file */
             
             if (pid < 0) {
                  write_str("redirect: spawn failed\n");
             } else {
                  sys_wait(0);
             }
             continue;
        }
        
        cmd_spawn(args, argc, envp);
    }
}
