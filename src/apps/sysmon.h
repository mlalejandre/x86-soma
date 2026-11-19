#ifndef SOMA_SYSMON_H
#define SOMA_SYSMON_H
#include <stdint.h>
/* PARCHE 089: Monitor del sistema */
void sysmon_init(void);
void sysmon_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  sysmon_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* 2 = se cerro una ventana */
void sysmon_handle_key(char c);
int  sysmon_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel);
uint32_t sysmon_version(void);    /* SOMA_216_RENDIMIENTO: cambia cuando cambia lo que se dibuja */
int  sysmon_tick(void);            /* 1 = hay datos nuevos (repintar); lo llama la GUI en cada vuelta */
#endif
