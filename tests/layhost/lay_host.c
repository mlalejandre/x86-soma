/* SOMA 181: banco de pruebas de maquetacion en el anfitrion. Maqueta un HTML (con sus <style>) y vuelca la caja de
 * cada elemento: "tag#id.clase x y w h". Se compara con Chrome (getBoundingClientRect) con lay_cmp.py. */
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

static void collect_styles(struct wcss *c, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) {
            struct wb b = {0}; wd_text_content(n, &b);
            if (b.p) wc_add_sheet(c, b.p, b.len, 1);
        }
        if (n->first) collect_styles(c, n->first);
    }
}
static void dump(struct wlay *L, struct wnode *n, int depth)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM) {
            int x, y, w, h;
            const char *id = wd_attr(n, "id"), *cl = wd_attr(n, "class");
            if (wl_node_rect(L, n, &x, &y, &w, &h))
                printf("%*s%s%s%s%s%s %d %d %d %d\n", depth * 2, "", n->name, id ? "#" : "", id ? id : "", cl ? "." : "", cl ? cl : "", x, y, w, h);
        }
        if (n->first) dump(L, n->first, depth + 1);
    }
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    g_wf182_on = getenv("SOMA_DEJAVU") ? 0 : 1;   /* SOMA_182: por defecto Liberation Sans, como el navegador */
    int vw = argc > 2 ? atoi(argv[2]) : 1280, vh = argc > 3 ? atoi(argv[3]) : 720;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1); fread(buf, 1, n, f); fclose(f); buf[n] = 0;
    struct wdoc *d = wd_new(); wd_parse(d, buf, (uint32_t)n);
    struct wcss *c = wc_new(d);
    wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0);
    collect_styles(c, d->root);
    wc_compute(c, vw, vh);
    struct wlay *L = wl_new(d, c, 0, 0);
    L->ar.limit = 14u << 20;
    wl_layout(L, vw, vh);
    if (argc > 4) {   /* lay_host pagina vw vh salida.ppm [scroll_y]: pinta */
        g_fbw = vw; g_fbh = vh; g_fb = calloc((size_t)vw * vh, 4); for (int i = 0; i < vw * vh; i++) g_fb[i] = 0xFFFFFFFFu;
        if (argc > 6) { int wx = 0, wy = 0, wd = 0; sscanf(argv[6], "%d,%d,%d", &wx, &wy, &wd); int r = wl_inner_scroll(L, wx, wy, wd); fprintf(stderr, "inner_scroll=%d gen=%u\n", r, wl_inner_gen(L)); }
        wl_paint(L, 0, 0, vw, vh, 0, argc > 5 ? atoi(argv[5]) : 0);
        FILE *o = fopen(argv[4], "wb"); fprintf(o, "P6\n%d %d\n255\n", vw, vh);
        for (int i = 0; i < vw * vh; i++) { fputc((g_fb[i] >> 16) & 255, o); fputc((g_fb[i] >> 8) & 255, o); fputc(g_fb[i] & 255, o); }
        fclose(o); return 0;
    }
    printf("DOC %d %d\n", L->doc_w, L->doc_h);
    dump(L, d->root, 0);
    return 0;
}
