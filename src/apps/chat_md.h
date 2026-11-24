#ifndef SOMA_CHAT_MD_H
#define SOMA_CHAT_MD_H
/* SOMA_285_CHAT: maquetador de Markdown para la aplicacion de chat. Convierte el texto de un mensaje (lo que escriben los modelos:
 * Markdown al estilo GitHub con LaTeX y algo de HTML) en una lista de operaciones de dibujo con coordenadas relativas al mensaje,
 * mas zonas activas (enlaces, botones de copiar codigo, imagenes). No dibuja ni toca el nucleo: pide medidas a quien llama
 * (md_metrics) y se prueba en el anfitrion (tests/test_chat_285.c).
 *
 * Bloques: titulos # a ######, parrafos, saltos duros (dos espacios, \ o <br>), citas > (anidadas), listas - * + y 1. 1) (anidadas,
 * casillas [ ] y [x]), bloques de codigo ``` / ~~~ con lenguaje, resaltado de sintaxis y boton Copiar, tablas GFM con alineacion
 * (:--, :-:, --:), reglas ---, formulas $$..$$, \[..\] y \begin{equation|align|...}, imagenes ![alt](ruta o URL).
 * En linea: **negrita**, *cursiva*, ***las dos***, __ y _, ~~tachado~~, `codigo`, [enlace](url), <url>, URL sueltas, $formula$ y
 * \(formula\) (con el motor LaTeX de SOMA, tex.c), escapes \*, entidades &amp; &lt; &#233; ..., y las etiquetas <b> <strong> <i> <em>
 * <u> <s> <del> <code> <kbd> <sup> <sub> <a href> <br> <mark>; las demas etiquetas se quitan y su texto queda. */
#include <stdint.h>

struct md_metrics {
    int (*width)(const char *s, int n, int px, int fl, void *u);       /* fl: MD_F_* */
    int (*ascent)(int px, void *u);
    int (*descent)(int px, void *u);
    int (*image)(const char *src, int n, int *w, int *h, void *u);      /* tamano natural de una imagen; 0 = conocida, 1 = cargando, -1 = error */
    void *u;
};
#define MD_F_BOLD   1          /* = WF_BOLD */
#define MD_F_ITALIC 2          /* = WF_ITALIC */
#define MD_F_MONO   4          /* = WF_MONO */

struct md_theme {
    uint32_t fg, muted, head, link, rule;
    uint32_t code_bg, code_hd, code_bd, code_fg, inl_bg, inl_fg, quote, mark_bg;
    uint32_t th_bg, tr_alt, check;
    uint32_t kw, str, num, com, fn, typ, err;
};

enum { MD_TEXT = 0, MD_RECT, MD_LINE, MD_IMG, MD_STROKE };
struct md_op {
    uint8_t type, fl; int16_t px;          /* TEXT: estilo y tamano; RECT/STROKE: px = radio; LINE: px = grosor en 1/16 px */
    int32_t x, y, w, h;                    /* TEXT: (x, linea base); RECT/STROKE/IMG: caja; LINE: de (x,y) a (x+w,y+h) en 1/16 px */
    uint32_t rgb;                          /* RECT: alfa en el byte alto (0 = opaco) */
    uint32_t s, n;                         /* TEXT: texto en el pool; IMG: origen (ruta o URL) en el pool */
    int16_t zone;                          /* zona a la que pertenece (-1 = ninguna): para resaltar enlaces al pasar el raton */
};
enum { MDZ_LINK = 1, MDZ_COPY, MDZ_IMG };
struct md_zone { uint8_t kind; int32_t x, y, w, h; uint32_t s, n; };   /* LINK: URL; COPY: el codigo; IMG: origen */

struct md_out {
    struct md_op *ops; int nops, cap;
    char *pool; uint32_t npool, poolcap;
    struct md_zone *z; int nz, zcap;
    int height, width;                     /* alto total y ancho realmente usado (la linea mas larga) */
    int full;                              /* algo no cupo */
    int blocks, maths, codes, tables, images, links;
};

/* Maqueta src en una columna de width px con letra base_px. Devuelve el alto. scratch: memoria de trabajo (MD_SCRATCH bytes). */
#define MD_SCRATCH (640u * 1024u)
int md_layout(const char *src, uint32_t len, int width, int base_px, const struct md_metrics *m, const struct md_theme *t,
              struct md_out *o, void *scratch);
/* Resaltado de sintaxis de una linea (lo usa md_layout y se prueba aparte): clases por byte (MDK_*). Devuelve el estado
 * (dentro de un comentario de bloque) para la linea siguiente. */
enum { MDK_TXT = 0, MDK_KW, MDK_STR, MDK_NUM, MDK_COM, MDK_FN, MDK_TYP };
int md_resalta(const char *lang, int nlang, const char *s, int n, uint8_t *cls, int estado);
#endif
