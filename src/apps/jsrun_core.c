/* SOMA_274_JS: ver jsrun_core.h */
#include <stdint.h>
#include <stddef.h>
#include "web_util.h"
#include "web_js.h"
#include "web_js_i.h"
#include "jsrun_core.h"

struct jrt *wjs_rt(struct wjs *js);
#include "web_css.h"
struct wdoc *wd_new(void);
void wd_parse(struct wdoc *d, const char *html, uint32_t len);
void wd_free(struct wdoc *d);

#define ARG(i) ((i) < argc ? args[i] : jundef())

static const struct jsr_io *g_io;
static int g_exit = -1, g_win_open;
static int32_t g_gb[4096];
static uint32_t g_gn;

static void outs(const char *s, uint32_t n) { if (g_io->out) g_io->out(g_io->ud, s, n); }
static void outz(const char *s) { outs(s, wu_strlen(s)); }

/* ------------------------------------------------------------ consola */
static int has_mark(const char *m, const char *K)
{
    for (; *m; m++) { int i = 0; while (K[i] && m[i] == K[i]) i++; if (!K[i]) return 1; }
    return 0;
}
static int has_exit_mark(const char *m) { return has_mark(m, "__soma_exit__"); }
static void host_log(void *ud, int level, const char *msg)
{
    (void)ud;
    if (g_exit >= 0 && has_exit_mark(msg)) return;
    if (level >= 2) outz("[error] "); else if (level == 1) outz("[aviso] ");
    outz(msg);
    if (level >= 2 && has_mark(msg, "modulos ES")) outz("  -> en js usa require('./modulo.js') y module.exports");
    outs("\n", 1);
}
static void host_dirty(void *ud, int v) { (void)ud; (void)v; }
static void host_alert(void *ud, const char *m) { (void)ud; outz(m); outs("\n", 1); }

