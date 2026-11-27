/* SOMA_357_CONTACTOS: ver contactos_core.h. Sin libc: se compila en el nucleo, en el servicio aislado mimesvc y en el anfitrion. */
#include "contactos_core.h"

/* ================================================================ cadenas */
static uint32_t sl(const char *s) { uint32_t n = 0; while (s[n]) ++n; return n; }
static void zero(void *p, uint32_t n) { uint8_t *q = (uint8_t *)p; while (n--) *q++ = 0; }
static void copia_mem(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }
/* copia n bytes sin partir un caracter UTF-8 al recortar */
static void cpyn(char *d, uint32_t cap, const char *s, uint32_t n)
{
    if (!cap) return;
    if (n > cap - 1) { n = cap - 1; while (n && ((uint8_t)s[n] & 0xC0) == 0x80) --n; }
    for (uint32_t i = 0; i < n; ++i) d[i] = s[i];
    d[n] = 0;
}
static void cpy(char *d, uint32_t cap, const char *s) { cpyn(d, cap, s, sl(s)); }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putc_(char *o, uint32_t cap, uint32_t *p, char c) { if (*p + 1 < cap) { o[(*p)++] = c; o[*p] = 0; } }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v, int ancho)
{
    char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
    if (v < 0) putc_(o, cap, p, '-');
    do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22);
    while (n < ancho && n < 22) b[n++] = '0';
    while (n) putc_(o, cap, p, b[--n]);
}
static int es_dig(char c) { return c >= '0' && c <= '9'; }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* siguiente caracter "plegado" (minuscula sin acento; un byte). Los caracteres que no son latinos se dan tal cual, byte a byte. */
static char pliega_sig(const char **pp)
{
    const uint8_t *p = (const uint8_t *)*pp; uint8_t c = p[0];
    if (!c) return 0;
    if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
        uint8_t d = p[1]; *pp += 2;
        if (d >= 0xA0) d = (uint8_t)(d - 0x20);                  /* minuscula -> mayuscula (0x80..0x9F) */
        if (d <= 0x85) return 'a';
        if (d == 0x87) return 'c';
        if (d >= 0x88 && d <= 0x8B) return 'e';
        if (d >= 0x8C && d <= 0x8F) return 'i';
        if (d == 0x91) return 'n';
        if ((d >= 0x92 && d <= 0x96) || d == 0x98) return 'o';
        if (d >= 0x99 && d <= 0x9C) return 'u';
        if (d == 0x9D || d == 0x9F) return 'y';
        if (d == 0x86) return 'a';
        return '?';
    }
    *pp += 1;
    return baja((char)c);
}
int ct_pliega(const char *s, char *out, uint32_t cap)
{
    uint32_t n = 0; if (!cap) return 0;
    while (*s && n + 1 < cap) { char c = pliega_sig(&s); if (!c) break; out[n++] = c; }
    out[n] = 0; return (int)n;
}
static int contiene(const char *pajar, const char *aguja)      /* ambas ya plegadas */
{
    uint32_t na = sl(aguja); if (!na) return 1;
    for (uint32_t i = 0; pajar[i]; ++i) { uint32_t k = 0; while (k < na && pajar[i + k] == aguja[k]) ++k; if (k == na) return 1; }
    return 0;
}
static int igual_pl(const char *a, const char *b)               /* iguales sin mayusculas ni acentos */
{
    for (;;) { char x = pliega_sig(&a), y = pliega_sig(&b); if (x != y) return 0; if (!x) return 1; }
}
static int ieq(const char *a, const char *b) { while (*a && *b) { if (baja(*a) != baja(*b)) return 0; ++a; ++b; } return !*a && !*b; }
static int iemp(const char *s, const char *pre) { while (*pre) { if (baja(*s) != baja(*pre)) return 0; ++s; ++pre; } return 1; }
static uint32_t digitos(const char *s, char *out, uint32_t cap) { uint32_t n = 0; for (; *s; ++s) if (es_dig(*s) && n + 1 < cap) out[n++] = *s; if (cap) out[n] = 0; return n; }
static int tel_igual(const char *a, const char *b)              /* mismos ultimos 9 digitos (+34 600... = 600...) */
{
    char x[40], y[40]; uint32_t nx = digitos(a, x, sizeof x), ny = digitos(b, y, sizeof y);
    if (nx < 6 || ny < 6) return nx == ny && nx && igual_pl(x, y);
    uint32_t k = nx < ny ? nx : ny; if (k > 9) k = 9;
    for (uint32_t i = 1; i <= k; ++i) if (x[nx - i] != y[ny - i]) return 0;
    return 1;
}

/* ================================================================ UTF-8 */
static int utf8_len(const uint8_t *s, uint32_t n)               /* bytes del caracter valido que empieza en s, o 0 */
{
    uint8_t c = s[0]; int k;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) k = 2; else if (c >= 0xE0 && c <= 0xEF) k = 3; else if (c >= 0xF0 && c <= 0xF4) k = 4; else return 0;
    if ((uint32_t)k > n) return 0;
    for (int i = 1; i < k; ++i) if ((s[i] & 0xC0) != 0x80) return 0;
    if (k == 3 && c == 0xE0 && s[1] < 0xA0) return 0;
    if (k == 3 && c == 0xED && s[1] >= 0xA0) return 0;
    if (k == 4 && c == 0xF0 && s[1] < 0x90) return 0;
    if (k == 4 && c == 0xF4 && s[1] >= 0x90) return 0;
    return k;
}
static int utf8_valido(const char *s, uint32_t n) { uint32_t i = 0; while (i < n) { int k = utf8_len((const uint8_t *)s + i, n - i); if (!k) return 0; i += (uint32_t)k; } return 1; }
/* latin-1 -> UTF-8 (cap >= 2n+1 para no recortar) */
static uint32_t latin1_utf8(const char *s, uint32_t n, char *o, uint32_t cap)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < n && p + 1 < cap; ++i) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) o[p++] = (char)c;
        else { if (p + 2 >= cap) break; o[p++] = (char)(0xC0 | (c >> 6)); o[p++] = (char)(0x80 | (c & 0x3F)); }
    }
    o[p] = 0; return p;
}
/* deja una cadena de un campo en UTF-8 valido, sin controles (salvo \n si multilinea), sin blancos sobrantes, y terminada */
static void limpia_campo(char *s, uint32_t cap, int multilinea)
{
    s[cap - 1] = 0;
    uint32_t n = sl(s), w = 0, i = 0;
    while (i < n) {
        int k = utf8_len((const uint8_t *)s + i, n - i);
        if (!k) { s[w++] = '?'; ++i; continue; }
        if (k == 1) {
            char c = s[i];
            if (c == '\n' && multilinea) s[w++] = c;
            else if ((uint8_t)c < 32 || c == 127) { if (w && s[w - 1] != ' ') s[w++] = ' '; }
            else s[w++] = c;
            ++i; continue;
        }
        for (int j = 0; j < k; ++j) s[w++] = s[i + (uint32_t)j];
        i += (uint32_t)k;
    }
    s[w] = 0;
    uint32_t a = 0; while (s[a] == ' ' || s[a] == '\n') ++a;
    while (w > a && (s[w - 1] == ' ' || s[w - 1] == '\n')) --w;
    if (a) for (uint32_t j = a; j <= w; ++j) s[j - a] = s[j];
    s[w - a] = 0;
}

