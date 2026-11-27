/* SOMA_351_ESTILOS_RAPIDOS: el filtro de antepasados y los cubos de 1024 no cambian ningun estilo.
 * Se calcula cada documento dos veces (con g_wc351_off = 1, como antes, y con el filtro) y se compara, elemento a elemento,
 * el color, el tamano de letra, el display y el margen izquierdo. Casos escritos a mano (combinadores, raices sombra, clases
 * repetidas, arboles mas hondos que la pila del filtro) y 300 documentos y hojas al azar. */
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
struct huella { uint32_t color; int font_px, display, ml; };
static int recoger(struct wnode *root, struct huella *o, int max)
{
    int k = 0;
    for (struct wnode *n = root->first; n; n = wd_walk_next(n, root)) {
        if (n->type != WN_ELEM || !n->st || k >= max) continue;
        o[k].color = n->st->color; o[k].font_px = n->st->font_px; o[k].display = n->st->display; o[k].ml = n->st->margin[3].px; k++;
    }
    return k;
}
static struct huella ha[40000], hb[40000];
static void comparar(const char *nombre, const char *html, const char *css)
{
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_add_sheet(c, css, (uint32_t)strlen(css), 1);
    g_wc351_off = 1; wc_compute(c, 1000, 600); int na = recoger(d->root, ha, 40000);
    g_wc351_off = 0; wc_compute(c, 1000, 600); int nb = recoger(d->root, hb, 40000);
    checks++;
    if (na != nb) { fails++; printf("FALLO %s: %d elementos con estilo frente a %d\n", nombre, na, nb); return; }
    for (int i = 0; i < na; i++)
        if (memcmp(&ha[i], &hb[i], sizeof ha[i])) { fails++; printf("FALLO %s: elemento %d: color %08x/%08x letra %d/%d display %d/%d margen %d/%d\n", nombre, i, ha[i].color, hb[i].color, ha[i].font_px, hb[i].font_px, ha[i].display, hb[i].display, ha[i].ml, hb[i].ml); return; }
}
/* tambien que el filtro acierte: el estilo esperado a mano */
static void esperar(const char *nombre, const char *html, const char *css, const char *id, uint32_t color)
{
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_add_sheet(c, css, (uint32_t)strlen(css), 1);
    g_wc351_off = 0; wc_compute(c, 1000, 600);
    struct wnode *n = wd_by_id(d->root, id);
    checks++;
    if (!n || !n->st || (n->st->color & 0xFFFFFFu) != color) { fails++; printf("FALLO %s: color %06x (esperado %06x)\n", nombre, n && n->st ? n->st->color & 0xFFFFFFu : 0, color); }
}
static uint32_t rs = 12345;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 16) % n; }
static const char *CL[] = { "a", "b", "c", "d", "e", "nav", "item", "x1", "x2", "card" };
static const char *TG[] = { "div", "span", "p", "ul", "li", "section", "a", "em" };
static char *ap(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *nodo(char *o, int prof, int *cuenta)
{
    const char *t = TG[rnd(8)];
    o = ap(o, "<"); o = ap(o, t);
    if (rnd(3)) { o = ap(o, " class=\""); int k = 1 + (int)rnd(3); for (int i = 0; i < k; i++) { if (i) o = ap(o, " "); o = ap(o, CL[rnd(10)]); } o = ap(o, "\""); }
    if (!rnd(6)) { char b[24]; snprintf(b, sizeof b, " id=\"i%u\"", rnd(5)); o = ap(o, b); }
    o = ap(o, ">t");
    int hijos = prof < 7 ? (int)rnd(4) : 0;
    for (int i = 0; i < hijos && *cuenta < 600; i++) { (*cuenta)++; o = nodo(o, prof + 1, cuenta); }
    o = ap(o, "</"); o = ap(o, t); o = ap(o, ">");
    return o;
}
static char *compuesto(char *o)
{
    int r = (int)rnd(10);
    if (r < 3) o = ap(o, TG[rnd(8)]);
    if (r >= 2 || r == 0) { o = ap(o, "."); o = ap(o, CL[rnd(10)]); }
    if (r == 9) { char b[16]; snprintf(b, sizeof b, "#i%u", rnd(5)); o = ap(o, b); }
    if (!rnd(7)) o = ap(o, ":first-child");
    if (!rnd(9)) { o = ap(o, ":not(."); o = ap(o, CL[rnd(10)]); o = ap(o, ")"); }
    return o;
}
static void azar(int vez)
{
    static char html[200000], css[60000];
    char *o = ap(html, "<!doctype html><body>"); int cuenta = 0;
    while (cuenta < 400) { cuenta++; o = nodo(o, 0, &cuenta); }
    ap(o, "</body>");
    o = css;
    for (int r = 0; r < 120; r++) {
        int partes = 1 + (int)rnd(4);
        for (int i = 0; i < partes; i++) {
            if (i) { static const char *CB[] = { " ", " > ", " + ", " ~ ", " " }; o = ap(o, CB[rnd(5)]); }
            o = compuesto(o);
        }
        char b[80]; snprintf(b, sizeof b, "{color:#%06x;font-size:%upx;margin-left:%upx}\n", rnd(0xFFFFFF), 8 + rnd(30), rnd(50)); o = ap(o, b);
    }
    char nm[32]; snprintf(nm, sizeof nm, "azar %d", vez);
    comparar(nm, html, css);
}
int main(void)
{
    const char *css1 = ".a .b{color:#ff0000} .a > .c{color:#00ff00} .x + .y .z{color:#0000ff} #k .w{color:#123456} ul li.q{color:#654321} .m ~ .n{color:#abcdef}";
    const char *h1 = "<div class='a'><p><span class='b' id='t1'>1</span></p><span class='c' id='t2'>2</span><p><span class='c' id='t3'>3</span></p></div>"
                     "<span class='b' id='t4'>4</span><div class='x'></div><div class='y'><i class='z' id='t5'>5</i></div>"
                     "<section id='k'><b><i class='w' id='t6'>6</i></b></section><ul><li class='q' id='t7'>7</li></ul><li class='q' id='t8'>8</li>"
                     "<i class='m'></i><i class='n' id='t9'>9</i><div class='b b a'><span class='b' id='t10'>10</span></div>";
    esperar("descendiente", h1, css1, "t1", 0xff0000);
    esperar("hijo directo", h1, css1, "t2", 0x00ff00);
    esperar("hijo no directo", h1, css1, "t3", 0x000000);
    esperar("sin antepasado", h1, css1, "t4", 0x000000);
    esperar("hermano y descendiente", h1, css1, "t5", 0x0000ff);
    esperar("id antepasado", h1, css1, "t6", 0x123456);
    esperar("etiqueta antepasada", h1, css1, "t7", 0x654321);
    esperar("etiqueta antepasada ausente", h1, css1, "t8", 0x000000);
    esperar("hermano general", h1, css1, "t9", 0xabcdef);
    esperar("clases repetidas", h1, css1, "t10", 0xff0000);
    comparar("a mano", h1, css1);
    /* mas hondo que la pila del filtro (2048): se calcula sin filtro desde ahi, con el mismo resultado */
    {
        static char h[80000]; char *o = ap(h, "<div class='a'>");
        for (int i = 0; i < 2300; i++) o = ap(o, "<div>");
        o = ap(o, "<span class='b' id='hondo'>x</span>");
        for (int i = 0; i < 2300; i++) o = ap(o, "</div>");
        ap(o, "</div>");
        esperar("arbol muy hondo", h, css1, "hondo", 0xff0000);
        comparar("arbol muy hondo", h, css1);
    }
    /* muchas clases en el mismo cubo de la tabla: todas siguen encontrando sus reglas */
    {
        static char css[200000], h[4000]; char *o = css;
        for (int i = 0; i < 3000; i++) { char b[64]; snprintf(b, sizeof b, ".k%d{margin-left:%dpx}\n", i, i % 97); o = ap(o, b); }
        ap(o, ".k2999{color:#0a0b0c}");
        o = ap(h, "<p class='k17 k2999' id='cub'>x</p>");
        for (int i = 0; i < 60; i++) { char b[48]; snprintf(b, sizeof b, "<i class='k%d k%d'>y</i>", i * 37 % 3000, i * 53 % 3000); o = ap(o, b); }
        esperar("cubos compartidos", h, css, "cub", 0x0a0b0c);
        comparar("cubos compartidos", h, css);
    }
    /* raiz sombra: '>' desde un hijo de la sombra llega al anfitrion */
    {
        struct wdoc *d = wd_new(); const char *h = "<div class='host' id='hs'></div><div class='a'><div class='host' id='hs2'></div></div>";
        wd_parse(d, h, (uint32_t)strlen(h));
        const char *css = ".host > .s{color:#112233} .a .s{color:#445566}";
        struct wnode *hs = wd_by_id(d->root, "hs"), *hs2 = wd_by_id(d->root, "hs2");
        struct wnode *t1 = 0, *t2 = 0;
        if (hs && hs2) {
            struct wnode *hh[2] = { hs, hs2 }, *tt[2] = { 0, 0 };
            for (int i = 0; i < 2; i++) {   /* como attachShadow (web_js_dom.c): "#shadow-root" con parent = anfitrion, fuera de sus hijos */
                struct wnode *sr = wd_new_elem(d, "#shadow-root", 12); if (!sr) continue;
                sr->parent = hh[i]; hh[i]->shadow = sr; sr->flags |= hh[i]->flags & WNF_INDOC;
                struct wnode *b = wd_new_elem(d, "b", 1); if (!b) continue;
                wd_set_attr(d, b, "class", "s"); wd_append(sr, b); tt[i] = b;
            }
            t1 = tt[0]; t2 = tt[1];
        }
        struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_add_sheet(c, css, (uint32_t)strlen(css), 1);
        g_wc351_off = 1; wc_compute(c, 1000, 600); uint32_t a1 = t1 && t1->st ? t1->st->color : 1, a2 = t2 && t2->st ? t2->st->color : 1;
        g_wc351_off = 0; wc_compute(c, 1000, 600); uint32_t b1 = t1 && t1->st ? t1->st->color : 2, b2 = t2 && t2->st ? t2->st->color : 2;
        checks++;
        if (!t1 || !t2 || a1 != b1 || a2 != b2 || (b1 & 0xFFFFFFu) != 0x112233u || (b2 & 0xFFFFFFu) != 0x445566u) { fails++; printf("FALLO sombra: %08x/%08x %08x/%08x\n", a1, b1, a2, b2); }
    }
    for (int i = 0; i < 300; i++) azar(i);
    printf("test_web_estilos_351: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
