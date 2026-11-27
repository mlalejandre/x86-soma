/* SOMA_PARCHE_NAVEGADOR: biblioteca estandar de JavaScript (Object, Function, Error, Symbol, globales). */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())

/* ------------------------------ ayudas ------------------------------ */
static jv nt_object(struct jrt *rt, jobj *def)
{
    jobj *proto = def;
    if (rt->new_target) { jv p = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (p.t == JT_OBJ) proto = p.u.o; rt->new_target = 0; }
    return jobjv(js_obj_new(rt, proto));
}
static jobj *mkctor(struct jrt *rt, const char *name, jnative f, int nargs, int magic, jobj *proto)
{
    jobj *c = js_native(rt, name, f, nargs, magic);
    if (proto) { js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0); }
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0);
    return c;
}
jstr *js_json_stringify(struct jrt *rt, jv v, jv replacer, jv indent);
static jobj *own_keys(struct jrt *rt, jv v, int all)
{
    js_fn_proto_ensure(rt, v);   /* SOMA_345 */
    jobj *out = js_arr_new(rt, 8);
    if (v.t == JT_STR) { uint32_t n16 = js_u16len(v.u.s); for (uint32_t i = 0; i < n16; i++) { char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_str_new(rt, t, (uint32_t)l))); } return out; }
    if (v.t != JT_OBJ) return out;
    jobj *o = v.u.o;
    if (o->cls == JC_PROXY) return js_proxy_own_keys(rt, o, all);   /* SOMA_188 */
    if (o->cls == JC_HOST && (o->flag & 1) && rt->dyn && rt->dyn->keys) { jobj *dk = rt->dyn->keys(rt, o); for (uint32_t i = 0; i < dk->alen; i++) js_arr_push(rt, out, dk->el[i]); }
    if (o->cls == JC_ARRAY || o->cls == JC_ARGS) for (uint32_t i = 0; i < o->alen; i++) { if (IS_HOLE(o->el[i])) continue; char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_intern_n(rt, t, (uint32_t)l))); }
    if (o->cls == JC_STRING) for (uint32_t i = 0; i < js_u16len(o->el[0].u.s); i++) { char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_str_new(rt, t, (uint32_t)l))); }
    if (o->cls == JC_TYPED) for (uint32_t i = 0; i < js_ta_length(o); i++) { char t[16]; int l = wu_itoa(i, t); js_arr_push(rt, out, jstrv(js_intern_n(rt, t, (uint32_t)l))); }   /* SOMA_186 */
    for (uint32_t i = 0; i < o->np; i++) { struct jprop *p = &o->props[i]; if ((!all && !(p->flags & JP_ENUM)) || (p->key->d[0] == '@' && p->key->d[1] == '@')) continue; js_arr_push(rt, out, jstrv(p->key)); }
    return out;
}
jobj *js_own_keys(struct jrt *rt, jv v, int all) { return own_keys(rt, v, all); }
/* SOMA_345_NAVEGADOR_REAL: los simbolos son cadenas "@@..." (ver sym_ctor). Los del programa ("@@desc#n", "@@for:clave") y los bien
 * conocidos son claves de verdad; "@@st", "@@cl", "@@ds", "@@rs", "@@fonts", "@@proxy"... son ranuras internas del motor y nunca se
 * enumeran. Antes Reflect.ownKeys y getOwnPropertySymbols no devolvian ningun simbolo (getOwnPropertySymbols daba las claves de texto). */
int js_user_sym(const jstr *k)
{
    if (k->len < 3 || k->d[0] != '@' || k->d[1] != '@') return 0;
    for (uint32_t i = 2; i < k->len; i++) if (k->d[i] == '#') return 1;
    if (k->len > 6 && !memcmp(k->d, "@@for:", 6)) return 1;
    static const char *const WK[] = { "iterator", "asyncIterator", "toStringTag", "toPrimitive", "hasInstance", "species", "isConcatSpreadable",
                                      "match", "matchAll", "replace", "search", "split", "unscopables", 0 };
    for (int i = 0; WK[i]; i++) { uint32_t n = (uint32_t)wu_strlen(WK[i]); if (k->len == n + 2 && !memcmp(k->d + 2, WK[i], n)) return 1; }
    return 0;
}
static void own_syms_into(jobj *o, jobj *out, struct jrt *rt, int enum_only)
{
    if (o->cls == JC_PROXY) return;
    for (uint32_t i = 0; i < o->np; i++) { struct jprop *p = &o->props[i]; if ((enum_only && !(p->flags & JP_ENUM)) || !js_user_sym(p->key)) continue; js_arr_push(rt, out, jstrv(p->key)); }
}