/* ================================================================ fechas */
static int bisiesto(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int dias_mes(int y, int m) { static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; if (m < 1 || m > 12) return 0; return m == 2 && (y == 0 || bisiesto(y)) ? 29 : D[m - 1]; }
static int64_t dias_civil(int y, int m, int d)
{
    y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static const char *const MESES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
const char *ct_nombre_mes(int m) { return m >= 1 && m <= 12 ? MESES[m - 1] : ""; }
static int mes_de(const char *w)                                /* palabra plegada -> 1..12 o 0 */
{
    for (int i = 0; i < 12; ++i) if (iemp(w, MESES[i]) || (sl(w) >= 3 && iemp(MESES[i], w))) return i + 1;
    if (iemp(w, "setiembre")) return 9;
    return 0;
}
static int leer_num(const char **s, int maxdig)
{
    int v = 0, n = 0; while (es_dig(**s) && n < maxdig) { v = v * 10 + (**s - '0'); ++*s; ++n; }
    return n ? v : -1;
}
int ct_parsea_fecha(const char *txt, int *py, int *pm, int *pd)
{
    char f[96]; ct_pliega(txt, f, sizeof f);
    const char *s = f; int y = 0, m = 0, d = 0;
    while (*s == ' ' || *s == ':' || *s == '=') ++s;
    for (int k = 0; k < 4; ++k) {                                /* "el", "dia", "es", "en" delante */
        if (iemp(s, "el ")) s += 3; else if (iemp(s, "dia ")) s += 4; else if (iemp(s, "es ")) s += 3; else if (iemp(s, "en ")) s += 3; else if (iemp(s, "nacio ")) s += 6;
        while (*s == ' ') ++s;
    }
    if (s[0] == '-' && s[1] == '-') {                            /* --MMDD o --MM-DD (vCard sin ano) */
        s += 2; m = leer_num(&s, 2); if (*s == '-') ++s; d = leer_num(&s, 2);
    } else {
        const char *q = s; int a = leer_num(&q, 8); int na = (int)(q - s);
        if (a < 0) return -1;
        if (na == 8 && (!*q || *q == 'T' || *q == 't' || *q == ' ')) { y = a / 10000; m = a / 100 % 100; d = a % 100; }   /* 19900503 */
        else if (na == 4 && (*q == '-' || *q == '/')) {           /* 1990-05-03 */
            y = a; ++q; m = leer_num(&q, 2); if (*q == '-' || *q == '/') ++q; d = leer_num(&q, 2);
        } else if (na <= 2 && (*q == '/' || *q == '-' || *q == '.')) {   /* 3/5/1990, 3-5-90, 3.5 */
            char sep = *q; d = a; ++q; m = leer_num(&q, 2);
            if (*q == sep || *q == '/' || *q == '-' || *q == '.') { ++q; const char *r = q; y = leer_num(&q, 4); if (y >= 0 && q - r == 2) y += y > 30 ? 1900 : 2000; if (y < 0) y = 0; }
        } else if (na <= 2) {                                    /* 3 de mayo [de 1990] */
            d = a; while (*q == ' ') ++q; if (iemp(q, "de ")) q += 3; while (*q == ' ') ++q;
            char w[16]; int k = 0; while (*q && *q != ' ' && *q != ',' && k < 15) w[k++] = *q++; w[k] = 0;
            m = mes_de(w); if (!m) return -1;
            while (*q == ' ') ++q;
            if (iemp(q, "de ")) q += 3; else if (iemp(q, "del ")) q += 4;
            while (*q == ' ') ++q;
            if (es_dig(*q)) { y = leer_num(&q, 4); if (y < 0) y = 0; }
        } else return -1;
    }
    if (y == 1604 || y == 1900 || y == 4) y = 0;                  /* "sin ano" de Apple / algunos moviles */
    if (y && (y < 1800 || y > 2200)) return -1;
    if (m < 1 || m > 12 || d < 1 || d > dias_mes(y, m)) return -1;
    *py = y; *pm = m; *pd = d; return 0;
}
int ct_dias_para_cumple(const struct ct_contacto *c, int hy, int hm, int hd)
{
    if (!c->nac_m) return -1;
    int64_t hoy = dias_civil(hy, hm, hd);
    for (int y = hy; y <= hy + 1; ++y) {
        int d = c->nac_d; if (c->nac_m == 2 && d == 29 && !bisiesto(y)) d = 28;
        int64_t t = dias_civil(y, c->nac_m, d);
        if (t >= hoy) return (int)(t - hoy);
    }
    return -1;
}
int ct_edad(const struct ct_contacto *c, int hy, int hm, int hd)
{
    if (!c->nac_m || !c->nac_y) return -1;
    int e = hy - c->nac_y; if (hm < c->nac_m || (hm == c->nac_m && hd < c->nac_d)) --e;
    return e >= 0 && e < 150 ? e : -1;
}

/* ================================================================ la libreta */
void ct_vacia(struct ct_libreta *l) { zero(l, (uint32_t)sizeof *l); l->sig_id = 1; }
struct ct_contacto *ct_nuevo(struct ct_libreta *l)
{
    if (!l->sig_id) l->sig_id = 1;
    for (int i = 0; i < CT_MAX; ++i) if (!l->c[i].activo) {
        struct ct_contacto *c = &l->c[i]; zero(c, (uint32_t)sizeof *c);
        c->id = l->sig_id++; c->activo = 1; return c;
    }
    return 0;
}
struct ct_contacto *ct_busca_id(struct ct_libreta *l, uint32_t id)
{
    if (!id) return 0;
    for (int i = 0; i < CT_MAX; ++i) if (l->c[i].activo && l->c[i].id == id) return &l->c[i];
    return 0;
}
int ct_borra(struct ct_libreta *l, uint32_t id) { struct ct_contacto *c = ct_busca_id(l, id); if (!c) return 0; zero(c, (uint32_t)sizeof *c); return 1; }
uint32_t ct_numero(const struct ct_libreta *l) { uint32_t n = 0; for (int i = 0; i < CT_MAX; ++i) n += l->c[i].activo ? 1u : 0u; return n; }
int ct_vacio(const struct ct_contacto *c)
{
    if (c->nombre[0] || c->apellidos[0] || c->apodo[0] || c->empresa[0]) return 0;
    for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0]) return 0;
    for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0]) return 0;
    return 1;
}
#define LIMPIA(f, ml) limpia_campo(f, (uint32_t)sizeof f, ml)
void ct_sanea(struct ct_contacto *c)
{
    LIMPIA(c->nombre, 0); LIMPIA(c->apellidos, 0); LIMPIA(c->apodo, 0);
    for (int i = 0; i < CT_NEMAIL; ++i) { LIMPIA(c->email[i], 0); if (c->t_email[i] > CT_T_MOVIL) c->t_email[i] = CT_T_OTRO; }
    for (int i = 0; i < CT_NTEL; ++i) { LIMPIA(c->tel[i], 0); if (c->t_tel[i] > CT_T_MOVIL) c->t_tel[i] = CT_T_OTRO; }
    LIMPIA(c->calle, 0); LIMPIA(c->ciudad, 0); LIMPIA(c->cp, 0); LIMPIA(c->prov, 0); LIMPIA(c->pais, 0);
    LIMPIA(c->grupo, 0); LIMPIA(c->empresa, 0); LIMPIA(c->cargo, 0); LIMPIA(c->web, 0); LIMPIA(c->nota, 1);
    LIMPIA(c->uid, 0); LIMPIA(c->rev, 0); LIMPIA(c->foto_url, 0);
    /* los huecos de email y telefono, juntos al principio */
    for (int i = 0, w = 0; i < CT_NEMAIL; ++i) if (c->email[i][0]) { if (i != w) { copia_mem(c->email[w], c->email[i], CT_EMAIL); c->t_email[w] = c->t_email[i]; c->email[i][0] = 0; c->t_email[i] = 0; } ++w; }
    for (int i = 0, w = 0; i < CT_NTEL; ++i) if (c->tel[i][0]) { if (i != w) { copia_mem(c->tel[w], c->tel[i], CT_TEL); c->t_tel[w] = c->t_tel[i]; c->tel[i][0] = 0; c->t_tel[i] = 0; } ++w; }
    if (c->foto_url[0] && !iemp(c->foto_url, "https://") && !iemp(c->foto_url, "http://")) c->foto_url[0] = 0;
    if (c->nac_m < 1 || c->nac_m > 12 || c->nac_y < 0 || (c->nac_y && (c->nac_y < 1800 || c->nac_y > 2200))) { c->nac_m = 0; c->nac_d = 0; c->nac_y = 0; }
    if (c->nac_m && (c->nac_d < 1 || c->nac_d > dias_mes(c->nac_y, c->nac_m))) { c->nac_m = 0; c->nac_d = 0; c->nac_y = 0; }
    if (c->nfotos > CT_FOTOS) c->nfotos = CT_FOTOS;
    c->fav = c->fav ? 1 : 0; c->cambiado = c->cambiado ? 1 : 0; c->voz = c->voz ? 1 : 0; c->activo = c->activo ? 1 : 0;
    c->pad_[0] = c->pad_[1] = c->pad_[2] = 0;
}

/* ================================================================ texto */
void ct_nombre_completo(const struct ct_contacto *c, char *out, uint32_t cap)
{
    uint32_t p = 0; if (!cap) return; out[0] = 0;
    put(out, cap, &p, c->nombre);
    if (c->apellidos[0]) { if (p) putc_(out, cap, &p, ' '); put(out, cap, &p, c->apellidos); }
    if (p) return;
    if (c->apodo[0]) put(out, cap, &p, c->apodo);
    else if (c->empresa[0]) put(out, cap, &p, c->empresa);
    else if (c->email[0][0]) put(out, cap, &p, c->email[0]);
    else if (c->tel[0][0]) put(out, cap, &p, c->tel[0]);
    else put(out, cap, &p, "(sin nombre)");
}
static uint32_t primer_car(const char *s, char *o)              /* primer caracter UTF-8 de s, en mayuscula si es ASCII/latino */
{
    while (*s == ' ' || *s == '(' || *s == '"' || *s == '\'') ++s;
    int k = utf8_len((const uint8_t *)s, sl(s)); if (!k || !*s) return 0;
    for (int i = 0; i < k; ++i) o[i] = s[i];
    if (k == 1 && o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32);
    if (k == 2 && (uint8_t)o[0] == 0xC3 && (uint8_t)o[1] >= 0xA0 && (uint8_t)o[1] <= 0xBE && (uint8_t)o[1] != 0xB7) o[1] = (char)((uint8_t)o[1] - 0x20);
    return (uint32_t)k;
}
void ct_iniciales(const struct ct_contacto *c, char out[8])
{
    uint32_t n = 0; out[0] = 0;
    if (c->nombre[0] || c->apellidos[0]) {
        n += primer_car(c->nombre[0] ? c->nombre : c->apellidos, out + n);
        if (c->nombre[0] && c->apellidos[0]) n += primer_car(c->apellidos, out + n);
    } else {
        char nc[CT_NOM + CT_APE]; ct_nombre_completo(c, nc, sizeof nc); n += primer_car(nc, out);
    }
    out[n] = 0;
}
const char *ct_nombre_tipo(int t) { return t == CT_T_CASA ? "casa" : t == CT_T_TRABAJO ? "trabajo" : t == CT_T_MOVIL ? "m\303\263vil" : ""; }
void ct_linea_txt(const struct ct_contacto *c, char *out, uint32_t cap)
{
    uint32_t p = 0; char nc[CT_NOM + CT_APE + 4]; if (!cap) return; out[0] = 0;
    put(out, cap, &p, "["); putn(out, cap, &p, c->id, 0); put(out, cap, &p, "] ");
    ct_nombre_completo(c, nc, sizeof nc); put(out, cap, &p, nc);
    if (c->fav) put(out, cap, &p, " \342\230\205");
    if (c->email[0][0]) { put(out, cap, &p, "  "); put(out, cap, &p, c->email[0]); }
    if (c->tel[0][0]) { put(out, cap, &p, "  "); put(out, cap, &p, c->tel[0]); }
    if (c->grupo[0]) { put(out, cap, &p, "  ("); put(out, cap, &p, c->grupo); put(out, cap, &p, ")"); }
}
static void linea_dato(char *o, uint32_t cap, uint32_t *p, const char *et, const char *v)
{
    if (!v[0]) return;
    put(o, cap, p, et); put(o, cap, p, ": "); put(o, cap, p, v); putc_(o, cap, p, '\n');
}
uint32_t ct_ficha_txt(const struct ct_contacto *c, int hy, int hm, int hd, char *o, uint32_t cap)
{
    uint32_t p = 0; char nc[CT_NOM + CT_APE + 4]; if (!cap) return 0; o[0] = 0;
    ct_nombre_completo(c, nc, sizeof nc);
    put(o, cap, &p, "["); putn(o, cap, &p, c->id, 0); put(o, cap, &p, "] "); put(o, cap, &p, nc); if (c->fav) put(o, cap, &p, "  \342\230\205 favorito"); putc_(o, cap, &p, '\n');
    linea_dato(o, cap, &p, "Nombre", c->nombre); linea_dato(o, cap, &p, "Apellidos", c->apellidos); linea_dato(o, cap, &p, "Apodo", c->apodo);
    for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0]) {
        put(o, cap, &p, "Email"); if (c->t_email[i]) { put(o, cap, &p, " ("); put(o, cap, &p, ct_nombre_tipo(c->t_email[i])); put(o, cap, &p, ")"); }
        put(o, cap, &p, ": "); put(o, cap, &p, c->email[i]); putc_(o, cap, &p, '\n');
    }
    for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0]) {
        put(o, cap, &p, "Tel\303\251fono"); if (c->t_tel[i]) { put(o, cap, &p, " ("); put(o, cap, &p, ct_nombre_tipo(c->t_tel[i])); put(o, cap, &p, ")"); }
        put(o, cap, &p, ": "); put(o, cap, &p, c->tel[i]); putc_(o, cap, &p, '\n');
    }
    if (c->calle[0] || c->ciudad[0] || c->cp[0] || c->prov[0] || c->pais[0]) {
        put(o, cap, &p, "Direcci\303\263n: "); int k = 0;
        const char *const P[5] = { c->calle, c->cp, c->ciudad, c->prov, c->pais };
        for (int i = 0; i < 5; ++i) if (P[i][0]) { if (k++) put(o, cap, &p, i == 2 && P[1][0] ? " " : ", "); put(o, cap, &p, P[i]); }
        putc_(o, cap, &p, '\n');
    }
    if (c->nac_m) {
        put(o, cap, &p, "Nacimiento: "); putn(o, cap, &p, c->nac_d, 0); put(o, cap, &p, " de "); put(o, cap, &p, ct_nombre_mes(c->nac_m));
        if (c->nac_y) { put(o, cap, &p, " de "); putn(o, cap, &p, c->nac_y, 0); }
        if (hy) {
            int e = ct_edad(c, hy, hm, hd), f = ct_dias_para_cumple(c, hy, hm, hd);
            if (e >= 0) { put(o, cap, &p, " ("); putn(o, cap, &p, e, 0); put(o, cap, &p, " a\303\261os)"); }
            if (f == 0) put(o, cap, &p, "  \342\200\224 \302\241hoy es su cumplea\303\261os!");
            else if (f == 1) put(o, cap, &p, "  \342\200\224 cumple ma\303\261ana");
            else if (f > 1 && f <= 30) { put(o, cap, &p, "  \342\200\224 cumple en "); putn(o, cap, &p, f, 0); put(o, cap, &p, " d\303\255as"); }
        }
        putc_(o, cap, &p, '\n');
    }
    linea_dato(o, cap, &p, "Grupo", c->grupo); linea_dato(o, cap, &p, "Empresa", c->empresa); linea_dato(o, cap, &p, "Cargo", c->cargo);
    linea_dato(o, cap, &p, "Web", c->web); linea_dato(o, cap, &p, "Nota", c->nota);
    if (c->nfotos) { put(o, cap, &p, "Fotos: "); putn(o, cap, &p, c->nfotos, 0); putc_(o, cap, &p, '\n'); }
    if (c->voz) put(o, cap, &p, "Voz: hay una muestra grabada\n");
    return p;
}

