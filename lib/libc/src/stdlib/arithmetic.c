#include <inttypes.h>
#include <stdlib.h>

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }
intmax_t imaxabs(intmax_t value) { return value < 0 ? -value : value; }

div_t div(int numerator, int denominator) {
    return (div_t) { numerator / denominator, numerator % denominator };
}

ldiv_t ldiv(long numerator, long denominator) {
    return (ldiv_t) { numerator / denominator, numerator % denominator };
}

static unsigned long long state = 1;

int rand(void) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)(state >> 33);
}

void srand(unsigned int seed) {
    state = seed ? seed : 1;
}
