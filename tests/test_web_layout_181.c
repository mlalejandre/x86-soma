/* SOMA 181: pruebas de maquetacion del navegador (en el anfitrion, sin texto para no depender de la fuente).
 * Se enlaza con tests/layhost/fbhost.c. Los valores esperados coinciden con Chrome (getBoundingClientRect). */
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

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void collect_styles(struct wcss *c, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) { struct wb b = {0}; wd_text_content(n, &b); if (b.p) wc_add_sheet(c, b.p, b.len, 1); }
        if (n->first) collect_styles(c, n->first);
    }
}
static struct wlay *build(const char *html, int vw, int vh, struct wdoc **dout)
{
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d);
    wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0);
    collect_styles(c, d->root);
    wc_compute(c, vw, vh);
    struct wlay *L = wl_new(d, c, 0, 0);
    L->ar.limit = 14u << 20;
    wl_layout(L, vw, vh);
    *dout = d; return L;
}
static void rect(const char *name, struct wlay *L, struct wdoc *d, const char *id, int x, int y, int w, int h)
{
    struct wnode *n = wd_by_id(d->root, id); int a, b, c, e;
    if (!n || !wl_node_rect(L, n, &a, &b, &c, &e)) { checks++; fails++; printf("FALLO %s: #%s sin caja\n", name, id); return; }
    CHECK(a == x && b == y && c == w && e == h, "%s #%s: obtenido %d,%d %dx%d, esperado %d,%d %dx%d", name, id, a, b, c, e, x, y, w, h);
}
static uint32_t col(const char *s) { int ok = 0; uint32_t v = wc_parse_color(s, &ok); return ok ? (v & 0xFFFFFFu) : 0xFFFFFFFFu; }
static int near_col(uint32_t a, uint32_t b, int tol)
{
    for (int i = 0; i < 24; i += 8) { int d = (int)((a >> i) & 255) - (int)((b >> i) & 255); if (d < 0) d = -d; if (d > tol) return 0; }
    return 1;
}

