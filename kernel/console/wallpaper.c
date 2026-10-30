#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wallpaper.h"

__asm__(".section .rodata\n"
        "wp_start:\n"
        ".incbin \"assets/narcito_whitehouse.rgb\"\n"
        "wp_end:\n"
        ".previous\n");

extern uint8_t wp_start[];
extern uint8_t wp_end[];

#define IMG_W 700
#define IMG_H 394

bool wallpaper_render(uint32_t *canvas, size_t w, size_t h) {
    if ((size_t)(wp_end - wp_start) != IMG_W * IMG_H * 3)
        return false;

    size_t sw, sh;
    if (IMG_W * h > IMG_H * w) {
        sh = IMG_H;
        sw = w * IMG_H / h;
    } else {
        sw = IMG_W;
        sh = h * IMG_W / w;
    }

    size_t ox = (IMG_W - sw) / 2;
    size_t oy = (IMG_H - sh) / 2;

    for (size_t y = 0; y < h; y++) {
        size_t fy = y * sh * 256 / h;
        size_t y0 = oy + fy / 256;
        size_t y1 = y0 + 1;
        if (y1 >= IMG_H) y1 = IMG_H - 1;
        uint32_t wy = fy % 256;

        for (size_t x = 0; x < w; x++) {
            size_t fx = x * sw * 256 / w;
            size_t x0 = ox + fx / 256;
            size_t x1 = x0 + 1;
            if (x1 >= IMG_W) x1 = IMG_W - 1;
            uint32_t wx = fx % 256;

            uint8_t *a = &wp_start[(y0 * IMG_W + x0) * 3];
            uint8_t *b = &wp_start[(y0 * IMG_W + x1) * 3];
            uint8_t *c = &wp_start[(y1 * IMG_W + x0) * 3];
            uint8_t *d = &wp_start[(y1 * IMG_W + x1) * 3];

            uint32_t px = 0;
            for (int i = 0; i < 3; i++) {
                uint32_t top = (a[i] * (256 - wx) + b[i] * wx) / 256;
                uint32_t bot = (c[i] * (256 - wx) + d[i] * wx) / 256;
                uint32_t v = (top * (256 - wy) + bot * wy) / 256;
                v = v * 6 / 10;
                px = (px << 8) | v;
            }

            canvas[y * w + x] = px;
        }
    }

    return true;
}
