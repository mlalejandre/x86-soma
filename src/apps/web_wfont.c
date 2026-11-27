/* SOMA_348_FUENTES_WEB: fuentes web (TrueType, WOFF 1.0 y WOFF 2.0) a un sfnt TrueType para src/fonts/vfont.h.
 *
 * WOFF 1.0 (W3C, 2012): cada tabla va sola, comprimida con zlib si gana algo.
 * WOFF 2.0 (W3C, 2018): todas las tablas en un unico flujo Brotli. glyf y loca suelen ir "transformadas": glyf en 7 flujos
 * separados (contornos, puntos, banderas, coordenadas en tripletes, compuestos, cajas, instrucciones) y loca vacia; hmtx puede ir
 * sin los lsb (se sacan de las cajas). Aqui se reconstruyen glyf (con coordenadas de 16 bits, sin compactar), loca larga y hmtx.
 *
 * Los datos vienen de Internet: cada lectura comprueba el limite (lectura fuera = error, nunca acceso fuera), los tamanos tienen
 * tope (WFF_MAX_IN, WFF_MAX_OUT) y el numero de glifos, contornos y puntos tambien. */
#include "web_wfont.h"
#include "web_util.h"
#include "web_inflate.h"
#include <brotli/decode.h>

void *kmalloc(size_t n);
void kfree(void *p);

enum { WFF_OK = 0, WFF_E_FMT = -1, WFF_E_TRUNC = -2, WFF_E_CFF = -3, WFF_E_MEM = -4, WFF_E_BIG = -5, WFF_E_ZLIB = -6, WFF_E_BROTLI = -7,
       WFF_E_GLYF = -8, WFF_E_TABLES = -9, WFF_E_TTC = -10 };
const char *wff_error(int code)
{
    switch (code) {
    case WFF_E_FMT: return "formato de fuente desconocido";
    case WFF_E_TRUNC: return "fuente cortada o con datos fuera de sitio";
    case WFF_E_CFF: return "fuente OpenType CFF (no TrueType)";
    case WFF_E_MEM: return "sin memoria";
    case WFF_E_BIG: return "fuente demasiado grande";
    case WFF_E_ZLIB: return "WOFF: zlib danado";
    case WFF_E_BROTLI: return "WOFF2: Brotli danado";
    case WFF_E_GLYF: return "WOFF2: glyf transformada danada";
    case WFF_E_TABLES: return "faltan tablas TrueType";
    case WFF_E_TTC: return "coleccion de fuentes (no admitida)";
    default: return "error";
    }
}

/* ------------------------------------------------------------------ lectura con limites */
struct rd { const uint8_t *p; uint32_t n, o; int err; };
static uint32_t r8(struct rd *r) { if (r->o >= r->n) { r->err = 1; return 0; } return r->p[r->o++]; }
static uint32_t r16(struct rd *r) { uint32_t a = r8(r); return (a << 8) | r8(r); }
static uint32_t r32(struct rd *r) { uint32_t a = r16(r); return (a << 16) | r16(r); }
static int32_t rs16(struct rd *r) { return (int32_t)(int16_t)r16(r); }
static void rskip(struct rd *r, uint32_t k) { if (r->o > r->n || k > r->n - r->o) { r->err = 1; r->o = r->n; } else r->o += k; }
static uint32_t base128(struct rd *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t b = r8(r);
        if (i == 0 && b == 0x80) { r->err = 1; return 0; }          /* sin ceros a la izquierda */
        if (v & 0xFE000000u) { r->err = 1; return 0; }             /* desbordaria 32 bits */
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    r->err = 1; return 0;
}
static uint32_t u255(struct rd *r)
{
    uint32_t c = r8(r);
    if (c == 253) return r16(r);
    if (c == 255) return r8(r) + 253;
    if (c == 254) return r8(r) + 506;
    return c;
}
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define T_GLYF TAG('g', 'l', 'y', 'f')
#define T_LOCA TAG('l', 'o', 'c', 'a')
#define T_HMTX TAG('h', 'm', 't', 'x')
#define T_HEAD TAG('h', 'e', 'a', 'd')
#define T_HHEA TAG('h', 'h', 'e', 'a')
#define T_MAXP TAG('m', 'a', 'x', 'p')
#define T_CMAP TAG('c', 'm', 'a', 'p')

