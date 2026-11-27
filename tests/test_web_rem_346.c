/* SOMA_346_NAVEGADOR_RENDER: rem se mide con la letra calculada de <html> (antes, 16 px fijos). 20minutos.es pone html{font-size:10px}
 * y mide todo en rem: en SOMA la portada salia un 60 % mas grande que en Chrome. Valores esperados = Chrome. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_dom.h"
#include "web_css.h"
#include "web_ua.h"
#include "web_util.h"
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static int fails, checks;
static void caso(const char *nombre, const char *css, const char *id, int esperado)
{
    const char *html = "<html><body><div id='d'><h2 id='h' class='t'>a</h2><p id='p'>b</p></div></body></html>";
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_add_sheet(c, css, (uint32_t)strlen(css), 1);
    wc_compute(c, 1266, 900);
    struct wnode *n = wd_by_id(d->root, id); int got = n && n->st ? n->st->font_px : -1;
    checks++; if (got != esperado) { fails++; printf("FALLO %s: %s = %d px (esperado %d)\n", nombre, id, got, esperado); }
    wc_free(c); wd_free(d);
}
int main(void)
{
    caso("html 10px", "html{font-size:10px} .t{font-size:2rem}", "h", 20);
    caso("html 62.5%", "html{font-size:62.5%} .t{font-size:2.4rem}", "h", 24);
    caso("sin html", ".t{font-size:2rem}", "h", 32);
    caso("html en rem (inicial)", "html{font-size:2rem} .t{font-size:1rem}", "h", 32);
    caso("rem en un hijo profundo", "html{font-size:12px} #d{font-size:30px} #p{font-size:1.5rem}", "p", 18);
    caso("em sigue al padre", "html{font-size:12px} #d{font-size:30px} #p{font-size:.5em}", "p", 15);
    caso("calc con rem", "html{font-size:10px} #p{font-size:calc(1rem + 4px)}", "p", 14);
    caso("media en rem: valor inicial", "html{font-size:10px} @media (min-width:100rem){#p{font-size:40px}} #p{font-size:11px}", "p", 11);
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO test_web_rem_346" : "test_web_rem_346 OK", checks, fails);
    return fails != 0;
}