static jv n_print(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    for (int i = 0; i < argc; i++) {
        if (i) outs(" ", 1);
        jstr *s = args[i].t == JT_STR ? args[i].u.s : args[i].t == JT_OBJ ? js_inspect(rt, args[i], 2) : js_tostr(rt, args[i]);
        outs(s->d, s->len);
    }
    outs("\n", 1);
    return jundef();
}
/* input("pregunta") -> cadena, o null si no hay mas entrada */
static jv n_input(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (argc) { jstr *p = js_tostr(rt, args[0]); outs(p->d, p->len); }
    char buf[1024];
    int n = g_io->readline ? g_io->readline(g_io->ud, buf, sizeof buf) : -1;
    if (n < 0) return jnull();
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    return jstrv(js_str_new(rt, buf, (uint32_t)n));
}
static jv n_sleep(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    int64_t ms = argc ? f64_to_i64(js_tonum(rt, args[0])) : 0;
    uint32_t m = ms < 0 ? 0 : ms > 60000 ? 60000 : (uint32_t)ms;
    if (g_io->sleep) g_io->sleep(g_io->ud, m);
    return jundef();
}
static jv n_exit(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    int c = argc ? (int)f64_to_i64(js_tonum(rt, args[0])) : 0;
    g_exit = c < 0 ? 0 : c > 99 ? 99 : c;
    js_throw_err(rt, E_ERROR, "__soma_exit__");      /* corta el guion; host_log no lo muestra (no es un fallo) */
}
/* ------------------------------------------------------------ archivos (solo los que deje pasar el anfitrion: /workspace) */
static jv n_fread(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (!argc || !g_io->readfile) return jnull();
    jstr *p = js_tostr(rt, args[0]); char *d = 0; uint32_t n = 0;
    if (g_io->readfile(g_io->ud, p->d, &d, &n) != 0) return jnull();
    jv r = jstrv(js_str_new(rt, d, n)); kfree(d); return r;
}
static jv n_fwrite(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (argc < 2 || !g_io->writefile) return jbool(0);
    jstr *p = js_tostr(rt, args[0]); jstr *d = js_tostr(rt, args[1]);
    return jbool(g_io->writefile(g_io->ud, p->d, d->d, d->len) == 0);
}
static jv n_fexists(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (!argc || !g_io->readfile) return jbool(0);
    jstr *p = js_tostr(rt, args[0]); char *d = 0; uint32_t n = 0;
    if (g_io->readfile(g_io->ud, p->d, &d, &n) != 0) return jbool(0);
    kfree(d); return jbool(1);
}
/* ------------------------------------------------------------ ventana: la misma API que win.* de Python y de SOMA-LANG */
static void gflush(void) { if (g_gn && g_io->win_draw) g_io->win_draw(g_io->ud, g_gb, g_gn); g_gn = 0; }
static void gneed(uint32_t w) { if (g_gn + w > sizeof g_gb / 4) gflush(); }
static int32_t ai(struct jrt *rt, jv v) { return (int32_t)f64_to_i64(js_tonum(rt, v)); }
static int32_t ac(struct jrt *rt, jv v) { return (int32_t)((uint32_t)f64_to_i64(js_tonum(rt, v)) & 0xFFFFFFu); }
enum { W_CREATE, W_CLOSE, W_ACTIVE, W_MOUSE, W_KEY, W_SIZE, W_PRESENT, W_CLEAR, W_PIXEL, W_RECT, W_LINE, W_CIRCLE, W_TEXT, W_RGB };
static jv n_win(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv;
    int64_t st[8];
    switch (magic) {
    case W_CREATE: {
        if (!g_io->win_create) js_throw_err(rt, E_ERROR, "no hay escritorio: la ventana solo funciona en el escritorio de SOMA");
        jstr *t = argc ? js_tostr(rt, args[0]) : js_str_cstr(rt, "JavaScript");
        int w = 640, h = 420;
        if (argc == 3) { w = ai(rt, args[1]); h = ai(rt, args[2]); }
        else if (argc >= 5) { w = ai(rt, args[3]); h = ai(rt, args[4]); }
        if (w < 120) w = 120;
        if (h < 80) h = 80;
        if (!g_io->win_create(g_io->ud, t->d, t->len, w, h)) js_throw_err(rt, E_ERROR, "no hay escritorio: la ventana solo funciona en el escritorio de SOMA");
        g_win_open = 1; return jbool(1);
    }
    case W_CLOSE: gflush(); if (g_io->win_close) g_io->win_close(g_io->ud); g_win_open = 0; return jundef();
    case W_PRESENT: gflush(); if (g_io->win_present) g_io->win_present(g_io->ud); return jundef();
    case W_KEY: return jnum(f64_from_i64(g_io->win_key ? g_io->win_key(g_io->ud) : 0));
    case W_ACTIVE: case W_MOUSE: case W_SIZE: {
        for (int i = 0; i < 8; i++) st[i] = 0;
        if (g_io->win_state) g_io->win_state(g_io->ud, st, magic == W_MOUSE);
        if (magic == W_ACTIVE) return jbool(st[0] != 0);
        jobj *o;
        if (magic == W_SIZE) { o = js_arr_new(rt, 2); js_arr_push(rt, o, jnum(f64_from_i64(st[1]))); js_arr_push(rt, o, jnum(f64_from_i64(st[2]))); return jobjv(o); }
        o = js_obj_new(rt, rt->p_object);
        js_set_c(rt, o, "x", jnum(f64_from_i64(st[3]))); js_set_c(rt, o, "y", jnum(f64_from_i64(st[4])));
        js_set_c(rt, o, "clicked", jbool(st[7] != 0)); js_set_c(rt, o, "button", jbool(st[5] != 0)); js_set_c(rt, o, "inside", jbool(st[6] != 0));
        return jobjv(o);
    }
    case W_CLEAR: gneed(2); g_gb[g_gn++] = JSR_G_CLEAR; g_gb[g_gn++] = argc ? ac(rt, args[0]) : 0; return jundef();
    case W_PIXEL: gneed(4); g_gb[g_gn++] = JSR_G_PIXEL; g_gb[g_gn++] = ai(rt, ARG(0)); g_gb[g_gn++] = ai(rt, ARG(1)); g_gb[g_gn++] = ac(rt, ARG(2)); return jundef();
    case W_RECT: case W_LINE:
        gneed(6); g_gb[g_gn++] = magic == W_RECT ? JSR_G_RECT : JSR_G_LINE;
        for (int i = 0; i < 4; i++) g_gb[g_gn++] = ai(rt, ARG(i));
        g_gb[g_gn++] = ac(rt, ARG(4)); return jundef();
    case W_CIRCLE:
        gneed(5); g_gb[g_gn++] = JSR_G_CIRC;
        for (int i = 0; i < 3; i++) g_gb[g_gn++] = ai(rt, ARG(i));
        g_gb[g_gn++] = ac(rt, ARG(3)); return jundef();
    case W_TEXT: {
        jstr *s = js_tostr(rt, ARG(2)); uint32_t tl = s->len > 1024 ? 1024 : s->len;
        uint32_t words = (tl + 3) / 4; gneed(5 + words);
        g_gb[g_gn++] = JSR_G_TEXT; g_gb[g_gn++] = ai(rt, ARG(0)); g_gb[g_gn++] = ai(rt, ARG(1)); g_gb[g_gn++] = ac(rt, ARG(3)); g_gb[g_gn++] = (int32_t)tl;
        char *d = (char *)(g_gb + g_gn);
        for (uint32_t i = 0; i < words * 4; i++) d[i] = i < tl ? s->d[i] : 0;
        g_gn += words; return jundef();
    }
    case W_RGB: return jnum(f64_from_i64(((ai(rt, ARG(0)) & 255) << 16) | ((ai(rt, ARG(1)) & 255) << 8) | (ai(rt, ARG(2)) & 255)));
    }
    return jundef();
}
static void def_fn(struct jrt *rt, jobj *o, const char *name, jnative f, int nargs, int magic) { js_method(rt, o, name, f, nargs, magic); }
static void install(struct wjs *js)
{
    struct jrt *rt = wjs_rt(js);
    def_fn(rt, rt->global, "print", n_print, 1, 0);
    def_fn(rt, rt->global, "input", n_input, 1, 0);
    def_fn(rt, rt->global, "readLine", n_input, 1, 0);
    def_fn(rt, rt->global, "sleep", n_sleep, 1, 0);
    def_fn(rt, rt->global, "exit", n_exit, 1, 0);
    jobj *fs = js_obj_new(rt, rt->p_object);
    def_fn(rt, fs, "read", n_fread, 1, 0); def_fn(rt, fs, "write", n_fwrite, 2, 0); def_fn(rt, fs, "exists", n_fexists, 1, 0);
    js_def(rt, rt->global, js_intern_c(rt, "fs"), jobjv(fs), 0);
    jobj *w = js_obj_new(rt, rt->p_object);
    static const struct { const char *n; int m, a; } W[] = {
        { "create", W_CREATE, 0 }, { "close", W_CLOSE, 0 }, { "active", W_ACTIVE, 0 }, { "mouse", W_MOUSE, 0 }, { "key", W_KEY, 0 }, { "size", W_SIZE, 0 },
        { "present", W_PRESENT, 0 }, { "clear", W_CLEAR, 1 }, { "pixel", W_PIXEL, 3 }, { "rect", W_RECT, 5 }, { "line", W_LINE, 5 }, { "circle", W_CIRCLE, 4 },
        { "text", W_TEXT, 4 }, { "rgb", W_RGB, 3 }, { 0, 0, 0 } };
    for (int i = 0; W[i].n; i++) def_fn(rt, w, W[i].n, n_win, W[i].a, W[i].m);
    js_set_c(rt, w, "UP", jnum(f64_from_i64(16))); js_set_c(rt, w, "DOWN", jnum(f64_from_i64(14))); js_set_c(rt, w, "LEFT", jnum(f64_from_i64(2))); js_set_c(rt, w, "RIGHT", jnum(f64_from_i64(6)));
    js_def(rt, rt->global, js_intern_c(rt, "win"), jobjv(w), 0);
}


