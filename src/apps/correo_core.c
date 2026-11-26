/* correo_core.c: SOMA_295_CORREO: la logica pura del correo. Ver correo_core.h. */
#include "correo_core.h"

/* ================================================================ texto */
uint32_t co_len(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
void co_cpy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; if (!cap) return; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
void co_cpyn(char *d, const char *s, uint32_t n, uint32_t cap) { uint32_t i = 0; if (!cap) return; while (s && i < n && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
void co_cat(char *d, const char *s, uint32_t cap) { uint32_t i = co_len(d), j = 0; if (!cap) return; while (s && s[j] && i + 1 < cap) d[i++] = s[j++]; if (i < cap) d[i] = 0; }
void co_num(char *d, uint32_t cap, uint64_t v) { char t[24]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); char r[24]; for (int i = 0; i < k; i++) r[i] = t[k - 1 - i]; r[k] = 0; co_cat(d, r, cap); }
static int lw(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int co_ieq(const char *a, const char *b) { while (*a && *b) { if (lw((unsigned char)*a) != lw((unsigned char)*b)) return 0; a++; b++; } return *a == *b; }
int co_iempieza(const char *s, const char *p) { while (*p) { if (lw((unsigned char)*s) != lw((unsigned char)*p)) return 0; s++; p++; } return 1; }
/* SOMA_319_CORREO_PROTO: como co_iempieza pero sin pasar de n bytes de s. Lo usan los lectores de lo que manda el servidor (IMAP,
 * SMTP): reciben (r, n) y la respuesta no tiene por que acabar en NUL. */
static int iempieza_n(const char *s, uint32_t n, const char *p)
{
    uint32_t i = 0;
    for (; p[i]; ++i) if (i >= n || lw((unsigned char)s[i]) != lw((unsigned char)p[i])) return 0;
    return 1;
}
const char *co_ibusca(const char *s, uint32_t n, const char *p)
{
    uint32_t m = co_len(p);
    if (!m) return s;
    for (uint32_t i = 0; i + m <= n; i++) {
        uint32_t k = 0; while (k < m && lw((unsigned char)s[i + k]) == lw((unsigned char)p[k])) k++;
        if (k == m) return s + i;
    }
    return 0;
}
static int es_esp(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static int es_alnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static void mcpy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void mset(void *d, int v, uint32_t n) { uint8_t *a = (uint8_t *)d; for (uint32_t i = 0; i < n; i++) a[i] = (uint8_t)v; }

/* U+00C0..U+00FF -> letra base (0 = no es letra) */
static char base_latin(uint32_t cp)
{
    static const char T[] = "aaaaaaaceeeeiiiidnooooo ouuuuyts" "aaaaaaaceeeeiiiidnooooo ouuuuyty";   /* SOMA_296_MAC: sin tamano (clang 21: -Wunterminated-string-initialization) */
    if (cp < 0xC0 || cp > 0xFF) return 0;
    char c = T[cp - 0xC0];
    return c == ' ' ? 0 : c;
}
/* lee un caracter UTF-8 (si no es valido, el byte como Latin-1) */
static uint32_t u8lee(const char *s, uint32_t n, uint32_t *len)
{
    const uint8_t *u = (const uint8_t *)s;
    if (!n) { *len = 0; return 0; }
    if (u[0] < 0x80) { *len = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && n >= 2 && (u[1] & 0xC0) == 0x80) { *len = 2; return ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F); }
    if ((u[0] & 0xF0) == 0xE0 && n >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { *len = 3; return ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
    if ((u[0] & 0xF8) == 0xF0 && n >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) { *len = 4; return ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F); }
    *len = 1; return u[0];
}
static uint32_t u8pon(uint32_t cp, char *o, uint32_t cap, uint32_t p)
{
    uint8_t b[4]; uint32_t k;
    if (cp < 0x80) { b[0] = (uint8_t)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (uint8_t)(0xC0 | (cp >> 6)); b[1] = (uint8_t)(0x80 | (cp & 0x3F)); k = 2; }
    else if (cp < 0x10000) { b[0] = (uint8_t)(0xE0 | (cp >> 12)); b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (uint8_t)(0x80 | (cp & 0x3F)); k = 3; }
    else { b[0] = (uint8_t)(0xF0 | (cp >> 18)); b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (uint8_t)(0x80 | (cp & 0x3F)); k = 4; }
    if (p + k >= cap) return p;
    for (uint32_t i = 0; i < k; i++) o[p + i] = (char)b[i];
    return p + k;
}
static int utf8_valido(const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n;) {
        if (s[i] < 0x80) { i++; continue; }
        uint32_t k = (s[i] & 0xE0) == 0xC0 ? 2 : (s[i] & 0xF0) == 0xE0 ? 3 : (s[i] & 0xF8) == 0xF0 ? 4 : 0;
        if (!k || i + k > n || s[i] == 0xC0 || s[i] == 0xC1) return 0;
        for (uint32_t j = 1; j < k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k;
    }
    return 1;
}
uint32_t co_norm(const char *s, char *out, uint32_t cap)
{
    uint32_t n = co_len(s), p = 0; int esp = 1;
    if (!cap) return 0;
    for (uint32_t i = 0; i < n && p + 1 < cap;) {
        uint32_t l; uint32_t cp = u8lee(s + i, n - i, &l);
        char c = 0;
        if (cp < 0x80) c = es_alnum((int)cp) ? (char)lw((int)cp) : (cp == '@' || cp == '.' || cp == '_' || cp == '-') ? (char)cp : ' ';
        else if (cp >= 0xC0 && cp <= 0xFF) c = base_latin(cp);
        else if (cp < 0xC0 || (cp >= 0x2000 && cp <= 0x206F) || cp == 0x20AC) c = ' ';   /* signos de Latin-1, comillas, rayas, euro */
        if (cp >= 0x80 && !c && !(cp >= 0xC0 && cp <= 0xFF)) {                /* otras letras: tal cual */
            if (p + l >= cap) break;
            for (uint32_t k = 0; k < l; k++) out[p++] = s[i + k];
            esp = 0; i += l; continue;
        }
        if (!c) c = ' ';
        if (c == ' ') { if (!esp) out[p++] = ' '; esp = 1; }
        else { out[p++] = c; esp = 0; }
        i += l;
    }
    while (p && out[p - 1] == ' ') p--;
    out[p] = 0;
    return p;
}

/* ================================================================ codificaciones */
static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
uint32_t co_b64_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t acc = 0, bits = 0, p = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] == '=') break;
        int v = b64v((unsigned char)s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (p < cap) out[p] = (uint8_t)(acc >> bits); p++; }
    }
    return p < cap ? p : cap;
}
uint32_t co_b64_enc(const uint8_t *d, uint32_t n, char *out, uint32_t cap)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t need = (n + 2) / 3 * 4;
    if (need + 1 > cap) { if (cap) out[0] = 0; return 0; }
    uint32_t p = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[p++] = A[(v >> 18) & 63]; out[p++] = A[(v >> 12) & 63];
        out[p++] = i + 1 < n ? A[(v >> 6) & 63] : '=';
        out[p++] = i + 2 < n ? A[v & 63] : '=';
    }
    out[p] = 0;
    return p;
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
uint32_t co_qp_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap, int cabecera)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < n && p < cap; i++) {
        char c = s[i];
        if (c == '=') {
            if (i + 1 < n && s[i + 1] == '\n') { i++; continue; }
            if (i + 2 < n && s[i + 1] == '\r' && s[i + 2] == '\n') { i += 2; continue; }
            int h = i + 1 < n ? hexv((unsigned char)s[i + 1]) : -1, l = i + 2 < n ? hexv((unsigned char)s[i + 2]) : -1;
            if (h >= 0 && l >= 0) { out[p++] = (uint8_t)(h * 16 + l); i += 2; continue; }
            /* "= " de espacios al final de una linea partida: suave */
            uint32_t j = i + 1; while (j < n && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < n && (s[j] == '\r' || s[j] == '\n')) { i = s[j] == '\r' && j + 1 < n && s[j + 1] == '\n' ? j + 1 : j; continue; }
            out[p++] = '='; continue;
        }
        if (cabecera && c == '_') c = ' ';
        out[p++] = (uint8_t)c;
    }
    return p;
}
uint32_t co_qp_enc(const char *s, uint32_t n, char *out, uint32_t cap)
{
    static const char H[] = "0123456789ABCDEF";
    uint32_t p = 0, col = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (p + 2 >= cap) return 0;
            out[p++] = '\r'; out[p++] = '\n'; col = 0; continue;
        }
        int fin = i + 1 >= n || s[i + 1] == '\n' || (s[i + 1] == '\r');
        char t[3]; uint32_t k;
        if ((c >= 33 && c <= 126 && c != '=') || ((c == ' ' || c == '\t') && !fin)) { t[0] = (char)c; k = 1; }
        else { t[0] = '='; t[1] = H[c >> 4]; t[2] = H[c & 15]; k = 3; }
        if (col + k > 75) { if (p + 3 >= cap) return 0; out[p++] = '='; out[p++] = '\r'; out[p++] = '\n'; col = 0; }
        if (p + k >= cap) return 0;
        for (uint32_t j = 0; j < k; j++) out[p++] = t[j];
        col += k;
    }
    if (p < cap) out[p] = 0;
    return p;
}
static const uint16_t W1252[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
uint32_t co_a_utf8(const uint8_t *s, uint32_t n, const char *charset, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!cap) return 0;
    char cs[32]; co_cpy(cs, charset ? charset : "", sizeof cs);
    for (uint32_t i = 0; cs[i]; i++) cs[i] = (char)lw((unsigned char)cs[i]);
    int utf = !cs[0] || co_ieq(cs, "utf-8") || co_ieq(cs, "utf8") || co_ieq(cs, "us-ascii") || co_ieq(cs, "ascii");
    int l15 = co_ieq(cs, "iso-8859-15") || co_ieq(cs, "latin9");
    if (utf && utf8_valido(s, n)) {
        for (uint32_t i = 0; i < n && p + 1 < cap; i++) out[p++] = (char)s[i];
        out[p] = 0; return p;
    }
    if (!utf && !l15 && !co_iempieza(cs, "iso-8859-1") && !co_iempieza(cs, "windows-125") && !co_iempieza(cs, "cp125") && !co_ieq(cs, "latin1")
        && utf8_valido(s, n)) {                                          /* otro juego que ya viene en UTF-8 (o ASCII) */
        for (uint32_t i = 0; i < n && p + 1 < cap; i++) out[p++] = (char)s[i];
        out[p] = 0; return p;
    }
    for (uint32_t i = 0; i < n; i++) {                                   /* Latin-1, Latin-9 o Windows-1252 (y UTF-8 roto) */
        uint32_t cp = s[i];
        if (cp >= 0x80 && cp < 0xA0) cp = W1252[cp - 0x80];
        else if (l15) { switch (cp) { case 0xA4: cp = 0x20AC; break; case 0xA6: cp = 0x160; break; case 0xA8: cp = 0x161; break; case 0xB4: cp = 0x17D; break;
                                      case 0xB8: cp = 0x17E; break; case 0xBC: cp = 0x152; break; case 0xBD: cp = 0x153; break; case 0xBE: cp = 0x178; break; } }
        uint32_t q = u8pon(cp, out, cap, p);
        if (q == p && cp) break;
        p = q;
    }
    out[p] = 0;
    return p;
}
/* una palabra codificada =?cs?e?txt?= en s[i..]; devuelve su longitud (0 si no lo es) y la decodifica en out */
static uint32_t palabra_cod(const char *s, uint32_t n, char *out, uint32_t cap, uint32_t *escrito)
{
    *escrito = 0;
    if (n < 8 || s[0] != '=' || s[1] != '?') return 0;
    uint32_t a = 2; while (a < n && s[a] != '?' && !es_esp((unsigned char)s[a])) a++;
    if (a + 3 >= n || s[a] != '?') return 0;
    char e = (char)lw((unsigned char)s[a + 1]);
    if ((e != 'b' && e != 'q') || s[a + 2] != '?') return 0;
    uint32_t t0 = a + 3, t = t0; while (t + 1 < n && !(s[t] == '?' && s[t + 1] == '=')) { if (s[t] == '\r' || s[t] == '\n') return 0; t++; }
    if (t + 1 >= n) return 0;
    char cs[40]; co_cpyn(cs, s + 2, a - 2, sizeof cs);
    for (uint32_t k = 0; cs[k]; k++) if (cs[k] == '*') { cs[k] = 0; break; }   /* RFC 2231: charset*idioma */
    uint8_t tmp[600]; uint32_t m;
    if (e == 'b') m = co_b64_dec(s + t0, t - t0, tmp, sizeof tmp);
    else m = co_qp_dec(s + t0, t - t0, tmp, sizeof tmp, 1);
    *escrito = co_a_utf8(tmp, m, cs, out, cap);
    return t + 2;
}
uint32_t co_cab_dec(const char *s, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0; int ult_cod = 0;
    if (!cap) return 0;
    char *crudo = (char *)co_alloc(n + 1); uint32_t cn = 0;            /* desplegado */
    if (!crudo) { out[0] = 0; return 0; }
    for (uint32_t i = 0; i < n; i++) { if (s[i] == '\r' || s[i] == '\n') continue; crudo[cn++] = s[i] == '\t' ? ' ' : s[i]; }
    uint32_t i = 0;
    while (i < cn && crudo[i] == ' ') i++;
    while (i < cn && p + 1 < cap) {
        uint32_t esc, l = palabra_cod(crudo + i, cn - i, out + p, cap - p, &esc);
        if (l) { p += esc; i += l; ult_cod = 1; continue; }
        if (crudo[i] == ' ' && ult_cod) {                                 /* espacio entre dos palabras codificadas: no cuenta */
            uint32_t j = i; while (j < cn && crudo[j] == ' ') j++;
            uint32_t e2; char tmp[4];
            if (j < cn && palabra_cod(crudo + j, cn - j, tmp, 1, &e2)) { i = j; continue; }
        }
        ult_cod = 0;
        uint32_t j = i; while (j < cn && !(crudo[j] == '=' && j + 1 < cn && crudo[j + 1] == '?')) j++;   /* trozo normal */
        if (j == i) j = i + 1;
        uint32_t w = co_a_utf8((const uint8_t *)crudo + i, j - i, "utf-8", out + p, cap - p);
        p += w; i = j;
    }
    co_free(crudo);
    while (p && out[p - 1] == ' ') p--;
    out[p] = 0;
    return p;
}
uint32_t co_cab_enc(const char *u, char *out, uint32_t cap)
{
    uint32_t n = co_len(u); int ascii = 1;
    for (uint32_t i = 0; i < n; i++) if ((uint8_t)u[i] >= 0x80 || u[i] == '\r' || u[i] == '\n') ascii = 0;
    if (ascii && n < 900) { if (n + 1 > cap) return 0; co_cpy(out, u, cap); return n; }
    uint32_t p = 0, i = 0;
    while (i < n) {
        uint32_t take = 0;                                               /* hasta 45 bytes sin partir un caracter */
        while (i + take < n) {
            uint32_t l; (void)u8lee(u + i + take, n - i - take, &l);
            if (take + l > 45) break;
            take += l;
        }
        if (!take) take = 1;
        uint8_t raw[48]; for (uint32_t k = 0; k < take; k++) raw[k] = (uint8_t)(u[i + k] == '\r' || u[i + k] == '\n' ? ' ' : u[i + k]);
        char b[72]; uint32_t bl = co_b64_enc(raw, take, b, sizeof b);
        if (p + bl + 16 >= cap) { if (cap) out[p < cap ? p : cap - 1] = 0; return 0; }
        if (p) { out[p++] = '\r'; out[p++] = '\n'; out[p++] = ' '; }
        const char *pre = "=?UTF-8?B?"; while (*pre) out[p++] = *pre++;
        for (uint32_t k = 0; k < bl; k++) out[p++] = b[k];
        out[p++] = '?'; out[p++] = '=';
        i += take;
    }
    out[p] = 0;
    return p;
}
uint32_t co_utf7_dec(const char *s, char *out, uint32_t cap)
{
    uint32_t p = 0, n = co_len(s);
    if (!cap) return 0;
    for (uint32_t i = 0; i < n && p + 1 < cap;) {
        if (s[i] != '&') { out[p++] = s[i++]; continue; }
        if (i + 1 < n && s[i + 1] == '-') { out[p++] = '&'; i += 2; continue; }
        uint32_t j = i + 1; uint32_t acc = 0, bits = 0, hi = 0;
        while (j < n && s[j] != '-') {
            int v = s[j] == ',' ? 63 : b64v((unsigned char)s[j]);
            if (v < 0 || s[j] == '_' || s[j] == '-') break;
            acc = (acc << 6) | (uint32_t)v; bits += 6;
            if (bits >= 16) {
                bits -= 16; uint32_t u = (acc >> bits) & 0xFFFF;
                if (u >= 0xD800 && u < 0xDC00) hi = u;
                else { uint32_t cp = u; if (u >= 0xDC00 && u < 0xE000 && hi) { cp = 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00); hi = 0; } p = u8pon(cp, out, cap, p); }
            }
            j++;
        }
        i = j < n && s[j] == '-' ? j + 1 : j;
    }
    out[p] = 0;
    return p;
}
uint32_t co_utf7_enc(const char *u, char *out, uint32_t cap)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
    uint32_t p = 0, n = co_len(u);
    if (!cap) return 0;
    for (uint32_t i = 0; i < n;) {
        uint8_t c = (uint8_t)u[i];
        if (c >= 0x20 && c < 0x7F) {
            if (c == '&') { if (p + 3 >= cap) break; out[p++] = '&'; out[p++] = '-'; }
            else { if (p + 2 >= cap) break; out[p++] = (char)c; }
            i++; continue;
        }
        if (p + 2 >= cap) break;
        out[p++] = '&';
        uint32_t acc = 0, bits = 0;
        while (i < n && !((uint8_t)u[i] >= 0x20 && (uint8_t)u[i] < 0x7F)) {
            uint32_t l, cp = u8lee(u + i, n - i, &l); i += l;
            uint32_t units[2]; int nu = 0;
            if (cp >= 0x10000) { cp -= 0x10000; units[nu++] = 0xD800 + (cp >> 10); units[nu++] = 0xDC00 + (cp & 0x3FF); } else units[nu++] = cp;
            for (int k = 0; k < nu; k++) {
                acc = (acc << 16) | units[k]; bits += 16;
                while (bits >= 6) { bits -= 6; if (p + 3 >= cap) { out[p] = 0; return p; } out[p++] = A[(acc >> bits) & 63]; }
            }
        }
        if (bits) { if (p + 3 >= cap) break; out[p++] = A[(acc << (6 - bits)) & 63]; }
        out[p++] = '-';
    }
    out[p < cap ? p : cap - 1] = 0;
    return p;
}

