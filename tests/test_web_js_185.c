/* SOMA_185: pruebas en el anfitrion de las cadenas UTF-16 del motor JavaScript: length/indices en unidades, pares sustitutos,
 * sustitutos sueltos (WTF-8), metodos de String, expresiones regulares (index/lastIndex), escape/unescape, TextEncoder/TextDecoder
 * y el modo soma.jsutf16=off (semantica por bytes de antes del parche). Se compila con ASan/UBSan. */
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
extern int g_ju16_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *src, *post, *exp; };
static const struct tc CASES[] = {
    { "'héllo'.length", 0, "5" },
    { "'日本語'.length", 0, "3" },
    { "'😀'.length", 0, "2" },
    { "'a😀b'.length", 0, "4" },
    { "String.fromCodePoint(128512).length", 0, "2" },
    { "'😀'.charCodeAt(0)", 0, "55357" },
    { "'😀'.charCodeAt(1)", 0, "56832" },
    { "'😀'.codePointAt(0)", 0, "128512" },
    { "'😀'.codePointAt(1)", 0, "56832" },
    { "'a😀b'.codePointAt(1)", 0, "128512" },
    { "'a😀b'[3]", 0, "b" },
    { "'a😀b'[1].length", 0, "1" },
    { "'a😀b'.charAt(2).charCodeAt(0)", 0, "56832" },
    { "'héllo'.indexOf('l')", 0, "2" },
    { "'日本語日'.lastIndexOf('日')", 0, "3" },
    { "'a😀b'.indexOf('b')", 0, "3" },
    { "'a😀b'.slice(1,3)", 0, "😀" },
    { "'a😀b'.slice(1,2).length", 0, "1" },
    { "'a😀b'.slice(2) + '' === '\\uDE00b'", 0, "true" },
    { "'a😀b'.slice(0,2) + '\\uDE00' === 'a😀'", 0, "true" },
    { "'a😀b'.substring(3)", 0, "b" },
    { "'héllo'.slice(1,3)", 0, "él" },
    { "'héllo'.substr(1,2)", 0, "él" },
    { "'héllo'.at(-1)", 0, "o" },
    { "'héllo'.at(1)", 0, "é" },
    { "'日本語'.split('').join('|')", 0, "日|本|語" },
    { "'😀'.split('').length", 0, "2" },
    { "[...'😀'].length", 0, "1" },
    { "Array.from('a😀b').length", 0, "3" },
    { "String.fromCharCode(0xD83D, 0xDE00) === '😀'", 0, "true" },
    { "String.fromCharCode(0xD83D) + String.fromCharCode(0xDE00) === '😀'", 0, "true" },
    { "var s=''; s += String.fromCharCode(0xD83D); s += String.fromCharCode(0xDE00); s === '😀' && s.length === 2", 0, "true" },
    { "'héllo'.padStart(8,'·').length", 0, "8" },
    { "'héllo'.padStart(8,'·')", 0, "···héllo" },
    { "'ab'.padEnd(5,'日本')", 0, "ab日本日" },
    { "'é'.startsWith('é')", 0, "true" },
    { "'a😀b'.startsWith('b',3)", 0, "true" },
    { "'a😀b'.endsWith('😀',3)", 0, "true" },
    { "'héllo wörld'.replace('wörld', function(m, off){return off})", 0, "héllo 6" },
    { "/é/.exec('aé').index", 0, "1" },
    { "/😀/.exec('a😀b').index", 0, "1" },
    { "'a😀b'.search(/b/)", 0, "3" },
    { "var r=/b/g; r.test('a😀b'); r.lastIndex", 0, "4" },
    { "var r=/\\w/g; var o=[]; var m; while((m=r.exec('é😀a'))) o.push(m.index+':'+r.lastIndex); o.join()", 0, "3:4" },
    { "'a😀b'.replace(/b/, function(m,i){return i})", 0, "a😀3" },
    { "'x😀y'.match(/y/).index", 0, "3" },
    { "Object.keys('a😀').length", 0, "3" },
    { "JSON.stringify('😀')", 0, "\"😀\"" },
    { "JSON.parse('\"\\\\ud83d\\\\ude00\"').length", 0, "2" },
    { "'\\u{1F600}'.length", 0, "2" },
    { "'😀'.split('').map(c=>c.charCodeAt(0)).join()", 0, "55357,56832" },
    { "escape('é')", 0, "%E9" },
    { "escape('日')", 0, "%u65E5" },
    { "escape('😀')", 0, "%uD83D%uDE00" },
    { "unescape('%E9')", 0, "é" },
    { "unescape('%uD83D%uDE00') === '😀'", 0, "true" },
    { "encodeURIComponent('é😀')", 0, "%C3%A9%F0%9F%98%80" },
    { "decodeURIComponent('%F0%9F%98%80').length", 0, "2" },
    { "btoa('é')", 0, "6Q==" },
    { "atob('6Q==') === 'é'", 0, "true" },
    { "'ß'.toUpperCase()", 0, "SS" },
    { "new TextEncoder().encode('é').length", 0, "2" },
    { "new TextEncoder().encode('😀').length", 0, "4" },
    { "new TextDecoder().decode(new Uint8Array([195,169]))", 0, "é" },
    { "new TextDecoder().decode(new TextEncoder().encode('日本語😀'))", 0, "日本語😀" },
    { "'日本語'.charCodeAt(1)", 0, "26412" },
    { "'abc'.charCodeAt(5)", 0, "NaN" },
    { "'ab'.at(5)", 0, "undefined" },
    { "var t=''; for (var i=0;i<'日本語'.length;i++) t+='日本語'[i]+'.'; t", 0, "日.本.語." },
    { "var n=0; for (var k in 'a日') n++; n", 0, "2" },
    { "'😀'.localeCompare('😀')", 0, "0" },
    { "'héllo'.includes('llo', 2)", 0, "true" },
    { "'a😀b'.includes('b', 3)", 0, "true" },
    { "'a😀b'.indexOf('😀')", 0, "1" },
    { "'😀😀'.lastIndexOf('😀')", 0, "2" },
    { "'日'.normalize()", 0, "日" },
};
/* con soma.jsutf16=off las cadenas se cuentan en bytes, como antes del parche 185 */
static const struct tc OFF[] = {
    { "'héllo'.length", 0, "6" },
    { "'日本語'.length", 0, "9" },
    { "'😀'.length", 0, "4" },
    { "'abc'.length", 0, "3" },
    { "'abc'.charAt(1)", 0, "b" },
    { "'héllo'.indexOf('l')", 0, "3" },
    { "'héllo'.slice(0,3)", 0, "hé" },
    { "'abc'.split('').join('|')", 0, "a|b|c" },
    { "'日本'.split('').join('|')", 0, "日|本" },
    { "String.fromCharCode(233)", 0, "é" },
    { "String.fromCodePoint(128512)", 0, "😀" },
    { "/b/.exec('éb').index", 0, "2" },
    { "'héllo'.padStart(7,'·').length", 0, "8" },
    { "escape('abc')", 0, "abc" },
    { "'a😀b'.codePointAt(1)", 0, "128512" },
};

static int run(const struct tc *t, int n, const char *name)
{
    const char *html = "<!doctype html><html><body><div id='p'></div></body></html>";
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
    g_ju16_off = 0; fail += run(CASES, (int)(sizeof CASES / sizeof CASES[0]), "u16");
    g_ju16_off = 1; fail += run(OFF, (int)(sizeof OFF / sizeof OFF[0]), "off");
    if (fail) { printf("test_web_js_185: %d fallos\n", fail); return 1; }
    printf("test_web_js_185: %d casos OK (UTF-16) + %d (modo off)\n", (int)(sizeof CASES / sizeof CASES[0]), (int)(sizeof OFF / sizeof OFF[0]));
    return 0;
}
