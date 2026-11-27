/* SOMA_358_HOJA: leer ZIP y XML de archivos que llegan de fuera. En x86_64 SOLO se enlaza en el servicio aislado mimesvc (ring 3). */
#include "hoja_util.h"
#include "web_inflate.h"

/* ================================================================ ZIP: leer */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
int zr_es_zip(const uint8_t *d, uint32_t n) { return n >= 22 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4; }
uint8_t *zr_lee(const uint8_t *z, uint32_t n, const char *nombre, uint32_t *len)
{
    *len = 0; if (n < 22) return 0;
    /* el directorio central: firma 06054b50 en los ultimos 64 KiB */
    uint32_t eocd = 0xFFFFFFFFu, desde = n > 65557 ? n - 65557 : 0;
    for (uint32_t i = n - 22 + 1; i-- > desde; ) if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 5 && z[i + 3] == 6) { eocd = i; break; }
    if (eocd == 0xFFFFFFFFu) return 0;
    uint32_t cnt = rd16(z + eocd + 10), cd = rd32(z + eocd + 16), cdtam = rd32(z + eocd + 12);
    if (cd > n || cdtam > n - cd) return 0;
    uint32_t p = cd, ln = sl(nombre);
    for (uint32_t k = 0; k < cnt && k < 65535; ++k) {
        if (p + 46 > n || rd32(z + p) != 0x02014b50) return 0;
        uint32_t flags = rd16(z + p + 8), metodo = rd16(z + p + 10), csize = rd32(z + p + 20), usize = rd32(z + p + 24);
        uint32_t nl = rd16(z + p + 28), xl = rd16(z + p + 30), cl = rd16(z + p + 32), off = rd32(z + p + 42);
        if (p + 46 + nl > n) return 0;
        const char *nm = (const char *)z + p + 46;
        int coincide = nl == ln && igualn(nm, nombre, ln);
        if (!coincide && nl == ln) {                             /* sin mayusculas y con '\' (algunos generadores) */
            coincide = 1; for (uint32_t i = 0; i < ln; ++i) { char a = nm[i] == '\\' ? '/' : nm[i], b = nombre[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) { coincide = 0; break; } }
        }
        if (coincide) {
            if (flags & 1) return 0;                             /* cifrada */
            if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || off == 0xFFFFFFFFu) return 0;   /* ZIP64 */
            if (off > n - 30 || rd32(z + off) != 0x04034b50) return 0;
            uint32_t d0 = off + 30 + rd16(z + off + 26) + rd16(z + off + 28);
            if (d0 > n || csize > n - d0) return 0;
            if (metodo == 0) {
                if (csize > HJF_MAX_PARTE) return 0;
                uint8_t *r = (uint8_t *)hj_alloc(csize + 1); if (!r) return 0;
                copia(r, z + d0, csize); r[csize] = 0; *len = csize; return r;
            }
            if (metodo != 8) return 0;
            struct wb w; zero(&w, (uint32_t)sizeof w);
            uint32_t tope = usize && usize < HJF_MAX_PARTE ? usize + 16 : HJF_MAX_PARTE;
            if (wz_inflate(z + d0, csize, 0, &w, tope) < 0 || w.err || !w.p) { if (w.p) wb_free(&w); return 0; }
            uint8_t *r = (uint8_t *)hj_alloc(w.len + 1); if (!r) { wb_free(&w); return 0; }
            copia(r, w.p, w.len); r[w.len] = 0; *len = w.len; wb_free(&w); return r;
        }
        p += 46 + nl + xl + cl;
    }
    return 0;
}

