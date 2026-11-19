/* SOMA_PARCHE_NAVEGADOR: Promise, Map, Set, temporizadores, bucle de eventos. */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())
static jobj *mkctor4(struct jrt *rt, const char *name, jnative f, int nargs, int magic, jobj *proto)
{
    jobj *c = js_native(rt, name, f, nargs, magic);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0); return c;
}
static jobj *capture(struct jrt *rt, jnative f, int magic, jv c0, jv c1, jv c2)
{
    jobj *n = js_native(rt, 0, f, 1, magic); n->el = (jv *)js_alloc(rt, 3 * (uint32_t)sizeof(jv)); n->alen = 3; n->el[0] = c0; n->el[1] = c1; n->el[2] = c2; return n;
}

/* ------------------------------ microtareas ------------------------------ */
void js_micro_enqueue(struct jrt *rt, jv fn, jv a, jv b, int kind, jobj *prom)
{
    struct jmicro *m = (struct jmicro *)js_alloc(rt, sizeof *m); m->fn = fn; m->a = a; m->b = b; m->kind = kind; m->prom = prom;
    if (rt->mtail) rt->mtail->next = m; else rt->mhead = m; rt->mtail = m;
}
static jv resolving_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic);
static void make_resolvers(struct jrt *rt, jobj *p, jv *res, jv *rej)
{
    jobj *flag = js_obj_new(rt, 0);
    *res = jobjv(capture(rt, resolving_fn, 0, jobjv(p), jobjv(flag), jundef())); *rej = jobjv(capture(rt, resolving_fn, 1, jobjv(p), jobjv(flag), jundef()));
}
static void run_job(struct jrt *rt, struct jmicro *m)
{
    struct jtry tb; tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
    if (__builtin_setjmp(tb.jb)) {
        rt->depth = tb.depth; rt->stop = tb.sstop;
        if (m->kind == 1 && m->prom) js_promise_reject(rt, m->prom, tb.exc);
        else if (m->kind == 2) js_promise_reject(rt, m->prom, tb.exc);
        else { char buf[240]; jstr *s = tb.exc.t == JT_OBJ ? js_inspect(rt, tb.exc, 1) : js_tostr(rt, tb.exc); wu_copy(buf, "Uncaught (en tarea): ", sizeof buf); wu_copy(buf + 21, s->d, sizeof buf - 21); js_log(rt, 2, buf); }
        return;
    }
    switch (m->kind) {
    case 0: js_call(rt, m->fn, jundef(), 0, 0); break;
    case 1: {
        int rejected = m->b.u.b;
        if (js_is_callable(m->fn)) { jv r = js_call(rt, m->fn, jundef(), &m->a, 1); if (m->prom) js_promise_resolve(rt, m->prom, r); }
        else if (m->prom) { if (rejected) js_promise_reject(rt, m->prom, m->a); else js_promise_resolve(rt, m->prom, m->a); }
        break;
    }
    case 2: { jv res, rej; make_resolvers(rt, m->prom, &res, &rej); jv a2[2] = { res, rej }; js_call(rt, m->fn, m->a, a2, 2); break; }
    }
    rt->try_top = tb.up;
}
static void report_unhandled(struct jrt *rt)
{
    if (!rt->unhandled) return;
    jobj *u = rt->unhandled; rt->unhandled = 0;
    for (uint32_t i = 0; i < u->alen; i++) {
        jobj *p = u->el[i].u.o; if (p->u.prom.handled & 1) continue;
        char buf[300]; jstr *s = p->u.prom.value.t == JT_OBJ ? js_inspect(rt, p->u.prom.value, 1) : js_tostr(rt, p->u.prom.value);
        wu_copy(buf, "Uncaught (in promise) ", sizeof buf); wu_copy(buf + 22, s->d, sizeof buf - 22); js_log(rt, 2, buf);
    }
}
static int run_one_micro(struct jrt *rt)
{
    struct jmicro *m = rt->mhead; if (!m) return 0;
    rt->mhead = m->next; if (!rt->mhead) rt->mtail = 0;
    run_job(rt, m); return 1;
}
void js_run_micro(struct jrt *rt)
{
    uint32_t guard = 0;
    while (run_one_micro(rt)) { if (++guard > 5000000) { rt->halted = 1; break; } }
    report_unhandled(rt);
}

