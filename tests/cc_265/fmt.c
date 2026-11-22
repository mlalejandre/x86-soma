#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
int main(void) {
    char b[256];
    printf("[%d] [%5d] [%-5d] [%05d] [%+d] [% d] [%.3d] [%x] [%X] [%#x] [%o] [%#o] [%u]\n", 42, 42, 42, 42, 42, 42, 7, 255, 255, 255, 8, 8, 3000000000u);
    printf("[%ld] [%lld] [%lu] [%llx] [%hd] [%hhu] [%zu] [%d] [%ld]\n", -1234567890123L, LLONG_MIN, ULONG_MAX, 0xdeadbeefcafeULL, (short)70000, (unsigned char)300, sizeof(long), INT_MIN, LONG_MAX);
    printf("[%s] [%10s] [%-10s] [%.2s] [%c] [%%] [%5c]\n", "hola", "hola", "hola", "hola", 'z', 'q');
    double v[] = { 0.0, 1.0, -1.5, 3.14159265358979, 1e-5, 123456789.125, 1e300, 2.5e-300, 0.1, 1.0/3, 99.995, 0.000123456, 1e21, 5e-324 };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; ++i)
        printf("[%f] [%.2f] [%e] [%.3e] [%g] [%.10g] [%G] [%10.4f] [%-12.3e] [%+.1f]\n", v[i], v[i], v[i], v[i], v[i], v[i], v[i], v[i], v[i], v[i]);
    printf("[%.0f] [%.0f] [%.0f] [%#.0f] [%g] [%g] [%g]\n", 0.5, 1.5, 2.5, 3.0, 100000.0, 1000000.0, 0.0001);
    printf("[%f] [%f] [%e]\n", INFINITY, -INFINITY, NAN);
    printf("[%*d] [%-*d] [%.*f]\n", 6, 1, 6, 2, 3, 2.71828);
    int n = snprintf(b, 8, "%s-%d", "abcdef", 12345); printf("[%s] %d\n", b, n);
    sprintf(b, "%08.3f|%x|%p", -3.5, 0, (void *)0); printf("%s\n", b);
    printf("[%Lf] [%Le]\n", 1.25L, 12345.678L);
    return 0;
}