/* ------------------------------------------------------------------ montaje del sfnt */
#define WFF_MAXT 64
struct tab { uint32_t tag; const uint8_t *d; uint32_t len; uint8_t *own; };
struct sf { struct tab t[WFF_MAXT]; int n; };
static void sf_free(struct sf *s) { for (int i = 0; i < s->n; i++) if (s->t[i].own) kfree(s->t[i].own); s->n = 0; }
static struct tab *sf_find(struct sf *s, uint32_t tag) { for (int i = 0; i < s->n; i++) if (s->t[i].tag == tag) return &s->t[i]; return 0; }
static int sf_build(struct sf *s, uint32_t flavor, uint8_t **out, uint32_t *out_n)
{
    for (int i = 1; i < s->n; i++) { struct tab x = s->t[i]; int j = i - 1; while (j >= 0 && s->t[j].tag > x.tag) { s->t[j + 1] = s->t[j]; j--; } s->t[j + 1] = x; }
    uint64_t tot = 12 + 16u * (uint32_t)s->n;
    for (int i = 0; i < s->n; i++) tot += ((uint64_t)s->t[i].len + 3) & ~3ull;
    if (tot > WFF_MAX_OUT) return WFF_E_BIG;
    uint8_t *o = (uint8_t *)kmalloc((size_t)tot); if (!o) return WFF_E_MEM;
    memset(o, 0, (size_t)tot);
    uint32_t es = 0; while ((2u << es) <= (uint32_t)s->n) es++;
    uint32_t sr = (1u << es) * 16;
    put32(o, flavor); put16(o + 4, (uint32_t)s->n); put16(o + 6, sr); put16(o + 8, es); put16(o + 10, (uint32_t)s->n * 16 - sr);
    uint32_t off = 12 + 16u * (uint32_t)s->n;
    for (int i = 0; i < s->n; i++) {
        uint8_t *rec = o + 12 + 16 * i;
        memcpy(o + off, s->t[i].d, s->t[i].len);
        uint32_t sum = 0, pl = (s->t[i].len + 3) & ~3u;
        for (uint32_t k = 0; k < pl; k += 4) sum += be32(o + off + k);
        put32(rec, s->t[i].tag); put32(rec + 4, sum); put32(rec + 8, off); put32(rec + 12, s->t[i].len);
        off += pl;
    }
    *out = o; *out_n = (uint32_t)tot;
    return WFF_OK;
}

int wff_is_truetype(const uint8_t *d, uint32_t n)
{
    if (!d || n < 12) return 0;
    uint32_t v = be32(d), nt = be16(d + 4), need = 0;
    if (v != 0x00010000u && v != TAG('t', 'r', 'u', 'e')) return 0;
    if (12 + 16 * nt > n) return 0;
    for (uint32_t i = 0; i < nt; i++) {
        const uint8_t *r = d + 12 + 16 * i; uint32_t tag = be32(r), off = be32(r + 8), len = be32(r + 12);
        if (off > n || len > n - off) return 0;
        if (tag == T_GLYF) need |= 1; else if (tag == T_LOCA) need |= 2; else if (tag == T_HEAD && len >= 54) need |= 4; else if (tag == T_HHEA && len >= 36) need |= 8;
        else if (tag == T_HMTX) need |= 16; else if (tag == T_CMAP) need |= 32; else if (tag == T_MAXP && len >= 6) need |= 64;
    }
    return need == 127;
}

