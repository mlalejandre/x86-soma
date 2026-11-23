/* SOMA_274_JS: el nucleo del programa "js" de SOMA (JavaScript para aplicaciones de consola y de ventana).
 * Ejecuta un guion con el mismo motor del navegador (web_js_*), sin pagina: console, input(), fs, win, temporizadores y promesas.
 * No toca el sistema: todo lo de fuera entra por jsr_io, asi que se prueba en el anfitrion y se enlaza en ring 3 (src/user/jsrun.c). */
#ifndef SOMA_JSRUN_CORE_H
#define SOMA_JSRUN_CORE_H
#include <stdint.h>

struct jsr_io {
    void *ud;
    void (*out)(void *ud, const char *s, uint32_t n);                         /* salida de la consola */
    int  (*readline)(void *ud, char *buf, uint32_t max);                      /* >= 0 longitud, -1 no hay mas entrada */
    int  (*readfile)(void *ud, const char *path, char **out, uint32_t *len);  /* 0 = bien; *out con kmalloc */
    int  (*writefile)(void *ud, const char *path, const char *d, uint32_t n); /* 0 = bien */
    uint64_t (*now)(void *ud);                                                /* ms monotonicos */
    uint64_t (*epoch)(void *ud);                                              /* ms desde 1970 (0 = usar now) */
    void (*sleep)(void *ud, uint32_t ms);
    int  (*stopped)(void *ud);                                                /* 1 = el usuario quiere parar */
    /* ventana de la aplicacion (NULL = no hay escritorio) */
    int  (*win_create)(void *ud, const char *title, uint32_t tl, int w, int h);   /* 1 = abierta */
    void (*win_close)(void *ud);
    void (*win_present)(void *ud);
    void (*win_state)(void *ud, int64_t st[8], int take_click);               /* st: activa, ancho, alto, x, y, boton, dentro, clic */
    int  (*win_key)(void *ud);
    void (*win_draw)(void *ud, const int32_t *words, uint32_t nwords);       /* lote de ordenes (JSR_G_*) */
};
enum { JSR_G_CLEAR = 1, JSR_G_PIXEL, JSR_G_RECT, JSR_G_LINE, JSR_G_CIRC, JSR_G_TEXT };   /* los mismos codigos que soma.win de Python */

#define JSR_OK        0
#define JSR_ERR_JS    1      /* excepcion o error de sintaxis (el mensaje ya salio por io->out) */
#define JSR_ERR_MEM   2
#define JSR_ERR_STOP  3      /* detenido por el usuario */
#define JSR_ERR_EXIT  100    /* exit(n) devuelve 100 + n (n 0..99) */

/* Ejecuta src. check = 1: solo comprueba la sintaxis (no ejecuta nada). Devuelve JSR_*. */
int jsrun_run(const struct jsr_io *io, const char *src, uint32_t len, const char *name, int check);
#endif
