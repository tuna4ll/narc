#include "libc.h"

static void check(const char *name, int ok) {
    print("parent: ");
    print(name);
    print(ok ? " ok\n" : " FAILED\n");
}

int main(void) {
    int status = 1;
    long pid = spawn("hello");
    check("wait hello", pid > 0 && wait(pid, &status) == pid && status == 0);

    status = 0;
    pid = spawn("crash");
    check("bad status pointer", pid > 0 && wait(pid, (int *)0x400000) == -1);
    check("wait crash", wait(pid, &status) == pid && status == -11);
    check("reaped child", wait(pid, &status) == -1);

    check("missing program", spawn("nope") == -1);
    check("kernel pointer", spawn((const char *)0xFFFFFFFF80000000) == -1);
    check("unmapped write", write((const char *)0x10, 1) == -1);

    sleep(200);
    print("parent: woke up\n");
    return 0;
}
