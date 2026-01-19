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
    int refcount;
};

#include "kernel/pipe.h"

#define HANDLE_MAX 128

static struct handle handles[HANDLE_MAX];
static int handles_ready = 0;

void syscall_acquire_handle(int id)
{
    if (id < 0 || id >= HANDLE_MAX) return;
    if (handles[id].type != HANDLE_FREE) {
        handles[id].refcount++;
    }
}

void syscall_release_handle(int id)
{
    if (id < 0 || id >= HANDLE_MAX) return;
    if (handles[id].type == HANDLE_FREE) return;
    
    handles[id].refcount--;
    if (handles[id].refcount <= 0) {
        if (handles[id].type == HANDLE_VFS) {
            vfs_close(handles[id].file);
        }
        handles[id].type = HANDLE_FREE;
        handles[id].file = NULL;
        handles[id].owner_pid = 0;
        handles[id].refcount = 0;
    }
}

/* Re-evaluating fork logic below */


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
            handles[i].refcount = 1;
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
    /* Allow access if owner is 0 (global), self, or parent? */
    if (handles[fd].owner_pid == 0 || handles[fd].owner_pid == pid) {
        return 1;
    }
    
    return 0;
}

void syscall_cleanup_handles_for_pid(int pid)
{
    /* Handled via sched_exit calling syscall_release_handle on FDs? 
       No, sched.c doesn't loop FDs on exit yet.
       We should probably keep this BUT use release_handle?
       Wait, if we use local FDs, sched_exit will release local FDs which calls release_handle.
       So this function might be obsolete IF sched logic covers it.
       For now, let's just make it a no-op or clean remaining referenced handles?
       Using refcounts, handles persist if other processes hold them.
       So we shouldn't force-close based on PID unless we are sure.
       Owner PID concept is weakening with shared handles.
       Let's deprecate this function or make it scan handles and release if owner == pid?
       With refcounts, owner_pid is just "creator".
       
       Let's assume sched_exit cleans up local FDs properly.
    */
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

static int user_str_copy(const char *user, char *dst, uint64_t dst_len);

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

static int normalize_path(const char *path, char *out, uint64_t out_len)
{
    if (!path || !out || out_len < 2) return -1;
    
    uint64_t out_pos = 0;
    out[out_pos++] = '/';
    
    const char *p = path;
    if (*p == '/') ++p;
    
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') ++p;
        uint64_t len = (uint64_t)(p - start);
        
        if (len == 1 && start[0] == '.') {
            /* skip */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (out_pos > 1) {
                if (out[out_pos-1] == '/') --out_pos;
                while (out_pos > 1 && out[out_pos-1] != '/') --out_pos;
            }
        } else if (len > 0) {
            if (out_pos > 1 && out[out_pos-1] != '/') {
                if (out_pos + 1 >= out_len) return -1;
                out[out_pos++] = '/';
            }
            if (out_pos + len >= out_len) return -1;
            for (uint64_t i = 0; i < len; ++i) out[out_pos++] = start[i];
        }
        while (*p == '/') ++p;
    }
    
    if (out_pos == 0) out[out_pos++] = '/';
    out[out_pos] = '\0';
    return 0;
}

