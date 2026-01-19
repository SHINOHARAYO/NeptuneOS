#include <stdint.h>
#include "libc.h"

#define HEAP_SIZE 32768
#define MAX_ENTRIES 512

static char heap[HEAP_SIZE];
static size_t heap_top = 0;

static void *simple_alloc(size_t size)
{
    if (heap_top + size > HEAP_SIZE) {
        return NULL;
    }
    void *ptr = &heap[heap_top];
    heap_top += size;
    return ptr;
}

static void simple_reset(void)
{
    heap_top = 0;
}

static void write_str(const char *s)
{
    sys_write(1, s, strlen(s));
}

static void write_char(char c)
{
    sys_write(1, &c, 1);
}

static int startswith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void print_entry(const char *name)
{
    write_str(name);
    write_str("\n");
}

static char *strdup_heap(const char *s)
{
    size_t len = strlen(s);
    char *d = simple_alloc(len + 1);
    if (d) {
        for (size_t i = 0; i <= len; ++i) d[i] = s[i];
    }
    return d;
}

/* Simple shell sort */
static void sort_entries(const char **entries, int count)
{
    for (int gap = count / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < count; i++) {
            const char *temp = entries[i];
            int j;
            for (j = i; j >= gap && strcmp(entries[j - gap], temp) > 0; j -= gap) {
                entries[j] = entries[j - gap];
            }
            entries[j] = temp;
        }
    }
}

static void list_directory(const char *target_path, int show_hidden)
{
    char list_req[256];
    char *abs_path = simple_alloc(256);
    if (!abs_path) {
        write_str("ls: out of memory (abs_path)\n");
        return;
    }

    /* Resolve absolute path */
    if (target_path[0] == '/') {
        int i = 0;
        for (; target_path[i] && i < 255; ++i) abs_path[i] = target_path[i];
        abs_path[i] = '\0';
    } else {
        char cwd[256];
        if (sys_getcwd(cwd, 256) < 0) {
            cwd[0] = '/';
            cwd[1] = '\0';
        }
        size_t cwd_len = strlen(cwd);
        size_t tgt_len = strlen(target_path);
        
        int pos = 0;
        for (size_t k=0; k<cwd_len; ++k) abs_path[pos++] = cwd[k];
        if (pos > 0 && abs_path[pos-1] != '/') abs_path[pos++] = '/';
        for (size_t k=0; k<tgt_len; ++k) abs_path[pos++] = target_path[k];
        abs_path[pos] = '\0';
    }
    
    /* Canonicalize: remove /./, resolve .., remove trailing / */
    // Simple pass for now: if ends in /., remove .
    size_t len = strlen(abs_path);
    if (len >= 2 && abs_path[len-1] == '.' && abs_path[len-2] == '/') {
        abs_path[len-1] = '\0';
        len--;
    }
    if (len > 1 && abs_path[len-1] == '/') {
        abs_path[len-1] = '\0';
        len--;
    }
    
    /* Construct /dev/ls/<abs_path> */
    {
        const char *prefix = "/dev/ls";
        int pos = 0;
        for (int i=0; prefix[i]; ++i) list_req[pos++] = prefix[i];
        if (abs_path[0] != '/') list_req[pos++] = '/';
        for (int i=0; abs_path[i]; ++i) list_req[pos++] = abs_path[i];
        list_req[pos] = '\0';
    }

    long fd = sys_open(list_req);
    if (fd < 0) {
        write_str("ls: cannot access '");
        write_str(target_path);
        write_str("': No such file or directory\n");
        return;
    }

    /* Read all listing */
    char *buf = simple_alloc(16384);
    if (!buf) {
        write_str("ls: buffer allocation failed\n");
        sys_close(fd);
        return;
    }

    long total_read = 0;
    for (;;) {
        long n = sys_read(fd, buf + total_read, 16384 - total_read);
        if (n < 0) {
           write_str("ls: read error\n");
           break;
        }
        if (n == 0) break;
        total_read += n;
        if (total_read >= 16384) break;
    }
    sys_close(fd);
    buf[total_read] = '\0';

    /* Parse and Filter */
    const char **entries = simple_alloc(MAX_ENTRIES * sizeof(char*));
    if (!entries) {
        write_str("ls: entries allocation failed\n");
        return;
    }
    int count = 0;
    
    char *line = buf;
    char *next_line = buf;
    
    size_t prefix_len = strlen(abs_path);
    if (prefix_len > 1 && abs_path[prefix_len-1] == '/') prefix_len--;
    /* If root, prefix_len is 1 ('/') */

    while (*line && count < MAX_ENTRIES) {
        while (*next_line && *next_line != '\n') next_line++;
        int had_newline = (*next_line == '\n');
        *next_line = '\0';
        
        if (startswith(line, abs_path)) {
            int match = 0;
            const char *suffix = NULL;
            
            /* Logic: 
               abs_path="/", line="/bin" -> match. suffix="bin".
               abs_path="/bin", line="/bin/ls" -> match. suffix="ls".
            */
            
            if (abs_path[1] == '\0' && abs_path[0] == '/') {
                if (line[0] == '/') {
                    suffix = line + 1;
                    match = 1;
                }
            } else {
                /* abs_path="/bin" */
                if (line[prefix_len] == '/' || line[prefix_len] == '\0') {
                    suffix = line + prefix_len;
                    if (*suffix == '/') suffix++;
                    match = 1;
                }
            }

            if (match && suffix && *suffix != '\0') {
                char *entry_name = strdup_heap(suffix);
                if (entry_name) {
                    /* Truncate at slash */
                    for (int i=0; entry_name[i]; ++i) {
                        if (entry_name[i] == '/') {
                            entry_name[i] = '\0';
                            break;
                        }
                    }
                    
                    int exists = 0;
                    for (int k=0; k<count; ++k) {
                        if (strcmp(entries[k], entry_name) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        if (show_hidden || entry_name[0] != '.') {
                            entries[count++] = entry_name;
                        }
                    }
                }
            }
        }

        if (had_newline) {
            next_line++;
            line = next_line;
        } else {
            break;
        }
    }
    
    sort_entries(entries, count);
    
    for (int i = 0; i < count; ++i) {
        print_entry(entries[i]);
    }
}

__attribute__((used)) static void start_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    const char **argv = (const char **)&sp[1];
    
    int show_hidden = 0;
    int arg_idx = 1;

    /* Parse Flags */
    while (arg_idx < argc) {
        const char *arg = argv[arg_idx];
        if (arg[0] == '-') {
            for (int k=1; arg[k]; ++k) {
                if (arg[k] == 'a') show_hidden = 1;
                else if (arg[k] == 'l') {} /* ignore */
                else {
                    write_str("ls: invalid option\n");
                    sys_exit(1);
                }
            }
            arg_idx++;
        } else {
            break;
        }
    }

    /* List Args */
    if (arg_idx >= argc) {
        list_directory(".", show_hidden);
    } else {
        for (int i = arg_idx; i < argc; ++i) {
            if (argc - arg_idx > 1) {
                write_str(argv[i]);
                write_str(":\n");
            }
            simple_reset();
            list_directory(argv[i], show_hidden);
            if (i < argc - 1) write_str("\n");
        }
    }

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
