/* SOMA_279_SUENO: pruebas del arbol, el olvido y las piezas del ciclo de sueno (src/agent/memoria_core.c), con ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "memoria_core.h"

static int checks, fails;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CKE(a, b) do { long long _a = (long long)(a), _b = (long long)(b); checks++; if (_a != _b) { fails++; printf("  FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CKS(a, b) do { const char *_a = (a), *_b = (b); checks++; if (!_a || strcmp(_a, _b)) { fails++; printf("  FALLO %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b); } } while (0)

void *mc_alloc(uint32_t n) { return calloc(1, n); }
void mc_free(void *p) { free(p); }

static void vec_tema(int tema, int ruido, int8_t *v, int dim)
{
    char js[16384]; int p = 0;
    p += sprintf(js, "{\"data\":[{\"embedding\":[");
    for (int i = 0; i < dim; i++) { double x = (i % 8 == tema) ? 1.0 : 0.0; x += ((i * 7 + ruido * 13) % 11) * 0.004; p += sprintf(js + p, "%s%.6f", i ? "," : "", x); }
    sprintf(js + p, "]}]}");
    mc_parse_embedding(js, v, MC_DIM_MAX);
}
static uint32_t celda(struct mc *m, const char *t, int imp, uint32_t tick) { return mc_add(m, MC_CELDA, MC_ROL_USUARIO, t, "test", imp, 1000, tick, 0, 0); }

int main(void)
{
    static struct mc m;
    CKE(mc_init(&m, 4096), 0);

    /* 1. curva del olvido frente a la de memorIA en coma flotante */
    int peor = 0;
    for (int imp = 1; imp <= 10; imp++) for (uint32_t ed = 0; ed <= 60000; ed += (ed < 100 ? 1 : 137)) {
        double ref = imp * exp(-(double)ed / (20.0 * imp * imp * imp)) * 256.0;
        int d = (int)mc_eff_q8(imp, ed, 20) - (int)ref; if (d < 0) d = -d;
        if (d > peor) peor = d;
    }
    CK(peor <= 2);
    printf("  (curva del olvido: error maximo %d/256)\n", peor);
    CKE(mc_eff_q8(5, 0, 20), 5 * 256);
    CKE(mc_eff_q8(10, 4000000000u, 20), 0);
    CK(mc_eff_q8(0, 5, 20) == mc_eff_q8(1, 5, 20));          /* imp 0 se trata como 1 */
    CK(mc_eff_q8(3, 10, 0) == 0);                             /* tau 0: sin curva, no divide */
    /* 1 de importancia: se archiva a los ~14 ticks (eff < 0,5) */
    CK(mc_eff_q8(1, 13, 20) >= 128 && mc_eff_q8(1, 15, 20) < 128);

    /* 2. olvido */
    uint32_t c1 = celda(&m, "El usuario tomo un cafe", 1, 0), c5 = celda(&m, "El usuario tiene un perro llamado Thor", 5, 0);
    uint32_t c9 = celda(&m, "Responde siempre en espanol", 9, 0), cn = celda(&m, "El usuario vio una serie", 1, 90);
    uint32_t ep = mc_add(&m, MC_EPISODIO, MC_ROL_USUARIO, "mensaje literal", "", 1, 1000, 0, 0, 0);
    CKE(mc_olvida(&m, 5, 10, 20, 128), 0);                    /* demasiado joven: min_edad */
    CKE(mc_olvida(&m, 30, 10, 20, 128), 1);                   /* solo c1 (imp 1, edad 30) y cn (edad 0) no */
    CKE(mc_get(&m, c1)->estado, MC_ARCHIVADO); CKE(mc_get(&m, c1)->t_hasta, 0);
    CKE(mc_get(&m, c5)->estado, MC_ACTIVO); CKE(mc_get(&m, c9)->estado, MC_ACTIVO); CKE(mc_get(&m, cn)->estado, MC_ACTIVO);
    CKE(mc_get(&m, ep)->estado, MC_ACTIVO);
    CKE(mc_olvida(&m, 6000, 10, 20, 128), 2);                 /* c5 (imp 5) y cn; c9 (imp 9) aguanta */
    CKE(mc_get(&m, c9)->estado, MC_ACTIVO); CKE(mc_get(&m, ep)->estado, MC_ACTIVO);
    CKE(mc_olvida(&m, 6000, 10, 20, 128), 0);                 /* nada mas */
    { static struct mc t2; mc_init(&t2, 8); uint32_t i9 = celda(&t2, "Instruccion permanente", 9, 0); CKE(mc_olvida(&t2, 100000, 10, 20, 128), 1); CKE(t2.it[i9 - 1].estado, MC_ARCHIVADO); mc_destroy(&t2); }   /* hasta lo importante se desvanece con el tiempo suficiente */
    CK(mc_frag_sucio(&m, 0));

    /* 3. restaurar lo archivado */
    uint32_t user = celda(&m, "Dato archivado a mano", 4, 0); mc_set_estado(&m, user, MC_ARCHIVADO, 5555, 0);
    uint32_t sus = celda(&m, "Dato sustituido", 4, 0); mc_set_estado(&m, sus, MC_SUSTITUIDO, 6000, c9);
    uint32_t ids[5] = { c1, c5, user, sus, 9999 };
    CKE(mc_restaura(&m, ids, 5, 200), 2);                     /* c1 y c5: por olvido; no el archivado a mano, ni el sustituido, ni el que no existe */
    CKE(mc_get(&m, c1)->estado, MC_ACTIVO); CKE(mc_get(&m, c1)->imp, 6); CKE(mc_get(&m, c1)->tick, 200);
    CKE(mc_get(&m, c5)->imp, 6);
    CKE(mc_get(&m, user)->estado, MC_ARCHIVADO); CKE(mc_get(&m, sus)->estado, MC_SUSTITUIDO);
    CKE(mc_restaura(&m, ids, 5, 201), 0);                     /* ya estan activos */

    /* 4. arbol: rutas, reutilizacion, limites */
    uint32_t n1 = mc_nodo_ruta(&m, "Personal/Mascotas", 1), n0 = mc_nodo_ruta(&m, "personal", 0), n2 = mc_nodo_ruta(&m, "PERSONAL / mascotás", 0);
    CK(n1 && n0 && n0 != n1); CKE(n2, n1);                    /* sin mayusculas ni acentos, con espacios */
    char r[160]; mc_nodo_ruta_texto(&m, n1, r, sizeof r); CKS(r, "Personal/Mascotas");
    CKE(m.nodo[n1].nivel, 1); CKE(m.nodo[n1].padre, n0); CKE(m.nodo[n0].nivel, 0);
    CKE(mc_nodo_ruta(&m, "Personal/Mascotas", 1), n1);          /* no duplica */
    CKE(mc_nodo_ruta(&m, "Nada/De/Esto", 0), 0);                /* sin crear: no existe */
    uint32_t n3 = mc_nodo_ruta(&m, "Trabajo/Proyectos/SOMA/Parches/Extra", 1);
    mc_nodo_ruta_texto(&m, n3, r, sizeof r); CKS(r, "Trabajo/Proyectos/SOMA");   /* maximo 3 niveles */
    CKE(m.nodo[n3].nivel, 2);
    CKE(mc_nodo_ruta(&m, "", 1), 0); CKE(mc_nodo_ruta(&m, " / / ", 1), 0);
    uint32_t nn = mc_nodo_ruta(&m, "Una cosa muy muy larga que no cabe en cuarenta y ocho bytes de nombre ñandú ñandú", 1);
    CK(nn && strlen(m.nodo[nn].nombre) < MC_NODO_NOMBRE);
    { const char *s = m.nodo[nn].nombre; int ok = 1; for (size_t i = 0; s[i]; i++) if (((uint8_t)s[i] & 0xC0) == 0x80 && (i == 0 || (uint8_t)s[i - 1] < 0x80)) ok = 0; CK(ok); }
    CKE(mc_nodo_ruta(&m, "Ctrl\x01\x02Nodo", 1) != 0, 1);

    /* 5. asignar, recontar, podar */
    CKE(mc_asigna(&m, c5, n1), 0); CKE(mc_asigna(&m, c1, n1), 0); CKE(mc_asigna(&m, c9, n3), 0);
    CKE(mc_asigna(&m, ep, n1), -1);                            /* un episodio no cuelga de un nodo */
    CKE(mc_asigna(&m, c5, 200), -1); CKE(mc_asigna(&m, 9999, n1), -1);
    mc_recuenta(&m);
    CKE(m.nodo[n1].cuenta, 2); CKE(m.nodo[n3].cuenta, 1); CKE(m.nodo[n0].cuenta, 0);
    uint32_t lista[8]; CKE(mc_nodo_celdas(&m, n1, lista, 8), 2);
    CKE(m.it[c5 - 1].nodo, n1);
    celda(&m, "Un hecho sin ordenar", 5, 150); celda(&m, "Otro hecho sin ordenar", 5, 150);
    uint32_t sin[16]; uint32_t nsin = mc_sin_nodo(&m, sin, 16); CK(nsin >= 2);
    for (uint32_t i = 0; i < nsin; i++) CKE(m.it[sin[i] - 1].nodo, 0);
    uint32_t vacio = mc_nodo_ruta(&m, "Casa/Cocina/Recetas", 1); (void)vacio;
    uint32_t antes = mc_recuenta(&m), podados = mc_poda_nodos(&m);
    CKE(podados, 5);                                           /* Casa/Cocina/Recetas (3, en cadena) + el de nombre larguisimo + el de los controles */
    (void)antes;
    CKE(mc_nodo_ruta(&m, "Casa/Cocina", 0), 0);
    CK(m.nodo[n1].usado && m.nodo[n3].usado && m.nodo[n0].usado);
    uint32_t fijo = mc_nodo_ruta(&m, "Permanente", 1); m.nodo[fijo].fijo = 1;
    mc_poda_nodos(&m); CK(m.nodo[fijo].usado);               /* los fijados no se podan */

    /* 6. descripciones */
    mc_nodo_ruta(&m, "Personal/Mascotas", 1);
    CK(m.nodo[n1].sucio);
    uint32_t sc = mc_nodo_sucio(&m);
    CK(sc != 0); CK(m.nodo[sc].nivel >= m.nodo[n0].nivel);
    CKE(mc_nodo_set_desc(&m, n1, "Animales de compania del usuario"), 0); CK(!m.nodo[n1].sucio); CKS(m.nodo[n1].desc, "Animales de compania del usuario");
    CKE(mc_nodo_set_desc(&m, 250, "x"), -1);
    { char larga[400]; memset(larga, 'a', 399); larga[399] = 0; mc_nodo_set_desc(&m, n1, larga); CKE(strlen(m.nodo[n1].desc), MC_NODO_DESC - 1); }
    mc_nodo_set_desc(&m, n1, "Animales de compania del usuario");

    /* 7. fusionar ramas */
    uint32_t pa = mc_nodo_ruta(&m, "Salud/Medicos", 1), pb = mc_nodo_ruta(&m, "Salud/Doctores", 1);
    uint32_t cs = celda(&m, "El usuario va al medico los martes", 5, 200); mc_asigna(&m, cs, pb);
    CKE(mc_nodo_fusiona(&m, pa, pb, "Medicos y doctores"), 0);
    CKE(m.it[cs - 1].nodo, pa); CK(!m.nodo[pb].usado); CKS(m.nodo[pa].nombre, "Medicos y doctores"); CKE(m.nodo[pa].cuenta, 1);
    CKE(mc_nodo_fusiona(&m, pa, pa, 0), -1); CKE(mc_nodo_fusiona(&m, pa, pb, 0), -1);

    /* 8. lectura de lo que contesta el LLM */
    struct mc_asig as[8];
    CKE(mc_parse_ordena("```json\n{\"asignaciones\":[{\"id\":12,\"ruta\":\"Personal/Mascotas\"},{\"id\":\"13\",\"ruta\":\"  Trabajo / SOMA \"},{\"id\":0,\"ruta\":\"x\"},{\"ruta\":\"sin id\"},{\"id\":15,\"ruta\":\"\"},{\"id\":16,\"ruta\":\"A\\u00f1o\"}]}\n```", as, 8), 3);
    CKE(as[0].id, 12); CKS(as[0].ruta, "Personal/Mascotas"); CKE(as[1].id, 13); CKS(as[1].ruta, "Trabajo / SOMA"); CKE(as[2].id, 16); CKS(as[2].ruta, "A\xC3\xB1o");
    CKE(mc_parse_ordena("no es json", as, 8), -1); CKE(mc_parse_ordena("{\"asignaciones\":\"x\"}", as, 8), -1); CKE(mc_parse_ordena(0, as, 8), -1);
    CKE(mc_parse_ordena("{\"asignaciones\":[{\"id\":1,\"ruta\":\"A\"},{\"id\":2,\"ruta\":\"B\"},{\"id\":3,\"ruta\":\"C\"}]}", as, 2), 2);
    char de[200];
    CK(mc_parse_descripcion("{\"description\": \"Mascotas y animales\"}", de, sizeof de)); CKS(de, "Mascotas y animales");
    CK(mc_parse_descripcion("Claro: {\"description\":\"  Dos   espacios \"} fin", de, sizeof de)); CKS(de, "Dos espacios");
    CK(!mc_parse_descripcion("{\"description\": \"\"}", de, sizeof de)); CK(!mc_parse_descripcion("{}", de, sizeof de)); CK(!mc_parse_descripcion("{\"description\": 5}", de, sizeof de));
    CK(mc_parse_descripcion("{\"description\": \"abcdefghij\"}", de, 5)); CKS(de, "abcd");
    struct mc_fusion fu;
    CK(mc_parse_fusion("{\"redundant\": true, \"merged\": \"El usuario tiene un perro, Thor\", \"importance\": 8}", &fu)); CK(fu.redundante); CKS(fu.texto, "El usuario tiene un perro, Thor"); CKE(fu.imp, 8);
    CK(mc_parse_fusion("{\"redundant\": false}", &fu)); CK(!fu.redundante); CKE(fu.imp, 0);
    CK(mc_parse_fusion("{\"redundant\": \"true\", \"importance\": 8.5}", &fu)); CK(fu.redundante); CKE(fu.imp, 8);
    CK(!mc_parse_fusion("{\"merged\":\"x\"}", &fu)); CK(!mc_parse_fusion("{\"redundant\": 7}", &fu)); CK(!mc_parse_fusion(0, &fu));
    int imp = 0;
    CKE(mc_parse_rescate("{\"preserve\": true, \"importance\": 8}", &imp), 1); CKE(imp, 8);
    CKE(mc_parse_rescate("{\"preserve\": true, \"importance\": 2}", &imp), 1); CKE(imp, 5);
    CKE(mc_parse_rescate("{\"preserve\": true}", &imp), 1); CKE(imp, 7);
    CKE(mc_parse_rescate("{\"preserve\": false, \"importance\": 9}", &imp), 0); CKE(imp, 0);
    CKE(mc_parse_rescate("{\"importance\": 9}", &imp), -1); CKE(mc_parse_rescate(0, &imp), -1);
    char nm[64];
    CKE(mc_parse_unir("{\"mergeable\": true, \"merged_name\": \"Postres\"}", nm, sizeof nm), 1); CKS(nm, "Postres");
    CKE(mc_parse_unir("{\"mergeable\": false, \"merged_name\": \"X\"}", nm, sizeof nm), 0); CKS(nm, "");
    CKE(mc_parse_unir("{\"x\":1}", nm, sizeof nm), -1);
    struct mc_split sp;
    CK(mc_parse_split("{\"grupo_A\": {\"nombre\": \"Perros\", \"ids\": [\"1\", 2]}, \"grupo_B\": {\"nombre\": \"Gatos\", \"ids\": [3, \"4\", 5, \"x\"]}}", &sp));
    CKS(sp.nombre_a, "Perros"); CKE(sp.na, 2); CKE(sp.ids_a[1], 2); CKS(sp.nombre_b, "Gatos"); CKE(sp.nb, 3); CKE(sp.ids_b[0], 3); CKE(sp.ids_b[2], 5);
    CK(!mc_parse_split("{\"grupo_A\": {\"nombre\": \"x\"}}", &sp)); CK(!mc_parse_split("basura", &sp)); CK(!mc_parse_split(0, &sp));
    CK(mc_parse_split("{\"grupo_B\": {\"ids\": [9], \"nombre\": \"Solo B\"}, \"grupo_A\": {\"ids\": [], \"nombre\": \"Solo A\"}}", &sp)); CKE(sp.nb, 1); CKE(sp.na, 0); CKS(sp.nombre_b, "Solo B");

    /* 9. aplicar fusion */
    uint32_t fa = celda(&m, "El usuario tiene un perro", 5, 300), fb = celda(&m, "El usuario tiene un perro llamado Thor", 6, 300);
    int8_t vv[MC_DIM_MAX]; vec_tema(1, 0, vv, 64); mc_set_vec(&m, fa, vv, 64);
    mc_parse_fusion("{\"redundant\": true, \"merged\": \"El usuario tiene un perro llamado Thor\", \"importance\": 4}", &fu);
    CKE(mc_aplica_fusion(&m, fa, fb, &fu, 310, 777), 0);
    CKS(m.it[fa - 1].texto, "El usuario tiene un perro llamado Thor"); CKE(m.it[fa - 1].imp, 6);       /* no baja de 6 aunque el LLM diga 4 */
    CKE(m.it[fa - 1].dim, 0);                                  /* el vector era del texto viejo */
    CKE(m.it[fb - 1].estado, MC_SUSTITUIDO); CKE(m.it[fb - 1].sust_por, fa); CKE(m.it[fb - 1].t_hasta, 777);
    CKE(mc_aplica_fusion(&m, fa, fb, &fu, 310, 777), -1);       /* b ya no esta activo */
    fu.redundante = 0; CKE(mc_aplica_fusion(&m, fa, c5, &fu, 310, 777), -1);

    /* 10. aplicar split */
    uint32_t big = mc_nodo_ruta(&m, "Viajes", 1), idsv[16];
    for (int i = 0; i < 15; i++) { char t[64]; sprintf(t, "Viaje numero %d a un sitio", i); idsv[i] = celda(&m, t, 5, 300); mc_asigna(&m, idsv[i], big); }
    CKE(mc_nodo_sobrecargado(&m), big);
    CK(mc_prompt_split(&m, big, (char[2000]){0}, 2000) > 100);
    memset(&sp, 0, sizeof sp); strcpy(sp.nombre_a, "Europa"); strcpy(sp.nombre_b, "Asia");
    for (int i = 0; i < 7; i++) sp.ids_a[sp.na++] = idsv[i];
    for (int i = 7; i < 12; i++) sp.ids_b[sp.nb++] = idsv[i];
    sp.ids_b[sp.nb++] = c5;                                    /* de otro nodo: se ignora */
    CKE(mc_aplica_split(&m, big, &sp), 12);
    uint32_t ne = mc_nodo_ruta(&m, "Viajes/Europa", 0), na = mc_nodo_ruta(&m, "Viajes/Asia", 0);
    CK(ne && na && m.nodo[ne].cuenta == 7 && m.nodo[na].cuenta == 5 && m.nodo[big].cuenta == 3);
    CKE(m.it[c5 - 1].nodo, n1);
    CKE(mc_nodo_sobrecargado(&m), 0);
    /* en el ultimo nivel el grupo B es un hermano, no un hijo */
    uint32_t hoja = mc_nodo_ruta(&m, "Ocio/Cine/Estrenos", 1), idh[14];
    for (int i = 0; i < 14; i++) { char t[64]; sprintf(t, "Pelicula %d vista en el cine", i); idh[i] = celda(&m, t, 5, 300); mc_asigna(&m, idh[i], hoja); }
    memset(&sp, 0, sizeof sp); strcpy(sp.nombre_b, "Clasicos"); for (int i = 0; i < 4; i++) sp.ids_b[sp.nb++] = idh[i];
    CKE(mc_aplica_split(&m, hoja, &sp), 4);
    uint32_t hermano = mc_nodo_ruta(&m, "Ocio/Cine/Clasicos", 0); CK(hermano && m.nodo[hermano].nivel == 2 && m.nodo[hermano].padre == m.nodo[hoja].padre);
    /* todos los ids en B: se queda la mitad */
    memset(&sp, 0, sizeof sp); strcpy(sp.nombre_b, "Todo"); for (int i = 4; i < 14; i++) sp.ids_b[sp.nb++] = idh[i];
    int mv = mc_aplica_split(&m, hoja, &sp); CK(mv >= 1 && mv <= 5);
    memset(&sp, 0, sizeof sp); CKE(mc_aplica_split(&m, hoja, &sp), 0);
    strcpy(sp.nombre_b, "Hoja"); sp.ids_b[sp.nb++] = idh[5]; CKE(mc_aplica_split(&m, 0, &sp), 0);

    /* 11. parejas redundantes y ramas */
    static struct mc q; CKE(mc_init(&q, 256), 0);
    uint32_t d1 = celda(&q, "El usuario tiene un perro que se llama Thor", 5, 0), d2 = celda(&q, "El usuario tiene un perro llamado Thor", 5, 0);
    uint32_t d3 = celda(&q, "La capital de Francia es Paris y esta junto al Sena", 5, 0);
    uint32_t cur = 0, a = 0, b = 0;
    CK(mc_par_redundante(&q, &cur, 0, 0, &a, &b)); CKE(a, d1); CKE(b, d2);
    uint64_t vistos[1] = { ((uint64_t)d1 << 32) | d2 };
    cur = 0; CK(!mc_par_redundante(&q, &cur, vistos, 1, &a, &b));         /* ya juzgado */
    (void)d3;
    int8_t va[MC_DIM_MAX], vb[MC_DIM_MAX], vc[MC_DIM_MAX];
    vec_tema(2, 1, va, 64); vec_tema(2, 2, vb, 64); vec_tema(5, 3, vc, 64);
    uint32_t e1 = celda(&q, "Hecho uno sobre cocina italiana", 5, 0), e2 = celda(&q, "Otro dato muy diferente de verdad", 5, 0), e3 = celda(&q, "Un tercero cualquiera sin parecido", 5, 0);
    mc_set_vec(&q, e1, va, 64); mc_set_vec(&q, e2, vb, 64); mc_set_vec(&q, e3, vc, 64);
    uint64_t v2[1] = { ((uint64_t)d1 << 32) | d2 };
    cur = 0; CK(mc_par_redundante(&q, &cur, v2, 1, &a, &b)); CK((a == e1 && b == e2));      /* por coseno, aunque las palabras no se parezcan */
    uint64_t v3[2] = { ((uint64_t)d1 << 32) | d2, ((uint64_t)e1 << 32) | e2 };
    cur = 0; CK(!mc_par_redundante(&q, &cur, v3, 2, &a, &b));
    CKE(mc_par_redundante(&q, &cur, 0, 0, &a, &b) != 0, 1);
    /* ramas: dos hojas con una celda cada una y vectores parecidos; una tercera distinta */
    uint32_t r1 = mc_nodo_ruta(&q, "Cocina/Postres", 1), r2 = mc_nodo_ruta(&q, "Cocina/Reposteria", 1), r3 = mc_nodo_ruta(&q, "Cocina/Gatos", 1);
    mc_asigna(&q, e1, r1); mc_asigna(&q, e2, r2); mc_asigna(&q, e3, r3); mc_recuenta(&q);
    int sim = 0; CK(mc_par_ramas(&q, 0, 0, &a, &b, &sim)); CK(a == r1 && b == r2 && sim >= 50);
    uint64_t vr[1] = { ((uint64_t)r1 << 32) | r2 };
    CK(!mc_par_ramas(&q, vr, 1, &a, &b, &sim));                 /* r3 no se parece: no se pregunta */
    char pr[3000];
    CK(mc_prompt_unir(&q, r1, r2, 83, pr, sizeof pr) > 100 && strstr(pr, "0.83") && strstr(pr, "Postres") && strstr(pr, "Reposteria"));
    mc_prompt_unir(&q, r1, r2, 7, pr, sizeof pr); CK(strstr(pr, "0.07"));
    mc_prompt_unir(&q, r1, r2, 100, pr, sizeof pr); CK(strstr(pr, "1.00"));
    mc_prompt_unir(&q, r1, r2, -1, pr, sizeof pr); CK(!strstr(pr, "similitud"));
    CKE(mc_prompt_unir(&q, r1, 99, 5, pr, sizeof pr), 0);
    mc_destroy(&q);

    /* 12. candidato al rescate: la franja justo encima del umbral */
    static struct mc z; CKE(mc_init(&z, 64), 0);
    uint32_t k1 = celda(&z, "Casi olvidado", 2, 0), k2 = celda(&z, "Reciente", 2, 200), k3 = celda(&z, "Importantisimo", 9, 0);
    (void)k2; (void)k3;
    uint32_t edad = 0; while (mc_eff_q8(2, edad, 20) >= 128 + 40) edad++;
    CKE(mc_candidato_rescate(&z, edad, 10, 20, 128, 0, 0), k1);
    { uint32_t ya[1] = { k1 }; CKE(mc_candidato_rescate(&z, edad, 10, 20, 128, ya, 1), 0); }        /* ya juzgada: se salta */
    CKE(mc_candidato_rescate(&z, 5, 10, 20, 128, 0, 0), 0);           /* demasiado joven */
    CKE(mc_candidato_rescate(&z, 100000, 10, 20, 128, 0, 0), 0);      /* ya por debajo del umbral: no es candidato, es olvido */
    mc_destroy(&z);

    /* 13. prompts */
    uint32_t tmp[3] = { c5, c9, 9999 };
    char big_[4000]; int l = mc_prompt_ordena(&m, tmp, 3, big_, sizeof big_);
    CK(l > 200 && strstr(big_, "Personal/Mascotas") && strstr(big_, "[") && strstr(big_, "asignaciones") && strstr(big_, "Thor"));
    CK(!strstr(big_, "[9999]"));
    mc_nodo_ruta_texto(&m, n1, r, sizeof r);
    int ld = mc_prompt_describe(&m, n1, big_, sizeof big_); CK(ld > 100 && strstr(big_, "Mascotas") && strstr(big_, "description") && strstr(big_, "profundidad 2"));
    CKE(mc_prompt_describe(&m, 0, big_, sizeof big_), 0);
    CK(mc_prompt_fusion(&m, c5, c9, big_, sizeof big_) > 100 && strstr(big_, "redundant"));
    CKE(mc_prompt_fusion(&m, c5, 9999, big_, sizeof big_), 0);
    CK(mc_prompt_rescate(&m, c5, big_, sizeof big_) > 50 && strstr(big_, "preserve") && strstr(big_, "Thor"));
    CKE(mc_prompt_rescate(&m, 9999, big_, sizeof big_), 0);
    char pequeno[40]; CK(mc_prompt_ordena(&m, tmp, 3, pequeno, sizeof pequeno) < 40);     /* no se sale del buffer */
    CKE(mc_prompt_ordena(&m, tmp, 3, pequeno, 0), 0);

    /* 14. el nodo viaja en los fragmentos; el arbol y el reloj se guardan aparte */
    static struct mc w; CKE(mc_init(&w, 4096), 0);
    static uint8_t buf[1 << 22];
    for (uint32_t i = 0; i < mc_nfrag(&m); i++) {
        int len = mc_frag_guarda(&m, i, buf, sizeof buf); CK(len > 0);
        CKE(mc_frag_carga(&w, buf, (uint32_t)len), 0);
    }
    CKE(w.n, m.n);
    int igual = 1; for (uint32_t i = 0; i < m.n; i++) if (w.it[i].nodo != m.it[i].nodo || w.it[i].estado != m.it[i].estado || w.it[i].imp != m.it[i].imp) igual = 0;
    CK(igual); CKE(w.it[c5 - 1].nodo, n1);
    m.tick = 12345; m.pasos = 678;
    int al = mc_arbol_guarda(&m, buf, sizeof buf); CK(al > 20 && (uint32_t)al <= mc_arbol_max_bytes());
    CKE(mc_arbol_carga(&w, buf, (uint32_t)al), 0);
    CKE(w.tick, 12345); CKE(w.pasos, 678);
    mc_nodo_ruta_texto(&w, n1, r, sizeof r); CKS(r, "Personal/Mascotas"); CKS(w.nodo[n1].desc, "Animales de compania del usuario");
    CKE(w.nodo[n1].cuenta, m.nodo[n1].cuenta); CK(w.nodo[fijo].fijo);
    uint8_t malo[8192]; memcpy(malo, buf, (size_t)al);
    malo[30] ^= 0x55; CKE(mc_arbol_carga(&w, malo, (uint32_t)al), -2);                    /* CRC */
    CKE(mc_arbol_carga(&w, buf, 10), -1); CKE(mc_arbol_carga(&w, buf, (uint32_t)al - 1), -2);
    buf[0] = 'X'; CKE(mc_arbol_carga(&w, buf, (uint32_t)al), -1); buf[0] = 'S';
    {   /* un hijo cuyo padre no existe: se rechaza sin tocar lo que habia */
        struct mc extra; memset(&extra, 0, sizeof extra); extra.nodo[3].usado = 1; extra.nodo[3].padre = 9; extra.nodo[3].nivel = 1; strcpy(extra.nodo[3].nombre, "huerfano");
        static uint8_t b2[65536]; int l2 = mc_arbol_guarda(&extra, b2, sizeof b2); CK(l2 > 0);
        uint32_t ant = w.tick; CKE(mc_arbol_carga(&w, b2, (uint32_t)l2), -5); CKE(w.tick, ant); CK(w.nodo[n1].usado);
    }
    CKE(mc_arbol_guarda(&m, buf, 10), -1);
    /* un fragmento antiguo (277) guardaba 0 donde ahora va el nodo: se lee como "sin ordenar" */
    static struct mc old; CKE(mc_init(&old, 16), 0);
    celda(&old, "dato viejo", 5, 0);
    int lo = mc_frag_guarda(&old, 0, buf, sizeof buf); CK(lo > 0);
    static struct mc old2; CKE(mc_init(&old2, 16), 0);
    CKE(mc_frag_carga(&old2, buf, (uint32_t)lo), 0); CKE(old2.it[0].nodo, 0);
    mc_destroy(&old); mc_destroy(&old2);

    /* 15. cambio de texto con el arena */
    uint32_t ct = celda(&m, "Texto original", 5, 0);
    CKE(mc_set_texto(&m, ct, "Texto nuevo mas largo que el original, con acentos: ñ"), 0); CKS(m.it[ct - 1].texto, "Texto nuevo mas largo que el original, con acentos: ñ");
    CKE(m.it[ct - 1].tlen, strlen(m.it[ct - 1].texto)); CKE(mc_set_texto(&m, 99999, "x"), -1); CKE(mc_set_texto(&m, ct, 0), -1);
    CKE(mc_set_imp(&m, ct, 99, 7), 0); CKE(m.it[ct - 1].imp, 10); CKE(m.it[ct - 1].tick, 7); CKE(mc_set_imp(&m, ct, -3, 7), 0); CKE(m.it[ct - 1].imp, 1);

    mc_destroy(&w); mc_destroy(&m);
    printf("test_memoria_279: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
