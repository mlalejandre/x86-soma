/* SOMA_359_HOJA_VENTANA: deshacer (hj_toma / hj_deja) y lo que la ventana pide al nucleo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hoja_core.h"

void *hj_alloc(uint32_t n) { return malloc(n ? n : 1); }
void hj_free(void *p) { free(p); }
static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)
static struct hj_libro L;
static char b[4096];
static const char *ver(int h, const char *dir) { uint32_t f, c; hj_lee_dir(dir, &f, &c); hj_texto_ver(&L, h, f, c, b, sizeof b); return b; }
static const char *edi(int h, const char *dir) { uint32_t f, c; hj_lee_dir(dir, &f, &c); hj_texto_edicion(&L, h, f, c, b, sizeof b); return b; }
static void esc(int h, const char *dir, const char *t) { uint32_t f, c; hj_lee_dir(dir, &f, &c); int e; (void)hj_escribe(&L, h, f, c, t, &e); }
#define VER(d, esp) do { const char *_v = ver(0, d); ++n; if (strcmp(_v, esp)) { ++fallos; printf("FALLO %d: %s = '%s' (esperaba '%s')\n", __LINE__, d, _v, esp); } } while (0)
#define EDI(d, esp) do { const char *_v = edi(0, d); ++n; if (strcmp(_v, esp)) { ++fallos; printf("FALLO %d: %s edita '%s' (esperaba '%s')\n", __LINE__, d, _v, esp); } } while (0)

int main(void)
{
    CK(hj_libro_nuevo(&L) == 0, "libro");
    esc(0, "A1", "Concepto"); esc(0, "B1", "Importe");
    esc(0, "A2", "Luz"); esc(0, "B2", "45,3");
    esc(0, "A3", "Agua"); esc(0, "B3", "22,1");
    esc(0, "B4", "=SUMA(B2:B3)");
    struct hj_estilo e = *hj_estilo_de(&L, 0); e.negrita = 1; e.fondo = 0x01FEF08Au; uint32_t neg = hj_estilo_id(&L, &e);
    hj_pon_estilo(&L, 0, 0, 0, (uint16_t)neg);
    hj_recalcula(&L); VER("B4", "67,4");
    /* foto de A1:B4, cambios y vuelta atras */
    struct hj_trozo *t = hj_toma(&L, 0, 0, 0, 3, 1);
    CK(t != 0, "toma"); CK(hj_trozo_bytes(t) > 0, "bytes");
    { int h; uint32_t f1, c1, f2, c2; hj_trozo_rect(t, &h, &f1, &c1, &f2, &c2); CK(h == 0 && f1 == 0 && c1 == 0 && f2 == 3 && c2 == 1, "rect"); }
    esc(0, "B2", "100"); esc(0, "A2", "otra"); esc(0, "B4", "=B2*2"); hj_pon_estilo(&L, 0, 0, 0, 0); esc(0, "B3", "");
    hj_borra_celda(&L, 0, 2, 1);
    hj_recalcula(&L); VER("B4", "200");
    hj_deja(&L, t); hj_recalcula(&L);
    VER("A2", "Luz"); VER("B2", "45,3"); VER("B3", "22,1"); VER("B4", "67,4"); EDI("B4", "=SUMA(B2:B3)");
    { const struct hj_celda *x = hj_celda_c(&L, 0, 0, 0); CK(x && hj_estilo_de(&L, x->estilo)->negrita && hj_estilo_de(&L, x->estilo)->fondo == 0x01FEF08Au, "estilo repuesto"); }
    hj_suelta(t);
    /* lo que no estaba en la foto se borra al reponer (celda nueva dentro del rango) */
    t = hj_toma(&L, 0, 5, 0, 6, 1);
    esc(0, "A6", "nueva"); esc(0, "B7", "=1+1");
    hj_deja(&L, t); hj_suelta(t); VER("A6", ""); VER("B7", "");
    /* formula de un archivo con su valor (es_form 2): sigue igual tras reponer */
    hj_pon_num(&L, 0, 9, 0, f64_from_i64(7)); hj_pon_form_cache(&L, 0, 9, 0, "_xlfn.FUNCIONRARA(1)");
    t = hj_toma(&L, 0, 9, 0, 9, 0); hj_borra_celda(&L, 0, 9, 0); hj_deja(&L, t); hj_suelta(t); hj_recalcula(&L);
    VER("A10", "7");
    { const struct hj_celda *x = hj_celda_c(&L, 0, 9, 0); CK(x && x->es_form == 2, "es_form 2 repuesto"); }
    /* un rango vacio grande no cuesta memoria */
    t = hj_toma(&L, 0, 100, 100, HJ_FILAS - 1, HJ_COLS - 1); CK(t && hj_trozo_bytes(t) < 256, "rango vacio"); hj_deja(&L, t); hj_suelta(t);
    /* hoja que ya no existe: no hace nada */
    { int k = hj_hoja_nueva(&L, "Otra"); CK(k == 1, "hoja nueva"); esc(1, "A1", "x"); t = hj_toma(&L, 1, 0, 0, 0, 0); CK(hj_hoja_borra(&L, 1) == 0, "borrar hoja"); hj_deja(&L, t); hj_suelta(t); CK(L.nh == 1, "sigue una hoja"); }
    hj_suelta(0); CK(hj_toma(&L, 5, 0, 0, 0, 0) == 0, "hoja no valida");
    /* rellenar con una sola semilla copia; con dos, serie */
    esc(0, "D1", "Luz"); hj_rellena(&L, 0, 0, 3, 3, 3, 0); VER("D4", "Luz");
    esc(0, "E1", "1"); esc(0, "E2", "3"); hj_rellena(&L, 0, 0, 4, 4, 4, 0); VER("E5", "9");
    hj_libro_libera(&L);
    printf("test_hoja_359: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos != 0;
}
