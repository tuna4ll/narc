#include <stdbool.h>

#include "io.h"
#include "serial.h"

#define COM1 0x3F8
#define REG_DATA (COM1 + 0)
#define REG_IER (COM1 + 1)
#define REG_DIVISOR_HIGH (COM1 + 1)
#define REG_FIFO (COM1 + 2)
#define REG_LCR (COM1 + 3)
#define REG_MCR (COM1 + 4)
#define REG_LSR (COM1 + 5)

#define LCR_DLAB 0x80
#define LCR_8N1 0x03
#define FIFO_ENABLE_CLEAR 0xC7
#define MCR_LOOPBACK 0x1E
#define MCR_NORMAL 0x0F
#define LSR_TX_EMPTY 0x20
#define LOOPBACK_BYTE 0xAE

static bool present = false;

void serial_init(void) {
    outb(REG_IER, 0);
    outb(REG_LCR, LCR_DLAB);
    outb(REG_DATA, 1);
    outb(REG_DIVISOR_HIGH, 0);
    outb(REG_LCR, LCR_8N1);
    outb(REG_FIFO, FIFO_ENABLE_CLEAR);

    outb(REG_MCR, MCR_LOOPBACK);
    outb(REG_DATA, LOOPBACK_BYTE);
    if (inb(REG_DATA) != LOOPBACK_BYTE)
        return;

    outb(REG_MCR, MCR_NORMAL);
    present = true;
}

void serial_putchar(char c) {
    if (!present)
        return;

    if (c == '\n')
        serial_putchar('\r');

    while (!(inb(REG_LSR) & LSR_TX_EMPTY)) {
    }
    outb(REG_DATA, c);
}
