/* SOMA_248_DOC: escritor de PDF. Ver pdfw.h. */
#include "pdfw.h"
#include "png_enc.h"
#include "mem.h"

#define PDFW_MAX_OBJ 1024

struct pdfw {
    uint8_t *buf; uint32_t n, cap; int bad;
    uint32_t off[PDFW_MAX_OBJ]; int nobj;      /* desplazamiento de cada objeto (1..nobj) */
    int page_obj[PDFW_MAX_OBJ / 3]; int npages;
    char title[256]; char creator[64];
};

static void grow(struct pdfw *p, uint32_t need)
{
    if (p->bad || p->n + need <= p->cap) return;
    uint32_t nc = p->cap ? p->cap : 65536;
    while (nc < p->n + need) { if (nc > 0x7FFFFFFFu / 2) { p->bad = 1; return; } nc *= 2; }
    uint8_t *nb = (uint8_t *)kmalloc(nc);
    if (!nb) { p->bad = 1; return; }
    if (p->buf) { memcpy(nb, p->buf, p->n); kfree(p->buf); }
    p->buf = nb; p->cap = nc;
}
static void put(struct pdfw *p, const void *d, uint32_t n) { grow(p, n); if (p->bad) return; memcpy(p->buf + p->n, d, n); p->n += n; }
static void puts_(struct pdfw *p, const char *s) { uint32_t n = 0; while (s[n]) n++; put(p, s, n); }
static void putu(struct pdfw *p, uint32_t v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } puts_(p, b + i); }
static void put10(struct pdfw *p, uint32_t v) { char b[11]; for (int i = 9; i >= 0; i--) { b[i] = (char)('0' + v % 10); v /= 10; } b[10] = 0; puts_(p, b); }
static int obj_begin(struct pdfw *p, int id) { if (id <= 0 || id >= PDFW_MAX_OBJ) { p->bad = 1; return 0; } p->off[id] = p->n; if (id > p->nobj) p->nobj = id; putu(p, (uint32_t)id); puts_(p, " 0 obj\n"); return 1; }
static int obj_new(struct pdfw *p) { int id = p->nobj + 1; if (id >= PDFW_MAX_OBJ) { p->bad = 1; return 0; } p->nobj = id; return id; }

static void scpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }

struct pdfw *pdfw_new(const char *title, const char *creator)
{
    struct pdfw *p = (struct pdfw *)kmalloc(sizeof *p);
    if (!p) return 0;
    uint8_t *z = (uint8_t *)p; for (uint32_t i = 0; i < sizeof *p; i++) z[i] = 0;
    scpy(p->title, title ? title : "", sizeof p->title); scpy(p->creator, creator ? creator : "SOMA", sizeof p->creator);
    puts_(p, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    p->nobj = 3;                                     /* 1 catalogo, 2 arbol de paginas, 3 info: se escriben al cerrar */
    return p;
}

size_t pdfw_png_idat(const uint8_t *png, size_t n, uint8_t *out, size_t cap)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 8) return 0;
    for (int i = 0; i < 8; i++) if (png[i] != sig[i]) return 0;
    size_t pos = 8, o = 0;
    while (pos + 12 <= n) {
        uint32_t len = (uint32_t)png[pos] << 24 | (uint32_t)png[pos + 1] << 16 | (uint32_t)png[pos + 2] << 8 | png[pos + 3];
        const uint8_t *t = png + pos + 4;
        if (len > n - pos - 12) return 0;
        if (t[0] == 'I' && t[1] == 'D' && t[2] == 'A' && t[3] == 'T') {
            if (o + len > cap) return 0;
            memcpy(out + o, png + pos + 8, len); o += len;
        } else if (t[0] == 'I' && t[1] == 'E' && t[2] == 'N' && t[3] == 'D') break;
        pos += 12 + (size_t)len;
    }
    return o;
}

