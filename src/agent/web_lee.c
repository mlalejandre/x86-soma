/* SOMA_288_INVESTIGA: ver web_lee.h */
#include "web_lee.h"

static int low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int pre_ci(const char *s, const char *e, const char *p) { while (*p) { if (s >= e || low((unsigned char)*s) != *p) return 0; s++; p++; } return 1; }
static const char *busca_ci(const char *s, const char *e, const char *p)
{
    for (; s < e; s++) if (low((unsigned char)*s) == *p && pre_ci(s, e, p)) return s;
    return 0;
}
static int es_nombre(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

/* nombre de la etiqueta que empieza en s ('<' ya pasado); devuelve su longitud */
static int etiqueta(const char *s, const char *e, char *nom, int cap, int *cierre)
{
    int k = 0; *cierre = 0;
    if (s < e && *s == '/') { *cierre = 1; s++; }
    while (s < e && es_nombre((unsigned char)*s) && k + 1 < cap) nom[k++] = (char)low((unsigned char)*s++);
    nom[k] = 0;
    return k;
}
static int en(const char *n, const char *const *l) { for (int i = 0; l[i]; i++) { const char *a = n, *b = l[i]; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return 1; } return 0; }

static uint32_t put_cp(char *o, uint32_t cap, uint32_t q, uint32_t cp)
{
    if (cp == 0xA0) cp = ' ';
    if (cp < 0x80) { if (q + 1 < cap) o[q++] = (char)cp; }
    else if (cp < 0x800) { if (q + 2 < cap) { o[q++] = (char)(0xC0 | (cp >> 6)); o[q++] = (char)(0x80 | (cp & 63)); } }
    else if (cp < 0x10000) { if (q + 3 < cap) { o[q++] = (char)(0xE0 | (cp >> 12)); o[q++] = (char)(0x80 | ((cp >> 6) & 63)); o[q++] = (char)(0x80 | (cp & 63)); } }
    else if (q + 4 < cap) { o[q++] = (char)(0xF0 | (cp >> 18)); o[q++] = (char)(0x80 | ((cp >> 12) & 63)); o[q++] = (char)(0x80 | ((cp >> 6) & 63)); o[q++] = (char)(0x80 | (cp & 63)); }
    return q;
}
/* entidad en s ('&'): consume y escribe; devuelve bytes consumidos (0 = no lo era) */
static uint32_t entidad(const char *s, const char *e, char *o, uint32_t cap, uint32_t *q)
{
    const char *f = s + 1; int n = 0; while (f + n < e && n < 10 && f[n] != ';' && f[n] != ' ' && f[n] != '<') n++;
    if (f + n >= e || f[n] != ';' || !n) return 0;
    uint32_t cp = 0;
    if (f[0] == '#') {
        int hex = n > 1 && (f[1] == 'x' || f[1] == 'X');
        for (int i = hex ? 2 : 1; i < n; i++) { int c = low((unsigned char)f[i]), v = c >= '0' && c <= '9' ? c - '0' : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1; if (v < 0) return 0; cp = cp * (hex ? 16u : 10u) + (uint32_t)v; if (cp > 0x10FFFF) return 0; }
    } else {
        static const struct { const char *n; uint32_t cp; } E[] = { { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
            { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 }, { "Aacute", 0xC1 }, { "Eacute", 0xC9 },
            { "Iacute", 0xCD }, { "Oacute", 0xD3 }, { "Uacute", 0xDA }, { "Ntilde", 0xD1 }, { "uuml", 0xFC }, { "iquest", 0xBF }, { "iexcl", 0xA1 }, { "laquo", 0xAB },
            { "raquo", 0xBB }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 }, { "rdquo", 0x201D },
            { "ldquo", 0x201C }, { "euro", 0x20AC }, { "copy", 0xA9 }, { "deg", 0xB0 }, { "middot", 0xB7 }, { "ordm", 0xBA }, { "ordf", 0xAA }, { 0, 0 } };
        for (int i = 0; E[i].n; i++) { int k = 0; while (E[i].n[k] && k < n && E[i].n[k] == f[k]) k++; if (!E[i].n[k] && k == n) { cp = E[i].cp; break; } }
        if (!cp) return 0;
    }
    *q = put_cp(o, cap, *q, cp);
    return (uint32_t)n + 2;
}

/* extrae el texto de [s, e) en o (sin filtrar lineas) */
static uint32_t extrae(const char *s, const char *e, char *o, uint32_t cap, char *tit, uint32_t tcap)
{
    static const char *const SALTA[] = { "script", "style", "noscript", "svg", "nav", "footer", "header", "aside", "form", "button", "select", "template",
                                         "iframe", "figcaption", "menu", "dialog", "canvas", "math", 0 };
    static const char *const BLOQUE[] = { "p", "div", "br", "li", "tr", "section", "article", "blockquote", "pre", "dd", "dt", "table", "ul", "ol", "main",
                                          "h1", "h2", "h3", "h4", "h5", "h6", "hr", "td", "th", "figure", "caption", 0 };
    uint32_t q = 0; int sp = 1;
    for (const char *p = s; p < e && q + 8 < cap; ) {
        if (*p == '<') {
            if (p + 3 < e && p[1] == '!' && p[2] == '-' && p[3] == '-') { const char *c = busca_ci(p + 4, e, "-->"); p = c ? c + 3 : e; continue; }
            char nom[16]; int cierre; int k = etiqueta(p + 1, e, nom, sizeof nom, &cierre);
            const char *g = p + 1; while (g < e && *g != '>') g++;
            if (!k) { p = g < e ? g + 1 : e; continue; }
            if (!cierre && nom[0] == 't' && nom[1] == 'i' && nom[2] == 't' && nom[3] == 'l' && nom[4] == 'e' && !nom[5]) {   /* <title> */
                const char *te = busca_ci(g, e, "</title");
                if (te && tit && tcap) { uint32_t tq = 0; for (const char *c = g + 1; c < te && tq + 2 < tcap; ) { if (*c == '&') { uint32_t u = entidad(c, te, tit, tcap, &tq); if (u) { c += u; continue; } } char ch = *c == '\n' || *c == '\t' || *c == '\r' ? ' ' : *c; if (!(ch == ' ' && (!tq || tit[tq - 1] == ' '))) tit[tq++] = ch; c++; } while (tq && tit[tq - 1] == ' ') tq--; tit[tq] = 0; }
                p = te ? te : e; continue;
            }
            if (!cierre && en(nom, SALTA)) {                                   /* salta hasta su cierre */
                char fin[20] = "</"; int f = 2; for (int i = 0; nom[i] && f < 18; i++) fin[f++] = nom[i]; fin[f] = 0;
                if (g < e && g > p && g[-1] == '/') { p = g + 1; continue; }        /* <svg/> */
                const char *c = busca_ci(g, e, fin); p = c ? c : e;
                while (p < e && *p != '>') p++;
                if (p < e) p++;
                continue;
            }
            if (en(nom, BLOQUE)) {
                while (q && o[q - 1] == ' ') q--;
                if (q && o[q - 1] != '\n') o[q++] = '\n';
                if (!cierre && nom[0] == 'h' && nom[1] >= '1' && nom[1] <= '4' && !nom[2]) { o[q++] = '#'; o[q++] = '#'; o[q++] = ' '; }
                if (!cierre && nom[0] == 'l' && nom[1] == 'i' && !nom[2]) { o[q++] = '-'; o[q++] = ' '; }
                sp = 1;
            }
            p = g < e ? g + 1 : e;
            continue;
        }
        if (*p == '&') { uint32_t u = entidad(p, e, o, cap, &q); if (u) { p += u; sp = 0; continue; } }
        char c = *p++;
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { if (!sp) { o[q++] = ' '; sp = 1; } continue; }
        o[q++] = c; sp = 0;
    }
    o[q] = 0;
    return q;
}
static int util(const char *l, uint32_t n)                             /* una linea que vale la pena */
{
    if (n >= 2 && l[0] == '#' && l[1] == '#') return n >= 6;
    if (n >= 2 && l[0] == '-' && l[1] == ' ') return n >= 30;
    if (n < 60) return 0;
    int letras = 0, espacios = 0; for (uint32_t i = 0; i < n; i++) { if (l[i] == ' ') espacios++; else if ((unsigned char)l[i] >= 'A') letras++; }
    return espacios >= 6 && letras * 2 > (int)n;
}
uint32_t wl_texto(const char *html, uint32_t n, char *out, uint32_t cap, char *titulo, uint32_t tcap)
{
    if (!cap) return 0;
    out[0] = 0; if (titulo && tcap) titulo[0] = 0;
    if (!html || !n) return 0;
    const char *s = html, *e = html + n;
    /* <article> o <main>: si tienen texto de verdad, solo eso */
    const char *a = busca_ci(s, e, "<article"), *a2 = a ? busca_ci(a, e, "</article") : 0;
    if (!a || !a2) { a = busca_ci(s, e, "<main"); a2 = a ? busca_ci(a, e, "</main") : 0; }
    uint32_t q;
    if (a && a2 && a2 - a > 2000) {
        const char *ti = busca_ci(s, a, "<title"), *te = ti ? busca_ci(ti, e, "</title") : 0;
        if (ti && te) (void)extrae(ti, te + 8, out, cap, titulo, tcap);            /* solo el titulo */
        q = extrae(a, a2, out, cap, 0, 0);
        uint32_t util_n = 0; for (uint32_t i = 0, ls = 0; i <= q; i++) if (i == q || out[i] == '\n') { if (util(out + ls, i - ls)) util_n += i - ls; ls = i + 1; }
        if (util_n < 400) q = extrae(s, e, out, cap, titulo, tcap);
    } else q = extrae(s, e, out, cap, titulo, tcap);
    /* filtro de lineas: solo las utiles, sin repetir la anterior, separadas por una linea en blanco.
     * SOMA_313_LECTOR_WEB: se hace en dos pasadas sobre el mismo bufer. Antes se escribia "\n\n" entre lineas en una sola pasada,
     * pero en el original solo habia un "\n": con dos lineas utiles seguidas la escritura adelantaba a la lectura y machacaba la
     * linea siguiente (salia una ristra de saltos de linea y el parrafo se perdia).
     * 1) compactar con UN salto: lo escrito nunca pasa de lo leido (cada linea util ya tenia al menos su "\n" detras); */
    uint32_t w = 0, ls = 0; uint32_t prev_s = 0, prev_n = 0, nlin = 0;
    for (uint32_t i = 0; i <= q; i++) {
        if (i < q && out[i] != '\n') continue;
        uint32_t a0 = ls, n0 = i - ls; ls = i + 1;
        while (n0 && out[a0] == ' ') { a0++; n0--; }
        while (n0 && out[a0 + n0 - 1] == ' ') n0--;
        if (!util(out + a0, n0)) continue;
        if (n0 == prev_n) { uint32_t k = 0; while (k < n0 && out[prev_s + k] == out[a0 + k]) k++; if (k == n0) continue; }
        if (nlin) out[w++] = '\n';
        prev_s = w;
        for (uint32_t k = 0; k < n0; k++) out[w++] = out[a0 + k];      /* w <= a0: hacia delante sin pisar */
        prev_n = n0; nlin++;
    }
    /* 2) cada salto pasa a ser doble, de derecha a izquierda; si no cabe en cap - 1, fuera lineas del final hasta que quepa */
    while (nlin > 1 && w + (nlin - 1) > cap - 1) {
        while (w && out[w - 1] != '\n') w--;
        if (w) w--;
        nlin--;
    }
    if (nlin <= 1) { out[w] = 0; return w; }
    uint32_t total = w + (nlin - 1), d = total;
    for (uint32_t i = w; i-- > 0; ) {
        out[--d] = out[i];
        if (out[i] == '\n') out[--d] = '\n';
    }
    out[total] = 0;
    return total;
}

void wl_dominio(const char *url, char *out, uint32_t cap)
{
    uint32_t q = 0; const char *p = url;
    while (*p && *p != ':') p++;
    if (p[0] == ':' && p[1] == '/' && p[2] == '/') p += 3; else p = url;
    if (pre_ci(p, p + 4, "www.")) p += 4;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && q + 1 < cap) out[q++] = (char)low((unsigned char)*p++);
    if (cap) out[q] = 0;
}
int wl_url_util(const char *url)
{
    if (!url || !(pre_ci(url, url + 8, "https://") || pre_ci(url, url + 7, "http://"))) return 0;
    char d[128]; wl_dominio(url, d, sizeof d);
    static const char *const MALO[] = { "duckduckgo.com", "youtube.com", "youtu.be", "facebook.com", "instagram.com", "twitter.com", "x.com", "tiktok.com",
                                        "pinterest.com", "pinterest.es", "linkedin.com", "reddit.com", "amazon.com", "amazon.es", "bing.com", "google.com", 0 };
    for (int i = 0; MALO[i]; i++) {
        uint32_t L = 0, D = 0; while (MALO[i][L]) L++; while (d[D]) D++;
        if (D >= L) { uint32_t k = 0; while (k < L && d[D - L + k] == MALO[i][k]) k++; if (k == L && (D == L || d[D - L - 1] == '.')) return 0; }
    }
    uint32_t n = 0; while (url[n]) n++;
    static const char *const EXT[] = { ".pdf", ".zip", ".mp4", ".mp3", ".jpg", ".png", ".exe", ".doc", ".docx", ".xls", ".xlsx", 0 };
    for (int i = 0; EXT[i]; i++) { uint32_t L = 0; while (EXT[i][L]) L++; if (n > L && pre_ci(url + n - L, url + n, EXT[i])) return 0; }
    return 1;
}
int wl_urls(const char *r, char urls[][300], int max)
{
    int n = 0;
    for (const char *p = r; p && *p && n < max; p++) {
        if (!(p[0] == '(' && p[1] == 'h' && p[2] == 't' && p[3] == 't' && p[4] == 'p')) continue;
        const char *a = p + 1, *e = a; int prof = 0;   /* "(https://es.wikipedia.org/wiki/Mercurio_(planeta))": parentesis dentro de la URL */
        while (*e && *e != '\n' && *e != ' ' && !(*e == ')' && prof == 0)) { if (*e == '(') prof++; else if (*e == ')') prof--; e++; }
        if (*e != ')') continue;
        char u[300]; uint32_t k = 0; for (const char *c = a; c < e && k + 1 < sizeof u; c++) u[k++] = *c;
        u[k] = 0; p = e;
        if (!wl_url_util(u)) continue;
        int dup = 0, mismo = 0; char d[128], d2[128]; wl_dominio(u, d, sizeof d);
        for (int i = 0; i < n; i++) {
            uint32_t z = 0; while (u[z] && u[z] == urls[i][z]) z++; if (!u[z] && !urls[i][z]) dup = 1;
            wl_dominio(urls[i], d2, sizeof d2); z = 0; while (d[z] && d[z] == d2[z]) z++; if (!d[z] && !d2[z]) mismo++;
        }
        if (dup || mismo >= 2) continue;
        for (uint32_t i = 0; i <= k; i++) urls[n][i] = u[i];
        n++;
    }
    return n;
}
