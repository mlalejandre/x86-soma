/* SOMA_357_CONTACTOS: la ventana Contactos: lista con buscador y grupos, ficha completa editable, fotos del contacto y muestra de voz. */
#ifndef CONTACTOS_GUI_H
#define CONTACTOS_GUI_H
#include <stdint.h>
void contactos_gui_init(void);
void contactos_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
/* 0 = nada mas; 2 = abrir la Camara (para hacer una foto al contacto); 3 = escribir un correo (contactos_gui_email()); 4 = abrir la Hoja (SOMA_360) */
int  contactos_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  contactos_gui_key(char c);                 /* 1 = tecla usada; 0 = Esc sin nada abierto (cerrar la ventana) */
int  contactos_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held);   /* 1 = repintar */
int  contactos_gui_tick(void);                  /* 1 = repintar (voz, fotos que llegan, miniaturas pendientes) */
uint32_t contactos_gui_version(void);
const char *contactos_gui_email(void);          /* el email al que escribir (tras devolver 3) */
void contactos_gui_ver(uint32_t id);            /* abre la ficha de ese contacto */
#endif