/* ------------------------------------------------------------------ WOFF 1.0 */
static int woff1(const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *out_n)
{
    if (n < 44) return WFF_E_TRUNC;
    uint32_t flavor = be32(in + 4), nt = be16(in + 12);
    if (flavor == TAG('O', 'T', 'T', 'O')) return WFF_E_CFF;
    if (nt == 0 || nt > WFF_MAXT || 44 + 20 * nt > n) return WFF_E_TRUNC;
    struct sf s; s.n = 0; uint64_t total = 0;
    for (uint32_t i = 0; i < nt; i++) {
        const uint8_t *e = in + 44 + 20 * i;
        uint32_t tag = be32(e), off = be32(e + 4), cl = be32(e + 8), ol = be32(e + 12);
        if (off > n || cl > n - off || cl > ol) { sf_free(&s); return WFF_E_TRUNC; }
        total += ol; if (total > WFF_MAX_OUT) { sf_free(&s); return WFF_E_BIG; }
        struct tab *t = &s.t[s.n++]; t->tag = tag; t->own = 0;
        if (cl == ol) { t->d = in + off; t->len = ol; continue; }
        struct wb z = {0};
        if (wz_inflate(in + off, cl, 1, &z, ol) != 0 || z.len != ol || z.err) { wb_free(&z); sf_free(&s); return WFF_E_ZLIB; }
        t->own = (uint8_t *)z.p; t->d = t->own; t->len = ol;
    }
    int r = sf_build(&s, flavor, out, out_n);
    sf_free(&s);
    return r;
}

/* ------------------------------------------------------------------ WOFF 2.0 */
static const char WOFF2_TAGS[63][5] = {
    "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT",
    "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH",
    "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar", "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar",
    "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop", "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill" };

/* salida que crece con tope */
struct ob { uint8_t *p; uint32_t len, cap; int err; };
static void ob_need(struct ob *o, uint32_t k)
{
    if (o->err) return;
    if ((uint64_t)o->len + k > WFF_MAX_OUT) { o->err = WFF_E_BIG; return; }
    if (o->len + k <= o->cap) return;
    uint32_t nc = o->cap ? o->cap : 4096; while (nc < o->len + k) nc = nc > (WFF_MAX_OUT / 2) ? WFF_MAX_OUT : nc * 2;
    uint8_t *q = (uint8_t *)kmalloc(nc); if (!q) { o->err = WFF_E_MEM; return; }
    if (o->p) { memcpy(q, o->p, o->len); kfree(o->p); }
    o->p = q; o->cap = nc;
}
static void ob16(struct ob *o, uint32_t v) { ob_need(o, 2); if (!o->err) { put16(o->p + o->len, v); o->len += 2; } }
static void obn(struct ob *o, const uint8_t *d, uint32_t k) { ob_need(o, k); if (!o->err && k) { memcpy(o->p + o->len, d, k); o->len += k; } }

#define WFF_MAX_GLYPHS 65535
#define WFF_MAX_PTS    32768
static int32_t s_px[WFF_MAX_PTS], s_py[WFF_MAX_PTS];
static uint8_t s_on[WFF_MAX_PTS];
static uint16_t s_end[WFF_MAX_PTS];

