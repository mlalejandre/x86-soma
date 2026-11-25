/* SOMA_308_EFICIENCIA: memcpy/memmove/memset/memcmp de aarch64 (src/core/memfn_gen.h) contra la libc: todos los tamanos de 0
 * a 300 y varios grandes, todas las alineaciones 0..8 de origen y destino y, en memmove, todos los solapes. Las zonas de guarda
 * comprueban que no se escribe fuera. Es C portable: se ejecuta en cualquier anfitrion little-endian (Linux x86_64, Mac ARM). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MEMFN(n) gen_##n
#include "../src/core/memfn_gen.h"
static int checks, fails;
#define CK(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("FALLO: " __VA_ARGS__); printf("\n"); } } } while (0)
static unsigned char A[70000] __attribute__((aligned(16))), B[70000] __attribute__((aligned(16))), C[70000] __attribute__((aligned(16)));
static unsigned rnd(void) { static unsigned s = 12345; s = s * 1103515245u + 12345u; return s >> 8; }
static void fill(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)rnd(); }
int main(void)
{
    static const size_t big[] = { 511, 512, 513, 1000, 4095, 4096, 4097, 7680, 65000 };
    for (int pass = 0; pass < 2; pass++) {
        size_t nn = pass ? sizeof big / sizeof big[0] : 301;
        for (size_t k = 0; k < nn; k++) {
            size_t n = pass ? big[k] : k;
            for (int da = 0; da <= 8; da++) for (int sa = 0; sa <= 8; sa += pass ? 3 : 1) {
                fill(A, n + 64); memset(B, 0xEE, n + 64); memset(C, 0xEE, n + 64);
                void *r = gen_memcpy(B + 16 + da, A + 8 + sa, n); memcpy(C + 16 + da, A + 8 + sa, n);
                CK(r == B + 16 + da && !memcmp(B, C, n + 64), "memcpy n=%zu da=%d sa=%d", n, da, sa);
                memset(B, 0x11, n + 64); memset(C, 0x11, n + 64);
                int v = (int)(rnd() & 0x3FF) - 256;
                r = gen_memset(B + 16 + da, v, n); memset(C + 16 + da, v, n);
                CK(r == B + 16 + da && !memcmp(B, C, n + 64), "memset n=%zu da=%d", n, da);
                memcpy(B + da, A + sa, n);
                CK(gen_memcmp(B + da, A + sa, n) == 0, "memcmp igual n=%zu da=%d sa=%d", n, da, sa);
                if (n) for (int t = 0; t < 6; t++) {
                    size_t pos = t == 0 ? 0 : t == 1 ? n - 1 : t == 2 ? n / 2 : rnd() % n;
                    unsigned char old = B[da + pos]; B[da + pos] = (unsigned char)(old + 1 + rnd() % 254);
                    int x = gen_memcmp(B + da, A + sa, n), y = memcmp(B + da, A + sa, n);
                    CK((x < 0) == (y < 0) && (x > 0) == (y > 0) && x != 0, "memcmp n=%zu pos=%zu: %d frente a %d", n, pos, x, y);
                    B[da + pos] = old;
                }
            }
            for (int off = -40; off <= 40; off += (pass ? 7 : 1)) for (int base = 80; base <= 87; base += (pass ? 7 : 1)) {
                fill(A, n + 200); memcpy(B, A, n + 200); memcpy(C, A, n + 200);
                void *r = gen_memmove(B + base + off, B + base, n); memmove(C + base + off, C + base, n);
                CK(r == B + base + off && !memcmp(B, C, n + 200), "memmove n=%zu base=%d off=%d", n, base, off);
            }
            if (pass) for (int t = 0; t < 40; t++) {
                long off = (long)(rnd() % (2 * n)) - (long)n;
                size_t tot = 3 * n + 64 > sizeof A ? sizeof A : 3 * n + 64;
                fill(A, tot);
                if ((long)n + off < 0 || n + (size_t)(off > 0 ? off : 0) + n + n > tot) continue;
                memcpy(B, A, tot); memcpy(C, A, tot);
                gen_memmove(B + n + off, B + n, n); memmove(C + n + off, C + n, n);
                CK(!memcmp(B, C, tot), "memmove grande n=%zu off=%ld", n, off);
            }
        }
    }
    printf("test_memfn_gen_308: %d comprobaciones, %d fallos\n", checks, fails);
    return fails != 0;
}
