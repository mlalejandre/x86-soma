/* SOMA_PARCHE_NAVEGADOR: doble precision por software. */
#include "web_f64.h"
#include "web_util.h"

#define SIGN 0x8000000000000000ULL
#define FRAC 0x000FFFFFFFFFFFFFULL
typedef unsigned __int128 u128;

static int fexp(f64 a) { return (int)((a >> 52) & 0x7FF); }
int f64_is_nan(f64 a) { return fexp(a) == 0x7FF && (a & FRAC); }
int f64_is_inf(f64 a) { return fexp(a) == 0x7FF && !(a & FRAC); }
int f64_is_finite(f64 a) { return fexp(a) != 0x7FF; }
static int is_zero(f64 a) { return (a & ~SIGN) == 0; }

/* desempaqueta a (e, m) con m normalizado en el bit 62; devuelve 0 si es cero */
static int unpack(f64 a, int *e, uint64_t *m)
{
    int be = fexp(a); uint64_t f = a & FRAC;
    if (be == 0) {
        if (!f) return 0;
        int sh = 0; while (!(f & (1ULL << 52))) { f <<= 1; sh++; }
        *e = -1022 - sh; *m = f << 10; return 1;
    }
    *e = be - 1023; *m = (f | (1ULL << 52)) << 10; return 1;
}
static f64 rp(int sign, int e, uint64_t m, int sticky)
{
    uint64_t sg = sign ? SIGN : 0;
    if (!m) return sg;
    int shift = 10;
    if (e < -1022) shift += -1022 - e;
    if (e > 1023) return sg | F64_INF;
    uint64_t q, rem, half; int up = 0;
    if (shift >= 64) {
        if (shift == 64 && (m > (1ULL << 63) || (m == (1ULL << 63) && sticky))) return sg | 1;
        return sg;
    }
    q = m >> shift; rem = m & ((1ULL << shift) - 1); half = 1ULL << (shift - 1);
    if (rem > half || (rem == half && (sticky || (q & 1)))) up = 1;
    q += (uint64_t)up;
    uint64_t bits;
    if (e < -1022) bits = q;
    else { bits = ((uint64_t)(e + 1023) << 52) + (q - (1ULL << 52)); }
    if (bits >= F64_INF) return sg | F64_INF;
    return sg | bits;
}
f64 f64_from_u64(uint64_t v)
{
    if (!v) return 0;
    int lz = __builtin_clzll(v); int e = 63 - lz; uint64_t m = v << lz;      /* bit 63 */
    int st = (m & 1) != 0; m >>= 1;                                           /* bit 62 */
    return rp(0, e, m, st);
}
f64 f64_from_i64(int64_t v) { if (v < 0) return f64_from_u64((uint64_t)0 - (uint64_t)v) | SIGN; return f64_from_u64((uint64_t)v); }

static void split(f64 a, int *e, uint64_t *m53)      /* valor = m53 * 2^(e-52) */
{
    int be = fexp(a); uint64_t f = a & FRAC;
    if (be == 0) { *e = -1022; *m53 = f; } else { *e = be - 1023; *m53 = f | (1ULL << 52); }
}
int64_t f64_to_i64(f64 a)
{
    if (f64_is_nan(a)) return 0;
    int e; uint64_t m; split(a, &e, &m); int neg = (a & SIGN) != 0;
    if (e >= 63) return neg ? INT64_MIN : INT64_MAX;
    if (e < 0) return 0;
    uint64_t v = e >= 52 ? m << (e - 52) : m >> (52 - e);
    return neg ? -(int64_t)v : (int64_t)v;
}
uint32_t f64_to_u32(f64 a)
{
    if (!f64_is_finite(a)) return 0;
    int e; uint64_t m; split(a, &e, &m); int neg = (a & SIGN) != 0;
    int sh = e - 52; uint64_t v;
    if (sh >= 32) v = 0; else if (sh >= 0) v = m << sh; else if (sh > -64) v = m >> -sh; else v = 0;
    uint32_t r = (uint32_t)v; return neg ? (uint32_t)(0u - r) : r;
}
int32_t f64_to_i32(f64 a) { return (int32_t)f64_to_u32(a); }

