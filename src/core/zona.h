#ifndef SOMA_ZONA_H
#define SOMA_ZONA_H
/* SOMA_332_HORA: zonas horarias, codigo puro (sin nucleo: se prueba en el anfitrion).
 *
 * Una zona es un desfase normal respecto a UTC y, si la hay, una regla de horario de verano. Las reglas son las vigentes
 * (UE, EE. UU./Canada, Cuba, Chile, Australia, Nueva Zelanda); los paises que ya no cambian la hora (Mexico, Brasil, Paraguay,
 * Turquia, Rusia...) van sin regla. Tambien vale un desfase fijo a mano ("UTC+3", "-04:30").
 * Los instantes son minutos desde 1970-01-01 00:00 UTC. */
#include <stdint.h>

enum { ZR_NADA, ZR_UE, ZR_EEUU, ZR_CUBA, ZR_CHILE, ZR_AUS, ZR_NZ };
enum { ZG_EUROPA, ZG_AMERICA, ZG_ASIA, ZG_N };            /* grupos para elegir en pantalla (Africa va con Europa) */

struct zona {
    const char *id;          /* nombre IANA: "Europe/Madrid" */
    const char *nombre;      /* para la pantalla: "Espana (Madrid)" */
    const char *alias;       /* otras formas de buscarla, separadas por '|' (minusculas, sin tildes) */
    int16_t std;             /* minutos respecto a UTC sin horario de verano */
    uint8_t regla, grupo;
};

#define ZONA_MANUAL (-2)     /* zona_busca: no es una zona de la lista sino un desfase fijo */

int  zona_n(void);
const struct zona *zona_i(int i);                          /* NULL fuera de rango */
int  zona_busca(const char *s, int16_t *manual);           /* indice, ZONA_MANUAL (con *manual) o -1 */
int  zona_desfase(int i, int16_t manual, int64_t min_utc); /* minutos que hay que sumar a UTC (i = ZONA_MANUAL: manual) */
int  zona_verano(int i, int64_t min_utc);                  /* 1 si en ese instante rige el horario de verano */
int64_t zona_dias(int y, int m, int d);                    /* dias desde 1970-01-01 */
void zona_civil(int64_t dias, int *y, int *m, int *d);
int  zona_dia_semana(int64_t dias);                        /* 0 = lunes ... 6 = domingo */
void zona_texto_desfase(int min, char *o, uint32_t cap);   /* "UTC+01:00", "UTC-03:30", "UTC" */

#endif
