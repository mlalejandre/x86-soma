/* SOMA_360_HOJA_GRAFICOS: la forma de los datos de un grafico (series, categorias y titulos) a partir de su rango, como Excel:
 *   - mas filas que columnas: cada columna es una serie; si no, cada fila;
 *   - la primera fila (o columna) son los nombres de las series si es texto;
 *   - la primera columna (o fila) son las categorias si tiene texto, si la esquina esta vacia o si es un grafico de dispersion (X).
 * Logica pura: la usan la ventana (para pintar), el escritor de XLSX (referencias de cada serie) y las pruebas. */
#include "hoja_core.h"

static int es_txt(const struct hj_libro *l, int h, uint32_t f, uint32_t c) { const struct hj_celda *x = hj_celda_c(l, h, f, c); return x && x->t == HJ_TXT; }
static int vacia(const struct hj_libro *l, int h, uint32_t f, uint32_t c) { const struct hj_celda *x = hj_celda_c(l, h, f, c); return !x || x->t == HJ_VACIA; }

int hj_grafico_forma(const struct hj_libro *l, int hoja_graf, const struct hj_grafico *g, struct hj_gforma *fm)
{
    int h = g->hoja_datos < l->nh ? g->hoja_datos : hoja_graf;
    if (h < 0 || h >= l->nh || g->r2 < g->r1 || g->c2 < g->c1) return -1;
    fm->h = h; fm->r1 = g->r1; fm->c1 = g->c1; fm->r2 = g->r2; fm->c2 = g->c2;
    /* lo que de verdad hay: el rango recortado a lo usado (A:A son un millon de filas) */
    const struct hj_hoja *s = &l->h[h];
    if (fm->r2 >= s->max_fila) fm->r2 = s->max_fila ? s->max_fila - 1 : 0;
    if (fm->c2 >= s->max_col) fm->c2 = s->max_col ? s->max_col - 1 : 0;
    if (fm->r2 < fm->r1 || fm->c2 < fm->c1) return -1;
    uint32_t filas = fm->r2 - fm->r1 + 1, cols = fm->c2 - fm->c1 + 1;
    fm->por_columnas = filas >= cols;
    /* a = eje de las series (columnas si por_columnas), b = eje de los puntos */
    uint32_t na = fm->por_columnas ? cols : filas, nb = fm->por_columnas ? filas : cols;
    #define F(ia, ib) (fm->por_columnas ? fm->r1 + (ib) : fm->r1 + (ia))
    #define C(ia, ib) (fm->por_columnas ? fm->c1 + (ia) : fm->c1 + (ib))
    /* titulos: la primera linea de puntos (sin la esquina) es texto */
    fm->titulos = 0;
    if (nb > 1) {
        int txt = 0, num = 0;
        for (uint32_t a = 0; a < na; ++a) { if (es_txt(l, h, F(a, 0), C(a, 0))) ++txt; else if (!vacia(l, h, F(a, 0), C(a, 0))) ++num; }
        if (na == 1) fm->titulos = txt == 1;
        else { int esq = vacia(l, h, F(0, 0), C(0, 0)) || es_txt(l, h, F(0, 0), C(0, 0)); int txt2 = txt - (es_txt(l, h, F(0, 0), C(0, 0)) ? 1 : 0); fm->titulos = esq && txt2 > 0 && num == 0; }
    }
    /* categorias: la primera serie es texto (o la esquina vacia con titulos, o dispersion) */
    fm->categorias = 0;
    if (na > 1) {
        if (g->tipo == 5) fm->categorias = 1;
        else {
            for (uint32_t b = fm->titulos; b < nb && !fm->categorias; ++b) if (es_txt(l, h, F(0, b), C(0, b))) fm->categorias = 1;
            if (!fm->categorias && fm->titulos && vacia(l, h, F(0, 0), C(0, 0))) fm->categorias = 1;
        }
    }
    #undef F
    #undef C
    fm->nser = na - fm->categorias; fm->ncat = nb - fm->titulos;
    if (!fm->nser || !fm->ncat) return -1;
    return 0;
}
void hj_gforma_nombre(const struct hj_gforma *fm, uint32_t k, uint32_t *f, uint32_t *c)
{
    uint32_t a = k + fm->categorias;
    *f = fm->por_columnas ? fm->r1 : fm->r1 + a; *c = fm->por_columnas ? fm->c1 + a : fm->c1;
}
void hj_gforma_valor(const struct hj_gforma *fm, uint32_t k, uint32_t i, uint32_t *f, uint32_t *c)
{
    uint32_t a = k + fm->categorias, b = i + fm->titulos;
    *f = fm->por_columnas ? fm->r1 + b : fm->r1 + a; *c = fm->por_columnas ? fm->c1 + a : fm->c1 + b;
}
void hj_gforma_cat(const struct hj_gforma *fm, uint32_t i, uint32_t *f, uint32_t *c)
{
    uint32_t b = i + fm->titulos;
    *f = fm->por_columnas ? fm->r1 + b : fm->r1; *c = fm->por_columnas ? fm->c1 : fm->c1 + b;
}
int hj_grafico_nuevo(struct hj_libro *l, int h, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2, int tipo)
{
    if (h < 0 || h >= l->nh || r2 < r1 || c2 < c1 || tipo < 0 || tipo > 5) return -1;
    struct hj_hoja *s = &l->h[h];
    if (s->ngraf >= HJ_GRAF_MAX) return -1;
    struct hj_grafico *g = &s->graf[s->ngraf];
    uint8_t *z = (uint8_t *)g; for (uint32_t i = 0; i < sizeof *g; ++i) z[i] = 0;
    g->tipo = (uint8_t)tipo; g->hoja_datos = (uint8_t)h; g->leyenda = 1;
    g->r1 = r1; g->c1 = c1; g->r2 = r2; g->c2 = c2;
    g->ancho = 520; g->alto = 320;
    /* a la derecha de todo lo escrito y, si choca con otro grafico, debajo de el */
    uint32_t col = (s->max_col > c2 + 1 ? s->max_col : c2 + 1) + 1; if (col >= HJ_COLS) col = c1;
    g->col = col; g->fila = r1;
    for (int vuelta = 0; vuelta < HJ_GRAF_MAX; ++vuelta) {
        int choca = 0;
        for (uint32_t k = 0; k < s->ngraf; ++k) {
            const struct hj_grafico *o = &s->graf[k];
            /* extension de o en celdas (con los anchos y altos de la hoja) */
            uint32_t oc2 = o->col, of2 = o->fila; uint32_t acc = 0;
            while (oc2 + 1 < HJ_COLS && acc + hj_ancho(s, oc2) < o->ancho) { acc += hj_ancho(s, oc2); ++oc2; }
            acc = 0; while (of2 + 1 < HJ_FILAS && acc + hj_alto(s, of2) < o->alto) { acc += hj_alto(s, of2); ++of2; }
            uint32_t gc2 = g->col, gf2 = g->fila; acc = 0;
            while (gc2 + 1 < HJ_COLS && acc + hj_ancho(s, gc2) < g->ancho) { acc += hj_ancho(s, gc2); ++gc2; }
            acc = 0; while (gf2 + 1 < HJ_FILAS && acc + hj_alto(s, gf2) < g->alto) { acc += hj_alto(s, gf2); ++gf2; }
            if (g->col <= oc2 && o->col <= gc2 && g->fila <= of2 && o->fila <= gf2) { g->fila = of2 + 2 < HJ_FILAS ? of2 + 2 : of2; choca = 1; }
        }
        if (!choca) break;
    }
    l->version++;
    return s->ngraf++;
}
void hj_grafico_borra(struct hj_libro *l, int h, int k)
{
    if (h < 0 || h >= l->nh) return;
    struct hj_hoja *s = &l->h[h];
    if (k < 0 || k >= s->ngraf) return;
    for (int i = k; i + 1 < s->ngraf; ++i) s->graf[i] = s->graf[i + 1];
    s->ngraf--; l->version++;
}
const char *hj_grafico_tipo_nombre(int t)
{
    static const char *const N[6] = { "Barras", "Columnas", "L\303\255neas", "Tarta", "\303\201rea", "Dispersi\303\263n" };
    return t >= 0 && t < 6 ? N[t] : "Columnas";
}