int main(void)
{
    struct wdoc *d; struct wlay *L;

    /* rejilla con areas, filas fijas y 1fr */
    L = build("<style>body{margin:0}#g{display:grid;width:400px;height:300px;grid-template-columns:100px 1fr;grid-template-rows:40px 1fr 30px;"
              "grid-template-areas:'h h' 's m' 'f f'}#h{grid-area:h}#s{grid-area:s}#m{grid-area:m}#f{grid-area:f}</style>"
              "<div id=g><div id=h></div><div id=s></div><div id=m></div><div id=f></div></div>", 800, 600, &d);
    rect("grid", L, d, "h", 0, 0, 400, 40); rect("grid", L, d, "s", 0, 40, 100, 230);
    rect("grid", L, d, "m", 100, 40, 300, 230); rect("grid", L, d, "f", 0, 270, 400, 30);

    /* rejilla auto-fill con huecos */
    L = build("<style>body{margin:0}#g{display:grid;width:410px;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:10px}"
              ".i{height:20px}</style><div id=g><div class=i id=a></div><div class=i id=b></div><div class=i id=c></div><div class=i id=e></div>"
              "<div class=i id=f></div></div>", 800, 600, &d);
    /* caben 3 pistas (4*100+30 > 410): (410-20)/3 = 130 cada una; la cuarta pasa a la segunda fila */
    rect("autofill", L, d, "a", 0, 0, 130, 20); rect("autofill", L, d, "b", 140, 0, 130, 20);
    rect("autofill", L, d, "c", 280, 0, 130, 20); rect("autofill", L, d, "e", 0, 30, 130, 20);

    /* flex en columna: crecer y encoger */
    L = build("<style>body{margin:0}#c{display:flex;flex-direction:column;height:300px;width:200px}#a{height:50px}#b{flex:1}#e{height:50px}</style>"
              "<div id=c><div id=a></div><div id=b></div><div id=e></div></div>", 800, 600, &d);
    rect("flexcol", L, d, "a", 0, 0, 200, 50); rect("flexcol", L, d, "b", 0, 50, 200, 200); rect("flexcol", L, d, "e", 0, 250, 200, 50);

    /* aspect-ratio */
    L = build("<style>body{margin:0}#a{width:320px;aspect-ratio:16/9}#b{width:200px;aspect-ratio:4/1}</style><div id=a></div><div id=b></div>", 800, 600, &d);
    rect("aspect", L, d, "a", 0, 0, 320, 180); rect("aspect", L, d, "b", 0, 180, 200, 50);

    /* velo fijo que cubre la ventana */
    L = build("<style>body{margin:0}#s{position:fixed;inset:0}#d{position:fixed;left:50%;top:50%;width:200px;height:100px;margin:-50px 0 0 -100px}"
              "</style><div style='height:2000px'></div><div id=s></div><div id=d></div>", 640, 480, &d);
    rect("fixed", L, d, "s", 0, 0, 640, 480); rect("fixed", L, d, "d", 220, 190, 200, 100);

    /* unidades modernas y fit-content */
    L = build("<style>body{margin:0}#a{height:50dvh;width:25vw}#b{width:fit-content;height:10px}#b i{display:block;width:70px;height:5px}</style>"
              "<div id=a></div><div id=b><i></i></div>", 800, 600, &d);
    rect("units", L, d, "a", 0, 0, 200, 300); rect("units", L, d, "b", 0, 300, 70, 10);

    /* desplazamiento interno de cajas con overflow */
    L = build("<style>body{margin:0}#p{width:100px;height:80px;overflow:auto}#p div{height:50px}#q{width:100px;height:80px;overflow:hidden}#q div{height:50px}</style>"
              "<div id=p><div></div><div></div><div></div></div><div id=q><div></div><div></div><div></div></div>", 800, 600, &d);
    {
        struct wnode *p = wd_by_id(d->root, "p"), *q = wd_by_id(d->root, "q");
        CHECK(wl_node_scroll_max(L, p) == 70, "scroll max p = %d", wl_node_scroll_max(L, p));
        CHECK(wl_node_scroll_max(L, q) == 70, "overflow:hidden: scrollTop por JS si, rueda no");
        CHECK(wl_inner_scroll(L, 10, 10, 30) == 1 && p->scy == 30, "rueda 30 -> scy=%d", p->scy);
        CHECK(wl_inner_scroll(L, 10, 10, 500) == 1 && p->scy == 70, "tope inferior scy=%d", p->scy);
        CHECK(wl_inner_scroll(L, 10, 10, 10) == 0, "ya en el tope: no consume");
        CHECK(wl_inner_scroll(L, 10, 100, 10) == 0 && q->scy == 0, "overflow:hidden no consume la rueda");
        CHECK(wl_inner_scroll(L, 10, 10, -1000) == 1 && p->scy == 0, "tope superior scy=%d", p->scy);
        CHECK(wl_inner_gen(L) == 3, "generacion %u", wl_inner_gen(L));
    }

    /* colores CSS 4 */
    CHECK(col("hwb(0 0% 0%)") == 0xFF0000, "hwb rojo %06x", col("hwb(0 0% 0%)"));
    CHECK(col("hwb(120 0% 50%)") == 0x008000, "hwb verde %06x", col("hwb(120 0% 50%)"));
    CHECK(col("color-mix(in srgb, red, blue)") == 0x800080, "mix %06x", col("color-mix(in srgb, red, blue)"));
    CHECK(near_col(col("color-mix(in srgb, red 25%, blue)"), 0x4000BF, 2), "mix 25%% %06x", col("color-mix(in srgb, red 25%, blue)"));
    CHECK(near_col(col("oklch(62.8% 0.2577 29.23)"), 0xFF0000, 4), "oklch rojo %06x", col("oklch(62.8% 0.2577 29.23)"));
    CHECK(near_col(col("oklab(100% 0 0)"), 0xFFFFFF, 1), "oklab blanco %06x", col("oklab(100% 0 0)"));
    CHECK(near_col(col("lab(50% 0 0)"), 0x777777, 3), "lab gris %06x", col("lab(50% 0 0)"));
    CHECK(near_col(col("color(srgb 1 0 0)"), 0xFF0000, 1), "color() %06x", col("color(srgb 1 0 0)"));
    CHECK(col("light-dark(#123456, #abcdef)") == 0x123456, "light-dark %06x", col("light-dark(#123456, #abcdef)"));
    CHECK(col("rgb(10 20 30 / 50%)") != 0xFFFFFFFFu, "rgb moderno");

    /* SOMA_346_NAVEGADOR_RENDER: max-content con flotantes (se suman) y con el ancho fijo de flotantes e inline-block. El menu de El
     * Bierzo Digital (<li> flotantes dentro de un elemento flex) media 0 y los puntos caian uno debajo de otro. Valores de Chrome. */
    L = build("<!doctype html><style>body{margin:0}*{box-sizing:border-box}#m{display:flex;justify-content:center;width:1200px}ul{margin:0;padding:0;list-style:none}"
              "li{float:left;width:100px;height:20px}</style><div id='m'><div id='c'><ul><li id='a'></li><li></li><li></li><li></li><li id='e'></li></ul></div>"
              "<div id='s' style='width:30px;height:20px'></div></div>", 1266, 900, &d);
    rect("flex+floats", L, d, "c", 335, 0, 500, 20); rect("flex+floats", L, d, "a", 335, 0, 100, 20); rect("flex+floats", L, d, "e", 735, 0, 100, 20); rect("flex+floats", L, d, "s", 835, 0, 30, 20);
    L = build("<!doctype html><style>body{margin:0}#m{float:left}i{float:left;width:50px;height:10px}.c{clear:left}</style><div id='m'><i id='a'></i><i id='b'></i>"
              "<i id='c' class='c'></i><i id='d'></i></div>", 1266, 900, &d);
    rect("float clear", L, d, "m", 0, 0, 100, 20); rect("float clear", L, d, "b", 50, 0, 50, 10); rect("float clear", L, d, "c", 0, 10, 50, 10); rect("float clear", L, d, "d", 50, 10, 50, 10);
    L = build("<!doctype html><style>body{margin:0}#m{position:absolute;top:0;left:0}i{float:left;width:60px;height:10px;margin-right:5px}</style><div id='m'><i id='a'></i><i></i><i id='c'></i></div>", 1266, 900, &d);
    rect("abs floats", L, d, "m", 0, 0, 195, 10); rect("abs floats", L, d, "c", 130, 0, 60, 10);
    L = build("<!doctype html><style>body{margin:0}#g{display:grid;grid-template-columns:auto 1fr;width:600px}i{float:left;width:30px;height:10px}</style>"
              "<div id='g'><div id='x'><i></i><i></i><i id='c'></i></div><div id='y' style='height:5px'></div></div>", 1266, 900, &d);
    rect("grid auto floats", L, d, "x", 0, 0, 90, 10); rect("grid auto floats", L, d, "y", 90, 0, 510, 5);

    /* SOMA_347_NAVEGADOR_GRID: plantilla de 20minutos: lineas con nombre, repeat(auto-fill, minmax(60px, 1fr)) y una pista fija de 300px.
     * Un elemento colocado solo en la primera columna con min-content de 148px no la ensancha (su minimo son los 60px). Valores = Chrome. */
    L = build("<style>body{margin:0}#g{display:grid;width:990px;grid-template-columns:[main-start content-start] repeat(auto-fill,minmax(60px,1fr)) [content-end sidebar-start] 300px [sidebar-end main-end];column-gap:20px}"
              "#t{grid-column:main-start/main-end;grid-row:1;height:10px}#c{grid-column:content-start/content-end;grid-row:2;height:10px}#s{grid-column:sidebar-start/sidebar-end;grid-row:2/span 2;height:10px}"
              "#w{height:10px}#w i{display:inline-block;width:148px;height:5px}</style><div id=g><div id=t></div><div id=c></div><div id=s></div><div id=w><i></i></div></div>", 1200, 600, &d);
    rect("grid 347 nombres", L, d, "t", 0, 0, 990, 10); rect("grid 347 nombres", L, d, "c", 0, 10, 670, 10);
    rect("grid 347 nombres", L, d, "s", 690, 10, 300, 10); rect("grid 347 minimo fijo", L, d, "w", 0, 20, 67, 10);
    /* SOMA_347_NAVEGADOR_INHERIT: *,*::before,*::after{box-sizing:inherit} con html{box-sizing:border-box} (Bootstrap, WordPress):
     * el relleno va dentro del ancho. Y width:unset es auto (no el ancho del padre). Valores = Chrome. */
    L = build("<style>html{box-sizing:border-box}*,*::before,*::after{box-sizing:inherit}body{margin:0}#a{width:100px;padding:10px 20px;height:30px}"
              "#p{width:300px}#u{float:left;width:unset;height:5px}#f{position:fixed;top:0;left:0;width:100vw;height:100vh;padding:5% 10%}</style>"
              "<div id=a></div><div id=p><div id=u></div></div><div id=f></div>", 1000, 600, &d);
    rect("box-sizing:inherit", L, d, "a", 0, 0, 100, 30); rect("width:unset", L, d, "u", 0, 30, 0, 5);
    rect("fixed 100vw con relleno", L, d, "f", 0, 0, 1000, 600);
    /* SOMA_347_NAVEGADOR_FLEXMIN: base flex acotada por min/max antes de repartir (min-width:30% con poco contenido; max-width con
     * flex-basis mayor). Valores = Chrome. */
    L = build("<!doctype html><style>body{margin:0}#m{display:flex;width:1000px}#l{min-width:30%;height:10px}#l i{display:block;width:66px;height:5px}"
              "#c{flex:1;height:10px}#r{max-width:50px;flex:0 0 200px;height:10px}</style><div id=m><div id=l><i></i></div><div id=c></div><div id=r></div></div>", 1200, 600, &d);
    rect("flex min-width %", L, d, "l", 0, 0, 300, 10); rect("flex min-width %", L, d, "c", 300, 0, 650, 10); rect("flex max-width", L, d, "r", 950, 0, 50, 10);
    /* SOMA_349: imagen "comprimible" (width fijo y max-width:100%) en una celda de tabla automatica: aporta 0 al minimo y la columna
     * de texto se lleva el sitio, como en Chrome (PyPI: 138/802 px; antes 270/670). Sin texto: la columna ancha la fija un bloque. */
    L = build("<!doctype html><style>*{box-sizing:border-box}body{margin:0}#t{display:table;width:1000px;padding:0 30px}#l{display:table-cell;padding-right:50px}"
              "#l img{display:block;width:220px;max-width:100%;height:20px}#x{display:table-cell}#x i{display:inline-block;width:40px;height:10px}</style>"
              "<div id=t><div id=l><img></div><div id=x><i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i> <i></i></div></div>", 1200, 600, &d);
    { struct wnode *nl = wd_by_id(d->root, "l"); int a, b2, w, h; CHECK(nl && wl_node_rect(L, nl, &a, &b2, &w, &h) && w < 200, "imagen comprimible en celda: columna %d px (Chrome < 200; antes 270)", w); }
    /* SOMA_349: min-content con un tramo nowrap al final (icono de enlace externo de PyPI): solo cuenta el tramo desde el ultimo corte,
     * no toda la linea. Chrome: 89 px (antes 335: el parrafo entero sin cortes). */
    L = build("<!doctype html><style>body{margin:0;font:16px Arial}#c{display:inline-block;width:min-content}#c b:after{content:\"zz\";white-space:nowrap;font-weight:400}</style>"
              "<div id=c>aaa bbbbbbbbbb cc dd ee ff gg hh ii jj kk <b id=b>mm</b>.</div>", 1200, 600, &d);
    { struct wnode *nc = wd_by_id(d->root, "c"); int a, b2, w, h; CHECK(nc && wl_node_rect(L, nc, &a, &b2, &w, &h) && w >= 80 && w <= 120, "min-content con nowrap al final: %d px (Chrome 89 con Liberation; aqui DejaVu; antes 335+)", w); }
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    if (fails) return 1;
    printf("SOMA 181: maquetacion OK\n");
    return 0;
}
