/* SOMA_200: fuzzing del motor del navegador en el anfitrion (ASan + UBSan): HTML -> CSS -> maquetacion -> pintado -> consulta.
 * El motor corre en ring 0: aqui se le da HTML y CSS generados al azar y mutados (anidamientos absurdos, valores enormes,
 * atributos rotos, bytes sueltos) y cualquier lectura o escritura fuera de sitio detiene la prueba con su traza.
 * Uso: fuzz_web_200 [iteraciones] [semilla]   (con un archivo como tercer argumento, reproduce ese caso) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include "web_dom.h"
#include "web_css.h"
#include "web_layout.h"
#include "web_ua.h"
#include "web_util.h"

uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
static size_t g_alloc, g_alloc_max = 96u << 20;
void *kmalloc(size_t n) { if (n > (64u << 20) || g_alloc + n > g_alloc_max) return 0; g_alloc += n; return calloc(1, n ? n : 1); }   /* tambien se prueba el "sin memoria" */
void kfree(void *p) { free(p); }

static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
static char buf[1 << 16]; static uint32_t bl;
static void add(const char *s) { while (*s && bl < sizeof buf - 2) buf[bl++] = *s++; }
#define PICK(a) a[rn(sizeof a / sizeof a[0])]
static const char *const TAGS[] = { "div", "span", "p", "a", "b", "i", "table", "tr", "td", "th", "tbody", "thead", "caption", "ul", "ol", "li", "img", "input", "button", "select", "option",
    "textarea", "form", "h1", "h3", "pre", "br", "hr", "section", "nav", "label", "fieldset", "legend", "details", "summary", "iframe", "canvas", "svg", "video", "center", "font", "sup", "colgroup", "col", "dl", "dt", "dd", "blockquote", "style", "body", "html", "head", "title" };
static const char *const PROPS[] = { "display", "position", "float", "clear", "width", "height", "min-width", "max-width", "min-height", "max-height", "margin", "margin-left", "padding", "border", "border-width",
    "top", "left", "right", "bottom", "font-size", "line-height", "flex", "flex-grow", "flex-shrink", "flex-basis", "flex-direction", "flex-wrap", "justify-content", "align-items", "gap", "grid-template-columns",
    "grid-template-rows", "grid-column", "grid-row", "overflow", "z-index", "opacity", "transform", "background", "color", "text-align", "vertical-align", "white-space", "box-sizing", "border-radius", "letter-spacing",
    "text-indent", "border-collapse", "border-spacing", "table-layout", "visibility", "column-count", "aspect-ratio", "inset", "order", "word-spacing", "font", "font-weight", "list-style", "content", "box-shadow", "outline" };
static const char *const VALS[] = { "block", "inline", "inline-block", "flex", "inline-flex", "grid", "table", "table-cell", "table-row", "none", "absolute", "relative", "fixed", "sticky", "left", "right", "both",
    "0", "1", "-1", "100%", "50%", "-50%", "auto", "1px", "-1px", "99999999px", "-99999999px", "2147483647px", "4294967296px", "1e30px", "0.0001px", "1em", "1000em", "10vw", "100vh", "calc(100% - 10px)", "calc(1px/0)",
    "calc(((((1px)))))", "min(1px,2px)", "max(", "repeat(3,1fr)", "repeat(99999,1px)", "repeat(auto-fill,minmax(0,1fr))", "1fr 2fr", "span 9999", "1 / -1", "hidden", "scroll", "wrap", "column", "row-reverse",
    "center", "space-between", "nowrap", "pre", "border-box", "collapse", "red", "#fff", "#12345678", "rgb(1,2,3)", "rgba(999,-1,1e9,2)", "url(x)", "inherit", "initial", "1px solid red", "rotate(1e9deg)",
    "scale(0)", "translate(-99999px,50%)", "matrix(1,2,3)", "1 1 0", "999999", "-999999", "\"x\"", "attr(x)", "var(--a)", "var(--a,var(--b,1px))", "!important", "1/0", "0/0", "bold 1px/0 x" };