/* ------------------------------ Promise ------------------------------ */
jobj *js_promise_new(struct jrt *rt) { jobj *p = js_obj_new(rt, rt->p_promise); p->cls = JC_PROMISE; return p; }
static void settle(struct jrt *rt, jobj *p, int state, jv v)
{
    if (p->u.prom.state) return;
    p->u.prom.state = state; p->u.prom.value = v;
    jv *r = p->el; uint32_t n = p->alen; p->el = 0; p->alen = p->acap = 0;
    for (uint32_t i = 0; i + 3 <= n; i += 3) {
        jv h = state == 1 ? r[i] : r[i + 1]; js_micro_enqueue(rt, h, v, jbool(state == 2), 1, r[i + 2].t == JT_OBJ ? r[i + 2].u.o : 0);
    }
    if (state == 2 && !(p->u.prom.handled & 1)) { if (!rt->unhandled) rt->unhandled = js_arr_new(rt, 4); js_arr_push(rt, rt->unhandled, jobjv(p)); }
}
void js_promise_reject(struct jrt *rt, jobj *p, jv v) { settle(rt, p, 2, v); }
void js_promise_resolve(struct jrt *rt, jobj *p, jv v)
{
    if (p->u.prom.state) return;
    if (v.t == JT_OBJ) {
        if (v.u.o == p) { settle(rt, p, 2, jobjv(js_make_error(rt, E_TYPE, "Una promesa no puede resolverse consigo misma"))); return; }
        jv th; struct jtry tb; tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
        if (__builtin_setjmp(tb.jb)) { rt->depth = tb.depth; rt->stop = tb.sstop; settle(rt, p, 2, tb.exc); return; }
        th = js_get(rt, v, rt->s_then); rt->try_top = tb.up;
        if (js_is_callable(th)) { js_micro_enqueue(rt, th, v, jundef(), 2, p); return; }
    }
    settle(rt, p, 1, v);
}
static jv resolving_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *self = rt->cur_native; jobj *p = self->el[0].u.o; jobj *flag = self->el[1].u.o;
    if (flag->flag) return jundef();
    flag->flag = 1;
    if (magic == 0) js_promise_resolve(rt, p, ARG(0)); else js_promise_reject(rt, p, ARG(0));
    return jundef();
}
jv js_promise_then(struct jrt *rt, jobj *p, jv onf, jv onr)
{
    jobj *d = js_promise_new(rt); p->u.prom.handled |= 1;
    if (p->u.prom.state == 0) { js_arr_push(rt, p, onf); js_arr_push(rt, p, onr); js_arr_push(rt, p, jobjv(d)); p->alen = p->alen; }
    else js_micro_enqueue(rt, p->u.prom.state == 1 ? onf : onr, p->u.prom.value, jbool(p->u.prom.state == 2), 1, d);
    return jobjv(d);
}
static jobj *to_promise(struct jrt *rt, jv v)
{
    if (v.t == JT_OBJ && v.u.o->cls == JC_PROMISE) return v.u.o;
    jobj *p = js_promise_new(rt); js_promise_resolve(rt, p, v); return p;
}
enum { P_THEN, P_CATCH, P_FINALLY, P_RESOLVE, P_REJECT, P_ALL, P_ALLSETTLED, P_RACE, P_ANY, P_WITHRESOLVERS };
static jv promise_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor Promise requiere 'new'");
    jobj *proto = rt->p_promise; { jv pr = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (pr.t == JT_OBJ) proto = pr.u.o; rt->new_target = 0; }
    if (!js_is_callable(ARG(0))) js_throw_err(rt, E_TYPE, "El ejecutor de Promise no es una funcion");
    jobj *p = js_promise_new(rt); p->proto = proto; jv res, rej; make_resolvers(rt, p, &res, &rej);
    struct jtry tb; tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
    if (__builtin_setjmp(tb.jb)) { rt->depth = tb.depth; rt->stop = tb.sstop; jv a1[1] = { tb.exc }; js_call(rt, rej, jundef(), a1, 1); return jobjv(p); }
    jv a2[2] = { res, rej }; js_call(rt, args[0], jundef(), a2, 2); rt->try_top = tb.up;
    return jobjv(p);
}
static jv ret_const(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *self = rt->cur_native; if (magic) js_throw(rt, self->el[0]); return self->el[0];
}
static jv finally_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *self = rt->cur_native; jv fn = self->el[0]; jv val = ARG(0);
    jv r = js_call(rt, fn, jundef(), 0, 0); jobj *rp = to_promise(rt, r);
    return js_promise_then(rt, rp, jobjv(capture(rt, ret_const, magic, val, jundef(), jundef())), jundef());
}
static jv promise_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_OBJ || thisv.u.o->cls != JC_PROMISE) js_throw_err(rt, E_TYPE, "Metodo de Promise llamado sobre un objeto incompatible");
    jobj *p = thisv.u.o;
    switch (magic) {
    case P_THEN: return js_promise_then(rt, p, ARG(0), ARG(1));
    case P_CATCH: return js_promise_then(rt, p, jundef(), ARG(0));
    case P_FINALLY: {
        jv fn = ARG(0); if (!js_is_callable(fn)) return js_promise_then(rt, p, fn, fn);
        return js_promise_then(rt, p, jobjv(capture(rt, finally_fn, 0, fn, jundef(), jundef())), jobjv(capture(rt, finally_fn, 1, fn, jundef(), jundef())));
    }
    }
    return jundef();
}
/* combinadores: estado en un array [resultados, pendientes, promesa, modo] */
static jv combo_item(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *self = rt->cur_native; jobj *st = self->el[0].u.o; uint32_t idx = (uint32_t)f64_to_i64(self->el[1].u.n); jobj *flag = self->el[2].u.o;
    if (flag->flag) return jundef();
    flag->flag = 1;
    jobj *res = st->el[0].u.o; jobj *rp = st->el[2].u.o; int mode = (int)f64_to_i64(st->el[3].u.n);
    jv v = ARG(0);
    if (mode == P_ALLSETTLED) { jobj *o = js_obj_new(rt, rt->p_object); js_set_c(rt, o, "status", jstrv(js_intern_c(rt, magic ? "rejected" : "fulfilled"))); js_set_c(rt, o, magic ? "reason" : "value", v); v = jobjv(o); }
    res->el[idx] = v;
    int rem = (int)f64_to_i64(st->el[1].u.n) - 1; st->el[1] = jint(rem);
    if (mode == P_ANY) {
        if (!magic) { js_promise_resolve(rt, rp, v); return jundef(); }
        if (rem == 0) { jobj *e = js_make_error(rt, E_ERROR, "Todas las promesas fueron rechazadas"); js_def(rt, e, js_intern_c(rt, "errors"), jobjv(res), 0); js_promise_reject(rt, rp, jobjv(e)); }
        return jundef();
    }
    if (mode == P_ALL && magic) { js_promise_reject(rt, rp, v); return jundef(); }
    if (rem == 0) js_promise_resolve(rt, rp, jobjv(res));
    return jundef();
}
struct comboctx { jobj *st; int mode; uint32_t n; };
static int combo_cb(struct jrt *rt, jv item, void *ud)
{
    struct comboctx *c = (struct comboctx *)ud; uint32_t i = c->n++; jobj *res = c->st->el[0].u.o; js_arr_push(rt, res, jundef());
    c->st->el[1] = jint(f64_to_i64(c->st->el[1].u.n) + 1);
    jobj *p = to_promise(rt, item); jobj *flag = js_obj_new(rt, 0); jobj *rp = c->st->el[2].u.o;
    if (c->mode == P_RACE) { jv res2, rej2; make_resolvers(rt, rp, &res2, &rej2); js_promise_then(rt, p, res2, rej2); return 1; }
    jv ok = jobjv(capture(rt, combo_item, 0, jobjv(c->st), jint(i), jobjv(flag))), bad = jobjv(capture(rt, combo_item, 1, jobjv(c->st), jint(i), jobjv(flag)));
    js_promise_then(rt, p, ok, bad);
    return 1;
}
static jv promise_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    switch (magic) {
    case P_RESOLVE: return jobjv(to_promise(rt, ARG(0)));
    case P_REJECT: { jobj *p = js_promise_new(rt); js_promise_reject(rt, p, ARG(0)); return jobjv(p); }
    case P_WITHRESOLVERS: { jobj *p = js_promise_new(rt); jv res, rej; make_resolvers(rt, p, &res, &rej); jobj *o = js_obj_new(rt, rt->p_object); js_set_c(rt, o, "promise", jobjv(p)); js_set_c(rt, o, "resolve", res); js_set_c(rt, o, "reject", rej); return jobjv(o); }
    default: {
        jobj *rp = js_promise_new(rt); jobj *st = js_arr_new(rt, 4); js_arr_push(rt, st, jobjv(js_arr_new(rt, 4))); js_arr_push(rt, st, jint(1)); js_arr_push(rt, st, jobjv(rp)); js_arr_push(rt, st, jint(magic));
        struct comboctx c = { st, magic, 0 };
        js_iterate(rt, ARG(0), combo_cb, &c);
        int rem = (int)f64_to_i64(st->el[1].u.n) - 1; st->el[1] = jint(rem);
        if (rem == 0) {
            if (magic == P_ANY) { jobj *e = js_make_error(rt, E_ERROR, "Todas las promesas fueron rechazadas"); js_def(rt, e, js_intern_c(rt, "errors"), st->el[0], 0); js_promise_reject(rt, rp, jobjv(e)); }
            else if (magic != P_RACE) js_promise_resolve(rt, rp, st->el[0]);
        }
        return jobjv(rp);
    }
    }
}