f64 f64_add(f64 a, f64 b)
{
    if (f64_is_nan(a) || f64_is_nan(b)) return F64_NAN;
    if (f64_is_inf(a)) { if (f64_is_inf(b) && ((a ^ b) & SIGN)) return F64_NAN; return a; }
    if (f64_is_inf(b)) return b;
    if (is_zero(a)) { if (is_zero(b)) return (a & b & SIGN) ? SIGN : 0; return b; }
    if (is_zero(b)) return a;
    int ea, eb; uint64_t ma, mb; unpack(a, &ea, &ma); unpack(b, &eb, &mb);
    int sa = (a & SIGN) != 0, sb = (b & SIGN) != 0;
    if (ea < eb || (ea == eb && ma < mb)) { int t = ea; ea = eb; eb = t; uint64_t tm = ma; ma = mb; mb = tm; t = sa; sa = sb; sb = t; }
    int d = ea - eb; int sticky = 0;
    if (d > 0) { if (d >= 64) { sticky = 1; mb = 0; } else { sticky = (mb & ((1ULL << d) - 1)) != 0; mb >>= d; } }
    uint64_t m; int e = ea;
    if (sa == sb) {
        m = ma + mb;
        if (m & (1ULL << 63)) { sticky |= (int)(m & 1); m >>= 1; e++; }
    } else {
        m = ma - mb - (sticky ? 1 : 0);
        if (!m && !sticky) return 0;
        if (!m) return 0;
        int lz = __builtin_clzll(m) - 1; if (lz > 0) { m <<= lz; e -= lz; }
    }
    return rp(sa, e, m, sticky);
}
f64 f64_sub(f64 a, f64 b) { return f64_add(a, b ^ SIGN); }
f64 f64_mul(f64 a, f64 b)
{
    int sg = ((a ^ b) & SIGN) != 0;
    if (f64_is_nan(a) || f64_is_nan(b)) return F64_NAN;
    if (f64_is_inf(a) || f64_is_inf(b)) { if (is_zero(a) || is_zero(b)) return F64_NAN; return (sg ? SIGN : 0) | F64_INF; }
    if (is_zero(a) || is_zero(b)) return sg ? SIGN : 0;
    int ea, eb; uint64_t ma, mb; unpack(a, &ea, &ma); unpack(b, &eb, &mb);
    u128 p = (u128)ma * mb;                       /* bit 124 o 125 */
    int e = ea + eb; uint64_t m; int st;
    if (p >> 125) { m = (uint64_t)(p >> 63); st = (p & (((u128)1 << 63) - 1)) != 0; e += 1; }
    else { m = (uint64_t)(p >> 62); st = (p & (((u128)1 << 62) - 1)) != 0; }
    return rp(sg, e, m, st);
}
f64 f64_div(f64 a, f64 b)
{
    int sg = ((a ^ b) & SIGN) != 0;
    if (f64_is_nan(a) || f64_is_nan(b)) return F64_NAN;
    if (f64_is_inf(a)) { if (f64_is_inf(b)) return F64_NAN; return (sg ? SIGN : 0) | F64_INF; }
    if (f64_is_inf(b)) return sg ? SIGN : 0;
    if (is_zero(b)) { if (is_zero(a)) return F64_NAN; return (sg ? SIGN : 0) | F64_INF; }
    if (is_zero(a)) return sg ? SIGN : 0;
    int ea, eb; uint64_t ma, mb; unpack(a, &ea, &ma); unpack(b, &eb, &mb);
    uint64_t q = 0, r = ma;
    for (int i = 0; i < 64; i++) { q <<= 1; if (r >= mb) { r -= mb; q |= 1; } r <<= 1; }
    int e = ea - eb, st = r != 0;
    if (q & (1ULL << 63)) { st |= (int)(q & 1); q >>= 1; } else e -= 1;
    return rp(sg, e, q, st);
}
f64 f64_mod(f64 a, f64 b)
{
    if (f64_is_nan(a) || f64_is_nan(b) || f64_is_inf(a) || is_zero(b)) return F64_NAN;
    if (f64_is_inf(b) || is_zero(a)) return a;
    f64 x = f64_abs(a), y = f64_abs(b);
    if (f64_lt(x, y)) return a;
    int ex, ey; uint64_t mx, my; split(x, &ex, &mx); split(y, &ey, &my);
    /* normaliza subnormales */
    while (!(mx & (1ULL << 52))) { mx <<= 1; ex--; }
    while (!(my & (1ULL << 52))) { my <<= 1; ey--; }
    uint64_t r = mx; int k = ex - ey;
    if (r >= my) r -= my;
    while (k-- > 0) { r <<= 1; if (r >= my) r -= my; }
    if (!r) return (a & SIGN);
    /* resultado = r * 2^(ey-52) */
    f64 res = f64_from_u64(r);
    int bits = (int)((res >> 52) & 0x7FF) - 1023 + (ey - 52);
    if (bits < -1022 - 53) { res = 0; } else {
        /* multiplicar por 2^(ey-52) en pasos seguros */
        int sc = ey - 52; while (sc > 0) { int s = sc > 1000 ? 1000 : sc; res = f64_mul(res, (uint64_t)(s + 1023) << 52); sc -= s; }
        while (sc < 0) { int s = -sc > 1000 ? 1000 : -sc; res = f64_mul(res, (uint64_t)(1023 - s) << 52); sc += s; }
    }
    return res | (a & SIGN);
}
int f64_eq(f64 a, f64 b) { if (f64_is_nan(a) || f64_is_nan(b)) return 0; if (is_zero(a) && is_zero(b)) return 1; return a == b; }
int f64_lt(f64 a, f64 b)
{
    if (f64_is_nan(a) || f64_is_nan(b)) return 0;
    if (is_zero(a) && is_zero(b)) return 0;
    int sa = (a & SIGN) != 0, sb = (b & SIGN) != 0;
    if (sa != sb) return sa;
    if (sa) return a > b;
    return a < b;
}
int f64_le(f64 a, f64 b) { if (f64_is_nan(a) || f64_is_nan(b)) return 0; return !f64_lt(b, a); }
f64 f64_trunc(f64 a)
{
    if (!f64_is_finite(a)) return a;
    int e = fexp(a) - 1023; if (e >= 52) return a;
    if (e < 0) return a & SIGN;
    return a & ~(FRAC >> e);
}
int f64_is_int(f64 a) { return f64_is_finite(a) && f64_trunc(a) == (a & ~(is_zero(a) ? SIGN : 0)) ? 1 : (is_zero(a)); }
f64 f64_floor(f64 a) { f64 t = f64_trunc(a); if ((a & SIGN) && t != a && !(is_zero(a))) return f64_sub(t, F64_ONE); return t; }
f64 f64_ceil(f64 a) { f64 t = f64_trunc(a); if (!(a & SIGN) && t != a) return f64_add(t, F64_ONE); if ((a & SIGN) && is_zero(t)) return SIGN; return t; }
f64 f64_round(f64 a)
{
    if (!f64_is_finite(a)) return a;
    f64 half = 0x3FE0000000000000ULL; f64 r = f64_floor(f64_add(a, half));
    if (is_zero(r) && (a & SIGN)) return SIGN;
    if (fexp(a) >= 1075) return a;
    return r;
}
f64 f64_sqrt(f64 a)
{
    if (f64_is_nan(a)) return a;
    if (is_zero(a)) return a;
    if (a & SIGN) return F64_NAN;
    if (f64_is_inf(a)) return a;
    int e; uint64_t m; unpack(a, &e, &m);
    /* m/2^62 en [1,2): valor = m*2^(e-62); raiz entera de m * 2^(62 + (e&1)) ... */
    int odd = e & 1; if (odd) e -= 1;                     /* e par (e&1 de negativos en complemento a 2 sirve) */
    u128 n = (u128)m << (62 + (odd ? 1 : 0));            /* bit 124/125 -> raiz de ~62/63 bits */
    u128 x = 0, bit = (u128)1 << 126;
    while (bit > n) bit >>= 2;
    while (bit) { if (n >= x + bit) { n -= x + bit; x = (x >> 1) + bit; } else x >>= 1; bit >>= 2; }
    uint64_t r = (uint64_t)x; int st = n != 0;
    int lz = __builtin_clzll(r) - 1; if (lz > 0) r <<= lz; else if (lz < 0) { st |= (int)(r & 1); r >>= 1; lz = -1; }
    /* x ~ sqrt(m * 2^(62+odd)) = sqrt(m/2^62) * 2^(62+odd/2...) -> exponente resultante e/2 */
    (void)lz;
    return rp(0, e / 2 + (e < 0 && (e & 1) ? 0 : 0), r, st);
}

