#include "libc.h"

int main(int argc, char **argv) {
    char buf[512];
    int status = 0;

    if (argc < 2) {
        print("usage: cat file...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i]);
        if (fd < 0) {
            print("cat: cannot open ");
            print(argv[i]);
            print("\n");
            status = 1;
            continue;
        }

        long n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(buf, n);
        close(fd);
    }
    return status;
}
