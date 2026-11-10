#include "libc.h"

// same as the name limit in the kernel
#define LINE_LEN 32

static void read_line(char *line) {
    int n = 0;

    for (;;) {
        char c;
        read(&c, 1);

        if (c == '\n') {
            print("\n");
            line[n] = '\0';
            return;
        }

        if (c == '\b') {
            if (n > 0) {
                n--;
                print("\b \b");
            }
        } else if (c >= ' ' && n < LINE_LEN - 1) {
            line[n++] = c;
            write(&c, 1);
        }
    }
}

int main(void) {
    char line[LINE_LEN];

    for (;;) {
        print("narc> ");
        read_line(line);

        if (line[0] == '\0')
            continue;

        long pid = spawn(line);
        if (pid < 0) {
            print("sh: cannot run ");
            print(line);
            print("\n");
            continue;
        }

        wait(pid, NULL);
    }
}
