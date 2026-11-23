/* SOMA_PARCHE_NAVEGADOR: nucleo del interprete JavaScript (valores, objetos, ambitos, ejecucion). */
#include "web_js_i.h"
#include "web_fault.h"
#include <stdarg.h>

enum { C_NORMAL = 0, C_BREAK, C_CONTINUE, C_RETURN };

/* ------------------------------ memoria y cadenas ------------------------------ */
static void *alloc2(struct jrt *rt, uint32_t n, int atomic)
{
    void *p = js_gc_alloc(rt, n, atomic);           /* SOMA 189: monton con recolector (antes una arena que solo crecia) */
    if (!p) {                                       /* sin memoria real: no se puede ni construir el error */
        rt->halted = 1; wu_copy(rt->errbuf, "memoria agotada", sizeof rt->errbuf);
        js_throw(rt, jundef());
    }
    uint32_t tot = js_heap_total(rt);
    if (tot > rt->heap_cap) {
        /* SOMA 188B: la primera vez solo falla el script en curso (RangeError) y queda una reserva de 1/4 del tope para lo que ya
         * estaba en marcha. Agotada tambien la reserva, el interprete se detiene. (El recolector ya ha corrido antes de llegar aqui.) */
        if (!rt->heap_over) {
            rt->heap_over = 1; uint32_t extra = rt->heap_cap / 4; if (extra < (8u << 20)) extra = 8u << 20;
            rt->heap_cap = rt->heap_cap > 0xFFFFFFFFu - extra ? 0xFFFFFFFFu : rt->heap_cap + extra;
            if (rt->heap_cap < tot + (512u << 10)) rt->heap_cap = tot + (512u << 10);
            js_throw_err(rt, E_RANGE, "memoria del script agotada");
        }
        if (!rt->halted) { rt->halted = 1; wu_copy(rt->errbuf, "memoria del script agotada", sizeof rt->errbuf); }
        rt->heap_cap = tot + (512u << 10);  /* reserva para construir el error */
        js_throw_err(rt, E_RANGE, "memoria del script agotada");
    }
    return p;
}
void *js_alloc(struct jrt *rt, uint32_t n) { return alloc2(rt, n, 0); }
void *js_alloc_atomic(struct jrt *rt, uint32_t n) { return alloc2(rt, n, 1); }   /* bloque sin punteros (texto): el recolector no lo recorre */
static uint32_t hash_bytes(const char *s, uint32_t n) { return wu_hash(s, n); }
jstr *js_str_new(struct jrt *rt, const char *s, uint32_t n)
{
    if (n == 0 && rt->s_empty) return rt->s_empty;
    if (n == 1 && rt->s_chars[0] && (uint8_t)s[0] < 128) return rt->s_chars[(uint8_t)s[0]];
    jstr *r = (jstr *)js_alloc_atomic(rt, (uint32_t)sizeof(jstr) + n);
    r->len = n; memcpy(r->d, s, n); r->d[n] = 0; r->hash = hash_bytes(s, n); return r;
}
jstr *js_str_cstr(struct jrt *rt, const char *s) { return js_str_new(rt, s, wu_strlen(s)); }
int js_str_eq(const jstr *a, const jstr *b) { return a == b || (a->len == b->len && a->hash == b->hash && !memcmp(a->d, b->d, a->len)); }
static void itab_grow(struct jrt *rt)
{
    uint32_t nc = rt->itab_cap ? rt->itab_cap * 2 : 1024; jstr **nt = (jstr **)js_alloc(rt, nc * (uint32_t)sizeof(jstr *));
    for (uint32_t i = 0; i < rt->itab_cap; i++) { jstr *s = rt->itab[i]; if (!s) continue; uint32_t h = s->hash & (nc - 1); while (nt[h]) h = (h + 1) & (nc - 1); nt[h] = s; }
    rt->itab = nt; rt->itab_cap = nc;
}
jstr *js_intern(struct jrt *rt, jstr *s)
{
    if (s->interned) return s;
    if (rt->itab_n * 2 >= rt->itab_cap) itab_grow(rt);
    uint32_t h = s->hash & (rt->itab_cap - 1);
    while (rt->itab[h]) { jstr *t = rt->itab[h]; if (t->len == s->len && t->hash == s->hash && !memcmp(t->d, s->d, s->len)) return t; h = (h + 1) & (rt->itab_cap - 1); }
    s->interned = 1; rt->itab[h] = s; rt->itab_n++; return s;
}
jstr *js_intern_n(struct jrt *rt, const char *s, uint32_t n)
{
    if (rt->itab_cap) {
        uint32_t hh = hash_bytes(s, n), h = hh & (rt->itab_cap - 1);
        while (rt->itab[h]) { jstr *t = rt->itab[h]; if (t->len == n && t->hash == hh && !memcmp(t->d, s, n)) return t; h = (h + 1) & (rt->itab_cap - 1); }
    }
    return js_intern(rt, js_str_new(rt, s, n));
}
jstr *js_intern_c(struct jrt *rt, const char *s) { return js_intern_n(rt, s, wu_strlen(s)); }
jstr *js_str_concat(struct jrt *rt, jstr *a, jstr *b)
{
    if (!a->len) return b;
    if (!b->len) return a;
    if ((uint64_t)a->len + b->len > (64u << 20)) js_throw_err(rt, E_RANGE, "cadena demasiado larga");
    if (a->len >= 3 && b->len >= 3 && (uint8_t)a->d[a->len - 3] == 0xED && ((uint8_t)a->d[a->len - 2] & 0xF0) == 0xA0 && (uint8_t)b->d[0] == 0xED && ((uint8_t)b->d[1] & 0xF0) == 0xB0 && js_u16_on()) {
        /* SOMA_185: mitad alta suelta + mitad baja suelta = un caracter fuera del plano basico (4 bytes en lugar de 3 + 3) */
        uint32_t hi = 0xD000u | ((uint32_t)((uint8_t)a->d[a->len - 2] & 0x3F) << 6) | ((uint8_t)a->d[a->len - 1] & 0x3Fu);
        uint32_t lo = 0xD000u | ((uint32_t)((uint8_t)b->d[1] & 0x3F) << 6) | ((uint8_t)b->d[2] & 0x3Fu);
        uint32_t cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00); char t[4]; int n = wu_utf8_put(cp, t);
        uint32_t nl = a->len - 3 + (uint32_t)n + b->len - 3;
        jstr *r = (jstr *)js_alloc(rt, (uint32_t)sizeof(jstr) + nl);
        memcpy(r->d, a->d, a->len - 3); memcpy(r->d + a->len - 3, t, (uint32_t)n); memcpy(r->d + a->len - 3 + n, b->d + 3, b->len - 3);
        r->len = nl; r->d[nl] = 0; r->hash = hash_bytes(r->d, nl); return r;
    }
    jstr *r = (jstr *)js_alloc(rt, (uint32_t)sizeof(jstr) + a->len + b->len);
    r->len = a->len + b->len; memcpy(r->d, a->d, a->len); memcpy(r->d + a->len, b->d, b->len); r->d[r->len] = 0; r->hash = hash_bytes(r->d, r->len); return r;
}

/* ------------------------------ errores ------------------------------ */
void js_throw(struct jrt *rt, jv exc)
{
    struct jtry *t = rt->try_top;
    if (!t) { rt->last_exc = exc; rt->exc_pending = 1; rt->halted = 1; for (;;) web_yield(); }
    t->exc = exc; rt->try_top = t->up; __builtin_longjmp(t->jb, 1);
}
/* SOMA_188: traza al estilo de V8: "Nombre: mensaje\n    at f (script:linea)" con las funciones activas */
void js_stack_trace(struct jrt *rt, const char *head, struct wb *b)
{
    wb_add(b, head);
    const char *src = rt->cur_src ? rt->cur_src : "<anonymous>"; int line = rt->cur_line, shown = 0;
    for (int d = rt->depth - 1; d >= 0 && shown < 10; d--, shown++) {
        if (d >= 40) { line = 0; continue; }
        jstr *nm = rt->fstk[d];
        wb_add(b, "\n    at "); if (nm && nm->len) { wb_addn(b, nm->d, nm->len); wb_add(b, " ("); }
        wb_add(b, src); if (line > 0) { wb_addc(b, ':'); wb_addint(b, line); } if (nm && nm->len) wb_addc(b, ')');
        line = rt->fline[d];
    }
    if (!shown || rt->depth == 0) { wb_add(b, "\n    at "); wb_add(b, src); if (line > 0) { wb_addc(b, ':'); wb_addint(b, line); } }
}
jobj *js_make_error(struct jrt *rt, int kind, const char *msg)
{
    jobj *e = js_obj_new(rt, rt->p_errs[kind]); e->cls = JC_ERROR;
    js_def(rt, e, rt->s_message, jstrv(js_str_cstr(rt, msg)), 0);
    static const char *EN[] = { "Error", "TypeError", "ReferenceError", "RangeError", "SyntaxError", "EvalError", "URIError" };
    struct wb b = {0}; char h[240]; wu_copy(h, kind >= 0 && kind < 7 ? EN[kind] : "Error", sizeof h); uint32_t n = wu_strlen(h);
    if (msg && *msg && n + 3 < sizeof h) { h[n++] = ':'; h[n++] = ' '; wu_copy(h + n, msg, sizeof h - n); }
    if (rt->s_stack) { js_stack_trace(rt, h, &b); if (b.p) js_def(rt, e, rt->s_stack, jstrv(js_str_new(rt, b.p, b.len)), 0); }
    wb_free(&b);
    return e;
}
void js_throw_err(struct jrt *rt, int kind, const char *fmt, ...)
{
    char buf[200]; va_list ap; va_start(ap, fmt); uint32_t o = 0;
    for (const char *f = fmt; *f && o + 2 < sizeof buf; f++) {
        if (*f != '%') { buf[o++] = *f; continue; }
        f++;
        if (*f == 's') { const char *s = va_arg(ap, const char *); if (!s) s = "?"; while (*s && o + 2 < sizeof buf) buf[o++] = *s++; }
        else if (*f == 'd') { int v = va_arg(ap, int); char t[16]; wu_itoa(v, t); for (char *q = t; *q && o + 2 < sizeof buf; q++) buf[o++] = *q; }
        else buf[o++] = *f;
    }
    buf[o] = 0; va_end(ap);
    jobj *e = rt->p_errs[kind] ? js_make_error(rt, kind, buf) : 0;
    if (!e) { rt->halted = 1; }
    js_throw(rt, e ? jobjv(e) : jundef());
}
void js_check_stack(struct jrt *rt) { if ((char *)__builtin_frame_address(0) < rt->stack_lo) js_throw_err(rt, E_RANGE, "Maximum call stack size exceeded"); }
void js_step(struct jrt *rt)
{
    static uint64_t last_yield;          /* SOMA_PARCHE_028_UI: cesion por tiempo (los scripts no congelan la GUI) */
    rt->steps = 0;
    { uint64_t t = web_now_ms(); if (t - last_yield >= 4) { last_yield = t; web_yield(); } }
    if (!rt->halted && rt->task_start && web_now_ms() - rt->task_start > (rt->task_ms ? rt->task_ms : 12000u)) { rt->halted = 1; rt->timed_out = 1; wu_copy(rt->errbuf, "tiempo de ejecucion del script excedido", sizeof rt->errbuf); }   /* SOMA_274_JS: limite configurable */
    if (rt->halted) {                               /* script detenido: salta al marco externo saltandose los try/catch del script */
        if (rt->base_try) rt->try_top = rt->base_try;
        js_throw_err(rt, E_ERROR, "%s", rt->errbuf[0] ? rt->errbuf : "script detenido");
    }
}

