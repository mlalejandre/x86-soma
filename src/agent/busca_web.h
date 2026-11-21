#ifndef SOMA_BUSCA_WEB_H
#define SOMA_BUSCA_WEB_H
/* SOMA_251_RUTAS: busqueda web de verdad (antes un proxy en el puerto 8095 de la puerta de enlace: en el portatil era el router,
 * cada busqueda podia esperar 30-45 s y siempre fallaba). Se pregunta por HTTPS a la API de Wikipedia (es) y a DuckDuckGo
 * (version ligera) y se resumen los resultados. Piezas puras (se prueban en el anfitrion): codificar la consulta y leer las
 * respuestas. */
#include <stdint.h>
void bw_urlenc(const char *in, char *out, uint32_t cap);
/* Resultados en texto: "- Titulo: resumen (url)\n". Devuelven cuantos resultados se escribieron. */
int  bw_parse_wikipedia(const char *json, const char *lang, char *out, uint32_t cap, int max);
int  bw_parse_ddg_lite(const char *html, char *out, uint32_t cap, int max);
#endif
