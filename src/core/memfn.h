/* SOMA_199: memcpy, memmove, memset y memcmp para x86_64. Archivo de IMPLEMENTACION: se incluye una sola vez por programa
 * (el nucleo desde mem.c; cada servicio de ring 3 desde sbxrt.h). Se prueba en el host contra la libc: tests/test_memfn_199.c.
 *
 * Antes: el nucleo copiaba SIEMPRE con `rep movsb` (y hacia atras con `std; rep movsb`, que ninguna CPU acelera: byte a byte), y los
 * servicios de ring 3 (decodificadores de imagen, zlib, PDF) con un bucle de un byte por vuelta. `rep movsb` es bueno para bloques
 * grandes en CPU con ERMS, pero su arranque cuesta decenas de ciclos, y la mayoria de las copias del nucleo son de pocos bytes; en un
 * emulador sin virtualizacion (QEMU TCG) ademas se ejecuta byte a byte.
 *
 * Ahora:
 *   - hasta 32 bytes: sin bucle ni `rep`, con cargas y escrituras de 8/4/2 bytes que se solapan (todas las lecturas antes de la
 *     primera escritura: vale tambien para memmove);
 *   - mas de 32: `rep movsq` / `rep stosq` (8 bytes por paso, rapido en toda CPU x86_64) y el resto con una escritura solapada;
 *   - memmove hacia atras: `rep movsq` descendente (8 veces menos pasos que antes);
 *   - memcmp: de 8 en 8 bytes.
 * Sin SSE (el nucleo se compila con -mgeneral-regs-only). MEMFN(nombre) permite compilar las funciones con otro nombre en las pruebas. */
#ifndef SOMA_MEMFN_H
#define SOMA_MEMFN_H
#include <stddef.h>
#include <stdint.h>
#ifndef MEMFN
#define MEMFN(n) n
#endif

static inline __attribute__((always_inline)) uint64_t mf_ld8(const void *p) { uint64_t v; __builtin_memcpy(&v, p, 8); return v; }
static inline __attribute__((always_inline)) uint32_t mf_ld4(const void *p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) uint16_t mf_ld2(const void *p) { uint16_t v; __builtin_memcpy(&v, p, 2); return v; }
static inline __attribute__((always_inline)) void mf_st8(void *p, uint64_t v) { __builtin_memcpy(p, &v, 8); }
static inline __attribute__((always_inline)) void mf_st4(void *p, uint32_t v) { __builtin_memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void mf_st2(void *p, uint16_t v) { __builtin_memcpy(p, &v, 2); }

/* n <= 32: todas las lecturas antes que las escrituras (correcto aunque origen y destino se solapen) */
static inline __attribute__((always_inline)) void mf_small(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 16) { uint64_t a = mf_ld8(s), b = mf_ld8(s + 8), c = mf_ld8(s + n - 16), e = mf_ld8(s + n - 8); mf_st8(d, a); mf_st8(d + 8, b); mf_st8(d + n - 16, c); mf_st8(d + n - 8, e); }
    else if (n >= 8) { uint64_t a = mf_ld8(s), b = mf_ld8(s + n - 8); mf_st8(d, a); mf_st8(d + n - 8, b); }
    else if (n >= 4) { uint32_t a = mf_ld4(s), b = mf_ld4(s + n - 4); mf_st4(d, a); mf_st4(d + n - 4, b); }
    else if (n >= 2) { uint16_t a = mf_ld2(s), b = mf_ld2(s + n - 2); mf_st2(d, a); mf_st2(d + n - 2, b); }
    else if (n) *d = *s;
}
/* n > 32, hacia delante (destino por debajo del origen, o sin solape) */
static inline __attribute__((always_inline)) void mf_fwd(uint8_t *d, const uint8_t *s, size_t n)
{
    uint64_t tail = mf_ld8(s + n - 8);                 /* se lee antes: con solape, la copia podria pisarlo */
    uint8_t *de = d + n - 8; size_t q = n >> 3;
    __asm__ volatile ("rep movsq" : "+D"(d), "+S"(s), "+c"(q) : : "memory");
    mf_st8(de, tail);
}

void *MEMFN(memcpy)(void *dst, const void *src, size_t n)
{
    if (n <= 32) mf_small((uint8_t *)dst, (const uint8_t *)src, n);
    else mf_fwd((uint8_t *)dst, (const uint8_t *)src, n);
    return dst;
}

void *MEMFN(memmove)(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    if (n <= 32) { mf_small(d, s, n); return dst; }
    if (d == s) return dst;
    if (d < s || d >= s + n) { mf_fwd(d, s, n); return dst; }
    {   /* el destino empieza dentro del origen: de atras hacia delante */
        uint64_t head = mf_ld8(s);
        uint8_t *dp = d + n - 8; const uint8_t *sp = s + n - 8; size_t q = n >> 3;
        __asm__ volatile ("std\n\trep movsq\n\tcld" : "+D"(dp), "+S"(sp), "+c"(q) : : "memory", "cc");
        mf_st8(d, head);
    }
    return dst;
}

void *MEMFN(memset)(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint64_t v = (uint64_t)(uint8_t)value * 0x0101010101010101ULL;
    if (n <= 32) {
        if (n >= 16) { mf_st8(d, v); mf_st8(d + 8, v); mf_st8(d + n - 16, v); mf_st8(d + n - 8, v); }
        else if (n >= 8) { mf_st8(d, v); mf_st8(d + n - 8, v); }
        else if (n >= 4) { mf_st4(d, (uint32_t)v); mf_st4(d + n - 4, (uint32_t)v); }
        else if (n >= 2) { mf_st2(d, (uint16_t)v); mf_st2(d + n - 2, (uint16_t)v); }
        else if (n) *d = (uint8_t)v;
        return dst;
    }
    {
        uint8_t *p = d; size_t q = n >> 3;
        __asm__ volatile ("rep stosq" : "+D"(p), "+c"(q) : "a"(v) : "memory");
        mf_st8(d + n - 8, v);
    }
    return dst;
}

int MEMFN(memcmp)(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (n >= 8) {
        uint64_t p = mf_ld8(x), q = mf_ld8(y);
        if (p != q) {                                  /* el primer byte distinto es el de menor direccion: el menos significativo */
            uint64_t df = p ^ q; unsigned sh = (unsigned)__builtin_ctzll(df) & ~7u;
            return (int)((p >> sh) & 255u) - (int)((q >> sh) & 255u);
        }
        x += 8; y += 8; n -= 8;
    }
    for (; n; --n, ++x, ++y) if (*x != *y) return (int)*x - (int)*y;
    return 0;
}
#endif
