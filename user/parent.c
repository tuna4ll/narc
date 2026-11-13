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

    pid = spawn("div");
    check("wait div", pid > 0 && wait(pid, &status) == pid && status == -8);

    pid = spawn("ud");
    check("wait ud", pid > 0 && wait(pid, &status) == pid && status == -4);

    check("bad read buffer", read(0, (char *)0x400000, 1) == -1);

    pid = spawn("echo  args   are   split");
    check("spawn with args", pid > 0 && wait(pid, &status) == pid && status == 0);
    check("blank line", spawn("   ") == -1);

    char buf[8];
    long fd = open("welcome.txt");
    check("open file", fd >= 3);
    check("read file", read(fd, buf, 7) == 7 && buf[0] == 'w' && buf[6] == 'e');
    check("close file", close(fd) == 0);
    check("close twice", close(fd) == -1);
    check("read closed", read(fd, buf, 1) == -1);
    check("open missing", open("nope") == -1);
    check("readdir end", readdir(1000, buf, sizeof(buf)) == 0);

    check("missing program", spawn("nope") == -1);
    check("kernel pointer", spawn((const char *)0xFFFFFFFF80000000) == -1);
    check("unmapped write", write((const char *)0x10, 1) == -1);

    sleep(200);
    print("parent: woke up\n");
    return 0;
}
