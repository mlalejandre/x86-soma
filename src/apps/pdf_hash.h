#ifndef SOMA_PDF_HASH_H
#define SOMA_PDF_HASH_H
/* PARCHE 141: huella de una lista de dibujo de PDF (todos los campos y los datos apuntados). La usan la prueba del kernel
 * (lo que llega del servicio aislado) y la referencia de host (pdf.c directo): deben coincidir bit a bit. */
#include <stdint.h>
#include "pdf.h"
static inline uint32_t pdfh_mix(uint32_t h, const void *p, uint32_t n) { const uint8_t *b = (const uint8_t *)p; for (uint32_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; } return h; }
static inline uint32_t pdfh_i(uint32_t h, int64_t v) { return pdfh_mix(h, &v, 8); }
static inline uint32_t pdf_dl_hash(const struct pdf_page *pg)
{
    uint32_t h = 2166136261u;
    h = pdfh_i(h, pg->w); h = pdfh_i(h, pg->h); h = pdfh_i(h, pg->unknown); h = pdfh_i(h, pg->nops);
    for (int i = 0; i < pg->nops; ++i) {
        const struct pdf_op *o = &pg->ops[i];
        h = pdfh_i(h, o->kind); h = pdfh_i(h, o->flags); h = pdfh_i(h, o->size); h = pdfh_i(h, o->x); h = pdfh_i(h, o->y);
        h = pdfh_i(h, o->w); h = pdfh_i(h, o->h); h = pdfh_i(h, o->color); h = pdfh_i(h, o->fill); h = pdfh_i(h, o->flip_x);
        h = pdfh_i(h, o->flip_y); h = pdfh_i(h, o->font);
        if (o->kind == PDF_OP_TEXT && o->text) { h = pdfh_i(h, o->len); h = pdfh_mix(h, o->text, (uint32_t)o->len); }
        if (o->kind == PDF_OP_PATH && o->pts) { h = pdfh_i(h, o->npts); h = pdfh_mix(h, o->pts, (uint32_t)o->npts * 8u); }
        if (o->kind == PDF_OP_IMAGE && o->img) { h = pdfh_i(h, o->iw); h = pdfh_i(h, o->ih); h = pdfh_mix(h, o->img, (uint32_t)o->iw * (uint32_t)o->ih * 4u); }
    }
    return h;
}
#endif
