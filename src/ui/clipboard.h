#ifndef SOMA_CLIPBOARD_H
#define SOMA_CLIPBOARD_H
/* SOMA_PARCHE_PORTAPAPELES: portapapeles global del sistema (texto, imagenes y archivos).
 *
 * Atajos (en cualquier aplicacion de la GUI):
 *   Ctrl+C copiar   Ctrl+X cortar   Ctrl+V pegar   Ctrl+Y panel de historial del portapapeles
 * Cada aplicacion aporta un adaptador (notepad, visor de imagenes, paint, gestor de archivos, barra del agente, navegador,
 * terminal). Todo el sistema comparte el mismo contenido, asi que lo copiado en una aplicacion se pega en otra.
 */
#include <stdint.h>

#define CLIP_NONE  0
#define CLIP_TEXT  1
#define CLIP_IMAGE 2
#define CLIP_FILE  3          /* ruta de un archivo (el texto del portapapeles es la ruta) */

#define CLIP_OP_COPY  0
#define CLIP_OP_CUT   1
#define CLIP_OP_PASTE 2

#define CLIP_HIST_MAX     8
#define CLIP_TEXT_MAX     65536u
#define CLIP_IMG_MAX_PIX  (1280u * 1024u)

/* --- contenido --- */
int         clip_kind(void);                                   /* tipo del elemento mas reciente */
uint32_t    clip_seq(void);                                    /* sube cada vez que cambia el contenido */
int         clip_set_text(const char *s, uint32_t n);          /* copia (UTF-8); n==0 => strlen */
int         clip_set_file(const char *path, int is_cut);       /* ruta de archivo */
int         clip_set_image(const uint32_t *px, int w, int h);  /* px: 0x00RRGGBB, w*h pixeles */
const char *clip_text(uint32_t *n);                            /* texto (o ruta) o NULL si no hay */
const uint32_t *clip_image(int *w, int *h);                    /* imagen o NULL */
int         clip_file_is_cut(void);
void        clip_file_done(void);                              /* tras pegar un archivo cortado: olvida el origen */
void        clip_clear(void);

/* codifica la imagen del portapapeles como BMP 24 bpp en out (devuelve bytes o <0 si no cabe) */
int         clip_image_to_bmp(uint8_t *out, uint32_t max);

/* --- avisos y panel de historial --- */
void        clip_toast(const char *msg);                       /* aviso breve en pantalla */
void        clip_toast_ms(const char *msg, uint32_t ms);       /* SOMA_238_GUARDADO: aviso que dura ms */
int         clip_overlay_active(void);
void        clip_overlay_open(void);
void        clip_overlay_close(void);
int         clip_overlay_key(char c);                          /* devuelve 1 si hay que pegar el elemento elegido */
void        clip_overlay_draw(void);                           /* lo llama fb_present() */

/* adaptadores de las aplicaciones (devuelven 1 si han tratado la orden) */
int notepad_clip_op(int op);
int imgview_clip_op(int op);
int paint_clip_op(int op);
int fm_clip_op(int op);

/* utilidades para los adaptadores */
void        clip_status_text(char *out, uint32_t max, const char *prefix, uint32_t n);   /* "prefijo: N B" */
int         keyboard_ctrl_held(void);
int         keyboard_shift_held(void);

#endif
