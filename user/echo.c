#include "libc.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            print(" ");
        print(argv[i]);
    }
    print("\n");
    return 0;
}