/* ------------------------------ objetos ------------------------------ */
jobj *js_obj_new(struct jrt *rt, jobj *proto)
{
    jobj *o = (jobj *)js_alloc(rt, sizeof *o); o->proto = proto; o->extensible = 1; return o;
}
jobj *js_arr_new(struct jrt *rt, uint32_t cap)
{
    jobj *a = js_obj_new(rt, rt->p_array); a->cls = JC_ARRAY;
    if (cap) { a->el = (jv *)js_alloc(rt, cap * (uint32_t)sizeof(jv)); a->acap = cap; }
    return a;
}
static void arr_reserve(struct jrt *rt, jobj *a, uint32_t need)
{
    if (need <= a->acap) return;
    if (need > (8u << 20)) js_throw_err(rt, E_RANGE, "array demasiado grande");
    uint32_t nc = a->acap ? a->acap * 2 : 8; while (nc < need) nc *= 2;
    jv *ne = (jv *)js_alloc(rt, nc * (uint32_t)sizeof(jv)); if (a->alen) memcpy(ne, a->el, a->alen * sizeof(jv));
    a->el = ne; a->acap = nc;
}
void js_arr_push(struct jrt *rt, jobj *a, jv v) { arr_reserve(rt, a, a->alen + 1); if (IS_HOLE(v)) v.u.n = 0; a->el[a->alen++] = v; }
void js_arr_push_raw(struct jrt *rt, jobj *a, jv v) { arr_reserve(rt, a, a->alen + 1); a->el[a->alen++] = v; }   /* conserva huecos (concat, slice, map) */
jobj *js_array_from(struct jrt *rt, jv *items, uint32_t n)
{
    jobj *a = js_arr_new(rt, n); if (n) { memcpy(a->el, items, n * sizeof(jv)); a->alen = n; } return a;
}
static void idx_rebuild(struct jrt *rt, jobj *o)
{
    uint32_t nc = 16; while (nc < o->np * 2 + 2) nc *= 2;
    uint32_t *ix = (uint32_t *)js_alloc(rt, nc * 4);
    for (uint32_t i = 0; i < o->np; i++) { uint32_t h = (o->props[i].key->hash) & (nc - 1); while (ix[h]) h = (h + 1) & (nc - 1); ix[h] = i + 1; }
    o->idx = ix; o->icap = nc;
}
struct jprop *js_find_own(jobj *o, jstr *key)
{
    if (o->idx) {
        uint32_t h = key->hash & (o->icap - 1);
        while (o->idx[h]) { struct jprop *p = &o->props[o->idx[h] - 1]; if (p->key == key) return p; h = (h + 1) & (o->icap - 1); }
        return 0;
    }
    for (uint32_t i = 0; i < o->np; i++) if (o->props[i].key == key) return &o->props[i];
    return 0;
}
static struct jprop *add_prop(struct jrt *rt, jobj *o, jstr *key)
{

#ifdef WJS_DEBUG_COL
    if (o == rt->p_object && rt->cur_line) { extern int fprintf(void *, const char *, ...); extern void *stderr; fprintf(stderr, "[proto] Object.prototype.%s en l:%d:%d\n", key->d, rt->cur_line, rt->cur_col); }
#endif
    if (!key->interned) key = js_intern(rt, key);
    if (o->np >= o->cap) {
        uint32_t nc = o->cap ? o->cap * 2 : 4; struct jprop *np = (struct jprop *)js_alloc(rt, nc * (uint32_t)sizeof(struct jprop));
        if (o->np) memcpy(np, o->props, o->np * sizeof(struct jprop));
        o->props = np; o->cap = nc;
    }
    struct jprop *p = &o->props[o->np++]; memset(p, 0, sizeof *p); p->key = key; p->flags = JP_ENUM;
    if (o->idx) { if (o->np * 2 > o->icap) idx_rebuild(rt, o); else { uint32_t h = key->hash & (o->icap - 1); while (o->idx[h]) h = (h + 1) & (o->icap - 1); o->idx[h] = o->np; } }
    else if (o->np > 8) idx_rebuild(rt, o);
    return p;
}
static int dyn_is(struct jrt *rt, jobj *o, jstr *key);
int js_delete(struct jrt *rt, jobj *o, jstr *key)
{
    if (!key->interned) key = js_intern(rt, key);
    if (dyn_is(rt, o, key)) return rt->dyn->del(rt, o, key);
    if (o->cls == JC_PROXY) return js_proxy_delete(rt, o, key);   /* SOMA_188 */
    if (o->cls == JC_ARRAY && key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix = wu_atoi(key->d); if (ix >= 0 && (uint32_t)ix < o->alen) { o->el[ix] = jhole(); return 1; } }
    for (uint32_t i = 0; i < o->np; i++) if (o->props[i].key == key) {
        if (o->frozen || (o->props[i].flags & JP_NOCONF)) return 0;
        memmove(&o->props[i], &o->props[i + 1], (o->np - i - 1) * sizeof(struct jprop)); o->np--;
        if (o->idx) idx_rebuild(rt, o);
        return 1;
    }
    return 1;
}
void js_def(struct jrt *rt, jobj *o, jstr *key, jv val, int flags)
{
    if (!key->interned) key = js_intern(rt, key);
    struct jprop *p = js_find_own(o, key); if (!p) p = add_prop(rt, o, key);
    p->val = val; p->flags = (uint8_t)flags; p->set = jundef();
}
void js_def_acc(struct jrt *rt, jobj *o, jstr *key, jv getter, jv setter, int flags)
{
    if (!key->interned) key = js_intern(rt, key);
    struct jprop *p = js_find_own(o, key); if (!p) p = add_prop(rt, o, key);
    if (!(p->flags & JP_ACC)) { p->val = jundef(); p->set = jundef(); }
    if (getter.t == JT_OBJ) p->val = getter;
    if (setter.t == JT_OBJ) p->set = setter;
    p->flags = (uint8_t)(flags | JP_ACC);
}
void js_set_c(struct jrt *rt, jobj *o, const char *key, jv val) { js_def(rt, o, js_intern_c(rt, key), val, JP_ENUM); }
jobj *js_native(struct jrt *rt, const char *name, jnative f, int nargs, int magic)
{
    jobj *o = js_obj_new(rt, rt->p_function); o->cls = JC_NATIVE; o->u.nat.f = f; o->magic = (int16_t)magic; o->flag = (uint8_t)nargs;
    if (name) js_def(rt, o, rt->s_name, jstrv(js_str_cstr(rt, name)), JP_RO);
    return o;
}
void js_method(struct jrt *rt, jobj *o, const char *name, jnative f, int nargs, int magic) { js_def(rt, o, js_intern_c(rt, name), jobjv(js_native(rt, name, f, nargs, magic)), 0); }
void js_getter(struct jrt *rt, jobj *o, const char *name, jnative get, jnative set, int magic)
{
    jv g = get ? jobjv(js_native(rt, name, get, 0, magic)) : jundef(), s = set ? jobjv(js_native(rt, name, set, 1, magic)) : jundef();
    js_def_acc(rt, o, js_intern_c(rt, name), g, s, 0);
}
int js_is_callable(jv v) { return v.t == JT_OBJ && (v.u.o->cls == JC_FUNC || v.u.o->cls == JC_NATIVE || v.u.o->cls == JC_BOUND || (v.u.o->cls == JC_PROXY && js_proxy_callable(v.u.o))); }

/* ------------------------------ conversiones ------------------------------ */
jstr *js_num_to_str(struct jrt *rt, f64 n)
{
    if (f64_is_int(n) && f64_lt(f64_abs(n), 0x4130000000000000ULL /* 2^20 */)) {
        int32_t v = (int32_t)f64_to_i64(n);
        if (v >= 0 && v < 10) return rt->s_chars['0' + v];
        char t[16]; int l = wu_itoa(v, t); return js_str_new(rt, t, (uint32_t)l);
    }
    char buf[80]; int l = f64_to_str(n, buf, 10); return js_str_new(rt, buf, (uint32_t)l);
}
static jstr *func_src(struct jrt *rt, jobj *f)
{
    if (f->cls == JC_FUNC && f->u.fn.cls && f->u.fn.cls->p) { struct jfi *ci = f->u.fn.cls->p; if (ci->src) return js_str_new(rt, ci->src, ci->srclen); }   /* SOMA 188B: una clase devuelve su texto completo */
    if (f->cls == JC_FUNC && f->u.fn.node && f->u.fn.node->p) { struct jfi *fi = f->u.fn.node->p; if (fi->src) return js_str_new(rt, fi->src, fi->srclen); }
    return js_str_cstr(rt, "function () { [native code] }");
}
jv js_toprim(struct jrt *rt, jv v, int hint_str)
{
    if (v.t != JT_OBJ) return v;
    jobj *o = v.u.o;
    if (o->cls == JC_BIGINT) return hint_str ? jstrv(js_big_tostr(rt, v, 10)) : jnum(js_big_tonum(v));   /* SOMA 188B */
    jstr *first = hint_str ? js_intern_c(rt, "toString") : js_intern_c(rt, "valueOf"), *second = hint_str ? js_intern_c(rt, "valueOf") : js_intern_c(rt, "toString");
    for (int i = 0; i < 2; i++) {
        jv f = js_get(rt, v, i ? second : first);
        if (js_is_callable(f)) { jv r = js_call(rt, f, v, 0, 0); if (r.t != JT_OBJ) return r; }
    }
    (void)o; js_throw_err(rt, E_TYPE, "No se puede convertir el objeto a un valor primitivo"); return jundef();
}
jstr *js_tostr(struct jrt *rt, jv v)
{
    switch (v.t) {
    case JT_UNDEF: return rt->s_undefined; case JT_NULL: return rt->s_null; case JT_BOOL: return v.u.b ? rt->s_true : rt->s_false;
    case JT_NUM: return js_num_to_str(rt, v.u.n); case JT_STR: return v.u.s;
    default: { jv p = js_toprim(rt, v, 1); return js_tostr(rt, p); }
    }
}
static f64 str_to_num(const char *s, uint32_t n)
{
    uint32_t i = 0; while (i < n && (wu_isspace(s[i]) || (uint8_t)s[i] == 0xC2)) i++;
    uint32_t e = n; while (e > i && wu_isspace(s[e - 1])) e--;
    if (i == e) return F64_ZERO;
    char buf[400]; uint32_t l = e - i; if (l >= sizeof buf) l = sizeof buf - 1; memcpy(buf, s + i, l); buf[l] = 0;
    if (buf[0] == '0' && l > 2 && (buf[1] | 32) == 'x') { f64 v = 0; for (uint32_t k = 2; k < l; k++) { if (!wu_ishex(buf[k])) return F64_NAN; v = f64_add(f64_mul(v, f64_from_i32(16)), f64_from_i32(wu_hexval(buf[k]))); } return v; }
    if (buf[0] == '0' && l > 2 && (buf[1] | 32) == 'b') { f64 v = 0; for (uint32_t k = 2; k < l; k++) { if (buf[k] != '0' && buf[k] != '1') return F64_NAN; v = f64_add(f64_mul(v, f64_from_i32(2)), f64_from_i32(buf[k] - '0')); } return v; }
    if (buf[0] == '0' && l > 2 && (buf[1] | 32) == 'o') { f64 v = 0; for (uint32_t k = 2; k < l; k++) { if (buf[k] < '0' || buf[k] > '7') return F64_NAN; v = f64_add(f64_mul(v, f64_from_i32(8)), f64_from_i32(buf[k] - '0')); } return v; }
    const char *p = buf; int neg = 0;
    if (wu_streq(p, "Infinity") || wu_streq(p, "+Infinity")) return F64_INF;
    if (wu_streq(p, "-Infinity")) return F64_NINF;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if (!(wu_isdigit(*p) || (*p == '.' && wu_isdigit(p[1])))) return F64_NAN;
    const char *end; f64 v = f64_parse(p, &end); if (!end || *end) return F64_NAN;
    return neg ? f64_neg(v) : v;
}
f64 js_tonum(struct jrt *rt, jv v)
{
    switch (v.t) {
    case JT_UNDEF: return F64_NAN; case JT_NULL: return F64_ZERO; case JT_BOOL: return v.u.b ? F64_ONE : F64_ZERO;
    case JT_NUM: return v.u.n; case JT_STR: return str_to_num(v.u.s->d, v.u.s->len);
    default: return js_tonum(rt, js_toprim(rt, v, 0));
    }
}
int js_truthy(jv v)
{
    switch (v.t) {
    case JT_UNDEF: case JT_NULL: return 0; case JT_BOOL: return v.u.b; case JT_NUM: return !(f64_abs(v.u.n) == 0 || f64_is_nan(v.u.n));
    case JT_STR: return v.u.s->len != 0; default: return !(v.u.o->cls == JC_BIGINT && js_big_is_zero(v));
    }
}
jobj *js_toobj(struct jrt *rt, jv v)
{
    if (v.t == JT_OBJ) return v.u.o;
    if (v.t <= JT_NULL) js_throw_err(rt, E_TYPE, "No se puede convertir undefined o null a objeto");
    jobj *o = js_obj_new(rt, v.t == JT_STR ? rt->p_string : v.t == JT_NUM ? rt->p_number : rt->p_boolean);
    o->cls = v.t == JT_STR ? JC_STRING : v.t == JT_NUM ? JC_NUMBER : JC_BOOL; o->el = (jv *)js_alloc(rt, sizeof(jv)); o->el[0] = v; return o;
}
const char *js_typeof(struct jrt *rt, jv v)
{
    (void)rt;
    switch (v.t) {
    case JT_UNDEF: return "undefined"; case JT_NULL: return "object"; case JT_BOOL: return "boolean"; case JT_NUM: return "number";
    case JT_STR: return (v.u.s->len > 2 && v.u.s->d[0] == '@' && v.u.s->d[1] == '@') ? "symbol" : "string";
    default: return v.u.o->cls == JC_BIGINT ? "bigint" : js_is_callable(v) ? "function" : "object";
    }
}
int js_strict_eq(jv a, jv b)
{
    if (a.t != b.t) return 0;
    switch (a.t) {
    case JT_UNDEF: case JT_NULL: return 1; case JT_BOOL: return a.u.b == b.u.b; case JT_NUM: return f64_eq(a.u.n, b.u.n);
    case JT_STR: return js_str_eq(a.u.s, b.u.s); default: return a.u.o == b.u.o || (a.u.o->cls == JC_BIGINT && b.u.o->cls == JC_BIGINT && js_big_same(a, b));
    }
}
int js_same_zero(jv a, jv b)
{
    if (a.t == JT_NUM && b.t == JT_NUM && f64_is_nan(a.u.n) && f64_is_nan(b.u.n)) return 1;
    return js_strict_eq(a, b);
}
int js_loose_eq(struct jrt *rt, jv a, jv b)
{
    if (js_is_big(a) || js_is_big(b)) return js_big_eq(rt, a, b);   /* SOMA 188B */
    if (a.t == b.t) return js_strict_eq(a, b);
    if (IS_NULLISH(a) && IS_NULLISH(b)) return 1;
    if (IS_NULLISH(a) || IS_NULLISH(b)) return 0;
    if (a.t == JT_OBJ && b.t != JT_OBJ) return js_loose_eq(rt, js_toprim(rt, a, 0), b);
    if (b.t == JT_OBJ && a.t != JT_OBJ) return js_loose_eq(rt, a, js_toprim(rt, b, 0));
    return f64_eq(js_tonum(rt, a), js_tonum(rt, b));
}
int js_to_index(struct jrt *rt, jv v, int64_t *out)
{
    (void)rt;
    if (v.t == JT_NUM) { if (f64_is_int(v.u.n) && !(v.u.n >> 63) && f64_lt(v.u.n, 0x41E0000000000000ULL)) { *out = f64_to_i64(v.u.n); return 1; } return 0; }
    if (v.t == JT_STR && v.u.s->len && v.u.s->len < 10 && v.u.s->d[0] >= '0' && v.u.s->d[0] <= '9') {
        if (v.u.s->d[0] == '0' && v.u.s->len > 1) return 0;
        int64_t r = 0; for (uint32_t i = 0; i < v.u.s->len; i++) { if (v.u.s->d[i] < '0' || v.u.s->d[i] > '9') return 0; r = r * 10 + (v.u.s->d[i] - '0'); }
        *out = r; return 1;
    }
    return 0;
}

