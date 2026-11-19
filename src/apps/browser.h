#ifndef SOMA_BROWSER_H
#define SOMA_BROWSER_H

#include <stdint.h>

void browser_init(void);
void browser_load_url(const char *url);
void browser_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  browser_right_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);   /* PARCHE 085 */
int  browser_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void browser_handle_key(char c);
int  browser_is_loading(void);

/* SOMA-JS: Puente DOM & JavaScript */
int         browser_set_element_text_by_id(const char *id, const char *new_text);
const char *browser_get_element_text_by_id(const char *id);
void        browser_js_eval(const char *code);


/* SOMA_PARCHE_NAVEGADOR: hover, rueda y arrastre de la barra de desplazamiento (opcional; lo llama gui.c) */
int  browser_frame_dirty(uint32_t win_w, uint32_t win_h);   /* SOMA_152_WEB_FLUIDO */
int  browser_hot(void);
int  browser_loading(void);   /* SOMA_190 */
void browser_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held);

#endif
