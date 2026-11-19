#ifndef SOMA_CKPTIO_H
#define SOMA_CKPTIO_H
/* PARCHE 120 (Fase 2, persistencia): punto de control (checkpoint) del VFS a prueba de cortes y de corrupcion.
 *
 * Antes: el guardado invalidaba el marcador, copiaba y escribia el marcador sin barreras (una cache de disco que reordena podia dejar el marcador
 * durable con la copia a medias), sin comprobar nada al restaurar (un checkpoint corrupto pisaba el sistema de archivos vivo) y una restauracion
 * interrumpida dejaba el VFS mezclado, mitad nuevo y mitad viejo.
 * Ahora:
 *   - guardar: marcador a cero -> barrera -> copia (calculando el CRC32 de todo) -> barrera -> marcador V3 con CRC -> barrera. Un corte deja un
 *     marcador invalido o un checkpoint completo y verificable; NUNCA uno valido a medias.
 *   - restaurar: primero se VERIFICA el CRC (si falla, el VFS vivo no se toca); despues se escribe una marca de intencion, se invalida el diario del
 *     VFS (para que no reaplique un guardado viejo), se copia y se borra la marca. Si hay un corte, `ckpt_recover` (en el arranque) rehace la
 *     restauracion entera: el resultado es el VFS anterior o el checkpoint COMPLETO, nunca una mezcla.
 * PARCHE 130 (dos generaciones A/B): hay DOS ranuras de checkpoint. Guardar escribe SIEMPRE en la ranura que no contiene el checkpoint valido mas
 *   reciente (por numero de secuencia grabado en el marcador), de modo que durante todo el guardado sigue habiendo un checkpoint valido (el anterior).
 *   Restaurar y recuperar usan el valido mas reciente; si el mas reciente esta corrupto cae al otro. Una ranura puede estar partida en dos tramos de disco
 *   (`base` para los primeros `split` sectores y `base2` para el resto), para aprovechar huecos libres sin cambiar el mapa del disco.
 * Codigo puro (sin dependencias del nucleo) para probarlo en el host con un corte tras cada escritura y con una cache volatil que reordena. */
#include "slotio.h"

#define CKPT_MAGIC3   "MYOS_CHECKPOINT_V3"
#define CKPT_MAGIC2   "MYOS_CHECKPOINT_V2"     /* formato antiguo: sin CRC; se acepta pero no se puede verificar */
#define CKPT_INTENT   "SOMA_RESTORE_PENDING"

struct ckpt_marker {
    char     magic[24];
    uint32_t seq;
    uint32_t nsec;
    uint32_t crc;           /* CRC32 de los nsec sectores copiados */
    uint32_t hdr_crc;       /* CRC32 de los 36 bytes anteriores */
    uint8_t  pad[512 - 40];
};

static inline int ckpt_streq(const char *a, const char *lit) { for (size_t i = 0; lit[i]; ++i) if (a[i] != lit[i]) return 0; return 1; }

/* PARCHE 130: una ranura = su marcador + sus sectores de datos (en uno o dos tramos) */
struct ckpt_slot { uint64_t marker, base, base2; uint32_t split; };
static inline uint64_t ckpt_lba(const struct ckpt_slot *sl, uint32_t s) { return s < sl->split ? sl->base + s : sl->base2 + (s - sl->split); }
static inline struct ckpt_slot ckpt_linear(uint64_t marker, uint64_t base, uint32_t nsec) { struct ckpt_slot x; x.marker = marker; x.base = base; x.base2 = 0; x.split = nsec; return x; }

/* 0 = ranura sin checkpoint valido; 1 = formato antiguo (valido sin poder verificar el contenido); 2 = V3 con el CRC verificado. *seq = secuencia (0 si V2). */
static inline int ckpt_slot_info(const struct slotdev *d, const struct ckpt_slot *sl, uint32_t nsec, uint32_t *seq)
{
    struct ckpt_marker m; uint8_t b[SLOTIO_SEC];
    if (seq) *seq = 0;
    if (d->rd(d->ctx, sl->marker, &m) != 0) return 0;
    if (ckpt_streq(m.magic, CKPT_MAGIC2)) { for (int i = 18; i < 24; ++i) if (m.magic[i]) return 0; return 1; }
    if (!ckpt_streq(m.magic, CKPT_MAGIC3)) return 0;
    if (m.hdr_crc != slotio_crc32(0, &m, 36) || m.nsec != nsec) return 0;
    uint32_t crc = 0;
    for (uint32_t s = 0; s < nsec; ++s) { if (d->rd(d->ctx, ckpt_lba(sl, s), b) != 0) return 0; crc = slotio_crc32(crc, b, sizeof b); }
    if (crc != m.crc) return 0;
    if (seq) *seq = m.seq;
    return 2;
}

/* La ranura valida mas reciente (o NULL). `b` puede ser NULL (una sola ranura). *level recibe el nivel de validez. */
static inline const struct ckpt_slot *ckpt_pick(const struct slotdev *d, const struct ckpt_slot *a, const struct ckpt_slot *b, uint32_t nsec, int *level, uint32_t *seq_out)
{
    uint32_t sa = 0, sb = 0;
    int la = ckpt_slot_info(d, a, nsec, &sa), lb = b ? ckpt_slot_info(d, b, nsec, &sb) : 0;
    const struct ckpt_slot *w = 0; int lw = 0; uint32_t sw = 0;
    if (la) { w = a; lw = la; sw = sa; }
    if (lb && (!w || (int32_t)(sb - sw) > 0)) { w = b; lw = lb; sw = sb; }
    if (level) *level = lw;
    if (seq_out) *seq_out = sw;
    return w;
}

