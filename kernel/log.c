#include "kernel/log.h"
#include "kernel/console.h"
#include "kernel/serial.h"

#include <stdint.h>

static enum log_level current_level = LOG_LEVEL_INFO;
static struct log_colors current_colors = {
    .debug_color = 0x0A,   /* bright green on black */
    .info_color  = 0x0F,   /* bright white on black */
    .warn_color  = 0x0E,   /* yellow on black */
    .error_color = 0x0C,   /* red on black */
    .default_color = 0x0F, /* fallback to bright white */
};

/* On AArch64, console writes to serial, so we skip explicit serial writes to avoid duplication. */
#ifdef __aarch64__
#define raw_serial_write(x) ((void)0)
#define raw_serial_write_hex(x) ((void)0)
#else
#define raw_serial_write(x) serial_write(x)
#define raw_serial_write_hex(x) serial_write_hex(x)
#endif

static void write_prefix(enum log_level level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        console_set_color(current_colors.debug_color);
        console_write("[DEBUG] ");
        raw_serial_write("[DEBUG] ");
        break;
    case LOG_LEVEL_INFO:
        console_set_color(current_colors.info_color);
        console_write("[INFO ] ");
        raw_serial_write("[INFO ] ");
        break;
    case LOG_LEVEL_WARN:
        console_set_color(current_colors.warn_color);
        console_write("[WARN ] ");
        raw_serial_write("[WARN ] ");
        break;
    case LOG_LEVEL_ERROR:
    default:
        console_set_color(current_colors.error_color);
        console_write("[ERROR] ");
        raw_serial_write("[ERROR] ");
        break;
    }
}

static void log_emit(enum log_level level, const char *msg)
{
    if (level < current_level) {
        return;
    }

    write_prefix(level);
    console_set_color(current_colors.default_color);
    console_write(msg);
    console_write("\n");
    raw_serial_write(msg);
    raw_serial_write("\n");
}

void log_init(void)
{
    serial_init();
    current_level = LOG_LEVEL_INFO;
    console_set_color(current_colors.default_color);
}

void log_set_level(enum log_level level)
{
    current_level = level;
}

void log_set_colors(const struct log_colors *colors)
{
    if (!colors) {
        return;
    }
    current_colors = *colors;
    console_set_color(current_colors.default_color);
}

void log_debug(const char *msg) { log_emit(LOG_LEVEL_DEBUG, msg); }
void log_info(const char *msg) { log_emit(LOG_LEVEL_INFO, msg); }
void log_warn(const char *msg) { log_emit(LOG_LEVEL_WARN, msg); }
void log_error(const char *msg) { log_emit(LOG_LEVEL_ERROR, msg); }

static void log_emit_hex(enum log_level level, const char *label, uint64_t value)
{
    if (level < current_level) {
        return;
    }

    write_prefix(level);
    console_set_color(current_colors.default_color);
    console_write(label);
    console_write(": ");
    console_write_hex(value);
    console_write("\n");

    raw_serial_write(label);
    raw_serial_write(": ");
    raw_serial_write_hex(value);
    raw_serial_write("\n");
}

void log_debug_hex(const char *label, uint64_t value)
{
    log_emit_hex(LOG_LEVEL_DEBUG, label, value);
}

void log_info_hex(const char *label, uint64_t value)
{
    log_emit_hex(LOG_LEVEL_INFO, label, value);
}
