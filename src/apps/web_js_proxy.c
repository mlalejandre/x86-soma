/* SOMA_188: Proxy y Reflect completos. Un Proxy (JC_PROXY) guarda en el[0] el destino y en el[1] el manejador
 * (null cuando se revoca). Las operaciones basicas del motor (js_get, js_put, js_has, js_delete, claves propias,
 * llamada y construccion) consultan aqui las trampas; si el manejador no define una trampa se aplica al destino.
 * Lo usan Vue 3 (reactividad), MobX, Immer, Solid, Valtio y muchos SDK. Se desactiva con soma.jsproxy=off. */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())

#if defined(WEB_HOST_TEST)
int g_jproxy_off;
#endif
int js_proxy_on(void)
{
#if defined(WEB_HOST_TEST)
    return !g_jproxy_off;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsproxy") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}

static jobj *ptarget(struct jrt *rt, jobj *p) { (void)rt; return p->el[0].u.o; }
static jobj *phandler(struct jrt *rt, jobj *p, const char *op)
{
    if (p->el[1].t != JT_OBJ) js_throw_err(rt, E_TYPE, "No se puede hacer '%s' sobre un Proxy revocado", op);
    return p->el[1].u.o;
}
static jv trap(struct jrt *rt, jobj *h, const char *name)
{
    jv t = js_get(rt, jobjv(h), js_intern_c(rt, name));
    if (IS_NULLISH(t)) return jundef();
    if (!js_is_callable(t)) js_throw_err(rt, E_TYPE, "La trampa '%s' del Proxy no es una funcion", name);
    return t;
}
static jv keyv(struct jrt *rt, jstr *k)
{
    if (k->len > 2 && k->d[0] == '@' && k->d[1] == '@') return jstrv(k);   /* simbolo (representacion interna) */
    (void)rt; return jstrv(k);
}

jv js_proxy_get(struct jrt *rt, jobj *p, jstr *key, jv receiver)
{
    jobj *h = phandler(rt, p, "get"); jv t = trap(rt, h, "get");
    if (t.t == JT_UNDEF) { jobj *tg = ptarget(rt, p); return tg->cls == JC_PROXY ? js_proxy_get(rt, tg, key, receiver) : js_get(rt, jobjv(tg), key); }
    jv a[3] = { p->el[0], keyv(rt, key), receiver };
    return js_call(rt, t, jobjv(h), a, 3);
}
int js_proxy_set(struct jrt *rt, jobj *p, jstr *key, jv val, jv receiver)
{
    jobj *h = phandler(rt, p, "set"); jv t = trap(rt, h, "set");
    if (t.t == JT_UNDEF) { js_put(rt, p->el[0], key, val); return 1; }
    jv a[4] = { p->el[0], keyv(rt, key), val, receiver };
    return js_truthy(js_call(rt, t, jobjv(h), a, 4));
}
int js_proxy_has(struct jrt *rt, jobj *p, jstr *key)
{
    jobj *h = phandler(rt, p, "has"); jv t = trap(rt, h, "has");
    if (t.t == JT_UNDEF) return js_has(rt, ptarget(rt, p), key);
    jv a[2] = { p->el[0], keyv(rt, key) };
    return js_truthy(js_call(rt, t, jobjv(h), a, 2));
}
int js_proxy_delete(struct jrt *rt, jobj *p, jstr *key)
{
    jobj *h = phandler(rt, p, "deleteProperty"); jv t = trap(rt, h, "deleteProperty");
    if (t.t == JT_UNDEF) return js_delete(rt, ptarget(rt, p), key);
    jv a[2] = { p->el[0], keyv(rt, key) };
    return js_truthy(js_call(rt, t, jobjv(h), a, 2));
}
/* descriptor propio (objeto) o undefined */
static jv own_desc(struct jrt *rt, jobj *p, jstr *key)
{
    jobj *h = phandler(rt, p, "getOwnPropertyDescriptor"); jv t = trap(rt, h, "getOwnPropertyDescriptor");
    if (t.t != JT_UNDEF) { jv a[2] = { p->el[0], keyv(rt, key) }; return js_call(rt, t, jobjv(h), a, 2); }
    jv od = js_get(rt, jobjv(rt->global), js_intern_c(rt, "Object"));
    jv gd = js_get(rt, od, js_intern_c(rt, "getOwnPropertyDescriptor"));
    jv a[2] = { p->el[0], jstrv(key) };
    return js_call(rt, gd, od, a, 2);
}
jobj *js_proxy_own_keys(struct jrt *rt, jobj *p, int all)
{
    jobj *h = phandler(rt, p, "ownKeys"); jv t = trap(rt, h, "ownKeys");
    jobj *ks;
    if (t.t == JT_UNDEF) {
        jobj *tg = ptarget(rt, p); jv gd = trap(rt, h, "getOwnPropertyDescriptor");
        if (tg->cls != JC_PROXY && gd.t == JT_UNDEF) return js_own_keys(rt, jobjv(tg), all);
        ks = tg->cls == JC_PROXY ? js_proxy_own_keys(rt, tg, 1) : js_own_keys(rt, jobjv(tg), 1);
    } else {
        jv a[1] = { p->el[0] }; jv r = js_call(rt, t, jobjv(h), a, 1);
        if (r.t != JT_OBJ) js_throw_err(rt, E_TYPE, "La trampa ownKeys debe devolver un objeto");
        ks = js_arr_new(rt, 8);
        jv len = js_get(rt, r, rt->s_length); int64_t n = f64_to_i64(js_tonum(rt, len));
        for (int64_t i = 0; i < n && i < 1000000; i++) { jv k = js_getv(rt, r, jint((int32_t)i)); if (k.t == JT_STR) js_arr_push(rt, ks, k); }
    }
    if (all) return ks;
    jobj *out = js_arr_new(rt, ks->alen + 1);
    for (uint32_t i = 0; i < ks->alen; i++) {
        jstr *k = ks->el[i].u.s; if (k->len > 2 && k->d[0] == '@' && k->d[1] == '@') continue;
        jv d = own_desc(rt, p, k);
        if (d.t == JT_OBJ && js_truthy(js_get(rt, d, js_intern_c(rt, "enumerable")))) js_arr_push(rt, out, ks->el[i]);
    }
    return out;
}
jv js_proxy_call(struct jrt *rt, jobj *p, jv thisv, jv *args, int argc)
{
    jobj *h = phandler(rt, p, "apply"); jv t = trap(rt, h, "apply");
    if (t.t == JT_UNDEF) return js_call(rt, p->el[0], thisv, args, argc);
    jv a[3] = { p->el[0], thisv, jobjv(js_array_from(rt, args, argc)) };
    return js_call(rt, t, jobjv(h), a, 3);
}
jv js_proxy_construct(struct jrt *rt, jobj *p, jv *args, int argc, jobj *newtarget)
{
    jobj *h = phandler(rt, p, "construct"); jv t = trap(rt, h, "construct");
    if (t.t == JT_UNDEF) return js_construct(rt, p->el[0], args, argc, newtarget && newtarget != p ? newtarget : ptarget(rt, p));
    jv a[3] = { p->el[0], jobjv(js_array_from(rt, args, argc)), jobjv(newtarget ? newtarget : p) };
    jv r = js_call(rt, t, jobjv(h), a, 3);
    if (r.t != JT_OBJ) js_throw_err(rt, E_TYPE, "La trampa construct debe devolver un objeto");
    return r;
}
jobj *js_proxy_proto(struct jrt *rt, jobj *p)
{
    jobj *h = phandler(rt, p, "getPrototypeOf"); jv t = trap(rt, h, "getPrototypeOf");
    if (t.t == JT_UNDEF) { jobj *tg = ptarget(rt, p); return tg->cls == JC_PROXY ? js_proxy_proto(rt, tg) : tg->proto; }
    jv a[1] = { p->el[0] }; jv r = js_call(rt, t, jobjv(h), a, 1);
    return r.t == JT_OBJ ? r.u.o : 0;
}
int js_proxy_is_array(jobj *p)
{
    for (int g = 0; p && p->cls == JC_PROXY && g < 64; g++) { jv t = p->el[0]; if (t.t != JT_OBJ) return 0; p = t.u.o; }
    return p && p->cls == JC_ARRAY;
}
int js_proxy_callable(jobj *p)
{
    for (int g = 0; p && p->cls == JC_PROXY && g < 64; g++) { jv t = p->el[0]; if (t.t != JT_OBJ) return 0; p = t.u.o; }
    return p && (p->cls == JC_FUNC || p->cls == JC_NATIVE || p->cls == JC_BOUND);
}

/* ------------------------------ constructor ------------------------------ */
static jv make_proxy(struct jrt *rt, jv target, jv handler)
{
    if (target.t != JT_OBJ || handler.t != JT_OBJ) js_throw_err(rt, E_TYPE, "No se puede crear un Proxy con un destino o manejador que no son objetos");
    jobj *p = js_obj_new(rt, target.u.o->cls == JC_PROXY ? js_proxy_proto(rt, target.u.o) : target.u.o->proto);
    p->cls = JC_PROXY; p->el = (jv *)js_alloc(rt, 2 * (uint32_t)sizeof(jv)); p->el[0] = target; p->el[1] = handler; p->acap = 2; p->alen = 0;
    return jobjv(p);
}
static jv proxy_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor Proxy requiere 'new'");
    return make_proxy(rt, ARG(0), ARG(1));
}
static jv proxy_revoke(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)args; (void)argc; (void)magic;
    (void)thisv; jobj *self = rt->cur_native; if (!self) return jundef();
    jv p = js_get(rt, jobjv(self), js_intern_c(rt, "@@proxy"));
    if (p.t == JT_OBJ && p.u.o->cls == JC_PROXY) { p.u.o->el[1] = jnull(); }
    return jundef();
}
static jv proxy_revocable(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    jv p = make_proxy(rt, ARG(0), ARG(1));
    jobj *r = js_obj_new(rt, rt->p_object);
    jobj *rv = js_native(rt, "revoke", proxy_revoke, 0, 0);
    js_def(rt, rv, js_intern_c(rt, "@@proxy"), p, 0);
    js_put(rt, jobjv(r), js_intern_c(rt, "proxy"), p);
    js_put(rt, jobjv(r), js_intern_c(rt, "revoke"), jobjv(rv));
    return jobjv(r);
}

void js_proxy_init(struct jrt *rt)
{
    if (!js_proxy_on()) return;
    jobj *c = js_native(rt, "Proxy", proxy_ctor, 2, 0);
    js_def(rt, rt->global, js_intern_c(rt, "Proxy"), jobjv(c), 0);
    js_def(rt, c, js_intern_c(rt, "revocable"), jobjv(js_native(rt, "revocable", proxy_revocable, 2, 0)), 0);
}