/* ================================================================ busqueda y orden */
int ct_coincide(const struct ct_contacto *c, const char *texto)
{
    char t[96], f[CT_CALLE + 8]; ct_pliega(texto, t, sizeof t);
    uint32_t a = 0; while (t[a] == ' ') ++a;
    const char *q = t + a; if (!*q) return 1;
    char nc[CT_NOM + CT_APE + 4]; ct_nombre_completo(c, nc, sizeof nc);
    const char *const C[] = { nc, c->apodo, c->email[0], c->email[1], c->email[2], c->empresa, c->grupo, c->ciudad, c->cargo };
    for (uint32_t i = 0; i < sizeof C / sizeof C[0]; ++i) { ct_pliega(C[i], f, sizeof f); if (contiene(f, q)) return 1; }
    char dq[40]; if (digitos(q, dq, sizeof dq) >= 3) {
        int solo = 1; for (const char *s = q; *s; ++s) if (!es_dig(*s) && *s != ' ' && *s != '+' && *s != '-' && *s != '.') solo = 0;
        if (solo) for (int i = 0; i < CT_NTEL; ++i) { char dt[40]; digitos(c->tel[i], dt, sizeof dt); if (contiene(dt, dq)) return 1; }
    }
    return 0;
}
int ct_en_grupo(const struct ct_contacto *c, const char *g)
{
    const char *s = c->grupo;
    while (*s) {
        while (*s == ' ' || *s == ',') ++s;
        const char *e = s; while (*e && *e != ',') ++e;
        const char *f = e; while (f > s && f[-1] == ' ') --f;
        char t[CT_GRUPO]; cpyn(t, sizeof t, s, (uint32_t)(f - s));
        if (t[0] && igual_pl(t, g)) return 1;
        s = e;
    }
    return 0;
}
static int cmp_nombre(const struct ct_contacto *a, const struct ct_contacto *b)
{
    char x[CT_NOM + CT_APE + 4], y[CT_NOM + CT_APE + 4];
    ct_nombre_completo(a, x, sizeof x); ct_nombre_completo(b, y, sizeof y);
    const char *p = x, *q = y;
    for (;;) { char u = pliega_sig(&p), v = pliega_sig(&q); if (u != v) return (uint8_t)u < (uint8_t)v ? -1 : 1; if (!u) break; }
    return a->id < b->id ? -1 : a->id > b->id;
}
static void ordena(const struct ct_libreta *l, uint16_t *idx, int n)   /* Shell sort: sin memoria extra */
{
    static const int G[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
    for (int g = 0; g < 8; ++g) for (int i = G[g]; i < n; ++i) {
        uint16_t t = idx[i]; int j = i;
        while (j >= G[g] && cmp_nombre(&l->c[idx[j - G[g]]], &l->c[t]) > 0) { idx[j] = idx[j - G[g]]; j -= G[g]; }
        idx[j] = t;
    }
}
int ct_lista(const struct ct_libreta *l, const char *texto, const char *grupo, int solo_fav, uint16_t *idx, int max)
{
    int n = 0;
    for (int i = 0; i < CT_MAX && n < max; ++i) {
        const struct ct_contacto *c = &l->c[i]; if (!c->activo) continue;
        if (solo_fav && !c->fav) continue;
        if (grupo && *grupo && !ct_en_grupo(c, grupo)) continue;
        if (texto && *texto && !ct_coincide(c, texto)) continue;
        idx[n++] = (uint16_t)i;
    }
    ordena(l, idx, n);
    return n;
}
static int pal_pref(const char *campo_pl, const char *w)        /* w es prefijo de alguna palabra de campo (ambos plegados) */
{
    uint32_t nw = sl(w); if (!nw) return 0;
    for (const char *s = campo_pl; *s; ) {
        while (*s == ' ' || *s == '-') ++s;
        uint32_t k = 0; while (k < nw && s[k] == w[k]) ++k;
        if (k == nw) return 1;
        while (*s && *s != ' ' && *s != '-') ++s;
    }
    return 0;
}
int ct_resuelve(const struct ct_libreta *l, const char *txt, uint16_t *idx, int max)
{
    while (*txt == ' ') ++txt;
    if (!*txt || max <= 0) return 0;
    { const char *q = txt; int v = 0, nd = 0; while (es_dig(*q) && nd < 9) { v = v * 10 + (*q - '0'); ++q; ++nd; } while (*q == ' ') ++q;
      if (nd && !*q) { for (int i = 0; i < CT_MAX; ++i) if (l->c[i].activo && l->c[i].id == (uint32_t)v) { idx[0] = (uint16_t)i; return 1; } } }
    char t[96]; ct_pliega(txt, t, sizeof t);
    { uint32_t n = sl(t); while (n && (t[n - 1] == ' ' || t[n - 1] == '?' || t[n - 1] == '.')) t[--n] = 0; }
    static const char *const ART[] = { "a ", "de ", "del ", "el ", "la ", "al ", 0 };
    for (int k = 0; ART[k]; ++k) if (iemp(t, ART[k])) { uint32_t m = sl(ART[k]), j = 0; while (t[m + j]) { t[j] = t[m + j]; ++j; } t[j] = 0; break; }
    int mejor = 0, n = 0;
    for (int i = 0; i < CT_MAX; ++i) {
        const struct ct_contacto *c = &l->c[i]; if (!c->activo) continue;
        char nc[CT_NOM + CT_APE + 4], f[CT_NOM + CT_APE + 4], g[CT_NOM + 4], ap[CT_APODO + 4];
        ct_nombre_completo(c, nc, sizeof nc); ct_pliega(nc, f, sizeof f); ct_pliega(c->nombre, g, sizeof g); ct_pliega(c->apodo, ap, sizeof ap);
        int s = 0;
        if (!sl(t)) break;
        if (ieq(f, t)) s = 100;
        for (int e = 0; e < CT_NEMAIL; ++e) if (c->email[e][0] && ieq(c->email[e], t)) s = 100;
        if (!s && (ieq(g, t) || (ap[0] && ieq(ap, t)))) s = 80;
        if (!s) {                                                /* todas las palabras son prefijo de palabras del nombre */
            int todas = 1, alguna = 0; const char *w = t; char pal[48];
            while (*w) {
                while (*w == ' ') ++w;
                uint32_t k = 0; while (w[k] && w[k] != ' ' && k + 1 < sizeof pal) { pal[k] = w[k]; ++k; } pal[k] = 0;
                w += k; while (*w && *w != ' ') ++w;
                if (!k) break;
                alguna = 1; if (!pal_pref(f, pal) && !pal_pref(ap, pal)) { todas = 0; break; }
            }
            if (todas && alguna) s = 60;
        }
        if (!s && ct_coincide(c, txt)) s = 20;
        if (!s) continue;
        if (s > mejor) { mejor = s; n = 0; }
        if (s == mejor && n < max) idx[n++] = (uint16_t)i;
    }
    ordena(l, idx, n);
    return n;
}
int ct_grupos(const struct ct_libreta *l, char grupos[][CT_GRUPO], int max)
{
    int n = 0;
    for (int i = 0; i < CT_MAX; ++i) {
        if (!l->c[i].activo) continue;
        const char *s = l->c[i].grupo;
        while (*s) {
            while (*s == ' ' || *s == ',') ++s;
            const char *e = s; while (*e && *e != ',') ++e;
            const char *f = e; while (f > s && f[-1] == ' ') --f;
            char t[CT_GRUPO]; cpyn(t, sizeof t, s, (uint32_t)(f - s));
            if (t[0]) { int ya = 0; for (int k = 0; k < n; ++k) if (igual_pl(grupos[k], t)) ya = 1; if (!ya && n < max) cpy(grupos[n++], CT_GRUPO, t); }
            s = e;
        }
    }
    return n;
}
int ct_cumples(const struct ct_libreta *l, int hy, int hm, int hd, int dias, uint16_t *idx, int16_t *falta, int max)
{
    int n = 0;
    for (int i = 0; i < CT_MAX; ++i) {
        const struct ct_contacto *c = &l->c[i]; if (!c->activo || !c->nac_m) continue;
        int f = ct_dias_para_cumple(c, hy, hm, hd); if (f < 0 || f > dias) continue;
        int j;
        if (n < max) j = n++;
        else if (max > 0 && falta[max - 1] > f) j = max - 1;
        else continue;
        while (j > 0 && falta[j - 1] > f) { idx[j] = idx[j - 1]; falta[j] = falta[j - 1]; --j; }
        idx[j] = (uint16_t)i; falta[j] = (int16_t)f;
    }
    return n;
}

/* ================================================================ editar */
struct campo { const char *nombre; int clase; };                  /* clase: indice interno */
enum { K_NOM = 1, K_APE, K_APODO, K_EM1, K_EM2, K_EM3, K_TEL1, K_TEL2, K_TEL3, K_MOVIL, K_FIJO, K_TELTRAB, K_EMTRAB, K_CALLE, K_CIUDAD, K_CP, K_PROV,
       K_PAIS, K_GRUPO, K_EMPRESA, K_CARGO, K_NAC, K_WEB, K_NOTA, K_FAV };
static const struct campo CAMPOS[] = {
    { "nombre", K_NOM }, { "apellidos", K_APE }, { "apellido", K_APE }, { "apodo", K_APODO }, { "alias", K_APODO }, { "mote", K_APODO },
    { "email", K_EM1 }, { "correo", K_EM1 }, { "mail", K_EM1 }, { "e-mail", K_EM1 }, { "email2", K_EM2 }, { "email3", K_EM3 }, { "correo2", K_EM2 },
    { "email-trabajo", K_EMTRAB },
    { "telefono", K_TEL1 }, { "tel", K_TEL1 }, { "tlf", K_TEL1 }, { "tfno", K_TEL1 }, { "numero", K_TEL1 }, { "tel2", K_TEL2 }, { "telefono2", K_TEL2 },
    { "tel3", K_TEL3 }, { "telefono3", K_TEL3 }, { "movil", K_MOVIL }, { "celular", K_MOVIL }, { "fijo", K_FIJO }, { "tel-trabajo", K_TELTRAB },
    { "direccion", K_CALLE }, { "calle", K_CALLE }, { "domicilio", K_CALLE }, { "ciudad", K_CIUDAD }, { "localidad", K_CIUDAD }, { "poblacion", K_CIUDAD },
    { "cp", K_CP }, { "codigo-postal", K_CP }, { "provincia", K_PROV }, { "region", K_PROV }, { "pais", K_PAIS },
    { "grupo", K_GRUPO }, { "grupos", K_GRUPO }, { "categoria", K_GRUPO }, { "empresa", K_EMPRESA }, { "compania", K_EMPRESA }, { "cargo", K_CARGO }, { "puesto", K_CARGO },
    { "nacimiento", K_NAC }, { "cumpleanos", K_NAC }, { "cumple", K_NAC }, { "fecha", K_NAC }, { "web", K_WEB }, { "pagina", K_WEB }, { "url", K_WEB },
    { "nota", K_NOTA }, { "notas", K_NOTA }, { "favorito", K_FAV }, { "favorita", K_FAV },
    { 0, 0 } };
static int clase_de(const char *pal_pl)                         /* palabra plegada (termina en blanco, ':' '=' o fin) */
{
    uint32_t n = 0; while (pal_pl[n] && pal_pl[n] != ' ' && pal_pl[n] != ':' && pal_pl[n] != '=' && pal_pl[n] != ',') ++n;
    for (int i = 0; CAMPOS[i].nombre; ++i) if (sl(CAMPOS[i].nombre) == n && iemp(pal_pl, CAMPOS[i].nombre)) return CAMPOS[i].clase;
    return 0;
}
int ct_es_campo(const char *palabra)
{
    char f[32]; ct_pliega(palabra, f, sizeof f);
    if (!clase_de(f)) return 0;
    uint32_t n = 0; while (palabra[n] && palabra[n] != ' ' && palabra[n] != ':' && palabra[n] != '=' && palabra[n] != ',') ++n;
    return (int)n;
}
static int email_valido(const char *s)
{
    const char *a = 0; for (const char *q = s; *q; ++q) { if (*q == ' ' || *q == ',' || *q == ';' || *q == '<' || *q == '>') return 0; if (*q == '@') { if (a) return 0; a = q; } }
    if (!a || a == s || !a[1]) return 0;
    for (const char *q = a + 1; *q; ++q) if (*q == '.' && q[1] && q > a + 1) return 1;
    return 0;
}
static void pon_tel(struct ct_contacto *c, int hueco, const char *v, int tipo)
{
    if (hueco < 0) {                                              /* el del tipo pedido, si hay; si no, el primero libre */
        for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0] && c->t_tel[i] == tipo) { hueco = i; break; }
        if (hueco < 0) for (int i = 0; i < CT_NTEL; ++i) if (!c->tel[i][0]) { hueco = i; break; }
        if (hueco < 0) hueco = 0;
    }
    cpy(c->tel[hueco], CT_TEL, v); c->t_tel[hueco] = (uint8_t)tipo;
}
int ct_pon_campo(struct ct_contacto *c, const char *campo, const char *valor)
{
    char f[32]; ct_pliega(campo, f, sizeof f);
    int k = clase_de(f); if (!k) return -1;
    while (*valor == ' ' || *valor == ':' || *valor == '=') ++valor;
    char v[CT_NOTA]; cpy(v, sizeof v, valor);
    { uint32_t n = sl(v); while (n && (v[n - 1] == ' ' || v[n - 1] == '\n')) v[--n] = 0; }
    int vacia = !v[0] || (v[0] == '-' && !v[1]);
    if (vacia) v[0] = 0;
    switch (k) {
    case K_NOM: cpy(c->nombre, CT_NOM, v); break;
    case K_APE: cpy(c->apellidos, CT_APE, v); break;
    case K_APODO: cpy(c->apodo, CT_APODO, v); break;
    case K_EM1: case K_EM2: case K_EM3: case K_EMTRAB: {
        if (!vacia && !email_valido(v)) return -2;
        int h = k == K_EM2 ? 1 : k == K_EM3 ? 2 : 0;
        if (k == K_EMTRAB) { h = -1; for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0] && c->t_email[i] == CT_T_TRABAJO) h = i; if (h < 0) for (int i = 0; i < CT_NEMAIL; ++i) if (!c->email[i][0]) { h = i; break; } if (h < 0) h = 0; }
        cpy(c->email[h], CT_EMAIL, v); if (k == K_EMTRAB) c->t_email[h] = CT_T_TRABAJO; break;
    }
    case K_TEL1: case K_TEL2: case K_TEL3: {
        char d[40]; if (!vacia && digitos(v, d, sizeof d) < 3) return -2;
        int h = k == K_TEL2 ? 1 : k == K_TEL3 ? 2 : 0; cpy(c->tel[h], CT_TEL, v); if (vacia) c->t_tel[h] = 0; break;
    }
    case K_MOVIL: case K_FIJO: case K_TELTRAB: {
        char d[40]; if (!vacia && digitos(v, d, sizeof d) < 3) return -2;
        int t = k == K_MOVIL ? CT_T_MOVIL : k == K_FIJO ? CT_T_CASA : CT_T_TRABAJO;
        if (vacia) { for (int i = 0; i < CT_NTEL; ++i) if (c->t_tel[i] == t) { c->tel[i][0] = 0; c->t_tel[i] = 0; } }
        else pon_tel(c, -1, v, t);
        break;
    }
    case K_CALLE: cpy(c->calle, CT_CALLE, v); break;
    case K_CIUDAD: cpy(c->ciudad, CT_CIUDAD, v); break;
    case K_CP: cpy(c->cp, CT_CP, v); break;
    case K_PROV: cpy(c->prov, CT_PROV, v); break;
    case K_PAIS: cpy(c->pais, CT_PAIS, v); break;
    case K_GRUPO: cpy(c->grupo, CT_GRUPO, v); break;
    case K_EMPRESA: cpy(c->empresa, CT_EMPRESA, v); break;
    case K_CARGO: cpy(c->cargo, CT_CARGO, v); break;
    case K_WEB: cpy(c->web, CT_WEB, v); break;
    case K_NOTA: cpy(c->nota, CT_NOTA, v); break;
    case K_NAC: {
        if (vacia) { c->nac_y = 0; c->nac_m = 0; c->nac_d = 0; break; }
        int y, m, d; if (ct_parsea_fecha(v, &y, &m, &d)) return -2;
        c->nac_y = (int16_t)y; c->nac_m = (uint8_t)m; c->nac_d = (uint8_t)d; break;
    }
    case K_FAV: { char w[8]; ct_pliega(v, w, sizeof w); c->fav = !(iemp(w, "no") || iemp(w, "off") || w[0] == '0'); if (vacia) c->fav = 1; break; }
    default: return -1;
    }
    ct_sanea(c);
    return 0;
}
void ct_marca(struct ct_contacto *c, const char *rev) { c->cambiado = 1; if (rev) cpy(c->rev, CT_REV, rev); }

