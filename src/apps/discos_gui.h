#ifndef SOMA_DISCOS_GUI_H
#define SOMA_DISCOS_GUI_H
/* SOMA_331_DISCOS: la aplicacion Discos (ver discos_gui.c) */
#include <stdint.h>
void discos_gui_init(void);
void discos_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  discos_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  discos_gui_key(char c);                 /* 1 = la ha usado */
int  discos_gui_tick(void);                  /* 1 = hay que repintar (progreso, discos releidos) */
uint32_t discos_gui_version(void);
#endif
