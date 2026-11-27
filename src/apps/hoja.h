/* SOMA_358_HOJA: la hoja de calculo dentro del sistema: libros abiertos, abrir y guardar archivos y la orden `hoja`.
 * Abrir un XLSX/ODS/CSV de fuera: en x86_64 se lee en el servicio aislado mimesvc (ring 3), que devuelve el libro en el formato propio
 * de SOMA; el nucleo lo vuelve a leer (hj_carga, que valida todo). Guardar (XLSX, ODS, CSV o .hoja) se hace aqui: el libro es de SOMA. */
#ifndef HOJA_H
#define HOJA_H
#include <stdint.h>
#include "hoja_core.h"

#define HOJA_LIBROS 8

const char *hoja_carpeta(void);                                 /* SOMA_363_ACCESO: "/usuario/Documentos/Hojas de calculo/" */
int   hoja_nuevo(const char *nombre);                           /* indice o -1; ruta <hoja_carpeta()><nombre>.xlsx */
int   hoja_abre(const char *ruta, char *msg, uint32_t cap);     /* indice (si ya estaba abierto, ese) o <0 */
int   hoja_guarda(int i, const char *ruta, char *msg, uint32_t cap);   /* ruta 0 = la suya; el formato sale de la extension */
int   hoja_cierra(int i);
struct hj_libro *hoja_libro(int i);
const char *hoja_ruta(int i);
int   hoja_cambiado(int i);                                     /* hay cambios sin guardar */
void  hoja_toca(int i);                                         /* se ha modificado (recalcula) */
int   hoja_actual(void);                                        /* el libro con el que trabaja la orden (-1 ninguno) */
void  hoja_pon_actual(int i);
int   hoja_formato_ruta(const char *ruta);                      /* HJF_* por la extension */
uint32_t hoja_version(void);
int   hoja_cmd(const char *args, char *out, uint32_t max);      /* la orden `hoja` */
#endif
