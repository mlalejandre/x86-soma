/* SOMA_252_VIDEO_NATIVO: lector de MP4 (ISO BMFF sin fragmentar) para el reproductor nativo. Logica pura: sin memoria
 * dinamica ni E/S, todo acotado por las longitudes que recibe (se prueba en el anfitrion con ASan: tests/test_mp4_252.c).
 *
 * Del "moov" saca, por pista (una de video H.264 y una de audio AAC): escala de tiempo, dimensiones, la configuracion del
 * decodificador (avcC / esds) y las tablas de muestras (stts, ctts, stsc, stsz, stco/co64, stss). mp4_build las convierte
 * en una lista plana {desplazamiento en el archivo, tamano, instante, es clave}. */
#ifndef SOMA_MP4_H
#define SOMA_MP4_H
#include <stdint.h>

struct mp4_sample { uint64_t off; uint64_t pts_us; uint32_t size; uint32_t key; };

struct mp4_tab { const uint8_t *p; uint32_t n; };          /* cuerpo de una caja (tras version y flags) */
struct mp4_track {
    int present;
    uint32_t timescale; uint64_t duration;                 /* en unidades de timescale */
    uint32_t width, height;                                /* video */
    uint32_t channels, rate;                               /* audio (segun la caja mp4a; la verdad esta en el esds) */
    struct mp4_tab cfg;                                    /* avcC (video) o esds (audio) */
    struct mp4_tab stts, ctts, stsc, stsz, stco, co64, stss;
    uint32_t nsamples;
};
struct mp4_info { struct mp4_track v, a; uint64_t duration_ms; };

static inline uint32_t mp4_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline uint64_t mp4_be64(const uint8_t *p) { return ((uint64_t)mp4_be32(p) << 32) | mp4_be32(p + 4); }
static inline int mp4_is(const uint8_t *p, const char *t) { return p[0] == (uint8_t)t[0] && p[1] == (uint8_t)t[1] && p[2] == (uint8_t)t[2] && p[3] == (uint8_t)t[3]; }

/* Cabecera de caja en b[0..n): 1 y (tipo en b+4, tamano total, tamano de la cabecera); 0 si faltan bytes; -1 si no es valida.
 * "rest" = bytes que quedan hasta el final del contenedor (para las cajas de tamano 0 = "hasta el final"). */
static inline int mp4_box(const uint8_t *b, uint64_t n, uint64_t rest, uint64_t *size, uint32_t *hdr)
{
    if (n < 8) return 0;
    uint64_t s = mp4_be32(b); uint32_t h = 8;
    if (s == 1) { if (n < 16) return 0; s = mp4_be64(b + 8); h = 16; }
    else if (s == 0) s = rest;
    if (s < h || s > rest) return -1;
    *size = s; *hdr = h;
    return 1;
}

/* Busca el "moov" recorriendo las cajas de primer nivel. "have" bytes del archivo estan en b, que empieza en el
 * desplazamiento "at" (donde empieza una caja). Devuelve:
 *    1  moov encontrado: *off y *size (del archivo);
 *    0  hay que seguir mirando en *off (el principio de la siguiente caja, que aun no esta en b);
 *   -1  esto no es un MP4. */
static inline int mp4_find_moov(const uint8_t *b, uint64_t have, uint64_t at, uint64_t file_size, uint64_t *off, uint64_t *size)
{
    uint64_t p = 0;
    for (int guard = 0; guard < 64; ++guard) {
        uint64_t s; uint32_t h;
        if (at + p >= file_size) return -1;
        if (have - p < 16 && have - p < file_size - at - p) { *off = at + p; return 0; }
        int r = mp4_box(b + p, have - p, file_size - at - p, &s, &h);
        if (r < 0) return -1;
        if (r == 0) { *off = at + p; return 0; }
        const uint8_t *t = b + p + 4;
        for (int k = 0; k < 4; ++k) if (t[k] < 0x20 || t[k] > 0x7E) return -1;             /* un tipo de caja son 4 caracteres */
        if (mp4_is(t, "moov")) { *off = at + p; *size = s; return 1; }
        if (at == 0 && p == 0 && !mp4_is(t, "ftyp") && !mp4_is(t, "styp") && !mp4_is(t, "free") && !mp4_is(t, "skip") && !mp4_is(t, "wide") && !mp4_is(t, "mdat")) return -1;
        p += s;
        if (p >= have) { *off = at + p; return 0; }
    }
    return -1;
}

