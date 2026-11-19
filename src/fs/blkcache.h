#ifndef SOMA_BLKCACHE_H
#define SOMA_BLKCACHE_H
/* PARCHE 131: cache de lectura de bloques (solo lectura, escritura directa: cada escritura al disco INVALIDA las lineas que toca).
 * Mapeo directo de lineas de 8 sectores (4 KiB) alineadas; la etiqueta es numero de linea + 1 (0 = vacia). Codigo puro: la memoria la aporta quien lo use. */
#include <stdint.h>
#include <stddef.h>

#define BLKC_LINE_SECT   8u
#define BLKC_LINE_BYTES  (BLKC_LINE_SECT * 512u)
#define BLKC_MAX_SECT    32u                       /* lecturas mayores se hacen directas: no tiene sentido cachear un streaming */
#define BLKC_SPAN_LINES  (BLKC_MAX_SECT / BLKC_LINE_SECT + 1u)

struct blkcache {
    uint32_t nlines;
    uint64_t *tag;
    uint8_t  *data;                                /* nlines * 4096 */
    uint8_t  *span;                                /* BLKC_SPAN_LINES * 4096: lectura contigua de las lineas que faltan */
    uint64_t hits, misses, invals;
};

static inline void blkc_init(struct blkcache *c, uint32_t nlines, uint64_t *tag, uint8_t *data, uint8_t *span)
{
    c->nlines = nlines; c->tag = tag; c->data = data; c->span = span; c->hits = c->misses = c->invals = 0;
    for (uint32_t i = 0; i < nlines; ++i) tag[i] = 0;
}
static inline const uint8_t *blkc_line(const struct blkcache *c, uint64_t line)
{
    uint32_t s = (uint32_t)(line % c->nlines);
    return c->tag[s] == line + 1 ? c->data + (size_t)s * BLKC_LINE_BYTES : 0;
}
static inline void blkc_put(struct blkcache *c, uint64_t line, const uint8_t *src)
{
    uint32_t s = (uint32_t)(line % c->nlines);
    uint8_t *d = c->data + (size_t)s * BLKC_LINE_BYTES;
    for (size_t i = 0; i < BLKC_LINE_BYTES; ++i) d[i] = src[i];
    c->tag[s] = line + 1;
}
/* invalida toda linea que solape con los sectores [lba, lba+n) */
static inline void blkc_invalidate(struct blkcache *c, uint64_t lba, uint32_t n)
{
    if (n == 0) return;
    uint64_t l0 = lba / BLKC_LINE_SECT, l1 = (lba + n - 1) / BLKC_LINE_SECT;
    if (l1 - l0 + 1 >= c->nlines) { for (uint32_t i = 0; i < c->nlines; ++i) if (c->tag[i]) { c->tag[i] = 0; c->invals++; } return; }
    for (uint64_t l = l0; l <= l1; ++l) { uint32_t s = (uint32_t)(l % c->nlines); if (c->tag[s] == l + 1) { c->tag[s] = 0; c->invals++; } }
}
#endif
