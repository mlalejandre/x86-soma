/* SOMA_280_IMPORTAR: el importador .smem del nucleo (src/agent/memoria_core.c) con los archivos que escribe tools/exporta_memoria.py.
 * Uso: test_memoria_280 CARPETA_DE_TRABAJO   (la deja tests/test_exporta_280.py: salida/memoria-NNN.smem, esperado.txt, vec_ref.json, vec_ref.q) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "memoria_core.h"

static int checks, fails;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CKE(a, b) do { long long _a = (long long)(a), _b = (long long)(b); checks++; if (_a != _b) { fails++; printf("  FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
void *mc_alloc(uint32_t n) { return calloc(1, n); }
void mc_free(void *p) { free(p); }

static char *lee(const char *ruta, long *n)
{
    FILE *f = fopen(ruta, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)l + 1); if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return 0; }
    b[l] = 0; fclose(f); if (n) *n = l; return b;
}
static int esperado(const char *dir, const char *clave)
{
    char r[600]; snprintf(r, sizeof r, "%s/esperado.txt", dir);
    char *b = lee(r, 0); if (!b) return -1;
    int v = -1; size_t kl = strlen(clave);
    for (char *l = strtok(b, "\n"); l; l = strtok(0, "\n")) if (!strncmp(l, clave, kl) && l[kl] == ' ') v = atoi(l + kl + 1);
    free(b); return v;
}
/* importa un archivo entero; devuelve las lineas leidas */
static int importa(struct mc *m, struct mc_imp *s, const char *ruta, int *rcs)
{
    long n = 0; char *b = lee(ruta, &n); if (!b) return -1;
    int lineas = 0;
    for (char *l = b; *l;) {
        char *e = l; while (*e && *e != '\n') e++;
        char sv = *e; *e = 0;
        int r = mc_imp_linea(m, s, l); if (rcs && r < 0) (*rcs)++;
        lineas++;
        *e = sv; l = sv ? e + 1 : e;
    }
    free(b);
    return lineas;
}
static uint32_t busca(const struct mc *m, const char *sub)
{
    for (uint32_t i = 0; i < m->n; i++) if (strstr(m->it[i].texto, sub)) return m->it[i].id;
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/f280";
    char ruta[700];
    int np = esperado(dir, "partes");
    CK(np >= 2);
    static struct mc m;
    CKE(mc_init(&m, 4096), 0);
    m.tick = 17;

    /* 1. importar todas las partes */
    struct mc_imp s; memset(&s, 0, sizeof s); s.vec_ok = 1; s.ahora = 1700000000u;
    uint32_t celdas = 0, epis = 0, repe = 0, sinvec = 0;
    int malas = 0;
    for (int p = 1; p <= np; p++) {
        snprintf(ruta, sizeof ruta, "%s/salida/memoria-%03d.smem", dir, p);
        CKE(mc_imp_ini(&m, &s, 5000), 0);
        s.vec_ok = 1;
        CK(importa(&m, &s, ruta, &malas) > 3);
        CKE(s.cab_ok, 1); CKE(s.cab_dim, 1024); CKE(s.cab_parte, p); CKE(s.cab_partes, np); CK(!strcmp(s.modelo, "bge-m3"));
        celdas += s.celdas; epis += s.epis; repe += s.repetidas; sinvec += s.sin_vec;
        mc_imp_fin(&m, &s);
    }
    CKE(malas, 0);
    CKE(celdas, esperado(dir, "celdas") - 1); CKE(epis, esperado(dir, "episodios")); CKE(repe, 1); CKE(sinvec, 0);          /* c9 repite a c1 (sin mayusculas ni puntuacion): se salta */
    uint32_t c, e, a, su, sv; mc_stats(&m, &c, &e, &a, &su, &sv);
    CKE(c, esperado(dir, "activas") - 1); CKE(a, esperado(dir, "archivadas")); CKE(su, esperado(dir, "sustituidas")); CKE(e, esperado(dir, "episodios"));
    CKE(m.n - sv, esperado(dir, "convec") + esperado(dir, "episodios_con_vec"));

    /* 2. los hechos y su estado */
    uint32_t bil = busca(&m, "Bilbao"), mad = busca(&m, "vivia en Madrid"), peni = busca(&m, "penicilina");
    CK(bil && mad && peni);
    CKE(m.it[bil - 1].estado, MC_ACTIVO); CKE(m.it[bil - 1].imp, 7);
    CKE(m.it[mad - 1].estado, MC_SUSTITUIDO); CKE(m.it[mad - 1].sust_por, bil); CK(m.it[mad - 1].t_hasta > 0); CK(m.it[mad - 1].t < m.it[mad - 1].t_hasta);
    CKE(m.it[busca(&m, "olvidado por el decaimiento") - 1].estado, MC_ARCHIVADO);
    CKE(m.it[busca(&m, "olvidado por el decaimiento") - 1].t_hasta, 0);                       /* se puede restaurar como lo archivado por el olvido */
    CKE(m.it[bil - 1].t, 1714731630u);
    CK(m.it[busca(&m, "Cumple") - 1].t == 1678780800u);                                         /* 2023-03-14 08:00:00 UTC */
    uint32_t miso = busca(&m, "Miso");
    CK(miso && strstr(m.it[miso - 1].texto, "\nsegunda linea \\ barra") && !strchr(m.it[miso - 1].texto, '\t'));
    CK(!busca(&m, "Hunter2") && !busca(&m, "sk-abcdef"));
    CK(m.it[bil - 1].tick == 17);                                                               /* reloj de SOMA: no nacen ya olvidadas */

    /* 3. el arbol: rutas, descripciones y a que nodo cuelga cada celda */
    CK(m.it[peni - 1].nodo != 0);
    char rt[200]; mc_nodo_ruta_texto(&m, m.it[peni - 1].nodo, rt, sizeof rt); CK(!strcmp(rt, "Salud/Alergias"));
    CK(!strcmp(m.nodo[m.it[peni - 1].nodo].desc, "Alergias conocidas")); CKE(m.nodo[m.it[peni - 1].nodo].sucio, 0);
    mc_nodo_ruta_texto(&m, m.it[miso - 1].nodo, rt, sizeof rt); CK(!strcmp(rt, "Vida/Casa Hogar/Mascotas"));
    CK(mc_nodo_ruta(&m, "Vida/Casa Hogar/Mascotas/Gatos", 0) == m.it[miso - 1].nodo);          /* mc_nodo_ruta corta en 3 niveles */
    CKE(mc_sin_nodo(&m, (uint32_t[4]){0}, 4), 0);                                              /* todas las celdas vienen ordenadas */
    CKE(mc_recuenta(&m) >= 5, 1);
    CK(m.nodo[m.it[bil - 1].nodo].cuenta >= 1);

    /* 4. los vectores son los del nucleo: misma cuantizacion que mc_parse_embedding */
    snprintf(ruta, sizeof ruta, "%s/vec_ref.json", dir);
    char *js = lee(ruta, 0); static int8_t qc[MC_DIM_MAX];
    CKE(mc_parse_embedding(js, qc, MC_DIM_MAX), 1024); free(js);
    CK(m.it[bil - 1].vec && m.it[bil - 1].dim == 1024);
    int maxd = 0; for (int i = 0; i < 1024; i++) { int d = m.it[bil - 1].vec[i] - qc[i]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
    CK(maxd <= 1);
    CK(mc_dot(m.it[bil - 1].vec, qc, 1024) > MC_COS_UNO - 200);
    { uint32_t ids[4]; int32_t sc[4]; int k = mc_busca(&m, qc, 1024, "x", 1u << MC_CELDA, 0, 6000, 4, ids, sc); CK(k >= 1 && ids[0] == bil); }     /* se busca con ellos */

    /* 5. importar otra vez no duplica nada */
    uint32_t antes = m.n; memset(&s, 0, sizeof s); s.vec_ok = 1;
    for (int p = 1; p <= np; p++) {
        snprintf(ruta, sizeof ruta, "%s/salida/memoria-%03d.smem", dir, p);
        CKE(mc_imp_ini(&m, &s, 5000), 0); s.vec_ok = 1;
        importa(&m, &s, ruta, 0);
        CKE(s.celdas + s.epis, 0);
        mc_imp_fin(&m, &s);
    }
    CKE(m.n, antes);

    /* 6. sin vectores, y con memoria que ya tenia cosas (la repetida se salta, la sustitucion se enlaza) */
    static struct mc m2; CKE(mc_init(&m2, 4096), 0);
    uint32_t previo = mc_add(&m2, MC_CELDA, MC_ROL_USUARIO, "EL USUARIO es alérgico a la PENICILINA", "x", 5, 1, 0, 0, 0);
    memset(&s, 0, sizeof s); s.vec_ok = 0;
    snprintf(ruta, sizeof ruta, "%s/salida/memoria-001.smem", dir);
    CKE(mc_imp_ini(&m2, &s, 5000), 0); s.vec_ok = 0;
    importa(&m2, &s, ruta, 0);
    CKE(s.repetidas, 2); CK(s.sin_vec > 0); mc_imp_fin(&m2, &s);       /* la que ya tenia SOMA y c9 */
    { uint32_t c2, e2, a2, su2, sv2; mc_stats(&m2, &c2, &e2, &a2, &su2, &sv2); CKE(sv2, m2.n); }   /* ninguno con vector */
    (void)previo;

    /* 7. dimension que no es la esperada: se queda sin vector, no se rompe */
    static struct mc m3; CKE(mc_init(&m3, 4096), 0);
    memset(&s, 0, sizeof s); s.dim_vec = 768; s.vec_ok = 1;
    CKE(mc_imp_ini(&m3, &s, 5000), 0); s.vec_ok = 1; s.dim_vec = 768;
    importa(&m3, &s, ruta, 0); CK(s.sin_vec > 0); mc_imp_fin(&m3, &s);
    { uint32_t c3, e3, a3, su3, sv3; mc_stats(&m3, &c3, &e3, &a3, &su3, &sv3); CKE(sv3, m3.n); }

    /* 8. lineas mal formadas, sin cabecera, vacias y memoria llena */
    static struct mc m4; CKE(mc_init(&m4, 5), 0);
    memset(&s, 0, sizeof s); s.vec_ok = 1; CKE(mc_imp_ini(&m4, &s, 100), 0); s.vec_ok = 1;
    char l1[] = "C\t1\t5\t100\t0\t0\t0\t\tfuente\tUn hecho cualquiera"; CKE(mc_imp_linea(&m4, &s, l1), 0);
    char l2[] = "C\tX\t5"; CKE(mc_imp_linea(&m4, &s, l2), -1);
    char l3[] = "Z\tlo que sea"; CKE(mc_imp_linea(&m4, &s, l3), -1);
    char l4[] = "C\t2\t5\t100\t0\t0\t0\t\tf\t"; CKE(mc_imp_linea(&m4, &s, l4), -1);               /* sin texto */
    char l5[] = "C\t900\t5\t100\t0\t0\t0\t\tf\tOrdinal fuera de la parte"; CKE(mc_imp_linea(&m4, &s, l5), -1);
    char l6[] = "C\t2\t5\t100\t0\t0\t0\t\tf\tOtro hecho distinto\tAAAA=!!"; CKE(mc_imp_linea(&m4, &s, l6), 0); CK(s.sin_vec == 1);   /* vector ilegible: la celda entra igual */
    char l7[] = ""; CKE(mc_imp_linea(&m4, &s, l7), 1);
    char l8[] = "#comentario cualquiera"; CKE(mc_imp_linea(&m4, &s, l8), 1); CKE(s.cab_ok, 0);
    char l9[] = "E\t100\t0\tUn mensaje literal\n"; CKE(mc_imp_linea(&m4, &s, l9), 0);
    char la[] = "E\t100\t7\tOtro mensaje con rol raro"; CKE(mc_imp_linea(&m4, &s, la), 0); CKE(m4.it[3].rol, MC_ROL_USUARIO);
    char lb[] = "E\t0\t1\tSin fecha"; CKE(mc_imp_linea(&m4, &s, lb), 0); CKE(m4.it[4].t, 0);                    /* ahora = 0 en este test */
    char lc[] = "E\t100\t1\tNo cabe ya"; CKE(mc_imp_linea(&m4, &s, lc), -2); CKE(s.lleno, 1);
    CK(s.malas >= 4); mc_imp_fin(&m4, &s);

    /* 9. con las celdas importadas, guardar por fragmentos y volver a cargar no pierde nada (estado, sustitucion, nodo) */
    static uint8_t buf[2 * 1024 * 1024];
    static struct mc w; CKE(mc_init(&w, 4096), 0);
    for (uint32_t i = 0; i < mc_nfrag(&m); i++) { int len = mc_frag_guarda(&m, i, buf, sizeof buf); CK(len > 0); CKE(mc_frag_carga(&w, buf, (uint32_t)len), 0); }
    CKE(w.n, m.n); CKE(w.it[mad - 1].sust_por, bil); CKE(w.it[mad - 1].estado, MC_SUSTITUIDO); CKE(w.it[peni - 1].nodo, m.it[peni - 1].nodo);
    CK(w.it[bil - 1].vec && !memcmp(w.it[bil - 1].vec, m.it[bil - 1].vec, 1024));
    { int la_ = mc_arbol_guarda(&m, buf, sizeof buf); CK(la_ > 0); CKE(mc_arbol_carga(&w, buf, (uint32_t)la_), 0); CK(!strcmp(w.nodo[w.it[peni - 1].nodo].desc, "Alergias conocidas")); }

    /* 10. despues de importar, el ciclo de sueno trata lo importado como a cualquier otro recuerdo */
    CK(mc_nodo_sucio(&m) != 0);                                                                  /* nodos sin descripcion de memorIA: se describiran */
    CKE(mc_olvida(&m, 17 + 5, 10, 20, 128), 0);                                                  /* recien llegados: ni uno se olvida */

    mc_destroy(&m); mc_destroy(&m2); mc_destroy(&m3); mc_destroy(&m4); mc_destroy(&w);
    printf("test_memoria_280: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
