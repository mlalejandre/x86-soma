/* SOMA_358_HOJA: ver hoja_core.h. Las formulas (tokenizador, compilador, evaluador y funciones) estan en hoja_formula.inc. */
#include "hoja_core.h"

int64_t (*hj_ahora_fn)(void);

/* ================================================================ utilidades */
uint32_t hj_slen(const char *s) { uint32_t n = 0; if (!s) return 0; while (s[n]) ++n; return n; }
static void zero(void *p, uint32_t n) { uint8_t *q = (uint8_t *)p; while (n--) *q++ = 0; }
static void copia(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; } }
static int igual_mem(const void *a, const void *b, uint32_t n) { const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b; while (n--) if (*x++ != *y++) return 0; return 1; }
char *hj_strndup(const char *s, uint32_t n) { char *d = (char *)hj_alloc(n + 1); if (!d) return 0; copia(d, s, n); d[n] = 0; return d; }
char *hj_strdup(const char *s) { return hj_strndup(s, hj_slen(s)); }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; if (!cap) return; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; ++i; } while (i && ((uint8_t)d[i] & 0xC0) == 0x80 && i + 1 >= cap) --i; d[i] = 0; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putc_(char *o, uint32_t cap, uint32_t *p, char c) { if (*p + 1 < cap) { o[(*p)++] = c; o[*p] = 0; } }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) putc_(o, cap, p, '-'); do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) putc_(o, cap, p, b[--n]); }
static int es_dig(char c) { return c >= '0' && c <= '9'; }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static char sube(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static char pliega_sig(const char **pp)                          /* minuscula sin acento (latinos de 2 bytes) */
{
    const uint8_t *p = (const uint8_t *)*pp; uint8_t c = p[0];
    if (!c) return 0;
    if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
        uint8_t d = p[1]; *pp += 2; if (d >= 0xA0) d = (uint8_t)(d - 0x20);
        if (d <= 0x86) return 'a';
        if (d == 0x87) return 'c';
        if (d >= 0x88 && d <= 0x8B) return 'e';
        if (d >= 0x8C && d <= 0x8F) return 'i';
        if (d == 0x91) return 'n';
        if ((d >= 0x92 && d <= 0x96) || d == 0x98) return 'o';
        if (d >= 0x99 && d <= 0x9C) return 'u';
        if (d == 0x9D || d == 0x9F) return 'y';
        return '?';
    }
    *pp += 1; return baja((char)c);
}
int hj_ieq_pl(const char *a, const char *b) { for (;;) { char x = pliega_sig(&a), y = pliega_sig(&b); if (x != y) return 0; if (!x) return 1; } }
static f64 F(int64_t v) { return f64_from_i64(v); }
static int f64_es_cero(f64 a) { return (a & 0x7FFFFFFFFFFFFFFFULL) == 0; }

/* ================================================================ libro y hojas */
static int hoja_ini(struct hj_hoja *h, const char *nombre)
{
    zero(h, (uint32_t)sizeof *h); cpy(h->nombre, HJ_NOMBRE, nombre);
    h->nhash = 256; h->hash = (int32_t *)hj_alloc(h->nhash * 4u); if (!h->hash) return -1;
    for (uint32_t i = 0; i < h->nhash; ++i) h->hash[i] = -1;
    h->libres = -1;
    return 0;
}
static void celda_libera(struct hj_celda *c) { if (c->txt) hj_free(c->txt); if (c->form) hj_free(c->form); if (c->code) hj_free(c->code); c->txt = c->form = 0; c->code = 0; c->ncode = 0; }
static void hoja_libera(struct hj_hoja *h)
{
    for (int32_t i = 0; i < h->nc; ++i) celda_libera(&h->c[i]);
    if (h->c) hj_free(h->c);
    if (h->hash) hj_free(h->hash);
    if (h->anchos) hj_free(h->anchos);
    if (h->altos) hj_free(h->altos);
    if (h->oculta) hj_free(h->oculta);
    zero(h, (uint32_t)sizeof *h);
}
int hj_libro_nuevo(struct hj_libro *l)
{
    zero(l, (uint32_t)sizeof *l);
    l->e = (struct hj_estilo *)hj_alloc(HJ_ESTILOS * (uint32_t)sizeof(struct hj_estilo));
    if (!l->e) return -1;
    zero(&l->e[0], (uint32_t)sizeof l->e[0]); l->ne = 1;
    if (hoja_ini(&l->h[0], "Hoja1")) { hj_free(l->e); l->e = 0; return -1; }
    l->nh = 1; l->gen = 1; l->version = 1;
    return 0;
}
void hj_libro_libera(struct hj_libro *l)
{
    for (int i = 0; i < l->nh; ++i) hoja_libera(&l->h[i]);
    if (l->e) hj_free(l->e);
    zero(l, (uint32_t)sizeof *l);
}
static int nombre_valido(const char *n)
{
    uint32_t L = hj_slen(n); if (!L || L >= HJ_NOMBRE) return 0;
    if (n[0] == '\'' || n[L - 1] == '\'') return 0;
    for (const char *s = n; *s; ++s) if (*s == '[' || *s == ']' || *s == ':' || *s == '*' || *s == '?' || *s == '/' || *s == '\\' || (uint8_t)*s < 32) return 0;
    return 1;
}
int hj_hoja_busca(const struct hj_libro *l, const char *nombre) { for (int i = 0; i < l->nh; ++i) if (hj_ieq_pl(l->h[i].nombre, nombre)) return i; return -1; }
int hj_hoja_nueva(struct hj_libro *l, const char *nombre)
{
    if (l->nh >= HJ_HOJAS) return -1;
    char nb[HJ_NOMBRE];
    if (nombre && nombre[0]) { if (!nombre_valido(nombre) || hj_hoja_busca(l, nombre) >= 0) return -1; cpy(nb, sizeof nb, nombre); }
    else for (int k = l->nh + 1; ; ++k) { uint32_t p = 0; nb[0] = 0; put(nb, sizeof nb, &p, "Hoja"); putn(nb, sizeof nb, &p, k); if (hj_hoja_busca(l, nb) < 0) break; }
    if (hoja_ini(&l->h[l->nh], nb)) return -1;
    l->version++;
    return l->nh++;
}
static void formulas_rehace(struct hj_libro *l);                 /* tras cambiar hojas: recompilar todo */
static int form_hojas(struct hj_libro *l, int borrada, int de, int a);
static int g_ren_h; static const char *g_ren_nombre;
int hj_hoja_borra(struct hj_libro *l, int h)
{
    if (h < 0 || h >= l->nh || l->nh <= 1) return -1;
    (void)form_hojas(l, h, -1, -1);                              /* referencias a ella -> #REF! (antes de mover los indices) */
    l->ncel -= (uint32_t)l->h[h].nc;
    hoja_libera(&l->h[h]);
    for (int i = h; i + 1 < l->nh; ++i) l->h[i] = l->h[i + 1];
    zero(&l->h[l->nh - 1], (uint32_t)sizeof l->h[0]);
    l->nh--; if (l->activa >= l->nh) l->activa = l->nh - 1;
    /* SOMA_360_HOJA_GRAFICOS: los graficos que miraban datos de otra hoja siguen a su hoja (o se quedan con la suya) */
    for (int i = 0; i < l->nh; ++i) for (uint32_t g = 0; g < l->h[i].ngraf; ++g) {
        uint8_t *d = &l->h[i].graf[g].hoja_datos;
        if (*d == h) *d = (uint8_t)i; else if (*d > h) --*d;
    }
    formulas_rehace(l); l->version++;
    return 0;
}
int hj_hoja_renombra(struct hj_libro *l, int h, const char *nombre)
{
    if (h < 0 || h >= l->nh) return -1;
    if (!nombre_valido(nombre)) return -1;
    int o = hj_hoja_busca(l, nombre); if (o >= 0 && o != h) return -2;
    char nuevo[HJ_NOMBRE]; cpy(nuevo, sizeof nuevo, nombre);
    g_ren_h = h; g_ren_nombre = nuevo;                           /* las formulas se leen con el nombre viejo y se escriben con el nuevo */
    (void)form_hojas(l, -1, h, h);
    g_ren_h = -1; g_ren_nombre = 0;
    cpy(l->h[h].nombre, HJ_NOMBRE, nuevo);
    l->version++;
    return 0;
}
int hj_hoja_mueve(struct hj_libro *l, int de, int a)
{
    if (de < 0 || de >= l->nh || a < 0 || a >= l->nh || de == a) return -1;
    struct hj_hoja t = l->h[de];
    if (de < a) for (int i = de; i < a; ++i) l->h[i] = l->h[i + 1]; else for (int i = de; i > a; --i) l->h[i] = l->h[i - 1];
    l->h[a] = t;
    for (int i = 0; i < l->nh; ++i) for (uint32_t g = 0; g < l->h[i].ngraf; ++g) {   /* SOMA_360_HOJA_GRAFICOS */
        int d = l->h[i].graf[g].hoja_datos, nd = d;
        if (d == de) nd = a; else if (de < a && d > de && d <= a) nd = d - 1; else if (de > a && d >= a && d < de) nd = d + 1;
        l->h[i].graf[g].hoja_datos = (uint8_t)nd;
    }
    formulas_rehace(l); l->version++;
    return 0;
}

