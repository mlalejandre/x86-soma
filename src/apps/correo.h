#ifndef SOMA_CORREO_H
#define SOMA_CORREO_H
/* SOMA_295_CORREO: la aplicacion Correo (IMAP + SMTP sobre TLS 1.3) con IA y unida al orbe.
 *   - Cuenta: Gmail, Yahoo, iCloud, GMX, Zoho o un servidor propio (contrasena de aplicacion; /etc/correo.conf, sin la clave en claro).
 *   - Carpetas del servidor, lista con extractos, lectura con adjuntos (se guardan en /usuario), responder, responder a todos,
 *     reenviar, destacar, marcar como no leido, mover a la papelera o al archivo. Copia local en /correo para abrir al instante.
 *   - IA: busqueda en lenguaje natural ("el correo donde me hablaban del presupuesto"), resumen de cada correo, respuesta con IA
 *     en el tono que se elija, escribir y mejorar borradores, bandeja "Importantes" clasificada por la IA y "Anadir al calendario".
 *   - El orbe: redacta correos (se envian solo cuando el usuario lo confirma), lee y resume la bandeja y busca correos.
 * Un hilo "correo" hace todo lo que va por la red o al modelo: la ventana nunca se queda esperando. */
#include <stdint.h>

void correo_init(void);
void correo_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  correo_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* 1 = repintar */
int  correo_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held);
void correo_handle_key(char c);
int  correo_tick(void);                 /* 1 = hay algo nuevo que pintar */
uint32_t correo_version(void);
int  correo_clip_op(int op);
int  correo_escape(void);               /* 1 = Esc usado (cierra la redaccion, la busqueda...) */

/* desde el orbe (hilo del agente): 1 = respondido (out tiene la respuesta para el usuario) */
enum { CORREO_ORBE_REDACTA = 1, CORREO_ORBE_ENVIA, CORREO_ORBE_LEE, CORREO_ORBE_BUSCA, CORREO_ORBE_ABRE };
int  correo_orbe(int que, const char *mision, char *out, uint32_t cap);
int  correo_orbe_hay_borrador(void);    /* hay un correo preparado por el orbe esperando el "envialo" */
int  correo_cmd(const char *args, char *out, uint32_t cap);   /* la orden `correo` */
#endif
