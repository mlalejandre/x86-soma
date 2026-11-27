/* SOMA_360_HOJA_GRAFICOS: la forma de los datos de un grafico (series, categorias, titulos), donde se colocan y que siguen a su hoja. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hoja_core.h"
void *hj_alloc(uint32_t n) { return malloc(n ? n : 1); }
void hj_free(void *p) { free(p); }
static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)
static struct hj_libro L;
static void esc(int h, const char *dir, const char *t) { uint32_t f, c; hj_lee_dir(dir, &f, &c); int e; (void)hj_escribe(&L, h, f, c, t, &e); }
static int forma(int tipo, const char *rango, struct hj_gforma *fm)
{
    struct hj_grafico g; memset(&g, 0, sizeof g); g.tipo = (uint8_t)tipo; hj_lee_rango(rango, &g.r1, &g.c1, &g.r2, &g.c2);
    return hj_grafico_forma(&L, 0, &g, fm);
}
int main(void)
{
    CK(hj_libro_nuevo(&L) == 0, "libro");
    const char *T[5][4] = { { "", "Norte", "Sur", "Este" }, { "Enero", "1", "2", "3" }, { "Febrero", "4", "5", "6" }, { "Marzo", "7", "8", "9" }, { "Abril", "1", "1", "1" } };
    for (int f = 0; f < 5; ++f) for (int c = 0; c < 4; ++c) { char d[8]; snprintf(d, sizeof d, "%c%d", 'A' + c, f + 1); if (T[f][c][0]) esc(0, d, T[f][c]); }
    struct hj_gforma fm; uint32_t f, c;
    CK(!forma(1, "A1:D5", &fm), "forma"); CK(fm.por_columnas && fm.titulos && fm.categorias && fm.nser == 3 && fm.ncat == 4, "tabla con esquina vacia");
    hj_gforma_nombre(&fm, 1, &f, &c); CK(f == 0 && c == 2, "nombre de la serie 2 en C1");
    hj_gforma_valor(&fm, 2, 3, &f, &c); CK(f == 4 && c == 3, "valor D5");
    hj_gforma_cat(&fm, 0, &f, &c); CK(f == 1 && c == 0, "categoria A2");
    CK(!forma(1, "B2:D5", &fm) && !fm.titulos && !fm.categorias && fm.nser == 3 && fm.ncat == 4, "solo numeros");
    CK(!forma(1, "A1:D2", &fm) && !fm.por_columnas && fm.nser == 1 && fm.ncat == 3, "mas columnas que filas: series en filas");
    CK(!forma(3, "A1:B5", &fm) && fm.nser == 1 && fm.ncat == 4 && fm.titulos && fm.categorias, "tarta");
    CK(forma(1, "H1:J9", &fm) != 0, "rango vacio");
    CK(!forma(1, "B:B", &fm) && fm.ncat == 4, "columna entera recortada a lo usado");
    /* dispersion: la primera columna es X aunque sea numerica */
    esc(0, "F1", "x"); esc(0, "G1", "y"); esc(0, "F2", "1"); esc(0, "G2", "1"); esc(0, "F3", "2"); esc(0, "G3", "4");
    CK(!forma(5, "F1:G3", &fm) && fm.categorias && fm.titulos && fm.nser == 1 && fm.ncat == 2, "dispersion");
    CK(!forma(1, "F2:G3", &fm) && !fm.categorias && fm.nser == 2, "columnas: numeros = series");
    /* colocar: a la derecha de lo escrito y, si choca, debajo */
    int k1 = hj_grafico_nuevo(&L, 0, 0, 0, 4, 3, 1), k2 = hj_grafico_nuevo(&L, 0, 0, 0, 4, 1, 3);
    CK(k1 == 0 && k2 == 1, "dos graficos");
    CK(L.h[0].graf[0].col == 8 && L.h[0].graf[0].fila == 0, "el primero a la derecha de todo (columna I)");
    CK(L.h[0].graf[1].col == 8 && L.h[0].graf[1].fila > 13, "el segundo debajo del primero");
    for (int i = 2; i < HJ_GRAF_MAX; ++i) CK(hj_grafico_nuevo(&L, 0, 0, 0, 4, 3, 2) == i, "hasta 16");
    CK(hj_grafico_nuevo(&L, 0, 0, 0, 4, 3, 2) < 0, "no mas de 16");
    hj_grafico_borra(&L, 0, 0); CK(L.h[0].ngraf == HJ_GRAF_MAX - 1 && L.h[0].graf[0].tipo == 3, "borrar");
    CK(hj_grafico_nuevo(&L, 0, 0, 0, 4, 3, 7) < 0, "tipo no valido");
    /* los datos en otra hoja siguen a su hoja al borrar o mover hojas */
    int h1 = hj_hoja_nueva(&L, "Uno"), h2 = hj_hoja_nueva(&L, "Dos");
    int g = hj_grafico_nuevo(&L, h2, 0, 0, 4, 3, 1); L.h[h2].graf[g].hoja_datos = 0;
    CK(hj_hoja_mueve(&L, 0, 2) == 0, "mover la hoja de los datos al final");
    CK(L.h[1].graf[0].hoja_datos == 2, "el grafico sigue a sus datos");
    CK(hj_hoja_borra(&L, 0) == 0, "borrar la hoja Uno");
    CK(L.h[0].graf[0].hoja_datos == 1, "y tras borrar otra hoja");
    CK(hj_hoja_borra(&L, 1) == 0, "borrar la hoja de los datos");
    CK(L.h[0].graf[0].hoja_datos == 0, "sin su hoja, mira la suya");
    (void)h1;
    /* el formato propio guarda los graficos */
    uint32_t cap = hj_serializa_tam(&L); char *b = malloc(cap); uint32_t k = hj_serializa(&L, b, cap); struct hj_libro M;
    CK(k && !hj_carga(&M, b, k) && M.h[0].ngraf == L.h[0].ngraf, "serializa"); hj_libro_libera(&M); free(b);
    hj_libro_libera(&L);
    printf("test_hoja_360: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos != 0;
}
