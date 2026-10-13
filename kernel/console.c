#include <kernel/console.h>
#include <kernel/font8x16.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <stdint.h>

#define FONT_WIDTH 8ULL
#define FONT_HEIGHT 16ULL
#define MAX_PARAMS 16
#define ATTR_BOLD 1u
#define ATTR_UNDERLINE 2u
#define ATTR_REVERSE 4u

struct cell {
    uint32_t fg, bg;
    uint8_t ch, attr;
};

enum parser_state { STATE_NORMAL, STATE_ESCAPE, STATE_CSI };

static const uint32_t palette[16] = {
    0x000000, 0xcd3131, 0x0dbc79, 0xe5e510, 0x2472c8, 0xbc3fbc, 0x11a8cd, 0xe5e5e5,
    0x666666, 0xf14c4c, 0x23d18b, 0xf5f543, 0x3b8eea, 0xd670d6, 0x29b8db, 0xffffff,
};

static struct limine_framebuffer *fb;
static uint32_t *shadow;
static struct cell *cells;
static uint64_t text_cols, text_rows;
static uint64_t cursor_col, cursor_row, saved_col, saved_row;
static uint64_t dirty_top, dirty_bottom;
static int wrap_pending, cursor_visible = 1;
static uint32_t fg = 0xe5e5e5, bg = 0x000000;
static uint8_t attr;
static enum parser_state state;
static unsigned params[MAX_PARAMS];
static unsigned param_count;
static int private_mode;
static struct spinlock console_lock = SPINLOCK_INIT;

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

static void mark_dirty(uint64_t top, uint64_t bottom) {
    if (top < dirty_top) dirty_top = top;
    if (bottom > dirty_bottom) dirty_bottom = bottom;
}

static void store_row(uint64_t y, uint64_t x0, const uint32_t *pixels, uint64_t count) {
    if (shadow) {
        memcpy(shadow + y * fb->width + x0, pixels, count * sizeof(uint32_t));
        return;
    }
    volatile uint8_t *row = (volatile uint8_t *)fb->address + y * fb->pitch;
    for (uint64_t x = 0; x < count; x++) {
        if (fb->bpp == 32) {
            *(volatile uint32_t *)(row + (x0 + x) * 4) = pixels[x];
        } else {
            volatile uint8_t *p = row + (x0 + x) * 3;
            p[0] = (uint8_t)pixels[x];
            p[1] = (uint8_t)(pixels[x] >> 8);
            p[2] = (uint8_t)(pixels[x] >> 16);
        }
    }
}

static void render(uint64_t row, uint64_t col, int inverse) {
    struct cell *cell = &cells[row * text_cols + col];
    uint32_t front = cell->fg, back = cell->bg;
    if (!!(cell->attr & ATTR_REVERSE) != !!inverse) {
        uint32_t swap = front;
        front = back;
        back = swap;
    }
    uint32_t on = pack_rgb(front), off = pack_rgb(back);
    unsigned char c = cell->ch >= 128 ? '?' : cell->ch;
    uint32_t pixels[FONT_WIDTH];
    for (uint64_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t bits = font8x16[c][y];
        if ((cell->attr & ATTR_UNDERLINE) && y == FONT_HEIGHT - 2) bits = 0xff;
        for (uint64_t x = 0; x < FONT_WIDTH; x++) pixels[x] = (bits & (0x80u >> x)) ? on : off;
        store_row(row * FONT_HEIGHT + y, col * FONT_WIDTH, pixels, FONT_WIDTH);
    }
    mark_dirty(row, row + 1);
}