/* ---- lenguaje natural */
enum { S_NADA = 0, S_EMAIL, S_TEL, S_CALLE, S_CIUDAD, S_CP, S_PROV, S_PAIS, S_GRUPO, S_EMPRESA, S_CARGO, S_NAC, S_WEB, S_NOTA, S_APODO, S_NOM, S_APE, S_FAV };
struct clave { const char *w; int seg; int tipo; int incluye; };   /* incluye = la palabra es parte del valor (calle, avenida...) */
static const struct clave CLAVES[] = {
    { "email", S_EMAIL, 0, 0 }, { "e-mail", S_EMAIL, 0, 0 }, { "mail", S_EMAIL, 0, 0 }, { "correo", S_EMAIL, 0, 0 },
    { "telefono", S_TEL, 0, 0 }, { "tel", S_TEL, 0, 0 }, { "tlf", S_TEL, 0, 0 }, { "tfno", S_TEL, 0, 0 }, { "movil", S_TEL, CT_T_MOVIL, 0 },
    { "celular", S_TEL, CT_T_MOVIL, 0 }, { "fijo", S_TEL, CT_T_CASA, 0 }, { "numero", S_TEL, 0, 0 },
    { "direccion", S_CALLE, 0, 0 }, { "domicilio", S_CALLE, 0, 0 }, { "vive", S_CALLE, 0, 0 }, { "calle", S_CALLE, 0, 1 }, { "c/", S_CALLE, 0, 1 },
    { "avenida", S_CALLE, 0, 1 }, { "avda", S_CALLE, 0, 1 }, { "avda.", S_CALLE, 0, 1 }, { "plaza", S_CALLE, 0, 1 }, { "paseo", S_CALLE, 0, 1 },
    { "ciudad", S_CIUDAD, 0, 0 }, { "localidad", S_CIUDAD, 0, 0 }, { "cp", S_CP, 0, 0 }, { "provincia", S_PROV, 0, 0 }, { "pais", S_PAIS, 0, 0 },
    { "grupo", S_GRUPO, 0, 0 }, { "grupos", S_GRUPO, 0, 0 }, { "empresa", S_EMPRESA, 0, 0 }, { "trabaja", S_EMPRESA, 0, 0 },
    { "cargo", S_CARGO, 0, 0 }, { "puesto", S_CARGO, 0, 0 },
    { "nacio", S_NAC, 0, 0 }, { "nacida", S_NAC, 0, 0 }, { "nacido", S_NAC, 0, 0 }, { "nacimiento", S_NAC, 0, 0 }, { "cumpleanos", S_NAC, 0, 0 },
    { "cumple", S_NAC, 0, 0 }, { "fecha", S_NAC, 0, 0 },
    { "web", S_WEB, 0, 0 }, { "pagina", S_WEB, 0, 0 }, { "nota", S_NOTA, 0, 0 }, { "notas", S_NOTA, 0, 0 }, { "apodo", S_APODO, 0, 0 }, { "alias", S_APODO, 0, 0 },
    { "nombre", S_NOM, 0, 0 }, { "apellidos", S_APE, 0, 0 }, { "apellido", S_APE, 0, 0 }, { "favorito", S_FAV, 0, 0 }, { "favorita", S_FAV, 0, 0 },
    { 0, 0, 0, 0 } };
