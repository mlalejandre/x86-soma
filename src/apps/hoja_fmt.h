/* SOMA_358_HOJA: formatos de archivo de la hoja de calculo: XLSX (Office Open XML), ODS (OpenDocument) y CSV, y lo que comparten
 * (ZIP y un lector de XML minimo). Leer un archivo de fuera se hace SOLO en el servicio aislado mimesvc (ring 3): alli se construye
 * el libro con hj_lee_archivo() y se devuelve en el formato propio de SOMA (hj_serializa), que el nucleo vuelve a leer y validar.
 * Escribir (exportar) se hace en el nucleo: el libro es de SOMA. */
#ifndef HOJA_FMT_H
#define HOJA_FMT_H
#include <stdint.h>
#include "hoja_core.h"

enum { HJF_DESCONOCIDO = 0, HJF_XLSX, HJF_ODS, HJF_CSV, HJF_SOMA };
#define HJF_MAX_ENTRADA   (48u << 20)          /* archivo de entrada */
#define HJF_MAX_PARTE     (96u << 20)          /* una parte del ZIP descomprimida (contra bombas) */

int      hj_formato_de(const uint8_t *d, uint32_t n, const char *nombre);   /* por el contenido y, si no, por la extension */
/* l queda creado (hj_libro_nuevo) aunque falle; 0 = bien. err: por que no */
int      hj_lee_archivo(struct hj_libro *l, const uint8_t *d, uint32_t n, int tipo, char *err, uint32_t cap);
uint8_t *hj_escribe_xlsx(struct hj_libro *l, uint32_t *n);      /* hj_alloc; 0 si no hay memoria */
uint8_t *hj_escribe_ods(struct hj_libro *l, uint32_t *n);

/* ---- ZIP */
struct zw;                                                        /* escritor */
struct zw *zw_nuevo(void);
int      zw_pon(struct zw *z, const char *nombre, const uint8_t *d, uint32_t n, int comprimir);
uint8_t *zw_cierra(struct zw *z, uint32_t *n);                   /* libera z; devuelve el ZIP (hj_alloc) o 0 */
/* lector: una entrada por su nombre (hj_alloc, terminada en 0); 0 si no esta o no se puede (cifrada, ZIP64, demasiado grande) */
uint8_t *zr_lee(const uint8_t *zip, uint32_t n, const char *nombre, uint32_t *len);
int      zr_es_zip(const uint8_t *d, uint32_t n);

/* ---- XML (lector de eventos minimo) */
struct xr { const char *s; uint32_t n, p; char tag[64]; int cierra, vacia; const char *attr; uint32_t nattr; };
int  xr_sig(struct xr *x);                                        /* 1 = hay etiqueta en x->tag (sin prefijo); 0 fin */
int  xr_attr(const struct xr *x, const char *nombre, char *out, uint32_t cap);   /* 1 si esta (desescapado); nombre sin prefijo */
uint32_t xr_texto(struct xr *x, const char *fin_tag, char *out, uint32_t cap);   /* texto hasta </fin_tag> (desescapado; concatena los nodos) */
/* ---- texto que crece (para escribir XML) */
struct gb { char *p; uint32_t n, cap; int err; };
void gb_s(struct gb *b, const char *s);
void gb_c(struct gb *b, char c);
void gb_n(struct gb *b, int64_t v);
void gb_f(struct gb *b, f64 v);                                   /* numero para XML ("1.5", "1e+21") */
void gb_esc(struct gb *b, const char *s);                         /* & < > " ' y fuera los controles */
void gb_libera(struct gb *b);
#endif
