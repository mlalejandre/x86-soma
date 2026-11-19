#ifndef SOMA_FM_H
#define SOMA_FM_H

#include <stdint.h>

void fm_init(void);
void fm_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  fm_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, char *out_path, uint32_t max_out);
void fm_handle_key(char c);
void fm_open_folder(const char *folder);
/* SOMA_PARCHE_028_UI: raton completo (movimiento, rueda, barra, boton derecho); 1 = repintar. fm_escape: 1 = Esc usado */
int  fm_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int left_held, int right_click);
int  fm_escape(void);
/* SOMA_214_ARCHIVOS: varias ventanas (como notepad_inst_*) */
int  fm_inst_new(void);
void fm_select(int slot);
void fm_inst_drop(int slot);
int  fm_inst_find(const char *path);
const char *fm_inst_name(int slot);

#endif
