#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long long z = 88172645463325252ULL;
static unsigned long long xs(void) { z ^= z << 13; z ^= z >> 7; z ^= z << 17; return z; }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000, malos = 0;
    for (int i = 0; i < n; ++i) {
        union { unsigned long long u; double d; } q; q.u = xs();
        if (i % 3 == 0) q.u = (q.u & 0x800fffffffffffffULL) | ((0x3c0ULL + xs() % 128) << 52);   /* cerca de 1 */
        double d = q.d;
        if (d != d) continue;
        char b[512];
        snprintf(b, sizeof b, "%.17g", d);
        double r = strtod(b, 0);
        if (memcmp(&r, &d, 8)) malos++;
        printf("%s|%a|%.3e|%g|%.10f|%.0e\n", b, d, d, d, d, d);
    }
    printf("ida y vuelta mal: %d\n", malos);
    return 0;
}
