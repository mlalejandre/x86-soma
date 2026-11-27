/* SOMA_359_HOJA_VENTANA: la ventana de la hoja de calculo: cuadricula con formato, barra de formulas, pestanas de hojas, menus,
 * seleccion con raton y teclado, rellenar arrastrando, ancho de columnas, inmovilizar, ordenar, filtrar, buscar y reemplazar,
 * deshacer/rehacer, copiar y pegar (tambien tablas que vienen de la web o del correo). Trabaja con el libro actual de hoja.c. */
#ifndef HOJA_GUI_H
#define HOJA_GUI_H
#include <stdint.h>
void hoja_gui_init(void);
void hoja_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  hoja_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* 0 = nada mas */
int  hoja_gui_key(char c);                      /* 1 = tecla usada; 0 = Esc sin nada abierto (cerrar la ventana) */
/* rueda, arrastres y boton derecho (rclick = menu de la celda). 1 = repintar */
int  hoja_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held, int rclick);
int  hoja_gui_tick(void);                       /* 1 = repintar (avisos que caducan, cambios hechos por la orden `hoja` o el agente) */
uint32_t hoja_gui_version(void);
int  hoja_gui_clip_op(int op);                  /* Ctrl+C / Ctrl+X / Ctrl+V (CLIP_OP_*) */
#endif