/* ------------------------------ Object ------------------------------ */
enum { O_KEYS, O_VALUES, O_ENTRIES, O_ASSIGN, O_FREEZE, O_ISFROZEN, O_CREATE, O_GETPROTO, O_SETPROTO, O_DEFPROP, O_DEFPROPS, O_GETOWNNAMES, O_GETOWNDESC, O_FROMENTRIES, O_HASOWN, O_SEAL, O_ISEXT, O_PREVENT, O_IS, O_GETOWNDESCS, O_GETOWNSYMS };
static void from_desc(struct jrt *rt, jobj *o, jstr *key, jv d)
{
    if (d.t != JT_OBJ) js_throw_err(rt, E_TYPE, "El descriptor debe ser un objeto");
    int fl = 0; jv e = js_get_c(rt, d, "enumerable"), w = js_get_c(rt, d, "writable"), c = js_get_c(rt, d, "configurable");
    if (js_truthy(e)) fl |= JP_ENUM;
    if (!js_truthy(w)) fl |= JP_RO;
    if (!js_truthy(c)) fl |= JP_NOCONF;
    jv g = js_get_c(rt, d, "get"), s = js_get_c(rt, d, "set");
    key = js_intern(rt, key);
    if (js_is_callable(g) || js_is_callable(s)) { fl &= ~JP_RO; js_def_acc(rt, o, key, g, s, fl); return; }
    struct jprop *ex = js_find_own(o, key);
    if (o->cls == JC_ARRAY) { int64_t ix; if (js_to_index(rt, jstrv(key), &ix)) { js_putv(rt, jobjv(o), jstrv(key), js_get_c(rt, d, "value")); return; } }
    if (ex && !(ex->flags & JP_ACC)) { /* conserva atributos no indicados */
        if (e.t == JT_UNDEF) fl = (fl & ~JP_ENUM) | (ex->flags & JP_ENUM);
        if (w.t == JT_UNDEF) fl = (fl & ~JP_RO) | (ex->flags & JP_RO);
        if (c.t == JT_UNDEF) fl = (fl & ~JP_NOCONF) | (ex->flags & JP_NOCONF);
        if (!js_has(rt, d.u.o, js_intern_c(rt, "value"))) { ex->flags = (uint8_t)fl; return; }
    }
    js_def(rt, o, key, js_get_c(rt, d, "value"), fl);
}
static jv obj_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (rt->new_target && rt->new_target->cls == JC_FUNC) return nt_object(rt, rt->p_object);
    rt->new_target = 0;
    jv v = ARG(0); if (IS_NULLISH(v)) return jobjv(js_obj_new(rt, rt->p_object)); return jobjv(js_toobj(rt, v));
}
static int fromentries_cb(struct jrt *r, jv item, void *ud) { js_put(r, jobjv((jobj *)ud), js_intern(r, js_tostr(r, js_getv(r, item, jint(0)))), js_getv(r, item, jint(1))); return 1; }
static jv obj_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv a = ARG(0);
    if (magic == O_DEFPROP || magic == O_DEFPROPS || magic == O_GETOWNDESC) js_fn_proto_ensure(rt, a);   /* SOMA_345: el prototype aun sin crear */
    switch (magic) {
    case O_KEYS: case O_VALUES: case O_ENTRIES: {
        if (IS_NULLISH(a)) js_throw_err(rt, E_TYPE, "No se puede convertir undefined o null a objeto");
        jobj *ks = own_keys(rt, a, 0);
        if (magic == O_KEYS) return jobjv(ks);
        jobj *out = js_arr_new(rt, ks->alen);
        for (uint32_t i = 0; i < ks->alen; i++) { jv v = js_get(rt, a, ks->el[i].u.s); if (magic == O_VALUES) js_arr_push(rt, out, v); else { jv pr[2] = { ks->el[i], v }; js_arr_push(rt, out, jobjv(js_array_from(rt, pr, 2))); } }
        return jobjv(out);
    }
    case O_ASSIGN: {
        if (IS_NULLISH(a)) js_throw_err(rt, E_TYPE, "No se puede convertir undefined o null a objeto");
        for (int i = 1; i < argc; i++) { if (IS_NULLISH(args[i])) continue; jobj *ks = own_keys(rt, args[i], 0); if (args[i].t == JT_OBJ) own_syms_into(args[i].u.o, ks, rt, 1); for (uint32_t k = 0; k < ks->alen; k++) js_put(rt, a, ks->el[k].u.s, js_get(rt, args[i], ks->el[k].u.s)); }   /* SOMA_345: y los simbolos enumerables */
        return a;
    }
    case O_FREEZE: if (a.t == JT_OBJ) { a.u.o->frozen = 1; a.u.o->extensible = 0; } return a;
    case O_SEAL: case O_PREVENT: if (a.t == JT_OBJ) a.u.o->extensible = 0; return a;
    case O_ISFROZEN: return jbool(a.t != JT_OBJ || a.u.o->frozen);
    case O_ISEXT: return jbool(a.t == JT_OBJ && a.u.o->extensible);
    case O_CREATE: {
        jobj *o = js_obj_new(rt, a.t == JT_OBJ ? a.u.o : 0);
        if (argc > 1 && args[1].t == JT_OBJ) { jobj *ks = own_keys(rt, args[1], 0); for (uint32_t i = 0; i < ks->alen; i++) from_desc(rt, o, ks->el[i].u.s, js_get(rt, args[1], ks->el[i].u.s)); }
        return jobjv(o);
    }
    case O_GETPROTO: { if (IS_NULLISH(a)) js_throw_err(rt, E_TYPE, "No se puede convertir a objeto"); jobj *o = a.t == JT_OBJ ? a.u.o : js_toobj(rt, a); if (o->cls == JC_PROXY) { jobj *pp = js_proxy_proto(rt, o); return pp ? jobjv(pp) : jnull(); } return o->proto ? jobjv(o->proto) : jnull(); }
    case O_SETPROTO: if (a.t == JT_OBJ) a.u.o->proto = ARG(1).t == JT_OBJ ? args[1].u.o : 0; return a;
    case O_DEFPROP: if (a.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Object.defineProperty llamado en un valor que no es objeto");
        if (a.u.o->cls == JC_PROXY) {   /* SOMA_188 */
            jobj *ph = a.u.o->el[1].t == JT_OBJ ? a.u.o->el[1].u.o : 0; if (!ph) js_throw_err(rt, E_TYPE, "Proxy revocado");
            jv tr = js_get(rt, jobjv(ph), js_intern_c(rt, "defineProperty"));
            if (js_is_callable(tr)) { jv ta[3] = { a.u.o->el[0], ARG(1), ARG(2) }; js_call(rt, tr, jobjv(ph), ta, 3); return a; }
            jv ta[3] = { a.u.o->el[0], ARG(1), ARG(2) }; obj_static(rt, thisv, ta, 3, O_DEFPROP); return a;
        }
        from_desc(rt, a.u.o, js_tostr(rt, ARG(1)), ARG(2)); return a;
    case O_DEFPROPS: { if (a.t != JT_OBJ) js_throw_err(rt, E_TYPE, "no es un objeto"); jobj *ks = own_keys(rt, ARG(1), 0); if (ARG(1).t == JT_OBJ) own_syms_into(args[1].u.o, ks, rt, 1); for (uint32_t i = 0; i < ks->alen; i++) from_desc(rt, a.u.o, ks->el[i].u.s, js_get(rt, args[1], ks->el[i].u.s)); return a; }
    case O_GETOWNSYMS: { jobj *ks = js_arr_new(rt, 4); if (a.t == JT_OBJ) own_syms_into(a.u.o, ks, rt, 0); return jobjv(ks); }   /* SOMA_345 */
    case O_GETOWNNAMES: { jobj *ks = own_keys(rt, a, 1); if (a.t == JT_OBJ && (a.u.o->cls == JC_ARRAY)) js_arr_push(rt, ks, jstrv(rt->s_length)); return jobjv(ks); }
    case O_GETOWNDESC: case O_GETOWNDESCS: {
        if (a.t != JT_OBJ) return jundef();
        if (a.u.o->cls == JC_PROXY && magic == O_GETOWNDESC) {   /* SOMA_188 */
            jobj *ph = a.u.o->el[1].t == JT_OBJ ? a.u.o->el[1].u.o : 0; if (!ph) js_throw_err(rt, E_TYPE, "Proxy revocado");
            jv tr = js_get(rt, jobjv(ph), js_intern_c(rt, "getOwnPropertyDescriptor"));
            if (js_is_callable(tr)) { jv ta[2] = { a.u.o->el[0], ARG(1) }; return js_call(rt, tr, jobjv(ph), ta, 2); }
            jv ta[2] = { a.u.o->el[0], ARG(1) }; return obj_static(rt, thisv, ta, 2, O_GETOWNDESC);
        }
        jstr *k = js_intern(rt, js_tostr(rt, ARG(1)));
        struct jprop *p = js_find_own(a.u.o, k);
        if (!p) {
            if (a.u.o->cls == JC_ARRAY) { jv v = js_get(rt, a, k); if (!IS_UNDEF(v) || k == rt->s_length) { jobj *d = js_obj_new(rt, rt->p_object); js_set_c(rt, d, "value", v); js_set_c(rt, d, "writable", jbool(1)); js_set_c(rt, d, "enumerable", jbool(k != rt->s_length)); js_set_c(rt, d, "configurable", jbool(k != rt->s_length)); return jobjv(d); } }
            return jundef();
        }
        jobj *d = js_obj_new(rt, rt->p_object);
        if (p->flags & JP_ACC) { js_set_c(rt, d, "get", p->val); js_set_c(rt, d, "set", p->set); }
        else { js_set_c(rt, d, "value", p->val); js_set_c(rt, d, "writable", jbool(!(p->flags & JP_RO) && !a.u.o->frozen)); }
        js_set_c(rt, d, "enumerable", jbool(p->flags & JP_ENUM)); js_set_c(rt, d, "configurable", jbool(!(p->flags & JP_NOCONF) && !a.u.o->frozen));
        return jobjv(d);
    }
    case O_FROMENTRIES: {
        jobj *o = js_obj_new(rt, rt->p_object);
        js_iterate(rt, a, fromentries_cb, o); return jobjv(o);
    }
    case O_HASOWN: { if (IS_NULLISH(a)) js_throw_err(rt, E_TYPE, "No se puede convertir a objeto"); jobj *ks = own_keys(rt, a, 1); jstr *k = js_tostr(rt, ARG(1)); for (uint32_t i = 0; i < ks->alen; i++) if (js_str_eq(ks->el[i].u.s, k)) return jbool(1); if (a.t == JT_OBJ && a.u.o->cls == JC_ARRAY && k == rt->s_length) return jbool(1); return jbool(0); }
    case O_IS: { jv b = ARG(1); if (a.t == JT_NUM && b.t == JT_NUM) { if (f64_is_nan(a.u.n) && f64_is_nan(b.u.n)) return jbool(1); return jbool(a.u.n == b.u.n || (f64_eq(a.u.n, b.u.n) && !(f64_abs(a.u.n) == 0))); } return jbool(js_strict_eq(a, b)); }
    }
    return jundef();
}
enum { OP_HASOWN, OP_TOSTRING, OP_VALUEOF, OP_ISPROTO, OP_PROPENUM, OP_TOLOCALE, OP_PROTOGET, OP_PROTOSET };
static jv obj_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    switch (magic) {
    case OP_HASOWN: { jobj *ks = own_keys(rt, thisv, 1); jstr *k = js_tostr(rt, ARG(0)); for (uint32_t i = 0; i < ks->alen; i++) if (js_str_eq(ks->el[i].u.s, k)) return jbool(1); return jbool(thisv.t == JT_OBJ && thisv.u.o->cls == JC_ARRAY && k == rt->s_length); }
    case OP_TOSTRING: {
        if (thisv.t == JT_UNDEF) return jstrv(js_intern_c(rt, "[object Undefined]"));
        if (thisv.t == JT_NULL) return jstrv(js_intern_c(rt, "[object Null]"));
        const char *t = "Object";
        if (thisv.t == JT_OBJ) { switch (thisv.u.o->cls) { case JC_ARRAY: t = "Array"; break; case JC_FUNC: case JC_NATIVE: case JC_BOUND: t = "Function"; break; case JC_ERROR: t = "Error"; break; case JC_DATE: t = "Date"; break; case JC_REGEXP: t = "RegExp"; break; case JC_ARGS: t = "Arguments"; break; default: break; } jv tag = js_get(rt, thisv, js_intern_c(rt, "@@toStringTag")); if (tag.t == JT_STR) { char b[80]; wu_copy(b, "[object ", sizeof b); uint32_t n = wu_strlen(b); n += wu_copy(b + n, tag.u.s->d, sizeof b - n); b[n++] = ']'; b[n] = 0; return jstrv(js_str_cstr(rt, b)); } }
        else if (thisv.t == JT_STR) t = "String"; else if (thisv.t == JT_NUM) t = "Number"; else t = "Boolean";
        char b[40]; wu_copy(b, "[object ", sizeof b); uint32_t n = wu_strlen(b); n += wu_copy(b + n, t, sizeof b - n); b[n++] = ']'; b[n] = 0; return jstrv(js_str_cstr(rt, b));
    }
    case OP_VALUEOF: if (thisv.t == JT_OBJ && thisv.u.o->el && (thisv.u.o->cls == JC_STRING || thisv.u.o->cls == JC_NUMBER || thisv.u.o->cls == JC_BOOL)) return thisv.u.o->el[0]; return thisv;
    case OP_ISPROTO: { if (thisv.t != JT_OBJ || ARG(0).t != JT_OBJ) return jbool(0); for (jobj *o = args[0].u.o->proto; o; o = o->proto) if (o == thisv.u.o) return jbool(1); return jbool(0); }
    case OP_PROPENUM: { if (thisv.t != JT_OBJ) return jbool(0); jstr *k = js_intern(rt, js_tostr(rt, ARG(0))); struct jprop *p = js_find_own(thisv.u.o, k); if (p) return jbool(p->flags & JP_ENUM); return jbool(js_has(rt, thisv.u.o, k) && thisv.u.o->cls == JC_ARRAY); }
    case OP_TOLOCALE: return jstrv(js_tostr(rt, thisv));
    case OP_PROTOGET: return thisv.t == JT_OBJ && thisv.u.o->proto ? jobjv(thisv.u.o->proto) : jnull();
    case OP_PROTOSET: if (thisv.t == JT_OBJ) { if (ARG(0).t == JT_OBJ) thisv.u.o->proto = args[0].u.o; else if (ARG(0).t == JT_NULL) thisv.u.o->proto = 0; } return jundef();
    }
    return jundef();
}

