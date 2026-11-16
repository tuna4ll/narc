#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

#include "terminal.h"
#include "random.h"
#include "wallpaper.h"
#include "heap.h"
#include "serial.h"

// wallpaper frame around the text is the shorter screen side divided by this
#define MARGIN_DIV 10
// black over the wallpaper behind the text out of 256 and 256 is fully opaque
#define PANEL_ALPHA 200

static struct flanterm_context *ft_ctx = NULL;

// flanterm has no alpha so the panel is baked into the canvas
static void darken_panel(uint32_t *canvas, size_t w, size_t h, size_t margin) {
    uint32_t keep = 256 - PANEL_ALPHA;

    for (size_t y = margin; y < h - margin; y++) {
        for (size_t x = margin; x < w - margin; x++) {
            uint32_t px = canvas[y * w + x];
            uint32_t r = ((px >> 16) & 0xFF) * keep / 256;
            uint32_t g = ((px >> 8) & 0xFF) * keep / 256;
            uint32_t b = (px & 0xFF) * keep / 256;
            canvas[y * w + x] = (r << 16) | (g << 8) | b;
        }
    }
}

static size_t randomColor(void) {
    return krand() % 8;
}

static void *terminal_malloc(size_t size) {
    return kmalloc(size);
}

static void terminal_free(void *ptr, size_t size) {
    (void)size;
    kfree(ptr);
}

void terminal_init(const struct boot_framebuffer *fb) {
    if (fb == NULL || fb->width == 0 || fb->height == 0)
        return;

    size_t shorter = fb->width < fb->height ? fb->width : fb->height;
    size_t margin = shorter / MARGIN_DIV;

    uint32_t *canvas = kmalloc(fb->width * fb->height * sizeof(uint32_t));
    if (canvas && !wallpaper_render(canvas, fb->width, fb->height)) {
        kfree(canvas);
        canvas = NULL;
    }
    if (canvas)
        darken_panel(canvas, fb->width, fb->height, margin);

    ft_ctx = flanterm_fb_init(
        terminal_malloc,
        terminal_free,
        fb->address, fb->width, fb->height, fb->pitch,
        fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift,
        fb->blue_mask_size, fb->blue_mask_shift,
        canvas,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        margin,
        FLANTERM_FB_ROTATE_0
    );

    kfree(canvas);
}

void terminal_putchar(char c) {
    serial_putchar(c);

    if (ft_ctx == NULL) {
        return;
    }

    if (c == 'N' || c == 'n') {
        flanterm_set_text_bg(ft_ctx, randomColor(), false);
    } else {
        flanterm_reset_text_bg(ft_ctx);
    }

    flanterm_set_text_fg(ft_ctx, 7, true);

    if (c == '\n') {
        flanterm_write(ft_ctx, "\r\n", 2);
        return;
    }

    flanterm_write(ft_ctx, &c, 1);
}
