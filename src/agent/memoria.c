/* SOMA_277_MEMORIA: pegamento de la memoria de SOMA (ver memoria.h). */
#include <stdint.h>
#include "memoria.h"
#include "memoria_core.h"
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "mutex.h"
#include "thread.h"
#include "net.h"
#include "http.h"
#include "llm.h"
#include "llm_proto.h"
#include "idt.h"
#include "rtc.h"
#include "agenda.h"                /* SOMA_280_IMPORTAR: la agenda entra en el contexto */
#if defined(__x86_64__)
#include "ia_sys.h"                /* SOMA_329_IA_LOCAL: embeddings con el motor local (ring 3) */
#include "ia_abi.h"
#endif

#define CONF "/etc/memoria.conf"
#define QN 6                                   /* mensajes esperando la extraccion de hechos */

struct conf {
    int activa;                                /* 0 = la memoria no se usa ni se guarda */
    int sueno, sueno_s;                        /* SOMA_279_SUENO: ciclo de sueno (1 paso cada sueno_s segundos con todo en calma) */
    int ingesta;                               /* extraer hechos de lo que dice el usuario */
    int n_hechos, n_epis;                      /* cuantos recuerdos se inyectan en el prompt */
    uint8_t ip[4]; uint16_t port; int srv;     /* servidor de embeddings (srv = 0: sin servidor, solo busqueda lexical) */
    char url[80], modelo[40];
    int motor;                                 /* SOMA_329_IA_LOCAL: 0 auto (local si hay modelo en /modelos), 1 local, 2 servidor */
    int local;                                 /* se usa el motor local (calculado por conf_srv) */
    char ruta_local[160];                      /* el GGUF del motor local */
};
struct qitem { char texto[MC_TEXTO_MAX + 1]; uint32_t t, intentos; uint64_t desde; };

static struct kmutex g_mx, g_emb_mx;
static struct mc g_m;
static struct conf g_c;
static int g_listo, g_hilo;
static struct qitem g_q[QN];
static uint32_t g_qn;
static volatile int g_agentes;
static volatile uint64_t g_libre_desde;
static volatile int g_guardar;
static uint64_t g_emb_cae_hasta;
static uint32_t g_emb_ok, g_emb_mal, g_n_hechos, g_n_dup, g_n_sust, g_n_fallo;
/* SOMA_279_SUENO */
static struct kmutex g_sm;                         /* un solo paso de sueno a la vez (el hilo o la orden `memoria sueno`) */
static char g_informe[10][150];                    /* los ultimos pasos, para `memoria sueno` y `memoria estado` */
static uint32_t g_ninforme;
static uint64_t g_vistos_red[64], g_vistos_ram[32];
static uint32_t g_nvr, g_nvm, g_cursor_red;
static uint32_t g_juzgadas[32], g_njuz;
static uint32_t g_n_rest, g_n_olv, g_n_fus;
static uint64_t g_ult_sueno;
static int g_dormido;                              /* 1 mientras se ejecuta un paso */
static int g_dim_servidor;
static uint8_t *g_buf;
/* SOMA_283_MEMORIA_APP: cartuchos, guardado serializado y peticiones de la aplicacion de Memoria */
static struct kmutex g_gm;                         /* un guardado a la vez (el hilo, la orden y los cambios de cartucho) */
static char g_pref[40] = "/memoria/";              /* carpeta del cartucho primario (el principal sigue en /memoria/, sin migrar nada) */
static volatile uint32_t g_gen;                    /* cambia cuando los ids dejan de valer (borrado, cambio de cartucho) */
static volatile uint32_t g_ver;                    /* cambia con cualquier cosa que la aplicacion deba repintar */
static volatile int g_extrayendo;
#define NCART 8
#define NMONT 3                                    /* cartuchos conectados a la vez (solo lectura), ademas del primario */
struct cart { char id[20], nombre[40], desc[100]; uint8_t usado, montado; uint32_t celdas, nodos, kb; };
static struct cart g_cart[NCART];
static int g_prim;                                 /* indice del primario en g_cart */
static struct mc *g_mont[NCART];                   /* cartuchos conectados (cargados); los lee el agente con g_mx tomado */

