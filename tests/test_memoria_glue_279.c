/* SOMA_279_SUENO: el pegamento de la memoria (src/agent/memoria.c) ejecutado en el anfitrion: se incluye el .c entero y se sustituye el
 * kernel por piezas minimas (un disco en memoria, un servidor de embeddings y un LLM falsos). Prueba las 9 tareas del ciclo de sueno,
 * la persistencia (guardar, "reiniciar", cargar), la restauracion de lo archivado y las ordenes `memoria ...`. Con ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memoria.c"

static int checks, fails;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CKE(a, b) do { long long _a = (long long)(a), _b = (long long)(b); checks++; if (_a != _b) { fails++; printf("  FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

/* ---------------- el "kernel" */
void *kmalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kmutex_init(struct kmutex *m, const char *name) { (void)m; (void)name; }
void kmutex_lock(struct kmutex *m) { (void)m; }
void kmutex_unlock(struct kmutex *m) { (void)m; }
void kprint(const char *s) { (void)s; }
void kprint_dec(uint32_t v) { (void)v; }
static struct tcb g_tcb;
struct tcb *thread_current(void) { return &g_tcb; }
struct tcb *thread_create(const char *n, thread_func_t f, void *a) { (void)n; (void)f; (void)a; return &g_tcb; }
void thread_sleep(uint32_t ms) { (void)ms; }
static uint64_t g_ms = 100000;
uint64_t timer_get_uptime_ms(void) { return g_ms; }
void rtc_get_datetime(struct rtc_time *t) { t->year = 2026; t->month = 11; t->day = 23; t->hour = 17; t->min = 0; t->sec = 0; }
void net_client_lock(void) {}
void net_client_unlock(void) {}
char llm_last_error[200];
int llm_launcher_emb_status(void) { return 1; }
int llm_launcher_emb_start(uint32_t w, void (*p)(uint32_t)) { (void)w; (void)p; return 1; }
int llm_launcher_emb_stop(void) { return 0; }
const struct llm_conf *llm_conf_get(void) { static struct llm_conf c; static int ini; if (!ini) { llm_conf_defaults(&c); c.prov = LLM_LOCAL; strcpy(c.base[LLM_LOCAL], "http://192.168.1.200:8087"); ini = 1; } return &c; }

/* disco en memoria */
static struct { char name[64]; char *d; uint32_t n; } fsx[300]; static int nfs;
static int fs_find(const char *n) { for (int i = 0; i < nfs; i++) if (!strcmp(fsx[i].name, n)) return i; return -1; }
int vfs_write(const char *n, const char *b, uint32_t l)
{
    int i = fs_find(n); if (i < 0) { if (nfs >= 300) return -1; i = nfs++; strcpy(fsx[i].name, n); fsx[i].d = 0; }
    free(fsx[i].d); fsx[i].d = malloc(l + 1); memcpy(fsx[i].d, b, l); fsx[i].d[l] = 0; fsx[i].n = l; return (int)l;
}
int vfs_read(const char *n, char *b, uint32_t m) { int i = fs_find(n); if (i < 0) return -1; uint32_t l = fsx[i].n < m ? fsx[i].n : m; memcpy(b, fsx[i].d, l); return (int)l; }
int vfs_size(const char *n) { int i = fs_find(n); return i < 0 ? -1 : (int)fsx[i].n; }                  /* SOMA_283_MEMORIA_APP */
int vfs_delete(const char *n) { int i = fs_find(n); if (i < 0) return -1; free(fsx[i].d); fsx[i] = fsx[nfs - 1]; nfs--; return 0; }
char *vfs_read_alloc(const char *n, uint32_t *len) { int i = fs_find(n); if (i < 0) return 0; char *p = malloc(fsx[i].n + 1); memcpy(p, fsx[i].d, fsx[i].n); p[fsx[i].n] = 0; *len = fsx[i].n; return p; }

/* SOMA_357_CONTACTOS: memoria.c tambien mete las fichas de los contactos; aqui no hay libreta */
void contactos_contexto(const char *m, char *d, uint32_t max, uint32_t *pos) { (void)m; (void)d; (void)max; (void)pos; }
void hoja_contexto(const char *m, char *d, uint32_t max, uint32_t *pos) { (void)m; (void)d; (void)max; (void)pos; }   /* SOMA_360_HOJA_GRAFICOS */
/* SOMA_280_IMPORTAR: la agenda (agenda_core.c es logica pura y se enlaza de verdad; solo el reloj y el almacen son falsos) */
static struct ag_cal g_cal; static int g_cal_ini;
int64_t agenda_ahora(void) { return ag_min(2026, 11, 23, 17, 0); }
struct ag_cal *agenda_cal(void)
{
    if (!g_cal_ini) {
        g_cal_ini = 1; struct ag_ev *e = ag_nuevo(&g_cal);
        e->ini = ag_min(2026, 11, 24, 10, 0); e->dur = 60; e->activo = 1; e->cada = 1; e->antes = -1; strcpy(e->titulo, "Reunion con Ana");
    }
    return &g_cal;
}