/* ------------------------------ await ------------------------------ */
static int run_next_timer(struct jrt *rt, int force);
jv js_await(struct jrt *rt, jv v)
{
    if (rt->timer_depth > 20) js_throw_err(rt, E_RANGE, "await anidado en exceso");
    jobj *p;
    if (v.t == JT_OBJ && v.u.o->cls == JC_PROMISE) p = v.u.o;
    else if (v.t == JT_OBJ && js_is_callable(js_get(rt, v, rt->s_then))) p = to_promise(rt, v);
    else if (rt->cur_co && js_co_available()) p = to_promise(rt, v);   /* SOMA_183: await de un valor corriente tambien cede un turno de microtareas */
    else return v;
    p->u.prom.handled |= 1;
    { jv res; if (js_co_await(rt, p, &res)) return res; }   /* SOMA_183: dentro de una corrutina el await suspende de verdad */
    rt->timer_depth++;
    uint32_t guard = 0;
    while (p->u.prom.state == 0) {
        if (run_one_micro(rt)) continue;
        if (run_next_timer(rt, 1)) { if (++guard > 1000000) break; continue; }
        break;
    }
    rt->timer_depth--;
    if (p->u.prom.state == 1) return p->u.prom.value;
    if (p->u.prom.state == 2) js_throw(rt, p->u.prom.value);
    js_throw_err(rt, E_ERROR, "await sobre una promesa que nunca se resuelve");
    return jundef();
}

