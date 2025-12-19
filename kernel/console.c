#include "kernel/console.h"

#include <stdint.h>
#include <stddef.h>

#ifndef VGA_BUFFER_ADDR
#define VGA_BUFFER_ADDR 0xFFFFFFFF800B8000ULL
#endif

#define VGA_COLS 80
#define VGA_ROWS 25

static volatile uint16_t *const vga_buffer = (uint16_t *)VGA_BUFFER_ADDR;
static uint8_t current_color = 0x0F; /* bright white on black */
static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

static void scroll_if_needed(void)
{
    if (cursor_row < VGA_ROWS) {
        return;
    }

    const size_t row_words = VGA_COLS;
    for (size_t row = 1; row < VGA_ROWS; ++row) {
        size_t dst_index = (row - 1) * row_words;
        size_t src_index = row * row_words;
        for (size_t col = 0; col < row_words; ++col) {
            vga_buffer[dst_index + col] = vga_buffer[src_index + col];
        }
    }

    size_t last_row = (VGA_ROWS - 1) * row_words;
    for (size_t col = 0; col < row_words; ++col) {
        vga_buffer[last_row + col] = ((uint16_t)current_color << 8) | ' ';
    }

    cursor_row = VGA_ROWS - 1;
}

static void advance_cursor(void)
{
    ++cursor_col;
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        ++cursor_row;
        scroll_if_needed();
    }
}

static void put_char(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        ++cursor_row;
        scroll_if_needed();
        return;
    }

    scroll_if_needed();
    const size_t index = (cursor_row * VGA_COLS) + cursor_col;
    vga_buffer[index] = ((uint16_t)current_color << 8) | (uint8_t)c;
    advance_cursor();
}

void console_clear(uint8_t color)
{
    current_color = color;
    cursor_row = 0;
    cursor_col = 0;
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; ++i) {
        vga_buffer[i] = ((uint16_t)current_color << 8) | ' ';
    }
}

void console_set_color(uint8_t color)
{
    current_color = color;
}

void console_write(const char *msg)
{
    for (size_t i = 0; msg[i] != '\0'; ++i) {
        put_char(msg[i]);
    }
}

static char hex_digit(uint8_t value)
{
    return value < 10 ? ('0' + value) : ('A' + (value - 10));
}

void console_write_hex(uint64_t value)
{
    console_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (value >> shift) & 0xF;
        put_char(hex_digit(nibble));
    }
}
