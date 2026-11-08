#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

#include "terminal.h"
#include "random.h"
#include "wallpaper.h"
#include "heap.h"

static struct flanterm_context *ft_ctx = NULL;

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

    uint32_t *canvas = kmalloc(fb->width * fb->height * sizeof(uint32_t));
    if (canvas && !wallpaper_render(canvas, fb->width, fb->height)) {
        kfree(canvas);
        canvas = NULL;
    }

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
        0,
        FLANTERM_FB_ROTATE_0
    );

    kfree(canvas);
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
