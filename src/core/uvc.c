#include "uvc.h"

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd16_be(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void zero(void *p, uint32_t n) { uint8_t *b = (uint8_t *)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }
static void copy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }

/* ------------------------------------------------------------------------------------------- descriptores */
static struct uvc_stream *vs_of(struct uvc_cfg *c, uint8_t iface)
{
    for (int i = 0; i < c->n_vs; i++) if (c->vs[i].iface == iface) return &c->vs[i];
    if (c->n_vs >= UVC_MAX_VS) return 0;
    struct uvc_stream *s = &c->vs[c->n_vs++];
    zero(s, sizeof *s); s->iface = iface;
    return s;
}

int uvc_parse(const uint8_t *d, uint32_t len, struct uvc_cfg *c)
{
    zero(c, sizeof *c);
    int cls = -1, sub = 0, in_vs = 0; uint8_t ifnum = 0, alt = 0;
    struct uvc_stream *vs = 0; struct uvc_format *fm = 0;
    uint32_t o = 0;
    while (o + 2 <= len) {
        uint32_t bl = d[o], ty = d[o + 1];
        if (bl < 2 || o + bl > len) break;
        const uint8_t *p = d + o;
        if (ty == 4 && bl >= 9) {                                                  /* interfaz */
            ifnum = p[2]; alt = p[3]; cls = p[5]; sub = p[6];
            in_vs = cls == 14 && sub == 2;
            vs = 0; fm = 0;
            if (cls == 14 && sub == 1) c->vc_iface = ifnum;
            if (in_vs) vs = vs_of(c, ifnum);
        } else if (ty == 0x24 && bl >= 3 && cls == 14 && sub == 1 && p[2] == 1 && bl >= 5) {   /* VC_HEADER */
            c->bcd_uvc = (uint16_t)rd16(p + 3);
        } else if (ty == 0x24 && in_vs && vs && bl >= 4) {
            uint8_t st = p[2];
            if ((st == 4 && bl >= 27) || (st == 6 && bl >= 11)) {                    /* FORMAT_UNCOMPRESSED / FORMAT_MJPEG */
                if (vs->n_fmt < UVC_MAX_FMT) {
                    fm = &vs->fmt[vs->n_fmt++];
                    zero(fm, sizeof *fm);
                    fm->index = p[3]; fm->n_frames = p[4]; fm->kind = st == 6 ? UVC_FMT_MJPEG : UVC_FMT_UNCOMP;
                    if (st == 4) { for (int i = 0; i < 4; i++) fm->fourcc[i] = (p[5 + i] >= 32 && p[5 + i] < 127) ? (char)p[5 + i] : '?'; fm->bpp = p[21]; fm->def_frame = p[22]; }
                    else { fm->fourcc[0] = 'M'; fm->fourcc[1] = 'J'; fm->fourcc[2] = 'P'; fm->fourcc[3] = 'G'; fm->def_frame = p[6]; }
                    fm->n_frames = 0;                                               /* se cuentan al leer los descriptores de tamano */
                } else fm = 0;
            } else if ((st == 5 || st == 7) && fm && bl >= 26) {                      /* FRAME_UNCOMPRESSED / FRAME_MJPEG */
                if (fm->n_frames < UVC_MAX_FRM) {
                    struct uvc_frame *f = &fm->frame[fm->n_frames++];
                    zero(f, sizeof *f);
                    f->index = p[3]; f->w = (uint16_t)rd16(p + 5); f->h = (uint16_t)rd16(p + 7);
                    f->max_size = rd32(p + 17); f->def_interval = rd32(p + 21);
                    uint8_t nt = p[25];
                    if (nt == 0 && bl >= 38) { f->min_interval = rd32(p + 26); f->max_interval = rd32(p + 30); f->step = rd32(p + 34); }
                    else {
                        for (uint32_t i = 0; i < nt && i < UVC_MAX_INT && 26 + 4 * (i + 1) <= bl; i++) f->interval[f->n_int++] = rd32(p + 26 + 4 * i);
                        if (f->n_int) { f->min_interval = f->max_interval = f->interval[0]; for (int i = 1; i < f->n_int; i++) { if (f->interval[i] < f->min_interval) f->min_interval = f->interval[i]; if (f->interval[i] > f->max_interval) f->max_interval = f->interval[i]; } }
                    }
                }
            }
        } else if (ty == 5 && in_vs && vs && bl >= 7) {                              /* extremo */
            uint8_t addr = p[2], attr = p[3]; uint16_t mps = (uint16_t)rd16(p + 4);
            if (addr & 0x80) {
                if ((attr & 3) == 1) {
                    if (alt > 0 && vs->n_alt < UVC_MAX_ALT) {
                        struct uvc_alt *a = &vs->alt[vs->n_alt++];
                        a->alt = alt; a->ep_addr = addr; a->ep_attr = attr; a->interval = p[6]; a->mps_raw = mps;
                        a->bytes = (uint32_t)(mps & 0x7FF) * (1u + ((mps >> 11) & 3u));
                    }
                } else if ((attr & 3) == 2) { vs->has_bulk = 1; vs->bulk_ep = addr; vs->bulk_mps = mps; }
            }
        }
        o += bl;
    }
    for (int i = 0; i < c->n_vs; i++) if (c->vs[i].n_fmt) c->found = 1;
    return c->found ? 0 : -1;
}

