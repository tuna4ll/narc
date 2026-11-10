#include <stdbool.h>
#include <stdint.h>
#include "io.h"
#include "pic.h"
#include "keyboard.h"
#include "process.h"

#define BUFFER_SIZE 64
#define SCANCODE_EXTENDED 0xE0
#define SCANCODE_RELEASED 0x80
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36

// us layout indexed by set 1 scancode
static const char keymap[] =
    "\0\0" "1234567890-=\b\t" "qwertyuiop[]\n\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
static const char keymap_shift[] =
    "\0\0" "!@#$%^&*()_+\b\t" "QWERTYUIOP{}\n\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 ";

static char buffer[BUFFER_SIZE];
static uint32_t head = 0;
static uint32_t tail = 0;
static bool shift = false;
static bool extended = false;

static void handle_scancode(uint8_t code) {
    if (code == SCANCODE_EXTENDED) {
        extended = true;
        return;
    }

    // arrows and friends would look like keypad digits
    if (extended) {
        extended = false;
        return;
    }

    bool released = code & SCANCODE_RELEASED;
    code &= ~SCANCODE_RELEASED;

    if (code == SCANCODE_LSHIFT || code == SCANCODE_RSHIFT) {
        shift = !released;
        return;
    }

    if (released || code >= sizeof(keymap) - 1)
        return;

    char c = shift ? keymap_shift[code] : keymap[code];
    if (c == '\0' || head - tail == BUFFER_SIZE)
        return;

    buffer[head++ % BUFFER_SIZE] = c;
    process_wake_readers();
}

void keyboard_handle_irq(void) {
    handle_scancode(inb(0x60));

    pic_send_eoi(1);
}

bool keyboard_read_char(char *c) {
    if (head == tail)
        return false;

    *c = buffer[tail++ % BUFFER_SIZE];
    return true;
}
