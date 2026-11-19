#ifndef SOMA_NOTEPAD_H
#define SOMA_NOTEPAD_H

#include <stdint.h>

/* Ciclo de vida y carga */
void notepad_init(void);
void notepad_load_file(const char *path);

/* Renderizado principal */
void notepad_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);

/* Control de eventos de ratón (clic izquierdo, clic derecho, arrastre y soltar) */
int  notepad_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
int  notepad_handle_right_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
int  notepad_handle_drag(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
int  notepad_handle_mouse_up(int32_t mx, int32_t my);
/* SOMA_PARCHE_028_UI: raton completo con las coordenadas reales del puntero (movimiento, arrastre, rueda, boton derecho).
 * Devuelve 1 si hay que repintar. notepad_escape() cierra el menu o el dialogo abiertos (1 = tecla usada). */
int  notepad_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h,
                   int wheel, int left_held, int right_click);
int  notepad_escape(void);

/* Control de teclado */
void notepad_handle_key(char c);

/* SOMA_PARCHE_LIVE_V1: Volcado en directo por la IA */
void     notepad_live_begin(const char *path, const char *data, uint32_t len);
uint32_t notepad_live_step(uint32_t chunk);
void     notepad_live_end(void);

/* SOMA_211_NOTAS: varias ventanas. notepad_inst_new() crea una instancia y la deja elegida (0 = no hay sitio); notepad_select()
 * elige la de una ventana; notepad_inst_drop() la libera al cerrar; notepad_inst_find() busca la que tiene abierto un archivo. */
int  notepad_inst_new(void);
void notepad_select(int slot);
void notepad_inst_drop(int slot);
int  notepad_inst_find(const char *path);
const char *notepad_inst_name(int slot);
int  notepad_inst_dirty(int slot);

/* Operaciones de portapapeles y navegador */
int  notepad_clip_op(int op);
void notepad_open_in_browser(const char *path);

#endif
