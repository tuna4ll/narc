#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

/* xorshift32, fixed seed, not secure */
uint32_t krand(void);
uint32_t krand_seed(void);
bool krand_hardware(void);

#endif /* KERNEL_RANDOM_H */
