#include <stdint.h>
#include <stdlib.h>

static void swap(unsigned char *a, unsigned char *b, size_t size) {
    while (size--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static void sift(unsigned char *base, size_t root, size_t count, size_t size,
                 int (*compare)(const void *, const void *)) {
    for (;;) {
        size_t child = root * 2 + 1;
        if (child >= count) return;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (compare(base + root * size, base + child * size) >= 0) return;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

static void insertion(unsigned char *base, size_t count, size_t size,
                      int (*compare)(const void *, const void *)) {
    for (size_t i = 1; i < count; i++)
        for (size_t j = i; j && compare(base + (j - 1) * size, base + j * size) > 0; j--)
            swap(base + (j - 1) * size, base + j * size, size);
}

void qsort(void *pointer, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    unsigned char *base = pointer;
    if (count < 2 || !size) return;
    if (count <= 16) {
        insertion(base, count, size, compare);
        return;
    }
    for (size_t i = count / 2; i--;) sift(base, i, count, size, compare);
    for (size_t end = count - 1; end; end--) {
        swap(base, base + end * size, size);
        sift(base, 0, end, size, compare);
    }
}

void *bsearch(const void *key, const void *pointer, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    const unsigned char *base = pointer;
    while (count) {
        const unsigned char *middle = base + (count / 2) * size;
        int order = compare(key, middle);
        if (!order) return (void *)middle;
        if (order > 0) {
            base = middle + size;
            count -= count / 2 + 1;
        } else {
            count /= 2;
        }
    }
    return 0;
}
