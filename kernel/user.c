#include "kernel/user.h"
#include "kernel/elf.h"
#include "kernel/fs.h"
#include "kernel/gdt.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/mem.h"
#include "kernel/mmu.h"
#include "kernel/sched.h"
#include "kernel/syscall.h"
#include "kernel/terminal.h"

#include <stddef.h>
#include <stdint.h>

#define USER_STACK_PAGE_SIZE 4096

static uint64_t str_len(const char *s)
{
    uint64_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

static int stack_write(struct user_space *space, uint64_t addr, const void *data, uint64_t len)
{
    if (!space || !data || len == 0) {
        return -1;
    }
    if (addr < space->stack_bottom || addr + len > space->stack_top) {
        return -1;
    }
    uint64_t offset = addr - space->stack_bottom;
    uint64_t remaining = len;
    const uint8_t *src = (const uint8_t *)data;
    while (remaining > 0) {
        uint64_t page_index = offset / USER_STACK_PAGE_SIZE;
        uint64_t page_off = offset % USER_STACK_PAGE_SIZE;
        if (page_index >= space->stack_pages) {
            return -1;
        }
        uint64_t phys = space->stack_phys[page_index];
        uint8_t *dst = (uint8_t *)phys_to_hhdm(phys);
        uint64_t chunk = USER_STACK_PAGE_SIZE - page_off;
        if (chunk > remaining) {
            chunk = remaining;
        }
        for (uint64_t i = 0; i < chunk; ++i) {
            dst[page_off + i] = src[i];
        }
        src += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int push_bytes(struct user_space *space, uint64_t *sp, const void *data, uint64_t len)
{
    if (!space || !sp || len == 0) {
        return -1;
    }
    if (*sp < space->stack_bottom + len) {
        return -1;
    }
    *sp -= len;
    return stack_write(space, *sp, data, len);
}

static int push_u64(struct user_space *space, uint64_t *sp, uint64_t value)
{
    return push_bytes(space, sp, &value, sizeof(value));
}

static int push_string(struct user_space *space, uint64_t *sp, const char *s, uint64_t *out_addr)
{
    uint64_t len = str_len(s) + 1;
    if (push_bytes(space, sp, s, len) != 0) {
        return -1;
    }
    if (out_addr) {
        *out_addr = *sp;
    }
    return 0;
}

__attribute__((noreturn)) static void user_exit_common(int code)
{
    log_info("User-mode exited to kernel");
    sched_set_current_exit_code(code);
    syscall_cleanup_handles_for_pid(sched_current_pid());
    mmu_reload_cr3();
    static int terminal_started = 0;
    if (!terminal_started && sched_current_exit_to_kernel()) {
        sched_kill_user_threads();
        terminal_started = 1;
        if (sched_create(terminal_thread, NULL) != 0) {
            log_error("Failed to start kernel terminal");
        }
    }
    sched_exit_current();
}

void user_exit_handler(void)
{
    user_exit_common(0);
}

void user_exit_with_code(int code)
{
    user_exit_common(code);
}

int user_space_init(struct user_space *space)
{
    if (!space) {
        return -1;
    }

    uint64_t pml4 = mmu_create_user_pml4();
    if (!pml4) {
        return -1;
    }

    space->pml4_phys = pml4;
    space->entry = USER_BASE;
    space->stack_top = USER_STACK_TOP;
    space->stack_bottom = USER_STACK_TOP;
    space->stack_pages = 0;
    for (size_t i = 0; i < USER_STACK_MAX_PAGES; ++i) {
        space->stack_phys[i] = 0;
    }
    return 0;
}

int user_space_map_page(struct user_space *space, uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!space || !space->pml4_phys) {
        return -1;
    }
    return mmu_map_page_in(space->pml4_phys, virt, phys, flags | MMU_FLAG_USER);
}

int user_space_map_stack(struct user_space *space, uint64_t pages)
{
    if (!space || !space->pml4_phys || pages == 0 || pages > USER_STACK_MAX_PAGES) {
        return -1;
    }

    uint64_t bottom = USER_STACK_TOP - (pages * USER_STACK_PAGE_SIZE);
    for (uint64_t i = 0; i < pages; ++i) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            return -1;
        }
        uint64_t virt = bottom + (i * USER_STACK_PAGE_SIZE);
        if (user_space_map_page(space, virt, phys, MMU_FLAG_WRITE | MMU_FLAG_NOEXEC) != 0) {
            return -1;
        }
        space->stack_phys[i] = phys;
    }
    space->stack_top = USER_STACK_TOP;
    space->stack_bottom = bottom;
    space->stack_pages = pages;
    return 0;
}