/* ------------------------------ acceso a propiedades ------------------------------ */
static jv proto_of_prim(struct jrt *rt, jv v) { return jobjv(v.t == JT_STR ? rt->p_string : v.t == JT_NUM ? rt->p_number : rt->p_boolean); }
static jv get_from(struct jrt *rt, jobj *start, jv self, jstr *key)
{
    for (jobj *o = start; o; o = o->proto) {
        if (o->cls == JC_PROXY) return js_proxy_get(rt, o, key, self);   /* SOMA_188 */
        struct jprop *p = js_find_own(o, key);
        if (p) {
            if (p->flags & JP_ACC) { if (p->val.t != JT_OBJ) return jundef(); return js_call(rt, p->val, self, 0, 0); }
            return p->val;
        }
        if (o->cls == JC_FUNC || o->cls == JC_NATIVE || o->cls == JC_BOUND) {
            if (key == rt->s_prototype && o->cls == JC_FUNC && o->u.fn.node && !(o->u.fn.node->fl & (NF_ARROW | NF_METHOD | NF_GETTER | NF_SETTER))) {
                jobj *pr = js_obj_new(rt, rt->p_object); js_def(rt, pr, rt->s_constructor, jobjv(o), 0); js_def(rt, o, rt->s_prototype, jobjv(pr), JP_NOCONF); return jobjv(pr);
            }
            if (key == rt->s_name && o->cls == JC_FUNC && o->u.fn.node) return jstrv(o->u.fn.node->str ? o->u.fn.node->str : rt->s_empty);
            if (key == rt->s_length) { if (o->cls == JC_FUNC && o->u.fn.node && o->u.fn.node->p) return jint(((struct jfi *)o->u.fn.node->p)->nparams); return jint(o->cls == JC_NATIVE ? o->flag : 0); }
        }
    }
    return jundef();
}
static int dyn_is(struct jrt *rt, jobj *o, jstr *key)
{
    if (o->cls != JC_HOST || !(o->flag & 1) || !rt->dyn) return 0;
    for (jobj *q = o; q; q = q->proto) if (js_find_own(q, key)) return 0;
    return 1;
}
jv js_get(struct jrt *rt, jv obj, jstr *key)
{
    if (!key->interned) key = js_intern(rt, key);
    switch (obj.t) {
    case JT_OBJ: {
        jobj *o = obj.u.o;
        if (o->cls == JC_PROXY) return js_proxy_get(rt, o, key, obj);   /* SOMA_188 */
        if (dyn_is(rt, o, key)) { int found = 0; jv v = rt->dyn->get(rt, o, key, &found); if (found) return v; }
        if (o->cls == JC_ARRAY || o->cls == JC_ARGS) {
            if (key == rt->s_length) return jint(o->alen);
            if (key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) { if ((uint32_t)ix >= o->alen) return jundef(); jv ev = o->el[ix]; if (IS_HOLE(ev)) ev.u.n = 0; return ev; } }
        } else if (o->cls == JC_TYPED) {                                    /* SOMA_186 */
            if (key == rt->s_length) return jint(js_ta_length(o));
            if (key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) return (uint64_t)ix < js_ta_length(o) ? js_ta_get(o, (uint32_t)ix) : jundef(); }
        } else if (o->cls == JC_STRING) {
            jstr *s = o->el[0].u.s;
            if (key == rt->s_length) return jint(js_u16len(s));
            int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) { jstr *c = (uint64_t)ix < js_u16len(s) ? js_u16_char(rt, s, (uint32_t)ix) : 0; return c ? jstrv(c) : jundef(); }
        }
        if (o->cls == JC_HOST && (o->flag & 2) && rt->dyn && rt->dyn->named) {   /* SOMA_187A */
            jv r = get_from(rt, o, obj, key);
            if (r.t == JT_UNDEF) { int found = 0; jv v = rt->dyn->named(rt, o, key, &found); if (found) return v; }
            return r;
        }
        return get_from(rt, o, obj, key);
    }
    case JT_STR: {
        jstr *s = obj.u.s;
        if (key == rt->s_length) return jint(js_u16len(s));
        if (key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) { jstr *c = (uint64_t)ix < js_u16len(s) ? js_u16_char(rt, s, (uint32_t)ix) : 0; return c ? jstrv(c) : jundef(); } }
        if (key->len == 11 && s->len > 2 && s->d[0] == '@' && s->d[1] == '@' && !memcmp(key->d, "description", 11)) return js_sym_description(rt, s);   /* SOMA_187 */
        return get_from(rt, rt->p_string, obj, key);
    }
    case JT_NUM: case JT_BOOL: return get_from(rt, proto_of_prim(rt, obj).u.o, obj, key);
    default: js_throw_err(rt, E_TYPE, "No se pueden leer propiedades de %s (leyendo '%s')", obj.t == JT_NULL ? "null" : "undefined", key->d); return jundef();
    }
}
jv js_get_c(struct jrt *rt, jv obj, const char *key) { return js_get(rt, obj, js_intern_c(rt, key)); }
jv js_getv(struct jrt *rt, jv obj, jv key)
{
    if (obj.t == JT_OBJ && key.t == JT_NUM) {
        jobj *o = obj.u.o;
        if (o->cls == JC_ARRAY || o->cls == JC_ARGS) { int64_t ix; if (js_to_index(rt, key, &ix)) { if ((uint32_t)ix >= o->alen) return jundef(); jv ev = o->el[ix]; if (IS_HOLE(ev)) ev.u.n = 0; return ev; } }
        if (o->cls == JC_TYPED) { int64_t ix; if (js_to_index(rt, key, &ix)) return (uint64_t)ix < js_ta_length(o) ? js_ta_get(o, (uint32_t)ix) : jundef(); }   /* SOMA_186 */
    }
    if (obj.t == JT_STR && key.t == JT_NUM) { int64_t ix; if (js_to_index(rt, key, &ix)) { jstr *c = (uint64_t)ix < js_u16len(obj.u.s) ? js_u16_char(rt, obj.u.s, (uint32_t)ix) : 0; return c ? jstrv(c) : jundef(); } }
    if (key.t != JT_STR) key = jstrv(js_tostr(rt, key.t == JT_OBJ ? js_toprim(rt, key, 1) : key));
    return js_get(rt, obj, key.u.s);
}
static void arr_set_len(struct jrt *rt, jobj *a, uint32_t n)
{
    if (n > a->alen) { arr_reserve(rt, a, n); for (uint32_t i = a->alen; i < n; i++) a->el[i] = jhole(); }
    a->alen = n;
}
void js_put(struct jrt *rt, jv obj, jstr *key, jv val)
{
    if (!key->interned) key = js_intern(rt, key);
    if (obj.t != JT_OBJ) {
        if (obj.t <= JT_NULL) js_throw_err(rt, E_TYPE, "No se pueden asignar propiedades de %s (asignando '%s')", obj.t == JT_NULL ? "null" : "undefined", key->d);
        return;
    }
    jobj *o = obj.u.o;
    if (o->cls == JC_PROXY) { js_proxy_set(rt, o, key, val, obj); return; }   /* SOMA_188 */
    if (dyn_is(rt, o, key)) { rt->dyn->put(rt, o, key, val); return; }
    if (o->cls == JC_ARRAY) {
        if (key == rt->s_length) { f64 n = js_tonum(rt, val); if (!f64_is_int(n) || (n >> 63)) js_throw_err(rt, E_RANGE, "Longitud de array no valida"); if (!o->frozen) arr_set_len(rt, o, (uint32_t)f64_to_i64(n)); return; }
        int64_t ix; if (key->d[0] >= '0' && key->d[0] <= '9' && js_to_index(rt, jstrv(key), &ix)) {
            if (o->frozen) return;
            if ((uint64_t)ix > o->alen + (1u << 22)) js_throw_err(rt, E_RANGE, "indice de array demasiado grande");
            if ((uint32_t)ix >= o->alen) arr_set_len(rt, o, (uint32_t)ix + 1);
            o->el[ix] = val; return;
        }
    }
    if (o->cls == JC_TYPED && key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) { if (!o->frozen) js_ta_set(rt, o, (uint32_t)(ix > 0xFFFFFFFFll ? 0xFFFFFFFFu : ix), val); return; } }   /* SOMA_186 */
    struct jprop *p = js_find_own(o, key);
    if (p) {
        if (p->flags & JP_ACC) { if (p->set.t == JT_OBJ) js_call(rt, p->set, obj, &val, 1); return; }
        if ((p->flags & JP_RO) || o->frozen) return;
        p->val = val; return;
    }
    for (jobj *q = o->proto; q; q = q->proto) {
        struct jprop *pp = js_find_own(q, key);
        if (pp) { if (pp->flags & JP_ACC) { if (pp->set.t == JT_OBJ) js_call(rt, pp->set, obj, &val, 1); return; } if (pp->flags & JP_RO) return; break; }
    }
    if (!o->extensible || o->frozen) return;
    p = add_prop(rt, o, key); p->val = val;
}
void js_putv(struct jrt *rt, jv obj, jv key, jv val)
{
    if (obj.t == JT_OBJ && key.t == JT_NUM && obj.u.o->cls == JC_ARRAY && !obj.u.o->frozen) {
        int64_t ix; if (js_to_index(rt, key, &ix) && (uint64_t)ix <= obj.u.o->alen + (1u << 22)) {
            jobj *a = obj.u.o; if ((uint32_t)ix >= a->alen) arr_set_len(rt, a, (uint32_t)ix + 1); if (IS_HOLE(val)) val.u.n = 0; a->el[ix] = val; return;
        }
    }
    if (obj.t == JT_OBJ && key.t == JT_NUM && obj.u.o->cls == JC_TYPED && !obj.u.o->frozen) { int64_t ix; if (js_to_index(rt, key, &ix)) { js_ta_set(rt, obj.u.o, (uint32_t)(ix > 0xFFFFFFFFll ? 0xFFFFFFFFu : ix), val); return; } }   /* SOMA_186 */
    if (key.t != JT_STR) key = jstrv(js_tostr(rt, key.t == JT_OBJ ? js_toprim(rt, key, 1) : key));
    js_put(rt, obj, key.u.s, val);
}
int js_has(struct jrt *rt, jobj *o, jstr *key)
{
    if (!key->interned) key = js_intern(rt, key);
    if (dyn_is(rt, o, key) && rt->dyn->has(rt, o, key)) return 1;
    for (jobj *q = o; q; q = q->proto) {
        if (q->cls == JC_PROXY) return js_proxy_has(rt, q, key);   /* SOMA_188 */
        if ((q->cls == JC_ARRAY || q->cls == JC_ARGS)) { if (key == rt->s_length) return 1; int64_t ix; if (key->d[0] >= '0' && key->d[0] <= '9' && js_to_index(rt, jstrv(key), &ix)) return (uint32_t)ix < q->alen && !IS_HOLE(q->el[ix]); }
        if (q->cls == JC_TYPED && key->d[0] >= '0' && key->d[0] <= '9') { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) return (uint64_t)ix < js_ta_length(q); }   /* SOMA_186 */
        if (js_find_own(q, key)) return 1;
        if (q->cls == JC_FUNC && key == rt->s_prototype) return 1;
    }
    return 0;
}
int js_instanceof(struct jrt *rt, jv v, jv ctor)
{
    if (!js_is_callable(ctor)) js_throw_err(rt, E_TYPE, "El lado derecho de instanceof no es invocable");
    if (ctor.u.o->cls == JC_BOUND) ctor = ctor.u.o->el[0];
    if (v.t != JT_OBJ) return 0;
    jv pr = js_get(rt, ctor, rt->s_prototype); if (pr.t != JT_OBJ) return 0;
    for (jobj *o = v.u.o->proto; o; o = o->proto) if (o == pr.u.o) return 1;
    return 0;
}
jv js_make_iter_result(struct jrt *rt, jv value, int done)
{
    jobj *o = js_obj_new(rt, rt->p_object); js_def(rt, o, rt->s_value, value, JP_ENUM); js_def(rt, o, rt->s_done, jbool(done), JP_ENUM); return jobjv(o);
}
int js_iterate(struct jrt *rt, jv it, int (*cb)(struct jrt *, jv, void *), void *ud)
{
    if (it.t == JT_STR) {
        jstr *s = it.u.s; uint32_t i = 0;
        while (i < s->len) { uint32_t adv; wu_utf8_next(s->d + i, &adv); if (!adv) adv = 1; if (i + adv > s->len) adv = s->len - i; if (!cb(rt, jstrv(js_str_new(rt, s->d + i, adv)), ud)) return 0; i += adv; }
        return 1;
    }
    if (it.t != JT_OBJ) js_throw_err(rt, E_TYPE, "el valor no es iterable");
    jobj *o = it.u.o;
    if (o->cls == JC_ARRAY || o->cls == JC_ARGS) { for (uint32_t i = 0; i < o->alen; i++) { if (!cb(rt, o->el[i], ud)) return 0; } return 1; }
    if (o->cls == JC_SET) { for (uint32_t i = 0; i < o->alen; i++) { if (!cb(rt, o->el[i], ud)) return 0; } return 1; }
    if (o->cls == JC_TYPED) { for (uint32_t i = 0; i < js_ta_length(o); i++) { if (!cb(rt, js_ta_get(o, i), ud)) return 0; } return 1; }   /* SOMA_186 */
    if (o->cls == JC_MAP) {
        for (uint32_t i = 0; i + 1 < o->alen; i += 2) { jv pr[2] = { o->el[i], o->el[i + 1] }; if (!cb(rt, jobjv(js_array_from(rt, pr, 2)), ud)) return 0; }
        return 1;
    }
    jv m = js_get(rt, it, js_intern_c(rt, "@@iterator"));
    if (!js_is_callable(m)) js_throw_err(rt, E_TYPE, "el objeto no es iterable");
    jv iter = js_call(rt, m, it, 0, 0);
    jv nx = js_get(rt, iter, rt->s_next);
    for (;;) {
        JS_STEP(rt);
        jv r = js_call(rt, nx, iter, 0, 0);
        if (r.t != JT_OBJ) js_throw_err(rt, E_TYPE, "el resultado del iterador no es un objeto");
        if (js_truthy(js_get(rt, r, rt->s_done))) return 1;
        if (!cb(rt, js_get(rt, r, rt->s_value), ud)) {
            jv rf = js_get(rt, iter, js_intern_c(rt, "return")); if (js_is_callable(rf)) js_call(rt, rf, iter, 0, 0);
            return 0;
        }
    }
}

