#include <kernel/keyboard.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/tty.h>

#define KEY_ESC 1
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_KPASTERISK 55
#define KEY_LEFTALT 56
#define KEY_CAPSLOCK 58
#define KEY_F1 59
#define KEY_F10 68
#define KEY_NUMLOCK 69
#define KEY_SCROLLLOCK 70
#define KEY_KP7 71
#define KEY_KPDOT 83
#define KEY_F11 87
#define KEY_F12 88
#define KEY_KPENTER 96
#define KEY_RIGHTCTRL 97
#define KEY_KPSLASH 98
#define KEY_RIGHTALT 100
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111

#define MOD_SHIFT 1u
#define MOD_ALT   2u
#define MOD_CTRL  4u
#define MAX_POLLERS 4

struct key_row {
    uint8_t first;
    const char *normal;
    const char *shifted;
};

static const struct key_row rows[] = {
    { 2, "1234567890-=", "!@#$%^&*()_+" },
    { 16, "qwertyuiop[]", "QWERTYUIOP{}" },
    { 30, "asdfghjkl;'`", "ASDFGHJKL:\"~" },
    { 43, "\\zxcvbnm,./", "|ZXCVBNM<>?" },
    { 57, " ", " " },
    { 86, "<", ">" },
};

static const char keypad_digits[] = "789-456+1230.";
static const uint8_t keypad_keys[] = {
    KEY_HOME, KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0,
    KEY_END, KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE,
};

static struct spinlock lock;
static unsigned shift_keys, ctrl_keys, alt_keys;
static unsigned leds = KEYBOARD_LED_NUM;
static void (*led_handler)(unsigned leds);
static struct {
    void (*poll)(void *data);
    void *data;
} pollers[MAX_POLLERS];
static unsigned poller_count;

static void emit(const char *text) {
    while (*text) tty_input(*text++);
}

static void emit_number(unsigned value) {
    char digits[4];
    unsigned count = 0;
    do digits[count++] = (char)('0' + value % 10); while ((value /= 10) && count < sizeof(digits));
    while (count) tty_input(digits[--count]);
}

static unsigned modifiers(void) {
    return (shift_keys ? MOD_SHIFT : 0) | (alt_keys ? MOD_ALT : 0) | (ctrl_keys ? MOD_CTRL : 0);
}

static void emit_csi(unsigned number, char final) {
    unsigned mods = modifiers();
    emit("\033[");
    if (number || mods) emit_number(number ? number : 1);
    if (mods) {
        tty_input(';');
        emit_number(mods + 1);
    }
    tty_input(final);
}

static int emit_special(uint16_t code) {
    static const uint8_t function_numbers[] = { 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24 };
    switch (code) {
    case KEY_UP: emit_csi(0, 'A'); return 1;
    case KEY_DOWN: emit_csi(0, 'B'); return 1;
    case KEY_RIGHT: emit_csi(0, 'C'); return 1;
    case KEY_LEFT: emit_csi(0, 'D'); return 1;
    case KEY_HOME: emit_csi(0, 'H'); return 1;
    case KEY_END: emit_csi(0, 'F'); return 1;
    case KEY_INSERT: emit_csi(2, '~'); return 1;
    case KEY_DELETE: emit_csi(3, '~'); return 1;
    case KEY_PAGEUP: emit_csi(5, '~'); return 1;
    case KEY_PAGEDOWN: emit_csi(6, '~'); return 1;
    }
    unsigned index;
    if (code >= KEY_F1 && code <= KEY_F10) index = code - KEY_F1;
    else if (code == KEY_F11 || code == KEY_F12) index = 10 + code - KEY_F11;
    else return 0;
    if (index < 4 && !modifiers()) {
        char sequence[] = { '\033', 'O', (char)('P' + index), 0 };
        emit(sequence);
    } else if (index < 4) {
        emit_csi(1, (char)('P' + index));
    } else {
        emit_csi(function_numbers[index], '~');
    }
    return 1;
}

static int printable(uint16_t code, int shifted, char *out) {
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const struct key_row *row = &rows[i];
        if (code < row->first || (size_t)(code - row->first) >= strlen(row->normal)) continue;
        unsigned offset = code - row->first;
        *out = shifted ? row->shifted[offset] : row->normal[offset];
        return 1;
    }
    return 0;
}

