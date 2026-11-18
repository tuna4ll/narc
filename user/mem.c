#include "libc.h"

#define SIZE (3 * 4096)

static int fail(const char *why) {
    print("mem: fail ");
    print(why);
    print("\n");
    return 1;
}

int main(void) {
    char *p = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)p < 0)
        return fail("mmap");

    for (int i = 0; i < SIZE; i++)
        if (p[i] != 0)
            return fail("not zeroed");
    for (int i = 0; i < SIZE; i++)
        p[i] = (char)i;
    for (int i = 0; i < SIZE; i++)
        if (p[i] != (char)i)
            return fail("readback");

    if (munmap(p, SIZE) != 0)
        return fail("munmap");
    if (munmap(p + 1, 4096) >= 0 || munmap((void *)0x400000, 4096) >= 0)
        return fail("bad munmap accepted");

    char *q = mmap(0, 1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)q < 0 || q == p)
        return fail("second mmap");
    if ((long)mmap(0, 4096, PROT_READ, MAP_PRIVATE, -1, 0) >= 0)
        return fail("file mmap accepted");

    print("mem: ok\n");
    return 0;
}
