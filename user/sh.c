#include "libc.h"

// must stay under the kernel args limit
#define LINE_LEN 128

static void read_line(char *line) {
    int n = 0;

    for (;;) {
        char c;
        read(0, &c, 1);

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
            write(1, &c, 1);
        }
    }
}

// returns the args when the first word of line is name
static const char *builtin(const char *line, const char *name) {
    while (*name != '\0')
        if (*line++ != *name++)
            return NULL;
    if (*line != ' ' && *line != '\0')
        return NULL;

    while (*line == ' ')
        line++;
    return line;
}

static int run_builtin(const char *line) {
    const char *args;

    if ((args = builtin(line, "cd")) != NULL) {
        const char *path = *args != '\0' ? args : "/";
        if (chdir(path) < 0) {
            print("cd: cannot enter ");
            print(path);
            print("\n");
        }
        return 1;
    }

    if (builtin(line, "pwd") != NULL) {
        char dir[256];
        if (getcwd(dir, sizeof(dir)) >= 0) {
            print(dir);
            print("\n");
        }
        return 1;
    }
    return 0;
}

int main(void) {
    char line[LINE_LEN];

    for (;;) {
        print("narc> ");
        read_line(line);

        const char *cmd = line;
        while (*cmd == ' ')
            cmd++;

        if (*cmd == '\0' || run_builtin(cmd))
            continue;

        long pid = spawn(cmd);
        if (pid < 0) {
            print("sh: cannot run ");
            print(cmd);
            print("\n");
            continue;
        }

        wait(pid, NULL);
    }
}
