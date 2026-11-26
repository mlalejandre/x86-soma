#ifndef SOMA_AGENTES_GUI_H
#define SOMA_AGENTES_GUI_H
/* SOMA_338_AGENTES: la aplicacion Agentes (ver agentes_gui.c) */
#include <stdint.h>
void agentes_gui_init(void);
void agentes_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  agentes_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  agentes_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel);   /* 1 = repintar */
int  agentes_gui_key(char c);                   /* 0 = no la usa (Esc cierra la ventana) */
int  agentes_gui_tick(void);                    /* 1 = repintar */
uint32_t agentes_gui_version(void);
#endif
