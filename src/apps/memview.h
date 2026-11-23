#ifndef SOMA_MEMVIEW_H
#define SOMA_MEMVIEW_H
/* SOMA_283_MEMORIA_APP: la aplicacion de Memoria. Consola de la memoria de SOMA (portada de memorIA): monitor del ciclo de sueno en vivo,
 * explorador del arbol de temas (ver, editar, mover, archivar y borrar recuerdos; crear, renombrar y borrar ramas), busqueda por significado,
 * gestor de cartuchos (crear, conectar, primario, copiar y mover recuerdos entre ellos, clonar, exportar, importar, vaciar, borrar) y ajustes. */
#include <stdint.h>

void memview_init(void);
void memview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  memview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);   /* 4 = abrir memview_get_selected_path() en Notas */
void memview_handle_key(char c);
const char *memview_get_selected_path(void);
int  memview_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held);   /* 1 = repintar */
int  memview_tick(void);                 /* 1 = hay datos nuevos (la GUI la llama en cada vuelta) */
uint32_t memview_version(void);          /* cambia cuando cambia lo dibujado */
int  memview_clip_op(int op);            /* copiar/cortar/pegar en el campo con el foco */

#endif
