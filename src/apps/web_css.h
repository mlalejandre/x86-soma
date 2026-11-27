#ifndef SOMA_WEB_CSS_H
#define SOMA_WEB_CSS_H
#include "web_dom.h"

enum { LK_AUTO = 0, LK_VAL = 1, LK_NONE = 2, LK_FR = 3, LK_FIT = 4, LK_MINC = 5 };   /* SOMA_181: LK_FIT (fit-content / max-content) y LK_MINC (min-content) solo en width */
struct wlen { int32_t px; int32_t pct; uint8_t kind; };      /* pct en 1/100 % ; fr: px = fr*100 */

enum { D_NONE = 0, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM, D_FLEX, D_INLINE_FLEX, D_TABLE, D_TABLE_ROW,
       D_TABLE_CELL, D_TABLE_GROUP, D_TABLE_CAPTION, D_GRID, D_CONTENTS, D_INLINE_TABLE };
enum { P_STATIC = 0, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum { FL_NONE = 0, FL_LEFT, FL_RIGHT };
enum { TA_LEFT = 0, TA_CENTER, TA_RIGHT, TA_JUSTIFY };
enum { WS_NORMAL = 0, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_SUB, VA_SUPER, VA_TEXT_TOP, VA_TEXT_BOTTOM };
enum { LS_DISC = 0, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_NONE, LS_ALPHA, LS_ROMAN, LS_UALPHA };
enum { JC_START = 0, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY };
enum { AI_STRETCH = 0, AI_START, AI_END, AI_CENTER, AI_BASELINE, AI_AUTO };

struct wvar { struct wvar *next; const char *name; const char *val; };

#define GRID_MAX_TRACKS 12   /* SOMA_181 (antes 8) */
#define GRID_MAX_ROWS 10
struct wgx { struct wlen rows[GRID_MAX_ROWS]; int8_t nrows; struct wlen auto_rows; const char *areas;
             /* SOMA_347_NAVEGADOR_GRID: nombres de lineas ("indice:nombre;" con el indice de linea antes de expandir repeat()),
              * el repeat(auto-fill/auto-fit) de las columnas (desde la pista rep_at, rep_n pistas; se expande al maquetar con el ancho
              * real) y, en un elemento, las lineas por nombre de grid-column/grid-row (0 col ini, 1 col fin, 2 fila ini, 3 fila fin) */
             int8_t rep_at, rep_n; const char *cnames, *rnames; const char *nm[4]; };   /* SOMA_181: filas y areas de una rejilla */
struct wstyle {
    uint8_t display, position, floating, clear, visibility, overflow_x, overflow_y, white_space, text_align;
    uint8_t text_decor;           /* bit0 underline, bit1 line-through, bit2 overline */
    uint8_t text_transform;       /* 0 none, 1 upper, 2 lower, 3 capitalize */
    uint8_t vertical_align, italic, bold, mono, list_style, list_inside, box_sizing;
    uint8_t flex_dir;             /* 0 row, 1 row-rev, 2 col, 3 col-rev */
    uint8_t flex_wrap, justify, align_items, align_self, align_content;
    uint8_t border_style[4];      /* 0 none, 1 solid, 2 dashed, 3 dotted */
    uint8_t border_collapse, text_overflow, word_break, bg_repeat, bg_size_mode, opacity;
    uint8_t grad_dir, has_grad, pointer, clearfix;
    uint8_t serif;                /* SOMA_347_NAVEGADOR_SERIF: font-family con serifa (heredado) */
    uint8_t wfam, wface;          /* SOMA_348_FUENTES_WEB: familia de @font-face (1..N, 0 = ninguna; heredada) y cara cargada que le toca (0 = aun no) */
    uint8_t no_ptr;               /* SOMA_188: pointer-events:none (heredado): el elemento no recibe clics; sus hijos con auto si */
    struct wnode *pse[2];
    uint32_t svg_fill, svg_stroke;   /* SOMA_188: fill/stroke de CSS (heredados) para SVG en linea: 0 = no fijado, 1 = none, 2 = currentColor, si no 0xFF000000|rgb */
    int16_t svg_sw;                  /* stroke-width x100 (0 = no fijado) */
    int16_t rot10, sc_x, sc_y;       /* SOMA_188: transform rotate (decimas de grado) y scale (en %, 0 = sin escala) */         /* SOMA_188: nodos sinteticos de ::before y ::after (NULL o display:none si no hay) */
    int16_t font_px;
    uint8_t lh_mode;              /* 0 normal, 1 multiplicador (lh_val x100), 2 px */
    int16_t lh_val;
    int16_t border_w[4];          /* top right bottom left */
    int16_t radius, letter_sp, z_index, order, spacing_h, spacing_v;
    int16_t flex_grow, flex_shrink;   /* x100 */
    int16_t grid_cols, col_span, bg_pos_x, bg_pos_y;
    uint32_t color, bg_color, border_color[4], grad_a, grad_b;
    struct wlen width, height, min_w, max_w, min_h, max_h;
    struct wlen margin[4], padding[4];
    struct wlen top, right, bottom, left, flex_basis, gap_r, gap_c, text_indent;
    struct wlen bg_w, bg_h;
    struct wlen grid_tracks[GRID_MAX_TRACKS];
    const char *bg_image;
    struct wvar *vars;
    /* SOMA_PARCHE_028_UI */
    uint32_t shadow_color;        /* box-shadow exterior (0 = sin sombra) */
    int16_t shadow_x, shadow_y, shadow_blur, shadow_spread;
    int16_t grid_auto_min;        /* repeat(auto-fit|auto-fill, minmax(N, 1fr)): ancho minimo de pista; las columnas se cuentan al maquetar */
    /* SOMA_181: maquetacion moderna */
    struct wlen tr_x, tr_y;       /* transform/translate: desplazamiento (px + %) del propio elemento */
    uint16_t ar_w, ar_h;          /* aspect-ratio (0 = auto) */
    uint8_t obj_fit, line_clamp;  /* object-fit: 0 fill, 1 contain, 2 cover, 3 none, 4 scale-down; -webkit-line-clamp */
    int8_t obj_pos_x, obj_pos_y;  /* object-position en % (50 = centro) */
    int16_t bg_off_x, bg_off_y;   /* background-position: desplazamiento en px sumado al porcentaje */
    struct wgx *gx;               /* grid-template-rows / -areas / grid-auto-rows */
    const char *garea;            /* grid-area: nombre */
    int16_t gs[2], ge[2], gsp[2]; /* colocacion en rejilla por eje (0 = columna, 1 = fila): linea inicial, linea final, span (0 = auto) */
    uint8_t justify_items, justify_self;   /* 0 stretch, 1 start, 2 end, 3 center */
    uint32_t ph_color;            /* SOMA_188: ::placeholder: color (0 = el gris por defecto) */
    int8_t ph_italic, ph_bold;    /* SOMA_188: ::placeholder: -1 = como el control */
    uint32_t ish_color;           /* SOMA_188: box-shadow inset (0 = sin sombra interior) */
    int8_t ish_x, ish_y, ish_blur, ish_spread;
    uint8_t grid_flow;            /* SOMA_188: grid-auto-flow: bit0 column, bit1 dense */
    uint32_t xsh_c[4];            /* SOMA_188: sombras exteriores 2a..5a de box-shadow (0 = sin) */
    int8_t xsh_x[4], xsh_y[4], xsh_b[4], xsh_s[4];
    uint16_t tr_ms, an_ms;        /* SOMA_188: transition (max duracion+retardo) y animation (duracion*repeticiones+retardo), en ms */
    uint8_t an_inf;               /* animation-iteration-count: infinite */
    const char *tr_prop, *an_name; /* transition-property (lista tal cual; NULL = all) y animation-name */
    uint8_t fs_zero;              /* SOMA_188: font-size: 0 (se guarda 1px para no dividir por cero; el texto no se pinta) */
};

extern int g_wl181_off;   /* SOMA_181 */
extern int g_wc_zoom;     /* SOMA_248_DOC: escala de las unidades absolutas en % (100 = normal) */
extern int (*g_wc_zoom_owner)(void);   /* SOMA_248_DOC: 1 si el hilo actual es el que pidio la escala */
extern int g_wf182_on;    /* SOMA_182 */
extern int g_wc351_off;   /* SOMA_351_ESTILOS_RAPIDOS: 1 = sin filtro de antepasados (solo pruebas) */
struct wcss;                                   /* contexto de estilos del documento */
struct wcss *wc_new(struct wdoc *d);
int wc_subst_vars(const struct wvar *vars, const char *in, char *out, uint32_t max);   /* SOMA_347: var(--x) en un valor */
/* SOMA_348_FUENTES_WEB: reglas @font-face de la pagina. state: 0 sin pedir, 1 pedida, 2 cargada (face = cara de web_font), 3 fallo.
 * want: la usa algun elemento (se pide). latin: su unicode-range cubre el latin basico (o no tiene). */
#define WC_MAXFONT 64
#define WC_MAXFAM  32
struct wcfont { const char *url; int16_t w0, w1; uint8_t fam, italic, state, face, want, latin; };
struct wcfont *wc_fonts(struct wcss *c, int *n);
void wc_free(struct wcss *c);
/* anade una hoja (origin 0 = UA, 1 = autor) */
void wc_add_sheet(struct wcss *c, const char *css, uint32_t len, int origin);
/* @import pendientes: devuelve la siguiente URL pendiente o NULL */
const char *wc_next_import(struct wcss *c);
/* calcula el estilo de todo el arbol para un viewport */
void wc_compute(struct wcss *c, int vw, int vh);
int wc_custom_prop(const struct wstyle *st, const char *name, char *out, uint32_t max);   /* SOMA_188 */
/* devuelve la primera coincidencia / llena un array. */
int wc_query(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only);
int wc_matches(struct wcss *c, struct wnode *n, const char *sel);
/* SOMA_152: 1 si cambiar el elemento bajo el puntero de old a new puede cambiar algun estilo (reglas :hover) */
int wc_hover_affects(struct wcss *c, struct wnode *old_hover, struct wnode *new_hover);
/* estilo en linea */
void wc_set_inline(struct wdoc *d, struct wnode *n, const char *prop, const char *val);
const char *wc_get_inline(struct wnode *n, const char *prop, char *out, uint32_t max);
uint32_t wc_parse_color(const char *s, int *ok);
int wc_line_px(const struct wstyle *s);
int wc_parse_length_px(const char *s, int base);   /* "12px", "1.5em" -> px (em = 16) */
#endif
