/* SOMA_360_HOJA_GRAFICOS: la IA dentro de la hoja: una pregunta sobre la hoja (con su contenido) al modelo de lenguaje, en un hilo
 * propio (la ventana nunca espera). La respuesta puede traer una FORMULA para la celda activa o una TABLA para pegar. */
#ifndef HOJA_IA_H
#define HOJA_IA_H
#include <stdint.h>
enum { HIA_NADA, HIA_PENSANDO, HIA_LISTA, HIA_ERROR };
int  hoja_ia_pide(const char *pregunta, const char *contexto);   /* 0 = en marcha; -1 ocupada o sin memoria */
int  hoja_ia_estado(void);
const char *hoja_ia_texto(void);          /* la respuesta (sin la formula ni la tabla) o el error */
const char *hoja_ia_formula(void);        /* "=..." o "" */
const char *hoja_ia_tabla(void);          /* filas con tabuladores o "" */
void hoja_ia_olvida(void);
uint32_t hoja_ia_version(void);
/* separa la respuesta del modelo (en el sitio) en texto, formula y tabla; publica para las pruebas */
void hoja_ia_separa(char *r, char **texto, char **formula, char **tabla);
#endif
