#include <stdint.h>
#include "pic.h"
#include "io.h"

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01

void pic_remap(void) {
    //restart
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    //start
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
}   

void pic_send_eoi(uint8_t irq) {

    //Slave PIC
    if(irq >= 8){
        outb(PIC2_COMMAND, 0x20);
    }
    //Master PIC
    outb(PIC1_COMMAND,0x20);
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port;

    if(irq >= 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t mask = inb(port);
    mask &= ~(1u << irq);
    outb(port, mask);
}

void pic_set_mask(uint8_t irq) {
    if (irq >= 16) {
        return;
    }

    uint16_t port;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t mask = inb(port);
    mask |= (1u << irq);
    outb(port, mask);
}