/* ------------------------------------------------------------------ utilidades */
void *mc_alloc(uint32_t n) { void *p = kmalloc(n); if (p) { uint8_t *b = (uint8_t *)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; } return p; }
void  mc_free(void *p) { kfree(p); }
static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int starts_with_(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static int eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int pre(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static void op(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void opn(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t n) { while (n-- && s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void opd(char *o, uint32_t cap, uint32_t *p, uint32_t v)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { if (*p + 1 < cap) o[(*p)++] = t[--n]; else n--; }
    if (cap) o[*p < cap ? *p : cap - 1] = 0;
}
static uint32_t ahora(void)
{
    struct rtc_time t; rtc_get_datetime(&t);
    return mc_epoch(t.year, t.month, t.day, t.hour, t.min, t.sec);
}
static void nombre_frag_en(const char *pref, uint32_t i, char *o) { uint32_t n = 0; while (*pref && n < 40) o[n++] = *pref++; o[n++] = 'm'; o[n++] = (char)('0' + i / 100 % 10); o[n++] = (char)('0' + i / 10 % 10); o[n++] = (char)('0' + i % 10); o[n++] = '.'; o[n++] = 'b'; o[n++] = 'i'; o[n++] = 'n'; o[n] = 0; }
static void nombre_frag(uint32_t i, char *o) { nombre_frag_en(g_pref, i, o); }
static void nombre_arbol(const char *pref, char *o) { uint32_t n = 0; while (*pref && n < 40) o[n++] = *pref++; const char *a = "arbol.bin"; while (*a) o[n++] = *a++; o[n] = 0; }

/* lee y escribe como el sistema (no como un agente: /etc/llm.conf y /memoria son del nucleo) */
static int sys_read(const char *n, char *b, uint32_t m) { struct tcb *t = thread_current(); int am = t ? t->agent_mode : 0; if (t) t->agent_mode = 0; int r = vfs_read(n, b, m); if (t) t->agent_mode = am; return r; }
static char *sys_read_alloc(const char *n, uint32_t *l) { struct tcb *t = thread_current(); int am = t ? t->agent_mode : 0; if (t) t->agent_mode = 0; char *r = vfs_read_alloc(n, l); if (t) t->agent_mode = am; return r; }
static int sys_write(const char *n, const char *b, uint32_t l) { struct tcb *t = thread_current(); int am = t ? t->agent_mode : 0; if (t) t->agent_mode = 0; int r = vfs_write(n, b, l); if (t) t->agent_mode = am; return r; }
static int sys_delete(const char *n) { struct tcb *t = thread_current(); int am = t ? t->agent_mode : 0; if (t) t->agent_mode = 0; int r = vfs_size(n) >= 0 ? vfs_delete(n) : 0; if (t) t->agent_mode = am; return r; }

/* ------------------------------------------------------------------ configuracion */
static int parse_url(const char *u, uint8_t ip[4], uint16_t *port)
{
    const char *p = "http://"; for (int i = 0; i < 7; i++) if (u[i] != p[i]) return 0;
    u += 7;
    for (int k = 0; k < 4; k++) {
        uint32_t v = 0; int nd = 0;
        while (*u >= '0' && *u <= '9' && nd < 4) { v = v * 10 + (uint32_t)(*u - '0'); u++; nd++; }
        if (!nd || v > 255) return 0;
        ip[k] = (uint8_t)v;
        if (k < 3) { if (*u != '.') return 0; u++; }
    }
    uint32_t pt = 80;
    if (*u == ':') { u++; pt = 0; while (*u >= '0' && *u <= '9' && pt < 100000) pt = pt * 10 + (uint32_t)(*u++ - '0'); }
    if ((*u && *u != '/') || !pt || pt > 65535) return 0;
    *port = (uint16_t)pt;
    return 1;
}
static void conf_srv(struct conf *c)
{
    c->srv = c->url[0] ? parse_url(c->url, c->ip, &c->port) : 0;
    c->local = 0; c->ruta_local[0] = 0;
#if defined(__x86_64__)
    /* SOMA_329_IA_LOCAL: con el modelo de embeddings (p. ej. bge-m3) en /modelos, la memoria calcula los vectores en SOMA mismo (motor
     * local en ring 3) sin servidor. "auto": local si esta el modelo; si no, el servidor de siempre. */
    if (c->motor != 2 && ia_disponible(0)) {
        int hay = ia_modelo_ruta(c->modelo, c->ruta_local, sizeof c->ruta_local) == 0;
        if (hay || c->motor == 1) { c->local = 1; c->srv = 1; }
    }
#endif
}
static void conf_defecto(struct conf *c)
{
    c->activa = 1; c->sueno = 1; c->sueno_s = 90; c->ingesta = 1; c->n_hechos = 10; c->n_epis = 20; c->srv = 0; c->url[0] = 0; c->modelo[0] = 0;
    const char *m = "bge-m3"; uint32_t i = 0; while (m[i]) { c->modelo[i] = m[i]; i++; } c->modelo[i] = 0;
    /* como memorIA: el servidor de embeddings esta en la misma maquina que el del modelo, en el puerto 8089 */
    const struct llm_conf *l = llm_conf_get();
    if (l && l->prov == LLM_LOCAL && pre(l->base[LLM_LOCAL], "http://")) {
        uint32_t n = 0; const char *b = l->base[LLM_LOCAL];
        while (b[n] && b[n] != '/' && n < 7) n++;
        uint32_t k = 7, o = 0; const char *ph = "http://";
        for (uint32_t j = 0; ph[j]; j++) c->url[o++] = ph[j];
        while (b[k] && b[k] != ':' && b[k] != '/' && o < 60) c->url[o++] = b[k++];
        const char *pp = ":8089"; for (uint32_t j = 0; pp[j]; j++) c->url[o++] = pp[j];
        c->url[o] = 0;
    }
}
static void conf_carga(void)
{
    static char t[1024];
    conf_defecto(&g_c);
    int n = sys_read(CONF, t, sizeof t - 1);
    if (n > 0) {
        t[n] = 0;
        for (char *l = t; *l;) {
            char *e = l; while (*e && *e != '\n') e++;
            char sv = *e; *e = 0;
            char *eq_ = l; while (*eq_ && *eq_ != '=') eq_++;
            if (*eq_ == '=') {
                *eq_ = 0; const char *k = l, *v = eq_ + 1;
                int x = 0; for (const char *d = v; *d >= '0' && *d <= '9'; d++) x = x * 10 + (*d - '0');
                if (eq(k, "activa")) g_c.activa = x != 0;
                else if (eq(k, "ingesta")) g_c.ingesta = x != 0;
                else if (eq(k, "sueno")) g_c.sueno = x != 0;
                else if (eq(k, "sueno_s")) g_c.sueno_s = x < 10 ? 10 : x > 3600 ? 3600 : x;
                else if (eq(k, "hechos")) g_c.n_hechos = x < 1 ? 1 : x > 40 ? 40 : x;
                else if (eq(k, "episodios")) g_c.n_epis = x < 0 ? 0 : x > 60 ? 60 : x;
                else if (eq(k, "emb_url")) { uint32_t i = 0; while (v[i] && i < sizeof g_c.url - 1) { g_c.url[i] = v[i]; i++; } g_c.url[i] = 0; }
                else if (eq(k, "emb_modelo")) { uint32_t i = 0; while (v[i] && i < sizeof g_c.modelo - 1) { g_c.modelo[i] = v[i]; i++; } g_c.modelo[i] = 0; }
                else if (eq(k, "emb_motor")) g_c.motor = eq(v, "local") ? 1 : eq(v, "servidor") ? 2 : 0;   /* SOMA_329 */
            }
            *e = sv; l = sv ? e + 1 : e;
        }
    }
    conf_srv(&g_c);
}
static void conf_guarda(void)
{
    static char t[512]; uint32_t p = 0;
    op(t, sizeof t, &p, "activa="); opd(t, sizeof t, &p, (uint32_t)g_c.activa);
    op(t, sizeof t, &p, "\ningesta="); opd(t, sizeof t, &p, (uint32_t)g_c.ingesta);
    op(t, sizeof t, &p, "\nsueno="); opd(t, sizeof t, &p, (uint32_t)g_c.sueno);
    op(t, sizeof t, &p, "\nsueno_s="); opd(t, sizeof t, &p, (uint32_t)g_c.sueno_s);
    op(t, sizeof t, &p, "\nhechos="); opd(t, sizeof t, &p, (uint32_t)g_c.n_hechos);
    op(t, sizeof t, &p, "\nepisodios="); opd(t, sizeof t, &p, (uint32_t)g_c.n_epis);
    op(t, sizeof t, &p, "\nemb_url="); op(t, sizeof t, &p, g_c.url);
    op(t, sizeof t, &p, "\nemb_modelo="); op(t, sizeof t, &p, g_c.modelo);
    op(t, sizeof t, &p, "\nemb_motor="); op(t, sizeof t, &p, g_c.motor == 1 ? "local" : g_c.motor == 2 ? "servidor" : "auto");
    op(t, sizeof t, &p, "\n");
    sys_write(CONF, t, p);
}

/* ------------------------------------------------------------------ servidor de embeddings */
static int json_esc(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t max)
{
    for (uint32_t i = 0; s[i] && i < max; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { if (*p + 2 >= cap) return 0; o[(*p)++] = '\\'; o[(*p)++] = (char)c; }
        else if (c < 0x20) { if (*p + 1 >= cap) return 0; o[(*p)++] = ' '; }
        else { if (*p + 1 >= cap) return 0; o[(*p)++] = (char)c; }
    }
    o[*p] = 0;
    return 1;
}
/* 1 = vector en out (dimension en *dim); 0 = no hay servidor o no contesta (se vuelve a intentar a los 30 s) */
static int emb_texto(const char *txt, int8_t *out, uint32_t *dim)
{
    static char body[3200], *resp;
#if defined(__x86_64__)
    {   /* SOMA_329_IA_LOCAL: en "auto", si aparece el modelo en /modelos (instalado despues de arrancar) se pasa al motor local */
        static uint64_t mirado;
        uint64_t t = timer_get_uptime_ms();
        if (g_c.motor == 0 && !g_c.local && (!mirado || t - mirado > 60000)) { mirado = t; conf_srv(&g_c); }
    }
#endif
    if (!g_c.srv || !txt || !*txt) return 0;
    uint64_t now = timer_get_uptime_ms();
    if (g_emb_cae_hasta && now < g_emb_cae_hasta) return 0;
    kmutex_lock(&g_emb_mx);
    if (!resp) resp = (char *)kmalloc(80 * 1024);
    int ok = 0;
    if (resp) {
        uint32_t p = 0;
        op(body, sizeof body, &p, "{\"input\":\""); 
        json_esc(body, sizeof body - 64, &p, txt, 1500);
        op(body, sizeof body, &p, "\",\"model\":\""); op(body, sizeof body, &p, g_c.modelo); op(body, sizeof body, &p, "\"}");
        struct http_response r; r.body = 0; r.body_len = 0; r.status_code = 0;
        int code;
#if defined(__x86_64__)
        if (g_c.local) {                       /* SOMA_329_IA_LOCAL: el motor local devuelve el mismo JSON que /v1/embeddings */
            static char err[200];
            if (!g_c.ruta_local[0] && ia_modelo_ruta(g_c.modelo, g_c.ruta_local, sizeof g_c.ruta_local) != 0) {
                static int intentado;              /* sin el modelo en /modelos: una vez por arranque se copia del pendrive */
                if (!intentado) { intentado = 1; (void)ia_instala(g_c.modelo, err, sizeof err); kprint("memoria: "); kprint(err); kprint("\n"); }
                (void)ia_modelo_ruta(g_c.modelo, g_c.ruta_local, sizeof g_c.ruta_local);
            }
            uint32_t n = 0; while (txt[n] && n < 6000) n++;
            int k = g_c.ruta_local[0] ? ia_orden(IA_CMD_EMBED, g_c.ruta_local, txt, n, resp, 80 * 1024, 180000, 0, err, sizeof err) : -1;
            if (k < 0 && g_c.ruta_local[0]) { kprint("memoria: motor local: "); kprint(err); kprint("\n"); }
            code = k >= 0 ? 200 : -1;
            if (k >= 0) { r.body = resp; r.body_len = (uint32_t)k; r.status_code = 200; }
        } else
#endif
        code = llm_http_local(g_c.ip, g_c.port, "/v1/embeddings", body, resp, 80 * 1024, &r, 8000, 0);   /* SOMA_327_RED_FUERA: por netsvc */
        if (code == 200 && r.body) {
            int d = mc_parse_embedding(r.body, out, MC_DIM_MAX);
            if (d > 0) { *dim = (uint32_t)d; g_dim_servidor = d; ok = 1; g_emb_ok++; }
        }
        if (!ok) { g_emb_mal++; g_emb_cae_hasta = timer_get_uptime_ms() + 30000u; }
    }
    kmutex_unlock(&g_emb_mx);
    return ok;
}

/* SOMA_327_RED_FUERA: prueba (tools/sbx_check.py): un embedding contra el servidor de url, por el camino real (netsvc) */
int memoria_prueba_emb(const char *url, const char *txt, uint32_t *dim)
{
    static int8_t v[MC_DIM_MAX];
    struct conf antes = g_c;
    uint32_t i = 0; while (url && url[i] && i + 1 < sizeof g_c.url) { g_c.url[i] = url[i]; i++; } g_c.url[i] = 0;
    g_c.motor = 2;                                     /* SOMA_329: esta prueba es la del servidor */
    conf_srv(&g_c); g_emb_cae_hasta = 0; *dim = 0;
    int ok = emb_texto(txt, v, dim);
    g_c = antes; g_emb_cae_hasta = 0;
    return ok;
}

/* ------------------------------------------------------------------ guardado */
static void guarda_sucios_(void)
{
    if (!g_listo) return;
    if (!g_buf) g_buf = (uint8_t *)kmalloc(mc_frag_max_bytes());
    if (!g_buf) return;
    for (uint32_t i = 0;; i++) {
        kmutex_lock(&g_mx);
        if (i >= mc_nfrag(&g_m)) { kmutex_unlock(&g_mx); break; }
        int len = -1;
        if (mc_frag_sucio(&g_m, i)) { len = mc_frag_guarda(&g_m, i, g_buf, mc_frag_max_bytes()); if (len > 0) mc_frag_limpio(&g_m, i); }
        char nm[56]; nombre_frag(i, nm);
        kmutex_unlock(&g_mx);
        if (len > 0) {
            if (sys_write(nm, (const char *)g_buf, (uint32_t)len) < 0) { kmutex_lock(&g_mx); g_m.sucio[i] = 1; kmutex_unlock(&g_mx); kprint("memoria: no se pudo guardar "); kprint(nm); kprint("\n"); }
        }
    }
}
static void guarda_arbol_(void)
{
    static uint8_t *ab;
    if (!g_listo) return;
    if (!ab) ab = (uint8_t *)kmalloc(mc_arbol_max_bytes());
    if (!ab) return;
    kmutex_lock(&g_mx);
    int len = g_m.arbol_sucio ? mc_arbol_guarda(&g_m, ab, mc_arbol_max_bytes()) : -1;
    if (len > 0) g_m.arbol_sucio = 0;
    char an[56]; nombre_arbol(g_pref, an);
    kmutex_unlock(&g_mx);
    if (len > 0 && sys_write(an, (const char *)ab, (uint32_t)len) < 0) {
        kmutex_lock(&g_mx); g_m.arbol_sucio = 1; kmutex_unlock(&g_mx);
        kprint("memoria: no se pudo guardar /memoria/arbol.bin\n");
    }
}
static void guarda_sucios(void) { if (!g_listo) return; kmutex_lock(&g_gm); guarda_sucios_(); kmutex_unlock(&g_gm); }
static void guarda_arbol(void) { if (!g_listo) return; kmutex_lock(&g_gm); guarda_arbol_(); kmutex_unlock(&g_gm); }
void memoria_guarda_todo(void) { guarda_sucios(); guarda_arbol(); }

/* SOMA_283_MEMORIA_APP: carga un cartucho (sus fragmentos y su arbol) en m. 0 = bien (vacio tambien vale), -1 = sin memoria */
static int carga_en(const char *pref, struct mc *m)
{
    if (mc_init(m, MC_MAX_ITEMS) != 0) return -1;
    struct mc *gm_ = m;
    uint32_t cargados = 0;
    for (uint32_t i = 0; i < MC_MAX_SHARDS; i++) {
        char nm[56]; nombre_frag_en(pref, i, nm);
        uint32_t len = 0; char *b = sys_read_alloc(nm, &len);
        if (!b) break;
        int r = mc_frag_carga(gm_, (const uint8_t *)b, len);
        kfree(b);
        if (r != 0) { kprint("memoria: fragmento danado, no se carga: "); kprint(nm); kprint("\n"); break; }
        cargados++;
    }
    for (uint32_t i = 0; i < MC_MAX_SHARDS; i++) gm_->sucio[i] = 0;
    (void)cargados;
    {   /* SOMA_279_SUENO: el arbol y el reloj; sin el (memoria del parche 277) el reloj arranca en lo ultimo que se uso */
        char an[56]; nombre_arbol(pref, an);
        uint32_t len = 0; char *b = sys_read_alloc(an, &len);
        int r = b ? mc_arbol_carga(gm_, (const uint8_t *)b, len) : -9;
        if (b) kfree(b);
        if (r != 0) {
            if (b) { kprint("memoria: "); kprint(an); kprint(" danado; el arbol se rehace con el ciclo de sueno\n"); }
            uint32_t mx = 0; for (uint32_t i = 0; i < gm_->n; i++) if (gm_->it[i].tick > mx) mx = gm_->it[i].tick;
            gm_->tick = mx;
            for (uint32_t i = 0; i < gm_->n; i++) gm_->it[i].nodo = 0;             /* sin arbol valido, los nodos de las celdas no significan nada */
        }
    }
    return 0;
}
static void registro_carga(void);
static void pref_de(int i, char *o);
static struct mc *mc_nueva(void);
static void mc_suelta(struct mc *m);
static void carga(void)
{
    registro_carga();                                   /* SOMA_283_MEMORIA_APP: que cartucho es el primario */
    carga_en(g_pref, &g_m);
    int nm = 0;                                         /* y los que estaban conectados se vuelven a conectar */
    for (int i = 0; i < NCART; i++) {
        if (!g_cart[i].usado || !g_cart[i].montado || i == g_prim) continue;
        struct mc *m = nm < NMONT ? mc_nueva() : 0; char pf[40]; pref_de(i, pf);
        if (m && carga_en(pf, m) == 0) { g_mont[i] = m; nm++; } else { mc_suelta(m); g_cart[i].montado = 0; }
    }
}

/* ------------------------------------------------------------------ extraccion de hechos (hilo) */
static void encola(const char *t, uint32_t when)
{
    kmutex_lock(&g_mx);
    if (g_qn == QN) { for (uint32_t i = 1; i < QN; i++) g_q[i - 1] = g_q[i]; g_qn--; }     /* cola llena: se descarta lo mas viejo */
    struct qitem *q = &g_q[g_qn++];
    uint32_t n = 0; while (t[n] && n < MC_TEXTO_MAX) { q->texto[n] = t[n]; n++; } q->texto[n] = 0;
    q->t = when; q->intentos = 0; q->desde = 0;
    kmutex_unlock(&g_mx);
}

static int llm_json(const char *prompt, char *reply, uint32_t cap)
{
    const char *sys = "Eres el modulo de memoria de un asistente. Respondes SOLO con JSON valido, sin explicaciones.";
    return llm_chat_ex(sys, prompt, 0, 0, 700, reply, cap, 120000);
}

/* guarda un hecho: duplicado exacto, duplicado o sustitucion decididos por el LLM (como memorIA), o celda nueva */
static void almacena_hecho(const struct mc_hecho *h, uint32_t t, const char *fuente)
{
    static char prompt[3000], reply[1500];
    static int8_t vec[MC_DIM_MAX];
    uint32_t dim = 0;
    int imp = h->instruccion && h->imp < 9 ? 9 : h->imp;
    uint32_t gen0 = g_gen;                                      /* SOMA_283_MEMORIA_APP */
    kmutex_lock(&g_mx);
    uint32_t ex = mc_find_exacto(&g_m, MC_CELDA, h->texto);
    if (ex) { mc_refuerza(&g_m, ex, g_m.tick); kmutex_unlock(&g_mx); g_n_dup++; return; }
    kmutex_unlock(&g_mx);
    int tiene = emb_texto(h->texto, vec, &dim);
    uint32_t sust = 0;
    if (tiene) {
        uint32_t nid[3]; int32_t nd[3];
        kmutex_lock(&g_mx);
        int nn = mc_vecinos(&g_m, vec, dim, 3, nid, nd);
        kmutex_unlock(&g_mx);
        for (int i = 0; i < nn && !sust; i++) {
            const int umbral_dup = 14194 /* 0,88 */, umbral_cerca = 5645 /* 0,35 */;
            kmutex_lock(&g_mx);
            const struct mc_item *o = mc_get(&g_m, nid[i]);
            char viejo[MC_TEXTO_MAX + 1]; uint32_t ot = 0;
            if (o) { while (o->texto[ot] && ot < MC_TEXTO_MAX) { viejo[ot] = o->texto[ot]; ot++; } viejo[ot] = 0; ot = o->t; }
            kmutex_unlock(&g_mx);
            if (!o) continue;
            int dup = nd[i] >= umbral_dup, cambio = nd[i] >= umbral_cerca && ot <= t && mc_parece_actualizacion(viejo, h->texto);
            if (!dup && !cambio) continue;
            char fecha[24]; mc_fecha(t, fecha, sizeof fecha);
            mc_prompt_relacion(viejo, nid[i], h->texto, fecha, prompt, sizeof prompt);
            if (!llm_json(prompt, reply, sizeof reply)) continue;
            int conf = 0, rel = mc_parse_relacion(reply, &conf);
            if (rel == MC_REL_IGUAL && conf >= 85) { kmutex_lock(&g_mx); if (gen0 == g_gen) mc_refuerza(&g_m, nid[i], g_m.tick); kmutex_unlock(&g_mx); g_n_dup++; return; }
            if (rel == MC_REL_SUSTITUYE && conf >= 85) sust = nid[i];
        }
    }
    kmutex_lock(&g_mx);
    if (gen0 != g_gen) sust = 0;
    uint32_t id = mc_add(&g_m, MC_CELDA, MC_ROL_USUARIO, h->texto, fuente, imp, t, g_m.tick, tiene ? vec : 0, tiene ? dim : 0);
    if (id && sust) { mc_set_estado(&g_m, sust, MC_SUSTITUIDO, t, id); g_n_sust++; }
    kmutex_unlock(&g_mx);
    if (id) { g_n_hechos++; g_guardar = 1; }
}

static void extrae(const struct qitem *q)
{
    static char prompt[MC_TEXTO_MAX + 2600], reply[3000];
    static struct mc_hecho hs[12];
    mc_prompt_hechos(q->texto, prompt, sizeof prompt);
    if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; return; }
    int n = mc_parse_hechos(reply, hs, 12);
    if (n < 0) { g_n_fallo++; return; }
    char fuente[MC_FUENTE_MAX], f[24]; uint32_t p = 0;
    mc_fecha(q->t, f, sizeof f);
    op(fuente, sizeof fuente, &p, "Conversacion con el usuario el "); op(fuente, sizeof fuente, &p, f);
    for (int i = 0; i < n; i++) almacena_hecho(&hs[i], q->t, fuente);
}

/* embebe lo que quedo sin vector (se guardo sin servidor o estaba caido); pocos por vuelta para no saturarlo */
static struct kmutex g_ep_mx;
static int embebe_pendientes_(void);
static int embebe_pendientes(void)                      /* la llama el hilo y el paso `embed` del sueno: usan los mismos buffers */
{
    kmutex_lock(&g_ep_mx);
    int n = embebe_pendientes_();
    kmutex_unlock(&g_ep_mx);
    return n;
}
static int embebe_pendientes_(void)
{
    int hechos = 0;
    static int8_t vec[MC_DIM_MAX]; static char txt[MC_TEXTO_MAX + 1];
    if (!g_c.srv || (g_emb_cae_hasta && timer_get_uptime_ms() < g_emb_cae_hasta)) return 0;
    for (int k = 0; k < 6; k++) {
        uint32_t id = 0;
        kmutex_lock(&g_mx);
        for (uint32_t i = g_m.n; i > 0; i--) if (!g_m.it[i - 1].vec) { id = i; break; }                        /* lo mas reciente primero */
        if (id) { const struct mc_item *e = &g_m.it[id - 1]; uint32_t n = 0; while (e->texto[n] && n < MC_TEXTO_MAX) { txt[n] = e->texto[n]; n++; } txt[n] = 0; }
        kmutex_unlock(&g_mx);
        if (!id) return hechos;
        uint32_t dim = 0;
        if (!emb_texto(txt, vec, &dim)) return hechos;
        kmutex_lock(&g_mx); mc_set_vec(&g_m, id, vec, dim); kmutex_unlock(&g_mx);
        g_guardar = 1; hechos++;
    }
    return hechos;
}


/* ------------------------------------------------------------------ SOMA_279_SUENO: el ciclo de sueno (las 9 tareas de memorIA) */
#define NTAREAS 9
static const char *const TAREAS[NTAREAS] = { "repair", "embed", "decay", "ordena", "describe", "dedupe", "reevaluate", "split", "merge" };
#define MEM_TAU 20u                                /* constantes de memorIA (config.py): MEM_TAU, MEM_MIN_AGE, MEM_ARCHIVE_BELOW = 0,5 */
#define MEM_MIN_AGE 10u
#define MEM_BAJO_Q8 128u

static void informa(const char *tarea, const char *txt)
{
    char l[150]; uint32_t p = 0;
    op(l, sizeof l, &p, "paso "); opd(l, sizeof l, &p, g_m.pasos); op(l, sizeof l, &p, " ["); op(l, sizeof l, &p, tarea); op(l, sizeof l, &p, "] "); op(l, sizeof l, &p, txt);
    kmutex_lock(&g_mx);
    if (g_ninforme == 10) { for (uint32_t i = 1; i < 10; i++) { uint32_t k = 0; while ((g_informe[i - 1][k] = g_informe[i][k])) k++; } g_ninforme--; }
    uint32_t k = 0; while (l[k] && k < sizeof g_informe[0] - 1) { g_informe[g_ninforme][k] = l[k]; k++; } g_informe[g_ninforme][k] = 0; g_ninforme++;
    g_ver++;
    kmutex_unlock(&g_mx);
}
static void vistos_mete(uint64_t *v, uint32_t *n, uint32_t cap, uint32_t a, uint32_t b)
{
    uint64_t k = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    if (*n == cap) { for (uint32_t i = 1; i < cap; i++) v[i - 1] = v[i]; (*n)--; }
    v[(*n)++] = k;
}
static int mismo_texto(const char *a, const char *b)
{
    char fa[MC_TEXTO_MAX + 1], fb[MC_TEXTO_MAX + 1];
    mc_fold(a, fa, sizeof fa); mc_fold(b, fb, sizeof fb);
    return fa[0] && eq(fa, fb);
}

static void t_repair(char *r, uint32_t cap)
{
    uint32_t p = 0, huerf = 0, rotas = 0, podados;
    kmutex_lock(&g_mx);
    for (uint32_t i = 0; i < g_m.n; i++) {
        struct mc_item *e = &g_m.it[i];
        if (e->nodo && !g_m.nodo[e->nodo].usado && e->estado == MC_ACTIVO) { e->nodo = 0; huerf++; g_m.sucio[i / MC_SHARD] = 1; }
        if (e->estado == MC_SUSTITUIDO && (!e->sust_por || e->sust_por > g_m.n)) { e->estado = MC_ACTIVO; e->t_hasta = 0; e->sust_por = 0; rotas++; g_m.sucio[i / MC_SHARD] = 1; }
    }
    mc_recuenta(&g_m);
    podados = mc_poda_nodos(&g_m);
    kmutex_unlock(&g_mx);
    if (!huerf && !rotas && !podados) { op(r, cap, &p, "· arbol y referencias en orden"); return; }
    if (podados) { opd(r, cap, &p, podados); op(r, cap, &p, " nodo(s) vacio(s) podado(s); "); }
    if (huerf) { opd(r, cap, &p, huerf); op(r, cap, &p, " celda(s) sin rama, a reordenar; "); }
    if (rotas) { opd(r, cap, &p, rotas); op(r, cap, &p, " sustitucion(es) rota(s) deshecha(s)"); }
}
static void t_embed(char *r, uint32_t cap)
{
    uint32_t p = 0;
    if (!g_c.srv) { op(r, cap, &p, "· sin servidor de embeddings configurado"); return; }
    int n = embebe_pendientes();
    if (n) { opd(r, cap, &p, (uint32_t)n); op(r, cap, &p, " recuerdo(s) embebidos"); } else op(r, cap, &p, "· nada pendiente de embeber (o el servidor no contesta)");
}
static void t_decay(char *r, uint32_t cap)
{
    uint32_t p = 0;
    kmutex_lock(&g_mx);
    uint32_t n = mc_olvida(&g_m, g_m.tick, MEM_MIN_AGE, MEM_TAU, MEM_BAJO_Q8);
    kmutex_unlock(&g_mx);
    if (!n) { op(r, cap, &p, "· celdas vivas estables, ninguna que olvidar"); return; }
    g_n_olv += n; g_guardar = 1;
    opd(r, cap, &p, n); op(r, cap, &p, " celda(s) archivadas por baja importancia efectiva (se recuperan si se vuelven a necesitar)");
}
static void t_ordena(char *r, uint32_t cap)
{
    static char prompt[5000], reply[2500]; static struct mc_asig as[8];
    uint32_t p = 0, ids[8];
    kmutex_lock(&g_mx);
    uint32_t n = mc_sin_nodo(&g_m, ids, 8);
    if (n) mc_prompt_ordena(&g_m, ids, n, prompt, sizeof prompt);
    kmutex_unlock(&g_mx);
    if (!n) { op(r, cap, &p, "· todo ordenado en el arbol"); return; }
    if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; op(r, cap, &p, "· el LLM no contesta; se reintenta"); return; }
    int k = mc_parse_ordena(reply, as, 8), puestas = 0;
    kmutex_lock(&g_mx);
    for (int i = 0; i < k; i++) {
        int pedida = 0; for (uint32_t j = 0; j < n; j++) if (ids[j] == as[i].id) pedida = 1;      /* solo las que se preguntaron */
        const struct mc_item *e = pedida ? mc_get(&g_m, as[i].id) : 0;
        if (!e || e->kind != MC_CELDA || e->estado != MC_ACTIVO || e->nodo) continue;
        uint32_t nodo = mc_nodo_ruta(&g_m, as[i].ruta, 1);
        if (nodo && mc_asigna(&g_m, as[i].id, nodo) == 0) puestas++;
    }
    mc_recuenta(&g_m);
    kmutex_unlock(&g_mx);
    if (puestas) g_guardar = 1;
    opd(r, cap, &p, (uint32_t)puestas); op(r, cap, &p, " de "); opd(r, cap, &p, n); op(r, cap, &p, " celda(s) colocadas en el arbol");
    if (k < 0) { g_n_fallo++; op(r, cap, &p, " (respuesta ilegible)"); }
}
static void t_describe(char *r, uint32_t cap)
{
    static char prompt[2600], reply[900];
    uint32_t p = 0, hechas = 0;
    for (int v = 0; v < 2; v++) {
        kmutex_lock(&g_mx);
        mc_recuenta(&g_m);
        uint32_t nodo = mc_nodo_sucio(&g_m);
        if (nodo) mc_prompt_describe(&g_m, nodo, prompt, sizeof prompt);
        kmutex_unlock(&g_mx);
        if (!nodo) break;
        if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; break; }
        char d[MC_NODO_DESC];
        int ok = mc_parse_descripcion(reply, d, sizeof d);
        kmutex_lock(&g_mx);
        if (g_m.nodo[nodo].usado) mc_nodo_set_desc(&g_m, nodo, ok ? d : "Area general");
        kmutex_unlock(&g_mx);
        hechas++;
    }
    if (!hechas) { op(r, cap, &p, "· descripciones al dia"); return; }
    g_guardar = 1;
    opd(r, cap, &p, hechas); op(r, cap, &p, " descripcion(es) de nodo sintetizadas");
}
static void t_dedupe(char *r, uint32_t cap)
{
    static char prompt[1800], reply[1500];
    uint32_t p = 0, a = 0, b = 0;
    int igual = 0;
    kmutex_lock(&g_mx);
    int hay = mc_par_redundante(&g_m, &g_cursor_red, g_vistos_red, g_nvr, &a, &b);
    if (hay) {
        igual = mismo_texto(g_m.it[a - 1].texto, g_m.it[b - 1].texto);
        if (!igual) mc_prompt_fusion(&g_m, a, b, prompt, sizeof prompt);
    }
    kmutex_unlock(&g_mx);
    if (!hay) { op(r, cap, &p, "· celdas revisadas, sin redundancias detectadas"); return; }
    struct mc_fusion fu; fu.redundante = 1; fu.texto[0] = 0; fu.imp = 0;
    if (!igual) {
        if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; op(r, cap, &p, "· el LLM no contesta; se reintenta"); return; }
        if (!mc_parse_fusion(reply, &fu)) { g_n_fallo++; kmutex_lock(&g_mx); vistos_mete(g_vistos_red, &g_nvr, 64, a, b); kmutex_unlock(&g_mx); op(r, cap, &p, "· respuesta ilegible; pareja descartada por ahora"); return; }
    }
    kmutex_lock(&g_mx);
    int rc = fu.redundante ? mc_aplica_fusion(&g_m, a, b, &fu, g_m.tick, ahora()) : -1;
    if (rc != 0) vistos_mete(g_vistos_red, &g_nvr, 64, a, b);           /* distintas, o ya cambiaron: no se vuelve a preguntar */
    kmutex_unlock(&g_mx);
    if (rc != 0) { op(r, cap, &p, "· #"); opd(r, cap, &p, a); op(r, cap, &p, " y #"); opd(r, cap, &p, b); op(r, cap, &p, " parecidas pero distintas: se conservan las dos"); return; }
    g_n_fus++; g_guardar = 1;
    op(r, cap, &p, "fusion de redundancia: #"); opd(r, cap, &p, a); op(r, cap, &p, " + #"); opd(r, cap, &p, b); op(r, cap, &p, " (la #"); opd(r, cap, &p, b); op(r, cap, &p, " queda como sustituida)");
}
static void t_reevaluate(char *r, uint32_t cap)
{
    static char prompt[1200], reply[500];
    uint32_t p = 0;
    kmutex_lock(&g_mx);
    uint32_t id = mc_candidato_rescate(&g_m, g_m.tick, MEM_MIN_AGE, MEM_TAU, MEM_BAJO_Q8, g_juzgadas, g_njuz);
    if (id) mc_prompt_rescate(&g_m, id, prompt, sizeof prompt);
    kmutex_unlock(&g_mx);
    if (!id) { op(r, cap, &p, "· importancias y prioridades en equilibrio"); return; }
    if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; op(r, cap, &p, "· el LLM no contesta; se reintenta"); return; }
    int imp = 0, v = mc_parse_rescate(reply, &imp);
    kmutex_lock(&g_mx);
    if (v == 1) mc_set_imp(&g_m, id, imp, g_m.tick);
    else { if (g_njuz == 32) { for (uint32_t i = 1; i < 32; i++) g_juzgadas[i - 1] = g_juzgadas[i]; g_njuz--; } g_juzgadas[g_njuz++] = id; }
    kmutex_unlock(&g_mx);
    if (v == 1) { g_guardar = 1; op(r, cap, &p, "rescate homeostatico: #"); opd(r, cap, &p, id); op(r, cap, &p, " reevaluada a importancia "); opd(r, cap, &p, (uint32_t)imp); }
    else { op(r, cap, &p, "· #"); opd(r, cap, &p, id); op(r, cap, &p, v == 0 ? " no merece preservarse: seguira su curso" : " sin veredicto claro"); }
}
static void t_split(char *r, uint32_t cap)
{
    static char prompt[3200], reply[1800]; static struct mc_split sp;
    uint32_t p = 0;
    kmutex_lock(&g_mx);
    uint32_t nodo = mc_nodo_sobrecargado(&g_m);
    if (nodo) mc_prompt_split(&g_m, nodo, prompt, sizeof prompt);
    kmutex_unlock(&g_mx);
    if (!nodo) { op(r, cap, &p, "· ramificacion optima (como mucho "); opd(r, cap, &p, MC_MAX_HIJOS); op(r, cap, &p, " celdas por nodo)"); return; }
    if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; op(r, cap, &p, "· el LLM no contesta; se reintenta"); return; }
    if (!mc_parse_split(reply, &sp)) { g_n_fallo++; op(r, cap, &p, "· respuesta ilegible; se reintenta"); return; }
    kmutex_lock(&g_mx);
    char nm[MC_NODO_NOMBRE]; { uint32_t k = 0; while (g_m.nodo[nodo].nombre[k] && k < sizeof nm - 1) { nm[k] = g_m.nodo[nodo].nombre[k]; k++; } nm[k] = 0; }
    int mv = g_m.nodo[nodo].usado ? mc_aplica_split(&g_m, nodo, &sp) : 0;
    kmutex_unlock(&g_mx);
    if (mv <= 0) { op(r, cap, &p, "· particion descartada (el LLM no dio grupos validos)"); return; }
    g_guardar = 1;
    op(r, cap, &p, "particion de '"); op(r, cap, &p, nm); op(r, cap, &p, "': "); opd(r, cap, &p, (uint32_t)mv); op(r, cap, &p, " celda(s) movidas a '"); op(r, cap, &p, sp.nombre_b); op(r, cap, &p, "'");
}
static void t_merge(char *r, uint32_t cap)
{
    static char prompt[1200], reply[500];
    uint32_t p = 0, a = 0, b = 0; int sim = -1;
    kmutex_lock(&g_mx);
    mc_recuenta(&g_m);
    int hay = mc_par_ramas(&g_m, g_vistos_ram, g_nvm, &a, &b, &sim);
    if (hay) mc_prompt_unir(&g_m, a, b, sim, prompt, sizeof prompt);
    kmutex_unlock(&g_mx);
    if (!hay) { op(r, cap, &p, "· ramas integradas y sin hojas dispersas parecidas"); return; }
    if (!llm_json(prompt, reply, sizeof reply)) { g_n_fallo++; op(r, cap, &p, "· el LLM no contesta; se reintenta"); return; }
    char nm[MC_NODO_NOMBRE]; int v = mc_parse_unir(reply, nm, sizeof nm);
    kmutex_lock(&g_mx);
    char n1[MC_NODO_NOMBRE], n2[MC_NODO_NOMBRE];
    { uint32_t k = 0; while (g_m.nodo[a].nombre[k] && k < sizeof n1 - 1) { n1[k] = g_m.nodo[a].nombre[k]; k++; } n1[k] = 0; k = 0; while (g_m.nodo[b].nombre[k] && k < sizeof n2 - 1) { n2[k] = g_m.nodo[b].nombre[k]; k++; } n2[k] = 0; }
    int rc = -1;
    if (v == 1) rc = mc_nodo_fusiona(&g_m, a, b, nm[0] ? nm : 0);
    else vistos_mete(g_vistos_ram, &g_nvm, 32, a, b);
    kmutex_unlock(&g_mx);
    if (rc != 0) { op(r, cap, &p, "· '"); op(r, cap, &p, n1); op(r, cap, &p, "' y '"); op(r, cap, &p, n2); op(r, cap, &p, "' son temas distintos: se quedan separados"); return; }
    g_guardar = 1;
    op(r, cap, &p, "fusion de ramas: '"); op(r, cap, &p, n1); op(r, cap, &p, "' + '"); op(r, cap, &p, n2); op(r, cap, &p, "'");
}

