#ifndef SOMA_RECORDER_H
#define SOMA_RECORDER_H

#include <stdint.h>

/* SOMA_177_T480S: aplicacion Grabadora (ventana de la GUI). Graba por el microfono (micrec.h), muestra el nivel y la
 * onda en vivo, reproduce, guarda como WAV en /grabaciones del almacen BIGFS y lista/carga/borra las que hay. */
void recorder_init(void);
void recorder_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  recorder_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void recorder_handle_key(char c);
int  recorder_tick(void);                    /* 1 = hay que repintar (nivel, tiempo, cambio de estado) */

#endif
