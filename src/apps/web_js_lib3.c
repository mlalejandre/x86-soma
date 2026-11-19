/* SOMA_PARCHE_NAVEGADOR: Number, Math, JSON, Date, Intl minimo. */
#include "web_js_i.h"
#include "rng.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())
jobj *js_own_keys(struct jrt *rt, jv v, int all);
static jobj *mkctor3(struct jrt *rt, const char *name, jnative f, int nargs, int magic, jobj *proto)
{
    jobj *c = js_native(rt, name, f, nargs, magic);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0); return c;
}
static f64 cnum(const char *s) { return f64_parse(s, 0); }

/* ------------------------------ Number ------------------------------ */
enum { N_TOSTRING, N_TOFIXED, N_TOPRECISION, N_TOEXP, N_VALUEOF, N_TOLOCALE };
static f64 this_num(struct jrt *rt, jv t)
{
    if (t.t == JT_NUM) return t.u.n;
    if (t.t == JT_OBJ && t.u.o->cls == JC_NUMBER) return t.u.o->el[0].u.n;
    js_throw_err(rt, E_TYPE, "Number.prototype llamado sobre un valor que no es numero"); return 0;
}
static jstr *group_num(struct jrt *rt, f64 n, int minfd, int maxfd, char thou, char dec, int grouping)
{
    char buf[F64_STR_MAX];                                   /* SOMA_200: antes 400, con maxfd elegido por la pagina */
    if (minfd < 0) minfd = 0;
    if (minfd > F64_DIGITS_MAX) minfd = F64_DIGITS_MAX;
    if (maxfd > F64_DIGITS_MAX) maxfd = F64_DIGITS_MAX;
    if (maxfd < minfd) maxfd = minfd;
    if (f64_is_nan(n)) return js_intern_c(rt, "NaN");
    if (f64_is_inf(n)) return js_intern_c(rt, (n >> 63) ? "-∞" : "∞");
    f64_to_fixed(n, maxfd, buf);
    char *p = buf; int neg = 0; if (*p == '-') { neg = 1; p++; }
    char *dot = p; while (*dot && *dot != '.') dot++;
    int fl = 0; if (*dot == '.') fl = (int)wu_strlen(dot + 1);
    int trim = 0; while (fl > minfd && dot[fl] == '0') { fl--; trim++; }
    struct wb b = {0}; if (neg) wb_addc(&b, '-');
    int il = (int)(dot - p);
    for (int i = 0; i < il; i++) { wb_addc(&b, p[i]); int rem = il - 1 - i; if (grouping && rem > 0 && rem % 3 == 0) wb_addc(&b, thou); }
    if (fl > 0) { wb_addc(&b, dec); wb_addn(&b, dot + 1, (uint32_t)fl); }
    (void)trim; jstr *r = js_str_new(rt, b.p, b.len); wb_free(&b); return r;
}
static jstr *locale_number(struct jrt *rt, f64 n, jv locale, jv opts)
{
    char thou = ',', dec = '.'; int grouping = 1;
    if (locale.t == JT_STR) { const char *l = locale.u.s->d; if (wu_starts(l, "es") || wu_starts(l, "de") || wu_starts(l, "it") || wu_starts(l, "pt") || wu_starts(l, "nl") || wu_starts(l, "id") || wu_starts(l, "tr")) { thou = '.'; dec = ','; } else if (wu_starts(l, "fr") || wu_starts(l, "ru") || wu_starts(l, "pl") || wu_starts(l, "cs")) { thou = ' '; dec = ','; } }
    int minfd = 0, maxfd = 3; jv style = jundef(), cur = jundef();
    if (opts.t == JT_OBJ) {
        jv a = js_get_c(rt, opts, "minimumFractionDigits"), b = js_get_c(rt, opts, "maximumFractionDigits"); style = js_get_c(rt, opts, "style"); cur = js_get_c(rt, opts, "currency");
        if (!IS_UNDEF(a)) { minfd = (int)f64_to_i64(js_tonum(rt, a)); if (maxfd < minfd) maxfd = minfd; } if (!IS_UNDEF(b)) { maxfd = (int)f64_to_i64(js_tonum(rt, b)); if (minfd > maxfd) minfd = maxfd; }
        if (style.t == JT_STR && wu_streq(style.u.s->d, "currency") && IS_UNDEF(a) && IS_UNDEF(b)) { minfd = 2; maxfd = 2; }
        if (style.t == JT_STR && wu_streq(style.u.s->d, "percent")) { n = f64_mul(n, f64_from_i32(100)); if (IS_UNDEF(b)) maxfd = minfd > 0 ? minfd : 0; }
        jv ug = js_get_c(rt, opts, "useGrouping"); if (ug.t == JT_BOOL && !ug.u.b) grouping = 0;
    }
    if (thou == '.' && locale.t == JT_STR && wu_starts(locale.u.s->d, "es") && f64_lt(f64_abs(n), cnum("10000")) && !(opts.t == JT_OBJ && js_truthy(js_get_c(rt, opts, "useGrouping")))) grouping = 0;
    jstr *num = group_num(rt, n, minfd, maxfd, thou, dec, grouping);
    if (style.t == JT_STR && wu_streq(style.u.s->d, "percent")) return js_str_concat(rt, num, js_intern_c(rt, "%"));
    if (style.t == JT_STR && wu_streq(style.u.s->d, "currency") && cur.t == JT_STR) {
        const char *c = cur.u.s->d, *sym = wu_streq(c, "USD") ? "$" : wu_streq(c, "EUR") ? "€" : wu_streq(c, "GBP") ? "£" : wu_streq(c, "JPY") ? "¥" : c;
        if (dec == ',') { jstr *r = js_str_concat(rt, num, js_intern_c(rt, "\xC2\xA0")); return js_str_concat(rt, r, js_intern_c(rt, sym)); }
        if (num->d[0] == '-') return js_str_concat(rt, js_str_concat(rt, js_intern_c(rt, "-"), js_intern_c(rt, sym)), js_str_new(rt, num->d + 1, num->len - 1));
        return js_str_concat(rt, js_intern_c(rt, sym), num);
    }
    return num;
}
static jv number_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    f64 n = this_num(rt, thisv); char buf[F64_STR_MAX];      /* SOMA_200: antes 400: (1e308).toString(2) escribe 1025 bytes */
    switch (magic) {
    case N_VALUEOF: return jnum(n);
    case N_TOSTRING: { int radix = IS_UNDEF(ARG(0)) ? 10 : (int)f64_to_i64(js_tonum(rt, args[0])); if (radix < 2 || radix > 36) js_throw_err(rt, E_RANGE, "radix debe estar entre 2 y 36"); if (radix == 10) return jstrv(js_num_to_str(rt, n)); int l = f64_to_str(n, buf, radix); return jstrv(js_str_new(rt, buf, (uint32_t)l)); }
    case N_TOFIXED: { int d = (int)f64_to_i64(js_tonum(rt, ARG(0))); if (d < 0 || d > 100) js_throw_err(rt, E_RANGE, "toFixed() digits debe estar entre 0 y 100"); int l = f64_to_fixed(n, d, buf); return jstrv(js_str_new(rt, buf, (uint32_t)l)); }
    case N_TOPRECISION: { if (IS_UNDEF(ARG(0))) return jstrv(js_num_to_str(rt, n)); int p = (int)f64_to_i64(js_tonum(rt, args[0])); if (p < 1 || p > 100) js_throw_err(rt, E_RANGE, "toPrecision() argumento debe estar entre 1 y 100"); int l = f64_to_precision(n, p, buf); return jstrv(js_str_new(rt, buf, (uint32_t)l)); }
    case N_TOEXP: { int d = IS_UNDEF(ARG(0)) ? -1 : (int)f64_to_i64(js_tonum(rt, args[0])); if (!IS_UNDEF(ARG(0)) && (d < 0 || d > F64_DIGITS_MAX)) js_throw_err(rt, E_RANGE, "toExponential() digits debe estar entre 0 y 100"); int l = f64_to_exponential(n, d, buf); return jstrv(js_str_new(rt, buf, (uint32_t)l)); }
    case N_TOLOCALE: return jstrv(locale_number(rt, n, ARG(0), ARG(1)));
    }
    return jundef();
}
enum { NS_ISINT, NS_ISSAFE, NS_ISFINITE, NS_ISNAN };
static jv number_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv v = ARG(0); if (v.t != JT_NUM) return jbool(0);
    switch (magic) {
    case NS_ISINT: return jbool(f64_is_int(v.u.n)); case NS_ISFINITE: return jbool(f64_is_finite(v.u.n)); case NS_ISNAN: return jbool(f64_is_nan(v.u.n));
    case NS_ISSAFE: return jbool(f64_is_int(v.u.n) && f64_le(f64_abs(v.u.n), cnum("9007199254740991")));
    }
    return jbool(0);
}
static jv number_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv v = jnum(argc ? js_tonum(rt, args[0]) : F64_ZERO);
    if (rt->new_target) { rt->new_target = 0; return jobjv(js_toobj(rt, v)); } return v;
}

