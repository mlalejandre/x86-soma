#ifndef SOMA_WEB_LEE_H
#define SOMA_WEB_LEE_H
/* SOMA_288_INVESTIGA: leer paginas web para el agente (piezas puras, se prueban en el anfitrion: tests/test_investiga_288.c).
 *   wl_texto   el texto que se lee de una pagina: sin scripts, estilos, menus, cabeceras, pies ni formularios; titulos como "## ",
 *              listas como "- ", entidades resueltas, espacios juntados; si la pagina tiene <article> o <main> con texto, solo eso.
 *              Se quitan las lineas cortas sueltas (menus, botones) y las repetidas.
 *   wl_urls    las URL "(http...)" de unos resultados de busca_web, sin repetir, sin redes sociales ni anuncios, como mucho dos por sitio.
 *   wl_dominio el sitio de una URL ("es.wikipedia.org"). */
#include <stdint.h>

uint32_t wl_texto(const char *html, uint32_t n, char *out, uint32_t cap, char *titulo, uint32_t tcap);
int      wl_urls(const char *resultados, char urls[][300], int max);
void     wl_dominio(const char *url, char *out, uint32_t cap);
int      wl_url_util(const char *url);          /* 0 = no merece la pena leerla (redes sociales, videos, anuncios, archivos) */
#endif
