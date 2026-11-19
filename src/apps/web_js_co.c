/* SOMA_183: corrutinas del interprete JavaScript: generadores (function*), async/await de verdad y generadores asincronos.
 *
 * El interprete recorre el arbol de forma recursiva sobre la pila de C, asi que suspender una funcion a medias exige
 * otra pila de C por cada generador/funcion async viva. Cada corrutina tiene su pila (CO_STACK), su cadena de excepciones
 * (struct jtry) y su pila de ambitos; el cambio de contexto guarda solo los registros que el ABI exige conservar.
 * Solo x86_64 (el T480s): en otras arquitecturas el motor sigue como antes (await en sincrono, sin generadores). */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())

#if defined(__x86_64__)
#define SOMA_CO 1
#else
#define SOMA_CO 0
#endif

/* SOMA_188: con memoria grande (>= 2 GiB de montón) pilas de 1 MiB: Nuxt/Vue anidan muchas llamadas dentro de funciones async */
#define CO_STACK (web_big_mem() ? (1024u * 1024u) : (512u * 1024u))
#define CO_SLACK (56u * 1024u)            /* margen bajo stack_lo para funciones nativas (regex, JSON, DOM) */
#define CO_POOL_MAX 16
#define CO_BYTES_CAP (web_big_mem() ? (384u << 20) : (192u << 20))

enum { CS_NEW = 0, CS_SUSP, CS_RUN, CS_DONE };
enum { RES_YIELD = 0, RES_DONE, RES_THROW, RES_AWAIT };

struct jco {
    struct jco *next, *prev;            /* lista de corrutinas con pila */
    struct jrt *rt;
    char *stack; void *sp, *caller_sp;
    uint8_t state, kind, unwinding, busy;
    struct jtry *try_top; int depth; char *stop, *send;     /* contexto de la corrutina mientras esta suspendida */
    jobj *fn; jv thisv; jv *args; int argc;
    jv xfer; int xmode;                 /* lo que recibe al reanudarse */
    jv out; int out_kind;               /* lo que entrega al ceder o terminar */
    jv ret_val;                         /* valor de return() mientras se desenrolla */
    jobj *self, *prom, *queue;
};

char *g_co_lo, *g_co_hi;                /* pila de la corrutina en curso (web_stack_left() de browser.c) */

#if SOMA_CO
void soma_co_switch(void **save_sp, void *new_sp);
void soma_co_tramp(void);
__asm__(".pushsection .text\n"
        ".globl soma_co_switch\n"
        "soma_co_switch:\n"
        "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
        "  movq %rsp, (%rdi)\n"
        "  movq %rsi, %rsp\n"
        "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n"
        "  ret\n"
        ".globl soma_co_tramp\n"
        "soma_co_tramp:\n"
        "  movq %r12, %rdi\n"
        "  andq $-16, %rsp\n"
        "  call soma_co_main\n"
        "  ud2\n"
        ".popsection\n");
#endif

/* ------------------------------ pilas ------------------------------ */
static int co_alloc_stack(struct jrt *rt, struct jco *co)
{
#if SOMA_CO
    if (rt->co_npool) co->stack = rt->co_pool[--rt->co_npool];
    else {
        if (rt->co_bytes + CO_STACK > CO_BYTES_CAP) return 0;
        co->stack = (char *)kmalloc(CO_STACK); if (!co->stack) return 0;
        rt->co_bytes += CO_STACK;
    }
    co->prev = 0; co->next = rt->co_live; if (rt->co_live) rt->co_live->prev = co; rt->co_live = co;
    uintptr_t top = ((uintptr_t)co->stack + CO_STACK) & ~(uintptr_t)15;
    uint64_t *sp = (uint64_t *)(top - 64);          /* r15 r14 r13 r12 rbx rbp ret pad */
    for (int i = 0; i < 8; i++) sp[i] = 0;
    sp[3] = (uint64_t)(uintptr_t)co; sp[6] = (uint64_t)(uintptr_t)soma_co_tramp;
    co->sp = sp; co->stop = co->send = co->stack;   /* sin pila de ambitos propia: todos los ambitos van al monton */
    co->try_top = 0; co->depth = 0;
    return 1;
#else
    (void)rt; (void)co; return 0;
#endif
}
static void co_release(struct jrt *rt, struct jco *co)
{
    if (!co->stack) return;
    if (co->prev) co->prev->next = co->next; else rt->co_live = co->next;
    if (co->next) co->next->prev = co->prev;
    co->next = co->prev = 0;
    if (rt->co_npool < CO_POOL_MAX) rt->co_pool[rt->co_npool++] = co->stack;
    else { kfree(co->stack); rt->co_bytes -= CO_STACK; }
    co->stack = 0;
}
void js_co_free_all(struct jrt *rt)
{
    while (rt->co_live) { struct jco *c = rt->co_live; rt->co_live = c->next; if (c->stack) kfree(c->stack); c->stack = 0; }
    for (int i = 0; i < rt->co_npool; i++) kfree(rt->co_pool[i]);
    rt->co_npool = 0; rt->co_bytes = 0; rt->cur_co = 0;
}
/* SOMA 189: raices del recolector que viven en pilas de C. Entrega la pila en curso (desde cur_sp) y las suspendidas: las
 * corrutinas que esperan (desde su sp guardado) y quien reanudo a una corrutina en marcha (desde caller_sp). Devuelve 0 si no
 * reconoce la pila en curso (entonces no se libera nada). */