/* ================================================================ celdas (tabla hash) */
static uint32_t hclave(uint32_t f, uint32_t c) { uint32_t k = f * 2654435761u ^ (c * 40503u + 0x9E37u); k ^= k >> 15; k *= 2246822519u; k ^= k >> 13; return k; }
static int32_t busca_i(const struct hj_hoja *h, uint32_t f, uint32_t c)
{
    if (!h->hash) return -1;
    for (int32_t i = h->hash[hclave(f, c) & (h->nhash - 1)]; i >= 0; i = h->c[i].sig) if (h->c[i].fila == f && h->c[i].col == c) return i;
    return -1;
}
static void rehash(struct hj_hoja *h, uint32_t nuevo)
{
    int32_t *t = (int32_t *)hj_alloc(nuevo * 4u); if (!t) return;
    hj_free(h->hash); h->hash = t; h->nhash = nuevo;
    for (uint32_t i = 0; i < nuevo; ++i) t[i] = -1;
    for (int32_t i = 0; i < h->nc; ++i) { uint32_t b = hclave(h->c[i].fila, h->c[i].col) & (nuevo - 1); h->c[i].sig = t[b]; t[b] = i; }
}
struct hj_celda *hj_celda(struct hj_libro *l, int h, uint32_t f, uint32_t c)
{
    if (h < 0 || h >= l->nh) return 0;
    int32_t i = busca_i(&l->h[h], f, c); return i < 0 ? 0 : &l->h[h].c[i];
}
const struct hj_celda *hj_celda_c(const struct hj_libro *l, int h, uint32_t f, uint32_t c)
{
    if (h < 0 || h >= l->nh) return 0;
    int32_t i = busca_i(&l->h[h], f, c); return i < 0 ? 0 : &l->h[h].c[i];
}
static struct hj_celda *celda_crea(struct hj_libro *l, int h, uint32_t f, uint32_t c)
{
    if (h < 0 || h >= l->nh || f >= HJ_FILAS || c >= HJ_COLS) return 0;
    struct hj_hoja *s = &l->h[h];
    int32_t i = busca_i(s, f, c); if (i >= 0) return &s->c[i];
    if (l->ncel >= HJ_CELDAS_MAX) { l->sin_memoria = 1; return 0; }
    if (s->nc >= s->capc) {
        int32_t nc = s->capc ? s->capc * 2 : 64;
        struct hj_celda *n = (struct hj_celda *)hj_alloc((uint32_t)nc * (uint32_t)sizeof *n);
        if (!n) { l->sin_memoria = 1; return 0; }
        if (s->c) { copia(n, s->c, (uint32_t)s->nc * (uint32_t)sizeof *n); hj_free(s->c); }
        s->c = n; s->capc = nc;
    }
    i = s->nc++; struct hj_celda *x = &s->c[i]; zero(x, (uint32_t)sizeof *x);
    x->fila = f; x->col = (uint16_t)c;
    uint32_t b = hclave(f, c) & (s->nhash - 1); x->sig = s->hash[b]; s->hash[b] = i;
    l->ncel++;
    if ((uint32_t)s->nc > s->nhash - s->nhash / 4) rehash(s, s->nhash * 2);
    if (f + 1 > s->max_fila) s->max_fila = f + 1;
    if (c + 1 > s->max_col) s->max_col = c + 1;
    return &s->c[i];
}
static void vacia(struct hj_celda *x) { celda_libera(x); x->t = HJ_VACIA; x->err = 0; x->es_form = 0; x->n = 0; x->gen = 0; x->estado = 0; }
int hj_pon_num(struct hj_libro *l, int h, uint32_t f, uint32_t c, f64 v)
{
    struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2;
    vacia(x); if (!f64_is_finite(v)) { x->t = HJ_ERR; x->err = HJE_NUM; } else { x->t = HJ_NUM; x->n = v; }
    l->version++; return 0;
}
int hj_pon_txt(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *s)
{
    if (!s || !s[0]) { hj_borra_celda(l, h, f, c); return 0; }
    struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2;
    uint32_t n = hj_slen(s); if (n > HJ_TXT_MAX) n = HJ_TXT_MAX;
    while (n && ((uint8_t)s[n] & 0xC0) == 0x80 && n == HJ_TXT_MAX) --n;
    char *d = hj_strndup(s, n); if (!d) { l->sin_memoria = 1; return -2; }
    vacia(x); x->t = HJ_TXT; x->txt = d; l->version++; return 0;
}
int hj_pon_bool(struct hj_libro *l, int h, uint32_t f, uint32_t c, int b) { struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2; vacia(x); x->t = HJ_BOOL; x->n = b ? F(1) : 0; l->version++; return 0; }
int hj_pon_err(struct hj_libro *l, int h, uint32_t f, uint32_t c, int e) { struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2; vacia(x); x->t = HJ_ERR; x->err = (uint8_t)(e > 0 && e < HJE_N ? e : HJE_VALOR); l->version++; return 0; }
static int compila(const struct hj_libro *l, int h, const char *canon, uint8_t **code, uint32_t *ncode);
int hj_pon_form(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *canon)
{
    while (*canon == '=' || *canon == ' ') ++canon;
    uint32_t n = hj_slen(canon); if (!n || n > HJ_FORM_MAX) return -1;
    struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2;
    char *d = hj_strndup(canon, n); if (!d) { l->sin_memoria = 1; return -2; }
    vacia(x); x->es_form = 1; x->form = d; x->t = HJ_NUM; x->n = 0;
    l->version++; return 0;
}
int hj_pon_form_cache(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *canon)
{
    while (*canon == '=' || *canon == ' ') ++canon;
    uint32_t n = hj_slen(canon); if (!n || n > HJ_FORM_MAX) return -1;
    struct hj_celda *x = celda_crea(l, h, f, c); if (!x) return -2;
    char *d = hj_strndup(canon, n); if (!d) { l->sin_memoria = 1; return -2; }
    if (x->form) hj_free(x->form);
    if (x->code) { hj_free(x->code); x->code = 0; x->ncode = 0; }
    x->form = d; x->es_form = 2; x->gen = 0; x->estado = 0;
    l->version++; return 0;
}
int hj_pon_estilo(struct hj_libro *l, int h, uint32_t f, uint32_t c, uint16_t est)
{
    struct hj_celda *x = hj_celda(l, h, f, c);
    if (!x) { if (!est) return 0; x = celda_crea(l, h, f, c); if (!x) return -2; }
    x->estilo = est < l->ne ? est : 0; l->version++; return 0;
}
void hj_borra_celda(struct hj_libro *l, int h, uint32_t f, uint32_t c) { struct hj_celda *x = hj_celda(l, h, f, c); if (x) { vacia(x); l->version++; } }
void hj_borra_rango(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int tambien_estilo)
{
    if (h < 0 || h >= l->nh) return;
    struct hj_hoja *s = &l->h[h];
    for (int32_t i = 0; i < s->nc; ++i) { struct hj_celda *x = &s->c[i]; if (x->fila >= f1 && x->fila <= f2 && x->col >= c1 && x->col <= c2) { vacia(x); if (tambien_estilo) x->estilo = 0; } }
    l->version++;
}
uint32_t hj_estilo_id(struct hj_libro *l, const struct hj_estilo *e)
{
    struct hj_estilo t = *e; t.codigo[HJ_NFCOD - 1] = 0;
    { uint32_t k = hj_slen(t.codigo); while (++k < HJ_NFCOD) t.codigo[k] = 0; }
    if (t.decimales > 15) t.decimales = 15;
    for (uint32_t i = 0; i < l->ne; ++i) if (igual_mem(&l->e[i], &t, (uint32_t)sizeof t)) return i;
    if (l->ne >= HJ_ESTILOS) return 0;
    l->e[l->ne] = t; return l->ne++;
}
const struct hj_estilo *hj_estilo_de(const struct hj_libro *l, uint16_t id) { return &l->e[id < l->ne ? id : 0]; }