/* ------------------------------ Function ------------------------------ */
enum { F_CALL, F_APPLY, F_BIND, F_TOSTRING, F_HASINST };
static jv func_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (!js_is_callable(thisv)) js_throw_err(rt, E_TYPE, "Function.prototype.%s llamado sobre algo que no es una funcion", magic == F_CALL ? "call" : magic == F_APPLY ? "apply" : "bind");
    switch (magic) {
    case F_CALL: return js_call(rt, thisv, ARG(0), argc > 1 ? args + 1 : args, argc > 1 ? argc - 1 : 0);
    case F_APPLY: {
        jv arr = ARG(1); if (IS_NULLISH(arr)) return js_call(rt, thisv, ARG(0), args, 0);
        if (arr.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Los argumentos de apply deben ser un array");
        jobj *a = arr.u.o; if (a->cls != JC_ARRAY && a->cls != JC_ARGS) { jv l = js_get(rt, arr, rt->s_length); int n = (int)f64_to_i64(js_tonum(rt, l)); jv *av = (jv *)js_alloc(rt, (uint32_t)(n + 1) * (uint32_t)sizeof(jv)); for (int i = 0; i < n; i++) av[i] = js_getv(rt, arr, jint(i)); return js_call(rt, thisv, ARG(0), av, n); }
        return js_call(rt, thisv, ARG(0), a->el, (int)a->alen);
    }
    case F_BIND: {
        jobj *b = js_obj_new(rt, rt->p_function); b->cls = JC_BOUND; uint32_t nb = argc > 1 ? (uint32_t)argc - 1 : 0;
        b->el = (jv *)js_alloc(rt, (2 + nb) * (uint32_t)sizeof(jv)); b->alen = b->acap = 2 + nb; b->el[0] = thisv; b->el[1] = ARG(0); for (uint32_t i = 0; i < nb; i++) b->el[2 + i] = args[1 + i];
        return jobjv(b);
    }
    case F_TOSTRING: {
        jobj *f = thisv.u.o;
        if (f->cls == JC_FUNC && f->u.fn.cls && f->u.fn.cls->p) { struct jfi *ci = (struct jfi *)f->u.fn.cls->p; if (ci->src) return jstrv(js_str_new(rt, ci->src, ci->srclen)); }   /* SOMA 188B */
        if (f->cls == JC_FUNC && f->u.fn.node && f->u.fn.node->p) { struct jfi *fi = (struct jfi *)f->u.fn.node->p; if (fi->src) return jstrv(js_str_new(rt, fi->src, fi->srclen)); }
        if (f->cls == JC_FUNC && f->u.fn.cls) return jstrv(js_str_cstr(rt, "class { }"));
        jv nm = js_get(rt, thisv, rt->s_name); char b[120]; wu_copy(b, "function ", sizeof b); uint32_t n = wu_strlen(b); if (nm.t == JT_STR) n += wu_copy(b + n, nm.u.s->d, sizeof b - n); wu_copy(b + n, "() { [native code] }", sizeof b - n); return jstrv(js_str_cstr(rt, b));
    }
    case F_HASINST: return jbool(js_instanceof(rt, ARG(0), thisv));
    }
    return jundef();
}
static jv func_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct wb b = {0}; wb_add(&b, "(function anonymous("); for (int i = 0; i + 1 < argc; i++) { if (i) wb_addc(&b, ','); jstr *s = js_tostr(rt, args[i]); wb_addn(&b, s->d, s->len); }
    wb_add(&b, "\n) {\n"); if (argc) { jstr *s = js_tostr(rt, args[argc - 1]); wb_addn(&b, s->d, s->len); } wb_add(&b, "\n})");
    int ok; jv r = js_eval_string(rt, b.p, b.len, &ok); wb_free(&b);
    if (!ok) js_throw(rt, r);
    return r;
}

/* ------------------------------ Error ------------------------------ */
static const char *ERRN[] = { "Error", "TypeError", "ReferenceError", "RangeError", "SyntaxError", "EvalError", "URIError" };
static jv err_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *def = rt->p_errs[magic]; jobj *proto = def;
    if (rt->new_target) { jv p = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (p.t == JT_OBJ) proto = p.u.o; rt->new_target = 0; }
    jobj *e = js_obj_new(rt, proto); e->cls = JC_ERROR;
    if (argc > 0 && !IS_UNDEF(args[0])) js_def(rt, e, rt->s_message, jstrv(js_tostr(rt, args[0])), 0);
    if (argc > 1 && args[1].t == JT_OBJ) { jv c = js_get_c(rt, args[1], "cause"); if (!IS_UNDEF(c)) js_def(rt, e, js_intern_c(rt, "cause"), c, 0); }
    char st[200]; jv nm = js_get(rt, jobjv(e), rt->s_name); wu_copy(st, nm.t == JT_STR ? nm.u.s->d : "Error", sizeof st);
    jv mm = js_get(rt, jobjv(e), rt->s_message); if (mm.t == JT_STR && mm.u.s->len) { uint32_t n = wu_strlen(st); st[n++] = ':'; st[n++] = ' '; wu_copy(st + n, mm.u.s->d, sizeof st - n); }
    { struct wb b = {0}; js_stack_trace(rt, st, &b); js_def(rt, e, rt->s_stack, b.p ? jstrv(js_str_new(rt, b.p, b.len)) : jstrv(js_str_cstr(rt, st)), 0); wb_free(&b); }   /* SOMA_188 */
    return jobjv(e);
}
static jv err_tostring(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Error.prototype.toString llamado sobre un valor no objeto");
    jv nm = js_get(rt, thisv, rt->s_name), ms = js_get(rt, thisv, rt->s_message);
    jstr *n = IS_UNDEF(nm) ? js_intern_c(rt, "Error") : js_tostr(rt, nm), *m = IS_UNDEF(ms) ? rt->s_empty : js_tostr(rt, ms);
    if (!m->len) return jstrv(n);
    if (!n->len) return jstrv(m);
    return jstrv(js_str_concat(rt, js_str_concat(rt, n, js_intern_c(rt, ": ")), m));
}

