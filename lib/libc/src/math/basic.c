#include <math.h>
#include <stdint.h>

double fabs(double value) {
    return __builtin_fabs(value);
}

long double fabsl(long double value) {
    return __builtin_fabsl(value);
}

long double modfl(long double value, long double *integral) {
    if (!isfinite(value)) {
        *integral = value;
        return isnan(value) ? value : 0.0L * value;
    }
    long double magnitude = fabsl(value);
    if (magnitude >= 18446744073709551616.0L) {
        *integral = value;
        return 0.0L * value;
    }
    long double whole = (long double)(uint64_t)magnitude;
    if (value < 0) whole = -whole;
    *integral = whole;
    return value - whole;
}

double modf(double value, double *integral) {
    long double whole;
    long double fraction = modfl(value, &whole);
    *integral = (double)whole;
    return (double)fraction;
}

long double floorl(long double value) {
    long double whole;
    long double fraction = modfl(value, &whole);
    return fraction < 0 ? whole - 1 : whole;
}

double floor(double value) {
    return (double)floorl(value);
}

double ceil(double value) {
    return -(double)floorl(-(long double)value);
}
