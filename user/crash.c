#include "libc.h"

int main(void) {
    print("crash: reading kernel memory...\n");
    volatile char *kernel = (volatile char *)0xFFFFFFFF80000000;
    char c = *kernel;
    print("crash: I read the kernel lol\n");
    return c;
}