/* servidor de embeddings falso: un "tema" por palabras clave */
static int g_srv_vivo = 1, g_n_emb;
static int tema_de(const char *t)
{
    if (strstr(t, "perro") || strstr(t, "Thor") || strstr(t, "mascota")) return 0;
    if (strstr(t, "trabaj") || strstr(t, "Iberdrola") || strstr(t, "empresa")) return 1;
    if (strstr(t, "cafe")) return 2;
    if (strstr(t, "Viaje") || strstr(t, "viaje")) return 3;
    if (strstr(t, "vive") || strstr(t, "Bilbao") || strstr(t, "Madrid")) return 4;
    return 5;
}
int http_post_json(const uint8_t *ip, uint16_t port, const char *path, const char *body, char *buf, uint32_t size, struct http_response *r, uint32_t to)
{
    (void)ip; (void)port; (void)path; (void)to;
    if (!g_srv_vivo) return -1;
    int t = tema_de(body); uint32_t h = 0; for (const char *c = body; *c; c++) h = h * 31u + (uint32_t)*c;
    uint32_t p = (uint32_t)snprintf(buf, size, "{\"data\":[{\"embedding\":[");
    for (int i = 0; i < 64; i++) p += (uint32_t)snprintf(buf + p, size - p, "%s%.5f", i ? "," : "", (i % 8 == t ? 1.0 : 0.0) + (((uint32_t)(i * 7) + h) & 15u) * 0.003);
    snprintf(buf + p, size - p, "]}]}");
    r->body = buf; r->body_len = (uint32_t)strlen(buf); r->status_code = 200; g_n_emb++;
    return 200;
}

/* SOMA_329_IA_LOCAL: sin motor de IA local en el anfitrion (la memoria usa el servidor falso) */
int ia_disponible(const char **why) { if (why) *why = "anfitrion"; return 0; }
int ia_modelo_ruta(const char *n, char *r, uint32_t cap) { (void)n; if (cap) r[0] = 0; return -1; }
int ia_instala(const char *n, char *m, uint32_t cap) { (void)n; if (cap) m[0] = 0; return -1; }
int ia_orden(int cmd, const char *modelo, const char *in, uint32_t in_len, char *out, uint32_t cap, uint32_t plazo_ms,
             const volatile int *cancel, char *err, uint32_t ecap) { (void)cmd; (void)modelo; (void)in; (void)in_len; (void)out; (void)cap; (void)plazo_ms; (void)cancel; if (ecap) err[0] = 0; return -1; }

/* SOMA_327_RED_FUERA: la memoria pide los embeddings por llm_http_local (netsvc en el sistema): aqui, al servidor falso */
int llm_http_local(const uint8_t ip[4], uint16_t port, const char *path, const char *body, char *resp_buf, uint32_t cap,
                   struct http_response *r, uint32_t ms, const volatile int *cancel)
{
    (void)cancel;
    return http_post_json(ip, port, path, body, resp_buf, cap, r, ms);
}

/* LLM falso: contesta segun el tipo de prompt */
static int g_llm_vivo = 1, g_n_llm;
static char g_lin[40][200];
static void ids_lineas(const char *prompt, uint32_t *ids, const char **txt, int *n)
{
    *n = 0;
    for (const char *p = prompt; (p = strstr(p, "\n- [")) != 0; p += 3) {
        ids[*n] = (uint32_t)atoi(p + 4); const char *c = strchr(p + 4, ']'); c = c ? c + 2 : "";
        size_t l = 0; while (c[l] && c[l] != '\n' && l < 199) l++;
        memcpy(g_lin[*n], c, l); g_lin[*n][l] = 0; txt[*n] = g_lin[*n]; (*n)++; if (*n >= 40) break;
    }
}
int llm_chat_ex(const char *sys, const char *prompt, const struct llm_img *img, int nimg, int maxt, char *reply, uint32_t cap, uint32_t to)
{
    (void)sys; (void)img; (void)nimg; (void)maxt; (void)to;
    if (!g_llm_vivo) return 0;
    g_n_llm++;
    char o[3000]; o[0] = 0;
    uint32_t ids[40]; const char *tx[40]; int n;
    if (strstr(prompt, "Extrae TODOS los hechos")) {
        const char *a = strstr(prompt, "<<<"); const char *b = strstr(prompt, ">>>");
        char t[600]; size_t l = (a && b && b > a) ? (size_t)(b - a - 3) : 0; if (l > 599) l = 599; memcpy(t, a + 3, l); t[l] = 0;
        const char *f = 0; int imp = 6;
        if (strstr(t, "Thor") && strstr(t, "se llama")) { f = "El usuario tiene un perro llamado Thor"; imp = 7; }
        else if (strstr(t, "Thor")) { f = "El usuario tiene un perro que se llama Thor"; imp = 5; }
        else if (strstr(t, "Bilbao")) f = "El usuario vive en Bilbao";
        else if (strstr(t, "Madrid")) f = "El usuario vive en Madrid";
        else if (strstr(t, "cafe")) { f = "El usuario tomo un cafe"; imp = 1; }
        else if (strstr(t, "Iberdrola")) f = "El usuario trabaja en Iberdrola";
        else if (strstr(t, "Viaje")) { f = t; imp = 5; }
        if (f) snprintf(o, sizeof o, "{\"hechos\":[{\"texto\":\"%s\",\"importancia\":%d,\"tipo\":\"dato\"}]}", f, imp); else strcpy(o, "{\"hechos\":[]}");
    } else if (strstr(prompt, "\"relation\"")) {
        strcpy(o, strstr(prompt, "vive en") ? "{\"relation\":\"supersedes\",\"confidence\":0.95}" : "{\"relation\":\"unrelated\",\"confidence\":0.9}");
    } else if (strstr(prompt, "Clasifica cada recuerdo")) {
        ids_lineas(strstr(prompt, "Recuerdos:"), ids, tx, &n);
        uint32_t p = (uint32_t)snprintf(o, sizeof o, "{\"asignaciones\":[");
        for (int i = 0; i < n; i++) {
            const char *r = strstr(tx[i], "perro") ? "Personal/Mascotas" : strstr(tx[i], "Viaje") ? "Viajes" : strstr(tx[i], "vive") ? "Personal/Vivienda" : strstr(tx[i], "trabaja") ? "Trabajo" : strstr(tx[i], "cafe") ? "Personal/Habitos" : "Otros temas";
            p += (uint32_t)snprintf(o + p, sizeof o - p, "%s{\"id\":%u,\"ruta\":\"%s\"}", i ? "," : "", ids[i], r);
        }
        snprintf(o + p, sizeof o - p, "]}");
    } else if (strstr(prompt, "AMBITO de este nodo")) {
        strcpy(o, "{\"description\": \"Descripcion de prueba del nodo\"}");
    } else if (strstr(prompt, "Compara estos dos recuerdos")) {
        strcpy(o, strstr(prompt, "Thor") ? "{\"redundant\": true, \"merged\": \"El usuario tiene un perro llamado Thor\", \"importance\": 6}" : "{\"redundant\": false}");
    } else if (strstr(prompt, "merece ser preservado")) {
        strcpy(o, "{\"preserve\": true, \"importance\": 7}");
    } else if (strstr(prompt, "Dividelos en 2 subgrupos")) {
        ids_lineas(strstr(prompt, "coherentes:"), ids, tx, &n);
        char a[200] = "", b[200] = "";
        for (int i = 0; i < n; i++) { char *d = i < n / 2 ? a : b; size_t k = strlen(d); snprintf(d + k, 200 - k, "%s%u", k ? "," : "", ids[i]); }
        snprintf(o, sizeof o, "{\"grupo_A\": {\"nombre\": \"Europa\", \"ids\": [%s]}, \"grupo_B\": {\"nombre\": \"Asia\", \"ids\": [%s]}}", a, b);
    } else if (strstr(prompt, "Pertenecen estos dos subtemas")) {
        strcpy(o, "{\"mergeable\": false}");
    } else strcpy(o, "{}");
    if (strlen(o) + 1 > cap) return 0;
    strcpy(reply, o);
    return 1;
}

