#pragma once

#define HUGE_VAL  __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define INFINITY  __builtin_inff()
#define NAN       __builtin_nanf("")

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf_sign(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)

double fabs(double value);
long double fabsl(long double value);
double floor(double value);
double ceil(double value);
long double floorl(long double value);
double modf(double value, double *integral);
long double modfl(long double value, long double *integral);
