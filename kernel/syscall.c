#include "kernel/syscall.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/mmu.h"
#include "kernel/sched.h"
#include "kernel/tty.h"
#include "kernel/user.h"
#include "kernel/vfs.h"

#include <stddef.h>
#include <stdint.h>

enum handle_type {
    HANDLE_FREE = 0,
    HANDLE_TTY,
    HANDLE_VFS,
};

struct handle {
    enum handle_type type;
    struct vfs_file *file;
    int owner_pid;
};

#define HANDLE_MAX 16

static struct handle handles[HANDLE_MAX];
static int handles_ready = 0;

static uint64_t syscall_error(enum syscall_error err)
{
    return (uint64_t)(-(int64_t)err);
}

static void handles_init(void)
{
    for (int i = 0; i < HANDLE_MAX; ++i) {
        handles[i].type = HANDLE_FREE;
        handles[i].file = NULL;
        handles[i].owner_pid = 0;
    }
    handles[0].type = HANDLE_TTY;
    handles[1].type = HANDLE_TTY;
    handles[2].type = HANDLE_TTY;
    handles[0].owner_pid = 0;
    handles[1].owner_pid = 0;
    handles[2].owner_pid = 0;
    handles_ready = 1;
}

static int handle_alloc(enum handle_type type, struct vfs_file *file, int owner_pid)
{
    for (int i = 0; i < HANDLE_MAX; ++i) {
        if (handles[i].type == HANDLE_FREE) {
            handles[i].type = type;
            handles[i].file = file;
            handles[i].owner_pid = owner_pid;
            return i;
        }
    }
    return -1;
}

static int handle_valid(int fd)
{
    if (fd < 0 || fd >= HANDLE_MAX || handles[fd].type == HANDLE_FREE) {
        return 0;
    }
    int pid = sched_current_pid();
    return handles[fd].owner_pid == 0 || handles[fd].owner_pid == pid;
}

void syscall_cleanup_handles_for_pid(int pid)
{
    if (pid <= 0) {
        return;
    }
    for (int i = 0; i < HANDLE_MAX; ++i) {
        if (handles[i].type == HANDLE_FREE || handles[i].owner_pid != pid) {
            continue;
        }
        if (handles[i].type == HANDLE_VFS) {
            vfs_close(handles[i].file);
        }
        handles[i].type = HANDLE_FREE;
        handles[i].file = NULL;
        handles[i].owner_pid = 0;
    }
}

static int streq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static int user_page_present(uint64_t pml4_phys, uint64_t virt)
{
    if (!pml4_phys) {
        return 0;
    }
    uint64_t *pml4 = (uint64_t *)phys_to_hhdm(pml4_phys);
    uint64_t pml4e = pml4[(virt >> 39) & 0x1FF];
    if (!(pml4e & ARCH_PTE_PRESENT) || !(pml4e & ARCH_PTE_USER)) {
        return 0;
    }

    uint64_t *pdpt = (uint64_t *)phys_to_hhdm(pml4e & ~0xFFFULL);
    uint64_t pdpte = pdpt[(virt >> 30) & 0x1FF];
    if (!(pdpte & ARCH_PTE_PRESENT) || !(pdpte & ARCH_PTE_USER)) {
        return 0;
    }
    if (ARCH_PTE_IS_HUGE(pdpte)) {
        return 1;
    }

    uint64_t *pd = (uint64_t *)phys_to_hhdm(pdpte & ~0xFFFULL);
    uint64_t pde = pd[(virt >> 21) & 0x1FF];
    if (!(pde & ARCH_PTE_PRESENT) || !(pde & ARCH_PTE_USER)) {
        return 0;
    }
    if (ARCH_PTE_IS_HUGE(pde)) {
        return 1;
    }

    uint64_t *pt = (uint64_t *)phys_to_hhdm(pde & ~0xFFFULL);
    uint64_t pte = pt[(virt >> 12) & 0x1FF];
    return (pte & ARCH_PTE_PRESENT) && (pte & ARCH_PTE_USER);
}

