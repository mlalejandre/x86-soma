#ifndef SOMA_PMM_CORE_H
#define SOMA_PMM_CORE_H
/* SOMA_157_T480S: nucleo puro del asignador de marcos fisicos (x86_64). Sin hardware ni consola: lo usan pmm.c y la prueba
 * del anfitrion (tests/test_pmm_157.c) con el MISMO codigo.
 *
 * Dos reservas sobre un unico mapa de bits (1 = ocupado):
 *   - BAJA  [16 MiB, 512 MiB): marcos accesibles por identidad. Las tablas de paginas, el DMA y todo el codigo que usa la
 *     direccion fisica como puntero sacan sus marcos de aqui (pmm_alloc_frame / pmm_alloc_contig), como siempre.
 *   - ALTA  [512 MiB, techo de la RAM): solo se usan a traves de una proyeccion (el montón del nucleo). En el T480s son
 *     ~15 GiB que antes no se usaban (el PMM tenia un limite fijo de 512 MiB).
 * El mapa de bits cubre hasta la direccion mas alta de RAM utilizable (17,5 GiB en el T480s: ~575 KiB) y se coloca en la
 * primera zona libre de la reserva baja; si la RAM cabe en 512 MiB se usa el mapa estatico de siempre. */
#include <stdint.h>
#include <stddef.h>
#include "pmm_scan.h"

#define PMMC_FRAME        4096ULL
#define PMMC_LOWER        (32ULL << 20)          /* zona reservada al principio (nucleo, virtqueues, BIOS). SOMA_202: 32 MiB (eran 16):
                                                  * la imagen del nucleo lleva dentro el programa del navegador aislado y ya no cabia */
#define PMMC_LOW_LIMIT    (512ULL << 20)         /* fin de la reserva baja (accesible por identidad) */
#define PMMC_MAX_PHYS     (64ULL << 30)          /* tope de seguridad del mapa */
#define PMMC_MAX_REGIONS  64

struct pmmc_region { uint64_t base, len; };
struct pmmc {
    uint64_t *bm;                 /* mapa de bits */
    size_t frames;                /* marcos que cubre el mapa (techo) */
    size_t low_end;               /* primer marco de la reserva alta */
    size_t usable;                /* marcos de RAM utilizable segun el firmware (dentro del mapa) */
    size_t used;                  /* de esos, ocupados */
    size_t hint, hint_high;       /* no hay marcos libres por debajo de la pista de cada reserva */
    uint64_t bm_phys, bm_bytes;   /* donde esta el mapa (0 = estatico) */
    int capped;                   /* 1 si la RAM no cupo en el mapa (se recorto) */
};

static inline void pmmc_set(struct pmmc *s, size_t f) { s->bm[f >> 6] |= 1ULL << (f & 63); }
static inline void pmmc_clr(struct pmmc *s, size_t f) { s->bm[f >> 6] &= ~(1ULL << (f & 63)); }
static inline int  pmmc_test(const struct pmmc *s, size_t f) { return (int)((s->bm[f >> 6] >> (f & 63)) & 1); }

/* Direccion mas alta de RAM utilizable (acotada a PMMC_MAX_PHYS). */
static inline uint64_t pmmc_top(const struct pmmc_region *r, int n)
{
    uint64_t top = 0;
    for (int i = 0; i < n; i++) {
        uint64_t e = r[i].base + r[i].len;
        if (e < r[i].base) e = ~0ULL;                       /* desbordamiento: region corrupta */
        if (e > top) top = e;
    }
    return top > PMMC_MAX_PHYS ? PMMC_MAX_PHYS : top;
}
/* Busca `bytes` (alineados a 4 KiB) dentro de una region libre, en [lo, hi). Devuelve 0 y la direccion, o -1. */
static inline int pmmc_place(const struct pmmc_region *r, int n, uint64_t bytes, uint64_t lo, uint64_t hi, uint64_t *out)
{
    for (int i = 0; i < n; i++) {
        uint64_t b = r[i].base, e = r[i].base + r[i].len;
        if (e < b) continue;
        if (b < lo) b = lo;
        if (e > hi) e = hi;
        b = (b + PMMC_FRAME - 1) & ~(PMMC_FRAME - 1);
        if (b < e && e - b >= bytes) { *out = b; return 0; }
    }
    return -1;
}
/* Cuantos bytes de mapa hacen falta para `frames` marcos (multiplo de 8). */
static inline uint64_t pmmc_bm_bytes(size_t frames) { return ((uint64_t)(frames + 63) / 64) * 8; }

/* Construye el estado. `bm_static` cubre `static_frames` marcos. `phys_to_ptr` convierte la direccion fisica del mapa
 * dinamico en un puntero (identidad en el nucleo; un bloque de memoria en la prueba). */