/* ------------------------------ Symbol / Boolean / globales ------------------------------ */
static uint32_t symctr;
static jv sym_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (rt->new_target) { rt->new_target = 0; js_throw_err(rt, E_TYPE, "Symbol no es un constructor"); }
    char b[96]; jstr *d = argc && !IS_UNDEF(args[0]) ? js_tostr(rt, args[0]) : rt->s_empty; char t[16]; wu_itoa(++symctr, t);
    wu_copy(b, "@@", sizeof b); uint32_t n = 2; n += wu_copy(b + n, d->d, sizeof b - n - 12); b[n++] = '#'; wu_copy(b + n, t, sizeof b - n);
    return jstrv(js_intern_c(rt, b));
}
static jv sym_for(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    char b[120]; jstr *d = js_tostr(rt, ARG(0)); wu_copy(b, "@@for:", sizeof b); wu_copy(b + 6, d->d, sizeof b - 6); return jstrv(js_intern_c(rt, b));
}
/* SOMA_187: los simbolos son cadenas "@@descripcion#n" (propios), "@@for:clave" (Symbol.for) o "@@nombre" (bien conocidos). description y toString los
 * presentan como lo hace un navegador: Symbol(a), Symbol(), Symbol.iterator. */
jv js_sym_description(struct jrt *rt, jstr *s)
{
    const char *d = s->d + 2; uint32_t n = s->len - 2, hash = n;
    for (uint32_t i = 0; i < n; i++) if (d[i] == '#') hash = i;
    if (hash < n) { int dig = hash + 1 < n; for (uint32_t i = hash + 1; i < n; i++) if (d[i] < '0' || d[i] > '9') dig = 0; if (dig) return hash ? jstrv(js_str_new(rt, d, hash)) : jundef(); }
    if (n >= 4 && !memcmp(d, "for:", 4)) return jstrv(js_str_new(rt, d + 4, n - 4));
    char b[96]; uint32_t k = wu_copy(b, "Symbol.", sizeof b); k += wu_copy(b + k, d, sizeof b - k); return jstrv(js_str_new(rt, b, k));
}
jstr *js_sym_tostring(struct jrt *rt, jstr *s)
{
    jv d = js_sym_description(rt, s); struct wb b = {0}; wb_add(&b, "Symbol("); if (d.t == JT_STR) wb_addn(&b, d.u.s->d, d.u.s->len); wb_addc(&b, ')');
    jstr *r = js_str_new(rt, b.p, b.len); wb_free(&b); return r;
}
static jv sym_tostring(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_STR || thisv.u.s->len < 3) return jstrv(js_tostr(rt, thisv));
    return jstrv(js_sym_tostring(rt, thisv.u.s));
}
static jv bool_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv v = jbool(js_truthy(ARG(0)));
    if (rt->new_target) { rt->new_target = 0; return jobjv(js_toobj(rt, v)); } return v;
}
static jv bool_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv v = thisv.t == JT_OBJ && thisv.u.o->cls == JC_BOOL ? thisv.u.o->el[0] : thisv; if (v.t != JT_BOOL) js_throw_err(rt, E_TYPE, "no es un booleano");
    return magic ? v : jstrv(v.u.b ? rt->s_true : rt->s_false);
}
static const char *HEXC = "0123456789ABCDEF";
static int uri_keep(int c, int comp, int full) { if (wu_isalnum(c)) return 1; if (wu_strchr("-_.!~*'()", c)) return 1; if (!comp && wu_strchr(";/?:@&=+$,#", c)) return 1; (void)full; return 0; }
static jv glob_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic);
enum { G_PARSEINT, G_PARSEFLOAT, G_ISNAN, G_ISFINITE, G_ENCURI, G_ENCCOMP, G_DECURI, G_DECCOMP, G_ESCAPE, G_UNESCAPE, G_ATOB, G_BTOA, G_EVAL, G_STRUCTCLONE, G_QUEUEMICRO };
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static jv glob_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    switch (magic) {
    case G_PARSEINT: {
        jstr *s = js_tostr(rt, ARG(0)); const char *p = s->d; int radix = argc > 1 ? f64_to_i32(js_tonum(rt, args[1])) : 0; int neg = 0;
        while (wu_isspace(*p)) p++;
        if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
        if ((radix == 0 || radix == 16) && p[0] == '0' && (p[1] | 32) == 'x') { p += 2; radix = 16; } if (radix == 0) radix = 10;
        if (radix < 2 || radix > 36) return jnum(F64_NAN);
        f64 v = 0, rf = f64_from_i32(radix); int any = 0;
        for (;; p++) { int d = wu_isdigit(*p) ? *p - '0' : wu_isalpha(*p) ? (wu_lower(*p) - 'a' + 10) : 99; if (d >= radix) break; v = f64_add(f64_mul(v, rf), f64_from_i32(d)); any = 1; }
        if (!any) return jnum(F64_NAN);
        return jnum(neg ? f64_neg(v) : v);
    }
    case G_PARSEFLOAT: {
        jstr *s = js_tostr(rt, ARG(0)); const char *p = s->d; while (wu_isspace(*p)) p++;
        if (wu_starts(p, "Infinity") || wu_starts(p, "+Infinity")) return jnum(F64_INF);
        if (wu_starts(p, "-Infinity")) return jnum(F64_NINF);
        const char *end; f64 v = f64_parse(p, &end); if (!end) return jnum(F64_NAN); return jnum(v);
    }
    case G_ISNAN: return jbool(f64_is_nan(js_tonum(rt, ARG(0))));
    case G_ISFINITE: return jbool(f64_is_finite(js_tonum(rt, ARG(0))));
    case G_ENCURI: case G_ENCCOMP: {
        jstr *s = js_tostr(rt, ARG(0)); struct wb b = {0};
        for (uint32_t i = 0; i < s->len; i++) { unsigned char c = (unsigned char)s->d[i]; if (c < 128 && uri_keep(c, magic == G_ENCCOMP, 0)) wb_addc(&b, (char)c); else { wb_addc(&b, '%'); wb_addc(&b, HEXC[c >> 4]); wb_addc(&b, HEXC[c & 15]); } }
        return jstrv(js_str_new(rt, b.p ? b.p : "", b.len));
    }
    case G_DECURI: case G_DECCOMP: {
        jstr *s = js_tostr(rt, ARG(0)); struct wb b = {0};
        for (uint32_t i = 0; i < s->len; i++) {
            if (s->d[i] == '%') { if (i + 2 >= s->len + 0 && i + 2 > s->len - 1 + 0) { if (i + 2 > s->len - 1) js_throw_err(rt, E_URI, "URI malformada"); } if (!wu_ishex(s->d[i + 1]) || !wu_ishex(s->d[i + 2])) js_throw_err(rt, E_URI, "URI malformada"); int c = wu_hexval(s->d[i + 1]) * 16 + wu_hexval(s->d[i + 2]); if (magic == G_DECURI && c < 128 && wu_strchr(";/?:@&=+$,#", c)) { wb_addn(&b, s->d + i, 3); } else wb_addc(&b, (char)c); i += 2; }
            else wb_addc(&b, s->d[i]);
        }
        return jstrv(js_str_new(rt, b.p ? b.p : "", b.len));
    }
    case G_ESCAPE: {                     /* SOMA_185: escape/unescape de verdad (antes devolvian la cadena tal cual) */
        jstr *s = js_tostr(rt, ARG(0)); struct wb b = {0}; uint32_t n16 = js_u16len(s);
        for (uint32_t i = 0; i < n16; i++) {
            uint32_t u = (uint32_t)js_u16_unit(rt, s, i);
            if (u < 128 && (wu_isalnum((char)u) || wu_strchr("@*_+-./", (int)u))) wb_addc(&b, (char)u);
            else if (u < 256) { wb_addc(&b, '%'); wb_addc(&b, HEXC[u >> 4]); wb_addc(&b, HEXC[u & 15]); }
            else { wb_add(&b, "%u"); for (int k = 12; k >= 0; k -= 4) wb_addc(&b, HEXC[(u >> k) & 15]); }
        }
        jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
    }
    case G_UNESCAPE: {
        jstr *s = js_tostr(rt, ARG(0)); struct wb b = {0};
        for (uint32_t i = 0; i < s->len; i++) {
            char c = s->d[i];
            if (c == '%' && i + 5 < s->len && s->d[i + 1] == 'u' && wu_ishex(s->d[i + 2]) && wu_ishex(s->d[i + 3]) && wu_ishex(s->d[i + 4]) && wu_ishex(s->d[i + 5])) {
                js_wb_unit(&b, (uint32_t)(wu_hexval(s->d[i + 2]) << 12 | wu_hexval(s->d[i + 3]) << 8 | wu_hexval(s->d[i + 4]) << 4 | wu_hexval(s->d[i + 5]))); i += 5;
            } else if (c == '%' && i + 2 < s->len && wu_ishex(s->d[i + 1]) && wu_ishex(s->d[i + 2])) {
                js_wb_unit(&b, (uint32_t)(wu_hexval(s->d[i + 1]) * 16 + wu_hexval(s->d[i + 2]))); i += 2;
            } else wb_addc(&b, c);
        }
        jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
    }
    case G_BTOA: {
        jstr *s = js_tostr(rt, ARG(0)); struct wb in = {0};
        for (uint32_t i = 0; i < s->len;) { uint32_t adv; uint32_t cp = wu_utf8_next(s->d + i, &adv); if (!adv) adv = 1; if (cp > 255) js_throw_err(rt, E_ERROR, "La cadena contiene caracteres fuera del rango Latin1"); wb_addc(&in, (char)cp); i += adv; }
        struct wb o = {0};
        for (uint32_t i = 0; i < in.len; i += 3) { uint32_t v = (uint8_t)in.p[i] << 16; if (i + 1 < in.len) v |= (uint8_t)in.p[i + 1] << 8; if (i + 2 < in.len) v |= (uint8_t)in.p[i + 2]; wb_addc(&o, B64[v >> 18]); wb_addc(&o, B64[(v >> 12) & 63]); wb_addc(&o, i + 1 < in.len ? B64[(v >> 6) & 63] : '='); wb_addc(&o, i + 2 < in.len ? B64[v & 63] : '='); }
        jstr *r = js_str_new(rt, o.p ? o.p : "", o.len); wb_free(&in); wb_free(&o); return jstrv(r);
    }
    case G_ATOB: {
        jstr *s = js_tostr(rt, ARG(0)); struct wb o = {0}; uint32_t acc = 0; int bits = 0;
        for (uint32_t i = 0; i < s->len; i++) { char c = s->d[i]; if (c == '=' || wu_isspace(c)) continue; const char *p = wu_strchr(B64, c); if (!p || !c) js_throw_err(rt, E_ERROR, "La cadena a decodificar no esta correctamente codificada"); acc = (acc << 6) | (uint32_t)(p - B64); bits += 6; if (bits >= 8) { bits -= 8; char t[4]; int n = wu_utf8_put((acc >> bits) & 255, t); wb_addn(&o, t, (uint32_t)n); } }
        jstr *r = js_str_new(rt, o.p ? o.p : "", o.len); wb_free(&o); return jstrv(r);
    }
    case G_EVAL: { if (ARG(0).t != JT_STR) return ARG(0); int ok; jv r = js_eval_string(rt, args[0].u.s->d, args[0].u.s->len, &ok); if (!ok) js_throw(rt, r); return r; }
    case G_STRUCTCLONE: { if (ARG(0).t == JT_OBJ && (args[0].u.o->cls == JC_ABUF || args[0].u.o->cls == JC_TYPED || args[0].u.o->cls == JC_DVIEW)) return js_bin_clone(rt, args[0]);   /* SOMA_186 */
        jstr *j = js_json_stringify(rt, ARG(0), jundef(), jundef()); if (!j) return jundef(); return js_json_parse(rt, j->d, j->len); }
    case G_QUEUEMICRO: js_micro_enqueue(rt, ARG(0), jundef(), jundef(), 0, 0); return jundef();
    }
    return jundef();
}

