/* SOMA_358_HOJA: operaciones sobre rangos, CSV y formato propio. Incluido desde hoja_core.c (despues de hoja_formula.inc). */

/* ================================================================ filas y columnas */
static void hoja_compacta(struct hj_libro *l, int h)
{
    struct hj_hoja *s = &l->h[h]; int32_t w = 0;
    uint32_t mf = 0, mc = 0;
    for (int32_t i = 0; i < s->nc; ++i) {
        struct hj_celda *x = &s->c[i];
        if (x->fila == 0xFFFFFFFFu || (x->t == HJ_VACIA && !x->es_form && !x->estilo)) { celda_libera(x); l->ncel--; continue; }
        if (w != i) s->c[w] = *x;
        if (s->c[w].fila + 1 > mf) mf = s->c[w].fila + 1;
        if ((uint32_t)s->c[w].col + 1 > mc) mc = (uint32_t)s->c[w].col + 1;
        ++w;
    }
    s->nc = w; s->max_fila = mf; s->max_col = mc;
    uint32_t nh = 256; while (nh < (uint32_t)w * 2) nh *= 2;
    rehash(s, nh);
    if (s->nhash != nh) rehash(s, s->nhash);                      /* sin memoria: al menos rehacer las cadenas */
}
static int mueve(struct hj_libro *l, int h, int filas, uint32_t en, uint32_t n, int borrar)
{
    if (h < 0 || h >= l->nh || !n) return -1;
    struct hj_hoja *s = &l->h[h]; uint32_t lim = filas ? HJ_FILAS : HJ_COLS;
    if (en >= lim) return -1;
    if (n > lim - en) n = lim - en;
    for (int32_t i = 0; i < s->nc; ++i) {
        struct hj_celda *x = &s->c[i]; uint32_t v = filas ? x->fila : x->col;
        if (v < en) continue;
        if (borrar) { if (v < en + n) { x->fila = 0xFFFFFFFFu; continue; } v -= n; }
        else { if (v + n >= lim) { x->fila = 0xFFFFFFFFu; continue; } v += n; }
        if (filas) x->fila = v; else x->col = (uint16_t)v;
    }
    /* anchos/altos */
    struct hj_tam *t = filas ? s->altos : s->anchos; uint32_t nt = filas ? s->naltos : s->nanchos, w = 0;
    for (uint32_t i = 0; i < nt; ++i) {
        uint32_t v = t[i].i;
        if (v >= en) { if (borrar) { if (v < en + n) continue; v -= n; } else { if (v + n >= lim) continue; v += n; } }
        t[w].i = v; t[w].px = t[i].px; ++w;
    }
    if (filas) s->naltos = w; else s->nanchos = w;
    if (filas && s->oculta) { hj_free(s->oculta); s->oculta = 0; s->noculta = 0; }
    /* graficos y autofiltro */
    for (uint32_t g = 0; g < s->ngraf; ++g) {
        uint32_t *a = filas ? &s->graf[g].r1 : &s->graf[g].c1, *b = filas ? &s->graf[g].r2 : &s->graf[g].c2, *p = filas ? &s->graf[g].fila : &s->graf[g].col;
        uint32_t *q[3] = { a, b, p };
        for (int k = 0; k < 3; ++k) { if (*q[k] >= en) { if (borrar) *q[k] = *q[k] >= en + n ? *q[k] - n : en; else if (*q[k] + n < lim) *q[k] += n; } }
    }
    if (s->hay_filtro) { uint32_t *a = filas ? &s->filtro_r1 : &s->filtro_c1, *b = filas ? &s->filtro_r2 : &s->filtro_c2;
        if (*a >= en) { if (borrar) *a = *a >= en + n ? *a - n : en; else *a += n; }
        if (*b >= en) { if (borrar) *b = *b >= en + n ? *b - n : (en ? en - 1 : 0); else if (*b + n < lim) *b += n; } }
    struct mov m = { 0, h, filas, en, n, borrar };
    form_mapea_todo(l, mapa_mov, &m, 0);
    hoja_compacta(l, h);
    l->version++;
    return 0;
}
int hj_inserta_filas(struct hj_libro *l, int h, uint32_t f, uint32_t n) { return mueve(l, h, 1, f, n, 0); }
int hj_borra_filas(struct hj_libro *l, int h, uint32_t f, uint32_t n) { return mueve(l, h, 1, f, n, 1); }
int hj_inserta_cols(struct hj_libro *l, int h, uint32_t c, uint32_t n) { return mueve(l, h, 0, c, n, 0); }
int hj_borra_cols(struct hj_libro *l, int h, uint32_t c, uint32_t n) { return mueve(l, h, 0, c, n, 1); }