/* entidades HTML con nombre que salen en el correo en castellano */
static uint32_t entidad(const char *s, uint32_t n, uint32_t *len)
{
    static const struct { const char *e; uint16_t cp; } E[] = {
        { "nbsp", 0xA0 }, { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
        { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 }, { "uuml", 0xFC },
        { "Aacute", 0xC1 }, { "Eacute", 0xC9 }, { "Iacute", 0xCD }, { "Oacute", 0xD3 }, { "Uacute", 0xDA }, { "Ntilde", 0xD1 }, { "Uuml", 0xDC },
        { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "euro", 0x20AC }, { "copy", 0xA9 }, { "reg", 0xAE }, { "laquo", 0xAB }, { "raquo", 0xBB },
        { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 }, { "ldquo", 0x201C },
        { "rdquo", 0x201D }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "ordm", 0xBA }, { "ordf", 0xAA }, { "deg", 0xB0 }, { "agrave", 0xE0 },
        { "egrave", 0xE8 }, { "ccedil", 0xE7 }, { "zwnj", 0x200C }, { "shy", 0xAD }, { 0, 0 } };
    *len = 0;
    if (n < 3 || s[0] != '&') return 0;
    if (s[1] == '#') {
        uint32_t i = 2, v = 0; int hex = 0, d = 0;
        if (i < n && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
        while (i < n && i < 12) { int h = hexv((unsigned char)s[i]); if (h < 0 || (!hex && h > 9)) break; v = v * (hex ? 16u : 10u) + (uint32_t)h; i++; d++; }
        if (!d || v > 0x10FFFF) return 0;
        if (i < n && s[i] == ';') i++;
        *len = i; return v ? v : ' ';
    }
    for (int k = 0; E[k].e; k++) {
        uint32_t l = co_len(E[k].e);
        if (l + 1 < n && s[1 + l] == ';') { uint32_t j = 0; while (j < l && s[1 + j] == E[k].e[j]) j++; if (j == l) { *len = l + 2; return E[k].cp; } }
    }
    return 0;
}
uint32_t co_html_texto(const char *h, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0; int esp = 1, saltos = 2;                   /* lo ultimo escrito era espacio / cuantos saltos seguidos */
    if (!cap) return 0;
#define PONC(c) do { if (p + 1 < cap) out[p++] = (char)(c); } while (0)
    for (uint32_t i = 0; i < n && p + 4 < cap;) {
        char c = h[i];
        if (c == '<') {
            if (i + 3 < n && h[i + 1] == '!' && h[i + 2] == '-' && h[i + 3] == '-') {   /* comentario */
                const char *e = co_ibusca(h + i + 4, n - i - 4, "-->"); i = e ? (uint32_t)(e - h) + 3 : n; continue;
            }
            uint32_t j = i + 1; int cierre = 0;
            if (j < n && h[j] == '/') { cierre = 1; j++; }
            char tag[12]; uint32_t t = 0;
            while (j < n && t < sizeof tag - 1 && es_alnum((unsigned char)h[j])) tag[t++] = (char)lw((unsigned char)h[j++]);
            tag[t] = 0;
            uint32_t k = j; char q = 0;
            while (k < n && (q || h[k] != '>')) { if (q) { if (h[k] == q) q = 0; } else if (h[k] == '"' || h[k] == '\'') q = h[k]; k++; }
            i = k < n ? k + 1 : n;
            if (!cierre && (co_ieq(tag, "style") || co_ieq(tag, "script") || co_ieq(tag, "head") || co_ieq(tag, "title"))) {
                char fin[16] = "</"; co_cat(fin, tag, sizeof fin);
                const char *e = co_ibusca(h + i, n - i, fin);
                if (e) { i = (uint32_t)(e - h); const char *g = co_ibusca(h + i, n - i, ">"); i = g ? (uint32_t)(g - h) + 1 : n; }
                else i = n;
                continue;
            }
            int bloque = co_ieq(tag, "br") || co_ieq(tag, "p") || co_ieq(tag, "div") || co_ieq(tag, "tr") || co_ieq(tag, "li") || co_ieq(tag, "table")
                      || co_ieq(tag, "ul") || co_ieq(tag, "ol") || co_ieq(tag, "blockquote") || co_ieq(tag, "hr") || (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2]);
            if (bloque) {
                int dos = !co_ieq(tag, "br") && !co_ieq(tag, "li") && !co_ieq(tag, "tr") && !co_ieq(tag, "div");
                while (p && out[p - 1] == ' ') p--;
                if (co_ieq(tag, "br")) { if (saltos < 2 && p) { PONC('\n'); saltos++; } }   /* <br><br> deja una linea en blanco */
                else { int quiere = dos ? 2 : 1; while (saltos < quiere && p) { PONC('\n'); saltos++; } }
                esp = 1;
                if (!cierre && co_ieq(tag, "li")) { p = u8pon(0x2022, out, cap, p); PONC(' '); saltos = 0; }
                if (!cierre && co_ieq(tag, "hr")) { for (int z = 0; z < 10; z++) PONC('-'); PONC('\n'); saltos = 1; }
            } else if (co_ieq(tag, "td") || co_ieq(tag, "th")) { if (!esp) { PONC(' '); esp = 1; } }
            continue;
        }
        uint32_t cp, l;
        if (c == '&' && (cp = entidad(h + i, n - i, &l)) != 0) { i += l; }
        else { cp = u8lee(h + i, n - i, &l); i += l; }
        if (cp == 0xA0 || cp == 0x200C || cp == 0xAD) { if (cp == 0xA0) cp = ' '; else continue; }
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n') { if (!esp) { PONC(' '); esp = 1; } continue; }
        p = u8pon(cp, out, cap, p); esp = 0; saltos = 0;
    }
#undef PONC
    while (p && (out[p - 1] == ' ' || out[p - 1] == '\n')) p--;
    out[p] = 0;
    /* espacios al principio de las lineas */
    uint32_t w = 0; int ini = 1;
    for (uint32_t r = 0; r < p; r++) { if (ini && out[r] == ' ') continue; ini = out[r] == '\n'; out[w++] = out[r]; }
    out[w] = 0;
    return w;
}

/* ================================================================ cabeceras */
static uint32_t fin_cab(const char *m, uint32_t n)
{
    for (uint32_t i = 0; i + 1 < n; i++) {
        if (m[i] == '\n' && m[i + 1] == '\n') return i + 1;
        if (m[i] == '\n' && m[i + 1] == '\r' && i + 2 < n && m[i + 2] == '\n') return i + 1;
    }
    return n;
}
int co_cabecera(const char *m, uint32_t n, const char *nombre, char *out, uint32_t cap)
{
    uint32_t fin = fin_cab(m, n), nl = co_len(nombre);
    if (cap) out[0] = 0;
    for (uint32_t i = 0; i < fin;) {
        uint32_t e = i; while (e < fin && m[e] != '\n') e++;
        if (m[i] != ' ' && m[i] != '\t' && e - i > nl && m[i + nl] == ':') {
            uint32_t k = 0; while (k < nl && lw((unsigned char)m[i + k]) == lw((unsigned char)nombre[k])) k++;
            if (k == nl) {
                uint32_t p = 0, j = i + nl + 1;
                while (j < fin && (m[j] == ' ' || m[j] == '\t')) j++;
                for (;;) {
                    while (j < fin && m[j] != '\n') { if (m[j] != '\r' && p + 1 < cap) out[p++] = m[j]; j++; }
                    if (j + 1 < fin && (m[j + 1] == ' ' || m[j + 1] == '\t')) { j++; continue; }
                    break;
                }
                while (p && (out[p - 1] == ' ' || out[p - 1] == '\t')) p--;
                if (cap) out[p < cap ? p : cap - 1] = 0;
                return (int)p;
            }
        }
        i = e + 1;
    }
    return -1;
}
static uint32_t pct_dec(const char *s, uint32_t n, uint8_t *o, uint32_t cap)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < n && p < cap; i++) {
        if (s[i] == '%' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) { o[p++] = (uint8_t)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2])); i += 2; }
        else o[p++] = (uint8_t)s[i];
    }
    return p;
}
int co_param(const char *v, const char *param, char *out, uint32_t cap)
{
    uint32_t n = co_len(v), pl = co_len(param), op = 0; int hallado = 0;
    char cs[40] = ""; uint8_t *acc = (uint8_t *)co_alloc(1024); uint32_t an = 0; int ext = 0;
    if (cap) out[0] = 0;
    if (!acc) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (v[i] == '"') { i++; while (i < n && v[i] != '"') { if (v[i] == '\\') i++; i++; } continue; }
        if (v[i] != ';') continue;
        uint32_t j = i + 1; while (j < n && es_esp((unsigned char)v[j])) j++;
        uint32_t k = 0; while (k < pl && j + k < n && lw((unsigned char)v[j + k]) == lw((unsigned char)param[k])) k++;
        if (k < pl) continue;
        uint32_t q = j + pl; int star = 0, seccion = 0;
        if (q < n && v[q] == '*') {                                        /* RFC 2231: nombre*= / nombre*0= / nombre*0*= */
            q++;
            if (q < n && v[q] >= '0' && v[q] <= '9') { seccion = 1; while (q < n && v[q] >= '0' && v[q] <= '9') q++; if (q < n && v[q] == '*') { star = 1; q++; } }
            else star = 1;
        }
        while (q < n && es_esp((unsigned char)v[q])) q++;
        if (q >= n || v[q] != '=') continue;
        if (hallado && !seccion) continue;                                  /* el primero que vale; las secciones se juntan */
        q++; while (q < n && es_esp((unsigned char)v[q])) q++;
        char val[600]; uint32_t vl = 0;
        if (q < n && v[q] == '"') { q++; while (q < n && v[q] != '"') { if (v[q] == '\\' && q + 1 < n) q++; if (vl + 1 < sizeof val) val[vl++] = v[q]; q++; } if (q < n) q++; }
        else while (q < n && v[q] != ';' && !es_esp((unsigned char)v[q])) { if (vl + 1 < sizeof val) val[vl++] = v[q]; q++; }
        val[vl] = 0;
        if (star) {
            ext = 1; uint32_t s = 0;
            if (!an) {                                                      /* charset'idioma'texto en el primero */
                uint32_t a1 = 0; while (a1 < vl && val[a1] != '\'') a1++;
                if (a1 < vl) { co_cpyn(cs, val, a1, sizeof cs); uint32_t a2 = a1 + 1; while (a2 < vl && val[a2] != '\'') a2++; s = a2 < vl ? a2 + 1 : 0; }
            }
            an += pct_dec(val + s, vl - s, acc + an, 1023 - an);
        } else { for (uint32_t z = 0; z < vl && an < 1023; z++) acc[an++] = (uint8_t)val[z]; }
        hallado = 1;
        i = q - 1;
    }
    if (hallado) {
        if (ext) op = co_a_utf8(acc, an, cs[0] ? cs : "utf-8", out, cap);
        else { acc[an] = 0; op = co_cab_dec((const char *)acc, an, out, cap); }
    }
    co_free(acc);
    return hallado ? (int)op : -1;
}
static void recorta(char *s)
{
    uint32_t n = co_len(s), a = 0;
    while (a < n && (es_esp((unsigned char)s[a]) || s[a] == '"')) a++;
    while (n > a && (es_esp((unsigned char)s[n - 1]) || s[n - 1] == '"')) n--;
    uint32_t k = 0; for (uint32_t i = a; i < n; i++) s[k++] = s[i];
    s[k] = 0;
}
int co_email_valido(const char *e)
{
    uint32_t n = co_len(e), at = 0, ats = 0, punto = 0;
    if (n < 3 || n > 119) return 0;
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)e[i];
        if (c == '@') { ats++; at = i; continue; }
        if (c <= ' ' || c == '<' || c == '>' || c == ',' || c == ';' || c == '"' || c == '(' || c == ')' || c == '[' || c == ']' || c == '\\') return 0;
        if (ats && c == '.') punto = i;
    }
    return ats == 1 && at > 0 && punto > at + 1 && punto + 1 < n;
}
int co_dirs(const char *s, struct co_dir *out, int max)
{
    uint32_t n = co_len(s); int k = 0;
    uint32_t i = 0;
    while (i < n && k < max) {
        uint32_t a = i; int q = 0, ang = 0, par = 0;
        while (i < n) {                                                     /* hasta la coma de fuera */
            char c = s[i];
            if (q) { if (c == '\\') i++; else if (c == '"') q = 0; }
            else if (c == '"') q = 1;
            else if (c == '<') ang = 1;
            else if (c == '>') ang = 0;
            else if (c == '(') par++;
            else if (c == ')' && par) par--;
            else if ((c == ',' || c == ';') && !ang && !par) break;
            i++;
        }
        uint32_t b = i; i++;
        char item[400]; co_cpyn(item, s + a, b - a, sizeof item);
        { uint32_t g = 0; int qq = 0; while (item[g]) { if (item[g] == '"') qq = !qq; if (item[g] == ':' && !qq && !co_ibusca(item, g, "<")) break; g++; }   /* grupo "nombre:" */
          if (item[g] == ':') { uint32_t z = 0; g++; while (item[g]) item[z++] = item[g++]; item[z] = 0; } }
        struct co_dir *d = &out[k]; d->nombre[0] = d->email[0] = 0;
        const char *lt = 0, *gt = 0; int qq = 0;
        for (const char *p = item; *p; p++) { if (*p == '"') qq = !qq; if (!qq && *p == '<') lt = p; if (lt && *p == '>') gt = p; }
        if (lt && gt && gt > lt) {
            co_cpyn(d->email, lt + 1, (uint32_t)(gt - lt - 1), sizeof d->email); recorta(d->email);
            char nom[300]; co_cpyn(nom, item, (uint32_t)(lt - item), sizeof nom);
            char dec[300]; co_cab_dec(nom, co_len(nom), dec, sizeof dec);
            uint32_t w = 0; for (uint32_t z = 0; dec[z]; z++) { if (dec[z] == '\\' && dec[z + 1]) z++; dec[w++] = dec[z]; } dec[w] = 0;
            recorta(dec); co_cpy(d->nombre, dec, sizeof d->nombre);
        } else {
            char t[300]; uint32_t w = 0; char com[200] = ""; uint32_t cw = 0; int en = 0;
            for (const char *p = item; *p && w + 1 < sizeof t; p++) { if (*p == '(') { en++; continue; } if (*p == ')') { if (en) en--; continue; } if (en) { if (cw + 1 < sizeof com) com[cw++] = *p; continue; } t[w++] = *p; }
            t[w] = 0; com[cw] = 0;
            recorta(t); co_cpy(d->email, t, sizeof d->email);
            if (cw) { char dec[200]; co_cab_dec(com, cw, dec, sizeof dec); recorta(dec); co_cpy(d->nombre, dec, sizeof d->nombre); }
        }
        for (char *p = d->email; *p; p++) if (*p == ' ') { *p = 0; break; }
        if (d->email[0] && co_ibusca(d->email, co_len(d->email), "@")) k++;
    }
    return k;
}
void co_dir_txt(const struct co_dir *d, char *out, uint32_t cap)
{
    if (!d->nombre[0] || co_ieq(d->nombre, d->email)) { co_cpy(out, d->email, cap); return; }
    co_cpy(out, d->nombre, cap); co_cat(out, " <", cap); co_cat(out, d->email, cap); co_cat(out, ">", cap);
}