int js_co_gc_roots(struct jrt *rt, const char *cur_sp)
{
    const char *main_hi = rt->gc_main_hi; if (!main_hi) return 0;
#if SOMA_CO
    struct jco *in = 0;
    for (struct jco *c = rt->co_live; c; c = c->next) if (c->stack && cur_sp >= c->stack && cur_sp < c->stack + CO_STACK) { in = c; break; }
    if (in) js_gc_scan_stack(rt, cur_sp, in->stack + CO_STACK);
    else { if (!(cur_sp < main_hi && (size_t)(main_hi - cur_sp) < ((size_t)64 << 20))) return 0; js_gc_scan_stack(rt, cur_sp, main_hi); }
    for (struct jco *c = rt->co_live; c; c = c->next) {
        if (!c->stack) continue;
        if (c->state == CS_RUN) {
            const char *s = (const char *)c->caller_sp; if (!s) continue;
            struct jco *o = 0; for (struct jco *d = rt->co_live; d; d = d->next) if (d->stack && s >= d->stack && s < d->stack + CO_STACK) { o = d; break; }
            if (o) js_gc_scan_stack(rt, s, o->stack + CO_STACK);
            else if (s < main_hi && (size_t)(main_hi - s) < ((size_t)64 << 20)) js_gc_scan_stack(rt, s, main_hi);
        } else if (c->state == CS_SUSP || c->state == CS_NEW) {
            const char *s = (const char *)c->sp; if (s && s >= c->stack && s < c->stack + CO_STACK) js_gc_scan_stack(rt, s, c->stack + CO_STACK);
        }
    }
    return 1;
#else
    if (!(cur_sp < main_hi && (size_t)(main_hi - cur_sp) < ((size_t)64 << 20))) return 0;
    js_gc_scan_stack(rt, cur_sp, main_hi); return 1;
#endif
}
void js_co_reset(struct jrt *rt) { rt->cur_co = 0; rt->gen_unwind = 0; rt->co_body = 0; rt->co_sync = 0; g_co_lo = g_co_hi = 0; }
/* soma.jsco=off (y el modo seguro): sin corrutinas; el motor se comporta como antes del parche 183 (await en sincrono, sin generadores) */
int js_co_available(void)
{
#if SOMA_CO && !defined(WEB_HOST_TEST)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsco") ? 0 : 1;
    return v;
#elif defined(WEB_HOST_TEST)
    extern int g_jsco_off;                    /* las pruebas en el anfitrion lo activan para comprobar el modo sin corrutinas */
    return SOMA_CO && !g_jsco_off;
#else
    return SOMA_CO;
#endif
}

static struct jco *co_new(struct jrt *rt, int kind, jobj *fn, jv thisv, jv *args, int argc)
{
    struct jco *co = (struct jco *)js_alloc(rt, (uint32_t)sizeof *co);
    co->rt = rt; co->kind = (uint8_t)kind; co->fn = fn; co->thisv = thisv; co->argc = argc;
    if (argc > 0) { co->args = (jv *)js_alloc(rt, (uint32_t)argc * (uint32_t)sizeof(jv)); for (int i = 0; i < argc; i++) co->args[i] = args[i]; }
    return co;
}