/* ================================================================ copiar y rellenar */
struct copia_c { uint32_t df, dc; uint8_t t, err, es_form; uint16_t estilo; f64 n; char *txt; char *form; };
static int toma(const struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, struct copia_c **out, uint32_t *nout)
{
    const struct hj_hoja *s = &l->h[h]; uint32_t n = 0;
    for (int32_t i = 0; i < s->nc; ++i) { const struct hj_celda *x = &s->c[i]; if (x->fila >= f1 && x->fila <= f2 && x->col >= c1 && x->col <= c2) ++n; }
    *out = 0; *nout = 0; if (!n) return 0;
    struct copia_c *v = (struct copia_c *)hj_alloc(n * (uint32_t)sizeof *v); if (!v) return -1;
    uint32_t k = 0;
    for (int32_t i = 0; i < s->nc && k < n; ++i) {
        const struct hj_celda *x = &s->c[i]; if (!(x->fila >= f1 && x->fila <= f2 && x->col >= c1 && x->col <= c2)) continue;
        struct copia_c *y = &v[k++]; zero(y, (uint32_t)sizeof *y);
        y->df = x->fila - f1; y->dc = x->col - c1; y->t = x->t; y->err = x->err; y->es_form = x->es_form; y->estilo = x->estilo; y->n = x->n;
        y->txt = x->txt ? hj_strdup(x->txt) : 0; y->form = x->form ? hj_strdup(x->form) : 0;
    }
    *out = v; *nout = k; return 0;
}
static void suelta(struct copia_c *v, uint32_t n) { for (uint32_t i = 0; i < n; ++i) { if (v[i].txt) hj_free(v[i].txt); if (v[i].form) hj_free(v[i].form); } if (v) hj_free(v); }
static void deja(struct hj_libro *l, int h, int ho, uint32_t f, uint32_t c, const struct copia_c *y, int32_t df, int32_t dc, int solo_valores)
{
    if (f >= HJ_FILAS || c >= HJ_COLS) return;
    if (y->es_form && !solo_valores && y->form) {
        char *b = (char *)hj_alloc(HJ_FORM_MAX + 256);
        if (b) {
            /* las referencias a "esta hoja" desde otra hoja: el desplazamiento se hace con el libro de origen */
            if (!hj_form_desplaza(l, ho, y->form, df, dc, b, HJ_FORM_MAX + 256)) (void)hj_pon_form(l, h, f, c, b);
            hj_free(b);
        }
    } else switch (y->t) {
        case HJ_NUM: (void)hj_pon_num(l, h, f, c, y->n); break;
        case HJ_BOOL: (void)hj_pon_bool(l, h, f, c, !f64_es_cero(y->n)); break;
        case HJ_ERR: (void)hj_pon_err(l, h, f, c, y->err); break;
        case HJ_TXT: (void)hj_pon_txt(l, h, f, c, y->txt ? y->txt : ""); break;
        default: hj_borra_celda(l, h, f, c); break;
    }
    (void)hj_pon_estilo(l, h, f, c, y->estilo);
}
int hj_copia(struct hj_libro *l, int hd, uint32_t fd, uint32_t cd, int ho, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int solo_valores)
{
    if (hd < 0 || hd >= l->nh || ho < 0 || ho >= l->nh || f2 < f1 || c2 < c1) return -1;
    struct copia_c *v; uint32_t n;
    if (toma(l, ho, f1, c1, f2, c2, &v, &n)) return -2;
    /* lo que habia en el destino se borra (contenido y estilo) */
    uint32_t af = f2 - f1, ac = c2 - c1;
    hj_borra_rango(l, hd, fd, cd, fd + af < HJ_FILAS ? fd + af : HJ_FILAS - 1, cd + ac < HJ_COLS ? cd + ac : HJ_COLS - 1, 1);
    for (uint32_t i = 0; i < n; ++i) deja(l, hd, ho, fd + v[i].df, cd + v[i].dc, &v[i], (int32_t)fd - (int32_t)f1, (int32_t)cd - (int32_t)c1, solo_valores);
    suelta(v, n);
    return 0;
}
/* SOMA_359_HOJA_VENTANA: deshacer */
struct hj_trozo { int h; uint32_t f1, c1, f2, c2, n, bytes; struct copia_c *v; };
struct hj_trozo *hj_toma(const struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    if (h < 0 || h >= l->nh || f2 < f1 || c2 < c1) return 0;
    struct hj_trozo *t = (struct hj_trozo *)hj_alloc((uint32_t)sizeof *t); if (!t) return 0;
    zero(t, (uint32_t)sizeof *t); t->h = h; t->f1 = f1; t->c1 = c1; t->f2 = f2; t->c2 = c2;
    if (toma(l, h, f1, c1, f2, c2, &t->v, &t->n)) { hj_free(t); return 0; }
    t->bytes = (uint32_t)sizeof *t + t->n * (uint32_t)sizeof(struct copia_c);
    for (uint32_t i = 0; i < t->n; ++i) t->bytes += hj_slen(t->v[i].txt) + hj_slen(t->v[i].form) + 2;
    return t;
}
void hj_deja(struct hj_libro *l, const struct hj_trozo *t)
{
    if (!t || t->h >= l->nh) return;
    hj_borra_rango(l, t->h, t->f1, t->c1, t->f2, t->c2, 1);
    for (uint32_t i = 0; i < t->n; ++i) {
        const struct copia_c *y = &t->v[i];
        if (y->es_form == 2 && y->form) {                       /* formula de un archivo: se repone con su valor */
            struct copia_c z = *y; z.es_form = 0; deja(l, t->h, t->h, t->f1 + y->df, t->c1 + y->dc, &z, 0, 0, 0);
            (void)hj_pon_form_cache(l, t->h, t->f1 + y->df, t->c1 + y->dc, y->form);
        } else deja(l, t->h, t->h, t->f1 + y->df, t->c1 + y->dc, y, 0, 0, 0);
    }
    l->version++;
}
void hj_suelta(struct hj_trozo *t) { if (!t) return; suelta(t->v, t->n); hj_free(t); }
uint32_t hj_trozo_bytes(const struct hj_trozo *t) { return t ? t->bytes : 0; }
void hj_trozo_rect(const struct hj_trozo *t, int *h, uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2) { *h = t ? t->h : 0; *f1 = t ? t->f1 : 0; *c1 = t ? t->c1 : 0; *f2 = t ? t->f2 : 0; *c2 = t ? t->c2 : 0; }