/* ---------- constantes y funciones elementales ---------- */
#define C_LN2   0x3FE62E42FEFA39EFULL
#define C_PI2   0x3FF921FB54442D18ULL
static f64 fi(int v) { return f64_from_i32(v); }
static f64 ldexp2(f64 x, int k)
{
    if (is_zero(x) || !f64_is_finite(x)) return x;
    while (k > 0) { int s = k > 1000 ? 1000 : k; x = f64_mul(x, (uint64_t)(s + 1023) << 52); k -= s; }
    while (k < 0) { int s = -k > 1000 ? 1000 : -k; x = f64_mul(x, (uint64_t)(1023 - s) << 52); k += s; }
    return x;
}
f64 f64_exp(f64 x)
{
    if (f64_is_nan(x)) return x;
    if (f64_is_inf(x)) return (x & SIGN) ? 0 : F64_INF;
    if (f64_lt(fi(710), x)) return F64_INF;
    if (f64_lt(x, fi(-746))) return 0;
    f64 kf = f64_round(f64_mul(x, 0x3FF71547652B82FEULL)); int k = (int)f64_to_i64(kf);
    f64 r = f64_sub(x, f64_mul(kf, C_LN2));
    f64 term = F64_ONE, sum = F64_ONE;
    for (int i = 1; i < 28; i++) { term = f64_div(f64_mul(term, r), fi(i)); sum = f64_add(sum, term); }
    return ldexp2(sum, k);
}
f64 f64_log(f64 x)
{
    if (f64_is_nan(x) || (x & SIGN && !is_zero(x))) return F64_NAN;
    if (is_zero(x)) return F64_NINF;
    if (f64_is_inf(x)) return x;
    int e; uint64_t m; unpack(x, &e, &m);
    f64 mm = rp(0, 0, m, 0);                                 /* [1,2) */
    if (f64_lt(0x3FF6A09E667F3BCDULL, mm)) { mm = f64_mul(mm, 0x3FE0000000000000ULL); e++; }
    f64 t = f64_div(f64_sub(mm, F64_ONE), f64_add(mm, F64_ONE)); f64 t2 = f64_mul(t, t);
    f64 sum = 0, p = t;
    for (int i = 1; i < 60; i += 2) { sum = f64_add(sum, f64_div(p, fi(i))); p = f64_mul(p, t2); }
    return f64_add(f64_mul(sum, fi(2)), f64_mul(fi(e), C_LN2));
}
f64 f64_pow(f64 a, f64 b)
{
    if (is_zero(b)) return F64_ONE;
    if (f64_is_nan(a) || f64_is_nan(b)) return F64_NAN;
    if (f64_is_inf(b)) { f64 aa = f64_abs(a); if (aa == F64_ONE) return F64_NAN; int gt = f64_lt(F64_ONE, aa); return ((b & SIGN) ? !gt : gt) ? F64_INF : 0; }
    int bint = f64_is_int(b);
    int bodd = bint && fexp(f64_abs(b)) < 1075 && (f64_to_i64(b) & 1);
    if (is_zero(a)) { int neg = (a & SIGN) && bodd; if (b & SIGN) return neg ? F64_NINF : F64_INF; return neg ? SIGN : 0; }
    if (f64_is_inf(a)) { if (a & SIGN) { if (b & SIGN) return bodd ? SIGN : 0; return bodd ? F64_NINF : F64_INF; } return (b & SIGN) ? 0 : F64_INF; }
    if (bint && f64_lt(f64_abs(b), fi(1024))) {          /* potencia entera por cuadrados */
        int64_t n = f64_to_i64(b); int neg = n < 0; if (neg) n = -n;
        f64 base = a, r = F64_ONE; while (n) { if (n & 1) r = f64_mul(r, base); base = f64_mul(base, base); n >>= 1; }
        return neg ? f64_div(F64_ONE, r) : r;
    }
    if (a & SIGN) { if (!bint) return F64_NAN; f64 r = f64_exp(f64_mul(b, f64_log(f64_abs(a)))); return bodd ? f64_neg(r) : r; }
    return f64_exp(f64_mul(b, f64_log(a)));
}
static f64 sin_poly(f64 r)
{
    f64 r2 = f64_mul(r, r), term = r, sum = r;
    for (int i = 1; i < 14; i++) { term = f64_neg(f64_div(f64_mul(term, r2), fi((2 * i) * (2 * i + 1)))); sum = f64_add(sum, term); }
    return sum;
}
static f64 cos_poly(f64 r)
{
    f64 r2 = f64_mul(r, r), term = F64_ONE, sum = F64_ONE;
    for (int i = 1; i < 14; i++) { term = f64_neg(f64_div(f64_mul(term, r2), fi((2 * i - 1) * (2 * i)))); sum = f64_add(sum, term); }
    return sum;
}
static void sincos_red(f64 x, f64 *s, f64 *c)
{
    f64 kf = f64_round(f64_div(x, C_PI2)); int64_t k = f64_to_i64(kf);
    /* pi/2 en dos partes para mejor reduccion */
    f64 r = f64_sub(x, f64_mul(kf, 0x3FF921FB54400000ULL)); r = f64_sub(r, f64_mul(kf, 0x3DD0B4611A600000ULL)); r = f64_sub(r, f64_mul(kf, 0x3BA3198A2E037073ULL));
    f64 sr = sin_poly(r), cr = cos_poly(r);
    switch ((int)(k & 3)) { case 0: *s = sr; *c = cr; break; case 1: *s = cr; *c = f64_neg(sr); break; case 2: *s = f64_neg(sr); *c = f64_neg(cr); break; default: *s = f64_neg(cr); *c = sr; }
}
f64 f64_sin(f64 x) { if (!f64_is_finite(x)) return F64_NAN; if (is_zero(x)) return x; f64 s, c; sincos_red(x, &s, &c); return s; }
f64 f64_cos(f64 x) { if (!f64_is_finite(x)) return F64_NAN; f64 s, c; sincos_red(x, &s, &c); return c; }
f64 f64_tan(f64 x) { if (!f64_is_finite(x)) return F64_NAN; if (is_zero(x)) return x; f64 s, c; sincos_red(x, &s, &c); return f64_div(s, c); }
f64 f64_atan(f64 x)
{
    if (f64_is_nan(x) || is_zero(x)) return x;
    int neg = (x & SIGN) != 0; f64 a = f64_abs(x); int inv = 0;
    if (f64_is_inf(a)) { f64 r = C_PI2; return neg ? f64_neg(r) : r; }
    if (f64_lt(F64_ONE, a)) { a = f64_div(F64_ONE, a); inv = 1; }
    a = f64_div(a, f64_add(F64_ONE, f64_sqrt(f64_add(F64_ONE, f64_mul(a, a)))));
    a = f64_div(a, f64_add(F64_ONE, f64_sqrt(f64_add(F64_ONE, f64_mul(a, a)))));
    f64 a2 = f64_mul(a, a), p = a, sum = a;
    for (int i = 1; i < 16; i++) { p = f64_neg(f64_mul(p, a2)); sum = f64_add(sum, f64_div(p, fi(2 * i + 1))); }
    sum = f64_mul(sum, fi(4));
    if (inv) sum = f64_sub(C_PI2, sum);
    return neg ? f64_neg(sum) : sum;
}
f64 f64_atan2(f64 y, f64 x)
{
    if (f64_is_nan(x) || f64_is_nan(y)) return F64_NAN;
    int xn = (x & SIGN) != 0, yn = (y & SIGN) != 0;
    if (is_zero(y)) { if (!xn) return y; return yn ? f64_neg(F64_PI) : F64_PI; }
    if (is_zero(x)) return yn ? f64_neg(C_PI2) : C_PI2;
    if (f64_is_inf(x) && f64_is_inf(y)) { f64 q = xn ? 0x4002D97C7F3321D2ULL : 0x3FE921FB54442D18ULL; return yn ? f64_neg(q) : q; }
    f64 r = f64_atan(f64_div(y, x));
    if (xn) r = yn ? f64_sub(r, F64_PI) : f64_add(r, F64_PI);
    return r;
}
f64 f64_asin(f64 x) { f64 a = f64_abs(x); if (f64_lt(F64_ONE, a) || f64_is_nan(x)) return F64_NAN; if (a == F64_ONE) return (x & SIGN) ? f64_neg(C_PI2) : C_PI2; return f64_atan(f64_div(x, f64_sqrt(f64_sub(F64_ONE, f64_mul(x, x))))); }
f64 f64_acos(f64 x) { f64 a = f64_asin(x); if (f64_is_nan(a)) return a; return f64_sub(C_PI2, a); }