static int user_ptr_range(uint64_t ptr, uint64_t len)
{
    const uint64_t pml4_phys = sched_current_aspace();
    if (len == 0) {
        return 0;
    }
    if (ptr < USER_BASE) {
        return 0;
    }
    if (ptr + len < ptr) {
        return 0;
    }
    if ((ptr + len) > USER_STACK_TOP) {
        return 0;
    }

    uint64_t end = ptr + len;
    uint64_t addr = ptr;
    while (addr < end) {
        if (!user_page_present(pml4_phys, addr)) {
            return 0;
        }
        uint64_t next = (addr & ~0xFFFULL) + 0x1000;
        if (next <= addr) {
            return 0;
        }
        addr = next;
    }
    return 1;
}

static int user_str_copy(const char *user, char *dst, uint64_t dst_len)
{
    if (!user || !dst || dst_len == 0) {
        return -1;
    }
    for (uint64_t i = 0; i + 1 < dst_len; ++i) {
        uint64_t addr = (uint64_t)user + i;
        if (!user_ptr_range(addr, 1)) {
            return -1;
        }
        char c = user[i];
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }
    dst[dst_len - 1] = '\0';
    return -1;
}

static int user_vec_copy(const char *const *user_vec,
                         char storage[][USER_STR_MAX],
                         const char **out_vec,
                         uint64_t max)
{
    if (!out_vec) {
        return -1;
    }
    if (!user_vec) {
        out_vec[0] = NULL;
        return 0;
    }
    for (uint64_t i = 0; i < max; ++i) {
        uint64_t addr = (uint64_t)(user_vec + i);
        if (!user_ptr_range(addr, sizeof(uint64_t))) {
            return -1;
        }
        const char *ptr = user_vec[i];
        if (!ptr) {
            out_vec[i] = NULL;
            return 0;
        }
        if (user_str_copy(ptr, storage[i], USER_STR_MAX) != 0) {
            return -1;
        }
        out_vec[i] = storage[i];
    }
    out_vec[max] = NULL;
    return -1;
}

static int user_launch_fill(struct user_launch *launch,
                            const char *path,
                            const char *const *argv,
                            const char *const *envp)
{
    if (!launch || !path) {
        return -1;
    }
    if (user_str_copy(path, launch->path, sizeof(launch->path)) != 0) {
        return -1;
    }
    if (user_vec_copy(argv, launch->argv_storage, launch->argv, USER_ARG_MAX) != 0) {
        return -1;
    }
    if (!launch->argv[0]) {
        launch->argv[0] = launch->path;
        launch->argv[1] = NULL;
    }
    if (user_vec_copy(envp, launch->env_storage, launch->envp, USER_ENV_MAX) != 0) {
        return -1;
    }
    return 0;
}

