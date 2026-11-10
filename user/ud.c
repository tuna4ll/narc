#include "libc.h"

int main(void) {
    print("ud: running a bad opcode...\n");
    __asm__ volatile ("ud2");
    return 0;
}