/* un paso: sube el reloj y hace la tarea que toca. Devuelve el numero de tarea. */
static int sueno_paso(char *r, uint32_t cap)
{
    kmutex_lock(&g_sm);
    g_dormido = 1;
    kmutex_lock(&g_mx); g_m.tick++; g_m.pasos++; g_m.arbol_sucio = 1; uint32_t k = (g_m.pasos - 1) % NTAREAS; kmutex_unlock(&g_mx);
    r[0] = 0;
    switch (k) {
    case 0: t_repair(r, cap); break;      case 1: t_embed(r, cap); break;     case 2: t_decay(r, cap); break;
    case 3: t_ordena(r, cap); break;      case 4: t_describe(r, cap); break;  case 5: t_dedupe(r, cap); break;
    case 6: t_reevaluate(r, cap); break;  case 7: t_split(r, cap); break;    default: t_merge(r, cap); break;
    }
    informa(TAREAS[k], r);
    g_guardar = 1;
    g_ult_sueno = timer_get_uptime_ms();
    g_dormido = 0;
    kmutex_unlock(&g_sm);
    return (int)k;
}

static void hilo(void *arg)
{
    (void)arg;
    uint64_t ult_guardado = 0;
    for (;;) {
        thread_sleep(1000);
        if (!g_listo || !g_c.activa) continue;
        uint64_t now = timer_get_uptime_ms();
        if (g_agentes == 0 && now - g_libre_desde >= 4000) {                  /* nadie esta usando el LLM desde hace unos segundos */
            struct qitem q; int hay = 0;
            kmutex_lock(&g_mx);
            if (g_qn && g_c.ingesta) { q = g_q[0]; for (uint32_t i = 1; i < g_qn; i++) g_q[i - 1] = g_q[i]; g_qn--; hay = 1; }
            kmutex_unlock(&g_mx);
            if (hay) {
                g_extrayendo = 1; g_ver++; extrae(&q); g_extrayendo = 0; g_ver++;
                if (g_agentes) continue;
            } else if (g_c.sueno && g_agentes == 0 && now - g_libre_desde >= 30000 && now - g_ult_sueno >= (uint64_t)g_c.sueno_s * 1000u) {
                char rep[150]; sueno_paso(rep, sizeof rep);               /* todo en calma: un paso del ciclo de sueno */
            } else embebe_pendientes();
        }
        if (g_guardar && now - ult_guardado >= 5000) { g_guardar = 0; ult_guardado = now; guarda_sucios(); guarda_arbol(); }
    }
}

static void ops_arranca(void);
static void inicia(void)
{
    if (g_listo) return;
    kmutex_init(&g_mx, "memoria"); kmutex_init(&g_emb_mx, "memoria-emb"); kmutex_init(&g_sm, "memoria-sueno"); kmutex_init(&g_ep_mx, "memoria-embebe");
    kmutex_init(&g_gm, "memoria-guarda");                                       /* SOMA_283_MEMORIA_APP */
    conf_carga();
    carga();
    g_libre_desde = timer_get_uptime_ms();
    g_listo = 1;
    if (!g_hilo) { g_hilo = 1; thread_create("memoria", hilo, 0); }
    ops_arranca();                                                              /* SOMA_283_MEMORIA_APP: peticiones de la aplicacion */
}
static int lista(void) { if (!g_listo) inicia(); return g_listo && g_c.activa; }

int memoria_activa(void) { return lista(); }
void memoria_agente_entra(void) { __atomic_add_fetch(&g_agentes, 1, __ATOMIC_SEQ_CST); }
void memoria_agente_sale(void) { if (__atomic_sub_fetch(&g_agentes, 1, __ATOMIC_SEQ_CST) <= 0) { g_agentes = 0; g_libre_desde = timer_get_uptime_ms(); } }

/* ------------------------------------------------------------------ para el agente */
void memoria_registra(const char *usuario, const char *respuesta)
{
    if (!lista()) return;
    uint32_t t = ahora(); int a = usuario && sl(usuario) > 0;
    kmutex_lock(&g_mx);
    if (a) mc_add(&g_m, MC_EPISODIO, MC_ROL_USUARIO, usuario, "", 5, t, g_m.tick, 0, 0);
    if (respuesta && *respuesta) mc_add(&g_m, MC_EPISODIO, MC_ROL_ASISTENTE, respuesta, "", 5, t, g_m.tick, 0, 0);
    kmutex_unlock(&g_mx);
    if (a && g_c.ingesta && !mc_noFactual(usuario)) encola(usuario, t);
    g_guardar = 1;
}

/* SOMA_279_SUENO: lo que el olvido archivo y una consulta vuelve a pedir, se restaura (como memorIA). Con g_mx tomado. */
static void restaura_busqueda(const int8_t *q, uint32_t qd)
{
    uint32_t ids[8], arch[8], na = 0; int32_t sc[8];
    if (!q || !qd) return;
    int n = mc_busca(&g_m, q, qd, "", 1u << MC_CELDA, 1, 8064 /* 0,5 */, 8, ids, sc);
    for (int i = 0; i < n; i++) if (sc[i] >= 0 && g_m.it[ids[i] - 1].estado == MC_ARCHIVADO) arch[na++] = ids[i];
    if (na) { uint32_t r = mc_restaura(&g_m, arch, na, g_m.tick); if (r) { g_n_rest += r; g_guardar = 1; } }
}
/* SOMA_280_IMPORTAR: la agenda dentro de la memoria. Si lo que se pregunta trata de planes, citas o fechas, el contexto lleva los eventos de los
 * proximos 7 dias (hora local), para que la respuesta no dependa de que el modelo se acuerde de mirar la agenda. Solo lee; no apunta nada. */