/* ================================================================ columnas y filas */
static uint16_t tam_de(const struct hj_tam *t, uint32_t n, uint32_t i, uint16_t def)
{
    uint32_t a = 0, b = n; while (a < b) { uint32_t m = (a + b) / 2; if (t[m].i < i) a = m + 1; else b = m; }
    return a < n && t[a].i == i ? t[a].px : def;
}
static void tam_pon(struct hj_tam **t, uint32_t *n, uint32_t *cap, uint32_t i, uint16_t px, uint16_t def)
{
    uint32_t a = 0, b = *n; while (a < b) { uint32_t m = (a + b) / 2; if ((*t)[m].i < i) a = m + 1; else b = m; }
    if (a < *n && (*t)[a].i == i) { if (px == def) { copia(&(*t)[a], &(*t)[a + 1], (*n - a - 1) * (uint32_t)sizeof **t); (*n)--; } else (*t)[a].px = px; return; }
    if (px == def) return;
    if (*n >= *cap) { uint32_t nc = *cap ? *cap * 2 : 16; struct hj_tam *x = (struct hj_tam *)hj_alloc(nc * (uint32_t)sizeof *x); if (!x) return; if (*t) { copia(x, *t, *n * (uint32_t)sizeof *x); hj_free(*t); } *t = x; *cap = nc; }
    copia(&(*t)[a + 1], &(*t)[a], (*n - a) * (uint32_t)sizeof **t); (*t)[a].i = i; (*t)[a].px = px; (*n)++;
}
uint16_t hj_ancho(const struct hj_hoja *h, uint32_t c) { return tam_de(h->anchos, h->nanchos, c, HJ_ANCHO_DEF); }
uint16_t hj_alto(const struct hj_hoja *h, uint32_t f) { return tam_de(h->altos, h->naltos, f, HJ_ALTO_DEF); }
void hj_pon_ancho(struct hj_hoja *h, uint32_t c, uint16_t px) { if (px < 8) px = 8; if (px > 2000) px = 2000; tam_pon(&h->anchos, &h->nanchos, &h->capanchos, c, px, HJ_ANCHO_DEF); }
void hj_pon_alto(struct hj_hoja *h, uint32_t f, uint16_t px) { if (px < 8) px = 8; if (px > 1000) px = 1000; tam_pon(&h->altos, &h->naltos, &h->capaltos, f, px, HJ_ALTO_DEF); }
int hj_fila_oculta(const struct hj_hoja *h, uint32_t f) { return h->oculta && f < h->noculta && (h->oculta[f >> 3] >> (f & 7) & 1); }
void hj_oculta_fila(struct hj_hoja *h, uint32_t f, int si)
{
    if (f >= HJ_FILAS) return;
    if (f >= h->noculta) {
        if (!si) return;
        uint32_t nn = (f + 1024) & ~1023u; uint8_t *x = (uint8_t *)hj_alloc(nn / 8); if (!x) return; zero(x, nn / 8);
        if (h->oculta) { copia(x, h->oculta, h->noculta / 8); hj_free(h->oculta); }
        h->oculta = x; h->noculta = nn;
    }
    if (si) h->oculta[f >> 3] |= (uint8_t)(1u << (f & 7)); else h->oculta[f >> 3] &= (uint8_t)~(1u << (f & 7));
}
void hj_nombre_col(uint32_t c, char *out)
{
    char t[4]; int n = 0; ++c;
    while (c && n < 3) { t[n++] = (char)('A' + (c - 1) % 26); c = (c - 1) / 26; }
    for (int i = 0; i < n; ++i) out[i] = t[n - 1 - i];
    out[n] = 0;
}
void hj_dir(uint32_t f, uint32_t c, char *out) { hj_nombre_col(c, out); uint32_t p = hj_slen(out); putn(out, 12, &p, (int64_t)f + 1); }
int hj_lee_dir(const char *s, uint32_t *f, uint32_t *c)
{
    const char *p = s; uint32_t col = 0, fila = 0; int nl = 0, nd = 0;
    if (*p == '$') ++p;
    while (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) && nl < 4) { col = col * 26 + (uint32_t)(sube(*p) - 'A' + 1); ++p; ++nl; }
    if (!nl || nl > 3) return -1;
    if (*p == '$') ++p;
    while (es_dig(*p) && nd < 8) { fila = fila * 10 + (uint32_t)(*p - '0'); ++p; ++nd; }
    if (!nd || !fila || fila > HJ_FILAS || col > HJ_COLS) return -1;
    *f = fila - 1; *c = col - 1; return (int)(p - s);
}
int hj_lee_rango(const char *s, uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2)
{
    while (*s == ' ') ++s;
    int k = hj_lee_dir(s, f1, c1);
    if (k > 0) {
        *f2 = *f1; *c2 = *c1;
        if (s[k] == ':') { int k2 = hj_lee_dir(s + k + 1, f2, c2); if (k2 < 0) return -1; k += 1 + k2; }
        if (*f2 < *f1) { uint32_t t = *f1; *f1 = *f2; *f2 = t; }
        if (*c2 < *c1) { uint32_t t = *c1; *c1 = *c2; *c2 = t; }
        return k;
    }
    /* "A:C" columnas enteras, "3:5" filas enteras */
    const char *p = s; uint32_t a = 0, b = 0; int n = 0;
    if (*p == '$') ++p;
    while (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) && n < 4) { a = a * 26 + (uint32_t)(sube(*p) - 'A' + 1); ++p; ++n; }
    if (n && n <= 3 && *p == ':') {
        ++p; if (*p == '$') ++p; int m = 0; while (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) && m < 4) { b = b * 26 + (uint32_t)(sube(*p) - 'A' + 1); ++p; ++m; }
        if (!m || m > 3 || a > HJ_COLS || b > HJ_COLS) return -1;
        *c1 = (a < b ? a : b) - 1; *c2 = (a < b ? b : a) - 1; *f1 = 0; *f2 = HJ_FILAS - 1; return (int)(p - s);
    }
    p = s; n = 0; a = 0; if (*p == '$') ++p;
    while (es_dig(*p) && n < 8) { a = a * 10 + (uint32_t)(*p - '0'); ++p; ++n; }
    if (n && *p == ':') {
        ++p; if (*p == '$') ++p; int m = 0; b = 0; while (es_dig(*p) && m < 8) { b = b * 10 + (uint32_t)(*p - '0'); ++p; ++m; }
        if (!m || !a || !b || a > HJ_FILAS || b > HJ_FILAS) return -1;
        *f1 = (a < b ? a : b) - 1; *f2 = (a < b ? b : a) - 1; *c1 = 0; *c2 = HJ_COLS - 1; return (int)(p - s);
    }
    return -1;
}

