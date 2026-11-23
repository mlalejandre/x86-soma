/* SOMA_275_HTML: pruebas en el anfitrion de src/apps/htmlproj.c (comprobar, empaquetar, plantillas, rutas) y de las plantillas EJECUTADAS en el
 * motor del navegador (clics, formulario, temporizadores). Se compila con ASan/UBSan. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "htmlproj.h"
#include "web_js.h"
#include "web_dom.h"

void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;
static void jlog(void *ud, int level, const char *m) { (void)ud; (void)level; fprintf(stderr, "[js] %s\n", m); }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

static int fails, total;
static void ck(const char *n, int c) { total++; if (!c) { fails++; printf("FALLO %s\n", n); } }

/* sistema de archivos de mentira */
static struct { const char *path; const char *data; uint32_t len; } F[16]; static int nf;
static void addf(const char *p, const char *d, uint32_t l) { F[nf].path = p; F[nf].data = d; F[nf].len = l ? l : (uint32_t)strlen(d); nf++; }
static int e_exists(void *u, const char *p) { (void)u; for (int i = 0; i < nf; i++) if (!strcmp(F[i].path, p)) return 1; return 0; }
static int e_read(void *u, const char *p, const char **d, uint32_t *l) { (void)u; for (int i = 0; i < nf; i++) if (!strcmp(F[i].path, p)) { *d = F[i].data; *l = F[i].len; return 0; } return -1; }
static void e_rel(void *u, const char *d) { (void)u; (void)d; }
static int e_js(void *u, const char *c, uint32_t n, int *line, char *m, uint32_t cap)
{ (void)u; char *t = malloc(n + 1); memcpy(t, c, n); t[n] = 0; int bad = strstr(t, "SINTAXIS") != 0; if (bad) { *line = 2; snprintf(m, cap, "token inesperado"); } free(t); return bad ? 1 : 0; }
static const struct html_env ENV = { 0, e_exists, e_read, e_rel, e_js };

static char out[16384];
static int chk(const char *src, const char *dir) { struct html_report r; return html_check(src, (uint32_t)strlen(src), "t.html", dir, &ENV, out, sizeof out, &r); }
static int has(const char *s) { return strstr(out, s) != 0; }

#define OK_DOC "<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n<meta charset=\"utf-8\">\n<title>T</title>\n</head>\n<body>\n"

static void test_check(void)
{
    nf = 0; addf("/w/p/estilo.css", "a{}", 0); addf("/w/p/app.js", "var a=1;", 0);
    ck("doc valido", chk(OK_DOC "<p>hola</p>\n</body>\n</html>\n", "/w/p") == 0 && out[0] == 0);
    ck("li/p sin cerrar son validos", chk(OK_DOC "<ul><li>a<li>b</ul><p>uno<p>dos</body></html>", "/w/p") == 0);
    ck("void y autocierre", chk(OK_DOC "<br><hr><img src=\"data:x\" alt=\"x\"><input type=text><svg><path d=\"M0\"/></svg></body></html>", "/w/p") == 0);
    ck("div sin cerrar", chk(OK_DOC "<div>\n<p>x</p>\n</body></html>", "/w/p") == 1 && has("t.html:8: error: <div> sin cerrar"));
    ck("cierre sin apertura", chk(OK_DOC "<p>x</p>\n</span>\n</body></html>", "/w/p") == 1 && has("t.html:9: error: </span> sin etiqueta de apertura"));
    ck("mal anidado", chk(OK_DOC "<b><i>x</b></i></body></html>", "/w/p") >= 1);
    ck("id repetido", chk(OK_DOC "<p id=a></p><p id=\"a\"></p></body></html>", "/w/p") == 1 && has("id repetido: a"));
    ck("archivo que no existe", chk(OK_DOC "<link rel=stylesheet href=\"nada.css\"><script src=\"x.js\"></script></body></html>", "/w/p") == 2 && has("no existe el archivo nada.css"));
    ck("archivo que existe y remotos", chk(OK_DOC "<link rel=stylesheet href=\"estilo.css\"><script src=\"./app.js\"></script><script src=\"https://x.org/a.js\"></script><a href=\"#a\">x</a><a href=\"otra.html\">y</a></body></html>", "/w/p") == 0);
    ck("ruta con ..", chk(OK_DOC "<script src=\"../p/app.js\"></script></body></html>", "/w/p") == 0);
    ck("script sin cerrar", chk(OK_DOC "<script>var a=1;\n</body></html>", "/w/p") == 1 && has("<script> sin cerrar"));
    ck("comentario sin cerrar", chk(OK_DOC "<!-- hola <p>x</p></body></html>", "/w/p") == 1 && has("comentario"));
    ck("etiqueta sin cerrar", chk(OK_DOC "<p class=\"a>x</p>", "/w/p") == 1 && has("sin cerrar (falta >"));
    ck("comentario y < en texto", chk(OK_DOC "<!-- <div> -->\n<p>1 < 2 y 3 > 2</p></body></html>", "/w/p") == 0);
    ck("<script> con </ dentro de cadena", chk(OK_DOC "<script>var s = '<div>';</script></body></html>", "/w/p") == 0);
    ck("js en linea con error: linea absoluta", chk(OK_DOC "<p>x</p>\n<script>\nSINTAXIS\n</script></body></html>", "/w/p") == 1 && has("t.html:") && has("error en el script: token inesperado"));
    ck("js: linea dentro del script", has("t.html:11:") || has("t.html:10:"));
    ck("avisos: sin doctype, title, lang", chk("<html><body><p>x</p></body></html>", "/w/p") == 0 && has("aviso: falta <!DOCTYPE") && has("falta <title>") && has("sin lang"));
    ck("aviso: img sin alt", chk(OK_DOC "<img src=\"data:x\"></body></html>", "/w/p") == 0 && has("<img> sin alt"));
    ck("aviso: canvas", chk(OK_DOC "<canvas id=c></canvas><script>var x=c.getContext('2d');</script></body></html>", "/w/p") == 0 && has("getContext()") && has("<canvas>"));
    ck("mayusculas", chk(OK_DOC "<DIV><P>x</P></DIV></body></html>", "/w/p") == 0);
    ck("sin env", html_check("<div>x", 6, "a.html", "/", 0, out, sizeof out, 0) == 1);
    char tiny[40]; html_check(OK_DOC "<div>", (uint32_t)strlen(OK_DOC "<div>"), "t.html", "/", &ENV, tiny, sizeof tiny, 0); ck("salida corta no desborda", strlen(tiny) < sizeof tiny);
}