/* ------------------------------ cambio de contexto ------------------------------ */
void soma_co_main(struct jco *co)
{
    struct jrt *rt = co->rt;
    struct jtry tb; tb.up = 0; tb.depth = 0; tb.sstop = rt->stop; tb.scope = 0; tb.exc = jundef();
    rt->try_top = &tb;
    jv result = jundef(); int kind = RES_DONE;
    if (__builtin_setjmp(tb.jb)) {
        if (rt->gen_unwind) { rt->gen_unwind = 0; result = co->ret_val; kind = RES_DONE; }   /* return() del generador */
        else { result = tb.exc; kind = RES_THROW; }
    } else {
        result = js_call_body(rt, co->fn, co->thisv, co->args, co->argc);
        kind = RES_DONE; rt->try_top = tb.up;
    }
    co->out = result; co->out_kind = kind; co->state = CS_DONE;
#if SOMA_CO
    soma_co_switch(&co->sp, co->caller_sp);
#endif
    for (;;) { }
}
/* entra (o reanuda) la corrutina; vuelve cuando cede o termina */
static void co_enter(struct jrt *rt, struct jco *co, int mode, jv val)
{
#if SOMA_CO
    struct jtry *s_try = rt->try_top; int s_depth = rt->depth; char *s_stop = rt->stop, *s_send = rt->send, *s_lo = rt->stack_lo;
    jv s_ret = rt->retval; jstr *s_brk = rt->brk_label; int s_scf = rt->sc_flag, s_gu = rt->gen_unwind, s_body = rt->co_body;
    jobj *s_nt = rt->new_target, *s_nat = rt->cur_native; int s_sync = rt->co_sync; struct jco *s_cur = rt->cur_co; char *s_glo = g_co_lo, *s_ghi = g_co_hi;
    co->xmode = mode; co->xfer = val;
    rt->cur_co = co; rt->try_top = co->try_top; rt->depth = co->depth; rt->stop = co->stop; rt->send = co->send;
    rt->stack_lo = co->stack + CO_SLACK; rt->gen_unwind = co->unwinding; rt->co_body = 0; rt->co_sync = 0;
    g_co_lo = co->stack; g_co_hi = co->stack + CO_STACK;
    co->state = CS_RUN;
    soma_co_switch(&co->caller_sp, co->sp);
    rt->cur_co = s_cur; rt->try_top = s_try; rt->depth = s_depth; rt->stop = s_stop; rt->send = s_send; rt->stack_lo = s_lo;
    rt->retval = s_ret; rt->brk_label = s_brk; rt->sc_flag = s_scf; rt->gen_unwind = s_gu; rt->co_body = s_body; rt->co_sync = s_sync;
    rt->new_target = s_nt; rt->cur_native = s_nat; g_co_lo = s_glo; g_co_hi = s_ghi;
#else
    (void)rt; (void)co; (void)mode; (void)val;
#endif
}
/* dentro de la corrutina: cede (kind) con un valor; devuelve lo que envien al reanudar */
static jv co_suspend(struct jrt *rt, struct jco *co, int kind, jv val)
{
    co->out = val; co->out_kind = kind;
    co->try_top = rt->try_top; co->depth = rt->depth; co->stop = rt->stop; co->send = rt->send; co->unwinding = (uint8_t)rt->gen_unwind;
    co->state = CS_SUSP;
#if SOMA_CO
    soma_co_switch(&co->sp, co->caller_sp);
#endif
    /* reanudada: co_enter ya reinstalo el contexto */
    if (co->xmode == 1) js_throw(rt, co->xfer);
    if (co->xmode == 2) { co->ret_val = co->xfer; rt->gen_unwind = 1; js_throw(rt, jundef()); }
    return co->xfer;
}

