#include "libc.h"

int main(void) {
    for (int i = 0; i < 5; i++) {
        print("N");
        for (volatile long j = 0; j < 20000000; j++) {
        }
    }
    print("\nn: done\n");
    return 0;
}
