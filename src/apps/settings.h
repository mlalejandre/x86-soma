#ifndef SOMA_SETTINGS_H
#define SOMA_SETTINGS_H
/* PARCHE 085: Ajustes de la interfaz (de momento: transparencia de las ventanas y fuente). Se guardan en /etc/ui.conf. */
#include <stdint.h>
void settings_load(void);                   /* lee /etc/ui.conf y aplica (al arrancar la GUI) */
int  settings_opacity(void);                /* 40..100 % */
int  settings_active_opaque(void);          /* 1 = la ventana activa no se transparenta */
void settings_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focus);
/* devuelven 1 si cambio la apariencia (la GUI debe repintar todas las ventanas) */
int  settings_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  settings_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int held);
int  settings_wheel(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel);   /* SOMA_227_IMAGEN: rueda */
int  settings_handle_key(char c);
int  settings_editor_face(void);            /* SOMA_184: cara del editor de texto (FFACE_*, 0 = DejaVu Sans Mono) */
int  settings_tick(void);                   /* SOMA_175C: 1 = la pagina Redes cambio (la GUI repinta) */
#endif
