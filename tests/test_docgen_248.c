/* SOMA_248_DOC: pruebas en el anfitrion del motor de documentos:
 *  - docgen: paginacion de un informe A4, presentacion 16:9 (una diapositiva por pagina), email (una pagina), LaTeX;
 *    la revision automatica detecta lo que se sale de la pagina; exportar HTML autonomo.
 *  - pdfw: PDF valido (cabecera, xref, paginas, titulo), y extraccion del IDAT de un PNG.
 *  - docagent: tipo de documento, cuando le toca la mision, extraccion del documento de una respuesta, veredicto y nombre. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "docgen.h"
#include "docagent.h"
#include "pdfw.h"
#include "png_enc.h"
#include "web_font.h"

extern uint32_t *g_fb; extern int g_fbw, g_fbh;
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint32_t *s_fb; static int s_w, s_h;
void fb_target_begin(uint32_t *buf, uint32_t w, uint32_t h) { s_fb = g_fb; s_w = g_fbw; s_h = g_fbh; g_fb = buf; g_fbw = (int)w; g_fbh = (int)h; }
void fb_target_end(void) { g_fb = s_fb; g_fbw = s_w; g_fbh = s_h; }
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w16, uint32_t argb) { (void)w16; int ax = x0 / 16, ay = y0 / 16, bx = x1 / 16, by = y1 / 16; if (ay == by) fb_fill_rect((uint32_t)(ax < bx ? ax : bx), (uint32_t)ay, (uint32_t)(abs(bx - ax) + 1), 1, argb & 0xFFFFFF); else fb_fill_rect((uint32_t)ax, (uint32_t)(ay < by ? ay : by), 1, (uint32_t)(abs(by - ay) + 1), argb & 0xFFFFFF); }
void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{ for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) { int x = dx + i, y = dy + j; if (x < 0 || y < 0 || x >= g_fbw || y >= g_fbh) continue; g_fb[y * g_fbw + x] = src[(j * sh / dh) * stride + i * sw / dw]; } }

static int fails, checks;
#define CHECK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)

static char big[200000];
static const char *report(int paras)
{
    int p = sprintf(big, "<!DOCTYPE html><html><head><meta name=\"soma-pagina\" content=\"A4; pie=Prueba {n}/{N}\"><title>Informe de prueba</title></head><body>"
                         "<div class=\"portada\"><div class=\"titulo\">Informe de prueba</div></div>");
    for (int i = 0; i < paras; i++) {
        if (i % 6 == 0) p += sprintf(big + p, "<h2>Seccion %d</h2>", i / 6 + 1);
        p += sprintf(big + p, "<p>Parrafo %d. El tercer trimestre cerro con una facturacion de 4,82 millones de euros, un 12,4 %% mas que en el mismo periodo "
                              "del ano anterior. El crecimiento se concentro en el canal digital y en los mercados de Portugal y Francia.</p>", i);
    }
    sprintf(big + p, "</body></html>");
    return big;
}
static int ink_rows(const uint32_t *px, int w, int h, uint32_t bg) { int n = 0; for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if ((px[y * w + x] & 0xFFFFFF) != (bg & 0xFFFFFF)) { n++; break; } return n; }

int main(void)
{
    g_wf182_on = 1;
    char err[160]; static char chk[4096];
    /* ---- informe A4 que ocupa varias paginas */
    const char *r = report(40);
    struct dg_doc *d = dg_open(r, (uint32_t)strlen(r), 100, err, sizeof err);
    CHECK(d != 0, "abre el informe");
    if (d) {
        int w, h, wp, hp; dg_page_px(d, &w, &h); dg_page_pt(d, &wp, &hp);
        CHECK(w == 794 && h == 1123, "A4 a 96 ppp");
        CHECK(wp == 595 && hp == 842, "A4 en puntos");
        CHECK(dg_npages(d) >= 3 && dg_npages(d) <= 6, "el informe se reparte en varias paginas");
        CHECK(strcmp(dg_title(d), "Informe de prueba") == 0, "titulo del documento");
        uint32_t *px = calloc((size_t)w * h, 4);
        CHECK(dg_render_page(d, 0, px) == 0, "dibuja la pagina 1");
        int rows = ink_rows(px, w, h, 0xFFFFFF);
        CHECK(rows > 300, "la pagina 1 tiene contenido");
        /* margen superior e izquierdo en blanco (20 mm = 75 px) */
        int dirty = 0; for (int y = 0; y < 60; y++) for (int x = 0; x < w; x++) if ((px[y * w + x] & 0xFFFFFF) != 0xFFFFFF) dirty = 1;
        CHECK(!dirty, "margen superior limpio");
        /* PDF */
        struct pdfw *pdf = pdfw_new(dg_title(d), "prueba");
        for (int i = 0; i < dg_npages(d); i++) { dg_render_page(d, i, px); CHECK(pdfw_page(pdf, px, w, h, wp, hp) == 0, "pagina del PDF"); }
        uint32_t n = 0; uint8_t *b = pdfw_finish(pdf, &n);
        CHECK(b && n > 1000, "PDF creado");
        if (b) {
            CHECK(memcmp(b, "%PDF-1.4", 8) == 0, "cabecera PDF");
            CHECK(memcmp(b + n - 6, "%%EOF\n", 6) == 0, "final PDF");
            char cnt[32]; sprintf(cnt, "/Count %d", dg_npages(d));
            CHECK(memmem(b, n, cnt, strlen(cnt)) != 0, "numero de paginas en el arbol");
            CHECK(memmem(b, n, "/MediaBox [0 0 595 842]", 23) != 0, "tamano de pagina en puntos");
            CHECK(memmem(b, n, "/Predictor 15", 13) != 0, "imagen con predictor PNG");
            /* la tabla xref apunta a "N 0 obj" */
            char *sx = memmem(b, n, "startxref\n", 10); int xo = sx ? atoi(sx + 10) : 0;
            CHECK(xo > 0 && memcmp(b + xo, "xref", 4) == 0, "startxref apunta a la tabla");
            int nobj = atoi((char *)b + xo + 7); int okx = nobj > 3;
            /* comprobacion fina: cada entrada en uso apunta a "<i> 0 obj" */
            const char *e = (const char *)b + xo; e = strchr(e, '\n') + 1; e = strchr(e, '\n') + 1; e += 20;
            for (int i = 1; i < nobj; i++, e += 20) { int off = atoi(e); char want[16]; int k = sprintf(want, "%d 0 obj", i); if (memcmp(b + off, want, (size_t)k) != 0) { okx = 0; break; } }
            CHECK(okx, "todas las entradas de la xref son correctas");
            FILE *f = fopen("build/tests/docgen_248.pdf", "wb"); if (f) { fwrite(b, 1, n, f); fclose(f); }
            free(b);
        }
        /* HTML autonomo */
        static char hb[300000]; int hn = dg_export_html(d, r, (uint32_t)strlen(r), hb, sizeof hb);
        CHECK(hn > (int)strlen(r) && strstr(hb, "@page{size:A4") && strstr(hb, "--acento") && strstr(hb, "Parrafo 39"), "HTML autonomo con la hoja de la casa y @page");
        CHECK(strstr(hb, "<meta charset=\"utf-8\">") != 0, "HTML autonomo declara utf-8");
        CHECK(dg_check(d, chk, sizeof chk) == 0, "el informe no tiene problemas");
        free(px);
        dg_close(d);
    }
    /* ---- zoom 200: mismas paginas, el doble de pixeles */
    d = dg_open(r, (uint32_t)strlen(r), 200, err, sizeof err);
    if (d) { int w, h; dg_page_px(d, &w, &h); CHECK(w == 1588 && h == 2246, "A4 a 192 ppp"); dg_close(d); }
    /* ---- presentacion */
    const char *pres = "<html><head><meta name=\"soma-pagina\" content=\"16:9\"><title>P</title></head><body>"
                       "<section class=\"diapositiva oscura\"><h1>Uno</h1></section><section class=\"diapositiva\"><h2>Dos</h2><ul><li>a</li></ul></section>"
                       "<section class=\"diapositiva acento\"><h1>Tres</h1></section></body></html>";
    d = dg_open(pres, (uint32_t)strlen(pres), 100, err, sizeof err);
    CHECK(d && dg_npages(d) == 3, "una diapositiva por pagina");
    if (d) {
        int w, h; dg_page_px(d, &w, &h); CHECK(w == 1280 && h == 720, "16:9");
        uint32_t *px = calloc((size_t)w * h, 4); dg_render_page(d, 0, px);
        CHECK((px[(h / 2) * w + 20] & 0xFFFFFF) == 0x0F172A, "portada oscura de borde a borde");
        free(px);
        CHECK(dg_check(d, chk, sizeof chk) == 0, "la presentacion no tiene problemas");
        dg_close(d);
    }
    /* ---- diapositiva demasiado llena y tabla que se sale */
    {
        static char s2[20000]; int p = sprintf(s2, "<html><head><meta name=\"soma-pagina\" content=\"16:9\"></head><body><section class=\"diapositiva\"><h2>Demasiado</h2><ul>");
        for (int i = 0; i < 30; i++) p += sprintf(s2 + p, "<li>Punto numero %d con bastante texto</li>", i);
        sprintf(s2 + p, "</ul></section></body></html>");
        d = dg_open(s2, (uint32_t)strlen(s2), 100, err, sizeof err);
        CHECK(d && dg_check(d, chk, sizeof chk) > 0 && strstr(chk, "diapositiva"), "detecta la diapositiva desbordada");
        if (d) dg_close(d);
        const char *wide = "<html><body><div style=\"width:1400px\">ancho</div><p>x</p></body></html>";
        d = dg_open(wide, (uint32_t)strlen(wide), 100, err, sizeof err);
        CHECK(d && dg_check(d, chk, sizeof chk) > 0 && strstr(chk, "se sale"), "detecta lo que se sale del ancho");
        if (d) dg_close(d);
    }
    /* ---- email: una sola pagina */
    const char *em = "<html><head><meta name=\"soma-pagina\" content=\"email\"></head><body><div class=\"email\"><div class=\"email-cabecera\">Hola</div><div class=\"email-cuerpo\"><p>Texto</p><p class=\"centro\"><a class=\"boton\" href=\"#\">Ir</a></p></div></div></body></html>";
    d = dg_open(em, (uint32_t)strlen(em), 100, err, sizeof err);
    CHECK(d && dg_npages(d) == 1, "email en una pagina");
    if (d) { int w, h; dg_page_px(d, &w, &h); CHECK(w == 720 && h > 100 && h < 1200, "email a su altura"); dg_close(d); }
    /* ---- LaTeX */
    const char *tex = "%soma-pagina: A4\n\\documentclass{article}\\title{Prueba}\\begin{document}\\maketitle\\section{Uno} Texto $E=mc^2$.\\end{document}";
    d = dg_open(tex, (uint32_t)strlen(tex), 100, err, sizeof err);
    CHECK(d && dg_kind(d) == DG_TEX && dg_npages(d) == 1, "LaTeX en una pagina");
    if (d) { static char hb[4096]; CHECK(dg_export_html(d, tex, (uint32_t)strlen(tex), hb, sizeof hb) < 0, "LaTeX no se exporta como HTML"); dg_close(d); }

    /* ---- PNG -> IDAT */
    {
        uint32_t px[16 * 8]; for (int i = 0; i < 16 * 8; i++) px[i] = (uint32_t)i * 0x010203u;
        static uint8_t png[8192], wk[1 << 20], z[8192];
        size_t pn = png_encode_rgb(px, 16, 8, png, sizeof png, wk);
        size_t zn = pdfw_png_idat(png, pn, z, sizeof z);
        CHECK(pn > 0 && zn > 2 && z[0] == 0x78, "IDAT con cabecera zlib");
        CHECK(pdfw_png_idat((const uint8_t *)"nopng", 5, z, sizeof z) == 0, "rechaza lo que no es PNG");
    }
    /* ---- docagent: piezas puras */
    CHECK(strcmp(docagent_kind("Prepara una presentación para el comité"), "presentacion") == 0, "tipo presentacion");
    CHECK(strcmp(docagent_kind("redacta un paper con ecuaciones sobre redes"), "paper") == 0, "tipo paper");
    CHECK(strcmp(docagent_kind("escribe un email para invitar a la jornada"), "email") == 0, "tipo email");
    CHECK(strcmp(docagent_kind("hazme el currículum de un ingeniero"), "cv") == 0, "tipo cv");
    CHECK(strcmp(docagent_kind("Redacta un reportaje para la revista"), "revista") == 0, "tipo revista");
    CHECK(strcmp(docagent_kind("escribe el primer capítulo de una novela"), "libro") == 0, "tipo libro");
    CHECK(strcmp(docagent_kind("redacta un informe trimestral"), "informe") == 0, "tipo informe");
    CHECK(docagent_should_handle("Redacta un informe trimestral con formato profesional en PDF"), "mision de documento");
    CHECK(docagent_should_handle("prepárame una presentación sobre energía solar"), "mision de presentacion");
    CHECK(!docagent_should_handle("crea una app calculadora en soma"), "una app no es un documento");
    CHECK(!docagent_should_handle("hola, que tal"), "charla no es documento");
    static char out[4096]; int tx;
    uint32_t n = docagent_extract("Claro, aqui tienes:\n```html\n<!DOCTYPE html><html><body><p>Hola</p></body></html>\n```\nEspero que te guste", out, sizeof out, &tx);
    CHECK(n > 0 && strncmp(out, "<!DOCTYPE html>", 15) == 0 && strstr(out, "</html>") && !strstr(out, "```") && !tx, "extrae el HTML de la respuesta");
    n = docagent_extract("%soma-pagina: A4\n\\documentclass{article}\\begin{document}x\\end{document} fin", out, sizeof out, &tx);
    CHECK(n > 0 && tx && strncmp(out, "%soma-pagina", 12) == 0 && strstr(out, "\\end{document}") && !strstr(out, "fin"), "extrae el LaTeX con su formato");
    CHECK(docagent_extract("no hay documento", out, sizeof out, &tx) == 0, "sin documento");
    CHECK(docagent_verdict("VEREDICTO: PUBLICABLE") == 1, "veredicto publicable");
    CHECK(docagent_verdict("**VEREDICTO:** CORREGIR\nPROBLEMAS:\n- x") == 0, "veredicto corregir");
    CHECK(docagent_verdict("VEREDICTO: BIEN") == 1, "veredicto bien (interfaz)");
    CHECK(docagent_verdict("nada") == -1, "sin veredicto");
    char slug[20]; docagent_slug("Informe trimestral T3 2026: ¡Éxito!", slug, sizeof slug);
    CHECK(strcmp(slug, "informe_trimestral") == 0, "nombre de archivo corto y sin acentos");
    docagent_slug("Año de diseño", slug, sizeof slug); CHECK(strcmp(slug, "ano_de_diseno") == 0, "enes y tildes");
    docagent_slug("", slug, sizeof slug); CHECK(strcmp(slug, "documento") == 0, "nombre por defecto");
    CHECK(strlen(docagent_system_prompt()) > 2000 && strstr(docagent_system_prompt(), "soma-pagina"), "indicaciones del maquetador");

    printf("docgen 248: %d/%d comprobaciones correctas\n", checks - fails, checks);
    return fails ? 1 : 0;
}
