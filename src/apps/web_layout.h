#ifndef SOMA_WEB_LAYOUT_H
#define SOMA_WEB_LAYOUT_H
#include "web_css.h"

/* informacion de imagen aportada por el cargador */
struct wimginfo { int w, h; const uint32_t *px; int state; int has_alpha; int pw, ph; uint32_t serial; int need_size; int ratio_only; int painting;
                  int fixed_w, fixed_h; };   /* SOMA_342_NAVEGADOR_AGIL (entrada, solo al maquetar un <img>): ancho/alto que fija el CSS o los
                                              * atributos; -1 = lo decide la imagen; 0 = sin informacion (al pintar) */   /* SOMA_188: painting (entrada): se pide al pintar (esta a la vista) */   /* SOMA_188: ratio_only = SVG con viewBox y sin width/height (ocupa el ancho disponible) */   /* SOMA_152B: need_size (entrada): la maquetacion necesita el tamano natural */   /* SOMA_152: serial unico por mapa de bits decodificado (0 = desconocido) */   /* w,h: tamano natural; pw,ph: pixeles guardados (0 = iguales) */   /* state: 0 desconocida, 1 cargando, 2 lista, 3 fallo */
typedef int (*wl_img_cb)(void *ud, const char *url, struct wimginfo *out);

enum { CTL_NONE = 0, CTL_IMG, CTL_TEXT, CTL_PASSWORD, CTL_CHECK, CTL_RADIO, CTL_BUTTON, CTL_BTN_INPUT, CTL_SELECT, CTL_TEXTAREA, CTL_PLACEHOLDER, CTL_RANGE, CTL_COLOR, CTL_FILE };
enum { BF_FLOAT = 1, BF_ABS = 2, BF_FIXED = 4, BF_BFC = 8, BF_ANON = 16, BF_REL = 32, BF_INLINE = 64, BF_BLOCKIFIED = 128 };

enum { II_TEXT = 1, II_OPEN, II_CLOSE, II_BR, II_ATOM, II_FLOAT, II_ABS };
struct wii { uint8_t type; const struct wstyle *st; struct wnode *node; struct wnode *link; const char *txt; uint32_t len; struct wbox *box; };

struct wfrag { int32_t x, y, w; int16_t asc, dsc; uint8_t va; int16_t sh; const char *txt; uint16_t len; const struct wstyle *st; struct wnode *node; struct wnode *link; int32_t jsp; };   /* SOMA_248_DOC: jsp = espacio extra por blanco (1/64 px, text-align: justify) */
struct wdeco { int32_t x0, y0, x1, y1; const struct wstyle *st; struct wnode *node; struct wnode *link; uint8_t first, last; };

struct wbox {
    struct wnode *node; const struct wstyle *st;
    struct wbox *parent, *first, *last, *next;
    uint8_t kind, ctl, flags, tab_sub;
    int32_t x, y, w, h;
    int32_t ml, mr, mt, mb, pl, pr, pt, pb, bl, br, bt, bb;
    int32_t baseline, imin, imax, rdx, rdy, mt_eff, mb_eff;
    uint8_t iok, top_abs, bot_abs, pad0;
    struct wii *items; int32_t nitems;
    struct wfrag *frags; int32_t nfrags;
    struct wdeco *decos; int32_t ndecos;
    struct wbox *abs_cb, *abs_head, *abs_next;
    int32_t sx, sy;                      /* posicion estatica para absolutos */
    int32_t list_n, colspan, rowspan, col, row;
    int32_t *tcols; int32_t ncols;       /* tablas: anchos de columna */
    int32_t cont_h;                      /* altura del contenido (sin relleno) */
    int32_t sch;                         /* SOMA_181: altura natural del contenido (para desplazar overflow:auto) */
    int32_t zi, def_h, cbh;
    uint8_t mauto;                       /* bit0 izq, bit1 der, bit2 arriba, bit3 abajo */
    struct wbox *abs_chain;              /* lista de absolutos de este bloque contenedor */
};

struct wfl;                              /* lista de flotantes */
struct wlay {
    struct wdoc *d; struct wcss *css;
    struct wa_arena ar;
    struct wbox *root;
    int vw, vh, doc_w, doc_h;
    wl_img_cb img_cb; void *img_ud;
    int zoom;
    int painting;                 /* SOMA_188: dentro de wl_paint */
    struct wbox **deferred; int ndef, capdef;
    int link_color_ok;
    struct wnode *hover;
    struct wnode *focus;
    int caret_on;
    uint32_t canvas_bg; int depth; int scroll_x, scroll_y;
    struct wnode *canvas_nodes[2];
    uint32_t inner_gen;                  /* SOMA_181: cambia al desplazar una caja con overflow */
};

struct wlay *wl_new(struct wdoc *d, struct wcss *css, wl_img_cb cb, void *ud);
void wl_free(struct wlay *L);
void wl_layout(struct wlay *L, int vw, int vh);
/* pinta la pagina en el rectangulo de pantalla (sx,sy,sw,sh) con desplazamiento vertical scroll_y */
void wl_paint(struct wlay *L, int sx, int sy, int sw, int sh, int scroll_x, int scroll_y);
/* busqueda de elemento en coordenadas de pagina */
struct wnode *wl_hit(struct wlay *L, int px, int py, struct wnode **link, struct wbox **boxout);
/* rectangulo de pagina de un elemento (primera caja); devuelve 0 si no tiene */
int wl_node_rect(struct wlay *L, struct wnode *n, int *x, int *y, int *w, int *h);
/* SOMA_181: desplaza la caja con overflow bajo (px,py) (coordenadas de pagina); 1 si consumio el gesto */
int wl_inner_scroll(struct wlay *L, int px, int py, int dy);
uint32_t wl_inner_gen(const struct wlay *L);
int wl_scan_flags(struct wlay *L);   /* SOMA_203 */
int wl_node_scroll_max(struct wlay *L, struct wnode *n);
const char *wl_ctl_text(struct wnode *n);
const char *wl_img_src(const struct wnode *n);
int wl_select_count(struct wnode *n);
struct wnode *wl_select_option(struct wnode *n, int i);
int wl_select_index(struct wnode *n);   /* URL de imagen elegida para <img>/<input type=image> */        /* valor visible de un campo */
#endif