/* ---------- texto -> numero ---------- */
static f64 p10[23]; static int p10_ok;
static void p10_init(void) { if (p10_ok) return; p10[0] = F64_ONE; for (int i = 1; i < 23; i++) p10[i] = f64_mul(p10[i - 1], fi(10)); p10_ok = 1; }
static f64 scale10(f64 v, int e)
{
    p10_init();
    if (is_zero(v)) return v;
    while (e > 22) { v = f64_mul(v, p10[22]); e -= 22; if (f64_is_inf(v)) return v; }
    while (e < -22) { v = f64_div(v, p10[22]); e += 22; if (is_zero(v)) return v; }
    if (e > 0) v = f64_mul(v, p10[e]); else if (e < 0) v = f64_div(v, p10[-e]);
    return v;
}
/* parseo exacto con enteros grandes (u32 binarios) para los casos fuera de la ruta rapida */
#define BL 40
struct bigu { uint32_t w[BL]; int n; };
static void bu_set(struct bigu *b, uint64_t v) { b->n = 0; while (v) { b->w[b->n++] = (uint32_t)v; v >>= 32; } }
static void bu_mulsmall(struct bigu *b, uint32_t m) { uint64_t c = 0; for (int i = 0; i < b->n; i++) { uint64_t v = (uint64_t)b->w[i] * m + c; b->w[i] = (uint32_t)v; c = v >> 32; } if (c && b->n < BL) b->w[b->n++] = (uint32_t)c; }
static int bu_bits(const struct bigu *b) { if (!b->n) return 0; return (b->n - 1) * 32 + (32 - __builtin_clz(b->w[b->n - 1])); }
static int bu_bit(const struct bigu *b, int i) { if (i < 0) return 0; int w = i >> 5; return w < b->n ? (int)((b->w[w] >> (i & 31)) & 1) : 0; }
static int bu_cmp(const struct bigu *a, const struct bigu *b) { if (a->n != b->n) return a->n < b->n ? -1 : 1; for (int i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1; return 0; }
static void bu_sub(struct bigu *a, const struct bigu *b) { int64_t br = 0; for (int i = 0; i < a->n; i++) { int64_t v = (int64_t)a->w[i] - (i < b->n ? b->w[i] : 0) - br; if (v < 0) { v += 4294967296LL; br = 1; } else br = 0; a->w[i] = (uint32_t)v; } while (a->n && !a->w[a->n - 1]) a->n--; }
static void bu_shl1_or(struct bigu *a, int bit) { uint32_t c = (uint32_t)bit; for (int i = 0; i < a->n; i++) { uint32_t nc = a->w[i] >> 31; a->w[i] = (a->w[i] << 1) | c; c = nc; } if (c && a->n < BL) a->w[a->n++] = c; }
static void pow10_big(struct bigu *b, int k) { bu_set(b, 1); while (k >= 9) { bu_mulsmall(b, 1000000000u); k -= 9; } uint32_t m = 1; while (k-- > 0) m *= 10; if (m > 1) bu_mulsmall(b, m); }
static f64 parse_exact(uint64_t m, int e10)
{
    if (!m) return 0;
    if (e10 > 310) return F64_INF;
    if (e10 < -345) return 0;
    struct bigu P; pow10_big(&P, e10 >= 0 ? e10 : -e10);
    if (e10 >= 0) {
        bu_mulsmall(&P, 1);   /* no-op */
        struct bigu B; B.n = 0; /* B = P * m */
        uint64_t mlo = m & 0xFFFFFFFFu, mhi = m >> 32; struct bigu T1 = P, T2 = P; bu_mulsmall(&T1, (uint32_t)mlo); bu_mulsmall(&T2, (uint32_t)mhi);
        /* B = T1 + (T2 << 32) */
        uint64_t c = 0; int n = (T1.n > T2.n + 1 ? T1.n : T2.n + 1); if (n > BL) n = BL; B.n = n;
        for (int i = 0; i < n; i++) { uint64_t v = c + (i < T1.n ? T1.w[i] : 0) + (i >= 1 && i - 1 < T2.n ? T2.w[i - 1] : 0); B.w[i] = (uint32_t)v; c = v >> 32; }
        while (B.n && !B.w[B.n - 1]) B.n--;
        int L = bu_bits(&B); uint64_t top = 0; int st = 0;
        for (int i = 0; i < 64; i++) { top = (top << 1) | (uint64_t)bu_bit(&B, L - 1 - i); }
        for (int i = L - 65; i >= 0 && !st; i--) if (bu_bit(&B, i)) st = 1;
        if (L < 64) { top = 0; for (int i = L - 1; i >= 0; i--) top = (top << 1) | (uint64_t)bu_bit(&B, i); top <<= (64 - L); }
        return rp(0, L - 1, top >> 1, st | (int)(top & 1));
    }
    int bm = 64 - __builtin_clzll(m), bD = bu_bits(&P);
    int sft = bD - bm + 66; if (sft < 0) sft = 0;
    struct bigu R; R.n = 0; u128 q = 0;
    int total = bm + sft;
    for (int i = total - 1; i >= 0; i--) {
        int bit = i >= sft ? (int)((m >> (i - sft)) & 1) : 0;
        bu_shl1_or(&R, bit); while (R.n && !R.w[R.n - 1]) R.n--;
        q <<= 1; if (bu_cmp(&R, &P) >= 0) { bu_sub(&R, &P); q |= 1; }
    }
    int st = R.n != 0;
    int Lq = 0; { u128 t = q; while (t) { Lq++; t >>= 1; } }
    uint64_t top; if (Lq > 64) { top = (uint64_t)(q >> (Lq - 64)); st |= (q & (((u128)1 << (Lq - 64)) - 1)) != 0; } else top = (uint64_t)(q << (64 - Lq));
    return rp(0, Lq - 1 - sft, top >> 1, st | (int)(top & 1));
}
f64 f64_parse(const char *s, const char **end)
{
    const char *p = s; int neg = 0;
    if (*p == '+') p++; else if (*p == '-') { neg = 1; p++; }
    uint64_t m = 0; int nd = 0, exp10 = 0, any = 0, drop = 0, inexact = 0;
    while (*p >= '0' && *p <= '9') { any = 1; if (nd < 19) { m = m * 10 + (uint64_t)(*p - '0'); if (m) nd++; } else { drop++; if (*p != '0') inexact = 1; } p++; }
    exp10 += drop;
    if (*p == '.') {
        const char *q = p + 1;
        if ((*q >= '0' && *q <= '9') || any) {
            p = q;
            while (*p >= '0' && *p <= '9') { any = 1; if (nd < 19) { m = m * 10 + (uint64_t)(*p - '0'); if (m) nd++; exp10--; } else if (!m) exp10--; else if (*p != '0') inexact = 1; p++; }
        }
    }
    if (!any) { if (end) *end = 0; return F64_NAN; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1; int en = 0, ev = 0, dd = 0; if (*q == '+') q++; else if (*q == '-') { en = 1; q++; }
        while (*q >= '0' && *q <= '9') { if (ev < 100000) ev = ev * 10 + (*q - '0'); q++; dd = 1; }
        if (dd) { exp10 += en ? -ev : ev; p = q; }
    }
    if (end) *end = p;
    f64 v;
    if (m < (1ULL << 53) && exp10 >= -22 && exp10 <= 22 && !inexact) { v = f64_from_u64(m); v = scale10(v, exp10); }
    else { if (inexact) { /* digitos recortados: se anade uno pegajoso para el redondeo */ if (m < 1844674407370955160ULL) { m = m * 10 + 1; exp10--; } } v = parse_exact(m, exp10); }
    return neg ? (v | SIGN) : v;
}

/* ---------- numero -> texto ---------- */
#define LIMBS 110
static int big_digits(uint64_t m, int e2, char *dig, int *pt)
{
    uint32_t L[LIMBS]; int n = 0; uint64_t t = m;
    while (t) { L[n++] = (uint32_t)(t % 1000000000u); t /= 1000000000u; }
    int k = e2 >= 0 ? e2 : -e2; uint32_t mul = e2 >= 0 ? 2u : 5u; int per = e2 >= 0 ? 29 : 12;
    uint32_t f = e2 >= 0 ? (1u << 29) : 244140625u;     /* 2^29, 5^12 */
    while (k > 0) {
        int s = k >= per ? per : k; uint32_t mm;
        if (s == per) mm = f; else { mm = 1; for (int i = 0; i < s; i++) mm *= mul; }
        uint64_t carry = 0;
        for (int i = 0; i < n; i++) { uint64_t v = (uint64_t)L[i] * mm + carry; L[i] = (uint32_t)(v % 1000000000u); carry = v / 1000000000u; }
        while (carry && n < LIMBS) { L[n++] = (uint32_t)(carry % 1000000000u); carry /= 1000000000u; }
        k -= s;
    }
    int len = 0; char tmp[LIMBS * 9 + 2];
    for (int i = n - 1; i >= 0; i--) {
        uint32_t v = L[i];
        if (i == n - 1) { char r[10]; int rl = 0; while (v) { r[rl++] = (char)('0' + v % 10); v /= 10; } while (rl) tmp[len++] = r[--rl]; }
        else { for (int j = 8; j >= 0; j--) { tmp[len + j] = (char)('0' + v % 10); v /= 10; } len += 9; }
    }
    for (int i = 0; i < len; i++) dig[i] = tmp[i];
    *pt = e2 >= 0 ? len : len + e2;
    while (len > 1 && dig[len - 1] == '0') len--;
    dig[len] = 0; return len;
}
/* redondea la cadena de digitos a p (>=0); puede incrementar *pt. Devuelve nueva longitud (puede ser 0 si p<=0 y no sube) */
static int round_digits(char *dig, int len, int p, int *pt)
{
    if (p >= len) return len;
    if (p < 0) { dig[0] = 0; return 0; }
    int up = dig[p] >= '5';
    len = p;
    if (up) {
        int i = len - 1;
        while (i >= 0 && dig[i] == '9') { dig[i] = '0'; i--; }
        if (i >= 0) dig[i]++;
        else { for (int j = len; j > 0; j--) dig[j] = dig[j - 1]; dig[0] = '1'; len++; (*pt)++; if (p == 0 || len > p) { len = p == 0 ? 1 : len; } }
    }
    while (len > 1 && dig[len - 1] == '0') len--;
    dig[len] = 0; return len;
}
static void decompose(f64 a, char *dig, int *len, int *pt)
{
    int e; uint64_t m; split(a, &e, &m); *len = big_digits(m, e - 52, dig, pt);
}
static int emit_js(const char *dig, int k, int n, char *out)
{
    int o = 0;
    if (k <= n && n <= 21) { for (int i = 0; i < k; i++) out[o++] = dig[i]; for (int i = k; i < n; i++) out[o++] = '0'; }
    else if (0 < n && n <= 21) { for (int i = 0; i < n; i++) out[o++] = dig[i]; out[o++] = '.'; for (int i = n; i < k; i++) out[o++] = dig[i]; }
    else if (-6 < n && n <= 0) { out[o++] = '0'; out[o++] = '.'; for (int i = 0; i < -n; i++) out[o++] = '0'; for (int i = 0; i < k; i++) out[o++] = dig[i]; }
    else {
        out[o++] = dig[0]; if (k > 1) { out[o++] = '.'; for (int i = 1; i < k; i++) out[o++] = dig[i]; }
        out[o++] = 'e'; int ex = n - 1; out[o++] = ex < 0 ? '-' : '+'; if (ex < 0) ex = -ex;
        char t[8]; int tl = 0; do { t[tl++] = (char)('0' + ex % 10); ex /= 10; } while (ex); while (tl) out[o++] = t[--tl];
    }
    out[o] = 0; return o;
}
static int radix_str(f64 a, int radix, char *out)
{
    int o = 0; if (a & SIGN) { out[o++] = '-'; a &= ~SIGN; }
    f64 ip = f64_floor(a), fp = f64_sub(a, ip); static const char dg[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char t[1100]; int tl = 0;
    if (is_zero(ip)) t[tl++] = '0';
    else { f64 r = fi(radix); while (!is_zero(ip) && tl < 1080) { f64 q = f64_floor(f64_div(ip, r)); int d = (int)f64_to_i64(f64_sub(ip, f64_mul(q, r))); t[tl++] = dg[d < 0 ? 0 : d >= radix ? radix - 1 : d]; ip = q; } }
    while (tl) out[o++] = t[--tl];
    if (!is_zero(fp)) { out[o++] = '.'; for (int i = 0; i < 52 && !is_zero(fp); i++) { fp = f64_mul(fp, fi(radix)); f64 d = f64_floor(fp); out[o++] = dg[f64_to_i64(d)]; fp = f64_sub(fp, d); } }
    out[o] = 0; return o;
}
static int dcmp(const char *a, int la, int pa, const char *b, int lb, int pb)
{
    if (pa != pb) return pa < pb ? -1 : 1;
    int n = la > lb ? la : lb;
    for (int i = 0; i < n; i++) { int ca = i < la ? a[i] : '0', cb = i < lb ? b[i] : '0'; if (ca != cb) return ca < cb ? -1 : 1; }
    return 0;
}
int f64_to_str(f64 a, char *out, int radix)
{
    if (f64_is_nan(a)) { memcpy(out, "NaN", 4); return 3; }
    if (is_zero(a)) { out[0] = '0'; out[1] = 0; return 1; }
    int o = 0; if (a & SIGN) { out[o++] = '-'; a &= ~SIGN; }
    if (f64_is_inf(a)) { memcpy(out + o, "Infinity", 9); return o + 8; }
    if (radix != 10 && radix >= 2 && radix <= 36) { return radix_str(o ? (a | SIGN) : a, radix, out); }
    int e; uint64_t m; split(a, &e, &m);
    if (e < 53 && e >= 0 && (m << 0) && e <= 52 + 0 && (a < 0x4340000000000000ULL) && f64_is_int(a)) {   /* entero < 2^53 */
        uint64_t v = (uint64_t)f64_to_i64(a); char t[24]; int tl = 0; do { t[tl++] = (char)('0' + v % 10); v /= 10; } while (v); while (tl) out[o++] = t[--tl]; out[o] = 0; return o;
    }
    char dig[LIMBS * 9 + 4], lo[LIMBS * 9 + 4], hi[LIMBS * 9 + 4]; int len, pt, llo, plo, lhi, phi;
    len = big_digits(m, e - 52, dig, &pt);
    int pow2edge = (m == (1ULL << 52) && e > -1022);
    llo = pow2edge ? big_digits(4 * m - 1, e - 54, lo, &plo) : big_digits(2 * m - 1, e - 53, lo, &plo);
    lhi = big_digits(2 * m + 1, e - 53, hi, &phi);
    int even = !(m & 1);
    char cand[LIMBS * 9 + 4]; int cl = len, cpt = pt; memcpy(cand, dig, (size_t)len + 1);
    for (int p = 1; p < len; p++) {
        char d2[LIMBS * 9 + 4]; memcpy(d2, dig, (size_t)len + 1); int pt2 = pt; int l2 = round_digits(d2, len, p, &pt2);
        int c1 = dcmp(d2, l2, pt2, lo, llo, plo), c2 = dcmp(d2, l2, pt2, hi, lhi, phi);
        if ((c1 > 0 || (even && c1 == 0)) && (c2 < 0 || (even && c2 == 0))) { memcpy(cand, d2, (size_t)l2 + 1); cl = l2; cpt = pt2; break; }
    }
    return o + emit_js(cand, cl, cpt, out + o);
}
int f64_to_fixed(f64 a, int fd, char *out)
{
    if (fd < 0) fd = 0;
    if (fd > F64_DIGITS_MAX) fd = F64_DIGITS_MAX;           /* SOMA_200: el numero de decimales lo elige la pagina; `out` es un bufer fijo de quien llama */
    if (f64_is_nan(a)) { memcpy(out, "NaN", 4); return 3; }
    int o = 0; if ((a & SIGN) && !is_zero(a)) { out[o++] = '-'; a &= ~SIGN; } else a &= ~SIGN;
    if (f64_is_inf(a)) { memcpy(out + o, "Infinity", 9); return o + 8; }
    if (!f64_lt(a, 0x444B1AE4D6E2EF50ULL)) { return o + f64_to_str(a, out + o, 10); }     /* >= 1e21 */
    char dig[LIMBS * 9 + 4]; int len = 1, pt = 1;
    if (is_zero(a)) { dig[0] = '0'; dig[1] = 0; pt = 1; } else decompose(a, dig, &len, &pt);
    int keep = pt + fd;
    if (is_zero(a)) len = 1;
    else if (keep < 0) { dig[0] = 0; len = 0; }
    else if (keep == 0) { int up = dig[0] >= '5'; if (up) { dig[0] = '1'; dig[1] = 0; len = 1; pt++; } else { dig[0] = 0; len = 0; } }
    else len = round_digits(dig, len, keep, &pt);
    /* escribir parte entera */
    if (pt <= 0 || len == 0) out[o++] = '0';
    else for (int i = 0; i < pt; i++) out[o++] = i < len ? dig[i] : '0';
    if (fd > 0) {
        out[o++] = '.';
        for (int i = 0; i < fd; i++) { int idx = pt + i; out[o++] = (idx >= 0 && idx < len) ? dig[idx] : '0'; }
    }
    out[o] = 0; return o;
}
int f64_to_exponential(f64 a, int fd, char *out)
{
    if (fd > F64_DIGITS_MAX) fd = F64_DIGITS_MAX;           /* SOMA_200 */
    if (!f64_is_finite(a)) return f64_to_str(a, out, 10);
    int o = 0; if (a & SIGN) { if (!is_zero(a)) out[o++] = '-'; a &= ~SIGN; }
    char dig[LIMBS * 9 + 4]; int len, pt;
    if (is_zero(a)) { dig[0] = '0'; dig[1] = 0; len = 1; pt = 1; } else decompose(a, dig, &len, &pt);
    if (fd >= 0) len = round_digits(dig, len, fd + 1, &pt);
    int nd = fd >= 0 ? fd + 1 : len;
    out[o++] = dig[0]; if (nd > 1) { out[o++] = '.'; for (int i = 1; i < nd; i++) out[o++] = i < len ? dig[i] : '0'; }
    out[o++] = 'e'; int ex = is_zero(a) ? 0 : pt - 1; out[o++] = ex < 0 ? '-' : '+'; if (ex < 0) ex = -ex;
    char t[8]; int tl = 0; do { t[tl++] = (char)('0' + ex % 10); ex /= 10; } while (ex); while (tl) out[o++] = t[--tl];
    out[o] = 0; return o;
}
int f64_to_precision(f64 a, int p, char *out)
{
    if (p < 1) p = 1;
    if (p > F64_DIGITS_MAX) p = F64_DIGITS_MAX;             /* SOMA_200 */
    if (!f64_is_finite(a)) return f64_to_str(a, out, 10);
    int o = 0; if ((a & SIGN) && !is_zero(a)) { out[o++] = '-'; } a &= ~SIGN;
    char dig[LIMBS * 9 + 4]; int len, pt;
    if (is_zero(a)) { dig[0] = '0'; dig[1] = 0; len = 1; pt = 1; } else decompose(a, dig, &len, &pt);
    len = round_digits(dig, len, p, &pt);
    int e = is_zero(a) ? 0 : pt - 1;
    if (e < -6 || e >= p) {
        out[o++] = dig[0]; if (p > 1) { out[o++] = '.'; for (int i = 1; i < p; i++) out[o++] = i < len ? dig[i] : '0'; }
        out[o++] = 'e'; out[o++] = e < 0 ? '-' : '+'; int ex = e < 0 ? -e : e; char t[8]; int tl = 0; do { t[tl++] = (char)('0' + ex % 10); ex /= 10; } while (ex); while (tl) out[o++] = t[--tl];
    } else if (e >= 0) {
        for (int i = 0; i <= e; i++) out[o++] = i < len ? dig[i] : '0';
        if (p > e + 1) { out[o++] = '.'; for (int i = e + 1; i < p; i++) out[o++] = i < len ? dig[i] : '0'; }
    } else {
        out[o++] = '0'; out[o++] = '.'; for (int i = 0; i < -e - 1; i++) out[o++] = '0'; for (int i = 0; i < p; i++) out[o++] = i < len ? dig[i] : '0';
    }
    out[o] = 0; return o;
}
