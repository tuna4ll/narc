#include "alloc.h"
#include <stdlib.h>

void *malloc(size_t size) {
    return __allocator_allocate(size);
}
