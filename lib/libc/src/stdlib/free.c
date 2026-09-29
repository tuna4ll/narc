#include "alloc.h"
#include <stdlib.h>

void free(void *pointer) {
    __allocator_release(pointer);
}