/* ------------------------------ Reflect ------------------------------ */
enum { R_OWNKEYS, R_HAS, R_GET, R_SET, R_APPLY, R_CONSTRUCT, R_GETPROTO, R_DEFINE, R_DELETE, R_GETOWNDESC, R_SETPROTO, R_ISEXT, R_PREVENT };
static jv reflect_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv t = ARG(0);
    switch (magic) {
    case R_OWNKEYS: { jobj *ks = own_keys(rt, t, 1); if (t.t == JT_OBJ) own_syms_into(t.u.o, ks, rt, 0); return jobjv(ks); }   /* SOMA_345: con los simbolos */
    case R_HAS: if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.has llamado sobre un valor que no es objeto"); return jbool(js_has(rt, t.u.o, js_tostr(rt, ARG(1))));
    case R_GET:
        if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.get llamado sobre un valor que no es objeto");
        if (argc > 2 && !(args[2].t == JT_OBJ && args[2].u.o == t.u.o) && t.u.o->cls != JC_ARRAY && t.u.o->cls != JC_TYPED) {   /* SOMA_188: getters con el receptor (Vue) */
            jstr *k = js_intern(rt, js_tostr(rt, ARG(1)));
            for (jobj *q = t.u.o; q; q = q->proto) {
                if (q->cls == JC_PROXY) return js_proxy_get(rt, q, k, args[2]);
                struct jprop *pp = js_find_own(q, k);
                if (pp) { if (pp->flags & JP_ACC) return pp->val.t == JT_OBJ ? js_call(rt, pp->val, args[2], 0, 0) : jundef(); return pp->val; }
            }
            return js_get(rt, t, k);
        }
        return js_getv(rt, t, ARG(1));
    case R_SET: {
        if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.set llamado sobre un valor que no es objeto");
        if (t.u.o->cls == JC_PROXY) return jbool(js_proxy_set(rt, t.u.o, js_intern(rt, js_tostr(rt, ARG(1))), ARG(2), argc > 3 ? args[3] : t));
        if (argc < 4 || (args[3].t == JT_OBJ && args[3].u.o == t.u.o)) { js_putv(rt, t, ARG(1), ARG(2)); return jbool(1); }
        /* SOMA_188: OrdinarySet con receptor distinto (core-js lo comprueba con Object.prototype: antes se contaminaba) */
        jstr *k = js_intern(rt, js_tostr(rt, ARG(1))); jv recv = args[3];
        for (jobj *q = t.u.o; q; q = q->proto) {
            if (q->cls == JC_PROXY) return jbool(js_proxy_set(rt, q, k, ARG(2), recv));
            struct jprop *pp = js_find_own(q, k);
            if (pp) {
                if (pp->flags & JP_ACC) { if (pp->set.t != JT_OBJ) return jbool(0); jv v = ARG(2); js_call(rt, pp->set, recv, &v, 1); return jbool(1); }
                if (pp->flags & JP_RO) return jbool(0);
                break;
            }
        }
        if (recv.t != JT_OBJ) return jbool(0);
        /* SOMA_188: receptor Proxy (la trampa set de Vue hace Reflect.set(target, k, v, proxy)): el estandar pide
           Receiver.[[GetOwnProperty]] y [[DefineOwnProperty]], no [[Set]] (que volveria a la trampa: recursion infinita) */
        while (recv.t == JT_OBJ && recv.u.o->cls == JC_PROXY) {
            jobj *px = recv.u.o; jv h = px->el ? px->el[1] : jnull();
            if (h.t != JT_OBJ) js_throw_err(rt, E_TYPE, "No se puede operar sobre un Proxy revocado");
            jv dp = js_get(rt, h, js_intern_c(rt, "defineProperty"));
            if (js_is_callable(dp)) {
                jv ga[2] = { recv, jstrv(k) }; jv ex = obj_static(rt, jundef(), ga, 2, O_GETOWNDESC);
                jobj *d = js_obj_new(rt, rt->p_object); js_put(rt, jobjv(d), js_intern_c(rt, "value"), ARG(2));
                if (ex.t != JT_OBJ) { js_put(rt, jobjv(d), js_intern_c(rt, "writable"), jbool(1)); js_put(rt, jobjv(d), js_intern_c(rt, "enumerable"), jbool(1)); js_put(rt, jobjv(d), js_intern_c(rt, "configurable"), jbool(1)); }
                jv ca[3] = { px->el[0], jstrv(k), jobjv(d) };
                return jbool(js_truthy(js_call(rt, dp, h, ca, 3)));
            }
            recv = px->el[0];
        }
        if (recv.t != JT_OBJ) return jbool(0);
        struct jprop *rp = js_find_own(recv.u.o, k);
        if (rp) { if ((rp->flags & (JP_ACC | JP_RO)) || recv.u.o->frozen) return jbool(0); rp->val = ARG(2); return jbool(1); }
        if (!recv.u.o->extensible || recv.u.o->frozen) return jbool(0);
        js_put(rt, recv, k, ARG(2)); return jbool(1);
    }
    case R_APPLY: { jv a2[2] = { ARG(1), ARG(2) }; return func_proto(rt, t, a2, 2, F_APPLY); }
    case R_CONSTRUCT: { jv a = ARG(1); return js_construct(rt, t, a.t == JT_OBJ ? a.u.o->el : 0, a.t == JT_OBJ ? (int)a.u.o->alen : 0, argc > 2 && args[2].t == JT_OBJ ? args[2].u.o : 0); }
    case R_GETPROTO: if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.getPrototypeOf llamado sobre un valor que no es objeto"); return obj_static(rt, thisv, args, argc, O_GETPROTO);
    case R_GETOWNDESC: if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.getOwnPropertyDescriptor llamado sobre un valor que no es objeto"); return obj_static(rt, thisv, args, argc, O_GETOWNDESC);   /* SOMA_188 */
    case R_SETPROTO: if (t.t != JT_OBJ) js_throw_err(rt, E_TYPE, "Reflect.setPrototypeOf llamado sobre un valor que no es objeto"); obj_static(rt, thisv, args, argc, O_SETPROTO); return jbool(1);
    case R_ISEXT: return t.t == JT_OBJ ? obj_static(rt, thisv, args, argc, O_ISEXT) : jbool(0);
    case R_PREVENT: if (t.t == JT_OBJ) obj_static(rt, thisv, args, argc, O_PREVENT); return jbool(t.t == JT_OBJ);
    case R_DEFINE: if (t.t == JT_OBJ) { js_fn_proto_ensure(rt, t); from_desc(rt, t.u.o, js_tostr(rt, ARG(1)), ARG(2)); return jbool(1); } return jbool(0);
    case R_DELETE: return jbool(t.t == JT_OBJ && js_delete(rt, t.u.o, js_tostr(rt, ARG(1))));
    }
    return jundef();
}

