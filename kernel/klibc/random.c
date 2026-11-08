#include "random.h"

/* never 0 */
static uint32_t random_state = 123456789;

uint32_t krand(void) {
    uint32_t x = random_state;

    /* marsaglia 13/17/5 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    random_state = x;

    return x;
}
