#ifndef SOMA_IMGVIEW_H
#define SOMA_IMGVIEW_H

#include <stdint.h>

void imgview_init(void);
void imgview_load_file(const char *path);
void imgview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  imgview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void imgview_handle_key(char c);
int  imgview_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held);   /* PARCHE 088 */
int  imgview_clip_op(int op);
/* SOMA_212_IMAGENES: varias ventanas (como notepad_inst_*) */
int  imgview_inst_new(void);
void imgview_select(int slot);
void imgview_inst_drop(int slot);
int  imgview_inst_find(const char *path);
const char *imgview_inst_name(int slot);

#endif
