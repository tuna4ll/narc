#include "cpu.h"

__attribute__((noreturn))
void khalt(void) {
    /* nmi can wake hlt */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
