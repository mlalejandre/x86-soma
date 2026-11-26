/* SOMA_338_AGENTES: registro de actividad de los agentes (src/agent/agentes_reg.c, parte pura). */
#include <stdio.h>
#include <string.h>
#include "agentes_reg.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)
static struct agr R;

int main(void)
{
    agr_inicia(&R);
    CK(agr_activo(&R) == -1 && R.nmis == 0 && !agr_mision_i(&R, 0));
    /* especialistas */
    CK(agr_esp_de("lcm") == AGE_LCM && agr_esp_de("Mayordomo") == AGE_MAYORDOMO && agr_esp_de("buscar") == AGE_INVESTIGADOR);
    CK(agr_esp_de("documento") == AGE_MAQUETADOR && agr_esp_de("programador") == AGE_PROGRAMADOR && agr_esp_de("pensar") == AGE_MODELO);
    CK(agr_esp_de("escribir") == AGE_MODELO && agr_esp_de("voz") == AGE_VOZ && agr_esp_de("hecho") == -1 && agr_esp_de("orden") == AGE_HERRAMIENTAS);
    CK(agr_esp_de("") == -1 && agr_esp_de(0) == -1 && agr_esp_de("traductor") == AGE_HERRAMIENTAS);
    CK(agr_esp_de_orden("search gripe sintomas") == AGE_INVESTIGADOR && agr_esp_de_orden("leer es.wikipedia.org") == AGE_INVESTIGADOR);
    CK(agr_esp_de_orden("python /workspace/a.py") == AGE_PROGRAMADOR && agr_esp_de_orden("ls /workspace") == AGE_HERRAMIENTAS);
    CK(agr_esp_de_orden("cat x") == AGE_HERRAMIENTAS && agr_esp_de_orden("ccc") == AGE_HERRAMIENTAS);   /* "cc" es palabra entera */
    /* una mision */
    agr_mision(&R, "investiga que sintomas\ntiene la gripe", 1000, 29000000);
    CK(R.en_curso && R.nmis == 1 && !strcmp(agr_mision_i(&R, 0)->texto, "investiga que sintomas tiene la gripe"));
    agr_fase(&R, "lcm", 1010);
    agr_fase(&R, "buscar", 1100);
    agr_orden(&R, "search gripe sintomas", 1200, 29000001);
    agr_orden(&R, "leer medlineplus.gov", 1300, 29000001);
    agr_fase(&R, "pensar", 5000);
    CK(agr_activo(&R) == AGE_MODELO);
    agr_fase(&R, "escribir", 6000);                            /* mismo especialista: un solo paso */
    const struct agr_mision *m = agr_mision_i(&R, 0);
    CK(m->npasos == 3 && m->paso[0] == AGE_LCM && m->paso[1] == AGE_INVESTIGADOR && m->paso[2] == AGE_MODELO && m->paso_ms[1] == 100);
    CK(m->esp == ((1u << AGE_LCM) | (1u << AGE_INVESTIGADOR) | (1u << AGE_MODELO)));
    CK(R.usos[AGE_INVESTIGADOR] == 1 && R.usos[AGE_MODELO] == 1);          /* una vez por mision, aunque trabaje varias */
    CK(R.nact == 2 && !strcmp(agr_act_i(&R, 0)->txt, "leer medlineplus.gov") && agr_act_i(&R, 1)->esp == AGE_INVESTIGADOR);
    agr_fin(&R, "[Razonamiento]:\npienso\n\n[Dictamen]:\nLa gripe da fiebre [1].", 9000);
    CK(!R.en_curso && m->estado == AGF_BIEN && m->fin_ms == 9000 && !strcmp(m->respuesta, "La gripe da fiebre [1]."));
    agr_fin(&R, "otra", 9500);                                 /* no hay mision abierta: nada */
    CK(m->fin_ms == 9000);
    /* errores, detenidas y una que no cerro */
    agr_mision(&R, "programa algo", 10000, 0); agr_fin(&R, "Mision detenida.", 11000);
    CK(agr_mision_i(&R, 0)->estado == AGF_DETENIDA);
    agr_mision(&R, "otra", 12000, 0); agr_fin(&R, "LLM: no hay respuesta del servidor local", 13000);
    CK(agr_mision_i(&R, 0)->estado == AGF_ERROR);
    agr_mision(&R, "sin cerrar", 14000, 0); agr_mision(&R, "siguiente", 15000, 0);
    CK(agr_mision_i(&R, 1)->estado == AGF_DETENIDA && agr_mision_i(&R, 1)->fin_ms == 15000 && agr_mision_i(&R, 0)->estado == AGF_EN_CURSO);
    /* fuera de una mision: cuenta el uso, sin pasos */
    agr_fin(&R, "ok", 16000);
    agr_orden(&R, "date", 17000, 0);
    CK(R.usos[AGE_HERRAMIENTAS] >= 1);
    /* anillos llenos: las mas nuevas, en orden */
    for (int i = 0; i < AGR_MIS + 5; i++) { char t[32]; snprintf(t, sizeof t, "m%d", i); agr_mision(&R, t, 20000 + (uint64_t)i, 0); agr_fin(&R, "bien", 20000 + (uint64_t)i); }
    CK(R.nmis == AGR_MIS && !strcmp(agr_mision_i(&R, 0)->texto, "m28") && !strcmp(agr_mision_i(&R, AGR_MIS - 1)->texto, "m5") && !agr_mision_i(&R, AGR_MIS));
    for (int i = 0; i < AGR_ACT + 3; i++) { char t[32]; snprintf(t, sizeof t, "ls %d", i); agr_orden(&R, t, 30000, 0); }
    CK(R.nact == AGR_ACT && !strcmp(agr_act_i(&R, 0)->txt, "ls 50") && !agr_act_i(&R, AGR_ACT));
    /* muchos pasos: se quedan los ultimos */
    agr_mision(&R, "larga", 40000, 0);
    for (int i = 0; i < 40; i++) agr_fase(&R, (i & 1) ? "pensar" : "orden", 40000 + (uint64_t)i);
    CK(agr_mision_i(&R, 0)->npasos == AGR_PASOS);
    /* textos largos y UTF-8 */
    char largo[600]; memset(largo, 0, sizeof largo); for (int i = 0; i < 290; i++) { largo[2 * i] = (char)0xC3; largo[2 * i + 1] = (char)0xB1; }
    agr_mision(&R, largo, 50000, 0);
    m = agr_mision_i(&R, 0); size_t L = strlen(m->texto);
    CK(L < sizeof m->texto && L % 2 == 0 && ((unsigned char)m->texto[L - 1] == 0xB1));
    /* duraciones */
    char d[32];
    agr_dura(850, d, sizeof d); CK(!strcmp(d, "850 ms"));
    agr_dura(4300, d, sizeof d); CK(!strcmp(d, "4,3 s"));
    agr_dura(42000, d, sizeof d); CK(!strcmp(d, "42 s"));
    agr_dura(185000, d, sizeof d); CK(!strcmp(d, "3 min 05 s"));
    agr_dura(7380000, d, sizeof d); CK(!strcmp(d, "2 h 03 min"));
    CK(agr_nombre(AGE_N)[0] == 0 && agr_desc(-1)[0] == 0 && agr_ejemplo(AGE_INVESTIGADOR)[0]);
    printf("test_agentes_338: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
