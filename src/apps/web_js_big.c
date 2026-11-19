/* SOMA 188B: BigInt (ES2020) para el interprete de JavaScript del navegador.
 *
 * El script principal de Google (y cualquier codigo con Protocol Buffers moderno) hace BigInt(Number.MAX_SAFE_INTEGER) al
 * cargarse: sin BigInt, un ReferenceError detiene el fichero entero. Aqui un BigInt es un entero con signo de 128 bits (cubre
 * de sobra los int64/uint64 para los que la web lo usa: identificadores, marcas de tiempo, campos de protobuf); las
 * operaciones que desbordan dan vuelta en 128 bits. Es un objeto de clase JC_BIGINT con typeof "bigint": se compara por
 * valor, 0n es falso, y mezclarlo con un numero en una operacion aritmetica lanza TypeError como en el estandar.
 * Se desactiva con soma.jsbigint=off. */
#include "web_js_i.h"

typedef __int128 i128;
typedef unsigned __int128 u128;
#define ARG(i) ((i) < argc ? args[i] : jundef())

int js_is_big(jv v) { return v.t == JT_OBJ && v.u.o->cls == JC_BIGINT; }
static i128 bval(jv v) { return (i128)(((u128)v.u.o->u.big.hi << 64) | (u128)v.u.o->u.big.lo); }
static jv bnew(struct jrt *rt, i128 x)
{
    jobj *o = js_obj_new(rt, rt->p_bigint ? rt->p_bigint : rt->p_object); o->cls = JC_BIGINT;
    o->u.big.lo = (uint64_t)(u128)x; o->u.big.hi = (uint64_t)((u128)x >> 64); return jobjv(o);
}
jv js_big_from_i64(struct jrt *rt, int64_t v) { return bnew(rt, (i128)v); }
jv js_big_from_u64(struct jrt *rt, uint64_t v) { return bnew(rt, (i128)(u128)v); }
uint64_t js_big_low64(jv v) { return v.u.o->u.big.lo; }
int js_big_is_zero(jv v) { return !v.u.o->u.big.lo && !v.u.o->u.big.hi; }
int js_big_same(jv a, jv b) { return a.u.o->u.big.lo == b.u.o->u.big.lo && a.u.o->u.big.hi == b.u.o->u.big.hi; }

