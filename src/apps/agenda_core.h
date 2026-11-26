/* SOMA_272_AGENDA: nucleo del calendario. Logica pura (sin nucleo, sin libc): fechas, repeticiones, lenguaje natural en castellano,
 * formato propio de guardado, .ics (importar y exportar) y avisos. Se prueba en el host (tests/test_agenda_272.c).
 *
 * Todas las horas son LOCALES ("hora de pared"): un evento "a las 13:00" es a las 13:00 de donde estes, cambie o no el horario de verano.
 * Un instante se guarda como minutos desde 1970-01-01 00:00 local (int64). El reloj del equipo (UTC) se pasa a local con ag_utc_a_local()
 * (Europa/Madrid: UTC+1 y UTC+2 de verano; ultimo domingo de marzo a ultimo domingo de octubre). */
#ifndef AGENDA_CORE_H
#define AGENDA_CORE_H
#include <stdint.h>

#define AG_MAX_EV   400
#define AG_TIT      80
#define AG_LUG      48
#define AG_NOTA     96
#define AG_EXC      4

enum { AG_REP_NO = 0, AG_REP_DIA, AG_REP_SEM, AG_REP_MES, AG_REP_ANO, AG_REP_LAB };
enum { AG_CAT_GENERAL = 0, AG_CAT_TRABAJO, AG_CAT_DEPORTE, AG_CAT_SALUD, AG_CAT_SOCIAL, AG_CAT_VIAJE, AG_CAT_FAMILIA, AG_CAT_ESTUDIO, AG_NCAT };

struct ag_ev {
    uint32_t id;                 /* 0 = hueco libre */
    int64_t  ini;                /* minutos locales desde 1970-01-01 (los de todo el dia: 00:00) */
    uint32_t dur;                /* minutos; 0 = puntual */
    uint8_t  todo_dia, rep, cat, activo;
    uint16_t cada;               /* repite cada N (dias, semanas, meses, anos); >= 1 */
    int16_t  antes;              /* aviso: -1 = el de la configuracion, 0 = sin aviso, N = minutos antes */
    int64_t  hasta;              /* ultimo DIA (desde 1970) en que se repite; 0 = sin fin */
    int32_t  exc[AG_EXC];        /* dias (desde 1970) en que NO se celebra (borrada solo esa vez); 0 = vacio */
    char     titulo[AG_TIT], lugar[AG_LUG], nota[AG_NOTA];
};
struct ag_cal { struct ag_ev ev[AG_MAX_EV]; uint32_t sig_id; };
struct ag_oc { int64_t ini; uint16_t idx; };          /* una celebracion de un evento (idx en ag_cal.ev) */

/* ---- fechas */
int64_t ag_dias(int y, int m, int d);                  /* dias desde 1970-01-01 */
void    ag_civil(int64_t dias, int *y, int *m, int *d);
int     ag_dia_semana(int64_t dias);                   /* 0 lunes .. 6 domingo */
int     ag_dias_mes(int y, int m);
int64_t ag_min(int y, int mo, int d, int h, int mi);
void    ag_desde_min(int64_t min, int *y, int *mo, int *d, int *h, int *mi);
int64_t ag_dia_de(int64_t min);                        /* dia (desde 1970) de un instante */
int64_t ag_utc_a_local(int64_t min_utc);               /* con ag_desfase_fn; si no hay, Europa/Madrid */
extern int (*ag_desfase_fn)(int64_t min_utc);         /* SOMA_332_HORA: minutos a sumar a UTC en ese instante (la zona elegida) */
const char *ag_nombre_mes(int m);                      /* 1..12 */
const char *ag_nombre_dia(int dsem);                   /* 0 lunes .. 6 domingo */
const char *ag_nombre_cat(int c);

/* ---- el calendario */
struct ag_ev *ag_nuevo(struct ag_cal *c);              /* hueco reservado con id nuevo, o 0 si esta lleno */
struct ag_ev *ag_busca_id(struct ag_cal *c, uint32_t id);
int      ag_borra(struct ag_cal *c, uint32_t id);      /* 1 si existia */
int      ag_borra_ocurrencia(struct ag_cal *c, uint32_t id, int64_t dia);   /* solo ese dia de un evento que repite (1 = hecho) */
uint32_t ag_numero(const struct ag_cal *c);

/* celebraciones de un evento que tocan [desde, hasta) (minutos locales); devuelve cuantas (hasta max) */
int ag_en_rango(const struct ag_ev *e, int64_t desde, int64_t hasta, int64_t *out, int max);
/* todas las de todos los eventos, ordenadas por inicio */
int ag_agenda_rango(const struct ag_cal *c, int64_t desde, int64_t hasta, struct ag_oc *out, int max);
/* la proxima celebracion que empieza en o despues de `desde`; 1 si hay */
int ag_proxima(const struct ag_cal *c, int64_t desde, struct ag_oc *oc);

/* ---- guardado (formato propio, texto) */
uint32_t ag_serializa(const struct ag_cal *c, char *out, uint32_t cap);     /* bytes escritos; 0 si no cabe */
int      ag_carga(struct ag_cal *c, const char *txt, uint32_t len);         /* eventos leidos; -1 si no es una agenda */

/* ---- lenguaje natural ("el miercoles dia 6 a las 13 horas tengo un partido de futbol con los amigos") */
/* Rellena ev (titulo, fecha, hora, duracion, repeticion, categoria, aviso). 0 = bien; -1 = no hay fecha ni hora que entender. */
int ag_parsea(const char *txt, int64_t ahora, struct ag_ev *ev);
/* Solo un dia o un tramo para consultar ("hoy", "manana", "el miercoles", "la semana", "el mes"): [dia_ini, dia_fin) en dias. 0 = bien. */
int ag_parsea_tramo(const char *txt, int64_t ahora, int64_t *dia_ini, int64_t *dia_fin);

/* ---- texto */
void ag_fecha_txt(int64_t min, int64_t ahora, char *out, uint32_t cap);          /* "hoy", "manana", "el miercoles 6 de enero" */
void ag_hora_txt(int64_t min, char *out, uint32_t cap);                          /* "13:00" */
void ag_describe(const struct ag_ev *e, int64_t ini, int64_t ahora, char *out, uint32_t cap);   /* una linea */
void ag_dicho(const struct ag_ev *e, int64_t ini, int64_t ahora, int64_t minutos_para, char *out, uint32_t cap);   /* para la voz */
uint32_t ag_lista_txt(const struct ag_cal *c, int64_t dia_ini, int64_t dia_fin, int64_t ahora, char *out, uint32_t cap);   /* agenda de un tramo */

/* ---- .ics */
uint32_t ag_ics_exporta(const struct ag_cal *c, char *out, uint32_t cap);
int      ag_ics_importa(struct ag_cal *c, const char *txt, uint32_t len);        /* eventos nuevos; -1 si no es un .ics */

/* ---- avisos: eventos que deben avisar ahora. `visto` (tabla propia del llamador) evita repetir. */
struct ag_visto { uint32_t id; int64_t ini; };
#define AG_VISTOS 32
struct ag_avisos { struct ag_visto v[AG_VISTOS]; uint32_t n; };
/* 1 y rellena id/ini/min_para si hay un aviso pendiente (y lo marca como dado). antes_def = minutos por defecto. */
int ag_aviso_siguiente(const struct ag_cal *c, struct ag_avisos *st, int64_t ahora, int antes_def, uint32_t *id, int64_t *ini);

#endif
