#ifndef SOMA_WEB_PAGE_H
#define SOMA_WEB_PAGE_H
/* Pagina web viva: descarga, analisis HTML/CSS, scripts, maquetacion, imagenes, formularios, navegacion e interaccion.
 * Todo lo bloqueante (red, scripts, maquetacion) se ejecuta en el hilo trabajador; el hilo de la GUI solo llama a wp_paint / wp_hit_info. */
#include "web_layout.h"
#include "web_js.h"
#include "web_net.h"

#define WP_HIST 32
enum { WP_IDLE = 0, WP_FETCH, WP_RES, WP_SCRIPT, WP_IMAGES };
enum { WPN_NORMAL = 0, WPN_REPLACE = 1, WPN_RELOAD = 2, WPN_NOHIST = 4, WPN_NOSORRY = 8 };
enum { WPS_OK = 0, WPS_BUSY, WPS_ERR };
enum { WPK_NONE = 0, WPK_TEXT, WPK_LINK };      /* cursor sugerido */

struct wp_hist { char url[2048]; int scroll_y; char title[64]; };   /* SOMA 188B: 2048 (era 512): las busquedas de Google pasan de 512 */
struct wp_img { char *url; int w, h; uint32_t *px; int state; int has_alpha; int tries; uint32_t hash; int pw, ph; uint32_t serial; /* SOMA_152 */ int frames, cur; uint16_t *delay; uint32_t total_ms; /* SOMA_180: animacion (px tiene `frames` fotogramas de pw*ph) */ int size_dep; /* SOMA_152B */
                int dist, dw, dh; uint8_t ratio_only, is_svg, redraws, painted;
                uint8_t aplazada;
                int fw, fh; uint8_t sin_fijo; };   /* SOMA_342_NAVEGADOR_AGIL: el mayor ancho/alto fijo con que se muestra (CSS o atributos;
                                                    * 0 = no se sabe); sin_fijo = en algun sitio se muestra a su tamano natural */   /* SOMA_341_NAVEGADOR_MEMORIA: no cupo lejos de la vista: se carga al acercarse (no antes de maquetar) */   /* SOMA_188: painted = pedida al pintar (fondo CSS a la vista) */             /* SOMA_PARCHE_028_UI: distancia a la zona visible y tamano mostrado (0 = desconocido) */
struct wp_store;
struct wp_svg;

struct wpage {
    struct wdoc *doc; struct wcss *css; struct wlay *lay; struct wjs *js;
    char url[WN_URL_MAX]; char title[160];
    char status[112]; int status_kind; int phase; int progress;
    int vw, vh, sx, sy;
    int need_style, forced, hover_rules, styles_pending;
    uint64_t last_layout_ms, load_t0, caret_t, last_img_layout;
    struct wp_hist hist[WP_HIST]; int hn, hpos;
    struct wp_img *imgs; int nimg, capimg; uint32_t img_bytes; int img_pending;
    struct wnode *focus, *hover; const char *focus_orig;
    char nav_url[WN_URL_MAX]; int nav_mode, nav_pending, nav_delta; char *nav_post; uint32_t nav_plen; char nav_ctype[64];
    char alert_msg[200]; int alert_pending;
    struct wnode *dd_sel;                         /* <select> con la lista desplegada */
    volatile int stop; int dead; int scripts_run; int in_script;
    struct wp_store *ss;                          /* sessionStorage */
    int caret_on;
    char last_form_url[WN_URL_MAX];
    struct wjs_host host; uint64_t nav_at;
    int resized; uint32_t layout_dur; int refresh_rel;
    volatile int lay_busy; volatile uint32_t gen;  /* lay_busy: hay una maquetacion en curso (el GUI no debe tocar p->lay) */
    volatile int scroll_req;                      /* SOMA_152: desplazamiento pedido por teclado (lo anima el hilo de la GUI) */
    int script_phase, script_guard, script_status; /* SOMA_152B: scripts repartidos en turnos (la pagina se usa mientras) */
    int img_relayout_due;                         /* SOMA_152B: una imagen de la que dependia la maquetacion ya llego */
    struct wnode *dynq[64]; int dynq_h, dynq_n;   /* SOMA_152C: <script src> insertados por JS, pendientes (async) */
    struct wnode *script_wait;                    /* SOMA_152D: script del HTML cuya descarga esta en el hilo de E/S */
    int img_ok, img_fail, img_logged; char img_why[72];
    uint32_t svg_color;                    /* SOMA_180: color actual mientras se dibuja un <svg> en linea */
    uint32_t anim_tick;                    /* SOMA_180: cambia cuando alguna imagen animada visible pasa de fotograma (entra en la clave de repintado) */
    int img_dirty;
    uint32_t img_scan_key; int img_waiting;       /* SOMA_PARCHE_028_UI: carga diferida de imagenes fuera de la pantalla */                                /* SOMA_PARCHE_028_UI: han llegado imagenes desde la ultima maquetacion */
    uint32_t shcss[256]; int nshcss;              /* SOMA_188: hojas de arboles sombra ya anadidas (hash) */
    uint64_t af_until, af_next; int user_acted;  /* SOMA_188: autofocus de elementos que llegan tarde (insertados por JS) */
    struct wp_svg *svgs; int nsvg;                /* SOMA_PARCHE_028_UI: <svg> en linea pendientes de rasterizar en la pasarela */
    /* SOMA_188: cola de transitionend/animationstart/animationend (no se anima; se avisa a su tiempo) */
    struct { struct wnode *n; uint64_t at; const char *name; uint16_t ms; uint8_t kind; } aq188[48];
    int naq188;
};

struct wpage *wp_new(void);
void wp_free(struct wpage *p);
void wp_set_viewport(struct wpage *p, int vw, int vh);
/* navegacion bloqueante (hilo trabajador) */
int  wp_navigate(struct wpage *p, const char *url, int flags);
void wp_history_go(struct wpage *p, int delta);
int  wp_can_back(const struct wpage *p);
int  wp_can_fwd(const struct wpage *p);
uint32_t wp_store_gen(void);                                   /* SOMA_203 */
void wp_store_export(const struct wpage *p, struct wb *o);
int  wp_store_import(struct wpage *p, const void *data, uint32_t len);
int  wp_img_get(struct wpage *p, const char *src, const uint32_t **px, int *w, int *h, char *abs, uint32_t absmax);   /* PARCHE 085 */
/* devuelve ms hasta que conviene volver a llamar */
int  wp_tick(struct wpage *p, uint64_t now);
/* GUI */
void wp_paint(struct wpage *p, int sx, int sy, int sw, int sh);
int  wp_hit_cursor(struct wpage *p, int px, int py);         /* WPK_* */
void wp_scroll_by(struct wpage *p, int dx, int dy);
int  wp_doc_height(const struct wpage *p);
/* eventos (hilo trabajador) */
void wp_mouse_click(struct wpage *p, int px, int py, int button, int mods, int dbl);
void wp_mouse_move(struct wpage *p, int px, int py);
void wp_key(struct wpage *p, int key, const char *utf8, int mods);
void wp_select_choose(struct wpage *p, struct wnode *sel, int index);
void wp_eval_js(struct wpage *p, const char *code, char *out, uint32_t max);
void wp_find_scroll(struct wpage *p, const char *text);      /* busca texto y desplaza */
const char *wp_status(const struct wpage *p);

/* servicios aportados por quien enlaza (browser.c en el kernel; arnes en pruebas) */
int  wp_fs_read(const char *path, char **out, uint32_t *len);         /* 0 = OK; *out con kmalloc */
void wp_log(int level, const char *msg);
#endif
