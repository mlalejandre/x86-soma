/* SOMA_290_VOZ_CODIGO: ver prog_voz.h */
#include "prog_voz.h"

static uint32_t L(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void put(char *o, uint32_t cap, uint32_t *q, const char *s) { for (uint32_t i = 0; s && s[i] && *q + 1 < cap; i++) o[(*q)++] = s[i]; if (cap) o[*q < cap ? *q : cap - 1] = 0; }
static void putn(char *o, uint32_t cap, uint32_t *q, uint32_t v) { char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11); while (k && *q + 1 < cap) o[(*q)++] = t[--k]; if (cap) o[*q < cap ? *q : cap - 1] = 0; }
static int has(const char *h, const char *n) { if (!h || !n) return 0; for (; *h; h++) { uint32_t i = 0; while (n[i] && (h[i] | 32) == (n[i] | 32)) i++; if (!n[i]) return 1; } return 0; }
/* "Linea 12: ..." -> 12 (0 si no hay) */
static uint32_t linea(const char *d)
{
    if (!d) return 0;
    for (const char *p = d; *p; p++) {
        if ((p[0] | 32) == 'l' && (p[1] | 32) == 'i' && (p[2] | 32) == 'n' && (p[3] | 32) == 'e' && (p[4] | 32) == 'a' && p[5] == ' ' && p[6] >= '0' && p[6] <= '9') {
            uint32_t v = 0; for (const char *c = p + 6; *c >= '0' && *c <= '9' && v < 1000000; c++) v = v * 10 + (uint32_t)(*c - '0');
            return v;
        }
    }
    return 0;
}

int pv_narra(int fase, const char *desc, const char *det, int n, char *out, uint32_t cap)
{
    uint32_t q = 0; if (cap) out[0] = 0;
    uint32_t ln = linea(det);
    if (fase == 4) {
        if (has(desc, "regenerando")) { put(out, cap, &q, "No terminaba de salir bien: pruebo con una versi\303\263n m\303\241s sencilla."); return 1; }
        if (n == 0) {
            if (ln) { put(out, cap, &q, "La primera versi\303\263n tiene un fallo en la l\303\255nea "); putn(out, cap, &q, ln); put(out, cap, &q, "; lo estoy corrigiendo."); }
            else put(out, cap, &q, "A la primera versi\303\263n le falta algo de lo que pediste; lo estoy corrigiendo.");
            return 1;
        }
        if (n == 1) {
            put(out, cap, &q, "Sigo corrigiendo");
            if (ln) { put(out, cap, &q, ": ahora el fallo est\303\241 en la l\303\255nea "); putn(out, cap, &q, ln); }
            put(out, cap, &q, ".");
            return 1;
        }
        if (n == 3) { put(out, cap, &q, "Todav\303\255a no est\303\241; sigo con ello."); return 1; }
        return 0;
    }
    if (fase == 5) { put(out, cap, &q, "Ya funciona. Le doy un \303\272ltimo repaso para comprobar que hace todo lo que pediste."); return 1; }
    return 0;
}

static int ext(const char *r, const char *e)
{
    uint32_t n = L(r), m = L(e); if (n <= m) return 0;
    for (uint32_t i = 0; i < m; i++) if ((r[n - m + i] | 32) != e[i]) return 0;
    return 1;
}
const char *pv_lenguaje(const char *r)
{
    if (ext(r, ".py")) return "Python";
    if (ext(r, ".c")) return "C";
    if (ext(r, ".js")) return "JavaScript";
    if (ext(r, ".html") || ext(r, ".htm")) return "HTML";
    return "SOMA-LANG";
}
void pv_nombre(const char *r, char *out, uint32_t cap)
{
    if (!cap) return;
    out[0] = 0; if (!r) return;
    uint32_t n = L(r), a = 0, e;
    for (uint32_t i = 0; i < n; i++) if (r[i] == '/') a = i + 1;
    e = n; for (uint32_t i = a; i < n; i++) if (r[i] == '.') e = i;
    if (e == a) e = n;                                     /* ".algo": el nombre entero */
    uint32_t q = 0;
    for (uint32_t i = a; i < e && q + 1 < cap; i++) out[q++] = (r[i] == '_' || r[i] == '-') ? ' ' : r[i];
    out[q] = 0;
}

