#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
static int cmp(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }
int main(void) {
    const char *s[] = { "0", "1", "-17", "3.5", "1e10", "  -2.5e-3xyz", "0x1p4", "123456789012345678901234567890", "1.7976931348623157e308", "2.2250738585072014e-308", "4.9e-324", "inf", "nan", ".5", "5.", "abc" };
    for (unsigned i = 0; i < sizeof s / sizeof *s; ++i) { char *e; double d = strtod(s[i], &e); printf("strtod(%s)=%.17g resto='%s'\n", s[i], d, e); }
    const char *t[] = { "42", "-42", "0x1F", "0777", "  +9z", "9223372036854775807", "9223372036854775808", "-9223372036854775808" };
    for (unsigned i = 0; i < sizeof t / sizeof *t; ++i) { char *e; errno = 0; long l = strtol(t[i], &e, 0); printf("strtol(%s)=%ld errno=%d resto='%s'\n", t[i], l, errno == ERANGE, e); }
    printf("strtoul=%lu atoi=%d atof=%g\n", strtoul("ffff", 0, 16), atoi("  -77"), atof("2.5"));
    int a[2000]; srand(7); for (int i = 0; i < 2000; ++i) a[i] = rand() % 1000;
    qsort(a, 2000, sizeof *a, cmp); int ok = 1; for (int i = 1; i < 2000; ++i) ok &= a[i - 1] <= a[i];
    int k = 500; int *f = bsearch(&k, a, 2000, sizeof *a, cmp);
    printf("qsort ordenado=%d bsearch=%d\n", ok, f ? *f : -1);
    printf("sqrt=%.15g sin=%.15g cos=%.15g atan2=%.15g\n", sqrt(2), sin(1), cos(1), atan2(1, 2));
    printf("exp=%.15g log=%.15g pow=%.15g pow2=%.15g log10=%.15g\n", exp(1), log(10), pow(2, 0.5), pow(3, 20), log10(1000));
    printf("floor=%g ceil=%g round=%g trunc=%g fmod=%g fabs=%g\n", floor(-2.5), ceil(-2.5), round(2.5), trunc(-2.7), fmod(10, 3), fabs(-4));
    printf("tan=%.12g asin=%.12g acos=%.12g tanh=%.12g hypot=%g cbrt=%.12g\n", tan(0.5), asin(0.5), acos(0.5), tanh(0.5), hypot(3, 4), cbrt(27));
    int e; double m = frexp(48, &e); double ip; double fr = modf(3.75, &ip);
    printf("frexp=%g,%d ldexp=%g modf=%g,%g\n", m, e, ldexp(0.75, 6), fr, ip);
    long long x = 1; for (int i = 0; i < 62; ++i) x *= 2; printf("2^62=%lld div=%lld mod=%lld\n", x, x / 7, x % 7);
    unsigned long long u = 18446744073709551615ULL; printf("u/3=%llu double=%.0f\n", u / 3, (double)u);
    float fl = 1.0f / 3; printf("float=%.7f int=%d\n", fl, (int)(fl * 300));
    return 0;
}
