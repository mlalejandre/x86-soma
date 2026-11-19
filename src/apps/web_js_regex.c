/* SOMA_PARCHE_NAVEGADOR: RegExp y operaciones de String con expresiones regulares. */
#include "web_js_i.h"
#include "web_regex.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())
#define RF_D 64
#define RC_N 64
struct rc_ent { jstr *src; int fl; struct wre *re; };

static jstr *LI(struct jrt *rt) { return js_intern_c(rt, "lastIndex"); }
static int parse_flags(struct jrt *rt, jstr *f)
{
    int fl = 0;
    for (uint32_t i = 0; i < f->len; i++) {
        int b = 0;
        switch (f->d[i]) { case 'g': b = WRE_G; break; case 'i': b = WRE_I; break; case 'm': b = WRE_M; break; case 's': b = WRE_S; break;
                           case 'u': case 'v': b = WRE_U; break; case 'y': b = WRE_Y; break; case 'd': b = RF_D; break; default: break; }
        if (!b || (fl & b)) js_throw_err(rt, E_SYNTAX, "Flags de expresion regular invalidos: '%s'", f->d);
        fl |= b;
    }
    return fl;
}
static jstr *flags_str(struct jrt *rt, int fl)
{
    char b[8]; int n = 0;
    if (fl & RF_D) b[n++] = 'd';
    if (fl & WRE_G) b[n++] = 'g';
    if (fl & WRE_I) b[n++] = 'i';
    if (fl & WRE_M) b[n++] = 'm';
    if (fl & WRE_S) b[n++] = 's';
    if (fl & WRE_U) b[n++] = 'u';
    if (fl & WRE_Y) b[n++] = 'y';
    return js_str_new(rt, b, (uint32_t)n);
}
static struct wre *compile_cached(struct jrt *rt, jstr *src, int fl)
{
    struct rc_ent *tab = (struct rc_ent *)rt->recache;
    if (!tab) { tab = (struct rc_ent *)js_alloc(rt, RC_N * (uint32_t)sizeof *tab); rt->recache = tab; }
    for (int i = 0; i < RC_N; i++)
        if (tab[i].re && tab[i].fl == fl && tab[i].src->len == src->len && !memcmp(tab[i].src->d, src->d, src->len)) return tab[i].re;
    char err[96];
    struct wre *re = wre_compile(&rt->ar, src->d, src->len, fl, err, sizeof err);
    if (!re) {
        char shown[80]; wu_copy(shown, src->d, sizeof shown);
        js_throw_err(rt, E_SYNTAX, "Expresion regular invalida: /%s/: %s", shown, err);
    }
    struct rc_ent *e = &tab[rt->recache_pos++ % RC_N]; e->src = src; e->fl = fl; e->re = re;
    return re;
}
jobj *js_regex_new(struct jrt *rt, jstr *src, jstr *flags)
{
    int fl = parse_flags(rt, flags);
    struct wre *re = compile_cached(rt, src, fl);
    jobj *o = js_obj_new(rt, rt->p_regexp); o->cls = JC_REGEXP;
    o->u.re.prog = re; o->u.re.src = src; o->u.re.flags = flags_str(rt, fl);
    js_def(rt, o, LI(rt), jint(0), 0);
    return o;
}
static jobj *this_re(struct jrt *rt, jv t, const char *who)
{
    if (t.t != JT_OBJ || t.u.o->cls != JC_REGEXP) js_throw_err(rt, E_TYPE, "%s llamado sobre un objeto que no es RegExp", who);
    return t.u.o;
}
static struct wre *prog(jobj *re) { return (struct wre *)re->u.re.prog; }
static uint32_t get_li(struct jrt *rt, jobj *re)
{
    int64_t i = f64_to_i64(js_tonum(rt, js_get(rt, jobjv(re), LI(rt))));
    if (i < 0) i = 0;
    if (i > 0x7fffffff) i = 0x7fffffff;
    return (uint32_t)i;
}
static void set_li(struct jrt *rt, jobj *re, uint32_t v) { js_put(rt, jobjv(re), LI(rt), jint(v)); }
/* SOMA_185: el motor de expresiones trabaja en bytes UTF-8; lastIndex, index y las posiciones que ve el script son unidades UTF-16 */
static uint32_t li_to_byte(struct jrt *rt, jstr *s, uint32_t li, int *beyond)
{
    *beyond = li > js_u16len(s); if (*beyond) return 0;
    int mid; uint32_t b = js_u16_to_byte(rt, s, li, &mid); return mid ? b + 4 : b;
}
static void set_li_b(struct jrt *rt, jobj *re, jstr *s, uint32_t byte) { set_li(rt, re, js_byte_to_u16(rt, s, byte)); }
static int exec_at(struct jrt *rt, jobj *re, jstr *s, uint32_t start, int32_t *caps)
{
    wre_stack_lo = rt->stack_lo ? rt->stack_lo + 16 * 1024 : 0;
    int r = wre_exec(prog(re), s->d, s->len, start, caps);
    return r > 0 ? 1 : 0;
}
static uint32_t adv1(const jstr *s, uint32_t i)
{
    if (i >= s->len) return i + 1;
    uint32_t a = 1; wu_utf8_next(s->d + i, &a); if (!a) a = 1;
    return i + a;
}
static jv group_val(struct jrt *rt, jstr *s, const int32_t *caps, int g)
{
    if (caps[2 * g] < 0) return jundef();
    return jstrv(js_str_new(rt, s->d + caps[2 * g], (uint32_t)(caps[2 * g + 1] - caps[2 * g])));
}
static jv groups_obj(struct jrt *rt, jobj *re, jstr *s, const int32_t *caps)
{
    struct wre *p = prog(re); int ng = wre_ngroups(p), any = 0;
    for (int g = 1; g <= ng; g++) if (wre_group_name(p, g)) any = 1;
    if (!any) return jundef();
    jobj *o = js_obj_new(rt, 0);
    for (int g = 1; g <= ng; g++) { const char *nm = wre_group_name(p, g); if (nm) js_set_c(rt, o, nm, group_val(rt, s, caps, g)); }
    return jobjv(o);
}
static jv build_result(struct jrt *rt, jobj *re, jstr *s, const int32_t *caps)
{
    int ng = wre_ngroups(prog(re));
    jobj *a = js_arr_new(rt, (uint32_t)ng + 1);
    for (int g = 0; g <= ng; g++) js_arr_push(rt, a, group_val(rt, s, caps, g));
    js_set_c(rt, a, "index", jint(js_byte_to_u16(rt, s, (uint32_t)caps[0]))); js_set_c(rt, a, "input", jstrv(s));
    js_set_c(rt, a, "groups", groups_obj(rt, re, s, caps));
    return jobjv(a);
}
/* RegExpBuiltinExec */
static jv builtin_exec(struct jrt *rt, jobj *re, jstr *s)
{
    int fl = wre_flags(prog(re)), use = (fl & (WRE_G | WRE_Y)) != 0; uint32_t li = use ? get_li(rt, re) : 0;
    int32_t caps[2 * (WRE_MAXG + 1)]; int beyond; uint32_t lb = li_to_byte(rt, s, li, &beyond);
    if (beyond) { if (use) set_li(rt, re, 0); return jnull(); }
    if (!exec_at(rt, re, s, lb, caps)) { if (use) set_li(rt, re, 0); return jnull(); }
    if (use) set_li_b(rt, re, s, (uint32_t)caps[1]);
    return build_result(rt, re, s, caps);
}
static jv re_exec(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *re = this_re(rt, thisv, "RegExp.prototype.exec"); jstr *s = js_tostr(rt, ARG(0)); (void)magic;
    return builtin_exec(rt, re, s);
}
static jv re_test(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *re = this_re(rt, thisv, "RegExp.prototype.test"); jstr *s = js_tostr(rt, ARG(0)); (void)magic;
    int fl = wre_flags(prog(re)), use = (fl & (WRE_G | WRE_Y)) != 0; uint32_t li = use ? get_li(rt, re) : 0;
    int32_t caps[2 * (WRE_MAXG + 1)]; int beyond; uint32_t lb = li_to_byte(rt, s, li, &beyond);
    if (beyond || !exec_at(rt, re, s, lb, caps)) { if (use) set_li(rt, re, 0); return jbool(0); }
    if (use) set_li_b(rt, re, s, (uint32_t)caps[1]);
    return jbool(1);
}
static jstr *source_of(struct jrt *rt, jobj *re)
{
    jstr *src = re->u.re.src;
    if (!src->len) return js_str_cstr(rt, "(?:)");
    struct wb b = {0}; int cls = 0;
    for (uint32_t i = 0; i < src->len; i++) {
        char c = src->d[i];
        if (c == '\\' && i + 1 < src->len) { wb_addc(&b, c); wb_addc(&b, src->d[++i]); continue; }
        if (c == '[') cls = 1; else if (c == ']') cls = 0;
        if (c == '/' && !cls) { wb_addc(&b, '\\'); wb_addc(&b, '/'); }
        else if (c == '\n') wb_add(&b, "\\n");
        else if (c == '\r') wb_add(&b, "\\r");
        else wb_addc(&b, c);
    }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
static jv re_tostring(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)args; (void)argc; (void)magic;
    if (thisv.t != JT_OBJ) js_throw_err(rt, E_TYPE, "RegExp.prototype.toString llamado sobre un valor invalido");
    if (thisv.u.o->cls != JC_REGEXP) return jstrv(js_str_cstr(rt, "/(?:)/"));
    jobj *re = thisv.u.o; struct wb b = {0};
    wb_addc(&b, '/'); jstr *s = source_of(rt, re); wb_addn(&b, s->d, s->len); wb_addc(&b, '/'); wb_addn(&b, re->u.re.flags->d, re->u.re.flags->len);
    jstr *r = js_str_new(rt, b.p, b.len); wb_free(&b); return jstrv(r);
}
static jv re_getter(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)args; (void)argc;
    if (thisv.t != JT_OBJ || thisv.u.o->cls != JC_REGEXP) {
        if (thisv.t == JT_OBJ && thisv.u.o == rt->p_regexp) return magic == -1 ? jstrv(js_str_cstr(rt, "(?:)")) : magic == -2 ? jstrv(rt->s_empty) : jundef();
        js_throw_err(rt, E_TYPE, "Getter de RegExp llamado sobre un objeto que no es RegExp");
    }
    jobj *re = thisv.u.o;
    if (magic == -1) return jstrv(source_of(rt, re));
    if (magic == -2) return jstrv(re->u.re.flags);
    int fl = wre_flags(prog(re)); return jbool((fl & magic) != 0);
}
static jv regexp_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    jobj *nt = rt->new_target; rt->new_target = 0;
    jv pat = ARG(0), fl = ARG(1); jstr *src, *flags;
    if (pat.t == JT_OBJ && pat.u.o->cls == JC_REGEXP) {
        if (!nt && IS_UNDEF(fl)) return pat;
        src = pat.u.o->u.re.src; flags = IS_UNDEF(fl) ? pat.u.o->u.re.flags : js_tostr(rt, fl);
    } else { src = IS_UNDEF(pat) ? rt->s_empty : js_tostr(rt, pat); flags = IS_UNDEF(fl) ? rt->s_empty : js_tostr(rt, fl); }
    jobj *o = js_regex_new(rt, src, flags);
    if (nt) { jv p = js_get(rt, jobjv(nt), rt->s_prototype); if (p.t == JT_OBJ) o->proto = p.u.o; }
    return jobjv(o);
}
static jv re_escape(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic; jstr *s = js_tostr(rt, ARG(0)); struct wb b = {0};
    for (uint32_t i = 0; i < s->len; i++) { char c = s->d[i]; if (wu_strchr("\\^$.*+?()[]{}|/", c) && c) wb_addc(&b, '\\'); wb_addc(&b, c); }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
}
void js_regex_init(struct jrt *rt)
{
    jobj *proto = rt->p_regexp;
    jobj *c = js_native(rt, "RegExp", regexp_ctor, 2, 0);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, "RegExp"), jobjv(c), 0);
    js_method(rt, c, "escape", re_escape, 1, 0);
    js_method(rt, proto, "exec", re_exec, 1, 0); js_method(rt, proto, "test", re_test, 1, 0); js_method(rt, proto, "toString", re_tostring, 0, 0);
    js_getter(rt, proto, "source", re_getter, 0, -1); js_getter(rt, proto, "flags", re_getter, 0, -2);
    js_getter(rt, proto, "global", re_getter, 0, WRE_G); js_getter(rt, proto, "ignoreCase", re_getter, 0, WRE_I); js_getter(rt, proto, "multiline", re_getter, 0, WRE_M);
    js_getter(rt, proto, "dotAll", re_getter, 0, WRE_S); js_getter(rt, proto, "unicode", re_getter, 0, WRE_U); js_getter(rt, proto, "sticky", re_getter, 0, WRE_Y);
    js_getter(rt, proto, "hasIndices", re_getter, 0, RF_D);
}

