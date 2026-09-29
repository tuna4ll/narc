#include "alloc.h"
#include <stdlib.h>
#include <string.h>

void *realloc(void *pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) {
        free(pointer);
        return 0;
    }

    size_t old_size = __allocator_size(pointer);
    if (__allocator_resize(pointer, size) == 0) return pointer;

    void *replacement = malloc(size);
    if (!replacement) return 0;
    memcpy(replacement, pointer, old_size);
    free(pointer);
    return replacement;
}
