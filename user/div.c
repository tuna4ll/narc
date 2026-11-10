#include "libc.h"

int main(void) {
    // gcc skips idiv when the top is a constant one
    volatile int one = 1;
    volatile int zero = 0;
    print("div: dividing by zero...\n");
    return one / zero;
}