static void test_paths(void)
{
    char o[64];
    ck("join relativa", html_path_join("/w/p", "a.css", o, sizeof o) == 0 && !strcmp(o, "/w/p/a.css"));
    ck("join ./ ..", html_path_join("/w/p/", "./x/../b/c.js?v=1#z", o, sizeof o) == 0 && !strcmp(o, "/w/p/b/c.js"));
    ck("join .. sube", html_path_join("/w/p", "../q.png", o, sizeof o) == 0 && !strcmp(o, "/w/q.png"));
    ck("join absoluta", html_path_join("/w/p", "/etc/x", o, sizeof o) == 0 && !strcmp(o, "/etc/x"));
    ck("join .. no sale de /", html_path_join("/", "../../a", o, sizeof o) == 0 && !strcmp(o, "/a"));
    ck("join pequeno", html_path_join("/w/p", "muuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuy-largo.js", o, 16) != 0);
}

static char big[200000];
static void test_bundle(void)
{
    nf = 0; const uint8_t png[] = { 0x89, 'P', 'N', 'G', 1 };
    addf("/w/p/estilo.css", "body{color:red}\n/* </style> */", 0); addf("/w/p/app.js", "var s='</script>'; var k=1;", 0); addf("/w/p/img/a.png", (const char *)png, 5);
    const char *src = OK_DOC "<link rel=\"stylesheet\" href=\"estilo.css\">\n<style>p{}</style>\n<img src=\"img/a.png\" alt=\"a\">\n<img src=\"https://x/b.png\" alt=\"b\">\n<link rel=\"icon\" href=\"f.ico\">\n<script type=\"module\" src=\"app.js\"></script>\n<script src=\"falta.js\"></script></body></html>";
    int miss = -1; int n = html_bundle(src, (uint32_t)strlen(src), "/w/p", &ENV, big, sizeof big, &miss);
    ck("bundle longitud", n > 0 && (uint32_t)n == strlen(big));
    ck("css en linea", strstr(big, "<style>/* estilo.css */\nbody{color:red}") != 0 && strstr(big, "<\\/style>") != 0 && !strstr(big, "estilo.css\">"));
    ck("style en linea intacto", strstr(big, "<style>p{}</style>") != 0);
    ck("js en linea, cierre escapado, type conservado", strstr(big, "<script type=\"module\">/* app.js */\nvar s='<\\/script>'; var k=1;\n</script>") != 0);
    ck("imagen a base64", strstr(big, "src=\"data:image/png;base64,iVBORwE=\" alt=\"a\">") != 0);
    ck("remoto y falta intactos", strstr(big, "https://x/b.png") != 0 && strstr(big, "<script src=\"falta.js\"></script>") != 0 && strstr(big, "href=\"f.ico\"") != 0);
    ck("cuenta lo que falta", miss == 1);
    char peq[64]; ck("bundle no cabe", html_bundle(src, (uint32_t)strlen(src), "/w/p", &ENV, peq, sizeof peq, 0) < 0);
    nf = 0; addf("/w/p/estilo.css", "a{}", 0); addf("/w/p/app.js", "1", 0);
    n = html_bundle(OK_DOC "<link rel=stylesheet href=estilo.css><script src=app.js></script></body></html>", (uint32_t)strlen(OK_DOC) + 66, "/w/p", &ENV, big, sizeof big, &miss);
    nf = 0; ck("el empaquetado pasa el comprobador sin archivos", n > 0 && chk(big, "/w/p") == 0 && !has("falta") );
}