/* ------------------------------ inspeccion para console ------------------------------ */
static void inspect_into(struct jrt *rt, struct wb *b, jv v, int depth, int top, jobj **seen, int nseen)
{
    switch (v.t) {
    case JT_STR: if (top) wb_addn(b, v.u.s->d, v.u.s->len); else { wb_addc(b, '\''); wb_addn(b, v.u.s->d, v.u.s->len); wb_addc(b, '\''); } return;
    case JT_OBJ: if (v.u.o->cls == JC_BIGINT) { jstr *s = js_big_tostr(rt, v, 10); wb_addn(b, s->d, s->len); if (!top) wb_addc(b, 'n'); return; } break;   /* SOMA 188B */
    default: { jstr *s = js_tostr(rt, v); wb_addn(b, s->d, s->len); return; }
    }
    jobj *o = v.u.o;
    for (int i = 0; i < nseen; i++) if (seen[i] == o) { wb_add(b, "[Circular]"); return; }
    if (js_is_callable(v)) {
        jv nm = js_get(rt, v, rt->s_name); wb_add(b, (o->cls == JC_FUNC && o->u.fn.cls) ? "[class " : "[Function"); 
        if (nm.t == JT_STR && nm.u.s->len) { if (o->cls != JC_FUNC || !o->u.fn.cls) wb_addc(b, ':'), wb_addc(b, ' '); else {} wb_addn(b, nm.u.s->d, nm.u.s->len); } else if (o->cls != JC_FUNC || !o->u.fn.cls) wb_add(b, " (anonymous)"); wb_addc(b, ']'); return;
    }
    if (o->cls == JC_ERROR) { jv st = js_get(rt, v, rt->s_stack); if (st.t == JT_STR) wb_addn(b, st.u.s->d, st.u.s->len); else { jstr *s = js_tostr(rt, v); wb_addn(b, s->d, s->len); } return; }
    if (o->cls == JC_REGEXP) { jstr *s = js_tostr(rt, v); wb_addn(b, s->d, s->len); return; }
    if (o->cls == JC_DATE) { jv f = js_get_c(rt, v, "toISOString"); if (js_is_callable(f)) { jv r = js_call(rt, f, v, 0, 0); if (r.t == JT_STR) { wb_addn(b, r.u.s->d, r.u.s->len); return; } } }
    if (o->cls == JC_HOST) { jv tag = js_get_c(rt, v, "tagName"); jv nn = js_get_c(rt, v, "nodeName"); wb_add(b, "<"); if (tag.t == JT_STR) { for (uint32_t i = 0; i < tag.u.s->len; i++) wb_addc(b, (char)wu_lower(tag.u.s->d[i])); } else if (nn.t == JT_STR) wb_addn(b, nn.u.s->d, nn.u.s->len); else wb_add(b, "object"); wb_add(b, ">"); return; }
    if (depth < 0) { wb_add(b, o->cls == JC_ARRAY ? "[Array]" : "[Object]"); return; }
    jobj *stk[16]; int ns = nseen < 15 ? nseen : 15; for (int i = 0; i < ns; i++) stk[i] = seen[i]; stk[ns++] = o;
    if (o->cls == JC_ARRAY || o->cls == JC_ARGS || o->cls == JC_SET) {
        wb_add(b, o->cls == JC_SET ? "Set(" : "["); if (o->cls == JC_SET) { wb_addint(b, o->alen); wb_add(b, ") { "); }
        else if (o->alen) wb_addc(b, ' ');
        for (uint32_t i = 0; i < o->alen && i < 100; i++) { if (i) wb_add(b, ", "); inspect_into(rt, b, o->el[i], depth - 1, 0, stk, ns); }
        if (o->alen > 100) { wb_add(b, ", ... "); wb_addint(b, o->alen - 100); wb_add(b, " more items"); }
        if (o->cls == JC_SET) wb_add(b, " }"); else { if (o->alen) wb_addc(b, ' '); wb_addc(b, ']'); } return;
    }
    if (o->cls == JC_MAP) {
        wb_add(b, "Map("); wb_addint(b, o->alen / 2); wb_add(b, ") { ");
        for (uint32_t i = 0; i + 1 < o->alen; i += 2) { if (i) wb_add(b, ", "); inspect_into(rt, b, o->el[i], depth - 1, 0, stk, ns); wb_add(b, " => "); inspect_into(rt, b, o->el[i + 1], depth - 1, 0, stk, ns); }
        wb_add(b, " }"); return;
    }
    if (o->cls == JC_PROMISE) { wb_add(b, "Promise { "); if (o->u.prom.state == 0) wb_add(b, "<pending>"); else { if (o->u.prom.state == 2) wb_add(b, "<rejected> "); inspect_into(rt, b, o->u.prom.value, depth - 1, 0, stk, ns); } wb_add(b, " }"); return; }
    if (o->cls == JC_STRING || o->cls == JC_NUMBER || o->cls == JC_BOOL) { wb_add(b, o->cls == JC_STRING ? "[String: " : o->cls == JC_NUMBER ? "[Number: " : "[Boolean: "); inspect_into(rt, b, o->el[0], 0, 0, stk, ns); wb_addc(b, ']'); return; }
    jv cn = o->proto ? js_get(rt, jobjv(o->proto), rt->s_constructor) : jundef(); jv cnn = cn.t == JT_OBJ ? js_get(rt, cn, rt->s_name) : jundef();
    if (cnn.t == JT_STR && cnn.u.s->len && !js_str_eq(cnn.u.s, js_intern_c(rt, "Object"))) { wb_addn(b, cnn.u.s->d, cnn.u.s->len); wb_addc(b, ' '); }
    wb_addc(b, '{'); int first = 1;
    for (uint32_t i = 0; i < o->np && i < 60; i++) {
        struct jprop *p = &o->props[i]; if (!(p->flags & JP_ENUM) || (p->key->d[0] == '@' && p->key->d[1] == '@')) continue;
        wb_add(b, first ? " " : ", "); first = 0; wb_addn(b, p->key->d, p->key->len); wb_add(b, ": ");
        if (p->flags & JP_ACC) wb_add(b, (p->val.t == JT_OBJ && p->set.t == JT_OBJ) ? "[Getter/Setter]" : p->val.t == JT_OBJ ? "[Getter]" : "[Setter]");
        else inspect_into(rt, b, p->val, depth - 1, 0, stk, ns);
    }
    if (!first) wb_addc(b, ' ');
    wb_addc(b, '}');
}
jstr *js_inspect(struct jrt *rt, jv v, int depth)
{
    struct wb b = {0}; inspect_into(rt, &b, v, depth, 1, 0, 0); jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
enum { CON_LOG, CON_INFO, CON_WARN, CON_ERROR, CON_DEBUG, CON_ASSERT, CON_CLEAR, CON_NOP, CON_TABLE, CON_COUNT, CON_TIME, CON_TIMEEND, CON_TRACE };
void js_log(struct jrt *rt, int level, const char *msg) { if (rt->host && rt->host->log) rt->host->log(rt->host->ud, level, msg); }
static jv console_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic == CON_CLEAR || magic == CON_NOP) return jundef();
    int level = magic == CON_WARN ? 1 : (magic == CON_ERROR || magic == CON_ASSERT || magic == CON_TRACE) ? 2 : 0;
    int start = 0;
    if (magic == CON_ASSERT) { if (js_truthy(ARG(0))) return jundef(); start = 1; }
    struct wb b = {0}; if (magic == CON_ASSERT) wb_add(&b, "Assertion failed: ");
    if (magic == CON_TIME || magic == CON_TIMEEND) { wb_add(&b, argc ? js_tostr(rt, args[0])->d : "default"); wb_add(&b, ": 0ms"); if (magic == CON_TIME) { wb_free(&b); return jundef(); } }
    for (int i = start; i < argc && magic != CON_TIME && magic != CON_TIMEEND; i++) {
        if (i > start) wb_addc(&b, ' ');
        if (i == start && args[i].t == JT_STR && wu_strchr(args[i].u.s->d, '%') && argc > start + 1) {   /* %s %d %o */
            jstr *f = args[i].u.s; int ai = i + 1;
            for (uint32_t k = 0; k < f->len; k++) {
                if (f->d[k] == '%' && k + 1 < f->len && ai < argc && wu_strchr("sdifoOjc", f->d[k + 1])) {
                    char c = f->d[++k]; if (c == 'c') { ai++; continue; }
                    if (c == 's') { jstr *s = args[ai].t == JT_OBJ ? js_inspect(rt, args[ai], 1) : js_tostr(rt, args[ai]); wb_addn(&b, s->d, s->len); }
                    else if (c == 'd' || c == 'i') { f64 n = js_tonum(rt, args[ai]); if (c == 'i') n = f64_trunc(n); jstr *s = js_num_to_str(rt, n); wb_addn(&b, s->d, s->len); }
                    else if (c == 'f') { jstr *s = js_num_to_str(rt, js_tonum(rt, args[ai])); wb_addn(&b, s->d, s->len); }
                    else { jstr *s = js_inspect(rt, args[ai], 2); wb_addn(&b, s->d, s->len); }
                    ai++;
                } else if (f->d[k] == '%' && k + 1 < f->len && f->d[k + 1] == '%') { wb_addc(&b, '%'); k++; }
                else wb_addc(&b, f->d[k]);
            }
            for (; ai < argc; ai++) { wb_addc(&b, ' '); jstr *s = js_inspect(rt, args[ai], 2); wb_addn(&b, s->d, s->len); }
            break;
        }
        jstr *s = js_inspect(rt, args[i], 2); wb_addn(&b, s->d, s->len);
    }
    js_log(rt, level, b.p ? b.p : ""); wb_free(&b); return jundef();
}

