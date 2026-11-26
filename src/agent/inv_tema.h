#ifndef SOMA_INV_TEMA_H
#define SOMA_INV_TEMA_H
/* SOMA_336_INVESTIGA_TEMAS: el investigador (shell_investiga.c) adapta la busqueda al tema de la pregunta. Codigo puro (se prueba en
 * el anfitrion: tests/test_inv_tema_336.c).
 *   - tema: deportes, salud y medicina, ciencia, tecnologia, actualidad (noticias, politica, economia) o general;
 *   - actualidad: si hace falta lo de esta semana o de hoy (resultados, noticias, "hoy", "ayer", "ultimo"...);
 *   - fuentes: cada URL se puntua segun el tema (fuentes sanitarias oficiales para la salud, prensa deportiva para los
 *     resultados, revistas y organismos cientificos para la ciencia...) y se penalizan los sitios de poca calidad;
 *   - instrucciones para el informe segun el tema (fechas y marcadores; prudencia y "consulta a un profesional"; consenso
 *     frente a hipotesis...). */
#include <stdint.h>

enum { IT_GENERAL = 0, IT_DEPORTES, IT_SALUD, IT_CIENCIA, IT_TECNO, IT_ACTUALIDAD, IT_N };
enum { IT_RECIENTE_NO = 0, IT_RECIENTE_SEMANA, IT_RECIENTE_DIA };

int  it_tema(const char *pregunta);                       /* IT_* */
int  it_reciente(const char *pregunta, int tema);         /* IT_RECIENTE_*: filtro de fecha para el buscador */
const char *it_nombre(int tema);                          /* "deportes", "salud"... */
int  it_puntua(int tema, const char *url);                /* mas alto = mejor fuente para ese tema; < 0 = no leerla */
int  it_wiki_en(int tema);                                /* 1 = buscar tambien en la Wikipedia en ingles */
const char *it_instrucciones(int tema, int reciente);     /* se anaden a las instrucciones del informe ("" si nada) */
/* Ordena urls[0..n) de mejor a peor fuente para el tema (estable: a igualdad, el orden del buscador) y quita las de puntuacion < 0.
 * Devuelve cuantas quedan. */
int  it_ordena(int tema, char urls[][300], int n);
/* Quita de r (en el sitio) el razonamiento de un modelo que razona: "[Razonamiento]: ... [Respuesta]:" y <think>...</think>.
 * Si el razonamiento no se cerro, no queda nada. Devuelve la longitud. */
uint32_t it_sin_razonamiento(char *r);

#endif
