#pragma once
#include <stdint.h>

void timer_init(void);
void lapic_eoi(void);
uint8_t lapic_id(void);