/* glyf transformada -> glyf + loca larga. xmin[] (numGlyphs) para hmtx. */
static int glyf_rebuild(const uint8_t *d, uint32_t n, struct ob *glyf, struct ob *loca, int16_t **xmin_out, uint32_t *ng_out)
{
    struct rd h = { d, n, 0, 0 };
    r16(&h); uint32_t opt = r16(&h), ng = r16(&h); r16(&h);
    uint32_t sz[7]; for (int i = 0; i < 7; i++) sz[i] = r32(&h);
    if (h.err || ng == 0) return WFF_E_GLYF;
    uint64_t off = h.o; struct rd st[7];
    for (int i = 0; i < 7; i++) { if (off + sz[i] > n) return WFF_E_GLYF; st[i].p = d + off; st[i].n = sz[i]; st[i].o = 0; st[i].err = 0; off += sz[i]; }
    (void)opt;
    struct rd *nct = &st[0], *npt = &st[1], *flg = &st[2], *gly = &st[3], *cmp = &st[4], *bbx = &st[5], *ins = &st[6];
    uint32_t bml = 4 * ((ng + 31) / 32);
    if (bml > bbx->n) return WFF_E_GLYF;
    const uint8_t *bbmap = bbx->p; bbx->o = bml;
    int16_t *xmin = (int16_t *)kmalloc(sizeof(int16_t) * ng); if (!xmin) return WFF_E_MEM;
    ob_need(loca, 4 * (ng + 1));
    for (uint32_t g = 0; g < ng && !glyf->err && !loca->err; g++) {
        while (glyf->len & 3) { uint8_t z = 0; obn(glyf, &z, 1); }
        put32(loca->p + 4 * g, glyf->len);
        int32_t nc = rs16(nct); int hasbb = (bbmap[g >> 3] >> (7 - (g & 7))) & 1;
        xmin[g] = 0;
        if (nct->err) { kfree(xmin); return WFF_E_GLYF; }
        if (nc == 0) { if (hasbb) { kfree(xmin); return WFF_E_GLYF; } continue; }
        if (nc > 0) {
            uint32_t np = 0;
            if ((uint32_t)nc > WFF_MAX_PTS) { kfree(xmin); return WFF_E_GLYF; }
            for (int32_t c = 0; c < nc; c++) {
                uint32_t k = u255(npt); if (npt->err || k == 0 || np + k > WFF_MAX_PTS) { kfree(xmin); return WFF_E_GLYF; }
                np += k; s_end[c] = (uint16_t)(np - 1);
            }
            int32_t x = 0, y = 0;
            for (uint32_t i = 0; i < np; i++) {
                uint32_t f = r8(flg); int on = !(f >> 7); f &= 0x7F;
                int32_t dx, dy;
                if (f < 10) { dx = 0; uint32_t b = r8(gly); dy = (int32_t)(((f & 14) << 7) + b); if (!(f & 1)) dy = -dy; }
                else if (f < 20) { uint32_t b = r8(gly); dx = (int32_t)((((f - 10) & 14) << 7) + b); if (!(f & 1)) dx = -dx; dy = 0; }
                else if (f < 84) { uint32_t b0 = f - 20, b1 = r8(gly); dx = (int32_t)(1 + (b0 & 0x30) + (b1 >> 4)); dy = (int32_t)(1 + ((b0 & 0x0C) << 2) + (b1 & 0x0F)); if (!(f & 1)) dx = -dx; if (!((f >> 1) & 1)) dy = -dy; }
                else if (f < 120) { uint32_t b0 = f - 84, a = r8(gly), b = r8(gly); dx = (int32_t)(1 + ((b0 / 12) << 8) + a); dy = (int32_t)(1 + (((b0 % 12) >> 2) << 8) + b); if (!(f & 1)) dx = -dx; if (!((f >> 1) & 1)) dy = -dy; }
                else if (f < 124) { uint32_t a = r8(gly), b = r8(gly), c = r8(gly); dx = (int32_t)((a << 4) + (b >> 4)); dy = (int32_t)(((b & 0x0F) << 8) + c); if (!(f & 1)) dx = -dx; if (!((f >> 1) & 1)) dy = -dy; }
                else { uint32_t a = r8(gly), b = r8(gly), c = r8(gly), e = r8(gly); dx = (int32_t)((a << 8) + b); dy = (int32_t)((c << 8) + e); if (!(f & 1)) dx = -dx; if (!((f >> 1) & 1)) dy = -dy; }
                x += dx; y += dy;
                if (x < -32768 || x > 32767 || y < -32768 || y > 32767) { kfree(xmin); return WFF_E_GLYF; }
                s_px[i] = x; s_py[i] = y; s_on[i] = (uint8_t)on;
            }
            uint32_t il = u255(gly);
            if (flg->err || gly->err || il > ins->n - ins->o) { kfree(xmin); return WFF_E_GLYF; }
            int32_t x0, y0, x1, y1;
            if (hasbb) { x0 = rs16(bbx); y0 = rs16(bbx); x1 = rs16(bbx); y1 = rs16(bbx); if (bbx->err) { kfree(xmin); return WFF_E_GLYF; } }
            else { x0 = y0 = 32767; x1 = y1 = -32768; for (uint32_t i = 0; i < np; i++) { if (s_px[i] < x0) x0 = s_px[i]; if (s_px[i] > x1) x1 = s_px[i]; if (s_py[i] < y0) y0 = s_py[i]; if (s_py[i] > y1) y1 = s_py[i]; } }
            xmin[g] = (int16_t)x0;
            ob16(glyf, (uint32_t)nc); ob16(glyf, (uint32_t)x0); ob16(glyf, (uint32_t)y0); ob16(glyf, (uint32_t)x1); ob16(glyf, (uint32_t)y1);
            for (int32_t c = 0; c < nc; c++) ob16(glyf, s_end[c]);
            ob16(glyf, il); obn(glyf, ins->p + ins->o, il); ins->o += il;
            ob_need(glyf, np); if (glyf->err) break;
            for (uint32_t i = 0; i < np; i++) glyf->p[glyf->len++] = s_on[i] ? 1 : 0;   /* sin x/y cortos: deltas de 16 bits */
            int32_t px = 0; for (uint32_t i = 0; i < np; i++) { int32_t dd = s_px[i] - px; if (dd < -32768 || dd > 32767) { kfree(xmin); return WFF_E_GLYF; } ob16(glyf, (uint32_t)dd); px = s_px[i]; }
            int32_t py = 0; for (uint32_t i = 0; i < np; i++) { int32_t dd = s_py[i] - py; if (dd < -32768 || dd > 32767) { kfree(xmin); return WFF_E_GLYF; } ob16(glyf, (uint32_t)dd); py = s_py[i]; }
            continue;
        }
        if (nc != -1 || !hasbb) { kfree(xmin); return WFF_E_GLYF; }
        /* compuesto: los componentes se copian tal cual */
        int32_t x0 = rs16(bbx), y0 = rs16(bbx), x1 = rs16(bbx), y1 = rs16(bbx);
        if (bbx->err) { kfree(xmin); return WFF_E_GLYF; }
        xmin[g] = (int16_t)x0;
        uint32_t c0 = cmp->o, flags = 0, more = 1, have_ins = 0, ncomp = 0;
        while (more) {
            flags = r16(cmp); r16(cmp);
            uint32_t a = (flags & 1) ? 4 : 2; if (flags & 8) a += 2; else if (flags & 0x40) a += 4; else if (flags & 0x80) a += 8;
            rskip(cmp, a);
            if (cmp->err || ++ncomp > 512) { kfree(xmin); return WFF_E_GLYF; }
            if (flags & 0x100) have_ins = 1;
            more = flags & 0x20;
        }
        ob16(glyf, 0xFFFF); ob16(glyf, (uint32_t)x0); ob16(glyf, (uint32_t)y0); ob16(glyf, (uint32_t)x1); ob16(glyf, (uint32_t)y1);
        obn(glyf, cmp->p + c0, cmp->o - c0);
        if (have_ins) {
            uint32_t il = u255(gly);
            if (gly->err || il > ins->n - ins->o) { kfree(xmin); return WFF_E_GLYF; }
            ob16(glyf, il); obn(glyf, ins->p + ins->o, il); ins->o += il;
        }
    }
    while (!glyf->err && (glyf->len & 3)) { uint8_t z = 0; obn(glyf, &z, 1); }
    if (glyf->err || loca->err) { kfree(xmin); return glyf->err ? glyf->err : loca->err; }
    put32(loca->p + 4 * ng, glyf->len); loca->len = 4 * (ng + 1);
    *xmin_out = xmin; *ng_out = ng;
    return WFF_OK;
}