/* ------------------------------ Map / Set ------------------------------ */
enum { MP_GET, MP_SET, MP_HAS, MP_DELETE, MP_CLEAR, MP_SIZE, MP_FOREACH, MP_KEYS, MP_VALUES, MP_ENTRIES, SP_ADD };
/* SOMA 189: Map/Set guardan las entradas en orden en el array el[]; buscar era lineal (17 % del tiempo en Google). Con mas de 16
 * entradas se mantiene un indice de dispersion clave -> posicion. Anadir lo amplia; borrar lo invalida y se rehace al buscar. */
static uint32_t key_hash(jv k)
{
    switch (k.t) {
    case JT_STR: return k.u.s->hash * 2654435761u + 1;
    case JT_NUM: { f64 n = k.u.n; if (f64_abs(n) == 0) n = 0; if (f64_is_nan(n)) n = 0x7FF8000000000000ULL; return (uint32_t)((n ^ (n >> 29)) * 0x9E3779B97F4A7C15ULL >> 32); }
    case JT_OBJ: if (k.u.o->cls == JC_BIGINT) return (uint32_t)(js_big_low64(k) * 0x9E3779B97F4A7C15ULL >> 32); return (uint32_t)(((uintptr_t)k.u.o >> 4) * 2654435761u);
    case JT_BOOL: return k.u.b ? 0x51ED270Bu : 0x2545F491u;
    default: return k.t * 0x9E3779B1u;
    }
}
static int find_key(struct jrt *rt, jobj *m, jv k, int stride)
{
    uint32_t n = m->alen / (uint32_t)stride;
    if (n <= 16) { m->u.map.h = 0; for (uint32_t i = 0; i < m->alen; i += (uint32_t)stride) if (js_same_zero(m->el[i], k)) return (int)i; return -1; }
    if (!m->u.map.h || m->u.map.hcap < n * 2u) {
        uint32_t hc = 64; while (hc < n * 4u) hc *= 2;
        m->u.map.h = (uint32_t *)js_alloc(rt, hc * (uint32_t)sizeof(uint32_t)); m->u.map.hcap = hc; m->u.map.hcount = 0;
    } else if (m->u.map.hcount > n) { memset(m->u.map.h, 0, (size_t)m->u.map.hcap * sizeof(uint32_t)); m->u.map.hcount = 0; }   /* hubo un borrado: se rehace */
    uint32_t *h = m->u.map.h, mask = m->u.map.hcap - 1;
    for (uint32_t e = m->u.map.hcount; e < n; e++) { uint32_t p = key_hash(m->el[e * (uint32_t)stride]) & mask; while (h[p]) p = (p + 1) & mask; h[p] = e + 1; }
    m->u.map.hcount = n;
    for (uint32_t p = key_hash(k) & mask; h[p]; p = (p + 1) & mask) { uint32_t i = (h[p] - 1) * (uint32_t)stride; if (js_same_zero(m->el[i], k)) return (int)i; }
    return -1;
}
jv js_make_iterator(struct jrt *rt, jv src, int kind);
struct mapinit { jobj *m; int is_map; };
static int mapinit_cb(struct jrt *rt, jv item, void *ud)
{
    struct mapinit *mi = (struct mapinit *)ud;
    if (mi->is_map) {
        if (item.t != JT_OBJ) js_throw_err(rt, E_TYPE, "El elemento del iterable no es un par clave-valor");
        jv k = js_getv(rt, item, jint(0)), v = js_getv(rt, item, jint(1)); int ix = find_key(rt, mi->m, k, 2);
        if (ix >= 0) mi->m->el[ix + 1] = v; else { js_arr_push(rt, mi->m, k); js_arr_push(rt, mi->m, v); }
    } else if (find_key(rt, mi->m, item, 1) < 0) js_arr_push(rt, mi->m, item);
    return 1;
}
static jv mapset_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)   /* 0 Map, 1 Set, 2 WeakMap, 3 WeakSet */
{
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor requiere 'new'");
    jobj *proto = (magic & 1) ? rt->p_set : rt->p_map; { jv pr = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (pr.t == JT_OBJ) proto = pr.u.o; rt->new_target = 0; }
    jobj *m = js_obj_new(rt, proto); m->cls = (magic & 1) ? JC_SET : JC_MAP; m->flag = (uint8_t)(magic >> 1);
    if (!IS_NULLISH(ARG(0))) { struct mapinit mi; mi.m = m; mi.is_map = !(magic & 1); js_iterate(rt, args[0], mapinit_cb, &mi); }
    return jobjv(m);
}
static jv mapset_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_OBJ || (thisv.u.o->cls != JC_MAP && thisv.u.o->cls != JC_SET)) js_throw_err(rt, E_TYPE, "Metodo de Map/Set llamado sobre un objeto incompatible");
    jobj *m = thisv.u.o; int is_map = m->cls == JC_MAP; int st = is_map ? 2 : 1;
    switch (magic) {
    case MP_GET: { int i = find_key(rt, m, ARG(0), st); return i >= 0 ? m->el[i + 1] : jundef(); }
    case MP_SET: { if (m->flag && ARG(0).t != JT_OBJ) js_throw_err(rt, E_TYPE, "Clave de WeakMap no valida"); int i = find_key(rt, m, ARG(0), 2); if (i >= 0) m->el[i + 1] = ARG(1); else { js_arr_push(rt, m, ARG(0)); js_arr_push(rt, m, ARG(1)); } return thisv; }
    case SP_ADD: if (find_key(rt, m, ARG(0), 1) < 0) js_arr_push(rt, m, ARG(0)); return thisv;
    case MP_HAS: return jbool(find_key(rt, m, ARG(0), st) >= 0);
    case MP_DELETE: { int i = find_key(rt, m, ARG(0), st); if (i < 0) return jbool(0); m->u.map.hcount = 0xFFFFFFFFu; memmove(&m->el[i], &m->el[i + st], (m->alen - (uint32_t)i - (uint32_t)st) * sizeof(jv)); m->alen -= (uint32_t)st; return jbool(1); }
    case MP_CLEAR: m->alen = 0; m->u.map.h = 0; m->u.map.hcount = 0; return jundef();
    case MP_SIZE: return jint(m->alen / (uint32_t)st);
    case MP_FOREACH:
        if (!js_is_callable(ARG(0))) js_throw_err(rt, E_TYPE, "El callback no es una funcion");
        for (uint32_t i = 0; i < m->alen; i += (uint32_t)st) { jv cb[3] = { is_map ? m->el[i + 1] : m->el[i], m->el[i], thisv }; js_call(rt, args[0], ARG(1), cb, 3); }
        return jundef();
    case MP_KEYS: return js_make_iterator(rt, thisv, 1);
    case MP_VALUES: return js_make_iterator(rt, thisv, 0);
    case MP_ENTRIES: return js_make_iterator(rt, thisv, 2);
    }
    return jundef();
}

