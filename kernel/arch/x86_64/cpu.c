#include "cpu.h"

__attribute__((noreturn))
void khalt(void) {
    /* NMI hlt'den uyandırabilir, o yüzden döngü */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
