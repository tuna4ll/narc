#include "internal.h"
#include <stdlib.h>
#include <unistd.h>

int closedir(DIR *directory) {
    int result = close(directory->descriptor);
    free(directory);
    return result;
}
