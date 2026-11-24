#ifndef SOMA_PROG_VOZ_H
#define SOMA_PROG_VOZ_H
/* SOMA_290_VOZ_CODIGO: lo que dice SOMA en voz alta mientras programa (desde el orbe o el chat) y al terminar.
 * Logica pura (frases, nombres, limpieza de la explicacion del modelo): se prueba en el anfitrion (tests/test_prog_voz_290.c).
 * El pegamento (voz, ventana, llamada al modelo) esta en agent.c (programador_run). */
#include <stdint.h>

/* Frase para una fase del programador (las de cc_run: 2 generar, 4 reparar o regenerar, 5 revisar). desc y det son los textos
 * de la fase ("Fase 4/6: Reparando (llamada 2/7)...", "Linea 12: se esperaba ')'"). n = cuantas veces se ha reparado ya (0 la
 * primera). 1 = hay algo que decir (en out). */
int  pv_narra(int fase, const char *desc, const char *det, int n, char *out, uint32_t cap);
/* "Python", "C", "JavaScript", "una pagina web"... por la extension del archivo */
const char *pv_lenguaje(const char *ruta);
/* "snake" de "/workspace/juegos/snake.py" */
void pv_nombre(const char *ruta, char *out, uint32_t cap);
/* Limpia la explicacion del modelo para leerla: sin <think>, sin bloques de codigo, sin Markdown; como mucho 3 frases y max caracteres. */
void pv_limpia(char *s, uint32_t max);
/* Lo que se dice al terminar. ok: el programa funciona; editar: era un cambio; expl: la explicacion (puede ser ""); error: el ultimo
 * fallo si no salio. */
void pv_final(int ok, int editar, const char *ruta, const char *expl, const char *error, char *out, uint32_t cap);
#endif
