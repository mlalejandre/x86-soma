#ifndef SOMA_WEB_NET_H
#define SOMA_WEB_NET_H
#include "web_util.h"

/* SOMA 188B: 8192 (era 1024): el script principal de Google tiene una URL de casi 4000 caracteres y se truncaba (404).
 * El servicio aislado netsvc (ring 3, pila pequena, URL limitada a 4096 por su ABI) conserva 1024. */
#ifdef SOMA_NETSVC
#define WN_URL_MAX 1024
#else
#define WN_URL_MAX 8192
#endif
struct wn_url { char scheme[12]; char host[128]; int port; char path[WN_URL_MAX]; char frag[128]; int has_host; };

struct wn_req {
    const char *url; const char *method;                  /* method NULL = GET */
    const char *body; uint32_t body_len; const char *ctype;
    const char *referer; const char *accept;
    uint32_t timeout_ms; uint32_t max_body; int no_redirect; int want_binary;
    const volatile int *cancel; /* SOMA_151_ASYNC_IMAGES: NULL si no es cancelable */
    int via_gateway;                                      /* SOMA_PARCHE_028_UI: pedir tambien http:// a traves de la pasarela (conversion de imagenes) */
    /* SOMA_195 (cliente del LLM): cabeceras propias ("Nombre: valor\r\n...", p. ej. la clave de una API) y https SIEMPRE con el TLS propio
     * y certificado verificado, tambien en QEMU. Las cabeceras propias nunca se envian a la pasarela del anfitrion. */
    const char *extra_headers; int direct;
};
struct wn_resp {
    int status; char *body; uint32_t len;
    char ctype[96]; char charset[24]; char final_url[WN_URL_MAX]; int redirects; int via_gateway;
    char err[96];
};

int  wn_url_parse(const char *url, struct wn_url *u);                        /* 0 = OK */
int  wn_url_resolve(const char *base, const char *ref, char *out, uint32_t max);
void wn_url_origin(const char *url, char *out, uint32_t max);                /* esquema://host[:puerto] */
void wn_url_encode(const char *s, char *out, uint32_t max);
void wn_http_lock_init(void); /* SOMA 151: registrar mutex antes de iniciar IO */
int  wn_fetch(const struct wn_req *rq, struct wn_resp *rs);                  /* 0 = respuesta recibida (mira rs->status) */
/* PARCHE 142: lo mismo pero en el servicio aislado netsvc (x86_64; src/apps/web_net_sbx.c). Lo usa el kernel. */
int  wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs);
void wn_free_isolated(struct wn_resp *rs);
/* SOMA_327_RED_FUERA (solo x86_64): GET (body = 0) o POST JSON por netsvc, con el contrato de http_get / http_post_json */
struct http_response;
int  wn_http_simple(const char *url, const char *body, char *buf, uint32_t cap, struct http_response *r, uint32_t ms, const volatile int *cancel);
void wn_free(struct wn_resp *rs);

/* cookies */
void wn_cookie_store(const char *url, const char *setcookie);
int  wn_cookie_header(const char *url, char *out, uint32_t max, int for_js);   /* longitud */
void wn_cookie_clear(void);
uint32_t wn_cookie_gen(void);                                  /* SOMA_203 */
uint32_t wn_cookie_export(void *out, uint32_t max);            /* bytes escritos (0 = no cabe) */
int  wn_cookie_import(const void *in, uint32_t len);
/* SOMA_PARCHE_028_UI: lo que sabe hacer la pasarela del anfitrion (cabecera X-Soma-Gateway). 0 = desconocida o antigua.
 * Las URL "soma-gw:/ruta" son ordenes directas a la pasarela (p. ej. POST soma-gw:/svg para rasterizar un <svg> en linea). */
#define WN_GW_PRESENT 1
#define WN_GW_IMAGES  2     /* convierte WebP/AVIF/ICO a PNG/JPEG */
#define WN_GW_SVG     4     /* rasteriza SVG */
int  wn_gateway_caps(void);
int  wn_https_unverified(void);                                              /* SOMA_176A: https directo (TLS propio) sin verificar certificados */
const char *wn_tls_last_error(void);
int wn_https_directo(void);   /* SOMA_316_RED_AISLADA: 1 si la red es real (https con TLS propio; en QEMU va por la pasarela) */
#endif
