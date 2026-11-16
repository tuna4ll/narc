#include "libc.h"

int main(void) {
    // CSI 2J = erase screen, CSI H = cursor home
    print("\033[2J\033[H");
    return 0;
}