/* xMin de cada glifo de una glyf/loca sin transformar (para hmtx transformada con glyf normal) */
static int16_t *xmins_plain(const uint8_t *glyf, uint32_t gl, const uint8_t *loca, uint32_t ll, int lfmt, uint32_t ng)
{
    int16_t *x = (int16_t *)kmalloc(sizeof(int16_t) * (ng ? ng : 1)); if (!x) return 0;
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t o0, o1;
        if (lfmt) { if (4 * g + 8 > ll) { x[g] = 0; continue; } o0 = be32(loca + 4 * g); o1 = be32(loca + 4 * g + 4); }
        else { if (2 * g + 4 > ll) { x[g] = 0; continue; } o0 = be16(loca + 2 * g) * 2; o1 = be16(loca + 2 * g + 2) * 2; }
        x[g] = (o1 > o0 && o0 + 4 <= gl) ? (int16_t)be16(glyf + o0 + 2) : 0;
    }
    return x;
}

static int woff2(const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *out_n)
{
    if (n < 48) return WFF_E_TRUNC;
    struct rd r = { in, n, 4, 0 };
    uint32_t flavor = r32(&r); r32(&r); uint32_t nt = r16(&r); r16(&r); r32(&r); uint32_t csize = r32(&r);
    if (flavor == TAG('t', 't', 'c', 'f')) return WFF_E_TTC;
    if (flavor == TAG('O', 'T', 'T', 'O')) return WFF_E_CFF;
    if (nt == 0 || nt > WFF_MAXT) return WFF_E_TRUNC;
    r.o = 48;
    struct { uint32_t tag, olen, slen, off; int xf; } e[WFF_MAXT];
    uint64_t tot = 0;
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t fl = r8(&r), tag;
        if ((fl & 63) == 63) tag = r32(&r); else tag = TAG(WOFF2_TAGS[fl & 63][0], WOFF2_TAGS[fl & 63][1], WOFF2_TAGS[fl & 63][2], WOFF2_TAGS[fl & 63][3]);
        uint32_t ver = (fl >> 6) & 3, ol = base128(&r);
        int xf = (tag == T_GLYF || tag == T_LOCA) ? ver == 0 : ver != 0;
        uint32_t sl = xf ? base128(&r) : ol;
        if (r.err) return WFF_E_TRUNC;
        if (xf && tag != T_GLYF && tag != T_LOCA && !(tag == T_HMTX && ver == 1)) return WFF_E_FMT;   /* transformaciones desconocidas */
        e[i].tag = tag; e[i].olen = ol; e[i].slen = sl; e[i].off = (uint32_t)tot; e[i].xf = xf;
        tot += sl; if (tot > WFF_MAX_OUT || ol > WFF_MAX_OUT) return WFF_E_BIG;
    }
    if (csize > n - r.o) return WFF_E_TRUNC;
    uint8_t *buf = (uint8_t *)kmalloc(tot ? (size_t)tot : 1); if (!buf) return WFF_E_MEM;
    size_t dsz = (size_t)tot;
    if (BrotliDecoderDecompress(csize, in + r.o, &dsz, buf) != BROTLI_DECODER_RESULT_SUCCESS || dsz != tot) { kfree(buf); return WFF_E_BROTLI; }
    struct sf s; s.n = 0;
    int ig = -1, il = -1, ih = -1;
    for (uint32_t i = 0; i < nt; i++) {
        if (e[i].tag == T_GLYF) ig = (int)i; else if (e[i].tag == T_LOCA) il = (int)i; else if (e[i].tag == T_HMTX) ih = (int)i;
        struct tab *t = &s.t[s.n++]; t->tag = e[i].tag; t->d = buf + e[i].off; t->len = e[i].slen; t->own = 0;
    }
    int rc = WFF_OK; int16_t *xmin = 0; uint32_t ngx = 0;
    if (ig >= 0 && e[ig].xf) {
        if (il < 0) { rc = WFF_E_TABLES; goto fin; }
        struct ob g = {0}, l = {0};
        rc = glyf_rebuild(buf + e[ig].off, e[ig].slen, &g, &l, &xmin, &ngx);
        if (rc) { if (g.p) kfree(g.p); if (l.p) kfree(l.p); goto fin; }
        struct tab *tg = sf_find(&s, T_GLYF), *tl = sf_find(&s, T_LOCA);
        tg->own = g.p; tg->d = g.p; tg->len = g.len; tl->own = l.p; tl->d = l.p; tl->len = l.len;
        struct tab *th = sf_find(&s, T_HEAD);
        if (!th || th->len < 54) { rc = WFF_E_TABLES; goto fin; }
        uint8_t *hc = (uint8_t *)kmalloc(th->len); if (!hc) { rc = WFF_E_MEM; goto fin; }
        memcpy(hc, th->d, th->len); put16(hc + 50, 1); th->own = hc; th->d = hc;   /* loca larga */
    }
    if (ih >= 0 && e[ih].xf) {
        struct tab *hh = sf_find(&s, T_HHEA), *mp = sf_find(&s, T_MAXP), *tg = sf_find(&s, T_GLYF), *tl = sf_find(&s, T_LOCA), *th = sf_find(&s, T_HEAD);
        if (!hh || hh->len < 36 || !mp || mp->len < 6 || !tg || !tl || !th || th->len < 54) { rc = WFF_E_TABLES; goto fin; }
        uint32_t nhm = be16(hh->d + 34), ng = be16(mp->d + 4);
        if (nhm == 0 || nhm > ng) { rc = WFF_E_TABLES; goto fin; }
        if (!xmin) { xmin = xmins_plain(tg->d, tg->len, tl->d, tl->len, (int)(int16_t)be16(th->d + 50), ng); ngx = ng; if (!xmin) { rc = WFF_E_MEM; goto fin; } }
        if (ngx < ng) { rc = WFF_E_TABLES; goto fin; }
        struct rd h = { buf + e[ih].off, e[ih].slen, 0, 0 };
        uint32_t fl = r8(&h);
        uint32_t olen = 4 * nhm + 2 * (ng - nhm);
        uint8_t *o = (uint8_t *)kmalloc(olen ? olen : 1); if (!o) { rc = WFF_E_MEM; goto fin; }
        for (uint32_t i = 0; i < nhm; i++) put16(o + 4 * i, r16(&h));
        for (uint32_t i = 0; i < nhm; i++) put16(o + 4 * i + 2, (fl & 1) ? (uint32_t)(uint16_t)xmin[i] : r16(&h));
        for (uint32_t i = nhm; i < ng; i++) put16(o + 4 * nhm + 2 * (i - nhm), (fl & 2) ? (uint32_t)(uint16_t)xmin[i] : r16(&h));
        if (h.err) { kfree(o); rc = WFF_E_TRUNC; goto fin; }
        struct tab *tm = sf_find(&s, T_HMTX); tm->own = o; tm->d = o; tm->len = olen;
    }
    for (int i = 0; i < s.n; i++) if (s.t[i].tag == TAG('C', 'F', 'F', ' ') || s.t[i].tag == TAG('C', 'F', 'F', '2')) { rc = WFF_E_CFF; goto fin; }
    rc = sf_build(&s, flavor, out, out_n);