int user_stack_setup(struct user_space *space, const char *const *argv, const char *const *envp, uint64_t *out_sp)
{
    if (!space || !out_sp || space->stack_pages == 0) {
        return -1;
    }

    uint64_t argc = 0;
    uint64_t envc = 0;
    while (argv && argv[argc] && argc < USER_ARG_MAX) {
        ++argc;
    }
    if (argv && argv[argc]) {
        return -1;
    }
    while (envp && envp[envc] && envc < USER_ENV_MAX) {
        ++envc;
    }
    if (envp && envp[envc]) {
        return -1;
    }

    uint64_t argv_addr[USER_ARG_MAX];
    uint64_t env_addr[USER_ENV_MAX];
    uint64_t sp = space->stack_top;

    for (uint64_t i = 0; i < argc; ++i) {
        if (push_string(space, &sp, argv[i], &argv_addr[i]) != 0) {
            return -1;
        }
    }
    for (uint64_t i = 0; i < envc; ++i) {
        if (push_string(space, &sp, envp[i], &env_addr[i]) != 0) {
            return -1;
        }
    }

    sp &= ~0xFULL;

    if (push_u64(space, &sp, 0) != 0) {
        return -1;
    }
    for (uint64_t i = envc; i > 0; --i) {
        if (push_u64(space, &sp, env_addr[i - 1]) != 0) {
            return -1;
        }
    }
    if (push_u64(space, &sp, 0) != 0) {
        return -1;
    }
    for (uint64_t i = argc; i > 0; --i) {
        if (push_u64(space, &sp, argv_addr[i - 1]) != 0) {
            return -1;
        }
    }
    if (push_u64(space, &sp, argc) != 0) {
        return -1;
    }

    *out_sp = sp;
    return 0;
}

int user_prepare_image(const char *path, const char *const *argv, const char *const *envp,
                       struct user_space *space, uint64_t *out_sp)
{
    if (!path || !space || !out_sp) {
        return -1;
    }

    if (user_space_init(space) != 0) {
        log_error("user_prepare: init failed");
        return -1;
    }

    const struct memfs_file *image = memfs_lookup(path);
    if (!image) {
        log_error("user_prepare: image not found");
        return -1;
    }
    if (elf_load_user(image->data, image->size, space) != 0) {
        log_error("user_prepare: ELF load failed");
        return -1;
    }

    if (user_space_map_stack(space, 1) != 0) {
        log_error("user_prepare: map stack failed");
        return -1;
    }

    if (user_stack_setup(space, argv, envp, out_sp) != 0) {
        log_error("user_prepare: stack setup failed");
        return -1;
    }

    return 0;
}

void user_enter(uint64_t entry, uint64_t user_stack, uint64_t pml4_phys)
{
    uint64_t rsp0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp0));
    gdt_set_kernel_stack(rsp0);

    if (pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    }

    __asm__ volatile(
        "pushq %[user_ss]\n"
        "pushq %[user_rsp]\n"
        "pushfq\n"
        "popq %%rax\n"
        "orq $0x200, %%rax\n"
        "pushq %%rax\n"
        "pushq %[user_cs]\n"
        "pushq %[user_rip]\n"
        "iretq\n"
        :
        : [user_ss] "i"(GDT_USER_DATA | 0x3),
          [user_rsp] "r"(user_stack),
          [user_cs] "i"(GDT_USER_CODE | 0x3),
          [user_rip] "r"(entry)
        : "rax", "memory");

    __builtin_unreachable();
}

__attribute__((naked, noreturn)) void user_exit_trampoline(void)
{
    __asm__ volatile(
        "add $16, %rsp\n"
        "call user_exit_handler\n"
        "1: hlt\n"
        "jmp 1b\n"
    );
}

void user_smoke_thread(void *arg)
{
    (void)arg;
    struct user_space space;
    const char *argv[] = { "/bin/init", NULL };
    const char *envp[] = { "TERM=neptune", "USER=guest", NULL };
    uint64_t user_sp = 0;

    if (user_prepare_image("/bin/init", argv, envp, &space, &user_sp) != 0) {
        log_error("user_smoke: init launch failed");
        return;
    }

    log_info("Entering user-mode init");
    sched_set_current_aspace(space.pml4_phys);
    sched_set_current_exit_to_kernel(1);
    user_enter(space.entry, user_sp, space.pml4_phys);
}

void user_launch_thread(void *arg)
{
    struct user_launch *launch = (struct user_launch *)arg;
    if (!launch) {
        return;
    }

    struct user_space space;
    uint64_t user_sp = 0;
    if (user_prepare_image(launch->path, launch->argv, launch->envp, &space, &user_sp) != 0) {
        log_error("user_launch: load failed");
        kfree(launch);
        return;
    }

    log_info("Entering user-mode image");
    kfree(launch);
    sched_set_current_aspace(space.pml4_phys);
    sched_set_current_exit_to_kernel(0);
    user_enter(space.entry, user_sp, space.pml4_phys);
}
