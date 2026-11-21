#ifndef SOMA_AGENT_RUTA_H
#define SOMA_AGENT_RUTA_H
/* SOMA_251_RUTAS: decisiones de camino de los agentes, por PALABRAS completas (sin mayusculas ni tildes).
 * Antes se buscaban trozos: "programa" contenia "ram" (iba al Mayordomo a ensenar la memoria), "describe" contenia "escribe" y
 * "creacion" contenia "crea" (obligaban a guardar un archivo). Logica pura: se prueba en el anfitrion (tests/test_rutas_251.c). */
#include <stdint.h>

/* 1 si text contiene la palabra o la frase (varias palabras seguidas). Un '*' final en una palabra = prefijo ("redact*"). */
int ar_word(const char *text, const char *phrase);
int ar_any(const char *text, const char *const *phrases);      /* lista terminada en 0 */

/* La mision pide GUARDAR el resultado en un archivo (guardalo, en un archivo, una ruta, nombre.txt...). path: ruta si se da
 * (relativa -> /workspace/...), o "" si hay que inventarla. */
int ar_wants_save(const char *mission, char *path, uint32_t cap);

/* Operaciones del sistema que resuelve el Mayordomo sin el modelo (RAM, disco, archivos, carpetas...). */
int ar_mayordomo(const char *mission);

/* 1 = se intenta responder con UNA llamada al modelo, sin herramientas (conversacion, redaccion, explicaciones...).
 * 0 = la mision necesita herramientas (internet, datos actuales, archivos, ejecutar...). El modelo puede devolver igualmente
 * AR_TOOLS_MARK desde la respuesta directa y se pasa al bucle con herramientas. */
int ar_direct_ok(const char *mission);
#define AR_TOOLS_MARK "[[HERRAMIENTAS]]"
#endif