/* ------------------------------ Math ------------------------------ */
enum { M_ABS, M_CEIL, M_FLOOR, M_ROUND, M_TRUNC, M_SIGN, M_SQRT, M_CBRT, M_POW, M_EXP, M_EXPM1, M_LOG, M_LOG2, M_LOG10, M_LOG1P, M_SIN, M_COS, M_TAN, M_ASIN, M_ACOS, M_ATAN, M_ATAN2, M_SINH, M_COSH, M_TANH,
       M_MIN, M_MAX, M_RANDOM, M_HYPOT, M_FROUND, M_CLZ32, M_IMUL, M_ASINH, M_ACOSH, M_ATANH };
static uint32_t xr(struct jrt *rt) { uint32_t *s = rt->rng; uint32_t t = s[0] ^ (s[0] << 11); s[0] = s[1]; s[1] = s[2]; s[2] = s[3]; s[3] = s[3] ^ (s[3] >> 19) ^ t ^ (t >> 8); return s[3]; }
static jv math_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    f64 x = argc ? js_tonum(rt, args[0]) : F64_NAN, y = argc > 1 ? js_tonum(rt, args[1]) : F64_NAN; f64 one = F64_ONE;
    switch (magic) {
    case M_ABS: return jnum(f64_abs(x)); case M_CEIL: return jnum(f64_ceil(x)); case M_FLOOR: return jnum(f64_floor(x)); case M_ROUND: return jnum(f64_round(x)); case M_TRUNC: return jnum(f64_trunc(x));
    case M_SIGN: return jnum(f64_is_nan(x) ? x : f64_eq(x, 0) ? x : (x >> 63) ? f64_neg(one) : one);
    case M_SQRT: return jnum(f64_sqrt(x));
    case M_CBRT: { if (f64_eq(x, 0) || !f64_is_finite(x)) return jnum(x); f64 r = f64_pow(f64_abs(x), cnum("0.3333333333333333")); r = f64_sub(r, f64_div(f64_sub(f64_mul(f64_mul(r, r), r), f64_abs(x)), f64_mul(cnum("3"), f64_mul(r, r)))); return jnum((x >> 63) ? f64_neg(r) : r); }
    case M_POW: return jnum(f64_pow(x, y)); case M_EXP: return jnum(f64_exp(x)); case M_EXPM1: return jnum(f64_sub(f64_exp(x), one));
    case M_LOG: return jnum(f64_log(x)); case M_LOG2: return jnum(f64_div(f64_log(x), f64_log(cnum("2")))); case M_LOG10: return jnum(f64_div(f64_log(x), f64_log(cnum("10")))); case M_LOG1P: return jnum(f64_log(f64_add(one, x)));
    case M_SIN: return jnum(f64_sin(x)); case M_COS: return jnum(f64_cos(x)); case M_TAN: return jnum(f64_tan(x)); case M_ASIN: return jnum(f64_asin(x)); case M_ACOS: return jnum(f64_acos(x)); case M_ATAN: return jnum(f64_atan(x)); case M_ATAN2: return jnum(f64_atan2(x, y));
    case M_SINH: { f64 e = f64_exp(x); return jnum(f64_div(f64_sub(e, f64_div(one, e)), cnum("2"))); }
    case M_COSH: { f64 e = f64_exp(x); return jnum(f64_div(f64_add(e, f64_div(one, e)), cnum("2"))); }
    case M_TANH: { if (f64_lt(cnum("20"), f64_abs(x))) return jnum((x >> 63) ? f64_neg(one) : one); f64 e = f64_exp(f64_mul(cnum("2"), x)); return jnum(f64_div(f64_sub(e, one), f64_add(e, one))); }
    case M_ASINH: return jnum(f64_log(f64_add(x, f64_sqrt(f64_add(f64_mul(x, x), one)))));
    case M_ACOSH: return jnum(f64_log(f64_add(x, f64_sqrt(f64_sub(f64_mul(x, x), one)))));
    case M_ATANH: return jnum(f64_mul(cnum("0.5"), f64_log(f64_div(f64_add(one, x), f64_sub(one, x)))));
    case M_MIN: case M_MAX: {
        f64 r = magic == M_MIN ? F64_INF : F64_NINF;
        for (int i = 0; i < argc; i++) { f64 v = js_tonum(rt, args[i]); if (f64_is_nan(v)) return jnum(F64_NAN); if (magic == M_MIN ? (f64_lt(v, r) || (f64_eq(v, r) && (v >> 63))) : (f64_lt(r, v) || (f64_eq(v, r) && !(v >> 63)))) r = v; }
        return jnum(r);
    }
    case M_RANDOM: { uint64_t hi = xr(rt) >> 5, lo = xr(rt) >> 6; return jnum(f64_div(f64_from_u64(hi * 67108864ULL + lo), cnum("9007199254740992"))); }
    case M_HYPOT: { f64 s = 0; for (int i = 0; i < argc; i++) { f64 v = js_tonum(rt, args[i]); s = f64_add(s, f64_mul(v, v)); } return jnum(f64_sqrt(s)); }
    case M_FROUND: return jnum(js_f32_round(x));   /* SOMA_186: redondeo real a 32 bits */
    case M_CLZ32: { uint32_t v = f64_to_u32(x); return jint(v ? __builtin_clz(v) : 32); }
    case M_IMUL: return jint((int32_t)((uint32_t)f64_to_i32(x) * (uint32_t)f64_to_i32(y)));
    }
    return jundef();
}

