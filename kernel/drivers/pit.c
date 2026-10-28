#include "pit.h"
#include "io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

void pit_init(uint32_t frequency) {
    if (frequency < 19 || frequency > PIT_BASE_FREQ)
        return;

    uint32_t divisor = PIT_BASE_FREQ / frequency;

    if (divisor == 0 || divisor > 65535)
        return;

    outb(PIT_COMMAND, 0x36);

    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}