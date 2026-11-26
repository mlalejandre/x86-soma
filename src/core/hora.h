#ifndef SOMA_HORA_H
#define SOMA_HORA_H
/* SOMA_332_HORA: la hora del sistema. El reloj del equipo (RTC) va en UTC, como en Linux y macOS; si el equipo comparte disco
 * con Windows puede ir en hora local ("reloj local"). Todo lo que se ensena a una persona (el reloj del escritorio, la fecha
 * del agente, la agenda, el programador, la orden date...) pasa por aqui y sale en la zona elegida, con su horario de verano.
 * Se guarda en /etc/hora.conf (zona=Europe/Madrid, reloj=utc|local). Por defecto, Espana (Madrid). */
#include <stdint.h>
#include "rtc.h"

void    hora_carga(void);                     /* lee /etc/hora.conf (una vez; la llama el escritorio al arrancar) */
void    hora_utc(struct rtc_time *t);         /* fecha y hora UTC */
void    hora_local(struct rtc_time *t);       /* fecha y hora en la zona elegida */
int64_t hora_utc_min(void);                   /* minutos desde 1970 (UTC) */
int64_t hora_utc_seg(void);                   /* segundos desde 1970 (UTC) */
int     hora_desfase(void);                   /* minutos que se suman a UTC ahora mismo */
int     hora_desfase_en(int64_t min_utc);     /* ... en ese instante */
int     hora_verano(void);                    /* 1 si rige el horario de verano */
int     hora_zona(void);                      /* indice en zona_i() o ZONA_MANUAL */
int16_t hora_manual(void);                    /* el desfase fijo, si la zona es ZONA_MANUAL */
int     hora_reloj_local(void);               /* 1 si el RTC guarda la hora local (Windows) */
int     hora_pon_zona(const char *s);         /* nombre IANA, ciudad, pais o "UTC+2". 0 = bien; se guarda */
int     hora_pon_zona_i(int i);               /* por indice */
void    hora_pon_reloj_local(int local);      /* se guarda */
void    hora_nombre_zona(char *o, uint32_t cap);   /* "Espana peninsular (Madrid)" o "UTC+03:00" */
uint32_t hora_version(void);                  /* sube con cada cambio de ajuste */
int     hora_cmd(const char *args, char *out, uint32_t cap);   /* orden "hora" */

#endif
