#include "libc.h"

int main(void) {
    print("cli: turning off interrupts...\n");
    __asm__ volatile ("cli");
    print("cli: interrupts are off lol\n");
    return 0;
}
