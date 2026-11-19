/* SOMA_187A: pruebas en el anfitrion del acceso al documento por nombre (document.f, document.<img name>), expandos del documento y eventos
 * InputEvent. Los eventos de teclado (keypress antes de la accion, code, Enter) y la ocultacion CSS se comprueban en QEMU (ver docs). */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"

void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;
extern int g_ju16_off, g_jbin_off, g_jstream_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *src, *post, *exp; };
static const struct tc CASES[] = {
    { "document.f.tagName", 0, "FORM" },
    { "document.f.id", 0, "tsf" },
    { "document.pic.tagName", 0, "IMG" },
    { "document.emb.tagName", 0, "EMBED" },
    { "typeof document.nope", 0, "undefined" },
    { "document.nope === undefined", 0, "true" },
    { "document.zz = 5; document.zz", 0, "5" },
    { "document.f = 7; document.f", 0, "7" },
    { "typeof document.forms.f", 0, "object" },
    { "document.f === document.forms.f", 0, "true" },
    { "document.f === document.getElementById('tsf')", 0, "true" },
    { "typeof document.f.submit", 0, "function" },
    { "document.body.tagName", 0, "BODY" },
    { "document.title", 0, "t" },
    { "var n=0; for (var k in document) { n++ } n >= 0", 0, "true" },
    { "document.f.q.name", 0, "q" },
    { "var e=new InputEvent('input',{data:'a',inputType:'insertText'}); e.data+e.inputType", 0, "ainsertText" },
    { "document.getElementById('x').name", 0, "nm" },
    { "typeof document.x", 0, "undefined" },
    { "document.f.q.tagName", 0, "INPUT" },
    { "typeof document.f.nope", 0, "undefined" },
    { "document.f.q.value = 'x'; document.f.q.value", 0, "x" },
    { "document.f.q === document.f.elements.q", 0, "true" },
    { "document.f.length", 0, "1" },
};
static int run(const struct tc *t, int n, const char *name)
{
    const char *html = "<!doctype html><html><head><title>t</title></head><body><form name=\"f\" id=\"tsf\"><input name=\"q\"></form><img name=\"pic\"><embed name=\"emb\"><div id=\"x\" name=\"nm\"></div></body></html>";
    struct wjs_host h = {0}; h.log = jlog; h.dirty = dirty; h.url = "https://example.org";
    int fail = 0;
    for (int i = 0; i < n; i++) {
        struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
        struct wjs *j = wjs_new(d, &h); char out[2048] = "";
        int rc = wjs_eval_str(j, t[i].src, out, sizeof out);
        if (t[i].post && rc == 0) {
            for (int k = 0; k < 200; k++) { int ran = 0; g_now += 50; int nx = wjs_pump(j, g_now, &ran); if (nx < 0 && !ran) break; }
            rc = wjs_eval_str(j, t[i].post, out, sizeof out);
        }
        if (rc != 0 || strcmp(out, t[i].exp)) { fail++; printf("FAIL %s #%d: %s\n   esperado [%s] obtenido [%s] rc=%d\n", name, i, t[i].src, t[i].exp, out, rc); }
        wjs_free(j); wd_free(d);
    }
    return fail;
}
int main(void)
{
    int fail = 0;
    g_ju16_off = 0; g_jbin_off = 0; g_jstream_off = 0; fail += run(CASES, (int)(sizeof CASES / sizeof CASES[0]), "nombres");
    if (fail) { printf("test_web_js_187a: %d fallos\n", fail); return 1; }
    printf("test_web_js_187a: %d casos OK (acceso por nombre, expandos, InputEvent)\n", (int)(sizeof CASES / sizeof CASES[0]));
    return 0;
}
