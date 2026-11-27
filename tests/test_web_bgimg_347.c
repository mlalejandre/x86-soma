/* SOMA_347_NAVEGADOR_ICONOS: imagenes de fondo (en el anfitrion, sobre tests/layhost/fbhost.c). Casos de 20minutos.es:
 * - un SVG solo con viewBox (sin tamano propio) en una caja de 13x13 con background-size auto se encaja como contain
 *   (antes se pintaba a 512x512 y solo se veia una esquina transparente: iconos de reloj y autor);
 * - background-image en un elemento en linea (antes solo se pintaba el color y el degradado);
 * - "left 1rem center" + background-size en un enlace inline-flex (boton "Anadenos en Google"). */
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
static uint32_t rojo24[24 * 24], disco[64 * 64];
/* r.png: 24x24 rojo opaco. v.svg: 64x64 (como si fuera de 512) con un disco rojo y las esquinas transparentes; solo proporcion. */
static int img_cb(void *ud, const char *url, struct wimginfo *o)
{
    (void)ud;
    if (strstr(url, "r.png")) { o->state = 2; o->w = o->h = 24; o->px = rojo24; o->has_alpha = 0; o->serial = 1; return 1; }
    if (strstr(url, "v.svg")) { o->state = 2; o->w = o->h = 512; o->pw = o->ph = 64; o->px = disco; o->has_alpha = 1; o->serial = 2; o->ratio_only = 1; return 1; }
    o->state = 3; return 0;
}
static void collect_styles(struct wcss *c, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) { struct wb b = {0}; wd_text_content(n, &b); if (b.p) wc_add_sheet(c, b.p, b.len, 1); }
        if (n->first) collect_styles(c, n->first);
    }
}
static void paint(const char *html)
{
    static uint32_t fb[300 * 120]; g_fb = fb; g_fbw = 300; g_fbh = 120;
    for (int i = 0; i < 300 * 120; i++) fb[i] = 0xFFFFFFFFu;
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); collect_styles(c, d->root); wc_compute(c, 300, 120);
    struct wlay *L = wl_new(d, c, img_cb, 0); L->ar.limit = 14u << 20; wl_layout(L, 300, 120);
    fb_set_clip(0, 0, 300, 120); wl_paint(L, 0, 0, 300, 120, 0, 0);
}
static void px(const char *name, int x, int y, int r, int g, int b)
{
    uint32_t v = g_fb[y * g_fbw + x]; int R = (v >> 16) & 255, G = (v >> 8) & 255, B = v & 255;
    checks++; if (abs(R - r) > 40 || abs(G - g) > 40 || abs(B - b) > 40) { fails++; printf("FALLO %s (%d,%d): %d,%d,%d (esperado %d,%d,%d)\n", name, x, y, R, G, B, r, g, b); }
}
int main(void)
{
    for (int i = 0; i < 24 * 24; i++) rojo24[i] = 0xFFFF0000u;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { int dx = x - 32, dy = y - 32; disco[y * 64 + x] = dx * dx + dy * dy < 30 * 30 ? 0xFFFF0000u : 0; }
    /* icono de 13x13 con un SVG solo de proporcion: centro rojo, como Chrome */
    paint("<!doctype html><style>html{font-size:10px}body{margin:0}#t:before{background:url(v.svg) no-repeat;content:'';display:inline-block;height:1.3rem;width:1.3rem}</style><span id='t'>x</span>");
    px("svg solo proporcion: centro", 6, 6, 255, 0, 0);
    /* fondo en un elemento en linea */
    paint("<!doctype html><style>body{margin:0;font:16px sans-serif}#s{background:url(r.png)}</style><p style='margin:0'>aa<span id='s'>MMMMMMMM</span></p>");
    px("en linea: fondo", 40, 8, 255, 0, 0);
    /* boton de 20minutos: posicion de 4 valores y background-size */
    paint("<!doctype html><style>html{font-size:10px}body{margin:0}div{display:flex}a{align-items:center;background:#fff url(r.png) no-repeat left 1rem center;background-size:1.6rem;border:.1rem solid #b6b7b8;border-radius:2rem;display:inline-flex;font-size:1.4rem;height:3rem;padding:0 1rem 0 3rem}</style><div><a>Anadenos en Google</a></div>");
    px("boton: icono", 1 + 10 + 8, 15, 255, 0, 0); px("boton: antes del icono", 1 + 5, 15, 255, 255, 255);
    /* degradado con borde redondeado: el interior conserva el degradado (antes se rellenaba con el color del padre) */
    paint("<!doctype html><style>body{margin:0;background:#fff}#g{width:100px;height:40px;border:2px solid #000;border-radius:10px;background:linear-gradient(#f00,#f00)}</style><div id='g'></div>");
    px("degradado + borde redondeado: interior", 50, 20, 255, 0, 0); px("degradado + borde redondeado: borde", 50, 0, 0, 0, 0); px("degradado + borde redondeado: esquina fuera", 0, 0, 255, 255, 255);
    /* fondo transparente con borde redondeado sobre un padre con imagen: se ve la imagen del padre */
    paint("<!doctype html><style>body{margin:0}#p{background:url(r.png);padding:4px}#c{height:30px;border:1px solid #000;border-radius:8px}</style><div id='p'><div id='c'></div></div>");
    px("transparente sobre imagen: interior", 60, 18, 255, 0, 0);
    /* texto suelto en flex con alto fijo y align-items:center: el elemento anonimo no toma el height del contenedor */
    paint("<!doctype html><style>body{margin:0;background:#fff}div{display:flex;align-items:center;height:40px;font:16px sans-serif;line-height:1;color:#000}</style><div>MMMMMMMM</div>");
    { int top = -1, bot = -1; for (int y = 0; y < 40; y++) for (int x = 0; x < 100; x++) if ((g_fb[y * g_fbw + x] & 0xFF) < 128) { if (top < 0) top = y; bot = y; }
      checks++; if (top < 0 || (top + bot) / 2 < 15 || (top + bot) / 2 > 25) { fails++; printf("FALLO flex anonimo centrado: tinta en filas %d..%d (esperado centro ~20)\n", top, bot); } }
    /* SOMA_349: <video poster>: la portada encajada en la caja (contain), centrada; fuera de ella, el fondo negro del video */
    paint("<!doctype html><style>body{margin:0}</style><video poster=r.png width=200 height=100></video>");
    px("video poster: centro", 100, 50, 255, 0, 0); px("video poster: banda lateral negra", 10, 50, 17, 17, 17);
    if (getenv("DUMP")) for (int y = 0; y < 40; y += 2) { for (int x = 0; x < 200; x += 2) { uint32_t v = g_fb[y * g_fbw + x]; putchar(((v >> 16) & 255) > 200 && ((v >> 8) & 255) < 80 ? 'R' : (v & 0xFFFFFF) == 0xFFFFFF ? '.' : '#'); } putchar('\n'); }
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO test_web_bgimg_347" : "test_web_bgimg_347 OK", checks, fails);
    return fails != 0;
}
