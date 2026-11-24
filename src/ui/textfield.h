#ifndef SOMA_TEXTFIELD_H
#define SOMA_TEXTFIELD_H
/* PARCHE 099: campo de texto reutilizable (una linea o varias con ajuste de linea).
 * Cursor, seleccion con raton (clic, arrastre, doble clic = palabra, triple = todo) y con Mayus+flechas, Ctrl+A, Inicio/Fin,
 * Supr, Retroceso, UTF-8, portapapeles del sistema, desplazamiento horizontal y marcador de texto. Fuente monoespaciada para que
 * cursor y seleccion caigan exactos. */
#include <stdint.h>

#define TF_NONE   0
#define TF_EDIT   1      /* tecla consumida / texto cambiado */
#define TF_ENTER  2      /* Intro en un campo de una linea (o Ctrl+Intro en uno multilinea) */
#define TF_TAB    3      /* Tab: el que aloja el campo decide a donde va el foco */
#define TF_ESC    4

struct tfield {
    char     *buf;
    uint32_t  cap;
    uint32_t  cur, anc;          /* cursor y ancla de la seleccion (bytes) */
    int       multiline;
    int       scroll;            /* px (una linea) o linea superior (multilinea) */
    int       x, y, w, h;        /* ultimo rectangulo pintado */
    int       dragging;
    uint64_t  last_click; uint32_t last_pos; int clicks;
    uint8_t   u8[4]; int u8n, u8need;
    int       readonly;
    int       bare;              /* SOMA_285_CHAT: sin fondo ni borde (lo pinta quien aloja el campo) */
};

void tf_init(struct tfield *t, char *buf, uint32_t cap, int multiline);
void tf_set(struct tfield *t, const char *text);              /* reemplaza todo, cursor al final */
void tf_select_all(struct tfield *t);
int  tf_len(const struct tfield *t);
void tf_render(struct tfield *t, int x, int y, int w, int h, int focused, const char *placeholder, uint32_t text_col);
int  tf_hit(const struct tfield *t, int mx, int my);
void tf_press(struct tfield *t, int mx, int my, int shift);
int  tf_drag(struct tfield *t, int mx, int my, int held);    /* 1 = redibujar */
int  tf_key(struct tfield *t, char c, int ctrl, int shift);
int  tf_clip(struct tfield *t, int op);                       /* CLIP_OP_* */
/* SOMA_285_CHAT */
int  tf_lines(struct tfield *t, int w);                       /* lineas que ocupa el texto con ese ancho (multilinea) */
void tf_insert(struct tfield *t, const char *s, uint32_t n);  /* inserta en el cursor (cualquier longitud que quepa) */
int  tf_line_h(void);                                         /* alto de una linea en px */
#endif
