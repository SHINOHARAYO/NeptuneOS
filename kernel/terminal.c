#include "kernel/terminal.h"
#include "kernel/console.h"
#include "kernel/irq.h"
#include "kernel/sched.h"
#include "kernel/timer.h"

#include <stdint.h>

#define LINE_MAX 128

static const char prompt[] = "neptune> ";

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

static void terminal_prompt(void)
{
    console_write(prompt);
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

static void terminal_execute(const char *line)
{
    if (!line || line[0] == '\0') {
        terminal_prompt();
        return;
    }
    if (streq(line, "help")) {
        console_write("Commands: help, clear, ticks\n");
        terminal_prompt();
        return;
    }
    if (streq(line, "clear")) {
        console_clear(0x0F);
        terminal_prompt();
        return;
    }
    if (streq(line, "ticks")) {
        console_write("ticks=");
        console_write_hex(timer_get_ticks());
        console_write("\n");
        terminal_prompt();
        return;
    }
    console_write("Unknown command: ");
    console_write(line);
    console_write("\n");
    terminal_prompt();
}

static int is_shift_scancode(uint8_t sc, int pressed)
{
    if (sc == 0x2A || sc == 0x36) {
        return pressed ? 1 : 0;
    }
    if (sc == 0xAA || sc == 0xB6) {
        return 0;
    }
    return -1;
}

void terminal_thread(void *arg)
{
    (void)arg;
    char line[LINE_MAX] = {0};
    uint32_t len = 0;
    int shift = 0;

    terminal_prompt();

    for (;;) {
        uint8_t sc;
        while (irq_kb_pop(&sc)) {
            int shift_state = is_shift_scancode(sc, (sc & 0x80) == 0);
            if (shift_state >= 0) {
                shift = shift_state;
                continue;
            }
            if (sc & 0x80) {
                continue;
            }
            if (sc == 0x1C) {
                console_write("\n");
                line[len] = '\0';
                terminal_execute(line);
                len = 0;
                line[0] = '\0';
                continue;
            }
            if (sc == 0x0E) {
                if (len > 0) {
                    --len;
                    line[len] = '\0';
                    console_backspace();
                }
                continue;
            }
            char c = shift ? scancode_shift_map[sc] : scancode_map[sc];
            if (!c) {
                continue;
            }
            if (len + 1 < LINE_MAX) {
                line[len++] = c;
                line[len] = '\0';
                console_write((char[2]){c, '\0'});
            }
        }
        sched_maybe_preempt();
    }
}
