#include "libc.h"

int main(void) {
    char name[128];

    for (long i = 0; readdir(i, name, sizeof(name)) > 0; i++) {
        print(name);
        print("\n");
    }
    return 0;
}
