#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
static jmp_buf salto;
static void profundo(int n) { if (n == 0) longjmp(salto, 42); profundo(n - 1); }
static int rec(int n) { volatile char pad[64]; pad[0] = (char)n; return n == 0 ? 0 : 1 + rec(n - 1) + pad[0] * 0; }
static int cmps(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void adios(void) { printf("atexit ejecutado\n"); }
struct grande { double v[20]; int n; };
static struct grande dobla(struct grande g) { for (int i = 0; i < 20; ++i) g.v[i] *= 2; g.n++; return g; }
static long double ld_mitad(long double x) { return x / 2; }
int main(void) {
    atexit(adios);
    int r = setjmp(salto);
    if (!r) profundo(1000); else printf("longjmp=%d\n", r);
    printf("recursion=%d\n", rec(8000));
    char *pal[] = { "pera", "manzana", "uva", "kiwi", "albaricoque" };
    qsort(pal, 5, sizeof *pal, cmps);
    for (int i = 0; i < 5; ++i) printf("%s%c", pal[i], i < 4 ? ' ' : '\n');
    unsigned long sum = 0; void *p[500] = { 0 }; srand(3);
    for (int it = 0; it < 20000; ++it) {
        int i = rand() % 500;
        if (p[i]) { sum += ((unsigned char *)p[i])[0]; free(p[i]); p[i] = 0; }
        else { size_t n = 1 + rand() % 3000; p[i] = malloc(n); if (!p[i]) { printf("sin memoria en %d\n", it); return 1; } memset(p[i], it & 255, n); }
    }
    for (int i = 0; i < 500; ++i) free(p[i]);
    printf("malloc: suma=%lu\n", sum);
    char *b = 0; size_t cap = 0;
    for (int i = 0; i < 19; ++i) { cap = cap ? cap * 2 : 16; b = realloc(b, cap); if (!b) { printf("realloc fallo en %zu\n", cap); return 1; } b[cap - 1] = 'z'; }
    printf("realloc hasta %zu KiB ok\n", cap >> 10); free(b);
    int *z = calloc(1000, sizeof *z); int t = 0; for (int i = 0; i < 1000; ++i) t += z[i]; printf("calloc ceros=%d\n", t == 0); free(z);
    void *dem = malloc(100u << 20); printf("malloc de 100 MiB: %s\n", dem ? "dio memoria (mal)" : "NULL"); 
    char s[] = "uno, dos;tres  cuatro"; for (char *k = strtok(s, ",; "); k; k = strtok(0, ",; ")) printf("[%s]", k); printf("\n");
    struct grande g; for (int i = 0; i < 20; ++i) g.v[i] = i; g.n = 0; g = dobla(dobla(g)); printf("struct: %g %d\n", g.v[19], g.n);
    double y = 0.5, x = 2; long double q = (long double)y * ld_mitad(x); printf("long double: %g\n", (double)q);
    time_t now = time(0); struct tm *tm = localtime(&now); char f[64]; strftime(f, sizeof f, "%Y-%m-%d", tm); printf("fecha valida: %d\n", tm->tm_year + 1900 >= 2024 && strlen(f) == 10);
    long long big = 1LL << 62; printf("%lld %llu %d\n", big / 3, (unsigned long long)-1 / 7, toupper('x'));
    assert(1 + 1 == 2);
    printf("fin\n");
    return 3;
}
