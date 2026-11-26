#ifndef SOMA_AGENTES_REG_H
#define SOMA_AGENTES_REG_H
/* SOMA_338_AGENTES: registro de lo que hacen los agentes de SOMA, para la aplicacion Agentes (src/apps/agentes_gui.c) y la orden
 * "agentes". Cada mision (lo que se le pide al orbe o al chat) con su hora, cuanto duro, que especialistas trabajaron y como
 * acabo; los pasos de la mision en curso; y la actividad reciente (ordenes, busquedas, lecturas). La parte de abajo es pura (se
 * prueba en el anfitrion: tests/test_agentes_338.c); en SOMA la alimentan gui_fase, gui_fase_orden y el hilo del agente. */
#include <stdint.h>

enum { AGE_LCM, AGE_MAYORDOMO, AGE_INVESTIGADOR, AGE_PROGRAMADOR, AGE_MAQUETADOR, AGE_HERRAMIENTAS, AGE_MODELO, AGE_VOZ, AGE_N };
enum { AGF_EN_CURSO, AGF_BIEN, AGF_ERROR, AGF_DETENIDA };

#define AGR_MIS   24                              /* misiones que se recuerdan */
#define AGR_ACT   48                              /* lineas de actividad */
#define AGR_PASOS 16                              /* pasos de una mision */
struct agr_mision {
    char texto[160], respuesta[160];
    uint64_t ini_ms, fin_ms;                      /* reloj del sistema (ms desde que arranco) */
    int64_t ini_min;                              /* hora de pared (minutos UTC) para ensenarla */
    uint32_t esp;                                 /* bit e = el especialista e trabajo en ella */
    int estado, npasos;
    uint8_t paso[AGR_PASOS]; uint32_t paso_ms[AGR_PASOS];   /* especialista y cuando (ms desde el inicio) */
};
struct agr_act { uint64_t ms; int64_t min; uint8_t esp; char txt[120]; };
struct agr {
    struct agr_mision mis[AGR_MIS]; int nmis, imis;          /* anillo: la mas nueva en imis - 1 */
    struct agr_act act[AGR_ACT]; int nact, iact;
    uint32_t usos[AGE_N]; uint64_t ult_ms[AGE_N];
    int en_curso;                                             /* hay una mision abierta (la mas nueva) */
    uint32_t ver;
};

void agr_inicia(struct agr *r);
void agr_mision(struct agr *r, const char *texto, uint64_t ms, int64_t min);    /* empieza una mision */
void agr_fase(struct agr *r, const char *fase, uint64_t ms);                     /* gui_fase: "lcm", "buscar", "pensar"... */
void agr_orden(struct agr *r, const char *linea, uint64_t ms, int64_t min);      /* gui_fase_orden: "search ...", "leer ...", "date" */
void agr_fin(struct agr *r, const char *respuesta, uint64_t ms);                 /* acaba la mision abierta */
int  agr_esp_de(const char *fase);                                               /* especialista de una fase; -1 ninguno */
int  agr_esp_de_orden(const char *linea);                                        /* especialista de una orden */
const char *agr_nombre(int esp);
const char *agr_desc(int esp);
const char *agr_ejemplo(int esp);                                                /* una peticion de ejemplo para probarlo */
const struct agr_mision *agr_mision_i(const struct agr *r, int i);              /* 0 = la mas nueva */
const struct agr_act *agr_act_i(const struct agr *r, int i);                    /* 0 = la mas nueva */
int  agr_activo(const struct agr *r);                                            /* especialista trabajando ahora (-1 ninguno) */
void agr_dura(uint64_t ms, char *o, uint32_t cap);                               /* "850 ms", "12 s", "3 min 05 s" */

/* ---- en SOMA (con cerrojo y reloj) ---- */
void agentes_reg_mision(const char *texto);
void agentes_reg_fase(const char *fase);
void agentes_reg_orden(const char *linea);
void agentes_reg_fin(const char *respuesta);
struct agr *agentes_reg_entra(void);              /* toma el cerrojo; soltar enseguida */
void agentes_reg_sale(void);
uint32_t agentes_reg_version(void);
int  agentes_cmd(const char *args, char *out, uint32_t cap);
#endif