static const struct clave *clave_de(const char *w, uint32_t n)  /* w ya plegada; sin ':' '=' finales */
{
    while (n && (w[n - 1] == ':' || w[n - 1] == '=' || w[n - 1] == '.')) --n;
    for (int i = 0; CLAVES[i].w; ++i) if (sl(CLAVES[i].w) == n && iemp(w, CLAVES[i].w)) return &CLAVES[i];
    return 0;
}
/* quita palabras de relleno delante del valor */
static const char *salta_relleno(const char *s)
{
    static const char *const R[] = { "es ", "son ", "en ", "el ", "la ", "los ", "las ", "de ", "del ", "electronico ", "electr\303\263nico ", "postal ", "como ", "mi ", "su ", "a ", 0 };
    for (int vuelta = 0; vuelta < 4; ++vuelta) {
        while (*s == ' ' || *s == ':' || *s == '=') ++s;
        int k = 0; for (; R[k]; ++k) if (iemp(s, R[k])) { s += sl(R[k]); break; }
        if (!R[k]) break;
    }
    while (*s == ' ' || *s == ':' || *s == '=') ++s;
    return s;
}
static int parece_tel(const char *s, char *out, uint32_t cap)    /* saca un telefono ("+34 600 11 22 33") si lo hay */
{
    uint32_t p = 0, nd = 0; const char *ini = 0;
    for (const char *q = s; *q; ++q) {
        int vale = es_dig(*q) || *q == '+' || *q == ' ' || *q == '-' || *q == '(' || *q == ')' || *q == '.';
        if (!ini) { if (es_dig(*q) || *q == '+') { ini = q; } else continue; }
        if (!vale) break;
        if (p + 1 < cap) out[p++] = *q;
        if (es_dig(*q)) ++nd;
    }
    while (p && (out[p - 1] == ' ' || out[p - 1] == '-' || out[p - 1] == '.')) --p;
    if (cap) out[p < cap ? p : cap - 1] = 0;
    return nd >= 6 && nd <= 15;
}
static int saca_email(const char *s, char *out, uint32_t cap)
{
    for (const char *q = s; *q; ++q) if (*q == '@') {
        const char *a = q; while (a > s && a[-1] != ' ' && a[-1] != ',' && a[-1] != ';' && a[-1] != '<' && a[-1] != ':' && a[-1] != '(') --a;
        const char *b = q; while (*b && *b != ' ' && *b != ',' && *b != ';' && *b != '>' && *b != ')') ++b;
        while (b > q && (b[-1] == '.')) --b;
        cpyn(out, cap, a, (uint32_t)(b - a));
        return email_valido(out);
    }
    return 0;
}
static void anade_nota(struct ct_contacto *c, const char *v)
{
    uint32_t p = sl(c->nota); if (!*v) return;
    if (p) put(c->nota, CT_NOTA, &p, ". ");
    put(c->nota, CT_NOTA, &p, v);
}
static void pon_email(struct ct_contacto *c, const char *v, int tipo)
{
    for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0] && ieq(c->email[i], v)) return;
    for (int i = 0; i < CT_NEMAIL; ++i) if (!c->email[i][0]) { cpy(c->email[i], CT_EMAIL, v); c->t_email[i] = (uint8_t)tipo; return; }
}
static void pon_tel_nuevo(struct ct_contacto *c, const char *v, int tipo)
{
    for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0] && tel_igual(c->tel[i], v)) return;
    for (int i = 0; i < CT_NTEL; ++i) if (!c->tel[i][0]) { cpy(c->tel[i], CT_TEL, v); c->t_tel[i] = (uint8_t)tipo; return; }
}
static void pon_nombre(struct ct_contacto *c, const char *s)     /* "Luis Perez Garcia" -> nombre y apellidos */
{
    while (*s == ' ') ++s;
    char b[CT_NOM + CT_APE]; cpy(b, sizeof b, s);
    uint32_t n = sl(b); while (n && (b[n - 1] == ' ' || b[n - 1] == '.')) b[--n] = 0;
    if (!b[0]) return;
    uint32_t e = 0; while (b[e] && b[e] != ' ') ++e;
    if (!c->nombre[0]) { cpyn(c->nombre, CT_NOM, b, e); const char *r = b + e; while (*r == ' ') ++r; if (*r && !c->apellidos[0]) cpy(c->apellidos, CT_APE, r); }
    else if (!c->apellidos[0]) cpy(c->apellidos, CT_APE, b);
    if (c->nombre[0] >= 'a' && c->nombre[0] <= 'z') c->nombre[0] = (char)(c->nombre[0] - 32);
}
int ct_parsea_texto(const char *txt, struct ct_contacto *c)
{
    zero(c, (uint32_t)sizeof *c);
    char b[640]; cpy(b, sizeof b, txt);
    /* palabras de delante: "a", "al contacto", "un contacto", "nuevo" */
    char *s = b;
    for (int vuelta = 0; vuelta < 4; ++vuelta) {
        while (*s == ' ') ++s;
        static const char *const P[] = { "a ", "al ", "el ", "un ", "una ", "contacto ", "nuevo ", "nueva ", "como ", "mis contactos ", "a mis contactos ", 0 };
        int k = 0; for (; P[k]; ++k) if (iemp(s, P[k])) { s += sl(P[k]); break; }
        if (!P[k]) break;
    }
    /* trocear: por comas y punto y coma, y delante de cada palabra clave */
    char seg[16][200]; int ns = 0; uint32_t sp = 0; seg[0][0] = 0; const struct clave *cur_k = 0; int cur_pal = 0;
    const char *q = s;
    while (*q && ns < 16) {
        while (*q == ' ') ++q;
        if (!*q) break;
        if (*q == ',' || *q == ';' || *q == '\n') { ++q; if (sp) { seg[ns][sp] = 0; if (++ns < 16) { sp = 0; seg[ns][0] = 0; } } continue; }
        const char *w = q; while (*q && *q != ' ' && *q != ',' && *q != ';' && *q != '\n') ++q;
        uint32_t wl = (uint32_t)(q - w);
        char wf[32]; { char tmp[40]; cpyn(tmp, sizeof tmp, w, wl < 39 ? wl : 39); ct_pliega(tmp, wf, sizeof wf); }
        const struct clave *k = clave_de(wf, sl(wf));
        if (k && sp && cur_k && (cur_k->seg == S_NOTA || (cur_k->seg == S_CALLE && !cur_k->incluye && k->incluye && cur_pal <= 2))) k = 0;   /* "vive en calle...", notas libres */
        if (!sp) { cur_k = k; cur_pal = 0; }
        /* "y" o "su" que preceden a una clave: fuera del trozo anterior */
        if (k && sp) {
            /* no partir "codigo postal", ni la fecha dentro de "fecha de nacimiento" */
            seg[ns][sp] = 0;
            for (int quita = 0; quita < 3; ++quita) {
                uint32_t e = sp; while (e && seg[ns][e - 1] == ' ') --e;
                uint32_t a = e; while (a && seg[ns][a - 1] != ' ') --a;
                char last[16]; char tmp[16]; cpyn(tmp, sizeof tmp, seg[ns] + a, e - a); ct_pliega(tmp, last, sizeof last);
                if (ieq(last, "y") || ieq(last, "su") || ieq(last, "sus") || ieq(last, "con") || ieq(last, "e")) { sp = a; seg[ns][sp] = 0; } else break;
            }
            while (sp && seg[ns][sp - 1] == ' ') --sp;
            seg[ns][sp] = 0;
            if (sp) { if (++ns >= 16) break; sp = 0; seg[ns][0] = 0; }
            cur_k = k; cur_pal = 0;
        }
        ++cur_pal;
        if (sp && sp + 1 < sizeof seg[0]) seg[ns][sp++] = ' ';
        for (uint32_t i = 0; i < wl && sp + 1 < sizeof seg[0]; ++i) seg[ns][sp++] = w[i];
        seg[ns][sp] = 0;
    }
    if (ns < 16 && sp) { seg[ns][sp] = 0; ++ns; }
    int ultimo = S_NADA, hay_nombre = 0;
    for (int i = 0; i < ns; ++i) {
        char *t = seg[i]; while (*t == ' ') ++t;
        if (!*t) continue;
        char wf[32]; { uint32_t wl = 0; while (t[wl] && t[wl] != ' ') ++wl; char tmp[40]; cpyn(tmp, sizeof tmp, t, wl < 39 ? wl : 39); ct_pliega(tmp, wf, sizeof wf); }
        const struct clave *k = clave_de(wf, sl(wf));
        const char *v = t;
        if (k && !k->incluye) { while (*v && *v != ' ') ++v; }
        if (k && k->seg == S_NAC && iemp(wf, "fecha")) { v = salta_relleno(v); if (iemp(v, "nacimiento")) v += 10; else if (iemp(v, "de nacimiento")) v += 13; }
        if (k && k->seg == S_CP) { const char *r = salta_relleno(v); if (iemp(r, "postal")) v = r + 6; }
        if (k && k->seg == S_EMPRESA && iemp(wf, "trabaja")) { v = salta_relleno(v); if (iemp(v, "en ")) v += 3; }
        if (k && k->seg == S_CALLE && iemp(wf, "vive")) { v = salta_relleno(v); }
        if (k && !k->incluye && k->seg != S_NOTA && k->seg != S_NOM && k->seg != S_APE) v = salta_relleno(v);
        else if (k && !k->incluye) { while (*v == ' ' || *v == ':' || *v == '=') ++v; }
        int seg_t = k ? k->seg : S_NADA;
        char val[200]; cpy(val, sizeof val, v);
        { uint32_t n = sl(val); while (n && (val[n - 1] == ' ' || val[n - 1] == '.')) val[--n] = 0; }
        char e[CT_EMAIL], tl[CT_TEL];
        if (!k) {                                                   /* sin clave: se adivina */
            if (saca_email(t, e, sizeof e) && !hay_nombre && i == 0) {
                /* "Luis Perez luis@x.es 600112233" */
                char nom[200]; uint32_t p = 0; nom[0] = 0;
                const char *x = t;
                while (*x) {
                    while (*x == ' ') ++x;
                    const char *y = x;
                    while (*y && *y != ' ') ++y;
                    char w[100]; cpyn(w, sizeof w, x, (uint32_t)(y - x));
                    int esdig = 0; for (const char *z = w; *z; ++z) if (es_dig(*z)) esdig = 1;
                    if (!w[0]) break;
                    int arroba = 0; for (const char *z = w; *z; ++z) if (*z == '@') arroba = 1;
                    if (!arroba && !esdig) { if (p) putc_(nom, sizeof nom, &p, ' '); put(nom, sizeof nom, &p, w); }
                    x = y;
                }
                pon_email(c, e, CT_T_OTRO);
                if (parece_tel(t, tl, sizeof tl)) pon_tel_nuevo(c, tl, CT_T_OTRO);
                if (nom[0]) { pon_nombre(c, nom); hay_nombre = 1; }
                ultimo = S_NADA; continue;
            }
            if (saca_email(t, e, sizeof e)) { pon_email(c, e, CT_T_OTRO); ultimo = S_EMAIL; continue; }
            { int y, m, d; if (hay_nombre && ct_parsea_fecha(t, &y, &m, &d) == 0) { c->nac_y = (int16_t)y; c->nac_m = (uint8_t)m; c->nac_d = (uint8_t)d; ultimo = S_NAC; continue; } }
            if (parece_tel(t, tl, sizeof tl)) {
                int letras = 0; for (const char *z = t; *z; ++z) if ((*z >= 'a' && *z <= 'z') || (*z >= 'A' && *z <= 'Z')) ++letras;
                if (letras < 3) { pon_tel_nuevo(c, tl, CT_T_OTRO); ultimo = S_TEL; continue; }
            }
            if (ultimo == S_GRUPO) {                                /* "grupo Trabajo, Amigos" */
                int una = 1; for (const char *z = val; *z; ++z) if (*z == ' ') una = 0;
                if (una) { uint32_t p = sl(c->grupo); char g[CT_GRUPO]; cpy(g, sizeof g, val); if (g[0] >= 'a' && g[0] <= 'z') g[0] = (char)(g[0] - 32); if (p) put(c->grupo, CT_GRUPO, &p, ","); put(c->grupo, CT_GRUPO, &p, g); continue; }
            }
            if (ultimo == S_CALLE || ultimo == S_CIUDAD || ultimo == S_CP) {     /* "Calle Mayor 3, 28013 Madrid, Madrid" */
                char d5[8]; const char *x = val; uint32_t nd = 0; while (es_dig(x[nd])) ++nd;
                if (nd == 5 && !c->cp[0]) { cpyn(d5, sizeof d5, x, 5); cpy(c->cp, CT_CP, d5); x += 5; while (*x == ' ') ++x; if (*x && !c->ciudad[0]) cpy(c->ciudad, CT_CIUDAD, x); ultimo = S_CIUDAD; continue; }
                if (!c->ciudad[0]) { cpy(c->ciudad, CT_CIUDAD, val); ultimo = S_CIUDAD; continue; }
                if (!c->prov[0]) { cpy(c->prov, CT_PROV, val); ultimo = S_PROV; continue; }
            }
            if (!hay_nombre) {
                char nom[200]; cpy(nom, sizeof nom, val);
                if (parece_tel(nom, tl, sizeof tl)) {               /* "Ana 600112233" */
                    pon_tel_nuevo(c, tl, CT_T_OTRO);
                    char *z = nom; while (*z && !es_dig(*z) && *z != '+') ++z; *z = 0;
                }
                pon_nombre(c, nom); hay_nombre = c->nombre[0] != 0; ultimo = S_NOM; continue;
            }
            anade_nota(c, val); ultimo = S_NOTA; continue;
        }
        switch (seg_t) {
        case S_EMAIL: if (saca_email(val, e, sizeof e)) { char vf[200]; ct_pliega(val, vf, sizeof vf); pon_email(c, e, contiene(vf, "trabajo") ? CT_T_TRABAJO : contiene(vf, "casa") || contiene(vf, "personal") ? CT_T_CASA : CT_T_OTRO); } break;
        case S_TEL: if (parece_tel(val, tl, sizeof tl)) { char vf[200]; ct_pliega(val, vf, sizeof vf); int tp = k->tipo; if (!tp && contiene(vf, "trabajo")) tp = CT_T_TRABAJO; if (!tp && contiene(vf, "casa")) tp = CT_T_CASA; if (!tp && contiene(vf, "movil")) tp = CT_T_MOVIL; pon_tel_nuevo(c, tl, tp); } break;
        case S_CALLE: if (!c->calle[0]) cpy(c->calle, CT_CALLE, val); else if (!c->ciudad[0]) cpy(c->ciudad, CT_CIUDAD, val); break;
        case S_CIUDAD: cpy(c->ciudad, CT_CIUDAD, val); break;
        case S_CP: cpy(c->cp, CT_CP, val); break;
        case S_PROV: cpy(c->prov, CT_PROV, val); break;
        case S_PAIS: cpy(c->pais, CT_PAIS, val); break;
        case S_GRUPO: {
            uint32_t p = sl(c->grupo);
            char g[CT_GRUPO]; cpy(g, sizeof g, val);
            if (g[0] >= 'a' && g[0] <= 'z') g[0] = (char)(g[0] - 32);
            if (p) put(c->grupo, CT_GRUPO, &p, ",");
            put(c->grupo, CT_GRUPO, &p, g);
            break;
        }
        case S_EMPRESA: cpy(c->empresa, CT_EMPRESA, val); break;
        case S_CARGO: cpy(c->cargo, CT_CARGO, val); break;
        case S_NAC: { int y, m, d; if (ct_parsea_fecha(val, &y, &m, &d) == 0) { c->nac_y = (int16_t)y; c->nac_m = (uint8_t)m; c->nac_d = (uint8_t)d; } break; }
        case S_WEB: cpy(c->web, CT_WEB, val); break;
        case S_NOTA: anade_nota(c, val); break;
        case S_APODO: cpy(c->apodo, CT_APODO, val); break;
        case S_NOM: cpy(c->nombre, CT_NOM, val); hay_nombre = 1; break;
        case S_APE: cpy(c->apellidos, CT_APE, val); break;
        case S_FAV: c->fav = 1; break;
        default: break;
        }
        ultimo = seg_t;
    }
    ct_sanea(c);
    return ct_vacio(c) ? -1 : 0;
}

