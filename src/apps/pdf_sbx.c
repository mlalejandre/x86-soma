/* PARCHE 141: API de pdf.h para el kernel. En x86_64 el analizador PDF (pdf.c) NO se enlaza en el kernel: corre en el servicio aislado
 * pdfsvc (ring 3). Aqui solo se guarda el documento, se piden las paginas y se VALIDA la lista de dibujo serializada antes de convertir
 * sus desplazamientos en punteros (tipos, fuentes, limites dentro del blob, alineacion, tamanos de imagen y de pagina no nulos y acotados).
 * Si algo no cuadra, la pagina entera se rechaza. En aarch64 (sin EL0) se usa pdf.c directamente. */
#include "pdf.h"
#if defined(__x86_64__)
#include "mem.h"
#include "sandbox.h"

#define PDF_TIMEOUT_MS 20000u
#define PDF_MAX_OPS    400000
#define PDF_MAX_DIM    16384

struct pdf {
    const uint8_t *data; uint32_t len;   /* lo conserva el llamante hasta pdf_close (igual que con pdf.c) */
    int npages;
    int32_t *sizes;                      /* [ancho_pt, alto_pt] por pagina */
    char title[256];
};

static int call(int op, const uint8_t *d, uint32_t n, int a0, int a1, struct sbx_call **out)
{
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    *out = c;
    if (!c) return -1;
    sbx_prepare(c, (uint32_t)op, d, n, 0);
    c->in.arg[0] = a0; c->in.arg[1] = a1;
    return sbx_svc_call(SBX_SVC_PDF, c, PDF_TIMEOUT_MS);
}
static void done(struct sbx_call *c) { if (c) { sbx_release(c); kfree(c); } }

struct pdf *pdf_open(const uint8_t *data, uint32_t len, char *err, int errmax)
{
    if (err && errmax > 0) err[0] = 0;
    if (!data || len == 0 || len > SBX_IN_MAX) { if (err && errmax > 1) { const char *m = "PDF vacio o demasiado grande"; int i = 0; for (; m[i] && i < errmax - 1; ++i) err[i] = m[i]; err[i] = 0; } return 0; }
    struct sbx_call *c;
    int r = call(SBX_PDF_OPEN, data, len, 0, 0, &c);
    struct pdf *p = 0;
    if (r == 0 && c->out.status == 0 && c->out.val[0] >= 0 && c->out.val[0] <= (int32_t)SBX_PDF_MAX_PAGES &&
        c->out.data_len == (uint64_t)c->out.val[0] * 8u) {
        p = (struct pdf *)kmalloc(sizeof *p);
        if (p) {
            p->data = data; p->len = len; p->npages = c->out.val[0]; p->sizes = 0;
            if (p->npages) {
                p->sizes = (int32_t *)kmalloc((size_t)p->npages * 8u);
                if (!p->sizes || sbx_copy_data(c, p->sizes, c->out.data_len) != 0) { if (p->sizes) kfree(p->sizes); kfree(p); p = 0; }
            }
            if (p) {
                uint32_t n = c->out.inline_len < sizeof p->title - 1 ? c->out.inline_len : (uint32_t)sizeof p->title - 1;
                for (uint32_t i = 0; i < n; ++i) p->title[i] = (char)c->out.inline_data[i];
                p->title[n] = 0;
            }
        }
    } else if (err && errmax > 1) {
        const char *m = r == 0 && c->out.status == -1 ? (const char *)c->out.inline_data : (r == SBX_E_DIED ? "el analizador PDF fallo (proceso aislado terminado)" : "no se pudo analizar el PDF");
        uint32_t lim = (r == 0 && c->out.status == -1) ? c->out.inline_len : 0xFFFFFFFFu;
        int i = 0; for (; m[i] && (uint32_t)i < lim && i < errmax - 1; ++i) err[i] = m[i];
        err[i] = 0;
    }
    done(c);
    return p;
}

void pdf_close(struct pdf *p) { if (!p) return; if (p->sizes) kfree(p->sizes); kfree(p); }
int  pdf_page_count(const struct pdf *p) { return p ? p->npages : 0; }
int  pdf_page_size(struct pdf *p, int idx, int *w_pt, int *h_pt)
{
    if (!p || idx < 0 || idx >= p->npages) return -1;
    *w_pt = p->sizes[2 * idx]; *h_pt = p->sizes[2 * idx + 1];
    return 0;
}
int  pdf_title(struct pdf *p, char *out, int max)
{
    if (!p || !out || max < 1) return 0;
    int i = 0; for (; p->title[i] && i < max - 1; ++i) out[i] = p->title[i];
    out[i] = 0;
    return i;
}