/* ---- las plantillas, de verdad en el motor ---- */
static struct wdoc *D; static struct wjs *J; static struct wjs_host H;
static char ev[4096];
static const char *js(const char *s) { ev[0] = 0; wjs_eval_str(J, s, ev, sizeof ev); return ev; }
static void pump(int ms) { for (int k = 0; k < ms / 50; k++) { int ran = 0; g_now += 50; wjs_pump(J, g_now, &ran); } }
static void abre(const char *kind)
{
    static char ih[8192], css[8192], app[8192];
    html_template(kind, 0, "Prueba <&>", ih, sizeof ih); html_template(kind, 1, "x", css, sizeof css); html_template(kind, 2, "x", app, sizeof app);
    nf = 0; addf("/w/p/estilo.css", css, 0); addf("/w/p/app.js", app, 0);
    int e = chk(ih, "/w/p"); ck(kind, e == 0 && !has("aviso") && out[0] == 0);
    if (e || out[0]) printf("  %s\n", out);
    ck("titulo escapado", strstr(ih, "<title>Prueba &lt;&amp;&gt;</title>") != 0);
    char *b2 = malloc(65536); int n = html_bundle(ih, (uint32_t)strlen(ih), "/w/p", &ENV, b2, 65536, 0); ck("plantilla se empaqueta", n > 0 && !strstr(b2, "app.js\"") && strstr(b2, "<script>/* app.js */"));
    free(b2);
    memset(&H, 0, sizeof H); H.log = jlog; H.dirty = dirty; H.url = "file:///w/p/index.html";
    D = wd_new(); wd_parse(D, ih, (uint32_t)strlen(ih)); J = wjs_new(D, &H);
    ck("app.js sin errores", wjs_eval(J, app, (uint32_t)strlen(app), "app.js") == 0);
    wjs_fire_load(J);
}
static void cierra(void) { wjs_free(J); wd_free(D); }

static void test_plantillas(void)
{
    char o[16]; ck("tipo desconocido", html_template("xx", 0, "t", o, sizeof o) < 0); ck("no cabe", html_template("app", 0, "t", o, sizeof o) < 0);
    ck("nombres", !strcmp(html_template_name(0), "index.html") && !strcmp(html_template_name(2), "app.js") && html_template_name(3) == 0);
    abre("basica");
    js("document.getElementById('boton').click(); document.getElementById('boton').click(); document.getElementById('cuenta').textContent");
    ck("basica: contador", !strcmp(ev, "Has pulsado 2 veces"));
    cierra();
    abre("app");
    js("var c = document.getElementById('nueva'); c.value = ' comprar pan '; document.getElementById('formulario').dispatchEvent(new Event('submit', {cancelable:true})); document.getElementById('lista').children.length + '|' + document.getElementById('lista').children[0].textContent + '|' + document.getElementById('resumen').textContent + '|' + c.value");
    ck("app: anade tarea", !strcmp(ev, "1|comprar pan|1 pendientes de 1|"));
    js("document.getElementById('lista').children[0].click(); document.getElementById('lista').children[0].className + '|' + document.getElementById('resumen').textContent");
    ck("app: marca hecha", !strcmp(ev, "hecha|0 pendientes de 1"));
    js("document.getElementById('nueva').value='  '; document.getElementById('formulario').dispatchEvent(new Event('submit', {cancelable:true})); document.getElementById('lista').children.length");
    ck("app: vacia no anade", !strcmp(ev, "1"));
    cierra();
    abre("juego");
    js("document.getElementById('empezar').click(); document.getElementById('objetivo').click(); document.getElementById('objetivo').click(); document.getElementById('puntos').textContent");
    ck("juego: puntos", !strcmp(ev, "2"));
    pump(3100); js("document.getElementById('tiempo').textContent"); ck("juego: el tiempo corre", !strcmp(ev, "27"));
    pump(28000); js("document.getElementById('tiempo').textContent"); ck("juego: termina en 0", !strcmp(ev, "0"));
    js("document.getElementById('objetivo').click(); document.getElementById('puntos').textContent"); ck("juego: sin partida no puntua", !strcmp(ev, "2"));
    js("document.getElementById('empezar').click(); document.getElementById('puntos').textContent + '|' + document.getElementById('tiempo').textContent"); ck("juego: reinicia", !strcmp(ev, "0|30"));
    cierra();
}

int main(void)
{
    test_paths(); test_check(); test_bundle(); test_plantillas();
    if (fails) { printf("test_html_275: %d fallos de %d\n", fails, total); return 1; }
    printf("test_html_275: %d comprobaciones OK\n", total);
    return 0;
}