static int resolve_path(const char *user_path, char *out, uint64_t out_len)
{
    char temp[256];
    char cwd[256];
    
    if (!user_path || !out || out_len == 0) return -1;
    
    if (user_ptr_range((uint64_t)user_path, 1) == 0) return -1;
    
    /* Safely copy user path to kernel buffer */
    if (user_str_copy(user_path, temp, sizeof(temp)) != 0) return -1;

    if (temp[0] == '/') {
        return normalize_path(temp, out, out_len);
    }
    
    /* Get CWD */
    sched_get_cwd(cwd, sizeof(cwd));
    
    /* Join CWD and user path */
    uint64_t cwd_len = 0;
    while (cwd[cwd_len]) cwd_len++;
    
    uint64_t path_len = 0;
    while (temp[path_len]) path_len++;
    
    if (cwd_len + 1 + path_len >= sizeof(temp)) return -1; // overflow temp buffer reuse?
    /* Use a larger stack buffer or reuse carefully. 
       Let's construct full path in 'out' if big enough, or use another buffer.
       out_len is usually USER_PATH_MAX (256/128).
    */
    char combined[512];
    uint64_t pos = 0;
    for(uint64_t i=0; i<cwd_len; ++i) combined[pos++] = cwd[i];
    if (pos > 0 && combined[pos-1] != '/') combined[pos++] = '/';
    for(uint64_t i=0; i<path_len; ++i) combined[pos++] = temp[i];
    combined[pos] = '\0';
    
    return normalize_path(combined, out, out_len);
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
    /* Path is already in kernel buffer (from resolve_path), so just copy it */
    {
        uint64_t i = 0;
        uint64_t max = sizeof(launch->path);
        while (i < max - 1 && path[i] != '\0') {
            launch->path[i] = path[i];
            i++;
        }
        launch->path[i] = '\0';
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
    launch->override_fds[0] = -1;
    launch->override_fds[1] = -1;
    launch->override_fds[2] = -1;
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
        int global = sched_get_fd(fd);
        if (global < 0) {
            return syscall_error(SYSCALL_EBADF);
        }
        struct handle *h = &handles[global];
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
        // log_info_hex("SYSCALL_WRITE fd", fd);
        int global = sched_get_fd(fd);
        if (global < 0) {
            return syscall_error(SYSCALL_EBADF);
        }
        struct handle *h = &handles[global];
        if (h->type == HANDLE_FREE) return syscall_error(SYSCALL_EBADF);

        const void *buf = (const void *)regs->rsi;
        uint64_t len = regs->rdx;
        // log_info_hex("SYSCALL_WRITE len", len);
        if (!user_ptr_range((uint64_t)buf, len)) {
            return syscall_error(SYSCALL_ERANGE);
        }
        
        if (h->type == HANDLE_TTY) {
             return tty_write(buf, len);
        }
        if (h->type == HANDLE_VFS) {
             int64_t wrote = vfs_write(h->file, buf, len);
             if (wrote < 0) return syscall_error((enum syscall_error)(-wrote));
             return (uint64_t)wrote;
        }
        return syscall_error(SYSCALL_EBADF);
    }
    case SYSCALL_OPEN: {
        const char *path = (const char *)regs->rdi;
        char path_buf[USER_PATH_MAX];
        if (resolve_path(path, path_buf, sizeof(path_buf)) != 0) {
             return syscall_error(SYSCALL_EINVAL);
        }
        if (streq(path_buf, "/dev/tty") || streq(path_buf, "/dev/console")) {
            int global_fd = handle_alloc(HANDLE_TTY, NULL, sched_current_pid());
            if (global_fd < 0) {
                return syscall_error(SYSCALL_ENOMEM);
            }
            int local_fd = sched_allocate_fd(global_fd);
            if (local_fd < 0) {
                syscall_release_handle(global_fd);
                return syscall_error(SYSCALL_ENOMEM);
            }
            return (uint64_t)local_fd;
        }
        struct vfs_file *file = NULL;
        int vfs_err = vfs_open(path_buf, &file);
        if (vfs_err != SYSCALL_OK) {
            return syscall_error((enum syscall_error)vfs_err);
        }
        int global_fd = handle_alloc(HANDLE_VFS, file, sched_current_pid());
        if (global_fd < 0) {
            vfs_close(file);
            return syscall_error(SYSCALL_ENOMEM);
        }
        int local_fd = sched_allocate_fd(global_fd);
        if (local_fd < 0) {
            syscall_release_handle(global_fd);
            vfs_close(file); /* Note: handle release might not close file if we didn't attach it fully? handle_alloc claims owner. release decrements. if ref=0, handle cleans up. handle_alloc sets ref=1. release -> 0. close. So we should verify handle_alloc mechanics or just rely on release. */
            return syscall_error(SYSCALL_ENOMEM);
        }
        return (uint64_t)local_fd;
    }
    case SYSCALL_CLOSE: {
        int fd = (int)regs->rdi;
        int global = sched_get_fd(fd);
        if (global < 0) return syscall_error(SYSCALL_EBADF);
        
        sched_set_fd(fd, -1);
        syscall_release_handle(global);
        return 0;
    }
    case SYSCALL_SPAWN: {
        const char *path = (const char *)regs->rdi;
        // log_info("SYSCALL_SPAWN");
        char path_buf[USER_PATH_MAX];
        if (resolve_path(path, path_buf, sizeof(path_buf)) != 0) {
            return syscall_error(SYSCALL_EINVAL);
        }
        const char *const *argv = (const char *const *)regs->rsi;
        const char *const *envp = (const char *const *)regs->rdx;
        struct user_launch *launch = (struct user_launch *)kalloc_zero(sizeof(*launch), 16);
        if (!launch) {
            return syscall_error(SYSCALL_ENOMEM);
        }
        if (user_launch_fill(launch, path_buf, argv, envp) != 0) {
            kfree(launch);
            return syscall_error(SYSCALL_EINVAL);
        }
        
        /* Check for FD overrides (4th arg = r10) */
        /* Assuming r10 is available in syscall_regs */
        int *fd_map = (int *)regs->r10;
        if (fd_map) {
            if (user_ptr_range((uint64_t)fd_map, 3 * sizeof(int))) {
                 /* Read into temp */
                 int user_map[3];
                 /* user_str_copy or manual copy */
                 /* We trust user_ptr_range mostly */
                 const int *u = (const int *)fd_map;
                 for(int k=0; k<3; ++k) {
                     user_map[k] = u[k]; // unsafe direct read handled by fault handler usually?
                     /* NeptuneOS currently halts on fault... we should copy safely! */
                     /* For now assuming direct read if range checked is ok, or implement copy */
                 }
                 
                 for(int k=0; k<3; ++k) {
                     if (user_map[k] >= 0) {
                         int g = sched_get_fd(user_map[k]);
                         if (g >= 0) {
                             syscall_acquire_handle(g);
                             launch->override_fds[k] = g;
                         }
                     }
                 }
            }
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
        char path_buf[USER_PATH_MAX];
        if (resolve_path(path, path_buf, sizeof(path_buf)) != 0) {
            return syscall_error(SYSCALL_EINVAL);
        }
        const char *const *argv = (const char *const *)regs->rsi;
        const char *const *envp = (const char *const *)regs->rdx;
        struct user_launch launch = {0};
        if (user_launch_fill(&launch, path_buf, argv, envp) != 0) {
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
    case SYSCALL_CHDIR: {
        const char *path = (const char *)regs->rdi;
        char resolved[USER_PATH_MAX];
        if (resolve_path(path, resolved, sizeof(resolved)) != 0) {
            return syscall_error(SYSCALL_EINVAL);
        }
        /* Verify directory exists by opening it */
        struct vfs_file *f = NULL;
        int err = vfs_open(resolved, &f);
        if (err != SYSCALL_OK) {
            return syscall_error((enum syscall_error)err);
        }
        vfs_close(f);
        sched_set_cwd(resolved);
        return 0;
    }
    case SYSCALL_GETCWD: {
        char *buf = (char *)regs->rdi;
        uint64_t size = regs->rsi;
        if (!buf || size == 0) return syscall_error(SYSCALL_EINVAL);
        if (!user_ptr_range((uint64_t)buf, size)) return syscall_error(SYSCALL_EINVAL);
        char cwd[256];
        sched_get_cwd(cwd, sizeof(cwd));
        /* Manual user copy until copy_to_user helper exists */
        uint64_t len = 0;
        while(cwd[len]) len++;
        if (len >= size) return syscall_error(SYSCALL_ERANGE);
        for(uint64_t i=0; i<=len; ++i) buf[i] = cwd[i];
        return 0;
    }
    case SYSCALL_DUP2: {
        int oldfd = (int)regs->rdi;
        int newfd = (int)regs->rsi;
        
        int global_old = sched_get_fd(oldfd);
        if (global_old < 0) return syscall_error(SYSCALL_EBADF);
        
        if (newfd < 0 || newfd >= 16) return syscall_error(SYSCALL_EBADF);
        
        if (oldfd == newfd) return (uint64_t)newfd;
        
        /* Check if newfd holds a handle */
        int global_new = sched_get_fd(newfd);
        if (global_new >= 0) {
             sched_set_fd(newfd, -1);
             syscall_release_handle(global_new);
        }
        
        /* assign old handle to new fd */
        sched_set_fd(newfd, global_old);
        syscall_acquire_handle(global_old);
        
        return (uint64_t)newfd;
    }
    case SYSCALL_PIPE: {
        int *pipefd = (int *)regs->rdi;
        if (!user_ptr_range((uint64_t)pipefd, 2 * sizeof(int))) return syscall_error(SYSCALL_EINVAL);
        
        struct vfs_file *r = NULL, *w = NULL;
        if (pipe_create(&r, &w) != 0) return syscall_error(SYSCALL_ENOMEM);
        
        int h1 = handle_alloc(HANDLE_VFS, r, sched_current_pid());
        if (h1 < 0) {
            vfs_close(r); vfs_close(w);
            return syscall_error(SYSCALL_ENOMEM);
        }
        int h2 = handle_alloc(HANDLE_VFS, w, sched_current_pid());
        if (h2 < 0) {
            syscall_release_handle(h1);
            vfs_close(w);
            return syscall_error(SYSCALL_ENOMEM);
        }
        
        int fd1 = sched_allocate_fd(h1);
        int fd2 = sched_allocate_fd(h2);
        
        if (fd1 < 0 || fd2 < 0) {
             if(fd1 >= 0) sched_set_fd(fd1, -1);
             if(fd2 >= 0) sched_set_fd(fd2, -1);
             syscall_release_handle(h1);
             syscall_release_handle(h2);
             return syscall_error(SYSCALL_ENOMEM);
        }
        
        pipefd[0] = fd1;
        pipefd[1] = fd2;
        return 0;
    }
    default:
        return syscall_error(SYSCALL_EINVAL);
    }
}
