/* SOMA_337_NOTICIAS: lector de RSS/Atom y piezas puras de la aplicacion de Noticias. Ver noticias_rss.h. */
#include "noticias_rss.h"

static uint32_t nlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int pre(const char *s, const char *e, const char *p) { while (*p) { if (s >= e || *s != *p) return 0; s++; p++; } return 1; }
static const char *busca(const char *s, const char *e, const char *p)
{
    uint32_t k = nlen(p);
    for (; s + k <= e; s++) if (pre(s, e, p)) return s;
    return 0;
}
static char minus(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* ---------------------------------------------------------------- texto */
static uint32_t utf8_pon(char *o, uint32_t cap, uint32_t n, uint32_t cp)
{
    char b[4]; uint32_t k = 0;
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return n;
    if (cp < 0x80) b[k++] = (char)cp;
    else if (cp < 0x800) { b[k++] = (char)(0xC0 | (cp >> 6)); b[k++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { b[k++] = (char)(0xE0 | (cp >> 12)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
    else { b[k++] = (char)(0xF0 | (cp >> 18)); b[k++] = (char)(0x80 | ((cp >> 12) & 63)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
    if (n + k + 1 > cap) return n;
    for (uint32_t i = 0; i < k; i++) o[n++] = b[i];
    return n;
}
static const struct { const char *n; uint32_t cp; } ENT[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
    { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 }, { "uuml", 0xFC },
    { "Aacute", 0xC1 }, { "Eacute", 0xC9 }, { "Iacute", 0xCD }, { "Oacute", 0xD3 }, { "Uacute", 0xDA }, { "Ntilde", 0xD1 }, { "Uuml", 0xDC },
    { "agrave", 0xE0 }, { "egrave", 0xE8 }, { "ograve", 0xF2 }, { "ccedil", 0xE7 }, { "Ccedil", 0xC7 }, { "iexcl", 0xA1 }, { "iquest", 0xBF },
    { "laquo", 0xAB }, { "raquo", 0xBB }, { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "euro", 0x20AC }, { "copy", 0xA9 }, { "reg", 0xAE }, { "deg", 0xB0 }, { "middot", 0xB7 },
    { "bull", 0x2022 }, { "ordf", 0xAA }, { "ordm", 0xBA }, { "pound", 0xA3 }, { "times", 0xD7 },
};
/* &...; en s (s apunta a '&'): pone el caracter y devuelve cuantos bytes ocupaba (0 si no es una entidad) */
static uint32_t entidad(const char *s, const char *e, char *o, uint32_t cap, uint32_t *n)
{
    const char *f = s + 1; uint32_t k = 0;
    while (f + k < e && k < 10 && f[k] != ';' && f[k] != '&' && f[k] != '<' && f[k] != ' ') k++;
    if (f + k >= e || f[k] != ';' || !k) return 0;
    uint32_t cp = 0;
    if (f[0] == '#') {
        int hex = k > 1 && (f[1] == 'x' || f[1] == 'X');
        uint32_t i = hex ? 2 : 1; if (i >= k) return 0;
        for (; i < k; i++) {
            char c = f[i]; uint32_t d;
            if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
            else if (hex && minus(c) >= 'a' && minus(c) <= 'f') d = (uint32_t)(minus(c) - 'a' + 10);
            else return 0;
            cp = cp * (hex ? 16u : 10u) + d; if (cp > 0x10FFFF) return 0;
        }
    } else {
        int hallada = 0;
        for (uint32_t j = 0; j < sizeof ENT / sizeof ENT[0] && !hallada; j++) {
            const char *a = ENT[j].n; uint32_t z = 0; while (z < k && a[z] && a[z] == f[z]) z++;
            if (z == k && !a[z]) { cp = ENT[j].cp; hallada = 1; }
        }
        if (!hallada) return 0;
    }
    *n = utf8_pon(o, cap, *n, cp);
    return k + 2;
}
/* [a, e) -> o: CDATA desenvuelto, entidades resueltas. Si html, ademas las etiquetas fuera (y <br>/<p> como espacio). */
static uint32_t decodifica(const char *a, const char *e, char *o, uint32_t cap, int html)
{
    uint32_t n = 0;
    if (!cap) return 0;
    while (a < e && n + 1 < cap) {
        if (pre(a, e, "<![CDATA[")) {
            const char *f = busca(a + 9, e, "]]>"); if (!f) f = e;
            n += decodifica(a + 9, f, o + n, cap - n, html);     /* lo de dentro puede traer HTML (y entidades, si es HTML) */
            a = f < e ? f + 3 : e; continue;
        }
        if (*a == '<' && html) {                                 /* etiqueta: fuera (con > dentro de comillas no se rompe) */
            const char *f = a + 1; char q = 0;
            while (f < e && (q || *f != '>')) { if (q) { if (*f == q) q = 0; } else if (*f == '"' || *f == '\'') q = *f; f++; }
            if (n && o[n - 1] != ' ') o[n++] = ' ';
            a = f < e ? f + 1 : e; continue;
        }
        if (*a == '&') { uint32_t k = entidad(a, e, o, cap, &n); if (k) { a += k; continue; } }
        o[n++] = *a++;
    }
    o[n] = 0;
    return n;
}
/* espacios juntados y recortados; corta en cap sin partir un caracter */
static void limpia_txt(char *s, uint32_t cap)
{
    uint32_t n = 0, i = 0; int esp = 1;
    for (; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C) { if (!esp) { s[n++] = ' '; esp = 1; } continue; }
        if (c < 32 || c == 127) continue;
        s[n++] = (char)c; esp = 0;
    }
    while (n && s[n - 1] == ' ') n--;
    if (n > cap - 1) { n = cap - 1; while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--; while (n && s[n - 1] == ' ') n--; }
    s[n] = 0;
}
/* texto de un campo: dos pasadas (RSS suele llevar HTML escapado: &lt;p&gt;...) */
static void campo(const char *a, const char *e, char *o, uint32_t cap, int html)
{
    static char t1[8192], t2[8192];
    if (e - a > 60000) e = a + 60000;
    decodifica(a, e, t1, sizeof t1, 0);
    decodifica(t1, t1 + nlen(t1), t2, sizeof t2, html);
    limpia_txt(t2, sizeof t2);
    uint32_t n = nlen(t2); if (n > cap - 1) { n = cap - 1; while (n && ((unsigned char)t2[n] & 0xC0) == 0x80) n--; }
    for (uint32_t i = 0; i < n; i++) o[i] = t2[i];
    o[n] = 0;
    limpia_txt(o, cap);
}
/* contenido de la primera <etq ...>...</etq> en [a, e); *attr: lo de dentro de la etiqueta de apertura */
static const char *elemento(const char *a, const char *e, const char *etq, const char **fin, const char **attr, const char **attr_fin)
{
    uint32_t k = nlen(etq);
    for (const char *s = a; s && s < e; ) {
        s = busca(s, e, "<"); if (!s) return 0;
        if (pre(s + 1, e, etq) && s + 1 + k < e && (s[1 + k] == '>' || s[1 + k] == ' ' || s[1 + k] == '/' || s[1 + k] == '\t' || s[1 + k] == '\n' || s[1 + k] == '\r')) {
            const char *g = s + 1 + k; char q = 0;
            while (g < e && (q || *g != '>')) { if (q) { if (*g == q) q = 0; } else if (*g == '"' || *g == '\'') q = *g; g++; }
            if (g >= e) return 0;
            if (attr) { *attr = s + 1 + k; *attr_fin = g; }
            if (g[-1] == '/') { *fin = g + 1; return g + 1; }       /* <link href="..."/> */
            const char *c = g + 1, *z = c;
            for (;;) {                                               /* </etq>, saltando CDATA */
                const char *cd = busca(z, e, "<![CDATA["), *ce = busca(z, e, "</");
                if (cd && (!ce || cd < ce)) { const char *f = busca(cd + 9, e, "]]>"); if (!f) { *fin = e; return c; } z = f + 3; continue; }
                if (!ce) { *fin = e; return c; }
                if (pre(ce + 2, e, etq) && ce + 2 + k < e && ce[2 + k] == '>') { *fin = ce; return c; }
                z = ce + 2;
            }
        }
        s++;
    }
    return 0;
}
static void atributo(const char *a, const char *e, const char *nom, char *o, uint32_t cap)
{
    if (cap) o[0] = 0;
    uint32_t k = nlen(nom);
    for (const char *s = a; s && s + k + 1 < e; s++) {
        if ((s == a || s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n') && pre(s, e, nom) && s[k] == '=') {
            const char *v = s + k + 1; char q = (*v == '"' || *v == '\'') ? *v++ : 0;
            const char *f = v; while (f < e && (q ? *f != q : (*f != ' ' && *f != '>' && *f != '/'))) f++;
            decodifica(v, f, o, cap, 0); limpia_txt(o, cap);
            return;
        }
    }
}
static int url_ok(const char *u) { return pre(u, u + 8, "https://") || pre(u, u + 7, "http://"); }

/* ---------------------------------------------------------------- fechas */
static int64_t dias(int y, int m, int d)
{
    int64_t yy = y - (m <= 2), era = (yy >= 0 ? yy : yy - 399) / 400, yoe = yy - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static int num(const char **p, int max)
{
    int v = 0, k = 0;
    while (**p >= '0' && **p <= '9' && k < max) { v = v * 10 + (**p - '0'); (*p)++; k++; }
    return k ? v : -1;
}
static int zona_min(const char *z)               /* "GMT" "+0100" "-05:00" "Z" "EST"... -> minutos a restar para UTC */
{
    while (*z == ' ') z++;
    if (*z == '+' || *z == '-') {
        int sg = *z++ == '-' ? -1 : 1; const char *q = z;
        int h = num(&q, 2); if (h < 0) return 0;
        if (*q == ':') q++;
        int m = num(&q, 2); if (m < 0) m = 0;
        return sg * (h * 60 + m);
    }
    static const struct { const char *n; int m; } Z[] = { { "EST", -300 }, { "EDT", -240 }, { "CST", -360 }, { "CDT", -300 }, { "MST", -420 },
        { "MDT", -360 }, { "PST", -480 }, { "PDT", -420 }, { "CET", 60 }, { "CEST", 120 }, { "BST", 60 }, { "WET", 0 }, { "WEST", 60 } };
    for (uint32_t i = 0; i < sizeof Z / sizeof Z[0]; i++) { const char *a = Z[i].n, *b = z; while (*a && *a == *b) { a++; b++; } if (!*a && !(*b >= 'A' && *b <= 'Z')) return Z[i].m; }
    return 0;                                     /* GMT, UT, UTC, Z o desconocida */
}
int64_t nt_fecha(const char *s)
{
    if (!s) return 0;
    while (*s == ' ') s++;
    int y, mo, d, h = 0, mi = 0, se = 0;
    const char *p = s;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9' && p[4] == '-') {   /* ISO 8601 */
        y = num(&p, 4); p++; mo = num(&p, 2); if (*p != '-') return 0; p++; d = num(&p, 2);
        if (*p == 'T' || *p == ' ') { p++; h = num(&p, 2); if (*p == ':') { p++; mi = num(&p, 2); } if (*p == ':') { p++; se = num(&p, 2); } while (*p == '.' || (*p >= '0' && *p <= '9')) p++; }
    } else {                                                                                                                                 /* RFC 822 */
        while (*p && !(*p >= '0' && *p <= '9')) p++;                     /* "Tue, " */
        d = num(&p, 2); while (*p == ' ') p++;
        static const char *const M = "janfebmaraprmayjunjulaugsepoctnovdec";
        char m3[3] = { minus(p[0]), p[0] ? minus(p[1]) : 0, p[0] && p[1] ? minus(p[2]) : 0 };
        mo = -1; for (int i = 0; i < 12; i++) if (M[i * 3] == m3[0] && M[i * 3 + 1] == m3[1] && M[i * 3 + 2] == m3[2]) mo = i + 1;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        y = num(&p, 4); if (y >= 0 && y < 100) y += y < 70 ? 2000 : 1900;
        while (*p == ' ') p++;
        h = num(&p, 2); if (*p == ':') { p++; mi = num(&p, 2); } if (*p == ':') { p++; se = num(&p, 2); }
    }
    if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60) return 0;
    int64_t t = (dias(y, mo, d) * 24 + h) * 60 + mi - zona_min(p);
    return t > 0 ? t : 0;
}

/* ---------------------------------------------------------------- canal */
int nt_rss(const char *xml, uint32_t n, struct nt_item *it, int max, const char *fuente_def)
{
    if (!xml || !it || max <= 0) return 0;
    const char *e = xml + n; int k = 0;
    for (uint32_t i = 0; i < n; i++) if (!xml[i]) { e = xml + i; break; }   /* un NUL corta */
    char canal[NT_FTE]; canal[0] = 0;
    {   /* nombre del canal (antes del primer elemento) para la fuente */
        const char *pi = busca(xml, e, "<item"), *pe = busca(xml, e, "<entry"); const char *lim = pi && (!pe || pi < pe) ? pi : pe ? pe : e;
        const char *f, *c = elemento(xml, lim, "title", &f, 0, 0);
        if (c) campo(c, f, canal, sizeof canal, 1);
    }
    const char *s = xml;
    while (s < e && k < max) {
        const char *a = busca(s, e, "<item"), *b = busca(s, e, "<entry");
        const char *ini; int atom;
        if (a && (!b || a < b)) { ini = a; atom = 0; } else if (b) { ini = b; atom = 1; } else break;
        const char *t = ini + (atom ? 6 : 5);
        if (t < e && !(*t == '>' || *t == ' ' || *t == '\n' || *t == '\r' || *t == '\t')) { s = t; continue; }   /* <items>, <entryX> */
        const char *fin = busca(t, e, atom ? "</entry>" : "</item>"), *sig = busca(t, e, atom ? "<entry" : "<item");
        if (!fin || (sig && sig < fin)) fin = sig ? sig : e;
        struct nt_item *x = &it[k];
        x->tit[0] = x->url[0] = x->fuente[0] = x->desc[0] = 0; x->t = 0;
        const char *f, *c, *at, *af;
        if ((c = elemento(t, fin, "title", &f, 0, 0)) != 0) campo(c, f, x->tit, sizeof x->tit, 1);
        if (!atom) {
            if ((c = elemento(t, fin, "link", &f, &at, &af)) != 0) campo(c, f, x->url, sizeof x->url, 0);
            if (!url_ok(x->url) && (c = elemento(t, fin, "guid", &f, 0, 0)) != 0) campo(c, f, x->url, sizeof x->url, 0);
        } else {                                                   /* <link rel="alternate" href="..."/> (o el primero) */
            const char *z = t;
            while (z < fin && (c = elemento(z, fin, "link", &f, &at, &af)) != 0) {
                char rel[24], href[NT_URL]; atributo(at, af, "rel", rel, sizeof rel); atributo(at, af, "href", href, sizeof href);
                if (url_ok(href) && (!x->url[0] || !rel[0] || (rel[0] == 'a' && rel[1] == 'l'))) { uint32_t q = 0; while (href[q] && q + 1 < sizeof x->url) { x->url[q] = href[q]; q++; } x->url[q] = 0; if (!rel[0] || rel[0] == 'a') break; }
                z = f > z ? f : z + 1;
            }
        }
        if (!url_ok(x->url)) x->url[0] = 0;
        char fe[64]; fe[0] = 0;
        if ((c = elemento(t, fin, "pubDate", &f, 0, 0)) || (c = elemento(t, fin, "dc:date", &f, 0, 0)) || (c = elemento(t, fin, "published", &f, 0, 0)) || (c = elemento(t, fin, "updated", &f, 0, 0)))
            campo(c, f, fe, sizeof fe, 0);
        x->t = nt_fecha(fe);
        if ((c = elemento(t, fin, "description", &f, 0, 0)) || (c = elemento(t, fin, "summary", &f, 0, 0)) || (c = elemento(t, fin, "content:encoded", &f, 0, 0)) || (c = elemento(t, fin, "content", &f, 0, 0)))
            campo(c, f, x->desc, sizeof x->desc, 1);
        if ((c = elemento(t, fin, "source", &f, 0, 0)) || (c = elemento(t, fin, "dc:creator", &f, 0, 0))) campo(c, f, x->fuente, sizeof x->fuente, 1);
        else if (atom && (c = elemento(t, fin, "author", &f, 0, 0)) != 0) { const char *f2, *c2 = elemento(c, f, "name", &f2, 0, 0); if (c2) campo(c2, f2, x->fuente, sizeof x->fuente, 1); }
        {   /* Google Noticias: "Titular - Medio" */
            uint32_t L = nlen(x->tit); int corte = -1;
            for (uint32_t i = 0; i + 3 <= L; i++) if (x->tit[i] == ' ' && x->tit[i + 1] == '-' && x->tit[i + 2] == ' ') corte = (int)i;
            if (corte > 10 && L - (uint32_t)corte - 3 < 50) {
                const char *m = x->tit + corte + 3;
                if (!x->fuente[0]) { uint32_t q = 0; while (m[q] && q + 1 < sizeof x->fuente) { x->fuente[q] = m[q]; q++; } x->fuente[q] = 0; x->tit[corte] = 0; }
                else { uint32_t q = 0; while (m[q] && minus(m[q]) == minus(x->fuente[q])) q++; if (!m[q] && !x->fuente[q]) x->tit[corte] = 0; }
            }
        }
        if (!x->fuente[0]) { const char *fd = fuente_def && *fuente_def ? fuente_def : canal; uint32_t q = 0; while (fd[q] && q + 1 < sizeof x->fuente) { x->fuente[q] = fd[q]; q++; } x->fuente[q] = 0; }
        {   /* un resumen que solo repite el titular no aporta */
            uint32_t q = 0; while (x->tit[q] && minus(x->tit[q]) == minus(x->desc[q])) q++;
            if (!x->tit[q] && (x->desc[q] == 0 || x->desc[q] == ' ')) { uint32_t L = nlen(x->desc); if (L < nlen(x->tit) + 40) x->desc[0] = 0; }
        }
        if (x->tit[0]) k++;
        s = fin > t ? fin : t;
    }
    return k;
}

void nt_sanea(struct nt_item *it, int n)
{
    for (int i = 0; i < n; i++) {
        it[i].tit[NT_TIT - 1] = 0; it[i].url[NT_URL - 1] = 0; it[i].fuente[NT_FTE - 1] = 0; it[i].desc[NT_DESC - 1] = 0;
        if (it[i].t < 0) it[i].t = 0;
        if (!url_ok(it[i].url)) it[i].url[0] = 0;
    }
}
static void clave(const char *t, char *o, uint32_t cap)
{
    uint32_t n = 0;
    for (; *t && n + 1 < cap; t++) { char c = minus(*t); if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || ((unsigned char)c >= 0x80)) o[n++] = c; }
    o[n] = 0;
}
static int igual(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
int nt_limpia(struct nt_item *it, int n)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        char ki[80]; clave(it[i].tit, ki, sizeof ki);
        if (!ki[0]) continue;
        int rep = 0;
        for (int j = 0; j < m && !rep; j++) { char kj[80]; clave(it[j].tit, kj, sizeof kj); if (igual(ki, kj) || (it[i].url[0] && igual(it[i].url, it[j].url))) rep = 1; }
        if (rep) continue;
        if (m != i) it[m] = it[i];
        m++;
    }
    for (int i = 1; i < m; i++) {                              /* insercion: de mas nuevo a mas viejo, estable */
        struct nt_item x = it[i]; int j = i;
        while (j > 0 && it[j - 1].t < x.t) { it[j] = it[j - 1]; j--; }
        it[j] = x;
    }
    return m;
}
static void pon(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void pond(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int k = 0; if (v < 0) v = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 23); while (k) { char c[2] = { b[--k], 0 }; pon(o, cap, p, c); } }
void nt_hace(int64_t ahora, int64_t t, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    if (!t) return;
    int64_t d = ahora - t;
    if (d < 1) { pon(o, cap, &p, "ahora"); return; }
    if (d < 60) { pon(o, cap, &p, "hace "); pond(o, cap, &p, d); pon(o, cap, &p, " min"); return; }
    if (d < 24 * 60) { pon(o, cap, &p, "hace "); pond(o, cap, &p, d / 60); pon(o, cap, &p, " h"); return; }
    if (d < 48 * 60) { pon(o, cap, &p, "ayer"); return; }
    if (d < 7 * 24 * 60) { pon(o, cap, &p, "hace "); pond(o, cap, &p, d / 1440); pon(o, cap, &p, " dias"); return; }
    int64_t z = t / 1440 + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int dd = (int)(doy - (153 * mp + 2) / 5 + 1), mm = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (dd < 10) pon(o, cap, &p, "0");
    pond(o, cap, &p, dd); pon(o, cap, &p, "/"); if (mm < 10) pon(o, cap, &p, "0"); pond(o, cap, &p, mm);
}

/* ---------------------------------------------------------------- categorias y configuracion */
static const struct { const char *id, *nombre, *tema; } CATS[NTC_N] = {
    { "portada", "Portada", 0 }, { "espana", "Espa\303\261a", "NATION" }, { "local", "Local", 0 }, { "mundo", "Mundo", "WORLD" },
    { "economia", "Econom\303\255a", "BUSINESS" }, { "tecnologia", "Tecnolog\303\255a", "TECHNOLOGY" }, { "ciencia", "Ciencia", "SCIENCE" },
    { "salud", "Salud", "HEALTH" }, { "deportes", "Deportes", "SPORTS" }, { "cultura", "Cultura", "ENTERTAINMENT" },
};
const char *nt_cat_nombre(int c) { return c >= 0 && c < NTC_N ? CATS[c].nombre : ""; }
const char *nt_cat_id(int c) { return c >= 0 && c < NTC_N ? CATS[c].id : ""; }
int nt_cat_busca(const char *s)
{
    char a[32]; uint32_t n = 0;
    if (!s) return -1;
    while (*s == ' ') s++;
    while (*s && *s != ' ' && *s != ',' && *s != '\n' && *s != '\r' && n + 1 < sizeof a) {
        unsigned char c = (unsigned char)*s;
        if (c == 0xC3 && s[1]) { unsigned char d = (unsigned char)s[1] | 0x20; a[n++] = d == 0xA1 ? 'a' : d == 0xA9 ? 'e' : d == 0xAD ? 'i' : d == 0xB3 ? 'o' : d == 0xBA ? 'u' : d == 0xB1 ? 'n' : '?'; s += 2; continue; }
        a[n++] = minus((char)c); s++;
    }
    a[n] = 0;
    if (!n) return -1;
    for (int i = 0; i < NTC_N; i++) { const char *b = CATS[i].id; uint32_t k = 0; while (k < n && a[k] == b[k]) k++; if (k == n && (n >= 4 || !b[k])) return i; }   /* "tecno" vale */
    return -1;
}
uint32_t nt_cat_url(int c, const char *lugar, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    if (c < 0 || c >= NTC_N) return 0;
    if (c == NTC_PORTADA) pon(o, cap, &p, "https://news.google.com/rss?hl=es&gl=ES&ceid=ES:es");
    else if (c == NTC_LOCAL) {
        if (!lugar || !*lugar) return 0;
        pon(o, cap, &p, "https://news.google.com/rss/search?q=");
        static const char HX[] = "0123456789ABCDEF";
        for (const char *s = lugar; *s && p + 4 < cap; s++) {
            unsigned char b = (unsigned char)*s;
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '-' || b == '.' || b == '_') o[p++] = (char)b;
            else if (b == ' ') o[p++] = '+';
            else { o[p++] = '%'; o[p++] = HX[b >> 4]; o[p++] = HX[b & 15]; }
        }
        o[p] = 0;
        pon(o, cap, &p, "+when%3A2d&hl=es&gl=ES&ceid=ES:es");          /* lo de los ultimos dos dias */
    } else {
        pon(o, cap, &p, "https://news.google.com/rss/headlines/section/topic/"); pon(o, cap, &p, CATS[c].tema); pon(o, cap, &p, "?hl=es&gl=ES&ceid=ES:es");
    }
    return p;
}
void nt_conf_defecto(struct nt_conf *c)
{
    c->cats = (1u << NTC_PORTADA) | (1u << NTC_ESPANA) | (1u << NTC_MUNDO) | (1u << NTC_TECNO) | (1u << NTC_CIENCIA) | (1u << NTC_DEPORTES);
    c->lugar[0] = 0; c->nextra = 0;
}
static const char *linea(const char *t, const char *k)            /* valor de "k=" al principio de una linea */
{
    uint32_t n = nlen(k);
    for (const char *s = t; *s; ) {
        if (pre(s, s + n + 1, k) && s[n] == '=') return s + n + 1;
        while (*s && *s != '\n') s++;
        if (*s) s++;
    }
    return 0;
}
void nt_conf_lee(struct nt_conf *c, const char *t)
{
    nt_conf_defecto(c);
    if (!t) return;
    const char *v = linea(t, "categorias");
    if (v) {
        c->cats = 0;
        while (*v && *v != '\n') { int i = nt_cat_busca(v); if (i >= 0) c->cats |= 1u << i; while (*v && *v != ',' && *v != '\n') v++; if (*v == ',') v++; }
    }
    if ((v = linea(t, "lugar")) != 0) { uint32_t n = 0; while (*v && *v != '\n' && *v != '\r' && n + 1 < sizeof c->lugar) c->lugar[n++] = *v++; c->lugar[n] = 0; limpia_txt(c->lugar, sizeof c->lugar); }
    for (const char *s = t; *s && c->nextra < NT_EXTRA; ) {
        if (pre(s, s + 7, "fuente=")) {
            const char *a = s + 7; uint32_t n = 0; char *d = c->extra[c->nextra];
            while (*a && *a != '\n' && *a != '\r' && *a != ' ' && n + 1 < NT_URL) d[n++] = *a++;
            d[n] = 0;
            if (url_ok(d)) c->nextra++;
        }
        while (*s && *s != '\n') s++;
        if (*s) s++;
    }
}
uint32_t nt_conf_escribe(const struct nt_conf *c, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    pon(o, cap, &p, "categorias=");
    int prim = 1;
    for (int i = 0; i < NTC_N; i++) if (c->cats & (1u << i)) { if (!prim) pon(o, cap, &p, ","); pon(o, cap, &p, CATS[i].id); prim = 0; }
    pon(o, cap, &p, "\nlugar="); pon(o, cap, &p, c->lugar); pon(o, cap, &p, "\n");
    for (int i = 0; i < c->nextra && i < NT_EXTRA; i++) { pon(o, cap, &p, "fuente="); pon(o, cap, &p, c->extra[i]); pon(o, cap, &p, "\n"); }
    return p;
}
