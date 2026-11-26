/* SOMA_315_YOUTUBE_AISLADO: lo que YouTube responde (JSON de Innertube, por HTTPS desde youtube.com) se lee en un SERVICIO
 * AISLADO de ring 3 (leesvc.elf, el lector del parche 312), con el mismo codigo de antes (yt_inner.h).
 *
 * La aplicacion YouTube nativa (youtube_nativo.c) buscaba los campos de la busqueda y de la ficha del video en el nucleo: una
 * respuesta preparada para romper ese analisis rompia el sistema. Ahora lo hace leesvc, que solo puede llamar a exit (sin memoria
 * del nucleo, sin red ni disco, con plazo y tope de monton). Si falla, la busqueda o el video dan error y la aplicacion lo dice;
 * NO se reintenta en el nucleo. Lo que devuelve se sanea (abajo, puro: tests/test_yt_aislado_315.c).
 * En aarch64 (sin EL0) y en las pruebas del anfitrion se llama directo. */
#ifndef SOMA_YT_AISLADO_H
#define SOMA_YT_AISLADO_H
#include <stdint.h>
#include "yt_inner.h"

/* Como yti_parse_search. Devuelve resultados (0..max) o -1 si el lector fallo; *estimado = la respuesta trae "estimatedResults"
 * (una busqueda sin resultados de verdad, no una respuesta que SOMA no entiende). */
int yti_busca_aislado(const char *b, uint32_t len, struct yt_item *it, int max, int *estimado);
/* Como yti_parse_player_ex: 1 = es una respuesta de YouTube, 0 = no lo es, -1 = el lector fallo. */
int yti_ficha_aislada(const char *b, uint32_t len, struct yti_player *o, char *desc, uint32_t dmax, int any_host);

/* ---- saneado de lo que devuelve el servicio (no se fia de nada) */

/* termina la cadena dentro de cap y cambia los caracteres de control por espacios (nl: deja los saltos de linea) */
static inline void yta_cadena(char *s, uint32_t cap, int nl)
{
    if (!cap) return;
    s[cap - 1] = 0;
    for (uint32_t i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 32 && !(nl && c == '\n')) || c == 127) s[i] = ' ';
    }
}
/* 11 caracteres de [A-Za-z0-9_-] y el terminador */
static inline int yta_id_ok(const char *id)
{
    for (int i = 0; i < YT_ID_LEN; ++i) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return id[YT_ID_LEN] == 0;
}
/* n resultados recibidos -> los que quedan (entre 0 y max): cadenas terminadas y limpias; los de id no valido se quitan */
static inline int yta_sanea_lista(struct yt_item *it, int n, int max)
{
    if (n < 0) n = 0;
    if (n > max) n = max;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        struct yt_item *o = &it[i];
        o->id[YT_ID_LEN] = 0;
        yta_cadena(o->title, sizeof o->title, 0);
        yta_cadena(o->channel, sizeof o->channel, 0);
        yta_cadena(o->desc, sizeof o->desc, 0);
        if (!yta_id_ok(o->id) || !o->title[0]) continue;
        if (k != i) it[k] = *o;
        k++;
    }
    return k;
}
/* direccion del video: solo ASCII visible (va a la linea de la peticion HTTP) y del sitio de YouTube (o de cualquiera en pruebas) */
static inline int yta_url_ok(const char *u, int any_host)
{
    if (!u[0]) return 0;
    for (const char *p = u; *p; ++p) if ((unsigned char)*p < 0x21 || (unsigned char)*p > 0x7E) return 0;
    return yti_media_url_ok(u) || (any_host && (yt_starts(u, "http://") || yt_starts(u, "https://")));
}
static inline void yta_sanea_ficha(struct yti_player *o, char *desc, uint32_t dmax, int any_host)
{
    yta_cadena(o->status, sizeof o->status, 0);
    yta_cadena(o->reason, sizeof o->reason, 0);
    yta_cadena(o->title, sizeof o->title, 0);
    yta_cadena(o->author, sizeof o->author, 0);
    o->url[sizeof o->url - 1] = 0;
    if (!yta_url_ok(o->url, any_host)) o->url[0] = 0;
    o->playable = yt_seq(o->status, "OK");
    if (desc && dmax) yta_cadena(desc, dmax, 1);
}
#endif
