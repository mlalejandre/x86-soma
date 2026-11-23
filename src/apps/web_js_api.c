/* SOMA_PARCHE_NAVEGADOR: capa de arranque del motor JS (pila grande, marcos de excepcion, API publica wjs_*). */
#include "web_js_i.h"

#define WJS_BIG_STACK  (web_big_mem() ? (1536u * 1024u) : (768u * 1024u))   /* SOMA_188: el doble con memoria grande */       /* pila en la que se ejecuta el interprete (los hilos del kernel solo tienen 32 KiB) */
#define WJS_STACK_SLACK (56u * 1024u)       /* margen bajo stack_lo para funciones nativas y avisos */
#define WJS_SSTACK     (160u * 1024u)       /* pila LIFO de ambitos */
#define WJS_HEAP_CAP   (24u << 20)
#define WJS_HOST_STACK (5u << 20)           /* en las pruebas de host se usa la pila normal */

struct wjs { struct jrt rt; int strikes; };

__attribute__((weak)) uint64_t web_epoch_ms(void) { return 1767225600000ull + web_now_ms(); }   /* 2026-01-01 + tiempo de actividad */

/* ------------------------------------------------------------ cambio de pila */
#if !defined(WEB_HOST_TEST) && defined(__x86_64__)
static void call_on_stack(char *top, void (*fn)(void *), void *arg)
{
    __asm__ volatile(
        "mov %%rsp, %%rbx\n\t"
        "mov %0, %%rsp\n\t"
        "call *%1\n\t"
        "mov %%rbx, %%rsp\n\t"
        : : "r"(top), "r"(fn), "D"(arg)
        : "rbx", "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory", "cc");
}
#elif !defined(WEB_HOST_TEST) && defined(__aarch64__)
static void call_on_stack(char *top, void (*fn)(void *), void *arg)
{
    register void *x0 __asm__("x0") = arg;
    __asm__ volatile(
        "mov x19, sp\n\t"
        "mov sp, %1\n\t"
        "blr %2\n\t"
        "mov sp, x19\n\t"
        : "+r"(x0) : "r"(top), "r"(fn)
        : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x19", "x30", "memory", "cc");
}
#endif

/* Ejecuta fn en otra pila (la usa el hilo trabajador del navegador para tener 1 MiB). En pruebas de host llama directamente. */
void web_call_on_stack(char *top, void (*fn)(void *), void *arg)
{
#if defined(WEB_HOST_TEST) || !(defined(__x86_64__) || defined(__aarch64__))
    (void)top; fn(arg);
#else
    call_on_stack(top, fn, arg);
#endif
}

void js_run_big(struct jrt *rt, void (*fn)(void *), void *arg)
{
    if (rt->in_task) { fn(arg); return; }
#if defined(WEB_HOST_TEST) || !(defined(__x86_64__) || defined(__aarch64__))
    char *old = rt->stack_lo;
    rt->stack_lo = (char *)__builtin_frame_address(0) - WJS_HOST_STACK;
#if defined(WEB_HOST_TEST) && defined(__linux__)
    { extern void *__libc_stack_end __attribute__((weak));   /* SOMA 189: tope de la pila que recorre el recolector (glibc lo publica) */
      rt->gc_main_hi = &__libc_stack_end && __libc_stack_end ? (char *)__libc_stack_end : (char *)(((uintptr_t)__builtin_frame_address(0) + 1024) & ~(uintptr_t)15); }
#elif defined(WEB_HOST_TEST)
    rt->gc_main_hi = (char *)(((uintptr_t)__builtin_frame_address(0) + 1024) & ~(uintptr_t)15);   /* otros anfitriones: los marcos de quien llama */
#else
    rt->gc_main_hi = 0;                              /* arquitectura sin cambio de pila: sin recolector */
#endif
    rt->in_task = 1; fn(arg); rt->in_task = 0; rt->stack_lo = old; rt->gc_main_hi = 0;
#else
    if (!rt->big_stack) rt->big_stack = (char *)kmalloc(WJS_BIG_STACK);
    if (!rt->big_stack) { rt->halted = 1; wu_copy(rt->errbuf, "sin memoria para la pila del interprete", sizeof rt->errbuf); js_log(rt, 2, rt->errbuf); return; }
    char *old = rt->stack_lo;
    rt->stack_lo = rt->big_stack + WJS_STACK_SLACK;
    rt->in_task = 1;
    rt->gc_main_hi = (char *)(((uintptr_t)rt->big_stack + WJS_BIG_STACK) & ~(uintptr_t)15); rt->gc_outer_sp = (char *)__builtin_frame_address(0);   /* SOMA 189 */
    call_on_stack(rt->gc_main_hi, fn, arg);
    rt->in_task = 0; rt->stack_lo = old; rt->gc_main_hi = 0; rt->gc_outer_sp = 0;
#endif
}

/* ------------------------------------------------------------ excepciones */
static int try_call(struct jrt *rt, void (*fn)(struct jrt *, void *), void *ud, jv *exc)
{
    struct jtry tb; int depth = rt->depth; char *stop = rt->stop; int root = rt->try_top == 0;
    tb.up = rt->try_top; tb.depth = depth; tb.sstop = stop; tb.scope = 0; tb.exc = jundef();
    if (root) rt->base_try = &tb;
    rt->try_top = &tb;
    if (__builtin_setjmp(tb.jb)) {
        rt->try_top = tb.up; rt->depth = depth; rt->stop = stop; rt->sc_flag = 0; rt->new_target = 0; js_co_reset(rt);   /* SOMA_183 */
        if (root) rt->base_try = 0;
        *exc = tb.exc; return -1;
    }
    fn(rt, ud);
    rt->try_top = tb.up; if (root) rt->base_try = 0;
    return 0;
}
struct rep { jv exc; char *out; uint32_t max; };
static void rep_fn(struct jrt *rt, void *u)
{
    struct rep *r = (struct rep *)u; jv e = r->exc; char tmp[300]; tmp[0] = 0;
    if (e.t == JT_OBJ && e.u.o->cls == JC_ERROR) {
        jstr *n = js_tostr(rt, js_get_c(rt, e, "name")), *m = js_tostr(rt, js_get(rt, e, rt->s_message));
        uint32_t k = wu_copy(tmp, n->d, sizeof tmp); if (k + 2 < sizeof tmp) { tmp[k++] = ':'; tmp[k++] = ' '; wu_copy(tmp + k, m->d, sizeof tmp - k); }
    } else if (IS_UNDEF(e) && rt->errbuf[0]) wu_copy(tmp, rt->errbuf, sizeof tmp);
    else { jstr *s = e.t == JT_OBJ ? js_inspect(rt, e, 1) : js_tostr(rt, e); wu_copy(tmp, s->d, sizeof tmp); }
    uint32_t k = wu_copy(r->out, "Uncaught ", r->max); k += wu_copy(r->out + k, tmp, r->max - k);
    if (rt->cur_line > 0 && k + 16 < r->max) { r->out[k++] = ' '; r->out[k++] = '('; r->out[k++] = 'l'; r->out[k++] = ':'; k += (uint32_t)wu_itoa(rt->cur_line, r->out + k);
#ifdef WJS_DEBUG_COL
        if (k + 12 < r->max) { r->out[k++] = ':'; k += (uint32_t)wu_itoa(rt->cur_col, r->out + k); }
#endif
        r->out[k++] = ')'; r->out[k] = 0; }
    if (e.t == JT_OBJ && e.u.o->cls == JC_ERROR) {   /* SOMA 188B: de que script viene (primer marco de la pila) */
        jv st = js_get_c(rt, e, "stack");
        if (st.t == JT_STR) {
            const char *d = st.u.s->d; const char *at = 0; for (const char *q = d; *q; q++) if (q[0] == 'a' && q[1] == 't' && q[2] == ' ') { at = q + 3; break; }
            if (at) { uint32_t n = 0; while (at[n] && at[n] != '\n') n++; const char *b = n > 70 ? at + n - 70 : at; uint32_t bn = n > 70 ? 70 : n;
                if (k + bn + 8 < r->max) { k += wu_copy(r->out + k, " en ", r->max - k); for (uint32_t i = 0; i < bn; i++) r->out[k++] = b[i]; r->out[k] = 0; } }
        }
    }
}
static void report(struct jrt *rt, jv exc)
{
    char buf[400]; struct rep r; r.exc = exc; r.out = buf; r.max = sizeof buf; jv e2;
    if (try_call(rt, rep_fn, &r, &e2) != 0) wu_copy(buf, "Uncaught (excepcion no representable)", sizeof buf);
    js_log(rt, 2, buf);
}
struct pcall { struct jrt *rt; void (*fn)(struct jrt *, void *); void *ud; int rc; int nested; };
static void pcall_body(void *p)
{
    struct pcall *c = (struct pcall *)p; jv exc;
    if (!c->nested) { c->rt->task_start = web_now_ms(); c->rt->steps = 0; }
    if (try_call(c->rt, c->fn, c->ud, &exc) != 0) { report(c->rt, exc); c->rc = -1; }
}
static int pcall(struct wjs *js, void (*fn)(struct jrt *, void *), void *ud)
{
    struct jrt *rt = &js->rt;
    if (rt->halted && rt->timed_out && js->strikes < 3) { rt->halted = 0; rt->timed_out = 0; rt->errbuf[0] = 0; }
    if (rt->halted) return -1;
    struct pcall c; c.rt = rt; c.fn = fn; c.ud = ud; c.rc = 0; c.nested = rt->in_task != 0;
    js_run_big(rt, pcall_body, &c);
    if (!c.nested) { if (rt->timed_out) js->strikes++; rt->task_start = 0; }
    return c.rc;
}

/* ------------------------------------------------------------ evaluacion */
jv js_eval_string(struct jrt *rt, const char *src, uint32_t len, int *ok)
{
    int line = 0; char msg[160];
    struct jn *prog = js_parse(rt, src, len, &line, msg, sizeof msg);
    if (!prog) { *ok = 0; char m2[200]; uint32_t k = wu_copy(m2, msg, sizeof m2); if (k + 12 < sizeof m2) { wu_copy(m2 + k, " (linea ", sizeof m2 - k); k += 8; k += (uint32_t)wu_itoa(line, m2 + k); m2[k++] = ')'; m2[k] = 0; } return jobjv(js_make_error(rt, E_SYNTAX, m2)); }
    *ok = 1; return js_run_program(rt, prog);
}
struct evargs { const char *src; uint32_t len; const char *name; char *out; uint32_t max; };
static void do_eval(struct jrt *rt, void *u)
{
    struct evargs *e = (struct evargs *)u; int line = 0; char msg[160];
    /* SOMA 188B: analizar un script cuesta unos 64 bytes de monton por byte de fuente y el interprete no tiene recolector: si no
     * cabe, se rechaza antes de empezar en vez de gastar toda la memoria en un arbol a medias y dejar la pagina sin JavaScript. */
    if (e->len > (64u << 10) && (uint64_t)e->len * 64u + js_gc_live(rt) > rt->heap_cap)
        js_throw_err(rt, E_RANGE, "script demasiado grande para la memoria del navegador (%d KiB de fuente, %d MiB libres)", (int)(e->len >> 10), (int)((rt->heap_cap > js_gc_live(rt) ? rt->heap_cap - js_gc_live(rt) : 0) >> 20));
    struct jn *prog = js_parse(rt, e->src, e->len, &line, msg, sizeof msg);
    if (!prog) js_throw_err(rt, E_SYNTAX, "%s (%s, linea %d)", msg, e->name ? e->name : "script", line);
    wu_copy(rt->srcname188, e->name ? e->name : "script", sizeof rt->srcname188); rt->cur_src = rt->srcname188;   /* SOMA_188 */
    jv v = js_run_program(rt, prog);
    js_run_micro(rt);
    if (e->out) { jstr *s = v.t == JT_STR ? v.u.s : js_inspect(rt, v, 2); wu_copy(e->out, s->d, e->max); }
}

/* ------------------------------------------------------------ API publica */
struct wjs *wjs_new(struct wdoc *doc, const struct wjs_host *host)
{
    struct wjs *js = (struct wjs *)kmalloc((uint32_t)sizeof *js);
    if (!js) return 0;
    memset(js, 0, sizeof *js);
    struct jrt *rt = &js->rt;
    rt->js = js; rt->host = host; rt->doc = doc; rt->heap_cap = web_js_heap_big();   /* SOMA 188B: proporcional al monton tambien en equipos pequenos */
#if defined(WEB_HOST_TEST)
    { extern char *getenv(const char *); const char *hc = getenv("HL_HEAP_MB"); if (hc) rt->heap_cap = (uint32_t)wu_atoi(hc) << 20; }
#endif
    rt->step_limit = 512;
    rt->sstack = (char *)kmalloc(WJS_SSTACK);
    if (!rt->sstack) { kfree(js); return 0; }
    rt->stop = rt->sstack; rt->send = rt->sstack + WJS_SSTACK;
    rt->gc_root_lo = (const char *)js; rt->gc_root_hi = (const char *)js + sizeof *js; rt->gc_sstack_size = WJS_SSTACK;   /* SOMA 189: raices del recolector */
    struct evargs none; memset(&none, 0, sizeof none);
    struct pcall c; c.rt = rt; c.fn = 0; c.ud = 0; c.rc = 0; c.nested = 0;
    /* js_lib_init y js_dom_init se ejecutan ya en la pila grande y con marco de excepcion */
    extern void wjs_init_body(struct jrt *rt, void *ud);
    c.fn = wjs_init_body;
    js_run_big(rt, pcall_body, &c);
    if (c.rc || rt->halted) { wjs_free(js); return 0; }
    return js;
}
void wjs_init_body(struct jrt *rt, void *ud)
{
    (void)ud;
    js_lib_init(rt);
    if (rt->doc) js_dom_init(rt);
}
void wjs_free(struct wjs *js)
{
    if (!js) return;
    struct jrt *rt = &js->rt;
    js_co_free_all(rt);   /* SOMA_183: pilas de las corrutinas (viven fuera del arena) */
    wa_free_all(&rt->ar); js_gc_free_all(rt);
    if (rt->sstack) kfree(rt->sstack);
    if (rt->big_stack) kfree(rt->big_stack);
    kfree(js);
}
int wjs_eval(struct wjs *js, const char *src, uint32_t len, const char *name)
{
    struct evargs e; e.src = src; e.len = len; e.name = name; e.out = 0; e.max = 0;
    return pcall(js, do_eval, &e);
}
int wjs_eval_str(struct wjs *js, const char *src, char *out, uint32_t max)
{
    struct evargs e; e.src = src; e.len = wu_strlen(src); e.name = "consola"; e.out = out; e.max = max;
    if (max) out[0] = 0;
    return pcall(js, do_eval, &e);
}
struct pumpargs { int ran, next; };
static void do_pump(struct jrt *rt, void *u) { struct pumpargs *p = (struct pumpargs *)u; p->next = js_pump_timers(rt, &p->ran); }
int wjs_pump(struct wjs *js, uint64_t now_ms, int *ran)
{
    (void)now_ms; struct pumpargs p; p.ran = 0; p.next = -1;
    if (pcall(js, do_pump, &p) != 0 && js->rt.halted) return -1;
    if (ran) *ran = p.ran;
    return p.next;
}
/* SOMA_274_JS: acceso al interprete para que un programa anfitrion (jsrun) registre sus propias funciones nativas */
struct jrt *wjs_rt(struct wjs *js) { return &js->rt; }
int wjs_has_timers(struct wjs *js) { return js->rt.timers != 0 || js->rt.mhead != 0; }
struct dispargs { struct wnode *t; const char *type; int x, y, key, mods; const char *keystr; int res; };
static void do_dispatch(struct jrt *rt, void *u) { struct dispargs *d = (struct dispargs *)u; d->res = js_dom_dispatch(rt, d->t, d->type, d->x, d->y, d->key, d->keystr, d->mods); js_run_micro(rt); }
int wjs_dispatch(struct wjs *js, struct wnode *target, const char *type, int x, int y, int key, const char *keystr, int mods)
{
    struct dispargs d; d.t = target; d.type = type; d.x = x; d.y = y; d.key = key; d.keystr = keystr; d.mods = mods; d.res = 0;
    pcall(js, do_dispatch, &d); return d.res;
}
static void do_load(struct jrt *rt, void *u) { (void)u; js_dom_fire_load(rt); js_run_micro(rt); }
void wjs_fire_load(struct wjs *js) { pcall(js, do_load, 0); }
int wjs_heap_used(struct wjs *js) { uint32_t t = js_heap_total(&js->rt); return t > 0x7FFFFFFFu ? 0x7FFFFFFF : (int)t; }
int wjs_halted(struct wjs *js) { return js->rt.halted; }
void wjs_gc_stats(struct wjs *js, uint32_t *collections, uint32_t *live_bytes) { if (collections) *collections = js_gc_count(&js->rt); if (live_bytes) *live_bytes = js_gc_live(&js->rt); }   /* SOMA 189 */
void wjs_set_url(struct wjs *js, const char *url) { if (js->rt.doc && js->rt.dom) js_dom_set_url(&js->rt, url); }
void wjs_set_script(struct wjs *js, struct wnode *n) { if (js->rt.doc) js_dom_set_script(&js->rt, n); }
