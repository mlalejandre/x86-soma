#ifndef _SOMA_MATH_H
#define _SOMA_MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_SQRT2 1.41421356237309504880
#ifdef __TINYC__
#define HUGE_VAL 1e500
#define INFINITY 1e50f
#define NAN (0.0F / 0.0F)
#else
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_huge_valf())
#define NAN (__builtin_nanf(""))
#endif
double sqrt(double x); double sin(double x); double cos(double x); double tan(double x); double asin(double x); double acos(double x);
double atan(double x); double atan2(double y, double x); double sinh(double x); double cosh(double x); double tanh(double x);
double exp(double x); double log(double x); double log10(double x); double log2(double x); double pow(double x, double y);
double floor(double x); double ceil(double x); double round(double x); double trunc(double x); double fabs(double x);
double fmod(double x, double y); double ldexp(double x, int e); double frexp(double x, int *e); double modf(double x, double *i);
double hypot(double x, double y); double cbrt(double x); double fmin(double a, double b); double fmax(double a, double b);
float sqrtf(float x); float sinf(float x); float cosf(float x); float fabsf(float x); float floorf(float x); float powf(float x, float y);
long double ldexpl(long double x, int e);
int isnan_(double x); int isinf_(double x);
#define isnan(x) ((x) != (x))
#define isinf(x) (!isnan(x) && isnan((x) - (x)))
#define isfinite(x) (!isnan((x) - (x)))
#endif
