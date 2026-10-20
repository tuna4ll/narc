#pragma once
#include <stdint.h>

#define KEYBOARD_LED_SCROLL 1u
#define KEYBOARD_LED_NUM    2u
#define KEYBOARD_LED_CAPS   4u

void keyboard_init(void);
void keyboard_event(uint16_t code, int pressed);
void keyboard_set_led_handler(void (*handler)(unsigned leds));
void keyboard_add_poller(void (*poll)(void *data), void *data);
void keyboard_poll(void);
void arch_keyboard_init(void);