static int pide_agenda(const char *q)
{
    static const char *const P[] = { "agenda", "calendario", "hoy", "manana", "semana", "cita", "citas", "reunion", "reuniones", "evento", "eventos", "proximo", "proxima",
        "proximos", "proximas", "pendiente", "pendientes", "recordatorio", "recordatorios", "cumple", "cumpleanos", "planes", "libre", "ocupado", "ocupada", "agendado", 0 };
    char f[400]; uint32_t n = mc_fold(q, f, sizeof f); (void)n;
    for (const char *w = f; *w;) {
        const char *e = w; while (*e && *e != ' ') e++;
        uint32_t l = (uint32_t)(e - w);
        for (int i = 0; P[i]; i++) if (sl(P[i]) == l && starts_with_(w, P[i])) return 1;
        w = *e ? e + 1 : e;
    }
    return 0;
}
static void agenda_bloque(const char *mision, char *dst, uint32_t max, uint32_t *pos)
{
    if (!pide_agenda(mision) || *pos + 1700 >= max) return;
    char *tx = (char *)mc_alloc(1400); if (!tx) return;
    int64_t ahora_ = agenda_ahora(), hoy = ag_dia_de(ahora_);
    if (ag_lista_txt(agenda_cal(), hoy, hoy + 7, ahora_, tx, 1400) && tx[0]) {
        op(dst, max, pos, "\n[AGENDA de SOMA: lo apuntado para los proximos 7 dias, en hora local. Es lo que consta en la agenda; no inventes eventos que no esten aqui.]\n");
        op(dst, max, pos, tx);
    }
    mc_free(tx);
}
int memoria_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos)
{
    int8_t q[MC_DIM_MAX];                                        /* sin estaticos: varios agentes pueden llamar a la vez */
    if (!lista() || !mision || !*mision || *pos + 2048 >= max) return 0;
    const uint32_t TXT = 16 * 1024;
    uint32_t tope = (max - *pos) / 3; if (tope > TXT) tope = TXT;
    char *txt = (char *)mc_alloc(TXT);
    if (!txt) return 0;          /* la memoria nunca se come mas de un tercio del prompt que queda */
    uint32_t qd = 0;
    int tiene = emb_texto(mision, q, &qd);
    uint32_t cel[40], epi[64], rec[4], nrec = 0; int32_t sc[64];
    int nc, ne = 0;
    kmutex_lock(&g_mx);
    restaura_busqueda(tiene ? q : 0, tiene ? qd : 0);
    nc = mc_busca(&g_m, tiene ? q : 0, tiene ? qd : 0, mision, 1u << MC_CELDA, 0, 6129, (uint32_t)g_c.n_hechos, cel, sc);
    for (uint32_t i = g_m.n; i > 0 && nrec < 4; i--) if (g_m.it[i - 1].kind == MC_EPISODIO) rec[nrec++] = i;      /* los ultimos mensajes: el hilo de la charla */
    if (g_c.n_epis > 0) ne = mc_busca(&g_m, tiene ? q : 0, tiene ? qd : 0, mision, 1u << MC_EPISODIO, 0, 6129, (uint32_t)g_c.n_epis + nrec, epi, sc);
    /* los recientes se muestran aparte, en orden cronologico; no se repiten entre los literales */
    uint32_t sel[64], ns = 0;
    for (int i = 0; i < ne; i++) { int rep = 0; for (uint32_t j = 0; j < nrec; j++) if (rec[j] == epi[i]) rep = 1; if (!rep && ns < (uint32_t)g_c.n_epis) sel[ns++] = epi[i]; }
    uint32_t orden[4], no = 0; for (uint32_t j = nrec; j > 0; j--) orden[no++] = rec[j - 1];
    uint32_t n1 = mc_contexto(&g_m, cel, (uint32_t)nc, sel, ns, txt, tope - 700);
    if (no) n1 += mc_contexto_charla(&g_m, orden, no, txt + n1, tope - n1);
    for (int c = 0; c < NCART && n1 + 900 < tope; c++) {        /* SOMA_283_MEMORIA_APP: los cartuchos conectados se consultan tambien (solo lectura) */
        if (!g_mont[c]) continue;
        uint32_t k = (uint32_t)(g_c.n_hechos / 2 < 3 ? 3 : g_c.n_hechos / 2), cc[40]; int32_t sc2[40];
        int n2 = mc_busca(g_mont[c], tiene ? q : 0, tiene ? qd : 0, mision, 1u << MC_CELDA, 0, 6129, k, cc, sc2);
        if (n2 <= 0) continue;
        uint32_t p2 = n1;
        op(txt, tope, &p2, "\n[Del cartucho de memoria \""); op(txt, tope, &p2, g_cart[c].nombre); op(txt, tope, &p2, "\" (conectado, solo lectura)]");
        n1 = p2 + mc_contexto(g_mont[c], cc, (uint32_t)n2, 0, 0, txt + p2, tope - p2);
    }
    kmutex_unlock(&g_mx);
    if (!txt[0]) { mc_free(txt); uint32_t p0 = *pos; agenda_bloque(mision, dst, max, pos); return *pos > p0; }
    op(dst, max, pos, "\n[MEMORIA: lo que SOMA recuerda de ti y de lo hablado. Regla de atribucion: cada recuerdo dice de quien es; antes de usarlo comprueba que "
                      "pertenece a la persona o cosa por la que se pregunta, y si no consta di que no consta en vez de deducirlo.]");
    op(dst, max, pos, txt);
    mc_free(txt);
    agenda_bloque(mision, dst, max, pos);
    return 1;
}

