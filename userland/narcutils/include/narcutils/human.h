#pragma once

#include <narcutils/xstrtol.h>
#include <stdint.h>

enum {
    human_ceiling = 0,
    human_round_to_nearest = 1,
    human_floor = 2,
    human_group_digits = 4,
    human_suppress_point_zero = 8,
    human_autoscale = 16,
    human_base_1024 = 32,
    human_space_before_unit = 64,
    human_SI = 128,
    human_B = 256,
};

#define LONGEST_HUMAN_READABLE 652

char *human_readable(uintmax_t n, char *buffer, int options, uintmax_t from_block_size,
                     uintmax_t to_block_size);
strtol_error human_options(const char *spec, int *options, uintmax_t *block_size);