/* require(): modulos CommonJS para aplicaciones de varios archivos (los import/export de ES son de las paginas web, no de js).
 * require("./util") busca util, util.js, util/index.js y util.json; relativo al archivo que lo pide (el principal: /workspace). */
static const char PRELUDE[] =
"(function(g){ var cache = {};\n"
"  function norm(p){ var o = [], a = p.split('/'); for (var i = 0; i < a.length; i++){ var s = a[i]; if (s === '' || s === '.') continue; if (s === '..') o.pop(); else o.push(s); } return '/' + o.join('/'); }\n"
"  function dirOf(p){ var i = p.lastIndexOf('/'); return i <= 0 ? '/' : p.slice(0, i); }\n"
"  function find(path){ var c = [path, path + '.js', path + '/index.js', path + '.json']; for (var i = 0; i < c.length; i++){ var t = fs.read(c[i]); if (t !== null) return [c[i], t]; } return null; }\n"
"  function mk(dir){ return function require(p){\n"
"    p = String(p); var full = p.charAt(0) === '/' ? norm(p) : norm(dir + '/' + p);\n"
"    var f = find(full); if (!f) throw new Error('no existe el modulo ' + p + ' (buscado en ' + full + ')');\n"
"    var path = f[0]; if (cache[path]) return cache[path].exports;\n"
"    var m = { exports: {}, id: path, filename: path }; cache[path] = m;\n"
"    if (path.slice(-5) === '.json') { m.exports = JSON.parse(f[1]); return m.exports; }\n"
"    new Function('exports', 'require', 'module', '__filename', '__dirname', f[1])(m.exports, mk(dirOf(path)), m, path, dirOf(path));\n"
"    return m.exports; }; }\n"
"  g.require = mk('/workspace'); g.module = { exports: {} }; g.exports = g.module.exports; g.__filename = '/workspace'; g.__dirname = '/workspace';\n"
"})(globalThis);\n";

