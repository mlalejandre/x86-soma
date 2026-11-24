#ifndef SOMA_PAINT_H
#define SOMA_PAINT_H

#include <stdint.h>

void paint_init(void);
void paint_ensure(void);          /* PARCHE 083: crea el lienzo solo si no existe */
void paint_open_file(const char *path);
void paint_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  paint_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void paint_handle_mouse(int32_t mx, int32_t my, int is_down, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void paint_handle_key(char c);
/* SOMA_215_PAINT: varias ventanas (como notepad_inst_*) */
int  paint_inst_new(void);
void paint_select(int slot);
void paint_inst_drop(int slot);
int  paint_inst_find(const char *path);
const char *paint_inst_name(int slot);
int  paint_inst_dirty(int slot);                 /* SOMA_219_AVISO: 1 = cambios sin guardar */

/* SOMA_292_PAINT_IA: dibujar con el modelo de lenguaje (boton IA de la barra, o desde el orbe).
 * paint_ia_encargar: en la ventana de Paint activa; 0 = en marcha, -1 = ya hay otro dibujo. paint_ia_estado: 0 pensando, 2 pintando,
 * 1 dibujado (msg = el tema), -1 fallo (msg = el motivo), 0 sin encargo. paint_animando: hay que repintar la ventana activa de Paint
 * (pasos del dibujo, aviso de espera). paint_ia_slot: la ventana (instancia) del encargo en curso, o -1. */
int  paint_ia_encargar(const char *pedido);
int  paint_ia_estado(char *msg, uint32_t cap);
int  paint_animando(void);
int  paint_ia_slot(void);
#endif
