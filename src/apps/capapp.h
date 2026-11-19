#ifndef SOMA_CAPAPP_H
#define SOMA_CAPAPP_H
/* SOMA_179_T480S: aplicacion Captura (imagen, video, audio interno y video con audio de la pantalla). Ver scrcap.h */
#include <stdint.h>
void capapp_init(void);
void capapp_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused);
int  capapp_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh);
void capapp_handle_key(char c);
int  capapp_tick(void);          /* 1 = hay que repintar */
#endif
