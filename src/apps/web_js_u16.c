/* SOMA_185: cadenas con semantica UTF-16 (la de JavaScript) sobre almacenamiento UTF-8.
 *
 * Las cadenas siguen guardadas en UTF-8, pero lo que ve el script (length, indices, charCodeAt, slice, indexOf, lastIndex de las
 * expresiones regulares...) son unidades de 16 bits: un caracter fuera del plano basico ("😀") cuenta 2 y un caracter de 2 o 3 bytes
 * ("é", "日") cuenta 1. Las mitades sueltas de un par sustituto no existen en UTF-8: se guardan como 3 bytes (ED A0..BF xx), igual
 * que WTF-8, y se vuelven a unir si se concatenan ("\uD83D" + "\uDE00" == "😀").
 *
 * Las cadenas ASCII (casi todas) no pagan nada: su longitud en unidades coincide con la de bytes y la conversion es la identidad. Para las
 * demas, la longitud se calcula una vez (jstr.u16) y un cursor por motor (rt->u16c_*) hace O(1) los recorridos secuenciales.
 * soma.jsutf16=off (o modo seguro): todo cuenta bytes, como antes del parche 185. */
#include "web_js_i.h"

#if defined(WEB_HOST_TEST)
int g_ju16_off;
#endif
int js_u16_on(void)
{
#if defined(WEB_HOST_TEST)
    return !g_ju16_off;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsutf16") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}

/* bytes y unidades del caracter que empieza en el byte c (por el byte de cabecera; una continuacion suelta cuenta como 1 y 1) */
static inline uint32_t seq_bytes(uint8_t c) { return c < 0x80 ? 1u : c >= 0xF0 ? 4u : c >= 0xE0 ? 3u : c >= 0xC0 ? 2u : 1u; }
static inline uint32_t seq_units(uint8_t c) { return c >= 0xF0 ? 2u : 1u; }

uint32_t js_u16len_n(const char *d, uint32_t L)         /* unidades UTF-16 de un bloque de bytes UTF-8 (sin cache) */
{
    if (!js_u16_on()) return L;
    const uint8_t *p = (const uint8_t *)d; uint32_t i = 0, n = 0;
    while (i < L) { n += seq_units(p[i]); i += seq_bytes(p[i]); }
    return n;
}

uint32_t js_u16len(jstr *s)
{
    if (!js_u16_on()) return s->len;
    if (s->u16) return s->u16 - 1;
    const uint8_t *p = (const uint8_t *)s->d; uint32_t i = 0, n = 0, L = s->len;
    while (i < L) {
        if (i + 8 <= L) { uint64_t w; memcpy(&w, p + i, 8); if (!(w & 0x8080808080808080ull)) { i += 8; n += 8; continue; } }
        n += seq_units(p[i]); i += seq_bytes(p[i]);
    }
    s->u16 = n + 1; return n;
}

/* Cursor: devuelve (u, i) = (unidad, byte) de un caracter que empieza en una posicion <= la pedida, para no recorrer desde el principio. */
static void cursor_for(struct jrt *rt, jstr *s, uint32_t idx, uint32_t *u, uint32_t *i)
{
    if (rt->u16c_s == s && rt->u16c_idx <= idx) { *u = rt->u16c_idx; *i = rt->u16c_off; } else { *u = 0; *i = 0; }
    /* SOMA 189: en cadenas largas con caracteres no ASCII, un acceso hacia atras o alternando cadenas volvia a recorrer desde el
     * principio (20 % del tiempo en Google). Tabla de puntos de control cada 256 unidades para las ultimas 4 cadenas largas. */
    if (s->len >= 1024 && idx >= 256 && idx - *u >= 256) {
        uint32_t *tab = 0;
        for (int k = 0; k < 4; k++) if (rt->u16t[k].s == s) { tab = rt->u16t[k].tab; break; }
        if (!tab) {
            uint32_t n16 = js_u16len(s), nt = n16 / 256 + 1;
            tab = (uint32_t *)js_alloc_atomic(rt, nt * (uint32_t)sizeof(uint32_t));
            uint32_t uu = 0, ii = 0, next = 256, t = 1; tab[0] = 0;
            while (ii < s->len && t < nt) {
                uint8_t c = (uint8_t)s->d[ii]; uint32_t un = seq_units(c);
                if (uu == next) { tab[t++] = ii; next += 256; }
                else if (uu + un > next) { tab[t++] = ii | 0x80000000u; next += 256; }   /* un par sustituto cruza el punto: se apunta su comienzo (unidad next-1) */
                uu += un; ii += seq_bytes(c);
            }
            while (t < nt) tab[t++] = 0xFFFFFFFFu;
            uint32_t slot = rt->u16t_next++ & 3; rt->u16t[slot].s = s; rt->u16t[slot].tab = tab;
        }
        uint32_t cp = idx / 256, e = tab[cp];
        if (e != 0xFFFFFFFFu && cp > 0) { if (e & 0x80000000u) { *u = cp * 256 - 1; *i = e & 0x7FFFFFFFu; } else { *u = cp * 256; *i = e; } }
    }
}

uint32_t js_u16_to_byte(struct jrt *rt, jstr *s, uint32_t idx, int *mid)
{
    *mid = 0;
    uint32_t n16 = js_u16len(s);
    if (n16 == s->len) return idx > s->len ? s->len : idx;           /* ASCII (o modo bytes) */
    if (idx >= n16) return s->len;
    uint32_t u, i; cursor_for(rt, s, idx, &u, &i);
    while (u < idx) {
        uint8_t c = (uint8_t)s->d[i]; uint32_t un = seq_units(c);
        if (u + un > idx) { *mid = 1; break; }                          /* idx cae entre las dos mitades de un par */
        u += un; i += seq_bytes(c);
        if (i >= s->len) break;
    }
    rt->u16c_s = s; rt->u16c_idx = u; rt->u16c_off = i;
    return i;
}

uint32_t js_byte_to_u16(struct jrt *rt, jstr *s, uint32_t off)
{
    uint32_t n16 = js_u16len(s);
    if (n16 == s->len) return off;
    if (off >= s->len) return n16;
    uint32_t u = 0, i = 0;
    if (rt->u16c_s == s && rt->u16c_off <= off) { u = rt->u16c_idx; i = rt->u16c_off; }
    while (i < off && i < s->len) { uint8_t c = (uint8_t)s->d[i]; u += seq_units(c); i += seq_bytes(c); }
    rt->u16c_s = s; rt->u16c_idx = u; rt->u16c_off = i;
    return u;
}

int32_t js_u16_unit(struct jrt *rt, jstr *s, uint32_t idx)
{
    uint32_t n16 = js_u16len(s);
    if (idx >= n16) return -1;
    if (n16 == s->len) return (uint8_t)s->d[idx];
    int mid; uint32_t b = js_u16_to_byte(rt, s, idx, &mid);
    uint32_t adv; uint32_t cp = wu_utf8_next(s->d + b, &adv);
    if (cp >= 0x10000) { cp -= 0x10000; return mid ? (int32_t)(0xDC00 + (cp & 0x3FF)) : (int32_t)(0xD800 + (cp >> 10)); }
    return (int32_t)cp;
}

/* una mitad suelta de un par sustituto, en 3 bytes */
static void put_surrogate(struct wb *w, uint32_t unit) { char t[4]; int n = wu_utf8_put(unit, t); wb_addn(w, t, (uint32_t)n); }

jstr *js_u16_slice(struct jrt *rt, jstr *s, uint32_t a, uint32_t b)
{
    uint32_t n16 = js_u16len(s);
    if (b > n16) b = n16;
    if (a >= b) return rt->s_empty;
    if (!js_u16_on()) {                                                  /* modo bytes (antes del parche): los cortes se alinean a caracteres */
        while (a > 0 && a < s->len && ((uint8_t)s->d[a] & 0xC0) == 0x80) a--;
        while (b < s->len && ((uint8_t)s->d[b] & 0xC0) == 0x80) b++;
        return js_str_new(rt, s->d + a, b - a);
    }
    if (n16 == s->len) return js_str_new(rt, s->d + a, b - a);
    int ma, mb; uint32_t ba = js_u16_to_byte(rt, s, a, &ma), bb = js_u16_to_byte(rt, s, b, &mb);
    if (!ma && !mb) return js_str_new(rt, s->d + ba, bb - ba);
    struct wb w = {0};
    if (ma) { uint32_t adv; uint32_t cp = wu_utf8_next(s->d + ba, &adv) - 0x10000; put_surrogate(&w, 0xDC00 + (cp & 0x3FF)); ba += 4; }
    if (bb > ba) wb_addn(&w, s->d + ba, bb - ba);
    if (mb) { uint32_t adv; uint32_t cp = wu_utf8_next(s->d + bb, &adv) - 0x10000; put_surrogate(&w, 0xD800 + (cp >> 10)); }
    jstr *r = js_str_new(rt, w.p ? w.p : "", w.len); wb_free(&w); return r;
}

jstr *js_u16_char(struct jrt *rt, jstr *s, uint32_t idx)
{
    uint32_t n16 = js_u16len(s);
    if (idx >= n16) return 0;
    if (!js_u16_on()) { if (((uint8_t)s->d[idx] & 0xC0) == 0x80) return rt->s_empty; uint32_t adv; wu_utf8_next(s->d + idx, &adv); if (!adv || idx + adv > s->len) adv = 1; return js_str_new(rt, s->d + idx, adv); }
    if (n16 == s->len) return js_str_new(rt, s->d + idx, 1);
    return js_u16_slice(rt, s, idx, idx + 1);
}

void js_wb_unit(struct wb *b, uint32_t unit)
{
    unit &= 0xFFFF;
    if (unit >= 0xDC00 && unit <= 0xDFFF && b->len >= 3) {
        const uint8_t *e = (const uint8_t *)b->p + b->len - 3;
        if (e[0] == 0xED && (e[1] & 0xF0) == 0xA0) {                              /* mitad alta suelta justo antes: se unen */
            uint32_t hi = 0xD000u | ((uint32_t)(e[1] & 0x3F) << 6) | (e[2] & 0x3Fu);
            uint32_t cp = 0x10000 + ((hi - 0xD800) << 10) + (unit - 0xDC00);
            b->len -= 3; b->p[b->len] = 0;
            char t[4]; int n = wu_utf8_put(cp, t); wb_addn(b, t, (uint32_t)n); return;
        }
    }
    char t[4]; int n = wu_utf8_put(unit, t); wb_addn(b, t, (uint32_t)n);
}