/* ------------------------------------------------------------------------------------------- eleccion */
static uint32_t absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

int uvc_choose(const struct uvc_cfg *c, uint32_t want_w, uint32_t want_h, uint32_t want_fps, int allow_raw, struct uvc_sel *s)
{
    int bv = -1, bf = -1, bfr = -1; uint32_t best = 0xFFFFFFFFu;
    for (int pass = 0; pass < 2 && bv < 0; pass++) {
        if (pass == 1 && !allow_raw) break;
        for (int v = 0; v < c->n_vs; v++) {
            const struct uvc_stream *vs = &c->vs[v];
            for (int f = 0; f < vs->n_fmt; f++) {
                const struct uvc_format *fm = &vs->fmt[f];
                if (pass == 0 && fm->kind != UVC_FMT_MJPEG) continue;
                if (pass == 1 && fm->kind != UVC_FMT_UNCOMP) continue;
                for (int r = 0; r < fm->n_frames; r++) {
                    const struct uvc_frame *fr = &fm->frame[r];
                    /* distancia: area pedida contra area del tamano; los menores que lo pedido cuestan un poco mas que los mayores */
                    uint32_t pa = (uint32_t)fr->w * fr->h, wa = want_w * want_h;   /* SOMA_340_RED_HOSTIL: uint16*uint16 era int: 65280*51664 desbordaba */
                    uint32_t dist = absdiff(pa, wa) / 64u + (pa < wa ? 1u : 0u);
                    if (fr->w == want_w && fr->h == want_h) dist = 0;
                    if (dist < best) { best = dist; bv = v; bf = f; bfr = r; }
                }
            }
        }
    }
    if (bv < 0) return -1;
    const struct uvc_frame *fr = &c->vs[bv].fmt[bf].frame[bfr];
    uint32_t want = want_fps ? 10000000u / want_fps : fr->def_interval, iv = fr->def_interval ? fr->def_interval : 333333u;
    if (fr->n_int) {                                  /* la mas cercana a la pedida; a igualdad, la mas lenta */
        uint32_t bd = 0xFFFFFFFFu;
        for (int i = 0; i < fr->n_int; i++) { uint32_t dd = absdiff(fr->interval[i], want); if (dd < bd || (dd == bd && fr->interval[i] > iv)) { bd = dd; iv = fr->interval[i]; } }
    } else if (fr->step && fr->max_interval >= fr->min_interval) {
        iv = want < fr->min_interval ? fr->min_interval : want > fr->max_interval ? fr->max_interval : want;
        iv = fr->min_interval + (iv - fr->min_interval) / fr->step * fr->step;
    }
    s->vs = bv; s->fmt = bf; s->frm = bfr; s->interval = iv;
    return 0;
}

void uvc_probe_fill(uint8_t *b, uint32_t len, uint8_t fmt_index, uint8_t frm_index, uint32_t interval)
{
    zero(b, len);
    wr16(b + 0, 1);                                   /* bmHint: dwFrameInterval fijo */
    b[2] = fmt_index; b[3] = frm_index; wr32(b + 4, interval);
}
uint32_t uvc_probe_max_payload(const uint8_t *b, uint32_t len) { return len >= 26 ? rd32(b + 22) : 0; }
uint32_t uvc_probe_max_frame(const uint8_t *b, uint32_t len) { return len >= 26 ? rd32(b + 18) : 0; }

int uvc_pick_alt(const struct uvc_stream *s, uint32_t need)
{
    int best = -1, big = -1;
    for (int i = 0; i < s->n_alt; i++) {
        if (big < 0 || s->alt[i].bytes > s->alt[big].bytes) big = i;
        if (s->alt[i].bytes >= need && (best < 0 || s->alt[i].bytes < s->alt[best].bytes)) best = i;
    }
    return best >= 0 ? best : big;
}

/* ------------------------------------------------------------------------------------------- ensamblador */
void uvc_asm_init(struct uvc_asm *a, uint8_t *b0, uint8_t *b1, uint32_t cap, uint32_t expect)
{
    zero(a, sizeof *a);
    a->buf[0] = b0; a->buf[1] = b1; a->cap = cap; a->expect = expect;
}

int uvc_mjpeg_valid(const uint8_t *d, uint32_t n)
{
    if (n < 6 || d[0] != 0xFF || d[1] != 0xD8) return 0;
    for (uint32_t i = 0; i < 16 && i + 2 <= n; i++) if (d[n - 2 - i] == 0xFF && d[n - 1 - i] == 0xD9) return 1;
    return 0;
}