/* ------------------------------ generadores sincronos ------------------------------ */
jv js_gen_create(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc)
{
    if (!SOMA_CO) js_throw_err(rt, E_ERROR, "los generadores no estan disponibles en esta arquitectura");
    int async = (fo->u.fn.node->fl & NF_ASYNC) != 0;
    struct jco *co = co_new(rt, async ? 2 : 0, fo, thisv, args, argc);
    jobj *g = js_obj_new(rt, async ? rt->p_asyncgen : rt->p_generator); g->cls = JC_GEN; g->u.host.p = co; g->u.host.kind = co->kind; co->self = g;
    return jobjv(g);
}
static jv gen_resume(struct jrt *rt, struct jco *co, int mode, jv v)
{
    if (co->state == CS_RUN) js_throw_err(rt, E_TYPE, "El generador ya esta en ejecucion");
    if (co->state == CS_NEW && mode != 0) { co->state = CS_DONE; co->out = jundef(); }
    if (co->state == CS_DONE) {
        if (mode == 1) js_throw(rt, v);
        return js_make_iter_result(rt, mode == 2 ? v : jundef(), 1);
    }
    if (co->state == CS_NEW && !co_alloc_stack(rt, co)) js_throw_err(rt, E_RANGE, "demasiados generadores o funciones async vivos");
    co_enter(rt, co, mode, v);
    if (co->out_kind == RES_YIELD) return js_make_iter_result(rt, co->out, 0);
    co_release(rt, co);
    if (co->out_kind == RES_THROW) js_throw(rt, co->out);
    return js_make_iter_result(rt, co->out, 1);
}
/* ------------------------------ generadores asincronos ------------------------------ */
static void agen_pop(struct jco *co)
{
    jobj *q = co->queue; if (!q || q->alen < 3) return;
    for (uint32_t i = 3; i < q->alen; i++) q->el[i - 3] = q->el[i];
    q->alen -= 3;
}
/* procesa lo que la corrutina acaba de entregar; 0 = sigue esperando un await */
static int agen_handle(struct jrt *rt, struct jco *co)
{
    jobj *q = co->queue; jobj *P = q->alen >= 3 && q->el[2].t == JT_OBJ ? q->el[2].u.o : 0;
    switch (co->out_kind) {
    case RES_AWAIT: return 0;
    case RES_YIELD: co->busy = 0; agen_pop(co); if (P) js_promise_resolve(rt, P, js_make_iter_result(rt, co->out, 0)); return 1;
    case RES_DONE: co->busy = 0; agen_pop(co); co_release(rt, co); if (P) js_promise_resolve(rt, P, js_make_iter_result(rt, co->out, 1)); return 1;
    default: co->busy = 0; agen_pop(co); co_release(rt, co); if (P) js_promise_reject(rt, P, co->out); return 1;
    }
}
static void agen_drain(struct jrt *rt, struct jco *co)
{
    while (co->queue && co->queue->alen >= 3 && !co->busy) {
        jobj *q = co->queue; int mode = (int)f64_to_i64(q->el[0].u.n); jv v = q->el[1]; jobj *P = q->el[2].t == JT_OBJ ? q->el[2].u.o : 0;
        if (co->state == CS_NEW && mode != 0) co->state = CS_DONE;
        if (co->state == CS_DONE) {
            agen_pop(co);
            if (!P) continue;
            if (mode == 1) js_promise_reject(rt, P, v); else js_promise_resolve(rt, P, js_make_iter_result(rt, mode == 2 ? v : jundef(), 1));
            continue;
        }
        if (co->state == CS_NEW && !co_alloc_stack(rt, co)) { agen_pop(co); if (P) js_promise_reject(rt, P, jobjv(js_make_error(rt, E_RANGE, "demasiados generadores o funciones async vivos"))); continue; }
        co->busy = 1; co_enter(rt, co, mode, v);
        if (!agen_handle(rt, co)) return;
    }
}
/* ------------------------------ funciones async ------------------------------ */
static void async_after(struct jrt *rt, struct jco *co)
{
    if (co->out_kind == RES_AWAIT) return;
    co_release(rt, co);
    if (co->out_kind == RES_THROW) js_promise_reject(rt, co->prom, co->out); else js_promise_resolve(rt, co->prom, co->out);
}
/* 0 si no hay pila disponible (demasiadas funciones async vivas): el llamador la ejecuta como antes (await en sincrono) */
int js_async_try(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc, jv *out)
{
    if (!SOMA_CO || (rt->co_bytes + CO_STACK > CO_BYTES_CAP && !rt->co_npool)) return 0;
    struct jco *co = co_new(rt, 1, fo, thisv, args, argc);
    jobj *p = js_promise_new(rt); co->prom = p;
    jobj *g = js_obj_new(rt, 0); g->cls = JC_GEN; g->u.host.p = co; g->u.host.kind = 1; co->self = g;
    if (!co_alloc_stack(rt, co)) return 0;
    co_enter(rt, co, 0, jundef());
    async_after(rt, co);
    *out = jobjv(p); return 1;
}
/* continuacion de un await: se ejecuta como microtarea con el resultado de la promesa */
static void co_continue(struct jrt *rt, struct jco *co, int mode, jv v)
{
    if (co->state != CS_SUSP) return;
    co_enter(rt, co, mode, v);
    if (co->kind == 1) async_after(rt, co);
    else if (co->kind == 2) { if (agen_handle(rt, co)) agen_drain(rt, co); }
}
static jv await_cont(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *self = rt->cur_native; struct jco *co = (struct jco *)self->el[0].u.o->u.host.p;
    co_continue(rt, co, magic, ARG(0)); return jundef();
}
/* await real: devuelve 1 si estaba en una corrutina (resultado en *out) */
int js_co_await(struct jrt *rt, jobj *p, jv *out)
{
    struct jco *co = rt->cur_co;
    if (!co || (co->kind != 1 && co->kind != 2) || !SOMA_CO || rt->co_sync > 0) return 0;
    jobj *f1 = js_native(rt, 0, await_cont, 1, 0), *f2 = js_native(rt, 0, await_cont, 1, 1);
    f1->el = (jv *)js_alloc(rt, (uint32_t)sizeof(jv)); f1->alen = 1; f1->el[0] = jobjv(co->self);
    f2->el = (jv *)js_alloc(rt, (uint32_t)sizeof(jv)); f2->alen = 1; f2->el[0] = jobjv(co->self);
    js_promise_then(rt, p, jobjv(f1), jobjv(f2));
    *out = co_suspend(rt, co, RES_AWAIT, jundef());
    return 1;
}
/* ------------------------------ yield ------------------------------ */
jv js_co_yield(struct jrt *rt, jv v)
{
    struct jco *co = rt->cur_co;
    if (!co || (co->kind != 0 && co->kind != 2)) js_throw_err(rt, E_SYNTAX, "yield fuera de un generador");
    if (co->kind == 2) v = js_await(rt, v);
    return co_suspend(rt, co, RES_YIELD, v);
}
static int yield_item(struct jrt *rt, jv item, void *ud) { (void)ud; js_co_yield(rt, item); return 1; }
jv js_co_yield_star(struct jrt *rt, jv iterable)
{
    js_iterate(rt, iterable, yield_item, 0);
    return jundef();
}