/* desplazamientos de 128 bits con mitades de 64 (en ARM el compilador llamaria a __ashlti3/__ashrti3 de libgcc, que el nucleo no enlaza) */
static u128 shl128(u128 x, int s)
{
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    if (s <= 0) return x;
    if (s >= 128) return 0;
    if (s >= 64) { hi = lo << (s - 64); lo = 0; } else { hi = (hi << s) | (lo >> (64 - s)); lo <<= s; }
    return ((u128)hi << 64) | lo;
}
static u128 shr128(u128 x, int s, int arith)
{
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x; uint64_t fill = arith && (hi >> 63) ? ~(uint64_t)0 : 0;
    if (s <= 0) return x;
    if (s >= 128) return ((u128)fill << 64) | fill;
    if (s >= 64) { lo = s == 64 ? hi : ((hi >> (s - 64)) | (fill << (128 - s))); hi = fill; }
    else { lo = (lo >> s) | (hi << (64 - s)); hi = (hi >> s) | (fill << (64 - s)); }
    return ((u128)hi << 64) | lo;
}
/* division sin signo por desplazamiento y resta (el nucleo no enlaza __udivti3 de libgcc) */
static void udivmod(u128 n, u128 d, u128 *q, u128 *r)
{
    u128 quo = 0, rem = 0;
    if (!(uint64_t)(n >> 64) && !(uint64_t)(d >> 64)) { *q = (uint64_t)n / (uint64_t)d; *r = (uint64_t)n % (uint64_t)d; return; }
    for (int i = 127; i >= 0; i--) {
        rem = shl128(rem, 1) | (shr128(n, i, 0) & 1);
        if (rem >= d) { rem -= d; quo |= shl128((u128)1, i); }
    }
    *q = quo; *r = rem;
}
static i128 sdiv(i128 a, i128 b, i128 *rem)
{
    int na = a < 0, nb = b < 0; u128 ua = na ? (u128)0 - (u128)a : (u128)a, ub = nb ? (u128)0 - (u128)b : (u128)b, q, r;
    udivmod(ua, ub, &q, &r);
    if (rem) *rem = na ? -(i128)r : (i128)r;
    return na != nb ? -(i128)q : (i128)q;
}
jstr *js_big_tostr(struct jrt *rt, jv v, int radix)
{
    char buf[140]; int k = (int)sizeof buf; buf[--k] = 0;
    i128 x = bval(v); int neg = x < 0; u128 u = neg ? (u128)0 - (u128)x : (u128)x;
    if (radix < 2 || radix > 36) radix = 10;
    if (!u) buf[--k] = '0';
    while (u) { u128 q, r; udivmod(u, (u128)(unsigned)radix, &q, &r); int d = (int)(uint64_t)r; buf[--k] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u = q; }
    if (neg) buf[--k] = '-';
    return js_str_new(rt, buf + k, (uint32_t)((int)sizeof buf - 1 - k));
}
f64 js_big_tonum(jv v)
{
    i128 x = bval(v); int neg = x < 0; u128 u = neg ? (u128)0 - (u128)x : (u128)x;
    uint64_t hi = (uint64_t)(u >> 64), lo = (uint64_t)u;
    const f64 two32 = 0x41F0000000000000ULL, two64 = 0x43F0000000000000ULL;
    f64 l = f64_add(f64_mul(f64_from_i64((int64_t)(lo >> 32)), two32), f64_from_i64((int64_t)(lo & 0xFFFFFFFFu)));
    f64 h = f64_add(f64_mul(f64_from_i64((int64_t)(hi >> 32)), two32), f64_from_i64((int64_t)(hi & 0xFFFFFFFFu)));
    f64 r = hi ? f64_add(f64_mul(h, two64), l) : l;
    return neg ? f64_neg(r) : r;
}
/* texto -> entero: espacios, signo, 0x/0o/0b; "" vale 0. Devuelve 0 si no es un entero valido */
static int big_parse(const char *s, uint32_t n, i128 *out)
{
    uint32_t i = 0; while (i < n && wu_isspace(s[i])) i++;
    while (n > i && wu_isspace(s[n - 1])) n--;
    int neg = 0, base = 10; u128 v = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; if (i == n) return 0; }
    else if (i + 1 < n && s[i] == '0' && ((s[i + 1] | 32) == 'x' || (s[i + 1] | 32) == 'o' || (s[i + 1] | 32) == 'b')) { base = (s[i + 1] | 32) == 'x' ? 16 : (s[i + 1] | 32) == 'o' ? 8 : 2; i += 2; if (i == n) return 0; }
    for (; i < n; i++) {
        int c = (unsigned char)s[i], d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'z' ? (c | 32) - 'a' + 10 : 99;
        if (c == '_') continue;
        if (d >= base) return 0;
        v = v * (u128)(unsigned)base + (u128)(unsigned)d;
    }
    *out = neg ? -(i128)v : (i128)v; return 1;
}
jv js_big_literal(struct jrt *rt, jstr *digits)
{
    i128 x = 0; if (!big_parse(digits->d, digits->len, &x)) js_throw_err(rt, E_SYNTAX, "Literal BigInt no valido");
    return bnew(rt, x);
}
static jv big_from(struct jrt *rt, jv v)
{
    if (js_is_big(v)) return v;
    if (v.t == JT_OBJ) return big_from(rt, js_toprim(rt, v, 0));
    if (v.t == JT_BOOL) return bnew(rt, v.u.b ? 1 : 0);
    if (v.t == JT_NUM) {
        f64 n = v.u.n;
        if (!f64_is_int(n)) js_throw_err(rt, E_RANGE, "El numero no es un entero y no se puede convertir a BigInt");
        if (f64_lt(f64_abs(n), 0x43E0000000000000ULL /* 2^63 */)) return bnew(rt, (i128)f64_to_i64(n));
        int exp = (int)((n >> 52) & 0x7FF) - 1075; u128 m = (u128)((n & 0xFFFFFFFFFFFFFULL) | 0x10000000000000ULL);
        if (exp > 74) js_throw_err(rt, E_RANGE, "El numero es demasiado grande para BigInt");
        m = shl128(m, exp); return bnew(rt, (n >> 63) ? -(i128)m : (i128)m);
    }
    if (v.t == JT_STR) { i128 x = 0; if (!big_parse(v.u.s->d, v.u.s->len, &x)) js_throw_err(rt, E_SYNTAX, "No se puede convertir '%s' a BigInt", v.u.s->d); return bnew(rt, x); }
    js_throw_err(rt, E_TYPE, "No se puede convertir el valor a BigInt"); return jundef();
}
jv js_big_neg(struct jrt *rt, jv v) { return bnew(rt, -bval(v)); }
jv js_big_not(struct jrt *rt, jv v) { return bnew(rt, ~bval(v)); }
jv js_big_inc(struct jrt *rt, jv v, int d) { return bnew(rt, bval(v) + d); }

