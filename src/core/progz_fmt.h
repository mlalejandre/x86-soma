/* SOMA_314_PROGZ: formato de los programas de ring 3 comprimidos dentro de la imagen del nucleo (los escribe tools/progz.py).
 *   cabecera de 24 bytes: "SOMZ" | version (1) | tamano original (u64) | huella FNV-1a 64 del original (u64)
 *   + el programa en zlib.
 * Funciones puras (sin nucleo): se prueban en el anfitrion (tests/test_progz_314.c). */
#ifndef SOMA_PROGZ_FMT_H
#define SOMA_PROGZ_FMT_H
#include <stdint.h>
#include <stddef.h>

#define PROGZ_CAB      24u
#define PROGZ_VERSION  1u
#define PROGZ_TAM_MAX  (64u << 20)

struct progz_cab { uint64_t tam, huella; };

static inline uint64_t progz_fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ULL; }
    return h;
}

static inline uint64_t progz_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

/* 1 si z empieza por una cabecera valida (y detras hay datos); rellena c. */
static inline int progz_cabecera(const uint8_t *z, size_t zn, struct progz_cab *c)
{
    if (!z || zn <= PROGZ_CAB || z[0] != 'S' || z[1] != 'O' || z[2] != 'M' || z[3] != 'Z') return 0;
    uint32_t ver = (uint32_t)z[4] | (uint32_t)z[5] << 8 | (uint32_t)z[6] << 16 | (uint32_t)z[7] << 24;
    uint64_t tam = progz_le64(z + 8);
    if (ver != PROGZ_VERSION || tam == 0 || tam > PROGZ_TAM_MAX) return 0;
    c->tam = tam; c->huella = progz_le64(z + 16);
    return 1;
}
#endif
