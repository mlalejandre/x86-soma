#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long long z = 99991ULL;
static unsigned long long xs(void) { z ^= z << 13; z ^= z >> 7; z ^= z << 17; return z; }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    for (int i = 0; i < n; ++i) {
        char b[80]; int k = 0, nd = 1 + (int)(xs() % 30);
        for (int j = 0; j < nd; ++j) { b[k++] = (char)('0' + xs() % 10); if (j == 0 && nd > 1 && xs() % 2) b[k++] = '.'; }
        int rango = i % 2 ? 9800 : 700;
        k += sprintf(b + k, "e%d", (int)(xs() % rango) - rango / 2);
        long double v = strtold(b, 0);
        unsigned char q[16] = { 0 }; memcpy(q, &v, 10);
        printf("%s ", b); for (int j = 9; j >= 0; --j) printf("%02x", q[j]); printf(" %.20Le\n", v);
    }
    return 0;
}
