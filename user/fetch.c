#include "libc.h"

// don narcito, pixel-art style
static const char *art[] = {
    "            /\\           ",
    "           /**\\          ",
    "          /****\\         ",
    "     __  /******\\        ",
    "    /\\  \\/  ||   \\       ",
    "   /  \\     ||    \\      ",
    "  /    \\    ||     \\     ",
    "  \\     \\   ||    _/     ",
    "   \\_    \\__||___/       ",
    "     \\____/  \\___\\       ",
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const char *info[] = {
        "OS:       narc",
        "Kernel:   narc x86_64",
        "Arch:     x86_64",
        "Shell:    sh",
        "WM:       none (murdered)",
        "Theme:    Don Narcito [Red]",
        "Uptime:   long enough",
        "CPU:      whatever QEMU says",
    };

    for (int i = 0; i < (int)(sizeof(art) / sizeof(art[0])); i++) {
        print(art[i]);
        if (i < (int)(sizeof(info) / sizeof(info[0]))) {
            print("   ");
            print(info[i]);
        }
        print("\n");
    }

    return 0;
}