static void una_linea(const char *s, char *o, uint32_t cap, uint32_t max)
{
    uint32_t n = 0; for (uint32_t i = 0; s[i] && n < max && n + 1 < cap; i++) o[n++] = (s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
    if (n == max && s[n]) { o[n - 1] = '.'; if (n >= 3) { o[n - 2] = '.'; o[n - 3] = '.'; } }
    o[n] = 0;
}

/* busca para mostrar (orden y mem_search): devuelve cuantos */
static int busca_texto(const char *consulta, char *out, uint32_t max, uint32_t *pos, int con_episodios)
{
    int8_t q[MC_DIM_MAX]; uint32_t qd = 0, ids[16]; int32_t sc[16];
    int tiene = emb_texto(consulta, q, &qd);
    kmutex_lock(&g_mx);
    restaura_busqueda(tiene ? q : 0, tiene ? qd : 0);
    int n = mc_busca(&g_m, tiene ? q : 0, tiene ? qd : 0, consulta, con_episodios ? (1u << MC_CELDA) | (1u << MC_EPISODIO) : 1u << MC_CELDA, 0, 6129, 8, ids, sc);
    for (int i = 0; i < n; i++) {
        const struct mc_item *e = mc_get(&g_m, ids[i]); if (!e) continue;
        char f[24], l[200]; mc_fecha(e->t, f, sizeof f); una_linea(e->texto, l, sizeof l, 170);
        op(out, max, pos, "#"); opd(out, max, pos, e->id);
        op(out, max, pos, e->kind == MC_CELDA ? " hecho " : e->rol == MC_ROL_ASISTENTE ? " asistente " : " usuario ");
        if (sc[i] >= 0) { opd(out, max, pos, (uint32_t)(sc[i] * 100 / MC_COS_UNO)); op(out, max, pos, "% "); } else op(out, max, pos, "(palabras) ");
        opn(out, max, pos, f, 10); op(out, max, pos, ": "); op(out, max, pos, l); op(out, max, pos, "\n");
    }
    for (int c = 0; c < NCART; c++) {                              /* SOMA_283_MEMORIA_APP: y en los cartuchos conectados */
        if (!g_mont[c]) continue;
        uint32_t i2[6]; int32_t s2[6];
        int n2 = mc_busca(g_mont[c], tiene ? q : 0, tiene ? qd : 0, consulta, 1u << MC_CELDA, 0, 6129, 6, i2, s2);
        for (int i = 0; i < n2; i++) {
            const struct mc_item *e = mc_get(g_mont[c], i2[i]); if (!e) continue;
            char l[200]; una_linea(e->texto, l, sizeof l, 150);
            op(out, max, pos, "["); op(out, max, pos, g_cart[c].nombre); op(out, max, pos, "] ");
            if (s2[i] >= 0) { opd(out, max, pos, (uint32_t)(s2[i] * 100 / MC_COS_UNO)); op(out, max, pos, "% "); } else op(out, max, pos, "(palabras) ");
            op(out, max, pos, l); op(out, max, pos, "\n"); n++;
        }
    }
    kmutex_unlock(&g_mx);
    return n;
}
int memoria_busca_agente(const char *consulta, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    if (!lista()) return 0;
    int n = busca_texto(consulta, out, max, &p, 1);
    if (!n) op(out, max, &p, "Sin coincidencias en la memoria.");
    return n;
}

static uint32_t guarda_celda(const char *texto, const char *fuente, int imp)
{
    int8_t v[MC_DIM_MAX]; uint32_t d = 0;
    int t = emb_texto(texto, v, &d);
    kmutex_lock(&g_mx);
    uint32_t ex = mc_find_exacto(&g_m, MC_CELDA, texto), id = ex;
    if (ex) mc_refuerza(&g_m, ex, g_m.tick); else id = mc_add(&g_m, MC_CELDA, MC_ROL_USUARIO, texto, fuente, imp, ahora(), g_m.tick, t ? v : 0, t ? d : 0);
    kmutex_unlock(&g_mx);
    g_guardar = 1;
    return id;
}
int memoria_recuerda_agente(const char *texto, const char *fuente, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    if (!lista() || !texto || !*texto) { op(out, max, &p, "Error: nada que recordar."); return 0; }
    uint32_t id = guarda_celda(texto, fuente && *fuente ? fuente : "Anotado por un agente", 7);
    if (!id) { op(out, max, &p, "Error: la memoria esta llena."); return 0; }
    op(out, max, &p, "Recuerdo guardado (#"); opd(out, max, &p, id); op(out, max, &p, ").");
    return 1;
}

/* ------------------------------------------------------------------ SOMA_280_IMPORTAR: el cartucho de memorIA (.smem) */
static char minus(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int contiene_(const char *h, const char *n)
{
    uint32_t ln = sl(n); if (!ln) return 0;
    for (; *h; h++) { uint32_t k = 0; while (k < ln && h[k] && minus(h[k]) == minus(n[k])) k++; if (k == ln) return 1; }
    return 0;
}
static int modelo_ok(const char *a, const char *b) { return a && b && a[0] && b[0] && (contiene_(a, b) || contiene_(b, a)); }
static void importa_smem(const char *ruta, int con_vec, char *out, uint32_t max, uint32_t *p)
{
    uint32_t len = 0; char *b = sys_read_alloc(ruta, &len);
    if (!b) { op(out, max, p, "No encuentro "); op(out, max, p, ruta); op(out, max, p, ". Se descarga con: descargar http://IP_DEL_MAC:PUERTO/CLAVE/memoria-001.smem /usuario/memoria-001.smem"); return; }
    if (!starts_with_(b, "#SOMAMEM ")) { op(out, max, p, ruta); op(out, max, p, " no es un archivo .smem (lo escribe tools/exporta_memoria.py en el Mac)."); kfree(b); return; }
    uint32_t nc = 1; for (const char *q = b; *q; q++) if (*q == '\n' && q[1] == 'C') nc++;
    if (nc > MC_MAX_ITEMS) nc = MC_MAX_ITEMS;
    struct mc_imp si; for (uint32_t i = 0; i < sizeof si; i++) ((char *)&si)[i] = 0;
    kmutex_lock(&g_sm);                                          /* el ciclo de sueno no toca la memoria mientras entra lo importado */
    kmutex_lock(&g_mx);
    si.ahora = ahora();
    int r0 = mc_imp_ini(&g_m, &si, nc + 1);
    kmutex_unlock(&g_mx);
    if (r0) { kmutex_unlock(&g_sm); kfree(b); op(out, max, p, "memoria: sin memoria suficiente para importar"); return; }
    uint32_t lineas = 0; int motivo = 0, fin = 0;
    for (char *l = b; *l && !fin;) {
        kmutex_lock(&g_mx);
        for (uint32_t k = 0; k < 200 && *l; k++) {
            char *e = l; while (*e && *e != '\n') e++;
            char sv = *e; *e = 0;
            int rc = mc_imp_linea(&g_m, &si, l);
            lineas++;
            *e = sv; l = sv ? e + 1 : e;
            if (lineas == 1) {                                  /* la cabecera dice con que modelo se calcularon los vectores */
                si.vec_ok = (uint32_t)(con_vec && si.cab_dim && modelo_ok(si.modelo, g_c.modelo));
                if (con_vec && si.cab_dim && !si.vec_ok) motivo = 1;
                si.dim_vec = g_dim_servidor > 0 ? (uint32_t)g_dim_servidor : 0;
            }
            if (rc == -2) { fin = 1; break; }
        }
        kmutex_unlock(&g_mx);
    }
    kmutex_lock(&g_mx);
    uint32_t ce = si.celdas, ep = si.epis, rp = si.repetidas, ml = si.malas, sv = si.sin_vec, nl = si.nodos_llenos, ll = si.lleno, nd = si.nodos_desc;
    char modelo[40]; for (uint32_t i = 0; i < sizeof modelo; i++) modelo[i] = si.modelo[i];
    mc_imp_fin(&g_m, &si);
    uint32_t nodos = mc_recuenta(&g_m), total = g_m.n;
    kmutex_unlock(&g_mx);
    kmutex_unlock(&g_sm);
    kfree(b);
    g_guardar = 0; memoria_guarda_todo();
    op(out, max, p, "Importado de "); op(out, max, p, ruta); op(out, max, p, ": "); opd(out, max, p, ce); op(out, max, p, " hechos y "); opd(out, max, p, ep); op(out, max, p, " mensajes literales");
    if (rp) { op(out, max, p, " ("); opd(out, max, p, rp); op(out, max, p, " ya estaban: no se repiten)"); }
    op(out, max, p, ".\n  arbol: "); opd(out, max, p, nodos); op(out, max, p, " nodo(s) ("); opd(out, max, p, nd); op(out, max, p, " con la descripcion de memorIA)");
    if (nl) { op(out, max, p, "; "); opd(out, max, p, nl); op(out, max, p, " hecho(s) sin sitio en el arbol (el ciclo de sueno los ordena)"); }
    op(out, max, p, "\n  vectores: ");
    if (motivo) { op(out, max, p, "omitidos: el archivo es de "); op(out, max, p, modelo); op(out, max, p, " y SOMA usa "); op(out, max, p, g_c.modelo); op(out, max, p, " (se calcularan con el servidor de embeddings)"); }
    else if (!con_vec) op(out, max, p, "omitidos (sin-vectores)");
    else { op(out, max, p, "los del archivo"); if (sv) { op(out, max, p, "; "); opd(out, max, p, sv); op(out, max, p, " sin vector valido (se calcularan con el servidor)"); } }
    if (ml) { op(out, max, p, "\n  lineas ilegibles: "); opd(out, max, p, ml); }
    if (ll) op(out, max, p, "\n  AVISO: la memoria se lleno (tope de 40960 elementos o falta de RAM); el resto del archivo no entro");
    op(out, max, p, "\n  total en la memoria: "); opd(out, max, p, total); op(out, max, p, " elementos. Guardado.");
}

/* ======================================================================================================================
 * SOMA_283_MEMORIA_APP: cartuchos de memoria (como memorIA) y lo que necesita la aplicacion de Memoria
 *   - Un cartucho es una memoria completa (sus fragmentos y su arbol). El PRIMARIO es en el que se aprende y se escribe; hasta NMONT mas se
 *     pueden CONECTAR en solo lectura: el agente los consulta a la vez. El principal sigue en /memoria/; los demas en /memoria/k_ID/.
 *   - Lo que cambia los ids (borrar de verdad, cambiar de primario, vaciar) se hace con el ciclo de sueno, el embebido y el guardado parados.
 * ==================================================================================================================== */
#define REGISTRO "/memoria/cartuchos.txt"
static const char *sig(const char *a, char *tok, uint32_t cap);
static void scopy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void pref_de(int i, char *o)
{
    if (i < 0 || eq(g_cart[i].id, "principal")) { scopy(o, "/memoria/", 40); return; }
    uint32_t p = 0; op(o, 40, &p, "/memoria/k_"); op(o, 40, &p, g_cart[i].id); op(o, 40, &p, "/");
}
static void campo(char **l, char *o, uint32_t cap) { uint32_t n = 0; while (**l && **l != '\t' && **l != '\n') { if (n + 1 < cap) o[n++] = **l; (*l)++; } o[n] = 0; if (**l == '\t') (*l)++; }
static uint32_t numero(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static void registro_carga(void)
{
    for (int i = 0; i < NCART; i++) { uint8_t *b = (uint8_t *)&g_cart[i]; for (uint32_t k = 0; k < sizeof g_cart[i]; k++) b[k] = 0; }
    char prim[20]; prim[0] = 0;
    uint32_t len = 0; char *b = sys_read_alloc(REGISTRO, &len);
    int n = 0;
    if (b) {
        for (char *l = b; *l && n < NCART;) {
            if (pre(l, "primario=")) { l += 9; campo(&l, prim, sizeof prim); }
            else if (*l != '\n' && *l != '#') {
                struct cart *c = &g_cart[n]; char t[16];
                campo(&l, c->id, sizeof c->id); campo(&l, c->nombre, sizeof c->nombre); campo(&l, c->desc, sizeof c->desc);
                campo(&l, t, sizeof t); c->montado = (uint8_t)(numero(t) != 0);
                campo(&l, t, sizeof t); c->celdas = numero(t); campo(&l, t, sizeof t); c->nodos = numero(t); campo(&l, t, sizeof t); c->kb = numero(t);
                if (c->id[0]) { c->usado = 1; if (!c->nombre[0]) scopy(c->nombre, c->id, sizeof c->nombre); n++; }
            }
            while (*l && *l != '\n') l++;
            if (*l) l++;
        }
        kfree(b);
    }
    int hay_pr = 0; for (int i = 0; i < n; i++) if (eq(g_cart[i].id, "principal")) hay_pr = 1;
    if (!hay_pr && n < NCART) { struct cart *c = &g_cart[n++]; scopy(c->id, "principal", sizeof c->id); scopy(c->nombre, "Principal", sizeof c->nombre); scopy(c->desc, "La memoria de SOMA de siempre.", sizeof c->desc); c->usado = 1; }
    g_prim = 0;
    for (int i = 0; i < NCART; i++) if (g_cart[i].usado && (prim[0] ? eq(g_cart[i].id, prim) : eq(g_cart[i].id, "principal"))) g_prim = i;
    g_cart[g_prim].montado = 0;
    pref_de(g_prim, g_pref);
}
static void registro_guarda(void)
{
    static char t[NCART * 220 + 64]; uint32_t p = 0;
    op(t, sizeof t, &p, "primario="); op(t, sizeof t, &p, g_cart[g_prim].id); op(t, sizeof t, &p, "\n");
    for (int i = 0; i < NCART; i++) {
        const struct cart *c = &g_cart[i]; if (!c->usado) continue;
        op(t, sizeof t, &p, c->id); op(t, sizeof t, &p, "\t"); op(t, sizeof t, &p, c->nombre); op(t, sizeof t, &p, "\t"); op(t, sizeof t, &p, c->desc); op(t, sizeof t, &p, "\t");
        opd(t, sizeof t, &p, c->montado); op(t, sizeof t, &p, "\t"); opd(t, sizeof t, &p, c->celdas); op(t, sizeof t, &p, "\t"); opd(t, sizeof t, &p, c->nodos); op(t, sizeof t, &p, "\t"); opd(t, sizeof t, &p, c->kb); op(t, sizeof t, &p, "\n");
    }
    sys_write(REGISTRO, t, p);
    g_ver++;
}
static void cuenta_mc(const struct mc *m, uint32_t *celdas, uint32_t *nodos, uint32_t *kb)
{
    uint32_t c = 0, n = 0;
    for (uint32_t i = 0; i < m->n; i++) if (m->it[i].kind == MC_CELDA && m->it[i].estado == MC_ACTIVO) c++;
    uint64_t by = 0;
    for (uint32_t i = 0; i < m->n; i++) by += (uint64_t)m->it[i].tlen + m->it[i].flen + m->it[i].dim + 40u;      /* lo que ocupan sus datos */
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado) n++;
    *celdas = c; *nodos = n; *kb = (uint32_t)((by + 1023) / 1024);
}
static int cart_busca(const char *id) { for (int i = 0; i < NCART; i++) if (g_cart[i].usado && (eq(g_cart[i].id, id) || eq(g_cart[i].nombre, id))) return i; return -1; }
static void excl_entra(void) { kmutex_lock(&g_sm); kmutex_lock(&g_ep_mx); kmutex_lock(&g_gm); }
static void excl_sale(void) { kmutex_unlock(&g_gm); kmutex_unlock(&g_ep_mx); kmutex_unlock(&g_sm); }
static void olvida_ids(void) { g_nvr = g_nvm = g_njuz = 0; g_cursor_red = 0; g_gen++; g_ver++; }     /* con g_mx tomado: los ids viejos ya no valen */
static void borra_archivos(const char *pref, uint32_t desde)
{
    for (uint32_t i = desde; i < MC_MAX_SHARDS; i++) { char nm[56]; nombre_frag_en(pref, i, nm); sys_delete(nm); }
    if (!desde) { char an[56]; nombre_arbol(pref, an); sys_delete(an); }
}
/* guarda una memoria entera que no es la primaria (conectada o cargada para la ocasion). 0 = bien */
static int guarda_en(const char *pref, struct mc *m)
{
    uint8_t *b = (uint8_t *)kmalloc(mc_frag_max_bytes()); if (!b) return -1;
    int r = 0;
    uint32_t nf = mc_nfrag(m);
    for (uint32_t i = 0; i < nf; i++) {
        int len = mc_frag_guarda(m, i, b, mc_frag_max_bytes());
        char nm[56]; nombre_frag_en(pref, i, nm);
        if (len <= 0 || sys_write(nm, (const char *)b, (uint32_t)len) < 0) r = -1; else mc_frag_limpio(m, i);
    }
    kfree(b);
    borra_archivos(pref, nf ? nf : 1);
    uint8_t *ab = (uint8_t *)kmalloc(mc_arbol_max_bytes());
    if (ab) { int len = mc_arbol_guarda(m, ab, mc_arbol_max_bytes()); char an[56]; nombre_arbol(pref, an); if (len <= 0 || sys_write(an, (const char *)ab, (uint32_t)len) < 0) r = -1; else m->arbol_sucio = 0; kfree(ab); }
    return r;
}
static struct mc *mc_nueva(void) { return (struct mc *)mc_alloc((uint32_t)sizeof(struct mc)); }
static void mc_suelta(struct mc *m) { if (m) { mc_destroy(m); mc_free(m); } }

/* ---- .smem: un generador y varios destinos (otra memoria, contar bytes, un bufer) */
typedef void (*smem_sink)(void *ctx, char *linea, uint32_t n);
/* escribe en el sink los elementos marcados en sel (sel = 0: todos), con los nodos que usan. Si src es la primaria, toma g_mx por linea. */
static uint32_t genera_smem(const struct mc *src, const uint8_t *sel, int con_vec, int con_episodios, smem_sink sk, void *ctx)
{
    int prim = src == &g_m;
    char *l = (char *)kmalloc(MC_SMEM_LINEA_MAX); if (!l) return 0;
    if (prim) kmutex_lock(&g_mx);
    uint32_t n = src->n, dim = 0;
    uint32_t *ord = (uint32_t *)mc_alloc(n * 4u + 4u);
    uint8_t usa[MC_NODOS_MAX + 1]; for (uint32_t i = 0; i <= MC_NODOS_MAX; i++) usa[i] = 0;
    uint32_t k = 0;
    if (ord) for (uint32_t i = 0; i < n; i++) {
        const struct mc_item *e = &src->it[i];
        if ((sel && !sel[i]) || (e->kind == MC_EPISODIO && !con_episodios)) continue;
        if (e->kind == MC_CELDA) { ord[i] = ++k; for (uint32_t x = e->nodo; x && x <= MC_NODOS_MAX && src->nodo[x].usado; x = src->nodo[x].padre) usa[x] = 1; }
        if (!dim && e->vec && e->dim) dim = e->dim;
    }
    if (prim) kmutex_unlock(&g_mx);
    if (!ord) { kfree(l); return 0; }
    uint32_t tot = 0, len = mc_smem_cabecera(con_vec && dim ? g_c.modelo : "ninguno", con_vec ? dim : 0, l, MC_SMEM_LINEA_MAX);
    sk(ctx, l, len);
    for (uint32_t x = 1; x <= MC_NODOS_MAX; x++) {
        if (!usa[x]) continue;
        if (prim) kmutex_lock(&g_mx);
        len = mc_smem_nodo(src, x, l, MC_SMEM_LINEA_MAX);
        if (prim) kmutex_unlock(&g_mx);
        if (len) sk(ctx, l, len);
    }
    for (uint32_t i = 0; i < n; i++) {
        if (prim) kmutex_lock(&g_mx);
        const struct mc_item *e = &src->it[i];
        len = 0;
        if (!(sel && !sel[i]) && !(e->kind == MC_EPISODIO && !con_episodios)) {
            uint32_t so = e->estado == MC_SUSTITUIDO && e->sust_por && e->sust_por <= n ? ord[e->sust_por - 1] : 0;
            len = mc_smem_item(src, i + 1, ord[i], so, con_vec, l, MC_SMEM_LINEA_MAX);
        }
        if (prim) kmutex_unlock(&g_mx);
        if (len) { sk(ctx, l, len); tot++; }
    }
    mc_free(ord); kfree(l);
    return tot;
}
struct sk_imp { struct mc *dst; struct mc_imp s; int primera, bloquea; };
static void sink_imp(void *c_, char *l, uint32_t n)
{
    struct sk_imp *c = (struct sk_imp *)c_; (void)n;
    if (c->bloquea) kmutex_lock(&g_mx);
    uint32_t vo = c->s.vec_ok;
    mc_imp_linea(c->dst, &c->s, l);
    if (c->primera) { c->primera = 0; c->s.vec_ok = vo; }
    if (c->bloquea) kmutex_unlock(&g_mx);
}
struct sk_buf { char *b; uint32_t n, cap; };
static void sink_buf(void *c_, char *l, uint32_t n) { struct sk_buf *c = (struct sk_buf *)c_; if (c->b && c->n + n <= c->cap) for (uint32_t i = 0; i < n; i++) c->b[c->n + i] = l[i]; c->n += n; }
/* copia lo marcado de src a dst con el importador (no duplica, conserva ramas y sustituciones). bloquea: dst la leen otros (g_mx) */
static int copia_mc(const struct mc *src, const uint8_t *sel, struct mc *dst, int bloquea, uint32_t *nuevos, uint32_t *repes)
{
    struct sk_imp c; uint8_t *z = (uint8_t *)&c; for (uint32_t i = 0; i < sizeof c; i++) z[i] = 0;
    c.dst = dst; c.bloquea = bloquea; c.primera = 1;
    uint32_t cap = src->n + 2;
    if (bloquea) kmutex_lock(&g_mx);
    c.s.ahora = ahora(); int r = mc_imp_ini(dst, &c.s, cap); c.s.vec_ok = 1; c.s.tick = dst->tick;
    if (bloquea) kmutex_unlock(&g_mx);
    if (r) return -1;
    genera_smem(src, sel, 1, 1, sink_imp, &c);
    if (bloquea) kmutex_lock(&g_mx);
    *nuevos = c.s.celdas + c.s.epis; *repes = c.s.repetidas;
    int lleno = (int)c.s.lleno;
    mc_imp_fin(dst, &c.s);
    if (bloquea) kmutex_unlock(&g_mx);
    return lleno ? 1 : 0;
}

/* ---- operaciones con cartuchos (las llaman la orden y el hilo de peticiones; nada tomado al entrar). Escriben el resultado en m. */
#define MSG(t) do { uint32_t p_ = 0; op(msg, cap, &p_, t); } while (0)
static void cart_refresca(int i)                   /* con g_mx tomado: cuentas de un cartucho cargado */
{
    if (i == g_prim) cuenta_mc(&g_m, &g_cart[i].celdas, &g_cart[i].nodos, &g_cart[i].kb);
    else if (g_mont[i]) cuenta_mc(g_mont[i], &g_cart[i].celdas, &g_cart[i].nodos, &g_cart[i].kb);
}
static int cart_nuevo(const char *nombre, const char *desc, char *msg, uint32_t cap)
{
    int lib = -1; for (int i = 0; i < NCART; i++) if (!g_cart[i].usado) { lib = i; break; }
    if (lib < 0) { MSG("No caben mas cartuchos (8). Borra alguno antes."); return -1; }
    char id[20]; uint32_t n = 0;
    for (const char *c = nombre; *c && n < 16; c++) { char x = *c; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if ((x >= 'a' && x <= 'z') || (x >= '0' && x <= '9')) id[n++] = x; else if (n && id[n - 1] != '_') id[n++] = '_'; }
    while (n && id[n - 1] == '_') n--;
    if (!n) { id[n++] = 'c'; } id[n] = 0;
    for (int k = 2; cart_busca(id) >= 0 && k < 99; k++) { id[n] = (char)('0' + k / 10); id[n + 1] = (char)('0' + k % 10); id[n + 2] = 0; }
    if (cart_busca(id) >= 0) { MSG("No se pudo dar un nombre unico al cartucho."); return -1; }
    struct cart *c = &g_cart[lib];
    uint8_t *z = (uint8_t *)c; for (uint32_t k = 0; k < sizeof *c; k++) z[k] = 0;
    scopy(c->id, id, sizeof c->id); scopy(c->nombre, nombre && *nombre ? nombre : id, sizeof c->nombre); scopy(c->desc, desc ? desc : "", sizeof c->desc); c->usado = 1;
    char pf[40]; pref_de(lib, pf); borra_archivos(pf, 0);                      /* por si quedaron restos de uno anterior con el mismo id */
    registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, "Cartucho creado: "); op(msg, cap, &p, c->nombre); op(msg, cap, &p, " ("); op(msg, cap, &p, id); op(msg, cap, &p, ")");
    return lib;
}
static int cart_montar(int i, int on, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    if (i == g_prim) { MSG("El primario ya esta en uso (lectura y escritura)."); return -1; }
    if (on) {
        if (g_mont[i]) { MSG("Ya estaba conectado."); return 0; }
        int nm = 0; for (int k = 0; k < NCART; k++) if (g_mont[k]) nm++;
        if (nm >= NMONT) { MSG("Ya hay 3 cartuchos conectados: desconecta alguno."); return -1; }
        struct mc *m = mc_nueva(); char pf[40]; pref_de(i, pf);
        if (!m || carga_en(pf, m) != 0) { mc_suelta(m); MSG("Sin memoria para cargar el cartucho."); return -1; }
        kmutex_lock(&g_mx); g_mont[i] = m; g_cart[i].montado = 1; cart_refresca(i); g_ver++; kmutex_unlock(&g_mx);
        registro_guarda();
        uint32_t p = 0; op(msg, cap, &p, "Conectado (solo lectura): "); op(msg, cap, &p, g_cart[i].nombre); op(msg, cap, &p, ". El agente lo consulta junto al primario.");
        return 0;
    }
    kmutex_lock(&g_mx); struct mc *m = g_mont[i]; g_mont[i] = 0; g_cart[i].montado = 0; g_ver++; kmutex_unlock(&g_mx);
    mc_suelta(m); registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, "Desconectado: "); op(msg, cap, &p, g_cart[i].nombre);
    return 0;
}
static int cart_primario(int i, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    if (i == g_prim) { MSG("Ya es el primario."); return 0; }
    excl_entra();
    guarda_sucios_(); guarda_arbol_();
    struct mc *nm = g_mont[i];
    char pf[40]; pref_de(i, pf);
    if (!nm) { nm = mc_nueva(); if (!nm || carga_en(pf, nm) != 0) { mc_suelta(nm); excl_sale(); MSG("Sin memoria para cargar el cartucho."); return -1; } }
    struct mc *viejo = mc_nueva();
    if (!viejo) { if (!g_mont[i]) mc_suelta(nm); excl_sale(); MSG("Sin memoria."); return -1; }
    kmutex_lock(&g_mx);
    int ant = g_prim;
    cuenta_mc(&g_m, &g_cart[ant].celdas, &g_cart[ant].nodos, &g_cart[ant].kb);
    *viejo = g_m; g_m = *nm; mc_free(nm);
    g_mont[i] = 0; g_cart[i].montado = 0;
    g_prim = i; scopy(g_pref, pf, sizeof g_pref);
    olvida_ids();
    kmutex_unlock(&g_mx);
    mc_suelta(viejo);
    excl_sale();
    registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, "Primario: "); op(msg, cap, &p, g_cart[i].nombre); op(msg, cap, &p, ". Lo nuevo se aprende aqui; "); op(msg, cap, &p, g_cart[ant].nombre); op(msg, cap, &p, " queda desconectado.");
    return 0;
}
static int cart_vaciar(int i, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    char pf[40]; pref_de(i, pf);
    if (i == g_prim) {
        excl_entra();
        struct mc *nuevo = mc_nueva();
        if (!nuevo || mc_init(nuevo, MC_MAX_ITEMS) != 0) { mc_suelta(nuevo); excl_sale(); MSG("Sin memoria."); return -1; }
        struct mc *viejo = mc_nueva();
        if (!viejo) { mc_suelta(nuevo); excl_sale(); MSG("Sin memoria."); return -1; }
        kmutex_lock(&g_mx); *viejo = g_m; g_m = *nuevo; mc_free(nuevo); g_qn = 0; olvida_ids(); cart_refresca(i); kmutex_unlock(&g_mx);
        mc_suelta(viejo);
        borra_archivos(pf, 0);
        excl_sale();
    } else {
        kmutex_lock(&g_mx); struct mc *m = g_mont[i];
        if (m) { struct mc *e = mc_nueva(); if (e && mc_init(e, MC_MAX_ITEMS) == 0) { g_mont[i] = e; cart_refresca(i); } else { mc_suelta(e); m = 0; } }
        kmutex_unlock(&g_mx);
        mc_suelta(m);
        borra_archivos(pf, 0);
        g_cart[i].celdas = g_cart[i].nodos = g_cart[i].kb = 0;
    }
    registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, "Vaciado: "); op(msg, cap, &p, g_cart[i].nombre); op(msg, cap, &p, " (sin hechos, mensajes ni ramas).");
    return 0;
}
static int cart_borrar(int i, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    if (i == g_prim) { MSG("No se puede borrar el primario: haz primario a otro antes."); return -1; }
    if (eq(g_cart[i].id, "principal")) { MSG("El cartucho Principal no se borra (se puede vaciar)."); return -1; }
    if (g_mont[i]) { char t[120]; cart_montar(i, 0, t, sizeof t); }
    char pf[40]; pref_de(i, pf); borra_archivos(pf, 0);
    char nom[40]; scopy(nom, g_cart[i].nombre, sizeof nom);
    uint8_t *z = (uint8_t *)&g_cart[i]; for (uint32_t k = 0; k < sizeof g_cart[i]; k++) z[k] = 0;
    registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, "Cartucho borrado: "); op(msg, cap, &p, nom);
    return 0;
}
/* carga (o toma, si esta conectado o es el primario) la memoria de un cartucho. *tmp = 1 si hay que soltarla al acabar */
static struct mc *cart_mc(int i, int *tmp)
{
    *tmp = 0;
    if (i == g_prim) return &g_m;
    if (g_mont[i]) return g_mont[i];
    struct mc *m = mc_nueva(); char pf[40]; pref_de(i, pf);
    if (!m || carga_en(pf, m) != 0) { mc_suelta(m); return 0; }
    *tmp = 1; return m;
}
static int cart_exporta(int i, const char *ruta, int con_vec, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    int tmp; if (i == g_prim) excl_entra();
    struct mc *m = cart_mc(i, &tmp);
    if (!m) { if (i == g_prim) excl_sale(); MSG("Sin memoria para cargar el cartucho."); return -1; }
    struct sk_buf c; c.b = 0; c.n = 0; c.cap = 0;
    genera_smem(m, 0, con_vec, 1, sink_buf, &c);                             /* primera vuelta: cuanto ocupa */
    uint32_t total = c.n; int r = -1;
    if (total > FS_FILE_MAX) { uint32_t p = 0; op(msg, cap, &p, "Demasiado grande para un archivo ("); opd(msg, cap, &p, total >> 20); op(msg, cap, &p, " MiB): exporta sin vectores."); }
    else if ((c.b = (char *)kmalloc(total + 1)) == 0) MSG("Sin memoria para exportar.");
    else {
        c.n = 0; c.cap = total;
        uint32_t n = genera_smem(m, 0, con_vec, 1, sink_buf, &c);
        if (sys_write(ruta, c.b, c.n) < 0) MSG("No se pudo escribir el archivo.");
        else { uint32_t p = 0; op(msg, cap, &p, "Exportado "); op(msg, cap, &p, g_cart[i].nombre); op(msg, cap, &p, " a "); op(msg, cap, &p, ruta); op(msg, cap, &p, ": "); opd(msg, cap, &p, n); op(msg, cap, &p, " elementos, "); opd(msg, cap, &p, (c.n + 1023) / 1024); op(msg, cap, &p, con_vec ? " KB (con vectores)." : " KB (sin vectores)."); r = 0; }
        kfree(c.b);
    }
    if (tmp) mc_suelta(m);
    if (i == g_prim) excl_sale();
    return r;
}
/* copia (o mueve) a otro cartucho lo marcado del primario. sel: marca por indice (n = g_m.n al marcar) */
static int copia_a_cart(uint8_t *sel, int dest, int mover, char *msg, uint32_t cap)
{
    if (dest < 0 || !g_cart[dest].usado || dest == g_prim) { MSG("Elige un cartucho distinto del primario."); return -1; }
    excl_entra();
    int tmp; struct mc *dst = cart_mc(dest, &tmp);
    if (!dst) { excl_sale(); MSG("Sin memoria para cargar el cartucho de destino."); return -1; }
    uint32_t nuevos = 0, rep = 0;
    int r = copia_mc(&g_m, sel, dst, !tmp, &nuevos, &rep);
    if (r < 0) { if (tmp) mc_suelta(dst); excl_sale(); MSG("Sin memoria para copiar."); return -1; }
    char pf[40]; pref_de(dest, pf);
    if (!tmp) kmutex_lock(&g_mx);
    cuenta_mc(dst, &g_cart[dest].celdas, &g_cart[dest].nodos, &g_cart[dest].kb);
    if (!tmp) kmutex_unlock(&g_mx);
    guarda_en(pf, dst);
    if (tmp) mc_suelta(dst);
    uint32_t borrados = 0;
    if (mover && r == 0) {
        uint32_t nf0 = mc_nfrag(&g_m);
        kmutex_lock(&g_mx); int b = mc_compacta(&g_m, sel); if (b > 0) { borrados = (uint32_t)b; olvida_ids(); } kmutex_unlock(&g_mx);
        if (borrados) { guarda_sucios_(); guarda_arbol_(); kmutex_lock(&g_mx); uint32_t nf = mc_nfrag(&g_m); kmutex_unlock(&g_mx); if (nf < nf0) borra_archivos(g_pref, nf ? nf : 1); }
    }
    excl_sale();
    registro_guarda();
    uint32_t p = 0; op(msg, cap, &p, mover ? "Movido a " : "Copiado a "); op(msg, cap, &p, g_cart[dest].nombre); op(msg, cap, &p, ": "); opd(msg, cap, &p, nuevos); op(msg, cap, &p, " nuevo(s)");
    if (rep) { op(msg, cap, &p, ", "); opd(msg, cap, &p, rep); op(msg, cap, &p, " ya estaban"); }
    if (r == 1) op(msg, cap, &p, ". AVISO: el destino se lleno");
    if (mover) { op(msg, cap, &p, "; "); opd(msg, cap, &p, borrados); op(msg, cap, &p, " quitado(s) del primario"); }
    op(msg, cap, &p, ".");
    return 0;
}
/* SOMA_337_NOTICIAS: guarda un texto en el cartucho llamado "nombre" (lo crea si no existe) sin cambiar el primario. Lo usa la
 * aplicacion de Noticias ("Guardar en memoria"). Si ya estaba, se refuerza. 0 = bien. */