/* ================================================================ fechas y numeros */
static int64_t dias_civil(int y, int m, int d)
{
    y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil(int64_t z, int *y, int *m, int *d)
{
    z += 719468; int64_t era = (z >= 0 ? z : z - 146096) / 146097; int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100); int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yy + (*m <= 2));
}
static int bisiesto(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int dias_mes(int y, int m) { static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && bisiesto(y) ? 29 : D[(m - 1) % 12]; }
#define EPOCA_EXCEL (-25569)                                     /* 1899-12-30 en dias desde 1970 */
f64 hj_fecha_serie(int y, int m, int d)
{
    /* FECHA(2026;14;1) = febrero de 2027, como Excel */
    int64_t mm = (int64_t)y * 12 + (m - 1); int yy = (int)(mm / 12), mo = (int)(mm % 12) + 1; if (mo <= 0) { mo += 12; yy--; }
    return F(dias_civil(yy, mo, 1) + (d - 1) - EPOCA_EXCEL);
}
void hj_serie_fecha(f64 s, int *y, int *m, int *d, int *hh, int *mi, int *ss)
{
    f64 fl = f64_floor(s); int64_t dias = f64_to_i64(fl);
    int64_t seg = f64_to_i64(f64_round(f64_mul(f64_sub(s, fl), F(86400))));
    if (seg >= 86400) { seg -= 86400; dias++; }
    civil(dias + EPOCA_EXCEL, y, m, d);
    if (hh) *hh = (int)(seg / 3600);
    if (mi) *mi = (int)(seg / 60 % 60);
    if (ss) *ss = (int)(seg % 60);
}
static f64 hoy_serie(int con_hora)
{
    int64_t min = hj_ahora_fn ? hj_ahora_fn() : dias_civil(2026, 1, 1) * 1440;
    int64_t dias = min >= 0 ? min / 1440 : -((-min + 1439) / 1440);
    f64 s = F(dias - EPOCA_EXCEL);
    if (con_hora) s = f64_add(s, f64_div(F(min - dias * 1440), F(1440)));
    return s;
}
/* "1234.5" con punto -> f64 (f64_parse) */
static int parse_punto(const char *s, f64 *v)
{
    const char *e = 0; f64 x = f64_parse(s, &e);
    if (!e || e == s) return 0;
    while (*e == ' ') ++e;
    if (*e) return 0;
    *v = x; return 1;
}
int hj_lee_numero(const char *s0, f64 *v, int *nf, int *dec)
{
    char b[96]; uint32_t n = 0; const char *s = s0;
    while (*s == ' ') ++s;
    if (nf) *nf = NF_GENERAL;
    if (dec) *dec = 0;
    uint32_t L = hj_slen(s); while (L && s[L - 1] == ' ') --L;
    if (!L || L > 80) return 0;
    int pct = 0, euro = 0, neg = 0;
    char t[96]; cpy(t, sizeof t, s); t[L] = 0;
    if (L && t[L - 1] == '%') { pct = 1; t[--L] = 0; while (L && t[L - 1] == ' ') t[--L] = 0; }
    if (L >= 3 && (uint8_t)t[L - 3] == 0xE2 && (uint8_t)t[L - 2] == 0x82 && (uint8_t)t[L - 1] == 0xAC) { euro = 1; L -= 3; t[L] = 0; while (L && t[L - 1] == ' ') t[--L] = 0; }
    const char *q = t;
    if (!euro && (uint8_t)q[0] == 0xE2 && (uint8_t)q[1] == 0x82 && (uint8_t)q[2] == 0xAC) { euro = 1; q += 3; while (*q == ' ') ++q; }
    if (*q == '-') { neg = 1; ++q; } else if (*q == '+') ++q;
    if (!pct && !euro) {                                         /* fecha u hora */
        int a, bb, c; const char *z = q;
        uint32_t k = 0; a = 0; while (es_dig(z[k]) && k < 5) { a = a * 10 + (z[k] - '0'); ++k; }
        if (k && (z[k] == '/' || (z[k] == '-' && !neg))) {
            char sep = z[k]; const char *w = z + k + 1; bb = 0; uint32_t k2 = 0; while (es_dig(w[k2]) && k2 < 3) { bb = bb * 10 + (w[k2] - '0'); ++k2; }
            if (k2 && w[k2] == sep) { const char *x = w + k2 + 1; c = 0; uint32_t k3 = 0; while (es_dig(x[k3]) && k3 < 5) { c = c * 10 + (x[k3] - '0'); ++k3; }
                int y, m, d; if (k == 4) { y = a; m = bb; d = c; } else { d = a; m = bb; y = c; if (k3 == 2) y += y < 50 ? 2000 : 1900; }
                const char *rest = x + k3; int hh = 0, mi = 0, ss = 0, con_h = 0;
                if (*rest == ' ' && es_dig(rest[1])) { const char *r = rest + 1; hh = 0; while (es_dig(*r)) hh = hh * 10 + (*r++ - '0'); if (*r == ':') { ++r; mi = 0; while (es_dig(*r)) mi = mi * 10 + (*r++ - '0'); if (*r == ':') { ++r; while (es_dig(*r)) ss = ss * 10 + (*r++ - '0'); } con_h = 1; rest = r; } }
                if (k3 && !*rest && !neg && m >= 1 && m <= 12 && d >= 1 && y >= 1900 && y <= 9999 && d <= dias_mes(y, m) && hh < 24 && mi < 60 && ss < 60) {
                    *v = hj_fecha_serie(y, m, d);
                    if (con_h) *v = f64_add(*v, f64_div(F(hh * 3600 + mi * 60 + ss), F(86400)));
                    if (nf) *nf = con_h ? NF_FECHAHORA : NF_FECHA;
                    return 1;
                } }
        }
        if (k && k <= 2 && z[k] == ':') {                         /* 14:30 o 14:30:15 */
            const char *w = z + k + 1; int mi = 0, ss = 0; uint32_t k2 = 0; while (es_dig(w[k2]) && k2 < 3) { mi = mi * 10 + (w[k2] - '0'); ++k2; }
            const char *r = w + k2; if (*r == ':') { ++r; uint32_t k3 = 0; while (es_dig(r[k3]) && k3 < 3) { ss = ss * 10 + (r[k3] - '0'); ++k3; } r += k3; if (!k3) k2 = 0; }
            if (k2 == 2 && !*r && a < 24 && mi < 60 && ss < 60 && !neg) { *v = f64_div(F(a * 3600 + mi * 60 + ss), F(86400)); if (nf) *nf = NF_HORA; return 1; }
        }
    }
    /* numero: miles con '.' y decimales con ',' (castellano); con un solo '.' y sin ',' tambien vale como decimal si no son 3 cifras detras */
    int comas = 0, puntos = 0, digs = 0; const char *ult_p = 0;
    for (const char *z = q; *z; ++z) { if (*z == ',') ++comas; else if (*z == '.') { ++puntos; ult_p = z; } else if (es_dig(*z)) ++digs; else if (*z == 'e' || *z == 'E') { if (!(z[1] == '-' || z[1] == '+' || es_dig(z[1]))) return 0; } else if (*z == '-' || *z == '+') { if (z == q || (baja(z[-1]) != 'e')) return 0; } else return 0; }
    if (!digs || comas > 1) return 0;
    int dec_n = 0;
    for (const char *z = q; *z; ++z) {
        if (*z == '.') {
            if (comas || puntos > 1) { /* miles: grupos de 3 */ const char *w = z + 1; int k = 0; while (es_dig(w[k])) ++k; if (k != 3) return 0; continue; }
            /* un solo punto sin coma: decimal si no parece de miles */
            int k = 0; while (es_dig(z[1 + k])) ++k;
            if (k == 3 && z != q) continue;                       /* "1.234" = mil doscientos */
            if (n + 1 < sizeof b) b[n++] = '.';
            dec_n = k;
            continue;
        }
        if (*z == ',') { if (n + 1 < sizeof b) b[n++] = '.'; int k = 0; while (es_dig(z[1 + k])) ++k; dec_n = k; continue; }
        if (n + 1 < sizeof b) b[n++] = *z;
    }
    b[n] = 0; (void)ult_p;
    f64 x; if (!parse_punto(b, &x) || !f64_is_finite(x)) return 0;
    if (neg) x = f64_neg(x);
    if (pct) { x = f64_div(x, F(100)); if (nf) *nf = NF_PORCENTAJE; }
    else if (euro) { if (nf) *nf = NF_MONEDA; }
    else if (puntos && comas) { if (nf) *nf = NF_NUMERO; }
    if (dec) *dec = dec_n > 15 ? 15 : dec_n;
    *v = x; return 1;
}
void hj_num_txt(f64 v, int decimales, int miles, char *out, uint32_t cap)
{
    char b[F64_STR_MAX]; uint32_t p = 0; if (!cap) return; out[0] = 0;
    if (!f64_is_finite(v)) { put(out, cap, &p, "#\302\241NUM!"); return; }
    if (decimales < 0) decimales = 0;
    if (decimales > 15) decimales = 15;
    f64 a = f64_abs(v);
    if (!f64_lt(a, F(1000000000000000000LL))) { f64_to_exponential(v, decimales, b); for (char *z = b; *z; ++z) { if (*z == '.') *z = ','; if (*z == 'e') *z = 'E'; } put(out, cap, &p, b); return; }
    f64_to_fixed(a, decimales, b);
    int es_cero = 1; for (char *z = b; *z; ++z) if (*z >= '1' && *z <= '9') es_cero = 0;
    if (f64_lt(v, 0) && !es_cero) putc_(out, cap, &p, '-');
    uint32_t ent = 0; while (b[ent] && b[ent] != '.') ++ent;
    for (uint32_t i = 0; i < ent; ++i) { putc_(out, cap, &p, b[i]); if (miles && i + 1 < ent && (ent - 1 - i) % 3 == 0) putc_(out, cap, &p, '.'); }
    if (b[ent] == '.') { putc_(out, cap, &p, ','); put(out, cap, &p, b + ent + 1); }
}
static void num_general(f64 v, char *out, uint32_t cap)
{
    char b[F64_STR_MAX]; uint32_t p = 0; out[0] = 0;
    if (!f64_is_finite(v)) { put(out, cap, &p, "#\302\241NUM!"); return; }
    f64 a = f64_abs(v);
    if (f64_es_cero(v)) { put(out, cap, &p, "0"); return; }
    if (f64_is_int(v) && f64_lt(a, F(100000000000LL))) { putn(out, cap, &p, f64_to_i64(v)); return; }
    if (!f64_lt(a, F(100000000000LL)) || f64_lt(a, 0x3E112E0BE826D695ULL /* 1e-9 */)) {
        f64_to_exponential(v, 5, b);                             /* 1,23457E+11 */
        char *e = b; while (*e && *e != 'e') ++e;
        char m[64]; uint32_t k = (uint32_t)(e - b); if (k > 60) k = 60; for (uint32_t i = 0; i < k; ++i) m[i] = b[i]; m[k] = 0;
        if (hj_slen(m) && hj_slen(m) < 60) { uint32_t L = hj_slen(m); int hay_p = 0; for (uint32_t i = 0; i < L; ++i) if (m[i] == '.') hay_p = 1; if (hay_p) { while (L && m[L - 1] == '0') m[--L] = 0; if (L && m[L - 1] == '.') m[--L] = 0; } }
        for (char *z = m; *z; ++z) if (*z == '.') *z = ',';
        put(out, cap, &p, m); put(out, cap, &p, "E"); if (*e) { ++e; if (*e != '-') putc_(out, cap, &p, '+'); else { putc_(out, cap, &p, '-'); ++e; } if (*e == '+') ++e; if (hj_slen(e) < 2) putc_(out, cap, &p, '0'); put(out, cap, &p, e); }
        return;
    }
    /* hasta 10 cifras significativas, sin ceros de sobra */
    f64_to_precision(v, 10, b);
    uint32_t L = hj_slen(b); int hay_p = 0; for (uint32_t i = 0; i < L; ++i) if (b[i] == '.') hay_p = 1;
    if (hay_p) { while (L && b[L - 1] == '0') b[--L] = 0; if (L && b[L - 1] == '.') b[--L] = 0; }
    for (char *z = b; *z; ++z) if (*z == '.') *z = ',';
    put(out, cap, &p, b);
}

/* ---- formato de numero: codigos de Excel ("0.00", "#,##0", "0%", "dd/mm/yyyy", "#,##0.00 \342\202\254") a un tipo de SOMA */
static void codigo_a_tipo(const char *cod, uint8_t *nf, uint8_t *dec, uint8_t *miles)
{
    int fecha = 0, hora = 0, pct = 0, cero = 0, almoh = 0, d = 0, coma = 0, tras_punto = 0, euro = 0, exp = 0, comillas = 0, txt = 0;
    for (const char *s = cod; *s; ++s) {
        char c = *s;
        if (c == '"') { comillas = !comillas; continue; }
        if (comillas) continue;
        if (c == '\\') { if (s[1]) ++s; continue; }
        if (c == '[') { while (*s && *s != ']') { if ((uint8_t)*s == 0xE2) euro = 1; ++s; } if (!*s) break; continue; }
        if ((uint8_t)c == 0xE2 && (uint8_t)s[1] == 0x82 && (uint8_t)s[2] == 0xAC) { euro = 1; s += 2; continue; }
        if (c == ';') break;                                     /* solo la seccion de los positivos */
        char b = baja(c);
        if (b == 'y' || b == 'd') fecha = 1;
        else if (b == 'm') { if (hora) hora = 1; else fecha = fecha ? 1 : 2; }   /* m: meses o minutos */
        else if (b == 'h' || b == 's') hora = 1;
        else if (c == '%') pct = 1;
        else if (c == '0') { cero = 1; if (tras_punto) ++d; }
        else if (c == '#' || c == '?') { almoh = 1; if (tras_punto) ++d; }
        else if (c == '.') tras_punto = 1;
        else if (c == ',') { if (!tras_punto) coma = 1; }
        else if (b == 'e' && (s[1] == '+' || s[1] == '-')) exp = 1;
        else if (c == '@') txt = 1;
    }
    *miles = (uint8_t)coma; *dec = (uint8_t)(d > 15 ? 15 : d);
    if (txt && !cero && !almoh) *nf = NF_TEXTO;
    else if (hora && fecha == 1) *nf = NF_FECHAHORA;
    else if (hora || (fecha == 2 && !cero)) *nf = hora || fecha == 2 ? (fecha == 1 ? NF_FECHAHORA : NF_HORA) : NF_FECHA;
    else if (fecha) *nf = NF_FECHA;
    else if (pct) *nf = NF_PORCENTAJE;
    else if (exp) *nf = NF_CIENTIFICA;
    else if (euro) *nf = NF_MONEDA;
    else if (cero || almoh) *nf = NF_NUMERO;
    else *nf = NF_GENERAL;
}
static void fecha_con_codigo(f64 v, const char *cod, char *out, uint32_t cap)
{
    int y, m, d, hh, mi, ss; hj_serie_fecha(v, &y, &m, &d, &hh, &mi, &ss);
    static const char *const MESES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    static const char *const DIAS[7] = { "domingo", "lunes", "martes", "mi\303\251rcoles", "jueves", "viernes", "s\303\241bado" };
    uint32_t p = 0; out[0] = 0; int hay_h = 0, ampm = 0;
    for (const char *s = cod; *s; ++s) { char b = baja(*s); if (b == 'h') hay_h = 1; if ((b == 'a' && (s[1] == 'm' || s[1] == 'M') && s[2] == '/') ) ampm = 1; }
    int ult_h = 0;
    for (const char *s = cod; *s && p + 1 < cap; ) {
        char b = baja(*s); int k = 0; while (baja(s[k]) == b && k < 6) ++k;
        if (*s == '"') { ++s; while (*s && *s != '"') putc_(out, cap, &p, *s++); if (*s) ++s; continue; }
        if (*s == '\\') { if (s[1]) putc_(out, cap, &p, s[1]); s += s[1] ? 2 : 1; continue; }
        if (*s == '[') { while (*s && *s != ']') ++s; if (*s) ++s; continue; }
        if (*s == ';') break;
        if (b == 'y') { if (k <= 2) { if (y % 100 < 10) putc_(out, cap, &p, '0'); putn(out, cap, &p, y % 100); } else putn(out, cap, &p, y); s += k; continue; }
        if (b == 'd') { if (k == 1) putn(out, cap, &p, d); else if (k == 2) { if (d < 10) putc_(out, cap, &p, '0'); putn(out, cap, &p, d); } else { int dw = (int)((dias_civil(y, m, d) % 7 + 11) % 7); put(out, cap, &p, k == 3 ? "" : DIAS[dw]); if (k == 3) { char t[4] = { DIAS[dw][0], DIAS[dw][1], DIAS[dw][2], 0 }; put(out, cap, &p, t); } } s += k; continue; }
        if (b == 'm') {
            int es_min = ult_h || (s[k] == ':' ) || (baja(s[k]) == 's');
            { const char *w = s + k; while (*w == ':' ) ++w; if (baja(*w) == 's') es_min = 1; }
            if (es_min && hay_h) { if (mi < 10 && k >= 2) putc_(out, cap, &p, '0'); putn(out, cap, &p, mi); }
            else if (k == 1) putn(out, cap, &p, m); else if (k == 2) { if (m < 10) putc_(out, cap, &p, '0'); putn(out, cap, &p, m); }
            else if (k == 3) { char t[4] = { MESES[m - 1][0], MESES[m - 1][1], MESES[m - 1][2], 0 }; put(out, cap, &p, t); } else put(out, cap, &p, MESES[m - 1]);
            s += k; ult_h = 0; continue;
        }
        if (b == 'h') { int h = ampm ? (hh % 12 ? hh % 12 : 12) : hh; if (k >= 2 && h < 10) putc_(out, cap, &p, '0'); putn(out, cap, &p, h); s += k; ult_h = 1; continue; }
        if (b == 's') { if (k >= 2 && ss < 10) putc_(out, cap, &p, '0'); putn(out, cap, &p, ss); s += k; continue; }
        if (b == 'a' && (s[1] == 'm' || s[1] == 'M') && s[2] == '/') { put(out, cap, &p, hh < 12 ? "a. m." : "p. m."); s += 5; continue; }
        putc_(out, cap, &p, *s); ++s;
    }
}
/* numero con un codigo de Excel: secciones (positivo;negativo;cero), textos entre comillas o sueltos delante y detras, [Red], %, \x */
static void numero_con_codigo(f64 v, const char *cod, char *out, uint32_t cap, uint32_t *color)
{
    const char *sec[3] = { cod, 0, 0 }; int ns = 1, q = 0;
    for (const char *s = cod; *s; ++s) { if (*s == '"') q = !q; else if (!q && *s == '\\' && s[1]) ++s; else if (!q && *s == ';' && ns < 3) sec[ns++] = s + 1; }
    const char *s = sec[0]; int quita_signo = 0;
    if (f64_lt(v, 0) && ns >= 2) { s = sec[1]; quita_signo = 1; }
    else if (f64_es_cero(v) && ns >= 3) s = sec[2];
    char pre[64], suf[64]; uint32_t np = 0, nsf = 0; int dec = 0, miles = 0, tras_punto = 0, vistos = 0, pct = 0; q = 0;
    pre[0] = suf[0] = 0;
    for (; *s && (q || *s != ';'); ++s) {
        char c = *s; char lit[8]; uint32_t nl = 0;
        if (c == '"') { q = !q; continue; }
        if (q) lit[nl++] = c;
        else if (c == '\\' && s[1]) { lit[nl++] = *++s; }
        else if (c == '[') { const char *e2 = s; while (*e2 && *e2 != ']') ++e2;
            if (s[1] == '$') { const char *z = s + 2; while (z < e2 && *z != '-' && nl < 7) lit[nl++] = *z++; }
            else if (color && (baja(s[1]) == 'r' || (baja(s[1]) == 'c' && baja(s[2]) == 'o' && s[7] == '3'))) *color = 0x01EF4444;
            s = *e2 ? e2 : e2 - 1; }
        else if (c == '_' && s[1]) { lit[nl++] = ' '; ++s; }
        else if (c == '*' && s[1]) { ++s; }
        else if (c == '0' || c == '#' || c == '?') { vistos = 1; if (tras_punto) ++dec; continue; }
        else if (c == '.' ) { tras_punto = 1; vistos = 1; continue; }
        else if (c == ',') { if (vistos && !tras_punto) miles = 1; continue; }
        else if (c == '%') { pct = 1; lit[nl++] = '%'; }
        else lit[nl++] = c;
        for (uint32_t k = 0; k < nl; ++k) { if (!vistos) { if (np + 1 < sizeof pre) pre[np++] = lit[k]; } else if (nsf + 1 < sizeof suf) suf[nsf++] = lit[k]; }
        pre[np] = 0; suf[nsf] = 0;
    }
    if (pct) v = f64_mul(v, F(100));
    if (quita_signo) v = f64_abs(v);
    char num[96]; if (vistos) hj_num_txt(v, dec > 15 ? 15 : dec, miles, num, sizeof num); else num[0] = 0;
    uint32_t p = 0; out[0] = 0; put(out, cap, &p, pre); put(out, cap, &p, num); put(out, cap, &p, suf);
}
void hj_tipo_numero(const struct hj_estilo *e, uint8_t *nf, uint8_t *dec, uint8_t *miles)
{
    *nf = e->nf; *dec = e->decimales; *miles = e->miles;
    if (e->nf == NF_CODIGO) codigo_a_tipo(e->codigo, nf, dec, miles);
}
const char *hj_nombre_error(int e, int ingles)
{
    static const char *const ES[HJE_N] = { "", "#\302\241NULO!", "#\302\241DIV/0!", "#\302\241VALOR!", "#\302\241REF!", "#\302\277NOMBRE?", "#\302\241NUM!", "#N/A", "#\302\241CIRC!" };
    static const char *const EN[HJE_N] = { "", "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A", "#REF!" };
    if (e <= 0 || e >= HJE_N) e = HJE_VALOR;
    return ingles ? EN[e] : ES[e];
}
int hj_error_de_texto(const char *s)
{
    for (int e = 1; e < HJE_N; ++e) { const char *a = hj_nombre_error(e, 1), *b = hj_nombre_error(e, 0); uint32_t la = hj_slen(a), lb = hj_slen(b), i;
        for (i = 0; i < la && baja(s[i]) == baja(a[i]); ++i) {}
        if (i == la) return e;
        for (i = 0; i < lb && baja(s[i]) == baja(b[i]); ++i) {}
        if (i == lb) return e; }
    return 0;
}
void hj_formatea(const struct hj_libro *l, uint16_t est, const struct hj_valor *v, char *out, uint32_t cap, uint32_t *color)
{
    const struct hj_estilo *e = hj_estilo_de(l, est); uint32_t p = 0; if (!cap) return; out[0] = 0;
    if (color) *color = 0;
    switch (v->t) {
    case HJ_VACIA: return;
    case HJ_TXT: put(out, cap, &p, v->s); return;
    case HJ_BOOL: put(out, cap, &p, f64_es_cero(v->n) ? "FALSO" : "VERDADERO"); return;
    case HJ_ERR: put(out, cap, &p, hj_nombre_error(v->err, 0)); return;
    default: break;
    }
    f64 x = v->n; uint8_t nf = e->nf, dec = e->decimales, miles = e->miles;
    if (nf == NF_CODIGO) {
        codigo_a_tipo(e->codigo, &nf, &dec, &miles);
        if (nf == NF_NUMERO || nf == NF_MONEDA || nf == NF_PORCENTAJE) { numero_con_codigo(x, e->codigo, out, cap, color); return; }
    }
    switch (nf) {
    case NF_GENERAL: case NF_TEXTO: num_general(x, out, cap); return;
    case NF_NUMERO: hj_num_txt(x, dec, miles, out, cap); if (color && e->nf == NF_CODIGO && f64_lt(x, 0)) { const char *s = e->codigo; while (*s && *s != ';') ++s; if (*s && (s[1] == '[' && (baja(s[2]) == 'r' || baja(s[2]) == 'c'))) *color = 0x01EF4444; } return;
    case NF_MONEDA: hj_num_txt(x, dec, 1, out, cap); p = hj_slen(out); put(out, cap, &p, " \342\202\254"); return;
    case NF_PORCENTAJE: hj_num_txt(f64_mul(x, F(100)), dec, miles, out, cap); p = hj_slen(out); put(out, cap, &p, "%"); return;
    case NF_CIENTIFICA: { char b[F64_STR_MAX]; f64_to_exponential(x, dec, b); for (char *z = b; *z; ++z) { if (*z == '.') *z = ','; if (*z == 'e') *z = 'E'; } put(out, cap, &p, b); return; }
    case NF_FECHA: case NF_HORA: case NF_FECHAHORA: {
        if (f64_lt(x, 0) || !f64_lt(x, F(2958466))) { put(out, cap, &p, "########"); return; }
        const char *cod = e->nf == NF_CODIGO && e->codigo[0] ? e->codigo : nf == NF_FECHA ? "dd/mm/yyyy" : nf == NF_HORA ? "hh:mm" : "dd/mm/yyyy hh:mm";
        fecha_con_codigo(x, cod, out, cap); return;
    }
    default: num_general(x, out, cap); return;
    }
}

/* ================================================================ edicion y vista */
void hj_valor_de(const struct hj_libro *l, int h, uint32_t f, uint32_t c, struct hj_valor *v)
{
    const struct hj_celda *x = hj_celda_c(l, h, f, c);
    v->t = HJ_VACIA; v->err = 0; v->n = 0; v->s = "";
    if (!x) return;
    v->t = x->t; v->err = x->err; v->n = x->n; v->s = x->txt ? x->txt : "";
    if (x->es_form && x->t == HJ_VACIA) { v->t = HJ_NUM; v->n = 0; }
}
void hj_texto_ver(const struct hj_libro *l, int h, uint32_t f, uint32_t c, char *out, uint32_t cap)
{
    struct hj_valor v; hj_valor_de(l, h, f, c, &v);
    const struct hj_celda *x = hj_celda_c(l, h, f, c);
    hj_formatea(l, x ? x->estilo : 0, &v, out, cap, 0);
}
void hj_texto_edicion(const struct hj_libro *l, int h, uint32_t f, uint32_t c, char *out, uint32_t cap)
{
    const struct hj_celda *x = hj_celda_c(l, h, f, c); uint32_t p = 0; if (!cap) return; out[0] = 0;
    if (!x) return;
    if (x->es_form) { putc_(out, cap, &p, '='); if (hj_form_a_ui(l, h, x->form, out + 1, cap - 1)) put(out, cap, &p, x->form); return; }
    const struct hj_estilo *e = hj_estilo_de(l, x->estilo);
    switch (x->t) {
    case HJ_TXT: { struct hj_valor v = { HJ_TXT, 0, 0, x->txt }; f64 t; int nf; if (hj_lee_numero(x->txt, &t, &nf, 0) || x->txt[0] == '=' ) putc_(out, cap, &p, '\''); hj_formatea(l, 0, &v, out + p, cap - p, 0); return; }
    case HJ_BOOL: put(out, cap, &p, f64_es_cero(x->n) ? "FALSO" : "VERDADERO"); return;
    case HJ_ERR: put(out, cap, &p, hj_nombre_error(x->err, 0)); return;
    case HJ_NUM: {
        uint8_t nf = e->nf, dec = e->decimales, mi = e->miles; if (nf == NF_CODIGO) codigo_a_tipo(e->codigo, &nf, &dec, &mi);
        if (nf == NF_FECHA || nf == NF_FECHAHORA || nf == NF_HORA) { fecha_con_codigo(x->n, nf == NF_FECHA ? "d/m/yyyy" : nf == NF_HORA ? "h:mm:ss" : "d/m/yyyy h:mm:ss", out, cap); return; }
        if (nf == NF_PORCENTAJE) { num_general(f64_mul(x->n, F(100)), out, cap); p = hj_slen(out); put(out, cap, &p, "%"); return; }
        num_general(x->n, out, cap); return;
    }
    default: return;
    }
}
int hj_escribe(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *txt, int *err)
{
    if (err) *err = 0;
    if (h < 0 || h >= l->nh || f >= HJ_FILAS || c >= HJ_COLS) return -2;
    while (*txt == ' ' && txt[1] == ' ') ++txt;
    if (!txt[0]) { hj_borra_celda(l, h, f, c); return 0; }
    if (txt[0] == '\'') return hj_pon_txt(l, h, f, c, txt + 1) ? -2 : 0;
    if (txt[0] == '=' && txt[1]) {
        char *canon = (char *)hj_alloc(HJ_FORM_MAX + 64); if (!canon) return -2;
        int e = 0; int r = hj_form_a_canon(l, h, txt + 1, canon, HJ_FORM_MAX + 64, &e);
        if (r) { hj_free(canon); if (err) *err = e; return -1; }
        uint8_t *code; uint32_t nc;
        if (compila(l, h, canon, &code, &nc)) { hj_free(canon); if (err) *err = 0; return -1; }
        hj_free(code);
        r = hj_pon_form(l, h, f, c, canon); hj_free(canon);
        return r ? -2 : 0;
    }
    f64 v; int nf, dec;
    if (hj_lee_numero(txt, &v, &nf, &dec)) {
        if (hj_pon_num(l, h, f, c, v)) return -2;
        if (nf != NF_GENERAL) {                                  /* el formato que se ha escrito pasa al estilo (si no tenia uno) */
            struct hj_celda *x = hj_celda(l, h, f, c); struct hj_estilo e = *hj_estilo_de(l, x->estilo);
            if (e.nf == NF_GENERAL || (nf >= NF_FECHA && nf <= NF_FECHAHORA && !(e.nf >= NF_FECHA && e.nf <= NF_FECHAHORA))) {
                e.nf = (uint8_t)nf; e.decimales = (uint8_t)(nf == NF_MONEDA ? 2 : dec); e.miles = nf == NF_NUMERO || nf == NF_MONEDA; e.codigo[0] = 0;
                x->estilo = (uint16_t)hj_estilo_id(l, &e);
            }
        }
        return 0;
    }
    { char p[16]; uint32_t k = 0; while (txt[k] && k < 15) { p[k] = txt[k]; ++k; } p[k] = 0;
      if (hj_ieq_pl(p, "verdadero") || hj_ieq_pl(p, "true")) return hj_pon_bool(l, h, f, c, 1) ? -2 : 0;
      if (hj_ieq_pl(p, "falso") || hj_ieq_pl(p, "false")) return hj_pon_bool(l, h, f, c, 0) ? -2 : 0; }
    { int e = hj_error_de_texto(txt); if (e && hj_slen(txt) == hj_slen(hj_nombre_error(e, 0))) return hj_pon_err(l, h, f, c, e) ? -2 : 0; }
    return hj_pon_txt(l, h, f, c, txt) ? -2 : 0;
}

#include "hoja_formula.inc"
#include "hoja_ops.inc"
