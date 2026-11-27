/* SOMA_353_CACHE_HTTP: cache de subrecursos (CSS, JavaScript, fuentes, imagenes) en memoria del proceso del navegador.
 * Guarda respuestas que web_net.c marca como guardables (wn_resp.cache_s > 0) y las sirve mientras siguen frescas, sin revalidar.
 * La clave es la URL pedida y si se pidio como binario. Nada va al disco. */
#ifndef SOMA_WEB_HCACHE_H
#define SOMA_WEB_HCACHE_H
#include <stdint.h>
#include "web_net.h"

int  whc_get(const char *url, int bin, uint32_t max_body, struct wn_resp *rs);   /* 1 = servida de la cache (rs->body es nuevo) */
void whc_put(const char *url, int bin, const struct wn_resp *rs);                 /* guarda una copia si rs->cache_s > 0 */
void whc_set_budget(uint32_t bytes);                                             /* tope de bytes (0 = el de siempre segun la memoria) */
void whc_clear(void);
uint32_t whc_bytes(void);                                                        /* bytes ocupados ahora */
uint32_t whc_count(void);                                                        /* entradas guardadas */

#endif
