#include <stddef.h>
#include <stdint.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

#include "terminal.h"
#include "random.h"

static struct flanterm_context *ft_ctx = NULL;

static size_t randomColor(void) {
    return krand() % 8;
}

void terminal_init(struct limine_framebuffer *fb) {
    ft_ctx = flanterm_fb_init(
        NULL, NULL,
        fb->address, fb->width, fb->height, fb->pitch,
        fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift,
        fb->blue_mask_size, fb->blue_mask_shift,
        NULL,
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

    /* boşluklar siyah kalsın, gerisi rastgele zemin */
    if (c == 'N' || c == 'n') {
        flanterm_set_text_bg(ft_ctx, randomColor(), false);
    } else {
        flanterm_set_text_bg(ft_ctx, 0, false);
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
