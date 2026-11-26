#ifndef SOMA_NOTICIAS_RSS_H
#define SOMA_NOTICIAS_RSS_H
/* SOMA_337_NOTICIAS: lector de canales RSS 2.0 y Atom, y lo que necesita la aplicacion de Noticias sin tocar el nucleo (se prueba
 * en el anfitrion: tests/test_noticias_337.c). En SOMA corre en el servicio aislado leesvc (ring 3): el XML que llega de Internet
 * no se lee en el nucleo.
 *   - <item> (RSS) y <entry> (Atom): titulo, enlace, fecha (RFC 822 y ISO 8601, con su zona), resumen sin HTML y la fuente
 *     (<source> de Google Noticias, <dc:creator>, o el sufijo " - Medio" del titulo de Google Noticias);
 *   - CDATA, entidades (&amp; &#233; &#xE9; &aacute; ...), espacios juntados; todo cortado a su tamano, nunca fuera del bufer;
 *   - categorias con sus canales (Google Noticias en espanol, y lo local por busqueda) y la configuracion (/etc/noticias.conf). */
#include <stdint.h>

#define NT_TIT  200
#define NT_URL  320
#define NT_FTE  64
#define NT_DESC 360
struct nt_item {
    char tit[NT_TIT], url[NT_URL], fuente[NT_FTE], desc[NT_DESC];
    int64_t t;                                   /* minutos desde 1970 (UTC); 0 = sin fecha */
};

/* Lee los elementos del canal. fuente_def: nombre del medio si el canal no lo dice (puede ser 0). Devuelve cuantos (<= max). */
int  nt_rss(const char *xml, uint32_t n, struct nt_item *it, int max, const char *fuente_def);
/* Fechas: "Tue, 25 Nov 2026 18:30:00 GMT", "... +0100", "2026-11-25T18:30:00Z", "...+01:00". Minutos UTC; 0 si no se entiende. */
int64_t nt_fecha(const char *s);
/* Asegura que cada cadena esta terminada (lo que llega del servicio aislado) */
void nt_sanea(struct nt_item *it, int n);
/* Quita repetidos (mismo titulo sin mayusculas ni signos, o mismo enlace) y ordena de mas nuevo a mas viejo. Devuelve cuantos quedan. */
int  nt_limpia(struct nt_item *it, int n);
/* "hace 5 min", "hace 3 h", "ayer", "hace 4 dias", "12/11" */
void nt_hace(int64_t ahora_min, int64_t t, char *o, uint32_t cap);

/* ---- categorias y configuracion ---- */
enum { NTC_PORTADA, NTC_ESPANA, NTC_LOCAL, NTC_MUNDO, NTC_ECONOMIA, NTC_TECNO, NTC_CIENCIA, NTC_SALUD, NTC_DEPORTES, NTC_CULTURA, NTC_N };
const char *nt_cat_nombre(int c);                /* "Portada", "Espana", "Local"... */
const char *nt_cat_id(int c);                    /* "portada", "espana", "local"... */
int  nt_cat_busca(const char *s);                /* por id o nombre, sin mayusculas ni tildes; -1 */
/* URL del canal de la categoria (Google Noticias, espanol de Espana). Local: busqueda del lugar. 0 si no hay (Local sin lugar). */
uint32_t nt_cat_url(int c, const char *lugar, char *o, uint32_t cap);

#define NT_EXTRA 8
struct nt_conf {
    uint32_t cats;                               /* bit c = categoria c activada */
    char lugar[64];                              /* para Local: ciudad, provincia o region */
    int nextra; char extra[NT_EXTRA][NT_URL];    /* canales RSS anadidos por la persona (salen en Portada) */
};
void nt_conf_defecto(struct nt_conf *c);
void nt_conf_lee(struct nt_conf *c, const char *txt);
uint32_t nt_conf_escribe(const struct nt_conf *c, char *o, uint32_t cap);
#endif
