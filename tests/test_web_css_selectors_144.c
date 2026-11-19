/* SOMA 144: CSS Level 4 - listas de selectores en pseudoclases. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "web_css.h"
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}

static int match(struct wcss *c, struct wnode *n, const char *s) {
    return wc_matches(c, n, s);
}
int main(void) {
    struct wdoc *d = wd_new();
    assert(d);
    const char *html = "<!doctype html><html><body>"
      "<div id='main' class='card active'>uno</div>"
      "<div id='other' class='card'>dos</div>"
      "<div id='skip' class='card blocked'>tres</div>"
      "<section id='sec' class='active'>cuatro</section>"
      "</body></html>";
    wd_parse(d, html, (uint32_t)strlen(html));
    struct wnode *main = wd_by_id(d->root, "main");
    struct wnode *other = wd_by_id(d->root, "other");
    struct wnode *skip = wd_by_id(d->root, "skip");
    struct wnode *sec = wd_by_id(d->root, "sec");
    assert(main && other && skip && sec);
    struct wcss *css = wc_new(d); assert(css);
    /* Listas de compuestos, combinacion con selector externo y coincidencias. */
    assert(match(css, main, "div:is(.active, #skip)"));
    assert(match(css, skip, "div:is(.active, #skip)"));
    assert(!match(css, other, "div:is(.active, #skip)"));
    assert(!match(css, sec, "div:is(.active, #skip)"));
    assert(match(css, other, "div:where(.card, .active)"));
    assert(match(css, other, "div:not(.blocked, .active)"));
    assert(!match(css, main, "div:not(.blocked, .active)"));
    assert(!match(css, skip, "div:not(.blocked, .active)"));
    assert(match(css, main, "body > :is(div, section).active"));
    assert(match(css, sec, "body > :is(div, section).active"));
    assert(!match(css, other, "body > :is(div, section).active"));
    assert(match(css, main, ":is(.card, .missing):not(.blocked, .missing)"));
    assert(!match(css, skip, ":is(.card, .missing):not(.blocked, .missing)"));
    /* Unsupported complex branches must fail closed, not silently match. */
    assert(!match(css, main, ":is(div > .active, section)"));
    assert(!match(css, main, ":is()"));
    assert(!match(css, main, ":is(.card,)"));
    assert(!match(css, main, ":is(.card, :is(.card, .active))"));
    /* Cascade: :where tiene especificidad cero, :is/:not el mayor brazo. */
    const char *sheet =
      "#main{color:blue} div:where(#main,.card){color:red}"
      "div.card{background-color:blue} div:is(#main,.foo){background-color:lime}"
      "#other{color:red} div:not(#main,.blocked){color:blue}";
    wc_add_sheet(css, sheet, (uint32_t)strlen(sheet), 1);
    wc_compute(css, 1024, 700);
    assert(main->st && other->st);
    assert((main->st->color & 0xffffffu) == 0x0000ffu);  /* #main > :where */
    assert((main->st->bg_color & 0xffffffu) == 0x00ff00u); /* :is(#main,.foo) */
    assert((other->st->color & 0xffffffu) == 0x0000ffu); /* :not(#main,.blocked) > #other */
    wc_free(css);
    wd_free(d);
    puts("SOMA 144: CSS :is/:where/:not listas + especificidad OK");
    return 0;
}
