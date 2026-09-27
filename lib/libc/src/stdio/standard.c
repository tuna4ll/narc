#include "internal.h"

static FILE standard_streams[3] = {
    { 0, FILE_READ },
    { 1, FILE_WRITE },
    { 2, FILE_WRITE },
};

FILE *stdin = &standard_streams[0];
FILE *stdout = &standard_streams[1];
FILE *stderr = &standard_streams[2];
