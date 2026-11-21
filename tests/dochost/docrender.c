/* SOMA_248_DOC: banco de pruebas de documentos en el anfitrion: maqueta un HTML o LaTeX con docgen.c (mismo codigo que SOMA),
 * lo pagina y escribe cada pagina como PPM, y la revision automatica por la salida.
 *   docrender documento.html prefijo [zoom]   ->  prefijo-1.ppm, prefijo-2.ppm ...  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "docgen.h"
#include "web_font.h"
#include "web_css.h"
#include "pdfw.h"

extern uint32_t *g_fb; extern int g_fbw, g_fbh;
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
/* lo que fbhost.c no trae */
static uint32_t *s_fb; static int s_w, s_h;
void fb_target_begin(uint32_t *buf, uint32_t w, uint32_t h) { s_fb = g_fb; s_w = g_fbw; s_h = g_fbh; g_fb = buf; g_fbw = (int)w; g_fbh = (int)h; }
void fb_target_end(void) { g_fb = s_fb; g_fbw = s_w; g_fbh = s_h; }
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w16, uint32_t argb)
{   /* lineas horizontales/verticales (las de LaTeX: reglas y fracciones) */
    (void)w16; int ax = x0 / 16, ay = y0 / 16, bx = x1 / 16, by = y1 / 16;
    if (ay == by) fb_fill_rect((uint32_t)(ax < bx ? ax : bx), (uint32_t)ay, (uint32_t)(abs(bx - ax) + 1), 1, argb & 0xFFFFFF);
    else fb_fill_rect((uint32_t)ax, (uint32_t)(ay < by ? ay : by), 1, (uint32_t)(abs(by - ay) + 1), argb & 0xFFFFFF);
}
void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) {
        int x = dx + i, y = dy + j; if (x < 0 || y < 0 || x >= g_fbw || y >= g_fbh) continue;
        uint32_t p = src[(j * sh / dh) * stride + i * sw / dw]; int a = (int)(p >> 24);
        if (a == 255) g_fb[y * g_fbw + x] = p; else if (a) { uint32_t d = g_fb[y * g_fbw + x]; uint32_t r = (((p >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255, g = (((p >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255, b = ((p & 255) * a + (d & 255) * (255 - a)) / 255; g_fb[y * g_fbw + x] = 0xFF000000u | r << 16 | g << 8 | b; }
    }
}
static int loader(const char *path, uint8_t **data, uint32_t *len)
{
    char p[512]; snprintf(p, sizeof p, "%s%s", getenv("DOC_ROOT") ? getenv("DOC_ROOT") : ".", path);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *data = malloc((size_t)n + 1); *len = (uint32_t)fread(*data, 1, (size_t)n, f); fclose(f); return 0;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: docrender doc.html prefijo [zoom]\n"); return 2; }
    g_wf182_on = 1;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1); n = (long)fread(buf, 1, (size_t)n, f); fclose(f); buf[n] = 0;
    dg_set_loader(loader);
    char err[200];
    struct dg_doc *d = dg_open(buf, (uint32_t)n, argc > 3 ? atoi(argv[3]) : 100, err, sizeof err);
    if (!d) { printf("ERROR %s\n", err); return 1; }
    if (getenv("DG_DUMP")) { void dg_debug_dump(struct dg_doc *); dg_debug_dump(d); }
    int w, h; dg_page_px(d, &w, &h);
    printf("PAGINAS %d  %dx%d  formato %s  titulo %s\n", dg_npages(d), w, h, dg_format_name(d), dg_title(d));
    uint32_t *pg = malloc((size_t)w * h * 4);
    struct pdfw *pdf = getenv("DG_PDF") ? pdfw_new(dg_title(d), "docrender") : 0;
    for (int i = 0; i < dg_npages(d); i++) {
        dg_render_page(d, i, pg);
        if (pdf) { int wp, hp; dg_page_pt(d, &wp, &hp); if (pdfw_page(pdf, pg, w, h, wp, hp)) printf("PDF: fallo en la pagina %d\n", i + 1); }
        char nm[512]; snprintf(nm, sizeof nm, "%s-%d.ppm", argv[2], i + 1);
        FILE *o = fopen(nm, "wb"); fprintf(o, "P6\n%d %d\n255\n", w, h);
        for (int k = 0; k < w * h; k++) { fputc((pg[k] >> 16) & 255, o); fputc((pg[k] >> 8) & 255, o); fputc(pg[k] & 255, o); }
        fclose(o);
    }
    if (pdf) { uint32_t n2; uint8_t *b = pdfw_finish(pdf, &n2); if (b) { FILE *o = fopen(getenv("DG_PDF"), "wb"); fwrite(b, 1, n2, o); fclose(o); printf("PDF %u bytes\n", n2); free(b); } else printf("PDF: error\n"); }
    if (getenv("DG_HTML")) { static char hb[1 << 20]; int hn = dg_export_html(d, buf, (uint32_t)n, hb, sizeof hb); if (hn > 0) { FILE *o = fopen(getenv("DG_HTML"), "wb"); fwrite(hb, 1, (size_t)hn, o); fclose(o); printf("HTML %d bytes\n", hn); } else printf("HTML: no\n"); }
    static char chk[4096]; int nc = dg_check(d, chk, sizeof chk);
    printf("REVISION %d\n%s", nc, chk);
    dg_close(d);
    return 0;
}