/* operadores binarios aritmeticos y de bits: los dos operandos han de ser BigInt */
jv js_big_binop(struct jrt *rt, int op, jv a, jv b)
{
    if (!js_is_big(a) || !js_is_big(b)) js_throw_err(rt, E_TYPE, "No se pueden mezclar BigInt y otros tipos: usa conversiones explicitas");
    i128 x = bval(a), y = bval(b);
    switch (op) {
    case B_ADD: return bnew(rt, (i128)((u128)x + (u128)y));
    case B_SUB: return bnew(rt, (i128)((u128)x - (u128)y));
    case B_MUL: return bnew(rt, (i128)((u128)x * (u128)y));
    case B_DIV: case B_MOD: { if (!y) js_throw_err(rt, E_RANGE, "Division por cero"); i128 r; i128 q = sdiv(x, y, &r); return bnew(rt, op == B_DIV ? q : r); }
    case B_EXP: {
        if (y < 0) js_throw_err(rt, E_RANGE, "El exponente debe ser no negativo");
        u128 r = 1, base = (u128)x; uint64_t e = (uint64_t)y; if (y > 100000) e = 100000;
        while (e) { if (e & 1) r *= base; base *= base; e >>= 1; }
        return bnew(rt, (i128)r);
    }
    case B_AND: return bnew(rt, x & y); case B_OR: return bnew(rt, x | y); case B_XOR: return bnew(rt, x ^ y);
    case B_SHL: case B_SHR: {
        int left = op == B_SHL; if (y < 0) { left = !left; y = -y; }
        if (y >= 128) return bnew(rt, left ? 0 : (x < 0 ? -1 : 0));
        int s = (int)y;
        return bnew(rt, (i128)(left ? shl128((u128)x, s) : shr128((u128)x, s, 1)));
    }
    case B_USHR: js_throw_err(rt, E_TYPE, "BigInt no admite >>>: usa >>");
    }
    js_throw_err(rt, E_TYPE, "Operacion no valida con BigInt"); return jundef();
}
/* otro operando -> entero exacto si es posible (1), si no numero (2), o no comparable (0) */
static int other_val(struct jrt *rt, jv v, i128 *x, f64 *f)
{
    if (js_is_big(v)) { *x = bval(v); return 1; }
    if (v.t == JT_OBJ) v = js_toprim(rt, v, 0);
    if (v.t == JT_STR) return big_parse(v.u.s->d, v.u.s->len, x) ? 1 : 0;
    f64 n = js_tonum(rt, v); if (f64_is_nan(n)) return 0;
    if (f64_is_int(n) && f64_lt(f64_abs(n), 0x43E0000000000000ULL)) { *x = (i128)f64_to_i64(n); return 1; }
    *f = n; return 2;
}
/* a < b cuando al menos uno es BigInt */
int js_big_lt(struct jrt *rt, jv a, jv b, int *undef)
{
    i128 x = 0, y = 0; f64 fx = 0, fy = 0; *undef = 0;
    int ka = other_val(rt, a, &x, &fx), kb = other_val(rt, b, &y, &fy);
    if (!ka || !kb) { *undef = 1; return 0; }
    if (ka == 1 && kb == 1) return x < y;
    if (ka == 1) fx = js_big_tonum(a);
    if (kb == 1) fy = js_big_tonum(b);
    return f64_lt(fx, fy);
}
/* a == b cuando al menos uno es BigInt (comparacion debil) */
int js_big_eq(struct jrt *rt, jv a, jv b)
{
    if (IS_NULLISH(a) || IS_NULLISH(b)) return 0;
    i128 x = 0, y = 0; f64 fx = 0, fy = 0;
    int ka = other_val(rt, a, &x, &fx), kb = other_val(rt, b, &y, &fy);
    return ka == 1 && kb == 1 && x == y;
}

