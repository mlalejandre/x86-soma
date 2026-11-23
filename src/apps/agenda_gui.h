/* SOMA_272_AGENDA: ventana del calendario (vistas mes / semana / dia) y avisos activos. */
#ifndef AGENDA_GUI_H
#define AGENDA_GUI_H
#include <stdint.h>
void agenda_gui_init(void);
void agenda_gui_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  agenda_gui_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void agenda_gui_handle_key(char c);
uint32_t agenda_gui_version(void);     /* cambia cuando cambia lo dibujado */
int  agenda_gui_aviso(void);           /* la GUI lo llama en cada vuelta, haya ventana o no: da los avisos (pantalla y voz); 1 = hay que repintar */
void agenda_gui_probar(void);          /* "Probar aviso" de Ajustes */
#endif