int memoria_guarda_en_cartucho(const char *nombre, const char *desc, const char *texto, const char *fuente, char *msg, uint32_t cap)
{
    uint32_t p = 0; if (cap) msg[0] = 0;
    if (!lista()) { op(msg, cap, &p, "La memoria no esta activa (memoria config activa 1)."); return -1; }
    if (!nombre || !*nombre || !texto || !*texto) { op(msg, cap, &p, "Nada que guardar."); return -1; }
    int8_t v[MC_DIM_MAX]; uint32_t d = 0;
    int t = emb_texto(texto, v, &d);                              /* antes de tomar nada: puede tardar */
    excl_entra();
    int i = cart_busca(nombre);
    if (i < 0) { char t2[120]; i = cart_nuevo(nombre, desc, t2, sizeof t2); if (i < 0) { excl_sale(); op(msg, cap, &p, t2); return -1; } }
    if (i == g_prim) {                                            /* el primario: como cualquier recuerdo */
        excl_sale();
        uint32_t id = guarda_celda(texto, fuente, 6);
        if (!id) { op(msg, cap, &p, "La memoria esta llena."); return -1; }
        op(msg, cap, &p, "Guardado (#"); opd(msg, cap, &p, id); op(msg, cap, &p, ")."); return 0;
    }
    int tmp; struct mc *m = cart_mc(i, &tmp);
    if (!m) { excl_sale(); op(msg, cap, &p, "Sin memoria para cargar el cartucho."); return -1; }
    if (!tmp) kmutex_lock(&g_mx);                                 /* un cartucho conectado lo lee el agente */
    uint32_t ex = mc_find_exacto(m, MC_CELDA, texto), id = ex;
    if (ex) mc_refuerza(m, ex, m->tick);
    else id = mc_add(m, MC_CELDA, MC_ROL_USUARIO, texto, fuente, 6, ahora(), m->tick, t ? v : 0, t ? d : 0);
    cuenta_mc(m, &g_cart[i].celdas, &g_cart[i].nodos, &g_cart[i].kb);
    if (!tmp) kmutex_unlock(&g_mx);
    char pf[40]; pref_de(i, pf); guarda_en(pf, m);
    if (tmp) mc_suelta(m);
    excl_sale();
    registro_guarda();
    g_ver++;
    if (!id) { op(msg, cap, &p, "El cartucho esta lleno."); return -1; }
    op(msg, cap, &p, "Guardado en "); op(msg, cap, &p, g_cart[i].nombre); op(msg, cap, &p, " (#"); opd(msg, cap, &p, id); op(msg, cap, &p, ").");
    return 0;
}
static int cart_clona(int i, const char *nombre, char *msg, uint32_t cap)
{
    if (i < 0 || !g_cart[i].usado) { MSG("No existe ese cartucho."); return -1; }
    char t[120]; int j = cart_nuevo(nombre, g_cart[i].desc, t, sizeof t);
    if (j < 0) { MSG(t); return -1; }
    if (i == g_prim) excl_entra();
    int tmp; struct mc *src = cart_mc(i, &tmp);
    struct mc *dst = mc_nueva();
    int ok = src && dst && mc_init(dst, MC_MAX_ITEMS) == 0;
    uint32_t nuevos = 0, rep = 0;
    if (ok) {
        if (src != &g_m && !tmp) kmutex_lock(&g_mx);                         /* un conectado lo lee el agente: que no cambie mientras */
        ok = copia_mc(src, 0, dst, 0, &nuevos, &rep) >= 0;
        if (src != &g_m && !tmp) kmutex_unlock(&g_mx);
    }
    if (ok) { dst->tick = src->tick; char pf[40]; pref_de(j, pf); guarda_en(pf, dst); cuenta_mc(dst, &g_cart[j].celdas, &g_cart[j].nodos, &g_cart[j].kb); }
    if (tmp) mc_suelta(src);
    mc_suelta(dst);
    if (i == g_prim) excl_sale();
    registro_guarda();
    if (!ok) { MSG("Sin memoria para clonar."); return -1; }
    uint32_t p = 0; op(msg, cap, &p, "Clonado en "); op(msg, cap, &p, g_cart[j].nombre); op(msg, cap, &p, ": "); opd(msg, cap, &p, nuevos); op(msg, cap, &p, " elementos.");
    return j;
}
/* borra de verdad lo marcado en el primario. sel se libera aqui */
static int borra_marcados(uint8_t *sel, char *msg, uint32_t cap)
{
    excl_entra();
    uint32_t nf0 = mc_nfrag(&g_m);
    kmutex_lock(&g_mx); int b = mc_compacta(&g_m, sel); if (b > 0) olvida_ids(); cart_refresca(g_prim); kmutex_unlock(&g_mx);
    if (b > 0) { guarda_sucios_(); guarda_arbol_(); kmutex_lock(&g_mx); uint32_t nf = mc_nfrag(&g_m); kmutex_unlock(&g_mx); if (nf < nf0) borra_archivos(g_pref, nf ? nf : 1); }
    excl_sale();
    if (b < 0) { MSG("Sin memoria para reordenar: no se ha borrado nada."); return -1; }
    uint32_t p = 0; op(msg, cap, &p, "Borrado(s) para siempre: "); opd(msg, cap, &p, (uint32_t)b); op(msg, cap, &p, ".");
    return b;
}