/* Guarda en la ranura que NO es la mas reciente; mientras dura el guardado esa sigue valida. */
static inline int ckpt_save(const struct slotdev *d, uint64_t src, const struct ckpt_slot *a, const struct ckpt_slot *b, uint32_t nsec)
{
    int lv; uint32_t seq0;
    const struct ckpt_slot *best = ckpt_pick(d, a, b, nsec, &lv, &seq0);
    const struct ckpt_slot *dst = (!best || !b) ? a : (best == a ? b : a);
    uint32_t seq = best ? seq0 + 1 : 1;
    uint8_t buf[SLOTIO_SEC]; slotio_zero(buf, sizeof buf);
    if (d->wr(d->ctx, dst->marker, buf) != 0) return -1;             /* 1. invalidar la ranura de destino (la otra sigue valida) */
    if (slotio_barrier(d) != 0) return -1;
    uint32_t crc = 0;
    for (uint32_t s = 0; s < nsec; ++s) {                            /* 2. copiar */
        if (d->rd(d->ctx, src + s, buf) != 0) return -1;
        crc = slotio_crc32(crc, buf, sizeof buf);
        if (d->wr(d->ctx, ckpt_lba(dst, s), buf) != 0) return -1;
    }
    if (slotio_barrier(d) != 0) return -1;                           /* 3. la copia es durable ANTES del marcador */
    struct ckpt_marker m; slotio_zero(&m, sizeof m);
    slotio_copy(m.magic, CKPT_MAGIC3, 18);
    m.seq = seq; m.nsec = nsec; m.crc = crc; m.hdr_crc = slotio_crc32(0, &m, 36);
    if (d->wr(d->ctx, dst->marker, &m) != 0) return -1;              /* 4. marcador el ULTIMO */
    return slotio_barrier(d);
}

/* 0 = ninguna ranura valida; 1 = solo formato antiguo; 2 = V3 verificado (de la mas reciente) */
static inline int ckpt_valid(const struct slotdev *d, const struct ckpt_slot *a, const struct ckpt_slot *b, uint32_t nsec)
{
    int lv = 0; ckpt_pick(d, a, b, nsec, &lv, 0); return lv;
}

static inline int ckpt_copy_back(const struct slotdev *d, const struct ckpt_slot *sl, uint64_t live, uint32_t nsec)
{
    uint8_t buf[SLOTIO_SEC];
    for (uint32_t s = 0; s < nsec; ++s) { if (d->rd(d->ctx, ckpt_lba(sl, s), buf) != 0 || d->wr(d->ctx, live + s, buf) != 0) return -1; }
    return slotio_barrier(d);
}

/* Restaura el checkpoint mas reciente valido sobre el VFS vivo. -1: no hay checkpoint valido (el VFS no se toca); -2: fallo de E/S. */
static inline int ckpt_restore(const struct slotdev *d, const struct ckpt_slot *a, const struct ckpt_slot *b, uint64_t live, uint32_t nsec, uint64_t intent_lba, uint64_t journal_lba)
{
    const struct ckpt_slot *w = ckpt_pick(d, a, b, nsec, 0, 0);
    if (!w) return -1;
    uint8_t buf[SLOTIO_SEC]; slotio_zero(buf, sizeof buf);
    slotio_copy(buf, CKPT_INTENT, 20);
    if (d->wr(d->ctx, intent_lba, buf) != 0 || slotio_barrier(d) != 0) return -2;    /* desde aqui, un corte se REHACE en el arranque */
    slotio_zero(buf, sizeof buf);
    if (d->wr(d->ctx, journal_lba, buf) != 0 || slotio_barrier(d) != 0) return -2;   /* que un guardado viejo no se reaplique sobre lo restaurado */
    if (ckpt_copy_back(d, w, live, nsec) != 0) return -2;
    if (d->wr(d->ctx, intent_lba, buf) != 0 || slotio_barrier(d) != 0) return -2;    /* restauracion terminada */
    return 0;
}

/* Arranque, ANTES de recuperar el diario y de leer archivos. 0 = nada pendiente; 1 = restauracion interrumpida completada; 2 = marca de intencion sin
 * checkpoint valido (se borra; el VFS queda como esta). */
static inline int ckpt_recover(const struct slotdev *d, const struct ckpt_slot *a, const struct ckpt_slot *b, uint64_t live, uint32_t nsec, uint64_t intent_lba, uint64_t journal_lba)
{
    uint8_t buf[SLOTIO_SEC];
    if (d->rd(d->ctx, intent_lba, buf) != 0 || !ckpt_streq((const char *)buf, CKPT_INTENT)) return 0;
    int rc = 2;
    const struct ckpt_slot *w = ckpt_pick(d, a, b, nsec, 0, 0);
    if (w) {
        slotio_zero(buf, sizeof buf);
        if (d->wr(d->ctx, journal_lba, buf) != 0 || slotio_barrier(d) != 0) return -2;
        if (ckpt_copy_back(d, w, live, nsec) != 0) return -2;
        rc = 1;
    }
    slotio_zero(buf, sizeof buf);
    if (d->wr(d->ctx, intent_lba, buf) != 0 || slotio_barrier(d) != 0) return -2;
    return rc;
}
#endif
