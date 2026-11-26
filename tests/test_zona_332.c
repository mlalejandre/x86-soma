/* SOMA_332_HORA: zonas horarias. Los cambios de hora de 2025 y 2026 se han comprobado minuto a minuto contra la base de datos
 * de zonas (tzdata, zoneinfo de Python); aqui quedan fijados. */
#include <stdio.h>
#include <string.h>
#include "zona.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)

struct cambio { const char *zona; long long t[4]; int o[5]; };   /* o[0] antes del primer cambio, o[k] desde t[k-1] */
static const struct cambio CAMBIOS[] = {
    { "Europe/Madrid",     { 29054940, 29357340, 29579100, 29881500 }, { 60, 120, 60, 120, 60 } },
    { "America/New_York",  { 29025060, 29367720, 29549220, 29891880 }, { -300, -240, -300, -240, -300 } },
    { "America/Havana",    { 29024940, 29367660, 29549100, 29891820 }, { -300, -240, -300, -240, -300 } },
    { "America/Santiago",  { 29065140, 29286960, 29589300, 29811120 }, { -180, -240, -180, -240, -180 } },
    { "Australia/Sydney",  { 29064480, 29326560, 29588640, 29850720 }, { 660, 600, 660, 600, 660 } },
    { "Pacific/Auckland",  { 29064360, 29316360, 29588520, 29840520 }, { 780, 720, 780, 720, 780 } },
};

int main(void)
{
    /* calendario */
    int y, m, d;
    CK(zona_dias(1970, 1, 1) == 0 && zona_dias(2000, 3, 1) == 11017 && zona_dias(1969, 12, 31) == -1);
    for (long long k = -800000; k < 800000; k += 97) { zona_civil(k, &y, &m, &d); if (zona_dias(y, m, d) != k) { CK(0); break; } }
    CK(zona_dia_semana(zona_dias(2026, 11, 25)) == 2);            /* miercoles */
    CK(zona_dia_semana(zona_dias(1969, 12, 28)) == 6);            /* domingo, antes de 1970 */

    /* cambios de hora exactos al minuto */
    for (unsigned c = 0; c < sizeof CAMBIOS / sizeof CAMBIOS[0]; c++) {
        int16_t mn = 0; int i = zona_busca(CAMBIOS[c].zona, &mn);
        CK(i >= 0);
        CK(zona_desfase(i, 0, 28928160) == CAMBIOS[c].o[0]);     /* 2025-01-01 */
        for (int k = 0; k < 4; k++) {
            CK(zona_desfase(i, 0, CAMBIOS[c].t[k] - 1) == CAMBIOS[c].o[k]);
            CK(zona_desfase(i, 0, CAMBIOS[c].t[k]) == CAMBIOS[c].o[k + 1]);
        }
    }
    /* sin horario de verano */
    int tk = zona_busca("Asia/Tokyo", 0), mx = zona_busca("mexico", 0), in = zona_busca("India", 0);
    CK(tk >= 0 && zona_desfase(tk, 0, 29200000) == 540 && zona_desfase(tk, 0, 29500000) == 540 && !zona_verano(tk, 29200000));
    CK(mx >= 0 && zona_desfase(mx, 0, 29200000) == -360 && zona_desfase(mx, 0, 29500000) == -360);
    CK(in >= 0 && zona_desfase(in, 0, 29200000) == 330);

    /* busqueda */
    int16_t mn = 0;
    int md = zona_busca("Europe/Madrid", 0);
    CK(md >= 0 && zona_busca("madrid", 0) == md && zona_busca("  MADRID ", 0) == md && zona_busca("Espa\303\261a", 0) == md && zona_busca("barcelona", 0) == md);
    CK(zona_busca("Espana (Madrid)", 0) == md && zona_busca("espana peninsular", 0) == md);
    CK(zona_busca("new_york", 0) == zona_busca("Nueva York", 0) && zona_busca("New York", 0) >= 0);
    CK(zona_busca("canarias", 0) == zona_busca("Atlantic/Canary", 0) && zona_busca("canarias", 0) != md);
    CK(zona_busca("Bogot\303\241", 0) == zona_busca("America/Bogota", 0));
    CK(zona_busca("utc", 0) == 0 && zona_busca("UTC", 0) == 0);
    CK(zona_busca("UTC+2", &mn) == ZONA_MANUAL && mn == 120);
    CK(zona_busca("gmt-3", &mn) == ZONA_MANUAL && mn == -180);
    CK(zona_busca("+05:30", &mn) == ZONA_MANUAL && mn == 330);
    CK(zona_busca("-0430", &mn) == ZONA_MANUAL && mn == -270);
    CK(zona_busca("UTC+14", &mn) == ZONA_MANUAL && mn == 840);
    CK(zona_busca("UTC+15", &mn) == -1 && zona_busca("+05:31", &mn) == -1 && zona_busca("+5:3x", &mn) == -1);
    CK(zona_busca("atlantida", 0) == -1 && zona_busca("", 0) == -1 && zona_busca(0, 0) == -1);
    CK(zona_desfase(ZONA_MANUAL, -270, 0) == -270);
    CK(zona_desfase(-1, 0, 0) == 0 && zona_desfase(9999, 0, 0) == 0 && zona_i(-1) == 0);
    for (int i = 0; i < zona_n(); i++) {                            /* cada zona se encuentra por su nombre, y una sola vez */
        CK(zona_busca(zona_i(i)->id, 0) == i);
        CK(zona_i(i)->std >= -720 && zona_i(i)->std <= 840 && zona_i(i)->grupo < ZG_N);
        for (int j = 0; j < i; j++) CK(strcmp(zona_i(i)->id, zona_i(j)->id) != 0);
    }
    /* entradas raras: nada se sale de los limites */
    char b[80];
    for (int k = 0; k < 20000; k++) {
        unsigned x = (unsigned)k * 2654435761u;
        int n = (int)(x % 79);
        for (int i = 0; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (char)(x >> 16); if (!b[i]) b[i] = 'a'; }
        b[n] = 0;
        int r = zona_busca(b, &mn);
        if (r < -2 || r >= zona_n()) { CK(0); break; }
    }
    char t[16];
    zona_texto_desfase(60, t, sizeof t); CK(!strcmp(t, "UTC+01:00"));
    zona_texto_desfase(-270, t, sizeof t); CK(!strcmp(t, "UTC-04:30"));
    zona_texto_desfase(0, t, sizeof t); CK(!strcmp(t, "UTC"));
    zona_texto_desfase(840, t, 6); CK(!strcmp(t, "UTC+1"));

    printf("test_zona_332: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
