#pragma once

#include <unistd.h>

#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

int getopt_long(int argc, char *const argv[], const char *options,
                const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char *const argv[], const char *options,
                     const struct option *longopts, int *longindex);