/* ------------------------------ objetos Generator / AsyncGenerator ------------------------------ */
static jv gen_native(struct jrt *rt, jv thisv, jv *args, int argc, int magic)   /* 0 next, 1 throw, 2 return */
{
    if (thisv.t != JT_OBJ || thisv.u.o->cls != JC_GEN) js_throw_err(rt, E_TYPE, "metodo de generador llamado sobre un objeto incompatible");
    struct jco *co = (struct jco *)thisv.u.o->u.host.p;
    if (co->kind == 0) return gen_resume(rt, co, magic, ARG(0));
    jobj *P = js_promise_new(rt);
    if (!co->queue) co->queue = js_arr_new(rt, 6);
    js_arr_push(rt, co->queue, jint(magic)); js_arr_push(rt, co->queue, ARG(0)); js_arr_push(rt, co->queue, jobjv(P));
    if (!co->busy) agen_drain(rt, co);
    return jobjv(P);
}
static jv gen_self(struct jrt *rt, jv thisv, jv *args, int argc, int magic) { (void)rt; (void)args; (void)argc; (void)magic; return thisv; }
void js_co_init(struct jrt *rt)
{
    jobj *g = js_obj_new(rt, rt->p_object); rt->p_generator = g;
    js_method(rt, g, "next", gen_native, 1, 0); js_method(rt, g, "throw", gen_native, 1, 1); js_method(rt, g, "return", gen_native, 1, 2);
    js_method(rt, g, "@@iterator", gen_self, 0, 0);
    js_def(rt, g, js_intern_c(rt, "@@toStringTag"), jstrv(js_str_cstr(rt, "Generator")), JP_RO);
    jobj *a = js_obj_new(rt, rt->p_object); rt->p_asyncgen = a;
    js_method(rt, a, "next", gen_native, 1, 0); js_method(rt, a, "throw", gen_native, 1, 1); js_method(rt, a, "return", gen_native, 1, 2);
    js_method(rt, a, "@@asyncIterator", gen_self, 0, 0);
    js_def(rt, a, js_intern_c(rt, "@@toStringTag"), jstrv(js_str_cstr(rt, "AsyncGenerator")), JP_RO);
}