/* ------------------------------ temporizadores ------------------------------ */
enum { T_TIMEOUT, T_INTERVAL, T_CLEAR, T_RAF, T_IDLE };
static void timer_unlink(struct jrt *rt, int id)
{
    struct jtimer *prev = 0;
    for (struct jtimer *t = rt->timers; t; prev = t, t = t->next) if (t->id == id) {
        if (prev) prev->next = t->next; else rt->timers = t->next;
        if (rt->timers_tail == t) rt->timers_tail = prev;
        return;
    }
}
static jv timer_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic == T_CLEAR) { if (IS_NUM(ARG(0))) timer_unlink(rt, (int)f64_to_i64(args[0].u.n)); return jundef(); }
    jv fn = ARG(0);
    if (!js_is_callable(fn) && fn.t != JT_STR) js_throw_err(rt, E_TYPE, "El callback del temporizador no es una funcion");
    int64_t d = (magic == T_RAF) ? 16 : (magic == T_IDLE ? 1 : f64_to_i64(js_tonum(rt, ARG(1)))); if (d < 0 || d > 0x7fffffff) d = 0; if (magic <= T_INTERVAL && d < 1) d = 1;
    struct jtimer *t = (struct jtimer *)js_alloc(rt, sizeof *t); t->id = ++rt->next_timer; t->due = web_now_ms() + (uint64_t)d; t->repeat = magic == T_INTERVAL; t->interval = (uint32_t)(d < 4 ? 4 : d); t->fn = fn;
    if (magic <= T_INTERVAL && argc > 2) { t->nargs = argc - 2; t->args = (jv *)js_alloc(rt, (uint32_t)t->nargs * (uint32_t)sizeof(jv)); for (int i = 0; i < t->nargs; i++) t->args[i] = args[2 + i]; }
    if (magic == T_RAF || magic == T_IDLE) { t->nargs = 1; t->args = (jv *)js_alloc(rt, sizeof(jv)); t->args[0] = jnum(f64_from_i64((int64_t)web_now_ms())); }
    if (rt->timers_tail) rt->timers_tail->next = t; else rt->timers = t; rt->timers_tail = t;
    return jint(t->id);
}
static void fire_timer(struct jrt *rt, struct jtimer *t)
{
    struct jtry tb; tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
    if (__builtin_setjmp(tb.jb)) {
        rt->depth = tb.depth; rt->stop = tb.sstop; char buf[300]; jstr *s = tb.exc.t == JT_OBJ ? js_inspect(rt, tb.exc, 1) : js_tostr(rt, tb.exc);
        wu_copy(buf, "Uncaught ", sizeof buf); wu_copy(buf + 9, s->d, sizeof buf - 9); js_log(rt, 2, buf); return;
    }
    if (t->fn.t == JT_STR) { int ok; js_eval_string(rt, t->fn.u.s->d, t->fn.u.s->len, &ok); }
    else js_call(rt, t->fn, jobjv(rt->global), t->args, t->nargs);
    rt->try_top = tb.up;
}
/* ejecuta el temporizador mas antiguo (force: sin esperar a su hora). Devuelve 1 si ejecuto algo */
static int run_next_timer(struct jrt *rt, int force)
{
    struct jtimer *best = 0; uint64_t now = web_now_ms();
    for (struct jtimer *t = rt->timers; t; t = t->next) if (!best || t->due < best->due) best = t;
    if (!best || (!force && best->due > now)) return 0;
    if (best->repeat) { best->due = (force ? best->due : now) + best->interval; if (force) best->due = now + best->interval; } else timer_unlink(rt, best->id);
    struct jtimer copy = *best; fire_timer(rt, &copy);
    js_run_micro(rt); return 1;
}
int js_pump_timers(struct jrt *rt, int *ran)
{
    uint64_t now = web_now_ms(); int guard = 0;
    rt->task_start = web_now_ms();
    js_run_micro(rt);
    while (guard++ < 200) {
        struct jtimer *best = 0; for (struct jtimer *t = rt->timers; t; t = t->next) if (!best || t->due < best->due) best = t;
        if (!best || best->due > now) break;
        run_next_timer(rt, 0); if (ran) (*ran)++;
        if (rt->halted) break;
    }
    struct jtimer *best = 0; for (struct jtimer *t = rt->timers; t; t = t->next) if (!best || t->due < best->due) best = t;
    if (!best) return -1;
    now = web_now_ms(); return best->due <= now ? 0 : (int)(best->due - now);
}