/* ------------------------------ JSON ------------------------------ */
struct jp { struct jrt *rt; const char *s, *e; int depth; };
static void jskip(struct jp *p) { while (p->s < p->e && wu_isspace(*p->s)) p->s++; }
static void jerr(struct jp *p) { js_throw_err(p->rt, E_SYNTAX, "JSON no valido en la posicion %d", (int)(p->e - p->s)); }
static jv jval(struct jp *p);
static jstr *jstring(struct jp *p)
{
    struct wb b = {0}; p->s++;
    while (p->s < p->e && *p->s != '"') {
        if (*p->s == '\\') {
            p->s++; char c = p->s < p->e ? *p->s++ : 0;
            switch (c) { case 'n': wb_addc(&b, '\n'); break; case 't': wb_addc(&b, '\t'); break; case 'r': wb_addc(&b, '\r'); break; case 'b': wb_addc(&b, '\b'); break; case 'f': wb_addc(&b, '\f'); break; case '/': case '\\': case '"': wb_addc(&b, c); break;
            case 'u': { uint32_t cp = 0; for (int i = 0; i < 4; i++) { if (p->s >= p->e || !wu_ishex(*p->s)) jerr(p); cp = cp * 16 + (uint32_t)wu_hexval(*p->s++); }
                if (cp >= 0xD800 && cp < 0xDC00 && p->s + 6 <= p->e && p->s[0] == '\\' && p->s[1] == 'u') { uint32_t lo = 0; for (int i = 0; i < 4; i++) lo = lo * 16 + (uint32_t)wu_hexval(p->s[2 + i]); if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p->s += 6; } }
                char t[4]; int n = wu_utf8_put(cp, t); wb_addn(&b, t, (uint32_t)n); break; }
            default: jerr(p); }
        } else if ((unsigned char)*p->s < 0x20) jerr(p); else wb_addc(&b, *p->s++);
    }
    if (p->s >= p->e) jerr(p);
    p->s++;
    jstr *r = js_str_new(p->rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
static jv jval(struct jp *p)
{
    js_check_stack(p->rt); jskip(p); if (p->s >= p->e) jerr(p);
    char c = *p->s;
    if (c == '{') {
        p->s++; jobj *o = js_obj_new(p->rt, p->rt->p_object); jskip(p);
        if (p->s < p->e && *p->s == '}') { p->s++; return jobjv(o); }
        for (;;) {
            jskip(p); if (p->s >= p->e || *p->s != '"') jerr(p); jstr *k = jstring(p); jskip(p); if (p->s >= p->e || *p->s != ':') jerr(p); p->s++;
            jv v = jval(p); if (js_intern(p->rt, k) == p->rt->s_proto) js_def(p->rt, o, p->rt->s_proto, v, JP_ENUM); else js_def(p->rt, o, js_intern(p->rt, k), v, JP_ENUM);
            jskip(p); if (p->s < p->e && *p->s == ',') { p->s++; continue; } if (p->s < p->e && *p->s == '}') { p->s++; break; } jerr(p);
        }
        return jobjv(o);
    }
    if (c == '[') {
        p->s++; jobj *a = js_arr_new(p->rt, 4); jskip(p); if (p->s < p->e && *p->s == ']') { p->s++; return jobjv(a); }
        for (;;) { js_arr_push(p->rt, a, jval(p)); jskip(p); if (p->s < p->e && *p->s == ',') { p->s++; continue; } if (p->s < p->e && *p->s == ']') { p->s++; break; } jerr(p); }
        return jobjv(a);
    }
    if (c == '"') return jstrv(jstring(p));
    if (p->e - p->s >= 4 && !memcmp(p->s, "true", 4)) { p->s += 4; return jbool(1); }
    if (p->e - p->s >= 5 && !memcmp(p->s, "false", 5)) { p->s += 5; return jbool(0); }
    if (p->e - p->s >= 4 && !memcmp(p->s, "null", 4)) { p->s += 4; return jnull(); }
    if (c == '-' || wu_isdigit(c)) { const char *end; char tmp[64]; uint32_t n = 0; while (p->s + n < p->e && n < 63 && (wu_isdigit(p->s[n]) || p->s[n] == '-' || p->s[n] == '+' || p->s[n] == '.' || p->s[n] == 'e' || p->s[n] == 'E')) { tmp[n] = p->s[n]; n++; } tmp[n] = 0; f64 v = f64_parse(tmp, &end); if (!end || end == tmp) jerr(p); p->s += end - tmp; return jnum(v); }
    jerr(p); return jundef();
}
jv js_json_parse(struct jrt *rt, const char *s, uint32_t n)
{
    struct jp p = { rt, s, s + n, 0 }; jv v = jval(&p); jskip(&p); if (p.s < p.e) jerr(&p); return v;
}
static void jquote(struct wb *b, jstr *s)
{
    wb_addc(b, '"');
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->d[i];
        switch (c) { case '"': wb_add(b, "\\\""); break; case '\\': wb_add(b, "\\\\"); break; case '\n': wb_add(b, "\\n"); break; case '\r': wb_add(b, "\\r"); break; case '\t': wb_add(b, "\\t"); break; case '\b': wb_add(b, "\\b"); break; case '\f': wb_add(b, "\\f"); break;
        default: if (c < 0x20) { char t[8]; static const char hx[] = "0123456789abcdef"; t[0] = '\\'; t[1] = 'u'; t[2] = '0'; t[3] = '0'; t[4] = hx[c >> 4]; t[5] = hx[c & 15]; wb_addn(b, t, 6); } else wb_addc(b, (char)c); }
    }
    wb_addc(b, '"');
}
struct js_ctx { struct jrt *rt; struct wb b; jv replacer; jstr *gap; jobj *stack[64]; int depth; jobj *allow; };
static int jser(struct js_ctx *c, jv holder, jstr *key, jv v, jstr *indent)
{
    struct jrt *rt = c->rt; js_check_stack(rt);
    if (v.t == JT_OBJ) { jv tj = js_get_c(rt, v, "toJSON"); if (js_is_callable(tj)) { jv a1[1] = { jstrv(key) }; v = js_call(rt, tj, v, a1, 1); } }
    if (js_is_callable(c->replacer)) { jv a2[2] = { jstrv(key), v }; v = js_call(rt, c->replacer, holder, a2, 2); }
    if (v.t == JT_OBJ && (v.u.o->cls == JC_NUMBER || v.u.o->cls == JC_STRING || v.u.o->cls == JC_BOOL)) v = v.u.o->el[0];
    switch (v.t) {
    case JT_NULL: wb_add(&c->b, "null"); return 1;
    case JT_BOOL: wb_add(&c->b, v.u.b ? "true" : "false"); return 1;
    case JT_NUM: if (f64_is_finite(v.u.n)) { jstr *s = js_num_to_str(rt, v.u.n); wb_addn(&c->b, s->d, s->len); } else wb_add(&c->b, "null"); return 1;
    case JT_STR: if (v.u.s->len > 1 && v.u.s->d[0] == '@' && v.u.s->d[1] == '@') return 0; jquote(&c->b, v.u.s); return 1;
    case JT_OBJ: break;
    default: return 0;
    }
    if (js_is_callable(v)) return 0;
    jobj *o = v.u.o;
    for (int i = 0; i < c->depth; i++) if (c->stack[i] == o) js_throw_err(rt, E_TYPE, "Conversion de estructura circular a JSON");
    if (c->depth >= 60) js_throw_err(rt, E_RANGE, "JSON demasiado profundo");
    c->stack[c->depth++] = o;
    jstr *ni = c->gap->len ? js_str_concat(rt, indent, c->gap) : indent;
    if (o->cls == JC_ARRAY) {
        wb_addc(&c->b, '[');
        for (uint32_t i = 0; i < o->alen; i++) {
            if (i) wb_addc(&c->b, ',');
            if (c->gap->len) { wb_addc(&c->b, '\n'); wb_addn(&c->b, ni->d, ni->len); }
            char t[16]; int l = wu_itoa(i, t); if (!jser(c, v, js_str_new(rt, t, (uint32_t)l), o->el[i], ni)) wb_add(&c->b, "null");
        }
        if (o->alen && c->gap->len) { wb_addc(&c->b, '\n'); wb_addn(&c->b, indent->d, indent->len); }
        wb_addc(&c->b, ']');
    } else {
        wb_addc(&c->b, '{'); int first = 1; jobj *ks = c->allow ? c->allow : js_own_keys(rt, v, 0);
        for (uint32_t i = 0; i < ks->alen; i++) {
            jstr *k = js_tostr(rt, ks->el[i]); jv pv = js_get(rt, v, js_intern(rt, k)); size_t mark = c->b.len;
            if (!first) wb_addc(&c->b, ',');
            if (c->gap->len) { wb_addc(&c->b, '\n'); wb_addn(&c->b, ni->d, ni->len); }
            jquote(&c->b, k); wb_addc(&c->b, ':'); if (c->gap->len) wb_addc(&c->b, ' ');
            if (!jser(c, v, k, pv, ni)) { c->b.len = (uint32_t)mark; c->b.p[mark] = 0; } else first = 0;
        }
        if (!first && c->gap->len) { wb_addc(&c->b, '\n'); wb_addn(&c->b, indent->d, indent->len); }
        wb_addc(&c->b, '}');
    }
    c->depth--; return 1;
}
jstr *js_json_stringify(struct jrt *rt, jv v, jv replacer, jv indent)
{
    struct js_ctx c; memset(&c, 0, sizeof c); c.rt = rt; c.replacer = replacer; c.gap = rt->s_empty;
    if (indent.t == JT_NUM) { int n = (int)f64_to_i64(indent.u.n); if (n > 10) n = 10; if (n > 0) { char sp[12]; memset(sp, ' ', (size_t)n); c.gap = js_str_new(rt, sp, (uint32_t)n); } }
    else if (indent.t == JT_STR) c.gap = js_str_new(rt, indent.u.s->d, indent.u.s->len > 10 ? 10 : indent.u.s->len);
    if (replacer.t == JT_OBJ && replacer.u.o->cls == JC_ARRAY) c.allow = replacer.u.o;
    jobj *h = js_obj_new(rt, rt->p_object); js_def(rt, h, rt->s_empty, v, JP_ENUM);
    int ok = jser(&c, jobjv(h), rt->s_empty, v, rt->s_empty);
    jstr *r = ok ? js_str_new(rt, c.b.p ? c.b.p : "", c.b.len) : 0; wb_free(&c.b); return r;
}
static jv revive(struct jrt *rt, jv reviver, jv holder, jstr *key)
{
    jv val = js_get(rt, holder, key);
    if (val.t == JT_OBJ) { jobj *ks = js_own_keys(rt, val, 0); for (uint32_t i = 0; i < ks->alen; i++) { jv nv = revive(rt, reviver, val, ks->el[i].u.s); if (IS_UNDEF(nv)) js_delete(rt, val.u.o, ks->el[i].u.s); else js_put(rt, val, ks->el[i].u.s, nv); } }
    jv a[2] = { jstrv(key), val }; return js_call(rt, reviver, holder, a, 2);
}
static jv json_parse_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jstr *s = js_tostr(rt, ARG(0)); jv v = js_json_parse(rt, s->d, s->len);
    if (js_is_callable(ARG(1))) { jobj *h = js_obj_new(rt, rt->p_object); js_def(rt, h, rt->s_empty, v, JP_ENUM); return revive(rt, args[1], jobjv(h), rt->s_empty); }
    return v;
}
static jv json_str_fn(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jstr *r = js_json_stringify(rt, ARG(0), ARG(1), ARG(2)); return r ? jstrv(r) : jundef();
}