static inline void mp4_stbl(const uint8_t *b, uint64_t n, struct mp4_track *t, int *is_video, int *is_audio)
{
    uint64_t p = 0;
    while (p + 8 <= n) {
        uint64_t s; uint32_t h;
        if (mp4_box(b + p, n - p, n - p, &s, &h) != 1) return;
        const uint8_t *ty = b + p + 4, *c = b + p + h; uint64_t cn = s - h;
        if (cn >= 4 && cn <= 0xFFFFFFFFu) {
            struct mp4_tab tab; tab.p = c + 4; tab.n = (uint32_t)cn - 4;
            if (mp4_is(ty, "stts")) t->stts = tab;
            else if (mp4_is(ty, "ctts")) t->ctts = tab;
            else if (mp4_is(ty, "stsc")) t->stsc = tab;
            else if (mp4_is(ty, "stsz")) t->stsz = tab;
            else if (mp4_is(ty, "stco")) t->stco = tab;
            else if (mp4_is(ty, "co64")) t->co64 = tab;
            else if (mp4_is(ty, "stss")) t->stss = tab;
            else if (mp4_is(ty, "stsd") && cn >= 16) {                                    /* version+flags, n entradas, 1a entrada */
                const uint8_t *e = c + 8; uint64_t en = cn - 8;
                uint64_t es = mp4_be32(e);
                if (es >= 8 && es <= en) {
                    if ((mp4_is(e + 4, "avc1") || mp4_is(e + 4, "avc3")) && es >= 86) {
                        *is_video = 1;
                        t->width = ((uint32_t)e[32] << 8) | e[33]; t->height = ((uint32_t)e[34] << 8) | e[35];
                        uint64_t q = 86;                                                  /* cajas hijas tras los 78 bytes de VisualSampleEntry */
                        while (q + 8 <= es) {
                            uint64_t s2; uint32_t h2;
                            if (mp4_box(e + q, es - q, es - q, &s2, &h2) != 1) break;
                            if (mp4_is(e + q + 4, "avcC") && s2 - h2 <= 0xFFFFFFu) { t->cfg.p = e + q + h2; t->cfg.n = (uint32_t)(s2 - h2); }
                            q += s2;
                        }
                    } else if (mp4_is(e + 4, "mp4a") && es >= 36) {
                        *is_audio = 1;
                        uint32_t ver = ((uint32_t)e[16] << 8) | e[17];
                        t->channels = ((uint32_t)e[24] << 8) | e[25]; t->rate = ((uint32_t)e[32] << 8) | e[33];
                        uint64_t q = 36 + (ver == 1 ? 16u : ver == 2 ? 36u : 0u);
                        while (q + 8 <= es) {
                            uint64_t s2; uint32_t h2;
                            if (mp4_box(e + q, es - q, es - q, &s2, &h2) != 1) break;
                            if (mp4_is(e + q + 4, "esds") && s2 - h2 >= 4 && s2 - h2 <= 0xFFFFFFu) { t->cfg.p = e + q + h2 + 4; t->cfg.n = (uint32_t)(s2 - h2) - 4; }
                            q += s2;
                        }
                    }
                }
            }
        }
        p += s;
    }
}

static inline void mp4_walk(const uint8_t *b, uint64_t n, int depth, struct mp4_info *o, struct mp4_track *cur, int *kind)
{
    uint64_t p = 0;
    while (p + 8 <= n) {
        uint64_t s; uint32_t h;
        if (mp4_box(b + p, n - p, n - p, &s, &h) != 1) return;
        const uint8_t *ty = b + p + 4, *c = b + p + h; uint64_t cn = s - h;
        if (mp4_is(ty, "trak") && depth == 0) {
            struct mp4_track t; int k = 0;
            for (uint32_t i = 0; i < sizeof t; ++i) ((uint8_t *)&t)[i] = 0;
            mp4_walk(c, cn, 1, o, &t, &k);
            if (t.stsz.n >= 8 && t.timescale && t.cfg.n) {
                uint32_t fixed = mp4_be32(t.stsz.p), cnt = mp4_be32(t.stsz.p + 4);
                if (!fixed && (uint64_t)cnt * 4 > t.stsz.n - 8) cnt = (t.stsz.n - 8) / 4;
                t.nsamples = cnt; t.present = 1;
                if (k == 1 && !o->v.present) o->v = t;
                else if (k == 2 && !o->a.present) o->a = t;
            }
        } else if (cur && depth >= 1 && depth < 5 && (mp4_is(ty, "mdia") || mp4_is(ty, "minf"))) {
            mp4_walk(c, cn, depth + 1, o, cur, kind);
        } else if (cur && mp4_is(ty, "stbl")) {
            int v = 0, a = 0; mp4_stbl(c, cn, cur, &v, &a);
            if (v) *kind = 1; else if (a) *kind = 2;
        } else if (cur && mp4_is(ty, "mdhd") && cn >= 24) {
            if (c[0] == 1 && cn >= 36) { cur->timescale = mp4_be32(c + 20); cur->duration = mp4_be64(c + 24); }
            else { cur->timescale = mp4_be32(c + 12); cur->duration = mp4_be32(c + 16); }
        }
        p += s;
    }
}

