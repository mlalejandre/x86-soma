/* SOMA 190: la pagina "trafico inusual" de Google se convierte en una busqueda de DuckDuckGo HTML (wu_google_sorry_alt) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define WEB_HOST_TEST 1
#include "web_util.h"
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
static int bad;
static void chk(const char *u, int st, const char *want)
{
    char o[1200]; o[0] = 0;
    int r = wu_google_sorry_alt(u, st, o, sizeof o);
    int ok = want ? (r == 1 && strcmp(o, want) == 0) : r == 0;
    if (!ok) { printf("FALLO: %s (%d) -> %d '%s'\n", u, st, r, o); bad++; }
}
int main(void)
{
    chk("https://www.google.com/sorry/index?continue=https://www.google.com/search%3Fq%3Dpajaros%26sca_esv%3D92c529a49cacda%26sxsrf%3DAKL5&q=EgRcuj", 429, "https://html.duckduckgo.com/html/?q=pajaros");
    chk("https://www.google.com/search?q=pajaros+rojos&hl=es", 429, "https://html.duckduckgo.com/html/?q=pajaros+rojos");
    chk("https://www.google.com/search?q=pajaros&hl=es", 200, 0);
    chk("https://www.example.com/sorry/index?continue=x%26q%3Da", 429, 0);
    chk("https://www.google.es/sorry/index?continue=https%3A%2F%2Fwww.google.es%2Fsearch%3Fq%3Dhola%2520mundo%26hl%3Des", 429, "https://html.duckduckgo.com/html/?q=hola%20mundo");
    chk("https://www.google.com/sorry/index?continue=https%3A%2F%2Fwww.google.com%2Fsearch%3Fhl%3Des", 429, 0);
    chk("https://www.google.com/sorry/index", 429, 0);
    chk("https://www.google.com/", 429, 0);
    printf(bad ? "sorry 190: %d fallos\n" : "sorry 190: 8 casos OK\n", bad);
    return bad != 0;
}