uint64_t syscall_handle(struct syscall_regs *regs, struct interrupt_frame *frame)
{
    if (!regs) {
        return syscall_error(SYSCALL_EINVAL);
    }
    (void)frame;
    if (!handles_ready) {
        handles_init();
    }

    uint64_t num = regs->rax;
    switch (num) {
    case SYSCALL_EXIT:
        log_info("Syscall exit");
        user_exit_with_code((int)regs->rdi);
        __builtin_unreachable();
    case SYSCALL_YIELD:
        sched_yield();
        return 0;
    case SYSCALL_READ: {
        int fd = (int)regs->rdi;
        char *buf = (char *)regs->rsi;
        uint64_t len = regs->rdx;
        if (!buf || len == 0) {
            return 0;
        }
        if (!user_ptr_range((uint64_t)buf, len)) {
            return syscall_error(SYSCALL_EINVAL);
        }
        if (!handle_valid(fd)) {
            return syscall_error(SYSCALL_EBADF);
        }
        struct handle *h = &handles[fd];
        if (h->type == HANDLE_TTY) {
            return tty_read(buf, len);
        }
        if (h->type == HANDLE_VFS) {
            int64_t read = vfs_read(h->file, buf, len);
            if (read < 0) {
                return syscall_error((enum syscall_error)(-read));
            }
            return (uint64_t)read;
        }
        return syscall_error(SYSCALL_EBADF);
    }
    case SYSCALL_WRITE: {
        int fd = (int)regs->rdi;
        const char *buf = (const char *)regs->rsi;
        uint64_t len = regs->rdx;
        log_info("sys_write called");   /* DEBUG */
        if (!buf || len == 0) {
            return 0;
        }
        if (!user_ptr_range((uint64_t)buf, len)) {
            log_error("sys_write: invalid pointer"); /* DEBUG */
            return syscall_error(SYSCALL_EINVAL);
        }
        if (!handle_valid(fd)) {
             log_error("sys_write: invalid handle"); /* DEBUG */
            return syscall_error(SYSCALL_EBADF);
        }
        struct handle *h = &handles[fd];
        if (h->type == HANDLE_TTY) {
            log_info("sys_write: tty"); /* DEBUG */
            return tty_write(buf, len);
        }
        if (h->type == HANDLE_VFS) {
            int64_t wrote = vfs_write(h->file, buf, len);
            if (wrote < 0) {
                return syscall_error((enum syscall_error)(-wrote));
            }
            return (uint64_t)wrote;
        }
        return syscall_error(SYSCALL_EBADF);
    }
    case SYSCALL_OPEN: {
        const char *path = (const char *)regs->rdi;
        char path_buf[USER_PATH_MAX];
        if (!path) {
            return syscall_error(SYSCALL_EINVAL);
        }
        if (user_str_copy(path, path_buf, sizeof(path_buf)) != 0) {
            return syscall_error(SYSCALL_EINVAL);
        }
        if (streq(path_buf, "/dev/tty") || streq(path_buf, "/dev/console")) {
            int fd = handle_alloc(HANDLE_TTY, NULL, sched_current_pid());
            if (fd < 0) {
                return syscall_error(SYSCALL_ENOMEM);
            }
            return (uint64_t)fd;
        }
        struct vfs_file *file = NULL;
        int vfs_err = vfs_open(path_buf, &file);
        if (vfs_err != SYSCALL_OK) {
            return syscall_error((enum syscall_error)vfs_err);
        }
        int fd = handle_alloc(HANDLE_VFS, file, sched_current_pid());
        if (fd < 0) {
            vfs_close(file);
            return syscall_error(SYSCALL_ENOMEM);
        }
        return (uint64_t)fd;
    }
    case SYSCALL_CLOSE: {
        int fd = (int)regs->rdi;
        if (!handle_valid(fd)) {
            return syscall_error(SYSCALL_EBADF);
        }
        if (handles[fd].type == HANDLE_VFS) {
            vfs_close(handles[fd].file);
        }
        handles[fd].type = HANDLE_FREE;
        handles[fd].file = NULL;
        return 0;
    }
    case SYSCALL_SPAWN: {
        const char *path = (const char *)regs->rdi;
        const char *const *argv = (const char *const *)regs->rsi;
        const char *const *envp = (const char *const *)regs->rdx;
        struct user_launch *launch = (struct user_launch *)kalloc_zero(sizeof(*launch), 16);
        if (!launch) {
            return syscall_error(SYSCALL_ENOMEM);
        }
        if (user_launch_fill(launch, path, argv, envp) != 0) {
            kfree(launch);
            return syscall_error(SYSCALL_EINVAL);
        }
        int pid = 0;
        if (sched_create_user(user_launch_thread, launch, sched_current_pid(), &pid) != 0) {
            kfree(launch);
            return syscall_error(SYSCALL_ENOMEM);
        }
        return (uint64_t)pid;
    }
    case SYSCALL_EXEC: {
        const char *path = (const char *)regs->rdi;
        const char *const *argv = (const char *const *)regs->rsi;
        const char *const *envp = (const char *const *)regs->rdx;
        struct user_launch launch = {0};
        if (user_launch_fill(&launch, path, argv, envp) != 0) {
            return syscall_error(SYSCALL_EINVAL);
        }
        struct user_space space;
        uint64_t user_sp = 0;
        if (user_prepare_image(launch.path, launch.argv, launch.envp, &space, &user_sp) != 0) {
            return syscall_error(SYSCALL_ENOENT);
        }
        sched_set_current_aspace(space.pml4_phys);
        arch_enter_user(space.entry, user_sp, space.pml4_phys);
        __builtin_unreachable();
    }
    case SYSCALL_GETPID:
        return (uint64_t)sched_current_pid();
    case SYSCALL_WAIT: {
        int *status = (int *)regs->rdi;
        if (status && !user_ptr_range((uint64_t)status, sizeof(int))) {
            return syscall_error(SYSCALL_EINVAL);
        }
        int code = 0;
        int pid = sched_wait_child(sched_current_pid(), status ? &code : NULL);
        if (pid < 0) {
            return syscall_error(SYSCALL_ENOENT);
        }
        if (status) {
            *status = code;
        }
        return (uint64_t)pid;
    }
    default:
        return syscall_error(SYSCALL_EINVAL);
    }
}
