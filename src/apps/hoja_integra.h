/* SOMA_360_HOJA_GRAFICOS: la hoja de calculo con Contactos, Calendario, Correo y el agente (ver hoja_integra.c). */
#ifndef HOJA_INTEGRA_H
#define HOJA_INTEGRA_H
#include <stdint.h>
int  hoja_desde_contactos(char *msg, uint32_t cap);                     /* libro nuevo con la libreta; indice o -1 */
int  hoja_desde_agenda(const char *tramo, char *msg, uint32_t cap);     /* "este mes", "la semana"... (vacio: 3 meses desde el dia 1) */
int  hoja_envia_correo(int libro, const char *para, char *msg, uint32_t cap);   /* 0 = correo nuevo con el XLSX adjunto */
void hoja_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos); /* para el prompt del agente */
#endif