int pdfw_page(struct pdfw *p, const uint32_t *argb, int w, int h, int w_pt, int h_pt)
{
    if (!p || p->bad || !argb || w <= 0 || h <= 0 || w_pt <= 0 || h_pt <= 0) return -22;
    if (p->nobj + 3 >= PDFW_MAX_OBJ || p->npages >= PDFW_MAX_OBJ / 3) return -28;
    size_t cap = (size_t)w * (size_t)h * 3u + (size_t)h + 65536u, wk = png_work_size(w, h);
    uint8_t *png = (uint8_t *)kmalloc(cap); void *work = kmalloc(wk);
    int rc = -12;
    if (png && work) {
        size_t pn = png_encode_rgb(argb, w, h, png, cap, work);
        size_t zn = pn ? pdfw_png_idat(png, pn, (uint8_t *)work, wk) : 0;   /* el bufer de trabajo ya no hace falta: guarda el zlib */
        if (!pn || !zn) rc = -5;
        else {
            int img = obj_new(p), cnt = obj_new(p), pg = obj_new(p);
            if (obj_begin(p, img)) {
                puts_(p, "<< /Type /XObject /Subtype /Image /Width "); putu(p, (uint32_t)w); puts_(p, " /Height "); putu(p, (uint32_t)h);
                puts_(p, " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns ");
                putu(p, (uint32_t)w); puts_(p, " >> /Length "); putu(p, (uint32_t)zn); puts_(p, " >>\nstream\n");
                put(p, work, (uint32_t)zn); puts_(p, "\nendstream\nendobj\n");
            }
            char c[96]; uint32_t q = 0;
#define CS(s) do { const char *_s = (s); while (*_s && q + 1 < sizeof c) c[q++] = *_s++; } while (0)
#define CU(v) do { char _b[12]; int _i = 11; uint32_t _v = (uint32_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } CS(_b + _i); } while (0)
            CS("q "); CU(w_pt); CS(" 0 0 "); CU(h_pt); CS(" 0 0 cm /Im0 Do Q\n");
#undef CS
#undef CU
            c[q] = 0;
            if (obj_begin(p, cnt)) { puts_(p, "<< /Length "); putu(p, q); puts_(p, " >>\nstream\n"); put(p, c, q); puts_(p, "endstream\nendobj\n"); }
            if (obj_begin(p, pg)) {
                puts_(p, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "); putu(p, (uint32_t)w_pt); puts_(p, " "); putu(p, (uint32_t)h_pt);
                puts_(p, "] /Resources << /XObject << /Im0 "); putu(p, (uint32_t)img); puts_(p, " 0 R >> >> /Contents "); putu(p, (uint32_t)cnt); puts_(p, " 0 R >>\nendobj\n");
            }
            p->page_obj[p->npages++] = pg;
            rc = p->bad ? -12 : 0;
        }
    }
    if (png) kfree(png);
    if (work) kfree(work);
    return rc;
}

/* cadena de texto PDF en UTF-16BE con BOM, en hexadecimal (admite cualquier caracter del plano basico) */
static void put_text(struct pdfw *p, const char *s)
{
    static const char hx[] = "0123456789ABCDEF";
    puts_(p, "<FEFF");
    const uint8_t *u = (const uint8_t *)s;
    while (*u) {
        uint32_t cp = *u++;
        if (cp >= 0xC0 && cp < 0xE0 && (*u & 0xC0) == 0x80) { cp = (cp & 0x1F) << 6 | (*u++ & 0x3F); }
        else if (cp >= 0xE0 && cp < 0xF0 && (u[0] & 0xC0) == 0x80 && (u[1] & 0xC0) == 0x80) { cp = (cp & 0x0F) << 12 | (uint32_t)(u[0] & 0x3F) << 6 | (u[1] & 0x3F); u += 2; }
        else if (cp >= 0x80) { cp = '?'; while ((*u & 0xC0) == 0x80) u++; }
        char b[4] = { hx[(cp >> 12) & 15], hx[(cp >> 8) & 15], hx[(cp >> 4) & 15], hx[cp & 15] };
        put(p, b, 4);
    }
    puts_(p, ">");
}

uint8_t *pdfw_finish(struct pdfw *p, uint32_t *len)
{
    if (len) *len = 0;
    if (!p) return 0;
    if (p->bad || !p->npages) { pdfw_free(p); return 0; }
    obj_begin(p, 1); puts_(p, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj_begin(p, 2); puts_(p, "<< /Type /Pages /Count "); putu(p, (uint32_t)p->npages); puts_(p, " /Kids [");
    for (int i = 0; i < p->npages; i++) { if (i) puts_(p, " "); putu(p, (uint32_t)p->page_obj[i]); puts_(p, " 0 R"); }
    puts_(p, "] >>\nendobj\n");
    obj_begin(p, 3); puts_(p, "<< /Producer "); put_text(p, "SOMA OS (motor de documentos)"); puts_(p, " /Creator "); put_text(p, p->creator);
    if (p->title[0]) { puts_(p, " /Title "); put_text(p, p->title); }
    puts_(p, " >>\nendobj\n");
    uint32_t xref = p->n;
    puts_(p, "xref\n0 "); putu(p, (uint32_t)p->nobj + 1); puts_(p, "\n0000000000 65535 f \n");
    for (int i = 1; i <= p->nobj; i++) { put10(p, p->off[i]); puts_(p, " 00000 n \n"); }
    puts_(p, "trailer\n<< /Size "); putu(p, (uint32_t)p->nobj + 1); puts_(p, " /Root 1 0 R /Info 3 0 R >>\nstartxref\n"); putu(p, xref); puts_(p, "\n%%EOF\n");
    if (p->bad) { pdfw_free(p); return 0; }
    uint8_t *b = p->buf; if (len) *len = p->n;
    p->buf = 0; kfree(p);
    return b;
}
void pdfw_free(struct pdfw *p) { if (!p) return; if (p->buf) kfree(p->buf); kfree(p); }
