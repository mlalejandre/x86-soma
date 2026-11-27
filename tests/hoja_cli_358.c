/* SOMA_358_HOJA: herramienta de prueba de los formatos de la hoja (la usa tests/test_hoja_fmt_358.py):
 *   hoja_cli_358 demo SALIDA.(xlsx|ods|csv|hoja)   escribe un libro de ejemplo
 *   hoja_cli_358 lee ARCHIVO                        lo lee como lo leeria SOMA y lista hojas y celdas (vista | edicion)
 *   hoja_cli_358 convierte ENTRADA SALIDA            lee y vuelve a escribir (ida y vuelta)
 *   hoja_cli_358 demograf SALIDA                     SOMA_360_HOJA_GRAFICOS: libro con graficos de todos los tipos */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hoja_core.h"
#include "hoja_fmt.h"

void *hj_alloc(uint32_t n) { return malloc(n ? n : 1); }
void hj_free(void *p) { free(p); }
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
static int64_t ahora(void) { return (int64_t)20784 * 1440 + 600; }
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }

static uint8_t *lee(const char *ruta, uint32_t *n)
{
    FILE *f = fopen(ruta, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long t = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)t + 1); if (fread(b, 1, (size_t)t, f) != (size_t)t) { fclose(f); free(b); return 0; }
    fclose(f); b[t] = 0; *n = (uint32_t)t; return b;
}
static int escribe(struct hj_libro *l, const char *ruta)
{
    uint32_t n = 0; uint8_t *d = 0; const char *ext = strrchr(ruta, '.');
    if (ext && !strcmp(ext, ".xlsx")) d = hj_escribe_xlsx(l, &n);
    else if (ext && !strcmp(ext, ".ods")) d = hj_escribe_ods(l, &n);
    else if (ext && !strcmp(ext, ".csv")) { d = malloc(1 << 22); n = hj_csv_escribe(l, l->activa, ';', (char *)d, 1 << 22); }
    else { uint32_t cap = hj_serializa_tam(l); d = malloc(cap); n = hj_serializa(l, (char *)d, cap); }
    if (!d || !n) { fprintf(stderr, "no se pudo escribir\n"); return 1; }
    FILE *f = fopen(ruta, "wb"); fwrite(d, 1, n, f); fclose(f); free(d); return 0;
}
static void demo(struct hj_libro *l)
{
    hj_libro_nuevo(l); int e;
    hj_hoja_renombra(l, 0, "Gastos");
    const char *C[][4] = { { "Concepto", "Importe", "Fecha", "IVA" }, { "Alquiler", "850", "1/11/2026", "=B2*21%" }, { "Luz", "63,40", "5/11/2026", "=B3*21%" },
                           { "Agua \"fr\303\255a\" & m\303\241s", "21,5", "9/11/2026", "=B4*0,1" }, { "Total", "=SUMA(B2:B4)", "=MAX(C2:C4)", "=SUMA(D2:D4)" } };
    for (int f = 0; f < 5; ++f) for (int c = 0; c < 4; ++c) hj_escribe(l, 0, (uint32_t)f, (uint32_t)c, C[f][c], &e);
    struct hj_estilo est = *hj_estilo_de(l, 0); est.negrita = 1; est.fondo = 0x01DDEEFF; est.bordes = 2; est.alinear = AL_CENTRO;
    uint32_t cab = hj_estilo_id(l, &est); for (int c = 0; c < 4; ++c) hj_pon_estilo(l, 0, 0, (uint32_t)c, (uint16_t)cab);
    struct hj_estilo mon = *hj_estilo_de(l, 0); mon.nf = NF_MONEDA; mon.decimales = 2; mon.miles = 1; uint32_t im = hj_estilo_id(l, &mon);
    for (int f = 1; f < 5; ++f) { hj_pon_estilo(l, 0, (uint32_t)f, 1, (uint16_t)im); hj_pon_estilo(l, 0, (uint32_t)f, 3, (uint16_t)im); }
    struct hj_estilo pc = *hj_estilo_de(l, 0); pc.nf = NF_PORCENTAJE; pc.decimales = 1; uint32_t ip = hj_estilo_id(l, &pc);
    hj_escribe(l, 0, 6, 0, "Subida", &e); hj_escribe(l, 0, 6, 1, "0,035", &e); hj_pon_estilo(l, 0, 6, 1, (uint16_t)ip);
    hj_escribe(l, 0, 7, 0, "Media", &e); hj_escribe(l, 0, 7, 1, "=REDONDEAR(PROMEDIO(B2:B4);2)", &e);
    hj_escribe(l, 0, 8, 0, "Hay agua", &e); hj_escribe(l, 0, 8, 1, "=SI(CONTAR.SI(A2:A4;\"Agua*\")>0;VERDADERO;FALSO)", &e);
    hj_escribe(l, 0, 9, 0, "Texto", &e); hj_escribe(l, 0, 9, 1, "=CONCATENAR(\"Total: \";TEXTO(B5;\"0,00\"))", &e);
    hj_escribe(l, 0, 10, 0, "Error", &e); hj_escribe(l, 0, 10, 1, "=1/0", &e);
    hj_escribe(l, 0, 11, 0, "Hora", &e); hj_escribe(l, 0, 11, 1, "14:30", &e);
    hj_escribe(l, 0, 12, 0, "L\303\255nea\nsegunda", &e);
    hj_pon_ancho(&l->h[0], 0, 180); hj_pon_alto(&l->h[0], 12, 40); l->h[0].inmov_f = 1;
    int h = hj_hoja_nueva(l, "Resumen 2026");
    hj_escribe(l, h, 0, 0, "Total de gastos", &e); hj_escribe(l, h, 0, 1, "=Gastos!B5", &e); hj_escribe(l, h, 1, 0, "Con IVA", &e); hj_escribe(l, h, 1, 1, "=Gastos!B5+Gastos!D5", &e);
    hj_escribe(l, h, 2, 0, "Buscar", &e); hj_escribe(l, h, 2, 1, "=BUSCARV(\"Luz\";Gastos!A2:B4;2;FALSO)", &e);
    hj_escribe(l, h, 3, 0, "Hoy", &e); hj_escribe(l, h, 3, 1, "=SI.ERROR(BUSCARX(\"Gas\";Gastos!A2:A4;Gastos!B2:B4);\"no hay\")", &e);
    hj_filtra(l, 0, 0, 0, 4, 3, 1, ">50");
    hj_filtra(l, 0, 0, 0, 4, 3, 1, "");
    hj_recalcula(l);
}
static void demograf(struct hj_libro *l)
{
    hj_libro_nuevo(l); int e;
    hj_hoja_renombra(l, 0, "Ventas 2026");
    const char *C[][4] = { { "", "Norte", "Sur", "Este" }, { "Enero", "120", "80", "45,5" }, { "Febrero", "135", "70", "60" }, { "Marzo", "150", "95", "52" }, { "Abril", "160", "110", "75" } };
    for (int f = 0; f < 5; ++f) for (int c = 0; c < 4; ++c) hj_escribe(l, 0, (uint32_t)f, (uint32_t)c, C[f][c], &e);
    int k = hj_grafico_nuevo(l, 0, 0, 0, 4, 3, 1); memcpy(l->h[0].graf[k].titulo, "Ventas por zona", 16);
    k = hj_grafico_nuevo(l, 0, 0, 0, 4, 1, 3); l->h[0].graf[k].fila = 20; memcpy(l->h[0].graf[k].titulo, "Norte", 6);
    k = hj_grafico_nuevo(l, 0, 0, 0, 4, 3, 2); l->h[0].graf[k].fila = 40; l->h[0].graf[k].leyenda = 0;
    hj_escribe(l, 0, 10, 0, "x", &e); hj_escribe(l, 0, 10, 1, "y", &e);
    for (int i = 1; i <= 6; ++i) { char a[8], b[8]; snprintf(a, sizeof a, "%d", i); snprintf(b, sizeof b, "%d", i * i); hj_escribe(l, 0, 10 + (uint32_t)i, 0, a, &e); hj_escribe(l, 0, 10 + (uint32_t)i, 1, b, &e); }
    k = hj_grafico_nuevo(l, 0, 10, 0, 16, 1, 5); l->h[0].graf[k].fila = 60;
    int h = hj_hoja_nueva(l, "Otra hoja");
    k = hj_grafico_nuevo(l, h, 0, 0, 4, 3, 0); l->h[h].graf[k].hoja_datos = 0; l->h[h].graf[k].col = 1; l->h[h].graf[k].fila = 1;
    k = hj_grafico_nuevo(l, h, 0, 0, 4, 2, 4); l->h[h].graf[k].hoja_datos = 0; l->h[h].graf[k].col = 1; l->h[h].graf[k].fila = 22;
    hj_recalcula(l);
}
static void lista(struct hj_libro *l)
{
    static char v[40000], ed[40000];
    printf("HOJAS %d ACTIVA %d\n", l->nh, l->activa);
    for (int h = 0; h < l->nh; ++h) {
        struct hj_hoja *s = &l->h[h];
        printf("HOJA %s|%u|%u|filtro=%d\n", s->nombre, s->inmov_f, s->inmov_c, s->hay_filtro);
        for (uint32_t f = 0; f < s->max_fila; ++f) for (uint32_t c = 0; c < s->max_col; ++c) {
            const struct hj_celda *x = hj_celda_c(l, h, f, c); if (!x || (x->t == HJ_VACIA && !x->es_form)) continue;
            char d[16]; hj_dir(f, c, d); hj_texto_ver(l, h, f, c, v, sizeof v); hj_texto_edicion(l, h, f, c, ed, sizeof ed);
            for (char *z = v; *z; ++z) if (*z == '\n') *z = '~';
            for (char *z = ed; *z; ++z) if (*z == '\n') *z = '~';
            const struct hj_estilo *e = hj_estilo_de(l, x->estilo);
            printf("%s|%s|%s|%s%s%s\n", d, v, ed, e->negrita ? "N" : "", e->fondo ? "F" : "", e->bordes ? "B" : "");
        }
        for (uint32_t k = 0; k < s->nanchos; ++k) printf("ANCHO %u %u\n", s->anchos[k].i, s->anchos[k].px);
        for (uint32_t k = 0; k < s->naltos; ++k) printf("ALTO %u %u\n", s->altos[k].i, s->altos[k].px);
        for (uint32_t k = 0; k < s->ngraf; ++k) { const struct hj_grafico *g = &s->graf[k]; printf("GRAF %u|%u|%u,%u,%u,%u|%u,%u|%u,%u|%s|%u\n", g->tipo, g->hoja_datos, g->r1, g->c1, g->r2, g->c2, g->fila, g->col, g->ancho, g->alto, g->titulo, g->leyenda); }
    }
}
int main(int argc, char **argv)
{
    hj_ahora_fn = ahora;
    if (argc < 3) { fprintf(stderr, "uso\n"); return 2; }
    struct hj_libro l;
    if (!strcmp(argv[1], "demo")) { demo(&l); int r = escribe(&l, argv[2]); hj_libro_libera(&l); return r; }
    if (!strcmp(argv[1], "demograf")) { demograf(&l); int r = escribe(&l, argv[2]); hj_libro_libera(&l); return r; }
    uint32_t n; uint8_t *d = lee(argv[2], &n); if (!d) { fprintf(stderr, "no puedo leer %s\n", argv[2]); return 2; }
    if (!strcmp(argv[1], "fuzz")) {                              /* fuzz ARCHIVO N: N variantes con bytes cambiados (y recortes) */
        int veces = argc > 3 ? atoi(argv[3]) : 200; srand(358); uint8_t *m = malloc(n + 1); char err[200];
        for (int it = 0; it < veces; ++it) {
            memcpy(m, d, n); uint32_t k = n;
            int cambios = 1 + rand() % 12; for (int c = 0; c < cambios; ++c) m[(uint32_t)rand() % n] = (uint8_t)rand();
            if (it % 5 == 0) k = (uint32_t)rand() % n + 1;
            struct hj_libro x; (void)hj_lee_archivo(&x, m, k, 0, err, sizeof err); hj_recalcula(&x); hj_libro_libera(&x);
        }
        free(m); free(d); printf("fuzz %d: bien\n", veces); return 0;
    }
    char err[200]; int r = hj_lee_archivo(&l, d, n, 0, err, sizeof err); free(d);
    if (r) { printf("ERROR %d %s\n", r, err); hj_libro_libera(&l); return 1; }
    if (!strcmp(argv[1], "lee")) lista(&l);
    else if (!strcmp(argv[1], "convierte") && argc > 3) r = escribe(&l, argv[3]);
    hj_libro_libera(&l);
    return r;
}