static void flush(void) {
    if (!shadow || dirty_top >= dirty_bottom) {
        dirty_top = text_rows;
        dirty_bottom = 0;
        return;
    }
    for (uint64_t y = dirty_top * FONT_HEIGHT; y < dirty_bottom * FONT_HEIGHT; y++) {
        const uint32_t *source = shadow + y * fb->width;
        volatile uint8_t *row = (volatile uint8_t *)fb->address + y * fb->pitch;
        if (fb->bpp == 32) {
            memcpy((void *)row, source, fb->width * sizeof(uint32_t));
            continue;
        }
        for (uint64_t x = 0; x < fb->width; x++) {
            row[x * 3] = (uint8_t)source[x];
            row[x * 3 + 1] = (uint8_t)(source[x] >> 8);
            row[x * 3 + 2] = (uint8_t)(source[x] >> 16);
        }
    }
    dirty_top = text_rows;
    dirty_bottom = 0;
}

static void clear_cells(uint64_t row, uint64_t col, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        struct cell *cell = &cells[row * text_cols + col + i];
        *cell = (struct cell) { fg, bg, ' ', 0 };
        render(row, col + i, 0);
    }
}

static void redraw_rows(uint64_t top, uint64_t bottom) {
    for (uint64_t row = top; row < bottom; row++)
        for (uint64_t col = 0; col < text_cols; col++) render(row, col, 0);
}

static void scroll_region(uint64_t top, uint64_t bottom, int up, uint64_t count) {
    if (top >= bottom) return;
    if (count > bottom - top) count = bottom - top;
    uint64_t moved = bottom - top - count;
    size_t row_cells = text_cols * sizeof(struct cell);
    if (up) {
        memmove(&cells[top * text_cols], &cells[(top + count) * text_cols], moved * row_cells);
        if (shadow)
            memmove(shadow + top * FONT_HEIGHT * fb->width,
                    shadow + (top + count) * FONT_HEIGHT * fb->width,
                    moved * FONT_HEIGHT * fb->width * sizeof(uint32_t));
        else
            redraw_rows(top, top + moved);
        for (uint64_t row = bottom - count; row < bottom; row++) clear_cells(row, 0, text_cols);
    } else {
        memmove(&cells[(top + count) * text_cols], &cells[top * text_cols], moved * row_cells);
        if (shadow)
            memmove(shadow + (top + count) * FONT_HEIGHT * fb->width,
                    shadow + top * FONT_HEIGHT * fb->width,
                    moved * FONT_HEIGHT * fb->width * sizeof(uint32_t));
        else
            redraw_rows(top + count, bottom);
        for (uint64_t row = top; row < top + count; row++) clear_cells(row, 0, text_cols);
    }
    mark_dirty(top, bottom);
}

static void line_feed(void) {
    if (cursor_row + 1 < text_rows) cursor_row++;
    else scroll_region(0, text_rows, 1, 1);
}

static void put_glyph(unsigned char c) {
    if (wrap_pending) {
        cursor_col = 0;
        line_feed();
        wrap_pending = 0;
    }
    cells[cursor_row * text_cols + cursor_col] = (struct cell) { fg, bg, c, attr };
    render(cursor_row, cursor_col, 0);
    if (cursor_col + 1 < text_cols) cursor_col++;
    else wrap_pending = 1;
}

static unsigned param(unsigned index, unsigned fallback) {
    return index < param_count && params[index] ? params[index] : fallback;
}

static uint32_t color_256(unsigned index) {
    if (index < 16) return palette[index];
    if (index >= 232) {
        uint32_t level = 8 + (index - 232) * 10;
        return (level << 16) | (level << 8) | level;
    }
    index -= 16;
    static const uint8_t steps[6] = { 0, 95, 135, 175, 215, 255 };
    return ((uint32_t)steps[index / 36] << 16) | ((uint32_t)steps[index / 6 % 6] << 8) |
           steps[index % 6];
}