static int translate(uint16_t code, char *out) {
    int shifted = shift_keys != 0;
    switch (code) {
    case KEY_ESC: *out = '\033'; return 1;
    case KEY_BACKSPACE: *out = ctrl_keys ? '\b' : 0x7f; return 1;
    case KEY_TAB: *out = '\t'; return 1;
    case KEY_ENTER: case KEY_KPENTER: *out = '\r'; return 1;
    case KEY_KPASTERISK: *out = '*'; return 1;
    case KEY_KPSLASH: *out = '/'; return 1;
    }
    if (!printable(code, shifted, out)) return 0;
    if ((leds & KEYBOARD_LED_CAPS) && *out >= 'a' && *out <= 'z') *out = (char)(*out - 32);
    else if ((leds & KEYBOARD_LED_CAPS) && *out >= 'A' && *out <= 'Z') *out = (char)(*out + 32);
    return 1;
}

static char control(char c) {
    if (c >= '@' && c <= '~') return (char)(c & 0x1f);
    if (c == ' ' || c == '2') return 0;
    if (c == '6') return 0x1e;
    if (c == '/' || c == '-') return 0x1f;
    return c;
}

static void toggle_led(unsigned led) {
    leds ^= led;
    if (led_handler) led_handler(leds);
}

static int modifier(uint16_t code, int pressed) {
    unsigned *keys;
    unsigned bit;
    switch (code) {
    case KEY_LEFTSHIFT: keys = &shift_keys; bit = 1; break;
    case KEY_RIGHTSHIFT: keys = &shift_keys; bit = 2; break;
    case KEY_LEFTCTRL: keys = &ctrl_keys; bit = 1; break;
    case KEY_RIGHTCTRL: keys = &ctrl_keys; bit = 2; break;
    case KEY_LEFTALT: keys = &alt_keys; bit = 1; break;
    case KEY_RIGHTALT: keys = &alt_keys; bit = 2; break;
    default: return 0;
    }
    if (pressed) *keys |= bit;
    else *keys &= ~bit;
    return 1;
}

static void key_down(uint16_t code) {
    switch (code) {
    case KEY_CAPSLOCK: toggle_led(KEYBOARD_LED_CAPS); return;
    case KEY_NUMLOCK: toggle_led(KEYBOARD_LED_NUM); return;
    case KEY_SCROLLLOCK: toggle_led(KEYBOARD_LED_SCROLL); return;
    }
    if (code >= KEY_KP7 && code <= KEY_KPDOT) {
        unsigned index = code - KEY_KP7;
        char digit = keypad_digits[index];
        int navigation = !(leds & KEYBOARD_LED_NUM) || shift_keys;
        if (digit == '-' || digit == '+' || !navigation) {
            tty_input(digit);
            return;
        }
        if (!keypad_keys[index]) return;
        code = keypad_keys[index];
    }
    if (emit_special(code)) return;
    char c;
    if (!translate(code, &c)) return;
    if (ctrl_keys) c = control(c);
    if (alt_keys) tty_input('\033');
    tty_input(c);
}

void keyboard_event(uint16_t code, int pressed) {
    unsigned long flags = spin_lock_irqsave(&lock);
    if (!modifier(code, pressed) && pressed) key_down(code);
    spin_unlock_irqrestore(&lock, flags);
}

void keyboard_set_led_handler(void (*handler)(unsigned leds)) {
    led_handler = handler;
    if (handler) handler(leds);
}

void keyboard_add_poller(void (*poll)(void *data), void *data) {
    if (poller_count == MAX_POLLERS) return;
    pollers[poller_count].data = data;
    pollers[poller_count].poll = poll;
    __atomic_store_n(&poller_count, poller_count + 1, __ATOMIC_RELEASE);
}

void keyboard_poll(void) {
    unsigned count = __atomic_load_n(&poller_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count; i++) pollers[i].poll(pollers[i].data);
}

void keyboard_init(void) {
    spin_init(&lock);
    arch_keyboard_init();
}