/* ------------------------------ ambitos ------------------------------ */
/* SOMA 189: busqueda de un nombre en un ambito. Los ambitos pequenos se recorren; los grandes (la funcion que envuelve un script
 * de megas tiene miles de variables) llevan un indice de dispersion por puntero de nombre (los nombres estan internados). */
static inline uint32_t nm_hash(jstr *name) { return (uint32_t)(((uintptr_t)name >> 4) * 2654435761u); }
static inline struct jvar *scope_find(struct scope *s, jstr *name)
{
    if (s->hidx) {
        uint32_t m = s->hcap - 1, h = nm_hash(name) & m;
        for (;;) { uint32_t e = s->hidx[h]; if (!e) return 0; if (e - 1 < s->n && s->vars[e - 1].name == name) return &s->vars[e - 1]; h = (h + 1) & m; }
    }
    for (int i = (int)s->n - 1; i >= 0; i--) if (s->vars[i].name == name) return &s->vars[i];
    return 0;
}
static void scope_index(struct jrt *rt, struct scope *s)
{
    uint32_t hc = 128; while (hc < s->n * 4u) hc *= 2;
    uint32_t *h = (uint32_t *)js_alloc(rt, hc * (uint32_t)sizeof(uint32_t)); uint32_t m = hc - 1;
    for (uint32_t i = 0; i < s->n; i++) { uint32_t k = nm_hash(s->vars[i].name) & m; while (h[k]) k = (k + 1) & m; h[k] = i + 1; }
    s->hidx = h; s->hcap = hc;
}
struct jvar *js_scope_lookup(struct scope *s, jstr *name)
{
    for (; s; s = s->parent) { struct jvar *v = scope_find(s, name); if (v) return v; }
    return 0;
}
static struct scope *new_scope(struct jrt *rt, struct scope *parent, int cap, int on_stack)
{
    struct scope *s; struct jvar *vars;
    if (on_stack) {
        uint32_t need = ((uint32_t)sizeof(struct scope) + (uint32_t)cap * (uint32_t)sizeof(struct jvar) + 15u) & ~15u;
        if (rt->stop + need > rt->send) on_stack = 0;
        else { s = (struct scope *)rt->stop; rt->stop += need; memset(s, 0, need); vars = (struct jvar *)(s + 1); }
    }
    if (!on_stack) { s = (struct scope *)js_alloc(rt, (uint32_t)sizeof *s); vars = cap ? (struct jvar *)js_alloc(rt, (uint32_t)cap * (uint32_t)sizeof(struct jvar)) : 0; s->heap = 1; }
    s->parent = parent; s->vars = vars; s->cap = (uint32_t)cap; s->n = 0; s->hidx = 0; s->hcap = 0; s->with_obj = 0; s->has_with = parent ? parent->has_with : 0; return s;
}
static struct jvar *scope_add(struct jrt *rt, struct scope *s, jstr *name, jv v, int kind)
{
    { struct jvar *e = scope_find(s, name); if (e) { e->v = v; e->kind = (uint8_t)kind; return e; } }
    if (s->n >= s->cap) {
        uint32_t nc = s->cap ? s->cap * 2u : 4u; struct jvar *nv = (struct jvar *)js_alloc(rt, nc * (uint32_t)sizeof(struct jvar));
        if (s->n) memcpy(nv, s->vars, (uint32_t)s->n * sizeof(struct jvar));
        s->vars = nv; s->cap = nc;
    }
    struct jvar *r = &s->vars[s->n++]; r->name = name; r->v = v; r->kind = (uint8_t)kind;
    if (s->hidx) { if (s->n * 2u > s->hcap) scope_index(rt, s); else { uint32_t m = s->hcap - 1, k = nm_hash(name) & m; while (s->hidx[k]) k = (k + 1) & m; s->hidx[k] = s->n; } }
    else if (s->n > 24) scope_index(rt, s);
    return &s->vars[s->n - 1];
}
static struct scope *fn_scope(struct scope *s) { while (s && !s->is_fn) s = s->parent; return s; }

/* ------------------------------ evaluacion ------------------------------ */
static jv eval(struct jrt *rt, struct jn *n, struct scope *sc);
static int exec(struct jrt *rt, struct jn *n, struct scope *sc);
static jv make_closure(struct jrt *rt, struct jn *fn, struct scope *sc, jobj *home);
static void bind_pattern(struct jrt *rt, struct jn *pat, jv val, struct scope *sc, int mode);
static jv call_closure(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc, jobj *newtarget);

/* SOMA_188: with. Devuelve el objeto del with mas interno que tiene la propiedad antes de encontrar la variable, o NULL */
static jobj *with_target(struct jrt *rt, struct scope *s, jstr *name)
{
    for (; s; s = s->parent) {
        for (int i = s->n - 1; i >= 0; i--) if (s->vars[i].name == name) return 0;
        if (s->with_obj && js_has(rt, s->with_obj, name)) return s->with_obj;
        if (!s->has_with) return 0;
    }
    return 0;
}
static jv lookup_ident(struct jrt *rt, jstr *name, struct scope *sc, int for_typeof)
{
    if (sc && sc->has_with) { jobj *w = with_target(rt, sc, name); if (w) return js_get(rt, jobjv(w), name); }
    struct jvar *v = js_scope_lookup(sc, name);
    if (v) { if (v->v.t == JT_UNDEF && v->kind >= 1 && v->v.u.b == 0x5A5A) js_throw_err(rt, E_REFERENCE, "No se puede acceder a '%s' antes de su inicializacion", name->d); return v->v; }
    if (name == rt->s_undefined) return jundef();
    for (jobj *o = rt->global; o; o = o->proto) { struct jprop *p = js_find_own(o, name); if (p) { if (p->flags & JP_ACC) return p->val.t == JT_OBJ ? js_call(rt, p->val, jobjv(rt->global), 0, 0) : jundef(); return p->val; } }
    if (for_typeof) return jundef();
    js_throw_err(rt, E_REFERENCE, "%s no esta definido", name->d); return jundef();
}
static void assign_ident(struct jrt *rt, jstr *name, jv val, struct scope *sc)
{
    if (sc && sc->has_with) { jobj *w = with_target(rt, sc, name); if (w) { js_put(rt, jobjv(w), name, val); return; } }
    struct jvar *v = js_scope_lookup(sc, name);
    if (v) { if (v->kind == 2) js_throw_err(rt, E_TYPE, "Asignacion a una variable constante '%s'", name->d); v->v = val; return; }
    js_put(rt, jobjv(rt->global), name, val);
}
static void declare(struct jrt *rt, struct scope *sc, jstr *name, jv val, int kind)
{
    if (kind == 0) {
        struct scope *f = sc; while (f && !f->is_fn && !f->arrow && f->parent) f = f->parent;   /* SOMA_183: las flechas tambien delimitan los var (antes se escapaban a la funcion exterior) */
        if (!f->parent && !f->is_fn && !f->arrow) { js_put(rt, jobjv(rt->global), name, val); return; }       /* nivel superior: propiedad global */
        struct jvar *v = 0; for (struct scope *s = sc; s; s = s->parent) { for (uint32_t i = 0; i < s->n; i++) if (s->vars[i].name == name) { v = &s->vars[i]; break; } if (v || s == f) break; }
        if (v) v->v = val; else scope_add(rt, f, name, val, 0);
        return;
    }
    scope_add(rt, sc, name, val, kind);
}
static jv array_literal_spread_item(struct jrt *rt, jv v, jobj *dst);
static int spread_cb(struct jrt *rt, jv item, void *ud) { js_arr_push(rt, (jobj *)ud, item); return 1; }
static jv array_literal_spread_item(struct jrt *rt, jv v, jobj *dst) { js_iterate(rt, v, spread_cb, dst); return jundef(); }

static int count_args(struct jn *a) { int c = 0; for (; a; a = a->next) c++; return c; }
static jv *eval_args(struct jrt *rt, struct jn *a, struct scope *sc, int *argc, jv *small, int smallcap)
{
    int n = count_args(a); int spread = 0; for (struct jn *q = a; q; q = q->next) if (q->t == N_SPREAD) spread = 1;
    if (!spread) {
        jv *out = n <= smallcap ? small : (jv *)js_alloc(rt, (uint32_t)n * (uint32_t)sizeof(jv)); int i = 0;
        for (; a; a = a->next) out[i++] = eval(rt, a, sc);
        *argc = n; return out;
    }
    jobj *tmp = js_arr_new(rt, (uint32_t)n + 4);
    for (; a; a = a->next) { if (a->t == N_SPREAD) js_iterate(rt, eval(rt, a->a, sc), spread_cb, tmp); else js_arr_push(rt, tmp, eval(rt, a, sc)); }
    *argc = (int)tmp->alen; return tmp->el;
}
static jv num_bin(struct jrt *rt, int op, jv a, jv b)
{
    f64 x = js_tonum(rt, a), y = js_tonum(rt, b);
    switch (op) {
    case B_SUB: return jnum(f64_sub(x, y)); case B_MUL: return jnum(f64_mul(x, y)); case B_DIV: return jnum(f64_div(x, y));
    case B_MOD: return jnum(f64_mod(x, y)); case B_EXP: return jnum(f64_pow(x, y));
    case B_AND: return jint(f64_to_i32(x) & f64_to_i32(y)); case B_OR: return jint(f64_to_i32(x) | f64_to_i32(y)); case B_XOR: return jint(f64_to_i32(x) ^ f64_to_i32(y));
    case B_SHL: return jint((int32_t)((uint32_t)f64_to_i32(x) << (f64_to_u32(y) & 31)));
    case B_SHR: return jint(f64_to_i32(x) >> (f64_to_u32(y) & 31));
    case B_USHR: return jint((int64_t)(f64_to_u32(x) >> (f64_to_u32(y) & 31)));
    }
    return jundef();
}
static int cmp_lt(struct jrt *rt, jv a, jv b, int *undef)
{
    *undef = 0; a = js_toprim(rt, a, 0); b = js_toprim(rt, b, 0);
    if (a.t == JT_STR && b.t == JT_STR) {
        uint32_t n = a.u.s->len < b.u.s->len ? a.u.s->len : b.u.s->len; int c = memcmp(a.u.s->d, b.u.s->d, n);
        if (c) return c < 0;
        return a.u.s->len < b.u.s->len;
    }
    f64 x = js_tonum(rt, a), y = js_tonum(rt, b); if (f64_is_nan(x) || f64_is_nan(y)) { *undef = 1; return 0; }
    return f64_lt(x, y);
}
static jv bin_op(struct jrt *rt, int op, jv a, jv b)
{
    if ((a.t == JT_OBJ && a.u.o->cls == JC_BIGINT) || (b.t == JT_OBJ && b.u.o->cls == JC_BIGINT)) {
        switch (op) {   /* SOMA 188B: BigInt */
        case B_ADD: { jv o = js_is_big(a) ? b : a; if (o.t == JT_OBJ && !js_is_big(o)) o = js_toprim(rt, o, 0); if (o.t == JT_STR) return jstrv(js_str_concat(rt, js_tostr(rt, a), js_tostr(rt, b))); return js_big_binop(rt, op, a, b); }
        case B_SUB: case B_MUL: case B_DIV: case B_MOD: case B_EXP: case B_AND: case B_OR: case B_XOR: case B_SHL: case B_SHR: case B_USHR: return js_big_binop(rt, op, a, b);
        case B_LT: { int u; int r = js_big_lt(rt, a, b, &u); return jbool(!u && r); }
        case B_GT: { int u; int r = js_big_lt(rt, b, a, &u); return jbool(!u && r); }
        case B_LE: { int u; int r = js_big_lt(rt, b, a, &u); return jbool(!u && !r); }
        case B_GE: { int u; int r = js_big_lt(rt, a, b, &u); return jbool(!u && !r); }
        default: break;
        }
    }
    switch (op) {
    case B_ADD: {
        if (a.t == JT_NUM && b.t == JT_NUM) return jnum(f64_add(a.u.n, b.u.n));
        a = js_toprim(rt, a, 0); b = js_toprim(rt, b, 0);
        if (a.t == JT_STR || b.t == JT_STR) return jstrv(js_str_concat(rt, js_tostr(rt, a), js_tostr(rt, b)));
        return jnum(f64_add(js_tonum(rt, a), js_tonum(rt, b)));
    }
    case B_SUB: if (a.t == JT_NUM && b.t == JT_NUM) return jnum(f64_sub(a.u.n, b.u.n)); return num_bin(rt, op, a, b);
    case B_MUL: if (a.t == JT_NUM && b.t == JT_NUM) return jnum(f64_mul(a.u.n, b.u.n)); return num_bin(rt, op, a, b);
    case B_DIV: case B_MOD: case B_EXP: case B_AND: case B_OR: case B_XOR: case B_SHL: case B_SHR: case B_USHR: return num_bin(rt, op, a, b);
    case B_EQ: return jbool(js_loose_eq(rt, a, b)); case B_NE: return jbool(!js_loose_eq(rt, a, b));
    case B_SEQ: return jbool(js_strict_eq(a, b)); case B_SNE: return jbool(!js_strict_eq(a, b));
    case B_LT: { int u; int r = cmp_lt(rt, a, b, &u); return jbool(!u && r); }
    case B_GT: { int u; int r = cmp_lt(rt, b, a, &u); return jbool(!u && r); }
    case B_LE: { int u; int r = cmp_lt(rt, b, a, &u); return jbool(!u && !r); }
    case B_GE: { int u; int r = cmp_lt(rt, a, b, &u); return jbool(!u && !r); }
    case B_INSTANCEOF: return jbool(js_instanceof(rt, a, b));
    case B_IN: { if (b.t != JT_OBJ) js_throw_err(rt, E_TYPE, "El operando derecho de 'in' no es un objeto"); jstr *k = js_tostr(rt, a); return jbool(js_has(rt, b.u.o, k)); }
    }
    return jundef();
}
static void put_member(struct jrt *rt, struct jn *m, jv obj, jv val, struct scope *sc)
{
    (void)sc;
    if (m->fl & NF_SHORT) js_put(rt, obj, m->b->str, val);
    else js_putv(rt, obj, eval(rt, m->b, sc), val);
}
/* super.x : propiedad buscada desde el prototipo de home */
static jv super_get(struct jrt *rt, struct scope *sc, jstr *key)
{
    struct scope *f = fn_scope(sc); jobj *home = f ? f->home : 0;
    if (!home || !home->proto) return jundef();
    return get_from(rt, home->proto, f->this_v, key);
}
static jv eval_member_obj(struct jrt *rt, struct jn *m, struct scope *sc, jv *keyout, jstr **keystr)
{
    jv o = eval(rt, m->a, sc);
    *keystr = 0;
    if (m->fl & NF_SHORT) *keystr = m->b->str; else { *keyout = eval(rt, m->b, sc); }
    return o;
}
static jv construct_obj(struct jrt *rt, jv fn, jv *args, int argc, jobj *nt);
/* SOMA_188: eval directo (ES 19.2.1.1 PerformEval, modo no estricto): el codigo ve y modifica las variables del ambito que
   llama; sus var y funciones van al ambito de variables de la funcion (o al global); let/const quedan en un ambito propio. */
