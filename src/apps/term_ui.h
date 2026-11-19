#ifndef SOMA_TERM_UI_H
#define SOMA_TERM_UI_H

#include <stdint.h>

/* SOMA_PARCHE_024_TERM: interfaz del emulador de terminal de la GUI: desplazamiento con barra, seleccion con el raton,
 * portapapeles, menu contextual, barra de herramientas, zoom del texto y paleta de simbolos. El estado de la edicion de la
 * linea de ordenes sigue en la GUI (buf/len/cur); aqui solo se lee y se modifica a traves de struct term_edit. */
struct term_edit { char *buf; uint32_t *len; uint32_t *cur; uint32_t cap; };

/* SOMA_203_INSTANCIAS: contexto de salida de cada terminal. */
int term_ui_select(int slot); /* 0 por defecto; 1..15 sesiones independientes */
void term_ui_drop(int slot);

void term_ui_init(void);                          /* vacia el historial visible y activa los valores por defecto */
void term_print(const char *raw_line);            /* anade una linea (admite codigos ANSI de color); las largas se parten */
void term_clear(void);
void term_note_input(void);                       /* el usuario ha escrito algo: vuelve al final y quita la seleccion */

void term_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *cmd, uint32_t cmd_len, uint32_t cursor_pos, int focused);

/* Eventos de raton (coordenadas de pantalla). Devuelven 1 si hay que redibujar. */
int  term_mouse_down(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, struct term_edit *ed);
int  term_mouse_move(int32_t mx, int32_t my, int left_held);
int  term_mouse_up(int32_t mx, int32_t my);
int  term_mouse_right(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  term_wheel(int notches);                     /* positivo = hacia arriba */
int  term_key(char c, struct term_edit *ed);      /* teclas propias del terminal; 1 = tratada */
int  term_popup_open(void);                       /* hay un menu o la paleta abierta (Esc lo cierra) */
void term_popup_close(void);

void term_set_debug(int on);                      /* imprime por serie la geometria y las acciones de copiar/pegar (pruebas) */

#endif
