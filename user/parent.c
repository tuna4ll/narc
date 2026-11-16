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

    char buf[16];
    long fd = open("welcome.txt");
    check("open file", fd >= 3);
    check("read file", read(fd, buf, 7) == 7 && buf[0] == 'w' && buf[6] == 'e');
    check("close file", close(fd) == 0);
    check("close twice", close(fd) == -1);
    check("read closed", read(fd, buf, 1) == -1);
    check("open missing", open("nope") == -1);
    check("write read only", write(fd = open("/welcome.txt"), "x", 1) == -1 && close(fd) == 0);
    check("messy path", (fd = open("//dev//console/")) >= 0 && close(fd) == 0);
    check("bad fd", write(42, "x", 1) == -1);

    fd = open("/dev");
    check("readdir dev", readdir(fd, buf, sizeof(buf)) == 7 && buf[0] == 'c');
    check("readdir end", readdir(fd, buf, sizeof(buf)) == 0);
    check("read dir", read(fd, buf, 1) == -1);
    check("readdir file", readdir(0, buf, sizeof(buf)) == -1);
    close(fd);

    pid = spawn("/bin/hello");
    check("spawn by path", pid > 0 && wait(pid, &status) == pid && status == 0);

    check("chdir", chdir("/bin/../dev/.") == 0 && getcwd(buf, sizeof(buf)) == 4 && buf[1] == 'd');
    check("relative open", (fd = open("console")) >= 0 && close(fd) == 0);
    check("dotdot open", (fd = open("../welcome.txt")) >= 0 && close(fd) == 0);
    pid = spawn("hello");
    check("bare name from bin", pid > 0 && wait(pid, &status) == pid && status == 0);
    check("chdir file", chdir("/welcome.txt") == -1);
    check("chdir missing", chdir("nope") == -1);
    check("small getcwd", getcwd(buf, 2) == -1);
    check("dotdot past root", chdir("../../..") == 0 && getcwd(buf, sizeof(buf)) == 1 && buf[0] == '/');

    int seen = 0;
    fd = open("/");
    while (readdir(fd, buf, sizeof(buf)) > 0) {
        if (buf[0] == 'd' && buf[1] == 'e' && buf[2] == 'v' && buf[3] == '\0')
            seen |= 1;
        if (buf[0] == 'b' && buf[1] == 'i' && buf[2] == 'n' && buf[3] == '\0')
            seen |= 2;
    }
    close(fd);
    check("root lists bin and dev", seen == 3);
    check("bin is a dir", (fd = open("/bin")) >= 0 && read(fd, buf, 1) == -1 && close(fd) == 0);

    check("missing program", spawn("nope") == -1);
    check("kernel pointer", spawn((const char *)0xFFFFFFFF80000000) == -1);
    check("unmapped write", write(1, (const char *)0x10, 1) == -1);

    sleep(200);
    print("parent: woke up\n");
    return 0;
}