static void select_graphics(void) {
    if (!param_count) param_count = 1, params[0] = 0;
    for (unsigned i = 0; i < param_count; i++) {
        unsigned p = params[i];
        if (p == 0) {
            fg = palette[7];
            bg = palette[0];
            attr = 0;
        } else if (p == 1) {
            attr |= ATTR_BOLD;
        } else if (p == 4) {
            attr |= ATTR_UNDERLINE;
        } else if (p == 7) {
            attr |= ATTR_REVERSE;
        } else if (p == 22) {
            attr &= ~ATTR_BOLD;
        } else if (p == 24) {
            attr &= ~ATTR_UNDERLINE;
        } else if (p == 27) {
            attr &= ~ATTR_REVERSE;
        } else if (p >= 30 && p <= 37) {
            fg = palette[p - 30 + (attr & ATTR_BOLD ? 8 : 0)];
        } else if (p == 39) {
            fg = palette[7];
        } else if (p >= 40 && p <= 47) {
            bg = palette[p - 40];
        } else if (p == 49) {
            bg = palette[0];
        } else if (p >= 90 && p <= 97) {
            fg = palette[p - 90 + 8];
        } else if (p >= 100 && p <= 107) {
            bg = palette[p - 100 + 8];
        } else if ((p == 38 || p == 48) && i + 1 < param_count) {
            uint32_t color;
            if (params[i + 1] == 5 && i + 2 < param_count) {
                color = color_256(params[i + 2]);
                i += 2;
            } else if (params[i + 1] == 2 && i + 4 < param_count) {
                color = ((params[i + 2] & 0xff) << 16) | ((params[i + 3] & 0xff) << 8) |
                        (params[i + 4] & 0xff);
                i += 4;
            } else {
                break;
            }
            if (p == 38) fg = color;
            else bg = color;
        }
    }
}

static void erase_display(unsigned mode) {
    if (mode == 0) {
        clear_cells(cursor_row, cursor_col, text_cols - cursor_col);
        for (uint64_t row = cursor_row + 1; row < text_rows; row++) clear_cells(row, 0, text_cols);
    } else if (mode == 1) {
        for (uint64_t row = 0; row < cursor_row; row++) clear_cells(row, 0, text_cols);
        clear_cells(cursor_row, 0, cursor_col + 1);
    } else {
        for (uint64_t row = 0; row < text_rows; row++) clear_cells(row, 0, text_cols);
    }
}

static void erase_line(unsigned mode) {
    if (mode == 0) clear_cells(cursor_row, cursor_col, text_cols - cursor_col);
    else if (mode == 1) clear_cells(cursor_row, 0, cursor_col + 1);
    else clear_cells(cursor_row, 0, text_cols);
}

static void shift_cells(int insert, unsigned count) {
    uint64_t base = cursor_row * text_cols;
    uint64_t span = text_cols - cursor_col;
    if (count > span) count = (unsigned)span;
    if (insert)
        memmove(&cells[base + cursor_col + count], &cells[base + cursor_col],
                (span - count) * sizeof(struct cell));
    else
        memmove(&cells[base + cursor_col], &cells[base + cursor_col + count],
                (span - count) * sizeof(struct cell));
    uint64_t blank = insert ? cursor_col : text_cols - count;
    for (uint64_t i = 0; i < count; i++)
        cells[base + blank + i] = (struct cell) { fg, bg, ' ', 0 };
    for (uint64_t col = cursor_col; col < text_cols; col++) render(cursor_row, col, 0);
}

static void clamp_cursor(void) {
    if (cursor_col >= text_cols) cursor_col = text_cols - 1;
    if (cursor_row >= text_rows) cursor_row = text_rows - 1;
}

