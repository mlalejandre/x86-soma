/* PARCHE 132: canario del protector de pila ALEATORIO por arranque (x86_64).
 * Antes era una constante de compilacion (cualquiera que leyese el binario la conocia). Se cambia en `boot.s`, ANTES de entrar en kernel_main,
 * cuando todavia no hay ninguna funcion protegida en curso (por eso no puede hacerse mas tarde: esas fallarian al retornar).
 * Fuente: RDRAND si la CPU lo tiene; si no, jitter del contador de ciclos. El byte bajo es 0 (frena desbordamientos de cadenas). */
#include <stdint.h>
#include "ssp_init.h"

extern uintptr_t __stack_chk_guard __attribute__((weak));
int ssp_source;     /* 0 = constante (sin SSP), 1 = RDRAND, 2 = contador de ciclos */

static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

__attribute__((no_stack_protector, used)) void ssp_randomize(void)
{
    if (!&__stack_chk_guard) return;                 /* SSP=0: no hay canario */
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    uint64_t v = 0; int got = 0;
    if (c & (1u << 30)) {                            /* RDRAND */
        for (int tries = 0; tries < 16 && !got; ++tries) {
            uint64_t r; unsigned char ok;
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(r), "=qm"(ok) : : "cc");
            if (ok) { v = r; got = 1; }
        }
    }
    if (got) ssp_source = 1;
    else {                                           /* jitter: diferencias de TSC entre lecturas encadenadas con cpuid */
        uint64_t h = 0x9E3779B97F4A7C15ULL ^ tsc();
        for (int i = 0; i < 256; ++i) {
            uint64_t t0 = tsc();
            uint32_t aa = 0, bb, cc = 0, dd; __asm__ volatile ("cpuid" : "+a"(aa), "=b"(bb), "+c"(cc), "=d"(dd));
            uint64_t t1 = tsc();
            h ^= (t1 - t0) + (uint64_t)i; h = (h << 13) | (h >> 51); h *= 0xBF58476D1CE4E5B9ULL;
        }
        v = h; ssp_source = 2;
    }
    v &= ~0xFFULL;                                   /* byte bajo 0 */
    if (v == 0) v = 0x5AC3A55A5A5A5A00ULL;           /* nunca un canario nulo */
    __stack_chk_guard = (uintptr_t)v;
}
