/* SOMA_274_JS: pruebas en el anfitrion del nucleo de "js" (src/apps/jsrun_core.c): consola, input, fs, temporizadores, promesas, errores,
 * exit, comprobacion de sintaxis y la ventana (win.*). Se compila con ASan/UBSan. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jsrun_core.h"

void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;

static char g_out[8192]; static uint32_t g_on;
static const char *g_in[8]; static int g_ni, g_ki;
static struct { char name[64], data[512]; } g_f[8]; static int g_nf;
static int g_win_alive, g_clicks, g_nwords, g_presents, g_stop_after;
static int32_t g_words[64];
static int g_frames;

static void io_out(void *u, const char *s, uint32_t n) { (void)u; if (g_on + n < sizeof g_out) { memcpy(g_out + g_on, s, n); g_on += n; g_out[g_on] = 0; } }
static int io_readline(void *u, char *b, uint32_t max) { (void)u; if (g_ki >= g_ni) return -1; strncpy(b, g_in[g_ki], max - 1); b[max - 1] = 0; return (int)strlen(g_in[g_ki++]); }
static int io_readfile(void *u, const char *p, char **o, uint32_t *l)
{ (void)u; for (int i = 0; i < g_nf; i++) if (!strcmp(p, g_f[i].name)) { *l = (uint32_t)strlen(g_f[i].data); *o = kmalloc(*l + 1); memcpy(*o, g_f[i].data, *l + 1); return 0; } return -1; }
static void putf(const char *name, const char *data) { for (int i = 0; i < g_nf; i++) if (!strcmp(g_f[i].name, name)) { strcpy(g_f[i].data, data); return; } strcpy(g_f[g_nf].name, name); strcpy(g_f[g_nf++].data, data); }
static int io_writefile(void *u, const char *p, const char *d, uint32_t n)
{ (void)u; if (strncmp(p, "/workspace/", 11) || n > 500) return -1; char t[512]; memcpy(t, d, n); t[n] = 0; putf(p, t); return 0; }
static uint64_t io_now(void *u) { (void)u; return g_now; }
static void io_sleep(void *u, uint32_t ms) { (void)u; g_now += ms ? ms : 1; }
static int io_stopped(void *u) { (void)u; return g_stop_after && g_now > 10000 + (uint64_t)g_stop_after; }
static int io_wcreate(void *u, const char *t, uint32_t tl, int w, int h) { (void)u; (void)t; (void)tl; (void)w; (void)h; g_win_alive = 1; return 1; }
static void io_wclose(void *u) { (void)u; g_win_alive = 0; }
static void io_wpresent(void *u) { (void)u; g_presents++; if (++g_frames >= 3) g_win_alive = 0; }
static void io_wstate(void *u, int64_t st[8], int tc) { (void)u; st[0] = g_win_alive; st[1] = 300; st[2] = 200; st[3] = 7; st[4] = 9; st[7] = tc && g_clicks > 0; if (tc && g_clicks) g_clicks--; }
static int io_wkey(void *u) { (void)u; return 16; }
static void io_wdraw(void *u, const int32_t *w, uint32_t n) { (void)u; g_nwords += (int)n; for (uint32_t i = 0; i < n && i < 64; i++) g_words[i] = w[i]; }

static struct jsr_io IO = { 0, io_out, io_readline, io_readfile, io_writefile, io_now, 0, io_sleep, io_stopped, io_wcreate, io_wclose, io_wpresent, io_wstate, io_wkey, io_wdraw };

static int fails, total;
static void reset(void) { g_on = 0; g_out[0] = 0; g_ni = g_ki = 0; g_nf = 0; g_win_alive = 0; g_clicks = 0; g_nwords = 0; g_presents = 0; g_stop_after = 0; g_frames = 0; g_now = 10000; }
static int run(const char *src, int check) { reset(); return jsrun_run(&IO, src, (uint32_t)strlen(src), "t.js", check); }
static void ck(const char *nombre, int cond) { total++; if (!cond) { fails++; printf("FAIL %s  (salida: [%s])\n", nombre, g_out); } }
static int has(const char *s) { return strstr(g_out, s) != 0; }

int main(void)
{
    ck("log", run("console.log('hola', 1+2, [1,2], {a:1})", 0) == JSR_OK && !strcmp(g_out, "hola 3 [ 1, 2 ] { a: 1 }\n"));
    ck("print", run("print('x', 5); print({k:'v'})", 0) == JSR_OK && !strcmp(g_out, "x 5\n{ k: 'v' }\n"));
    ck("template/clases", run("class A{constructor(n){this.n=n} get d(){return `n=${this.n}`}} print(new A(4).d)", 0) == JSR_OK && !strcmp(g_out, "n=4\n"));
    ck("arrow/map", run("print([1,2,3].map(x=>x*2).join('-'))", 0) == JSR_OK && !strcmp(g_out, "2-4-6\n"));
    ck("destructuring", run("const {a,b:[c]}={a:1,b:[2]}; print(a+c)", 0) == JSR_OK && !strcmp(g_out, "3\n"));
    ck("JSON", run("print(JSON.stringify({a:[1,{b:2}]}))", 0) == JSR_OK && !strcmp(g_out, "{\"a\":[1,{\"b\":2}]}\n"));
    ck("Math", run("print(Math.max(3,9,2), Math.floor(2.7))", 0) == JSR_OK && !strcmp(g_out, "9 2\n"));
    ck("regex", run("print('a1b22'.replace(/\\d+/g, '#'))", 0) == JSR_OK && !strcmp(g_out, "a#b#\n"));
    ck("bigint", run("print(2n**64n)", 0) == JSR_OK && !strcmp(g_out, "18446744073709551616\n"));
    ck("generadores", run("function* g(){yield 1;yield 2} print([...g()].join())", 0) == JSR_OK && !strcmp(g_out, "1,2\n"));

    g_ni = 0; reset(); g_in[0] = "Ana"; g_in[1] = "41"; g_ni = 2;
    { const char *s = "const n = input('Nombre: '); const e = Number(input('Edad: ')); print('Hola ' + n + ', ' + (e+1)); print(input('mas? '))";
      int rc = jsrun_run(&IO, s, (uint32_t)strlen(s), "t.js", 0);
      ck("input", rc == JSR_OK && !strcmp(g_out, "Nombre: Edad: Hola Ana, 42\nmas? null\n")); }

    ck("timeout", run("setTimeout(()=>print('b'),50); setTimeout(()=>print('a'),10); print('0')", 0) == JSR_OK && !strcmp(g_out, "0\na\nb\n"));
    ck("interval", run("let n=0; const id=setInterval(()=>{ print('t'+(++n)); if(n==3) clearInterval(id) }, 5)", 0) == JSR_OK && !strcmp(g_out, "t1\nt2\nt3\n"));
    ck("promesas", run("Promise.resolve(5).then(v=>print('p'+v)); print('s')", 0) == JSR_OK && !strcmp(g_out, "s\np5\n"));
    ck("async", run("const w=ms=>new Promise(r=>setTimeout(r,ms)); (async()=>{ await w(20); print('x'); await w(5); print('y') })()", 0) == JSR_OK && !strcmp(g_out, "x\ny\n"));
    ck("sleep", run("const t=Date.now(); sleep(30); print('ok')", 0) == JSR_OK && !strcmp(g_out, "ok\n") && g_now >= 10030);

    ck("fs.write/read", run("print(fs.write('/workspace/a.txt','datos')); print(fs.read('/workspace/a.txt')); print(fs.exists('/workspace/a.txt'))", 0) == JSR_OK && !strcmp(g_out, "true\ndatos\ntrue\n"));
    ck("fs.privado", run("print(fs.write('/etc/x','no')); print(fs.read('/etc/passwd'))", 0) == JSR_OK && !strcmp(g_out, "false\nnull\n"));

    reset(); putf("/workspace/util.js", "exports.suma = (a,b)=>a+b; exports.n = 3;"); putf("/workspace/lib/index.js", "module.exports = { hi: 'lib', up: require('../util').n + 1 };");
    putf("/workspace/datos.json", "{\"a\":[1,2]}"); putf("/workspace/ciclo.js", "exports.v = 1; var o = require('./ciclo2'); exports.w = o.v + 1;"); putf("/workspace/ciclo2.js", "var a = require('./ciclo'); exports.v = 10 + a.v;");
    putf("/workspace/cnt.js", "globalThis.__cnt = (globalThis.__cnt||0) + 1; module.exports = __cnt;");
    { const char *sr = "const u = require('./util'); print(u.suma(2,3), u.n); print(require('./lib').hi, require('./lib').up); print(require('./datos').a.length); const c = require('./ciclo'); print(c.v, c.w); require('./cnt'); print(require('./cnt.js'), require('/workspace/cnt'))";
      int rc = jsrun_run(&IO, sr, (uint32_t)strlen(sr), "t.js", 0);
      ck("require", rc == JSR_OK && !strcmp(g_out, "5 3\nlib 4\n2\n1 12\n1 1\n")); }
    ck("require no existe", run("try { require('./nada') } catch(e){ print(e.message) }", 0) == JSR_OK && has("no existe el modulo ./nada"));
    { reset(); putf("/workspace/sub/m.js", "module.exports = __filename + '|' + __dirname;"); const char *sf = "print(require('./sub/m'))";
      ck("require __filename", jsrun_run(&IO, sf, (uint32_t)strlen(sf), "t.js", 0) == JSR_OK && !strcmp(g_out, "/workspace/sub/m.js|/workspace/sub\n")); }
    ck("import da pista", run("import x from './m.js'", 0) == JSR_ERR_JS && has("require("));
    ck("error runtime", run("print('a'); null.x", 0) == JSR_ERR_JS && has("a\n") && has("[error]") && has("Uncaught"));
    ck("error sintaxis", run("var = ;", 0) == JSR_ERR_JS && has("[error]"));
    ck("throw", run("throw new Error('boom')", 0) == JSR_ERR_JS && has("boom"));
    ck("promesa rechazada no cuelga", run("Promise.reject(new Error('x')); print('fin')", 0) == JSR_OK && has("fin"));
    ck("exit 0", run("print('a'); exit(0); print('b')", 0) == JSR_ERR_EXIT && !strcmp(g_out, "a\n"));
    ck("exit 3", run("exit(3)", 0) == JSR_ERR_EXIT + 3);
    ck("exit en temporizador", run("setTimeout(()=>{print('t'); exit(2)},5); setTimeout(()=>print('no'),50)", 0) == JSR_ERR_EXIT + 2 && !strcmp(g_out, "t\n"));
    ck("bucle infinito de temporizador parado", (g_stop_after = 200, 1) && (reset(), g_stop_after = 200, jsrun_run(&IO, "setInterval(()=>{},5)", 21, "t.js", 0)) == JSR_ERR_STOP);

    ck("check ok", run("let a = 1; function f(){ return a }", 1) == JSR_OK && g_on == 0);
    ck("check no ejecuta", run("print('no debe salir')", 1) == JSR_OK && g_on == 0);
    ck("check error", run("function (", 1) == JSR_ERR_JS && has("[error]"));
    ck("check llaves", run("if (x) {", 1) == JSR_ERR_JS);

    ck("win sin escritorio", (IO.win_create = 0, run("try { win.create('x',200,100) } catch(e){ print(e.message) }", 0)) == JSR_OK && has("no hay escritorio"));
    IO.win_create = io_wcreate;
    ck("win bucle", run("win.create('t',300,200); function f(){ win.clear(win.rgb(1,2,3)); win.rect(1,2,3,4,255); win.text(5,6,'hi',0xFFFFFF); win.present(); if(win.active()) setTimeout(f,16) } f()", 0) == JSR_OK && g_presents == 3 && g_nwords > 0);
    ck("win datos", g_words[0] == JSR_G_CLEAR && g_words[1] == 0x010203 && g_words[2] == JSR_G_RECT && g_words[3] == 1 && g_words[7] == 255 && g_words[8] == JSR_G_TEXT && g_words[11] == 0xFFFFFF && g_words[12] == 2);
    ck("win mouse/key/size", run("win.create('t'); const m=win.mouse(), k=win.key(), s=win.size(); print(m.x,m.y,m.clicked,k===win.UP,s[0]+'x'+s[1]); win.close()", 0) == JSR_OK && !strcmp(g_out, "7 9 false true 300x200\n"));
    ck("win clic", (reset(), g_clicks = 1, jsrun_run(&IO, "win.create('t'); print(win.mouse().clicked, win.mouse().clicked); win.close()", 77, "t.js", 0)) == JSR_OK && !strcmp(g_out, "true false\n"));

    if (fails) { printf("test_jsrun_274: %d fallos de %d\n", fails, total); return 1; }
    printf("test_jsrun_274: %d comprobaciones OK\n", total);
    return 0;
}
