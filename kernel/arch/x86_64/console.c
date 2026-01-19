#include <kernel/hal.h>
#include <kernel/io.h>
#include <stdint.h>

#define VGA_BUFFER_PHYS 0xB8000ULL
#define VGA_HIGHER_HALF (0xFFFFFFFF80000000ULL + VGA_BUFFER_PHYS) /* HARDCODED generic hhdm base? Use macro */
/* Better: */
#include <kernel/mmu.h>
#define VGA_BUFFER_ADDR ((uint16_t*)phys_to_higher_half(VGA_BUFFER_PHYS))

#define VGA_COLS 80
#define VGA_ROWS 25

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;
static uint8_t current_color = 0x0F;

static inline void update_hw_cursor(void)
{
    uint16_t pos = (uint16_t)((cursor_row * VGA_COLS) + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll_if_needed(void)
{
    if (cursor_row < VGA_ROWS) {
        return;
    }

    volatile uint16_t *buf = VGA_BUFFER_ADDR;
    const size_t row_words = VGA_COLS;
    for (size_t row = 1; row < VGA_ROWS; ++row) {
        size_t dst_index = (row - 1) * row_words;
        size_t src_index = row * row_words;
        for (size_t col = 0; col < row_words; ++col) {
            buf[dst_index + col] = buf[src_index + col];
        }
    }

    size_t last_row = (VGA_ROWS - 1) * row_words;
    for (size_t col = 0; col < row_words; ++col) {
        buf[last_row + col] = ((uint16_t)current_color << 8) | ' ';
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

void arch_console_write_char(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        ++cursor_row;
        scroll_if_needed();
        update_hw_cursor();
        return;
    }
    
    scroll_if_needed();
    volatile uint16_t *buf = VGA_BUFFER_ADDR;
    const size_t index = (cursor_row * VGA_COLS) + cursor_col;
    buf[index] = ((uint16_t)current_color << 8) | (uint8_t)c;
    advance_cursor();
    update_hw_cursor();
}

void arch_console_write(const char *msg, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        arch_console_write_char(msg[i]);
    }
}

void arch_console_clear(uint8_t color)
{
    current_color = color;
    cursor_row = 0;
    cursor_col = 0;
    volatile uint16_t *buf = VGA_BUFFER_ADDR;
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; ++i) {
        buf[i] = ((uint16_t)current_color << 8) | ' ';
    }
    update_hw_cursor();
}

void arch_console_set_color(uint8_t color)
{
    current_color = color;
}

void arch_console_backspace(void)
{
    if (cursor_row == 0 && cursor_col == 0) return;

    if (cursor_col == 0) {
        cursor_col = VGA_COLS - 1;
        if (cursor_row > 0) --cursor_row;
    } else {
        --cursor_col;
    }
    volatile uint16_t *buf = VGA_BUFFER_ADDR;
    const size_t index = (cursor_row * VGA_COLS) + cursor_col;
    buf[index] = ((uint16_t)current_color << 8) | ' ';
    update_hw_cursor();
}

void arch_console_init(void)
{
    /* Nothing special for VGA text mode pre-boot */
}
