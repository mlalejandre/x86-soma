/* PARCHE 141: servicio aislado del analizador PDF (ring 3, solo exit + svc_next). Lo usa el kernel desde src/apps/pdf_sbx.c.
 * Todo lo que interpreta bytes del PDF (xref, flujos de objetos, FlateDecode y predictores, fuentes y ToUnicode, contenido de pagina,
 * imagenes DCT/crudas) corre aqui. Sin estado entre peticiones: cada una trae el documento entero y se analiza desde cero.
 * La respuesta es la lista de dibujo SERIALIZADA (sbx_abi.h): sin punteros; el kernel valida cada desplazamiento antes de usarlo. */
#include "sbxrt.h"
#include "../apps/pdf.h"
#include "../apps/web_img.h"

uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }
/* dentro del servicio, las imagenes del PDF se decodifican directamente (ya estamos aislados: nada de servicios anidados) */
int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o) { return wi_decode_local(d, n, o); }
int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh) { return wi_decode_fit_local(d, n, o, maxw, maxh); }

static void put_inline(struct sbx_out *out, const char *s)
{
    uint32_t n = 0;
    while (s[n] && n < SBX_OUT_INLINE - 1) { out->inline_data[n] = (uint8_t)s[n]; n++; }
    out->inline_data[n] = 0; out->inline_len = n;
}

static int do_open(const uint8_t *data, uint32_t len, struct sbx_out *out)
{
    char err[200]; err[0] = 0;
    struct pdf *p = pdf_open(data, len, err, sizeof err);
    if (!p) { out->status = -1; put_inline(out, err[0] ? err : "PDF no valido"); return 0; }
    int np = pdf_page_count(p);
    if (np < 0) np = 0;
    if (np > (int)SBX_PDF_MAX_PAGES) np = (int)SBX_PDF_MAX_PAGES;
    int32_t *sz = np ? (int32_t *)kmalloc((size_t)np * 8u) : 0;
    if (np && !sz) { pdf_close(p); out->status = -2; return 0; }
    for (int i = 0; i < np; ++i) { int w = 0, h = 0; if (pdf_page_size(p, i, &w, &h) != 0) w = h = 0; sz[2 * i] = w; sz[2 * i + 1] = h; }
    char title[1024]; title[0] = 0;
    if (pdf_title(p, title, sizeof title) <= 0) title[0] = 0;
    put_inline(out, title);
    out->val[0] = np; out->status = 0;
    if (np) { out->data_va = (uint64_t)(uintptr_t)sz; out->data_len = (uint64_t)np * 8u; }
    return 0;
}

static uint32_t al4(uint32_t v) { return (v + 3u) & ~3u; }

static int do_render(const uint8_t *data, uint32_t len, int idx, int scale, struct sbx_out *out)
{
    char err[64];
    struct pdf *p = pdf_open(data, len, err, sizeof err);
    if (!p) { out->status = -1; return 0; }
    struct pdf_page pg;
    if (pdf_render_page(p, idx, scale, &pg) != 0) { pdf_close(p); out->status = -1; return 0; }
    /* tamano del blob */
    uint64_t blob = 0;
    for (int i = 0; i < pg.nops; ++i) {
        const struct pdf_op *o = &pg.ops[i];
        if (o->kind == PDF_OP_TEXT && o->text && o->len > 0) blob += al4((uint32_t)o->len);
        else if (o->kind == PDF_OP_PATH && o->pts && o->npts > 0) blob += (uint64_t)o->npts * 8u;
        else if (o->kind == PDF_OP_IMAGE && o->img && o->iw > 0 && o->ih > 0) blob += (uint64_t)o->iw * (uint64_t)o->ih * 4u;
    }
    uint64_t total = sizeof(struct sbx_pdf_hdr) + (uint64_t)pg.nops * sizeof(struct sbx_pdf_op) + blob;
    if (total > (160u << 20)) { pdf_page_free(&pg); pdf_close(p); out->status = -2; return 0; }
    uint8_t *buf = (uint8_t *)kmalloc((size_t)total);
    if (!buf) { pdf_page_free(&pg); pdf_close(p); out->status = -2; return 0; }
    struct sbx_pdf_hdr *hd = (struct sbx_pdf_hdr *)buf;
    struct sbx_pdf_op *so = (struct sbx_pdf_op *)(buf + sizeof *hd);
    uint8_t *bl = (uint8_t *)(so + pg.nops);
    hd->magic = SBX_PDFL_MAGIC; hd->w = pg.w; hd->h = pg.h; hd->unknown = pg.unknown; hd->nops = pg.nops; hd->blob_len = (uint32_t)blob;
    uint32_t pos = 0;
    for (int i = 0; i < pg.nops; ++i) {
        const struct pdf_op *o = &pg.ops[i];
        struct sbx_pdf_op *d = &so[i];
        memset(d, 0, sizeof *d);
        d->kind = o->kind; d->flags = o->flags; d->flip_x = o->flip_x; d->flip_y = o->flip_y; d->font = o->font;
        d->size = o->size; d->x = o->x; d->y = o->y; d->w = o->w; d->h = o->h; d->color = o->color; d->fill = o->fill;
        if (o->kind == PDF_OP_TEXT && o->text && o->len > 0) {
            d->off = pos; d->n = (uint32_t)o->len; memcpy(bl + pos, o->text, (size_t)o->len); pos += al4((uint32_t)o->len);
        } else if (o->kind == PDF_OP_PATH && o->pts && o->npts > 0) {
            d->off = pos; d->n = (uint32_t)o->npts; memcpy(bl + pos, o->pts, (size_t)o->npts * 8u); pos += (uint32_t)o->npts * 8u;
        } else if (o->kind == PDF_OP_IMAGE && o->img && o->iw > 0 && o->ih > 0) {
            d->off = pos; d->iw = o->iw; d->ih = o->ih; d->n = 1;
            memcpy(bl + pos, o->img, (size_t)o->iw * (size_t)o->ih * 4u); pos += (uint32_t)o->iw * (uint32_t)o->ih * 4u;
        }
    }
    pdf_page_free(&pg);
    pdf_close(p);
    out->status = 0;
    out->data_va = (uint64_t)(uintptr_t)buf; out->data_len = total;
    return 0;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    if (in->op == SBX_PDF_OPEN) return do_open(data, in->len, out);
    if (in->op == SBX_PDF_RENDER) return do_render(data, in->len, in->arg[0], in->arg[1], out);
    out->status = -1;
    return 0;
}
