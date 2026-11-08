#include "libc.h"

int main(void) {
    for (int i = 0; i < 25; i++) {
        print("A");
        yield();
    }
    print("\na: done\n");
    return 0;
}
