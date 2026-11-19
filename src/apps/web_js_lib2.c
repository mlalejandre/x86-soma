/* SOMA_PARCHE_NAVEGADOR: Array, String e iteradores. */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())
jv js_regex_string_op(struct jrt *rt, int op, jstr *s, jv *args, int argc);
jobj *js_own_keys(struct jrt *rt, jv v, int all);

static jobj *mkctor2(struct jrt *rt, const char *name, jnative f, int nargs, int magic, jobj *proto)
{
    jobj *c = js_native(rt, name, f, nargs, magic);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0); return c;
}
/* ------------------------------ iteradores ------------------------------ */
enum { IT_VALUES, IT_KEYS, IT_ENTRIES };
static jv make_iter(struct jrt *rt, jv src, int kind)
{
    jobj *it = js_obj_new(rt, rt->p_iter); it->el = (jv *)js_alloc(rt, 3 * (uint32_t)sizeof(jv)); it->alen = 3; it->el[0] = src; it->el[1] = jint(0); it->magic = (int16_t)kind; return jobjv(it);
}
static jv iter_next(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_OBJ || !thisv.u.o->el) js_throw_err(rt, E_TYPE, "next llamado sobre un valor que no es iterador");
    jobj *it = thisv.u.o; jv src = it->el[0]; uint32_t i = (uint32_t)f64_to_i64(it->el[1].u.n); int kind = it->magic;
    if (src.t == JT_OBJ && (src.u.o->cls == JC_ARRAY || src.u.o->cls == JC_ARGS || src.u.o->cls == JC_SET)) {
        jobj *a = src.u.o; if (i >= a->alen) { return js_make_iter_result(rt, jundef(), 1); }
        it->el[1] = jint(i + 1); jv v = a->el[i];
        if (a->cls == JC_SET) { if (kind == IT_ENTRIES) { jv pr[2] = { v, v }; return js_make_iter_result(rt, jobjv(js_array_from(rt, pr, 2)), 0); } return js_make_iter_result(rt, v, 0); }
        if (kind == IT_KEYS) return js_make_iter_result(rt, jint(i), 0);
        if (kind == IT_ENTRIES) { jv pr[2] = { jint(i), v }; return js_make_iter_result(rt, jobjv(js_array_from(rt, pr, 2)), 0); }
        return js_make_iter_result(rt, v, 0);
    }
    if (src.t == JT_OBJ && src.u.o->cls == JC_MAP) {
        jobj *m = src.u.o; uint32_t p = i * 2; if (p + 1 >= m->alen) return js_make_iter_result(rt, jundef(), 1);
        it->el[1] = jint(i + 1);
        if (kind == IT_KEYS) return js_make_iter_result(rt, m->el[p], 0);
        if (kind == IT_VALUES) return js_make_iter_result(rt, m->el[p + 1], 0);
        jv pr[2] = { m->el[p], m->el[p + 1] }; return js_make_iter_result(rt, jobjv(js_array_from(rt, pr, 2)), 0);
    }
    if (src.t == JT_OBJ && src.u.o->cls == JC_TYPED) {                          /* SOMA_186 */
        jobj *a = src.u.o; if (i >= js_ta_length(a)) return js_make_iter_result(rt, jundef(), 1);
        it->el[1] = jint(i + 1); jv v = js_ta_get(a, i);
        if (kind == IT_KEYS) return js_make_iter_result(rt, jint(i), 0);
        if (kind == IT_ENTRIES) { jv pr[2] = { jint(i), v }; return js_make_iter_result(rt, jobjv(js_array_from(rt, pr, 2)), 0); }
        return js_make_iter_result(rt, v, 0);
    }
    if (src.t == JT_STR) {
        jstr *s = src.u.s; if (i >= s->len) return js_make_iter_result(rt, jundef(), 1);
        uint32_t adv; wu_utf8_next(s->d + i, &adv); if (!adv) adv = 1; if (i + adv > s->len) adv = s->len - i; it->el[1] = jint(i + adv);
        return js_make_iter_result(rt, jstrv(js_str_new(rt, s->d + i, adv)), 0);
    }
    return js_make_iter_result(rt, jundef(), 1);
}
static jv iter_self(struct jrt *rt, jv thisv, jv *args, int argc, int magic) { return thisv; }
jv js_make_iterator(struct jrt *rt, jv src, int kind) { return make_iter(rt, src, kind); }

/* ------------------------------ Array ------------------------------ */
enum { A_PUSH, A_POP, A_SHIFT, A_UNSHIFT, A_SLICE, A_SPLICE, A_CONCAT, A_JOIN, A_REVERSE, A_INDEXOF, A_LASTINDEXOF, A_INCLUDES, A_FIND, A_FINDINDEX, A_FINDLAST, A_FINDLASTINDEX,
       A_FILTER, A_MAP, A_FOREACH, A_REDUCE, A_REDUCERIGHT, A_SOME, A_EVERY, A_SORT, A_FLAT, A_FLATMAP, A_FILL, A_KEYS, A_VALUES, A_ENTRIES, A_AT, A_TOSTRING, A_COPYWITHIN, A_TOSORTED, A_TOREVERSED, A_WITH };