/* moov[0..len) es la caja moov entera (con su cabecera). 1 si hay al menos una pista que se puede reproducir. */
static inline int mp4_parse_moov(const uint8_t *moov, uint64_t len, struct mp4_info *o)
{
    for (uint32_t i = 0; i < sizeof *o; ++i) ((uint8_t *)o)[i] = 0;
    uint64_t s; uint32_t h;
    if (mp4_box(moov, len, len, &s, &h) != 1 || !mp4_is(moov + 4, "moov")) return 0;
    int k = 0;
    mp4_walk(moov + h, s - h, 0, o, 0, &k);
    uint64_t dv = o->v.present ? o->v.duration * 1000u / o->v.timescale : 0, da = o->a.present ? o->a.duration * 1000u / o->a.timescale : 0;
    o->duration_ms = dv > da ? dv : da;
    return o->v.present || o->a.present;
}

/* Lista plana de las muestras de una pista. Devuelve cuantas ha escrito (<= max y <= nsamples); 0 si las tablas no cuadran. */
static inline uint32_t mp4_build(const struct mp4_track *t, struct mp4_sample *out, uint32_t max)
{
    if (!t->present || t->stsz.n < 8 || t->stsc.n < 4 || (t->stco.n < 4 && t->co64.n < 4) || t->stts.n < 4) return 0;
    uint32_t n = t->nsamples < max ? t->nsamples : max;
    uint32_t fixed = mp4_be32(t->stsz.p);
    int c64 = t->co64.n >= 4;
    const uint8_t *co = c64 ? t->co64.p : t->stco.p;
    uint32_t nco = mp4_be32(co), con = c64 ? t->co64.n : t->stco.n;
    if ((uint64_t)nco * (c64 ? 8u : 4u) > con - 4) nco = (con - 4) / (c64 ? 8u : 4u);
    uint32_t nsc = mp4_be32(t->stsc.p); if ((uint64_t)nsc * 12 > t->stsc.n - 4) nsc = (t->stsc.n - 4) / 12;
    uint32_t ntt = mp4_be32(t->stts.p); if ((uint64_t)ntt * 8 > t->stts.n - 4) ntt = (t->stts.n - 4) / 8;
    uint32_t nct = t->ctts.n >= 4 ? mp4_be32(t->ctts.p) : 0; if (nct && (uint64_t)nct * 8 > t->ctts.n - 4) nct = (t->ctts.n - 4) / 8;
    int ctts_v1 = t->ctts.n >= 4 && t->ctts.p[-4] == 1;
    uint32_t nss = t->stss.n >= 4 ? mp4_be32(t->stss.p) : 0; if (nss && (uint64_t)nss * 4 > t->stss.n - 4) nss = (t->stss.n - 4) / 4;
    if (!nco || !nsc || !ntt) return 0;
    /* cursores */
    uint32_t sc = 0, chunk = 0, in_chunk = 0, per = mp4_be32(t->stsc.p + 4 + 4);         /* muestras por trozo de la entrada sc */
    uint64_t off = c64 ? mp4_be64(co + 4) : mp4_be32(co + 4);
    uint32_t tt = 0, tt_left = mp4_be32(t->stts.p + 4), tt_delta = mp4_be32(t->stts.p + 8);
    uint32_t ct = 0, ct_left = nct ? mp4_be32(t->ctts.p + 4) : 0; int64_t ct_off = nct ? (ctts_v1 ? (int32_t)mp4_be32(t->ctts.p + 8) : (int64_t)mp4_be32(t->ctts.p + 8)) : 0;
    uint32_t ss = 0; uint64_t dts = 0; int64_t first_cts = -1;
    if (!per) return 0;
    uint32_t i = 0;
    for (; i < n; ++i) {
        uint32_t size = fixed ? fixed : mp4_be32(t->stsz.p + 8 + (uint64_t)i * 4);
        while (tt_left == 0) { if (++tt >= ntt) { tt_delta = tt_delta ? tt_delta : 1; tt_left = 0xFFFFFFFFu; break; } tt_left = mp4_be32(t->stts.p + 4 + (uint64_t)tt * 8); tt_delta = mp4_be32(t->stts.p + 8 + (uint64_t)tt * 8); }
        while (nct && ct_left == 0) { if (++ct >= nct) { ct_off = 0; ct_left = 0xFFFFFFFFu; break; } ct_left = mp4_be32(t->ctts.p + 4 + (uint64_t)ct * 8); uint32_t raw = mp4_be32(t->ctts.p + 8 + (uint64_t)ct * 8); ct_off = ctts_v1 ? (int64_t)(int32_t)raw : (int64_t)raw; }
        int64_t cts = (int64_t)dts + ct_off; if (cts < 0) cts = 0;
        if (first_cts < 0) first_cts = cts;                                               /* el primer fotograma se muestra en 0 */
        out[i].off = off; out[i].size = size;
        out[i].pts_us = (uint64_t)(cts >= first_cts ? cts - first_cts : 0) * 1000000u / t->timescale;
        if (nss) { while (ss < nss && mp4_be32(t->stss.p + 4 + (uint64_t)ss * 4) < i + 1) ss++; out[i].key = ss < nss && mp4_be32(t->stss.p + 4 + (uint64_t)ss * 4) == i + 1; }
        else out[i].key = 1;
        dts += tt_delta; tt_left--; if (nct) ct_left--;
        off += size;
        if (++in_chunk >= per) {                                                          /* siguiente trozo */
            in_chunk = 0;
            if (++chunk >= nco) { i++; break; }
            off = c64 ? mp4_be64(co + 4 + (uint64_t)chunk * 8) : mp4_be32(co + 4 + (uint64_t)chunk * 4);
            if (sc + 1 < nsc && mp4_be32(t->stsc.p + 4 + (uint64_t)(sc + 1) * 12) == chunk + 1) { sc++; per = mp4_be32(t->stsc.p + 4 + (uint64_t)sc * 12 + 4); if (!per) { i++; break; } }
        }
    }
    return i;
}