static inline void pmmc_build(struct pmmc *s, const struct pmmc_region *r, int n,
                              uint64_t *bm_static, size_t static_frames, void *(*phys_to_ptr)(uint64_t))
{
    uint64_t top = pmmc_top(r, n);
    size_t frames = (size_t)(top / PMMC_FRAME);
    s->bm = bm_static; s->bm_phys = 0; s->bm_bytes = 0; s->capped = 0;
    if (frames > static_frames) {
        uint64_t bytes = pmmc_bm_bytes(frames), at;
        if (pmmc_place(r, n, bytes, PMMC_LOWER, PMMC_LOW_LIMIT, &at) == 0) {
            s->bm = (uint64_t *)phys_to_ptr(at); s->bm_phys = at; s->bm_bytes = bytes;
        } else { frames = static_frames; s->capped = 1; }
    }
    s->frames = frames;
    size_t words = (frames + 63) / 64;
    for (size_t w = 0; w < words; w++) s->bm[w] = ~0ULL;   /* todo ocupado; despues se libera lo que dice el firmware */
    s->usable = 0;
    for (int i = 0; i < n; i++) {
        uint64_t b = r[i].base, e = r[i].base + r[i].len;
        if (e < b) continue;
        size_t f0 = (size_t)((b + PMMC_FRAME - 1) / PMMC_FRAME), f1 = (size_t)(e / PMMC_FRAME);
        if (f1 > frames) f1 = frames;
        for (size_t f = f0; f < f1; f++) if (pmmc_test(s, f)) { pmmc_clr(s, f); s->usable++; }   /* regiones solapadas: una vez */
    }
    /* zona baja reservada y el propio mapa */
    size_t lowf = (size_t)(PMMC_LOWER / PMMC_FRAME); if (lowf > frames) lowf = frames;
    for (size_t f = 0; f < lowf; f++) pmmc_set(s, f);
    if (s->bm_bytes) {
        size_t a = (size_t)(s->bm_phys / PMMC_FRAME), b = (size_t)((s->bm_phys + s->bm_bytes + PMMC_FRAME - 1) / PMMC_FRAME);
        for (size_t f = a; f < b && f < frames; f++) pmmc_set(s, f);
    }
    size_t freef = 0;
    for (size_t f = 0; f < frames; f++) if (!pmmc_test(s, f)) freef++;
    s->used = s->usable > freef ? s->usable - freef : 0;
    s->low_end = frames < (size_t)(PMMC_LOW_LIMIT / PMMC_FRAME) ? frames : (size_t)(PMMC_LOW_LIMIT / PMMC_FRAME);
    s->hint = lowf;
    s->hint_high = s->low_end;
}
static inline uint64_t pmmc_alloc_low(struct pmmc *s)
{
    size_t f = pmm_scan_zero(s->bm, s->hint, s->low_end);
    if (f == (size_t)-1) { s->hint = s->low_end; return 0; }
    pmmc_set(s, f); s->used++; s->hint = f + 1;
    return (uint64_t)f * PMMC_FRAME;
}
/* Reserva alta; si esta agotada (o no existe: RAM <= 512 MiB), la baja. */
static inline uint64_t pmmc_alloc_high(struct pmmc *s)
{
    if (s->low_end < s->frames) {
        size_t f = pmm_scan_zero(s->bm, s->hint_high, s->frames);
        if (f != (size_t)-1) { pmmc_set(s, f); s->used++; s->hint_high = f + 1; return (uint64_t)f * PMMC_FRAME; }
        s->hint_high = s->frames;
    }
    return pmmc_alloc_low(s);
}
static inline uint64_t pmmc_alloc_contig(struct pmmc *s, size_t n)
{
    if (!n) return 0;
    size_t f = pmm_scan_run(s->bm, (size_t)(PMMC_LOWER / PMMC_FRAME), s->low_end, n);
    if (f == (size_t)-1) return 0;
    for (size_t k = f; k < f + n; k++) { pmmc_set(s, k); s->used++; }
    if (f <= s->hint && s->hint < f + n) s->hint = f + n;
    return (uint64_t)f * PMMC_FRAME;
}
static inline void pmmc_free(struct pmmc *s, uint64_t phys)
{
    if (phys < PMMC_LOWER) return;                          /* prohibido liberar la zona reservada */
    size_t f = (size_t)(phys / PMMC_FRAME);
    if (f >= s->frames || !pmmc_test(s, f)) return;         /* fuera del mapa o ya libre (doble liberacion) */
    if (s->bm_bytes && phys >= s->bm_phys && phys < s->bm_phys + s->bm_bytes) return;   /* el propio mapa */
    pmmc_clr(s, f);
    if (s->used) s->used--;
    if (f < s->low_end) { if (f < s->hint) s->hint = f; }
    else if (f < s->hint_high) s->hint_high = f;
}
#endif
