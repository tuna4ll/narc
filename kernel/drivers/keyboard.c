#include <stdint.h>
#include "io.h"
#include "pic.h"
#include "keyboard.h"
static volatile uint8_t last_scancode = 0;

void keyboard_handle_irq(void) {
    last_scancode = inb(0x60);
    
    pic_send_eoi(1);
}

uint8_t keyboard_get_last_scancode(void) {
    return last_scancode;
}