static const char *const DIAS_S[7] = { "lunes", "martes", "mi\303\251rcoles", "jueves", "viernes", "s\303\241bado", "domingo" };
static const char *const MESES_S[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
static int en_lista(const char *s, const char *const *L, int n) { for (int i = 0; i < n; ++i) if (hj_ieq_pl(s, L[i])) return i; return -1; }
static void caso_como(const char *modelo, char *s)                /* "Lunes" -> "Martes"; "LUNES" -> "MARTES" */
{
    int todo_may = 1, primera = modelo[0] >= 'A' && modelo[0] <= 'Z';
    for (const char *z = modelo; *z; ++z) if (*z >= 'a' && *z <= 'z') todo_may = 0;
    if (todo_may) u8_caso(s, 1, 0); else if (primera && s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 32);
}
int hj_rellena(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int der)
{
    if (h < 0 || h >= l->nh || f2 < f1 || c2 < c1) return -1;
    uint32_t nlin = der ? f2 - f1 + 1 : c2 - c1 + 1, largo = der ? c2 - c1 + 1 : f2 - f1 + 1;
    for (uint32_t k = 0; k < nlin; ++k) {
        /* semillas: las primeras celdas no vacias seguidas */
#define POS(i) (der ? f1 + k : f1 + (i)), (der ? c1 + (i) : c1 + k)
        uint32_t ns = 0;
        while (ns < largo) { const struct hj_celda *x = hj_celda_c(l, h, POS(ns)); if (!x || (x->t == HJ_VACIA && !x->es_form)) break; ++ns; }
        if (!ns || ns >= largo) continue;
        const struct hj_celda *a = hj_celda_c(l, h, POS(0));
        int todas_num = 1; for (uint32_t i = 0; i < ns; ++i) { const struct hj_celda *x = hj_celda_c(l, h, POS(i)); if (x->es_form || x->t != HJ_NUM) todas_num = 0; }
        if (todas_num && ns >= 2) {                              /* serie lineal */
            const struct hj_celda *p = hj_celda_c(l, h, POS(ns - 2)), *q = hj_celda_c(l, h, POS(ns - 1));
            f64 paso = f64_sub(q->n, p->n), v = q->n; uint16_t est = q->estilo;
            for (uint32_t i = ns; i < largo; ++i) { v = f64_add(v, paso); (void)hj_pon_num(l, h, POS(i), v); (void)hj_pon_estilo(l, h, POS(i), est); }
            continue;
        }
        if (ns == 1 && a->t == HJ_NUM && !a->es_form) {
            uint8_t nf = hj_estilo_de(l, a->estilo)->nf;
            int fecha = nf == NF_FECHA || nf == NF_FECHAHORA;
            for (uint32_t i = 1; i < largo; ++i) { (void)hj_pon_num(l, h, POS(i), fecha ? f64_add(a->n, F((int64_t)i)) : a->n); (void)hj_pon_estilo(l, h, POS(i), a->estilo); }
            continue;
        }
        if (ns == 1 && a->t == HJ_TXT && !a->es_form && a->txt) {
            int d = en_lista(a->txt, DIAS_S, 7), m = d < 0 ? en_lista(a->txt, MESES_S, 12) : -1;
            const char *t = a->txt; uint32_t L = hj_slen(t), j = L; while (j && es_dig(t[j - 1])) --j;
            char modelo[HJ_NOMBRE * 4]; cpy(modelo, sizeof modelo, t);
            for (uint32_t i = 1; i < largo; ++i) {
                char b[256];
                if (d >= 0) { cpy(b, sizeof b, DIAS_S[(d + i) % 7]); caso_como(modelo, b); }
                else if (m >= 0) { cpy(b, sizeof b, MESES_S[(m + i) % 12]); caso_como(modelo, b); }
                else if (j < L && L - j < 10) { uint64_t v = 0; for (uint32_t z = j; z < L; ++z) v = v * 10 + (uint64_t)(t[z] - '0'); uint32_t p = 0; b[0] = 0; for (uint32_t z = 0; z < j && p + 1 < sizeof b; ++z) b[p++] = t[z]; b[p] = 0; putn(b, sizeof b, &p, (int64_t)(v + i)); }
                else cpy(b, sizeof b, t);
                (void)hj_pon_txt(l, h, POS(i), b); (void)hj_pon_estilo(l, h, POS(i), a->estilo);
            }
            continue;
        }
        /* formulas o un patron: se repite el bloque de semillas, desplazando las formulas */
        for (uint32_t i = ns; i < largo; ++i) {
            uint32_t s0 = i % ns; const struct hj_celda *x = hj_celda_c(l, h, POS(s0)); if (!x) continue;
            struct copia_c y; zero(&y, sizeof y); y.t = x->t; y.err = x->err; y.es_form = x->es_form; y.estilo = x->estilo; y.n = x->n; y.txt = x->txt; y.form = x->form;
            int32_t dd = (int32_t)(i - s0);
            deja(l, h, h, POS(i), &y, der ? 0 : dd, der ? dd : 0, 0);
        }
#undef POS
    }
    l->version++;
    return 0;
}

/* ================================================================ ordenar */
struct fila_o { uint32_t f; struct hj_valor a, b; };
static int cmp_val(const struct hj_valor *a, const struct hj_valor *b, int desc)
{
    if (a->t == HJ_VACIA || b->t == HJ_VACIA) return (a->t == HJ_VACIA) - (b->t == HJ_VACIA);   /* vacias al final siempre */
    struct ev x, y; zero(&x, sizeof x); zero(&y, sizeof y);
    x.t = a->t; x.n = a->n; x.s = a->s; x.err = a->err; y.t = b->t; y.n = b->n; y.s = b->s; y.err = b->err;
    if (x.t == HJ_ERR || y.t == HJ_ERR) return (x.t == HJ_ERR) - (y.t == HJ_ERR);
    int k = compara(&x, &y);
    return desc ? -k : k;
}
int hj_ordena(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, uint32_t por, int desc, int por2, int desc2)
{
    if (h < 0 || h >= l->nh || f2 < f1 || c2 < c1 || por < c1 || por > c2) return -1;
    struct hj_hoja *s = &l->h[h];
    if (f2 >= s->max_fila) f2 = s->max_fila ? s->max_fila - 1 : 0;
    if (f2 < f1) return 0;
    uint32_t n = f2 - f1 + 1;
    if (n > 200000) return -2;
    struct fila_o *v = (struct fila_o *)hj_alloc(n * (uint32_t)sizeof *v), *tmp = (struct fila_o *)hj_alloc(n * (uint32_t)sizeof *v);
    if (!v || !tmp) { if (v) hj_free(v); if (tmp) hj_free(tmp); return -2; }
    for (uint32_t i = 0; i < n; ++i) { v[i].f = f1 + i; hj_valor_de(l, h, f1 + i, por, &v[i].a); if (por2 >= 0) hj_valor_de(l, h, f1 + i, (uint32_t)por2, &v[i].b); else v[i].b.t = HJ_VACIA; }
    /* mezcla estable de abajo arriba */
    for (uint32_t w = 1; w < n; w *= 2) {
        for (uint32_t i = 0; i < n; i += 2 * w) {
            uint32_t a = i, m = i + w < n ? i + w : n, b = m, fin = i + 2 * w < n ? i + 2 * w : n, k = i;
            while (a < m && b < fin) {
                int c = cmp_val(&v[a].a, &v[b].a, desc); if (!c && por2 >= 0) c = cmp_val(&v[a].b, &v[b].b, desc2);
                tmp[k++] = c <= 0 ? v[a++] : v[b++];
            }
            while (a < m) tmp[k++] = v[a++];
            while (b < fin) tmp[k++] = v[b++];
        }
        struct fila_o *t = v; v = tmp; tmp = t;
    }
    /* los valores de texto apuntan a las celdas: se toma el rango entero antes de escribir */
    struct copia_c *cv; uint32_t nc;
    if (toma(l, h, f1, c1, f2, c2, &cv, &nc)) { hj_free(v); hj_free(tmp); return -2; }
    uint32_t *destino = (uint32_t *)hj_alloc(n * 4u); if (!destino) { suelta(cv, nc); hj_free(v); hj_free(tmp); return -2; }
    for (uint32_t i = 0; i < n; ++i) destino[v[i].f - f1] = f1 + i;
    hj_borra_rango(l, h, f1, c1, f2, c2, 1);
    for (uint32_t i = 0; i < nc; ++i) { uint32_t nf = destino[cv[i].df]; deja(l, h, h, nf, c1 + cv[i].dc, &cv[i], (int32_t)nf - (int32_t)(f1 + cv[i].df), 0, 0); }
    hj_free(destino); suelta(cv, nc); hj_free(v); hj_free(tmp);
    l->version++;
    return 0;
}

/* ================================================================ filtrar y buscar */
static void crit_de_txt(struct ectx *e, const char *s, struct crit *c) { struct ev v; zero(&v, sizeof v); v.t = HJ_TXT; v.s = s; crit_prepara(e, &v, c); }
static int cumple_fila(struct hj_libro *l, int h, uint32_t f)
{
    struct hj_hoja *s = &l->h[h]; struct ectx e; zero(&e, sizeof e); e.l = l; e.hoja = h; e.gen = l->gen;
    for (uint32_t k = 0; k < s->nfil; ++k) {
        if (!s->fil[k].crit[0]) continue;
        struct crit c; crit_de_txt(&e, s->fil[k].crit, &c);
        struct hj_valor hv; hj_valor_de(l, h, f, s->fil[k].col, &hv);
        struct ev v; zero(&v, sizeof v); v.t = hv.t; v.n = hv.n; v.s = hv.s; v.err = hv.err;
        if (!crit_cumple(&c, &v)) return 0;
    }
    return 1;
}
int hj_filtra(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, uint32_t col, const char *criterio)
{
    if (h < 0 || h >= l->nh || f2 <= f1) return -1;
    struct hj_hoja *s = &l->h[h];
    if (!s->hay_filtro || s->filtro_r1 != f1 || s->filtro_c1 != c1) s->nfil = 0;
    s->hay_filtro = 1; s->filtro_r1 = f1; s->filtro_c1 = c1; s->filtro_r2 = f2; s->filtro_c2 = c2;
    int k = -1; for (uint32_t i = 0; i < s->nfil; ++i) if (s->fil[i].col == col) k = (int)i;
    if (k < 0 && criterio && criterio[0]) { if (s->nfil >= 8) return -1; k = s->nfil++; s->fil[k].col = col; }
    if (k >= 0) { if (criterio && criterio[0]) cpy(s->fil[k].crit, sizeof s->fil[k].crit, criterio); else { s->fil[k] = s->fil[s->nfil - 1]; s->nfil--; } }
    int vis = 0;
    uint32_t fin = f2 < s->max_fila ? f2 : (s->max_fila ? s->max_fila - 1 : f1);
    for (uint32_t f = f1 + 1; f <= fin; ++f) { int ok = cumple_fila(l, h, f); hj_oculta_fila(s, f, !ok); vis += ok; }
    l->version++;
    return vis;
}
static int contiene_pl(const char *pajar, const char *aguja, int exacta)
{
    if (exacta) return hj_ieq_pl(pajar, aguja);
    char a[128], b[64]; uint32_t na = 0, nb = 0;
    for (const char *s = aguja; *s && nb + 1 < sizeof b; ) b[nb++] = pliega_sig(&s);
    b[nb] = 0; if (!nb) return 0;
    for (const char *s = pajar; *s; ) {
        na = 0; const char *z = s; while (*z && na < nb) { a[na++] = pliega_sig(&z); }
        a[na] = 0; if (na == nb && igual_mem(a, b, nb)) return 1;
        (void)pliega_sig(&s);
    }
    return 0;
}
int hj_busca(const struct hj_libro *l, int h, uint32_t *f, uint32_t *c, const char *que, int en_formulas, int exacta)
{
    if (h < 0 || h >= l->nh || !que || !que[0]) return 0;
    const struct hj_hoja *s = &l->h[h]; uint64_t ini = ((uint64_t)*f << 14) | *c, mejor = ~0ULL, primero = ~0ULL;
    char *b = (char *)hj_alloc(HJ_FORM_MAX + 64); if (!b) return 0;
    for (int32_t i = 0; i < s->nc; ++i) {
        const struct hj_celda *x = &s->c[i]; uint64_t k = ((uint64_t)x->fila << 14) | x->col;
        if (x->t == HJ_VACIA && !x->es_form) continue;
        if (en_formulas && x->es_form) hj_texto_edicion(l, h, x->fila, x->col, b, HJ_FORM_MAX + 64); else hj_texto_ver(l, h, x->fila, x->col, b, HJ_FORM_MAX + 64);
        if (!contiene_pl(b, que, exacta)) continue;
        if (k > ini && k < mejor) mejor = k;
        if (k < primero) primero = k;
    }
    hj_free(b);
    if (mejor == ~0ULL) mejor = primero;                          /* vuelve a empezar por arriba */
    if (mejor == ~0ULL) return 0;
    *f = (uint32_t)(mejor >> 14); *c = (uint32_t)(mejor & 0x3FFF); return 1;
}
int hj_reemplaza(struct hj_libro *l, int h, const char *que, const char *por, int exacta)
{
    if (h < 0 || h >= l->nh || !que || !que[0]) return 0;
    struct hj_hoja *s = &l->h[h]; int n = 0; uint32_t lq = hj_slen(que), lp = hj_slen(por);
    for (int32_t i = 0; i < s->nc; ++i) {
        struct hj_celda *x = &s->c[i]; if (x->es_form || x->t != HJ_TXT || !x->txt) continue;
        if (exacta) { if (hj_ieq_pl(x->txt, que)) { char *d = hj_strdup(por); if (d) { hj_free(x->txt); x->txt = d; ++n; } } continue; }
        uint32_t cap = hj_slen(x->txt) * (lp > lq ? (lp / (lq ? lq : 1) + 2) : 1) + lp + 8; if (cap > HJ_TXT_MAX + 1) cap = HJ_TXT_MAX + 1;
        char *d = (char *)hj_alloc(cap); if (!d) continue;
        uint32_t p = 0; int hubo = 0;
        for (const char *z = x->txt; *z; ) {
            uint32_t k = 0; while (k < lq && z[k] && baja(z[k]) == baja(que[k])) ++k;
            if (k == lq) { for (uint32_t j = 0; j < lp && p + 1 < cap; ++j) d[p++] = por[j]; z += lq; hubo = 1; continue; }
            if (p + 1 < cap) d[p++] = *z;
            ++z;
        }
        d[p] = 0;
        if (hubo) { hj_free(x->txt); x->txt = d; ++n; } else hj_free(d);
    }
    if (n) l->version++;
    return n;
}

/* ================================================================ CSV */
int hj_csv_lee(struct hj_libro *l, int h, const char *t, uint32_t n, char sep)
{
    if (h < 0 || h >= l->nh) return -1;
    uint32_t i = 0;
    if (n >= 3 && (uint8_t)t[0] == 0xEF && (uint8_t)t[1] == 0xBB && (uint8_t)t[2] == 0xBF) i = 3;
    if (!sep) {                                                  /* el que mas sale en la primera linea, fuera de comillas */
        uint32_t pc = 0, co = 0, ta = 0; int q = 0;
        for (uint32_t k = i; k < n && t[k] != '\n'; ++k) { if (t[k] == '"') q = !q; else if (!q) { if (t[k] == ';') ++pc; else if (t[k] == ',') ++co; else if (t[k] == '\t') ++ta; } }
        sep = ta > pc && ta > co ? '\t' : co > pc ? ',' : ';';
    }
    char *campo = (char *)hj_alloc(HJ_TXT_MAX + 8); if (!campo) return -2;
    uint32_t f = 0, c = 0, filas = 0;
    while (i <= n && f < HJ_FILAS) {
        uint32_t k = 0; int entre = 0;
        if (i < n && t[i] == '"') { entre = 1; ++i;
            while (i < n) { if (t[i] == '"') { if (i + 1 < n && t[i + 1] == '"') { if (k < HJ_TXT_MAX) campo[k++] = '"'; i += 2; continue; } ++i; break; } if (k < HJ_TXT_MAX) campo[k++] = t[i]; ++i; }
            while (i < n && t[i] != sep && t[i] != '\n' && t[i] != '\r') ++i;
        } else while (i < n && t[i] != sep && t[i] != '\n' && t[i] != '\r') { if (k < HJ_TXT_MAX) campo[k++] = t[i]; ++i; }
        campo[k] = 0;
        if (k && c < HJ_COLS) {
            if (!entre && sep == ',') { f64 v; if (parse_punto(campo, &v)) { (void)hj_pon_num(l, h, f, c, v); goto sig; } }
            if (campo[0] == '=' || campo[0] == '@') (void)hj_pon_txt(l, h, f, c, campo);   /* nada de formulas desde un CSV */
            else if (entre) { f64 v; int nf, d; if (hj_lee_numero(campo, &v, &nf, &d)) (void)hj_escribe(l, h, f, c, campo, 0); else (void)hj_pon_txt(l, h, f, c, campo); }
            else (void)hj_escribe(l, h, f, c, campo, 0);
        }
sig:
        if (i >= n) { ++filas; break; }
        if (t[i] == sep) { ++c; ++i; continue; }
        if (t[i] == '\r') ++i;
        if (i < n && t[i] == '\n') ++i;
        ++f; ++filas; c = 0;
        if (i >= n) break;
    }
    hj_free(campo);
    l->version++;
    return (int)filas;
}
uint32_t hj_csv_escribe(const struct hj_libro *l, int h, char sep, char *o, uint32_t cap)
{
    if (h < 0 || h >= l->nh || !cap) return 0;
    if (!sep) sep = ';';
    const struct hj_hoja *s = &l->h[h]; uint32_t p = 0; o[0] = 0;
    char *b = (char *)hj_alloc(HJ_TXT_MAX + 64); if (!b) return 0;
    for (uint32_t f = 0; f < s->max_fila; ++f) {
        uint32_t ult = 0; for (uint32_t c = 0; c < s->max_col; ++c) { const struct hj_celda *x = hj_celda_c(l, h, f, c); if (x && (x->t != HJ_VACIA || x->es_form)) ult = c + 1; }
        for (uint32_t c = 0; c < ult; ++c) {
            if (c) { if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = sep; }
            hj_texto_ver(l, h, f, c, b, HJ_TXT_MAX + 64);
            const struct hj_celda *x = hj_celda_c(l, h, f, c);
            int peligro = x && x->t == HJ_TXT && !x->es_form && (b[0] == '=' || b[0] == '+' || b[0] == '@');
            int comillas = peligro; for (const char *z = b; *z; ++z) if (*z == sep || *z == '"' || *z == '\n' || *z == '\r') comillas = 1;
            if (comillas) { if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = '"'; }
            if (peligro) { if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = '\''; }
            for (const char *z = b; *z; ++z) { if (*z == '"' && comillas) { if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = '"'; } if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = *z; }
            if (comillas) { if (p + 1 >= cap) { hj_free(b); return 0; } o[p++] = '"'; }
        }
        if (p + 2 >= cap) { hj_free(b); return 0; }
        o[p++] = '\r'; o[p++] = '\n';
    }
    hj_free(b);
    o[p < cap ? p : cap - 1] = 0;
    return p;
}

/* ================================================================ formato propio "SOMA-HOJA 1" */
struct sb { char *o; uint32_t cap, p; int lleno; };
static void sb_c(struct sb *b, char c) { if (b->p + 1 >= b->cap) { b->lleno = 1; return; } b->o[b->p++] = c; }
static void sb_s(struct sb *b, const char *s) { while (s && *s) sb_c(b, *s++); }
static void sb_esc(struct sb *b, const char *s) { for (; s && *s; ++s) { if (*s == '|') sb_s(b, "\\p"); else if (*s == '\n') sb_s(b, "\\n"); else if (*s == '\r') sb_s(b, "\\r"); else if (*s == '\\') sb_s(b, "\\\\"); else sb_c(b, *s); } }
static void sb_n(struct sb *b, int64_t v) { char t[24]; uint32_t p = 0; t[0] = 0; putn(t, sizeof t, &p, v); sb_s(b, t); }
static void sb_f(struct sb *b, f64 v) { char t[F64_STR_MAX]; f64_to_str(v, t, 10); sb_s(b, t); }
uint32_t hj_serializa_tam(const struct hj_libro *l)
{
    uint64_t t = 64 + (uint64_t)l->ne * 160;
    for (int h = 0; h < l->nh; ++h) {
        const struct hj_hoja *s = &l->h[h]; t += 256 + (uint64_t)s->nanchos * 24 + (uint64_t)s->naltos * 24 + (uint64_t)s->ngraf * 200 + 8 * 100 + (uint64_t)s->max_fila * 0;
        for (uint32_t f = 0; f < s->noculta; ++f) if (hj_fila_oculta(s, f)) t += 16;
        for (int32_t i = 0; i < s->nc; ++i) { const struct hj_celda *x = &s->c[i]; t += 64 + (uint64_t)hj_slen(x->txt) * 2 + (uint64_t)hj_slen(x->form) * 2; }
    }
    return t > 0x7FFFFFFF ? 0x7FFFFFFF : (uint32_t)t;
}
uint32_t hj_serializa(const struct hj_libro *l, char *o, uint32_t cap)
{
    struct sb b = { o, cap, 0, 0 };
    sb_s(&b, "SOMA-HOJA 1\n");
    for (uint32_t i = 1; i < l->ne; ++i) {
        const struct hj_estilo *e = &l->e[i];
        sb_s(&b, "E|"); sb_n(&b, i); int v[13] = { e->nf, e->decimales, e->miles, e->negrita, e->cursiva, e->subrayado, e->tachado, e->alinear, e->valinear, e->ajustar, e->tam, e->bordes, 0 };
        for (int k = 0; k < 12; ++k) { sb_c(&b, '|'); sb_n(&b, v[k]); }
        sb_c(&b, '|'); sb_n(&b, e->color); sb_c(&b, '|'); sb_n(&b, e->fondo); sb_c(&b, '|'); sb_esc(&b, e->codigo); sb_c(&b, '\n');
    }
    for (int h = 0; h < l->nh; ++h) {
        const struct hj_hoja *s = &l->h[h];
        sb_s(&b, "H|"); sb_esc(&b, s->nombre); sb_c(&b, '|'); sb_n(&b, s->inmov_f); sb_c(&b, '|'); sb_n(&b, s->inmov_c); sb_c(&b, '|'); sb_n(&b, h == l->activa); sb_c(&b, '\n');
        if (s->hay_filtro) { sb_s(&b, "R|"); sb_n(&b, s->filtro_r1); sb_c(&b, '|'); sb_n(&b, s->filtro_c1); sb_c(&b, '|'); sb_n(&b, s->filtro_r2); sb_c(&b, '|'); sb_n(&b, s->filtro_c2); sb_c(&b, '\n');
            for (uint32_t k = 0; k < s->nfil; ++k) { sb_s(&b, "F|"); sb_n(&b, s->fil[k].col); sb_c(&b, '|'); sb_esc(&b, s->fil[k].crit); sb_c(&b, '\n'); } }
        for (uint32_t k = 0; k < s->nanchos; ++k) { sb_s(&b, "W|"); sb_n(&b, s->anchos[k].i); sb_c(&b, '|'); sb_n(&b, s->anchos[k].px); sb_c(&b, '\n'); }
        for (uint32_t k = 0; k < s->naltos; ++k) { sb_s(&b, "A|"); sb_n(&b, s->altos[k].i); sb_c(&b, '|'); sb_n(&b, s->altos[k].px); sb_c(&b, '\n'); }
        for (uint32_t f = 0; f < s->noculta; ++f) if (hj_fila_oculta(s, f)) { sb_s(&b, "O|"); sb_n(&b, f); sb_c(&b, '\n'); }
        for (uint32_t g = 0; g < s->ngraf; ++g) {
            const struct hj_grafico *x = &s->graf[g]; int64_t v[11] = { x->tipo, x->hoja_datos, x->leyenda, x->r1, x->c1, x->r2, x->c2, x->fila, x->col, x->ancho, x->alto };
            sb_s(&b, "G"); for (int k = 0; k < 11; ++k) { sb_c(&b, '|'); sb_n(&b, v[k]); } sb_c(&b, '|'); sb_esc(&b, x->titulo); sb_c(&b, '\n');
        }
        for (int32_t i = 0; i < s->nc; ++i) {
            const struct hj_celda *x = &s->c[i];
            if (x->t == HJ_VACIA && !x->es_form && !x->estilo) continue;
            sb_s(&b, "C|"); sb_n(&b, x->fila); sb_c(&b, '|'); sb_n(&b, x->col); sb_c(&b, '|'); sb_n(&b, x->estilo); sb_c(&b, '|');
            if (x->es_form) { sb_s(&b, x->es_form == 2 ? "g|" : "f|"); sb_esc(&b, x->form); sb_c(&b, '|'); }
            switch (x->t) {
            case HJ_NUM: sb_s(&b, "n|"); sb_f(&b, x->n); break;
            case HJ_TXT: sb_s(&b, "s|"); sb_esc(&b, x->txt); break;
            case HJ_BOOL: sb_s(&b, "b|"); sb_n(&b, !f64_es_cero(x->n)); break;
            case HJ_ERR: sb_s(&b, "e|"); sb_n(&b, x->err); break;
            default: sb_s(&b, "y|"); break;
            }
            sb_c(&b, '\n');
            if (b.lleno) return 0;
        }
    }
    if (b.lleno) return 0;
    o[b.p] = 0;
    return b.p;
}
/* un campo de la linea (desescapado); devuelve el puntero al siguiente o 0 */
static const char *campo_lee(const char *s, const char *fin, char *out, uint32_t cap)
{
    uint32_t k = 0;
    while (s < fin && *s != '|') {
        char c = *s++;
        if (c == '\\' && s < fin) { char d = *s++; c = d == 'p' ? '|' : d == 'n' ? '\n' : d == 'r' ? '\r' : d; }
        if (k + 1 < cap) out[k++] = c;
    }
    if (cap) out[k] = 0;
    return s < fin ? s + 1 : fin;
}
static int64_t num_de(const char *s) { int64_t v = 0; int neg = 0; if (*s == '-') { neg = 1; ++s; } int n = 0; while (es_dig(*s) && n < 18) { v = v * 10 + (*s++ - '0'); ++n; } return neg ? -v : v; }
int hj_carga(struct hj_libro *l, const char *t, uint32_t n)
{
    if (hj_libro_nuevo(l)) return -2;
    if (n < 11 || !igual_mem(t, "SOMA-HOJA 1", 11)) { hj_libro_libera(l); return -1; }
    char *a = (char *)hj_alloc(HJ_TXT_MAX + 64), *b = (char *)hj_alloc(HJ_FORM_MAX + 64);
    if (!a || !b) { if (a) hj_free(a); if (b) hj_free(b); hj_libro_libera(l); return -2; }
    int h = -1, primera = 1; uint32_t i = 0, est_n = 1;
    uint16_t *mapa_e = (uint16_t *)hj_alloc(HJ_ESTILOS * 2u); if (!mapa_e) { hj_free(a); hj_free(b); hj_libro_libera(l); return -2; }
    for (uint32_t k = 0; k < HJ_ESTILOS; ++k) mapa_e[k] = 0;
    while (i < n) {
        uint32_t j = i; while (j < n && t[j] != '\n') ++j;
        const char *s = t + i, *fin = t + j; i = j + 1;
        if (fin - s < 2 || s[1] != '|') continue;
        char tipo = s[0]; s += 2;
        if (tipo == 'E') {
            int64_t v[15]; char cod[HJ_NFCOD * 2];
            for (int k = 0; k < 15; ++k) { s = campo_lee(s, fin, a, 64); v[k] = num_de(a); }
            s = campo_lee(s, fin, cod, sizeof cod);
            struct hj_estilo e; zero(&e, sizeof e);
            e.nf = (uint8_t)(v[1] >= 0 && v[1] <= NF_CODIGO ? v[1] : 0); e.decimales = (uint8_t)(v[2] >= 0 && v[2] <= 15 ? v[2] : 0); e.miles = v[3] != 0;
            e.negrita = v[4] != 0; e.cursiva = v[5] != 0; e.subrayado = v[6] != 0; e.tachado = v[7] != 0; e.alinear = (uint8_t)(v[8] & 3); e.valinear = (uint8_t)(v[9] & 3);
            e.ajustar = v[10] != 0; e.tam = (uint8_t)(v[11] >= 0 && v[11] <= 96 ? v[11] : 0); e.bordes = (uint8_t)(v[12] & 15);
            e.color = (uint32_t)v[13] & 0x01FFFFFFu; e.fondo = (uint32_t)v[14] & 0x01FFFFFFu; cpy(e.codigo, HJ_NFCOD, cod);
            if (v[0] > 0 && v[0] < HJ_ESTILOS) mapa_e[v[0]] = (uint16_t)hj_estilo_id(l, &e);
            ++est_n; continue;
        }
        if (tipo == 'H') {
            s = campo_lee(s, fin, a, HJ_NOMBRE * 2);
            if (primera) { if (hj_hoja_renombra(l, 0, a)) (void)hj_hoja_renombra(l, 0, "Hoja1"); h = 0; primera = 0; }
            else { h = hj_hoja_nueva(l, a); if (h < 0) h = hj_hoja_nueva(l, 0); if (h < 0) break; }
            s = campo_lee(s, fin, b, 32); l->h[h].inmov_f = (uint32_t)(num_de(b) & 0xFFFFF);
            s = campo_lee(s, fin, b, 32); l->h[h].inmov_c = (uint32_t)(num_de(b) & 0x3FFF);
            s = campo_lee(s, fin, b, 32); if (num_de(b)) l->activa = h;
            continue;
        }
        if (h < 0) continue;
        struct hj_hoja *sh = &l->h[h];
        if (tipo == 'W' || tipo == 'A') { s = campo_lee(s, fin, a, 32); int64_t x = num_de(a); s = campo_lee(s, fin, b, 32); int64_t px = num_de(b); if (x < 0 || px <= 0 || px > 4000) continue;
            if (tipo == 'W' && x < HJ_COLS) hj_pon_ancho(sh, (uint32_t)x, (uint16_t)px);
            if (tipo == 'A' && x < HJ_FILAS) hj_pon_alto(sh, (uint32_t)x, (uint16_t)px);
            continue;
            }
        if (tipo == 'O') { s = campo_lee(s, fin, a, 32); int64_t x = num_de(a); if (x >= 0 && x < HJ_FILAS) hj_oculta_fila(sh, (uint32_t)x, 1); continue; }
        if (tipo == 'R') { int64_t v[4]; for (int k = 0; k < 4; ++k) { s = campo_lee(s, fin, a, 32); v[k] = num_de(a); }
            if (v[0] >= 0 && v[2] >= v[0] && v[2] < HJ_FILAS && v[1] >= 0 && v[3] >= v[1] && v[3] < HJ_COLS) { sh->hay_filtro = 1; sh->filtro_r1 = (uint32_t)v[0]; sh->filtro_c1 = (uint32_t)v[1]; sh->filtro_r2 = (uint32_t)v[2]; sh->filtro_c2 = (uint32_t)v[3]; } continue; }
        if (tipo == 'F') { if (sh->nfil >= 8) continue; s = campo_lee(s, fin, a, 32); int64_t col = num_de(a); s = campo_lee(s, fin, b, 64); if (col < 0 || col >= HJ_COLS) continue; sh->fil[sh->nfil].col = (uint32_t)col; cpy(sh->fil[sh->nfil].crit, 64, b); sh->nfil++; continue; }
        if (tipo == 'G') {
            if (sh->ngraf >= HJ_GRAF_MAX) continue;
            int64_t v[11]; for (int k = 0; k < 11; ++k) { s = campo_lee(s, fin, a, 32); v[k] = num_de(a); }
            s = campo_lee(s, fin, b, 64);
            struct hj_grafico *g = &sh->graf[sh->ngraf]; zero(g, sizeof *g);
            if (v[0] < 0 || v[0] > 5 || v[3] < 0 || v[5] < v[3] || v[5] >= HJ_FILAS || v[4] < 0 || v[6] < v[4] || v[6] >= HJ_COLS || v[7] < 0 || v[7] >= HJ_FILAS || v[8] < 0 || v[8] >= HJ_COLS) continue;
            g->tipo = (uint8_t)v[0]; g->hoja_datos = (uint8_t)(v[1] >= 0 && v[1] < HJ_HOJAS ? v[1] : h); g->leyenda = v[2] != 0;
            g->r1 = (uint32_t)v[3]; g->c1 = (uint32_t)v[4]; g->r2 = (uint32_t)v[5]; g->c2 = (uint32_t)v[6]; g->fila = (uint32_t)v[7]; g->col = (uint32_t)v[8];
            g->ancho = (uint32_t)(v[9] >= 100 && v[9] <= 3000 ? v[9] : 480); g->alto = (uint32_t)(v[10] >= 80 && v[10] <= 2000 ? v[10] : 300); cpy(g->titulo, sizeof g->titulo, b);
            sh->ngraf++; continue;
        }
        if (tipo == 'C') {
            int64_t f, c, e; s = campo_lee(s, fin, a, 32); f = num_de(a); s = campo_lee(s, fin, a, 32); c = num_de(a); s = campo_lee(s, fin, a, 32); e = num_de(a);
            if (f < 0 || f >= HJ_FILAS || c < 0 || c >= HJ_COLS) continue;
            uint16_t est = e > 0 && e < HJ_ESTILOS ? mapa_e[e] : 0;
            s = campo_lee(s, fin, a, 8);
            int form = 0; b[0] = 0;
            if (a[0] == 'f' || a[0] == 'g') { form = a[0] == 'f' ? 1 : 2; s = campo_lee(s, fin, b, HJ_FORM_MAX + 64); s = campo_lee(s, fin, a, 8); }
            char k = a[0];
            s = campo_lee(s, fin, a, HJ_TXT_MAX + 64);
            if (k == 'n') { f64 v; if (parse_punto(a, &v)) (void)hj_pon_num(l, h, (uint32_t)f, (uint32_t)c, v); }
            else if (k == 's') (void)hj_pon_txt(l, h, (uint32_t)f, (uint32_t)c, a);
            else if (k == 'b') (void)hj_pon_bool(l, h, (uint32_t)f, (uint32_t)c, num_de(a) != 0);
            else if (k == 'e') (void)hj_pon_err(l, h, (uint32_t)f, (uint32_t)c, (int)num_de(a));
            if (form && b[0]) { if (form == 1) (void)hj_pon_form(l, h, (uint32_t)f, (uint32_t)c, b); else (void)hj_pon_form_cache(l, h, (uint32_t)f, (uint32_t)c, b); }
            if (est) (void)hj_pon_estilo(l, h, (uint32_t)f, (uint32_t)c, est);
            if (l->sin_memoria) break;
            continue;
        }
    }
    (void)est_n;
    hj_free(a); hj_free(b); hj_free(mapa_e);
    if (l->sin_memoria) { hj_libro_libera(l); return -2; }
    (void)hj_recalcula(l);
    return 0;
}