/* termina la trama en curso: la deja en `done` si es valida */
static int finish(struct uvc_asm *a)
{
    int r = UVC_FRAME_NONE;
    if (a->len || a->bad) {
        int ok = !a->bad && a->len;
        if (ok) {
            if (a->expect) { if (a->len < a->expect) { a->fr_short++; ok = 0; } else if (a->len > a->expect) a->len = a->expect; }
            else if (!uvc_mjpeg_valid(a->buf[a->cur], a->len)) { a->fr_short++; ok = 0; }
        }
        if (ok) { a->done = a->buf[a->cur]; a->done_len = a->len; a->cur ^= 1; a->fr_ok++; r = UVC_FRAME_OK; }
        else { a->fr_bad++; r = UVC_FRAME_BAD; }
    }
    a->len = 0; a->bad = 0;
    return r;
}

int uvc_asm_feed(struct uvc_asm *a, const uint8_t *p, uint32_t n)
{
    a->pk++;
    if (n < 2) { a->pk_empty++; return UVC_FRAME_NONE; }
    uint32_t hl = p[0];
    if (hl < 2 || hl > n || hl > 12) { a->pk_hdr_err++; a->bad = 1; return UVC_FRAME_NONE; }
    uint8_t fl = p[1], fid = fl & 1u;
    int ret = UVC_FRAME_NONE;
    if (a->have_fid && fid != a->fid) ret = finish(a);                              /* cambio de FID: la trama anterior acabo */
    a->have_fid = 1; a->fid = fid;
    if (fl & 0x40u) { a->pk_err_flag++; a->bad = 1; }
    uint32_t dl = n - hl;
    if (dl && !a->bad) {
        if (a->len + dl > a->cap) { a->fr_over++; a->bad = 1; }
        else { copy(a->buf[a->cur] + a->len, p + hl, dl); a->len += dl; }
    }
    if (fl & 2u) { int r2 = finish(a); if (r2 != UVC_FRAME_NONE) ret = r2; }          /* EOF */
    return ret;
}

/* ------------------------------------------------------------------------------------------- DHT estandar (ITU T.81, anexo K) */
static const uint8_t DC_L_C[16] = { 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };
static const uint8_t DC_C_C[16] = { 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
static const uint8_t DC_V[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
static const uint8_t AC_L_C[16] = { 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d };
static const uint8_t AC_L_V[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };
static const uint8_t AC_C_C[16] = { 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77 };
static const uint8_t AC_C_V[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };

static uint32_t put_table(uint8_t *o, uint32_t n, uint8_t tc_th, const uint8_t *counts, const uint8_t *vals, uint32_t nv)
{
    o[n++] = tc_th;
    for (int i = 0; i < 16; i++) o[n++] = counts[i];
    for (uint32_t i = 0; i < nv; i++) o[n++] = vals[i];
    return n;
}
/* escribe en out (420 bytes) el segmento DHT con las cuatro tablas estandar; devuelve su longitud */
uint32_t uvc_std_dht_build(uint8_t *out)
{
    uint32_t n = 0;
    out[n++] = 0xFF; out[n++] = 0xC4; out[n++] = 0x01; out[n++] = 0xA2;                 /* longitud 418 */
    n = put_table(out, n, 0x00, DC_L_C, DC_V, 12);
    n = put_table(out, n, 0x10, AC_L_C, AC_L_V, 162);
    n = put_table(out, n, 0x01, DC_C_C, DC_V, 12);
    n = put_table(out, n, 0x11, AC_C_C, AC_C_V, 162);
    return n;
}

int32_t uvc_mjpeg_add_dht(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (n < 4 || in[0] != 0xFF || in[1] != 0xD8) return 0;
    uint32_t o = 2, sos = 0;
    while (o + 4 <= n) {
        if (in[o] != 0xFF) return 0;
        uint8_t m = in[o + 1];
        if (m == 0xFF) { o++; continue; }
        if (m == 0xC4) return 0;                                                       /* ya trae tablas Huffman */
        if (m == 0xDA) { sos = o; break; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { o += 2; continue; }
        uint32_t l = rd16_be(in + o + 2);
        if (l < 2) return 0;
        o += 2 + l;
    }
    if (!sos) return 0;
    if (n + 420u > cap) return -1;
    copy(out, in, sos);
    uint32_t k = sos + uvc_std_dht_build(out + sos);
    copy(out + k, in + sos, n - sos);
    return (int32_t)(n + 420u);
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
void uvc_yuy2_to_rgb(const uint8_t *y, uint32_t w, uint32_t h, uint32_t *out, uint32_t stride)
{
    for (uint32_t r = 0; r < h; r++) {
        const uint8_t *s = y + (uint64_t)r * w * 2u; uint32_t *d = out + (uint64_t)r * stride;
        for (uint32_t x = 0; x + 1 < w; x += 2) {
            int y0 = s[0], u = s[1] - 128, y1 = s[2], v = s[3] - 128;
            int rr = (359 * v) >> 8, gg = (88 * u + 183 * v) >> 8, bb = (454 * u) >> 8;
            d[x]     = (uint32_t)clamp8(y0 + rr) << 16 | (uint32_t)clamp8(y0 - gg) << 8 | clamp8(y0 + bb);
            d[x + 1] = (uint32_t)clamp8(y1 + rr) << 16 | (uint32_t)clamp8(y1 - gg) << 8 | clamp8(y1 + bb);
            s += 4;
        }
    }
}