/* ================================================================ XML */
static int esp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int xr_sig(struct xr *x)
{
    for (;;) {
        while (x->p < x->n && x->s[x->p] != '<') ++x->p;
        if (x->p + 1 >= x->n) return 0;
        uint32_t i = x->p + 1;
        if (x->s[i] == '?' || x->s[i] == '!') {                   /* declaracion, comentario, CDATA, DOCTYPE: se saltan */
            if (i + 2 < x->n && x->s[i] == '!' && x->s[i + 1] == '-' && x->s[i + 2] == '-') { uint32_t j = i + 3; while (j + 2 < x->n && !(x->s[j] == '-' && x->s[j + 1] == '-' && x->s[j + 2] == '>')) ++j; x->p = j + 3; continue; }
            while (i < x->n && x->s[i] != '>') ++i;
            x->p = i + 1; continue;
        }
        x->cierra = 0; x->vacia = 0;
        if (x->s[i] == '/') { x->cierra = 1; ++i; }
        uint32_t ini = i; while (i < x->n && !esp(x->s[i]) && x->s[i] != '>' && x->s[i] != '/') ++i;
        uint32_t a = ini; for (uint32_t k = ini; k < i; ++k) if (x->s[k] == ':') a = k + 1;   /* sin prefijo de espacio de nombres */
        uint32_t L = i - a; if (L >= sizeof x->tag) L = sizeof x->tag - 1;
        copia(x->tag, x->s + a, L); x->tag[L] = 0;
        x->attr = x->s + i;
        uint32_t q = 0; while (i < x->n && (q || x->s[i] != '>')) { if (x->s[i] == '"' || x->s[i] == '\'') { if (!q) q = (uint8_t)x->s[i]; else if ((uint8_t)x->s[i] == q) q = 0; } ++i; }
        x->nattr = (uint32_t)(x->s + i - x->attr);
        if (i > 0 && x->s[i - 1] == '/') x->vacia = 1;
        x->p = i < x->n ? i + 1 : x->n;
        return 1;
    }
}
static uint32_t desescapa(const char *s, uint32_t n, char *o, uint32_t cap, uint32_t p)
{
    for (uint32_t i = 0; i < n && p + 1 < cap; ) {
        if (s[i] == '&') {
            uint32_t j = i + 1; while (j < n && j < i + 12 && s[j] != ';') ++j;
            if (j < n && s[j] == ';') {
                const char *e = s + i + 1; uint32_t el = j - i - 1; uint32_t cp = 0; int ok = 1;
                if (el == 3 && igualn(e, "amp", 3)) cp = '&'; else if (el == 2 && igualn(e, "lt", 2)) cp = '<'; else if (el == 2 && igualn(e, "gt", 2)) cp = '>';
                else if (el == 4 && igualn(e, "quot", 4)) cp = '"'; else if (el == 4 && igualn(e, "apos", 4)) cp = '\'';
                else if (el >= 2 && e[0] == '#') { if (e[1] == 'x' || e[1] == 'X') { for (uint32_t k = 2; k < el; ++k) { char c = e[k]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (v < 0) { ok = 0; break; } cp = cp * 16 + (uint32_t)v; if (cp > 0x10FFFF) { ok = 0; break; } } }
                    else for (uint32_t k = 1; k < el; ++k) { if (e[k] < '0' || e[k] > '9') { ok = 0; break; } cp = cp * 10 + (uint32_t)(e[k] - '0'); if (cp > 0x10FFFF) { ok = 0; break; } } }
                else ok = 0;
                if (ok && cp && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                    if (cp < 0x80) o[p++] = (char)cp;
                    else if (cp < 0x800) { if (p + 2 >= cap) break; o[p++] = (char)(0xC0 | (cp >> 6)); o[p++] = (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { if (p + 3 >= cap) break; o[p++] = (char)(0xE0 | (cp >> 12)); o[p++] = (char)(0x80 | ((cp >> 6) & 63)); o[p++] = (char)(0x80 | (cp & 63)); }
                    else { if (p + 4 >= cap) break; o[p++] = (char)(0xF0 | (cp >> 18)); o[p++] = (char)(0x80 | ((cp >> 12) & 63)); o[p++] = (char)(0x80 | ((cp >> 6) & 63)); o[p++] = (char)(0x80 | (cp & 63)); }
                    i = j + 1; continue;
                }
            }
        }
        o[p++] = s[i++];
    }
    if (cap) o[p < cap ? p : cap - 1] = 0;
    return p;
}
int xr_attr(const struct xr *x, const char *nombre, char *out, uint32_t cap)
{
    const char *s = x->attr; uint32_t n = x->nattr, i = 0, ln = sl(nombre);
    if (cap) out[0] = 0;
    while (i < n) {
        while (i < n && (esp(s[i]) || s[i] == '/')) ++i;
        uint32_t a = i; while (i < n && s[i] != '=' && !esp(s[i])) ++i;
        uint32_t b = i, ia = a; for (uint32_t k = a; k < b; ++k) if (s[k] == ':') ia = k + 1;
        while (i < n && esp(s[i])) ++i;
        if (i >= n || s[i] != '=') { if (i == a) ++i; continue; }
        ++i; while (i < n && esp(s[i])) ++i;
        if (i >= n || (s[i] != '"' && s[i] != '\'')) continue;
        char q = s[i++]; uint32_t v = i; while (i < n && s[i] != q) ++i;
        uint32_t ve = i; if (i < n) ++i;
        if (b - ia == ln && igualn(s + ia, nombre, ln)) { desescapa(s + v, ve - v, out, cap, 0); return 1; }
    }
    return 0;
}
uint32_t xr_texto(struct xr *x, const char *fin_tag, char *out, uint32_t cap)
{
    uint32_t p = 0; if (cap) out[0] = 0;
    if (x->vacia) return 0;
    for (;;) {
        uint32_t a = x->p; while (x->p < x->n && x->s[x->p] != '<') ++x->p;
        p = desescapa(x->s + a, x->p - a, out, cap, p);
        if (x->p >= x->n) return p;
        uint32_t guarda = x->p;
        if (x->p + 8 < x->n && igualn(x->s + x->p, "<![CDATA[", 9)) {
            uint32_t j = x->p + 9; while (j + 2 < x->n && !(x->s[j] == ']' && x->s[j + 1] == ']' && x->s[j + 2] == '>')) ++j;
            for (uint32_t k = x->p + 9; k < j && p + 1 < cap; ++k) out[p++] = x->s[k];
            if (cap) out[p < cap ? p : cap - 1] = 0;
            x->p = j + 3; continue;
        }
        if (!xr_sig(x)) return p;
        if (x->cierra && !igual(x->tag, fin_tag)) continue;
        if (x->cierra) return p;
        /* elemento dentro del texto (p. ej. <text:s/> o <text:span>): espacios de ODS */
        if (igual(x->tag, "s") && x->vacia) { char c[8]; uint32_t k = 1; if (xr_attr(x, "c", c, sizeof c)) { k = 0; for (char *z = c; *z >= '0' && *z <= '9'; ++z) k = k * 10 + (uint32_t)(*z - '0'); if (k > 1000) k = 1000; } while (k-- && p + 1 < cap) out[p++] = ' '; if (cap) out[p] = 0; }
        else if (igual(x->tag, "tab") && x->vacia) { if (p + 1 < cap) { out[p++] = '\t'; out[p] = 0; } }
        else if (igual(x->tag, "line-break") && x->vacia) { if (p + 1 < cap) { out[p++] = '\n'; out[p] = 0; } }
        else if (igual(x->tag, "rPh") && !x->vacia) { while (xr_sig(x)) if (x->cierra && igual(x->tag, "rPh")) break; }   /* fonetica de XLSX: fuera (sin recursion) */
        (void)guarda;
    }
}
