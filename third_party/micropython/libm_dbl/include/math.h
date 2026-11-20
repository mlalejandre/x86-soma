/* SOMA: math.h minimo para compilar libm_dbl (musl) sin la biblioteca C del sistema: la de glibc declara __sin, __cos... con
 * otros tipos y choca con los de musl. Solo se usa para compilar los archivos de libm_dbl/. */
#ifndef SOMA_LIBM_MATH_H
#define SOMA_LIBM_MATH_H
#include <float.h>
typedef float float_t;
typedef double double_t;
#define NAN         (__builtin_nan(""))
#define INFINITY    (__builtin_inf())
#define HUGE_VAL    (__builtin_huge_val())
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define signbit(x)    __builtin_signbit(x)
double acos(double); double acosh(double); double asin(double); double asinh(double); double atan(double); double atan2(double, double);
double atanh(double); double ceil(double); double copysign(double, double); double cos(double); double cosh(double); double erf(double);
double erfc(double); double exp(double); double expm1(double); double fabs(double); double floor(double); double fmod(double, double);
double frexp(double, int *); double ldexp(double, int); double lgamma(double); double log(double); double log10(double);
double log1p(double); double log2(double); double modf(double, double *); double nearbyint(double); double pow(double, double);
double rint(double); double round(double); double scalbn(double, int); double sin(double); double sinh(double); double sqrt(double);
double tan(double); double tanh(double); double tgamma(double); double trunc(double); double nan(const char *);
#endif