static const char *const ATTRS[] = { "id", "class", "style", "width", "height", "colspan", "rowspan", "span", "size", "rows", "cols", "href", "src", "type", "value", "align", "border", "cellpadding", "cellspacing", "hidden", "start", "multiple", "open", "dir" };
static const char *const AVALS[] = { "a", "b c", "0", "-1", "99999", "4294967295", "1e9", "100%", "", "x\"y", "checkbox", "radio", "text", "hidden", "file", "range", "rtl", "data:image/png;base64,AAAA", "#", "javascript:1" };
static void gen_style(int n) { for (int i = 0; i < n; i++) { add(PICK(PROPS)); add(":"); add(PICK(VALS)); if (!rn(6)) { add(" "); add(PICK(VALS)); } add(";"); } }
static void gen_text(void) { static const char *const T[] = { "hola ", "mundo", "&amp;", "&#x110000;", "&#0;", "&nbsp;&nbsp;", "\xc3\xb1", "\xf0\x9f\x98\x80", "\xff\xfe", "x\ty\n", "<", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "\xe2\x80\x8b", " " }; add(PICK(T)); }
static void gen_node(int depth)
{
    if (bl > sizeof buf - 4096) return;
    int kind = (int)rn(12);
    if (kind < 3 || depth > 40) { gen_text(); return; }
    if (kind == 3) { add("<!--"); gen_text(); if (rn(4)) add("-->"); return; }
    const char *t = PICK(TAGS);
    add("<"); add(t);
    for (int a = (int)rn(4); a > 0; a--) {
        const char *an = PICK(ATTRS); add(" "); add(an);
        if (rn(8)) { add(rn(6) ? "=\"" : "="); if (!strcmp(an, "style")) gen_style(1 + (int)rn(5)); else add(PICK(AVALS)); if (rn(10)) add("\""); }
    }
    add(rn(30) ? ">" : "/>");
    if (!strcmp(t, "style")) { for (int r = (int)rn(4); r >= 0; r--) { static const char *const S[] = { "*", "div", ".a", "#a", "div>p", "a:hover", "li:nth-child(2n+1)", ":not(.b)", "td+td", "[id^=a]", "@media (max-width:1px){div", "div::before", ":root", "p~p", ":is(a,b) c", /* SOMA_204 */ "div:has(p)", "li:has(> a:hover)", ":is(a:hover,.b)", "p:not(:hover)", "div:has(+ p) a:hover", "@supports (x:y){*", "@import url(x);a" }; add(PICK(S)); add("{"); gen_style(1 + (int)rn(6)); if (rn(8)) add("}"); } }
    else for (int c = (int)rn(depth < 4 ? 6 : 3); c > 0; c--) gen_node(depth + 1);
    if (rn(9)) { add("</"); add(rn(12) ? t : PICK(TAGS)); add(">"); }
}
static void mutate(void)
{
    for (int m = (int)rn(4); m > 0 && bl > 4; m--) {
        uint32_t p = rn(bl), k = rn(5);
        if (k == 0) buf[p] = (char)rnd();
        else if (k == 1) { uint32_t n = rn(64) + 1; if (p + n < bl) { memmove(buf + p, buf + p + n, bl - p - n); bl -= n; } }
        else if (k == 2) { uint32_t n = rn(200) + 1, q = rn(bl); if (bl + n < sizeof buf - 2 && q + n < bl) { memmove(buf + p + n, buf + p, bl - p); memcpy(buf + p, buf + q + (q >= p ? n : 0) < buf + bl - n ? buf + q : buf, n); bl += n; } }
        else if (k == 3) buf[p] = "<>\"'&;:{}()/=\0\x80"[rn(15)];
        else bl = p + 1;
    }
}
static void collect_styles(struct wcss *c, struct wnode *n, int depth)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) { struct wb b = {0}; wd_text_content(n, &b); if (b.p) { wc_add_sheet(c, b.p, b.len, 1); kfree(b.p); } }
        if (n->first && depth < 400) collect_styles(c, n->first, depth + 1);
    }
}
static void run_case(const char *html, uint32_t len)
{
    static const int VW[] = { 800, 1, 16, 320, 1920, 30000 };
    int vw = VW[rn(6)], vh = VW[rn(6)];
    g_alloc = 0;
    struct wdoc *d = wd_new(); if (!d) return;
    wd_parse(d, html, len);
    struct wcss *c = wc_new(d);
    if (c) {
        wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0);
        collect_styles(c, d->root, 0);
        wc_compute(c, vw, vh);
        struct wlay *L = wl_new(d, c, 0, 0);
        if (L) {
            L->ar.limit = 14u << 20;
            wl_layout(L, vw, vh);
            wl_paint(L, 0, 0, vw < 2000 ? vw : 2000, vh < 2000 ? vh : 2000, 0, (int)rn(3000));
            for (int i = 0; i < 6; i++) { struct wnode *lk = 0; struct wbox *bx = 0; (void)wl_hit(L, (int)rn(2000) - 100, (int)rn(4000) - 100, &lk, &bx); (void)wl_inner_scroll(L, (int)rn(800), (int)rn(800), (int)rn(400) - 200); }
            { struct wnode *hv[4] = { 0, 0, 0, 0 };   /* SOMA_204: el raton cambia de nodo (lo que hace wp_mouse_move): aqui caia el motor con :has() */
              for (int i = 1; i < 4; i++) hv[i] = wl_hit(L, (int)rn(vw > 0 ? (uint32_t)vw : 1), (int)rn(1200), 0, 0);
              for (int i = 0; i < 8; i++) (void)wc_hover_affects(c, hv[rn(4)], hv[rn(4)]); }
            if (rn(2)) { wc_compute(c, vh, vw); wl_layout(L, vh, vw); }        /* segunda pasada con otro tamano (redimensionar) */
            wl_free(L);
        }
        wc_free(c);
    }
    wd_free(d);
}
static uint32_t g_iter;
static void on_alarm(int s) { (void)s; fprintf(stderr, "\nCUELGUE: el caso %u lleva mas de 20 s (semilla del caso guardada en /tmp/fuzz_web_cuelgue.html)\n", g_iter); FILE *f = fopen("/tmp/fuzz_web_cuelgue.html", "wb"); if (f) { fwrite(buf, 1, bl, f); fclose(f); } _exit(3); }
int main(int argc, char **argv)
{
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 3000, seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    signal(SIGALRM, on_alarm);
    if (argc > 3) { FILE *f = fopen(argv[3], "rb"); if (!f) return 2; bl = (uint32_t)fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[bl] = 0; rs = seed ? seed : 1; alarm(60); run_case(buf, bl); printf("caso reproducido sin fallo\n"); return 0; }
    for (g_iter = getenv("FUZZ_START") ? (uint32_t)atoi(getenv("FUZZ_START")) : 0; g_iter < iters; g_iter++) {
        rs = (seed * 2654435761u) ^ (g_iter * 40503u + 1u); if (!rs) rs = 1;
        bl = 0; if (rn(3)) add("<!DOCTYPE html>");
        for (int k = 1 + (int)rn(5); k > 0; k--) gen_node(0);
        if (rn(3) == 0) mutate();
        buf[bl] = 0;
        FILE *f = fopen("/tmp/fuzz_web_ultimo.html", "wb"); if (f) { fwrite(buf, 1, bl, f); fclose(f); }
        alarm(getenv("FUZZ_ALARM") ? (unsigned)atoi(getenv("FUZZ_ALARM")) : 20);
        run_case(buf, bl);
    }
    alarm(0);
    printf("fuzz_web_200: %u casos sin fallos de memoria ni cuelgues (semilla %u)\n", iters, seed);
    return 0;
}
