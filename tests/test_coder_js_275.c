/* SOMA_275_HTML: el programador de SOMA escribiendo JavaScript y HTML. En el anfitrion, con un LLM falso y comprobadores falsos que contestan con los
 * mensajes REALES de js (jsrun) y de html_check (htmlproj):
 *  - la referencia y el ejemplo son los de cada lenguaje; el codigo va en ```javascript / ```html; SOMA-LANG, Python y C no aparecen;
 *  - require('fs'), alert(), import... y los CDN/canvas/<link> locales se rechazan con su pista y se reparan;
 *  - un error de sintaxis llega a la reparacion con su linea; las pruebas de consola se ejecutan con run_src;
 *  - las lecciones van a su propio archivo; utilidades: cc_js_error, cc_html_normalize, cc_mission_wants_js, cc_mission_wants_html. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"
#include "coder_core.h"

static char g_file[32768]; static uint32_t g_flen; static int g_has; static int g_les_js, g_les_html, g_les_otros;
static int fs_read(const char *p, char *b, uint32_t max) { if (!strstr(p, "/workspace/t.")) return -1; if (!g_has) return -1; uint32_t n = g_flen < max ? g_flen : max; memcpy(b, g_file, n); return (int)n; }
static int fs_write(const char *p, const char *b, uint32_t len)
{
    if (!strcmp(p, CC_LESSONS_JS)) { g_les_js++; return (int)len; }
    if (!strcmp(p, CC_LESSONS_HTML)) { g_les_html++; return (int)len; }
    if (!strcmp(p, CC_LESSONS) || !strcmp(p, CC_LESSONS_PY) || !strcmp(p, CC_LESSONS_C)) { g_les_otros++; return -1; }
    memcpy(g_file, b, len); g_flen = len; g_file[len] = 0; g_has = 1; return (int)len;
}
static void nolog(const char *m) { (void)m; }
static const char *const *g_replies; static int g_call;
static char g_prompts[8][24576];
static int fake_llm(const char *prompt, char *out, uint32_t max, uint32_t to)
{
    (void)to;
    if (g_call < 8) snprintf(g_prompts[g_call], sizeof g_prompts[0], "%s", prompt);
    const char *r = strstr(prompt, "revisor") ? "OK" : g_replies[g_call] ? g_replies[g_call] : "OK";
    if (!strstr(prompt, "revisor")) g_call++;
    snprintf(out, max, "%s", r);
    return 1;
}
static int g_checks, g_runs;
static int fake_check_js(const char *src, uint32_t n, char *out, uint32_t max)
{
    (void)n; g_checks++;
    const char *p = strstr(src, "ERRORSINTAXIS");
    if (!p) { out[0] = 0; return 0; }
    int line = 1; for (const char *q = src; q < p; q++) if (*q == '\n') line++;
    snprintf(out, max, "[error] Uncaught SyntaxError: token inesperado (col 1: ERRORSINTAXIS) (programa.js, linea %d) en <anonymous>\n", line);
    return 1;
}
static int fake_run_js(const char *src, uint32_t n, const char *input, char *out, uint32_t max)
{
    (void)n; g_runs++;
    if (strstr(src, "null.x")) { snprintf(out, max, "[error] Uncaught TypeError: No se pueden leer propiedades de null (leyendo 'x') (l:3) en <cadena>:3\n"); return 1; }
    int a = 0, b = 0; sscanf(input, "%d\n%d", &a, &b);
    snprintf(out, max, "%d\n", strstr(src, "a - b") ? a - b : a + b);
    return 0;
}
static int fake_check_html(const char *src, uint32_t n, char *out, uint32_t max)
{
    (void)n; g_checks++;
    const char *p = strstr(src, "<div>SIN");
    if (!p) { snprintf(out, max, "OK\n"); return 0; }
    int line = 1; for (const char *q = src; q < p; q++) if (*q == '\n') line++;
    snprintf(out, max, "programa.html:%d: error: <div> sin cerrar\n", line);
    return 1;
}
static const struct cc_test SUMA_TESTS[] = { { "dos y tres", "2\n3\n", "5" }, { "negativos", "-4\n1\n", "-3" } };
static struct cc_result R;
static int run_mission(int lang, const char *target, const char *mission, const char *const *replies, int with_tests)
{
    struct cc_env e; memset(&e, 0, sizeof e);
    e.llm = fake_llm; e.read = fs_read; e.write = fs_write; e.log = nolog; e.max_llm_calls = 7;
    e.lang = lang; e.check_src = lang == CC_LANG_HTML ? fake_check_html : fake_check_js; e.run_src = fake_run_js;
    if (with_tests) { e.tests = SUMA_TESTS; e.ntests = 2; }
    g_replies = replies; g_call = 0; g_has = 0; g_checks = g_runs = 0; memset(g_prompts, 0, sizeof g_prompts);
    cc_run(&e, target, mission, &R);
    return R.ok;
}
static int any_prompt(const char *needle) { for (int i = 0; i < 8; i++) if (strstr(g_prompts[i], needle)) return 1; return 0; }

#define JS_CONTADOR "```javascript\nwin.create('Contador', 320, 200);\nlet n = 0;\nwhile (win.active()) {\n  const m = win.mouse();\n  if (m.clicked) n++;\n  win.clear(0x101828);\n  win.text(20, 30, `n=${n}`, 0xFFFFFF);\n  win.present();\n  sleep(16);\n}\n```"
#define JS_NODE "```javascript\nconst fs = require('fs');\nconst l = require('readline');\nconsole.log(process.argv);\n```"
#define JS_SUMA(OP) "```javascript\nconst a = Number(input()), b = Number(input());\nconsole.log(" OP ");\n```"
#define JS_ERR "```javascript\nconst a = Number(input());\nconst b = ERRORSINTAXIS Number(input());\nconsole.log(a + b);\n```"
#define JS_SE_CAE "```javascript\nconst a = Number(input()), b = Number(input());\nnull.x;\nconsole.log(a + b);\n```"
#define H_OK "```html\n<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n<meta charset=\"utf-8\">\n<title>T</title>\n<style>body{color:red}</style>\n</head>\n<body>\n<h1 id=\"t\">Hola</h1>\n<script>document.getElementById('t').textContent='x';</script>\n</body>\n</html>\n```"
#define H_CDN "```html\n<!DOCTYPE html>\n<html lang=\"es\"><head><meta charset=\"utf-8\"><title>T</title>\n<link rel=\"stylesheet\" href=\"https://cdn.x.org/b.css\">\n<script src=\"https://cdn.x.org/jq.js\"></script></head>\n<body><p>hola</p></body></html>\n```"
#define H_CANVAS "```html\n<!DOCTYPE html>\n<html lang=\"es\"><head><meta charset=\"utf-8\"><title>T</title></head>\n<body><canvas id=\"c\"></canvas><script>var x = c.getContext('2d');</script></body></html>\n```"
#define H_SIN_CERRAR "```html\n<!DOCTYPE html>\n<html lang=\"es\"><head><meta charset=\"utf-8\"><title>T</title></head>\n<body>\n<div>SIN cerrar\n</body></html>\n```"

int main(void)
{
    /* 1. detectores */
    CHECK(cc_mission_wants_js("un juego de la serpiente en JavaScript"));
    CHECK(cc_mission_wants_js("haz un script de js que sume dos numeros"));
    CHECK(cc_mission_wants_js("crea hola.js que salude"));
    CHECK(cc_mission_wants_js("programa con js, que cuente palabras"));
    CHECK(cc_mission_wants_js("usando javascript calcula primos"));
    CHECK(!cc_mission_wants_js("un programa en Java"));
    CHECK(!cc_mission_wants_js("lee datos.json y cuentalos"));
    CHECK(!cc_mission_wants_js("un juego con coches"));
    CHECK(!cc_mission_wants_js("hazlo en Python"));
    CHECK(cc_mission_wants_html("una pagina web de tareas"));
    CHECK(cc_mission_wants_html("hazme una landing para mi cafeteria"));
    CHECK(cc_mission_wants_html("en HTML, un formulario de contacto"));
    CHECK(cc_mission_wants_html("crea index.html con un menu"));
    CHECK(cc_mission_wants_html("una aplicacion web para apuntar gastos"));
    CHECK(!cc_mission_wants_html("un juego en C"));
    CHECK(!cc_mission_wants_html("una calculadora en Python"));
    uint32_t ln = 0; char msg[160];
    CHECK(cc_js_error("[error] Uncaught SyntaxError: token inesperado (col 1: ) (programa.js, linea 2) en <anonymous>\n", &ln, msg, sizeof msg));   /* texto real de js */
    CHECK_EQ_INT((int)ln, 2);
    CHECK(strcmp(msg, "SyntaxError: token inesperado") == 0);
    CHECK(cc_js_error("a\n[error] Uncaught TypeError: No se pueden leer propiedades de null (leyendo 'x') (l:7) en <cadena>:7\n", &ln, msg, sizeof msg));
    CHECK_EQ_INT((int)ln, 7);
    CHECK(strcmp(msg, "TypeError: No se pueden leer propiedades de null (leyendo 'x')") == 0);
    CHECK(cc_js_error("[error] Uncaught Error: no existe el modulo ./x (buscado en /workspace/x) (l:7) en require (t.js:7)\n", &ln, msg, sizeof msg));
    CHECK(strncmp(msg, "Error: no existe el modulo ./x", 30) == 0);
    CHECK(!cc_js_error("todo bien\n", &ln, msg, sizeof msg));
    CHECK(cc_c_error("programa.html:8: error: <div> sin cerrar\n", &ln, msg, sizeof msg) && ln == 8 && strcmp(msg, "<div> sin cerrar") == 0);   /* html usa el formato de cc */
    CHECK(!cc_c_error("programa.html:1: aviso: falta <title>\n", &ln, msg, sizeof msg));
    char src[128]; strcpy(src, "<p>a</p>\r\n<p>b</p>\r\n"); uint32_t sl = (uint32_t)strlen(src);
    CHECK_EQ_INT(cc_html_normalize(src, &sl, sizeof src), 2); CHECK(strcmp(src, "<p>a</p>\n<p>b</p>\n") == 0 && sl == strlen(src));

    /* 2. JavaScript con ventana: referencia de JS, bloque ```javascript, nada de los otros lenguajes */
    static const char *const r_ok[] = { JS_CONTADOR, 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "contador con un boton en una ventana", r_ok, 0));
    CHECK_EQ_INT(R.repairs, 0);
    CHECK(strstr(g_prompts[0], "JavaScript de SOMA") != 0 && strstr(g_prompts[0], "win.present()") != 0);
    CHECK(strstr(g_prompts[0], "```javascript\n") != 0);
    CHECK(strstr(g_prompts[0], "REFERENCIA SOMA-LANG") == 0 && strstr(g_prompts[0], "from soma import win") == 0 && strstr(g_prompts[0], "soma_win_create") == 0);
    CHECK(strstr(g_file, "win.present()") != 0 && strstr(g_file, "```") == 0);
    CHECK_EQ_INT(g_runs, 0);

    /* 3. Node/DOM -> pista y reparacion */
    static const char *const r_nd[] = { JS_NODE, JS_CONTADOR, 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "contador grafico con el raton", r_nd, 0));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("require('fs')', que no existe en el JavaScript de SOMA"));

    /* 4. consola: las pruebas se ejecutan; un fallo de logica se repara */
    static const char *const r_s1[] = { JS_SUMA("a + b"), 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "programa que lea dos numeros y escriba su suma", r_s1, 1));
    CHECK_EQ_INT(R.tests_pass, 2); CHECK_EQ_INT(g_runs, 2);
    static const char *const r_s2[] = { JS_SUMA("a - b"), JS_SUMA("a + b"), 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "programa que lea dos numeros y escriba su suma", r_s2, 1));
    CHECK(R.repairs >= 1 && R.tests_first_fail >= 1);
    CHECK(any_prompt("La prueba 'dos y tres' FALLA") || any_prompt("FALLA"));
    /* error de sintaxis: llega con su linea */
    static const char *const r_er[] = { JS_ERR, JS_SUMA("a + b"), 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "programa que lea dos numeros y escriba su suma", r_er, 1));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("Error de JavaScript en la linea 2: SyntaxError: token inesperado"));
    CHECK(any_prompt("Error de sintaxis de JavaScript") || any_prompt("sintaxis de JavaScript"));
    /* se cae al ejecutar: el error real llega a la reparacion */
    static const char *const r_cae[] = { JS_SE_CAE, JS_SUMA("a + b"), 0 };
    CHECK(run_mission(CC_LANG_JS, "/workspace/t.js", "programa que lea dos numeros y escriba su suma", r_cae, 1));
    CHECK(any_prompt("No se pueden leer propiedades de null"));

    /* 5. HTML */
    static const char *const h_ok[] = { H_OK, 0 };
    CHECK(run_mission(CC_LANG_HTML, "/workspace/t.html", "una pagina con un titulo", h_ok, 0));
    CHECK_EQ_INT(R.repairs, 0);
    CHECK(strstr(g_prompts[0], "HTML de SOMA") != 0 && strstr(g_prompts[0], "UN SOLO archivo") != 0 && strstr(g_prompts[0], "```html\n") != 0);
    CHECK(strstr(g_prompts[0], "win.present") == 0 && strstr(g_prompts[0], "soma_win") == 0 && strstr(g_prompts[0], "REFERENCIA SOMA-LANG") == 0);
    CHECK(strstr(g_file, "<!DOCTYPE html>") != 0 && strstr(g_file, "```") == 0);
    CHECK_EQ_INT(g_runs, 0);
    static const char *const h_cdn[] = { H_CDN, H_OK, 0 };
    CHECK(run_mission(CC_LANG_HTML, "/workspace/t.html", "una pagina con un titulo", h_cdn, 0));
    CHECK_EQ_INT(R.repairs, 1); CHECK(any_prompt("Carga recursos de internet"));
    static const char *const h_cv[] = { H_CANVAS, H_OK, 0 };
    CHECK(run_mission(CC_LANG_HTML, "/workspace/t.html", "una pagina con un dibujo", h_cv, 0));
    CHECK_EQ_INT(R.repairs, 1); CHECK(any_prompt("getContext()"));
    static const char *const h_sc[] = { H_SIN_CERRAR, H_OK, 0 };
    CHECK(run_mission(CC_LANG_HTML, "/workspace/t.html", "una pagina con un titulo", h_sc, 0));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("Error al comprobar la pagina en la linea 4: <div> sin cerrar"));
    CHECK(any_prompt("etiqueta sin cerrar"));

    /* 6. lecciones: cada lenguaje en su archivo, nunca en los de los otros */
    g_les_js = g_les_html = g_les_otros = 0;
    run_mission(CC_LANG_JS, "/workspace/t.js", "programa que lea dos numeros y escriba su suma", r_er, 1);
    run_mission(CC_LANG_HTML, "/workspace/t.html", "una pagina con un titulo", h_sc, 0);
    CHECK(g_les_js >= 1 && g_les_html >= 1 && g_les_otros == 0);
    TEST_MAIN_END("programador_js_html_275");
}
