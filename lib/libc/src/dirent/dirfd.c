#include "internal.h"

int dirfd(DIR *directory) {
    return directory->descriptor;
}
