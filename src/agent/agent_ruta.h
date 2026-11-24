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

/* SOMA_288_INVESTIGA: la mision pide investigar o informarse en internet ("investiga...", "busca informacion sobre...", "averigua...",
 * "busca X en internet", "ultimas noticias de..."). No cuenta lo que es de archivos, del disco, de YouTube o de la memoria de SOMA. */
int ar_investiga(const char *mission);              /* 0 = no; 1 = una busqueda ("busca X en internet"); 2 = investigacion a fondo */
/* La consulta para el buscador sin las palabras de la peticion: "buscame en internet recetas de pasta" -> "recetas de pasta". */
void ar_consulta(const char *mission, char *out, uint32_t cap);
/* SOMA_292_PAINT_IA: la mision pide un DIBUJO ("dibujame un faro", "pinta un gato en paint", "haz un dibujo de..."). No cuentan
 * los graficos, diagramas, esquemas o tablas, ni preguntar como se dibuja algo, ni "pinta la pared de azul". */
int ar_dibujo(const char *mission);
/* SOMA_295_CORREO: la mision es del correo. 0 no; AR_CO_REDACTA "escribe un correo a Ana diciendole..." (tambien "responde al correo de...");
 * AR_CO_ENVIA "envialo" (frase corta: confirma el borrador que preparo el orbe); AR_CO_LEE "tengo correos nuevos?", "resume mis correos de hoy";
 * AR_CO_BUSCA "busca el correo de Amazon del pedido"; AR_CO_ABRE "abre el correo". No cuenta lo que es de internet, de programar o de explicar. */
enum { AR_CO_REDACTA = 1, AR_CO_ENVIA = 2, AR_CO_LEE = 3, AR_CO_BUSCA = 4, AR_CO_ABRE = 5 };
int ar_correo(const char *mission);
#endif