enum { BG_TOSTRING = 1, BG_VALUEOF, BG_ASUINTN, BG_ASINTN };
static jv big_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (rt->new_target) { rt->new_target = 0; js_throw_err(rt, E_TYPE, "BigInt no es un constructor"); }
    return big_from(rt, ARG(0));
}
static jv big_method(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    switch (magic) {
    case BG_TOSTRING: { if (!js_is_big(thisv)) js_throw_err(rt, E_TYPE, "No es un BigInt"); int radix = IS_UNDEF(ARG(0)) ? 10 : (int)f64_to_i64(js_tonum(rt, args[0])); if (radix < 2 || radix > 36) js_throw_err(rt, E_RANGE, "La base debe estar entre 2 y 36"); return jstrv(js_big_tostr(rt, thisv, radix)); }
    case BG_VALUEOF: if (!js_is_big(thisv)) js_throw_err(rt, E_TYPE, "No es un BigInt"); return thisv;
    case BG_ASUINTN: case BG_ASINTN: {
        int64_t bits = f64_to_i64(js_tonum(rt, ARG(0))); if (bits < 0) js_throw_err(rt, E_RANGE, "Numero de bits no valido");
        u128 x = (u128)bval(big_from(rt, ARG(1)));
        if (bits == 0) return bnew(rt, 0);
        if (bits >= 128) return bnew(rt, (i128)x);
        u128 mask = shl128((u128)1, (int)bits) - 1; x &= mask;
        if (magic == BG_ASINTN && (shr128(x, (int)bits - 1, 0) & 1)) x |= ~mask;
        return bnew(rt, (i128)x);
    }
    }
    return jundef();
}
static int big_on(void)
{
#if defined(WEB_HOST_TEST)
    extern char *getenv(const char *); const char *o = getenv("SOMA_OFF"); return !(o && strstr(o, "jsbigint"));
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsbigint") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}
void js_big_init(struct jrt *rt)
{
    rt->p_bigint = js_obj_new(rt, rt->p_object);
    js_method(rt, rt->p_bigint, "toString", big_method, 0, BG_TOSTRING); js_method(rt, rt->p_bigint, "toLocaleString", big_method, 0, BG_TOSTRING);
    js_method(rt, rt->p_bigint, "valueOf", big_method, 0, BG_VALUEOF);
    if (!big_on()) return;
    jobj *c = js_native(rt, "BigInt", big_ctor, 1, 0);
    js_def(rt, c, rt->s_prototype, jobjv(rt->p_bigint), JP_NOCONF | JP_RO);
    js_def(rt, rt->p_bigint, js_intern_c(rt, "constructor"), jobjv(c), 0);
    js_def(rt, c, js_intern_c(rt, "asUintN"), jobjv(js_native(rt, "asUintN", big_method, 2, BG_ASUINTN)), 0);
    js_def(rt, c, js_intern_c(rt, "asIntN"), jobjv(js_native(rt, "asIntN", big_method, 2, BG_ASINTN)), 0);
    js_def(rt, rt->global, js_intern_c(rt, "BigInt"), jobjv(c), 0);
}
