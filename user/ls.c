#include "libc.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    char name[128];

    long fd = open(path);
    if (fd < 0) {
        print("ls: cannot open ");
        print(path);
        print("\n");
        return 1;
    }

    long n;
    while ((n = readdir(fd, name, sizeof(name))) > 0) {
        print(name);
        print("\n");
    }
    close(fd);
    return n < 0 ? 1 : 0;
}
