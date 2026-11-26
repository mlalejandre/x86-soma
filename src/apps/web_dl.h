/* SOMA_343_DESCARGAS: descargas en paralelo en el proceso del navegador (ring 3, src/user/web_dl.c).
 * El proceso no tiene hilos: cada descarga corre en una corrutina con pila propia. El codigo de red no cambia (sigue escrito "en
 * bloqueo"): cuando espera a la red (unet_idle, web_yield) cede al bucle principal, que va dando turnos a las descargas vivas.
 * Solo en x86_64 y solo en el proceso del navegador (SOMA_WEBSVC). */
#ifndef SOMA_WEB_DL_H
#define SOMA_WEB_DL_H

#define WDL_N 4                    /* descargas a la vez (el nucleo tiene 16 conexiones TCP para todo el sistema) */

int  wdl_start(void (*fn)(void *), void *arg);   /* 0 = en marcha; -1 = no hay hueco o no hay memoria (hazlo en sincrono) */
void wdl_pump(void);                             /* un turno a cada descarga viva (no hace nada si se llama desde una descarga) */
int  wdl_live(void);                             /* descargas en marcha */
int  wdl_inside(void);                           /* 1 = estamos dentro de una descarga */
void wdl_yield(void);                            /* desde una descarga: cede al bucle principal */

#endif
