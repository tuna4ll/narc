#include "libc.h"

int main(void) {
    for (int i = 0; i < 25; i++) {
        print("B");
        yield();
    }
    print("\nb: done\n");
    return 0;
}
