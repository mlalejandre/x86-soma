#ifndef SOMA_CODER_APP_H
#define SOMA_CODER_APP_H

#include <stdint.h>

void coder_app_init(void);
void coder_app_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int is_focused);
int  coder_app_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void coder_app_handle_key(char c);
void coder_app_notify_thought(const char *txt);
void coder_app_load_project(const char *path);
int  coder_app_escape(void);
int  coder_run_mission(const char *target, const char *mission, char *out, uint32_t max);   /* PARCHE 097 */
void coder_pick_target(const char *mission, char *out, uint32_t cap);                                  /* PARCHE 093 */
int  coder_app_clip_op(int op);
int  coder_app_mouse(int32_t mx, int32_t my, int wheel, int held);

/* SOMA_217_STUDIO: varias ventanas (como notepad_inst_*) */
int  coder_app_inst_new(void);
void coder_app_select(int slot);
void coder_app_inst_drop(int slot);
int  coder_app_inst_find(const char *path);
const char *coder_app_inst_name(int slot);
void coder_app_open_path(const char *path);
/* SOMA_290_VOZ_CODIGO: si no es NULL, coder_run_mission le pasa cada fase (1 analizar, 2 generar, 4 reparar, 5 revisar) */
extern void (*coder_narrador)(int fase, const char *desc, const char *detalle);
#endif