/* ---- peticiones de la aplicacion: lo que tarda (LLM, servidor, disco) va a un hilo propio para no congelar la pantalla */
static struct mem_pet g_pet;
static volatile int g_pet_hay, g_ops_ocupado, g_ops_hilo;
static uint32_t g_ops_n;
static char g_ops_msg[200];
static struct mem_res g_res[24];
static int g_nres; static uint32_t g_res_ver;
static void ops_msg(const char *t) { kmutex_lock(&g_mx); scopy(g_ops_msg, t, sizeof g_ops_msg); g_ver++; kmutex_unlock(&g_mx); }
static uint8_t *marca_ids(const uint32_t *ids, uint32_t n)                 /* con g_mx tomado */
{
    uint8_t *sel = (uint8_t *)mc_alloc(g_m.n + 1);
    if (sel) for (uint32_t i = 0; i < n; i++) if (ids[i] >= 1 && ids[i] <= g_m.n) sel[ids[i] - 1] = 1;
    return sel;
}
static void busca_gui(const char *q)
{
    int8_t v[MC_DIM_MAX]; uint32_t qd = 0;
    int tiene = emb_texto(q, v, &qd);
    kmutex_lock(&g_mx);
    restaura_busqueda(tiene ? v : 0, tiene ? qd : 0);
    uint32_t ids[16]; int32_t sc[16]; int n = 0;
    g_nres = 0;
    for (int c = -1; c < NCART; c++) {
        const struct mc *m = c < 0 ? &g_m : g_mont[c];
        if (!m) continue;
        n = mc_busca(m, tiene ? v : 0, tiene ? qd : 0, q, (1u << MC_CELDA) | (1u << MC_EPISODIO), 1, 6129, c < 0 ? 16 : 6, ids, sc);
        for (int i = 0; i < n && g_nres < 24; i++) {
            const struct mc_item *e = mc_get(m, ids[i]); if (!e) continue;
            struct mem_res *r = &g_res[g_nres++];
            r->cart = c < 0 ? g_prim : c; r->primario = c < 0; r->id = e->id; r->score = sc[i]; r->kind = e->kind; r->estado = e->estado; r->imp = e->imp; r->rol = e->rol; r->t = e->t;
            scopy(r->texto, e->texto, sizeof r->texto); scopy(r->cart_nombre, g_cart[r->cart].nombre, sizeof r->cart_nombre);
        }
    }
    g_res_ver++; g_ver++;
    kmutex_unlock(&g_mx);
    char t[120]; uint32_t p = 0; opd(t, sizeof t, &p, (uint32_t)g_nres); op(t, sizeof t, &p, tiene ? " resultado(s) por significado (embeddings)." : " resultado(s) por palabras (el servidor de embeddings no contesta).");
    ops_msg(t);
}
static void ops_ejecuta(struct mem_pet *pt)
{
    static char m[200]; m[0] = 0;
    switch (pt->op) {
    case MEMP_SUENO: {
        uint32_t n = pt->a ? pt->a : 1; if (n > 9) n = 9;
        for (uint32_t i = 0; i < n; i++) {
            char t[60]; uint32_t p = 0; op(t, sizeof t, &p, "Durmiendo: paso "); opd(t, sizeof t, &p, i + 1); op(t, sizeof t, &p, " de "); opd(t, sizeof t, &p, n); op(t, sizeof t, &p, " ...");
            ops_msg(t);
            char rep[150]; sueno_paso(rep, sizeof rep);
        }
        g_guardar = 1;
        kmutex_lock(&g_mx); scopy(m, g_ninforme ? g_informe[g_ninforme - 1] : "Hecho.", sizeof m); kmutex_unlock(&g_mx);
        break; }
    case MEMP_SRV_ON: ops_msg("Arrancando bge-m3 en el servidor (puede tardar un minuto) ..."); memoria_cmd("servidor arrancar", m, sizeof m); break;
    case MEMP_SRV_OFF: memoria_cmd("servidor parar", m, sizeof m); break;
    case MEMP_PROBAR: ops_msg("Probando el servidor de embeddings ..."); memoria_cmd("probar", m, sizeof m); break;
    case MEMP_GUARDAR: g_guardar = 0; memoria_guarda_todo(); scopy(m, "Memoria guardada en disco.", sizeof m); break;
    case MEMP_BUSCAR: ops_msg("Buscando ..."); busca_gui(pt->s1); return;
    case MEMP_BORRA: case MEMP_BORRA_RAMA: case MEMP_COPIA: case MEMP_MUEVE: {
        kmutex_lock(&g_mx);
        uint8_t *sel = 0;
        if (pt->op == MEMP_BORRA_RAMA || ((pt->op == MEMP_COPIA || pt->op == MEMP_MUEVE) && pt->b)) {
            sel = (uint8_t *)mc_alloc(g_m.n + 1);
            if (sel && pt->op == MEMP_BORRA_RAMA) mc_nodo_quita(&g_m, pt->a, sel);
            else if (sel) for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && g_m.it[i].nodo && mc_nodo_dentro(&g_m, g_m.it[i].nodo, pt->b)) sel[i] = 1;
        } else sel = marca_ids(pt->ids, pt->nids);
        uint32_t gen = g_gen;
        kmutex_unlock(&g_mx);
        if (!sel) { scopy(m, "Sin memoria.", sizeof m); break; }
        if (gen != pt->gen) { scopy(m, "La memoria cambio mientras tanto (otro borrado o cambio de cartucho): vuelve a elegir.", sizeof m); mc_free(sel); break; }
        if (pt->op == MEMP_COPIA || pt->op == MEMP_MUEVE) { if (pt->op == MEMP_MUEVE) ops_msg("Moviendo ..."); copia_a_cart(sel, (int)pt->a, pt->op == MEMP_MUEVE, m, sizeof m); }
        else borra_marcados(sel, m, sizeof m);
        mc_free(sel);
        kmutex_lock(&g_mx); g_nres = 0; g_res_ver++; kmutex_unlock(&g_mx);
        break; }
    case MEMP_C_NUEVO: cart_nuevo(pt->s1, pt->s2, m, sizeof m); break;
    case MEMP_C_PRIM: ops_msg("Cambiando de cartucho primario ..."); cart_primario((int)pt->a, m, sizeof m); kmutex_lock(&g_mx); g_nres = 0; g_res_ver++; kmutex_unlock(&g_mx); break;
    case MEMP_C_MONTA: cart_montar((int)pt->a, 1, m, sizeof m); break;
    case MEMP_C_DESMONTA: cart_montar((int)pt->a, 0, m, sizeof m); break;
    case MEMP_C_VACIA: cart_vaciar((int)pt->a, m, sizeof m); kmutex_lock(&g_mx); g_nres = 0; g_res_ver++; kmutex_unlock(&g_mx); break;
    case MEMP_C_BORRA: cart_borrar((int)pt->a, m, sizeof m); break;
    case MEMP_C_EXPORTA: ops_msg("Exportando ..."); cart_exporta((int)pt->a, pt->s1, (int)pt->b, m, sizeof m); break;
    case MEMP_C_CLONA: ops_msg("Clonando ..."); cart_clona((int)pt->a, pt->s1, m, sizeof m); break;
    case MEMP_IMPORTA: { ops_msg("Importando ..."); uint32_t p = 0; importa_smem(pt->s1, (int)pt->b, m, sizeof m, &p); break; }
    default: scopy(m, "Peticion desconocida.", sizeof m); break;
    }
    for (uint32_t i = 0; m[i]; i++) if (m[i] == '\n') m[i] = ' ';
    ops_msg(m);
}
static void ops_hilo(void *arg)
{
    (void)arg;
    for (;;) {
        thread_sleep(40);
        if (!g_pet_hay) continue;
        struct mem_pet pt = g_pet;
        g_pet_hay = 0;
        ops_ejecuta(&pt);
        g_ops_n++; g_ops_ocupado = 0; g_ver++;
    }
}
static void ops_arranca(void) { if (!g_ops_hilo) { g_ops_hilo = 1; thread_create("memoria-app", ops_hilo, 0); } }

/* ---- API de la aplicacion (memoria.h) */
int memoria_gui_pide(const struct mem_pet *p)
{
    if (!g_listo) inicia();
    if (g_ops_ocupado || g_pet_hay) return 0;
    g_ops_ocupado = 1; g_pet = *p; g_pet.gen = p->gen; g_pet_hay = 1; g_ver++;
    return 1;
}
uint32_t memoria_gui_gen(void) { return g_gen; }
uint32_t memoria_version(void)
{
    return g_ver * 2654435761u ^ g_m.n * 40503u ^ g_m.pasos * 977u ^ g_qn * 131u ^ (uint32_t)g_dormido * 7u ^ (uint32_t)g_extrayendo * 11u ^ g_ninforme * 13u ^
           (uint32_t)g_ops_ocupado * 17u ^ g_ops_n * 19u ^ g_n_hechos * 23u ^ g_emb_ok * 29u ^ g_emb_mal * 31u ^ (uint32_t)g_agentes * 37u ^ g_n_dup * 41u;
}
struct mc *memoria_gui_entra(void) { if (!g_listo) inicia(); if (!g_listo) return 0; kmutex_lock(&g_mx); return &g_m; }
void memoria_gui_sale(int cambiado) { if (cambiado) { g_guardar = 1; g_ver++; mc_recuenta(&g_m); cart_refresca(g_prim); } kmutex_unlock(&g_mx); }
uint32_t memoria_gui_ahora(void) { return ahora(); }
void memoria_vista(struct mem_vista *v)
{
    uint8_t *z = (uint8_t *)v; for (uint32_t i = 0; i < sizeof *v; i++) z[i] = 0;
    if (!g_listo) inicia();
    v->activa = g_c.activa; v->ingesta = g_c.ingesta; v->sueno = g_c.sueno; v->sueno_s = g_c.sueno_s; v->n_hechos = g_c.n_hechos; v->n_epis = g_c.n_epis;
    v->srv = g_c.srv; v->srv_dim = g_dim_servidor; v->emb_ok = g_emb_ok; v->emb_mal = g_emb_mal;
    uint64_t now = timer_get_uptime_ms();
    v->emb_caido = g_emb_cae_hasta && now < g_emb_cae_hasta;
    scopy(v->url, g_c.local ? "motor local de SOMA" : g_c.url, sizeof v->url); scopy(v->modelo, g_c.modelo, sizeof v->modelo);   /* SOMA_329 */
    kmutex_lock(&g_mx);
    mc_stats(&g_m, &v->celdas, &v->episodios, &v->archivadas, &v->sustituidas, &v->sin_vec);
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (g_m.nodo[i].usado) v->nodos++;
    for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && g_m.it[i].estado == MC_ACTIVO && !g_m.it[i].nodo) v->sin_ordenar++;
    v->total = g_m.n; v->cap = g_m.cap; v->nfrag = mc_nfrag(&g_m); v->bytes = mc_bytes(&g_m);
    v->tick = g_m.tick; v->pasos = g_m.pasos; v->qn = g_qn;
    v->ninf = g_ninforme; for (uint32_t i = 0; i < g_ninforme && i < 10; i++) scopy(v->inf[i], g_informe[i], sizeof v->inf[i]);
    scopy(v->ops_msg, g_ops_msg, sizeof v->ops_msg);
    scopy(v->cartucho, g_cart[g_prim].nombre, sizeof v->cartucho);
    for (int c = 0; c < NCART; c++) if (g_mont[c]) v->nmont++;
    kmutex_unlock(&g_mx);
    v->n_rest = g_n_rest; v->n_olv = g_n_olv; v->n_fus = g_n_fus; v->n_nuevos = g_n_hechos; v->n_dup = g_n_dup; v->n_sust = g_n_sust; v->n_fallo = g_n_fallo;
    v->agentes = g_agentes; v->dormido = g_dormido; v->extrayendo = g_extrayendo;
    v->tarea = g_dormido ? (int)((v->pasos + NTAREAS - 1) % NTAREAS) : (int)(v->pasos % NTAREAS);
    uint32_t calma = (uint32_t)((now - g_libre_desde) / 1000u);
    v->seg_calma = g_agentes ? 0 : calma;
    uint32_t desde = g_ult_sueno ? (uint32_t)((now - g_ult_sueno) / 1000u) : 100000u;
    uint32_t falta = desde >= (uint32_t)g_c.sueno_s ? 0 : (uint32_t)g_c.sueno_s - desde;
    if (!g_agentes && calma < 30 && falta < 30 - calma) falta = 30 - calma;
    v->seg_proximo = falta;
    v->ops_ocupado = g_ops_ocupado; v->ops_n = g_ops_n;
}
int memoria_gui_cartuchos(struct mem_cart *o, int max)
{
    if (!g_listo) inicia();
    int n = 0;
    kmutex_lock(&g_mx);
    for (int i = 0; i < NCART && n < max; i++) {
        if (!g_cart[i].usado) continue;
        cart_refresca(i);
        struct mem_cart *c = &o[n++];
        c->idx = i; scopy(c->id, g_cart[i].id, sizeof c->id); scopy(c->nombre, g_cart[i].nombre, sizeof c->nombre); scopy(c->desc, g_cart[i].desc, sizeof c->desc);
        c->primario = i == g_prim; c->montado = g_mont[i] != 0; c->celdas = g_cart[i].celdas; c->nodos = g_cart[i].nodos; c->kb = g_cart[i].kb;
    }
    kmutex_unlock(&g_mx);
    return n;
}
int memoria_gui_resultados(struct mem_res *o, int max, uint32_t *ver)
{
    kmutex_lock(&g_mx);
    int n = g_nres < max ? g_nres : max;
    for (int i = 0; i < n; i++) o[i] = g_res[i];
    if (ver) *ver = g_res_ver;
    kmutex_unlock(&g_mx);
    return n;
}
int memoria_gui_config(const char *clave, int valor, const char *texto)
{
    char a[160], o[160]; uint32_t p = 0;
    op(a, sizeof a, &p, "config "); op(a, sizeof a, &p, clave); op(a, sizeof a, &p, " ");
    if (texto) op(a, sizeof a, &p, texto); else opd(a, sizeof a, &p, (uint32_t)(valor < 0 ? 0 : valor));
    memoria_cmd(a, o, sizeof o);
    g_ver++;
    return 0;
}

/* ---- la orden `memoria cartucho ...` y `memoria borrar ID` */
static int cmd_cartucho(const char *args, char *out, uint32_t max)
{
    char acc[16], a1[48]; uint32_t p = 0;
    args = sig(args, acc, sizeof acc);
    if (!acc[0] || eq(acc, "lista")) {
        struct mem_cart c[NCART]; int n = memoria_gui_cartuchos(c, NCART);
        for (int i = 0; i < n; i++) {
            op(out, max, &p, c[i].primario ? "* " : c[i].montado ? "+ " : "  "); op(out, max, &p, c[i].nombre); op(out, max, &p, " ("); op(out, max, &p, c[i].id); op(out, max, &p, ") ");
            op(out, max, &p, c[i].primario ? "PRIMARIO" : c[i].montado ? "conectado" : "desconectado"); op(out, max, &p, ": "); opd(out, max, &p, c[i].celdas); op(out, max, &p, " hechos, ");
            opd(out, max, &p, c[i].nodos); op(out, max, &p, " ramas, "); opd(out, max, &p, c[i].kb); op(out, max, &p, " KB\n");
        }
        op(out, max, &p, "memoria cartucho nuevo NOMBRE | usar ID | conectar ID | desconectar ID | exportar ID [RUTA] [vectores] | clonar ID NOMBRE | vaciar ID | borrar ID");
        return 1;
    }
    const char *resto = sig(args, a1, sizeof a1);
    if (eq(acc, "nuevo")) { cart_nuevo(args, "", out, max); return 1; }
    int i = cart_busca(a1);
    if (i < 0) { op(out, max, &p, "No existe el cartucho '"); op(out, max, &p, a1); op(out, max, &p, "' (memoria cartucho lista)."); return 1; }
    if (eq(acc, "usar") || eq(acc, "primario")) cart_primario(i, out, max);
    else if (eq(acc, "conectar") || eq(acc, "montar")) cart_montar(i, 1, out, max);
    else if (eq(acc, "desconectar") || eq(acc, "desmontar")) cart_montar(i, 0, out, max);
    else if (eq(acc, "vaciar")) cart_vaciar(i, out, max);
    else if (eq(acc, "borrar")) cart_borrar(i, out, max);
    else if (eq(acc, "clonar")) cart_clona(i, *resto ? resto : a1, out, max);
    else if (eq(acc, "exportar")) {
        char ruta[56]; const char *r2 = sig(resto, ruta, sizeof ruta);
        if (!ruta[0] || eq(ruta, "vectores")) { uint32_t q = 0; op(ruta, sizeof ruta, &q, "/usuario/"); op(ruta, sizeof ruta, &q, g_cart[i].id); op(ruta, sizeof ruta, &q, ".smem"); r2 = resto; }
        cart_exporta(i, ruta, contiene_(r2, "vectores"), out, max);
    } else op(out, max, &p, "memoria cartucho: accion desconocida (memoria cartucho lista)");
    return 1;
}

/* ------------------------------------------------------------------ la orden */
static const char *sig(const char *a, char *tok, uint32_t cap)
{
    while (*a == ' ') a++;
    uint32_t n = 0; while (*a && *a != ' ' && n + 1 < cap) tok[n++] = *a++;
    tok[n] = 0;
    while (*a == ' ') a++;
    return a;
}
static int importa_mem(char *out, uint32_t max, uint32_t *p)
{
    static const char *const F[] = { "/mem/ciencia_fisica.txt", "/mem/ciencia_computacion.txt", "/mem/historia_antigua.txt", "/mem/historia_moderna.txt", "/mem/conversaciones.txt", 0 };
    (void)out; (void)max; (void)p;
    int n = 0;
    for (int i = 0; F[i]; i++) {
        uint32_t len = 0; char *b = sys_read_alloc(F[i], &len);
        if (!b) continue;
        for (char *l = b; *l;) {
            char *e = l; while (*e && *e != '\n') e++;
            char sv = *e; *e = 0;
            if (sl(l) >= 20) { char fuente[MC_FUENTE_MAX]; uint32_t q = 0; op(fuente, sizeof fuente, &q, "Archivo "); op(fuente, sizeof fuente, &q, F[i]); if (guarda_celda(l, fuente, 5)) n++; }
            *e = sv; l = sv ? e + 1 : e;
        }
        kfree(b);
    }
    return n;
}

