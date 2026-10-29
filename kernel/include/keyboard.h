#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_handle_irq(void);
uint8_t keyboard_get_last_scancode(void);

#endif