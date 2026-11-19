/* PARCHE 138: referencia de host para las imagenes de prueba del servicio aislado imgdec.
 * Decodifica cada imagen de src/arch/x86_64/sbx_testimg.h con los MISMOS decodificadores (web_img.c, bmp_dec.c...) compilados en el host
 * y comprueba que el hash coincide con el esperado guardado en la cabecera (el kernel compara contra esos mismos valores lo que
 * devuelve el proceso aislado). Con --gen imprime las lineas de resultados para tools/gen_sbx_testimg.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_img.h"
#include "image.h"
#include "jpeg.h"
#include "web_inflate.h"
#include "pdf_hash.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }
#define SBX_TESTIMG_NO_EXPECT_CHECK
#include "sbx_testimg.h"

static uint32_t fnv(const uint32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) for (int b = 0; b < 4; ++b) { h ^= (uint8_t)(p[i] >> (8 * b)); h *= 16777619u; }
    return h;
}

int main(int argc, char **argv)
{
    int gen = argc > 1 && !strcmp(argv[1], "--gen"), bad = 0;
    for (int i = 0; i < SBX_TESTIMG_N; ++i) {
        const struct sbx_testimg *t = &sbx_testimg[i];
        int w = 0, h = 0, r; uint32_t hs = 0;
        if (t->op == 4) {                                   /* PARCHE 139: jpeg_decode crudo (MJPEG) */
            struct jpeg_info ji; memset(&ji, 0, sizeof ji);
            int jr = jpeg_get_info(t->data, t->len, &ji);
            if (jr == 0 && (ji.width > (uint32_t)t->a0 || ji.height > (uint32_t)t->a1)) jr = JPEG_E_SIZE;
            uint32_t *b = jr == 0 ? calloc((size_t)t->a0 * ji.height, 4) : 0;
            if (jr == 0) jr = jpeg_decode(t->data, t->len, b, (uint32_t)t->a0, ji.height, &ji);
            r = jr; if (jr == 0) { w = (int)ji.width; h = (int)ji.height; hs = fnv(b, (size_t)t->a0 * h); }
            free(b);
        } else if (t->op == 3) {
            struct image im; memset(&im, 0, sizeof im);
            r = bmp_decode(t->data, t->len, &im);
            if (r == 0) { w = (int)im.width; h = (int)im.height; hs = fnv(im.pixels, (size_t)w * h); free(im.pixels); }
        } else {
            struct wimg im; memset(&im, 0, sizeof im);
            r = t->op == 2 ? wi_decode_fit_local(t->data, t->len, &im, t->a0, t->a1) : wi_decode_local(t->data, t->len, &im);
            if (r == 0) { w = im.w; h = im.h; hs = fnv(im.px, (size_t)w * h); wi_free(&im); }
        }
        if (gen) { printf("%s %d %d %d 0x%08x\n", t->name, r, w, h, hs); continue; }
        int ok = r == t->exp_r && w == t->exp_w && h == t->exp_h && hs == t->exp_hash;
        if (!ok) { bad++; printf("FALLO %s: r=%d %dx%d hash=0x%08x (esperado r=%d %dx%d 0x%08x)\n", t->name, r, w, h, hs, t->exp_r, t->exp_w, t->exp_h, t->exp_hash); }
    }
    for (int i = 0; i < SBX_TESTZ_N; ++i) {                 /* PARCHE 139: inflate */
        const struct sbx_testz *z = &sbx_testz[i];
        struct wb o; memset(&o, 0, sizeof o);
        int r = wz_inflate(z->data, z->len, z->fmt, &o, z->max_out);
        uint32_t hs = 2166136261u; for (uint32_t k = 0; k < o.len; ++k) { hs ^= (uint8_t)o.p[k]; hs *= 16777619u; }
        if (gen) printf("Z %s %d %u 0x%08x\n", z->name, r, o.len, hs);
        else if (r != z->exp_r || o.len != z->exp_len || hs != z->exp_hash) { bad++; printf("FALLO %s: r=%d len=%u hash=0x%08x\n", z->name, r, o.len, hs); }
        wb_free(&o);
    }
    for (int i = 0; i < SBX_TESTPDF_N; ++i) {               /* PARCHE 141: PDF (pdf.c directo) */
        const struct sbx_testpdf *t = &sbx_testpdf[i];
        char err[128]; uint32_t h = 2166136261u; int np = 0, ok = 0;
        struct pdf *p = pdf_open(t->data, t->len, err, sizeof err);
        if (p) {
            ok = 1; np = pdf_page_count(p);
            char title[256]; int tl = pdf_title(p, title, sizeof title); h = pdfh_i(h, np); h = pdfh_mix(h, title, tl > 0 ? (uint32_t)tl : 0);
            for (int k = 0; k < np; ++k) {
                int w = 0, hh = 0; pdf_page_size(p, k, &w, &hh); h = pdfh_i(h, w); h = pdfh_i(h, hh);
                struct pdf_page pg; int r = pdf_render_page(p, k, 256, &pg); h = pdfh_i(h, r);
                if (r == 0) { h = pdfh_i(h, pdf_dl_hash(&pg)); pdf_page_free(&pg); }
            }
            pdf_close(p);
        }
        if (gen) printf("P %s %d %d 0x%08x\n", t->name, ok, np, h);
        else if (ok != t->exp_open || np != t->exp_pages || h != t->exp_hash) { bad++; printf("FALLO %s: abre=%d paginas=%d hash=0x%08x\n", t->name, ok, np, h); }
    }
    if (!gen) printf("%s: %d imagenes + %d flujos comprimidos + %d PDF de referencia, %d fallos\n", bad ? "FALLO" : "OK", SBX_TESTIMG_N, SBX_TESTZ_N, SBX_TESTPDF_N, bad);
    return bad != 0;
}
