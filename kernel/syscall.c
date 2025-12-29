#include "kernel/syscall.h"
#include "kernel/console.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/irq.h"
#include "kernel/log.h"
#include "kernel/sched.h"
#include "kernel/serial.h"
#include "kernel/user.h"

#include <stddef.h>
#include <stdint.h>

static const char scancode_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const char scancode_shift_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?',
    [0x39] = ' ',
};

static int shift_state = 0;

static int scancode_to_char(uint8_t sc, char *out)
{
    if (!out) {
        return 0;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_state = 1;
        return 0;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift_state = 0;
        return 0;
    }
    if (sc & 0x80) {
        return 0;
    }
    if (sc == 0x1C) {
        *out = '\n';
        return 1;
    }
    if (sc == 0x0E) {
        *out = '\b';
        return 1;
    }
    char c = shift_state ? scancode_shift_map[sc] : scancode_map[sc];
    if (!c) {
        return 0;
    }
    *out = c;
    return 1;
}

uint64_t syscall_handle(struct syscall_regs *regs, struct interrupt_frame *frame)
{
    if (!regs) {
        return (uint64_t)-1;
    }
    (void)frame;

    uint64_t num = regs->rax;
    switch (num) {
    case SYSCALL_EXIT:
        log_info("Syscall exit");
        user_exit_handler();
        __builtin_unreachable();
    case SYSCALL_YIELD:
        sched_yield();
        return 0;
    case SYSCALL_WRITE: {
        uint64_t fd = regs->rdi;
        const char *buf = (const char *)regs->rsi;
        uint64_t len = regs->rdx;
        if (!buf || len == 0) {
            return 0;
        }
        if (fd != 1 && fd != 2) {
            return (uint64_t)-1;
        }
        console_write_len(buf, len);
        serial_write_len(buf, len);
        return len;
    }
    case SYSCALL_READ: {
        uint64_t fd = regs->rdi;
        char *buf = (char *)regs->rsi;
        uint64_t len = regs->rdx;
        if (!buf || len == 0) {
            return 0;
        }
        if (fd != 0) {
            return (uint64_t)-1;
        }
        uint64_t count = 0;
        while (count < len) {
            uint8_t ch = 0;
            if (irq_com_pop(&ch)) {
                if (ch == '\r') {
                    ch = '\n';
                }
                buf[count++] = (char)ch;
                if (ch == '\n') {
                    break;
                }
                continue;
            }
            uint8_t sc = 0;
            if (irq_kb_pop(&sc)) {
                char c;
                if (scancode_to_char(sc, &c)) {
                    buf[count++] = c;
                    if (c == '\n') {
                        break;
                    }
                }
                continue;
            }
            break;
        }
        return count;
    }
    default:
        return (uint64_t)-1;
    }
}
