/* SOMA_308_EFICIENCIA: memcpy, memmove, memset y memcmp para aarch64 (y cualquier arquitectura little-endian de 64 bits).
 * Archivo de IMPLEMENTACION: lo incluye src/core/mem.c una sola vez. Se prueba en el host contra la libc:
 * tests/test_memfn_gen_308.c (todos los tamanos de 0 a 300 y varios grandes, todas las alineaciones, todos los solapes).
 *
 * Antes, en aarch64 las cuatro funciones iban byte a byte: copiar una fila de pantalla de 1920 pixeles eran 7680 vueltas.
 * Ahora van de 8 en 8 bytes (y de 32 en 32 en el bucle principal), con solo accesos ALINEADOS: el nucleo de aarch64 se
 * compila con -mstrict-align (antes de activar la MMU, y en memoria de dispositivo o no cacheable, un acceso desalineado
 * da un fallo). Cuando origen y destino no tienen la misma alineacion (una cabecera Ethernet de 14 bytes, por ejemplo) se
 * leen palabras alineadas del origen y se juntan con desplazamientos. Esas lecturas pueden tocar hasta 7 bytes de fuera del
 * rango pedido, pero siempre dentro de una palabra alineada que contiene al menos un byte del rango: misma pagina, nunca falla.
 * MEMFN(nombre) permite compilar las funciones con otro nombre en las pruebas. */
#ifndef SOMA_MEMFN_GEN_H
#define SOMA_MEMFN_GEN_H
#include <stddef.h>
#include <stdint.h>
#ifndef MEMFN
#define MEMFN(n) n
#endif
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "memfn_gen.h junta palabras suponiendo little-endian"
#endif
/* gcc no debe convertir estos bucles en llamadas a memcpy/memset (seria llamarse a si mismas) */
#if defined(__GNUC__) && !defined(__clang__)
#define MG_FN __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MG_FN
#endif

typedef uint64_t __attribute__((may_alias)) mg_u64;
#define MG_DES(p) ((unsigned)((uintptr_t)(p) & 7u))

/* Hacia delante. Correcto tambien con solape si el destino esta por debajo del origen (memmove). */
static MG_FN void mg_adelante(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 16) {
        while (MG_DES(d)) { *d++ = *s++; n--; }                 /* como mucho 7: quedan 9 o mas */
        mg_u64 *dw = (mg_u64 *)(void *)d;
        unsigned k = MG_DES(s);
        if (!k) {
            const mg_u64 *sw = (const mg_u64 *)(const void *)s;
            while (n >= 32) {
                uint64_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
                dw[0] = a; dw[1] = b; dw[2] = c; dw[3] = e;
                sw += 4; dw += 4; n -= 32;
            }
            while (n >= 8) { *dw++ = *sw++; n -= 8; }
            s = (const uint8_t *)(const void *)sw;
        } else {
            unsigned sh = 8u * k;
            const mg_u64 *sw = (const mg_u64 *)(const void *)(s - k);
            uint64_t w0 = *sw++;
            while (n >= 8) {                                     /* la palabra leida contiene el byte s[7]: no sale de la pagina */
                uint64_t w1 = *sw++;
                *dw++ = (w0 >> sh) | (w1 << (64u - sh));
                w0 = w1; s += 8; n -= 8;
            }
        }
        d = (uint8_t *)(void *)dw;
    }
    while (n--) *d++ = *s++;
}

/* Hacia atras (memmove con el destino dentro del origen). */
static MG_FN void mg_atras(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 16 && MG_DES(d + n) == MG_DES(s + n)) {
        while (MG_DES(d + n)) { n--; d[n] = s[n]; }
        mg_u64 *dw = (mg_u64 *)(void *)(d + n);
        const mg_u64 *sw = (const mg_u64 *)(const void *)(s + n);
        while (n >= 32) {
            uint64_t a = sw[-1], b = sw[-2], c = sw[-3], e = sw[-4];
            dw[-1] = a; dw[-2] = b; dw[-3] = c; dw[-4] = e;
            sw -= 4; dw -= 4; n -= 32;
        }
        while (n >= 8) { *--dw = *--sw; n -= 8; }
    }
    while (n) { n--; d[n] = s[n]; }
}

MG_FN void *MEMFN(memcpy)(void *dst, const void *src, size_t n)
{
    mg_adelante((uint8_t *)dst, (const uint8_t *)src, n);
    return dst;
}

MG_FN void *MEMFN(memmove)(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    if (d == s || !n) return dst;
    if (d < s || d >= s + n) mg_adelante(d, s, n);
    else mg_atras(d, s, n);
    return dst;
}

MG_FN void *MEMFN(memset)(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t b = (uint8_t)value;
    if (n >= 16) {
        uint64_t v = (uint64_t)b * 0x0101010101010101ULL;
        while (MG_DES(d)) { *d++ = b; n--; }
        mg_u64 *dw = (mg_u64 *)(void *)d;
        while (n >= 32) { dw[0] = v; dw[1] = v; dw[2] = v; dw[3] = v; dw += 4; n -= 32; }
        while (n >= 8) { *dw++ = v; n -= 8; }
        d = (uint8_t *)(void *)dw;
    }
    while (n--) *d++ = b;
    return dst;
}

MG_FN int MEMFN(memcmp)(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    if (n >= 16 && MG_DES(x) == MG_DES(y)) {
        while (MG_DES(x)) { if (*x != *y) return (int)*x - (int)*y; x++; y++; n--; }
        while (n >= 8) {
            uint64_t p = *(const mg_u64 *)(const void *)x, q = *(const mg_u64 *)(const void *)y;
            if (p != q) {                                        /* el primer byte distinto es el menos significativo */
                unsigned sh = (unsigned)__builtin_ctzll(p ^ q) & ~7u;
                return (int)((p >> sh) & 255u) - (int)((q >> sh) & 255u);
            }
            x += 8; y += 8; n -= 8;
        }
    }
    for (; n; --n, ++x, ++y) if (*x != *y) return (int)*x - (int)*y;
    return 0;
}
#endif