static void hoist_funcs(struct jrt *rt, struct jn *s, struct scope *sc);
static jv eval_direct(struct jrt *rt, const char *src, uint32_t len, struct scope *sc)
{
    int line = 0; char msg[160];
    struct jn *prog = js_parse(rt, src, len, &line, msg, sizeof msg);
    if (!prog) js_throw_err(rt, E_SYNTAX, "%s (eval, linea %d)", msg, line);
    struct jfi *fi = (struct jfi *)prog->p;
    struct scope *f = sc; while (f && !f->is_fn && !f->arrow && f->parent) f = f->parent;
    int global = !f || (!f->parent && !f->is_fn && !f->arrow);
    struct scope *es = new_scope(rt, sc, 8, 0);
    for (int i = 0; i < fi->nvn; i++) {
        jstr *nm = fi->vn[i];
        if (global) { if (!js_find_own(rt->global, nm)) js_def(rt, rt->global, nm, jundef(), JP_ENUM); continue; }
        int found = 0; for (struct scope *q = sc; q && !found; q = q->parent) { for (uint32_t k = 0; k < q->n; k++) if (q->vars[k].name == nm) { found = 1; break; } if (q == f) break; }
        if (!found) scope_add(rt, f, nm, jundef(), 0);
    }
    hoist_funcs(rt, prog->b, global ? rt->gscope : f);
    jv last = jundef();
    for (struct jn *s = prog->b; s; s = s->next) {
        if (s->t == N_EXPR) { last = eval(rt, s->a, es); continue; }
        int c = exec(rt, s, es);
        if (c == C_RETURN || c == C_BREAK || c == C_CONTINUE) break;
    }
    return last;
}
static jv eval_call(struct jrt *rt, struct jn *n, struct scope *sc)
{
    struct jn *callee = n->a; jv fn, thisv = jundef(); jv small[6]; int argc;
    if (callee->t == N_SUPER) {
        struct scope *f = fn_scope(sc); if (!f || !f->callee) js_throw_err(rt, E_SYNTAX, "'super' fuera de un constructor");
        jobj *parent = f->callee->proto; jv *args = eval_args(rt, n->b, sc, &argc, small, 6);
        if (!parent || !js_is_callable(jobjv(parent))) js_throw_err(rt, E_TYPE, "La clase padre no es un constructor");
        jv r = construct_obj(rt, jobjv(parent), args, argc, f->newtarget ? f->newtarget : f->callee);
        f->this_v = r;
        /* campos de instancia de esta clase */
        if (f->callee->u.fn.cls && r.t == JT_OBJ) {
            struct scope *cs = f->callee->u.fn.env;
            for (struct jn *m = f->callee->u.fn.cls->b; m; m = m->next) if ((m->fl & NF_FIELD) && !(m->fl & NF_STATIC)) {
                struct scope *fs = new_scope(rt, cs, 0, 0); fs->is_fn = 1; fs->arrow = 0; fs->this_v = r; fs->home = f->callee->u.fn.home;
                jstr *key = (m->fl & NF_COMPUTED) ? js_tostr(rt, eval(rt, m->a, cs)) : m->a->str;
                js_def(rt, r.u.o, js_intern(rt, key), m->b ? eval(rt, m->b, fs) : jundef(), JP_ENUM);
            }
        }
        return r;
    }
    if (callee->t == N_MEMBER) {
        if (callee->a->t == N_SUPER) {
            struct scope *f = fn_scope(sc); jstr *k = (callee->fl & NF_SHORT) ? callee->b->str : js_tostr(rt, eval(rt, callee->b, sc));
            fn = super_get(rt, sc, js_intern(rt, k)); thisv = f ? f->this_v : jundef();
        } else {
            rt->sc_flag = 0;
            jv key; jstr *ks; jv o = eval_member_obj(rt, callee, sc, &key, &ks);
            if (rt->sc_flag && (callee->a->t == N_MEMBER || callee->a->t == N_CALL)) return jundef();
            rt->sc_flag = 0;
            if ((callee->fl & NF_OPT) && IS_NULLISH(o)) { rt->sc_flag = 1; return jundef(); }
            thisv = o; fn = ks ? js_get(rt, o, ks) : js_getv(rt, o, key);
            if (!js_is_callable(fn)) {
                if ((n->fl & NF_OPT) && IS_NULLISH(fn)) { rt->sc_flag = 1; return jundef(); }
                js_throw_err(rt, E_TYPE, "%s no es una funcion", ks ? ks->d : "la expresion");
            }
        }
    } else {
        rt->sc_flag = 0;
        fn = eval(rt, callee, sc);
        if (rt->sc_flag && (callee->t == N_MEMBER || callee->t == N_CALL)) return jundef();
        if (callee->t == N_IDENT && (callee->str == rt->s_undefined)) {}
    }
    if ((n->fl & NF_OPT) && IS_NULLISH(fn)) { rt->sc_flag = 1; return jundef(); }
    if (!js_is_callable(fn)) js_throw_err(rt, E_TYPE, "%s no es una funcion", callee->t == N_IDENT ? callee->str->d : "la expresion");
    jv *args = eval_args(rt, n->b, sc, &argc, small, 6);
    if (callee->t == N_IDENT && callee->str == rt->s_eval188 && fn.t == JT_OBJ && fn.u.o == rt->eval_fn && rt->eval_fn && !web_feat_off("jsdeval")) {   /* SOMA_188: eval directo */
        if (argc < 1 || args[0].t != JT_STR) return argc ? args[0] : jundef();
        rt->cur_line = n->line;
        return eval_direct(rt, args[0].u.s->d, args[0].u.s->len, sc);
    }
    rt->cur_line = n->line;
#ifdef WJS_DEBUG_COL
    rt->cur_col = n->col;
#endif

    return js_call(rt, fn, thisv, args, argc);
}