/* avcC -> SPS y PPS en formato Annex B (00 00 00 01 ...) y cuantos bytes ocupa la longitud de cada NAL (1, 2 o 4).
 * Devuelve los bytes escritos en out (0 = avcC no valido). */
static inline uint32_t mp4_avcc(const uint8_t *c, uint32_t n, uint8_t *out, uint32_t max, uint32_t *nal_len)
{
    if (n < 7 || c[0] != 1) return 0;
    *nal_len = (uint32_t)(c[4] & 3) + 1;
    if (*nal_len == 3) return 0;
    uint32_t p = 5, o = 0;
    for (int pass = 0; pass < 2; ++pass) {
        if (p >= n) return pass ? o : 0;
        uint32_t cnt = pass == 0 ? (uint32_t)(c[p] & 31) : c[p]; p++;
        for (uint32_t i = 0; i < cnt; ++i) {
            if (p + 2 > n) return 0;
            uint32_t l = ((uint32_t)c[p] << 8) | c[p + 1]; p += 2;
            if (p + l > n || o + 4 + l > max) return 0;
            out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 1;
            for (uint32_t k = 0; k < l; ++k) out[o++] = c[p + k];
            p += l;
        }
    }
    return o;
}

/* esds (sin version/flags) -> AudioSpecificConfig. Devuelve su longitud (0 = no esta) y deja el puntero en *asc. */
static inline uint32_t mp4_esds_asc(const uint8_t *c, uint32_t n, const uint8_t **asc)
{
    uint32_t p = 0;
    while (p + 2 <= n) {
        uint8_t tag = c[p++]; uint32_t len = 0; int k = 0;
        do { if (p >= n) return 0; len = (len << 7) | (c[p] & 0x7Fu); } while ((c[p++] & 0x80) && ++k < 4);
        if (len > n - p) len = n - p;
        if (tag == 0x03) { if (len < 3) return 0; uint8_t fl = c[p + 2]; p += 3; if (fl & 0x80) p += 2; if (fl & 0x40) { if (p >= n) return 0; p += 1u + c[p]; } if (fl & 0x20) p += 2; }
        else if (tag == 0x04) { if (len < 13) return 0; p += 13; }
        else if (tag == 0x05) { *asc = c + p; return len; }
        else p += len;
    }
    return 0;
}

/* Indice de la ultima muestra clave con pts <= t (busqueda binaria por tiempo y retroceso hasta una clave). */
static inline uint32_t mp4_seek_index(const struct mp4_sample *s, uint32_t n, uint64_t t_us, int need_key)
{
    if (!n) return 0;
    uint32_t lo = 0, hi = n;
    while (hi - lo > 1) { uint32_t m = lo + (hi - lo) / 2; if (s[m].pts_us <= t_us) lo = m; else hi = m; }
    if (need_key) while (lo && !s[lo].key) lo--;
    return lo;
}
#endif
