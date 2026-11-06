#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <stdint.h>

/* xorshift32, fixed seed, not secure */
uint32_t krand(void);

#endif /* KERNEL_RANDOM_H */
