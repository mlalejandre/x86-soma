/* SOMA_358_HOJA: ZIP (escribir, con el deflate de png_enc.c) y texto creciente. Lo que LEE archivos de fuera esta en hoja_zip_lee.c
 * (solo en el servicio aislado mimesvc en x86_64). Ver hoja_fmt.h. */
#include "hoja_util.h"
#include "png_enc.h"


/* ================================================================ texto creciente */
static int gb_hueco(struct gb *b, uint32_t extra)
{
    if (b->err) return 0;
    uint64_t nec = (uint64_t)b->n + extra + 1;
    if (nec <= b->cap) return 1;
    if (nec > (256u << 20)) { b->err = 1; return 0; }
    uint32_t nc = b->cap ? b->cap : 4096; while (nc < nec) nc *= 2;
    char *x = (char *)hj_alloc(nc); if (!x) { b->err = 1; return 0; }
    if (b->p) { copia(x, b->p, b->n); hj_free(b->p); }
    b->p = x; b->cap = nc; return 1;
}
void gb_c(struct gb *b, char c) { if (!gb_hueco(b, 1)) return; b->p[b->n++] = c; b->p[b->n] = 0; }
void gb_s(struct gb *b, const char *s) { uint32_t n = sl(s); if (!gb_hueco(b, n)) return; copia(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0; }
void gb_n(struct gb *b, int64_t v) { char t[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) gb_c(b, '-'); do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) gb_c(b, t[--n]); }
void gb_f(struct gb *b, f64 v) { char t[F64_STR_MAX]; if (!f64_is_finite(v)) v = 0; f64_to_str(v, t, 10); gb_s(b, t); }
void gb_esc(struct gb *b, const char *s)
{
    for (const uint8_t *p = (const uint8_t *)s; p && *p; ++p) {
        uint8_t c = *p;
        if (c == '&') gb_s(b, "&amp;"); else if (c == '<') gb_s(b, "&lt;"); else if (c == '>') gb_s(b, "&gt;"); else if (c == '"') gb_s(b, "&quot;");
        else if (c == '\'') gb_s(b, "&apos;");
        else if (c < 32 && c != '\t' && c != '\n' && c != '\r') continue;
        else gb_c(b, (char)c);
    }
}
void gb_libera(struct gb *b) { if (b->p) hj_free(b->p); zero(b, (uint32_t)sizeof *b); }

/* ================================================================ ZIP: escribir */
#define ZW_MAX 512
struct zw_e { char nombre[96]; uint32_t crc, csize, usize, off; uint16_t metodo; };
struct zw { struct gb b; struct zw_e e[ZW_MAX]; int ne; int err; };
struct zw *zw_nuevo(void) { struct zw *z = (struct zw *)hj_alloc((uint32_t)sizeof *z); if (z) zero(z, (uint32_t)sizeof *z); return z; }
static void le16(struct gb *b, uint32_t v) { gb_c(b, (char)(v & 255)); gb_c(b, (char)((v >> 8) & 255)); }
static void le32(struct gb *b, uint32_t v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
static void bytes(struct gb *b, const uint8_t *d, uint32_t n) { if (!gb_hueco(b, n)) return; copia(b->p + b->n, d, n); b->n += n; b->p[b->n] = 0; }
int zw_pon(struct zw *z, const char *nombre, const uint8_t *d, uint32_t n, int comprimir)
{
    if (!z || z->err || z->ne >= ZW_MAX || sl(nombre) >= sizeof z->e[0].nombre) { if (z) z->err = 1; return -1; }
    struct zw_e *e = &z->e[z->ne];
    zero(e, (uint32_t)sizeof *e);
    copia(e->nombre, nombre, sl(nombre) + 1);
    e->crc = png_crc32(0, d, n); e->usize = n; e->off = z->b.n;
    uint8_t *c = 0; uint32_t cn = 0;
    if (comprimir && n > 64) {
        uint32_t cap = n + n / 4 + 256; void *w = hj_alloc((uint32_t)deflate_work_size(n));
        c = (uint8_t *)hj_alloc(cap);
        if (c && w) { size_t k = zlib_compress(d, n, c, cap, w); if (k > 6 && k - 6 < n) cn = (uint32_t)k; }
        if (w) hj_free(w);
    }
    e->metodo = cn ? 8 : 0; e->csize = cn ? cn - 6 : n;          /* zlib: 2 de cabecera y 4 de adler32 fuera = deflate crudo */
    le32(&z->b, 0x04034b50); le16(&z->b, 20); le16(&z->b, 0x0800 /* nombres UTF-8 */); le16(&z->b, e->metodo); le16(&z->b, 0); le16(&z->b, 0x5C21);
    le32(&z->b, e->crc); le32(&z->b, e->csize); le32(&z->b, e->usize); le16(&z->b, sl(nombre)); le16(&z->b, 0);
    gb_s(&z->b, nombre);
    if (cn) bytes(&z->b, c + 2, cn - 6); else bytes(&z->b, d, n);
    if (c) hj_free(c);
    if (z->b.err) { z->err = 1; return -1; }
    z->ne++; return 0;
}
uint8_t *zw_cierra(struct zw *z, uint32_t *n)
{
    *n = 0; if (!z) return 0;
    uint32_t cd = z->b.n;
    for (int i = 0; i < z->ne; ++i) {
        struct zw_e *e = &z->e[i];
        le32(&z->b, 0x02014b50); le16(&z->b, 20); le16(&z->b, 20); le16(&z->b, 0x0800); le16(&z->b, e->metodo); le16(&z->b, 0); le16(&z->b, 0x5C21);
        le32(&z->b, e->crc); le32(&z->b, e->csize); le32(&z->b, e->usize); le16(&z->b, sl(e->nombre)); le16(&z->b, 0); le16(&z->b, 0); le16(&z->b, 0); le16(&z->b, 0);
        le32(&z->b, 0); le32(&z->b, e->off); gb_s(&z->b, e->nombre);
    }
    uint32_t tam = z->b.n - cd;
    le32(&z->b, 0x06054b50); le16(&z->b, 0); le16(&z->b, 0); le16(&z->b, (uint32_t)z->ne); le16(&z->b, (uint32_t)z->ne); le32(&z->b, tam); le32(&z->b, cd); le16(&z->b, 0);
    uint8_t *r = 0;
    if (!z->err && !z->b.err) { r = (uint8_t *)z->b.p; *n = z->b.n; z->b.p = 0; }
    gb_libera(&z->b); hj_free(z);
    return r;
}

