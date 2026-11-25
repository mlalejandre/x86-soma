#ifndef SOMA_MOUSE_H
#define SOMA_MOUSE_H

#include <stdint.h>

enum soma_cursor_id {
    CURSOR_ARROW     = 0,
    CURSOR_CROSSHAIR = 1,
    CURSOR_HAND      = 2,
    CURSOR_RESIZE_H  = 3,    /* PARCHE 085: punteros vectoriales para redimensionar, mover y texto */
    CURSOR_RESIZE_V  = 4,
    CURSOR_RESIZE_NWSE = 5,
    CURSOR_RESIZE_NESW = 6,
    CURSOR_MOVE      = 7,
    CURSOR_TEXT      = 8,
    CURSOR_COUNT     = 9
};

void mouse_init(void);
void mouse_shutdown(void);
int  mouse_poll(int32_t *out_dx, int32_t *out_dy, int *out_left_click);
int  mouse_set_speed(int pct);   /* SOMA_225_AJUSTES: velocidad del puntero en % (25..300); devuelve la aplicada */
int  mouse_left_held(void);
/* SOMA_PARCHE_024_TERM: rueda y boton derecho */
void mouse_inject(int32_t dx, int32_t dy, uint8_t buttons, int wheel);   /* PARCHE 047: raton USB (x86_64) */
int  mouse_wheel_take(void);        /* muescas acumuladas desde la ultima llamada (positivo = hacia arriba) */
int  mouse_right_take_click(void);  /* 1 una vez por cada pulsacion del boton derecho */
void mouse_draw_cursor(int32_t mx, int32_t my);
void mouse_erase_cursor(void);
int  point_in_rect(int32_t px, int32_t py, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh);

/* Control dinámico de puntero */
int         mouse_set_cursor(int cursor_id);
int         mouse_set_cursor_by_name(const char *name);
int         mouse_get_current_cursor(void);
const char *mouse_get_cursor_name(int cursor_id);
const char *mouse_get_cursor_desc(int cursor_id);
int         mouse_get_cursor_count(void);

#endif
