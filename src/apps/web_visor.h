#ifndef SOMA_WEB_VISOR_H
#define SOMA_WEB_VISOR_H
/* SOMA_289_VISOR: visor de documentos dentro del navegador (una pestana muestra un PDF o un documento LaTeX como Chrome muestra
 * un PDF). El motor web crea una pagina marcador (web_tipo.h); el navegador, al verla, pone este visor en la zona de la pagina.
 *   PDF   : el lector de PDF de SOMA (pdfview) en una instancia propia: paginas, zoom, miniaturas, copiar texto.
 *   LaTeX : el motor tex.c en una hoja de papel con desplazamiento suave; boton Codigo para ver el fuente, letra mas grande o
 *           mas pequena, copiar el codigo y abrirlo en la Maquetacion.
 * Hilos: wv_sync, wv_pinta, wv_click, wv_raton y wv_tecla en el de la GUI; wv_descarga en el trabajador del navegador. */
#include <stdint.h>

/* La GUI, en cada fotograma, con el titulo y la URL de la pagina viva. 1 = hay que descargar (encolar wv_descarga). */
int  wv_sync(const char *titulo_pagina, const char *url);
void wv_descarga(void);                                  /* hilo trabajador: baja o lee el documento pedido */
int  wv_activo(void);                                    /* el visor ocupa la zona de la pagina */
void wv_cierra(void);
void wv_pinta(int x, int y, int w, int h);
int  wv_click(int mx, int my, int x, int y, int w, int h);
int  wv_raton(int mx, int my, int x, int y, int w, int h, int wheel, int held);   /* 1 = repintar */
int  wv_tecla(char c);                                   /* 1 = la ha usado */
uint64_t wv_huella(void);                                /* cambia cuando hay que repintar */
const char *wv_titulo(void);                             /* para la pestana ("" = el de la pagina) */
#endif
