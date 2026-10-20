#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/ioapic.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define STATUS_OUTPUT 0x01
#define STATUS_INPUT 0x02
#define STATUS_AUX 0x20
#define CONFIG_IRQ1 0x01
#define CONFIG_IRQ12 0x02
#define CONFIG_CLOCK1 0x10
#define CONFIG_TRANSLATE 0x40
#define KEYBOARD_IRQ 1
#define KEYBOARD_VECTOR 37
#define SPIN_LIMIT 100000

static const uint8_t extended[128] = {
    [0x1c] = 96, [0x1d] = 97, [0x35] = 98, [0x38] = 100, [0x47] = 102, [0x48] = 103,
    [0x49] = 104, [0x4b] = 105, [0x4d] = 106, [0x4f] = 107, [0x50] = 108, [0x51] = 109,
    [0x52] = 110, [0x53] = 111, [0x5b] = 125, [0x5c] = 126, [0x5d] = 127,
};

static int prefix, skip;
static int led_pending = -1;

static int wait_input_clear(void) {
    for (unsigned i = 0; i < SPIN_LIMIT; i++)
        if (!(inb(PS2_STATUS) & STATUS_INPUT)) return 0;
    return -1;
}

static int wait_output_full(void) {
    for (unsigned i = 0; i < SPIN_LIMIT; i++)
        if (inb(PS2_STATUS) & STATUS_OUTPUT) return 0;
    return -1;
}

static void command(uint8_t value) {
    wait_input_clear();
    outb(PS2_COMMAND, value);
}

static void write_data(uint8_t value) {
    wait_input_clear();
    outb(PS2_DATA, value);
}

static void set_leds(unsigned leds) {
    led_pending = (int)(leds & 7);
    write_data(0xed);
}

static void receive(uint8_t byte) {
    if (skip) {
        skip--;
        return;
    }
    if (byte == 0xfa) {
        if (led_pending >= 0) write_data((uint8_t)led_pending);
        led_pending = -1;
        return;
    }
    if (byte == 0xfe || byte == 0x00 || byte == 0xff) return;
    if (byte == 0xe0) {
        prefix = 1;
        return;
    }
    if (byte == 0xe1) {
        skip = 5;
        return;
    }
    uint8_t code = byte & 0x7f;
    int pressed = !(byte & 0x80);
    if (prefix) {
        prefix = 0;
        code = extended[code];
        if (!code) return;
    }
    keyboard_event(code, pressed);
}

static void keyboard_dispatch(struct task_frame *frame) {
    (void)frame;
    uint8_t status;
    while ((status = inb(PS2_STATUS)) & STATUS_OUTPUT) {
        uint8_t byte = inb(PS2_DATA);
        if (!(status & STATUS_AUX)) receive(byte);
    }
    lapic_eoi();
}

void arch_keyboard_init(void) {
    command(0xad);
    command(0xa7);
    for (unsigned i = 0; i < 64 && (inb(PS2_STATUS) & STATUS_OUTPUT); i++) inb(PS2_DATA);
    command(0x20);
    if (wait_output_full() != 0) return;
    uint8_t config = inb(PS2_DATA);
    config |= CONFIG_IRQ1 | CONFIG_TRANSLATE;
    config &= (uint8_t)~(CONFIG_IRQ12 | CONFIG_CLOCK1);
    command(0x60);
    write_data(config);
    command(0xae);
    idt_set_handler(KEYBOARD_VECTOR, keyboard_dispatch);
    ioapic_route_irq(KEYBOARD_IRQ, KEYBOARD_VECTOR);
    keyboard_set_led_handler(set_leds);
}
