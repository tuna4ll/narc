#include <kernel/font8x16.h>
#include <kernel/serial.h>
#include <kernel/console.h>
#include <kernel/spinlock.h>
#include <stdint.h>

#define FONT_WIDTH 8ULL
#define FONT_HEIGHT 16ULL

static struct limine_framebuffer *fb;
static uint64_t text_cols;
static uint64_t text_rows;
static uint64_t cursor_col;
static uint64_t cursor_row;
static uint32_t fg = 0x00e6e6e6u;
static uint32_t bg = 0x00000000u;
static struct spinlock console_lock = SPINLOCK_INIT;

static void console_clear(void);

static uint32_t scale_channel(uint32_t value, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits >= 32) return value;
    uint64_t max = (1ULL << bits) - 1;
    return (uint32_t)(((uint64_t)value * max) / 255ULL);
}

static uint32_t pack_rgb(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xff;
    uint32_t g = (rgb >> 8) & 0xff;
    uint32_t b = rgb & 0xff;

    return (scale_channel(r, fb->red_mask_size) << fb->red_mask_shift) |
           (scale_channel(g, fb->green_mask_size) << fb->green_mask_shift) |
           (scale_channel(b, fb->blue_mask_size) << fb->blue_mask_shift);
}

static void put_pixel(uint64_t x, uint64_t y, uint32_t rgb) {
    if (!fb || x >= fb->width || y >= fb->height) return;

    volatile uint8_t *row = (volatile uint8_t *)fb->address + y * fb->pitch;
    uint32_t pixel = pack_rgb(rgb);

    if (fb->bpp == 32) {
        *(volatile uint32_t *)(row + x * 4) = pixel;
    } else if (fb->bpp == 24) {
        volatile uint8_t *p = row + x * 3;
        p[0] = (uint8_t)(pixel & 0xff);
        p[1] = (uint8_t)((pixel >> 8) & 0xff);
        p[2] = (uint8_t)((pixel >> 16) & 0xff);
    }
}

static void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t rgb) {
    for (uint64_t py = 0; py < h; py++) {
        for (uint64_t px = 0; px < w; px++) {
            put_pixel(x + px, y + py, rgb);
        }
    }
}

static void draw_glyph(uint64_t col, uint64_t row, unsigned char c) {
    if (c >= 128) c = '?';
    uint64_t x0 = col * FONT_WIDTH;
    uint64_t y0 = row * FONT_HEIGHT;

    for (uint64_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t bits = font8x16[c][y];
        for (uint64_t x = 0; x < FONT_WIDTH; x++) {
            put_pixel(x0 + x, y0 + y, (bits & (0x80u >> x)) ? fg : bg);
        }
    }
}

static void scroll_one_line(void) {
    uint64_t line_bytes = FONT_HEIGHT * fb->pitch;
    uint64_t keep_bytes = (fb->height - FONT_HEIGHT) * fb->pitch;
    volatile uint8_t *base = (volatile uint8_t *)fb->address;

    for (uint64_t i = 0; i < keep_bytes; i++) {
        base[i] = base[i + line_bytes];
    }

    fill_rect(0, fb->height - FONT_HEIGHT, fb->width, FONT_HEIGHT, bg);
    cursor_row = text_rows - 1;
}

int console_init(struct limine_framebuffer_response *response) {
    if (!response || response->framebuffer_count == 0 || !response->framebuffers) {
        serial_puts("[panic] Limine did not provide a framebuffer\n");
        return -1;
    }

    fb = response->framebuffers[0];
    if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        (fb->bpp != 24 && fb->bpp != 32) || fb->width < FONT_WIDTH || fb->height < FONT_HEIGHT) {
        serial_puts("[panic] unsupported framebuffer format\n");
        fb = 0;
        return -1;
    }

    text_cols = fb->width / FONT_WIDTH;
    text_rows = fb->height / FONT_HEIGHT;
    cursor_col = 0;
    cursor_row = 0;
    console_clear();
    return 0;
}

static void console_clear(void) {
    if (!fb) return;
    fill_rect(0, 0, fb->width, fb->height, bg);
    cursor_col = 0;
    cursor_row = 0;
}

static void console_putc(char c) {
    serial_putc(c);
    if (!fb) return;

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        uint64_t spaces = 4 - (cursor_col & 3);
        for (uint64_t i = 0; i < spaces; i++) console_putc(' ');
        return;
    } else if ((unsigned char)c >= 32) {
        draw_glyph(cursor_col, cursor_row, (unsigned char)c);
        cursor_col++;
        if (cursor_col >= text_cols) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= text_rows) scroll_one_line();
}

void console_write(const char *s, size_t n) {
    unsigned long flags = spin_lock_irqsave(&console_lock);
    for (size_t i = 0; i < n; i++) console_putc(s[i]);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_puts(const char *s) {
    unsigned long flags = spin_lock_irqsave(&console_lock);
    while (*s) console_putc(*s++);
    spin_unlock_irqrestore(&console_lock, flags);
}

uint16_t console_cols(void) {
    return (uint16_t)text_cols;
}

uint16_t console_rows(void) {
    return (uint16_t)text_rows;
}
