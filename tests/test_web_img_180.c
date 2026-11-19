/* SOMA_180: formatos de imagen nuevos del navegador: WebP (con perdida, sin perdida, alfa, animado), GIF animado, ICO y SVG.
 * Los pixeles de WebP/GIF/ICO se comparan (huella) con los que da la referencia (Pillow/libwebp); el SVG, por colores en puntos conocidos. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_img.h"
#include "web_img_180_data.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }
#define CHECK(c) do { if (!(c)) { printf("web_img_180: fallo linea %d: %s\n", __LINE__, #c); return 1; } } while (0)
static unsigned fnv(const uint32_t *p, size_t n) { unsigned h = 2166136261u; for (size_t i = 0; i < n; i++) { uint32_t v = (p[i] >> 24) ? p[i] : 0; h = (h ^ v) * 16777619u; } return h; }
static uint32_t at(const struct wimg *im, int x, int y) { return im->px[(size_t)y * im->w + x]; }
static int near(uint32_t c, uint32_t want, int tol) { for (int sh = 0; sh < 32; sh += 8) { int d = (int)((c >> sh) & 255) - (int)((want >> sh) & 255); if (d < -tol || d > tol) return 0; } return 1; }
static int svg(const char *s, struct wimg *im, int dw, int dh, uint32_t col) { memset(im, 0, sizeof *im); return wi_decode_svg((const uint8_t *)s, (uint32_t)strlen(s), im, dw, dh, col); }
int main(void)
{
    struct wimg im;
    for (unsigned i = 0; i < sizeof t180_cases / sizeof t180_cases[0]; i++) {
        memset(&im, 0, sizeof im);
        int r = t180_cases[i].anim ? wi_decode_anim(t180_cases[i].d, t180_cases[i].n, &im, 0) : wi_decode(t180_cases[i].d, t180_cases[i].n, &im);
        if (r != 0 || im.w != t180_cases[i].w || im.h != t180_cases[i].h || im.frames != t180_cases[i].frames) { printf("web_img_180: %s no decodifica bien (r=%d %dx%d f=%d)\n", t180_cases[i].name, r, im.w, im.h, im.frames); return 1; }
        size_t n = (size_t)im.w * im.h * (size_t)(im.frames > 1 ? im.frames : 1);
        if (fnv(im.px, n) != t180_cases[i].hash) { printf("web_img_180: %s: pixeles distintos de la referencia\n", t180_cases[i].name); return 1; }
        if (t180_cases[i].anim) { CHECK(wi_anim_frames(t180_cases[i].d, t180_cases[i].n) == im.frames); CHECK(im.delay && im.delay[0] == 80 && im.delay[5] == 130); }
        else CHECK(wi_anim_frames(t180_cases[i].d, t180_cases[i].n) == 1);
        wi_free(&im);
        /* un animado tambien se puede pedir como imagen fija (primer fotograma) y reducido si no cabe */
        if (t180_cases[i].anim) {
            memset(&im, 0, sizeof im); CHECK(wi_decode(t180_cases[i].d, t180_cases[i].n, &im) == 0 && im.w == t180_cases[i].w && im.frames == 0); wi_free(&im);
            memset(&im, 0, sizeof im); CHECK(wi_decode_anim(t180_cases[i].d, t180_cases[i].n, &im, 20000) == 0 && im.frames == 6 && im.w == 48 && im.h == 32 && im.nw == 96 && im.nh == 64); wi_free(&im);
            memset(&im, 0, sizeof im); CHECK(wi_decode_anim(t180_cases[i].d, t180_cases[i].n, &im, 1500) == 0 && im.frames >= 1 && im.frames < 6); wi_free(&im);
        }
        /* cortado a la mitad: falla o decodifica, pero sin salirse */
        memset(&im, 0, sizeof im); (void)wi_decode(t180_cases[i].d, t180_cases[i].n / 2, &im); wi_free(&im);
    }
    /* SVG: formas, colores, viewBox, transformaciones, estilos, degradado, use/symbol y currentColor */
    CHECK(wi_is_svg((const uint8_t *)"<?xml version=\"1.0\"?>\n<!-- c --><svg xmlns='x'/>", 46) == 1);
    CHECK(wi_is_svg((const uint8_t *)"<html><svg></svg></html>", 24) == 0);
    CHECK(svg("<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20'><rect width='20' height='20' fill='#ff0000'/><rect x='20' width='20' height='20' fill='rgb(0,0,255)' fill-opacity='.5'/></svg>", &im, 0, 0, 0) == 0);
    CHECK(im.w == 40 && im.h == 20 && im.nw == 40 && near(at(&im, 5, 5), 0xFFFF0000u, 1) && near(at(&im, 30, 10), 0x800000FFu, 2)); wi_free(&im);
    CHECK(svg("<svg viewBox='0 0 10 10'><circle cx='5' cy='5' r='4' fill='lime' stroke='black' stroke-width='1'/></svg>", &im, 100, 100, 0) == 0);
    CHECK(im.w == 100 && im.nw == 10 && near(at(&im, 50, 50), 0xFF00FF00u, 1) && (at(&im, 2, 2) >> 24) == 0 && near(at(&im, 50, 10), 0xFF000000u, 8) && near(at(&im, 50, 20), 0xFF00FF00u, 1)); wi_free(&im);
    CHECK(svg("<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='4'><path d='M2 12 H22'/></svg>", &im, 48, 48, 0x3366CC) == 0);
    CHECK(near(at(&im, 24, 24), 0xFF3366CCu, 1) && (at(&im, 24, 10) >> 24) == 0 && (at(&im, 24, 38) >> 24) == 0); wi_free(&im);
    CHECK(svg("<svg width='20' height='20'><style>.a{fill:#00f} #b{fill:#0f0}</style><g transform='translate(10,0)'><rect class='a' width='10' height='10'/></g><rect id='b' y='10' width='10' height='10' style='fill:#f0f'/>"
              "<path d='M0 0h10v10h-10z M2 2h6v6h-6z' fill-rule='evenodd' fill='red'/></svg>", &im, 0, 0, 0) == 0);
    CHECK(near(at(&im, 15, 5), 0xFF0000FFu, 1) && near(at(&im, 5, 15), 0xFFFF00FFu, 1) && near(at(&im, 1, 1), 0xFFFF0000u, 1) && (at(&im, 5, 5) >> 24) == 0 && (at(&im, 15, 15) >> 24) == 0); wi_free(&im);
    CHECK(svg("<svg viewBox='0 0 100 10'><defs><linearGradient id='g'><stop offset='0' stop-color='#000'/><stop offset='1' stop-color='#fff'/></linearGradient>"
              "<symbol id='s' viewBox='0 0 2 2'><rect width='2' height='2'/></symbol></defs><rect width='100' height='5' fill='url(#g)'/><use href='#s' x='40' y='5' width='5' height='5' fill='#0a0'/></svg>", &im, 0, 0, 0) == 0);
    CHECK(near(at(&im, 1, 2), 0xFF030303u, 6) && near(at(&im, 50, 2), 0xFF808080u, 8) && near(at(&im, 98, 2), 0xFFFBFBFBu, 6) && near(at(&im, 42, 7), 0xFF00AA00u, 1) && (at(&im, 60, 7) >> 24) == 0); wi_free(&im);
    /* arco y rotacion: un semicirculo girado 90 grados sigue siendo medio disco */
    CHECK(svg("<svg viewBox='-10 -10 20 20'><path transform='rotate(90)' d='M-8 0 A8 8 0 0 1 8 0 Z' fill='#123456'/></svg>", &im, 200, 200, 0) == 0);
    CHECK(near(at(&im, 140, 100), 0xFF123456u, 1) && (at(&im, 60, 100) >> 24) == 0 && near(at(&im, 110, 40), 0xFF123456u, 1) && near(at(&im, 110, 160), 0xFF123456u, 1) && (at(&im, 185, 100) >> 24) == 0); wi_free(&im);
    /* nombres en minusculas (el DOM de HTML los pasa asi) y elementos sin cerrar */
    CHECK(svg("<svg viewbox='0 0 4 4'><lineargradient id='q'><stop offset='0' stop-color='red'></stop></lineargradient><rect width='4' height='4' fill='url(#q)'></rect></svg>", &im, 8, 8, 0) == 0);
    CHECK(near(at(&im, 4, 4), 0xFFFF0000u, 1)); wi_free(&im);
    /* basura: no falla ni se cuelga */
    CHECK(svg("<svg", &im, 0, 0, 0) != 0 || 1); wi_free(&im);
    CHECK(svg("<svg viewBox='0 0 1e30 1e30' width='99999999' height='1'><path d='M0 0 A 1e20 1e20 0 1 1 5 5 C'/><use href='#x'/><g transform='matrix(1e9 1e9 1e9 1e9 1e9 1e9) rotate('><circle r='1e12'/></g></svg>", &im, 0, 0, 0) <= 0); wi_free(&im);
    printf("web_img_180: WebP (4 variantes + animado), GIF animado, ICO (PNG y BMP) y SVG correctos\n");
    return 0;
}