/* ================================================================ base64 */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
uint32_t ct_b64_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t acc = 0, nb = 0, p = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (s[i] == '=') break;
        int v = b64v(s[i]); if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; nb += 6;
        if (nb >= 8) { nb -= 8; if (p >= cap) return 0; out[p++] = (uint8_t)(acc >> nb); }
    }
    return p;
}
uint32_t ct_b64_enc(const uint8_t *s, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)s[i] << 16; if (i + 1 < n) v |= (uint32_t)s[i + 1] << 8; if (i + 2 < n) v |= s[i + 2];
        if (p + 4 >= cap) return 0;
        out[p++] = B64[(v >> 18) & 63]; out[p++] = B64[(v >> 12) & 63];
        out[p++] = i + 1 < n ? B64[(v >> 6) & 63] : '='; out[p++] = i + 2 < n ? B64[v & 63] : '=';
    }
    if (p < cap) out[p] = 0;
    return p;
}

/* ================================================================ vCard: escribir */
struct vw { char *o; uint32_t cap, p, col; int lleno; };
static void vw_c(struct vw *w, char c)                           /* un byte, plegando a 75 octetos sin partir UTF-8 */
{
    if (w->col >= 73 && ((uint8_t)c & 0xC0) != 0x80) { if (w->p + 3 >= w->cap) { w->lleno = 1; return; } w->o[w->p++] = '\r'; w->o[w->p++] = '\n'; w->o[w->p++] = ' '; w->col = 1; }
    if (w->p + 1 >= w->cap) { w->lleno = 1; return; }
    w->o[w->p++] = c; w->col++;
}
static void vw_s(struct vw *w, const char *s) { while (*s) vw_c(w, *s++); }
static void vw_fin(struct vw *w) { if (w->p + 2 >= w->cap) { w->lleno = 1; return; } w->o[w->p++] = '\r'; w->o[w->p++] = '\n'; w->col = 0; }
static void vw_esc(struct vw *w, const char *s)                  /* texto con \\ \, \; \n */
{
    for (; *s; ++s) {
        if (*s == '\\') vw_s(w, "\\\\"); else if (*s == ',') vw_s(w, "\\,"); else if (*s == ';') vw_s(w, "\\;"); else if (*s == '\n') vw_s(w, "\\n");
        else if (*s != '\r') vw_c(w, *s);
    }
}
static void vw_prop(struct vw *w, const char *nombre, const char *valor) { if (!valor[0]) return; vw_s(w, nombre); vw_c(w, ':'); vw_esc(w, valor); vw_fin(w); }
static int foto_tipo(const uint8_t *f, uint32_t n)                /* 1 JPEG, 2 PNG, 3 GIF, 0 otro */
{
    if (n >= 3 && f[0] == 0xFF && f[1] == 0xD8 && f[2] == 0xFF) return 1;
    if (n >= 8 && f[0] == 0x89 && f[1] == 'P' && f[2] == 'N' && f[3] == 'G') return 2;
    if (n >= 6 && f[0] == 'G' && f[1] == 'I' && f[2] == 'F') return 3;
    return 0;
}
uint32_t ct_vcard_uno(const struct ct_contacto *c, int interno, const uint8_t *foto, uint32_t foto_len, char *out, uint32_t cap)
{
    struct vw w = { out, cap, 0, 0, 0 }; char nc[CT_NOM + CT_APE + 4];
    vw_s(&w, "BEGIN:VCARD"); vw_fin(&w); vw_s(&w, "VERSION:3.0"); vw_fin(&w); vw_s(&w, "PRODID:-//SOMA//Contactos//ES"); vw_fin(&w);
    vw_prop(&w, "UID", c->uid);
    ct_nombre_completo(c, nc, sizeof nc); vw_prop(&w, "FN", nc);
    vw_s(&w, "N:"); vw_esc(&w, c->apellidos); vw_c(&w, ';'); vw_esc(&w, c->nombre); vw_s(&w, ";;;"); vw_fin(&w);
    vw_prop(&w, "NICKNAME", c->apodo);
    for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0]) {
        vw_s(&w, "EMAIL;TYPE=INTERNET"); if (c->t_email[i] == CT_T_CASA) vw_s(&w, ",HOME"); else if (c->t_email[i] == CT_T_TRABAJO) vw_s(&w, ",WORK");
        if (!i && (c->email[1][0])) vw_s(&w, ",PREF");
        vw_c(&w, ':'); vw_esc(&w, c->email[i]); vw_fin(&w);
    }
    for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0]) {
        static const char *const T[4] = { "VOICE", "HOME", "WORK", "CELL" };
        vw_s(&w, "TEL;TYPE="); vw_s(&w, T[c->t_tel[i] & 3]); vw_c(&w, ':'); vw_esc(&w, c->tel[i]); vw_fin(&w);
    }
    if (c->calle[0] || c->ciudad[0] || c->cp[0] || c->prov[0] || c->pais[0]) {
        vw_s(&w, "ADR;TYPE=HOME:;;"); vw_esc(&w, c->calle); vw_c(&w, ';'); vw_esc(&w, c->ciudad); vw_c(&w, ';'); vw_esc(&w, c->prov); vw_c(&w, ';');
        vw_esc(&w, c->cp); vw_c(&w, ';'); vw_esc(&w, c->pais); vw_fin(&w);
    }
    vw_prop(&w, "ORG", c->empresa); vw_prop(&w, "TITLE", c->cargo);
    if (c->nac_m) {
        char b[16]; uint32_t p = 0; b[0] = 0;
        if (c->nac_y) { putn(b, sizeof b, &p, c->nac_y, 4); putc_(b, sizeof b, &p, '-'); } else put(b, sizeof b, &p, "--");
        putn(b, sizeof b, &p, c->nac_m, 2); putc_(b, sizeof b, &p, '-'); putn(b, sizeof b, &p, c->nac_d, 2);
        vw_s(&w, "BDAY:"); vw_s(&w, b); vw_fin(&w);
    }
    vw_prop(&w, "URL", c->web); vw_prop(&w, "NOTE", c->nota);
    if (c->grupo[0]) {                                           /* CATEGORIES: los grupos separados por comas, cada uno escapado */
        vw_s(&w, "CATEGORIES:"); const char *s = c->grupo; int k = 0;
        while (*s) {
            while (*s == ' ' || *s == ',') ++s;
            const char *e = s; while (*e && *e != ',') ++e;
            const char *f = e; while (f > s && f[-1] == ' ') --f;
            char t[CT_GRUPO]; cpyn(t, sizeof t, s, (uint32_t)(f - s));
            if (t[0]) { if (k++) vw_c(&w, ','); vw_esc(&w, t); }
            s = e;
        }
        vw_fin(&w);
    }
    int ft = foto && foto_len ? foto_tipo(foto, foto_len) : 0;
    if (ft) {
        vw_s(&w, ft == 1 ? "PHOTO;ENCODING=b;TYPE=JPEG:" : ft == 2 ? "PHOTO;ENCODING=b;TYPE=PNG:" : "PHOTO;ENCODING=b;TYPE=GIF:");
        for (uint32_t i = 0; i < foto_len && !w.lleno; i += 3) {
            uint32_t v = (uint32_t)foto[i] << 16; if (i + 1 < foto_len) v |= (uint32_t)foto[i + 1] << 8; if (i + 2 < foto_len) v |= foto[i + 2];
            vw_c(&w, B64[(v >> 18) & 63]); vw_c(&w, B64[(v >> 12) & 63]); vw_c(&w, i + 1 < foto_len ? B64[(v >> 6) & 63] : '='); vw_c(&w, i + 2 < foto_len ? B64[v & 63] : '=');
        }
        vw_fin(&w);
    } else if (c->foto_url[0]) { vw_s(&w, "PHOTO;VALUE=URI:"); vw_s(&w, c->foto_url); vw_fin(&w); }
    if (c->rev[0]) { vw_s(&w, "REV:"); vw_s(&w, c->rev); vw_fin(&w); }
    if (interno) {
        char b[16]; uint32_t p = 0;
        vw_s(&w, "X-SOMA-ID:"); p = 0; putn(b, sizeof b, &p, c->id, 0); vw_s(&w, b); vw_fin(&w);
        if (c->fav) { vw_s(&w, "X-SOMA-FAV:1"); vw_fin(&w); }
        if (c->cambiado) { vw_s(&w, "X-SOMA-CAMBIADO:1"); vw_fin(&w); }
        if (c->nfotos) { vw_s(&w, "X-SOMA-FOTOS:"); p = 0; putn(b, sizeof b, &p, c->nfotos, 0); vw_s(&w, b); vw_fin(&w); }
        if (c->voz) { vw_s(&w, "X-SOMA-VOZ:1"); vw_fin(&w); }
    } else if (c->fav) { vw_s(&w, "X-SOMA-FAV:1"); vw_fin(&w); }
    vw_s(&w, "END:VCARD"); vw_fin(&w);
    if (w.lleno) { if (cap) out[0] = 0; return 0; }
    if (w.p < cap) out[w.p] = 0;
    return w.p;
}