fin:
    if (xmin) kfree(xmin);
    sf_free(&s);
    kfree(buf);
    return rc;
}

int wff_to_sfnt(const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *out_n)
{
    *out = 0; *out_n = 0;
    if (!in || n < 12) return WFF_E_TRUNC;
    if (n > WFF_MAX_IN) return WFF_E_BIG;
    uint32_t sig = be32(in); int rc;
    if (sig == TAG('w', 'O', 'F', 'F')) rc = woff1(in, n, out, out_n);
    else if (sig == TAG('w', 'O', 'F', '2')) rc = woff2(in, n, out, out_n);
    else if (sig == TAG('O', 'T', 'T', 'O')) return WFF_E_CFF;
    else if (sig == TAG('t', 't', 'c', 'f')) return WFF_E_TTC;
    else if (sig == 0x00010000u || sig == TAG('t', 'r', 'u', 'e')) {
        uint8_t *c = (uint8_t *)kmalloc(n); if (!c) return WFF_E_MEM;
        memcpy(c, in, n); *out = c; *out_n = n; rc = WFF_OK;
    } else return WFF_E_FMT;
    if (rc == WFF_OK && !wff_is_truetype(*out, *out_n)) { kfree(*out); *out = 0; *out_n = 0; return WFF_E_TABLES; }
    return rc;
}

/* SOMA_348_FUENTES_WEB: Content-Encoding: br (paginas, CSS y scripts mas pequenos que con gzip). Anade a out; 0 = bien. */
int wbr_decode(const uint8_t *in, uint32_t n, struct wb *out, uint32_t max_out)
{
    BrotliDecoderState *st = BrotliDecoderCreateInstance(0, 0, 0); if (!st) return -1;
    size_t ain = n; const uint8_t *nin = in; int rc = -1;
    for (int guard = 0; guard < 100000; guard++) {
        uint8_t tmp[8192]; size_t aout = sizeof tmp; uint8_t *nout = tmp;
        BrotliDecoderResult r = BrotliDecoderDecompressStream(st, &ain, &nin, &aout, &nout, 0);
        uint32_t got = (uint32_t)(sizeof tmp - aout);
        if (got) { if (out->len + got > max_out) { rc = -2; break; } wb_addn(out, (const char *)tmp, got); if (out->err) { rc = -3; break; } }
        if (r == BROTLI_DECODER_RESULT_SUCCESS) { rc = 0; break; }
        if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) continue;
        break;                                                     /* error o datos cortados */
    }
    BrotliDecoderDestroyInstance(st);
    return rc;
}
