/* SOMA_272_AGENDA: el calendario dentro del sistema (guardado en DATOS, reloj local, avisos y la orden `agenda`).
 * La logica esta en agenda_core.c (pura, probada en el host). Los eventos viven en /agenda/eventos.txt (formato propio) y se pueden
 * exportar/importar como .ics (Google Calendar, Outlook, Apple...). Los ajustes de avisos, en /etc/agenda.conf. */
#ifndef AGENDA_H
#define AGENDA_H
#include <stdint.h>
#include "agenda_core.h"

struct agenda_conf {
    int avisos;        /* 1 = avisar de los eventos que van a empezar */
    int voz;           /* 1 = aviso hablado (si hay voz instalada) */
    int visual;        /* 1 = aviso en pantalla */
    int antes;         /* minutos de antelacion por defecto (0, 5, 10, 15, 30, 60...) */
};

struct ag_cal *agenda_cal(void);             /* carga de DATOS la primera vez */
int64_t        agenda_ahora(void);           /* minutos locales (Europa/Madrid) desde 1970 */
int            agenda_guarda(void);          /* 0 = bien */
uint32_t       agenda_version(void);         /* cambia con cada modificacion (para repintar) */
void           agenda_conf_get(struct agenda_conf *c);
void           agenda_conf_set(const struct agenda_conf *c);
int            agenda_anade_texto(const char *txt, char *resumen, uint32_t cap);   /* "mañana a las 9 reunion": 0 bien, <0 no entiendo */
int            agenda_borra(uint32_t id);
/* Lo llama la GUI cada segundo: 1 si hay un aviso que dar ahora (texto = lo que se dice y se muestra). */
int            agenda_tick(char *texto, uint32_t cap);
int            agenda_cmd(const char *args, char *out, uint32_t max);              /* la orden `agenda` */
#endif