/* ------------------------------ Date ------------------------------ */
static int64_t days_from_civil(int64_t y, int m, int d) { y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; int64_t yoe = y - era * 400; int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + doe - 719468; }
static void civil_from_days(int64_t z, int64_t *y, int *m, int *d)
{
    z += 719468; int64_t era = (z >= 0 ? z : z - 146096) / 146097; int64_t doe = z - era * 146097; int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100); int64_t mp = (5 * doy + 2) / 153; *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = yy + (*m <= 2);
}
struct dt { int64_t y; int mo, d, h, mi, s, ms, wd; };
static void dt_from_ms(int64_t t, struct dt *o)
{
    int64_t days = t / 86400000; int64_t rem = t % 86400000; if (rem < 0) { rem += 86400000; days--; }
    civil_from_days(days, &o->y, &o->mo, &o->d); o->h = (int)(rem / 3600000); o->mi = (int)(rem / 60000 % 60); o->s = (int)(rem / 1000 % 60); o->ms = (int)(rem % 1000);
    int64_t wd = (days + 4) % 7; if (wd < 0) wd += 7; o->wd = (int)wd;
}
static int64_t ms_from_parts(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t ms)
{
    y += mo / 12; mo %= 12; if (mo < 0) { mo += 12; y--; }
    return (days_from_civil(y, (int)mo + 1, 1) + (d - 1)) * 86400000 + h * 3600000 + mi * 60000 + s * 1000 + ms;
}
static const char *MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *DAY[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *MONF[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *MONES[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
static const char *DAYES[] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *DAYF[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static int parse_date(const char *s, int64_t *out)
{
    const char *p = s; while (wu_isspace(*p)) p++;
    int64_t y = 0, mo = 1, d = 1, h = 0, mi = 0, sec = 0, ms = 0; int tz = 0; int neg = 0;
    if (wu_isdigit(p[0]) && wu_isdigit(p[1]) && wu_isdigit(p[2]) && wu_isdigit(p[3]) && (p[4] == '-' || p[4] == 0 || p[4] == 'T' || p[4] == '/')) {   /* ISO */
        y = wu_atoi(p); p += 4;
        if (*p == '-' || *p == '/') { p++; mo = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == '-' || *p == '/') { p++; d = wu_atoi(p); while (wu_isdigit(*p)) p++; } }
        if (*p == 'T' || *p == ' ') { p++; h = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == ':') { p++; mi = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == ':') { p++; sec = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == '.') { p++; int dg = 0; ms = 0; while (wu_isdigit(*p)) { if (dg < 3) { ms = ms * 10 + (*p - '0'); dg++; } p++; } while (dg++ < 3) ms *= 10; } } } }
        if (*p == 'Z') p++; else if (*p == '+' || *p == '-') { neg = *p == '-'; p++; int th = wu_atoi(p); p += 2; if (*p == ':') p++; int tm = wu_atoi(p); tz = (th * 60 + tm) * (neg ? -1 : 1); }
        *out = ms_from_parts(y, mo - 1, d, h, mi, sec, ms) - (int64_t)tz * 60000; return 1;
    }
    /* "Mon, 02 Jan 2006 15:04:05 GMT" / "January 2, 2006 10:00:00" / "2 January 2006" / "12/31/2020" */
    int have_m = 0, have_y = 0, have_d = 0; int num[6], nn = 0; (void)have_d;
    while (*p) {
        if (wu_isalpha(*p)) { const char *w = p; while (wu_isalpha(*p)) p++; uint32_t l = (uint32_t)(p - w); for (int i = 0; i < 12; i++) if (l >= 3 && wu_strnieq(w, MON[i], 3)) { mo = i + 1; have_m = 1; } if ((l == 3 || l == 1) && (wu_strnieq(w, "gmt", 3) || wu_strnieq(w, "utc", 3) || wu_strnieq(w, "z", 1))) {} if (l == 2 && wu_strnieq(w, "pm", 2) && h < 12) h += 12; if (l == 2 && wu_strnieq(w, "am", 2) && h == 12) h = 0; }
        else if (wu_isdigit(*p)) {
            int v = wu_atoi(p); const char *st = p; while (wu_isdigit(*p)) p++;
            if (*p == ':') { h = v; p++; mi = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == ':') { p++; sec = wu_atoi(p); while (wu_isdigit(*p)) p++; } }
            else if (*p == '/' && nn == 0) { mo = v; have_m = 1; p++; d = wu_atoi(p); while (wu_isdigit(*p)) p++; if (*p == '/') { p++; y = wu_atoi(p); have_y = 1; while (wu_isdigit(*p)) p++; } }
            else if (p - st >= 3) { y = v; have_y = 1; } else if (nn < 6) num[nn++] = v;
        }
        else if ((*p == '+' || *p == '-') && wu_isdigit(p[1]) && have_y) { neg = *p == '-'; p++; int v = wu_atoi(p); tz = ((v / 100) * 60 + v % 100) * (neg ? -1 : 1); while (wu_isdigit(*p)) p++; }
        else p++;
    }
    if (nn > 0) d = num[0];
    if (!have_y && nn > 1) { y = num[1] < 100 ? 2000 + num[1] : num[1]; have_y = 1; }
    if (!have_y || !have_m) return 0;
    *out = ms_from_parts(y, mo - 1, d, h, mi, sec, 0) - (int64_t)tz * 60000; return 1;
}
enum { D_NOW, D_PARSE, D_UTC };
static f64 tv(jv t) { return t.u.o->u.date.t; }
static jv date_new(struct jrt *rt, f64 t, jobj *proto)
{
    jobj *o = js_obj_new(rt, proto ? proto : rt->p_date); o->cls = JC_DATE; o->u.date.t = t; return jobjv(o);
}
static f64 time_clip(f64 t) { if (!f64_is_finite(t) || f64_lt(cnum("8.64e15"), f64_abs(t))) return F64_NAN; return f64_trunc(t); }
static jv date_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *proto = rt->p_date; int is_new = rt->new_target != 0;
    if (rt->new_target) { jv p = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (p.t == JT_OBJ) proto = p.u.o; rt->new_target = 0; }
    int64_t now = (int64_t)web_epoch_ms();
    if (!is_new) { jv d = date_new(rt, f64_from_i64(now), 0); return js_call(rt, js_get_c(rt, d, "toString"), d, 0, 0); }
    if (argc == 0) return date_new(rt, f64_from_i64(now), proto);
    if (argc == 1) {
        jv a = args[0]; if (a.t == JT_OBJ && a.u.o->cls == JC_DATE) return date_new(rt, tv(a), proto);
        a = js_toprim(rt, a, 0);
        if (a.t == JT_STR) { int64_t t; if (parse_date(a.u.s->d, &t)) return date_new(rt, f64_from_i64(t), proto); return date_new(rt, F64_NAN, proto); }
        return date_new(rt, time_clip(js_tonum(rt, a)), proto);
    }
    f64 v[7] = { 0, 0, F64_ONE, 0, 0, 0, 0 }; for (int i = 0; i < 7 && i < argc; i++) v[i] = js_tonum(rt, args[i]);
    for (int i = 0; i < 7; i++) if (!f64_is_finite(v[i])) return date_new(rt, F64_NAN, proto);
    int64_t y = f64_to_i64(v[0]); if (y >= 0 && y <= 99) y += 1900;
    return date_new(rt, f64_from_i64(ms_from_parts(y, f64_to_i64(v[1]), f64_to_i64(v[2]), f64_to_i64(v[3]), f64_to_i64(v[4]), f64_to_i64(v[5]), f64_to_i64(v[6]))), proto);
}
static jv date_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic == D_NOW) return jint((int64_t)web_epoch_ms());
    if (magic == D_PARSE) { int64_t t; jstr *s = js_tostr(rt, ARG(0)); if (parse_date(s->d, &t)) return jint(t); return jnum(F64_NAN); }
    f64 v[7] = { 0, 0, F64_ONE, 0, 0, 0, 0 }; for (int i = 0; i < 7 && i < argc; i++) v[i] = js_tonum(rt, args[i]);
    int64_t y = f64_to_i64(v[0]); if (y >= 0 && y <= 99) y += 1900;
    return jint(ms_from_parts(y, f64_to_i64(v[1]), f64_to_i64(v[2]), f64_to_i64(v[3]), f64_to_i64(v[4]), f64_to_i64(v[5]), f64_to_i64(v[6])));
}
enum { DG_TIME, DG_YEAR, DG_MONTH, DG_DATE, DG_DAY, DG_HOURS, DG_MIN, DG_SEC, DG_MS, DS_TIME, DS_YEAR, DS_MONTH, DS_DATE, DS_HOURS, DS_MIN, DS_SEC, DS_MS, DT_ISO, DT_TOSTRING, DT_DATESTR, DT_TIMESTR, DT_LOCALE, DT_LOCALEDATE, DT_LOCALETIME, DT_TZO, DT_UTCSTR, DT_JSON };
static void p2(struct wb *b, int v) { wb_addc(b, (char)('0' + v / 10 % 10)); wb_addc(b, (char)('0' + v % 10)); }
static jv date_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (thisv.t != JT_OBJ || thisv.u.o->cls != JC_DATE) js_throw_err(rt, E_TYPE, "this no es un objeto Date");
    f64 t = tv(thisv); struct dt d; int valid = !f64_is_nan(t);
    if (valid) dt_from_ms(f64_to_i64(t), &d);
    if (magic <= DG_MS || magic == DT_TZO) {
        if (magic == DT_TZO) return jint(0);
        if (!valid) return jnum(F64_NAN);
        switch (magic) { case DG_TIME: return jnum(t); case DG_YEAR: return jint(d.y); case DG_MONTH: return jint(d.mo - 1); case DG_DATE: return jint(d.d); case DG_DAY: return jint(d.wd); case DG_HOURS: return jint(d.h); case DG_MIN: return jint(d.mi); case DG_SEC: return jint(d.s); default: return jint(d.ms); }
    }
    if (magic >= DS_TIME && magic <= DS_MS) {
        if (magic == DS_TIME) { thisv.u.o->u.date.t = time_clip(js_tonum(rt, ARG(0))); return jnum(thisv.u.o->u.date.t); }
        if (!valid) { if (magic != DS_YEAR) return jnum(F64_NAN); dt_from_ms(0, &d); }
        int64_t v[7] = { d.y, d.mo - 1, d.d, d.h, d.mi, d.s, d.ms };
        int base = magic == DS_YEAR ? 0 : magic == DS_MONTH ? 1 : magic == DS_DATE ? 2 : magic == DS_HOURS ? 3 : magic == DS_MIN ? 4 : magic == DS_SEC ? 5 : 6;
        for (int i = 0; i < argc && base + i < 7; i++) { f64 n = js_tonum(rt, args[i]); if (!f64_is_finite(n)) { thisv.u.o->u.date.t = F64_NAN; return jnum(F64_NAN); } v[base + i] = f64_to_i64(n); if (i >= (magic == DS_YEAR ? 3 : magic == DS_MONTH ? 2 : magic == DS_HOURS ? 4 : magic == DS_MIN ? 3 : magic == DS_SEC ? 2 : 1) - 1 + 1 && 0) break; }
        thisv.u.o->u.date.t = f64_from_i64(ms_from_parts(v[0], v[1], v[2], v[3], v[4], v[5], v[6])); return jnum(thisv.u.o->u.date.t);
    }
    if (!valid) { if (magic == DT_ISO) js_throw_err(rt, E_RANGE, "Fecha invalida"); if (magic == DT_JSON) return jnull(); return jstrv(js_intern_c(rt, "Invalid Date")); }
    struct wb b = {0};
    switch (magic) {
    case DT_ISO: case DT_JSON: { char y[24]; int l = wu_itoa(d.y, y); for (int i = l; i < 4; i++) wb_addc(&b, '0'); wb_addn(&b, y, (uint32_t)l); wb_addc(&b, '-'); p2(&b, d.mo); wb_addc(&b, '-'); p2(&b, d.d); wb_addc(&b, 'T'); p2(&b, d.h); wb_addc(&b, ':'); p2(&b, d.mi); wb_addc(&b, ':'); p2(&b, d.s); wb_addc(&b, '.'); wb_addc(&b, (char)('0' + d.ms / 100)); p2(&b, d.ms % 100); wb_addc(&b, 'Z'); break; }
    case DT_UTCSTR: wb_add(&b, DAY[d.wd]); wb_add(&b, ", "); p2(&b, d.d); wb_addc(&b, ' '); wb_add(&b, MON[d.mo - 1]); wb_addc(&b, ' '); wb_addint(&b, d.y); wb_addc(&b, ' '); p2(&b, d.h); wb_addc(&b, ':'); p2(&b, d.mi); wb_addc(&b, ':'); p2(&b, d.s); wb_add(&b, " GMT"); break;
    case DT_DATESTR: wb_add(&b, DAY[d.wd]); wb_addc(&b, ' '); wb_add(&b, MON[d.mo - 1]); wb_addc(&b, ' '); p2(&b, d.d); wb_addc(&b, ' '); wb_addint(&b, d.y); break;
    case DT_TIMESTR: p2(&b, d.h); wb_addc(&b, ':'); p2(&b, d.mi); wb_addc(&b, ':'); p2(&b, d.s); wb_add(&b, " GMT+0000 (UTC)"); break;
    case DT_TOSTRING: wb_add(&b, DAY[d.wd]); wb_addc(&b, ' '); wb_add(&b, MON[d.mo - 1]); wb_addc(&b, ' '); p2(&b, d.d); wb_addc(&b, ' '); wb_addint(&b, d.y); wb_addc(&b, ' '); p2(&b, d.h); wb_addc(&b, ':'); p2(&b, d.mi); wb_addc(&b, ':'); p2(&b, d.s); wb_add(&b, " GMT+0000 (UTC)"); break;
    case DT_LOCALE: case DT_LOCALEDATE: case DT_LOCALETIME: {
        const char *loc = ARG(0).t == JT_STR ? args[0].u.s->d : "es-ES"; int es = !wu_starts(loc, "en"); int us = wu_starts(loc, "en-US") || wu_streq(loc, "en");
        jv opts = ARG(1); int longmonth = 0, wkd = 0, onlydate = magic == DT_LOCALEDATE, h12 = us;
        if (opts.t == JT_OBJ) { jv m = js_get_c(rt, opts, "month"); if (m.t == JT_STR && (wu_streq(m.u.s->d, "long") || wu_streq(m.u.s->d, "short"))) longmonth = wu_streq(m.u.s->d, "long") ? 2 : 1; if (js_get_c(rt, opts, "weekday").t == JT_STR) wkd = 1; jv hh = js_get_c(rt, opts, "hour12"); if (hh.t == JT_BOOL) h12 = hh.u.b; }
        if (magic != DT_LOCALETIME) {
            if (wkd) { wb_add(&b, es ? DAYES[d.wd] : DAYF[d.wd]); wb_add(&b, ", "); }
            if (longmonth) { if (es) { wb_addint(&b, d.d); wb_add(&b, " de "); wb_add(&b, MONES[d.mo - 1]); wb_add(&b, " de "); wb_addint(&b, d.y); } else { wb_add(&b, longmonth == 2 ? MONF[d.mo - 1] : MON[d.mo - 1]); wb_addc(&b, ' '); wb_addint(&b, d.d); wb_add(&b, ", "); wb_addint(&b, d.y); } }
            else if (us) { wb_addint(&b, d.mo); wb_addc(&b, '/'); wb_addint(&b, d.d); wb_addc(&b, '/'); wb_addint(&b, d.y); }
            else if (wu_starts(loc, "en-GB") || wu_starts(loc, "fr") || wu_starts(loc, "it") || wu_starts(loc, "pt")) { p2(&b, d.d); wb_addc(&b, '/'); p2(&b, d.mo); wb_addc(&b, '/'); wb_addint(&b, d.y); }
            else if (wu_starts(loc, "de")) { wb_addint(&b, d.d); wb_addc(&b, '.'); wb_addint(&b, d.mo); wb_addc(&b, '.'); wb_addint(&b, d.y); }
            else { wb_addint(&b, d.d); wb_addc(&b, '/'); wb_addint(&b, d.mo); wb_addc(&b, '/'); wb_addint(&b, d.y); }
        }
        if (!onlydate) {
            if (magic == DT_LOCALE) wb_add(&b, ", ");
            if (h12) { int hh = d.h % 12; if (!hh) hh = 12; wb_addint(&b, hh); } else p2(&b, d.h);
            wb_addc(&b, ':'); p2(&b, d.mi); wb_addc(&b, ':'); p2(&b, d.s); if (h12) wb_add(&b, d.h < 12 ? " AM" : " PM");
        }
        break; }
    }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
}
static jv date_toprim(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jv h = ARG(0); if (h.t == JT_STR && wu_streq(h.u.s->d, "number")) return thisv.t == JT_OBJ ? jnum(tv(thisv)) : jnum(F64_NAN);
    return date_proto(rt, thisv, args, 0, DT_TOSTRING);
}
/* Intl */
static jv intl_nf_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    jobj *o = js_obj_new(rt, rt->p_object); rt->new_target = 0; js_def(rt, o, js_intern_c(rt, "_loc"), ARG(0), 0); js_def(rt, o, js_intern_c(rt, "_opt"), ARG(1), 0);
    js_def(rt, o, js_intern_c(rt, "format"), jobjv(js_native(rt, "format", magic ? date_proto : number_proto, 1, magic ? DT_LOCALE : N_TOLOCALE)), 0); /* se sustituye abajo */
    return jobjv(o);
}
static jv intl_format(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct jobj *self = rt->cur_native; (void)self;
    return jundef();
}
void js_lib_numbers_init(struct jrt *rt)
{
    jobj *nc = mkctor3(rt, "Number", number_ctor, 1, 0, rt->p_number);
    static const struct { const char *n; const char *v; } NC[] = { { "EPSILON", "2.220446049250313e-16" }, { "MAX_SAFE_INTEGER", "9007199254740991" }, { "MIN_SAFE_INTEGER", "-9007199254740991" }, { "MAX_VALUE", "1.7976931348623157e308" }, { "MIN_VALUE", "5e-324" }, { 0, 0 } };
    for (int i = 0; NC[i].n; i++) js_def(rt, nc, js_intern_c(rt, NC[i].n), jnum(cnum(NC[i].v)), JP_RO);
    js_def(rt, nc, js_intern_c(rt, "POSITIVE_INFINITY"), jnum(F64_INF), JP_RO); js_def(rt, nc, js_intern_c(rt, "NEGATIVE_INFINITY"), jnum(F64_NINF), JP_RO); js_def(rt, nc, js_intern_c(rt, "NaN"), jnum(F64_NAN), JP_RO);
    js_method(rt, nc, "isInteger", number_static, 1, NS_ISINT); js_method(rt, nc, "isSafeInteger", number_static, 1, NS_ISSAFE); js_method(rt, nc, "isFinite", number_static, 1, NS_ISFINITE); js_method(rt, nc, "isNaN", number_static, 1, NS_ISNAN);
    js_def(rt, nc, js_intern_c(rt, "parseFloat"), js_get_c(rt, jobjv(rt->global), "parseFloat"), 0); js_def(rt, nc, js_intern_c(rt, "parseInt"), js_get_c(rt, jobjv(rt->global), "parseInt"), 0);
    js_method(rt, rt->p_number, "toString", number_proto, 1, N_TOSTRING); js_method(rt, rt->p_number, "toFixed", number_proto, 1, N_TOFIXED); js_method(rt, rt->p_number, "toPrecision", number_proto, 1, N_TOPRECISION);
    js_method(rt, rt->p_number, "toExponential", number_proto, 1, N_TOEXP); js_method(rt, rt->p_number, "valueOf", number_proto, 0, N_VALUEOF); js_method(rt, rt->p_number, "toLocaleString", number_proto, 0, N_TOLOCALE);
    jobj *m = js_obj_new(rt, rt->p_object); js_def(rt, rt->global, js_intern_c(rt, "Math"), jobjv(m), 0); js_def(rt, m, js_intern_c(rt, "@@toStringTag"), jstrv(js_intern_c(rt, "Math")), 0);
    static const struct { const char *n; int m; } MF[] = { { "abs", M_ABS }, { "ceil", M_CEIL }, { "floor", M_FLOOR }, { "round", M_ROUND }, { "trunc", M_TRUNC }, { "sign", M_SIGN }, { "sqrt", M_SQRT }, { "cbrt", M_CBRT }, { "pow", M_POW }, { "exp", M_EXP }, { "expm1", M_EXPM1 },
        { "log", M_LOG }, { "log2", M_LOG2 }, { "log10", M_LOG10 }, { "log1p", M_LOG1P }, { "sin", M_SIN }, { "cos", M_COS }, { "tan", M_TAN }, { "asin", M_ASIN }, { "acos", M_ACOS }, { "atan", M_ATAN }, { "atan2", M_ATAN2 }, { "sinh", M_SINH }, { "cosh", M_COSH },
        { "tanh", M_TANH }, { "asinh", M_ASINH }, { "acosh", M_ACOSH }, { "atanh", M_ATANH }, { "min", M_MIN }, { "max", M_MAX }, { "random", M_RANDOM }, { "hypot", M_HYPOT }, { "fround", M_FROUND }, { "clz32", M_CLZ32 }, { "imul", M_IMUL }, { 0, 0 } };
    for (int i = 0; MF[i].n; i++) js_method(rt, m, MF[i].n, math_fn, 1, MF[i].m);
    static const struct { const char *n; const char *v; } MC[] = { { "PI", "3.141592653589793" }, { "E", "2.718281828459045" }, { "LN2", "0.6931471805599453" }, { "LN10", "2.302585092994046" }, { "LOG2E", "1.4426950408889634" }, { "LOG10E", "0.4342944819032518" }, { "SQRT2", "1.4142135623730951" }, { "SQRT1_2", "0.7071067811865476" }, { 0, 0 } };
    for (int i = 0; MC[i].n; i++) js_def(rt, m, js_intern_c(rt, MC[i].n), jnum(cnum(MC[i].v)), JP_RO);
    uint64_t seed = web_now_ms() * 2654435761u + 12345; rt->rng[0] = (uint32_t)seed | 1; rt->rng[1] = (uint32_t)(seed >> 16) ^ 0x9E3779B9u; rt->rng[2] = 0x85EBCA6Bu; rt->rng[3] = 0xC2B2AE35u;
    { uint32_t e[4]; rng_bytes(e, sizeof e); for (int i = 0; i < 4; i++) rt->rng[i] ^= e[i]; rt->rng[0] |= 1; }   /* SOMA_192: Math.random ya no se siembra solo con la hora */
    for (int i = 0; i < 16; i++) xr(rt);
    jobj *j = js_obj_new(rt, rt->p_object); js_def(rt, rt->global, js_intern_c(rt, "JSON"), jobjv(j), 0); js_method(rt, j, "parse", json_parse_fn, 2, 0); js_method(rt, j, "stringify", json_str_fn, 3, 0);
    jobj *dc = mkctor3(rt, "Date", date_ctor, 7, 0, rt->p_date); js_method(rt, dc, "now", date_static, 0, D_NOW); js_method(rt, dc, "parse", date_static, 1, D_PARSE); js_method(rt, dc, "UTC", date_static, 7, D_UTC);
    static const struct { const char *n; int m; } DM[] = { { "getTime", DG_TIME }, { "valueOf", DG_TIME }, { "getFullYear", DG_YEAR }, { "getMonth", DG_MONTH }, { "getDate", DG_DATE }, { "getDay", DG_DAY }, { "getHours", DG_HOURS }, { "getMinutes", DG_MIN }, { "getSeconds", DG_SEC }, { "getMilliseconds", DG_MS },
        { "getUTCFullYear", DG_YEAR }, { "getUTCMonth", DG_MONTH }, { "getUTCDate", DG_DATE }, { "getUTCDay", DG_DAY }, { "getUTCHours", DG_HOURS }, { "getUTCMinutes", DG_MIN }, { "getUTCSeconds", DG_SEC }, { "getUTCMilliseconds", DG_MS },
        { "setTime", DS_TIME }, { "setFullYear", DS_YEAR }, { "setMonth", DS_MONTH }, { "setDate", DS_DATE }, { "setHours", DS_HOURS }, { "setMinutes", DS_MIN }, { "setSeconds", DS_SEC }, { "setMilliseconds", DS_MS },
        { "setUTCFullYear", DS_YEAR }, { "setUTCMonth", DS_MONTH }, { "setUTCDate", DS_DATE }, { "setUTCHours", DS_HOURS }, { "setUTCMinutes", DS_MIN }, { "setUTCSeconds", DS_SEC }, { "setUTCMilliseconds", DS_MS },
        { "toISOString", DT_ISO }, { "toJSON", DT_JSON }, { "toString", DT_TOSTRING }, { "toDateString", DT_DATESTR }, { "toTimeString", DT_TIMESTR }, { "toLocaleString", DT_LOCALE }, { "toLocaleDateString", DT_LOCALEDATE }, { "toLocaleTimeString", DT_LOCALETIME },
        { "getTimezoneOffset", DT_TZO }, { "toUTCString", DT_UTCSTR }, { "toGMTString", DT_UTCSTR }, { 0, 0 } };
    for (int i = 0; DM[i].n; i++) js_method(rt, rt->p_date, DM[i].n, date_proto, 1, DM[i].m);
    js_def(rt, rt->p_date, js_intern_c(rt, "@@toPrimitive"), jobjv(js_native(rt, "[Symbol.toPrimitive]", date_toprim, 1, 0)), 0);
    (void)intl_nf_ctor; (void)intl_format; (void)MONES; (void)DAYES;
}
