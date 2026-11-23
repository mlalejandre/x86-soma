/* SOMA_283_MEMORIA_APP: piezas del nucleo que usa la aplicacion de Memoria: borrar de verdad (compactar), quitar y renombrar ramas,
 * y copiar recuerdos entre cartuchos escribiendo .smem y leyendolo con el importador. */
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

static void vec(int8_t *v, int k) { memset(v, 0, 64); v[k % 64] = 127; }
static uint32_t busca(const struct mc *m, const char *sub) { for (uint32_t i = 0; i < m->n; i++) if (strstr(m->it[i].texto, sub)) return m->it[i].id; return 0; }

/* copia unos ids de a en b como lo hace la aplicacion: .smem linea a linea */
static void copia(struct mc *a, struct mc *b, const uint32_t *ids, uint32_t n, int con_vec, uint32_t *celdas, uint32_t *rep)
{
    static char l[MC_SMEM_LINEA_MAX];
    struct mc_imp s; memset(&s, 0, sizeof s); s.vec_ok = 1;
    CKE(mc_imp_ini(b, &s, n + 1), 0); s.vec_ok = (uint32_t)con_vec;
    CK(mc_smem_cabecera("bge-m3", 64, l, sizeof l) > 0); mc_imp_linea(b, &s, l); s.vec_ok = (uint32_t)con_vec;

    for (uint32_t i = 0; i < n; i++) {
        const struct mc_item *e = mc_get(a, ids[i]); if (!e) continue;
        if (e->kind == MC_CELDA && e->nodo) { CK(mc_smem_nodo(a, e->nodo, l, sizeof l) > 0); mc_imp_linea(b, &s, l); }
        uint32_t so = 0; for (uint32_t j = 0; j < n; j++) if (e->sust_por && ids[j] == e->sust_por) so = j + 1;
        CK(mc_smem_item(a, ids[i], e->kind == MC_CELDA ? i + 1 : 0, so, con_vec, l, sizeof l) > 0);
        mc_imp_linea(b, &s, l);
    }
    *celdas = s.celdas + s.epis; *rep = s.repetidas;
    mc_imp_fin(b, &s);
}