void pv_limpia(char *s, uint32_t max)
{
    if (!s) return;
    char *src = s;
    { const char *t = 0; for (char *p = s; *p; p++) if (p[0] == '<' && p[1] == '/' && (p[2] | 32) == 't' && (p[3] | 32) == 'h' && (p[4] | 32) == 'i' && (p[5] | 32) == 'n' && (p[6] | 32) == 'k' && p[7] == '>') t = p + 8; if (t) src = (char *)t; }
    uint32_t q = 0; int code = 0, frases = 0;
    for (char *p = src; *p; ) {
        if (p[0] == '`' && p[1] == '`' && p[2] == '`') { code = !code; p += 3; continue; }
        if (code) { p++; continue; }
        char c = *p++;
        if (c == '*' || c == '#' || c == '`' || c == '>' || c == '|') continue;
        if (c == '_' && (q == 0 || s[q - 1] == ' ' || *p == ' ' || *p == 0)) continue;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == '-' && (q == 0 || s[q - 1] == ' ') && *p == ' ') continue;   /* vinetas */
        if (c == ' ' && (q == 0 || s[q - 1] == ' ')) continue;
        s[q++] = c;                                         /* q <= p - s: se escribe detras de lo que se lee */
        if ((c == '.' || c == '!' || c == '?') && (*p == ' ' || *p == '\n' || *p == 0)) { if (++frases >= 3) break; }
    }
    while (q && s[q - 1] == ' ') q--;
    s[q] = 0;
    if (max && q > max) {                                   /* recortar al final de una frase, o de una palabra */
        uint32_t k = max; while (k > max / 2 && !(s[k - 1] == '.' || s[k - 1] == '!' || s[k - 1] == '?')) k--;
        if (k <= max / 2) { k = max; while (k > 0 && s[k] != ' ') k--; while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--; if (k && s[k - 1] != '.') { s[k] = '.'; k++; } }
        s[k] = 0;
    }
}

void pv_final(int ok, int editar, const char *ruta, const char *expl, const char *error, char *out, uint32_t cap)
{
    uint32_t q = 0; if (cap) out[0] = 0;
    char nom[96]; pv_nombre(ruta, nom, sizeof nom);
    const char *lg = pv_lenguaje(ruta);
    int web = lg[0] == 'H';
    if (ok) {
        if (editar) { put(out, cap, &q, "Listo: he hecho los cambios en \302\253"); put(out, cap, &q, nom); put(out, cap, &q, "\302\273 y sigue funcionando."); }
        else if (web) { put(out, cap, &q, "Listo: he creado la p\303\241gina web \302\253"); put(out, cap, &q, nom); put(out, cap, &q, "\302\273, con el HTML, el estilo y el JavaScript en un solo archivo, y est\303\241 comprobada."); }
        else { put(out, cap, &q, "Listo: he creado \302\253"); put(out, cap, &q, nom); put(out, cap, &q, "\302\273 en "); put(out, cap, &q, lg); put(out, cap, &q, " y funciona."); }
        if (expl && expl[0]) { put(out, cap, &q, " "); put(out, cap, &q, expl); }
        put(out, cap, &q, " Lo tienes abierto en SOMA Studio.");
        return;
    }
    put(out, cap, &q, editar ? "No he conseguido hacer los cambios sin romper el programa" : "No he conseguido que el programa funcione");
    uint32_t ln = linea(error);
    if (ln) { put(out, cap, &q, ": el \303\272ltimo fallo est\303\241 en la l\303\255nea "); putn(out, cap, &q, ln); }
    put(out, cap, &q, ". El archivo queda como estaba; prueba a ped\303\255rmelo con m\303\241s detalle o algo m\303\241s sencillo.");
}