/* Convierte la lista serializada (ya copiada a `buf`, n bytes) en struct pdf_page. 0 = valida. */
static int unpack(uint8_t *buf, uint64_t n, struct pdf_page *pg)
{
    if (n < sizeof(struct sbx_pdf_hdr)) return -1;
    const struct sbx_pdf_hdr *hd = (const struct sbx_pdf_hdr *)buf;
    if (hd->magic != SBX_PDFL_MAGIC || hd->nops < 0 || hd->nops > PDF_MAX_OPS) return -1;
    if (hd->w < 0 || hd->h < 0 || hd->w > PDF_MAX_DIM || hd->h > PDF_MAX_DIM) return -1;
    uint64_t ops_end = sizeof *hd + (uint64_t)hd->nops * sizeof(struct sbx_pdf_op);
    if (ops_end > n || n - ops_end != hd->blob_len) return -1;
    const struct sbx_pdf_op *so = (const struct sbx_pdf_op *)(buf + sizeof *hd);
    const uint8_t *bl = buf + ops_end; uint64_t bn = hd->blob_len;
    struct pdf_op *ops = hd->nops ? (struct pdf_op *)kmalloc((size_t)hd->nops * sizeof *ops) : 0;
    if (hd->nops && !ops) return -2;
    for (int i = 0; i < hd->nops; ++i) {
        const struct sbx_pdf_op *s = &so[i];
        struct pdf_op *o = &ops[i];
        uint8_t *z = (uint8_t *)o; for (size_t k = 0; k < sizeof *o; ++k) z[k] = 0;
        if (s->kind < PDF_OP_TEXT || s->kind > PDF_OP_IMAGE || s->font > 64) goto bad;
        o->kind = s->kind; o->flags = s->flags; o->size = s->size; o->x = s->x; o->y = s->y; o->w = s->w; o->h = s->h;
        o->color = s->color; o->fill = s->fill; o->flip_x = s->flip_x; o->flip_y = s->flip_y; o->font = s->font;
        if (s->kind == PDF_OP_TEXT && s->n) {
            if (s->n > (1u << 20) || (uint64_t)s->off + s->n > bn) goto bad;
            o->text = (const char *)(bl + s->off); o->len = (int)s->n;
        } else if (s->kind == PDF_OP_PATH && s->n) {
            if (s->n > (1u << 24) || (s->off & 3) || (uint64_t)s->off + (uint64_t)s->n * 8u > bn) goto bad;
            o->pts = (const int32_t *)(bl + s->off); o->npts = (int)s->n;
        } else if (s->kind == PDF_OP_IMAGE && s->n) {
            if (s->iw < 1 || s->ih < 1 || s->iw > PDF_MAX_DIM || s->ih > PDF_MAX_DIM || (uint64_t)s->iw * (uint64_t)s->ih > (64u << 20) ||
                (s->off & 3) || (uint64_t)s->off + (uint64_t)s->iw * (uint64_t)s->ih * 4u > bn || s->w < 1 || s->h < 1) goto bad;
            o->img = (const uint32_t *)(bl + s->off); o->iw = s->iw; o->ih = s->ih;
        }
    }
    pg->ops = ops; pg->nops = hd->nops; pg->cap = hd->nops; pg->w = hd->w; pg->h = hd->h; pg->unknown = hd->unknown; pg->arena = buf;
    return 0;
bad:
    if (ops) kfree(ops);
    return -1;
}

/* solo pruebas (sandbox_test.c): el validador contra listas manipuladas. Copia `src` y devuelve el resultado de unpack. */
int pdf_sbx_unpack_test(const uint8_t *src, uint32_t n)
{
    uint8_t *buf = (uint8_t *)kmalloc(n ? n : 1);
    if (!buf) return -2;
    for (uint32_t i = 0; i < n; ++i) buf[i] = src[i];
    struct pdf_page pg;
    int r = unpack(buf, n, &pg);
    if (r == 0) pdf_page_free(&pg); else kfree(buf);
    return r;
}

int pdf_render_page(struct pdf *p, int idx, int scale256, struct pdf_page *out)
{
    uint8_t *z = (uint8_t *)out; for (size_t k = 0; k < sizeof *out; ++k) z[k] = 0;
    if (!p || idx < 0 || idx >= p->npages) return -1;
    struct sbx_call *c;
    int r = call(SBX_PDF_RENDER, p->data, p->len, idx, scale256, &c), ret = -1;
    if (r == 0 && c->out.status == 0 && c->out.data_len >= sizeof(struct sbx_pdf_hdr) && c->out.data_len <= (160u << 20)) {
        uint8_t *buf = (uint8_t *)kmalloc((size_t)c->out.data_len);
        if (buf && sbx_copy_data(c, buf, c->out.data_len) == 0 && unpack(buf, c->out.data_len, out) == 0) ret = 0;
        else if (buf) kfree(buf);
    }
    done(c);
    return ret;
}

void pdf_page_free(struct pdf_page *pg)
{
    if (!pg) return;
    if (pg->ops) kfree(pg->ops);
    if (pg->arena) kfree(pg->arena);
    uint8_t *z = (uint8_t *)pg; for (size_t k = 0; k < sizeof *pg; ++k) z[k] = 0;
}

int pdf_page_text(const struct pdf_page *pg, char *out, int max)   /* igual que en pdf.c (opera sobre la lista ya validada) */
{
    int k = 0, lasty = -100000, lastx = 0;
    if (max < 1) return 0;
    for (int i = 0; i < pg->nops && k < max - 2; i++) {
        const struct pdf_op *o = &pg->ops[i];
        if (o->kind != PDF_OP_TEXT || !o->text) continue;
        int dy = o->y - lasty; if (dy < 0) dy = -dy;
        if (k > 0) { if (dy > o->size / 2) out[k++] = '\n'; else if (o->x > lastx) out[k++] = ' '; }
        for (int j = 0; j < o->len && k < max - 1; j++) out[k++] = o->text[j];
        lasty = o->y; lastx = o->x + o->w;
    }
    out[k] = 0; return k;
}
#endif
