#ifndef SOMA_CAMAPP_H
#define SOMA_CAMAPP_H
#include <stdint.h>
/* SOMA_178_T480S: aplicacion Camara (ventana de la GUI): vista previa en vivo, foto JPEG y grabacion de video AVI con audio. */
void camapp_init(void);
void camapp_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  camapp_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void camapp_handle_key(char c);
int  camapp_tick(void);                      /* 1 = hay que repintar (imagen nueva, tiempo, estado) */
void camapp_close(void);                     /* al cerrar la ventana: para la camara */
#endif