static jv eval_assign(struct jrt *rt, struct jn *n, struct scope *sc)
{
    struct jn *t = n->a; int op = n->op;
    if (op == 0) {
        if (t->t == N_IDENT) {
            jv v = eval(rt, n->b, sc);
            if (v.t == JT_OBJ && v.u.o->cls == JC_FUNC && v.u.o->u.fn.node && !v.u.o->u.fn.node->str && (n->b->t == N_FUNC || n->b->t == N_CLASS)) {}
            assign_ident(rt, t->str, v, sc); return v;
        }
        if (t->t == N_MEMBER) {
            rt->sc_flag = 0;
            jv key; jstr *ks; jv o = eval_member_obj(rt, t, sc, &key, &ks); jv v;
            if (t->a->t == N_SUPER) { struct scope *f = fn_scope(sc); o = f ? f->this_v : jundef(); }
            v = eval(rt, n->b, sc);
            if (ks) js_put(rt, o, ks, v); else js_putv(rt, o, key, v);
            return v;
        }
        jv v = eval(rt, n->b, sc); bind_pattern(rt, t, v, sc, -1); return v;
    }
    /* compuestas / logicas */
    jv old, o = jundef(), key = jundef(); jstr *ks = 0;
    if (t->t == N_IDENT) old = lookup_ident(rt, t->str, sc, 0);
    else if (t->t == N_MEMBER) { o = eval_member_obj(rt, t, sc, &key, &ks); old = ks ? js_get(rt, o, ks) : js_getv(rt, o, key); }
    else js_throw_err(rt, E_SYNTAX, "destino de asignacion invalido");
    jv v;
    if (op == B_LAND) { if (!js_truthy(old)) return old; v = eval(rt, n->b, sc); }
    else if (op == B_LOR) { if (js_truthy(old)) return old; v = eval(rt, n->b, sc); }
    else if (op == B_NULLISH) { if (!IS_NULLISH(old)) return old; v = eval(rt, n->b, sc); }
    else v = bin_op(rt, op, old, eval(rt, n->b, sc));
    if (t->t == N_IDENT) assign_ident(rt, t->str, v, sc); else if (ks) js_put(rt, o, ks, v); else js_putv(rt, o, key, v);
    return v;
}
static jv eval_update(struct jrt *rt, struct jn *n, struct scope *sc)
{
    struct jn *t = n->a; jv old, o = jundef(), key = jundef(); jstr *ks = 0;
    if (t->t == N_IDENT) old = lookup_ident(rt, t->str, sc, 0);
    else if (t->t == N_MEMBER) { o = eval_member_obj(rt, t, sc, &key, &ks); old = ks ? js_get(rt, o, ks) : js_getv(rt, o, key); }
    else { js_throw_err(rt, E_SYNTAX, "destino invalido en ++/--"); return jundef(); }
    if (js_is_big(old)) { jv bn = js_big_inc(rt, old, n->op == '+' ? 1 : -1); if (t->t == N_IDENT) assign_ident(rt, t->str, bn, sc); else if (ks) js_put(rt, o, ks, bn); else js_putv(rt, o, key, bn); return n->fl & 1 ? bn : old; }   /* SOMA 188B */
    f64 x = js_tonum(rt, old), y = n->op == '+' ? f64_add(x, F64_ONE) : f64_sub(x, F64_ONE);
    jv nv = jnum(y);
    if (t->t == N_IDENT) assign_ident(rt, t->str, nv, sc); else if (ks) js_put(rt, o, ks, nv); else js_putv(rt, o, key, nv);
    return n->fl & 1 ? nv : jnum(x);
}
static jv eval_unary(struct jrt *rt, struct jn *n, struct scope *sc)
{
    switch (n->op) {
    case '!': return jbool(!js_truthy(eval(rt, n->a, sc)));
    case '-': { jv v = eval(rt, n->a, sc); if (js_is_big(v)) return js_big_neg(rt, v); return jnum(f64_neg(js_tonum(rt, v))); }
    case '+': return jnum(js_tonum(rt, eval(rt, n->a, sc)));
    case '~': { jv v = eval(rt, n->a, sc); if (js_is_big(v)) return js_big_not(rt, v); return jint(~f64_to_i32(js_tonum(rt, v))); }
    case 'v': eval(rt, n->a, sc); return jundef();
    case 't': {
        if (n->a->t == N_IDENT) return jstrv(js_intern_c(rt, js_typeof(rt, lookup_ident(rt, n->a->str, sc, 1))));
        return jstrv(js_intern_c(rt, js_typeof(rt, eval(rt, n->a, sc))));
    }
    case 'd': {
        struct jn *t = n->a;
        if (t->t == N_MEMBER) {
            jv key; jstr *ks; jv o = eval_member_obj(rt, t, sc, &key, &ks); if (o.t != JT_OBJ) return jbool(1);
            if (!ks) ks = js_tostr(rt, key);
            return jbool(js_delete(rt, o.u.o, ks));
        }
        return jbool(1);
    }
    }
    return jundef();
}
static jv eval_object(struct jrt *rt, struct jn *n, struct scope *sc)
{
    jobj *o = js_obj_new(rt, rt->p_object); jv ov = jobjv(o);
    for (struct jn *p = n->a; p; p = p->next) {
        if (p->t == N_SPREAD) {
            jv s = eval(rt, p->a, sc);
            if (s.t == JT_OBJ) {
                jobj *so = s.u.o;
                if (so->cls == JC_ARRAY) for (uint32_t i = 0; i < so->alen; i++) { char t[16]; int l = wu_itoa(i, t); js_def(rt, o, js_intern_n(rt, t, (uint32_t)l), so->el[i], JP_ENUM); }
                for (uint32_t i = 0; i < so->np; i++) { struct jprop *pp = &so->props[i]; if (!(pp->flags & JP_ENUM) || (pp->key->d[0] == '@' && pp->key->d[1] == '@')) continue; js_def(rt, o, pp->key, (pp->flags & JP_ACC) ? js_get(rt, s, pp->key) : pp->val, JP_ENUM); }
            } else if (s.t == JT_STR) { uint32_t n16 = js_u16len(s.u.s); for (uint32_t i = 0; i < n16; i++) { char t[16]; int l = wu_itoa(i, t); js_def(rt, o, js_intern_n(rt, t, (uint32_t)l), jstrv(js_u16_char(rt, s.u.s, i)), JP_ENUM); } }
            continue;
        }
        jstr *key = (p->fl & NF_COMPUTED) ? js_tostr(rt, eval(rt, p->a, sc)) : p->a->str; key = js_intern(rt, key);
        if (p->fl & (NF_GETTER | NF_SETTER)) {
            jv f = make_closure(rt, p->b, sc, o);
            js_def_acc(rt, o, key, (p->fl & NF_GETTER) ? f : jundef(), (p->fl & NF_SETTER) ? f : jundef(), JP_ENUM);
        } else if (p->fl & NF_METHOD) js_def(rt, o, key, make_closure(rt, p->b, sc, o), JP_ENUM);
        else {
            jv v = eval(rt, p->b, sc);
            if (key == rt->s_proto && !(p->fl & (NF_SHORT | NF_COMPUTED))) { if (v.t == JT_OBJ) o->proto = v.u.o; else if (v.t == JT_NULL) o->proto = 0; continue; }
            if (v.t == JT_OBJ && v.u.o->cls == JC_FUNC && v.u.o->u.fn.node && !v.u.o->u.fn.node->str && (p->b->t == N_FUNC)) { struct jn *fnn = v.u.o->u.fn.node; (void)fnn; }
            js_def(rt, o, key, v, JP_ENUM);
        }
    }
    return ov;
}
static jv eval_class(struct jrt *rt, struct jn *n, struct scope *sc);
static jv eval(struct jrt *rt, struct jn *n, struct scope *sc)
{
    JS_STEP(rt);
    switch (n->t) {
    case N_NUM: if (n->ival == 0x42 && n->str) return js_big_literal(rt, n->str); return jnum(n->num);
    case N_STR: return jstrv(n->str);
    case N_TPL: {
        jstr *r = n->a->str; struct jn *e = n->b, *s = n->a->next;
        while (e) { jv v = eval(rt, e, sc); r = js_str_concat(rt, r, js_tostr(rt, v.t == JT_OBJ ? js_toprim(rt, v, 1) : v)); if (s) { r = js_str_concat(rt, r, s->str); s = s->next; } e = e->next; }
        return jstrv(r);
    }
    case N_REGEX: return jobjv(js_regex_new(rt, n->str, (jstr *)n->p));
    case N_IDENT: return lookup_ident(rt, n->str, sc, 0);
    case N_THIS: { struct scope *f = fn_scope(sc); if (!f) return jobjv(rt->global); if (f->this_v.t == JT_UNDEF && f->callee && (f->callee->u.fn.node ? (f->callee->u.fn.node->fl & NF_DERIVED) : f->callee->u.fn.cls && (f->callee->u.fn.cls->fl & NF_DERIVED)) && f->newtarget) js_throw_err(rt, E_REFERENCE, "Debe llamar a super() antes de usar 'this'"); return f->this_v; }
    case N_NULL: return jnull(); case N_TRUE: return jbool(1); case N_FALSE: return jbool(0); case N_UNDEF: return jundef();
    case N_ARRAY: {
        jobj *a = js_arr_new(rt, 4);
        for (struct jn *e = n->a; e; e = e->next) {
            if (e->t == N_HOLE) js_arr_push_raw(rt, a, jhole());
            else if (e->t == N_SPREAD) js_iterate(rt, eval(rt, e->a, sc), spread_cb, a);
            else js_arr_push(rt, a, eval(rt, e, sc));
        }
        return jobjv(a);
    }
    case N_OBJECT: return eval_object(rt, n, sc);
    case N_FUNC: return make_closure(rt, n, sc, 0);
    case N_CLASS: return eval_class(rt, n, sc);
    case N_UNARY: return eval_unary(rt, n, sc);
    case N_UPDATE: return eval_update(rt, n, sc);
    case N_BINARY: { jv a = eval(rt, n->a, sc); jv b = eval(rt, n->b, sc); return bin_op(rt, n->op, a, b); }
    case N_LOGICAL: {
        jv a = eval(rt, n->a, sc);
        if (n->op == B_LAND) return js_truthy(a) ? eval(rt, n->b, sc) : a;
        if (n->op == B_LOR) return js_truthy(a) ? a : eval(rt, n->b, sc);
        return IS_NULLISH(a) ? eval(rt, n->b, sc) : a;
    }
    case N_ASSIGN: return eval_assign(rt, n, sc);
    case N_COND: return js_truthy(eval(rt, n->a, sc)) ? eval(rt, n->b, sc) : eval(rt, n->c, sc);
    case N_CALL: return eval_call(rt, n, sc);
    case N_NEW: {
        jv fn = eval(rt, n->a, sc); jv small[6]; int argc; jv *args = eval_args(rt, n->b, sc, &argc, small, 6);
        if (!js_is_callable(fn)) js_throw_err(rt, E_TYPE, "%s no es un constructor", n->a->t == N_IDENT ? n->a->str->d : "la expresion");
        rt->cur_line = n->line;
#ifdef WJS_DEBUG_COL
    rt->cur_col = n->col;
#endif
 return js_construct(rt, fn, args, argc, 0);
    }
    case N_MEMBER: {
        if (n->a->t == N_SUPER) { jstr *k = (n->fl & NF_SHORT) ? n->b->str : js_tostr(rt, eval(rt, n->b, sc)); return super_get(rt, sc, js_intern(rt, k)); }
        rt->sc_flag = 0;
        jv key; jstr *ks; jv o = eval_member_obj(rt, n, sc, &key, &ks);
        if (rt->sc_flag && (n->a->t == N_MEMBER || n->a->t == N_CALL)) return jundef();
        rt->sc_flag = 0;
        if ((n->fl & NF_OPT) && IS_NULLISH(o)) { rt->sc_flag = 1; return jundef(); }
        if (o.t <= JT_NULL) js_throw_err(rt, E_TYPE, "No se pueden leer propiedades de %s (leyendo '%s')", o.t == JT_NULL ? "null" : "undefined", ks ? ks->d : js_tostr(rt, key)->d);
        return ks ? js_get(rt, o, ks) : js_getv(rt, o, key);
    }
    case N_SEQ: { jv r = jundef(); for (struct jn *e = n->a; e; e = e->next) r = eval(rt, e, sc); return r; }
    case N_AWAIT: { jv v = eval(rt, n->a, sc); return js_await(rt, v); }
    case N_YIELD: { jv v = n->a ? eval(rt, n->a, sc) : jundef(); return n->op ? js_co_yield_star(rt, v) : js_co_yield(rt, v); }   /* SOMA_183 */
    case N_NEWTARGET: { struct scope *f = fn_scope(sc); return f && f->newtarget ? jobjv(f->newtarget) : jundef(); }
    case N_TAGGED: {
        jv fn = eval(rt, n->a, sc), thisv = jundef();
        if (n->a->t == N_MEMBER) { rt->sc_flag = 0; jv k; jstr *ks; thisv = eval_member_obj(rt, n->a, sc, &k, &ks); fn = ks ? js_get(rt, thisv, ks) : js_getv(rt, thisv, k); }
        /* SOMA_188: el objeto de cadenas es unico por punto del codigo y esta congelado (GetTemplateObject): lit-html
           cachea sus plantillas por esa identidad; antes cada render creaba otra y se rehacia el DOM (perdiendo el foco) */
        jobj *strs = (jobj *)n->p;
        if (!strs) {
            strs = js_arr_new(rt, 4); for (struct jn *s = n->b->a; s; s = s->next) js_arr_push(rt, strs, jstrv(s->str));
            jobj *raw = js_arr_new(rt, 4); for (struct jn *s = n->b->a; s; s = s->next) js_arr_push(rt, raw, jstrv(s->str));
            raw->frozen = 1; raw->extensible = 0;
            js_def(rt, strs, js_intern_c(rt, "raw"), jobjv(raw), JP_RO | JP_NOCONF);
            strs->frozen = 1; strs->extensible = 0; n->p = strs;
        }
        int cnt = 1; for (struct jn *e = n->b->b; e; e = e->next) cnt++;
        jv *args = (jv *)js_alloc(rt, (uint32_t)cnt * (uint32_t)sizeof(jv)); args[0] = jobjv(strs); int i = 1; for (struct jn *e = n->b->b; e; e = e->next) args[i++] = eval(rt, e, sc);
        return js_call(rt, fn, thisv, args, cnt);
    }
    case N_SPREAD: return eval(rt, n->a, sc);
    case N_SUPER: js_throw_err(rt, E_SYNTAX, "'super' no valido aqui"); return jundef();
    default: js_throw_err(rt, E_SYNTAX, "expresion no soportada (%d)", n->t); return jundef();
    }
}

/* ------------------------------ patrones ------------------------------ */
/* mode: -1 asignacion, 0 var, 1 let, 2 const */
static void bind_target(struct jrt *rt, struct jn *t, jv v, struct scope *sc, int mode)
{
    if (t->t == N_IDENT) { if (mode < 0) assign_ident(rt, t->str, v, sc); else declare(rt, sc, t->str, v, mode); return; }
    if (t->t == N_MEMBER && mode < 0) {
        jv key; jstr *ks; jv o = eval_member_obj(rt, t, sc, &key, &ks); if (ks) js_put(rt, o, ks, v); else js_putv(rt, o, key, v); return;
    }
    bind_pattern(rt, t, v, sc, mode);
}
static void bind_pattern(struct jrt *rt, struct jn *pat, jv val, struct scope *sc, int mode)
{
    js_check_stack(rt);
    if (pat->t == N_IDENT || pat->t == N_MEMBER) { bind_target(rt, pat, val, sc, mode); return; }
    if (pat->t == N_ASSIGN) { if (val.t == JT_UNDEF) val = eval(rt, pat->b, sc); bind_pattern(rt, pat->a, val, sc, mode); return; }
    if (pat->t == N_ARRAY) {
        if (IS_NULLISH(val)) js_throw_err(rt, E_TYPE, "%s no es iterable", val.t == JT_NULL ? "null" : "undefined");
        jobj *tmp = js_arr_new(rt, 4);
        if (val.t == JT_OBJ && val.u.o->cls == JC_ARRAY) tmp = val.u.o; else js_iterate(rt, val, spread_cb, tmp);
        uint32_t i = 0;
        for (struct jn *e = pat->a; e; e = e->next, i++) {
            if (e->t == N_HOLE) continue;
            if (e->t == N_SPREAD) { jobj *rest = js_arr_new(rt, 4); for (uint32_t k = i; k < tmp->alen; k++) js_arr_push(rt, rest, tmp->el[k]); bind_target(rt, e->a, jobjv(rest), sc, mode); break; }
            bind_pattern(rt, e, i < tmp->alen ? tmp->el[i] : jundef(), sc, mode);
        }
        return;
    }
    if (pat->t == N_OBJECT) {
        if (IS_NULLISH(val)) js_throw_err(rt, E_TYPE, "No se puede desestructurar '%s'", val.t == JT_NULL ? "null" : "undefined");
        jobj *used = js_obj_new(rt, 0);
        for (struct jn *p = pat->a; p; p = p->next) {
            if (p->t == N_SPREAD) {
                jobj *rest = js_obj_new(rt, rt->p_object);
                if (val.t == JT_OBJ) for (uint32_t i = 0; i < val.u.o->np; i++) { struct jprop *pp = &val.u.o->props[i]; if ((pp->flags & JP_ENUM) && !js_find_own(used, pp->key)) js_def(rt, rest, pp->key, (pp->flags & JP_ACC) ? js_get(rt, val, pp->key) : pp->val, JP_ENUM); }
                bind_target(rt, p->a, jobjv(rest), sc, mode); continue;
            }
            jstr *key = (p->fl & NF_COMPUTED) ? js_intern(rt, js_tostr(rt, eval(rt, p->a, sc))) : p->a->str; js_def(rt, used, key, jundef(), 0);
            jv v = js_get(rt, val, key);
            bind_pattern(rt, p->b, v, sc, mode);
        }
        return;
    }
    js_throw_err(rt, E_SYNTAX, "patron de destructuracion invalido");
}

