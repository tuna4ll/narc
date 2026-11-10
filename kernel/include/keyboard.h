#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

void keyboard_handle_irq(void);
bool keyboard_read_char(char *c);

#endif