int main(void)
{
    static struct mc m; int8_t v[64];
    CKE(mc_init(&m, 4096), 0);
    m.tick = 30;
    uint32_t a = mc_add(&m, MC_CELDA, 0, "El usuario vive en Madrid", "charla", 6, 1000, 1, 0, 0);
    vec(v, 1); uint32_t b = mc_add(&m, MC_CELDA, 0, "El usuario vive en Bilbao", "charla", 7, 2000, 2, v, 64);
    mc_set_estado(&m, a, MC_SUSTITUIDO, 2000, b);
    uint32_t e1 = mc_add(&m, MC_EPISODIO, MC_ROL_USUARIO, "Ahora vivo en Bilbao\tcon tab", "", 5, 2000, 2, 0, 0);
    vec(v, 2); uint32_t c = mc_add(&m, MC_CELDA, 0, "Tiene un perro llamado Thor", "nota\\a", 8, 3000, 3, v, 64);
    uint32_t d = mc_add(&m, MC_CELDA, 0, "Le gusta el cafe solo", "", 3, 3100, 3, 0, 0);
    uint32_t mas = mc_nodo_ruta(&m, "Personal/Mascotas", 1), viv = mc_nodo_ruta(&m, "Personal/Vivienda", 1), hab = mc_nodo_ruta(&m, "Personal/Habitos/Bebidas", 1);
    mc_asigna(&m, c, mas); mc_asigna(&m, b, viv); mc_asigna(&m, a, viv); mc_asigna(&m, d, hab);
    mc_nodo_set_desc(&m, mas, "Animales del usuario");
    for (uint32_t i = 0; i < 3; i++) mc_set_texto(&m, d, i == 2 ? "Le gusta el cafe solo y sin azucar" : "Le gusta el cafe");   /* basura en la arena */
    uint32_t antes = mc_bytes(&m);
    CK(antes > 0);

    /* 1. renombrar y comprobar la pertenencia a ramas */
    uint32_t per = mc_nodo_ruta(&m, "Personal", 0);
    CK(mc_nodo_dentro(&m, hab, per) && mc_nodo_dentro(&m, per, per) && !mc_nodo_dentro(&m, per, hab) && !mc_nodo_dentro(&m, mas, viv));
    CKE(mc_nodo_renombra(&m, mas, "Vivienda"), -1);                       /* ya hay un hermano con ese nombre */
    CKE(mc_nodo_renombra(&m, mas, "  Animales  "), 0); CK(!strcmp(m.nodo[mas].nombre, "Animales") && m.nodo[mas].fijo);
    CKE(mc_nodo_renombra(&m, mas, ""), -1); CKE(mc_nodo_renombra(&m, 200, "x"), -1);
    CK(mc_nodo_ruta(&m, "Personal/Animales", 0) == mas);

    /* 2. borrar de verdad: la celda que sustituia (Bilbao) y el episodio */
    uint8_t q[16] = {0}; q[b - 1] = 1; q[e1 - 1] = 1;
    CKE(mc_compacta(&m, q), 2);
    CKE(m.n, 3);
    uint32_t na = busca(&m, "Madrid"), nc = busca(&m, "Thor"), nd = busca(&m, "cafe");
    CK(na == 1 && nc == 2 && nd == 3);                                     /* ids sin huecos, en el mismo orden */
    CKE(m.it[na - 1].estado, MC_ACTIVO); CKE(m.it[na - 1].sust_por, 0); CKE(m.it[na - 1].t_hasta, 0);     /* su sustituta se borro: vuelve a valer */
    CKE(m.it[na - 1].nodo, viv); CKE(m.it[nc - 1].nodo, mas); CK(m.it[nc - 1].vec && m.it[nc - 1].vec[2] == 127 && m.it[nc - 1].dim == 64);
    CK(!strcmp(m.it[nd - 1].texto, "Le gusta el cafe solo y sin azucar") && !strcmp(m.it[nc - 1].fuente, "nota\\a"));
    CKE(m.tick, 30); CK(!strcmp(m.nodo[mas].desc, "Animales del usuario")); CK(m.sucio[0] && m.arbol_sucio);
    CK(mc_bytes(&m) < antes);                                              /* la arena se ha recuperado */
    CKE(m.nodo[viv].cuenta, 1);
    { uint8_t z[4] = {0}; CKE(mc_compacta(&m, z), 0); CKE(m.n, 3); }
    /* sustitucion que sobrevive: renumerada */
    uint32_t x1 = mc_add(&m, MC_CELDA, 0, "Trabaja en Iberdrola", "", 6, 4000, 4, 0, 0), x2 = mc_add(&m, MC_CELDA, 0, "Trabaja en Repsol", "", 6, 5000, 5, 0, 0);
    mc_set_estado(&m, x1, MC_SUSTITUIDO, 5000, x2);
    { uint8_t z[8] = {0}; z[0] = 1; CKE(mc_compacta(&m, z), 1); }
    uint32_t y1 = busca(&m, "Iberdrola"), y2 = busca(&m, "Repsol");
    CKE(m.it[y1 - 1].estado, MC_SUSTITUIDO); CKE(m.it[y1 - 1].sust_por, y2); CKE(y2, x2 - 1);

    /* 3. quitar una rama: sus celdas quedan sin ordenar y marcadas */
    uint8_t mk[16] = {0};
    uint32_t nn = mc_nodo_quita(&m, per, mk);
    CKE(nn, 2); CK(mk[nc - 2] == 1 || mk[busca(&m, "Thor") - 1] == 1);
    CK(!m.nodo[per].usado && !m.nodo[mas].usado && !m.nodo[viv].usado && !m.nodo[hab].usado);
    CKE(m.it[busca(&m, "Thor") - 1].nodo, 0); CKE(mc_nodo_quita(&m, per, 0), 0);

    /* 4. copiar a otro cartucho por .smem (con vectores, rama y sustitucion) */
    static struct mc o; CKE(mc_init(&o, 4096), 0); o.tick = 7;
    uint32_t t1 = mc_nodo_ruta(&m, "Trabajo/Empleos", 1); mc_nodo_set_desc(&m, t1, "Donde trabaja");
    mc_asigna(&m, y1, t1); mc_asigna(&m, y2, t1);
    uint32_t th = busca(&m, "Thor"); vec(v, 9); mc_set_vec(&m, th, v, 64);
    uint32_t ep = mc_add(&m, MC_EPISODIO, MC_ROL_ASISTENTE, "Hola, \\ que tal\nlinea dos", "", 5, 6000, 6, v, 64);
    uint32_t ids[4] = { y1, y2, th, ep }, cel = 0, rep = 0;
    copia(&m, &o, ids, 4, 1, &cel, &rep);
    CKE(cel, 4); CKE(rep, 0); CKE(o.n, 4);
    uint32_t o1 = busca(&o, "Iberdrola"), o2 = busca(&o, "Repsol"), o3 = busca(&o, "Thor"), o4 = busca(&o, "Hola");
    CKE(o.it[o1 - 1].estado, MC_SUSTITUIDO); CKE(o.it[o1 - 1].sust_por, o2); CKE(o.it[o1 - 1].t_hasta, 5000); CKE(o.it[o2 - 1].t, 5000);
    char rt[160]; mc_nodo_ruta_texto(&o, o.it[o2 - 1].nodo, rt, sizeof rt); CK(!strcmp(rt, "Trabajo/Empleos"));
    CK(!strcmp(o.nodo[o.it[o2 - 1].nodo].desc, "Donde trabaja"));
    CK(o.it[o3 - 1].vec && o.it[o3 - 1].dim == 64 && o.it[o3 - 1].vec[9] == 127); CKE(o.it[o3 - 1].nodo, 0);
    CK(!strcmp(o.it[o4 - 1].texto, "Hola, \\ que tal\nlinea dos") && o.it[o4 - 1].kind == MC_EPISODIO && o.it[o4 - 1].rol == MC_ROL_ASISTENTE && o.it[o4 - 1].vec);
    CKE(o.it[o3 - 1].imp, 8); CK(!strcmp(o.it[o3 - 1].fuente, "nota\\a")); CKE(o.it[o3 - 1].tick, 7);
    copia(&m, &o, ids, 4, 1, &cel, &rep); CKE(cel, 0); CKE(rep, 4); CKE(o.n, 4);           /* copiar otra vez no duplica */
    static struct mc o2_; CKE(mc_init(&o2_, 64), 0);
    copia(&m, &o2_, ids, 3, 0, &cel, &rep); CKE(cel, 3); CK(!o2_.it[busca(&o2_, "Thor") - 1].vec);           /* sin vectores */
    /* lineas largas: texto maximo lleno de caracteres a escapar, con vector maximo */
    { static char big[MC_TEXTO_MAX + 1]; memset(big, '\t', MC_TEXTO_MAX); big[MC_TEXTO_MAX] = 0; static int8_t vb[MC_DIM_MAX]; memset(vb, 3, sizeof vb);
      static char fb[MC_FUENTE_MAX + 1]; memset(fb, '\\', MC_FUENTE_MAX); fb[MC_FUENTE_MAX] = 0;
      uint32_t g = mc_add(&m, MC_CELDA, 0, big, fb, 5, 1, 1, vb, MC_DIM_MAX);
      static char l[MC_SMEM_LINEA_MAX]; uint32_t n = mc_smem_item(&m, g, 1, 0, 1, l, sizeof l); CK(n > 4000 && l[n - 1] == '\n');
      CKE(mc_smem_item(&m, g, 1, 0, 1, l, 200), 0); }

    /* 5. podar una rama vacia deja sin tema lo archivado que colgaba de ella (y un tema nuevo en el mismo hueco no lo hereda) */
    { static struct mc p5; CKE(mc_init(&p5, 64), 0);
      uint32_t c1 = mc_add(&p5, MC_CELDA, 0, "Le gusta el cafe", "", 1, 1, 1, 0, 0), h1 = mc_nodo_ruta(&p5, "Habitos", 1);
      mc_asigna(&p5, c1, h1); mc_set_estado(&p5, c1, MC_ARCHIVADO, 0, 0);
      CKE(mc_poda_nodos(&p5), 1); CKE(p5.it[c1 - 1].nodo, 0);
      uint32_t h2 = mc_nodo_ruta(&p5, "Proyectos", 1); CKE(h2, h1); CKE(mc_nodo_celdas(&p5, h2, (uint32_t[4]){0}, 4), 0);
      uint32_t r = c1; CKE(mc_restaura(&p5, &r, 1, 5), 1); CKE(p5.it[c1 - 1].nodo, 0);
      mc_destroy(&p5); }
    mc_destroy(&m); mc_destroy(&o); mc_destroy(&o2_);
    printf("test_memoria_283: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