static jobj *as_array(struct jrt *rt, jv t, int must_real)
{
    if (t.t == JT_OBJ && (t.u.o->cls == JC_ARRAY || t.u.o->cls == JC_ARGS)) return t.u.o;
    if (IS_NULLISH(t)) js_throw_err(rt, E_TYPE, "Array.prototype llamado sobre null o undefined");
    if (must_real) js_throw_err(rt, E_TYPE, "metodo de Array llamado sobre un objeto que no es array");
    jobj *a = js_arr_new(rt, 4);
    if (t.t == JT_STR) { for (uint32_t i = 0; i < t.u.s->len; i++) js_arr_push(rt, a, jstrv(js_str_new(rt, t.u.s->d + i, 1))); return a; }
    if (t.t == JT_OBJ) { jv l = js_get(rt, t, rt->s_length); int64_t n = f64_to_i64(js_tonum(rt, l)); if (n > 1000000) n = 1000000; for (int64_t i = 0; i < n; i++) js_arr_push(rt, a, js_getv(rt, t, jint(i))); }
    return a;
}
static int64_t rel_index(struct jrt *rt, jv v, int64_t len, int64_t def)
{
    if (IS_UNDEF(v)) return def;
    f64 n = js_tonum(rt, v); if (f64_is_nan(n)) return 0;
    if (f64_is_inf(n)) return (n >> 63) ? 0 : len;
    int64_t i = f64_to_i64(n); if (i < 0) { i += len; if (i < 0) i = 0; } if (i > len) i = len; return i;
}
static void arr_insert(struct jrt *rt, jobj *a, uint32_t at, jv *items, uint32_t n)
{
    uint32_t old = a->alen; for (uint32_t i = 0; i < n; i++) js_arr_push(rt, a, jundef());
    memmove(&a->el[at + n], &a->el[at], (old - at) * sizeof(jv)); for (uint32_t i = 0; i < n; i++) a->el[at + i] = items[i];
}
struct sortctx { struct jrt *rt; jv cmp; };
static int sort_cmp(struct sortctx *c, jv a, jv b)
{
    if (a.t == JT_UNDEF) return b.t == JT_UNDEF ? 0 : 1;
    if (b.t == JT_UNDEF) return -1;
    if (c->cmp.t != JT_UNDEF) { jv args[2] = { a, b }; f64 r = js_tonum(c->rt, js_call(c->rt, c->cmp, jundef(), args, 2)); return f64_lt(r, F64_ZERO) ? -1 : f64_lt(F64_ZERO, r) ? 1 : 0; }
    jstr *x = js_tostr(c->rt, a), *y = js_tostr(c->rt, b); uint32_t n = x->len < y->len ? x->len : y->len; int r = memcmp(x->d, y->d, n); if (r) return r < 0 ? -1 : 1; return x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
}
static void msort(struct sortctx *c, jv *a, jv *tmp, uint32_t n)
{
    if (n < 2) return;
    if (n <= 8) { for (uint32_t i = 1; i < n; i++) { jv v = a[i]; uint32_t j = i; while (j > 0 && sort_cmp(c, a[j - 1], v) > 0) { a[j] = a[j - 1]; j--; } a[j] = v; } return; }
    uint32_t m = n / 2; msort(c, a, tmp, m); msort(c, a + m, tmp, n - m);
    uint32_t i = 0, j = m, k = 0; while (i < m && j < n) tmp[k++] = sort_cmp(c, a[i], a[j]) <= 0 ? a[i++] : a[j++];
    while (i < m) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, n * sizeof(jv));
}
static void flatten(struct jrt *rt, jobj *out, jobj *src, int depth)
{
    for (uint32_t i = 0; i < src->alen; i++) { jv v = src->el[i]; if (depth > 0 && v.t == JT_OBJ && v.u.o->cls == JC_ARRAY) flatten(rt, out, v.u.o, depth - 1); else js_arr_push(rt, out, v); }
}
static jstr *join_arr(struct jrt *rt, jobj *a, jstr *sep, int depth)
{
    struct wb b = {0};
    for (uint32_t i = 0; i < a->alen; i++) {
        if (i) wb_addn(&b, sep->d, sep->len);
        jv v = a->el[i];
        if (IS_NULLISH(v)) continue;
        if (v.t == JT_OBJ && v.u.o->cls == JC_ARRAY && depth < 6) { jstr *s = join_arr(rt, v.u.o, js_intern_c(rt, ","), depth + 1); wb_addn(&b, s->d, s->len); }
        else { jstr *s = js_tostr(rt, v); wb_addn(&b, s->d, s->len); }
    }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
/* SOMA_188: push/pop/shift/unshift/splice genericos sobre objetos tipo array (jQuery, Backbone, arguments-like, Proxy) */
static int64_t gen_len(struct jrt *rt, jv t) { f64 n = js_tonum(rt, js_get(rt, t, rt->s_length)); int64_t l = f64_is_finite(n) ? f64_to_i64(n) : 0; return l < 0 ? 0 : l > 10000000 ? 10000000 : l; }
static jstr *ixs(struct jrt *rt, int64_t i) { char b[24]; int l = wu_itoa(i, b); return js_intern_n(rt, b, (uint32_t)l); }
static void gen_setlen(struct jrt *rt, jv t, int64_t n) { js_put(rt, t, rt->s_length, jnum(f64_from_i64(n))); }
static jv array_generic_mut(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    if (IS_NULLISH(t)) js_throw_err(rt, E_TYPE, "Array.prototype llamado sobre null o undefined");
    if (t.t != JT_OBJ) return jundef();
    jobj *o = t.u.o; int64_t len = gen_len(rt, t);
    switch (magic) {
    case A_PUSH: for (int i = 0; i < argc; i++) js_put(rt, t, ixs(rt, len + i), args[i]); gen_setlen(rt, t, len + argc); return jnum(f64_from_i64(len + argc));
    case A_POP: { if (!len) { gen_setlen(rt, t, 0); return jundef(); } jstr *k = ixs(rt, len - 1); jv v = js_get(rt, t, k); js_delete(rt, o, k); gen_setlen(rt, t, len - 1); return v; }
    case A_SHIFT: {
        if (!len) { gen_setlen(rt, t, 0); return jundef(); }
        jv first = js_get(rt, t, ixs(rt, 0));
        for (int64_t k = 1; k < len; k++) { jstr *from = ixs(rt, k), *to = ixs(rt, k - 1); if (js_has(rt, o, from)) js_put(rt, t, to, js_get(rt, t, from)); else js_delete(rt, o, to); }
        js_delete(rt, o, ixs(rt, len - 1)); gen_setlen(rt, t, len - 1); return first;
    }
    case A_UNSHIFT: {
        if (argc) for (int64_t k = len; k > 0; k--) { jstr *from = ixs(rt, k - 1), *to = ixs(rt, k - 1 + argc); if (js_has(rt, o, from)) js_put(rt, t, to, js_get(rt, t, from)); else js_delete(rt, o, to); }
        for (int i = 0; i < argc; i++) js_put(rt, t, ixs(rt, i), args[i]);
        gen_setlen(rt, t, len + argc); return jnum(f64_from_i64(len + argc));
    }
    default: {   /* splice */
        int64_t st = rel_index(rt, ARG(0), len, 0);
        int64_t dc = argc == 0 ? 0 : argc == 1 ? len - st : f64_to_i64(js_tonum(rt, args[1])); if (dc < 0) dc = 0; if (dc > len - st) dc = len - st;
        int ni = argc > 2 ? argc - 2 : 0;
        jobj *rem = js_arr_new(rt, (uint32_t)dc + 1); for (int64_t k = 0; k < dc; k++) js_arr_push(rt, rem, js_get(rt, t, ixs(rt, st + k)));
        if (ni < dc) {
            for (int64_t k = st; k < len - dc; k++) { jstr *from = ixs(rt, k + dc), *to = ixs(rt, k + ni); if (js_has(rt, o, from)) js_put(rt, t, to, js_get(rt, t, from)); else js_delete(rt, o, to); }
            for (int64_t k = len; k > len - dc + ni; k--) js_delete(rt, o, ixs(rt, k - 1));
        } else if (ni > dc) {
            for (int64_t k = len - dc; k > st; k--) { jstr *from = ixs(rt, k + dc - 1), *to = ixs(rt, k + ni - 1); if (js_has(rt, o, from)) js_put(rt, t, to, js_get(rt, t, from)); else js_delete(rt, o, to); }
        }
        for (int i = 0; i < ni; i++) js_put(rt, t, ixs(rt, st + i), args[i + 2]);
        gen_setlen(rt, t, len - dc + ni); return jobjv(rem);
    }
    }
}
static jv array_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    JS_STEP(rt);
    if ((magic == A_PUSH || magic == A_POP || magic == A_SHIFT || magic == A_UNSHIFT || magic == A_SPLICE) && !(thisv.t == JT_OBJ && (thisv.u.o->cls == JC_ARRAY || thisv.u.o->cls == JC_ARGS)))
        return array_generic_mut(rt, thisv, args, argc, magic);
    switch (magic) {
    case A_PUSH: { jobj *a = as_array(rt, thisv, 1); if (a->frozen) js_throw_err(rt, E_TYPE, "No se puede anadir una propiedad a un objeto congelado"); for (int i = 0; i < argc; i++) js_arr_push(rt, a, args[i]); return jint(a->alen); }
    case A_POP: { jobj *a = as_array(rt, thisv, 1); if (!a->alen || a->frozen) return jundef(); return a->el[--a->alen]; }
    case A_SHIFT: { jobj *a = as_array(rt, thisv, 1); if (!a->alen || a->frozen) return jundef(); jv v = a->el[0]; memmove(a->el, a->el + 1, (a->alen - 1) * sizeof(jv)); a->alen--; return v; }
    case A_UNSHIFT: { jobj *a = as_array(rt, thisv, 1); if (a->frozen) return jint(a->alen); arr_insert(rt, a, 0, args, (uint32_t)argc); return jint(a->alen); }
    case A_SLICE: { jobj *a = as_array(rt, thisv, 0); int64_t s = rel_index(rt, ARG(0), a->alen, 0), e = rel_index(rt, ARG(1), a->alen, a->alen); jobj *r = js_arr_new(rt, e > s ? (uint32_t)(e - s) : 0); for (int64_t i = s; i < e; i++) js_arr_push_raw(rt, r, a->el[i]); return jobjv(r); }
    case A_SPLICE: {
        jobj *a = as_array(rt, thisv, 1); int64_t s = rel_index(rt, ARG(0), a->alen, 0); int64_t dc = argc == 0 ? 0 : argc == 1 ? (int64_t)a->alen - s : f64_to_i64(js_tonum(rt, args[1])); if (dc < 0) dc = 0; if (dc > (int64_t)a->alen - s) dc = (int64_t)a->alen - s;
        if (a->frozen) return jobjv(js_arr_new(rt, 0));
        jobj *rem = js_arr_new(rt, (uint32_t)dc); for (int64_t i = 0; i < dc; i++) js_arr_push(rt, rem, a->el[s + i]);
        uint32_t ni = argc > 2 ? (uint32_t)argc - 2 : 0; int64_t delta = (int64_t)ni - dc;
        if (delta > 0) { uint32_t old = a->alen; for (int64_t i = 0; i < delta; i++) js_arr_push(rt, a, jundef()); memmove(&a->el[s + dc + delta], &a->el[s + dc], (old - (uint32_t)(s + dc)) * sizeof(jv)); }
        else if (delta < 0) { memmove(&a->el[s + ni], &a->el[s + dc], (a->alen - (uint32_t)(s + dc)) * sizeof(jv)); a->alen = (uint32_t)((int64_t)a->alen + delta); }
        for (uint32_t i = 0; i < ni; i++) a->el[s + i] = args[2 + i];
        return jobjv(rem);
    }
    case A_CONCAT: {
        jobj *a = as_array(rt, thisv, 0), *r = js_arr_new(rt, a->alen + 4); for (uint32_t i = 0; i < a->alen; i++) js_arr_push_raw(rt, r, a->el[i]);
        for (int k = 0; k < argc; k++) { if (args[k].t == JT_OBJ && args[k].u.o->cls == JC_ARRAY) for (uint32_t i = 0; i < args[k].u.o->alen; i++) js_arr_push_raw(rt, r, args[k].u.o->el[i]); else js_arr_push(rt, r, args[k]); }
        return jobjv(r);
    }
    case A_JOIN: return jstrv(join_arr(rt, as_array(rt, thisv, 0), IS_UNDEF(ARG(0)) ? js_intern_c(rt, ",") : js_tostr(rt, args[0]), 0));
    case A_TOSTRING: return jstrv(join_arr(rt, as_array(rt, thisv, 0), js_intern_c(rt, ","), 0));
    case A_REVERSE: { jobj *a = as_array(rt, thisv, 1); if (a->frozen) return thisv; for (uint32_t i = 0, j = a->alen; i + 1 < j; i++, j--) { jv t = a->el[i]; a->el[i] = a->el[j - 1]; a->el[j - 1] = t; } return thisv; }
    case A_TOREVERSED: { jobj *a = as_array(rt, thisv, 0), *r = js_arr_new(rt, a->alen); for (uint32_t i = a->alen; i > 0; i--) js_arr_push(rt, r, a->el[i - 1]); return jobjv(r); }
    case A_INDEXOF: { jobj *a = as_array(rt, thisv, 0); int64_t s = rel_index(rt, ARG(1), a->alen, 0); for (int64_t i = s; i < (int64_t)a->alen; i++) if (!IS_HOLE(a->el[i]) && js_strict_eq(a->el[i], ARG(0))) return jint(i); return jint(-1); }
    case A_LASTINDEXOF: { jobj *a = as_array(rt, thisv, 0); for (int64_t i = (int64_t)a->alen - 1; i >= 0; i--) if (!IS_HOLE(a->el[i]) && js_strict_eq(a->el[i], ARG(0))) return jint(i); return jint(-1); }
    case A_INCLUDES: { jobj *a = as_array(rt, thisv, 0); for (uint32_t i = 0; i < a->alen; i++) if (js_same_zero(a->el[i], ARG(0))) return jbool(1); return jbool(0); }
    case A_AT: { jobj *a = as_array(rt, thisv, 0); int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); if (i < 0) i += a->alen; return i >= 0 && i < (int64_t)a->alen ? a->el[i] : jundef(); }
    case A_FILL: { jobj *a = as_array(rt, thisv, 1); int64_t s = rel_index(rt, ARG(1), a->alen, 0), e = rel_index(rt, ARG(2), a->alen, a->alen); for (int64_t i = s; i < e; i++) a->el[i] = ARG(0); return thisv; }
    case A_KEYS: return make_iter(rt, jobjv(as_array(rt, thisv, 0)), IT_KEYS);
    case A_VALUES: return make_iter(rt, jobjv(as_array(rt, thisv, 0)), IT_VALUES);
    case A_ENTRIES: return make_iter(rt, jobjv(as_array(rt, thisv, 0)), IT_ENTRIES);
    case A_FLAT: { jobj *a = as_array(rt, thisv, 0), *r = js_arr_new(rt, a->alen); flatten(rt, r, a, IS_UNDEF(ARG(0)) ? 1 : (int)(f64_to_i64(js_tonum(rt, args[0])) > 1000 ? 1000 : f64_to_i64(js_tonum(rt, args[0])))); return jobjv(r); }
    case A_SORT: case A_TOSORTED: {
        jobj *a = as_array(rt, thisv, magic == A_SORT);
        if (!IS_UNDEF(ARG(0)) && !js_is_callable(args[0])) js_throw_err(rt, E_TYPE, "El comparador debe ser una funcion");
        if (magic == A_TOSORTED) { a = js_array_from(rt, a->el, a->alen); }
        if (a->frozen) return thisv;
        struct sortctx c = { rt, ARG(0) }; jv *tmp = (jv *)js_alloc(rt, (a->alen + 1) * (uint32_t)sizeof(jv)); msort(&c, a->el, tmp, a->alen); return jobjv(a);
    }
    case A_WITH: { jobj *a = js_array_from(rt, as_array(rt, thisv, 0)->el, as_array(rt, thisv, 0)->alen); int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); if (i < 0) i += a->alen; if (i < 0 || i >= (int64_t)a->alen) js_throw_err(rt, E_RANGE, "Indice fuera de rango"); a->el[i] = ARG(1); return jobjv(a); }
    case A_REDUCE: case A_REDUCERIGHT: {
        jobj *a = as_array(rt, thisv, 0); if (!js_is_callable(ARG(0))) js_throw_err(rt, E_TYPE, "%s no es una funcion", "El callback de reduce");
        int64_t n = a->alen, i = magic == A_REDUCE ? 0 : n - 1, step = magic == A_REDUCE ? 1 : -1; jv acc;
        if (argc > 1) acc = args[1]; else { while (i >= 0 && i < n && IS_HOLE(a->el[i])) i += step; if (i < 0 || i >= n) js_throw_err(rt, E_TYPE, "Reduce de un array vacio sin valor inicial"); acc = a->el[i]; i += step; }
        for (; i >= 0 && i < (int64_t)a->alen; i += step) { if (IS_HOLE(a->el[i])) continue; jv cb[4] = { acc, a->el[i], jint(i), jobjv(a) }; acc = js_call(rt, args[0], jundef(), cb, 4); }
        return acc;
    }
    case A_FIND: case A_FINDINDEX: case A_FINDLAST: case A_FINDLASTINDEX: case A_FILTER: case A_MAP: case A_FOREACH: case A_SOME: case A_EVERY: case A_FLATMAP: {
        jobj *a = as_array(rt, thisv, 0); jv fn = ARG(0); if (!js_is_callable(fn)) js_throw_err(rt, E_TYPE, "El callback no es una funcion");
        jobj *out = (magic == A_FILTER || magic == A_MAP || magic == A_FLATMAP) ? js_arr_new(rt, a->alen) : 0; int last = (magic == A_FINDLAST || magic == A_FINDLASTINDEX);
        for (int64_t k = 0; k < (int64_t)a->alen; k++) {
            int64_t i = last ? (int64_t)a->alen - 1 - k : k; if (i >= (int64_t)a->alen) break;
            jv v = a->el[i];
            if (IS_HOLE(v)) { if (magic == A_MAP) { js_arr_push_raw(rt, out, v); continue; } if (magic == A_FILTER || magic == A_FOREACH || magic == A_SOME || magic == A_EVERY || magic == A_FLATMAP) continue; v.u.n = 0; }
            jv cb[3] = { v, jint(i), jobjv(a) }; jv r = js_call(rt, fn, ARG(1), cb, 3);
            switch (magic) {
            case A_FIND: case A_FINDLAST: if (js_truthy(r)) return v; break;
            case A_FINDINDEX: case A_FINDLASTINDEX: if (js_truthy(r)) return jint(i); break;
            case A_FILTER: if (js_truthy(r)) js_arr_push(rt, out, v); break;
            case A_MAP: js_arr_push(rt, out, r); break;
            case A_FLATMAP: if (r.t == JT_OBJ && r.u.o->cls == JC_ARRAY) flatten(rt, out, r.u.o, 0); else js_arr_push(rt, out, r); break;
            case A_SOME: if (js_truthy(r)) return jbool(1); break;
            case A_EVERY: if (!js_truthy(r)) return jbool(0); break;
            }
        }
        switch (magic) { case A_FIND: case A_FINDLAST: case A_FOREACH: return jundef(); case A_FINDINDEX: case A_FINDLASTINDEX: return jint(-1); case A_SOME: return jbool(0); case A_EVERY: return jbool(1); default: return jobjv(out); }
    }
    case A_COPYWITHIN: return thisv;
    }
    return jundef();
}
static jv array_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *a = js_arr_new(rt, 4);
    if (rt->new_target) { jv p = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (p.t == JT_OBJ) a->proto = p.u.o; rt->new_target = 0; }
    if (argc == 1 && args[0].t == JT_NUM) {
        f64 n = args[0].u.n; if (!f64_is_int(n) || (n >> 63) || f64_lt(0x41C0000000000000ULL, n)) js_throw_err(rt, E_RANGE, "Longitud de array no valida");
        uint32_t len = (uint32_t)f64_to_i64(n); if (len > (4u << 20)) js_throw_err(rt, E_RANGE, "Longitud de array demasiado grande");
        for (uint32_t i = 0; i < len; i++) js_arr_push_raw(rt, a, jhole());
    } else for (int i = 0; i < argc; i++) js_arr_push(rt, a, args[i]);
    return jobjv(a);
}
struct fromctx { jobj *out; jv fn; struct jrt *rt; };
static int from_cb(struct jrt *rt, jv item, void *ud) { struct fromctx *c = (struct fromctx *)ud; if (c->fn.t != JT_UNDEF) { jv a[2] = { item, jint(c->out->alen) }; item = js_call(rt, c->fn, jundef(), a, 2); } js_arr_push(rt, c->out, item); return 1; }
static jv array_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic == 0) return jbool(ARG(0).t == JT_OBJ && (args[0].u.o->cls == JC_ARRAY || (args[0].u.o->cls == JC_PROXY && js_proxy_is_array(args[0].u.o))));   /* SOMA_188 */
    if (magic == 2) return jobjv(js_array_from(rt, args, (uint32_t)argc));
    jv src = ARG(0); jobj *out = js_arr_new(rt, 8); struct fromctx c = { out, ARG(1), rt };
    if (IS_NULLISH(src)) js_throw_err(rt, E_TYPE, "undefined o null no es iterable");
    if (src.t == JT_STR || (src.t == JT_OBJ && (src.u.o->cls == JC_ARRAY || src.u.o->cls == JC_ARGS || src.u.o->cls == JC_SET || src.u.o->cls == JC_MAP || js_is_callable(js_get(rt, src, js_intern_c(rt, "@@iterator")))))) { js_iterate(rt, src, from_cb, &c); return jobjv(out); }
    if (src.t == JT_OBJ) { int64_t n = f64_to_i64(js_tonum(rt, js_get(rt, src, rt->s_length))); if (n > (4 << 20)) n = 4 << 20; for (int64_t i = 0; i < n; i++) from_cb(rt, js_getv(rt, src, jint(i)), &c); }
    return jobjv(out);
}