/* ------------------------------ inicializacion ------------------------------ */
void js_lib_arrays_init(struct jrt *rt);
void js_lib_numbers_init(struct jrt *rt);
void js_lib_async_init(struct jrt *rt);
void js_lib_init(struct jrt *rt)
{
    for (int i = 0; i < 128; i++) { char c = (char)i; rt->s_chars[i] = 0; (void)c; }
    rt->s_empty = 0; rt->s_empty = js_str_new(rt, "", 0); rt->s_empty->interned = 0; rt->s_empty = js_intern(rt, rt->s_empty);
    for (int i = 0; i < 128; i++) { char c = (char)i; jstr *s = (jstr *)js_alloc(rt, (uint32_t)sizeof(jstr) + 2); s->len = 1; s->d[0] = c; s->hash = wu_hash(&c, 1); rt->s_chars[i] = js_intern(rt, s); }
    rt->s_length = js_intern_c(rt, "length"); rt->s_prototype = js_intern_c(rt, "prototype"); rt->s_constructor = js_intern_c(rt, "constructor"); rt->s_proto = js_intern_c(rt, "__proto__");
    rt->s_name = js_intern_c(rt, "name"); rt->s_message = js_intern_c(rt, "message"); rt->s_undefined = js_intern_c(rt, "undefined"); rt->s_null = js_intern_c(rt, "null");
    rt->s_true = js_intern_c(rt, "true"); rt->s_false = js_intern_c(rt, "false"); rt->s_object = js_intern_c(rt, "object"); rt->s_function = js_intern_c(rt, "function");
    rt->s_number = js_intern_c(rt, "number"); rt->s_string = js_intern_c(rt, "string"); rt->s_boolean = js_intern_c(rt, "boolean"); rt->s_nan = js_intern_c(rt, "NaN");
    rt->s_stack = js_intern_c(rt, "stack"); rt->s_then = js_intern_c(rt, "then"); rt->s_value = js_intern_c(rt, "value"); rt->s_done = js_intern_c(rt, "done"); rt->s_next = js_intern_c(rt, "next");
    rt->s_index = js_intern_c(rt, "index"); rt->s_input = js_intern_c(rt, "input"); rt->s_get = js_intern_c(rt, "get"); rt->s_set = js_intern_c(rt, "set");
    rt->p_object = js_obj_new(rt, 0); rt->p_function = js_obj_new(rt, rt->p_object); rt->p_function->cls = JC_NATIVE; rt->p_function->u.nat.f = 0;
    rt->p_function->cls = JC_OBJECT;
    rt->p_array = js_obj_new(rt, rt->p_object); rt->p_array->cls = JC_ARRAY;
    rt->p_string = js_obj_new(rt, rt->p_object); rt->p_number = js_obj_new(rt, rt->p_object); rt->p_boolean = js_obj_new(rt, rt->p_object);
    rt->p_error = js_obj_new(rt, rt->p_object); rt->p_date = js_obj_new(rt, rt->p_object); rt->p_regexp = js_obj_new(rt, rt->p_object);
    rt->p_promise = js_obj_new(rt, rt->p_object); rt->p_map = js_obj_new(rt, rt->p_object); rt->p_set = js_obj_new(rt, rt->p_object); rt->p_iter = js_obj_new(rt, rt->p_object);
    rt->p_symbol = js_obj_new(rt, rt->p_object);
    rt->global = js_obj_new(rt, rt->p_object);
    rt->gscope = (struct scope *)js_alloc(rt, (uint32_t)sizeof(struct scope)); rt->gscope->heap = 1; rt->gscope->is_fn = 0; rt->gscope->this_v = jobjv(rt->global);
    js_def(rt, rt->global, js_intern_c(rt, "globalThis"), jobjv(rt->global), 0);
    js_def(rt, rt->global, js_intern_c(rt, "undefined"), jundef(), JP_RO | JP_NOCONF); js_def(rt, rt->global, js_intern_c(rt, "NaN"), jnum(F64_NAN), JP_RO); js_def(rt, rt->global, js_intern_c(rt, "Infinity"), jnum(F64_INF), JP_RO);

    /* Object */
    jobj *oc = mkctor(rt, "Object", obj_ctor, 1, 0, rt->p_object);
    static const struct { const char *n; int m; int a; } OS[] = { { "keys", O_KEYS, 1 }, { "values", O_VALUES, 1 }, { "entries", O_ENTRIES, 1 }, { "assign", O_ASSIGN, 2 }, { "freeze", O_FREEZE, 1 }, { "isFrozen", O_ISFROZEN, 1 }, { "seal", O_SEAL, 1 }, { "isSealed", O_ISFROZEN, 1 },
        { "preventExtensions", O_PREVENT, 1 }, { "isExtensible", O_ISEXT, 1 }, { "create", O_CREATE, 2 }, { "getPrototypeOf", O_GETPROTO, 1 }, { "setPrototypeOf", O_SETPROTO, 2 }, { "defineProperty", O_DEFPROP, 3 }, { "defineProperties", O_DEFPROPS, 2 },
        { "getOwnPropertyNames", O_GETOWNNAMES, 1 }, { "getOwnPropertyDescriptor", O_GETOWNDESC, 2 }, { "fromEntries", O_FROMENTRIES, 1 }, { "hasOwn", O_HASOWN, 2 }, { "is", O_IS, 2 }, { "getOwnPropertySymbols", O_GETOWNSYMS, 1 }, { 0, 0, 0 } };
    for (int i = 0; OS[i].n; i++) js_method(rt, oc, OS[i].n, obj_static, OS[i].a, OS[i].m);
    js_method(rt, oc->u.nat.f ? rt->p_object : rt->p_object, "hasOwnProperty", obj_proto, 1, OP_HASOWN);
    js_method(rt, rt->p_object, "toString", obj_proto, 0, OP_TOSTRING); js_method(rt, rt->p_object, "valueOf", obj_proto, 0, OP_VALUEOF); js_method(rt, rt->p_object, "isPrototypeOf", obj_proto, 1, OP_ISPROTO);
    js_method(rt, rt->p_object, "propertyIsEnumerable", obj_proto, 1, OP_PROPENUM); js_method(rt, rt->p_object, "toLocaleString", obj_proto, 0, OP_TOLOCALE);
    js_getter(rt, rt->p_object, "__proto__", obj_proto, obj_proto, OP_PROTOGET);
    { struct jprop *pp = js_find_own(rt->p_object, rt->s_proto); if (pp) { jobj *g = js_native(rt, "get __proto__", obj_proto, 0, OP_PROTOGET), *s = js_native(rt, "set __proto__", obj_proto, 1, OP_PROTOSET); pp->val = jobjv(g); pp->set = jobjv(s); } }
    /* Function */
    jobj *fc = mkctor(rt, "Function", func_ctor, 1, 0, rt->p_function); (void)fc;
    js_method(rt, rt->p_function, "call", func_proto, 1, F_CALL); js_method(rt, rt->p_function, "apply", func_proto, 2, F_APPLY); js_method(rt, rt->p_function, "bind", func_proto, 1, F_BIND);
    js_method(rt, rt->p_function, "toString", func_proto, 0, F_TOSTRING); js_def(rt, rt->p_function, js_intern_c(rt, "@@hasInstance"), jobjv(js_native(rt, "[Symbol.hasInstance]", func_proto, 1, F_HASINST)), 0);
    /* Error */
    jobj *ec = 0;
    for (int k = 0; k < 7; k++) {
        jobj *pr = k == 0 ? rt->p_error : js_obj_new(rt, rt->p_error); rt->p_errs[k] = pr;
        jobj *c = mkctor(rt, ERRN[k], err_ctor, 1, k, pr); if (k == 0) ec = c; else c->proto = ec;
        js_def(rt, pr, rt->s_name, jstrv(js_intern_c(rt, ERRN[k])), 0); js_def(rt, pr, rt->s_message, jstrv(rt->s_empty), 0);
    }
    js_method(rt, rt->p_error, "toString", err_tostring, 0, 0); js_method(rt, ec, "captureStackTrace", console_fn, 1, CON_NOP);
    /* Symbol */
    jobj *sc = mkctor(rt, "Symbol", sym_ctor, 0, 0, rt->p_symbol); js_method(rt, sc, "for", sym_for, 1, 0); js_method(rt, rt->p_symbol, "toString", sym_tostring, 0, 0);
    static const char *WK[] = { "iterator", "asyncIterator", "hasInstance", "toPrimitive", "toStringTag", "species", 0 };
    for (int i = 0; WK[i]; i++) { char b[40]; wu_copy(b, "@@", sizeof b); wu_copy(b + 2, WK[i], sizeof b - 2); js_def(rt, sc, js_intern_c(rt, WK[i]), jstrv(js_intern_c(rt, b)), 0); }
    /* Boolean */
    mkctor(rt, "Boolean", bool_ctor, 1, 0, rt->p_boolean); js_method(rt, rt->p_boolean, "toString", bool_proto, 0, 0); js_method(rt, rt->p_boolean, "valueOf", bool_proto, 0, 1);
    /* globales */
    static const struct { const char *n; int m; int a; } GS[] = { { "parseInt", G_PARSEINT, 2 }, { "parseFloat", G_PARSEFLOAT, 1 }, { "isNaN", G_ISNAN, 1 }, { "isFinite", G_ISFINITE, 1 }, { "encodeURI", G_ENCURI, 1 }, { "encodeURIComponent", G_ENCCOMP, 1 },
        { "decodeURI", G_DECURI, 1 }, { "decodeURIComponent", G_DECCOMP, 1 }, { "escape", G_ESCAPE, 1 }, { "unescape", G_UNESCAPE, 1 }, { "atob", G_ATOB, 1 }, { "btoa", G_BTOA, 1 }, { "eval", G_EVAL, 1 }, { "structuredClone", G_STRUCTCLONE, 1 }, { "queueMicrotask", G_QUEUEMICRO, 1 }, { 0, 0, 0 } };
    for (int i = 0; GS[i].n; i++) js_method(rt, rt->global, GS[i].n, glob_fn, GS[i].a, GS[i].m);
    { rt->s_eval188 = js_intern_c(rt, "eval"); jv ev = js_get(rt, jobjv(rt->global), rt->s_eval188); rt->eval_fn = ev.t == JT_OBJ ? ev.u.o : 0; }   /* SOMA_188 */
    /* Reflect */
    jobj *rf = js_obj_new(rt, rt->p_object); js_def(rt, rt->global, js_intern_c(rt, "Reflect"), jobjv(rf), 0);
    static const struct { const char *n; int m; } RS[] = { { "ownKeys", R_OWNKEYS }, { "has", R_HAS }, { "get", R_GET }, { "set", R_SET }, { "apply", R_APPLY }, { "construct", R_CONSTRUCT }, { "getPrototypeOf", R_GETPROTO }, { "defineProperty", R_DEFINE }, { "deleteProperty", R_DELETE },
        { "getOwnPropertyDescriptor", R_GETOWNDESC }, { "setPrototypeOf", R_SETPROTO }, { "isExtensible", R_ISEXT }, { "preventExtensions", R_PREVENT }, { 0, 0 } };
    for (int i = 0; RS[i].n; i++) js_method(rt, rf, RS[i].n, reflect_fn, 2, RS[i].m);
    /* console */
    jobj *con = js_obj_new(rt, rt->p_object); rt->g_console = con; js_def(rt, rt->global, js_intern_c(rt, "console"), jobjv(con), 0);
    static const struct { const char *n; int m; } CS[] = { { "log", CON_LOG }, { "info", CON_INFO }, { "warn", CON_WARN }, { "error", CON_ERROR }, { "debug", CON_DEBUG }, { "assert", CON_ASSERT }, { "clear", CON_CLEAR }, { "group", CON_LOG }, { "groupCollapsed", CON_LOG },
        { "groupEnd", CON_NOP }, { "table", CON_LOG }, { "dir", CON_LOG }, { "count", CON_NOP }, { "time", CON_TIME }, { "timeEnd", CON_TIMEEND }, { "timeLog", CON_TIMEEND }, { "trace", CON_TRACE }, { "profile", CON_NOP }, { "profileEnd", CON_NOP }, { 0, 0 } };
    for (int i = 0; CS[i].n; i++) js_method(rt, con, CS[i].n, console_fn, 0, CS[i].m);
    js_lib_arrays_init(rt); js_lib_numbers_init(rt); js_lib_async_init(rt); js_regex_init(rt); js_co_init(rt);   /* SOMA_183 */
    js_bin_init(rt);                                                                                      /* SOMA_186 */
    js_proxy_init(rt); js_big_init(rt);                                                                                    /* SOMA_188 */
    js_module_init(rt);                                                                                   /* SOMA_188 */
}
