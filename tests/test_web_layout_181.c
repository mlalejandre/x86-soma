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

    printf("%d comprobaciones, %d fallos\n", checks, fails);
    if (fails) return 1;
    printf("SOMA 181: maquetacion OK\n");
    return 0;
}
