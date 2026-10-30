#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

#include "terminal.h"
#include "random.h"
#include "wallpaper.h"

static struct flanterm_context *ft_ctx = NULL;

static size_t randomColor(void) {
    return krand() % 8;
}

#define CANVAS_MAX_WIDTH  1920
#define CANVAS_MAX_HEIGHT 1080
#define TERMINAL_ARENA_SIZE (10 * 1024 * 1024)

static uint32_t canvas[CANVAS_MAX_WIDTH * CANVAS_MAX_HEIGHT];

static uint8_t terminal_arena[TERMINAL_ARENA_SIZE];
static size_t terminal_arena_used = 0;

static void *terminal_malloc(size_t size) {
    size = (size + 15) & ~(size_t)15;

    if (size > TERMINAL_ARENA_SIZE - terminal_arena_used)
        return NULL;

    void *ptr = &terminal_arena[terminal_arena_used];
    terminal_arena_used += size;
    return ptr;
}

static void terminal_free(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
}

void terminal_init(const struct boot_framebuffer *fb) {
    if (fb == NULL || fb->width == 0 || fb->height == 0)
        return;

    uint32_t *bg = NULL;
    if (fb->width <= CANVAS_MAX_WIDTH && fb->height <= CANVAS_MAX_HEIGHT) {
        if (wallpaper_render(canvas, fb->width, fb->height))
            bg = canvas;
    }

    ft_ctx = flanterm_fb_init(
        terminal_malloc,
        terminal_free,
        fb->address, fb->width, fb->height, fb->pitch,
        fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift,
        fb->blue_mask_size, fb->blue_mask_shift,
        bg,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0,
        FLANTERM_FB_ROTATE_0
    );
}

void terminal_putchar(char c) {
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

void kprint(const char *c) {
    for (size_t i = 0; c[i] != '\0'; i++) {
        terminal_putchar(c[i]);
    }
}
