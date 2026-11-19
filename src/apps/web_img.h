#ifndef SOMA_WEB_IMG_H
#define SOMA_WEB_IMG_H
#include <stdint.h>
/* Decodificadores de imagen del navegador: PNG, GIF, JPEG (base y progresivo), BMP y, desde SOMA_180, WebP (con perdida, sin perdida, alfa y animado), SVG (rasterizador propio de enteros), ICO y animaciones GIF/WebP. Salida ARGB (alfa 0xFF = opaco). */
struct wimg { int w, h; uint32_t *px; int nw, nh; int frames; uint16_t *delay; };   /* SOMA_180: frames > 1 = animacion: px tiene `frames` fotogramas de w*h seguidos y delay[i] su duracion en ms */
/* PARCHE 086: nw/nh = tamano natural si se decodifico reducida (0 = w/h) */

extern int wi_oom;   /* PARCHE 086: 1 si la ultima decodificacion se quedo sin memoria */
#define WI_MAX_PIXELS (6u * 1024u * 1024u)
int  wi_decode(const uint8_t *d, uint32_t n, struct wimg *o);   /* 0 = OK */
void wi_free(struct wimg *o);
/* PARCHE 086: decodifica sin bajar de maxw x maxh (los JPEG se reducen al decodificar si sobra tamano). -2 = sin memoria */
int  wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh);
/* PARCHE 138: decodificadores directos (se ejecutan en el proceso aislado imgdec en x86_64; en aarch64, sin ring 3, en el kernel) */
int  wi_decode_local(const uint8_t *d, uint32_t n, struct wimg *o);
int  wi_decode_fit_local(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh);
/* SOMA_180 */
#define WI_ANIM_MAX_PIXELS (3u * 1024u * 1024u)                  /* pixeles de todos los fotogramas de una animacion juntos */
#define WI_ANIM_MAX_FRAMES 400
int  wi_is_svg(const uint8_t *d, uint32_t n);                    /* 1 = parece un documento SVG */
int  wi_anim_frames(const uint8_t *d, uint32_t n);               /* fotogramas de un GIF/WebP (1 = imagen fija); no decodifica */
/* animacion completa (fotogramas ya compuestos). Si no cabe en max_pixels se reduce (nw/nh = tamano natural) o se corta. 0 = bien, -2 = sin memoria */
int  wi_decode_anim(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels);
int  wi_decode_anim_local(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels);
/* SVG dibujado a dw x dh (0 = tamano natural); color = valor de currentColor (0xRRGGBB). nw/nh = tamano natural */
int  wi_decode_svg(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color);
int  wi_decode_svg_local(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color);
struct image;
int  wi_decode_bmp_raw(const uint8_t *d, uint32_t n, struct image *img);   /* BMP con su alfa (image_decode) */
#endif
