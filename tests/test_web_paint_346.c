/* SOMA_346_NAVEGADOR_RENDER: degradados con extremos transparentes (en el anfitrion, sobre tests/layhost/fbhost.c).
 * Antes un color con alfa 0 se pintaba opaco ("linear-gradient(transparent,#000)" era negro entero) y la mezcla era canal a canal
 * (la sombra "#fff0 -> rgb(0 0 0 / .6)" sobre las fotos de El Bierzo Digital dejaba un velo blanco). Valores = Chrome (+-3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_dom.h"
#include "web_css.h"
#include "web_layout.h"
#include "web_ua.h"
#include "web_util.h"
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
extern uint32_t *g_fb; extern int g_fbw, g_fbh;
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
static int fails, checks;
static void collect_styles(struct wcss *c, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) { struct wb b = {0}; wd_text_content(n, &b); if (b.p) wc_add_sheet(c, b.p, b.len, 1); }
        if (n->first) collect_styles(c, n->first);
    }
}
static void paint(const char *html)
{
    static uint32_t fb[200 * 120]; g_fb = fb; g_fbw = 200; g_fbh = 120;
    for (int i = 0; i < 200 * 120; i++) fb[i] = 0xFFFFFFFFu;
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); collect_styles(c, d->root); wc_compute(c, 200, 120);
    struct wlay *L = wl_new(d, c, 0, 0); L->ar.limit = 14u << 20; wl_layout(L, 200, 120);
    fb_set_clip(0, 0, 200, 120); wl_paint(L, 0, 0, 200, 120, 0, 0);
}
static void px(const char *name, int x, int y, int r, int g, int b)
{
    uint32_t v = g_fb[y * g_fbw + x]; int R = (v >> 16) & 255, G = (v >> 8) & 255, B = v & 255;
    checks++; if (abs(R - r) > 3 || abs(G - g) > 3 || abs(B - b) > 3) { fails++; printf("FALLO %s (%d,%d): %d,%d,%d (esperado %d,%d,%d)\n", name, x, y, R, G, B, r, g, b); }
}
int main(void)
{
    paint("<!doctype html><style>body{margin:0;background:#fff}#a{height:101px;background:linear-gradient(transparent,#000)}</style><div id='a'></div>");
    px("transparente->negro arriba", 50, 0, 255, 255, 255); px("transparente->negro medio", 50, 50, 128, 128, 128); px("transparente->negro abajo", 50, 100, 0, 0, 0);
    paint("<!doctype html><style>body{margin:0;background:#fff}#a{height:101px;background:linear-gradient(to bottom,#fff0,rgb(0 0 0 / .6))}</style><div id='a'></div>");
    px("#fff0->negro .6 arriba", 50, 0, 255, 255, 255); px("#fff0->negro .6 medio", 50, 50, 178, 178, 178); px("#fff0->negro .6 abajo", 50, 100, 102, 102, 102);
    paint("<!doctype html><style>body{margin:0;background:#00f}#a{height:101px;background:linear-gradient(rgba(255,0,0,0),rgba(255,0,0,1))}</style><div id='a'></div>");
    px("rojo transparente sobre azul arriba", 50, 0, 0, 0, 255); px("rojo sobre azul medio", 50, 50, 128, 0, 127); px("rojo sobre azul abajo", 50, 100, 255, 0, 0);
    paint("<!doctype html><style>body{margin:0;background:#fff}#a{height:101px;background:linear-gradient(#f00,#00f)}</style><div id='a'></div>");
    px("opacos medio", 50, 50, 128, 0, 127);
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO test_web_paint_346" : "test_web_paint_346 OK", checks, fails);
    return fails != 0;
}