/* ------------------------------------------------------------ operaciones de String */
static void expand_tpl(struct wb *b, jstr *tpl, jstr *s, const int32_t *caps, struct wre *p)
{
    int ng = wre_ngroups(p);
    for (uint32_t k = 0; k < tpl->len; k++) {
        char ch = tpl->d[k];
        if (ch != '$' || k + 1 >= tpl->len) { wb_addc(b, ch); continue; }
        char c = tpl->d[k + 1];
        if (c == '$') { wb_addc(b, '$'); k++; }
        else if (c == '&') { wb_addn(b, s->d + caps[0], (uint32_t)(caps[1] - caps[0])); k++; }
        else if (c == '`') { wb_addn(b, s->d, (uint32_t)caps[0]); k++; }
        else if (c == '\'') { wb_addn(b, s->d + caps[1], s->len - (uint32_t)caps[1]); k++; }
        else if (wu_isdigit(c)) {
            int n = c - '0', used = 1;
            if (k + 2 < tpl->len && wu_isdigit(tpl->d[k + 2]) && n * 10 + (tpl->d[k + 2] - '0') <= ng) { n = n * 10 + (tpl->d[k + 2] - '0'); used = 2; }
            if (n >= 1 && n <= ng) { if (caps[2 * n] >= 0) wb_addn(b, s->d + caps[2 * n], (uint32_t)(caps[2 * n + 1] - caps[2 * n])); k += (uint32_t)used; }
            else wb_addc(b, '$');
        } else if (c == '<') {
            int named = 0; for (int g = 1; g <= ng; g++) if (wre_group_name(p, g)) named = 1;
            uint32_t e = k + 2; while (named && e < tpl->len && tpl->d[e] != '>') e++;
            if (!named || e >= tpl->len) { wb_addc(b, '$'); continue; }
            for (int g = 1; g <= ng; g++) {
                const char *nm = wre_group_name(p, g);
                if (nm && wu_strlen(nm) == e - (k + 2) && !memcmp(nm, tpl->d + k + 2, e - (k + 2))) { if (caps[2 * g] >= 0) wb_addn(b, s->d + caps[2 * g], (uint32_t)(caps[2 * g + 1] - caps[2 * g])); break; }
            }
            k = e;
        } else wb_addc(b, '$');
    }
}
static jv op_replace(struct jrt *rt, int op, jobj *re, jstr *s, jv rep)
{
    struct wre *p = prog(re); int fl = wre_flags(p), all = (fl & WRE_G) != 0, sticky = (fl & WRE_Y) != 0, ng = wre_ngroups(p);
    if (op == S_REPLACEALL && !all) js_throw_err(rt, E_TYPE, "replaceAll debe llamarse con una RegExp global");
    int fn = js_is_callable(rep); jstr *rs = fn ? 0 : js_tostr(rt, rep);
    struct wb out = {0}; uint32_t last = 0, pos = 0; int32_t caps[2 * (WRE_MAXG + 1)]; int matched = 0;
    if (!all && sticky) { int beyond; pos = li_to_byte(rt, s, get_li(rt, re), &beyond); if (beyond) { set_li(rt, re, 0); return jstrv(s); } }
    if (all) set_li(rt, re, 0);
    while (pos <= s->len) {
        if (!exec_at(rt, re, s, pos, caps)) { if (sticky) set_li(rt, re, 0); break; }
        matched = 1;
        wb_addn(&out, s->d + last, (uint32_t)caps[0] - last);
        if (fn) {
            jv a[WRE_MAXG + 4]; int n = 0;
            for (int g = 0; g <= ng; g++) a[n++] = group_val(rt, s, caps, g);
            a[n++] = jint(js_byte_to_u16(rt, s, (uint32_t)caps[0])); a[n++] = jstrv(s);
            jv go = groups_obj(rt, re, s, caps); if (!IS_UNDEF(go)) a[n++] = go;
            jstr *r = js_tostr(rt, js_call(rt, rep, jundef(), a, n)); wb_addn(&out, r->d, r->len);
        } else expand_tpl(&out, rs, s, caps, p);
        last = (uint32_t)caps[1];
        if (!all) { if (sticky) set_li_b(rt, re, s, last); break; }
        pos = caps[1] == caps[0] ? adv1(s, (uint32_t)caps[1]) : (uint32_t)caps[1];
    }
    if (!matched) { wb_free(&out); return jstrv(s); }
    if (last < s->len) wb_addn(&out, s->d + last, s->len - last);
    jstr *r = js_str_new(rt, out.p ? out.p : "", out.len); wb_free(&out); return jstrv(r);
}
static jv op_split(struct jrt *rt, jobj *re, jstr *s, jv limv)
{
    struct wre *p = prog(re); int ng = wre_ngroups(p); int64_t lim = IS_UNDEF(limv) ? 0x7fffffff : (int64_t)(uint32_t)f64_to_i64(js_tonum(rt, limv));
    jobj *out = js_arr_new(rt, 4); int32_t caps[2 * (WRE_MAXG + 1)];
    if (lim == 0) return jobjv(out);
    if (!s->len) { if (!exec_at(rt, re, s, 0, caps) || caps[1] != 0) js_arr_push(rt, out, jstrv(s)); return jobjv(out); }
    uint32_t pp = 0, q = 0;
    while (q < s->len) {
        /* split ignora el flag sticky: busca la siguiente coincidencia desde q */
        wre_stack_lo = rt->stack_lo ? rt->stack_lo + 16 * 1024 : 0;
        int r = wre_exec(p, s->d, s->len, q, caps);
        if (r <= 0 || (uint32_t)caps[0] >= s->len) break;
        uint32_t ms = (uint32_t)caps[0], me = (uint32_t)caps[1];
        if (me == pp) { q = adv1(s, ms); continue; }
        js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + pp, ms - pp)));
        if ((int64_t)out->alen >= lim) return jobjv(out);
        for (int g = 1; g <= ng; g++) { js_arr_push(rt, out, group_val(rt, s, caps, g)); if ((int64_t)out->alen >= lim) return jobjv(out); }
        pp = me; q = me == ms ? adv1(s, me) : me;
    }
    js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + pp, s->len - pp)));
    return jobjv(out);
}
jv js_regex_string_op(struct jrt *rt, int op, jstr *s, jv *args, int argc)
{
    if (argc < 1 || args[0].t != JT_OBJ || args[0].u.o->cls != JC_REGEXP) js_throw_err(rt, E_TYPE, "Se esperaba una expresion regular");
    jobj *re = args[0].u.o; int fl = wre_flags(prog(re)); int32_t caps[2 * (WRE_MAXG + 1)];
    switch (op) {
    case S_MATCH: {
        if (!(fl & WRE_G)) return builtin_exec(rt, re, s);
        jobj *out = js_arr_new(rt, 8); uint32_t pos = 0; set_li(rt, re, 0);
        while (pos <= s->len && exec_at(rt, re, s, pos, caps)) {
            js_arr_push(rt, out, jstrv(js_str_new(rt, s->d + caps[0], (uint32_t)(caps[1] - caps[0]))));
            pos = caps[1] == caps[0] ? adv1(s, (uint32_t)caps[1]) : (uint32_t)caps[1];
        }
        return out->alen ? jobjv(out) : jnull();
    }
    case S_MATCHALL: {
        if (!(fl & WRE_G)) js_throw_err(rt, E_TYPE, "matchAll requiere una RegExp global");
        jobj *out = js_arr_new(rt, 8); uint32_t pos = 0;
        while (pos <= s->len && exec_at(rt, re, s, pos, caps)) {
            js_arr_push(rt, out, build_result(rt, re, s, caps));
            pos = caps[1] == caps[0] ? adv1(s, (uint32_t)caps[1]) : (uint32_t)caps[1];
        }
        jv arr = jobjv(out); return js_call(rt, js_get_c(rt, arr, "values"), arr, 0, 0);
    }
    case S_SEARCH: return exec_at(rt, re, s, 0, caps) ? jint(js_byte_to_u16(rt, s, (uint32_t)caps[0])) : jint(-1);
    case S_REPLACE: case S_REPLACEALL: return op_replace(rt, op, re, s, argc > 1 ? args[1] : jundef());
    case S_SPLIT: return op_split(rt, re, s, argc > 1 ? args[1] : jundef());
    default: js_throw_err(rt, E_TYPE, "Operacion de cadena no soportada con RegExp");
    }
}