/* ------------------------------------------------------------ ejecucion */
static const char *g_check_name;
static jv n_check(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    jstr *src = argc ? js_tostr(rt, args[0]) : js_str_cstr(rt, "");
    int line = 0; char msg[160];
    if (!js_parse(rt, src->d, src->len, &line, msg, sizeof msg)) js_throw_err(rt, E_SYNTAX, "%s (%s, linea %d)", msg, g_check_name, line);
    return jundef();
}
static void host_nav(void *ud, const char *u, int r) { (void)ud; (void)u; (void)r; }

int jsrun_run(const struct jsr_io *io, const char *src, uint32_t len, const char *name, int check)
{
    g_io = io; g_exit = -1; g_win_open = 0; g_gn = 0;
    static struct wjs_host h;
    h.ud = 0; h.log = host_log; h.dirty = host_dirty; h.alert = host_alert; h.navigate = host_nav;
    h.url = "soma://js"; h.ua = "SOMA-JS";
    const char *html = "<!doctype html><html><head></head><body></body></html>";
    struct wdoc *d = wd_new();
    if (!d) return JSR_ERR_MEM;
    wd_parse(d, html, wu_strlen(html));
    struct wcss *css347 = wc_new(d); d->sheets = css347;   /* SOMA_347: sin hoja, querySelector(All)/matches/closest no encontraban nada */
    struct wjs *js = wjs_new(d, &h);
    if (!js) { d->sheets = 0; wc_free(css347); wd_free(d); return JSR_ERR_MEM; }
    int rc = JSR_OK;
    if (check) {
        /* solo sintaxis: se analiza con el mismo analizador y no se ejecuta nada */
        struct jrt *rt = wjs_rt(js);
        js_def(rt, rt->global, js_intern_c(rt, "__jsrc"), jstrv(js_str_new(rt, src, len)), 0);
        js_def(rt, rt->global, js_intern_c(rt, "__jscheck"), jobjv(js_native(rt, "__jscheck", n_check, 1, 0)), 0);
        static const char call[] = "__jscheck(__jsrc)";
        g_check_name = name ? name : "script";
        rc = wjs_eval(js, call, sizeof call - 1, g_check_name) == 0 ? JSR_OK : JSR_ERR_JS;
    } else {
        install(js);
        wjs_eval(js, PRELUDE, sizeof PRELUDE - 1, "<require>");
        { struct jrt *r0 = wjs_rt(js); r0->cur_line = 0; r0->cur_src = 0; r0->srcname188[0] = 0; }   /* que los errores del guion no hereden la posicion del preludio */
        wjs_rt(js)->task_ms = 0x7FFFFFFFu;           /* sin el limite de 12 s del navegador: el nucleo ya limita el proceso (10 min) y Esc lo para */
        if (wjs_eval(js, src, len, name ? name : "script") != 0 && g_exit < 0) rc = JSR_ERR_JS;
        uint64_t last = io->now ? io->now(io->ud) : 0;
        while (rc == JSR_OK && g_exit < 0) {
            if (io->stopped && io->stopped(io->ud)) { rc = JSR_ERR_STOP; break; }
            if (g_win_open) {
                int64_t st[8] = {0};
                if (io->win_state) io->win_state(io->ud, st, 0);
                if (!st[0]) break;                                   /* cerraron la ventana */
            } else if (!wjs_has_timers(js)) break;                   /* nada mas que hacer */
            if (!wjs_has_timers(js)) { if (io->sleep) io->sleep(io->ud, 8); continue; }
            int ran = 0; uint64_t now = io->now ? io->now(io->ud) : last + 8; last = now;
            int nx = wjs_pump(js, now, &ran);
            gflush();
            if (nx < 0 && !ran && wjs_halted(js)) { rc = g_exit >= 0 ? JSR_OK : JSR_ERR_JS; break; }
            if (!ran && io->sleep) io->sleep(io->ud, 2);
        }
        gflush();
        if (g_win_open && io->win_close) io->win_close(io->ud);
    }
    wjs_free(js); d->sheets = 0; wc_free(css347); wd_free(d);
    if (g_exit >= 0) return JSR_ERR_EXIT + g_exit;
    return rc;
}