int memoria_cmd(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0; char sub[24];
    if (max) out[0] = 0;
    if (!g_listo) inicia();
    args = sig(args ? args : "", sub, sizeof sub);
    if (!sub[0] || eq(sub, "estado")) {
        uint32_t c, e, a, s, v; kmutex_lock(&g_mx); mc_stats(&g_m, &c, &e, &a, &s, &v); uint32_t nf = mc_nfrag(&g_m), qn = g_qn; kmutex_unlock(&g_mx);
        op(out, max, &p, g_c.activa ? "Memoria de SOMA: activa" : "Memoria de SOMA: DESACTIVADA (memoria config activa 1)");
        { int nm = 0; for (int c = 0; c < NCART; c++) if (g_mont[c]) nm++;                                    /* SOMA_283_MEMORIA_APP */
          op(out, max, &p, " | cartucho primario: "); op(out, max, &p, g_cart[g_prim].nombre); if (nm) { op(out, max, &p, " (+"); opd(out, max, &p, (uint32_t)nm); op(out, max, &p, " conectado/s)"); } op(out, max, &p, "\n"); }
        op(out, max, &p, "  hechos vigentes: "); opd(out, max, &p, c); op(out, max, &p, " | mensajes literales: "); opd(out, max, &p, e);
        op(out, max, &p, " | sustituidos: "); opd(out, max, &p, s); op(out, max, &p, " | archivados: "); opd(out, max, &p, a); op(out, max, &p, "\n");
        op(out, max, &p, "  sin embedding aun: "); opd(out, max, &p, v); op(out, max, &p, " | archivos: "); opd(out, max, &p, nf); op(out, max, &p, "/"); opd(out, max, &p, MC_MAX_SHARDS);
        op(out, max, &p, " | en cola de extraccion: "); opd(out, max, &p, qn); op(out, max, &p, "\n");
        op(out, max, &p, "  embeddings: ");
        if (g_c.local) { op(out, max, &p, "motor local de SOMA ("); op(out, max, &p, g_c.ruta_local[0] ? g_c.ruta_local : "falta el modelo en /modelos"); op(out, max, &p, ")"); }   /* SOMA_329 */
        else op(out, max, &p, g_c.srv ? g_c.url : "sin servidor (busqueda por palabras)");
        if (g_c.srv) { op(out, max, &p, " ["); op(out, max, &p, g_c.modelo); op(out, max, &p, g_dim_servidor ? ", " : ", sin probar"); if (g_dim_servidor) { opd(out, max, &p, (uint32_t)g_dim_servidor); op(out, max, &p, " dimensiones"); } op(out, max, &p, "]"); }
        { uint32_t nodos, sin_n, ids_[1]; kmutex_lock(&g_mx); nodos = mc_recuenta(&g_m); sin_n = 0; for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && g_m.it[i].estado == MC_ACTIVO && !g_m.it[i].nodo) sin_n++; (void)ids_;
          uint32_t tk = g_m.tick, ps = g_m.pasos, ni = g_ninforme; kmutex_unlock(&g_mx);
          op(out, max, &p, "\n  arbol: "); opd(out, max, &p, nodos); op(out, max, &p, " nodo(s), "); opd(out, max, &p, sin_n); op(out, max, &p, " celda(s) sin ordenar | sueno: ");
          op(out, max, &p, g_c.sueno ? "activo" : "apagado"); op(out, max, &p, ", reloj "); opd(out, max, &p, tk); op(out, max, &p, ", pasos "); opd(out, max, &p, ps);
          op(out, max, &p, " | olvidadas: "); opd(out, max, &p, g_n_olv); op(out, max, &p, " | restauradas: "); opd(out, max, &p, g_n_rest); op(out, max, &p, " | fusionadas: "); opd(out, max, &p, g_n_fus);
          if (ni) { op(out, max, &p, "\n  ultimo paso: "); kmutex_lock(&g_mx); op(out, max, &p, g_informe[ni - 1]); kmutex_unlock(&g_mx); } }
        op(out, max, &p, "\n  extraccion de hechos: "); op(out, max, &p, g_c.ingesta ? "activa" : "desactivada");
        op(out, max, &p, " | hechos nuevos: "); opd(out, max, &p, g_n_hechos); op(out, max, &p, " | repetidos: "); opd(out, max, &p, g_n_dup);
        op(out, max, &p, " | sustituciones: "); opd(out, max, &p, g_n_sust); op(out, max, &p, " | fallos: "); opd(out, max, &p, g_n_fallo);
        return 1;
    }
    if (eq(sub, "ayuda")) {
        op(out, max, &p, "memoria estado | memoria recordar TEXTO | memoria buscar CONSULTA | memoria ver ID | memoria olvidar ID | memoria borrar ID | memoria cartucho [lista|nuevo|usar|conectar|desconectar|exportar|clonar|vaciar|borrar] | memoria arbol | memoria sueno [N] | memoria probar | memoria servidor [arrancar|parar|estado] | "
                         "memoria config [activa|ingesta|hechos|episodios|emb_url|emb_modelo|emb_motor VALOR] | memoria importar [RUTA.smem [sin-vectores]] (sin ruta: el arbol antiguo /mem; con ruta: el cartucho de memorIA exportado con tools/exporta_memoria.py) | memoria guardar");
        return 1;
    }
    if (eq(sub, "cartucho") || eq(sub, "cartuchos")) return cmd_cartucho(args, out, max);     /* SOMA_283_MEMORIA_APP */
    if (eq(sub, "borrar")) {
        uint32_t id = 0; for (const char *d = args; *d >= '0' && *d <= '9'; d++) id = id * 10 + (uint32_t)(*d - '0');
        kmutex_lock(&g_mx); uint8_t *sel = mc_get(&g_m, id) ? marca_ids(&id, 1) : 0; kmutex_unlock(&g_mx);
        if (!sel) { op(out, max, &p, "memoria: no existe ese numero (memoria borrar ID borra para siempre; memoria olvidar ID solo archiva)"); return 1; }
        borra_marcados(sel, out, max); mc_free(sel);
        { uint32_t q = sl(out); op(out, max, &q, " Los numeros de los demas recuerdos pueden haber cambiado."); }
        return 1;
    }
    if (eq(sub, "recordar")) {
        if (!*args) { op(out, max, &p, "uso: memoria recordar TEXTO"); return 1; }
        uint32_t id = guarda_celda(args, "Orden memoria recordar", 8);
        if (id) { op(out, max, &p, "Recordado (#"); opd(out, max, &p, id); op(out, max, &p, ")."); } else op(out, max, &p, "memoria: llena");
        return 1;
    }
    if (eq(sub, "buscar")) {
        if (!*args) { op(out, max, &p, "uso: memoria buscar CONSULTA"); return 1; }
        if (!busca_texto(args, out, max, &p, 1)) op(out, max, &p, "Sin coincidencias.");
        return 1;
    }
    if (eq(sub, "ver") || eq(sub, "olvidar")) {
        uint32_t id = 0; for (const char *d = args; *d >= '0' && *d <= '9'; d++) id = id * 10 + (uint32_t)(*d - '0');
        kmutex_lock(&g_mx);
        const struct mc_item *e = mc_get(&g_m, id);
        if (!e) op(out, max, &p, "memoria: no existe ese numero");
        else if (eq(sub, "olvidar")) { mc_set_estado(&g_m, id, MC_ARCHIVADO, ahora(), 0); g_guardar = 1; op(out, max, &p, "Archivado (se puede recuperar con memoria ver)."); }
        else {
            char f[24]; mc_fecha(e->t, f, sizeof f);
            op(out, max, &p, e->kind == MC_CELDA ? "Hecho #" : "Mensaje #"); opd(out, max, &p, e->id); op(out, max, &p, e->estado == MC_ACTIVO ? " (vigente)" : e->estado == MC_ARCHIVADO ? " (archivado)" : " (sustituido por #");
            if (e->estado == MC_SUSTITUIDO) { opd(out, max, &p, e->sust_por); op(out, max, &p, ")"); }
            op(out, max, &p, ", importancia "); opd(out, max, &p, e->imp); op(out, max, &p, ", "); op(out, max, &p, f);
            if (e->flen) { op(out, max, &p, ", fuente: "); op(out, max, &p, e->fuente); }
            if (e->kind == MC_CELDA && e->nodo && g_m.nodo[e->nodo].usado) { char rt[160]; mc_nodo_ruta_texto(&g_m, e->nodo, rt, sizeof rt); op(out, max, &p, ", tema: "); op(out, max, &p, rt); }     /* SOMA_279_SUENO */
            op(out, max, &p, e->vec ? "\n" : " (sin embedding)\n"); op(out, max, &p, e->texto);
        }
        kmutex_unlock(&g_mx);
        return 1;
    }
    if (eq(sub, "servidor")) {                                   /* SOMA_278_EMBEDDINGS: arrancar y parar bge-m3 por el lanzador, como el LLM */
        char acc[16]; sig(args, acc, sizeof acc);
        if (!acc[0] || eq(acc, "estado")) {
            int st = llm_launcher_emb_status();
            op(out, max, &p, "Servidor de embeddings: "); op(out, max, &p, st == 1 ? "LISTO" : st == 2 ? "cargando" : st == 0 ? "parado" : st == -2 ? "el lanzador no responde" : "ERROR");
            if (st < 0) { op(out, max, &p, "\n"); op(out, max, &p, llm_last_error); }
            else if (st == 0) op(out, max, &p, " (memoria servidor arrancar)");
            return 1;
        }
        if (eq(acc, "arrancar") || eq(acc, "iniciar")) {
            int st = llm_launcher_emb_status();
            if (st == 1) { op(out, max, &p, "El servidor de embeddings ya estaba listo."); g_emb_cae_hasta = 0; return 1; }
            kprint("memoria: arrancando bge-m3 en el servidor (unos segundos) ...\n");
            if (llm_launcher_emb_start(120000, 0)) {
                g_emb_cae_hasta = 0;
                op(out, max, &p, "Servidor de embeddings LISTO. "); op(out, max, &p, g_c.srv ? "Los recuerdos sin vector se iran embebiendo solos." : "Falta fijar su direccion: memoria config emb_url http://IP:8089");
            } else { op(out, max, &p, "No arranco. "); op(out, max, &p, llm_last_error); }
            return 1;
        }
        if (eq(acc, "parar")) {
            if (llm_launcher_emb_stop() == 0) op(out, max, &p, "Servidor de embeddings parado (la memoria busca por palabras mientras tanto).");
            else op(out, max, &p, llm_last_error);
            return 1;
        }
        op(out, max, &p, "uso: memoria servidor [estado|arrancar|parar]");
        return 1;
    }
    if (eq(sub, "probar")) {
        int8_t v[MC_DIM_MAX]; uint32_t d = 0;
        if (!g_c.srv) { op(out, max, &p, "No hay servidor de embeddings configurado: memoria config emb_url http://IP:8089"); return 1; }
        g_emb_cae_hasta = 0;
        uint64_t t0 = timer_get_uptime_ms();
        if (emb_texto("prueba de embeddings", v, &d)) { op(out, max, &p, g_c.local ? "Motor local de embeddings OK: " : "Servidor de embeddings OK: "); opd(out, max, &p, d); op(out, max, &p, " dimensiones, "); opd(out, max, &p, (uint32_t)(timer_get_uptime_ms() - t0)); op(out, max, &p, " ms"); }
        else if (g_c.local) { op(out, max, &p, "El motor local no pudo calcular el embedding con "); op(out, max, &p, g_c.ruta_local[0] ? g_c.ruta_local : g_c.modelo); op(out, max, &p, " (ia instalar "); op(out, max, &p, g_c.modelo); op(out, max, &p, " lo copia del pendrive). Mientras, la memoria busca por palabras."); }
        else { op(out, max, &p, "El servidor "); op(out, max, &p, g_c.url); op(out, max, &p, " no contesta a /v1/embeddings (arrancalo con: memoria servidor arrancar). Mientras, la memoria busca por palabras."); }
        return 1;
    }
    if (eq(sub, "config")) {
        char k[24]; const char *v = sig(args, k, sizeof k);
        if (!k[0]) { op(out, max, &p, "activa="); opd(out, max, &p, (uint32_t)g_c.activa); op(out, max, &p, " ingesta="); opd(out, max, &p, (uint32_t)g_c.ingesta); op(out, max, &p, " sueno="); opd(out, max, &p, (uint32_t)g_c.sueno); op(out, max, &p, " sueno_s="); opd(out, max, &p, (uint32_t)g_c.sueno_s); op(out, max, &p, " hechos="); opd(out, max, &p, (uint32_t)g_c.n_hechos);
            op(out, max, &p, " episodios="); opd(out, max, &p, (uint32_t)g_c.n_epis); op(out, max, &p, " emb_url="); op(out, max, &p, g_c.url); op(out, max, &p, " emb_modelo="); op(out, max, &p, g_c.modelo);
            op(out, max, &p, " emb_motor="); op(out, max, &p, g_c.motor == 1 ? "local" : g_c.motor == 2 ? "servidor" : "auto"); return 1; }
        int x = 0; for (const char *d = v; *d >= '0' && *d <= '9'; d++) x = x * 10 + (*d - '0');
        if (eq(k, "activa")) g_c.activa = x != 0; else if (eq(k, "ingesta")) g_c.ingesta = x != 0;
        else if (eq(k, "sueno")) g_c.sueno = x != 0; else if (eq(k, "sueno_s")) g_c.sueno_s = x < 10 ? 10 : x > 3600 ? 3600 : x;
        else if (eq(k, "hechos")) g_c.n_hechos = x < 1 ? 1 : x > 40 ? 40 : x; else if (eq(k, "episodios")) g_c.n_epis = x < 0 ? 0 : x > 60 ? 60 : x;
        else if (eq(k, "emb_url")) { uint32_t i = 0; while (v[i] && i < sizeof g_c.url - 1) { g_c.url[i] = v[i]; i++; } g_c.url[i] = 0; conf_srv(&g_c); g_emb_cae_hasta = 0; if (g_c.url[0] && !g_c.srv) { op(out, max, &p, "URL no valida (formato http://A.B.C.D:puerto). "); } }
        else if (eq(k, "emb_modelo")) { uint32_t i = 0; while (v[i] && i < sizeof g_c.modelo - 1) { g_c.modelo[i] = v[i]; i++; } g_c.modelo[i] = 0; conf_srv(&g_c); g_emb_cae_hasta = 0; }
        else if (eq(k, "emb_motor")) {                                  /* SOMA_329_IA_LOCAL */
            if (!eq(v, "local") && !eq(v, "servidor") && !eq(v, "auto")) { op(out, max, &p, "emb_motor: local, servidor o auto"); return 1; }
            g_c.motor = eq(v, "local") ? 1 : eq(v, "servidor") ? 2 : 0; conf_srv(&g_c); g_emb_cae_hasta = 0;
        }
        else { op(out, max, &p, "memoria config: clave desconocida"); return 1; }
        conf_guarda(); op(out, max, &p, "Guardado en /etc/memoria.conf");
        return 1;
    }
    if (eq(sub, "importar") && *args) {                           /* SOMA_280_IMPORTAR */
        char ruta[64]; uint32_t i = 0; while (*args && *args != ' ' && i < sizeof ruta - 1) ruta[i++] = *args++; ruta[i] = 0;
        while (*args == ' ') args++;
        importa_smem(ruta, !eq(args, "sin-vectores"), out, max, &p);
        return 1;
    }
    if (eq(sub, "importar")) {
        int n = importa_mem(out, max, &p);
        op(out, max, &p, "Importados "); opd(out, max, &p, (uint32_t)n); op(out, max, &p, " hechos del arbol antiguo /mem (los archivos no se tocan).");
        return 1;
    }
    if (eq(sub, "guardar")) { g_guardar = 0; memoria_guarda_todo(); op(out, max, &p, "Memoria guardada."); return 1; }
    if (eq(sub, "arbol")) {                                       /* SOMA_279_SUENO */
        kmutex_lock(&g_mx);
        mc_recuenta(&g_m);
        uint32_t sin_n = 0; for (uint32_t i = 0; i < g_m.n; i++) if (g_m.it[i].kind == MC_CELDA && g_m.it[i].estado == MC_ACTIVO && !g_m.it[i].nodo) sin_n++;
        uint32_t n0 = 0;
        for (uint32_t r1 = 1; r1 <= MC_NODOS_MAX; r1++) {
            if (!g_m.nodo[r1].usado || g_m.nodo[r1].padre) continue;
            n0++;
            /* recorrido en profundidad con pila explicita: raiz, hijos, nietos */
            uint32_t st[MC_NODOS_MAX + 1], sp = 0; st[sp++] = r1;
            while (sp) {
                uint32_t id = st[--sp]; const struct mc_nodo *x = &g_m.nodo[id];
                for (uint32_t k = 0; k < x->nivel; k++) op(out, max, &p, "   ");
                op(out, max, &p, x->nivel ? "- " : "* "); op(out, max, &p, x->nombre); op(out, max, &p, " ("); opd(out, max, &p, x->cuenta); op(out, max, &p, ")");
                if (x->desc[0]) { op(out, max, &p, ": "); op(out, max, &p, x->desc); }
                op(out, max, &p, "\n");
                for (uint32_t c = MC_NODOS_MAX; c >= 1; c--) if (g_m.nodo[c].usado && g_m.nodo[c].padre == id && sp < MC_NODOS_MAX) st[sp++] = c;
            }
        }
        if (!n0) op(out, max, &p, "El arbol esta vacio: el ciclo de sueno lo va armando (memoria sueno 9 lo adelanta).\n");
        op(out, max, &p, "Sin ordenar todavia: "); opd(out, max, &p, sin_n); op(out, max, &p, " celda(s)");
        kmutex_unlock(&g_mx);
        return 1;
    }
    if (eq(sub, "sueno")) {                                       /* SOMA_279_SUENO: pasos del ciclo a mano */
        uint32_t n = 0; for (const char *d = args; *d >= '0' && *d <= '9'; d++) n = n * 10 + (uint32_t)(*d - '0');
        if (!n) n = 1;
        if (n > 9) n = 9;
        kprint("memoria: ejecutando "); kprint_dec(n); kprint(" paso(s) del ciclo de sueno (cada uno puede llamar al LLM) ...\n");
        for (uint32_t i = 0; i < n; i++) {
            char rep[150]; sueno_paso(rep, sizeof rep);
            kmutex_lock(&g_mx); op(out, max, &p, g_informe[g_ninforme - 1]); kmutex_unlock(&g_mx);
            op(out, max, &p, "\n");
        }
        g_guardar = 1;
        return 1;
    }
    op(out, max, &p, "memoria: orden desconocida. memoria ayuda");
    return 1;
}