/* ================================================================ vCard: leer */
struct vr { const char *t; uint32_t n, p; };
static int vr_salto(const struct vr *r, uint32_t i)              /* bytes de un salto de linea en i (0 si no hay) */
{
    if (i >= r->n) return 0;
    if (r->t[i] == '\r') return i + 1 < r->n && r->t[i + 1] == '\n' ? 2 : 1;
    return r->t[i] == '\n' ? 1 : 0;
}
/* siguiente caracter de la linea logica: -1 = fin de linea (consumido), -2 = fin del texto. qp: "=" + salto = sigue en la otra linea */
static int vr_get(struct vr *r, int qp)
{
    for (;;) {
        if (r->p >= r->n) return -2;
        int s = vr_salto(r, r->p);
        if (s) {
            uint32_t sig = r->p + (uint32_t)s;
            if (sig < r->n && (r->t[sig] == ' ' || r->t[sig] == '\t')) { r->p = sig + 1; continue; }   /* linea plegada */
            r->p = sig; return -1;
        }
        char c = r->t[r->p];
        if (qp && c == '=' && vr_salto(r, r->p + 1)) { r->p += 1 + (uint32_t)vr_salto(r, r->p + 1); continue; }
        r->p++;
        return (uint8_t)c;
    }
}
static void vr_salta_linea(struct vr *r) { int c; while ((c = vr_get(r, 0)) >= 0) {} }
static int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; c = baja(c); if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; }
/* separa el componente `k` (por ';' sin escapar) y lo desescapa */
static void componente(const char *v, int k, char *o, uint32_t cap, int lista)
{
    int i = 0; const char *s = v;
    while (*s && i < k) { if (*s == '\\' && s[1]) s += 2; else { if (*s == ';') ++i; ++s; } }
    uint32_t p = 0; if (!cap) return;
    if (i < k) { o[0] = 0; return; }
    while (*s && *s != ';' && p + 1 < cap) {
        if (*s == '\\' && s[1]) { char e = s[1]; o[p++] = (e == 'n' || e == 'N') ? '\n' : e; s += 2; continue; }
        if (*s == ',' && lista) { o[p++] = ' '; ++s; continue; }
        o[p++] = *s++;
    }
    o[p] = 0;
    while (p && ((uint8_t)o[p] & 0xC0) == 0x80 && ((uint8_t)o[p - 1] & 0x80)) --p;    /* no dejar medio caracter si se recorto */
}
static void desescapa(const char *v, char *o, uint32_t cap) { componente(v, 0, o, cap, 0); }
static void une(char *dst, uint32_t cap, const char *a)          /* anade a una lista separada por comas sin repetir */
{
    if (!a[0]) return;
    struct ct_contacto tmp; zero(tmp.grupo, sizeof tmp.grupo); cpy(tmp.grupo, sizeof tmp.grupo, dst);
    if (ct_en_grupo(&tmp, a)) return;
    uint32_t p = sl(dst); if (p) put(dst, cap, &p, ","); put(dst, cap, &p, a);
}
#define VR_HDR 512
#define VR_VAL 2048
int ct_vcard_lee(const char *txt, uint32_t len, struct ct_contacto *out, int max, uint8_t *fotos, uint32_t fotos_cap, uint32_t foto_max, uint32_t *fotos_n)
{
    if (fotos_n) *fotos_n = 0;
    if (!txt || !len || max <= 0) return -1;
    { int hay = 0; for (uint32_t k = 0; k + 11 <= len && k < 1024; ++k) if (iemp(txt + k, "BEGIN:VCARD")) { hay = 1; break; } if (!hay) return -1; }
    struct vr r = { txt, len, 0 };
    if (len >= 3 && (uint8_t)txt[0] == 0xEF && (uint8_t)txt[1] == 0xBB && (uint8_t)txt[2] == 0xBF) r.p = 3;
    int n = 0, dentro = 0, tiene_adr = 0; uint32_t fusados = 0;
    struct ct_contacto *c = 0;
    char hdr[VR_HDR], val[VR_VAL], conv[VR_VAL * 2 + 1], tmp[VR_VAL];
    while (r.p < r.n) {
        /* cabecera: hasta ':' fuera de comillas */
        uint32_t h = 0; int ch, comillas = 0, cortada = 0;
        while ((ch = vr_get(&r, 0)) >= 0) {
            if (ch == '"') comillas = !comillas;
            if (ch == ':' && !comillas) break;
            if (h + 1 < sizeof hdr) hdr[h++] = (char)ch; else cortada = 1;
        }
        hdr[h] = 0;
        if (ch == -2) break;
        if (ch == -1 || cortada || !h) continue;                     /* linea sin ':' o demasiado larga: se ignora */
        /* nombre (sin el grupo "item1.") y parametros */
        char *pn = hdr; { char *sc = hdr; while (*sc && *sc != ';') ++sc; char *pt = hdr; for (char *z = hdr; z < sc; ++z) if (*z == '.') pt = z + 1; pn = pt; }
        char nombre[40]; { uint32_t k = 0; while (pn[k] && pn[k] != ';' && k + 1 < sizeof nombre) { nombre[k] = baja(pn[k]); ++k; } nombre[k] = 0; }
        int qp = 0, b64 = 0, latin = 0, uri = 0, t_casa = 0, t_trab = 0, t_movil = 0;
        for (char *z = pn; *z; ) {
            if (*z != ';') { ++z; continue; }
            ++z; char par[96]; uint32_t k = 0;
            while (*z && *z != ';' && k + 1 < sizeof par) { par[k++] = *z++; }
            par[k] = 0;
            char pl[96]; for (uint32_t q = 0; q <= k; ++q) pl[q] = baja(par[q]);
            if (iemp(pl, "encoding=")) { if (contiene(pl, "quoted")) qp = 1; if (ieq(pl + 9, "b") || contiene(pl, "base64")) b64 = 1; }
            if (ieq(pl, "quoted-printable")) qp = 1;
            if (ieq(pl, "base64") || ieq(pl, "b")) b64 = 1;
            if (iemp(pl, "charset=") && (contiene(pl, "8859") || contiene(pl, "1252") || contiene(pl, "latin"))) latin = 1;
            if (iemp(pl, "value=") && (contiene(pl, "uri") || contiene(pl, "url"))) uri = 1;
            const char *tv = iemp(pl, "type=") ? pl + 5 : pl;           /* TYPE=a,b  |  2.1: HOME;CELL */
            if (contiene(tv, "home")) t_casa = 1;
            if (contiene(tv, "work")) t_trab = 1;
            if (contiene(tv, "cell") || contiene(tv, "mobile") || contiene(tv, "iphone")) t_movil = 1;
        }
        if (ieq(nombre, "begin")) {
            uint32_t k = 0; while ((ch = vr_get(&r, 0)) >= 0) if (k + 1 < sizeof val) val[k++] = (char)ch;
            val[k] = 0;
            if (ieq(val, "vcard") || iemp(val, "vcard")) {
                if (dentro && c) { /* vCard dentro de otra (AGENT): se ignora la de dentro */ }
                else if (n < max) { c = &out[n]; zero(c, (uint32_t)sizeof *c); c->activo = 1; dentro = 1; tiene_adr = 0; }
                else { dentro = 1; c = 0; }
            }
            if (ch == -2) break;
            continue;
        }
        if (ieq(nombre, "end")) {
            vr_salta_linea(&r);
            if (dentro) {
                if (c) { ct_sanea(c); if (!ct_vacio(c)) ++n; else zero(c, (uint32_t)sizeof *c); }
                dentro = 0; c = 0;
            }
            continue;
        }
        if (!dentro || !c) { vr_salta_linea(&r); continue; }
        if (ieq(nombre, "photo") || ieq(nombre, "logo")) {
            if (ieq(nombre, "logo") && (c->foto_len || c->foto_url[0])) { vr_salta_linea(&r); continue; }
            /* base64 en streaming (la foto puede medir cientos de KiB): directa a `fotos` */
            uint32_t ini = fusados, k = 0; int ok = fotos && !c->foto_len, es_url = 0, es_data = 0;
            uint32_t acc = 0, nb = 0, pref = 0; char pre[8];
            while ((ch = vr_get(&r, 0)) >= 0) {
                if (pref < 5) { pre[pref++] = (char)ch; pre[pref] = 0; if (pref == 5 && (iemp(pre, "http:") || iemp(pre, "https"))) { es_url = 1; continue; } if (pref == 5 && iemp(pre, "data:")) es_data = 1; }
                if (es_url) { if (k + 1 < sizeof val) val[k++] = (char)ch; continue; }
                if (es_data) { if (ch == ',') { es_data = 0; acc = 0; nb = 0; fusados = ini; } continue; }   /* vCard 4: data:image/jpeg;base64,... */
                if (!ok) continue;
                if (ch == '=') continue;
                int v = b64v((char)ch); if (v < 0) continue;
                acc = (acc << 6) | (uint32_t)v; nb += 6;
                if (nb >= 8) { nb -= 8; if (fusados >= fotos_cap || fusados - ini >= foto_max) { ok = 0; fusados = ini; continue; } fotos[fusados++] = (uint8_t)(acc >> nb); }
            }
            if (es_url) {
                val[k] = 0;
                { char u[CT_URL]; uint32_t q = 0; for (uint32_t z = 0; z < pref && q + 1 < sizeof u; ++z) u[q++] = pre[z]; for (uint32_t z = 0; z < k && q + 1 < sizeof u; ++z) u[q++] = val[z]; u[q] = 0; if (q + 1 < sizeof u) cpy(c->foto_url, CT_URL, u); }
                fusados = ini;
            } else if (ok && fusados > ini && foto_tipo(fotos + ini, fusados - ini)) { c->foto_off = ini; c->foto_len = fusados - ini; }
            else fusados = ini;
            (void)b64; (void)uri;
            if (ch == -2) break;
            continue;
        }
        /* resto: valor en val (recortado si es enorme) */
        uint32_t k = 0;
        while ((ch = vr_get(&r, qp)) >= 0) if (k + 1 < sizeof val) val[k++] = (char)ch;
        val[k] = 0;
        if (qp) {                                                   /* =C3=B1 -> bytes */
            uint32_t w = 0;
            for (uint32_t i = 0; i < k; ++i) {
                if (val[i] == '=' && i + 2 < k + 1 && hexv(val[i + 1]) >= 0 && hexv(val[i + 2]) >= 0) { tmp[w++] = (char)(hexv(val[i + 1]) * 16 + hexv(val[i + 2])); i += 2; }
                else tmp[w++] = val[i];
            }
            tmp[w] = 0; copia_mem(val, tmp, w + 1); k = w;
        }
        if (latin || !utf8_valido(val, k)) { latin1_utf8(val, k, conv, sizeof conv); cpy(val, sizeof val, conv); }
        char a[CT_CALLE * 2];
        if (ieq(nombre, "fn")) { if (!c->nombre[0] && !c->apellidos[0]) { desescapa(val, a, sizeof a); pon_nombre(c, a); } }
        else if (ieq(nombre, "n")) {
            char nom[CT_NOM * 2], ape[CT_APE * 2], extra[CT_NOM];
            componente(val, 0, ape, sizeof ape, 1); componente(val, 1, nom, sizeof nom, 1); componente(val, 2, extra, sizeof extra, 1);
            if (nom[0] || ape[0]) {
                cpy(c->nombre, CT_NOM, nom);
                if (extra[0] && nom[0]) { uint32_t p = sl(c->nombre); put(c->nombre, CT_NOM, &p, " "); put(c->nombre, CT_NOM, &p, extra); }
                cpy(c->apellidos, CT_APE, ape);
            }
        }
        else if (ieq(nombre, "nickname")) { componente(val, 0, a, sizeof a, 0); { uint32_t z = 0; while (a[z] && a[z] != ',') ++z; a[z] = 0; } cpy(c->apodo, CT_APODO, a); }
        else if (ieq(nombre, "email")) {
            desescapa(val, a, sizeof a); { char *z = a; if (iemp(z, "mailto:")) { uint32_t m = 7, j = 0; while (a[m + j]) { a[j] = a[m + j]; ++j; } a[j] = 0; } }
            pon_email(c, a, t_trab ? CT_T_TRABAJO : t_casa ? CT_T_CASA : CT_T_OTRO);
        }
        else if (ieq(nombre, "tel")) {
            desescapa(val, a, sizeof a); { if (iemp(a, "tel:")) { uint32_t j = 0; while (a[4 + j]) { a[j] = a[4 + j]; ++j; } a[j] = 0; } }
            if (a[0]) pon_tel_nuevo(c, a, t_movil ? CT_T_MOVIL : t_trab ? CT_T_TRABAJO : t_casa ? CT_T_CASA : CT_T_OTRO);
        }
        else if (ieq(nombre, "adr")) {
            if (!tiene_adr || (t_casa && !c->calle[0])) {
                char ext[CT_CALLE], calle[CT_CALLE * 2];
                componente(val, 1, ext, sizeof ext, 0); componente(val, 2, calle, sizeof calle, 0);
                for (char *z = calle; *z; ++z) if (*z == '\n') *z = ' ';
                if (ext[0]) { uint32_t p = sl(calle); put(calle, sizeof calle, &p, ", "); put(calle, sizeof calle, &p, ext); }
                cpy(c->calle, CT_CALLE, calle);
                componente(val, 3, a, sizeof a, 0); cpy(c->ciudad, CT_CIUDAD, a);
                componente(val, 4, a, sizeof a, 0); cpy(c->prov, CT_PROV, a);
                componente(val, 5, a, sizeof a, 0); cpy(c->cp, CT_CP, a);
                componente(val, 6, a, sizeof a, 0); cpy(c->pais, CT_PAIS, a);
                tiene_adr = 1;
            }
        }
        else if (ieq(nombre, "org")) { componente(val, 0, a, sizeof a, 0); cpy(c->empresa, CT_EMPRESA, a); }
        else if (ieq(nombre, "title")) { desescapa(val, a, sizeof a); cpy(c->cargo, CT_CARGO, a); }
        else if (ieq(nombre, "bday")) { int y, m, d; desescapa(val, a, sizeof a); if (ct_parsea_fecha(a, &y, &m, &d) == 0) { c->nac_y = (int16_t)y; c->nac_m = (uint8_t)m; c->nac_d = (uint8_t)d; } }
        else if (ieq(nombre, "url")) { if (!c->web[0]) { desescapa(val, a, sizeof a); cpy(c->web, CT_WEB, a); } }
        else if (ieq(nombre, "note")) { desescapa(val, a, sizeof a); cpy(c->nota, CT_NOTA, a); }
        else if (ieq(nombre, "categories")) {
            /* lista separada por comas (escapadas con \,). Google: myContacts, starred */
            const char *s = val;
            while (*s) {
                uint32_t q = 0; char g[CT_GRUPO * 2];
                while (*s && *s != ',') { if (*s == '\\' && s[1]) { if (q + 1 < sizeof g) g[q++] = s[1]; s += 2; continue; } if (q + 1 < sizeof g) g[q++] = *s; ++s; }
                g[q] = 0; if (*s == ',') ++s;
                char *gg = g; while (*gg == ' ') ++gg;
                { uint32_t e = sl(gg); while (e && gg[e - 1] == ' ') gg[--e] = 0; }
                if (ieq(gg, "starred")) { c->fav = 1; continue; }
                if (!gg[0] || ieq(gg, "mycontacts") || iemp(gg, "* ") || ieq(gg, "my contacts")) continue;
                une(c->grupo, CT_GRUPO, gg);
            }
        }
        else if (ieq(nombre, "uid")) { desescapa(val, a, sizeof a); cpy(c->uid, CT_UID, a); }
        else if (ieq(nombre, "rev")) { uint32_t q = 0; for (const char *s = val; *s && q + 1 < CT_REV; ++s) if (es_dig(*s) || *s == 'T' || *s == 'Z') c->rev[q++] = *s; c->rev[q] = 0; }
        else if (ieq(nombre, "x-soma-id")) { const char *s = val; int v = leer_num(&s, 9); if (v > 0) c->id = (uint32_t)v; }
        else if (ieq(nombre, "x-soma-fav")) c->fav = val[0] == '1';
        else if (ieq(nombre, "x-soma-cambiado")) c->cambiado = val[0] == '1';
        else if (ieq(nombre, "x-soma-fotos")) { const char *s = val; int v = leer_num(&s, 2); if (v > 0) c->nfotos = (uint8_t)(v > CT_FOTOS ? CT_FOTOS : v); }
        else if (ieq(nombre, "x-soma-voz")) c->voz = val[0] == '1';
        if (ch == -2) break;
    }
    if (dentro && c) { ct_sanea(c); if (!ct_vacio(c)) ++n; }       /* sin END:VCARD al final: se queda lo leido */
    if (fotos_n) *fotos_n = fusados;
    return n;
}

