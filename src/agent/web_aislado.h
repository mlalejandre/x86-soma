/* SOMA_312_WEB_AISLADA: el agente lee lo que trae de Internet en un SERVICIO AISLADO de ring 3 (src/user/leesvc.c).
 *
 * Las ordenes "investiga" y "search" descargan paginas web cualesquiera y resultados de Wikipedia y DuckDuckGo, y los convierten en
 * texto para el modelo. Antes ese analisis (HTML, entidades, JSON) corria en el nucleo: una pagina preparada para romperlo rompia el
 * sistema. Ahora corre en leesvc.elf, que solo puede llamar a exit (sin memoria del nucleo, sin red ni disco, con plazo y tope de
 * monton). Si falla, la pagina o la busqueda cuentan como no leidas: el agente ya sabe seguir sin ellas. NO se reintenta en el nucleo.
 * Lo que devuelve se sanea (abajo, puro: tests/test_web_aislado_312.c). En aarch64 y en las pruebas del anfitrion se llama directo. */
#ifndef SOMA_WEB_AISLADO_H
#define SOMA_WEB_AISLADO_H
#include <stdint.h>

uint32_t wl_texto_aislado(const char *html, uint32_t n, char *out, uint32_t cap, char *titulo, uint32_t tcap);   /* como wl_texto */
int      bw_wikipedia_aislado(const char *json, const char *lang, char *out, uint32_t cap, int max);           /* como bw_parse_wikipedia */
int      bw_ddg_aislado(const char *html, char *out, uint32_t cap, int max);                                   /* como bw_parse_ddg_lite */

/* Texto que llega del servicio: como mucho cap - 1 bytes, terminado, y cortado en el primer NUL (lo usan como cadena C). n = bytes recibidos. */
static inline uint32_t web_aislado_sanea(char *out, uint32_t n, uint32_t cap)
{
    if (!cap) return 0;
    if (n > cap - 1) n = cap - 1;
    out[n] = 0;
    uint32_t k = 0;
    while (k < n && out[k]) k++;
    return k;
}
/* Cuantos resultados dice el servicio: entre 0 y max. */
static inline int web_aislado_cuenta(int k, int max) { return k < 0 ? 0 : (k > max ? max : k); }
#endif