static void ingiere(const char *texto)
{
    memoria_registra(texto, "De acuerdo.");
    while (g_qn) { struct qitem q = g_q[0]; for (uint32_t i = 1; i < g_qn; i++) g_q[i - 1] = g_q[i]; g_qn--; extrae(&q); }
}
static void paso(uint32_t n) { char r[150]; for (uint32_t i = 0; i < n; i++) sueno_paso(r, sizeof r); }
static const struct mc_item *busca_celda(const char *sub)
{
    for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && strstr(g_m.it[i].texto, sub)) return &g_m.it[i];
    return 0;
}
static char out[8192];
static const char *cmd(const char *c) { out[0] = 0; memoria_cmd(c, out, sizeof out); return out; }

int main(int argc, char **argv)
{
    g_c.srv = 0;
    cmd("estado");                                           /* inicia: carga (disco vacio), config por defecto */
    CK(g_listo && g_c.activa && g_c.sueno && g_c.sueno_s == 90);
    CK(g_c.srv && g_c.ip[0] == 192 && g_c.port == 8089);     /* la IP del LLM local con el puerto de embeddings */
    CKE(g_m.n, 0);

    /* 1. ingesta: hechos, sustitucion temporal, duplicados */
    ingiere("Me llamo Marcos y vivo en Madrid");
    ingiere("Ahora vivo en Bilbao");
    ingiere("Mi perro se llama Thor");
    ingiere("Mi perro Thor es muy travieso");
    ingiere("Me tome un cafe");
    ingiere("Trabajo en Iberdrola");
    uint32_t c, e, a, s, v; mc_stats(&g_m, &c, &e, &a, &s, &v);
    CKE(e, 12); CKE(s, 1); CK(c >= 5);                       /* 6 mensajes y 6 respuestas; Madrid sustituido por Bilbao */
    CK(busca_celda("Bilbao")->estado == MC_ACTIVO && busca_celda("Madrid")->estado == MC_SUSTITUIDO);
    { uint32_t sv = 0; for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && !g_m.it[i].vec) sv++; CKE(sv, 0); }        /* las celdas llevan vector del servidor; los mensajes se embeben despues */
    CK(v > 0);
    for (int i = 0; i < 14; i++) { char t[80]; snprintf(t, sizeof t, "Viaje numero %d a un sitio muy lejano", i); ingiere(t); }
    mc_stats(&g_m, &c, &e, &a, &s, &v);
    uint32_t celdas0 = c;
    CK(celdas0 >= 18);

    /* 2. un ciclo de sueno completo (9 pasos) */
    CKE(g_m.tick, 0); CKE(g_m.pasos, 0);
    paso(1); CK(strstr(g_informe[0], "[repair]"));
    paso(1); CK(strstr(g_informe[1], "[embed]"));
    paso(1); CK(strstr(g_informe[2], "[decay]") && strstr(g_informe[2], "ninguna que olvidar"));
    paso(1); CK(strstr(g_informe[3], "[ordena]") && strstr(g_informe[3], "colocadas"));
    paso(5);
    CKE(g_m.pasos, 9); CKE(g_m.tick, 9);
    mc_recuenta(&g_m);
    CK(mc_nodo_ruta(&g_m, "Personal/Mascotas", 0) != 0 || mc_nodo_ruta(&g_m, "Personal/Vivienda", 0) != 0);   /* ya hay arbol tras la primera vuelta */
    /* ordena toma 8 celdas por vuelta (9 pasos): hacen falta tres vueltas mas para ordenar las 20 y pico */
    paso(9 * 3);
    uint32_t nm = mc_nodo_ruta(&g_m, "Personal/Mascotas", 0); CK(nm);
    CK(mc_nodo_ruta(&g_m, "Trabajo", 0) && mc_nodo_ruta(&g_m, "Viajes", 0) && mc_nodo_ruta(&g_m, "Personal/Vivienda", 0));
    uint32_t sin = 0; for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && g_m.it[i].estado == MC_ACTIVO && !g_m.it[i].nodo) sin++;
    CKE(sin, 0);
    mc_recuenta(&g_m);
    /* Viajes tenia 14 celdas (> 12): el paso split lo partio en Europa/Asia */
    CK(mc_nodo_ruta(&g_m, "Viajes/Europa", 0) && mc_nodo_ruta(&g_m, "Viajes/Asia", 0));
    CK(g_m.nodo[mc_nodo_ruta(&g_m, "Viajes", 0)].cuenta <= MC_MAX_HIJOS);
    CK(g_m.nodo[nm].desc[0] != 0 && !g_m.nodo[nm].sucio);    /* describe puso la descripcion */
    paso(9 * 4); CK(mc_nodo_sucio(&g_m) == 0);          /* describe hace 2 nodos por vuelta: con unas vueltas mas, todos al dia */
    CK(g_n_llm > 20);
    /* dedupe: Thor esta dos veces ("tiene un perro llamado Thor" y "que se llama Thor"): una acaba sustituida por la otra */
    int thor_act = 0, thor_sus = 0; for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && strstr(g_m.it[i].texto, "Thor")) { if (g_m.it[i].estado == MC_ACTIVO) thor_act++; if (g_m.it[i].estado == MC_SUSTITUIDO) thor_sus++; }
    CKE(thor_act, 1); CKE(thor_sus, 1); CK(g_n_fus >= 1);

    /* 3. el olvido: el cafe (importancia 1) se archiva con el tiempo; lo importante sigue */
    const struct mc_item *cafe = busca_celda("cafe"), *bilbao = busca_celda("Bilbao");
    CKE(cafe->imp, 1);
    paso(30);
    CKE(cafe->estado, MC_ARCHIVADO); CKE(cafe->t_hasta, 0);          /* importancia 1: a los ~14 ticks ya estaba archivado */
    CKE(bilbao->estado, MC_ACTIVO);
    CK(g_n_olv >= 1);

    /* 4. restauracion: una consulta sobre el cafe lo trae de vuelta */
    uint32_t tick_antes = g_m.tick;
    char dst[4096]; uint32_t pos = 0;
    CK(memoria_contexto("cuentame lo del cafe", dst, sizeof dst, &pos));
    CKE(cafe->estado, MC_ACTIVO); CK(cafe->imp >= 6); CKE(cafe->tick, tick_antes); CK(g_n_rest >= 1);
    CK(strstr(dst, "tomo un cafe"));
    /* lo olvidado a mano NO se restaura */
    const struct mc_item *vive = busca_celda("Iberdrola");
    char cm[40]; snprintf(cm, sizeof cm, "olvidar %u", vive->id); cmd(cm); CKE(vive->estado, MC_ARCHIVADO); CK(vive->t_hasta != 0);
    pos = 0; memoria_contexto("donde trabaja el usuario en Iberdrola", dst, sizeof dst, &pos);
    CKE(vive->estado, MC_ARCHIVADO);

    /* 5. persistencia: guardar, "reiniciar" y cargar */
    memoria_guarda_todo();
    CK(fs_find("/memoria/m000.bin") >= 0 && fs_find("/memoria/arbol.bin") >= 0);
    uint32_t n_ant = g_m.n, tick_ant = g_m.tick, pasos_ant = g_m.pasos, nodos_ant = mc_recuenta(&g_m);
    uint32_t id_cafe = cafe->id, nodo_cafe = cafe->nodo;
    char arbol_ant[4096]; strcpy(arbol_ant, cmd("arbol"));
    mc_destroy(&g_m); g_listo = 0; g_n_rest = 0; g_n_olv = 0;
    cmd("estado");
    CKE(g_m.n, n_ant); CKE(g_m.tick, tick_ant); CKE(g_m.pasos, pasos_ant); CKE(mc_recuenta(&g_m), nodos_ant);
    CKE(g_m.it[id_cafe - 1].nodo, nodo_cafe); CKE(g_m.it[id_cafe - 1].estado, MC_ACTIVO);
    CK(!strcmp(arbol_ant, cmd("arbol")));
    {   /* un arbol.bin danado: no se usa, las celdas vuelven a "sin ordenar" y el ciclo lo rehace */
        int i = fs_find("/memoria/arbol.bin"); fsx[i].d[40] ^= 0x55;
        mc_destroy(&g_m); g_listo = 0; cmd("estado");
        CKE(g_m.n, n_ant); CKE(mc_recuenta(&g_m), 0);
        uint32_t sn = 0; for (uint32_t k = 0; k < g_m.n; k++) if (g_m.it[k].kind == MC_CELDA && g_m.it[k].estado == MC_ACTIVO && !g_m.it[k].nodo) sn++;
        CK(sn > 10);
        paso(9 * 6); CK(mc_nodo_ruta(&g_m, "Personal/Mascotas", 0) != 0);
    }
    /* 6. el servidor de embeddings caido y el LLM caido: no pasa nada grave */
    g_srv_vivo = 0; g_llm_vivo = 0; g_emb_cae_hasta = 0;
    uint32_t antes = g_m.n;
    memoria_registra("Tengo un hermano que se llama Pablo", "Vale");
    CKE(g_m.n, antes + 2); CKE(g_qn, 1);
    extrae(&g_q[0]); g_qn = 0; CK(g_n_fallo >= 1);           /* el LLM no contesta: se anota y se sigue */
    char r1[150]; paso(9); sueno_paso(r1, sizeof r1);
    CK(g_m.pasos >= 10);
    pos = 0; memoria_contexto("como se llama mi hermano", dst, sizeof dst, &pos);           /* sin servidor: busqueda por palabras */
    CK(strstr(dst, "Pablo"));
    g_srv_vivo = 1; g_llm_vivo = 1; g_emb_cae_hasta = 0;
    uint32_t sin_vec = 0; for (uint32_t i = 0; i < g_m.n; i++) if (!g_m.it[i].vec) sin_vec++;
    CK(sin_vec >= 2);
    for (int k = 0; k < 4; k++) embebe_pendientes();
    sin_vec = 0; for (uint32_t i = 0; i < g_m.n; i++) if (!g_m.it[i].vec) sin_vec++;
    CKE(sin_vec, 0);

    /* 7. las ordenes */
    CK(strstr(cmd("estado"), "arbol:") && strstr(out, "sueno: activo") && strstr(out, "restauradas"));
    CK(strstr(cmd("arbol"), "Personal") && strstr(out, "(") && strstr(out, "Sin ordenar todavia"));
    CK(strstr(cmd("sueno 3"), "paso ") && strstr(out, "["));
    CK(strstr(cmd("config sueno_s 5"), "Guardado") && g_c.sueno_s == 10); CK(strstr(cmd("config"), "sueno_s=10"));
    cmd("config sueno 0"); CKE(g_c.sueno, 0);
    snprintf(cm, sizeof cm, "ver %u", busca_celda("Bilbao")->id); CK(strstr(cmd(cm), "tema: Personal/Vivienda"));
    CK(strstr(cmd("buscar perro"), "Thor"));
    CK(strstr(cmd("recordar Mi color favorito es el verde"), "Recordado"));
    CK(strstr(cmd("ayuda"), "arbol") && strstr(out, "sueno"));
    CK(strstr(cmd("servidor"), "LISTO"));

    {   /* sin arbol.bin (memoria del parche 277): el reloj arranca en lo ultimo usado */
        fsx[fs_find("/memoria/arbol.bin")].name[0] = 'X';
        mc_destroy(&g_m); g_listo = 0; cmd("estado");
        uint32_t mx = 0; for (uint32_t k = 0; k < g_m.n; k++) if (g_m.it[k].tick > mx) mx = g_m.it[k].tick;
        CKE(g_m.tick, mx);
    }

    /* 8. SOMA_280_IMPORTAR: `memoria importar RUTA.smem` (el cartucho de memorIA) y la agenda en el contexto */
    const char *fx = argc > 1 ? argv[1] : 0;
    if (fx) {
        char ru[700];
        for (int pass = 0; pass < 3; pass++) {
            mc_destroy(&g_m); g_listo = 0; for (int i = 0; i < nfs; i++) { free(fsx[i].d); fsx[i].d = 0; } nfs = 0;       /* disco y memoria en blanco */
            cmd("estado"); g_srv_vivo = 1;
            CK(strstr(cmd("importar /usuario/no_existe.smem"), "No encuentro"));
            vfs_write("/usuario/basura.smem", "esto no es un cartucho\n", 23);
            CK(strstr(cmd("importar /usuario/basura.smem"), "no es un archivo .smem"));
            CKE(g_m.n, 0);
            cmd("recordar EL usuario es alergico a la PENICILINA");                                                       /* ya estaba: no se repite */
            uint32_t previos = g_m.n;
            for (int k = 1; k <= 9; k++) {
                snprintf(ru, sizeof ru, "%s/salida/memoria-%03d.smem", fx, k); FILE *f = fopen(ru, "rb"); if (!f) break;
                fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); char *bb = malloc((size_t)l + 1); if (fread(bb, 1, (size_t)l, f) != (size_t)l) l = 0; fclose(f);
                snprintf(ru, sizeof ru, "/usuario/memoria-%03d.smem", k); vfs_write(ru, bb, (uint32_t)l); free(bb);
            }
            if (pass == 1) g_dim_servidor = 64;              /* el servidor da vectores de 64: los de 1024 del archivo no valen y se recalculan */
            if (pass == 0) g_dim_servidor = 0;               /* sin probar: se fia de la cabecera */
            if (pass == 2) { strcpy(g_c.modelo, "nomic-embed"); g_dim_servidor = 0; }       /* otro modelo: no mezcla espacios vectoriales */
            const char *r1 = cmd("importar /usuario/memoria-001.smem");
            CK(strstr(r1, "Importado de /usuario/memoria-001.smem"));
            if (pass == 0) CK(strstr(r1, "los del archivo") && !strstr(r1, "sin vector valido"));
            if (pass == 1) CK(strstr(r1, "sin vector valido"));
            if (pass == 2) CK(strstr(r1, "omitidos: el archivo es de bge-m3 y SOMA usa nomic-embed"));
            CK(strstr(r1, "ya estaban"));
            CK(strstr(r1, "nodo(s)") && strstr(r1, "Guardado."));
            for (int k = 2; k <= 9; k++) { snprintf(ru, sizeof ru, "importar /usuario/memoria-%03d.smem", k); if (fs_find(ru + 9) < 0) break; cmd(ru); }
            uint32_t ci, ei, ai, si, vi; mc_stats(&g_m, &ci, &ei, &ai, &si, &vi);
            CKE(ei, 6); CKE(si, 1); CKE(ai, 1); CKE(ci, 34);                       /* 35 activas en el archivo; c1 y c9 repiten la que ya tenia SOMA: 33 nuevas + 1 = 34 */
            CKE(g_m.n, previos + 33 + 2 + 6);
            CK(strstr(cmd("arbol"), "Salud") && strstr(out, "Alergias conocidas") && strstr(out, "Mascotas"));
            CK(strstr(cmd("buscar alergico penicilina"), "PENICILINA"));
            CK(strstr(cmd("estado"), "arbol: 7 nodo(s)") || strstr(out, "nodo(s)"));
            uint32_t sinv = 0; for (uint32_t i = 0; i < g_m.n; i++) if (!g_m.it[i].vec) sinv++;
            if (pass == 0) CK(sinv <= g_m.n - 10);                              /* las celdas del archivo con vector lo conservan */
            else CK(sinv >= g_m.n - 1 || pass == 1);
            /* reiniciar: todo sigue ahi */
            uint32_t n0 = g_m.n; mc_destroy(&g_m); g_listo = 0; cmd("estado");
            CKE(g_m.n, n0);
            CK(g_m.it[busca_celda("vivia en Madrid")->id - 1].sust_por == busca_celda("Bilbao")->id);
            if (pass == 0) {                                                    /* la agenda entra en el contexto cuando se habla de planes */
                char dst[4096]; uint32_t pos = 0; dst[0] = 0;
                CK(memoria_contexto("que tengo en la agenda manana?", dst, sizeof dst, &pos));
                CK(strstr(dst, "[AGENDA de SOMA") && strstr(dst, "Reunion con Ana"));
                pos = 0; dst[0] = 0; memoria_contexto("donde vive el usuario", dst, sizeof dst, &pos);
                CK(strstr(dst, "[MEMORIA") && !strstr(dst, "[AGENDA"));       /* una pregunta que no trata de planes no la lleva */
                CK(pide_agenda("Que tengo hoy") && pide_agenda("mis REUNIONES de la semana") && !pide_agenda("cual es la capital de Francia") && !pide_agenda("hoyuelo"));
            }
        }
        CK(strstr(cmd("importar"), "Importados"));                              /* sin ruta: el arbol antiguo /mem, como antes */
    }

    /* 9. SOMA_283_MEMORIA_APP: cartuchos, borrar de verdad y lo que usa la aplicacion de Memoria */
    {
        static struct mem_pet pt; static struct mem_cart cs[8]; static struct mem_vista vv; char rt[200];
        #define PIDE(OP, A, B) do { memset(&pt, 0, sizeof pt); pt.op = (OP); pt.a = (A); pt.b = (B); pt.gen = memoria_gui_gen(); } while (0)
        mc_destroy(&g_m); g_listo = 0; for (int i = 0; i < nfs; i++) { free(fsx[i].d); fsx[i].d = 0; } nfs = 0;
        for (int i = 0; i < NCART; i++) { if (g_mont[i]) { mc_destroy(g_mont[i]); free(g_mont[i]); g_mont[i] = 0; } }
        g_prim = 0; strcpy(g_pref, "/memoria/"); g_c.sueno = 1; g_c.srv = 1; g_srv_vivo = 1; g_llm_vivo = 1; g_emb_cae_hasta = 0; strcpy(g_c.modelo, "bge-m3");
        cmd("estado");
        CKE(memoria_gui_cartuchos(cs, 8), 1); CK(!strcmp(cs[0].id, "principal") && cs[0].primario && !cs[0].montado);
        cmd("recordar El usuario trabaja en Iberdrola"); cmd("recordar El usuario tiene un perro llamado Thor"); cmd("recordar Viaje a Japon en abril");
        cmd("recordar El usuario vive en Madrid");
        uint32_t idt = busca_celda("Iberdrola")->id, idp = busca_celda("Thor")->id, idv = busca_celda("Japon")->id, idm = busca_celda("Madrid")->id;
        struct mc *gm = memoria_gui_entra(); CK(gm == &g_m);
        uint32_t ntr = mc_nodo_ruta(gm, "Trabajo/Empresa", 1); mc_asigna(gm, idt, ntr); gm->nodo[ntr].fijo = 1;
        uint32_t nmas = mc_nodo_ruta(gm, "Personal/Mascotas", 1); mc_asigna(gm, idp, nmas);
        uint32_t idb = mc_add(gm, MC_CELDA, 0, "El usuario vive en Bilbao", "x", 7, memoria_gui_ahora(), gm->tick, 0, 0);
        mc_set_estado(gm, idm, MC_SUSTITUIDO, memoria_gui_ahora(), idb);
        uint32_t v0 = memoria_version(); memoria_gui_sale(1); CK(memoria_version() != v0 && g_guardar);
        memoria_vista(&vv); CKE(vv.celdas, 4); CKE(vv.sustituidas, 1); CKE(vv.nodos, 4); CK(!strcmp(vv.cartucho, "Principal")); CKE(vv.nmont, 0);
        CK(vv.tarea >= 0 && vv.tarea < 9);

        /* crear, copiar, conectar */
        PIDE(MEMP_C_NUEVO, 0, 0); strcpy(pt.s1, "Trabajo y oficina"); strcpy(pt.s2, "Lo del trabajo"); ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "Cartucho creado: Trabajo y oficina (trabajo_y_oficin)"));
        int kt = cart_busca("trabajo_y_oficin"); CK(kt > 0);
        CK(fs_find("/memoria/cartuchos.txt") >= 0);
        PIDE(MEMP_C_NUEVO, 0, 0); strcpy(pt.s1, "Trabajo y oficina"); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "(trabajo_y_oficin02)"));
        int kt2 = cart_busca("trabajo_y_oficin02"); CK(kt2 > 0 && kt2 != kt);
        PIDE(MEMP_COPIA, (uint32_t)kt, ntr); ops_ejecuta(&pt);                           /* copiar una rama */
        CK(strstr(g_ops_msg, "Copiado a Trabajo y oficina: 1 nuevo(s)"));
        CK(fs_find("/memoria/k_trabajo_y_oficin/m000.bin") >= 0 && fs_find("/memoria/k_trabajo_y_oficin/arbol.bin") >= 0);
        PIDE(MEMP_COPIA, (uint32_t)kt, 0); pt.nids = 2; pt.ids[0] = idt; pt.ids[1] = idp; ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "1 nuevo(s), 1 ya estaban"));
        PIDE(MEMP_COPIA, (uint32_t)g_prim, 0); pt.nids = 1; pt.ids[0] = idt; ops_ejecuta(&pt); CK(strstr(g_ops_msg, "distinto del primario"));
        CKE(g_m.n, 5 + 0); CK(busca_celda("Iberdrola") && busca_celda("Thor"));          /* copiar no quita nada */
        CK(strstr(cmd("cartucho conectar trabajo_y_oficin"), "Conectado (solo lectura)"));
        CK(g_mont[kt] && g_mont[kt]->n == 2);
        { char rr[200]; uint32_t q = 0; const struct mc_item *x = 0; for (uint32_t i = 0; i < g_mont[kt]->n; i++) if (strstr(g_mont[kt]->it[i].texto, "Iberdrola")) x = &g_mont[kt]->it[i];
          CK(x && x->nodo); if (x) { mc_nodo_ruta_texto(g_mont[kt], x->nodo, rr, sizeof rr); CK(!strcmp(rr, "Trabajo/Empresa")); } (void)q; }
        { char dst[8192]; uint32_t pos = 0; dst[0] = 0; memoria_contexto("donde trabaja el usuario", dst, sizeof dst, &pos);
          CK(strstr(dst, "[Del cartucho de memoria \"Trabajo y oficina\" (conectado, solo lectura)]")); }
        CK(strstr(cmd("buscar perro"), "[Trabajo y oficina]"));
        memoria_vista(&vv); CKE(vv.nmont, 1);
        CK(strstr(cmd("cartucho lista"), "* Principal (principal) PRIMARIO") && strstr(out, "+ Trabajo y oficina"));
        CK(strstr(cmd("estado"), "cartucho primario: Principal (+1 conectado/s)"));

        /* mover: se va del primario y llega al conectado */
        PIDE(MEMP_MUEVE, (uint32_t)kt, 0); pt.nids = 1; pt.ids[0] = idv; ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "Movido a Trabajo y oficina: 1 nuevo(s); 1 quitado(s) del primario"));
        CK(!busca_celda("Japon")); CKE(g_mont[kt]->n, 3);
        /* una peticion con ids de antes de un borrado se rechaza */
        PIDE(MEMP_BORRA, 0, 0); pt.nids = 1; pt.ids[0] = 1; pt.gen = memoria_gui_gen() - 1; ops_ejecuta(&pt); CK(strstr(g_ops_msg, "cambio mientras tanto"));

        /* borrar de verdad: la que sustituia; la antigua vuelve a valer */
        uint32_t n0 = g_m.n; idb = busca_celda("Bilbao")->id;
        PIDE(MEMP_BORRA, 0, 0); pt.nids = 1; pt.ids[0] = idb; ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "Borrado(s) para siempre: 1")); CKE(g_m.n, n0 - 1); CK(!busca_celda("Bilbao"));
        CKE(busca_celda("Madrid")->estado, MC_ACTIVO);
        for (uint32_t i = 0; i < g_m.n; i++) CKE(g_m.it[i].id, i + 1);
        { uint32_t id = busca_celda("Madrid")->id; char c2[40]; snprintf(c2, sizeof c2, "borrar %u", id); CK(strstr(cmd(c2), "Borrado(s) para siempre: 1")); CK(!busca_celda("Madrid")); }
        CK(strstr(cmd("borrar 999"), "no existe"));
        /* borrar una rama con sus hechos, y una rama sola (lo hace la ventana, sin peticion) */
        cmd("recordar Su jefe se llama Pedro"); gm = memoria_gui_entra(); mc_asigna(gm, busca_celda("Pedro")->id, ntr); memoria_gui_sale(1);
        PIDE(MEMP_BORRA_RAMA, ntr, 0); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "Borrado(s) para siempre: 2")); CK(!busca_celda("Pedro") && !busca_celda("Iberdrola"));
        CK(!g_m.nodo[ntr].usado);
        /* muchos elementos: compactar varios fragmentos y que se borren los archivos sobrantes */
        gm = memoria_gui_entra(); for (int i = 0; i < 1100; i++) { char t[60]; snprintf(t, sizeof t, "Relleno numero %d", i); mc_add(gm, MC_EPISODIO, 0, t, "", 5, 1, 1, 0, 0); } memoria_gui_sale(1);
        memoria_guarda_todo(); CK(fs_find("/memoria/m001.bin") >= 0);
        { PIDE(MEMP_BORRA, 0, 0); uint32_t k = 0; for (uint32_t i = 0; i < g_m.n && k < 64; i++) if (strstr(g_m.it[i].texto, "Relleno")) pt.ids[k++] = g_m.it[i].id; pt.nids = k; ops_ejecuta(&pt); }
        CK(strstr(g_ops_msg, "Borrado(s) para siempre: 64")); CKE(g_m.n, 1 + 1100 - 64 + 0 * 0 + (uint32_t)(busca_celda("Thor") ? 0 : 0));
        gm = memoria_gui_entra(); { uint8_t *z = (uint8_t *)calloc(1, gm->n + 1); for (uint32_t i = 0; i < gm->n; i++) if (strstr(gm->it[i].texto, "Relleno")) z[i] = 1; memoria_gui_sale(0); borra_marcados(z, rt, sizeof rt); free(z); }
        CK(fs_find("/memoria/m001.bin") < 0 && fs_find("/memoria/m000.bin") >= 0);             /* el fragmento sobrante se borra del disco */

        /* primario: cambiar al de trabajo; lo nuevo se aprende alli; el principal queda en disco */
        uint32_t nprin = g_m.n;
        PIDE(MEMP_C_PRIM, (uint32_t)kt, 0); ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "Primario: Trabajo y oficina")); CKE(g_prim, kt); CK(!strcmp(g_pref, "/memoria/k_trabajo_y_oficin/"));
        CK(!g_mont[kt]); CKE(g_m.n, 3); CK(busca_celda("Japon"));
        cmd("recordar Usa Python en el trabajo"); memoria_guarda_todo();
        CK(fs_find("/memoria/k_trabajo_y_oficin/m000.bin") >= 0);
        mc_destroy(&g_m); g_listo = 0; cmd("estado");                                       /* reiniciar: sigue siendo el primario */
        CKE(g_prim, kt); CK(busca_celda("Python") && busca_celda("Japon"));
        CK(strstr(cmd("cartucho usar principal"), "Primario: Principal")); CKE(g_m.n, nprin); CK(busca_celda("Thor") && !busca_celda("Python"));
        CK(strstr(cmd("cartucho usar principal"), "Ya es el primario"));

        /* exportar, importar y clonar */
        PIDE(MEMP_C_EXPORTA, (uint32_t)kt, 0); strcpy(pt.s1, "/usuario/trabajo.smem"); ops_ejecuta(&pt);
        CK(strstr(g_ops_msg, "Exportado Trabajo y oficina a /usuario/trabajo.smem: ") && strstr(g_ops_msg, "(sin vectores)"));
        { int f = fs_find("/usuario/trabajo.smem"); CK(f >= 0 && !strncmp(fsx[f].d, "#SOMAMEM 1 modelo=ninguno", 25) && strstr(fsx[f].d, "Python") && strstr(fsx[f].d, "\nN\tTrabajo/Empresa\t")); }
        PIDE(MEMP_C_EXPORTA, (uint32_t)g_prim, 1); strcpy(pt.s1, "/usuario/prin.smem"); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "(con vectores)"));
        { int f = fs_find("/usuario/prin.smem"); CK(f >= 0 && strstr(fsx[f].d, "modelo=bge-m3 dim=64") && strstr(fsx[f].d, "\nN\tPersonal/Mascotas\t")); }
        PIDE(MEMP_C_CLONA, (uint32_t)g_prim, 0); strcpy(pt.s1, "Copia principal"); ops_ejecuta(&pt);
        int kc = cart_busca("copia_principal"); CK(kc > 0 && strstr(g_ops_msg, "Clonado en Copia principal"));
        CK(memoria_gui_cartuchos(cs, 8) == 4);
        { int ok = 0; for (int i = 0; i < 4; i++) if (cs[i].idx == kc && cs[i].celdas == 1) ok = 1; CK(ok); }       /* Thor */
        PIDE(MEMP_C_VACIA, (uint32_t)kc, 0); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "Vaciado")); CK(fs_find("/memoria/k_copia_principal/m000.bin") < 0);
        PIDE(MEMP_C_BORRA, (uint32_t)kc, 0); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "Cartucho borrado: Copia principal")); CKE(cart_busca("copia_principal"), -1);
        PIDE(MEMP_C_BORRA, (uint32_t)g_prim, 0); ops_ejecuta(&pt); CK(strstr(g_ops_msg, "No se puede borrar el primario"));
        CK(strstr(cmd("cartucho usar trabajo_y_oficin"), "Primario")); CK(strstr(cmd("cartucho borrar principal"), "no se borra")); cmd("cartucho usar principal");
        /* conectado se recuerda al reiniciar; tope de conectados */
        cmd("cartucho conectar trabajo_y_oficin"); mc_destroy(&g_m); g_listo = 0;
        for (int i = 0; i < NCART; i++) if (g_mont[i]) { mc_destroy(g_mont[i]); free(g_mont[i]); g_mont[i] = 0; }
        cmd("estado"); CK(g_mont[kt] != 0);
        cmd("cartucho nuevo A1"); cmd("cartucho nuevo A2"); cmd("cartucho nuevo A3");
        cmd("cartucho conectar a1"); cmd("cartucho conectar a2"); CK(strstr(cmd("cartucho conectar a3"), "Ya hay 3"));
        CK(strstr(cmd("cartucho nuevo A4"), "creado")); CK(strstr(cmd("cartucho nuevo A5"), "creado")); CK(strstr(cmd("cartucho nuevo A6"), "No caben"));
        PIDE(MEMP_C_VACIA, (uint32_t)g_prim, 0); ops_ejecuta(&pt); CKE(g_m.n, 0); CK(fs_find("/memoria/m000.bin") < 0);

        /* buscar desde la ventana (primario + conectados) */
        cmd("recordar El usuario tiene un perro llamado Thor");
        PIDE(MEMP_BUSCAR, 0, 0); strcpy(pt.s1, "perro"); ops_ejecuta(&pt);
        { static struct mem_res rs[24]; uint32_t rv; int n = memoria_gui_resultados(rs, 24, &rv); CK(n >= 1 && rs[0].primario && strstr(rs[0].texto, "Thor") && strstr(g_ops_msg, "por significado")); }
        /* ajustes desde la ventana y peticiones ocupadas */
        memoria_gui_config("sueno_s", 120, 0); CKE(g_c.sueno_s, 120); memoria_gui_config("emb_modelo", 0, "bge-m3");
        g_ops_ocupado = 0; g_pet_hay = 0; PIDE(MEMP_GUARDAR, 0, 0); CKE(memoria_gui_pide(&pt), 1); CKE(memoria_gui_pide(&pt), 0);
        g_ops_ocupado = 0; g_pet_hay = 0;
        PIDE(MEMP_SUENO, 2, 0); uint32_t ps = g_m.pasos; ops_ejecuta(&pt); CKE(g_m.pasos, ps + 2);
        CK(strstr(cmd("ayuda"), "cartucho") && strstr(out, "memoria borrar ID"));
    }


    printf("test_memoria_glue_279: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
