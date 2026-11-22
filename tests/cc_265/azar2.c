#include <stdio.h>
#include <stdlib.h>
static unsigned long long z = 1234567ULL;
static unsigned long long xs(void) { z ^= z << 13; z ^= z >> 7; z ^= z << 17; return z; }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    for (int i = 0; i < n; ++i) {
        char b[80]; int k = 0, nd = 1 + (int)(xs() % 25);
        for (int j = 0; j < nd; ++j) { b[k++] = (char)('0' + xs() % 10); if (j == 0 && nd > 1 && xs() % 2) b[k++] = '.'; }
        k += sprintf(b + k, "e%d", (int)(xs() % 660) - 340);
        b[k] = 0;
        double d = strtod(b, 0);
        float f = strtof(b, 0);
        printf("%s %a %a\n", b, d, (double)f);
    }
    return 0;
}