/* ================================================================ fechas */
static int64_t dias_civil(int64_t y, int m, int d)
{
    y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil(int64_t z, int *y, int *m, int *d)
{
    z += 719468; int64_t era = (z >= 0 ? z : z - 146096) / 146097; int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yoe + era * 400 + (*m <= 2));
}
static const char *const MESES[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static const char *const MESES_ES[12] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };
int64_t co_fecha(const char *s)
{
    uint32_t n = co_len(s), i = 0;
    int dia = -1, mes = -1, anio = -1, h = 0, mi = 0, se = 0, zona = 0, hay_hora = 0;
    while (i < n) {
        while (i < n && (es_esp((unsigned char)s[i]) || s[i] == ',' || s[i] == '-' || s[i] == '"')) {
            if (s[i] == '-' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9' && hay_hora && anio >= 0) break;   /* -0500 */
            i++;
        }
        if (i >= n) break;
        if (s[i] == '(') { while (i < n && s[i] != ')') i++; i++; continue; }
        uint32_t a = i;
        if (s[i] == '+' || s[i] == '-') {
            int sg = s[i] == '-' ? -1 : 1; i++; uint32_t v = 0, d = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9' && d < 4) { v = v * 10 + (uint32_t)(s[i++] - '0'); d++; }
            if (d == 4) zona = sg * (int)((v / 100) * 60 + v % 100);
            continue;
        }
        if (s[i] >= '0' && s[i] <= '9') {
            uint32_t v = 0, d = 0; while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (uint32_t)(s[i++] - '0'); d++; }
            if (i < n && s[i] == ':') {                                    /* hh:mm[:ss] */
                h = (int)v; i++; uint32_t m2 = 0; while (i < n && s[i] >= '0' && s[i] <= '9') m2 = m2 * 10 + (uint32_t)(s[i++] - '0');
                mi = (int)m2; if (i < n && s[i] == ':') { i++; uint32_t s2 = 0; while (i < n && s[i] >= '0' && s[i] <= '9') s2 = s2 * 10 + (uint32_t)(s[i++] - '0'); se = (int)s2; }
                hay_hora = 1; continue;
            }
            if (dia < 0 && d <= 2) dia = (int)v;
            else if (anio < 0) anio = (int)(d <= 2 ? (v < 50 ? 2000 + v : 1900 + v) : v);
            continue;
        }
        char w[8]; uint32_t k = 0; while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) { if (k < 7) w[k++] = (char)lw((unsigned char)s[i]); i++; }
        w[k] = 0;
        if (i == a) { i++; continue; }
        if (k >= 3) for (int z = 0; z < 12; z++) if (w[0] == MESES[z][0] && w[1] == MESES[z][1] && w[2] == MESES[z][2]) mes = z + 1;
        if (co_ieq(w, "edt")) zona = -240; else if (co_ieq(w, "est") || co_ieq(w, "cdt")) zona = -300; else if (co_ieq(w, "cst") || co_ieq(w, "mdt")) zona = -360;
        else if (co_ieq(w, "mst") || co_ieq(w, "pdt")) zona = -420; else if (co_ieq(w, "pst")) zona = -480; else if (co_ieq(w, "cet")) zona = zona ? zona : 60; else if (co_ieq(w, "cest")) zona = zona ? zona : 120;
    }
    if (dia < 1 || dia > 31 || mes < 1 || anio < 1970 || anio > 9999 || h > 23 || mi > 59 || se > 60) return 0;
    return dias_civil(anio, mes, dia) * 1440 + h * 60 + mi - zona;
}
static int64_t ultimo_domingo(int y, int m)                         /* dia (desde 1970) del ultimo domingo del mes */
{
    int dm = m == 3 || m == 10 ? 31 : 30;
    int64_t d = dias_civil(y, m, dm);
    int ds = (int)((d % 7 + 7 + 3) % 7);                             /* 0 lunes .. 6 domingo (1970-01-01 fue jueves) */
    return d - ((ds + 1) % 7);
}
int co_desfase(int64_t utc_min)
{
    int y, m, d; civil(utc_min / 1440, &y, &m, &d);
    int64_t ini = ultimo_domingo(y, 3) * 1440 + 60, fin = ultimo_domingo(y, 10) * 1440 + 60;
    return utc_min >= ini && utc_min < fin ? 120 : 60;
}
void co_fecha_rfc(int64_t utc_min, int off, char *out, uint32_t cap)
{
    static const char *const DS[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    static const char *const MS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int64_t l = utc_min + off, dd = l / 1440; int y, m, d; civil(dd, &y, &m, &d);
    int ds = (int)((dd % 7 + 7 + 3) % 7), h = (int)((l % 1440) / 60), mi = (int)(l % 60);
    out[0] = 0;
    co_cat(out, DS[ds], cap); co_cat(out, ", ", cap); co_num(out, cap, (uint64_t)d); co_cat(out, " ", cap); co_cat(out, MS[m - 1], cap); co_cat(out, " ", cap);
    co_num(out, cap, (uint64_t)y); co_cat(out, " ", cap);
    char t[16] = { (char)('0' + h / 10), (char)('0' + h % 10), ':', (char)('0' + mi / 10), (char)('0' + mi % 10), ':', '0', '0', ' ', off < 0 ? '-' : '+', 0 };
    co_cat(out, t, cap);
    int a = off < 0 ? -off : off;
    char z[5] = { (char)('0' + a / 600), (char)('0' + a / 60 % 10), (char)('0' + a % 60 / 10), (char)('0' + a % 10), 0 };
    co_cat(out, z, cap);
}

/* ================================================================ mensajes MIME */
static uint32_t ini_cuerpo(const char *m, uint32_t n)               /* donde empieza el cuerpo (tras la linea en blanco) */
{
    if (n && m[0] == '\n') return 1;
    if (n > 1 && m[0] == '\r' && m[1] == '\n') return 2;
    uint32_t f = fin_cab(m, n);
    if (f >= n) return n;
    while (f < n && m[f] != '\n') f++;
    return f < n ? f + 1 : n;
}
static int cte_de(const char *v) { return co_ibusca(v, co_len(v), "base64") ? CO_CTE_B64 : co_ibusca(v, co_len(v), "quoted") ? CO_CTE_QP : CO_CTE_7BIT; }
static uint32_t decodifica(const char *s, uint32_t n, int cte, uint8_t *out, uint32_t cap)
{
    if (cte == CO_CTE_B64) return co_b64_dec(s, n, out, cap);
    if (cte == CO_CTE_QP) return co_qp_dec(s, n, out, cap, 0);
    uint32_t k = n < cap ? n : cap; mcpy(out, s, k); return k;
}
struct ptxt { uint32_t off, len; int cte, html; char cs[32]; };
struct mctx { const char *raw; uint32_t n; struct co_mime *m; struct ptxt t[16]; int nt; };
static void tipo_de(const char *ct, char *tipo, uint32_t cap)
{
    uint32_t i = 0, k = 0; while (ct[i] == ' ') i++;
    while (ct[i] && ct[i] != ';' && !es_esp((unsigned char)ct[i]) && k + 1 < cap) tipo[k++] = (char)lw((unsigned char)ct[i++]);
    tipo[k] = 0;
    if (!k) co_cpy(tipo, "text/plain", cap);
}
static void parte(struct mctx *c, uint32_t a, uint32_t b, int prof)
{
    const char *p = c->raw + a; uint32_t pn = b - a;
    uint32_t cb = ini_cuerpo(p, pn);
    char *ct = (char *)co_alloc(1024), *cd = (char *)co_alloc(512), *cte = (char *)co_alloc(64);
    if (!ct || !cd || !cte) { co_free(ct); co_free(cd); co_free(cte); return; }
    if (cb == 0 || co_cabecera(p, cb, "Content-Type", ct, 1024) < 0) ct[0] = 0;
    if (cb == 0 || co_cabecera(p, cb, "Content-Disposition", cd, 512) < 0) cd[0] = 0;
    if (cb == 0 || co_cabecera(p, cb, "Content-Transfer-Encoding", cte, 64) < 0) cte[0] = 0;
    char tipo[64]; tipo_de(ct, tipo, sizeof tipo);
    char nombre[96] = ""; char tmp[200];
    if (co_param(cd, "filename", tmp, sizeof tmp) > 0 || co_param(ct, "name", tmp, sizeof tmp) > 0) co_cpy(nombre, tmp, sizeof nombre);
    int adjunto = co_iempieza(cd, "attachment") || nombre[0];
    if (co_iempieza(tipo, "multipart/") && prof < 10) {
        char bnd[120];
        if (co_param(ct, "boundary", bnd, sizeof bnd) > 0) {
            uint32_t bl = co_len(bnd), i = cb, ini = 0; int dentro = 0, cerrado = 0;
            while (i < pn) {
                uint32_t e = i; while (e < pn && p[e] != '\n') e++;
                if (e - i >= bl + 2 && p[i] == '-' && p[i + 1] == '-') {
                    uint32_t k = 0; while (k < bl && p[i + 2 + k] == bnd[k]) k++;
                    if (k == bl) {
                        int fin = e - i >= bl + 4 && p[i + 2 + bl] == '-' && p[i + 3 + bl] == '-';
                        if (dentro) {
                            uint32_t f = i; if (f > ini && p[f - 1] == '\n') f--; if (f > ini && p[f - 1] == '\r') f--;
                            parte(c, a + ini, a + f, prof + 1);
                        }
                        if (fin) { cerrado = 1; break; }
                        dentro = 1; ini = e + 1;
                    }
                }
                i = e + 1;
            }
            if (dentro && !cerrado) { c->m->truncado = 1; if (ini < pn) parte(c, a + ini, b, prof + 1); }
        }
        co_free(ct); co_free(cd); co_free(cte);
        return;
    }
    int html = co_ieq(tipo, "text/html");
    if (!adjunto && (co_ieq(tipo, "text/plain") || html || co_ieq(tipo, "text/enriched")) && c->nt < 16) {
        struct ptxt *t = &c->t[c->nt++];
        t->off = a + cb; t->len = pn - cb; t->cte = cte_de(cte); t->html = html;
        if (co_param(ct, "charset", t->cs, sizeof t->cs) < 0) t->cs[0] = 0;
    } else if (c->m->nadj < CO_ADJ_MAX && pn > cb) {
        struct co_adj *j = &c->m->adj[c->m->nadj++];
        mset(j, 0, sizeof *j);
        if (!nombre[0]) {
            if (co_ieq(tipo, "message/rfc822")) co_cpy(nombre, "mensaje.eml", sizeof nombre);
            else if (co_iempieza(tipo, "image/")) { co_cpy(nombre, "imagen.", sizeof nombre); co_cat(nombre, tipo + 6, sizeof nombre); }
            else if (co_ieq(tipo, "text/calendar")) co_cpy(nombre, "invitacion.ics", sizeof nombre);
            else co_cpy(nombre, "adjunto", sizeof nombre);
        }
        for (char *z = nombre; *z; z++) if (*z == '/' || *z == '\\') *z = '_';
        co_cpy(j->nombre, nombre, sizeof j->nombre); co_cpy(j->tipo, tipo, sizeof j->tipo);
        j->off = a + cb; j->len = pn - cb; j->cte = cte_de(cte);
        uint32_t util = 0; for (uint32_t z = cb; z < pn; z++) if (!es_esp((unsigned char)p[z])) util++;
        j->tam = j->cte == CO_CTE_B64 ? util * 3 / 4 : pn - cb;
        char cid[120];
        j->en_linea = co_iempieza(cd, "inline") && co_cabecera(p, cb, "Content-ID", cid, sizeof cid) > 0 && co_iempieza(tipo, "image/");
    }
    co_free(ct); co_free(cd); co_free(cte);
}
static uint32_t visibles(const char *s, uint32_t n) { uint32_t v = 0; for (uint32_t i = 0; i < n; i++) if (!es_esp((unsigned char)s[i])) v++; return v; }
int co_mime_lee(const char *raw, uint32_t n, struct co_mime *m, char *texto, uint32_t cap)
{
    mset(m, 0, sizeof *m);
    if (cap) texto[0] = 0;
    if (!raw || !n) return 0;
    uint32_t cb = ini_cuerpo(raw, n);
    char *v = (char *)co_alloc(2048); if (!v) return 0;
    if (co_cabecera(raw, cb, "Subject", v, 2048) >= 0) co_cab_dec(v, co_len(v), m->asunto, sizeof m->asunto);
    if (co_cabecera(raw, cb, "From", v, 2048) >= 0) co_cpy(m->de, v, sizeof m->de);
    if (co_cabecera(raw, cb, "To", v, 2048) >= 0) co_cpy(m->para, v, sizeof m->para);
    if (co_cabecera(raw, cb, "Cc", v, 2048) >= 0) co_cpy(m->cc, v, sizeof m->cc);
    if (co_cabecera(raw, cb, "Reply-To", v, 2048) >= 0) co_cpy(m->responder_a, v, sizeof m->responder_a);
    if (co_cabecera(raw, cb, "Message-ID", v, 2048) >= 0) co_cpy(m->msgid, v, sizeof m->msgid);
    if (co_cabecera(raw, cb, "References", v, 2048) >= 0) co_cpy(m->refs, v, sizeof m->refs);
    if (co_cabecera(raw, cb, "Date", v, 2048) >= 0) m->fecha = co_fecha(v);
    co_free(v);
    struct mctx *c = (struct mctx *)co_alloc(sizeof *c);
    if (!c) return 0;
    mset(c, 0, sizeof *c); c->raw = raw; c->n = n; c->m = m;
    parte(c, 0, n, 0);
    /* texto: las partes text/plain; si no hay (o es un aviso de "tu programa no ve HTML"), el HTML */
    uint32_t pv = 0, hv = 0; int hay_html = 0;
    for (int i = 0; i < c->nt; i++) { if (c->t[i].html) { hay_html = 1; hv += c->t[i].len; } else pv += visibles(raw + c->t[i].off, c->t[i].len); }
    int usar_html = hay_html && (pv == 0 || (pv < 160 && hv > pv * 8));
    uint32_t p = 0;
    for (int i = 0; i < c->nt && p + 2 < cap; i++) {
        struct ptxt *t = &c->t[i];
        if (t->html != usar_html) continue;
        uint8_t *dec = (uint8_t *)co_alloc(t->len + 1); if (!dec) break;
        uint32_t dn = decodifica(raw + t->off, t->len, t->cte, dec, t->len);
        uint32_t ucap = dn * 3 + 16; char *u = (char *)co_alloc(ucap);
        if (!u) { co_free(dec); break; }
        uint32_t un = co_a_utf8(dec, dn, t->cs, u, ucap);
        co_free(dec);
        if (p) { texto[p++] = '\n'; if (p + 1 < cap) texto[p++] = '\n'; }
        if (t->html) p += co_html_texto(u, un, texto + p, cap - p);
        else for (uint32_t k = 0; k < un && p + 1 < cap; k++) if (u[k] != '\r') texto[p++] = u[k];
        co_free(u);
    }
    if (p >= cap) p = cap ? cap - 1 : 0;
    while (p && (texto[p - 1] == '\n' || texto[p - 1] == ' ')) p--;
    if (cap) texto[p] = 0;
    m->texto_n = p; m->html = usar_html;
    co_free(c);
    return 1;
}
uint32_t co_adj_saca(const char *raw, uint32_t n, const struct co_adj *a, uint8_t *out, uint32_t cap)
{
    if (a->off > n || a->off + a->len > n) return 0;
    return decodifica(raw + a->off, a->len, a->cte, out, cap);
}
void co_extracto(const char *t, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0; int esp = 1;
    if (!cap) return;
    for (uint32_t i = 0; i < n && p + 1 < cap;) {
        uint32_t e = i; while (e < n && t[e] != '\n') e++;
        uint32_t a = i; while (a < e && (t[a] == ' ' || t[a] == '\t' || t[a] == '\r')) a++;
        int cita = a < e && t[a] == '>';
        int firma = e - a >= 2 && t[a] == '-' && t[a + 1] == '-' && (e - a == 2 || (e - a == 3 && t[a + 2] == ' '));
        if (firma) break;
        int escribio = (co_ibusca(t + a, e - a, "escribi") || co_ibusca(t + a, e - a, "wrote:")) && e - a < 200 && e > a && t[e - 1 - (t[e - 1] == '\r')] == ':';
        if (!cita && !escribio) {
            for (uint32_t k = a; k < e && p + 1 < cap; k++) {
                char c = t[k] == '\t' || t[k] == '\r' ? ' ' : t[k];
                if (c == ' ') { if (!esp) out[p++] = ' '; esp = 1; } else { out[p++] = c; esp = 0; }
            }
            if (!esp && p + 1 < cap) { out[p++] = ' '; esp = 1; }
        }
        i = e + 1;
    }
    while (p && out[p - 1] == ' ') p--;
    while (p && ((uint8_t)out[p - 1] & 0xC0) == 0x80 && p + 1 >= cap) p--;   /* no dejar media letra al final */
    if (p && ((uint8_t)out[p - 1] & 0xC0) == 0xC0) p--;
    out[p] = 0;
}

/* ================================================================ IMAP */
static int literal_al_final(const char *b, uint32_t e, uint32_t *L)  /* la linea b[..e) (sin \r\n) acaba en {N} o {N+} */
{
    if (!e || b[e - 1] != '}') return 0;
    uint32_t i = e - 1; if (i && b[i - 1] == '+') i--;
    uint32_t v = 0, mul = 1, d = 0;
    while (i && b[i - 1] >= '0' && b[i - 1] <= '9' && d < 10) { v += (uint32_t)(b[i - 1] - '0') * mul; mul *= 10; i--; d++; }
    if (!d || !i || b[i - 1] != '{') return 0;
    *L = v; return 1;
}
uint32_t co_imap_completa(const char *b, uint32_t n)
{
    uint32_t i = 0;
    for (;;) {
        uint32_t e = i; while (e < n && b[e] != '\n') e++;
        if (e >= n) return 0;
        uint32_t fin = e; if (fin > i && b[fin - 1] == '\r') fin--;
        uint32_t L;
        if (literal_al_final(b, fin, &L) && fin > i) {
            uint64_t nx = (uint64_t)e + 1 + L;
            if (nx > n) return 0;
            i = (uint32_t)nx; continue;
        }
        return e + 1;
    }
}
int co_imap_etiqueta(const char *r, uint32_t n, const char *tag, char *texto, uint32_t cap)
{
    uint32_t tl = co_len(tag);
    if (cap) texto[0] = 0;
    if (n < tl + 3 || r[tl] != ' ') return 0;
    for (uint32_t i = 0; i < tl; i++) if (r[i] != tag[i]) return 0;
    const char *s = r + tl + 1; uint32_t sn = n - tl - 1;
    int v = iempieza_n(s, sn, "OK") ? 1 : (iempieza_n(s, sn, "NO") || iempieza_n(s, sn, "BAD")) ? -1 : 0;
    if (!v) return 0;
    uint32_t k = iempieza_n(s, sn, "BAD") ? 3 : 2; while (k < sn && s[k] == ' ') k++;
    uint32_t e = k; while (e < sn && s[e] != '\r' && s[e] != '\n') e++;
    co_cpyn(texto, s + k, e - k, cap);
    return v;
}
static uint32_t salta_esp(const char *r, uint32_t i, uint32_t n) { while (i < n && r[i] == ' ') i++; return i; }
/* un valor IMAP en r[i]: devuelve el indice tras el; dat/datn: contenido (cadena o literal) */
static uint32_t valor(const char *r, uint32_t i, uint32_t n, const char **dat, uint32_t *datn)
{
    *dat = 0; *datn = 0;
    i = salta_esp(r, i, n);
    if (i >= n) return n;
    /* SOMA_319_CORREO_PROTO: ningun indice pasa de n (antes, una \ al final de una cadena, un [ sin cerrar o un literal {N} sin
     * salto de linea lo dejaban 1 o 2 bytes mas alla, y el dato devuelto era mas largo que lo que quedaba del bufer) */
    if (r[i] == '"') {
        uint32_t a = ++i; while (i < n && r[i] != '"') { if (r[i] == '\\' && i + 1 < n) i++; i++; }
        *dat = r + a; *datn = i - a; return i < n ? i + 1 : n;
    }
    if (r[i] == '{') {
        uint32_t v = 0; i++; while (i < n && r[i] >= '0' && r[i] <= '9') { v = v > 0x0FFFFFFFu ? 0xFFFFFFFFu : v * 10 + (uint32_t)(r[i] - '0'); i++; }
        while (i < n && r[i] != '\n') i++;
        if (i < n) i++;
        if (v > n - i) v = n - i;
        *dat = r + i; *datn = v; return i + v;
    }
    if (r[i] == '(') {
        uint32_t a = i; int prof = 0;
        while (i < n) {
            if (r[i] == '"') { i++; while (i < n && r[i] != '"') { if (r[i] == '\\' && i + 1 < n) i++; i++; } if (i < n) i++; continue; }
            if (r[i] == '{') { const char *d; uint32_t dn; i = valor(r, i, n, &d, &dn); continue; }
            if (r[i] == '(') prof++;
            else if (r[i] == ')') { prof--; if (!prof) { i++; break; } }
            i++;
        }
        *dat = r + a; *datn = i - a; return i;
    }
    uint32_t a = i;
    while (i < n && r[i] != ' ' && r[i] != ')' && r[i] != '\r' && r[i] != '\n') { if (r[i] == '[') { while (i < n && r[i] != ']') i++; if (i >= n) break; } i++; }
    *dat = r + a; *datn = i - a; return i;
}
static uint32_t numero(const char *s, uint32_t n) { uint32_t v = 0; for (uint32_t i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (uint32_t)(s[i] - '0'); return v; }
static uint32_t banderas(const char *s, uint32_t n)
{
    uint32_t f = 0;
    if (co_ibusca(s, n, "\\Seen")) f |= CO_F_VISTO;
    if (co_ibusca(s, n, "\\Flagged")) f |= CO_F_DESTACADO;
    if (co_ibusca(s, n, "\\Answered")) f |= CO_F_RESPONDIDO;
    if (co_ibusca(s, n, "\\Deleted")) f |= CO_F_BORRADO;
    if (co_ibusca(s, n, "\\Draft")) f |= CO_F_BORRADOR;
    return f;
}
int co_imap_fetch(const char *r, uint32_t n, struct co_fetch *f)
{
    mset(f, 0, sizeof *f);
    if (n < 10 || r[0] != '*' || r[1] != ' ') return 0;
    uint32_t i = 2, s = 0; while (i < n && r[i] >= '0' && r[i] <= '9') s = s * 10 + (uint32_t)(r[i++] - '0');
    i = salta_esp(r, i, n);
    if (!iempieza_n(r + i, n - i, "FETCH")) return 0;
    i = salta_esp(r, i + 5, n);
    if (i >= n || r[i] != '(') return 0;
    i++; f->seq = s;
    while (i < n) {
        i = salta_esp(r, i, n);
        if (i >= n || r[i] == ')') break;
        uint32_t a = i;
        while (i < n && r[i] != ' ' && r[i] != '(' && r[i] != '"' && r[i] != '{' && r[i] != ')') {
            if (r[i] == '[') { while (i < n && r[i] != ']') i++; if (i >= n) break; }
            i++;
        }
        uint32_t nl = i - a; const char *nom = r + a;
        const char *d; uint32_t dn;
        i = valor(r, i, n, &d, &dn);
        if (nl == 3 && iempieza_n(nom, nl, "UID")) f->uid = numero(d, dn);
        else if (nl == 5 && iempieza_n(nom, nl, "FLAGS")) { f->flags = banderas(d, dn); f->tiene_flags = 1; }
        else if (nl == 11 && iempieza_n(nom, nl, "RFC822.SIZE")) f->tam = numero(d, dn);
        else if (iempieza_n(nom, nl, "BODY[") || iempieza_n(nom, nl, "RFC822")) {
            if (d && dn == 3 && iempieza_n(d, dn, "NIL")) continue;
            if (co_ibusca(nom, nl, "HEADER")) { f->cab = d; f->cab_n = dn; }
            else { f->cuerpo = d; f->cuerpo_n = dn; }
        }
    }
    return 1;
}
static int uso_por_nombre(const char *nombre)
{
    char t[100]; co_norm(nombre, t, sizeof t);
    const char *u = t; for (const char *p = t; *p; p++) if (*p == '/' || *p == '.') u = p + 1;   /* el ultimo trozo */
    if (co_ieq(t, "inbox")) return CO_U_ENTRADA;
    if (co_ieq(u, "sent") || co_ieq(u, "enviados") || co_ieq(u, "sent items") || co_ieq(u, "sent messages") || co_ieq(u, "elementos enviados") || co_ieq(u, "sent mail")) return CO_U_ENVIADOS;
    if (co_ieq(u, "drafts") || co_ieq(u, "borradores")) return CO_U_BORRADORES;
    if (co_ieq(u, "trash") || co_ieq(u, "papelera") || co_ieq(u, "deleted items") || co_ieq(u, "deleted messages") || co_ieq(u, "elementos eliminados") || co_ieq(u, "bin")) return CO_U_PAPELERA;
    if (co_ieq(u, "junk") || co_ieq(u, "spam") || co_ieq(u, "correo no deseado") || co_ieq(u, "bulk mail")) return CO_U_SPAM;
    if (co_ieq(u, "archive") || co_ieq(u, "archivo") || co_ieq(u, "archivados")) return CO_U_ARCHIVO;
    return CO_U_NADA;
}
int co_imap_list(const char *r, uint32_t n, struct co_carpeta *c)
{
    mset(c, 0, sizeof *c);
    if (n < 10 || r[0] != '*') return 0;
    uint32_t i = salta_esp(r, 1, n);
    if (iempieza_n(r + i, n - i, "LIST ")) i += 5; else if (iempieza_n(r + i, n - i, "XLIST ")) i += 6; else if (iempieza_n(r + i, n - i, "LSUB ")) i += 5; else return 0;
    const char *fl, *del, *nom; uint32_t fln, deln, nomn;
    i = valor(r, i, n, &fl, &fln);
    i = valor(r, i, n, &del, &deln);
    i = valor(r, i, n, &nom, &nomn);
    if (!nom || !nomn) return 0;
    uint32_t k = 0; int entre_comillas = nom > r && nom[-1] == '"';
    for (uint32_t z = 0; z < nomn && k + 1 < sizeof c->imap; z++) {
        if (entre_comillas && nom[z] == '\\' && z + 1 < nomn) z++;           /* \" y \\ dentro de una cadena */
        if (nom[z] == '\r' || nom[z] == '\n') break;
        c->imap[k++] = nom[z];
    }
    c->imap[k] = 0;
    if (co_ibusca(fl, fln, "\\Noselect") || co_ibusca(fl, fln, "\\NonExistent")) c->noselect = 1;
    if (co_ieq(c->imap, "INBOX")) c->uso = CO_U_ENTRADA;
    else if (co_ibusca(fl, fln, "\\Sent")) c->uso = CO_U_ENVIADOS;
    else if (co_ibusca(fl, fln, "\\Drafts")) c->uso = CO_U_BORRADORES;
    else if (co_ibusca(fl, fln, "\\Trash")) c->uso = CO_U_PAPELERA;
    else if (co_ibusca(fl, fln, "\\Junk")) c->uso = CO_U_SPAM;
    else if (co_ibusca(fl, fln, "\\Archive")) c->uso = CO_U_ARCHIVO;
    else if (co_ibusca(fl, fln, "\\All")) c->uso = CO_U_TODOS;
    else if (co_ibusca(fl, fln, "\\Flagged")) c->uso = CO_U_DESTACADOS;
    char u8[200]; co_utf7_dec(c->imap, u8, sizeof u8);
    if (!c->uso) c->uso = uso_por_nombre(u8);
    char dl = (del && deln == 1) ? del[0] : 0;
    const char *ult = u8; if (dl) for (const char *p = u8; *p; p++) if (*p == dl && p[1]) ult = p + 1;
    co_cpy(c->nombre, c->uso ? co_uso_nombre(c->uso) : ult, sizeof c->nombre);
    return 1;
}
const char *co_uso_nombre(int uso)
{
    switch (uso) {
    case CO_U_ENTRADA: return "Recibidos"; case CO_U_ENVIADOS: return "Enviados"; case CO_U_BORRADORES: return "Borradores";
    case CO_U_PAPELERA: return "Papelera"; case CO_U_SPAM: return "Spam"; case CO_U_ARCHIVO: return "Archivo"; case CO_U_TODOS: return "Todos";
    case CO_U_DESTACADOS: return "Destacados"; default: return "";
    }
}
uint32_t co_imap_search(const char *r, uint32_t n, uint32_t *uids, uint32_t max)
{
    if (n < 8 || r[0] != '*') return 0;
    uint32_t i = salta_esp(r, 1, n);
    if (!iempieza_n(r + i, n - i, "SEARCH")) return 0;
    i += 6; uint32_t k = 0;
    while (i < n && k < max) {
        i = salta_esp(r, i, n);
        if (i >= n || r[i] < '0' || r[i] > '9') { if (i < n && r[i] == '(') break; if (i < n && r[i] != '\r' && r[i] != '\n') { i++; continue; } break; }
        uint32_t v = 0; while (i < n && r[i] >= '0' && r[i] <= '9') v = v * 10 + (uint32_t)(r[i++] - '0');
        uids[k++] = v;
    }
    return k;
}
int co_imap_num(const char *r, uint32_t n, const char *clave, uint32_t *v)
{
    uint32_t cl = co_len(clave);
    for (uint32_t i = 0; i + cl <= n; i++) {
        if (r[i] == '\n') break;
        uint32_t k = 0; while (k < cl && lw((unsigned char)r[i + k]) == lw((unsigned char)clave[k])) k++;
        if (k < cl) continue;
        if (i && es_alnum((unsigned char)r[i - 1])) continue;
        if (i + cl < n && es_alnum((unsigned char)r[i + cl])) continue;
        uint32_t j = i + cl; if (j < n && r[j] == ' ') j++;                   /* "[UIDNEXT 456]", "(MESSAGES 23 UNSEEN 2)" */
        if (j < n && r[j] >= '0' && r[j] <= '9') { uint32_t x = 0; while (j < n && r[j] >= '0' && r[j] <= '9') x = x * 10 + (uint32_t)(r[j++] - '0'); *v = x; return 1; }
        if (i >= 2 && r[i - 1] == ' ' && r[i - 2] >= '0' && r[i - 2] <= '9') {   /* "* 23 EXISTS" */
            uint32_t k = i - 1, mul = 1, x = 0; while (k && r[k - 1] >= '0' && r[k - 1] <= '9') { x += (uint32_t)(r[k - 1] - '0') * mul; mul *= 10; k--; }
            *v = x; return 1;
        }
    }
    return 0;
}
int co_imap_cap(const char *r, uint32_t n, const char *cap)
{
    uint32_t cl = co_len(cap);
    for (uint32_t i = 0; i + cl <= n; i++) {
        if ((i == 0 || r[i - 1] == ' ' || r[i - 1] == '[') && (i + cl == n || r[i + cl] == ' ' || r[i + cl] == ']' || r[i + cl] == '\r' || r[i + cl] == '\n')) {
            uint32_t k = 0; while (k < cl && lw((unsigned char)r[i + k]) == lw((unsigned char)cap[k])) k++;
            if (k == cl) return 1;
        }
    }
    return 0;
}
uint32_t co_imap_cadena(const char *s, char *out, uint32_t cap)
{
    uint32_t n = co_len(s), p = 0;
    if (n > 1000) return 0;
    for (uint32_t i = 0; i < n; i++) if (s[i] == '\r' || s[i] == '\n' || (uint8_t)s[i] >= 0x80) return 0;
    if (p + 2 >= cap) return 0;
    out[p++] = '"';
    for (uint32_t i = 0; i < n; i++) { if (p + 3 >= cap) return 0; if (s[i] == '"' || s[i] == '\\') out[p++] = '\\'; out[p++] = s[i]; }
    out[p++] = '"'; out[p] = 0;
    return p;
}

/* ================================================================ SMTP */
int co_smtp_resp(const char *b, uint32_t n, int *codigo, uint32_t *usado)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t e = i; while (e < n && b[e] != '\n') e++;
        if (e >= n) return 0;
        if (e - i < 3 || b[i] < '0' || b[i] > '9') { *codigo = 0; *usado = e + 1; return 1; }   /* no es SMTP */
        if (e - i == 3 || b[i + 3] == ' ' || b[i + 3] == '\r') {
            *codigo = (b[i] - '0') * 100 + (b[i + 1] - '0') * 10 + (b[i + 2] - '0'); *usado = e + 1; return 1;
        }
        i = e + 1;
    }
    return 0;
}
uint32_t co_smtp_caps(const char *r, uint32_t n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n;) {
        uint32_t e = i; while (e < n && r[e] != '\n') e++;
        if (e - i > 4) {
            const char *l = r + i + 4; uint32_t ln = e - i - 4;
            if (iempieza_n(l, ln, "STARTTLS")) c |= CO_S_STARTTLS;
            if (iempieza_n(l, ln, "8BITMIME")) c |= CO_S_8BIT;
            if (iempieza_n(l, ln, "SMTPUTF8")) c |= CO_S_UTF8;
            if (iempieza_n(l, ln, "AUTH")) {
                if (co_imap_cap(l, ln, "PLAIN")) c |= CO_S_PLAIN;
                if (co_imap_cap(l, ln, "LOGIN")) c |= CO_S_LOGIN;
                if (co_imap_cap(l, ln, "XOAUTH2")) c |= CO_S_XOAUTH;
            }
        }
        i = e + 1;
    }
    return c;
}
uint32_t co_smtp_plain(const char *u, const char *k, char *out, uint32_t cap)
{
    uint32_t ul = co_len(u), kl = co_len(k);
    if (ul + kl > 600) return 0;
    uint8_t b[604]; uint32_t p = 0;
    b[p++] = 0; mcpy(b + p, u, ul); p += ul; b[p++] = 0; mcpy(b + p, k, kl); p += kl;
    return co_b64_enc(b, p, out, cap);
}
static int necesita_comillas(const char *s)
{
    for (; *s; s++) { char c = *s; if (!(es_alnum((unsigned char)c) || c == ' ' || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' || c == '/' || c == '=' || c == '?' || c == '^' || c == '_' || c == '`' || c == '{' || c == '|' || c == '}' || c == '~')) return 1; }
    return 0;
}
/* lista de direcciones para una cabecera: nombres codificados si hace falta, lineas plegadas */
static void dirs_cab(const char *lista, char *out, uint32_t cap, uint32_t col0)
{
    struct co_dir *d = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 40);
    out[0] = 0;
    if (!d) return;
    int nd = co_dirs(lista, d, 40); uint32_t col = col0;
    for (int i = 0; i < nd; i++) {
        char una[600] = "";
        if (d[i].nombre[0] && !co_ieq(d[i].nombre, d[i].email)) {
            int ascii = 1; for (const char *p = d[i].nombre; *p; p++) if ((uint8_t)*p >= 0x80) ascii = 0;
            if (!ascii) { char e[400]; if (co_cab_enc(d[i].nombre, e, sizeof e)) co_cat(una, e, sizeof una); }
            else if (necesita_comillas(d[i].nombre)) {
                co_cat(una, "\"", sizeof una);
                for (const char *p = d[i].nombre; *p; p++) { char t[3] = { 0, 0, 0 }; if (*p == '"' || *p == '\\') { t[0] = '\\'; t[1] = *p; } else t[0] = *p; co_cat(una, t, sizeof una); }
                co_cat(una, "\"", sizeof una);
            } else co_cat(una, d[i].nombre, sizeof una);
            co_cat(una, " <", sizeof una); co_cat(una, d[i].email, sizeof una); co_cat(una, ">", sizeof una);
        } else co_cat(una, d[i].email, sizeof una);
        uint32_t ul = co_len(una);
        if (i) { co_cat(out, ",", cap); col++; if (col + ul + 1 > 76) { co_cat(out, "\r\n ", cap); col = 1; } else { co_cat(out, " ", cap); col++; } }
        co_cat(out, una, cap); col += ul;
        for (const char *p = una; *p; p++) if (*p == '\n') col = 1;
    }
    co_free(d);
}
static void hexn(char *d, uint32_t cap, uint32_t v) { static const char H[] = "0123456789abcdef"; char t[9]; for (int i = 7; i >= 0; i--) { t[i] = H[v & 15]; v >>= 4; } t[8] = 0; co_cat(d, t, cap); }
uint32_t co_compone(struct co_envio *e, char *out, uint32_t cap)
{
    uint32_t bl = co_len(e->cuerpo);
    if (!cap) return 0;
    out[0] = 0;
    char *v = (char *)co_alloc(4096); if (!v) return 0;
    char f[64]; co_fecha_rfc(e->utc_min, e->off_min, f, sizeof f);
    co_cat(out, "Date: ", cap); co_cat(out, f, cap); co_cat(out, "\r\n", cap);
    { char de[400] = ""; if (e->de_nombre && e->de_nombre[0]) { co_cpy(de, e->de_nombre, sizeof de); co_cat(de, " <", sizeof de); co_cat(de, e->de_email, sizeof de); co_cat(de, ">", sizeof de); } else co_cpy(de, e->de_email, sizeof de);
      dirs_cab(de, v, 4096, 6); co_cat(out, "From: ", cap); co_cat(out, v, cap); co_cat(out, "\r\n", cap); }
    if (e->para && e->para[0]) { dirs_cab(e->para, v, 4096, 4); if (v[0]) { co_cat(out, "To: ", cap); co_cat(out, v, cap); co_cat(out, "\r\n", cap); } }
    if (e->cc && e->cc[0]) { dirs_cab(e->cc, v, 4096, 4); if (v[0]) { co_cat(out, "Cc: ", cap); co_cat(out, v, cap); co_cat(out, "\r\n", cap); } }
    co_cab_enc(e->asunto ? e->asunto : "", v, 4096);
    co_cat(out, "Subject: ", cap); co_cat(out, v, cap); co_cat(out, "\r\n", cap);
    { const char *dom = e->de_email; for (const char *p = e->de_email; *p; p++) if (*p == '@') dom = p + 1;
      e->msgid[0] = 0; co_cat(e->msgid, "<", sizeof e->msgid); hexn(e->msgid, sizeof e->msgid, e->azar); co_cat(e->msgid, ".", sizeof e->msgid);
      co_num(e->msgid, sizeof e->msgid, (uint64_t)(e->utc_min > 0 ? e->utc_min : 0)); co_cat(e->msgid, ".soma@", sizeof e->msgid); co_cat(e->msgid, dom[0] ? dom : "soma.local", sizeof e->msgid); co_cat(e->msgid, ">", sizeof e->msgid); }
    co_cat(out, "Message-ID: ", cap); co_cat(out, e->msgid, cap); co_cat(out, "\r\n", cap);
    if (e->en_respuesta_a && e->en_respuesta_a[0]) {
        co_cat(out, "In-Reply-To: ", cap); co_cat(out, e->en_respuesta_a, cap); co_cat(out, "\r\n", cap);
        co_cat(out, "References: ", cap);
        if (e->referencias && e->referencias[0]) { co_cat(out, e->referencias, cap); co_cat(out, "\r\n ", cap); }
        co_cat(out, e->en_respuesta_a, cap); co_cat(out, "\r\n", cap);
    }
    int ascii = 1; uint32_t col = 0, maxl = 0;
    for (uint32_t i = 0; i < bl; i++) { if ((uint8_t)e->cuerpo[i] >= 0x80) ascii = 0; if (e->cuerpo[i] == '\n') { if (col > maxl) maxl = col; col = 0; } else col++; }
    if (col > maxl) maxl = col;
    int qp = !ascii || maxl > 900;
    co_cat(out, "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: ", cap);
    co_cat(out, qp ? "quoted-printable" : "7bit", cap);
    co_cat(out, "\r\nX-Mailer: SOMA Correo\r\n\r\n", cap);
    co_free(v);
    uint32_t p = co_len(out);
    if (p + 4 >= cap) return 0;
    if (qp) { uint32_t w = co_qp_enc(e->cuerpo, bl, out + p, cap - p); if (!w && bl) return 0; p += w; }
    else for (uint32_t i = 0; i < bl; i++) { if (e->cuerpo[i] == '\r') continue; if (e->cuerpo[i] == '\n') { if (p + 3 >= cap) return 0; out[p++] = '\r'; out[p++] = '\n'; } else { if (p + 2 >= cap) return 0; out[p++] = e->cuerpo[i]; } }
    if (p < 2 || out[p - 1] != '\n') { if (p + 3 >= cap) return 0; out[p++] = '\r'; out[p++] = '\n'; }
    out[p] = 0;
    return p;
}
int co_destinatarios(const struct co_envio *e, struct co_dir *out, int max)
{
    int k = 0; const char *L[3] = { e->para, e->cc, e->cco };
    struct co_dir *t = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 40);
    if (!t) return 0;
    for (int z = 0; z < 3; z++) {
        if (!L[z] || !L[z][0]) continue;
        int n = co_dirs(L[z], t, 40);
        for (int i = 0; i < n && k < max; i++) {
            int rep = 0; for (int j = 0; j < k; j++) if (co_ieq(out[j].email, t[i].email)) rep = 1;
            if (!rep) out[k++] = t[i];
        }
    }
    co_free(t);
    return k;
}
uint32_t co_puntea(const char *m, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0; int ini = 1;
    for (uint32_t i = 0; i < n; i++) {
        if (p + 6 >= cap) return 0;
        if (ini && m[i] == '.') out[p++] = '.';
        if (m[i] == '\n' && (i == 0 || m[i - 1] != '\r')) out[p++] = '\r';
        out[p++] = m[i];
        ini = m[i] == '\n';
    }
    if (p + 6 >= cap) return 0;
    if (!ini) { out[p++] = '\r'; out[p++] = '\n'; }
    out[p++] = '.'; out[p++] = '\r'; out[p++] = '\n'; out[p] = 0;
    return p;
}
void co_asunto_resp(const char *a, int reenvio, char *out, uint32_t cap)
{
    const char *s = a ? a : "";
    for (;;) {
        while (*s == ' ') s++;
        if (co_iempieza(s, "re:")) s += 3; else if (co_iempieza(s, "fwd:")) s += 4; else if (co_iempieza(s, "fw:")) s += 3;
        else if (co_iempieza(s, "rv:")) s += 3; else if (co_iempieza(s, "re[") ) { const char *c = s; while (*c && *c != ']') c++; if (*c && c[1] == ':') s = c + 2; else break; }
        else break;
    }
    co_cpy(out, reenvio ? "Fwd: " : "Re: ", cap); co_cat(out, s, cap);
}
static void fecha_corta_es(int64_t local, char *d, uint32_t cap)
{
    int y, m, dd; civil(local / 1440, &y, &m, &dd);
    int h = (int)((local % 1440) / 60), mi = (int)(local % 60);
    d[0] = 0; co_num(d, cap, (uint64_t)dd); co_cat(d, " ", cap); co_cat(d, MESES_ES[m - 1], cap); co_cat(d, " ", cap); co_num(d, cap, (uint64_t)y);
    char t[8] = { ',', ' ', (char)('0' + h / 10), (char)('0' + h % 10), ':', (char)('0' + mi / 10), (char)('0' + mi % 10), 0 };
    co_cat(d, t, cap);
}
uint32_t co_cita(const char *de, int64_t fl, const char *texto, uint32_t n, int reenvio, const char *asunto, const char *para, char *out, uint32_t cap)
{
    char f[48]; fecha_corta_es(fl, f, sizeof f);
    uint32_t p = 0;
    if (!cap) return 0;
    out[0] = 0;
    if (reenvio) {
        co_cat(out, "\n\n---------- Mensaje reenviado ----------\nDe: ", cap); co_cat(out, de, cap);
        co_cat(out, "\nFecha: ", cap); co_cat(out, f, cap); co_cat(out, "\nAsunto: ", cap); co_cat(out, asunto ? asunto : "", cap);
        if (para && para[0]) { co_cat(out, "\nPara: ", cap); co_cat(out, para, cap); }
        co_cat(out, "\n\n", cap);
        p = co_len(out);
        for (uint32_t i = 0; i < n && p + 1 < cap; i++) out[p++] = texto[i];
        out[p] = 0; return p;
    }
    co_cat(out, "\n\nEl ", cap); co_cat(out, f, cap); co_cat(out, ", ", cap); co_cat(out, de, cap); co_cat(out, " escribi\303\263:\n", cap);
    p = co_len(out);
    int ini = 1;
    for (uint32_t i = 0; i < n && p + 3 < cap; i++) {
        if (ini) { out[p++] = '>'; if (texto[i] != '>' && texto[i] != '\n') out[p++] = ' '; }
        out[p++] = texto[i]; ini = texto[i] == '\n';
    }
    out[p] = 0;
    return p;
}

/* ================================================================ lo guardado */
const char *co_cat_nombre(int cat)
{
    switch (cat) { case CO_CAT_IMPORTANTE: return "Importante"; case CO_CAT_PERSONAL: return "Personal"; case CO_CAT_TRABAJO: return "Trabajo";
                   case CO_CAT_NOTIF: return "Avisos"; case CO_CAT_PROMO: return "Promociones"; default: return ""; }
}
void co_msg_de_cab(struct co_msg *m, const char *cab, uint32_t n)
{
    uint32_t uid = m->uid, fl = m->flags, tam = m->tam;
    mset(m, 0, sizeof *m); m->uid = uid; m->flags = fl; m->tam = tam;
    char *v = (char *)co_alloc(2048); struct co_dir *d = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 8);
    if (!v || !d) { co_free(v); co_free(d); return; }
    if (co_cabecera(cab, n, "From", v, 2048) >= 0 && co_dirs(v, d, 1) == 1) {
        co_cpy(m->de_email, d[0].email, sizeof m->de_email);
        if (d[0].nombre[0]) co_cpy(m->de, d[0].nombre, sizeof m->de);
        else { co_cpy(m->de, d[0].email, sizeof m->de); for (char *p = m->de; *p; p++) if (*p == '@') { *p = 0; break; } }
    }
    if (co_cabecera(cab, n, "To", v, 2048) >= 0) {
        int nd = co_dirs(v, d, 8);
        for (int i = 0; i < nd; i++) { char t[220]; co_dir_txt(&d[i], t, sizeof t); if (co_len(m->para) + co_len(t) + 3 >= sizeof m->para) break; if (i) co_cat(m->para, ", ", sizeof m->para); co_cat(m->para, t, sizeof m->para); }
    }
    if (co_cabecera(cab, n, "Subject", v, 2048) >= 0) co_cab_dec(v, co_len(v), m->asunto, sizeof m->asunto);
    if (co_cabecera(cab, n, "Date", v, 2048) >= 0) m->fecha = co_fecha(v);
    if (co_cabecera(cab, n, "Message-ID", v, 2048) >= 0) co_cpy(m->msgid, v, sizeof m->msgid);
    if (co_cabecera(cab, n, "Content-Type", v, 2048) >= 0 && co_iempieza(v, "multipart/mixed")) m->adj = 1;
    for (char *p = m->asunto; *p; p++) if (*p == '\t') *p = ' ';
    co_free(v); co_free(d);
}
void co_ficha(struct co_msg *m, const char *cab, uint32_t cab_n, const char *cuerpo, uint32_t cuerpo_n)
{
    co_msg_de_cab(m, cab ? cab : "", cab ? cab_n : 0);
    if (!cab || !cuerpo) return;
    uint32_t n = cab_n + cuerpo_n + 4;
    char *raw = (char *)co_alloc(n + 1), *tx = (char *)co_alloc(4000); struct co_mime *mm = (struct co_mime *)co_alloc(sizeof *mm);
    if (raw && tx && mm) {
        uint32_t p = 0; mcpy(raw, cab, cab_n); p = cab_n;
        if (p < 2 || raw[p - 1] != '\n' || (raw[p - 2] != '\n' && !(p >= 4 && raw[p - 3] == '\n'))) { raw[p++] = '\r'; raw[p++] = '\n'; }
        mcpy(raw + p, cuerpo, cuerpo_n); p += cuerpo_n; raw[p] = 0;
        if (co_mime_lee(raw, p, mm, tx, 4000)) {
            co_extracto(tx, mm->texto_n, m->extracto, sizeof m->extracto);
            int reales = 0; for (int i = 0; i < mm->nadj; i++) if (!mm->adj[i].en_linea) reales++;
            if (reales) m->adj = 1;
        }
    }
    co_free(raw); co_free(tx); co_free(mm);
}
static void pon_campo(char *o, uint32_t cap, uint32_t *p, const char *s)
{
    for (; s && *s && *p + 2 < cap; s++) o[(*p)++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
    if (*p + 1 < cap) o[(*p)++] = '\t';
}
static void pon_num(char *o, uint32_t cap, uint32_t *p, int64_t v) { char t[24] = ""; if (v < 0) { co_cat(t, "-", sizeof t); v = -v; } co_num(t, sizeof t, (uint64_t)v); pon_campo(o, cap, p, t); }
uint32_t co_msgs_serializa(const struct co_msg *v, int n, char *out, uint32_t cap)
{
    uint32_t p = 0;
    const char *cab = "#correo 1\n";
    while (*cab) { if (p + 1 >= cap) return 0; out[p++] = *cab++; }
    for (int i = 0; i < n; i++) {
        const struct co_msg *m = &v[i];
        uint32_t ini = p;
        pon_num(out, cap, &p, m->uid); pon_num(out, cap, &p, m->flags); pon_num(out, cap, &p, m->tam); pon_num(out, cap, &p, m->fecha);
        pon_num(out, cap, &p, m->adj); pon_num(out, cap, &p, m->prio); pon_num(out, cap, &p, m->cat);
        pon_campo(out, cap, &p, m->de); pon_campo(out, cap, &p, m->de_email); pon_campo(out, cap, &p, m->para);
        pon_campo(out, cap, &p, m->asunto); pon_campo(out, cap, &p, m->extracto); pon_campo(out, cap, &p, m->msgid);
        if (p + 2 >= cap) return 0;
        out[p - 1] = '\n';
        (void)ini;
    }
    if (p < cap) out[p] = 0;
    return p;
}
static int64_t num64(const char *s, uint32_t n) { int sg = 1; uint32_t i = 0; int64_t v = 0; if (n && s[0] == '-') { sg = -1; i = 1; } for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0'); return v * sg; }
int co_msgs_carga(const char *t, uint32_t n, struct co_msg *v, int max)
{
    if (n < 10 || !co_iempieza(t, "#correo 1")) return 0;
    uint32_t i = 0; while (i < n && t[i] != '\n') i++;
    i++;
    int k = 0;
    while (i < n && k < max) {
        uint32_t e = i; while (e < n && t[e] != '\n') e++;
        const char *f[13]; uint32_t fl[13]; int nf = 0; uint32_t a = i;
        for (uint32_t j = i; j <= e && nf < 13; j++) if (j == e || t[j] == '\t') { f[nf] = t + a; fl[nf] = j - a; nf++; a = j + 1; }
        if (nf >= 12) {
            struct co_msg *m = &v[k]; mset(m, 0, sizeof *m);
            m->uid = (uint32_t)num64(f[0], fl[0]); m->flags = (uint32_t)num64(f[1], fl[1]); m->tam = (uint32_t)num64(f[2], fl[2]); m->fecha = num64(f[3], fl[3]);
            m->adj = (uint8_t)num64(f[4], fl[4]); m->prio = (uint8_t)num64(f[5], fl[5]); m->cat = (uint8_t)num64(f[6], fl[6]);
            co_cpyn(m->de, f[7], fl[7], sizeof m->de); co_cpyn(m->de_email, f[8], fl[8], sizeof m->de_email); co_cpyn(m->para, f[9], fl[9], sizeof m->para);
            co_cpyn(m->asunto, f[10], fl[10], sizeof m->asunto); co_cpyn(m->extracto, f[11], fl[11], sizeof m->extracto);
            if (nf >= 13) co_cpyn(m->msgid, f[12], fl[12], sizeof m->msgid);
            if (m->uid) k++;
        }
        i = e + 1;
    }
    return k;
}

/* ================================================================ busqueda */
static const char *const VACIAS[] = { "el", "la", "los", "las", "lo", "de", "del", "un", "una", "unos", "unas", "y", "o", "a", "al", "en", "con", "por", "para",
    "que", "me", "mi", "mis", "se", "su", "sus", "sobre", "correo", "correos", "email", "emails", "mail", "mails", "mensaje", "mensajes", "e-mail", "busca", "buscame",
    "encuentra", "encuentrame", "muestra", "muestrame", "ensename", "donde", "cual", "hablaba", "hablaban", "habla", "hablan", "hay", "tengo", "algun", "alguno",
    "alguna", "quiero", "ver", "the", "of", "to", "and", "from", 0 };
static int vacia(const char *w) { for (int i = 0; VACIAS[i]; i++) if (co_ieq(w, VACIAS[i])) return 1; return 0; }
static int64_t local_a_utc(int64_t local) { return local - co_desfase(local - 60); }
static int64_t dia_local(int y, int m, int d) { return local_a_utc(dias_civil(y, m, d) * 1440); }
static int64_t fecha_iso(const char *s, uint32_t n)             /* 2026-11-01 o 1/11/2026 -> minutos UTC del principio de ese dia (local) */
{
    int a[3] = { 0, 0, 0 }, k = 0; uint32_t i = 0;
    while (i < n && k < 3) { if (s[i] >= '0' && s[i] <= '9') { int v = 0; while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0'); a[k++] = v; } else i++; }
    if (k < 3) return 0;
    if (a[0] > 1900) return a[1] >= 1 && a[1] <= 12 && a[2] >= 1 && a[2] <= 31 ? dia_local(a[0], a[1], a[2]) : 0;
    if (a[2] > 1900) return a[1] >= 1 && a[1] <= 12 && a[0] >= 1 && a[0] <= 31 ? dia_local(a[2], a[1], a[0]) : 0;
    return 0;
}
/* una fecha dicha con palabras en las palabras w[i..]: cuantas usa (0 si no) */
static int fecha_palabras(char w[][64], int nw, int i, int64_t hoy, struct co_filtro *f)
{
    const char *a = w[i], *b = i + 1 < nw ? w[i + 1] : "";
    if (co_ieq(a, "hoy") || co_ieq(a, "today")) { f->desde = local_a_utc(hoy); return 1; }
    if (co_ieq(a, "ayer") || co_ieq(a, "yesterday")) { f->desde = local_a_utc(hoy - 1440); f->hasta = local_a_utc(hoy); return 1; }
    if ((co_ieq(a, "esta") || co_ieq(a, "this") || co_ieq(a, "ultima") || co_ieq(a, "last")) && (co_ieq(b, "semana") || co_ieq(b, "week"))) {
        int64_t d = hoy / 1440; int ds = (int)((d % 7 + 7 + 3) % 7);
        f->desde = local_a_utc(co_ieq(a, "esta") || co_ieq(a, "this") ? (d - ds) * 1440 : hoy - 7 * 1440);
        return 2;
    }
    if ((co_ieq(a, "este") || co_ieq(a, "this") || co_ieq(a, "ultimo")) && (co_ieq(b, "mes") || co_ieq(b, "month"))) {
        int y, m, d; civil(hoy / 1440, &y, &m, &d);
        f->desde = co_ieq(a, "ultimo") ? local_a_utc(hoy - 30 * 1440) : dia_local(y, m, 1);
        return 2;
    }
    return 0;
}
void co_filtro_texto(const char *q, int64_t ahora, struct co_filtro *f)
{
    static const char *const OPS[] = { "de", "from", "para", "to", "asunto", "subject", "antes", "before", "despues", "desde", "after", "since", "tiene", "es", "has", "is", 0 };
    mset(f, 0, sizeof *f);
    int64_t hoy = (ahora / 1440) * 1440;
    char (*w)[64] = (char (*)[64])co_alloc(64 * 64); int nw = 0;
    if (!w) return;
    uint32_t n = co_len(q), i = 0;
    while (i < n) {
        while (i < n && es_esp((unsigned char)q[i])) i++;
        if (i >= n) break;
        uint32_t a = i; int com = 0;
        while (i < n && (com || !es_esp((unsigned char)q[i]))) { if (q[i] == '"') com = !com; i++; }
        char tok[200]; co_cpyn(tok, q + a, i - a, sizeof tok);
        char *dos = 0; for (char *z = tok; *z && *z != '"'; z++) if (*z == ':') { dos = z; break; }
        int op = -1;
        if (dos) { *dos = 0; char o[40]; co_norm(tok, o, sizeof o); for (int k = 0; OPS[k]; k++) if (co_ieq(o, OPS[k])) op = k; if (op < 0) *dos = ' '; }
        if (op >= 0) {
            char val[200]; co_cpy(val, dos + 1, sizeof val);
            if (!val[0] && i < n) {                                             /* "de: ana" */
                while (i < n && es_esp((unsigned char)q[i])) i++;
                uint32_t b = i; int c2 = 0; while (i < n && (c2 || !es_esp((unsigned char)q[i]))) { if (q[i] == '"') c2 = !c2; i++; }
                co_cpyn(val, q + b, i - b, sizeof val);
            }
            if (op <= 5) {
                char nv[120]; co_norm(val, nv, sizeof nv);
                co_cpy(op <= 1 ? f->de : op <= 3 ? f->para : f->asunto, nv, op <= 1 ? sizeof f->de : op <= 3 ? sizeof f->para : sizeof f->asunto);
            } else if (op <= 7) f->hasta = fecha_iso(val, co_len(val));
            else if (op <= 11) f->desde = fecha_iso(val, co_len(val));
            else {
                char nv[60]; co_norm(val, nv, sizeof nv);
                if (co_iempieza(nv, "adjunto") || co_iempieza(nv, "attachment")) f->adjuntos = 1;
                else if (co_iempieza(nv, "no leido") || co_iempieza(nv, "noleido") || co_iempieza(nv, "unread")) f->no_leidos = 1;
                else if (co_iempieza(nv, "destacado") || co_iempieza(nv, "starred")) f->destacados = 1;
                else if (co_iempieza(nv, "importante")) f->cat = CO_CAT_IMPORTANTE;
            }
            continue;
        }
        char nt[200]; co_norm(tok, nt, sizeof nt);                             /* palabras normales (un token puede dar varias) */
        const char *z = nt;
        while (*z && nw < 64) { while (*z == ' ') z++; const char *e = z; while (*e && *e != ' ') e++; if (e > z) { co_cpyn(w[nw], z, (uint32_t)(e - z), 64); nw++; } z = e; }
    }
    for (int k = 0; k < nw; k++) {
        const char *x = w[k], *sig = k + 1 < nw ? w[k + 1] : "";
        int usa = fecha_palabras(w, nw, k, hoy, f);
        if (usa) { k += usa - 1; continue; }
        if ((co_ieq(x, "no") || co_ieq(x, "sin")) && (co_iempieza(sig, "leid") || co_ieq(sig, "leer"))) { f->no_leidos = 1; k++; continue; }
        if (co_iempieza(x, "noleido") || co_ieq(x, "unread") || co_ieq(x, "nuevos") || co_ieq(x, "nuevo")) { f->no_leidos = 1; continue; }
        if (co_iempieza(x, "importante")) { f->cat = CO_CAT_IMPORTANTE; continue; }
        if (co_iempieza(x, "destacad") || co_ieq(x, "starred")) { f->destacados = 1; continue; }
        if ((co_ieq(x, "con") || co_ieq(x, "tiene")) && co_iempieza(sig, "adjunto")) { f->adjuntos = 1; k++; continue; }
        if (co_iempieza(x, "adjunto") || co_ieq(x, "attachment") || co_ieq(x, "attachments")) { f->adjuntos = 1; continue; }
        if (vacia(x)) continue;
        if (co_len(f->palabras) + co_len(x) + 2 < sizeof f->palabras) { if (f->palabras[0]) co_cat(f->palabras, " ", sizeof f->palabras); co_cat(f->palabras, x, sizeof f->palabras); }
    }
    co_free(w);
}
/* valor de "clave" en un JSON plano (cadena -> texto; true/false -> "1"/"0"; numero) */
static int json_val(const char *j, const char *clave, char *out, uint32_t cap)
{
    uint32_t n = co_len(j), cl = co_len(clave);
    out[0] = 0;
    for (uint32_t i = 0; i + cl + 2 < n; i++) {
        if (j[i] != '"') continue;
        uint32_t k = 0; while (k < cl && lw((unsigned char)j[i + 1 + k]) == lw((unsigned char)clave[k])) k++;
        if (k < cl || j[i + 1 + cl] != '"') continue;
        uint32_t p = i + cl + 2; while (p < n && es_esp((unsigned char)j[p])) p++;
        if (p >= n || j[p] != ':') continue;
        p++; while (p < n && es_esp((unsigned char)j[p])) p++;
        if (p < n && j[p] == '"') {
            uint32_t o = 0; p++;
            while (p < n && j[p] != '"' && o + 4 < cap) {
                if (j[p] == '\\' && p + 1 < n) {
                    p++;
                    if (j[p] == 'n') out[o++] = '\n'; else if (j[p] == 't') out[o++] = ' ';
                    else if (j[p] == 'u' && p + 4 < n) { uint32_t cp = 0; for (int z = 1; z <= 4; z++) cp = cp * 16 + (uint32_t)(hexv((unsigned char)j[p + z]) & 15); o = u8pon(cp, out, cap, o); p += 4; }
                    else out[o++] = j[p];
                    p++; continue;
                }
                out[o++] = j[p++];
            }
            out[o] = 0; return 1;
        }
        if (co_iempieza(j + p, "true")) { co_cpy(out, "1", cap); return 1; }
        if (co_iempieza(j + p, "false") || co_iempieza(j + p, "null")) { co_cpy(out, "", cap); return 1; }
        uint32_t o = 0; while (p < n && o + 1 < cap && j[p] != ',' && j[p] != '}' && !es_esp((unsigned char)j[p])) out[o++] = j[p++];
        out[o] = 0; return 1;
    }
    return 0;
}
int co_filtro_json(const char *j, struct co_filtro *f)
{
    mset(f, 0, sizeof *f);
    const char *a = j; while (*a && *a != '{') a++;
    if (!*a) return 0;
    char v[240];
    if (json_val(a, "de", v, sizeof v)) co_norm(v, f->de, sizeof f->de);
    if (json_val(a, "para", v, sizeof v)) co_norm(v, f->para, sizeof f->para);
    if (json_val(a, "asunto", v, sizeof v)) co_norm(v, f->asunto, sizeof f->asunto);
    if (json_val(a, "palabras", v, sizeof v)) {
        char t[240]; co_norm(v, t, sizeof t);
        uint32_t i = 0, n = co_len(t);
        while (i < n) { while (i < n && t[i] == ' ') i++; uint32_t b = i; while (i < n && t[i] != ' ') i++; char w[60]; co_cpyn(w, t + b, i - b, sizeof w);
                        if (w[0] && !vacia(w) && co_len(f->palabras) + co_len(w) + 2 < sizeof f->palabras) { if (f->palabras[0]) co_cat(f->palabras, " ", sizeof f->palabras); co_cat(f->palabras, w, sizeof f->palabras); } }
    }
    if (json_val(a, "desde", v, sizeof v) && v[0]) f->desde = fecha_iso(v, co_len(v));
    if (json_val(a, "hasta", v, sizeof v) && v[0]) { int64_t h = fecha_iso(v, co_len(v)); if (h) f->hasta = h + 1440; }   /* hasta incluido */
    if (json_val(a, "no_leidos", v, sizeof v)) f->no_leidos = v[0] == '1';
    if (json_val(a, "destacados", v, sizeof v)) f->destacados = v[0] == '1';
    if (json_val(a, "adjuntos", v, sizeof v)) f->adjuntos = v[0] == '1';
    if (json_val(a, "categoria", v, sizeof v) && v[0]) { uint32_t ids[1]; uint8_t c[1]; char l[80] = "1: "; co_cat(l, v, sizeof l); if (co_ia_cats(l, ids, c, 1) == 1) f->cat = c[0]; }
    return 1;
}
int co_filtro_vacio(const struct co_filtro *f)
{
    return !f->de[0] && !f->para[0] && !f->asunto[0] && !f->palabras[0] && !f->desde && !f->hasta && !f->no_leidos && !f->destacados && !f->adjuntos && !f->cat;
}
static int contiene(const char *campo_norm, const char *w)
{
    uint32_t n = co_len(campo_norm), wl = co_len(w);
    if (co_ibusca(campo_norm, n, w)) return 1;
    if (wl > 4 && w[wl - 1] == 's') { char r[80]; co_cpyn(r, w, wl - 1, sizeof r); if (wl > 5 && w[wl - 2] == 'e') { char r2[80]; co_cpyn(r2, w, wl - 2, sizeof r2); if (co_ibusca(campo_norm, n, r2)) return 1; } return co_ibusca(campo_norm, n, r) != 0; }
    return 0;
}
int co_coincide(const struct co_msg *m, const struct co_filtro *f)
{
    if (f->no_leidos && (m->flags & CO_F_VISTO)) return 0;
    if (f->destacados && !(m->flags & CO_F_DESTACADO)) return 0;
    if (f->adjuntos && !m->adj) return 0;
    if (f->cat && m->cat != f->cat) return 0;
    if (f->desde && m->fecha < f->desde) return 0;
    if (f->hasta && m->fecha >= f->hasta) return 0;
    char de[240], as[240], ex[220], pa[200];
    { char t[240]; co_cpy(t, m->de, sizeof t); co_cat(t, " ", sizeof t); co_cat(t, m->de_email, sizeof t); co_norm(t, de, sizeof de); }
    co_norm(m->asunto, as, sizeof as); co_norm(m->extracto, ex, sizeof ex); co_norm(m->para, pa, sizeof pa);
    if (f->de[0] && !contiene(de, f->de)) return 0;
    if (f->para[0] && !contiene(pa, f->para)) return 0;
    if (f->asunto[0] && !contiene(as, f->asunto)) return 0;
    int pts = 1;
    const char *w = f->palabras;
    while (*w) {
        while (*w == ' ') w++;
        const char *e = w; while (*e && *e != ' ') e++;
        if (e > w) {
            char p[80]; co_cpyn(p, w, (uint32_t)(e - w), sizeof p);
            int a = 0;
            if (contiene(as, p)) a += 3;
            if (contiene(de, p)) a += 3;
            if (contiene(ex, p)) a += 1;
            if (contiene(pa, p)) a += 1;
            if (!a) return 0;
            pts += a;
        }
        w = e;
    }
    return pts;
}
int co_busca(const struct co_msg *v, int n, const struct co_filtro *f, int *idx, int max)
{
    int k = 0; int *pts = (int *)co_alloc(sizeof(int) * (uint32_t)(max > 0 ? max : 1));
    if (!pts) return 0;
    for (int i = 0; i < n; i++) {
        int p = co_coincide(&v[i], f);
        if (!p) continue;
        int pos = k < max ? k : max;
        while (pos > 0 && (pts[pos - 1] < p || (pts[pos - 1] == p && v[idx[pos - 1]].fecha < v[i].fecha))) pos--;
        if (pos >= max) continue;
        int fin = k < max ? k : max - 1;
        for (int z = fin; z > pos; z--) { idx[z] = idx[z - 1]; pts[z] = pts[z - 1]; }
        idx[pos] = i; pts[pos] = p;
        if (k < max) k++;
    }
    co_free(pts);
    return k;
}
static void fecha_imap(int64_t utc, char *d, uint32_t cap)
{
    static const char *const MS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int y, m, dd; civil((utc + co_desfase(utc)) / 1440, &y, &m, &dd);
    co_num(d, cap, (uint64_t)dd); co_cat(d, "-", cap); co_cat(d, MS[m - 1], cap); co_cat(d, "-", cap); co_num(d, cap, (uint64_t)y);
}
static void crit_txt(char *o, uint32_t cap, const char *k, const char *v)
{
    char q[260];
    if (!v[0] || !co_imap_cadena(v, q, sizeof q)) return;
    if (o[0]) co_cat(o, " ", cap);
    co_cat(o, k, cap); co_cat(o, " ", cap); co_cat(o, q, cap);
}
uint32_t co_filtro_imap(const struct co_filtro *f, char *o, uint32_t cap)
{
    o[0] = 0;
    if (f->no_leidos) co_cat(o, "UNSEEN", cap);
    if (f->destacados) { if (o[0]) co_cat(o, " ", cap); co_cat(o, "FLAGGED", cap); }
    crit_txt(o, cap, "FROM", f->de); crit_txt(o, cap, "TO", f->para); crit_txt(o, cap, "SUBJECT", f->asunto);
    const char *w = f->palabras;
    while (*w) { while (*w == ' ') w++; const char *e = w; while (*e && *e != ' ') e++; if (e > w) { char p[80]; co_cpyn(p, w, (uint32_t)(e - w), sizeof p); crit_txt(o, cap, "TEXT", p); } w = e; }
    if (f->desde) { if (o[0]) co_cat(o, " ", cap); co_cat(o, "SINCE ", cap); fecha_imap(f->desde, o, cap); }
    if (f->hasta) { if (o[0]) co_cat(o, " ", cap); co_cat(o, "BEFORE ", cap); fecha_imap(f->hasta + 1439, o, cap); }
    if (!o[0]) co_cat(o, "ALL", cap);
    return co_len(o);
}

/* ================================================================ contactos */
int co_contactos(const struct co_msg *v, int n, const char *propio, struct co_dir *out, int *veces, int max)
{
    int k = 0;
    struct co_dir *t = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 9);
    if (!t) return 0;
    for (int i = 0; i < n; i++) {
        int nt = 0;
        if (v[i].de_email[0]) { co_cpy(t[0].email, v[i].de_email, sizeof t[0].email); co_cpy(t[0].nombre, v[i].de, sizeof t[0].nombre); nt = 1; }
        nt += co_dirs(v[i].para, t + nt, 8);
        for (int z = 0; z < nt; z++) {
            if (!co_email_valido(t[z].email) || (propio && co_ieq(t[z].email, propio))) continue;
            if (co_ibusca(t[z].email, co_len(t[z].email), "noreply") || co_ibusca(t[z].email, co_len(t[z].email), "no-reply") || co_ibusca(t[z].email, co_len(t[z].email), "no_reply")) continue;
            int j = 0; while (j < k && !co_ieq(out[j].email, t[z].email)) j++;
            if (j < k) { veces[j]++; if (t[z].nombre[0] && !co_ieq(t[z].nombre, t[z].email) && co_len(t[z].nombre) > co_len(out[j].nombre)) co_cpy(out[j].nombre, t[z].nombre, sizeof out[j].nombre); continue; }   /* el nombre mas completo */
            if (k >= max) continue;
            out[k] = t[z]; veces[k] = 1;
            if (co_ieq(out[k].nombre, out[k].email)) out[k].nombre[0] = 0;
            k++;
        }
    }
    co_free(t);
    for (int i = 1; i < k; i++) { struct co_dir d = out[i]; int c = veces[i], j = i; while (j > 0 && veces[j - 1] < c) { out[j] = out[j - 1]; veces[j] = veces[j - 1]; j--; } out[j] = d; veces[j] = c; }
    return k;
}
int co_resuelve(const struct co_msg *v, int n, const char *propio, const char *quien, struct co_dir *out)
{
    char q[120]; co_norm(quien, q, sizeof q);
    if (!q[0]) return 0;
    if (co_email_valido(quien)) { co_cpy(out->email, quien, sizeof out->email); out->nombre[0] = 0; return 1; }
    struct co_dir *c = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 200); int *ve = (int *)co_alloc(sizeof(int) * 200);
    if (!c || !ve) { co_free(c); co_free(ve); return 0; }
    int nc = co_contactos(v, n, propio, c, ve, 200), mejor = -1, mp = 0;
    for (int i = 0; i < nc; i++) {
        char nm[120], em[120]; co_norm(c[i].nombre, nm, sizeof nm); co_norm(c[i].email, em, sizeof em);
        int p = 0;
        if (co_ieq(em, q)) p = 100;
        else if (nm[0] && co_ieq(nm, q)) p = 90;
        else {
            /* todas las palabras de lo pedido estan en el nombre o al principio del correo */
            int todas = 1, alguna = 0; const char *w = q;
            while (*w) { while (*w == ' ') w++; const char *e = w; while (*e && *e != ' ') e++;
                         if (e > w) { char pw[60]; co_cpyn(pw, w, (uint32_t)(e - w), sizeof pw);
                                      int en_nombre = 0; const char *z = nm; uint32_t pl = co_len(pw);
                                      while (*z) { while (*z == ' ') z++; if (co_iempieza(z, pw) && (z[pl] == 0 || z[pl] == ' ' || pl >= 4)) { en_nombre = 1; break; } while (*z && *z != ' ') z++; }
                                      int en_mail = co_iempieza(em, pw);
                                      if (en_nombre || en_mail) alguna = 1; else todas = 0; }
                         w = e; }
            if (todas && alguna) p = 50 + (nm[0] && co_iempieza(nm, q) ? 10 : 0);
        }
        if (p) p = p * 100 + (ve[i] > 99 ? 99 : ve[i]);
        if (p > mp) { mp = p; mejor = i; }
    }
    if (mejor >= 0) *out = c[mejor];
    co_free(c); co_free(ve);
    return mejor >= 0;
}

/* ================================================================ lo que contesta el modelo */
uint32_t co_ia_limpia(const char *r, char *out, uint32_t cap)
{
    uint32_t n = co_len(r), p = 0, i = 0;
    if (!cap) return 0;
    const char *th = co_ibusca(r, n, "</think>");
    if (th) i = (uint32_t)(th - r) + 8;
    else if (co_ibusca(r, n, "<think>")) { const char *a = co_ibusca(r, n, "<think>"); (void)a; i = n; }   /* solo pensamiento: nada */
    for (; i < n;) {
        uint32_t e = i; while (e < n && r[e] != '\n') e++;
        uint32_t a = i; while (a < e && r[a] == ' ') a++;
        if (e - a >= 3 && r[a] == '`' && r[a + 1] == '`' && r[a + 2] == '`') { i = e + 1; continue; }
        for (uint32_t k = i; k < e && p + 1 < cap; k++) if (r[k] != '\r') out[p++] = r[k];
        if (e < n && p + 1 < cap) out[p++] = '\n';
        i = e + 1;
    }
    while (p && (out[p - 1] == '\n' || out[p - 1] == ' ')) p--;
    uint32_t a = 0; while (a < p && (out[a] == '\n' || out[a] == ' ')) a++;
    uint32_t k = 0; for (uint32_t z = a; z < p; z++) out[k++] = out[z];
    out[k] = 0;
    return k;
}
/* "**Asunto:** texto" -> 1 si la linea empieza por alguna de las claves; val: lo que sigue a los dos puntos */
static int marca(const char *l, uint32_t n, const char *const *claves, const char **val)
{
    uint32_t i = 0; while (i < n && (l[i] == ' ' || l[i] == '*' || l[i] == '#' || l[i] == '-')) i++;
    for (int k = 0; claves[k]; k++) {
        uint32_t cl = co_len(claves[k]);
        if (i + cl <= n && co_iempieza(l + i, claves[k])) {
            uint32_t j = i + cl; while (j < n && (l[j] == ' ' || l[j] == '*')) j++;
            if (j < n && l[j] == ':') { j++; while (j < n && (l[j] == ' ' || l[j] == '*')) j++; *val = l + j; return 1; }
        }
    }
    return 0;
}
static void linea_val(const char *v, const char *fin, char *out, uint32_t cap)
{
    const char *e = v; while (e < fin && *e != '\n') e++;
    while (e > v && (e[-1] == ' ' || e[-1] == '*' || e[-1] == '\r')) e--;
    while (v < e && (*v == '"' || *v == '\'' || *v == ' ')) v++;
    while (e > v && (e[-1] == '"' || e[-1] == '\'')) e--;
    co_cpyn(out, v, (uint32_t)(e - v), cap);
}
int co_ia_borrador(const char *r, char *para, uint32_t pcap, char *asunto, uint32_t acap, char *cuerpo, uint32_t ccap)
{
    static const char *const KP[] = { "para", "to", "destinatario", "destinatarios", 0 };
    static const char *const KA[] = { "asunto", "subject", "titulo", 0 };
    static const char *const KC[] = { "cuerpo", "mensaje", "texto", "body", "contenido", 0 };
    uint32_t n0 = co_len(r);
    char *t = (char *)co_alloc(n0 + 1); if (!t) return 0;
    uint32_t n = co_ia_limpia(r, t, n0 + 1);
    if (pcap) para[0] = 0;
    if (acap) asunto[0] = 0;
    if (ccap) cuerpo[0] = 0;
    const char *cuerpo_ini = 0, *fin = t + n;
    for (uint32_t i = 0; i < n;) {
        uint32_t e = i; while (e < n && t[e] != '\n') e++;
        const char *v;
        if (!cuerpo_ini) {
            if (marca(t + i, e - i, KP, &v)) linea_val(v, fin, para, pcap);
            else if (marca(t + i, e - i, KA, &v)) { linea_val(v, fin, asunto, acap); if (!cuerpo_ini) cuerpo_ini = 0; }
            else if (marca(t + i, e - i, KC, &v)) { cuerpo_ini = v < t + e && *v ? v : t + e + (e < n ? 1 : 0); }
        }
        i = e + 1;
    }
    if (!cuerpo_ini) {                                              /* sin "Cuerpo:": lo que va tras la ultima cabecera reconocida */
        cuerpo_ini = t;
        for (uint32_t i = 0; i < n;) {
            uint32_t e = i; while (e < n && t[e] != '\n') e++;
            const char *v;
            if (marca(t + i, e - i, KP, &v) || marca(t + i, e - i, KA, &v)) cuerpo_ini = t + (e < n ? e + 1 : e);
            i = e + 1;
        }
    }
    const char *c = cuerpo_ini; while (c < fin && (*c == '\n' || *c == ' ')) c++;
    const char *ce = fin;
    while (ce > c && (ce[-1] == '\n' || ce[-1] == ' ' || ce[-1] == '-')) ce--;
    co_cpyn(cuerpo, c, (uint32_t)(ce - c), ccap);
    for (char *z = asunto; *z; z++) if (*z == '\n') *z = ' ';
    co_free(t);
    return cuerpo[0] != 0;
}
int co_ia_ids(const char *r, uint32_t *ids, int max)
{
    uint32_t n = co_len(r); const char *a = co_ibusca(r, n, "IDS");
    const char *p = a ? a + 3 : r;
    if (!a && n > 300) return 0;
    if (co_ibusca(p, co_len(p), "ningun")) return 0;
    int k = 0;
    while (*p && k < max) {
        if (*p >= '0' && *p <= '9') { uint32_t v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (uint32_t)(*p++ - '0'); int rep = 0; for (int z = 0; z < k; z++) if (ids[z] == v) rep = 1; if (!rep) ids[k++] = v; continue; }
        if (*p == '\n' && a && k) break;
        p++;
    }
    return k;
}
int co_ia_cats(const char *r, uint32_t *ids, uint8_t *cats, int max)
{
    uint32_t n = co_len(r); int k = 0;
    for (uint32_t i = 0; i < n && k < max;) {
        uint32_t e = i; while (e < n && r[e] != '\n') e++;
        uint32_t a = i; while (a < e && (r[a] == ' ' || r[a] == '-' || r[a] == '*' || r[a] == '[')) a++;
        if (a < e && r[a] >= '0' && r[a] <= '9') {
            uint32_t v = 0; while (a < e && r[a] >= '0' && r[a] <= '9') v = v * 10 + (uint32_t)(r[a++] - '0');
            char resto[80]; co_cpyn(resto, r + a, e - a, sizeof resto);
            char nr[80]; co_norm(resto, nr, sizeof nr);
            uint8_t c = 0;
            if (co_ibusca(nr, co_len(nr), "important") || co_ibusca(nr, co_len(nr), "urgente")) c = CO_CAT_IMPORTANTE;
            else if (co_ibusca(nr, co_len(nr), "personal")) c = CO_CAT_PERSONAL;
            else if (co_ibusca(nr, co_len(nr), "trabajo") || co_ibusca(nr, co_len(nr), "work")) c = CO_CAT_TRABAJO;
            else if (co_ibusca(nr, co_len(nr), "notific") || co_ibusca(nr, co_len(nr), "aviso") || co_ibusca(nr, co_len(nr), "automatic")) c = CO_CAT_NOTIF;
            else if (co_ibusca(nr, co_len(nr), "promo") || co_ibusca(nr, co_len(nr), "publicidad") || co_ibusca(nr, co_len(nr), "newsletter") || co_ibusca(nr, co_len(nr), "boletin") || co_ibusca(nr, co_len(nr), "oferta")) c = CO_CAT_PROMO;
            if (c) { ids[k] = v; cats[k] = c; k++; }
        }
        i = e + 1;
    }
    return k;
}

/* ================================================================ cuentas */
static const struct { const char *dom; const char *imap; uint16_t ip; uint8_t it; const char *smtp; uint16_t sp; uint8_t st; uint8_t gm; const char *aviso; } PROV[] = {
    { "gmail.com", "imap.gmail.com", 993, 1, "smtp.gmail.com", 465, 1, 1, "Gmail pide una contrase\303\261a de aplicaci\303\263n: Cuenta de Google > Seguridad > Verificaci\303\263n en dos pasos > Contrase\303\261as de aplicaciones." },
    { "googlemail.com", "imap.gmail.com", 993, 1, "smtp.gmail.com", 465, 1, 1, "Gmail pide una contrase\303\261a de aplicaci\303\263n: Cuenta de Google > Seguridad > Verificaci\303\263n en dos pasos > Contrase\303\261as de aplicaciones." },
    { "yahoo.com", "imap.mail.yahoo.com", 993, 1, "smtp.mail.yahoo.com", 465, 1, 0, "Yahoo pide una contrase\303\261a de aplicaci\303\263n (Seguridad de la cuenta > Generar contrase\303\261a de aplicaci\303\263n)." },
    { "yahoo.es", "imap.mail.yahoo.com", 993, 1, "smtp.mail.yahoo.com", 465, 1, 0, "Yahoo pide una contrase\303\261a de aplicaci\303\263n (Seguridad de la cuenta > Generar contrase\303\261a de aplicaci\303\263n)." },
    { "ymail.com", "imap.mail.yahoo.com", 993, 1, "smtp.mail.yahoo.com", 465, 1, 0, "Yahoo pide una contrase\303\261a de aplicaci\303\263n (Seguridad de la cuenta > Generar contrase\303\261a de aplicaci\303\263n)." },
    { "icloud.com", "imap.mail.me.com", 993, 1, "smtp.mail.me.com", 587, 2, 0, "iCloud pide una contrase\303\261a espec\303\255fica para apps (account.apple.com > Inicio de sesi\303\263n y seguridad)." },
    { "me.com", "imap.mail.me.com", 993, 1, "smtp.mail.me.com", 587, 2, 0, "iCloud pide una contrase\303\261a espec\303\255fica para apps (account.apple.com > Inicio de sesi\303\263n y seguridad)." },
    { "mac.com", "imap.mail.me.com", 993, 1, "smtp.mail.me.com", 587, 2, 0, "iCloud pide una contrase\303\261a espec\303\255fica para apps (account.apple.com > Inicio de sesi\303\263n y seguridad)." },
    { "outlook.com", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "outlook.es", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "hotmail.com", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "hotmail.es", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "live.com", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "msn.com", "outlook.office365.com", 993, 1, "smtp-mail.outlook.com", 587, 2, 0, "Outlook y Hotmail ya solo aceptan OAuth, que SOMA no tiene: lo m\303\241s probable es que rechacen la contrase\303\261a." },
    { "gmx.net", "imap.gmx.net", 993, 1, "mail.gmx.net", 587, 2, 0, "En GMX hay que activar el acceso POP3/IMAP en Ajustes de correo." },
    { "gmx.com", "imap.gmx.com", 993, 1, "mail.gmx.com", 587, 2, 0, "En GMX hay que activar el acceso POP3/IMAP en Ajustes de correo." },
    { "gmx.es", "imap.gmx.com", 993, 1, "mail.gmx.com", 587, 2, 0, "En GMX hay que activar el acceso POP3/IMAP en Ajustes de correo." },
    { "zoho.com", "imap.zoho.com", 993, 1, "smtp.zoho.com", 465, 1, 0, "Zoho: si tu cuenta es europea, cambia los servidores a imap.zoho.eu y smtp.zoho.eu." },
    { "zohomail.com", "imap.zoho.com", 993, 1, "smtp.zoho.com", 465, 1, 0, "Zoho: si tu cuenta es europea, cambia los servidores a imap.zoho.eu y smtp.zoho.eu." },
    { "zohomail.eu", "imap.zoho.eu", 993, 1, "smtp.zoho.eu", 465, 1, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
int co_cuenta_auto(struct co_cuenta *c, const char **aviso)
{
    const char *dom = 0; for (const char *p = c->email; *p; p++) if (*p == '@') dom = p + 1;
    if (aviso) *aviso = 0;
    if (!c->usuario[0]) co_cpy(c->usuario, c->email, sizeof c->usuario);
    if (!dom || !dom[0]) return 0;
    for (int i = 0; PROV[i].dom; i++) if (co_ieq(dom, PROV[i].dom)) {
        co_cpy(c->imap, PROV[i].imap, sizeof c->imap); c->imap_p = PROV[i].ip; c->imap_tls = PROV[i].it;
        co_cpy(c->smtp, PROV[i].smtp, sizeof c->smtp); c->smtp_p = PROV[i].sp; c->smtp_tls = PROV[i].st;
        c->gmail = PROV[i].gm;
        if (aviso) *aviso = PROV[i].aviso;
        return 1;
    }
    co_cpy(c->imap, "imap.", sizeof c->imap); co_cat(c->imap, dom, sizeof c->imap); c->imap_p = 993; c->imap_tls = CO_TLS_DIRECTO;
    co_cpy(c->smtp, "smtp.", sizeof c->smtp); co_cat(c->smtp, dom, sizeof c->smtp); c->smtp_p = 465; c->smtp_tls = CO_TLS_DIRECTO;
    c->gmail = 0;
    return 0;
}
uint32_t co_cuenta_serializa(const struct co_cuenta *c, char *out, uint32_t cap)
{
    char k64[260]; co_b64_enc((const uint8_t *)c->clave, co_len(c->clave), k64, sizeof k64);
    out[0] = 0;
    co_cat(out, "email=", cap); co_cat(out, c->email, cap); co_cat(out, "\nnombre=", cap); co_cat(out, c->nombre, cap);
    co_cat(out, "\nusuario=", cap); co_cat(out, c->usuario, cap); co_cat(out, "\nclave64=", cap); co_cat(out, k64, cap);
    co_cat(out, "\nimap=", cap); co_cat(out, c->imap, cap); co_cat(out, ":", cap); co_num(out, cap, c->imap_p); co_cat(out, ":", cap); co_num(out, cap, c->imap_tls);
    co_cat(out, "\nsmtp=", cap); co_cat(out, c->smtp, cap); co_cat(out, ":", cap); co_num(out, cap, c->smtp_p); co_cat(out, ":", cap); co_num(out, cap, c->smtp_tls);
    co_cat(out, "\ngmail=", cap); co_num(out, cap, c->gmail); co_cat(out, "\n", cap);
    return co_len(out);
}
static int linea_conf(const char *t, uint32_t n, const char *k, char *out, uint32_t cap)
{
    uint32_t kl = co_len(k);
    for (uint32_t i = 0; i < n;) {
        uint32_t e = i; while (e < n && t[e] != '\n') e++;
        if (e - i > kl && t[i + kl] == '=' && co_iempieza(t + i, k)) { uint32_t f = e; if (f > i && t[f - 1] == '\r') f--; co_cpyn(out, t + i + kl + 1, f - i - kl - 1, cap); return 1; }
        i = e + 1;
    }
    if (cap) out[0] = 0;
    return 0;
}
static void servidor(const char *v, char *host, uint32_t hc, uint16_t *port, uint8_t *tls)
{
    const char *a = v, *b = 0, *c = 0;
    for (const char *p = v; *p; p++) if (*p == ':') { if (!b) b = p; else if (!c) c = p; }
    if (!b) { co_cpy(host, v, hc); return; }
    co_cpyn(host, a, (uint32_t)(b - a), hc);
    *port = (uint16_t)num64(b + 1, co_len(b + 1));
    if (c) *tls = (uint8_t)num64(c + 1, co_len(c + 1));
}
int co_cuenta_carga(struct co_cuenta *c, const char *t, uint32_t n)
{
    mset(c, 0, sizeof *c);
    char v[300];
    if (!linea_conf(t, n, "email", c->email, sizeof c->email) || !c->email[0]) return 0;
    linea_conf(t, n, "nombre", c->nombre, sizeof c->nombre);
    linea_conf(t, n, "usuario", c->usuario, sizeof c->usuario);
    if (linea_conf(t, n, "clave64", v, sizeof v)) { uint32_t k = co_b64_dec(v, co_len(v), (uint8_t *)c->clave, sizeof c->clave - 1); c->clave[k] = 0; }
    c->imap_p = 993; c->imap_tls = 1; c->smtp_p = 465; c->smtp_tls = 1;
    if (linea_conf(t, n, "imap", v, sizeof v)) servidor(v, c->imap, sizeof c->imap, &c->imap_p, &c->imap_tls);
    if (linea_conf(t, n, "smtp", v, sizeof v)) servidor(v, c->smtp, sizeof c->smtp, &c->smtp_p, &c->smtp_tls);
    if (linea_conf(t, n, "gmail", v, sizeof v)) c->gmail = v[0] == '1';
    if (!c->usuario[0]) co_cpy(c->usuario, c->email, sizeof c->usuario);
    return 1;
}
