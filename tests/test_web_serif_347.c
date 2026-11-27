/* SOMA_347_NAVEGADOR_SERIF: font-family con serifa se dibuja con Liberation Serif (anchos de Times New Roman, como Chrome en
 * Linux). Antes todo salia en sans: los titulares de 20minutos (Source-Serif, serif) eran un 10 % mas anchos y cortaban distinto.
 * Anchos esperados = Chrome (getBoundingClientRect) +-2 px. */
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
extern int g_wf182_on;
static int fails, checks;
static const struct wbox *find(const struct wbox *b, const struct wnode *n)
{
    for (; b; b = b->next) {
        if (b->node == n && !(b->flags & 16)) return b;
        const struct wbox *r = find(b->first, n); if (r) return r;
        for (int i = 0; i < b->nitems; i++) if (b->items[i].box) { r = find(b->items[i].box, n); if (r) return r; }   /* inline-block: en los elementos de linea */
    }
    return 0;
}
static void caso(const char *nombre, const char *estilo, int esperado)
{
    char html[400]; snprintf(html, sizeof html, "<!doctype html><body style='margin:0'><div id='x' style='display:inline-block;%s'>Hundimiento de un buque</div>", estilo);
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_compute(c, 1200, 600);
    struct wlay *L = wl_new(d, c, 0, 0); L->ar.limit = 14u << 20; wl_layout(L, 1200, 600);
    const struct wbox *b = find(L->root, wd_by_id(d->root, "x")); int w = b ? b->w : -1;
    checks++; if (w < esperado - 2 || w > esperado + 2) { fails++; printf("FALLO %s: %d px (Chrome %d)\n", nombre, w, esperado); }
}
int main(void)
{
    g_wf182_on = 1;   /* como web_page.c (soma.webfont activado) */
    caso("Times New Roman", "font:40px \"Times New Roman\",serif", 417);
    caso("fuente web desconocida + serif", "font:40px Source-Serif,serif", 417);
    caso("Georgia negrita", "font:bold 40px Georgia,serif", 448);
    caso("font-family suelta", "font-size:40px;font-family:\"Source-Serif\", serif", 417);
    caso("sans-serif no es serif", "font:40px Arial,sans-serif", 458);
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO test_web_serif_347" : "test_web_serif_347 OK", checks, fails);
    return fails != 0;
}