/* ------------------------------ String ------------------------------ */

static jstr *this_str(struct jrt *rt, jv t)
{
    if (t.t == JT_STR) return t.u.s;
    if (IS_NULLISH(t)) js_throw_err(rt, E_TYPE, "String.prototype llamado sobre null o undefined");
    if (t.t == JT_OBJ && t.u.o->cls == JC_STRING) return t.u.o->el[0].u.s;
    return js_tostr(rt, t);
}
static int64_t str_index(struct jrt *rt, jv v, int64_t len, int64_t def, int clampneg)
{
    if (IS_UNDEF(v)) return def;
    f64 n = js_tonum(rt, v); if (f64_is_nan(n)) return 0; if (f64_is_inf(n)) return (n >> 63) ? 0 : len;
    int64_t i = f64_to_i64(n); if (i < 0) { if (clampneg) return 0; i += len; if (i < 0) i = 0; } if (i > len) i = len; return i;
}
static uint32_t case_map(uint32_t c, int up)
{
    if (c < 128) { if (up && c >= 'a' && c <= 'z') return c - 32; if (!up && c >= 'A' && c <= 'Z') return c + 32; return c; }
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return up ? c - 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return up ? c : c + 32;
    if (c == 0xFF) return up ? 0x178 : c;
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) { int odd = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E) ? !(c & 1) : (c & 1); return up ? (odd ? c - 1 : c) : (odd ? c : c + 1); }
    if (c >= 0x430 && c <= 0x44F) return up ? c - 32 : c;
    if (c >= 0x410 && c <= 0x42F) return up ? c : c + 32;
    if (c >= 0x450 && c <= 0x45F) return up ? c - 80 : c;
    if (c >= 0x400 && c <= 0x40F) return up ? c : c + 80;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return up ? c - 32 : c;
    if (c >= 0x391 && c <= 0x3A9) return up ? c : c + 32;
    return c;
}
static jstr *map_case(struct jrt *rt, jstr *s, int up)
{
    struct wb b = {0}; for (uint32_t i = 0; i < s->len;) { uint32_t adv; uint32_t cp = wu_utf8_next(s->d + i, &adv); if (!adv) adv = 1; char t[4]; if (cp == 0xDF && up) { wb_add(&b, "SS"); } else { int n = wu_utf8_put(case_map(cp, up), t); wb_addn(&b, t, (uint32_t)n); } i += adv; }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
static const char *find_sub(const char *h, uint32_t hl, const char *n, uint32_t nl, uint32_t from)
{
    if (nl == 0) return from <= hl ? h + from : 0;
    if (nl > hl) return 0;
    for (uint32_t i = from; i + nl <= hl; i++) if (h[i] == n[0] && !memcmp(h + i, n, nl)) return h + i;
    return 0;
}
static jv string_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jstr *s = this_str(rt, thisv); int64_t len = js_u16len(s);      /* SOMA_185: indices y longitudes en unidades UTF-16 */
    switch (magic) {
    case S_TOSTRING: if (s->len > 2 && s->d[0] == '@' && s->d[1] == '@') return jstrv(js_sym_tostring(rt, s)); return jstrv(s);   /* SOMA_187: Symbol(a) */
    case S_CHARAT: { int64_t i = str_index(rt, ARG(0), len, 0, 1); if (i >= len) return jstrv(rt->s_empty); return jstrv(js_u16_char(rt, s, (uint32_t)i)); }
    case S_AT: { int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); if (i < 0) i += len; if (i < 0 || i >= len) return jundef(); return jstrv(js_u16_char(rt, s, (uint32_t)i)); }
    case S_CHARCODEAT: case S_CODEPOINTAT: {
        int64_t i = str_index(rt, ARG(0), len, 0, 1); if (i >= len) return magic == S_CHARCODEAT ? jnum(F64_NAN) : jundef();
        if (!js_u16_on()) { uint32_t adv; uint32_t cp = wu_utf8_next(s->d + i, &adv); if (magic == S_CHARCODEAT && cp > 0xFFFF) cp = 0xD800 + ((cp - 0x10000) >> 10); return jint(cp); }   /* modo bytes: como antes del 185 */
        if (magic == S_CHARCODEAT) return jint(js_u16_unit(rt, s, (uint32_t)i));
        int mid; uint32_t b = js_u16_to_byte(rt, s, (uint32_t)i, &mid); uint32_t adv; uint32_t cp = wu_utf8_next(s->d + b, &adv);
        if (mid) return jint(0xDC00 + ((cp - 0x10000) & 0x3FF));
        return jint(cp);
    }
    case S_INDEXOF: case S_LASTINDEXOF: case S_INCLUDES: case S_STARTSWITH: case S_ENDSWITH: {
        if (ARG(0).t == JT_OBJ && args[0].u.o->cls == JC_REGEXP && (magic == S_INCLUDES || magic == S_STARTSWITH || magic == S_ENDSWITH)) js_throw_err(rt, E_TYPE, "El argumento no puede ser una expresion regular");
        jstr *n = js_tostr(rt, ARG(0));
        if (magic == S_INDEXOF || magic == S_INCLUDES) { int64_t from = str_index(rt, ARG(1), len, 0, 1); int mid; uint32_t fb = js_u16_to_byte(rt, s, (uint32_t)from, &mid); if (mid) fb += 4; const char *p = find_sub(s->d, s->len, n->d, n->len, fb); if (magic == S_INCLUDES) return jbool(p != 0); return jint(p ? (int64_t)js_byte_to_u16(rt, s, (uint32_t)(p - s->d)) : -1); }
        if (magic == S_LASTINDEXOF) { for (int64_t i = (int64_t)s->len - (int64_t)n->len; i >= 0; i--) if (!memcmp(s->d + i, n->d, n->len)) return jint(js_byte_to_u16(rt, s, (uint32_t)i)); return jint(-1); }
        if (magic == S_STARTSWITH) { int64_t from = str_index(rt, ARG(1), len, 0, 1); int mid; uint32_t fb = js_u16_to_byte(rt, s, (uint32_t)from, &mid); return jbool(!mid && (uint64_t)fb + n->len <= s->len && !memcmp(s->d + fb, n->d, n->len)); }
        { int64_t end = str_index(rt, ARG(1), len, len, 1); int mid; uint32_t eb = js_u16_to_byte(rt, s, (uint32_t)end, &mid); return jbool(!mid && eb >= n->len && !memcmp(s->d + eb - n->len, n->d, n->len)); }
    }
    case S_SLICE: case S_SUBSTRING: case S_SUBSTR: {
        int64_t a, b;
        if (magic == S_SLICE) { a = str_index(rt, ARG(0), len, 0, 0); b = str_index(rt, ARG(1), len, len, 0); }
        else if (magic == S_SUBSTRING) { a = str_index(rt, ARG(0), len, 0, 1); b = str_index(rt, ARG(1), len, len, 1); if (a > b) { int64_t t = a; a = b; b = t; } }
        else { a = str_index(rt, ARG(0), len, 0, 0); int64_t l = IS_UNDEF(ARG(1)) ? len - a : f64_to_i64(js_tonum(rt, args[1])); if (l < 0) l = 0; b = a + l; if (b > len) b = len; }
        if (b <= a) return jstrv(rt->s_empty);
        return jstrv(js_u16_slice(rt, s, (uint32_t)a, (uint32_t)b));
    }
    case S_UPPER: return jstrv(map_case(rt, s, 1)); case S_LOWER: return jstrv(map_case(rt, s, 0));
    case S_TRIM: case S_TRIMSTART: case S_TRIMEND: {
        uint32_t a = 0, b = s->len;
        if (magic != S_TRIMEND) while (a < b && (wu_isspace(s->d[a]) || ((uint8_t)s->d[a] == 0xC2 && a + 1 < b && (uint8_t)s->d[a + 1] == 0xA0 && (a++, 1)))) a++;
        if (magic != S_TRIMSTART) while (b > a && wu_isspace(s->d[b - 1])) b--;
        return jstrv(js_str_new(rt, s->d + a, b - a));
    }
    case S_PADSTART: case S_PADEND: {
        int64_t target = f64_to_i64(js_tonum(rt, ARG(0))); jstr *pad = IS_UNDEF(ARG(1)) ? js_intern_c(rt, " ") : js_tostr(rt, args[1]);
        if (target <= len || !pad->len) return jstrv(s);
        if (target > (8 << 20)) js_throw_err(rt, E_RANGE, "cadena demasiado larga");
        struct wb b = {0}; int64_t need = target - len; struct wb fill = {0}; uint32_t pl = js_u16len(pad); for (int64_t i = 0; i < need; i++) js_wb_unit(&fill, (uint32_t)js_u16_unit(rt, pad, (uint32_t)(i % pl)));
        if (magic == S_PADSTART) { wb_addn(&b, fill.p, fill.len); wb_addn(&b, s->d, s->len); } else { wb_addn(&b, s->d, s->len); wb_addn(&b, fill.p, fill.len); }
        jstr *r = js_str_new(rt, b.p, b.len); wb_free(&b); wb_free(&fill); return jstrv(r);
    }
    case S_REPEAT: {
        f64 n = js_tonum(rt, ARG(0)); if (f64_lt(n, F64_ZERO) || f64_is_inf(n)) js_throw_err(rt, E_RANGE, "Conteo de repeticion no valido"); int64_t c = f64_to_i64(n);
        if ((uint64_t)c * s->len > (16u << 20)) js_throw_err(rt, E_RANGE, "cadena demasiado larga");
        struct wb b = {0}; for (int64_t i = 0; i < c; i++) wb_addn(&b, s->d, s->len); jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
    }
    case S_CONCAT: { jstr *r = s; for (int i = 0; i < argc; i++) r = js_str_concat(rt, r, js_tostr(rt, args[i])); return jstrv(r); }
    case S_LOCALECMP: { jstr *o = js_tostr(rt, ARG(0)); uint32_t n = s->len < o->len ? s->len : o->len;
        for (uint32_t i = 0; i < n; i++) { int a = wu_lower((uint8_t)s->d[i]), b = wu_lower((uint8_t)o->d[i]); if (a != b) return jint(a < b ? -1 : 1); }
        return jint(s->len < o->len ? -1 : s->len > o->len ? 1 : 0); }
    case S_NORMALIZE: return jstrv(s);
    case S_ITER: return make_iter(rt, jstrv(s), IT_VALUES);
    case S_SPLIT: {
        jv sep = ARG(0); int64_t lim = IS_UNDEF(ARG(1)) ? 0x7fffffff : f64_to_i64(js_tonum(rt, args[1]));
        if (sep.t == JT_OBJ && sep.u.o->cls == JC_REGEXP) return js_regex_string_op(rt, S_SPLIT, s, args, argc);
        jobj *out = js_arr_new(rt, 4); if (IS_UNDEF(sep)) { js_arr_push(rt, out, jstrv(s)); return jobjv(out); }
        jstr *sp = js_tostr(rt, sep);
        if (!sp->len) {
            if (!js_u16_on()) { for (uint32_t i = 0; i < s->len && (int64_t)out->alen < lim;) { uint32_t adv; wu_utf8_next(s->d + i, &adv); if (!adv || i + adv > s->len) adv = 1; js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + i, adv))); i += adv; } return jobjv(out); }
            for (uint32_t i = 0; i < (uint32_t)len && (int64_t)out->alen < lim; i++) js_arr_push(rt, out, jstrv(js_u16_char(rt, s, i)));
            return jobjv(out);
        }
        uint32_t pos = 0; for (;;) { const char *p = find_sub(s->d, s->len, sp->d, sp->len, pos); if (!p || (int64_t)out->alen >= lim) { if ((int64_t)out->alen < lim) js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + pos, s->len - pos))); break; } js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + pos, (uint32_t)(p - s->d) - pos))); pos = (uint32_t)(p - s->d) + sp->len; }
        return jobjv(out);
    }
    case S_REPLACE: case S_REPLACEALL: case S_MATCH: case S_MATCHALL: case S_SEARCH: {
        jv pat = ARG(0);
        if (pat.t == JT_OBJ && pat.u.o->cls == JC_REGEXP) return js_regex_string_op(rt, magic, s, args, argc);
        if (magic == S_MATCH || magic == S_SEARCH || magic == S_MATCHALL) {
            jobj *re = js_regex_new(rt, IS_UNDEF(pat) ? rt->s_empty : js_tostr(rt, pat), magic == S_MATCHALL ? js_intern_c(rt, "g") : rt->s_empty); jv a2[2] = { jobjv(re), ARG(1) };
            return js_regex_string_op(rt, magic, s, a2, 1);
        }
        jstr *needle = js_tostr(rt, pat); jv rep = ARG(1); struct wb b = {0}; uint32_t pos = 0; int done = 0;
        while (!done) {
            const char *p = find_sub(s->d, s->len, needle->d, needle->len, pos); if (!p) break;
            uint32_t at = (uint32_t)(p - s->d); wb_addn(&b, s->d + pos, at - pos);
            if (js_is_callable(rep)) { jv cb[3] = { jstrv(needle), jint(js_byte_to_u16(rt, s, at)), jstrv(s) }; jstr *r = js_tostr(rt, js_call(rt, rep, jundef(), cb, 3)); wb_addn(&b, r->d, r->len); }
            else { jstr *r = js_tostr(rt, rep);
                for (uint32_t k = 0; k < r->len; k++) { if (r->d[k] == '$' && k + 1 < r->len) { char c = r->d[k + 1]; if (c == '$') { wb_addc(&b, '$'); k++; continue; } if (c == '&') { wb_addn(&b, needle->d, needle->len); k++; continue; } if (c == '`') { wb_addn(&b, s->d, at); k++; continue; } if (c == '\'') { wb_addn(&b, s->d + at + needle->len, s->len - at - needle->len); k++; continue; } } wb_addc(&b, r->d[k]); } }
            pos = at + needle->len; if (!needle->len) { if (pos < s->len) wb_addc(&b, s->d[pos]); pos++; if (pos > s->len) { done = 1; break; } }
            if (magic == S_REPLACE) break;
        }
        if (pos <= s->len) wb_addn(&b, s->d + pos, s->len - pos);
        jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
    }
    }
    return jundef();
}
static jv string_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv v = argc ? (args[0].t == JT_STR && args[0].u.s->len > 2 && args[0].u.s->d[0] == '@' && args[0].u.s->d[1] == '@' ? jstrv(js_tostr(rt, js_call(rt, js_get(rt, jobjv(rt->p_symbol), js_intern_c(rt, "toString")), args[0], 0, 0))) : jstrv(js_tostr(rt, args[0]))) : jstrv(rt->s_empty);
    if (rt->new_target) { rt->new_target = 0; return jobjv(js_toobj(rt, v)); } return v;
}
static jv string_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic == 2) { jv t = ARG(0); jv raw = js_get_c(rt, t, "raw"); struct wb b = {0}; jobj *r = raw.t == JT_OBJ ? raw.u.o : 0; if (r) for (uint32_t i = 0; i < r->alen; i++) { jstr *x = js_tostr(rt, r->el[i]); wb_addn(&b, x->d, x->len); if (i + 1 < r->alen && (int)(i + 1) < argc) { jstr *y = js_tostr(rt, args[i + 1]); wb_addn(&b, y->d, y->len); } } jstr *res = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(res); }
    struct wb b = {0};
    for (int i = 0; i < argc; i++) {
        f64 nv = js_tonum(rt, args[i]);
        if (magic == 1) { int64_t iv = f64_to_i64(nv); if (f64_is_nan(nv) || !f64_is_int(nv) || iv < 0 || iv > 0x10FFFF) js_throw_err(rt, E_RANGE, "Punto de codigo no valido"); }
        uint32_t cp = f64_to_u32(nv);
        if (magic == 0) js_wb_unit(&b, cp & 0xFFFF);                    /* SOMA_185: dos unidades seguidas forman un par */
        else if (cp >= 0x10000) { char t[4]; int n = wu_utf8_put(cp, t); wb_addn(&b, t, (uint32_t)n); }
        else js_wb_unit(&b, cp);
    }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
}
void js_lib_arrays_init(struct jrt *rt)
{
    jobj *ac = mkctor2(rt, "Array", array_ctor, 1, 0, rt->p_array);
    js_method(rt, ac, "isArray", array_static, 1, 0); js_method(rt, ac, "from", array_static, 1, 1); js_method(rt, ac, "of", array_static, 0, 2);
    static const struct { const char *n; int m; int a; } AM[] = { { "push", A_PUSH, 1 }, { "pop", A_POP, 0 }, { "shift", A_SHIFT, 0 }, { "unshift", A_UNSHIFT, 1 }, { "slice", A_SLICE, 2 }, { "splice", A_SPLICE, 2 }, { "concat", A_CONCAT, 1 }, { "join", A_JOIN, 1 },
        { "reverse", A_REVERSE, 0 }, { "indexOf", A_INDEXOF, 1 }, { "lastIndexOf", A_LASTINDEXOF, 1 }, { "includes", A_INCLUDES, 1 }, { "find", A_FIND, 1 }, { "findIndex", A_FINDINDEX, 1 }, { "findLast", A_FINDLAST, 1 }, { "findLastIndex", A_FINDLASTINDEX, 1 },
        { "filter", A_FILTER, 1 }, { "map", A_MAP, 1 }, { "forEach", A_FOREACH, 1 }, { "reduce", A_REDUCE, 1 }, { "reduceRight", A_REDUCERIGHT, 1 }, { "some", A_SOME, 1 }, { "every", A_EVERY, 1 }, { "sort", A_SORT, 1 }, { "flat", A_FLAT, 0 },
        { "flatMap", A_FLATMAP, 1 }, { "fill", A_FILL, 1 }, { "keys", A_KEYS, 0 }, { "values", A_VALUES, 0 }, { "entries", A_ENTRIES, 0 }, { "at", A_AT, 1 }, { "toString", A_TOSTRING, 0 }, { "toSorted", A_TOSORTED, 1 }, { "toReversed", A_TOREVERSED, 0 }, { "with", A_WITH, 2 }, { "copyWithin", A_COPYWITHIN, 2 }, { 0, 0, 0 } };
    for (int i = 0; AM[i].n; i++) js_method(rt, rt->p_array, AM[i].n, array_proto, AM[i].a, AM[i].m);
    js_def(rt, rt->p_array, js_intern_c(rt, "@@iterator"), js_get_c(rt, jobjv(rt->p_array), "values"), 0);
    js_method(rt, rt->p_iter, "next", iter_next, 0, 0); js_def(rt, rt->p_iter, js_intern_c(rt, "@@iterator"), jobjv(js_native(rt, "[Symbol.iterator]", iter_self, 0, 0)), 0);
    jobj *sc = mkctor2(rt, "String", string_ctor, 1, 0, rt->p_string);
    js_method(rt, sc, "fromCharCode", string_static, 1, 0); js_method(rt, sc, "fromCodePoint", string_static, 1, 1); js_method(rt, sc, "raw", string_static, 1, 2);
    static const struct { const char *n; int m; int a; } SM[] = { { "charAt", S_CHARAT, 1 }, { "charCodeAt", S_CHARCODEAT, 1 }, { "codePointAt", S_CODEPOINTAT, 1 }, { "at", S_AT, 1 }, { "indexOf", S_INDEXOF, 1 }, { "lastIndexOf", S_LASTINDEXOF, 1 },
        { "includes", S_INCLUDES, 1 }, { "startsWith", S_STARTSWITH, 1 }, { "endsWith", S_ENDSWITH, 1 }, { "slice", S_SLICE, 2 }, { "substring", S_SUBSTRING, 2 }, { "substr", S_SUBSTR, 2 }, { "toUpperCase", S_UPPER, 0 }, { "toLowerCase", S_LOWER, 0 },
        { "toLocaleUpperCase", S_UPPER, 0 }, { "toLocaleLowerCase", S_LOWER, 0 }, { "trim", S_TRIM, 0 }, { "trimStart", S_TRIMSTART, 0 }, { "trimEnd", S_TRIMEND, 0 }, { "trimLeft", S_TRIMSTART, 0 }, { "trimRight", S_TRIMEND, 0 }, { "split", S_SPLIT, 2 },
        { "replace", S_REPLACE, 2 }, { "replaceAll", S_REPLACEALL, 2 }, { "match", S_MATCH, 1 }, { "matchAll", S_MATCHALL, 1 }, { "search", S_SEARCH, 1 }, { "padStart", S_PADSTART, 2 }, { "padEnd", S_PADEND, 2 }, { "repeat", S_REPEAT, 1 }, { "concat", S_CONCAT, 1 },
        { "localeCompare", S_LOCALECMP, 1 }, { "normalize", S_NORMALIZE, 0 }, { "toString", S_TOSTRING, 0 }, { "valueOf", S_TOSTRING, 0 }, { 0, 0, 0 } };
    for (int i = 0; SM[i].n; i++) js_method(rt, rt->p_string, SM[i].n, string_proto, SM[i].a, SM[i].m);
    js_def(rt, rt->p_string, js_intern_c(rt, "@@iterator"), jobjv(js_native(rt, "[Symbol.iterator]", string_proto, 0, S_ITER)), 0);
}