/* ================================================================ fusion */
static int campo_fusiona(char *d, uint32_t cap, const char *s, int local_gana)
{
    if (!s[0] || ieq(d, s)) return 0;
    if (d[0] && local_gana) return 0;
    if (igual_pl(d, s) && d[0]) return 0;
    cpy(d, cap, s); return 1;
}
static int coincide_fusion(const struct ct_contacto *a, const struct ct_contacto *n)
{
    if (a->uid[0] && n->uid[0]) return ieq(a->uid, n->uid);
    return 0;
}
int ct_fusiona(struct ct_libreta *l, const struct ct_contacto *n, struct ct_contacto **dest) { return ct_fusiona_ex(l, n, dest, 0); }
int ct_fusiona_ex(struct ct_libreta *l, const struct ct_contacto *n, struct ct_contacto **dest, int manda_nuevo)
{
    struct ct_contacto *m = 0;
    for (int i = 0; i < CT_MAX && !m; ++i) if (l->c[i].activo && coincide_fusion(&l->c[i], n)) m = &l->c[i];
    for (int i = 0; i < CT_MAX && !m; ++i) {
        if (!l->c[i].activo) continue;
        for (int a = 0; a < CT_NEMAIL && !m; ++a) for (int b = 0; b < CT_NEMAIL && !m; ++b)
            if (l->c[i].email[a][0] && n->email[b][0] && ieq(l->c[i].email[a], n->email[b])) m = &l->c[i];
    }
    if (!m && (n->nombre[0] || n->apellidos[0])) {
        char x[CT_NOM + CT_APE + 4], y[CT_NOM + CT_APE + 4]; ct_nombre_completo(n, y, sizeof y);
        for (int i = 0; i < CT_MAX && !m; ++i) {
            if (!l->c[i].activo || (!l->c[i].nombre[0] && !l->c[i].apellidos[0])) continue;
            ct_nombre_completo(&l->c[i], x, sizeof x);
            if (igual_pl(x, y)) m = &l->c[i];
        }
    }
    if (!m) {
        struct ct_contacto *c = ct_nuevo(l); if (!c) return -1;
        uint32_t id = c->id; *c = *n; c->id = id; c->activo = 1; c->cambiado = 0; c->nfotos = 0; c->voz = 0; c->foto_off = 0; c->foto_len = 0;
        ct_sanea(c); if (dest) *dest = c; return 1;
    }
    if (dest) *dest = m;
    int local_gana = m->cambiado;
    if (manda_nuevo) local_gana = 0;
    else if (n->rev[0] && m->rev[0]) { int k = 0; while (n->rev[k] && n->rev[k] == m->rev[k]) ++k; if ((uint8_t)n->rev[k] > (uint8_t)m->rev[k]) local_gana = 0; else if (m->cambiado || (uint8_t)n->rev[k] < (uint8_t)m->rev[k]) local_gana = 1; }
    int cambia = 0;
    cambia |= campo_fusiona(m->nombre, CT_NOM, n->nombre, local_gana);
    cambia |= campo_fusiona(m->apellidos, CT_APE, n->apellidos, local_gana);
    cambia |= campo_fusiona(m->apodo, CT_APODO, n->apodo, local_gana);
    cambia |= campo_fusiona(m->calle, CT_CALLE, n->calle, local_gana);
    cambia |= campo_fusiona(m->ciudad, CT_CIUDAD, n->ciudad, local_gana);
    cambia |= campo_fusiona(m->cp, CT_CP, n->cp, local_gana);
    cambia |= campo_fusiona(m->prov, CT_PROV, n->prov, local_gana);
    cambia |= campo_fusiona(m->pais, CT_PAIS, n->pais, local_gana);
    cambia |= campo_fusiona(m->empresa, CT_EMPRESA, n->empresa, local_gana);
    cambia |= campo_fusiona(m->cargo, CT_CARGO, n->cargo, local_gana);
    cambia |= campo_fusiona(m->web, CT_WEB, n->web, local_gana);
    cambia |= campo_fusiona(m->nota, CT_NOTA, n->nota, local_gana);
    cambia |= campo_fusiona(m->foto_url, CT_URL, n->foto_url, local_gana);
    if (!m->uid[0] && n->uid[0]) { cpy(m->uid, CT_UID, n->uid); cambia = 1; }
    if (n->nac_m && (!m->nac_m || (!local_gana && (m->nac_m != n->nac_m || m->nac_d != n->nac_d || (n->nac_y && m->nac_y != n->nac_y))))) {
        m->nac_y = n->nac_y ? n->nac_y : m->nac_y; m->nac_m = n->nac_m; m->nac_d = n->nac_d; cambia = 1;
    }
    if (n->fav && !m->fav) { m->fav = 1; cambia = 1; }
    { char g[CT_GRUPO]; cpy(g, sizeof g, m->grupo); const char *s = n->grupo;
      while (*s) { while (*s == ' ' || *s == ',') ++s; const char *e = s; while (*e && *e != ',') ++e; char t[CT_GRUPO]; cpyn(t, sizeof t, s, (uint32_t)(e - s)); une(g, sizeof g, t); s = e; }
      if (!ieq(g, m->grupo)) { cpy(m->grupo, CT_GRUPO, g); cambia = 1; } }
    /* emails y telefonos: si manda lo importado, se toma su lista; si manda SOMA, se juntan */
    int hay_em = 0, hay_tel = 0; for (int i = 0; i < CT_NEMAIL; ++i) hay_em |= n->email[i][0] != 0; for (int i = 0; i < CT_NTEL; ++i) hay_tel |= n->tel[i][0] != 0;
    if (hay_em) {
        struct ct_contacto t = *m;
        if (!local_gana) { for (int i = 0; i < CT_NEMAIL; ++i) { t.email[i][0] = 0; t.t_email[i] = 0; } for (int i = 0; i < CT_NEMAIL; ++i) if (n->email[i][0]) pon_email(&t, n->email[i], n->t_email[i]); for (int i = 0; i < CT_NEMAIL; ++i) if (m->email[i][0]) pon_email(&t, m->email[i], m->t_email[i]); }
        else for (int i = 0; i < CT_NEMAIL; ++i) if (n->email[i][0]) pon_email(&t, n->email[i], n->t_email[i]);
        for (int i = 0; i < CT_NEMAIL; ++i) if (!ieq(t.email[i], m->email[i]) || t.t_email[i] != m->t_email[i]) { cambia = 1; break; }
        for (int i = 0; i < CT_NEMAIL; ++i) { copia_mem(m->email[i], t.email[i], CT_EMAIL); m->t_email[i] = t.t_email[i]; }
    }
    if (hay_tel) {
        struct ct_contacto t = *m;
        if (!local_gana) { for (int i = 0; i < CT_NTEL; ++i) { t.tel[i][0] = 0; t.t_tel[i] = 0; } for (int i = 0; i < CT_NTEL; ++i) if (n->tel[i][0]) pon_tel_nuevo(&t, n->tel[i], n->t_tel[i]); }
        else for (int i = 0; i < CT_NTEL; ++i) if (n->tel[i][0]) pon_tel_nuevo(&t, n->tel[i], n->t_tel[i]);
        for (int i = 0; i < CT_NTEL; ++i) if (!ieq(t.tel[i], m->tel[i]) || t.t_tel[i] != m->t_tel[i]) { cambia = 1; break; }
        for (int i = 0; i < CT_NTEL; ++i) { copia_mem(m->tel[i], t.tel[i], CT_TEL); m->t_tel[i] = t.t_tel[i]; }
    }
    if (cambia && !local_gana && n->rev[0]) cpy(m->rev, CT_REV, n->rev);
    ct_sanea(m);
    return cambia ? 2 : 0;
}
