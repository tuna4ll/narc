#pragma once
#include <stdint.h>

void ioapic_route_irq(unsigned irq, uint8_t vector);