void js_lib_async_init(struct jrt *rt)
{
    jobj *pc = mkctor4(rt, "Promise", promise_ctor, 1, 0, rt->p_promise);
    js_method(rt, rt->p_promise, "then", promise_proto, 2, P_THEN); js_method(rt, rt->p_promise, "catch", promise_proto, 1, P_CATCH); js_method(rt, rt->p_promise, "finally", promise_proto, 1, P_FINALLY);
    static const struct { const char *n; int m; } PS[] = { { "resolve", P_RESOLVE }, { "reject", P_REJECT }, { "all", P_ALL }, { "allSettled", P_ALLSETTLED }, { "race", P_RACE }, { "any", P_ANY }, { "withResolvers", P_WITHRESOLVERS }, { 0, 0 } };
    for (int i = 0; PS[i].n; i++) js_method(rt, pc, PS[i].n, promise_static, 1, PS[i].m);
    mkctor4(rt, "Map", mapset_ctor, 0, 0, rt->p_map); mkctor4(rt, "Set", mapset_ctor, 0, 1, rt->p_set);
    jobj *wm = js_obj_new(rt, rt->p_map), *ws = js_obj_new(rt, rt->p_set); mkctor4(rt, "WeakMap", mapset_ctor, 0, 2, wm); mkctor4(rt, "WeakSet", mapset_ctor, 0, 3, ws);
    static const struct { const char *n; int m; } MM[] = { { "get", MP_GET }, { "set", MP_SET }, { "has", MP_HAS }, { "delete", MP_DELETE }, { "clear", MP_CLEAR }, { "forEach", MP_FOREACH }, { "keys", MP_KEYS }, { "values", MP_VALUES }, { "entries", MP_ENTRIES }, { 0, 0 } };
    for (int i = 0; MM[i].n; i++) js_method(rt, rt->p_map, MM[i].n, mapset_proto, 1, MM[i].m);
    js_getter(rt, rt->p_map, "size", mapset_proto, 0, MP_SIZE);
    js_def(rt, rt->p_map, js_intern_c(rt, "@@iterator"), js_get_c(rt, jobjv(rt->p_map), "entries"), 0);
    js_method(rt, rt->p_set, "add", mapset_proto, 1, SP_ADD); js_method(rt, rt->p_set, "has", mapset_proto, 1, MP_HAS); js_method(rt, rt->p_set, "delete", mapset_proto, 1, MP_DELETE); js_method(rt, rt->p_set, "clear", mapset_proto, 0, MP_CLEAR);
    js_method(rt, rt->p_set, "forEach", mapset_proto, 1, MP_FOREACH); js_method(rt, rt->p_set, "values", mapset_proto, 0, MP_VALUES); js_method(rt, rt->p_set, "keys", mapset_proto, 0, MP_VALUES); js_method(rt, rt->p_set, "entries", mapset_proto, 0, MP_ENTRIES);
    js_getter(rt, rt->p_set, "size", mapset_proto, 0, MP_SIZE); js_def(rt, rt->p_set, js_intern_c(rt, "@@iterator"), js_get_c(rt, jobjv(rt->p_set), "values"), 0);
    js_method(rt, wm, "get", mapset_proto, 1, MP_GET); js_method(rt, wm, "set", mapset_proto, 2, MP_SET); js_method(rt, wm, "has", mapset_proto, 1, MP_HAS); js_method(rt, wm, "delete", mapset_proto, 1, MP_DELETE);
    js_method(rt, ws, "add", mapset_proto, 1, SP_ADD); js_method(rt, ws, "has", mapset_proto, 1, MP_HAS); js_method(rt, ws, "delete", mapset_proto, 1, MP_DELETE);
    static const struct { const char *n; int m; } TS[] = { { "setTimeout", T_TIMEOUT }, { "setInterval", T_INTERVAL }, { "clearTimeout", T_CLEAR }, { "clearInterval", T_CLEAR }, { "requestAnimationFrame", T_RAF }, { "cancelAnimationFrame", T_CLEAR }, { "requestIdleCallback", T_IDLE }, { "cancelIdleCallback", T_CLEAR }, { "setImmediate", T_IDLE }, { 0, 0 } };
    for (int i = 0; TS[i].n; i++) js_method(rt, rt->global, TS[i].n, timer_fn, 2, TS[i].m);
}
