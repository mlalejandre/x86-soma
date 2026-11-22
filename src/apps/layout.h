#ifndef SOMA_LAYOUT_H
#define SOMA_LAYOUT_H

#include <stdint.h>

void layout_init(void);
void layout_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  layout_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void layout_handle_key(char c);
int  layout_escape(void);               /* SOMA_254: Esc suelta el campo de texto (1 = lo ha usado; 0 = cerrar la ventana) */
int  layout_clip_op(int op);            /* SOMA_254: Ctrl+C / Ctrl+X / Ctrl+V (CLIP_OP_*): campo con foco, o copia el codigo fuente */

/* Carga un documento LaTeX (PARCHE 084) o #SOMA-LAYOUT v1 (se convierte a LaTeX) */
void layout_set_source(const char *src, uint32_t len);

/* Devuelve el prompt que el usuario ha escrito para el subagente maquetador */
/* Paso 2: Generar / Enter invocan agent_run("maquetador") y recargan el lienzo */
const char *layout_get_prompt(void);

/* PARCHE 084: abrir un .tex, rueda y arrastre de la barra de desplazamiento (1 = hay que redibujar) */
void layout_open_file(const char *path);
int  layout_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held);

#endif