/* ------------------------------ closures y llamadas ------------------------------ */
static jv make_closure(struct jrt *rt, struct jn *fn, struct scope *sc, jobj *home)
{
    jobj *f = js_obj_new(rt, rt->p_function); f->cls = JC_FUNC; f->u.fn.node = fn; f->u.fn.env = sc; f->u.fn.home = home;
    if (sc && !sc->heap) { /* un ambito de pila capturado: no deberia ocurrir (has_closure fuerza heap) */ }
    return jobjv(f);
}
jv js_call(struct jrt *rt, jv fn, jv thisv, jv *args, int argc)
{
    web_fault_point(2);                                            /* SOMA_198: solo pruebas (panic web 2) */
    if (fn.t != JT_OBJ) js_throw_err(rt, E_TYPE, "no es una funcion");
    jobj *f = fn.u.o;
    js_check_stack(rt);
    switch (f->cls) {
    case JC_NATIVE: rt->new_target = 0; rt->cur_native = f; return f->u.nat.f(rt, thisv, args, argc, f->magic);
    case JC_FUNC: return call_closure(rt, f, thisv, args, argc, 0);
    case JC_PROXY: return js_proxy_call(rt, f, thisv, args, argc);   /* SOMA_188 */
    case JC_BOUND: {
        uint32_t nb = f->alen > 2 ? f->alen - 2 : 0; jv *all = (jv *)js_alloc(rt, (nb + (uint32_t)argc + 1) * (uint32_t)sizeof(jv));
        for (uint32_t i = 0; i < nb; i++) all[i] = f->el[2 + i];
        for (int i = 0; i < argc; i++) all[nb + (uint32_t)i] = args[i];
        return js_call(rt, f->el[0], f->el[1], all, (int)nb + argc);
    }
    default: js_throw_err(rt, E_TYPE, "no es una funcion"); return jundef();
    }
}
static void init_fields(struct jrt *rt, jobj *ctor, jv self)
{
    if (!ctor->u.fn.cls || self.t != JT_OBJ) return;
    struct scope *cs = ctor->u.fn.env;
    for (struct jn *m = ctor->u.fn.cls->b; m; m = m->next) if ((m->fl & NF_FIELD) && !(m->fl & NF_STATIC)) {
        struct scope *fs = new_scope(rt, cs, 0, 0); fs->is_fn = 1; fs->this_v = self; fs->home = ctor->u.fn.home;
        jstr *key = (m->fl & NF_COMPUTED) ? js_tostr(rt, eval(rt, m->a, cs)) : m->a->str;
        js_def(rt, self.u.o, js_intern(rt, key), m->b ? eval(rt, m->b, fs) : jundef(), JP_ENUM);
    }
}
static jv construct_obj(struct jrt *rt, jv fnv, jv *args, int argc, jobj *nt)
{
    jobj *f = fnv.u.o; js_check_stack(rt);
    if (f->cls == JC_BOUND) return construct_obj(rt, f->el[0], args, argc, nt);
    if (!nt) nt = f;
    if (f->cls == JC_NATIVE) { rt->new_target = nt; rt->cur_native = f; jv r = f->u.nat.f(rt, jundef(), args, argc, f->magic); rt->new_target = 0; return r; }
    if (f->cls != JC_FUNC) js_throw_err(rt, E_TYPE, "no es un constructor");
    struct jn *node = f->u.fn.node;
    if (node && (node->fl & (NF_ARROW | NF_METHOD | NF_GETTER | NF_SETTER)) && !(node->fl & NF_CTOR)) js_throw_err(rt, E_TYPE, "%s no es un constructor", node->str ? node->str->d : "la funcion");
    int derived = f->u.fn.cls ? (f->u.fn.cls->fl & NF_DERIVED) != 0 : 0;
    if (!node) {   /* constructor por defecto de una clase */
        if (derived) {
            jobj *parent = f->proto; if (!parent || !js_is_callable(jobjv(parent))) js_throw_err(rt, E_TYPE, "La clase padre no es un constructor");
            jv r = construct_obj(rt, jobjv(parent), args, argc, nt); init_fields(rt, f, r); return r;
        }
        jv pr = js_get(rt, jobjv(nt), rt->s_prototype); jobj *o = js_obj_new(rt, pr.t == JT_OBJ ? pr.u.o : rt->p_object); init_fields(rt, f, jobjv(o)); return jobjv(o);
    }
    jv self = jundef();
    if (!derived) { jv pr = js_get(rt, jobjv(nt), rt->s_prototype); self = jobjv(js_obj_new(rt, pr.t == JT_OBJ ? pr.u.o : rt->p_object)); init_fields(rt, f, self); }
    jv r = call_closure(rt, f, self, args, argc, nt);
    return r;
}
jv js_construct(struct jrt *rt, jv fn, jv *args, int argc, jobj *newtarget)
{
    if (!js_is_callable(fn)) js_throw_err(rt, E_TYPE, "no es un constructor");
    if (fn.u.o->cls == JC_PROXY) return js_proxy_construct(rt, fn.u.o, args, argc, newtarget);   /* SOMA_188 */
    return construct_obj(rt, fn, args, argc, newtarget);
}
static int stmts_exec(struct jrt *rt, struct jn *s, struct scope *sc);
jv js_call_body(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc) { rt->co_body = 1; return call_closure(rt, fo, thisv, args, argc, 0); }   /* SOMA_183 */
static void hoist_funcs(struct jrt *rt, struct jn *s, struct scope *sc)
{
    for (; s; s = s->next) if (s->t == N_FUNCDECL && s->str) { jv f = make_closure(rt, s, sc, 0); if (sc->parent || sc->is_fn) { struct scope *t = sc; declare(rt, t, s->str, f, 0); } else js_put(rt, jobjv(rt->global), s->str, f); }
}
static int sloppy_this(void)   /* soma.jssloppy=off */
{
#if defined(WEB_HOST_TEST)
    extern char *getenv(const char *); static int v = -1; if (v < 0) { const char *o = getenv("SOMA_OFF"); v = !(o && strstr(o, "jssloppy")); } return v;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jssloppy") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}
static jv call_closure(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc, jobj *newtarget)
{
    struct jn *fn = fo->u.fn.node;
    if (!fn) { if (newtarget) return construct_obj(rt, jobjv(fo), args, argc, newtarget); js_throw_err(rt, E_TYPE, "El constructor de clase no puede llamarse sin 'new'"); }
    struct jfi *fi = (struct jfi *)fn->p;
    int sync_async = 0; int in_body = rt->co_body; rt->co_body = 0;   /* SOMA_183: cuerpo de una corrutina: se ejecuta como funcion normal */
    if (!fi->strict && !newtarget && !(fn->fl & NF_ARROW) && (thisv.t == JT_UNDEF || thisv.t == JT_NULL) && sloppy_this()) thisv = jobjv(rt->global);   /* SOMA 188B: en codigo no estricto, this sin receptor es el objeto global (ES 10.2.1.2 OrdinaryCallBindThis) */
    if (!in_body && js_co_available()) {
        if (fn->fl & NF_GEN) return js_gen_create(rt, fo, thisv, args, argc);
        if ((fn->fl & NF_ASYNC) && fi->has_await) { jv pr; if (js_async_try(rt, fo, thisv, args, argc, &pr)) return pr; sync_async = 1; rt->co_sync++; }
    }
    if (rt->depth > (web_big_mem() ? 800 : 400)) js_throw_err(rt, E_RANGE, "Maximum call stack size exceeded");   /* SOMA_188: 800 con memoria grande (la pila nativa sigue vigilada) */
    int arrow = (fn->fl & NF_ARROW) != 0; int is_async = (fn->fl & NF_ASYNC) != 0 && !in_body;
    int on_stack = !fi->has_closure && !is_async;
    char *save_top = rt->stop;
    struct scope *sc = new_scope(rt, fo->u.fn.env, fi->ncap + fi->nparams + 3, on_stack);
    sc->is_fn = 1; sc->arrow = (uint8_t)arrow;
    if (arrow) sc->is_fn = 0; /* las flechas heredan this/arguments del ambito exterior */
    if (!arrow) { sc->this_v = thisv; sc->callee = fo; sc->argv = args; sc->argc = argc; sc->newtarget = newtarget; sc->home = fo->u.fn.home; }
    jobj *promise = 0;
    if (is_async) promise = js_promise_new(rt);
    if (rt->depth >= 0 && rt->depth < 40) { rt->fstk[rt->depth] = fi->name; rt->fline[rt->depth] = rt->cur_line; }   /* SOMA_188 */
    rt->depth++;
    struct jtry tb; int caught = 0; jv result = jundef();
    if (is_async || !on_stack) {
        tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
        if (__builtin_setjmp(tb.jb)) { caught = 1; rt->depth = tb.depth; rt->stop = tb.sstop; }
    } else { tb.up = 0; }
    if (!caught) {
        /* SOMA_188: una expresion de funcion con nombre ve su propio nombre (parametros y var del mismo nombre lo tapan) */
        if (!arrow && fn->t == N_FUNC && fn->str && !(fn->fl & (NF_METHOD | NF_GETTER | NF_SETTER))) scope_add(rt, sc, fn->str, jobjv(fo), 0);
        /* variables hoisted (vn) incluyen los parametros */
        for (int i = 0; i < fi->nvn; i++) scope_add(rt, sc, fi->vn[i], jundef(), 0);
        if (fi->uses_args && !arrow) { jobj *a = js_array_from(rt, args, (uint32_t)argc); a->cls = JC_ARGS; scope_add(rt, sc, js_intern_c(rt, "arguments"), jobjv(a), 0); }
        int i = 0;
        for (struct jn *p = fn->a; p; p = p->next, i++) {
            if (p->t == N_IDENT) scope_add(rt, sc, p->str, i < argc ? args[i] : jundef(), 0);
            else if (p->t == N_SPREAD) { jobj *rest = js_arr_new(rt, 4); for (int k = i; k < argc; k++) js_arr_push(rt, rest, args[k]); bind_pattern(rt, p->a, jobjv(rest), sc, 0); }
            else bind_pattern(rt, p, i < argc ? args[i] : jundef(), sc, 0);
        }
        if (fn->fl & NF_EXPRBODY) result = eval(rt, fn->b, sc);
        else {
            hoist_funcs(rt, fn->b, sc);
            int c = stmts_exec(rt, fn->b, sc);
            if (c == C_RETURN) { result = rt->retval; rt->retval = jundef(); }
        }
    }
    if (is_async || !on_stack) { if (!caught) rt->try_top = tb.up; }
    rt->depth--; if (sync_async) rt->co_sync--;
    if (on_stack) rt->stop = save_top;
    if (is_async) {
        if (caught) js_promise_reject(rt, promise, tb.exc); else js_promise_resolve(rt, promise, result);
        return jobjv(promise);
    }
    if (caught) js_throw(rt, tb.exc);
    if (newtarget) {
        if (result.t == JT_OBJ) return result;
        if (fn->fl & NF_DERIVED || (fo->u.fn.cls && (fo->u.fn.cls->fl & NF_DERIVED))) {
            if (sc->this_v.t == JT_UNDEF && sc->is_fn) js_throw_err(rt, E_REFERENCE, "El constructor derivado debe llamar a super()");
            return sc->this_v;
        }
        return thisv;
    }
    return result;
}

/* ------------------------------ clases ------------------------------ */
static jv eval_class(struct jrt *rt, struct jn *n, struct scope *sc)
{
    jobj *parent = 0;
    if (n->a) {
        jv pv = eval(rt, n->a, sc);
        if (pv.t == JT_NULL) parent = 0; else if (!js_is_callable(pv)) js_throw_err(rt, E_TYPE, "La clase padre no es un constructor"); else parent = pv.u.o;
    }
    jobj *protoparent = rt->p_object;
    if (parent) { jv pp = js_get(rt, jobjv(parent), rt->s_prototype); protoparent = pp.t == JT_OBJ ? pp.u.o : 0; }
    jobj *proto = js_obj_new(rt, protoparent);
    struct scope *cs = new_scope(rt, sc, 2, 0);
    struct jn *ctor_node = 0; for (struct jn *m = n->b; m; m = m->next) if (m->fl & NF_CTOR) ctor_node = m->b;
    jobj *ctor = js_obj_new(rt, parent ? parent : rt->p_function); ctor->cls = JC_FUNC; ctor->u.fn.node = ctor_node; ctor->u.fn.env = cs; ctor->u.fn.home = proto; ctor->u.fn.cls = n;
    if (ctor_node) { ctor_node->str = n->str; ctor_node->fl |= NF_CTOR; }
    ctor->flag = 1;
    js_def(rt, ctor, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO);
    js_def(rt, proto, rt->s_constructor, jobjv(ctor), 0);
    if (n->str) { js_def(rt, ctor, rt->s_name, jstrv(n->str), JP_RO); scope_add(rt, cs, n->str, jobjv(ctor), 2); }
    /* SOMA_188: como manda ES2022: primero todos los metodos/accesores; despues campos estaticos y bloques static {} en orden */
    for (struct jn *m = n->b; m; m = m->next) {
        if (m->fl & (NF_CTOR | NF_FIELD)) continue;
        int st = (m->fl & NF_STATIC) != 0; jobj *target = st ? ctor : proto;
        jstr *key = (m->fl & NF_COMPUTED) ? js_intern(rt, js_tostr(rt, eval(rt, m->a, cs))) : m->a->str;
        jv f = make_closure(rt, m->b, cs, target);
        if (m->fl & (NF_GETTER | NF_SETTER)) js_def_acc(rt, target, key, (m->fl & NF_GETTER) ? f : jundef(), (m->fl & NF_SETTER) ? f : jundef(), 0);
        else js_def(rt, target, key, f, 0);
    }
    for (struct jn *m = n->b; m; m = m->next) {
        if (!(m->fl & NF_FIELD) || !(m->fl & NF_STATIC)) continue;
        if (!m->a) { jv f = make_closure(rt, m->b, cs, ctor); js_call(rt, f, jobjv(ctor), 0, 0); continue; }   /* static { ... } */
        struct scope *fs = new_scope(rt, cs, 0, 0); fs->is_fn = 1; fs->this_v = jobjv(ctor); fs->home = ctor;
        jstr *key = (m->fl & NF_COMPUTED) ? js_tostr(rt, eval(rt, m->a, cs)) : m->a->str;
        js_def(rt, ctor, js_intern(rt, key), m->b ? eval(rt, m->b, fs) : jundef(), JP_ENUM);
    }
    return jobjv(ctor);
}

/* ------------------------------ sentencias ------------------------------ */
static int block_needs_scope(struct jn *b) { return b->ival > 0; }
static int exec_block(struct jrt *rt, struct jn *stmts, int cap, struct scope *sc)
{
    if (cap > 0) {
        char *save_top = rt->stop;
        struct scope *bs = new_scope(rt, sc, cap, 0);   /* siempre heap: los cierres pueden capturarlo */
        hoist_funcs(rt, stmts, bs);
        int c = stmts_exec(rt, stmts, bs);
        rt->stop = save_top; return c;
    }
    hoist_funcs(rt, stmts, sc);
    return stmts_exec(rt, stmts, sc);
}
static int stmts_exec(struct jrt *rt, struct jn *s, struct scope *sc)
{
    for (; s; s = s->next) { int c = exec(rt, s, sc); if (c != C_NORMAL) return c; }
    return C_NORMAL;
}
struct forin_ctx { jobj *keys; };
static void collect_keys(struct jrt *rt, jv v, jobj *out, int own_only)
{
    if (v.t == JT_STR) { uint32_t n16 = js_u16len(v.u.s); for (uint32_t i = 0; i < n16; i++) { char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_str_new(rt, t, (uint32_t)l))); } return; }
    if (v.t != JT_OBJ) return;
    for (jobj *o = v.u.o; o; o = o->proto) {
        if (o->cls == JC_ARRAY || o->cls == JC_ARGS) for (uint32_t i = 0; i < o->alen; i++) { if (IS_HOLE(o->el[i])) continue; char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_str_new(rt, t, (uint32_t)l))); }
        if (o->cls == JC_TYPED) for (uint32_t i = 0; i < js_ta_length(o); i++) { char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_str_new(rt, t, (uint32_t)l))); }   /* SOMA_186 */
        for (uint32_t i = 0; i < o->np; i++) {
            struct jprop *p = &o->props[i]; if (!(p->flags & JP_ENUM) || (p->key->d[0] == '@' && p->key->d[1] == '@')) continue;
            int dup = 0; for (uint32_t k = 0; k < out->alen; k++) if (out->el[k].u.s == p->key) { dup = 1; break; }
            if (!dup) js_arr_push(rt, out, jstrv(p->key));
        }
        if (own_only) break;
    }
}
static int exec_loop_body(struct jrt *rt, struct jn *body, struct scope *sc, jstr *mylabel, int *brk)
{
    int c = exec(rt, body, sc); *brk = 0;
    if (c == C_BREAK) { if (!rt->brk_label || rt->brk_label == mylabel) { rt->brk_label = 0; *brk = 1; return C_NORMAL; } *brk = 1; return C_BREAK; }
    if (c == C_CONTINUE) { if (!rt->brk_label || rt->brk_label == mylabel) { rt->brk_label = 0; return C_NORMAL; } *brk = 1; return C_CONTINUE; }
    if (c == C_RETURN) { *brk = 1; return C_RETURN; }
    return C_NORMAL;
}
struct forof_ctx { struct jrt *rt; struct jn *n; struct scope *sc; jstr *label; int code; };
static int forof_item(struct jrt *rt, jv item, void *ud)
{
    struct forof_ctx *f = (struct forof_ctx *)ud; struct jn *n = f->n;
    struct scope *is = new_scope(rt, f->sc, n->ival + 1, 0);
    struct jn *decl = n->a;
    if (decl->t == N_VAR) bind_pattern(rt, decl->a->a, item, is, decl->op); else bind_pattern(rt, decl, item, is, -1);
    int brk; int c = exec_loop_body(rt, n->c, is, f->label, &brk);
    if (brk) { f->code = c; return 0; }
    return 1;
}
/* SOMA_183: for await (x of iterable): iterador asincrono si lo hay; si no, el sincrono esperando cada valor */
static int forof_await_item(struct jrt *rt, jv item, void *ud) { return forof_item(rt, js_await(rt, item), ud); }
static int forawait(struct jrt *rt, jv it, struct forof_ctx *f)
{
    jv m = it.t == JT_OBJ ? js_get(rt, it, js_intern_c(rt, "@@asyncIterator")) : jundef();
    if (!js_is_callable(m)) { js_iterate(rt, it, forof_await_item, f); return f->code; }
    jv iter = js_call(rt, m, it, 0, 0); jv nx = js_get(rt, iter, rt->s_next);
    for (;;) {
        JS_STEP(rt);
        jv r = js_await(rt, js_call(rt, nx, iter, 0, 0));
        if (r.t != JT_OBJ) js_throw_err(rt, E_TYPE, "el resultado del iterador no es un objeto");
        if (js_truthy(js_get(rt, r, rt->s_done))) break;
        if (!forof_item(rt, js_get(rt, r, rt->s_value), f)) {
            jv rf = js_get(rt, iter, js_intern_c(rt, "return")); if (js_is_callable(rf)) js_await(rt, js_call(rt, rf, iter, 0, 0));
            break;
        }
    }
    return f->code;
}
static int exec(struct jrt *rt, struct jn *n, struct scope *sc)
{
    JS_STEP(rt);
    switch (n->t) {
    case N_EXPR: rt->cur_line = n->line;
#ifdef WJS_DEBUG_COL
    rt->cur_col = n->col;
#endif
 eval(rt, n->a, sc); return C_NORMAL;
    case N_VAR:
        for (struct jn *d = n->a; d; d = d->next) {
            if (!d->b) { if (n->op != 0) bind_pattern(rt, d->a, jundef(), sc, n->op); continue; }
            jv v = eval(rt, d->b, sc);
            bind_pattern(rt, d->a, v, sc, n->op);
        }
        return C_NORMAL;
    case N_BLOCK: return exec_block(rt, n->a, n->ival, sc);
    case N_IF: if (js_truthy(eval(rt, n->a, sc))) return exec(rt, n->b, sc); else if (n->c) return exec(rt, n->c, sc); return C_NORMAL;
    case N_RETURN: rt->retval = n->a ? eval(rt, n->a, sc) : jundef(); return C_RETURN;
    case N_BREAK: rt->brk_label = n->str; return C_BREAK;
    case N_CONTINUE: rt->brk_label = n->str; return C_CONTINUE;
    case N_THROW: js_throw(rt, eval(rt, n->a, sc)); return C_NORMAL;
    case N_EMPTY: return C_NORMAL;
    case N_FUNCDECL: return C_NORMAL;
    case N_CLASSDECL: { jv c = eval_class(rt, n, sc); if (n->str) declare(rt, sc, n->str, c, 1); return C_NORMAL; }
    case N_LABEL: {
        struct jn *b = n->a;
        if (b->t == N_FOR || b->t == N_WHILE || b->t == N_DO || b->t == N_FOROF || b->t == N_FORIN) { b->str = n->str; return exec(rt, b, sc); }
        int c = exec(rt, b, sc); if (c == C_BREAK && rt->brk_label == n->str) { rt->brk_label = 0; return C_NORMAL; } return c;
    }
    case N_WITH: {   /* SOMA_188 */
        jv o = eval(rt, n->a, sc);
        if (o.t == JT_UNDEF || o.t == JT_NULL) js_throw_err(rt, E_TYPE, "with sobre %s", o.t == JT_NULL ? "null" : "undefined");
        if (o.t != JT_OBJ) { jobj *bx = js_obj_new(rt, proto_of_prim(rt, o).u.o); o = jobjv(bx); }   /* primitivo: sus metodos via prototipo */
        struct scope *ws = new_scope(rt, sc, 0, 0); ws->with_obj = o.u.o; ws->has_with = 1;
        return exec(rt, n->b, ws);
    }
    case N_WHILE: {
        for (;;) {
            if (!js_truthy(eval(rt, n->a, sc))) break;
            int brk; int c = exec_loop_body(rt, n->b, sc, n->str, &brk); if (brk) { if (c == C_NORMAL) break; return c; }
        }
        return C_NORMAL;
    }
    case N_DO: {
        for (;;) {
            int brk; int c = exec_loop_body(rt, n->b, sc, n->str, &brk); if (brk) { if (c == C_NORMAL) break; return c; }
            if (!js_truthy(eval(rt, n->a, sc))) break;
        }
        return C_NORMAL;
    }
    case N_FOR: {
        struct scope *ls = sc; int fresh = 0; struct jn *init = n->a;
        if (init && init->t == N_VAR && init->op != 0) { ls = new_scope(rt, sc, n->ival + 1, 0); fresh = 1; }
        if (init) { if (init->t == N_VAR) exec(rt, init, ls); else eval(rt, init, ls); }
        for (;;) {
            if (n->b && !js_truthy(eval(rt, n->b, ls))) break;
            int brk; int c = exec_loop_body(rt, n->d, ls, n->str, &brk); if (brk) { if (c == C_NORMAL) break; return c; }
            if (fresh && n->ival > 0) {   /* copia por iteracion para que los cierres capturen su propia variable */
                struct scope *nx = new_scope(rt, sc, n->ival + 1, 0);
                for (uint32_t i = 0; i < ls->n; i++) scope_add(rt, nx, ls->vars[i].name, ls->vars[i].v, ls->vars[i].kind);
                ls = nx;
            }
            if (n->c) eval(rt, n->c, ls);
        }
        return C_NORMAL;
    }
    case N_FORIN: {
        jv obj = eval(rt, n->b, sc); if (IS_NULLISH(obj)) return C_NORMAL;
        jobj *keys = js_arr_new(rt, 8); collect_keys(rt, obj, keys, 0);
        for (uint32_t i = 0; i < keys->alen; i++) {
            struct scope *is = new_scope(rt, sc, n->ival + 1, 0); struct jn *decl = n->a;
            if (decl->t == N_VAR) bind_pattern(rt, decl->a->a, keys->el[i], is, decl->op); else bind_pattern(rt, decl, keys->el[i], is, -1);
            int brk; int c = exec_loop_body(rt, n->c, is, n->str, &brk); if (brk) { if (c == C_NORMAL) break; return c; }
        }
        return C_NORMAL;
    }
    case N_FOROF: {
        jv it = eval(rt, n->b, sc); struct forof_ctx f; f.rt = rt; f.n = n; f.sc = sc; f.label = n->str; f.code = C_NORMAL;
        if (n->op == 1) return forawait(rt, it, &f);   /* SOMA_183: for await */
        js_iterate(rt, it, forof_item, &f); return f.code;
    }
    case N_SWITCH: {
        jv d = eval(rt, n->a, sc); struct scope *bs = n->ival > 0 ? new_scope(rt, sc, n->ival, 0) : sc;
        struct jn *start = 0;
        for (struct jn *c = n->b; c && !start; c = c->next) if (!(c->fl & 1) && js_strict_eq(d, eval(rt, c->a, bs))) start = c;
        if (!start) for (struct jn *c = n->b; c; c = c->next) if (c->fl & 1) { start = c; break; }
        for (struct jn *c = start; c; c = c->next) {
            hoist_funcs(rt, c->b, bs);
            int r = stmts_exec(rt, c->b, bs);
            if (r == C_BREAK && !rt->brk_label) return C_NORMAL;
            if (r != C_NORMAL) return r;
        }
        return C_NORMAL;
    }
    case N_TRY: {
        struct jtry tb; int code = C_NORMAL; int caught = 0; int pending_throw = 0; jv exc = jundef();
        jv saved_ret = jundef();
        tb.up = rt->try_top; tb.depth = rt->depth; tb.sstop = rt->stop; rt->try_top = &tb;
        if (__builtin_setjmp(tb.jb)) { caught = 1; rt->depth = tb.depth; rt->stop = tb.sstop; exc = tb.exc; }
        else { code = exec(rt, n->a, sc); rt->try_top = tb.up; }
        if (caught && n->b && !rt->gen_unwind) {   /* SOMA_183: el desenrollado de return() de un generador no entra en los catch */
            /* catch */
            struct jtry tb2; int caught2 = 0;
            if (n->c) { tb2.up = rt->try_top; tb2.depth = rt->depth; tb2.sstop = rt->stop; rt->try_top = &tb2; if (__builtin_setjmp(tb2.jb)) { caught2 = 1; rt->depth = tb2.depth; rt->stop = tb2.sstop; } }
            if (!caught2) {
                struct scope *cs = new_scope(rt, sc, n->ival + 1, 0);
                if (n->d) bind_pattern(rt, n->d, exc, cs, 1);
                code = exec_block(rt, n->b->a, n->b->ival, cs);
                if (n->c) rt->try_top = tb2.up;
            } else { pending_throw = 1; exc = tb2.exc; }
        } else if (caught) { pending_throw = 1; }
        if (n->c) {
            saved_ret = rt->retval; jstr *sl = rt->brk_label; int gu = rt->gen_unwind; rt->gen_unwind = 0;
            int fc = exec(rt, n->c, sc);
            if (fc != C_NORMAL) return fc;
            rt->retval = saved_ret; rt->brk_label = sl; rt->gen_unwind = gu;
        }
        if (pending_throw) js_throw(rt, exc);
        return code;
    }
    default: eval(rt, n, sc); return C_NORMAL;
    }
}

/* ------------------------------ programa ------------------------------ */
jv js_run_program(struct jrt *rt, struct jn *prog)
{
    struct jfi *fi = (struct jfi *)prog->p;
    for (int i = 0; i < fi->nvn; i++) { if (!js_find_own(rt->global, fi->vn[i])) js_def(rt, rt->global, fi->vn[i], jundef(), JP_ENUM); }
    hoist_funcs(rt, prog->b, rt->gscope);
    jv last = jundef();
    for (struct jn *s = prog->b; s; s = s->next) {
        if (s->t == N_EXPR) { rt->cur_line = s->line;
#ifdef WJS_DEBUG_COL
    rt->cur_col = s->col;
#endif
 last = eval(rt, s->a, rt->gscope); continue; }
        int c = exec(rt, s, rt->gscope);
        if (c == C_RETURN) break;
    }
    return last;
}
