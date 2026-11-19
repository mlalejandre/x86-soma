#ifndef SOMA_GUI_FONT_H
#define SOMA_GUI_FONT_H

#include <stdint.h>

enum soma_font_id {
    FONT_CLASSIC             = 0,
    FONT_MODERN              = 1,
    FONT_BOLD                = 2,
    FONT_MODERN_BOLD         = 3,
    FONT_ITALIC              = 4,
    FONT_MODERN_ITALIC       = 5,
    FONT_CAPS                = 6,
    FONT_MODERN_CAPS         = 7,
    FONT_BOLD_ITALIC         = 8,
    FONT_MODERN_BOLD_ITALIC  = 9,
    FONT_CLASSIC_ZERO        = 10,
    FONT_MODERN_HEAVY        = 11,
    FONT_BACKSLANT           = 12,
    FONT_MODERN_BACKSLANT    = 13,
    FONT_LOWER               = 14,
    FONT_MODERN_LOWER        = 15,
    FONT_HYBRID              = 16,
    FONT_HYBRID_ALT          = 17,
    FONT_ROUNDED             = 18,
    FONT_ROUNDED_ITALIC      = 19,
    FONT_LCD                 = 20,
    FONT_LCD_ITALIC          = 21,
    FONT_LCD_BOLD            = 22,
    FONT_SEGOE               = 23,
    FONT_ARIAL               = 24,
    FONT_VERDANA             = 25,
    FONT_TAHOMA              = 26,
    FONT_CONSOLAS            = 27,
    FONT_GEORGIA             = 28,
    FONT_TREBUCHET           = 29,
    FONT_UBUNTU              = 30,
    FONT_COMIC               = 31,
    FONT_VECTOR              = 32,     /* TrueType: DejaVu Sans Mono */
    FONT_DEJAVU_SANS         = 33,     /* SOMA_184: TrueType proporcional (la de la web y la interfaz) */
    FONT_LIBERATION          = 34,     /* SOMA_184: Liberation Sans (anchos de Arial) */
    FONT_LIBERATION_BOLD     = 35,     /* SOMA_184: Liberation Sans Negrita */
    FONT_COUNT               = 36
};

/* SOMA_184: caras TrueType del catalogo. Son las mismas que usa el navegador (una sola copia de cada fuente en el kernel). */
enum soma_font_face {
    FFACE_MONO       = 0,   /* DejaVu Sans Mono (la del terminal y el editor) */
    FFACE_DEJAVU     = 1,   /* DejaVu Sans */
    FFACE_LIBERATION = 2,   /* Liberation Sans */
    FFACE_LIBERATION_BOLD = 3,
    FFACE_COUNT      = 4
};
int         font_face_of(int font_id);            /* cara TrueType de una entrada del catalogo (-1 = fuente de mapa de bits) */
int         font_face_catalog_id(int face);       /* entrada del catalogo que corresponde a una cara */
const char *font_face_name(int face);             /* nombre para mostrar ("Liberation Sans") */
int         font_sysfont_off(void);               /* 1 con soma.sysfont=off (o modo seguro): sin las fuentes TrueType anadidas al catalogo */
int         font_vec_em(int face, int em);        /* tamano em equivalente en esa cara para una rejilla pensada para la mono */
void        font_vec_draw_face(int face, int32_t x, int32_t y, uint32_t cp, uint32_t fg, int em, int cell_w, int baseline);

/* Control de selección de fuente activa */
int         font_set(int font_id);
int         font_set_by_name(const char *name);
int         font_get_current(void);
const char *font_get_name(int font_id);
const char *font_get_desc(int font_id);
int         font_get_count(void);
int         font_get_sorted_id(int order); /* posición alfabética -> ID real */

/* Renderizado con la fuente activa */
const uint8_t *font_get_glyph(char c);
void font_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, int scale);
void font_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, int scale);
void font_draw_string_ansi(uint32_t x, uint32_t y, const char *str, uint32_t def_color);


/* SOMA_PARCHE_024_TERM: para el terminal (rejilla fija, tamano propio, independiente de la fuente global) */
int  font_vec_available(void);
void font_vec_draw(int32_t x, int32_t y, uint32_t cp, uint32_t fg, int em, int cell_w, int baseline);
void font_bitmap_cell(int32_t x, int32_t y, const char *seq, uint32_t fg, int scale);

/* ==== SOMA_VECTOR_FONTS_PATCH: fuentes e iconos vectoriales ==== */
int  font_vector_set_size(int percent);      /* 40..300 (100 = normal)  */
int  font_vector_get_size(void);
int  font_vector_set_contrast(int percent);  /* 0..100                  */
int  font_vector_get_contrast(void);

enum soma_vicon {
    VICON_AGENT    = 0,
    VICON_AGENTS   = 1,
    VICON_TERMINAL = 2,
    VICON_BROWSER  = 3,
    VICON_FILES    = 4,
    VICON_IMAGE    = 5,
    VICON_NOTES    = 6,
    VICON_PAINT    = 7,
    VICON_LAYOUT   = 8,
    VICON_MEMORY   = 9,
    VICON_COUNT    = 10
};

/* Dibuja un icono vectorial (size 8..192 px) tintado con 'color' */
void vicon_draw(int id, int32_t x, int32_t y, int32_t size, uint32_t color);
/* Igual, sobre una baldosa redondeada de color 'bg' */
void vicon_draw_tile(int id, int32_t x, int32_t y, int32_t size, uint32_t fg, uint32_t bg);


/* ==== SOMA_VECTOR_CELL_PATCH ====
 * Dibuja una celda del escritorio HER (caracter base + marca diacritica:
 * 0 ninguna, 1 agudo, 2 tilde, 3 dieresis, 4 invertido) con la fuente
 * vectorial. Devuelve 1 si la dibujo, 0 si la fuente activa no es vectorial
 * (el llamador debe usar entonces su camino bitmap). */
int font_draw_cell_vector(int32_t x, int32_t y, uint8_t base, uint32_t mark,
                          uint32_t col, int sc);

#endif
