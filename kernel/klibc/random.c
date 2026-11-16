#include <stdbool.h>

#include "random.h"
#include "rtc.h"

/* never 0 */
static uint32_t random_state = 123456789;
static bool has_rdrand;

static bool rdrand32(uint32_t *out) {
    for (int i = 0; i < 10; i++) {
        uint32_t v;
        bool ok;

        __asm__ volatile ("rdrand %0" : "=r"(v), "=@ccc"(ok));
        if (ok) {
            *out = v;
            return true;
        }
    }
    return false;
}

static bool rdrand_works(void) {
    uint32_t eax = 1, ebx, ecx = 0, edx;

    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (!(ecx & (1u << 30)))
        return false;

    uint32_t a, b, c;
    if (!rdrand32(&a) || !rdrand32(&b) || !rdrand32(&c))
        return false;

    return !(a == b && b == c);
}

static uint32_t xorshift(void) {
    uint32_t x = random_state;

    /* marsaglia 13/17/5 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    random_state = x;

    return x;
}

uint32_t krand(void) {
    uint32_t v;

    if (has_rdrand && rdrand32(&v))
        return v;
    return xorshift();
}

uint32_t krand_seed(void) {
    struct rtc_time t;
    uint32_t lo, hi;

    has_rdrand = rdrand_works();

    uint32_t x;
    if (!has_rdrand || !rdrand32(&x)) {
        rtc_read(&t);
        __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));

        x = lo ^ hi ^ t.second ^ (t.minute << 6) ^ (t.hour << 12) ^
            (t.day << 17) ^ (t.month << 22) ^ ((uint32_t)t.year << 26);

        x ^= x >> 16;
        x *= 0x85EBCA6B;
        x ^= x >> 13;
        x *= 0xC2B2AE35;
        x ^= x >> 16;
    }

    random_state = x ? x : 123456789;
    return random_state;
}

bool krand_hardware(void) {
    return has_rdrand;
}
