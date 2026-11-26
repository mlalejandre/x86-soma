#ifndef SOMA_NOTICIAS_GUI_H
#define SOMA_NOTICIAS_GUI_H
/* SOMA_337_NOTICIAS: ventana de la aplicacion de Noticias (ver noticias.h) */
#include <stdint.h>
void noticias_gui_init(void);
void noticias_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  noticias_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* 3 = abrir noticias_gui_url() */
int  noticias_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel);   /* 1 = repintar */
int  noticias_gui_key(char c);                  /* 1 = usada */
int  noticias_gui_tick(void);                   /* 1 = repintar */
uint32_t noticias_gui_version(void);
const char *noticias_gui_url(void);
#endif