static void control_sequence(char final) {
    unsigned n = param(0, 1);
    wrap_pending = 0;
    switch (final) {
    case 'A': cursor_row = cursor_row > n ? cursor_row - n : 0; break;
    case 'B': cursor_row += n; break;
    case 'C': cursor_col += n; break;
    case 'D': cursor_col = cursor_col > n ? cursor_col - n : 0; break;
    case 'E': cursor_row += n; cursor_col = 0; break;
    case 'F': cursor_row = cursor_row > n ? cursor_row - n : 0; cursor_col = 0; break;
    case 'G': case '`': cursor_col = n - 1; break;
    case 'd': cursor_row = n - 1; break;
    case 'H': case 'f':
        cursor_row = param(0, 1) - 1;
        cursor_col = param(1, 1) - 1;
        break;
    case 'J': erase_display(param_count ? params[0] : 0); break;
    case 'K': erase_line(param_count ? params[0] : 0); break;
    case 'm': select_graphics(); break;
    case 's': saved_col = cursor_col; saved_row = cursor_row; break;
    case 'u': cursor_col = saved_col; cursor_row = saved_row; break;
    case '@': shift_cells(1, n); break;
    case 'P': shift_cells(0, n); break;
    case 'X': clear_cells(cursor_row, cursor_col, n < text_cols - cursor_col ? n : text_cols - cursor_col); break;
    case 'L': scroll_region(cursor_row, text_rows, 0, n); break;
    case 'M': scroll_region(cursor_row, text_rows, 1, n); break;
    case 'S': scroll_region(0, text_rows, 1, n); break;
    case 'T': scroll_region(0, text_rows, 0, n); break;
    case 'h': case 'l':
        if (private_mode && param_count && params[0] == 25) cursor_visible = final == 'h';
        break;
    default: break;
    }
    clamp_cursor();
}

static void emulate(char c) {
    if (state == STATE_ESCAPE) {
        state = STATE_NORMAL;
        if (c == '[') {
            state = STATE_CSI;
            param_count = 0;
            private_mode = 0;
            memset(params, 0, sizeof(params));
        } else if (c == '7') {
            saved_col = cursor_col;
            saved_row = cursor_row;
        } else if (c == '8') {
            cursor_col = saved_col;
            cursor_row = saved_row;
        } else if (c == 'c') {
            fg = palette[7];
            bg = palette[0];
            attr = 0;
            erase_display(2);
            cursor_col = cursor_row = 0;
        } else if (c == 'M') {
            if (cursor_row) cursor_row--;
            else scroll_region(0, text_rows, 0, 1);
        }
        return;
    }
    if (state == STATE_CSI) {
        if (c >= '0' && c <= '9') {
            if (!param_count) param_count = 1;
            params[param_count - 1] = params[param_count - 1] * 10 + (unsigned)(c - '0');
        } else if (c == ';') {
            if (!param_count) param_count = 1;
            if (param_count < MAX_PARAMS) param_count++;
        } else if (c == '?' || c == '>' || c == '=') {
            private_mode = 1;
        } else if (c >= 0x40 && c <= 0x7e) {
            state = STATE_NORMAL;
            control_sequence(c);
        }
        return;
    }
    switch (c) {
    case 0x1b: state = STATE_ESCAPE; break;
    case '\n': wrap_pending = 0; cursor_col = 0; line_feed(); break;
    case '\r': wrap_pending = 0; cursor_col = 0; break;
    case '\b': wrap_pending = 0; if (cursor_col) cursor_col--; break;
    case '\t':
        do put_glyph(' '); while (cursor_col % 8 && !wrap_pending);
        break;
    case '\a': break;
    default:
        if ((unsigned char)c >= 32) put_glyph((unsigned char)c);
        break;
    }
}

static void show_cursor(int visible) {
    if (fb && cursor_visible) render(cursor_row, cursor_col, visible);
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
    cells = kmalloc(text_cols * text_rows * sizeof(struct cell));
    if (!cells) {
        fb = 0;
        return -1;
    }
    shadow = kmalloc(fb->width * fb->height * sizeof(uint32_t));
    dirty_top = 0;
    dirty_bottom = text_rows;
    erase_display(2);
    show_cursor(1);
    flush();
    return 0;
}

static void write_locked(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) serial_putc(s[i]);
    if (!fb) return;
    show_cursor(0);
    for (size_t i = 0; i < n; i++) emulate(s[i]);
    show_cursor(1);
    flush();
}

void console_write(const char *s, size_t n) {
    unsigned long flags = spin_lock_irqsave(&console_lock);
    write_locked(s, n);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_puts(const char *s) {
    console_write(s, strlen(s));
}

uint16_t console_cols(void) {
    return (uint16_t)text_cols;
}

uint16_t console_rows(void) {
    return (uint16_t)text_rows;
}
