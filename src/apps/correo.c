/* correo.c: SOMA_295_CORREO: la aplicacion Correo. Ver correo.h.
 * Hilos: el de la GUI pinta y atiende; "correo" hace la red (IMAP/SMTP) y las llamadas al modelo, una tras otra, sacandolas de una
 * cola. El hilo del agente (el orbe) usa la misma cola y espera a que su trabajo termine. Los resultados se dejan en el estado de
 * la aplicacion de una vez (sin esperar a la red en medio), asi la GUI nunca ve nada a medias. */
#include "correo.h"
#include "correo_core.h"
#include "correo_red.h"
#include "correo_sbx.h"   /* SOMA_311_CORREO_AISLADO: los mensajes se leen en el servicio aislado */
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"
#include "textfield.h"
#include "clipboard.h"
#include "idt.h"
#include "mem.h"
#include "fs.h"
#include "thread.h"
#include "llm.h"
#include "agenda.h"
#ifndef SOMA_CORREO_HOST
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "tls13.h"
#include "tls_trust.h"
#include "tun_tls.h"     /* SOMA_326_TUNEL_TLS */
#endif

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);
void gui_open_correo(void);

void *co_alloc(uint32_t n) { return kmalloc(n ? n : 1); }
void  co_free(void *p) { if (p) kfree(p); }

/* el transporte de la red: TCP o TLS 1.3 de la pila de SOMA (en la prueba del anfitrion lo pone la prueba) */
void correo_transporte(struct co_tr *t);
void correo_transporte_libera(struct co_tr *t);

/* ---------------------------------------------------------------- paleta (la del chat) */
#define C_BG     0x000B0F1Au
#define C_SIDE   0x00070A12u
#define C_SIDEH  0x00121827u
#define C_SEL    0x001A2238u
#define C_CARD   0x00111727u
#define C_CARD2  0x00171E33u
#define C_INP    0x00141B2Eu
#define C_BD     0x00222A44u
#define C_BDL    0x00313B60u
#define C_TX     0x00F3F4F6u
#define C_TX2    0x00CBD2DDu
#define C_MU     0x008B949Eu
#define C_FAINT  0x00535B70u
#define C_ACC    0x006366F1u
#define C_ACC2   0x008B5CF6u
#define C_CYAN   0x0022D3EEu
#define C_GREEN  0x0010B981u
#define C_AMBER  0x00F59E0Bu
#define C_RED    0x00EF4444u
#define C_IA     0x00A78BFAu

#define CONF_FILE   "/etc/correo.conf"
#define DIR_CACHE   "/correo"
#define MAX_CARP    24
#define CAP_ENTRADA 400
#define CAP_OTRA    200
#define RAW_CAP     (4u << 20)
#define TXT_CAP     (512u << 10)
#define CUERPO_CAP  32768
#define NRES        120

/* ---------------------------------------------------------------- utilidades */
static int slen(const char *s) { return (int)co_len(s); }
static void scpy(char *d, const char *s, int cap) { co_cpy(d, s, (uint32_t)cap); }
static void scat(char *d, const char *s, int cap) { co_cat(d, s, (uint32_t)cap); }
static void snum(char *d, int cap, uint32_t v) { co_num(d, (uint32_t)cap, v); }
static uint64_t ms(void) { return timer_get_uptime_ms(); }
static int64_t ahora_local(void) { return agenda_ahora(); }
static int64_t ahora_utc(void) { int64_t l = ahora_local(); return l - co_desfase(l - 60); }
static uint32_t g_ver = 1;
static void cambia(void) { g_ver++; }
static char g_toast[200]; static uint64_t g_toast_t; static int g_toast_err;
static void toast(const char *t, int err) { scpy(g_toast, t, sizeof g_toast); g_toast_t = ms(); g_toast_err = err; cambia(); }
static uint32_t g_azar = 0x29501u;
static uint32_t azar(void) { g_azar ^= g_azar << 13; g_azar ^= g_azar >> 17; g_azar ^= g_azar << 5; g_azar += (uint32_t)ms(); return g_azar; }

/* ---------------------------------------------------------------- estado */
struct cache { char imap[160]; uint32_t uidv, max_uid; struct co_msg *v; int n, cap; int cargada; };
static struct co_cuenta g_c; static int g_hay_cuenta, g_ini;
static struct co_carpeta g_carp[MAX_CARP]; static int g_ncarp;
static struct cache g_cache[MAX_CARP];
static int g_ci;                                          /* carpeta a la vista */
enum { V_CARPETA, V_IMPORTANTES, V_NO_LEIDOS, V_DESTACADOS, V_BUSQUEDA };
static int g_vista;
struct res { int c; uint32_t uid; };                     /* lo que se ve en la lista (carpeta y UID) */
static struct res g_res[NRES]; static int g_nres;        /* resultados de una busqueda */
static struct co_msg g_extra[60]; static int g_extra_c[60]; static int g_nextra;   /* mensajes que llegaron por una busqueda en el servidor */
static char g_busca_txt[200]; static int g_busca_ia, g_busca_estado;   /* 0 nada, 1 buscando, 2 lista */
static char g_busca_expl[200];                           /* lo que entendio la IA */
static int g_sel_c = -1; static uint32_t g_sel_uid;      /* el mensaje elegido */
/* el mensaje abierto */
static char *g_raw; static uint32_t g_raw_n; static int g_raw_cortado;
static struct co_mime *g_mime; static char *g_txt; static uint32_t g_txt_n;
static int g_abierto_c = -1; static uint32_t g_abierto_uid; static int g_abriendo;
/* estado de la red */
static volatile int g_ocupado; static char g_estado[160]; static uint64_t g_ult_sinc; static int g_sinc_err;
static char g_err_conexion[240];

/* ---------------------------------------------------------------- copia local */
static uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }
static void ruta_cache(const char *imap, char *o, int cap)
{
    static const char H[] = "0123456789abcdef"; uint32_t h = fnv(imap); char x[9]; for (int i = 7; i >= 0; i--) { x[i] = H[h & 15]; h >>= 4; } x[8] = 0;
    scpy(o, DIR_CACHE "/c", cap); scat(o, x, cap); scat(o, ".txt", cap);
}
static int cache_cap(int c) { return g_carp[c].uso == CO_U_ENTRADA ? CAP_ENTRADA : CAP_OTRA; }
static int cache_asegura(int c)
{
    struct cache *k = &g_cache[c];
    if (k->v) return 1;
    k->cap = cache_cap(c);
    k->v = (struct co_msg *)co_alloc((uint32_t)k->cap * sizeof(struct co_msg));
    if (!k->v) { k->cap = 0; return 0; }
    k->n = 0; scpy(k->imap, g_carp[c].imap, sizeof k->imap);
    return 1;
}
static void cache_guarda(int c)
{
    struct cache *k = &g_cache[c];
    if (!k->v) return;
    uint32_t need = (uint32_t)k->n * 1100u + 256u; char *b = (char *)co_alloc(need);
    if (!b) return;
    uint32_t n = co_msgs_serializa(k->v, k->n, b, need);
    if (n) {
        char cab[64] = "#uidv "; snum(cab, sizeof cab, k->uidv); scat(cab, "\n", sizeof cab);
        uint32_t cl = co_len(cab); char *t = (char *)co_alloc(n + cl + 1);
        if (t) { for (uint32_t i = 0; i < cl; i++) t[i] = cab[i]; for (uint32_t i = 0; i < n; i++) t[cl + i] = b[i]; char r[64]; ruta_cache(k->imap, r, sizeof r); (void)vfs_write(r, t, n + cl); co_free(t); }
    }
    co_free(b);
}
static void cache_carga(int c)
{
    struct cache *k = &g_cache[c];
    if (k->cargada || !cache_asegura(c)) return;
    k->cargada = 1;
    char r[64]; ruta_cache(g_carp[c].imap, r, sizeof r);
    uint32_t n = 0; char *t = vfs_read_alloc(r, &n);
    if (!t) return;
    if (n > 6 && t[0] == '#' && t[1] == 'u') {
        uint32_t i = 6, v = 0; while (i < n && t[i] >= '0' && t[i] <= '9') v = v * 10 + (uint32_t)(t[i++] - '0');
        while (i < n && t[i] != '\n') i++;
        k->uidv = v;
        k->n = co_msgs_carga(t + i + 1, n - i - 1, k->v, k->cap);
        k->max_uid = 0; for (int j = 0; j < k->n; j++) if (k->v[j].uid > k->max_uid) k->max_uid = k->v[j].uid;
    }
    kfree(t);
}
static void carpetas_guarda(void)
{
    uint32_t cap = (uint32_t)g_ncarp * 300u + 32u; char *b = (char *)co_alloc(cap);
    if (!b) return;
    b[0] = 0;
    for (int i = 0; i < g_ncarp; i++) {
        char t[16] = ""; snum(t, sizeof t, (uint32_t)g_carp[i].uso); scat(t, "\t", sizeof t); snum(t, sizeof t, (uint32_t)g_carp[i].noselect); scat(t, "\t", sizeof t);
        co_cat(b, t, cap); co_cat(b, g_carp[i].imap, cap); co_cat(b, "\t", cap); co_cat(b, g_carp[i].nombre, cap); co_cat(b, "\n", cap);
    }
    (void)vfs_write(DIR_CACHE "/carpetas.txt", b, co_len(b));
    co_free(b);
}
static void carpetas_carga(void)
{
    uint32_t n = 0; char *t = vfs_read_alloc(DIR_CACHE "/carpetas.txt", &n);
    g_ncarp = 0;
    if (t) {
        for (uint32_t i = 0; i < n && g_ncarp < MAX_CARP;) {
            uint32_t e = i; while (e < n && t[e] != '\n') e++;
            const char *f[4]; uint32_t fl[4]; int nf = 0; uint32_t a = i;
            for (uint32_t j = i; j <= e && nf < 4; j++) if (j == e || t[j] == '\t') { f[nf] = t + a; fl[nf] = j - a; nf++; a = j + 1; }
            if (nf == 4 && fl[2]) {
                struct co_carpeta *c = &g_carp[g_ncarp++]; for (uint32_t z = 0; z < sizeof *c; z++) ((uint8_t *)c)[z] = 0;
                c->uso = f[0][0] - '0'; c->noselect = f[1][0] == '1';
                co_cpyn(c->imap, f[2], fl[2], sizeof c->imap); co_cpyn(c->nombre, f[3], fl[3], sizeof c->nombre);
            }
            i = e + 1;
        }
        kfree(t);
    }
    if (!g_ncarp) { struct co_carpeta *c = &g_carp[g_ncarp++]; for (uint32_t z = 0; z < sizeof *c; z++) ((uint8_t *)c)[z] = 0; c->uso = CO_U_ENTRADA; scpy(c->imap, "INBOX", sizeof c->imap); scpy(c->nombre, "Recibidos", sizeof c->nombre); }
}
static int carpeta_uso(int uso) { for (int i = 0; i < g_ncarp; i++) if (g_carp[i].uso == uso && !g_carp[i].noselect) return i; return -1; }
static int carpeta_entrada(void) { int i = carpeta_uso(CO_U_ENTRADA); return i < 0 ? 0 : i; }
static void cuenta_guarda(void)
{
    char b[1200]; co_cuenta_serializa(&g_c, b, sizeof b);
    (void)vfs_write(CONF_FILE, b, co_len(b));
}
static void cuenta_carga(void)
{
    char b[1200]; int n = vfs_read(CONF_FILE, b, sizeof b - 1);
    g_hay_cuenta = 0;
    if (n > 0) { b[n] = 0; g_hay_cuenta = co_cuenta_carga(&g_c, b, (uint32_t)n); }
}

/* mensaje por carpeta y UID (en la copia local o en lo que llego por una busqueda) */
static struct co_msg *msg_de(int c, uint32_t uid)
{
    if (c >= 0 && c < g_ncarp && g_cache[c].v) for (int i = 0; i < g_cache[c].n; i++) if (g_cache[c].v[i].uid == uid) return &g_cache[c].v[i];
    for (int i = 0; i < g_nextra; i++) if (g_extra_c[i] == c && g_extra[i].uid == uid) return &g_extra[i];
    return 0;
}

/* ================================================================ la cola de trabajos (hilo "correo") */
enum { J_NADA, J_SINC, J_ABRE, J_BANDERA, J_MUEVE, J_ENVIA, J_BUSCA, J_RESUMEN, J_RESPUESTA, J_REDACTA, J_MEJORA, J_CLASIFICA, J_CALENDARIO, J_GUARDA_ADJ, J_PRUEBA, J_LEE_ORBE };
struct trabajo { int tipo, c, modo, poner; uint32_t uid, flags, id; char *texto; };
#define NTRAB 32
static struct trabajo g_q[NTRAB]; static volatile uint32_t g_qi, g_qf;
static volatile uint32_t g_id_sig = 1, g_id_hecho;
static volatile int g_hilo;
static struct co_imap g_s; static struct co_tr g_tr, g_tr2; static int g_tr_listo;
static uint64_t g_ult_red;
static void trabaja_uno(void);
static void hilo_correo(void *arg)
{
    (void)arg;
    for (;;) {
        if (g_qi == g_qf) {
            if (g_s.abierta && ms() - g_ult_red > 120000u) { g_ocupado = 1; co_imap_cierra(&g_s); g_ocupado = 0; }   /* sin uso: se cierra la sesion */
            thread_sleep(40); continue;
        }
        trabaja_uno();
    }
}
static uint32_t encola(int tipo, int c, uint32_t uid, uint32_t flags, int poner, int modo, const char *texto)
{
    if (g_qf - g_qi >= NTRAB) { toast("Demasiadas tareas pendientes del correo: espera un momento", 1); return 0; }
    struct trabajo *j = &g_q[g_qf % NTRAB];
    j->tipo = tipo; j->c = c; j->uid = uid; j->flags = flags; j->poner = poner; j->modo = modo; j->texto = 0;
    if (texto) { uint32_t n = co_len(texto); j->texto = (char *)co_alloc(n + 1); if (j->texto) co_cpy(j->texto, texto, n + 1); }
    j->id = g_id_sig++;
    __sync_synchronize();
    g_qf++;
#ifndef SOMA_CORREO_HOST
    if (!g_hilo) { g_hilo = 1; thread_create("correo", hilo_correo, 0); }
#endif
    cambia();
    return j->id;
}
static int pendiente(int tipo) { for (uint32_t i = g_qi; i != g_qf; i++) if (g_q[i % NTRAB].tipo == tipo) return 1; return 0; }

/* --- la sesion IMAP (se abre al hacer falta y se reabre si se cayo) */
static int imap_listo(char *err, uint32_t ecap)
{
    if (!g_tr_listo) { correo_transporte(&g_tr); correo_transporte(&g_tr2); g_tr_listo = 1; }
    g_ult_red = ms();
    if (g_s.abierta) return 1;
    if (co_imap_abre(&g_s, &g_tr, &g_c) != 0) { co_cpy(err, g_s.err, ecap); return 0; }
    return 1;
}
static int imap_sel(const char *imap, int rw)
{
    if (g_s.abierta && !co_ieq(g_s.sel, imap)) g_s.sel[0] = 0;
    if (g_s.abierta && g_s.sel[0] && (g_s.sel_rw || !rw)) return 0;
    return co_imap_selecciona(&g_s, imap, rw);
}
static void estado(const char *t) { scpy(g_estado, t ? t : "", sizeof g_estado); cambia(); }

/* --- sincronizar una carpeta */
static int sinc_carpeta(int c, int *nuevos)
{
    struct cache *k = &g_cache[c];
    *nuevos = 0;
    cache_carga(c);
    if (!k->v) return -1;
    if (imap_sel(g_carp[c].imap, 1)) return -1;
    int reinicia = k->uidv && k->uidv != g_s.uidvalidity;
    int cap = k->cap;
    struct co_msg *nv = (struct co_msg *)co_alloc((uint32_t)cap * sizeof(struct co_msg));
    if (!nv) return -1;
    int nn = co_imap_ultimos(&g_s, nv, reinicia || !k->n ? (cap < 80 ? cap : 80) : (cap < 150 ? cap : 150), reinicia ? 0 : k->max_uid);
    if (nn < 0) { co_free(nv); return -1; }
    /* banderas (y los que ya no estan) de lo que hay en la copia */
    uint32_t *uids = 0, *fl = 0; int nf = -1;
    if (k->n && !reinicia) {
        uint32_t menor = k->v[0].uid; for (int i = 0; i < k->n; i++) if (k->v[i].uid < menor) menor = k->v[i].uid;
        uint32_t m = (uint32_t)k->n + 400u;
        uids = (uint32_t *)co_alloc(m * 4u); fl = (uint32_t *)co_alloc(m * 4u);
        if (uids && fl) nf = co_imap_banderas_todas(&g_s, menor, uids, fl, (int)m);
    }
    /* juntar: lo nuevo delante (mas nuevo primero), lo que sigue en el servidor detras */
    if (reinicia) k->n = 0;
    int w = 0;
    struct co_msg *out = (struct co_msg *)co_alloc((uint32_t)cap * sizeof(struct co_msg));
    if (out) {
        for (int i = 0; i < nn && w < cap; i++) { int dup = 0; for (int j = 0; j < k->n; j++) if (k->v[j].uid == nv[i].uid) dup = 1; if (!dup) { out[w++] = nv[i]; (*nuevos)++; } }
        for (int i = 0; i < k->n && w < cap; i++) {
            struct co_msg m = k->v[i];
            if (nf >= 0) { int f = -1; for (int j = 0; j < nf; j++) if (uids[j] == m.uid) { f = j; break; } if (f < 0) continue; m.flags = fl[f]; }
            out[w++] = m;
        }
        for (int i = 1; i < w; i++) { struct co_msg t = out[i]; int j = i; while (j > 0 && out[j - 1].uid < t.uid) { out[j] = out[j - 1]; j--; } out[j] = t; }
        for (int i = 0; i < w; i++) k->v[i] = out[i];                     /* de una vez: la GUI no ve nada a medias */
        k->n = w; k->uidv = g_s.uidvalidity;
        k->max_uid = 0; for (int i = 0; i < w; i++) if (k->v[i].uid > k->max_uid) k->max_uid = k->v[i].uid;
        co_free(out);
    }
    co_free(nv); co_free(uids); co_free(fl);
    cache_guarda(c);
    { uint32_t nl = 0, tot = 0; if (!co_imap_estado(&g_s, g_carp[c].imap, &nl, &tot)) { g_carp[c].no_leidos = nl; g_carp[c].total = tot; } }
    return 0;
}
static void j_sinc(struct trabajo *j)
{
    char err[240];
    estado("Conectando con el servidor de correo\342\200\246");
    if (!imap_listo(err, sizeof err)) { scpy(g_err_conexion, err, sizeof g_err_conexion); g_sinc_err = 1; estado(""); if (!j->modo) toast(err, 1); return; }
    g_err_conexion[0] = 0;
    if (j->modo == 2 || g_ncarp <= 1) {                                    /* la lista de carpetas */
        struct co_carpeta *cp = (struct co_carpeta *)co_alloc(sizeof(struct co_carpeta) * 64);
        int n = cp ? co_imap_carpetas(&g_s, cp, 64) : -1;
        if (n > 0) {
            /* orden: Recibidos, Destacados... (usos) y despues las del usuario por nombre; las mismas cach\303\251s siguen con su carpeta */
            static const int ORD[] = { CO_U_ENTRADA, CO_U_ENVIADOS, CO_U_BORRADORES, CO_U_ARCHIVO, CO_U_SPAM, CO_U_PAPELERA };
            static struct co_carpeta nueva[MAX_CARP]; int k = 0;
            for (int o = 0; o < 6; o++) for (int i = 0; i < n && k < MAX_CARP; i++) if (cp[i].uso == ORD[o] && !cp[i].noselect) { nueva[k++] = cp[i]; break; }
            for (int i = 0; i < n && k < MAX_CARP; i++) if (!cp[i].noselect && (cp[i].uso == CO_U_NADA)) nueva[k++] = cp[i];
            if (k) {
                char sel[160]; scpy(sel, g_ci < g_ncarp ? g_carp[g_ci].imap : "INBOX", sizeof sel);
                static struct cache vieja[MAX_CARP]; for (int i = 0; i < MAX_CARP; i++) vieja[i] = g_cache[i];
                for (int i = 0; i < MAX_CARP; i++) { g_cache[i].v = 0; g_cache[i].n = g_cache[i].cap = 0; g_cache[i].cargada = 0; g_cache[i].imap[0] = 0; g_cache[i].uidv = g_cache[i].max_uid = 0; }
                for (int i = 0; i < k; i++) {
                    g_carp[i] = nueva[i];
                    for (int z = 0; z < MAX_CARP; z++) if (vieja[z].v && co_ieq(vieja[z].imap, nueva[i].imap)) { g_cache[i] = vieja[z]; vieja[z].v = 0; }
                }
                for (int z = 0; z < MAX_CARP; z++) if (vieja[z].v) co_free(vieja[z].v);
                g_ncarp = k; g_ci = 0;
                for (int i = 0; i < k; i++) if (co_ieq(g_carp[i].imap, sel)) g_ci = i;
                carpetas_guarda();
            }
        }
        co_free(cp);
    }
    int c = j->c >= 0 && j->c < g_ncarp ? j->c : g_ci;
    estado("Sincronizando\342\200\246");
    int nuevos = 0;
    if (sinc_carpeta(c, &nuevos)) { toast(g_s.err[0] ? g_s.err : "No se pudo leer la carpeta", 1); g_sinc_err = 1; }
    else {
        g_sinc_err = 0; g_ult_sinc = ms();
        int e = carpeta_entrada();
        if (c != e && g_cache[e].v) { int n2; (void)sinc_carpeta(e, &n2); }
        for (int i = 0; i < g_ncarp; i++) if (i != c && i != e && (g_carp[i].uso == CO_U_ENTRADA)) { uint32_t nl, tot; if (!co_imap_estado(&g_s, g_carp[i].imap, &nl, &tot)) g_carp[i].no_leidos = nl; }
        if (nuevos && j->modo == 1 && g_carp[c].uso == CO_U_ENTRADA) { char t[80] = ""; snum(t, sizeof t, (uint32_t)nuevos); scat(t, nuevos == 1 ? " correo nuevo" : " correos nuevos", sizeof t); toast(t, 0); }
    }
    estado("");
}
static void abre_comun(int c, uint32_t uid, char *raw, uint32_t rn, int cortado)
{
    struct co_mime *mm = (struct co_mime *)co_alloc(sizeof *mm);
    uint32_t tcap = rn + 4096 < TXT_CAP ? rn + 4096 : TXT_CAP;
    char *tx = (char *)co_alloc(tcap);
    if (!mm || !tx) { co_free(mm); co_free(tx); co_free(raw); toast("Sin memoria para abrir el correo", 1); return; }
    correo_mime_lee(raw, rn, mm, tx, tcap);                 /* SOMA_311_CORREO_AISLADO: en el servicio aislado */
    if (cortado) mm->truncado = 1;
    /* de una vez */
    char *r0 = g_raw; struct co_mime *m0 = g_mime; char *t0 = g_txt;
    g_raw = raw; g_raw_n = rn; g_raw_cortado = cortado; g_mime = mm; g_txt = tx; g_txt_n = mm->texto_n; g_abierto_c = c; g_abierto_uid = uid;
    co_free(r0); co_free(m0); co_free(t0);
    struct co_msg *m = msg_de(c, uid);
    if (m && !m->extracto[0]) co_extracto(tx, mm->texto_n, m->extracto, sizeof m->extracto);
}
static void j_abre(struct trabajo *j)
{
    char err[240];
    if (!imap_listo(err, sizeof err)) { toast(err, 1); g_abriendo = 0; return; }
    if (imap_sel(g_carp[j->c].imap, 1)) { toast(g_s.err, 1); g_abriendo = 0; return; }
    uint32_t cap = RAW_CAP; char *raw = (char *)co_alloc(cap);
    if (!raw) { cap = 1u << 20; raw = (char *)co_alloc(cap); }
    if (!raw) { toast("Sin memoria para abrir el correo", 1); g_abriendo = 0; return; }
    int cortado = 0;
    estado("Abriendo el correo\342\200\246");
    uint32_t rn = co_imap_mensaje(&g_s, j->uid, raw, cap, &cortado);
    estado("");
    if (!rn) { co_free(raw); toast(g_s.err, 1); g_abriendo = 0; return; }
    abre_comun(j->c, j->uid, raw, rn, cortado);
    struct co_msg *m = msg_de(j->c, j->uid);
    if (m && !(m->flags & CO_F_VISTO)) {
        if (!co_imap_banderas(&g_s, j->uid, CO_F_VISTO, 1)) { m->flags |= CO_F_VISTO; if (g_carp[j->c].no_leidos) g_carp[j->c].no_leidos--; cache_guarda(j->c); }
    }
    g_abriendo = 0;
}
static void j_bandera(struct trabajo *j)
{
    char err[240];
    if (!imap_listo(err, sizeof err) || imap_sel(g_carp[j->c].imap, 1) || co_imap_banderas(&g_s, j->uid, j->flags, j->poner)) {
        toast(err[0] && !g_s.abierta ? err : g_s.err, 1);
        struct co_msg *m = msg_de(j->c, j->uid);                          /* deshacer lo que ya se veia */
        if (m) { if (j->poner) m->flags &= ~j->flags; else m->flags |= j->flags; }
        return;
    }
    cache_guarda(j->c);
}
static void quita_de_cache(int c, uint32_t uid)
{
    struct cache *k = &g_cache[c]; int w = 0;
    for (int i = 0; i < k->n; i++) if (k->v[i].uid != uid) k->v[w++] = k->v[i];
    k->n = w;
    int r = 0; for (int i = 0; i < g_nres; i++) if (!(g_res[i].c == c && g_res[i].uid == uid)) g_res[r++] = g_res[i];
    g_nres = r;
}
static void j_mueve(struct trabajo *j)
{
    char err[240];
    int dest = carpeta_uso(j->modo);
    if (dest < 0 && j->modo != -1) { toast("El servidor no tiene esa carpeta", 1); return; }
    if (!imap_listo(err, sizeof err) || imap_sel(g_carp[j->c].imap, 1)) { toast(g_s.abierta ? g_s.err : err, 1); return; }
    int r = (j->modo == -1 || dest == j->c) ? co_imap_borra(&g_s, j->uid) : co_imap_mueve(&g_s, j->uid, g_carp[dest].imap);
    if (r) { toast(g_s.err, 1); int n; g_s.sel[0] = 0; (void)sinc_carpeta(j->c, &n); return; }
    if (dest >= 0 && g_cache[dest].v) { int n; (void)sinc_carpeta(dest, &n); (void)imap_sel(g_carp[j->c].imap, 1); }
    cache_guarda(j->c);
}
/* el texto del trabajo de envio: campos separados por \1 */
static int campo(const char *t, int k, char *o, int cap)
{
    const char *p = t; for (int i = 0; i < k && p; i++) { while (*p && *p != 1) p++; p = *p ? p + 1 : 0; }
    if (!p) { o[0] = 0; return 0; }
    int n = 0; while (p[n] && p[n] != 1 && n + 1 < cap) { o[n] = p[n]; n++; } o[n] = 0;
    return n;
}
static volatile int g_envio_res; static char g_envio_err[300];
static void j_envia(struct trabajo *j)
{
    static char para[600], cc[400], cco[300], asunto[300], irt[200], refs[600], adj[200];
    char *cuerpo = (char *)co_alloc(CUERPO_CAP);
    char *msg = 0;
    g_envio_err[0] = 0;
    if (!cuerpo || !j->texto) { scpy(g_envio_err, "Sin memoria para enviar", sizeof g_envio_err); g_envio_res = -1; co_free(cuerpo); return; }
    campo(j->texto, 0, para, sizeof para); campo(j->texto, 1, cc, sizeof cc); campo(j->texto, 2, cco, sizeof cco);
    campo(j->texto, 3, asunto, sizeof asunto); campo(j->texto, 4, cuerpo, CUERPO_CAP); campo(j->texto, 5, irt, sizeof irt); campo(j->texto, 6, refs, sizeof refs); campo(j->texto, 7, adj, sizeof adj);
    struct co_envio en; for (uint32_t z = 0; z < sizeof en; z++) ((uint8_t *)&en)[z] = 0;
    en.de_nombre = g_c.nombre; en.de_email = g_c.email; en.para = para; en.cc = cc; en.cco = cco; en.asunto = asunto; en.cuerpo = cuerpo;
    en.en_respuesta_a = irt[0] ? irt : 0; en.referencias = refs[0] ? refs : 0;
    en.utc_min = ahora_utc(); en.off_min = co_desfase(en.utc_min); en.azar = azar();
    char *adj_d = 0; uint32_t adj_n = 0;                                /* SOMA_360_HOJA_GRAFICOS: el adjunto */
    if (adj[0]) {
        adj_d = vfs_read_alloc(adj, &adj_n);
        if (!adj_d || !adj_n || adj_n > CORREO_ADJ_MAX) { if (adj_d) kfree(adj_d); scpy(g_envio_err, "No se pudo leer el adjunto (o pasa de 20 MB)", sizeof g_envio_err); g_envio_res = -1; co_free(cuerpo); return; }
        const char *b = adj; for (const char *z = adj; *z; ++z) if (*z == '/') b = z + 1;
        en.adj_nombre = b; en.adj = (const uint8_t *)adj_d; en.adj_n = adj_n;
    }
    uint32_t cap = (uint32_t)co_len(cuerpo) * 4u + 16384u + (adj_n ? adj_n / 57u * 80u + 8192u : 0u); msg = (char *)co_alloc(cap);
    static struct co_dir rc[40]; int nr = co_destinatarios(&en, rc, 40);
    uint32_t mn = msg ? co_compone(&en, msg, cap) : 0;
    if (!mn) { scpy(g_envio_err, "No se pudo componer el mensaje", sizeof g_envio_err); g_envio_res = -1; goto fin; }
    if (!g_tr_listo) { correo_transporte(&g_tr); correo_transporte(&g_tr2); g_tr_listo = 1; }
    estado("Enviando\342\200\246");
    if (co_smtp_envia(&g_tr2, &g_c, rc, nr, msg, mn, g_envio_err, sizeof g_envio_err)) { g_envio_res = -1; estado(""); goto fin; }
    g_envio_res = 1;
    /* copia en Enviados (Gmail la guarda solo) y la marca de respondido en el original */
    char err[240]; int env = carpeta_uso(CO_U_ENVIADOS);
    if (!g_c.gmail && !g_s.gmail && env >= 0 && imap_listo(err, sizeof err)) { if (!co_imap_anade(&g_s, g_carp[env].imap, msg, mn, CO_F_VISTO) && g_cache[env].v) { int n; (void)sinc_carpeta(env, &n); } }
    if (j->uid && j->c >= 0 && imap_listo(err, sizeof err) && !imap_sel(g_carp[j->c].imap, 1) && !co_imap_banderas(&g_s, j->uid, CO_F_RESPONDIDO, 1)) {
        struct co_msg *m = msg_de(j->c, j->uid); if (m) m->flags |= CO_F_RESPONDIDO;
    }
    estado("");
fin:
    co_free(cuerpo); co_free(msg);
    if (adj_d) kfree(adj_d);
    cambia();
}

/* ---------------------------------------------------------------- el modelo */
static const char SYS_BUSCA[] =
    "Conviertes una petici\303\263n de b\303\272squeda de correo electr\303\263nico en un filtro JSON. Responde SOLO con un objeto JSON con estas claves "
    "(omite las que no se pidan): \"de\" (remitente: nombre o direcci\303\263n), \"para\", \"asunto\", \"palabras\" (palabras clave que deben aparecer, "
    "pocas y significativas, sin art\303\255culos), \"desde\" y \"hasta\" (fechas AAAA-MM-DD), \"no_leidos\", \"destacados\", \"adjuntos\" (true/false) y "
    "\"categoria\" (importante, personal, trabajo, notificacion o promocion). Ejemplo: {\"de\":\"amazon\",\"palabras\":\"pedido auriculares\",\"desde\":\"2026-11-01\"}";
static const char SYS_ELIGE[] =
    "Eres el buscador del correo de SOMA. Te paso lo que busca el usuario y una lista de correos (id | de | asunto | extracto). "
    "Elige los que encajan, del m\303\241s al menos relevante, y responde SOLO con una l\303\255nea: IDS: n\303\272meros separados por comas (o IDS: ninguno).";
static const char SYS_RESUMEN[] =
    "Resumes correos electr\303\263nicos para el usuario de SOMA, en espa\303\261ol y en 2 a 4 frases claras: qu\303\251 dice y qu\303\251 quiere el remitente. "
    "Si pide algo al usuario o hay una fecha, plazo o importe, ponlo en una l\303\255nea aparte que empiece por \"Te pide:\" o \"Fecha:\". "
    "No inventes nada que no est\303\251 en el correo. Sin Markdown.";
static const char SYS_RESPUESTA[] =
    "Redactas respuestas de correo electr\303\263nico en nombre del usuario, en el idioma del correo original (normalmente espa\303\261ol). "
    "Responde SOLO con el cuerpo de la respuesta, listo para enviar: saludo, contenido y despedida con el nombre del usuario. "
    "Sin asunto, sin comillas y sin Markdown. No inventes datos (fechas, importes, compromisos) que el usuario no haya dado: si faltan, deja [corchetes] para rellenar.";
static const char SYS_REDACTA[] =
    "Redactas correos electr\303\263nicos en nombre del usuario. Responde SOLO en este formato:\nPARA: nombre <direcci\303\263n>\nASUNTO: el asunto\nCUERPO:\nel texto del correo\n"
    "Usa la direcci\303\263n de la lista de contactos que se te pasa si el destinatario est\303\241 en ella; si no la sabes, deja PARA con solo el nombre. "
    "Escribe en espa\303\261ol (salvo que se pida otro idioma), con saludo y despedida con el nombre del usuario, sin Markdown y sin inventar datos.";
static const char SYS_MEJORA[] =
    "Reescribes borradores de correo electr\303\263nico. Mant\303\251n el idioma, el sentido y todos los datos (nombres, fechas, importes). "
    "Responde SOLO con el texto nuevo del cuerpo, sin comentarios ni Markdown.";
static const char SYS_CLASIFICA[] =
    "Clasificas correos. Para cada uno (id | de | asunto | extracto) elige UNA categor\303\255a: importante (requiere atenci\303\263n o respuesta del usuario, "
    "de una persona o algo urgente: facturas, citas, trabajo pendiente), personal, trabajo, notificacion (avisos autom\303\241ticos, env\303\255os, seguridad) o "
    "promocion (publicidad, ofertas, boletines). Responde SOLO con una l\303\255nea por correo: id: categoria";
static const char SYS_CALENDARIO[] =
    "Del correo que te paso, saca el evento con fecha (una cita, reuni\303\263n, plazo o entrega) y resp\303\263ndelo en UNA frase corta en espa\303\261ol con este estilo: "
    "\"el martes 24 a las 10 reuni\303\263n con Ana\" o \"el 3 de diciembre entrega del presupuesto\". Si hay hora, ponla. Si no hay ning\303\272n evento, responde NINGUNO.";
static const char SYS_LEE[] =
    "Eres SOMA y le cuentas al usuario, en voz alta y en espa\303\261ol, qu\303\251 correos tiene. Te paso la lista (de | asunto | extracto). "
    "Haz un resumen natural de 2 a 5 frases: cu\303\241ntos hay, de qui\303\251n son los importantes y qu\303\251 piden. Sin listas ni Markdown.";

static void fecha_hoy(char *d, int cap)
{
    int64_t l = ahora_local(); int y, m, dd, h, mi; ag_desde_min(l, &y, &m, &dd, &h, &mi);
    d[0] = 0; snum(d, cap, (uint32_t)y); scat(d, "-", cap); if (m < 10) scat(d, "0", cap); snum(d, cap, (uint32_t)m); scat(d, "-", cap); if (dd < 10) scat(d, "0", cap); snum(d, cap, (uint32_t)dd);
    scat(d, " (", cap); scat(d, ag_nombre_dia(ag_dia_semana(ag_dia_de(l))), cap); scat(d, ")", cap);
}
static int ia(const char *sys, const char *prompt, char *out, uint32_t cap, const char *tag, int max_tokens)
{
    llm_trace_tag(tag);
    int r = llm_chat_ex(sys, prompt, 0, 0, max_tokens, out, cap, 240000u);
    if (r != 1) { co_cpy(out, llm_last_error[0] ? llm_last_error : "El modelo de lenguaje no ha contestado", cap); return 0; }
    char *t = (char *)co_alloc(cap); if (t) { co_ia_limpia(out, t, cap); co_cpy(out, t, cap); co_free(t); }
    return 1;
}
static void linea_msg(char *p, uint32_t cap, uint32_t id, const struct co_msg *m)
{
    char t[24] = ""; co_num(t, sizeof t, id); co_cat(p, t, cap); co_cat(p, " | ", cap); co_cat(p, m->de[0] ? m->de : m->de_email, cap);
    co_cat(p, " | ", cap); co_cat(p, m->asunto, cap); co_cat(p, " | ", cap);
    char e[110]; co_cpy(e, m->extracto, sizeof e); co_cat(p, e, cap); co_cat(p, "\n", cap);
}
/* --- busqueda: escrita (filtros) o con IA (el modelo entiende la peticion y, si hace falta, elige entre los candidatos) */
static int res_pon(int c, uint32_t uid) { for (int i = 0; i < g_nres; i++) if (g_res[i].c == c && g_res[i].uid == uid) return 0; if (g_nres >= NRES) return 0; g_res[g_nres].c = c; g_res[g_nres].uid = uid; g_nres++; return 1; }
static void j_busca(struct trabajo *j)
{
    struct co_filtro f; char *r = (char *)co_alloc(8192);
    int c = j->c;
    if (!r) return;
    g_busca_expl[0] = 0;
    if (j->modo) {                                                         /* con IA */
        estado("La IA est\303\241 entendiendo lo que buscas\342\200\246");
        char p[600] = "Hoy es "; char f0[40]; fecha_hoy(f0, sizeof f0); scat(p, f0, sizeof p); scat(p, ". Busca: ", sizeof p); scat(p, j->texto ? j->texto : "", sizeof p);
        if (!ia(SYS_BUSCA, p, r, 8192, "correo-busca", 300) || !co_filtro_json(r, &f)) {
            co_filtro_texto(j->texto ? j->texto : "", ahora_local(), &f);
            if (!co_ibusca(r, co_len(r), "{")) { scpy(g_busca_expl, "La IA no ha contestado: busco las palabras tal cual", sizeof g_busca_expl); }
        }
    } else co_filtro_texto(j->texto ? j->texto : "", ahora_local(), &f);
    if (!g_busca_expl[0]) {                                                 /* lo que se busca, dicho en claro */
        char *e = g_busca_expl; int cap = sizeof g_busca_expl; e[0] = 0;
        if (f.de[0]) { scat(e, "de ", cap); scat(e, f.de, cap); scat(e, " \302\267 ", cap); }
        if (f.para[0]) { scat(e, "para ", cap); scat(e, f.para, cap); scat(e, " \302\267 ", cap); }
        if (f.asunto[0]) { scat(e, "asunto \342\200\234", cap); scat(e, f.asunto, cap); scat(e, "\342\200\235 \302\267 ", cap); }
        if (f.palabras[0]) { scat(e, "\342\200\234", cap); scat(e, f.palabras, cap); scat(e, "\342\200\235 \302\267 ", cap); }
        if (f.desde || f.hasta) scat(e, "fechas \302\267 ", cap);
        if (f.no_leidos) scat(e, "sin leer \302\267 ", cap);
        if (f.destacados) scat(e, "destacados \302\267 ", cap);
        if (f.adjuntos) scat(e, "con adjuntos \302\267 ", cap);
        if (f.cat) { scat(e, co_cat_nombre(f.cat), cap); scat(e, " \302\267 ", cap); }
        int n = slen(e); if (n >= 4) e[n - 4] = 0;
    }
    /* 1) la copia local de la carpeta (y de Recibidos si es otra) */
    int idx[NRES]; int nres_local = 0;
    g_nres = 0;
    int carp[2] = { c, carpeta_entrada() };
    for (int z = 0; z < 2; z++) {
        if (z && carp[1] == carp[0]) break;
        cache_carga(carp[z]);
        int n = co_busca(g_cache[carp[z]].v, g_cache[carp[z]].n, &f, idx, NRES);
        for (int i = 0; i < n; i++) if (res_pon(carp[z], g_cache[carp[z]].v[idx[i]].uid)) nres_local++;
    }
    /* 2) el servidor (lo que no esta en la copia) */
    char err[240];
    if (g_hay_cuenta && !co_filtro_vacio(&f) && imap_listo(err, sizeof err) && !imap_sel(g_carp[c].imap, 0)) {
        estado("Buscando en el servidor\342\200\246");
        char crit[700]; co_filtro_imap(&f, crit, sizeof crit);
        uint32_t *u = (uint32_t *)co_alloc(4u * 2000u); int nu = u ? co_imap_busca(&g_s, crit, u, 2000) : -1;
        if (nu > 0) {
            uint32_t falta[40]; int nf = 0;
            for (int i = nu - 1; i >= 0 && nf < 40; i--) if (!msg_de(c, u[i])) falta[nf++] = u[i];
            if (nf) {
                struct co_msg *tmp = (struct co_msg *)co_alloc(sizeof(struct co_msg) * 40u);
                int k = tmp ? co_imap_estos(&g_s, falta, nf, tmp) : 0;
                for (int i = 0; i < k && g_nextra < 60; i++) { g_extra[g_nextra] = tmp[i]; g_extra_c[g_nextra] = c; g_nextra++; res_pon(c, tmp[i].uid); }
                co_free(tmp);
            }
        }
        co_free(u);
    }
    /* 3) con IA: si no hay nada (o hay demasiado), el modelo elige entre los candidatos */
    if (j->modo && (g_nres == 0 || g_nres > 12)) {
        estado("La IA est\303\241 eligiendo los correos que encajan\342\200\246");
        struct co_filtro amplio = f; amplio.palabras[0] = 0; amplio.asunto[0] = 0;
        struct res cand[60]; int nc = 0;
        if (g_nres > 12) { for (int i = 0; i < g_nres && nc < 60; i++) cand[nc++] = g_res[i]; }
        else {
            struct cache *k = &g_cache[carp[0]];
            int n = co_busca(k->v, k->n, &amplio, idx, 60);
            if (n == 0 && !co_filtro_vacio(&amplio)) { struct co_filtro nada; for (uint32_t z = 0; z < sizeof nada; z++) ((uint8_t *)&nada)[z] = 0; n = co_busca(k->v, k->n, &nada, idx, 60); }
            for (int i = 0; i < n; i++) { cand[nc].c = carp[0]; cand[nc].uid = k->v[idx[i]].uid; nc++; }
        }
        if (nc) {
            uint32_t pc = 900u + (uint32_t)nc * 320u; char *p = (char *)co_alloc(pc);
            if (p) {
                co_cpy(p, "Busca: ", pc); co_cat(p, j->texto ? j->texto : "", pc); co_cat(p, "\n\nCorreos:\n", pc);
                for (int i = 0; i < nc; i++) { struct co_msg *m = msg_de(cand[i].c, cand[i].uid); if (m) linea_msg(p, pc, (uint32_t)i + 1u, m); }
                if (ia(SYS_ELIGE, p, r, 8192, "correo-elige", 200)) {
                    uint32_t ids[NRES]; int ni = co_ia_ids(r, ids, NRES);
                    g_nres = 0;
                    for (int i = 0; i < ni; i++) if (ids[i] >= 1 && (int)ids[i] <= nc) res_pon(cand[ids[i] - 1].c, cand[ids[i] - 1].uid);
                }
                co_free(p);
            }
        }
    }
    (void)nres_local;
    g_busca_estado = 2; g_vista = V_BUSQUEDA;
    estado("");
    co_free(r);
    cambia();
}
/* --- el resumen de un correo */
struct resumen { uint32_t uid; int c, estado; char t[1400]; };
static struct resumen g_resu[6]; static int g_resu_sig;
static struct resumen *resumen_de(int c, uint32_t uid) { for (int i = 0; i < 6; i++) if (g_resu[i].uid == uid && g_resu[i].c == c && g_resu[i].estado) return &g_resu[i]; return 0; }
static uint32_t prompt_correo(char *p, uint32_t cap, uint32_t max_txt)      /* el correo abierto para el modelo */
{
    p[0] = 0;
    if (!g_mime) return 0;
    struct co_dir d[2]; char de[260] = "";
    if (co_dirs(g_mime->de, d, 1) == 1) co_dir_txt(&d[0], de, sizeof de);
    co_cat(p, "De: ", cap); co_cat(p, de, cap); co_cat(p, "\nAsunto: ", cap); co_cat(p, g_mime->asunto, cap);
    if (g_mime->fecha) { char f[64]; co_fecha_rfc(g_mime->fecha, co_desfase(g_mime->fecha), f, sizeof f); co_cat(p, "\nFecha: ", cap); co_cat(p, f, cap); }
    co_cat(p, "\n\n", cap);
    uint32_t k = co_len(p), n = g_txt_n < max_txt ? g_txt_n : max_txt;
    for (uint32_t i = 0; i < n && k + 1 < cap; i++) p[k++] = g_txt[i];
    p[k] = 0;
    if (g_txt_n > max_txt) co_cat(p, "\n[... recortado ...]", cap);
    return co_len(p);
}
static void j_resumen(struct trabajo *j)
{
    struct resumen *s = &g_resu[g_resu_sig++ % 6];
    s->uid = j->uid; s->c = j->c; s->estado = 1; s->t[0] = 0; cambia();
    uint32_t cap = 24000; char *p = (char *)co_alloc(cap); char *r = (char *)co_alloc(4096);
    if (!p || !r || g_abierto_uid != j->uid) { s->estado = -1; scpy(s->t, "Abre el correo para resumirlo", sizeof s->t); co_free(p); co_free(r); return; }
    prompt_correo(p, cap, 16000);
    estado("La IA est\303\241 leyendo el correo\342\200\246");
    int ok = ia(SYS_RESUMEN, p, r, 4096, "correo-resumen", 400);
    co_cpy(s->t, r, sizeof s->t); s->estado = ok ? 2 : -1;
    estado("");
    co_free(p); co_free(r);
    cambia();
}
/* --- la redaccion (las respuestas de la IA van a la ventana de redaccion por g_ia_*) */
static volatile int g_ia_res; static char *g_ia_cuerpo; static char g_ia_asunto[300], g_ia_para[600], g_ia_err[240]; static int g_ia_que;
static void ia_deja(int ok, const char *para, const char *asunto, const char *cuerpo, int que)
{
    if (!g_ia_cuerpo) g_ia_cuerpo = (char *)co_alloc(CUERPO_CAP);
    if (g_ia_cuerpo) co_cpy(g_ia_cuerpo, cuerpo ? cuerpo : "", CUERPO_CAP);
    co_cpy(g_ia_para, para ? para : "", sizeof g_ia_para); co_cpy(g_ia_asunto, asunto ? asunto : "", sizeof g_ia_asunto);
    g_ia_que = que;
    __sync_synchronize();
    g_ia_res = ok ? 1 : -1;
    cambia();
}
static const char *const TONOS[4] = { "natural y amable", "formal", "cercano", "muy breve (2 o 3 frases)" };
static void j_respuesta(struct trabajo *j)
{
    uint32_t cap = 26000; char *p = (char *)co_alloc(cap); char *r = (char *)co_alloc(CUERPO_CAP);
    if (!p || !r) { scpy(g_ia_err, "Sin memoria", sizeof g_ia_err); ia_deja(0, 0, 0, 0, J_RESPUESTA); co_free(p); co_free(r); return; }
    co_cpy(p, "El usuario se llama ", cap); co_cat(p, g_c.nombre[0] ? g_c.nombre : g_c.email, cap); co_cat(p, ". Tono: ", cap); co_cat(p, TONOS[j->modo & 3], cap); co_cat(p, ".\n", cap);
    if (j->texto && j->texto[0]) { co_cat(p, "Lo que quiere decir el usuario: ", cap); co_cat(p, j->texto, cap); co_cat(p, "\n", cap); }
    co_cat(p, "\nCorreo al que responde:\n", cap);
    uint32_t k = co_len(p); prompt_correo(p + k, cap - k, 12000);
    estado("La IA est\303\241 escribiendo la respuesta\342\200\246");
    int ok = ia(SYS_RESPUESTA, p, r, CUERPO_CAP, "correo-respuesta", 900);
    if (!ok) scpy(g_ia_err, r, sizeof g_ia_err);
    ia_deja(ok, 0, 0, r, J_RESPUESTA);
    estado("");
    co_free(p); co_free(r);
}
static uint32_t contactos_txt(char *o, uint32_t cap)
{
    struct co_dir *d = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 40); int *ve = (int *)co_alloc(sizeof(int) * 40);
    o[0] = 0;
    if (!d || !ve) { co_free(d); co_free(ve); return 0; }
    int e = carpeta_entrada(), env = carpeta_uso(CO_U_ENVIADOS), n = 0;
    cache_carga(e); if (env >= 0) cache_carga(env);
    /* primero a quien se escribe (Enviados), despues quien escribe */
    if (env >= 0 && g_cache[env].v) n = co_contactos(g_cache[env].v, g_cache[env].n, g_c.email, d, ve, 25);
    if (g_cache[e].v) {
        static struct co_dir d2[40]; static int v2[40]; int n2 = co_contactos(g_cache[e].v, g_cache[e].n, g_c.email, d2, v2, 40);
        for (int i = 0; i < n2 && n < 40; i++) { int rep = 0; for (int z = 0; z < n; z++) if (co_ieq(d[z].email, d2[i].email)) rep = 1; if (!rep) d[n++] = d2[i]; }
    }
    for (int i = 0; i < n; i++) { char t[220]; co_dir_txt(&d[i], t, sizeof t); co_cat(o, "- ", cap); co_cat(o, t, cap); co_cat(o, "\n", cap); }
    co_free(d); co_free(ve);
    return co_len(o);
}
/* PARA que dio el modelo -> direcciones de verdad (las que no se sepan se dejan con el nombre para que el usuario las complete) */
static void resuelve_para(const char *in, char *out, uint32_t cap, char *falta, uint32_t fcap)
{
    out[0] = 0; falta[0] = 0;
    char tmp[600]; co_cpy(tmp, in, sizeof tmp);
    const char *p = tmp;
    int e = carpeta_entrada(), env = carpeta_uso(CO_U_ENVIADOS);
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == ';') p++;
        const char *q = p; int ang = 0; while (*q && !((*q == ',' || *q == ';') && !ang)) { if (*q == '<') ang = 1; if (*q == '>') ang = 0; q++; }
        char uno[300]; co_cpyn(uno, p, (uint32_t)(q - p), sizeof uno);
        if (uno[0]) {
            struct co_dir d[1]; char t[300] = "";
            if (co_dirs(uno, d, 1) == 1 && co_email_valido(d[0].email)) co_dir_txt(&d[0], t, sizeof t);
            else {
                struct co_dir r; int ok = (g_cache[e].v && co_resuelve(g_cache[e].v, g_cache[e].n, g_c.email, uno, &r)) || (env >= 0 && g_cache[env].v && co_resuelve(g_cache[env].v, g_cache[env].n, g_c.email, uno, &r));
                if (ok) co_dir_txt(&r, t, sizeof t);
                else { if (falta[0]) co_cat(falta, ", ", fcap); co_cat(falta, uno, fcap); }
            }
            if (t[0]) { if (out[0]) co_cat(out, ", ", cap); co_cat(out, t, cap); }
        }
        p = *q ? q + 1 : q;
    }
}
static int redacta(const char *instr, char *para, uint32_t pcap, char *asunto, uint32_t acap, char *cuerpo, uint32_t ccap, char *falta, uint32_t fcap, char *err, uint32_t ecap)
{
    uint32_t cap = 9000; char *p = (char *)co_alloc(cap); char *r = (char *)co_alloc(CUERPO_CAP);
    if (!p || !r) { co_cpy(err, "Sin memoria", ecap); co_free(p); co_free(r); return 0; }
    char f[40]; fecha_hoy(f, sizeof f);
    co_cpy(p, "El usuario se llama ", cap); co_cat(p, g_c.nombre[0] ? g_c.nombre : g_c.email, cap); co_cat(p, " <", cap); co_cat(p, g_c.email, cap); co_cat(p, ">. Hoy es ", cap); co_cat(p, f, cap);
    co_cat(p, ".\nContactos del usuario:\n", cap);
    uint32_t k = co_len(p); contactos_txt(p + k, cap - k - 1200);
    co_cat(p, "\nLo que pide el usuario: ", cap); co_cat(p, instr, cap);
    int ok = ia(SYS_REDACTA, p, r, CUERPO_CAP, "correo-redacta", 900);
    char pa[600] = "";
    if (ok) ok = co_ia_borrador(r, pa, sizeof pa, asunto, acap, cuerpo, ccap);
    if (!ok) co_cpy(err, r[0] ? r : "El modelo no ha dado un borrador", ecap);
    else resuelve_para(pa, para, pcap, falta, fcap);
    co_free(p); co_free(r);
    return ok;
}
static void j_redacta(struct trabajo *j)
{
    char *cu = (char *)co_alloc(CUERPO_CAP); static char pa[600], as[300], falta[300];
    if (!cu) { scpy(g_ia_err, "Sin memoria", sizeof g_ia_err); ia_deja(0, 0, 0, 0, J_REDACTA); return; }
    estado("La IA est\303\241 escribiendo el correo\342\200\246");
    int ok = redacta(j->texto ? j->texto : "", pa, sizeof pa, as, sizeof as, cu, CUERPO_CAP, falta, sizeof falta, g_ia_err, sizeof g_ia_err);
    if (ok && falta[0]) { scpy(g_ia_err, "No s\303\251 la direcci\303\263n de: ", sizeof g_ia_err); scat(g_ia_err, falta, sizeof g_ia_err); }
    else if (ok) g_ia_err[0] = 0;
    ia_deja(ok, pa, as, cu, J_REDACTA);
    estado("");
    co_free(cu);
}
static const char *const MEJORAS[5] = { "Corrige la ortograf\303\255a, la gram\303\241tica y la puntuaci\303\263n sin cambiar el estilo.",
    "Hazlo m\303\241s formal y profesional.", "Hazlo m\303\241s cercano y amable.", "Hazlo m\303\241s breve y directo.", "Trad\303\272celo al ingl\303\251s." };
static void j_mejora(struct trabajo *j)
{
    uint32_t cap = CUERPO_CAP + 400; char *p = (char *)co_alloc(cap); char *r = (char *)co_alloc(CUERPO_CAP);
    if (!p || !r) { scpy(g_ia_err, "Sin memoria", sizeof g_ia_err); ia_deja(0, 0, 0, 0, J_MEJORA); co_free(p); co_free(r); return; }
    co_cpy(p, MEJORAS[j->modo % 5], cap); co_cat(p, "\n\nBorrador:\n", cap); co_cat(p, j->texto ? j->texto : "", cap);
    estado("La IA est\303\241 mejorando el borrador\342\200\246");
    int ok = ia(SYS_MEJORA, p, r, CUERPO_CAP, "correo-mejora", 1200);
    if (!ok) scpy(g_ia_err, r, sizeof g_ia_err);
    ia_deja(ok, 0, 0, r, J_MEJORA);
    estado("");
    co_free(p); co_free(r);
}
static void j_clasifica(struct trabajo *j)
{
    int c = j->c; struct cache *k = &g_cache[c];
    cache_carga(c);
    if (!k->v || !k->n) return;
    uint32_t pc = 400u + 60u * 300u; char *p = (char *)co_alloc(pc); char *r = (char *)co_alloc(4096);
    uint32_t uids[60]; int n = 0;
    if (!p || !r) { co_free(p); co_free(r); return; }
    p[0] = 0;
    for (int i = 0; i < k->n && n < 60; i++) if (!k->v[i].cat) { uids[n] = k->v[i].uid; linea_msg(p, pc, k->v[i].uid, &k->v[i]); n++; }
    if (n) {
        estado("La IA est\303\241 clasificando los correos\342\200\246");
        if (ia(SYS_CLASIFICA, p, r, 4096, "correo-clasifica", 600)) {
            uint32_t ids[60]; uint8_t cats[60]; int nc = co_ia_cats(r, ids, cats, 60), puestas = 0;
            for (int i = 0; i < nc; i++) { struct co_msg *m = msg_de(c, ids[i]); if (m) { m->cat = cats[i]; puestas++; } }
            for (int i = 0; i < n; i++) { struct co_msg *m = msg_de(c, uids[i]); if (m && !m->cat) m->cat = CO_CAT_PERSONAL; }   /* lo que no dijo: normal */
            cache_guarda(c);
            if (!j->modo) { char t[80] = "La IA ha clasificado "; snum(t, sizeof t, (uint32_t)puestas); scat(t, " correos", sizeof t); toast(t, 0); }
        } else if (!j->modo) toast(r, 1);
        estado("");
    } else if (!j->modo) toast("Ya estaban todos clasificados", 0);
    co_free(p); co_free(r);
    cambia();
}
static void j_calendario(struct trabajo *j)
{
    uint32_t cap = 16000; char *p = (char *)co_alloc(cap); char *r = (char *)co_alloc(2048);
    if (!p || !r || g_abierto_uid != j->uid) { co_free(p); co_free(r); toast("Abre el correo para a\303\261adirlo al calendario", 1); return; }
    char f[40]; fecha_hoy(f, sizeof f);
    co_cpy(p, "Hoy es ", cap); co_cat(p, f, cap); co_cat(p, ".\n\n", cap);
    uint32_t k = co_len(p); prompt_correo(p + k, cap - k, 10000);
    estado("La IA est\303\241 buscando la fecha en el correo\342\200\246");
    if (ia(SYS_CALENDARIO, p, r, 2048, "correo-calendario", 120)) {
        char nr[64]; co_norm(r, nr, sizeof nr);
        if (co_iempieza(nr, "ninguno")) toast("La IA no ve ninguna cita o fecha en este correo", 1);
        else {
            char res[300], t[400];
            if (agenda_anade_texto(r, res, sizeof res) == 0) { scpy(t, "A\303\261adido al calendario: ", sizeof t); scat(t, res, sizeof t); toast(t, 0); }
            else { scpy(t, "El calendario no entiende la fecha: ", sizeof t); scat(t, r, sizeof t); toast(t, 1); }
        }
    } else toast(r, 1);
    estado("");
    co_free(p); co_free(r);
}
static void j_guarda_adj(struct trabajo *j)
{
    if (!g_mime || g_abierto_uid != j->uid || j->modo < 0 || j->modo >= g_mime->nadj) { toast("Abre el correo otra vez para guardar el adjunto", 1); return; }
    const struct co_adj *a = &g_mime->adj[j->modo];
    if (g_raw_cortado && a->off + a->len >= g_raw_n) { toast("Este adjunto es demasiado grande para SOMA (el correo lleg\303\263 cortado)", 1); return; }
    uint32_t cap = a->len + 16; uint8_t *b = (uint8_t *)co_alloc(cap);
    if (!b) { toast("Sin memoria para el adjunto", 1); return; }
    uint32_t n = correo_adj_saca(g_raw, g_raw_n, a, b, cap);   /* SOMA_311_CORREO_AISLADO */
    char ruta[200] = "/usuario/"; scat(ruta, a->nombre, sizeof ruta);
    if (vfs_write(ruta, (const char *)b, n) < 0) toast("No se pudo guardar el adjunto", 1);
    else { char t[260] = "Guardado en "; scat(t, ruta, sizeof t); toast(t, 0); }
    co_free(b);
}
static volatile int g_prueba_res; static char g_prueba_err[300];
static void j_prueba(struct trabajo *j)
{
    (void)j;
    char err[300];
    if (g_s.abierta) co_imap_cierra(&g_s);
    estado("Probando la cuenta (IMAP)\342\200\246");
    if (!imap_listo(err, sizeof err)) { scpy(g_prueba_err, "Recibir (IMAP): ", sizeof g_prueba_err); scat(g_prueba_err, err, sizeof g_prueba_err); g_prueba_res = -1; estado(""); return; }
    estado("Probando la cuenta (SMTP)\342\200\246");
    if (co_smtp_prueba(&g_tr2, &g_c, err, sizeof err)) { scpy(g_prueba_err, "Enviar (SMTP): ", sizeof g_prueba_err); scat(g_prueba_err, err, sizeof g_prueba_err); g_prueba_res = -1; estado(""); return; }
    g_prueba_res = 1;
    estado("");
    struct trabajo s = { J_SINC, 0, 2, 0, 0, 0, 0, 0 }; j_sinc(&s);          /* las carpetas y Recibidos */
}
/* --- el orbe: lo que hay en Recibidos, contado por la IA */
static char g_lee_txt[1600];
static void j_lee_orbe(struct trabajo *j)
{
    struct trabajo s = { J_SINC, carpeta_entrada(), 0, 0, 0, 0, 0, 0 }; j_sinc(&s);
    int e = carpeta_entrada(); struct cache *k = &g_cache[e];
    g_lee_txt[0] = 0;
    if (g_sinc_err) { scpy(g_lee_txt, "No he podido leer tu correo: ", sizeof g_lee_txt); scat(g_lee_txt, g_err_conexion[0] ? g_err_conexion : g_s.err, sizeof g_lee_txt); return; }
    struct co_filtro f; co_filtro_texto(j->texto ? j->texto : "", ahora_local(), &f);
    f.palabras[0] = 0;                                                     /* del orbe solo cuentan las fechas ("de hoy") */
    if (!f.desde) f.no_leidos = 1;
    int idx[30]; int n = co_busca(k->v, k->n, &f, idx, 30);
    if (!n) { scpy(g_lee_txt, f.desde ? "No tienes correos de esas fechas en Recibidos." : "No tienes correos sin leer. Todo al d\303\255a.", sizeof g_lee_txt); return; }
    uint32_t pc = 200u + (uint32_t)n * 330u; char *p = (char *)co_alloc(pc); char *r = (char *)co_alloc(2048);
    if (p && r) {
        p[0] = 0; for (int i = 0; i < n; i++) linea_msg(p, pc, (uint32_t)i + 1u, &k->v[idx[i]]);
        if (ia(SYS_LEE, p, r, 2048, "correo-orbe", 400)) co_cpy(g_lee_txt, r, sizeof g_lee_txt);
        else {                                                             /* sin modelo: la lista */
            scpy(g_lee_txt, "Tienes ", sizeof g_lee_txt); snum(g_lee_txt, sizeof g_lee_txt, (uint32_t)n); scat(g_lee_txt, n == 1 ? " correo: " : " correos: ", sizeof g_lee_txt);
            for (int i = 0; i < n && i < 5; i++) { if (i) scat(g_lee_txt, "; ", sizeof g_lee_txt); scat(g_lee_txt, k->v[idx[i]].de, sizeof g_lee_txt); scat(g_lee_txt, ", ", sizeof g_lee_txt); scat(g_lee_txt, k->v[idx[i]].asunto, sizeof g_lee_txt); }
            scat(g_lee_txt, ".", sizeof g_lee_txt);
        }
    }
    co_free(p); co_free(r);
}
static void trabaja_uno(void)
{
    if (g_qi == g_qf) return;
    struct trabajo j = g_q[g_qi % NTRAB];
    g_ocupado = 1; cambia();
    switch (j.tipo) {
    case J_SINC: j_sinc(&j); break;
    case J_ABRE: j_abre(&j); break;
    case J_BANDERA: j_bandera(&j); break;
    case J_MUEVE: j_mueve(&j); break;
    case J_ENVIA: j_envia(&j); break;
    case J_BUSCA: j_busca(&j); break;
    case J_RESUMEN: j_resumen(&j); break;
    case J_RESPUESTA: j_respuesta(&j); break;
    case J_REDACTA: j_redacta(&j); break;
    case J_MEJORA: j_mejora(&j); break;
    case J_CLASIFICA: j_clasifica(&j); break;
    case J_CALENDARIO: j_calendario(&j); break;
    case J_GUARDA_ADJ: j_guarda_adj(&j); break;
    case J_PRUEBA: j_prueba(&j); break;
    case J_LEE_ORBE: j_lee_orbe(&j); break;
    default: break;
    }
    co_free(j.texto);
    g_id_hecho = j.id;
    __sync_synchronize();
    g_qi++;
    g_ocupado = g_qi != g_qf; cambia();
}

/* ================================================================ dibujo */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int twn(const char *s, int n, int px, int fl) { return has_wf() ? wf_width(s, (uint32_t)n, px, fl) : n * 8; }
static int tw(const char *s, int px, int fl) { return twn(s, slen(s), px, fl); }
static void txn(int x, int top, int h, const char *s, int n, int px, uint32_t col, int fl)
{
    if (n <= 0) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, fl, 255); return; }
    char b[256]; int k = 0;
    for (int i = 0; i < n && k < 255;) { uint8_t c = (uint8_t)s[i]; if (c < 0x80) { b[k++] = (char)c; i++; } else { int l = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4; b[k++] = '?'; i += l; } }
    b[k] = 0; font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), b, col, 1);
}
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int fl) { txn(x, top, h, s, slen(s), px, col, fl); }
static int u8back(const char *s, int n) { while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; return n; }
static void txe(int x, int top, int h, const char *s, int px, uint32_t col, int fl, int maxw)   /* recortado con "..." */
{
    int n = slen(s);
    if (maxw <= 0 || !n) return;
    if (tw(s, px, fl) <= maxw) { txn(x, top, h, s, n, px, col, fl); return; }
    int pw = tw("\342\200\246", px, fl), lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (twn(s, mid, px, fl) + pw <= maxw) lo = mid; else hi = mid - 1; }
    lo = u8back(s, lo);
    txn(x, top, h, s, lo, px, col, fl); tx(x + twn(s, lo, px, fl), top, h, "\342\200\246", px, col, fl);
}
static void rr(int x, int y, int w, int h, int r, uint32_t rgb) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | rgb); }
static void rra(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void rs(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_stroke_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void ln(int x0, int y0, int x1, int y1, uint32_t col, int g16) { fb_line_aa(x0 * 16, y0 * 16, x1 * 16, y1 * 16, g16, 0xFF000000u | col); }
static void punto(int cx, int cy, int r, uint32_t col, uint32_t a) { fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, (a << 24) | col); }
static uint32_t mezcla(uint32_t a, uint32_t b, int k)
{
    uint32_t r = (((a >> 16) & 255) * (255 - (uint32_t)k) + ((b >> 16) & 255) * (uint32_t)k) / 255, g = (((a >> 8) & 255) * (255 - (uint32_t)k) + ((b >> 8) & 255) * (uint32_t)k) / 255,
             bl = ((a & 255) * (255 - (uint32_t)k) + (b & 255) * (uint32_t)k) / 255;
    return r << 16 | g << 8 | bl;
}
enum { I_SOBRE, I_SINC, I_ESTRELLA, I_ESTRELLA_LLENA, I_CLIP, I_PAPELERA, I_RESP, I_RESP_TODOS, I_REENV, I_IA, I_LUPA, I_ARCHIVO, I_CAL, I_ATRAS, I_LAPIZ,
       I_BANDEJA, I_AVION, I_BORRADOR, I_SPAM, I_CARPETA, I_X, I_NOLEIDO, I_CUENTA, I_DESCARGA, I_FILTRO };
static void icono(int k, int cx, int cy, uint32_t c, int s)
{
    int h = s / 2, g = s >= 18 ? 28 : 24;
    switch (k) {
    case I_SOBRE: rs(cx - h, cy - h + 3, s, s - 6, 3, c, 255); ln(cx - h + 1, cy - h + 4, cx, cy + 1, c, g); ln(cx + h - 1, cy - h + 4, cx, cy + 1, c, g); break;
    case I_SINC: { int r = h - 1; for (int a = 0; a < 5; a++) { static const int X[6] = { 7, 5, 0, -5, -7, -5 }, Y[6] = { 0, -5, -7, -5, 0, 5 }; ln(cx + X[a] * r / 7, cy + Y[a] * r / 7, cx + X[a + 1] * r / 7, cy + Y[a + 1] * r / 7, c, g); }
                   ln(cx + r, cy, cx + r - 3, cy - 3, c, g); ln(cx + r, cy, cx + r + 3, cy - 3, c, g); break; }
    case I_ESTRELLA: case I_ESTRELLA_LLENA: {
        static const int PX[10] = { 0, 3, 9, 4, 6, 0, -6, -4, -9, -3 }, PY[10] = { -9, -3, -3, 2, 9, 5, 9, 2, -3, -3 };
        for (int i = 0; i < 10; i++) ln(cx + PX[i] * h / 9, cy + PY[i] * h / 9, cx + PX[(i + 1) % 10] * h / 9, cy + PY[(i + 1) % 10] * h / 9, c, g);
        if (k == I_ESTRELLA_LLENA) { punto(cx, cy, h / 2, c, 255); for (int i = 0; i < 10; i += 2) ln(cx, cy, cx + PX[i] * h / 11, cy + PY[i] * h / 11, c, 40); }
        break; }
    case I_CLIP: ln(cx - 2, cy + h - 2, cx - 2, cy - h + 4, c, g); ln(cx - 2, cy - h + 4, cx + 2, cy - h + 2, c, g); ln(cx + 3, cy - h + 3, cx + 3, cy + h - 5, c, g);
                 ln(cx + 3, cy + h - 5, cx, cy + h - 1, c, g); ln(cx, cy + h - 1, cx - 2, cy + h - 3, c, g); break;
    case I_PAPELERA: ln(cx - h + 2, cy - h + 4, cx + h - 2, cy - h + 4, c, g); ln(cx - 2, cy - h + 2, cx + 2, cy - h + 2, c, g); rs(cx - h + 4, cy - h + 5, s - 8, s - 7, 2, c, 255);
                     ln(cx - 1, cy - 2, cx - 1, cy + h - 4, c, 18); ln(cx + 2, cy - 2, cx + 2, cy + h - 4, c, 18); break;
    case I_RESP: ln(cx - h + 2, cy - 1, cx - 2, cy - 5, c, g); ln(cx - h + 2, cy - 1, cx - 2, cy + 3, c, g); ln(cx - h + 2, cy - 1, cx + 3, cy - 1, c, g); ln(cx + 3, cy - 1, cx + h - 1, cy + h - 3, c, g); break;
    case I_RESP_TODOS: icono(I_RESP, cx + 2, cy, c, s); ln(cx - h, cy - 1, cx - 4, cy - 5, c, g); ln(cx - h, cy - 1, cx - 4, cy + 3, c, g); break;
    case I_REENV: ln(cx + h - 2, cy - 1, cx + 2, cy - 5, c, g); ln(cx + h - 2, cy - 1, cx + 2, cy + 3, c, g); ln(cx + h - 2, cy - 1, cx - 3, cy - 1, c, g); ln(cx - 3, cy - 1, cx - h + 1, cy + h - 3, c, g); break;
    case I_IA: ln(cx, cy - h, cx, cy + h, c, g); ln(cx - h, cy, cx + h, cy, c, g); ln(cx - 3, cy - 3, cx + 3, cy + 3, c, 16); ln(cx - 3, cy + 3, cx + 3, cy - 3, c, 16); punto(cx, cy, 2, c, 255);
               punto(cx + h, cy - h + 1, 1, c, 255); break;
    case I_LUPA: rs(cx - h + 1, cy - h + 1, s - 6, s - 6, (s - 6) / 2, c, 255); ln(cx + 2, cy + 2, cx + h - 1, cy + h - 1, c, 30); break;
    case I_ARCHIVO: rs(cx - h + 1, cy - h + 2, s - 2, 5, 1, c, 255); rs(cx - h + 2, cy - h + 7, s - 4, s - 9, 2, c, 255); ln(cx - 3, cy + 1, cx + 3, cy + 1, c, g); break;
    case I_CAL: rs(cx - h + 1, cy - h + 3, s - 2, s - 4, 2, c, 255); ln(cx - h + 1, cy - h + 7, cx + h - 1, cy - h + 7, c, g); ln(cx - 3, cy - h + 1, cx - 3, cy - h + 4, c, g); ln(cx + 3, cy - h + 1, cx + 3, cy - h + 4, c, g);
                punto(cx - 2, cy + 2, 1, c, 255); punto(cx + 2, cy + 2, 1, c, 255); break;
    case I_ATRAS: ln(cx + h - 2, cy, cx - h + 2, cy, c, g); ln(cx - h + 2, cy, cx - 1, cy - h + 3, c, g); ln(cx - h + 2, cy, cx - 1, cy + h - 3, c, g); break;
    case I_LAPIZ: ln(cx - h + 3, cy + h - 3, cx + h - 4, cy - h + 4, c, g); ln(cx + h - 6, cy - h + 2, cx + h - 2, cy - h + 6, c, g); ln(cx - h + 3, cy + h - 3, cx - h + 2, cy + h - 2, c, g); break;
    case I_BANDEJA: ln(cx - h + 1, cy, cx - h + 1, cy + h - 2, c, g); ln(cx - h + 1, cy + h - 2, cx + h - 1, cy + h - 2, c, g); ln(cx + h - 1, cy + h - 2, cx + h - 1, cy, c, g);
                    ln(cx - h + 1, cy, cx - 3, cy, c, g); ln(cx - 3, cy, cx - 2, cy + 3, c, g); ln(cx - 2, cy + 3, cx + 2, cy + 3, c, g); ln(cx + 2, cy + 3, cx + 3, cy, c, g); ln(cx + 3, cy, cx + h - 1, cy, c, g);
                    ln(cx, cy - h + 1, cx, cy - 3, c, g); ln(cx - 3, cy - 5, cx, cy - 2, c, g); ln(cx + 3, cy - 5, cx, cy - 2, c, g); break;
    case I_AVION: ln(cx - h + 1, cy - 1, cx + h - 1, cy - h + 2, c, g); ln(cx + h - 1, cy - h + 2, cx + 1, cy + h - 1, c, g); ln(cx + 1, cy + h - 1, cx - 1, cy + 1, c, g); ln(cx - 1, cy + 1, cx - h + 1, cy - 1, c, g); ln(cx - 1, cy + 1, cx + h - 1, cy - h + 2, c, 16); break;
    case I_BORRADOR: rs(cx - h + 3, cy - h + 1, s - 6, s - 2, 2, c, 255); ln(cx - 2, cy - 2, cx + 3, cy - 2, c, 18); ln(cx - 2, cy + 1, cx + 3, cy + 1, c, 18); ln(cx - 2, cy + 4, cx + 1, cy + 4, c, 18); break;
    case I_SPAM: rs(cx - h + 1, cy - h + 1, s - 2, s - 2, (s - 2) / 2, c, 255); ln(cx, cy - 4, cx, cy + 1, c, g); punto(cx, cy + 4, 1, c, 255); break;
    case I_CARPETA: rs(cx - h + 1, cy - h + 3, h, 4, 2, c, 255); rs(cx - h + 1, cy - h + 5, s - 2, s - 8, 2, c, 255); break;
    case I_X: ln(cx - h + 4, cy - h + 4, cx + h - 4, cy + h - 4, c, g); ln(cx - h + 4, cy + h - 4, cx + h - 4, cy - h + 4, c, g); break;
    case I_NOLEIDO: icono(I_SOBRE, cx - 1, cy + 1, c, s - 2); punto(cx + h - 1, cy - h + 2, 3, C_ACC, 255); break;
    case I_CUENTA: rs(cx - 3, cy - h + 1, 7, 7, 3, c, 255); rs(cx - h + 1, cy + 1, s - 2, h, 5, c, 255); break;
    case I_DESCARGA: ln(cx, cy - h + 2, cx, cy + 2, c, g); ln(cx - 4, cy - 2, cx, cy + 2, c, g); ln(cx + 4, cy - 2, cx, cy + 2, c, g); ln(cx - h + 2, cy + h - 3, cx + h - 2, cy + h - 3, c, g); break;
    case I_FILTRO: ln(cx - h + 1, cy - h + 3, cx + h - 1, cy - h + 3, c, g); ln(cx - h + 1, cy - h + 3, cx - 1, cy + 1, c, g); ln(cx + h - 1, cy - h + 3, cx + 1, cy + 1, c, g); ln(cx - 1, cy + 1, cx - 1, cy + h - 1, c, g); ln(cx + 1, cy + 1, cx + 1, cy + h - 2, c, g); break;
    }
}

/* ---------------------------------------------------------------- zonas de clic */
enum { A_NADA, A_FOCO, A_REDACTAR, A_CARPETA, A_VISTA, A_MSG, A_ESTRELLA, A_SINC, A_IA_TOGGLE, A_BUSCAR, A_LIMPIA, A_RESPONDER, A_RESP_TODOS, A_REENVIAR,
       A_DESTACAR, A_NOLEIDO, A_BORRAR, A_ARCHIVAR, A_RESUMEN, A_RESP_IA, A_CALENDARIO, A_ADJ, A_ENVIAR, A_DESCARTAR, A_CC, A_IA_ESCRIBE, A_IA_GENERA,
       A_MEJORA, A_CUENTA, A_CFG_PROBAR, A_CFG_CANCELA, A_CFG_AVANZADO, A_CFG_SEG, A_CLASIFICA, A_VOLVER, A_CFG_CLAVE, A_CFG_VER, A_QUITA_ADJ };
struct hit { int x, y, w, h, act; int32_t a, b; struct tfield *f; };
#define NHIT 500
static struct hit H[NHIT]; static int nH;
static int g_rx = -1, g_ry = -1;
static void hit(int x, int y, int w, int h, int act, int32_t a, int32_t b) { if (nH < NHIT && w > 0 && h > 0) { struct hit *z = &H[nH++]; z->x = x; z->y = y; z->w = w; z->h = h; z->act = act; z->a = a; z->b = b; z->f = 0; } }
static void hit_f(int x, int y, int w, int h, struct tfield *f) { hit(x, y, w, h, A_FOCO, 0, 0); if (nH) H[nH - 1].f = f; }
static int hit_en(int mx, int my) { for (int i = nH - 1; i >= 0; i--) if (mx >= H[i].x && my >= H[i].y && mx < H[i].x + H[i].w && my < H[i].y + H[i].h) return i; return -1; }
static int sobre(int x, int y, int w, int h) { return g_rx >= x && g_ry >= y && g_rx < x + w && g_ry < y + h; }
static void boton_ic(int x, int y, int s, int ic, uint32_t col, int act, int32_t a, int32_t b, const char *ayuda)
{
    int ov = sobre(x, y, s, s);
    if (ov) rra(x, y, s, s, 8, 0xFFFFFFu, 0x16);
    icono(ic, x + s / 2, y + s / 2, ov ? C_TX : col, 16);
    hit(x, y, s, s, act, a, b);
    if (ov && ayuda) { int w = tw(ayuda, 12, 0) + 16; rr(x + s / 2 - w / 2, y + s + 4, w, 24, 6, 0x0002040Au); rs(x + s / 2 - w / 2, y + s + 4, w, 24, 6, C_BDL, 255); tx(x + s / 2 - w / 2 + 8, y + s + 4, 24, ayuda, 12, C_TX2, 0); }
}
/* estilo: 0 contorno, 1 solido, 2 IA (violeta suave) */
static int boton(int x, int y, int h, int ic, const char *s, int estilo, uint32_t col, int act, int32_t a, int32_t b)
{
    int iw = ic >= 0 ? 22 : 0, w = tw(s, 13, WF_BOLD) + 24 + iw, ov = sobre(x, y, w, h);
    if (estilo == 1) rr(x, y, w, h, h / 2, ov ? mezcla(col, 0xFFFFFF, 30) : col);
    else if (estilo == 2) { rra(x, y, w, h, h / 2, C_IA, ov ? 0x40 : 0x26); rs(x, y, w, h, h / 2, C_IA, ov ? 200 : 120); }
    else { rra(x, y, w, h, h / 2, 0xFFFFFF, ov ? 0x14 : 0x06); rs(x, y, w, h, h / 2, ov ? C_BDL : C_BD, 255); }
    uint32_t tc = estilo == 1 ? 0xFFFFFFu : estilo == 2 ? 0x00DDD6FEu : C_TX;
    if (ic >= 0) icono(ic, x + 12 + 7, y + h / 2, tc, 14);
    tx(x + 12 + iw, y, h, s, 13, tc, WF_BOLD);
    hit(x, y, w, h, act, a, b);
    return w;
}
static int ancho_boton(int ic, const char *s) { return tw(s, 13, WF_BOLD) + 24 + (ic >= 0 ? 22 : 0); }

/* ---------------------------------------------------------------- campos */
static char b_busca[200]; static struct tfield f_busca;
static char b_para[600], b_cc[400], b_cco[300], b_asunto[300], b_ia[400];
static char *b_cuerpo; static struct tfield f_para, f_cc, f_cco, f_asunto, f_cuerpo, f_ia;
static char b_nombre[80], b_email[120], b_clave[160], b_imap[96], b_imap_p[8], b_smtp[96], b_smtp_p[8], b_usuario[120];
static struct tfield f_nombre, f_email, f_imap, f_imap_p, f_smtp, f_smtp_p, f_usuario;
static struct tfield *g_foco;
static int g_foco_clave;                                  /* el campo de la contrasena (no es un tfield: se ve con puntos) */
static uint8_t g_cfg_imap_tls = CO_TLS_DIRECTO, g_cfg_smtp_tls = CO_TLS_DIRECTO;
static int g_cfg, g_cfg_av, g_cfg_ver_clave, g_cfg_probando; static char g_cfg_aviso[300];

/* la redaccion */
enum { R_NUEVO, R_RESPONDER, R_RESP_TODOS, R_REENVIAR };
static int g_comp, g_comp_modo, g_cc_vis, g_ia_fila, g_enviando, g_comp_ia;   /* g_comp_ia: la IA esta escribiendo (J_*) */
static char g_comp_irt[200], g_comp_refs[600], g_comp_estado[300]; static int g_comp_err;
static int g_comp_c = -1; static uint32_t g_comp_uid;
static int g_comp_orbe;                                   /* el borrador lo preparo el orbe */
static char g_comp_adj[200]; static uint32_t g_comp_adj_tam;   /* SOMA_360_HOJA_GRAFICOS: archivo adjunto (ruta en SOMA) */
static int g_tono;
static int g_borrador_vivo;                              /* hay un borrador escondido (se abrio un correo mientras se escribia) */

/* ---------------------------------------------------------------- geometria */
struct geo { int x, y, w, h, top, sx, sw, lx, lw, rx, rw, cuerpo_y, estrecho; };
static struct geo G;
static void geometria(int x, int y, int w, int h)
{
    G.x = x; G.y = y; G.w = w; G.h = h; G.top = 56;
    G.sw = w >= 1200 ? 220 : 196; G.sx = x;
    G.lw = w >= 1400 ? 400 : w >= 1100 ? 360 : 320; G.lx = x + G.sw;
    G.rx = G.lx + G.lw; G.rw = x + w - G.rx;
    G.estrecho = G.rw < 380;
    if (G.estrecho) { G.lw = w - G.sw; G.rx = G.lx; G.rw = G.lw; }
    G.cuerpo_y = y + G.top;
}
static int vista_lectura(void) { return !G.estrecho || g_sel_c >= 0 || g_comp; }   /* en estrecho: la lectura tapa la lista */

/* ---------------------------------------------------------------- fechas en la lista */
static void fecha_corta(int64_t utc, char *d, int cap)
{
    d[0] = 0;
    if (!utc) return;
    int64_t l = utc + co_desfase(utc), hoy = ahora_local();
    int y, m, dd, h, mi, y2, m2, d2, h2, mi2; ag_desde_min(l, &y, &m, &dd, &h, &mi); ag_desde_min(hoy, &y2, &m2, &d2, &h2, &mi2);
    int64_t dia = ag_dia_de(l), dhoy = ag_dia_de(hoy);
    if (dia == dhoy) { char t[8] = { (char)('0' + h / 10), (char)('0' + h % 10), ':', (char)('0' + mi / 10), (char)('0' + mi % 10), 0 }; scpy(d, t, cap); return; }
    if (dia == dhoy - 1) { scpy(d, "ayer", cap); return; }
    if (dia > dhoy - 7 && dia < dhoy) { const char *n = ag_nombre_dia(ag_dia_semana(dia)); int k = 0, letras = 0; while (n[k] && letras < 3) { k++; while (((uint8_t)n[k] & 0xC0) == 0x80) k++; letras++; } char t[12]; co_cpyn(t, n, (uint32_t)k, sizeof t); scpy(d, t, cap); return; }
    snum(d, cap, (uint32_t)dd); scat(d, " ", cap);
    { const char *mn = ag_nombre_mes(m); char t[8]; co_cpyn(t, mn, 3, sizeof t); scat(d, t, cap); }
    if (y != y2) { scat(d, " ", cap); snum(d, cap, (uint32_t)y); }
}
static void fecha_larga(int64_t utc, char *d, int cap)
{
    d[0] = 0;
    if (!utc) return;
    int64_t l = utc + co_desfase(utc); int y, m, dd, h, mi; ag_desde_min(l, &y, &m, &dd, &h, &mi);
    scpy(d, ag_nombre_dia(ag_dia_semana(ag_dia_de(l))), cap); scat(d, ", ", cap); snum(d, cap, (uint32_t)dd); scat(d, " de ", cap); scat(d, ag_nombre_mes(m), cap);
    scat(d, " de ", cap); snum(d, cap, (uint32_t)y); scat(d, ", ", cap);
    char t[8] = { (char)('0' + h / 10), (char)('0' + h % 10), ':', (char)('0' + mi / 10), (char)('0' + mi % 10), 0 }; scat(d, t, cap);
}
static uint32_t color_de(const char *s)
{
    static const uint32_t P[8] = { 0x006366F1u, 0x0022D3EEu, 0x0010B981u, 0x00F59E0Bu, 0x00EC4899u, 0x008B5CF6u, 0x0014B8A6u, 0x00F97316u };
    return P[fnv(s) % 8];
}
static void avatar(int cx, int cy, int r, const char *nombre, const char *email)
{
    punto(cx, cy, r, color_de(email && email[0] ? email : nombre), 255);
    char ini[8] = ""; const char *n = nombre && nombre[0] ? nombre : email ? email : "?";
    while (*n == '"' || *n == ' ') n++;
    int l = 1; if (((uint8_t)*n & 0xE0) == 0xC0) l = 2; else if (((uint8_t)*n & 0xF0) == 0xE0) l = 3;
    for (int i = 0; i < l && n[i]; i++) ini[i] = n[i];
    if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
    if ((uint8_t)ini[0] == 0xC3 && (uint8_t)ini[1] >= 0xA0 && (uint8_t)ini[1] <= 0xBE) ini[1] = (char)((uint8_t)ini[1] - 0x20);
    tx(cx - tw(ini, r > 16 ? 16 : 13, WF_BOLD) / 2, cy - r, 2 * r, ini, r > 16 ? 16 : 13, 0xFFFFFFu, WF_BOLD);
}
static int icono_uso(int uso)
{
    switch (uso) { case CO_U_ENTRADA: return I_BANDEJA; case CO_U_ENVIADOS: return I_AVION; case CO_U_BORRADORES: return I_BORRADOR; case CO_U_PAPELERA: return I_PAPELERA;
                   case CO_U_SPAM: return I_SPAM; case CO_U_ARCHIVO: return I_ARCHIVO; default: return I_CARPETA; }
}
static uint32_t color_cat(int c)
{
    switch (c) { case CO_CAT_IMPORTANTE: return C_RED; case CO_CAT_PERSONAL: return C_GREEN; case CO_CAT_TRABAJO: return C_CYAN; case CO_CAT_NOTIF: return C_AMBER; case CO_CAT_PROMO: return C_ACC2; default: return C_MU; }
}

/* ---------------------------------------------------------------- la lista que se ve */
static struct res *g_lista; static int g_nlista, g_lista_cap;
static int g_lscroll, g_lmax;
static void lista_calcula(void)
{
    if (!g_lista) { g_lista_cap = CAP_ENTRADA + NRES; g_lista = (struct res *)co_alloc((uint32_t)g_lista_cap * sizeof(struct res)); if (!g_lista) return; }
    g_nlista = 0;
    if (g_vista == V_BUSQUEDA) { for (int i = 0; i < g_nres && g_nlista < g_lista_cap; i++) g_lista[g_nlista++] = g_res[i]; return; }
    int c = g_vista == V_CARPETA ? g_ci : carpeta_entrada();
    if (c < 0 || c >= g_ncarp) return;
    cache_carga(c);
    struct cache *k = &g_cache[c];
    for (int i = 0; i < k->n && g_nlista < g_lista_cap; i++) {
        const struct co_msg *m = &k->v[i];
        if (g_vista == V_IMPORTANTES && m->cat != CO_CAT_IMPORTANTE) continue;
        if (g_vista == V_NO_LEIDOS && (m->flags & CO_F_VISTO)) continue;
        if (g_vista == V_DESTACADOS && !(m->flags & CO_F_DESTACADO)) continue;
        g_lista[g_nlista].c = c; g_lista[g_nlista].uid = m->uid; g_nlista++;
    }
}

/* ---------------------------------------------------------------- el texto del correo, en lineas */
struct linea { uint32_t off; uint16_t len; uint8_t cita, _r; };
static struct linea *g_lin; static int g_nlin, g_lin_cap; static int g_lin_w; static uint32_t g_lin_uid, g_lin_n;
static int g_bscroll, g_bmax;
static void envuelve(int ancho)
{
    if (g_lin && g_lin_w == ancho && g_lin_uid == g_abierto_uid && g_lin_n == g_txt_n) return;
    g_lin_w = ancho; g_lin_uid = g_abierto_uid; g_lin_n = g_txt_n; g_nlin = 0;
    if (!g_txt) return;
    if (!g_lin) { g_lin_cap = 4096; g_lin = (struct linea *)co_alloc((uint32_t)g_lin_cap * sizeof(struct linea)); if (!g_lin) return; }
    int esp = tw(" ", 14, 0);
    for (uint32_t i = 0; i <= g_txt_n && g_nlin < g_lin_cap;) {
        uint32_t e = i; while (e < g_txt_n && g_txt[e] != '\n') e++;
        int cita = i < g_txt_n && g_txt[i] == '>';
        /* un parrafo: palabras hasta llenar el ancho */
        uint32_t ini = i, ult = i; int w = 0;
        if (e == i) { g_lin[g_nlin].off = i; g_lin[g_nlin].len = 0; g_lin[g_nlin].cita = (uint8_t)cita; g_nlin++; }
        while (i < e && g_nlin < g_lin_cap) {
            uint32_t p = i; while (p < e && g_txt[p] != ' ') p++;
            int ww = twn(g_txt + i, (int)(p - i), 14, 0);
            if (w && w + esp + ww > ancho) {
                g_lin[g_nlin].off = ini; g_lin[g_nlin].len = (uint16_t)(ult - ini); g_lin[g_nlin].cita = (uint8_t)cita; g_nlin++;
                ini = i; w = 0;
            }
            if (!w && ww > ancho) {                                          /* palabra mas larga que la linea: se parte */
                uint32_t q = i; int acc = 0;
                while (q < p) { uint32_t l = 1; uint8_t c = (uint8_t)g_txt[q]; if (c >= 0xF0) l = 4; else if (c >= 0xE0) l = 3; else if (c >= 0xC0) l = 2;
                                int cw = twn(g_txt + q, (int)l, 14, 0); if (acc + cw > ancho && q > i) break; acc += cw; q += l; }
                if (q < p) { g_lin[g_nlin].off = i; g_lin[g_nlin].len = (uint16_t)(q - i); g_lin[g_nlin].cita = (uint8_t)cita; g_nlin++; i = q; ini = q; continue; }
            }
            w += (w ? esp : 0) + ww; ult = p;
            i = p; while (i < e && g_txt[i] == ' ') i++;
        }
        if (w && g_nlin < g_lin_cap) { g_lin[g_nlin].off = ini; g_lin[g_nlin].len = (uint16_t)(ult - ini); g_lin[g_nlin].cita = (uint8_t)cita; g_nlin++; }
        i = e + 1;
        if (e >= g_txt_n) break;
    }
}

/* ---------------------------------------------------------------- partes de la ventana */
static void pinta_cabecera(void)
{
    int x = G.x, y = G.y, w = G.w;
    rr(x, y, w, G.top, 0, C_SIDE);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + G.top - 1), (uint32_t)w, 1, C_BD);
    icono(I_SOBRE, x + 24, y + G.top / 2, C_CYAN, 18);
    tx(x + 42, y, G.top, "Correo", 17, C_TX, WF_BOLD);
    /* la busqueda */
    int bx = G.lx + 16, bw = G.rx > G.lx + 200 && !G.estrecho ? G.rx + (G.rw > 500 ? 260 : 120) - bx : G.w - (bx - x) - 200;
    if (bw > 640) bw = 640;
    if (bw < 200) bw = 200;
    int bh = 36, by = y + (G.top - bh) / 2;
    { int ex = x + 42 + tw("Correo", 17, WF_BOLD) + 12; if (g_hay_cuenta && bx - 12 - ex > 60) txe(ex, y, G.top, g_c.email, 12, C_MU, 0, bx - 12 - ex); }
    int foco = g_foco == &f_busca;
    rr(bx, by, bw, bh, bh / 2, C_INP); rs(bx, by, bw, bh, bh / 2, foco ? (g_busca_ia ? C_IA : C_ACC) : C_BD, 255);
    icono(I_LUPA, bx + 18, by + bh / 2, C_MU, 15);
    int iaw = 64;
    f_busca.bare = 1;
    tf_render(&f_busca, bx + 32, by + 4, bw - 32 - iaw - 26, bh - 8, foco, g_busca_ia ? "Busca con IA: \342\200\234el presupuesto de la reforma\342\200\235" : "Buscar: de:ana asunto:factura tiene:adjunto", C_TX);
    hit_f(bx, by, bw - iaw - 26, bh, &f_busca);
    if (b_busca[0] || g_vista == V_BUSQUEDA) { boton_ic(bx + bw - iaw - 30, by + 4, 28, I_X, C_MU, A_LIMPIA, 0, 0, 0); }
    { int ix = bx + bw - iaw - 4, iy = by + 5, ih = bh - 10, on = g_busca_ia, ov = sobre(ix, iy, iaw, ih);
      if (on) { rra(ix, iy, iaw, ih, ih / 2, C_IA, ov ? 0x70 : 0x50); } else rra(ix, iy, iaw, ih, ih / 2, 0xFFFFFF, ov ? 0x18 : 0x0A);
      icono(I_IA, ix + 14, iy + ih / 2, on ? 0x00EDE9FEu : C_MU, 13); tx(ix + 26, iy, ih, "IA", 13, on ? 0x00EDE9FEu : C_MU, WF_BOLD);
      hit(ix, iy, iaw, ih, A_IA_TOGGLE, 0, 0); }
    /* estado y sincronizar */
    int sx = x + w - 52;
    boton_ic(sx, y + (G.top - 36) / 2, 36, I_SINC, g_ocupado ? C_CYAN : C_TX2, A_SINC, 0, 0, "Sincronizar");
    const char *st = g_estado[0] ? g_estado : g_sinc_err && g_err_conexion[0] ? "Sin conexi\303\263n con el servidor" : "";
    if (st[0] && bx + bw + 20 < sx - 40) { int sw2 = sx - 12 - (bx + bw + 20); txe(sx - 12 - (tw(st, 12, 0) < sw2 ? tw(st, 12, 0) : sw2), y, G.top, st, 12, g_sinc_err && !g_estado[0] ? C_AMBER : C_MU, 0, sw2); }
}
static void pinta_lateral(void)
{
    int x = G.sx, y = G.cuerpo_y, w = G.sw, h = G.h - G.top;
    rr(x, y, w, h, 0, C_SIDE);
    fb_fill_rect((uint32_t)(x + w - 1), (uint32_t)y, 1, (uint32_t)h, C_BD);
    int bw = w - 28; int bx = x + 14, by = y + 16;
    { int ov = sobre(bx, by, bw, 42); rr(bx, by, bw, 42, 14, ov ? mezcla(C_ACC, 0xFFFFFF, 30) : C_ACC); icono(I_LAPIZ, bx + 22, by + 21, 0xFFFFFFu, 16); tx(bx + 40, by, 42, "Redactar", 14, 0xFFFFFFu, WF_BOLD); hit(bx, by, bw, 42, A_REDACTAR, 0, 0); }
    int yy = by + 58;
    tx(x + 18, yy, 18, "CARPETAS", 11, C_FAINT, WF_BOLD); yy += 24;
    for (int i = 0; i < g_ncarp && yy < y + h - 160; i++) {
        int sel = g_vista == V_CARPETA && g_ci == i, ov = sobre(x + 8, yy, w - 16, 34);
        if (sel) rr(x + 8, yy, w - 16, 34, 10, C_SEL); else if (ov) rr(x + 8, yy, w - 16, 34, 10, C_SIDEH);
        icono(icono_uso(g_carp[i].uso), x + 28, yy + 17, sel ? C_TX : C_TX2, 16);
        char cnt[12] = ""; if (g_carp[i].no_leidos && (g_carp[i].uso == CO_U_ENTRADA || g_carp[i].uso == CO_U_NADA || g_carp[i].uso == CO_U_SPAM)) snum(cnt, sizeof cnt, g_carp[i].no_leidos);
        int cw = cnt[0] ? tw(cnt, 12, WF_BOLD) + 14 : 0;
        txe(x + 46, yy, 34, g_carp[i].nombre, 14, sel ? C_TX : C_TX2, sel ? WF_BOLD : 0, w - 70 - cw);
        if (cnt[0]) { rr(x + w - 20 - cw, yy + 8, cw, 18, 9, sel ? C_ACC : C_BDL); tx(x + w - 20 - cw + 7, yy + 8, 18, cnt, 12, C_TX, WF_BOLD); }
        hit(x + 8, yy, w - 16, 34, A_CARPETA, i, 0);
        yy += 36;
    }
    yy += 10;
    tx(x + 18, yy, 18, "VISTAS CON IA", 11, C_FAINT, WF_BOLD); yy += 24;
    static const struct { int v; int ic; const char *n; } VI[3] = { { V_IMPORTANTES, I_IA, "Importantes" }, { V_NO_LEIDOS, I_NOLEIDO, "Sin leer" }, { V_DESTACADOS, I_ESTRELLA, "Destacados" } };
    for (int i = 0; i < 3 && yy < y + h - 70; i++) {
        int sel = g_vista == VI[i].v, ov = sobre(x + 8, yy, w - 16, 34);
        if (sel) rr(x + 8, yy, w - 16, 34, 10, C_SEL); else if (ov) rr(x + 8, yy, w - 16, 34, 10, C_SIDEH);
        icono(VI[i].ic, x + 28, yy + 17, i == 0 ? C_IA : sel ? C_TX : C_TX2, 16);
        tx(x + 46, yy, 34, VI[i].n, 14, sel ? C_TX : C_TX2, sel ? WF_BOLD : 0);
        hit(x + 8, yy, w - 16, 34, A_VISTA, VI[i].v, 0);
        yy += 36;
    }
    /* la cuenta, abajo */
    int cy = y + h - 56, ov = sobre(x + 8, cy, w - 16, 44);
    if (ov) rr(x + 8, cy, w - 16, 44, 10, C_SIDEH);
    if (g_hay_cuenta) { avatar(x + 30, cy + 22, 14, g_c.nombre, g_c.email); txe(x + 52, cy + 4, 20, g_c.nombre[0] ? g_c.nombre : g_c.email, 13, C_TX, WF_BOLD, w - 70); tx(x + 52, cy + 22, 18, "Cuenta y servidores", 11, C_MU, 0); }
    else { icono(I_CUENTA, x + 30, cy + 22, C_TX2, 16); tx(x + 52, cy, 44, "A\303\261adir cuenta", 13, C_TX, WF_BOLD); }
    hit(x + 8, cy, w - 16, 44, A_CUENTA, 0, 0);
}
static const char *titulo_vista(void)
{
    switch (g_vista) { case V_IMPORTANTES: return "Importantes"; case V_NO_LEIDOS: return "Sin leer"; case V_DESTACADOS: return "Destacados"; case V_BUSQUEDA: return "Resultados";
                       default: return g_ci < g_ncarp ? g_carp[g_ci].nombre : "Recibidos"; }
}
#define FILA_H 76
static void pinta_lista(void)
{
    int x = G.lx, y = G.cuerpo_y, w = G.lw, h = G.h - G.top;
    rr(x, y, w, h, 0, C_BG);
    if (!G.estrecho) fb_fill_rect((uint32_t)(x + w - 1), (uint32_t)y, 1, (uint32_t)h, C_BD);
    lista_calcula();
    /* titulo */
    tx(x + 18, y + 8, 30, titulo_vista(), 17, C_TX, WF_BOLD);
    char n[40] = ""; snum(n, sizeof n, (uint32_t)g_nlista); scat(n, g_nlista == 1 ? " correo" : " correos", sizeof n);
    tx(x + 18, y + 36, 18, n, 12, C_MU, 0);
    if (g_vista == V_IMPORTANTES || g_vista == V_CARPETA) {
        const char *s = "Clasificar"; int bw = ancho_boton(I_IA, s);
        boton(x + w - bw - 14, y + 14, 30, I_IA, s, 2, 0, A_CLASIFICA, 0, 0);
    }
    int ly = y + 62;
    if (g_vista == V_BUSQUEDA) {
        rra(x + 12, ly, w - 24, 46, 10, g_busca_ia ? C_IA : C_ACC, 0x1C);
        icono(g_busca_ia ? I_IA : I_FILTRO, x + 30, ly + 23, g_busca_ia ? C_IA : C_ACC, 15);
        if (g_busca_estado == 1) tx(x + 46, ly, 46, g_busca_ia ? "La IA est\303\241 buscando\342\200\246" : "Buscando\342\200\246", 13, C_TX2, 0);
        else { txe(x + 46, ly + 4, 20, g_busca_txt, 13, C_TX, WF_BOLD, w - 70); txe(x + 46, ly + 22, 18, g_busca_expl, 11, C_MU, 0, w - 70); }
        ly += 56;
    }
    fb_fill_rect((uint32_t)x, (uint32_t)(ly - 1), (uint32_t)w, 1, C_BD);
    int vh = y + h - ly;
    g_lmax = g_nlista * FILA_H - vh; if (g_lmax < 0) g_lmax = 0;
    if (g_lscroll > g_lmax) g_lscroll = g_lmax;
    if (g_lscroll < 0) g_lscroll = 0;
    fb_set_clip(x, ly, w, vh);
    if (!g_nlista) {
        const char *t = !g_hay_cuenta ? "A\303\261ade tu cuenta para ver el correo" : g_ocupado ? "Cargando\342\200\246" : g_vista == V_BUSQUEDA ? (g_busca_estado == 1 ? "" : "No hay correos que encajen") :
                        g_vista == V_IMPORTANTES ? "Nada importante (o a\303\272n sin clasificar: pulsa Clasificar)" : "No hay correos aqu\303\255";
        tx(x + (w - tw(t, 13, 0)) / 2, ly + 40, 30, t, 13, C_MU, 0);
    }
    int i0 = g_lscroll / FILA_H;
    for (int i = i0; i < g_nlista; i++) {
        int ry = ly + i * FILA_H - g_lscroll;
        if (ry > y + h) break;
        struct co_msg *m = msg_de(g_lista[i].c, g_lista[i].uid);
        if (!m) continue;
        int sel = g_sel_c == g_lista[i].c && g_sel_uid == m->uid, ov = sobre(x, ry, w, FILA_H) && g_ry >= ly, nl = !(m->flags & CO_F_VISTO);
        if (sel) rr(x + 6, ry + 3, w - 12, FILA_H - 6, 12, C_SEL); else if (ov) rr(x + 6, ry + 3, w - 12, FILA_H - 6, 12, C_SIDEH);
        if (nl) punto(x + 12, ry + 26, 3, C_ACC, 255);
        avatar(x + 38, ry + 30, 16, m->de, m->de_email);
        char fe[24]; fecha_corta(m->fecha, fe, sizeof fe);
        int fw = tw(fe, 12, nl ? WF_BOLD : 0);
        int tx0 = x + 64, tw0 = w - 64 - 16;
        const char *quien = m->de[0] ? m->de : m->de_email;
        char para[200];
        if (g_lista[i].c >= 0 && g_lista[i].c < g_ncarp && (g_carp[g_lista[i].c].uso == CO_U_ENVIADOS || g_carp[g_lista[i].c].uso == CO_U_BORRADORES)) {
            struct co_dir d[1]; scpy(para, "Para: ", sizeof para);
            if (co_dirs(m->para, d, 1) == 1) scat(para, d[0].nombre[0] ? d[0].nombre : d[0].email, sizeof para); else scat(para, m->para, sizeof para);
            quien = para;
        }
        txe(tx0, ry + 10, 20, quien, 14, nl ? C_TX : C_TX2, nl ? WF_BOLD : 0, tw0 - fw - 10);
        tx(x + w - 16 - fw, ry + 10, 20, fe, 12, nl ? C_CYAN : C_MU, nl ? WF_BOLD : 0);
        int ax = tx0, aw = tw0;
        if (m->flags & CO_F_DESTACADO) { icono(I_ESTRELLA_LLENA, x + w - 24, ry + 52, C_AMBER, 14); aw -= 22; }
        if (m->adj) { icono(I_CLIP, x + w - 24 - ((m->flags & CO_F_DESTACADO) ? 22 : 0), ry + 52, C_MU, 14); aw -= 20; }
        if (m->cat && m->cat != CO_CAT_PERSONAL) {
            const char *cn = co_cat_nombre(m->cat); int cw = tw(cn, 10, WF_BOLD) + 12;
            rra(ax, ry + 32, cw, 16, 8, color_cat(m->cat), 0x30); tx(ax + 6, ry + 32, 16, cn, 10, mezcla(color_cat(m->cat), 0xFFFFFF, 90), WF_BOLD);
            ax += cw + 6; aw -= cw + 6;
        }
        txe(ax, ry + 30, 20, m->asunto[0] ? m->asunto : "(sin asunto)", 13, nl ? C_TX : C_TX2, nl ? WF_BOLD : 0, aw);
        txe(tx0, ry + 50, 18, m->extracto, 12, C_MU, 0, tw0 - ((m->flags & CO_F_DESTACADO) ? 22 : 0) - (m->adj ? 20 : 0));
        hit(x, ry, w, FILA_H, A_MSG, g_lista[i].c, (int32_t)m->uid);
    }
    fb_reset_clip();
}
static void pinta_vacia(int x, int y, int w, int h)
{
    rr(x, y, w, h, 0, C_BG);
    icono(I_SOBRE, x + w / 2, y + h / 2 - 50, C_BDL, 48);
    const char *t = g_hay_cuenta ? "Elige un correo para leerlo" : "Correo de SOMA";
    tx(x + (w - tw(t, 16, WF_BOLD)) / 2, y + h / 2 - 10, 26, t, 16, C_TX2, WF_BOLD);
    const char *s = g_hay_cuenta ? "o p\303\255deselo al orbe: \342\200\234escribe un correo a Ana dici\303\251ndole que llego tarde\342\200\235" : "A\303\261ade tu cuenta (Gmail, iCloud, Yahoo, GMX, Zoho o la tuya) para empezar";
    tx(x + (w - tw(s, 12, 0)) / 2, y + h / 2 + 18, 20, s, 12, C_MU, 0);
    if (!g_hay_cuenta) { int bw = ancho_boton(I_CUENTA, "A\303\261adir cuenta"); boton(x + (w - bw) / 2, y + h / 2 + 52, 38, I_CUENTA, "A\303\261adir cuenta", 1, C_ACC, A_CUENTA, 0, 0); }
}
static void pinta_lectura(void)
{
    int x = G.rx, y = G.cuerpo_y, w = G.rw, h = G.h - G.top;
    if (g_sel_c < 0) { pinta_vacia(x, y, w, h); return; }
    rr(x, y, w, h, 0, C_BG);
    struct co_msg *m = msg_de(g_sel_c, g_sel_uid);
    int pad = w > 700 ? 32 : 20, cx = x + pad, cw = w - 2 * pad;
    int yy = y + 12;
    /* barra de acciones */
    int bx = cx;
    if (G.estrecho) { boton_ic(bx, yy, 34, I_ATRAS, C_TX2, A_VOLVER, 0, 0, "Volver a la lista"); bx += 40; }
    boton_ic(bx, yy, 34, I_RESP, C_TX2, A_RESPONDER, 0, 0, "Responder"); bx += 38;
    boton_ic(bx, yy, 34, I_RESP_TODOS, C_TX2, A_RESP_TODOS, 0, 0, "Responder a todos"); bx += 38;
    boton_ic(bx, yy, 34, I_REENV, C_TX2, A_REENVIAR, 0, 0, "Reenviar"); bx += 46;
    int dest = m && (m->flags & CO_F_DESTACADO);
    boton_ic(bx, yy, 34, dest ? I_ESTRELLA_LLENA : I_ESTRELLA, dest ? C_AMBER : C_TX2, A_DESTACAR, 0, 0, dest ? "Quitar la estrella" : "Destacar"); bx += 38;
    boton_ic(bx, yy, 34, I_NOLEIDO, C_TX2, A_NOLEIDO, 0, 0, "Marcar como no le\303\255do"); bx += 38;
    if ((carpeta_uso(CO_U_ARCHIVO) >= 0 && g_carp[g_sel_c].uso != CO_U_ARCHIVO) || ((g_c.gmail || g_s.gmail) && g_carp[g_sel_c].uso == CO_U_ENTRADA)) { boton_ic(bx, yy, 34, I_ARCHIVO, C_TX2, A_ARCHIVAR, 0, 0, "Archivar"); bx += 38; }
    boton_ic(bx, yy, 34, I_PAPELERA, C_TX2, A_BORRAR, 0, 0, g_carp[g_sel_c].uso == CO_U_PAPELERA ? "Borrar para siempre" : "A la papelera"); bx += 38;
    { const char *s = "A\303\261adir al calendario"; int bw = ancho_boton(I_CAL, s); if (bx + 16 + bw < x + w - pad) boton(x + w - pad - bw, yy + 2, 30, I_CAL, s, 2, 0, A_CALENDARIO, 0, 0); else boton_ic(x + w - pad - 34, yy, 34, I_CAL, C_IA, A_CALENDARIO, 0, 0, s); }
    yy += 50;
    fb_fill_rect((uint32_t)x, (uint32_t)(yy - 4), (uint32_t)w, 1, C_BD);
    if (!m) { tx(cx, yy + 20, 24, "Este correo ya no est\303\241 en la carpeta", 14, C_MU, 0); return; }
    /* asunto y remitente */
    fb_set_clip(x, yy, w, y + h - yy);
    const char *as = g_mime && g_abierto_uid == m->uid && g_mime->asunto[0] ? g_mime->asunto : m->asunto[0] ? m->asunto : "(sin asunto)";
    txe(cx, yy + 8, 30, as, 20, C_TX, WF_BOLD, cw);
    yy += 46;
    avatar(cx + 20, yy + 20, 20, m->de, m->de_email);
    txe(cx + 50, yy, 22, m->de[0] ? m->de : m->de_email, 14, C_TX, WF_BOLD, cw - 250);
    { char t[140] = "<"; scat(t, m->de_email, sizeof t); scat(t, ">", sizeof t); txe(cx + 50 + tw(m->de[0] ? m->de : m->de_email, 14, WF_BOLD) + 8, yy, 22, t, 12, C_MU, 0, cw - 260 - tw(m->de, 14, WF_BOLD)); }
    char fe[80]; fecha_larga(m->fecha, fe, sizeof fe);
    tx(x + w - pad - tw(fe, 12, 0), yy, 22, fe, 12, C_MU, 0);
    { char t[700] = "Para: "; if (g_mime && g_abierto_uid == m->uid) { struct co_dir d[8]; int nd = co_dirs(g_mime->para, d, 8); for (int i = 0; i < nd; i++) { if (i) scat(t, ", ", sizeof t); scat(t, d[i].nombre[0] ? d[i].nombre : d[i].email, sizeof t); }
                                                         if (g_mime->cc[0]) { nd = co_dirs(g_mime->cc, d, 8); scat(t, "  \302\267  Cc: ", sizeof t); for (int i = 0; i < nd; i++) { if (i) scat(t, ", ", sizeof t); scat(t, d[i].nombre[0] ? d[i].nombre : d[i].email, sizeof t); } } }
      else scat(t, m->para, sizeof t);
      txe(cx + 50, yy + 22, 20, t, 12, C_MU, 0, cw - 50); }
    yy += 54;
    /* la IA: resumen y respuesta */
    struct resumen *s = resumen_de(g_sel_c, m->uid);
    {
        int bx2 = cx;
        bx2 += boton(bx2, yy, 32, I_IA, s && s->estado == 1 ? "Resumiendo\342\200\246" : "Resumir", 2, 0, A_RESUMEN, 0, 0) + 8;
        bx2 += boton(bx2, yy, 32, I_IA, "Responder con IA", 2, 0, A_RESP_IA, 0, 0) + 8;
        (void)bx2;
        yy += 44;
    }
    if (s && s->estado >= 1) {
        int lines = 1; const char *t = s->estado == 1 ? "La IA est\303\241 leyendo el correo\342\200\246" : s->t;
        /* el resumen, en lineas sencillas */
        char buf[1400]; scpy(buf, t, sizeof buf);
        int ah = 0; { int esp = tw(" ", 13, 0), w0 = 0; lines = 1; for (char *p = buf; *p;) { char *e = p; while (*e && *e != ' ' && *e != '\n') e++; int ww = twn(p, (int)(e - p), 13, 0); if (w0 && w0 + esp + ww > cw - 44) { lines++; w0 = 0; } w0 += (w0 ? esp : 0) + ww; if (*e == '\n') { lines++; w0 = 0; } p = *e ? e + 1 : e; } }
        ah = 40 + lines * 20;
        rra(cx, yy, cw, ah, 12, C_IA, 0x1A); rs(cx, yy, cw, ah, 12, C_IA, 90);
        icono(I_IA, cx + 18, yy + 18, C_IA, 14); tx(cx + 32, yy + 6, 24, s->estado == -1 ? "No se pudo resumir" : "Resumen de la IA", 12, 0x00DDD6FEu, WF_BOLD);
        int ly2 = yy + 30, w0 = 0, esp = tw(" ", 13, 0), lx = cx + 18;
        for (char *p = buf; *p;) {
            char *e = p; while (*e && *e != ' ' && *e != '\n') e++;
            int ww = twn(p, (int)(e - p), 13, 0);
            if (w0 && w0 + esp + ww > cw - 44) { ly2 += 20; w0 = 0; }
            txn(lx + w0 + (w0 ? esp : 0), ly2, 20, p, (int)(e - p), 13, s->estado == -1 ? C_AMBER : C_TX2, 0);
            w0 += (w0 ? esp : 0) + ww;
            if (*e == '\n') { ly2 += 20; w0 = 0; }
            p = *e ? e + 1 : e;
        }
        yy += ah + 12;
    }
    /* el cuerpo */
    int by = yy, bh = y + h - by - 8;
    if (!g_mime || g_abierto_uid != m->uid || g_abierto_c != g_sel_c) {
        tx(cx, by + 10, 24, g_abriendo ? "Abriendo\342\200\246" : m->extracto, 13, C_MU, 0);
        fb_reset_clip(); return;
    }
    envuelve(cw - 8);
    int nadj = 0; for (int i = 0; i < g_mime->nadj; i++) if (!g_mime->adj[i].en_linea) nadj++;
    int extra = (nadj ? 24 + ((nadj + 2) / 3) * 52 : 0) + (g_raw_cortado ? 30 : 0) + 30;
    g_bmax = g_nlin * 20 + extra - bh; if (g_bmax < 0) g_bmax = 0;
    if (g_bscroll > g_bmax) g_bscroll = g_bmax;
    fb_set_clip(x, by, w, bh);
    int ly = by - g_bscroll;
    int i0 = g_bscroll / 20; if (i0 > g_nlin) i0 = g_nlin;
    for (int i = i0; i < g_nlin; i++) {
        int ry = ly + i * 20;
        if (ry > by + bh) break;
        struct linea *l = &g_lin[i];
        if (l->cita) fb_fill_rect((uint32_t)cx, (uint32_t)ry, 3, 20, C_BDL);
        txn(cx + (l->cita ? 10 : 0), ry, 20, g_txt + l->off, l->len, 14, l->cita ? C_MU : C_TX2, 0);
    }
    int ay = ly + g_nlin * 20 + 16;
    if (g_raw_cortado) { tx(cx, ay, 22, "El correo es muy grande: se muestra el principio (los adjuntos del final no se pueden guardar).", 12, C_AMBER, 0); ay += 30; }
    if (nadj) {
        char t[40] = ""; snum(t, sizeof t, (uint32_t)nadj); scat(t, nadj == 1 ? " ADJUNTO" : " ADJUNTOS", sizeof t);
        tx(cx, ay, 18, t, 11, C_FAINT, WF_BOLD); ay += 24;
        int aw = (cw - 16) / 3; if (aw > 260) aw = 260;
        int k = 0;
        for (int i = 0; i < g_mime->nadj; i++) {
            if (g_mime->adj[i].en_linea) continue;
            int ax = cx + (k % 3) * (aw + 8), ay2 = ay + (k / 3) * 52, ov = sobre(ax, ay2, aw, 44);
            rr(ax, ay2, aw, 44, 10, ov ? C_CARD2 : C_CARD); rs(ax, ay2, aw, 44, 10, C_BD, 255);
            icono(I_CLIP, ax + 18, ay2 + 22, C_CYAN, 15);
            txe(ax + 34, ay2 + 4, 20, g_mime->adj[i].nombre, 13, C_TX, WF_BOLD, aw - 70);
            char tm[32] = ""; uint32_t b = g_mime->adj[i].tam; if (b >= 1048576) { snum(tm, sizeof tm, b / 1048576); scat(tm, ",", sizeof tm); snum(tm, sizeof tm, (b % 1048576) * 10 / 1048576); scat(tm, " MB", sizeof tm); } else { snum(tm, sizeof tm, (b + 1023) / 1024); scat(tm, " KB", sizeof tm); }
            tx(ax + 34, ay2 + 22, 18, tm, 11, C_MU, 0);
            icono(I_DESCARGA, ax + aw - 20, ay2 + 22, ov ? C_TX : C_MU, 15);
            hit(ax, ay2, aw, 44, A_ADJ, i, 0);
            k++;
        }
    }
    fb_reset_clip();
}
static void campo_ui(int x, int y, int w, const char *etq, struct tfield *f, const char *ph)
{
    int lw = etq[0] ? 64 : 4;
    if (etq[0]) tx(x, y, 34, etq, 13, C_MU, 0);
    int ex = x + lw;
    f->bare = 1;
    tf_render(f, ex, y + 5, w - lw, 24, g_foco == f, ph, C_TX);
    hit_f(ex - 4, y, w - lw + 4, 34, f);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + 34), (uint32_t)w, 1, g_foco == f ? C_ACC : C_BD);
}
static void pinta_redaccion(void)
{
    int x = G.rx, y = G.cuerpo_y, w = G.rw, h = G.h - G.top;
    rr(x, y, w, h, 0, C_BG);
    int pad = w > 700 ? 28 : 18, cx = x + pad, cw = w - 2 * pad;
    int yy = y + 14;
    if (G.estrecho) boton_ic(cx - 6, yy, 34, I_ATRAS, C_TX2, A_DESCARTAR, 0, 0, "Volver");
    static const char *const T[4] = { "Nuevo correo", "Responder", "Responder a todos", "Reenviar" };
    tx(cx + (G.estrecho ? 34 : 0), yy, 34, T[g_comp_modo & 3], 17, C_TX, WF_BOLD);
    if (g_comp_orbe) { const char *o = "Preparado por el orbe: rev\303\255salo y env\303\255alo"; int ow = tw(o, 11, WF_BOLD) + 20; rra(cx + 200, yy + 7, ow, 22, 11, C_CYAN, 0x26); tx(cx + 210, yy + 7, 22, o, 11, C_CYAN, WF_BOLD); }
    boton_ic(x + w - pad - 34, yy, 34, I_X, C_MU, A_DESCARTAR, 0, 0, "Descartar");
    yy += 44;
    campo_ui(cx, yy, cw - (g_cc_vis ? 0 : 70), "Para", &f_para, "nombre@correo.com, otra@correo.com");
    if (!g_cc_vis) { int ov = sobre(cx + cw - 64, yy + 4, 64, 26); tx(cx + cw - 58, yy + 4, 26, "Cc/Cco", 12, ov ? C_TX : C_MU, WF_BOLD); hit(cx + cw - 64, yy + 4, 64, 26, A_CC, 0, 0); }
    yy += 38;
    if (g_cc_vis) { campo_ui(cx, yy, cw, "Cc", &f_cc, ""); yy += 38; campo_ui(cx, yy, cw, "Cco", &f_cco, "copia oculta"); yy += 38; }
    campo_ui(cx, yy, cw, "Asunto", &f_asunto, "");
    yy += 46;
    if (g_comp_adj[0]) {                                                /* SOMA_360_HOJA_GRAFICOS: el adjunto */
        const char *b = g_comp_adj; for (const char *z = g_comp_adj; *z; ++z) if (*z == '/') b = z + 1;
        char t[260]; scpy(t, b, sizeof t); scat(t, "   \302\267   ", sizeof t);
        { char n[24] = ""; uint32_t kb = (g_comp_adj_tam + 1023u) / 1024u; co_num(n, sizeof n, kb > 1024 ? (kb + 1023u) / 1024u : kb); scat(t, n, sizeof t); scat(t, kb > 1024 ? " MB" : " KB", sizeof t); }
        int aw = tw(t, 13, 0) + 74; if (aw > cw) aw = cw;
        rr(cx, yy, aw, 34, 10, C_CARD); rs(cx, yy, aw, 34, 10, C_BD, 255);
        icono(I_CLIP, cx + 18, yy + 17, C_CYAN, 14); txe(cx + 34, yy, 34, t, 13, C_TX, 0, aw - 70);
        { int ov = sobre(cx + aw - 30, yy + 5, 24, 24); icono(I_X, cx + aw - 18, yy + 17, ov ? C_TX : C_MU, 12); hit(cx + aw - 30, yy + 5, 24, 24, A_QUITA_ADJ, 0, 0); }
        yy += 44;
    }
    /* la fila de la IA */
    if (g_ia_fila) {
        rra(cx, yy, cw, 44, 12, C_IA, 0x1A); rs(cx, yy, cw, 44, 12, C_IA, 110);
        icono(I_IA, cx + 18, yy + 22, C_IA, 15);
        f_ia.bare = 1;
        int gw = ancho_boton(-1, "Escribir");
        tf_render(&f_ia, cx + 34, yy + 10, cw - 34 - gw - 18, 24, g_foco == &f_ia, "\302\277Qu\303\251 quieres decir? \342\200\234dile que el lunes no puedo y prop\303\263n el martes\342\200\235", C_TX);
        hit_f(cx + 30, yy, cw - gw - 40, 44, &f_ia);
        boton(cx + cw - gw - 8, yy + 7, 30, -1, g_comp_ia ? "Escribiendo\342\200\246" : "Escribir", 1, C_ACC2, A_IA_GENERA, 0, 0);
        yy += 54;
    }
    /* el cuerpo */
    int bot = 58 + (g_comp_estado[0] ? 30 : 0), bh = y + h - yy - bot;
    rr(cx, yy, cw, bh, 12, C_CARD); rs(cx, yy, cw, bh, 12, g_foco == &f_cuerpo ? C_BDL : C_BD, 255);
    f_cuerpo.bare = 1;
    tf_render(&f_cuerpo, cx + 12, yy + 10, cw - 24, bh - 20, g_foco == &f_cuerpo, g_comp_ia ? "La IA est\303\241 escribiendo\342\200\246" : "Escribe tu mensaje", C_TX);
    hit_f(cx, yy, cw, bh, &f_cuerpo);
    yy += bh + 10 + (g_comp_estado[0] ? 30 : 0);
    /* la barra de abajo */
    int bx = cx;
    { int ov = sobre(bx, yy, 112, 38), on = !g_enviando; rr(bx, yy, 112, 38, 19, on ? (ov ? mezcla(C_ACC, 0xFFFFFF, 30) : C_ACC) : C_BDL);
      icono(I_AVION, bx + 22, yy + 19, 0xFFFFFFu, 15); tx(bx + 38, yy, 38, g_enviando ? "Enviando" : "Enviar", 14, 0xFFFFFFu, WF_BOLD); hit(bx, yy, 112, 38, A_ENVIAR, 0, 0); }
    bx += 124;
    bx += boton(bx, yy + 4, 30, I_IA, "Escribir con IA", 2, 0, A_IA_ESCRIBE, 0, 0) + 8;
    static const char *const M[5] = { "Corregir", "Formal", "Cercano", "Breve", "Ingl\303\251s" };
    for (int i = 0; i < 5; i++) { int bw = ancho_boton(-1, M[i]); if (bx + bw > x + w - pad) break; bx += boton(bx, yy + 4, 30, -1, M[i], 2, 0, A_MEJORA, i, 0) + 6; }
    if (g_comp_estado[0]) {
        int sw = cw; int ty = yy - 32;
        rra(cx, ty, tw(g_comp_estado, 12, 0) + 20 < sw ? tw(g_comp_estado, 12, 0) + 20 : sw, 24, 8, g_comp_err ? C_RED : C_GREEN, 0x26);
        txe(cx + 10, ty, 24, g_comp_estado, 12, g_comp_err ? 0x00FCA5A5u : 0x0086EFACu, 0, sw - 20);
    }
}
static void pinta_cuenta(void)
{
    int W = G.w, Hh = G.h;
    fb_fill_rect_alpha(G.x, G.y, W, Hh, 0xB0000000u);
    int w = W > 760 ? 620 : W - 40, h = g_cfg_av ? 620 : 470; if (h > Hh - 40) h = Hh - 40;
    int x = G.x + (W - w) / 2, y = G.y + (Hh - h) / 2;
    rr(x, y, w, h, 18, C_CARD); rs(x, y, w, h, 18, C_BDL, 255);
    int cx = x + 28, cw = w - 56, yy = y + 20;
    icono(I_SOBRE, cx + 12, yy + 16, C_CYAN, 20);
    tx(cx + 32, yy, 32, g_hay_cuenta ? "Tu cuenta de correo" : "A\303\261adir una cuenta de correo", 18, C_TX, WF_BOLD);
    if (g_hay_cuenta) boton_ic(x + w - 50, yy, 32, I_X, C_MU, A_CFG_CANCELA, 0, 0, 0);
    yy += 44;
    campo_ui(cx, yy, cw, "Nombre", &f_nombre, "Como quieres que te vean (Jos\303\251 Ram\303\263n)"); yy += 40;
    campo_ui(cx, yy, cw, "Correo", &f_email, "tu@gmail.com"); yy += 40;
    /* la contrasena (con puntos) */
    tx(cx, yy, 34, "Clave", 13, C_MU, 0);
    { char pts[200] = ""; int n = 0; for (const char *p = b_clave; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) n++;
      if (g_cfg_ver_clave) scpy(pts, b_clave, sizeof pts); else for (int i = 0; i < n && i < 60; i++) scat(pts, "\342\200\242", sizeof pts);
      if (!n && g_foco_clave) {} else if (!n) tx(cx + 64, yy + 5, 24, "Contrase\303\261a de aplicaci\303\263n", 13, C_FAINT, 0);
      tx(cx + 64, yy + 5, 24, pts, 13, C_TX, 0);
      if (g_foco_clave && ((ms() / 500) & 1)) fb_fill_rect((uint32_t)(cx + 64 + tw(pts, 13, 0) + 1), (uint32_t)(yy + 9), 2, 16, C_TX);
      fb_fill_rect((uint32_t)cx, (uint32_t)(yy + 34), (uint32_t)cw, 1, g_foco_clave ? C_ACC : C_BD);
      hit(cx + 60, yy, cw - 120, 34, A_CFG_CLAVE, 0, 0);
      int ov = sobre(cx + cw - 50, yy + 4, 50, 26); tx(cx + cw - 44, yy + 4, 26, g_cfg_ver_clave ? "Ocultar" : "Ver", 12, ov ? C_TX : C_MU, WF_BOLD); hit(cx + cw - 50, yy + 4, 50, 26, A_CFG_VER, 0, 0); }
    yy += 44;
    /* lo que se sabe del proveedor */
    if (g_cfg_aviso[0]) {
        int lines = tw(g_cfg_aviso, 12, 0) / (cw - 40) + 1; int ah = 16 + lines * 18;
        rra(cx, yy, cw, ah, 10, C_AMBER, 0x1C);
        char b[300]; scpy(b, g_cfg_aviso, sizeof b); int ly = yy + 8, w0 = 0, esp = tw(" ", 12, 0);
        for (char *p = b; *p;) { char *e = p; while (*e && *e != ' ') e++; int ww = twn(p, (int)(e - p), 12, 0); if (w0 && w0 + esp + ww > cw - 24) { ly += 18; w0 = 0; } txn(cx + 12 + w0 + (w0 ? esp : 0), ly, 18, p, (int)(e - p), 12, 0x00FCD34Du, 0); w0 += (w0 ? esp : 0) + ww; p = *e ? e + 1 : e; }
        yy += ah + 10;
    }
    { int ov = sobre(cx, yy, 260, 24); uint32_t c = ov ? C_TX : C_MU;
      if (g_cfg_av) { ln(cx + 1, yy + 9, cx + 5, yy + 14, c, 22); ln(cx + 5, yy + 14, cx + 9, yy + 9, c, 22); } else { ln(cx + 3, yy + 7, cx + 8, yy + 12, c, 22); ln(cx + 8, yy + 12, cx + 3, yy + 17, c, 22); }
      tx(cx + 16, yy, 24, g_cfg_av ? "Servidores" : "Servidores (se ponen solos)", 12, c, WF_BOLD); hit(cx, yy, 260, 24, A_CFG_AVANZADO, 0, 0); }
    yy += 30;
    if (g_cfg_av) {
        int hw = cw - 200;
        campo_ui(cx, yy, hw, "IMAP", &f_imap, "imap.servidor.com"); campo_ui(cx + hw + 10, yy, 76, "", &f_imap_p, "993");
        { const char *s = g_cfg_imap_tls == CO_TLS_STARTTLS ? "STARTTLS" : "TLS"; boton(cx + hw + 96, yy + 3, 28, -1, s, 0, 0, A_CFG_SEG, 0, 0); }
        yy += 40;
        campo_ui(cx, yy, hw, "SMTP", &f_smtp, "smtp.servidor.com"); campo_ui(cx + hw + 10, yy, 76, "", &f_smtp_p, "465");
        { const char *s = g_cfg_smtp_tls == CO_TLS_STARTTLS ? "STARTTLS" : "TLS"; boton(cx + hw + 96, yy + 3, 28, -1, s, 0, 0, A_CFG_SEG, 1, 0); }
        yy += 40;
        campo_ui(cx, yy, cw, "Usuario", &f_usuario, "normalmente, el correo entero"); yy += 44;
    }
    /* pie */
    int py = y + h - 64;
    if (g_prueba_res < 0 && g_prueba_err[0]) {
        int lines = tw(g_prueba_err, 12, 0) / (cw - 40) + 1; if (lines > 4) lines = 4; int ah = 14 + lines * 18;
        rra(cx, py - ah - 8, cw, ah, 10, C_RED, 0x26);
        char b[300]; scpy(b, g_prueba_err, sizeof b); int ly = py - ah - 1, w0 = 0, esp = tw(" ", 12, 0), nl = 1;
        for (char *p = b; *p && nl <= lines;) { char *e = p; while (*e && *e != ' ') e++; int ww = twn(p, (int)(e - p), 12, 0); if (w0 && w0 + esp + ww > cw - 24) { ly += 18; w0 = 0; nl++; if (nl > lines) break; } txn(cx + 12 + w0 + (w0 ? esp : 0), ly, 18, p, (int)(e - p), 12, 0x00FCA5A5u, 0); w0 += (w0 ? esp : 0) + ww; p = *e ? e + 1 : e; }
    }
    const char *pr = g_cfg_probando ? "Probando\342\200\246" : "Probar y guardar"; int pw = ancho_boton(I_SINC, pr);
    txe(cx, py + 10, 38, "La clave solo se guarda en este equipo (/etc/correo.conf).", 11, C_FAINT, 0, cw - pw - 16);
    boton(x + w - 28 - pw, py + 10, 38, I_SINC, pr, 1, C_ACC, A_CFG_PROBAR, 0, 0);
}
static void pinta_toast(void)
{
    if (!g_toast[0] || ms() - g_toast_t > 4500) return;
    int w = tw(g_toast, 13, 0) + 48; if (w > G.w - 40) w = G.w - 40;
    int x = G.x + (G.w - w) / 2, y = G.y + G.top + 10;
    rr(x, y, w, 40, 20, 0x0002040Au); rs(x, y, w, 40, 20, g_toast_err ? C_RED : C_GREEN, 200);
    punto(x + 20, y + 20, 4, g_toast_err ? C_RED : C_GREEN, 255);
    txe(x + 34, y, 40, g_toast, 13, C_TX, 0, w - 48);
}
static void abre_ventana_cuenta(void);
void correo_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused)
{
    (void)focused;
    if (!g_ini) correo_init();
    nH = 0;
    geometria((int)x, (int)y, (int)w, (int)h);
    fb_set_clip((int)x, (int)y, (int)w, (int)h);
    pinta_cabecera();
    pinta_lateral();
    if (!G.estrecho || !vista_lectura() || (g_sel_c < 0 && !g_comp)) pinta_lista();
    if (vista_lectura()) { if (g_comp) pinta_redaccion(); else pinta_lectura(); }
    fb_set_clip((int)x, (int)y, (int)w, (int)h);
    if (g_cfg) pinta_cuenta();
    pinta_toast();
    fb_reset_clip();
    fb_damage((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
}

/* ================================================================ acciones */
static void abre_msg(int c, uint32_t uid)
{
    g_sel_c = c; g_sel_uid = uid; g_bscroll = 0;
    if (g_comp) { g_borrador_vivo = b_para[0] || b_asunto[0] || (b_cuerpo && b_cuerpo[0]); g_comp = 0; }   /* el borrador se guarda: Redactar lo devuelve */
    if (g_abierto_uid == uid && g_abierto_c == c && g_mime) return;
    g_abriendo = 1;
    encola(J_ABRE, c, uid, 0, 0, 0, 0);
}
static void elige_vecino(int dir)                                         /* el siguiente o el anterior de la lista */
{
    lista_calcula();
    int k = -1; for (int i = 0; i < g_nlista; i++) if (g_lista[i].c == g_sel_c && g_lista[i].uid == g_sel_uid) k = i;
    int n = k < 0 ? (dir > 0 ? 0 : g_nlista - 1) : k + dir;
    if (n < 0 || n >= g_nlista) return;
    abre_msg(g_lista[n].c, g_lista[n].uid);
    int ry = n * FILA_H; int vh = G.h - G.top - 62;
    if (ry < g_lscroll) g_lscroll = ry; else if (ry + FILA_H > g_lscroll + vh) g_lscroll = ry + FILA_H - vh;
}
static void carpeta(int i)
{
    g_vista = V_CARPETA; g_ci = i; g_lscroll = 0;
    cache_carga(i);
    if (g_hay_cuenta && !pendiente(J_SINC)) encola(J_SINC, i, 0, 0, 0, 0, 0);
}
static void separa_cita(const char *cuerpo, uint32_t *corte)              /* donde empieza la cita (para no reescribirla) */
{
    uint32_t n = co_len(cuerpo); *corte = n;
    for (uint32_t i = 0; i + 4 < n; i++) {
        if (cuerpo[i] != '\n') continue;
        const char *l = cuerpo + i + 1; uint32_t e = 0; while (l[e] && l[e] != '\n') e++;
        if ((co_iempieza(l, "El ") && e > 10 && l[e - 1] == ':' && co_ibusca(l, e, "escribi")) || co_iempieza(l, "---------- Mensaje reenviado")) {
            *corte = i; while (*corte > 0 && cuerpo[*corte - 1] == '\n') (*corte)--; return;
        }
    }
}
static void pon_cuerpo(const char *nuevo, int conserva_cita)
{
    uint32_t corte; separa_cita(b_cuerpo, &corte);
    char *t = (char *)co_alloc(CUERPO_CAP);
    if (!t) return;
    co_cpy(t, nuevo, CUERPO_CAP);
    if (conserva_cita && corte < co_len(b_cuerpo)) { const char *q = b_cuerpo + corte; while (*q == '\n') q++; co_cat(t, "\n\n", CUERPO_CAP); co_cat(t, q, CUERPO_CAP); }
    tf_set(&f_cuerpo, t); f_cuerpo.cur = f_cuerpo.anc = 0; f_cuerpo.scroll = 0;
    co_free(t);
}
static void abre_redaccion(int modo)
{
    tf_set(&f_para, ""); tf_set(&f_cc, ""); tf_set(&f_cco, ""); tf_set(&f_asunto, ""); tf_set(&f_cuerpo, ""); tf_set(&f_ia, "");
    g_comp_irt[0] = g_comp_refs[0] = 0; g_comp_estado[0] = 0; g_comp_err = 0; g_comp_orbe = 0; g_ia_fila = 0; g_cc_vis = 0; g_borrador_vivo = 0;
    g_comp_adj[0] = 0; g_comp_adj_tam = 0;
    g_comp_modo = modo; g_comp_c = -1; g_comp_uid = 0;
    if (modo != R_NUEVO) {
        if (!g_mime || g_abierto_uid != g_sel_uid) { toast("Espera a que se abra el correo", 1); return; }
        struct co_mime *m = g_mime; static struct co_dir d[24]; static char t[2000]; t[0] = 0;
        g_comp_c = g_sel_c; g_comp_uid = g_sel_uid;
        if (modo != R_REENVIAR) {
            int nd = co_dirs(m->responder_a[0] ? m->responder_a : m->de, d, 1);
            if (nd == 1 && co_ieq(d[0].email, g_c.email) && m->para[0]) nd = co_dirs(m->para, d, 1);   /* responder a uno propio: a quien iba */
            if (nd) co_dir_txt(&d[0], t, sizeof t);
            if (modo == R_RESP_TODOS) {
                static char cc[1200]; cc[0] = 0; int n2 = co_dirs(m->para, d, 24), n3 = n2 + co_dirs(m->cc, d + n2, 24 - n2);
                for (int i = 0; i < n3; i++) {
                    if (co_ieq(d[i].email, g_c.email) || co_ibusca(t, co_len(t), d[i].email)) continue;
                    char u[260]; co_dir_txt(&d[i], u, sizeof u); if (cc[0]) scat(cc, ", ", sizeof cc); scat(cc, u, sizeof cc);
                }
                if (cc[0]) { tf_set(&f_cc, cc); g_cc_vis = 1; }
            }
            tf_set(&f_para, t);
            scpy(g_comp_irt, m->msgid, sizeof g_comp_irt); scpy(g_comp_refs, m->refs, sizeof g_comp_refs);
        }
        char as[300]; co_asunto_resp(m->asunto, modo == R_REENVIAR, as, sizeof as); tf_set(&f_asunto, as);
        char de[260] = ""; if (co_dirs(m->de, d, 1)) co_dir_txt(&d[0], de, sizeof de);
        char para_txt[600] = ""; if (modo == R_REENVIAR) { int n2 = co_dirs(m->para, d, 8); for (int i = 0; i < n2; i++) { char u[260]; co_dir_txt(&d[i], u, sizeof u); if (i) scat(para_txt, ", ", sizeof para_txt); scat(para_txt, u, sizeof para_txt); } }
        uint32_t cap = g_txt_n + 4096 < CUERPO_CAP ? g_txt_n + 4096 : CUERPO_CAP; char *c = (char *)co_alloc(cap);
        if (c) {
            uint32_t tn = g_txt_n < cap - 2048 ? g_txt_n : cap - 2048;
            co_cita(de, m->fecha + co_desfase(m->fecha), g_txt, tn, modo == R_REENVIAR, m->asunto, para_txt, c, cap);
            tf_set(&f_cuerpo, c); f_cuerpo.cur = f_cuerpo.anc = 0; f_cuerpo.scroll = 0;
            co_free(c);
        }
        if (modo == R_REENVIAR) {
            int nadj = 0; for (int i = 0; i < m->nadj; i++) if (!m->adj[i].en_linea) nadj++;
            if (nadj) { scpy(g_comp_estado, "Los adjuntos del original no se reenv\303\255an (gu\303\241rdalos y env\303\255a el enlace o el texto)", sizeof g_comp_estado); g_comp_err = 1; }
        }
    }
    g_comp = 1;
    g_foco = modo == R_NUEVO || modo == R_REENVIAR ? &f_para : &f_cuerpo; g_foco_clave = 0;
    cambia();
}
static uint32_t junta_envio(char *o, uint32_t cap)
{
    const char *F[8] = { b_para, b_cc, b_cco, b_asunto, b_cuerpo, g_comp_irt, g_comp_refs, g_comp_adj };
    o[0] = 0;
    for (int i = 0; i < 8; i++) { if (i) { uint32_t n = co_len(o); if (n + 1 < cap) { o[n] = 1; o[n + 1] = 0; } } co_cat(o, F[i], cap); }
    return co_len(o);
}
static int envia_redaccion(char *motivo, uint32_t mcap)
{
    if (motivo && mcap) motivo[0] = 0;
    if (g_enviando) return 0;
    if (!g_hay_cuenta) { co_cpy(motivo, "No hay ninguna cuenta de correo", mcap); scpy(g_comp_estado, "A\303\261ade una cuenta para enviar", sizeof g_comp_estado); g_comp_err = 1; return -1; }
    struct co_dir *d = (struct co_dir *)co_alloc(sizeof(struct co_dir) * 40);
    if (!d) { co_cpy(motivo, "Sin memoria", mcap); return -1; }
    int n = co_dirs(b_para, d, 40) + co_dirs(b_cc, d, 40) + co_dirs(b_cco, d, 40);
    int malos = 0; { int k = co_dirs(b_para, d, 40); for (int i = 0; i < k; i++) if (!co_email_valido(d[i].email)) malos++; }
    co_free(d);
    if (!n || malos) {
        const char *e = !n ? "Pon al menos un destinatario con su direcci\303\263n (nombre@dominio)" : "Hay una direcci\303\263n que no es v\303\241lida en Para";
        scpy(g_comp_estado, e, sizeof g_comp_estado); g_comp_err = 1; co_cpy(motivo, e, mcap); g_foco = &f_para; cambia(); return -1;
    }
    uint32_t cap = CUERPO_CAP + 4096; char *t = (char *)co_alloc(cap);
    if (!t) { co_cpy(motivo, "Sin memoria", mcap); return -1; }
    junta_envio(t, cap);
    g_enviando = 1; g_envio_res = 0; scpy(g_comp_estado, "Enviando\342\200\246", sizeof g_comp_estado); g_comp_err = 0;
    encola(J_ENVIA, g_comp_c, g_comp_modo == R_RESPONDER || g_comp_modo == R_RESP_TODOS ? g_comp_uid : 0, 0, 0, 0, t);
    co_free(t);
    return 1;
}
static void cuenta_a_campos(void)
{
    tf_set(&f_nombre, g_c.nombre); tf_set(&f_email, g_c.email); scpy(b_clave, g_c.clave, sizeof b_clave);
    tf_set(&f_imap, g_c.imap); tf_set(&f_smtp, g_c.smtp); tf_set(&f_usuario, g_c.usuario);
    b_imap_p[0] = b_smtp_p[0] = 0; if (g_c.imap_p) snum(b_imap_p, sizeof b_imap_p, g_c.imap_p); if (g_c.smtp_p) snum(b_smtp_p, sizeof b_smtp_p, g_c.smtp_p);
    tf_set(&f_imap_p, b_imap_p); tf_set(&f_smtp_p, b_smtp_p);
    g_cfg_imap_tls = g_c.imap_tls ? g_c.imap_tls : CO_TLS_DIRECTO; g_cfg_smtp_tls = g_c.smtp_tls ? g_c.smtp_tls : CO_TLS_DIRECTO;
}
static void abre_ventana_cuenta(void)
{
    g_cfg = 1; g_cfg_av = 0; g_cfg_probando = 0; g_prueba_res = 0; g_prueba_err[0] = 0; g_cfg_ver_clave = 0;
    if (g_hay_cuenta) { cuenta_a_campos(); const char *av = 0; struct co_cuenta t = g_c; t.imap[0] = 0; (void)co_cuenta_auto(&t, &av); scpy(g_cfg_aviso, av ? av : "", sizeof g_cfg_aviso); }
    else { b_clave[0] = 0; g_cfg_aviso[0] = 0; tf_set(&f_imap, ""); tf_set(&f_smtp, ""); tf_set(&f_imap_p, ""); tf_set(&f_smtp_p, ""); tf_set(&f_usuario, ""); }
    g_foco = g_hay_cuenta ? 0 : &f_nombre; g_foco_clave = 0;
    if (!g_hay_cuenta && b_nombre[0]) g_foco = &f_email;
    cambia();
}
static void email_cambia(void)                                            /* se escribe el correo: se ponen los servidores del proveedor */
{
    struct co_cuenta t; for (uint32_t z = 0; z < sizeof t; z++) ((uint8_t *)&t)[z] = 0;
    co_cpy(t.email, b_email, sizeof t.email);
    const char *av = 0; int conocido = co_cuenta_auto(&t, &av);
    scpy(g_cfg_aviso, av ? av : "", sizeof g_cfg_aviso);
    if (!co_ibusca(b_email, co_len(b_email), "@")) return;
    (void)conocido;
    tf_set(&f_imap, t.imap); tf_set(&f_smtp, t.smtp); tf_set(&f_usuario, t.email);
    b_imap_p[0] = b_smtp_p[0] = 0; snum(b_imap_p, sizeof b_imap_p, t.imap_p); snum(b_smtp_p, sizeof b_smtp_p, t.smtp_p);
    tf_set(&f_imap_p, b_imap_p); tf_set(&f_smtp_p, b_smtp_p);
    g_cfg_imap_tls = t.imap_tls; g_cfg_smtp_tls = t.smtp_tls;
}
static struct co_cuenta g_c_vieja; static int g_hay_vieja;
static void cuenta_prueba(void)
{
    if (g_cfg_probando) return;
    if (!co_email_valido(b_email)) { scpy(g_prueba_err, "Escribe tu direcci\303\263n de correo completa", sizeof g_prueba_err); g_prueba_res = -1; g_foco = &f_email; cambia(); return; }
    if (!b_clave[0]) { scpy(g_prueba_err, "Falta la contrase\303\261a", sizeof g_prueba_err); g_prueba_res = -1; g_foco = 0; g_foco_clave = 1; cambia(); return; }
    if (!b_imap[0] || !b_smtp[0]) email_cambia();
    g_c_vieja = g_c; g_hay_vieja = g_hay_cuenta;
    struct co_cuenta c; for (uint32_t z = 0; z < sizeof c; z++) ((uint8_t *)&c)[z] = 0;
    co_cpy(c.email, b_email, sizeof c.email); co_cpy(c.nombre, b_nombre, sizeof c.nombre); co_cpy(c.clave, b_clave, sizeof c.clave);
    co_cpy(c.usuario, b_usuario[0] ? b_usuario : b_email, sizeof c.usuario);
    co_cpy(c.imap, b_imap, sizeof c.imap); co_cpy(c.smtp, b_smtp, sizeof c.smtp);
    { uint32_t v = 0; for (const char *p = b_imap_p; *p >= '0' && *p <= '9'; p++) v = v * 10 + (uint32_t)(*p - '0'); c.imap_p = (uint16_t)(v ? v : 993); }
    { uint32_t v = 0; for (const char *p = b_smtp_p; *p >= '0' && *p <= '9'; p++) v = v * 10 + (uint32_t)(*p - '0'); c.smtp_p = (uint16_t)(v ? v : 465); }
    c.imap_tls = g_cfg_imap_tls; c.smtp_tls = g_cfg_smtp_tls;
    { struct co_cuenta t = c; const char *av; t.imap[0] = 0; co_cuenta_auto(&t, &av); c.gmail = t.gmail && co_ieq(t.imap, c.imap); }
    g_c = c;
    g_cfg_probando = 1; g_prueba_res = 0; g_prueba_err[0] = 0;
    encola(J_PRUEBA, 0, 0, 0, 0, 0, 0);
}
static void ia_redaccion(void)                                           /* el boton "Escribir" de la fila de la IA */
{
    if (g_comp_ia) return;
    if (!b_ia[0]) { g_foco = &f_ia; return; }
    g_comp_ia = 1; g_ia_res = 0;
    if ((g_comp_modo == R_RESPONDER || g_comp_modo == R_RESP_TODOS) && g_comp_uid && g_abierto_uid == g_comp_uid) encola(J_RESPUESTA, g_comp_c, g_comp_uid, 0, 0, g_tono, b_ia);
    else {
        char t[1200]; scpy(t, b_ia, sizeof t);
        if (b_para[0]) { scat(t, "\n(Destinatario ya puesto: ", sizeof t); scat(t, b_para, sizeof t); scat(t, ")", sizeof t); }
        if (b_asunto[0]) { scat(t, "\n(Asunto ya puesto: ", sizeof t); scat(t, b_asunto, sizeof t); scat(t, ")", sizeof t); }
        encola(J_REDACTA, -1, 0, 0, 0, 0, t);
    }
    scpy(g_comp_estado, "La IA est\303\241 escribiendo\342\200\246", sizeof g_comp_estado); g_comp_err = 0;
}
static int accion(struct hit *z)
{
    cambia();
    switch (z->act) {
    case A_FOCO: g_foco = z->f; g_foco_clave = 0; if (z->f) tf_press(z->f, g_rx, g_ry, keyboard_shift_held()); return 1;
    case A_REDACTAR: if (!g_hay_cuenta) { abre_ventana_cuenta(); return 1; } if (g_borrador_vivo) { g_borrador_vivo = 0; g_comp = 1; g_foco = &f_cuerpo; return 1; } abre_redaccion(R_NUEVO); return 1;
    case A_CARPETA: g_foco = 0; carpeta(z->a); return 1;
    case A_VISTA:
        g_vista = z->a; g_lscroll = 0; g_foco = 0;
        if (z->a == V_IMPORTANTES) { int e = carpeta_entrada(); cache_carga(e); int sin = 0; for (int i = 0; i < g_cache[e].n && i < 60; i++) if (!g_cache[e].v[i].cat) sin = 1; if (sin && !pendiente(J_CLASIFICA)) encola(J_CLASIFICA, e, 0, 0, 0, 1, 0); }
        return 1;
    case A_MSG: g_foco = 0; abre_msg(z->a, (uint32_t)z->b); return 1;
    case A_SINC: if (!g_hay_cuenta) { abre_ventana_cuenta(); return 1; } if (!pendiente(J_SINC)) encola(J_SINC, g_vista == V_CARPETA ? g_ci : carpeta_entrada(), 2, 0, 0, 0, 0); return 1;
    case A_IA_TOGGLE: g_busca_ia = !g_busca_ia; g_foco = &f_busca; return 1;
    case A_BUSCAR: case A_LIMPIA:
        if (z->act == A_LIMPIA || !b_busca[0]) { tf_set(&f_busca, ""); g_vista = V_CARPETA; g_nres = 0; g_busca_estado = 0; g_lscroll = 0; return 1; }
        scpy(g_busca_txt, b_busca, sizeof g_busca_txt); g_busca_estado = 1; g_vista = V_BUSQUEDA; g_nres = 0; g_lscroll = 0; g_busca_expl[0] = 0;
        encola(J_BUSCA, g_ci, 0, 0, 0, g_busca_ia, b_busca);
        return 1;
    case A_RESPONDER: abre_redaccion(R_RESPONDER); return 1;
    case A_RESP_TODOS: abre_redaccion(R_RESP_TODOS); return 1;
    case A_REENVIAR: abre_redaccion(R_REENVIAR); return 1;
    case A_DESTACAR: case A_NOLEIDO: {
        struct co_msg *m = msg_de(g_sel_c, g_sel_uid); if (!m) return 1;
        int c = g_sel_c;
        uint32_t f = z->act == A_DESTACAR ? CO_F_DESTACADO : CO_F_VISTO; int poner = z->act == A_DESTACAR ? !(m->flags & CO_F_DESTACADO) : 0;
        if (poner) m->flags |= f; else m->flags &= ~f;
        if (z->act == A_NOLEIDO) { if (c < g_ncarp) g_carp[c].no_leidos++; g_sel_c = -1; }
        encola(J_BANDERA, c, m->uid, f, poner, 0, 0);
        return 1; }
    case A_BORRAR: case A_ARCHIVAR: {
        struct co_msg *m = msg_de(g_sel_c, g_sel_uid); if (!m) return 1;
        int uso = z->act == A_ARCHIVAR ? CO_U_ARCHIVO : CO_U_PAPELERA;
        int c = g_sel_c; uint32_t uid = m->uid;
        int modo = g_carp[c].uso == CO_U_PAPELERA && uso == CO_U_PAPELERA ? -1 : uso;
        if (modo == CO_U_PAPELERA && carpeta_uso(CO_U_PAPELERA) < 0) modo = -1;
        if (modo == CO_U_ARCHIVO && carpeta_uso(CO_U_ARCHIVO) < 0) { if (!(g_c.gmail || g_s.gmail)) return 1; modo = -1; }   /* Gmail: archivar es quitarlo de Recibidos */
        elige_vecino(1); if (g_sel_uid == uid) g_sel_c = -1;
        quita_de_cache(c, uid);
        encola(J_MUEVE, c, uid, 0, 0, modo, 0);
        toast(modo == -1 ? "Borrado" : uso == CO_U_ARCHIVO ? "Archivado" : "Movido a la papelera", 0);
        return 1; }
    case A_RESUMEN: if (!g_mime || g_abierto_uid != g_sel_uid) { toast("Espera a que se abra el correo", 1); return 1; } { struct resumen *s = resumen_de(g_sel_c, g_sel_uid); if (s && s->estado == 1) return 1; } encola(J_RESUMEN, g_sel_c, g_sel_uid, 0, 0, 0, 0); return 1;
    case A_RESP_IA:
        abre_redaccion(R_RESPONDER);
        if (!g_comp) return 1;
        g_ia_fila = 1; g_comp_ia = 1; g_ia_res = 0; g_foco = &f_ia;
        scpy(g_comp_estado, "La IA est\303\241 escribiendo la respuesta\342\200\246 (puedes decirle qu\303\251 contestar arriba)", sizeof g_comp_estado);
        encola(J_RESPUESTA, g_comp_c, g_comp_uid, 0, 0, g_tono, 0);
        return 1;
    case A_CALENDARIO: if (!g_mime || g_abierto_uid != g_sel_uid) { toast("Espera a que se abra el correo", 1); return 1; } encola(J_CALENDARIO, g_sel_c, g_sel_uid, 0, 0, 0, 0); return 1;
    case A_ADJ: encola(J_GUARDA_ADJ, g_sel_c, g_sel_uid, 0, 0, z->a, 0); return 1;
    case A_ENVIAR: { char m[200]; envia_redaccion(m, sizeof m); return 1; }
    case A_DESCARTAR: if (g_enviando) return 1; g_comp = 0; g_comp_orbe = 0; g_foco = 0; g_borrador_vivo = 0; return 1;
    case A_CC: g_cc_vis = 1; g_foco = &f_cc; return 1;
    case A_QUITA_ADJ: if (g_enviando) return 1; g_comp_adj[0] = 0; g_comp_adj_tam = 0; return 1;
    case A_IA_ESCRIBE: g_ia_fila = !g_ia_fila; g_foco = g_ia_fila ? &f_ia : &f_cuerpo; return 1;
    case A_IA_GENERA: ia_redaccion(); return 1;
    case A_MEJORA: {
        if (g_comp_ia) return 1;
        uint32_t corte; separa_cita(b_cuerpo, &corte);
        if (!corte) { scpy(g_comp_estado, "Escribe algo primero (o usa Escribir con IA)", sizeof g_comp_estado); g_comp_err = 1; return 1; }
        char *t = (char *)co_alloc(corte + 1); if (!t) return 1; co_cpyn(t, b_cuerpo, corte, corte + 1);
        g_comp_ia = 1; g_ia_res = 0; encola(J_MEJORA, -1, 0, 0, 0, z->a, t); co_free(t);
        scpy(g_comp_estado, "La IA est\303\241 mejorando el borrador\342\200\246", sizeof g_comp_estado); g_comp_err = 0;
        return 1; }
    case A_CUENTA: abre_ventana_cuenta(); return 1;
    case A_CFG_PROBAR: cuenta_prueba(); return 1;
    case A_CFG_CANCELA: if (g_hay_cuenta && !g_cfg_probando) { g_cfg = 0; g_foco = 0; g_foco_clave = 0; } return 1;
    case A_CFG_AVANZADO: g_cfg_av = !g_cfg_av; if (g_cfg_av && !b_imap[0]) email_cambia(); return 1;
    case A_CFG_SEG:
        if (z->a == 0) { g_cfg_imap_tls = g_cfg_imap_tls == CO_TLS_STARTTLS ? CO_TLS_DIRECTO : CO_TLS_STARTTLS; tf_set(&f_imap_p, g_cfg_imap_tls == CO_TLS_STARTTLS ? "143" : "993"); }
        else { g_cfg_smtp_tls = g_cfg_smtp_tls == CO_TLS_STARTTLS ? CO_TLS_DIRECTO : CO_TLS_STARTTLS; tf_set(&f_smtp_p, g_cfg_smtp_tls == CO_TLS_STARTTLS ? "587" : "465"); }
        return 1;
    case A_CFG_CLAVE: g_foco = 0; g_foco_clave = 1; return 1;
    case A_CFG_VER: g_cfg_ver_clave = !g_cfg_ver_clave; return 1;
    case A_CLASIFICA: if (!g_hay_cuenta) return 1; if (!pendiente(J_CLASIFICA)) { int c = g_vista == V_CARPETA ? g_ci : carpeta_entrada(); for (int i = 0; i < g_cache[c].n; i++) g_cache[c].v[i].cat = 0; encola(J_CLASIFICA, c, 0, 0, 0, 0, 0); } return 1;
    case A_VOLVER: g_sel_c = -1; return 1;
    default: return 1;
    }
}
int correo_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh;
    g_rx = mx; g_ry = my;
    int i = hit_en(mx, my);
    if (g_cfg) {                                                         /* con la ventana de la cuenta, solo cuenta lo de dentro */
        if (i < 0) return 1;
        int a = H[i].act;
        if (!(a == A_FOCO && (H[i].f == &f_nombre || H[i].f == &f_email || H[i].f == &f_imap || H[i].f == &f_imap_p || H[i].f == &f_smtp || H[i].f == &f_smtp_p || H[i].f == &f_usuario))
            && a != A_CFG_PROBAR && a != A_CFG_CANCELA && a != A_CFG_AVANZADO && a != A_CFG_SEG && a != A_CFG_CLAVE && a != A_CFG_VER) return 1;
    }
    if (i < 0) { cambia(); return 1; }
    struct hit z = H[i];
    if (z.act != A_FOCO) { g_foco_clave = g_foco_clave && (z.act == A_CFG_CLAVE || z.act == A_CFG_VER); }
    return accion(&z);
}
int correo_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held)
{
    int r = 0;
    int dentro = mx >= (int32_t)x && my >= (int32_t)y && mx < (int32_t)(x + w) && my < (int32_t)(y + h);
    if (mx != g_rx || my != g_ry) {
        g_rx = mx; g_ry = my;
        static int32_t ant = -2;
        int hv = dentro ? hit_en(mx, my) : -1;
        int32_t clave = hv < 0 ? -1 : (int32_t)((H[hv].act * 7919 + H[hv].a * 31 + H[hv].b) & 0x7FFFFF);
        if (clave != ant) { ant = clave; r = 1; }
    }
    if (held && g_foco && g_foco->dragging && tf_drag(g_foco, mx, my, held)) r = 1;
    if (wheel && dentro && !g_cfg) {
        int d = -wheel; if (d > 5) d = 5; if (d < -5) d = -5;
        if (mx >= G.lx && mx < G.lx + G.lw && !(G.estrecho && vista_lectura() && (g_sel_c >= 0 || g_comp))) { g_lscroll += d * FILA_H; if (g_lscroll < 0) g_lscroll = 0; if (g_lscroll > g_lmax) g_lscroll = g_lmax; }
        else if (mx >= G.rx) {
            if (g_comp) { f_cuerpo.scroll += d * 3; if (f_cuerpo.scroll < 0) f_cuerpo.scroll = 0; }
            else { g_bscroll += d * 60; if (g_bscroll < 0) g_bscroll = 0; if (g_bscroll > g_bmax) g_bscroll = g_bmax; }
        }
        r = 1;
    }
    if (r) cambia();
    return r;
}
static struct tfield *const CFG_ORDEN[9] = { &f_nombre, &f_email, 0, &f_imap, &f_imap_p, &f_smtp, &f_smtp_p, &f_usuario, 0 };
static void cfg_tab(int dir)
{
    int n = g_cfg_av ? 8 : 3, k = 0;
    for (int i = 0; i < n; i++) if ((CFG_ORDEN[i] && CFG_ORDEN[i] == g_foco) || (!CFG_ORDEN[i] && g_foco_clave)) k = i;
    k = (k + dir + n) % n;
    g_foco = CFG_ORDEN[k]; g_foco_clave = !CFG_ORDEN[k];
}
static void clave_tecla(char ch)
{
    int c = (uint8_t)ch, n = slen(b_clave);
    if (c == '\b' || c == 127) { if (n) { n--; while (n > 0 && ((uint8_t)b_clave[n] & 0xC0) == 0x80) n--; b_clave[n] = 0; } return; }
    if (c == '\t') { cfg_tab(keyboard_shift_held() ? -1 : 1); return; }
    if (c == '\n' || c == '\r') { cuenta_prueba(); return; }
    if (c >= 32 && c != 127 && n + 1 < (int)sizeof b_clave) { b_clave[n] = (char)c; b_clave[n + 1] = 0; }
}
void correo_handle_key(char ch)
{
    int c = (uint8_t)ch, ctrl = keyboard_ctrl_held(), shift = keyboard_shift_held();
    cambia();
    if (!g_ini) correo_init();
    if (g_cfg) {
        if (g_foco_clave) { clave_tecla(ch); return; }
        if (!g_foco) { if (c == '\t') cfg_tab(1); else if (c == '\n' || c == '\r') cuenta_prueba(); return; }
        int r = tf_key(g_foco, ch, ctrl, shift);
        if (g_foco == &f_email && r == TF_EDIT) email_cambia();
        if (r == TF_TAB) cfg_tab(shift ? -1 : 1);
        else if (r == TF_ENTER) cuenta_prueba();
        return;
    }
    if (g_comp && g_foco && (g_foco == &f_para || g_foco == &f_cc || g_foco == &f_cco || g_foco == &f_asunto || g_foco == &f_cuerpo || g_foco == &f_ia)) {
        int r = tf_key(g_foco, ch, ctrl, shift);
        if (r == TF_ENTER) {
            if (g_foco == &f_cuerpo) { char m[200]; envia_redaccion(m, sizeof m); }
            else if (g_foco == &f_ia) ia_redaccion();
            else g_foco = g_foco == &f_asunto ? &f_cuerpo : &f_asunto;
        } else if (r == TF_TAB) {
            struct tfield *o[6] = { &f_para, &f_cc, &f_cco, &f_asunto, &f_cuerpo, &f_ia }; int k = 0;
            for (int i = 0; i < 6; i++) if (o[i] == g_foco) k = i;
            for (int z = 0; z < 6; z++) { k = (k + (shift ? 5 : 1)) % 6; if ((o[k] == &f_cc || o[k] == &f_cco) && !g_cc_vis) continue; if (o[k] == &f_ia && !g_ia_fila) continue; break; }
            g_foco = o[k];
        }
        return;
    }
    if (g_foco == &f_busca) {
        int r = tf_key(&f_busca, ch, ctrl, shift);
        if (r == TF_ENTER) { struct hit z; z.act = A_BUSCAR; z.a = z.b = 0; z.f = 0; accion(&z); g_foco = 0; }
        else if (r == TF_ESC) g_foco = 0;
        return;
    }
    /* sin campo: atajos */
    if (c == 14) { if (g_sel_c >= 0 && G.rw > 0 && !ctrl) elige_vecino(1); else elige_vecino(1); return; }
    if (c == 16) { elige_vecino(-1); return; }
    if (c == 29 || c == 28) { g_bscroll += c == 29 ? 400 : -400; if (g_bscroll < 0) g_bscroll = 0; if (g_bscroll > g_bmax) g_bscroll = g_bmax; return; }
    if (ctrl) return;
    struct hit z; z.a = z.b = 0; z.f = 0; z.act = A_NADA;
    switch (c) {
    case '/': g_foco = &f_busca; return;
    case 'c': case 'n': z.act = A_REDACTAR; break;
    case 'r': z.act = A_RESPONDER; break;
    case 'a': z.act = A_RESP_TODOS; break;
    case 'f': z.act = A_REENVIAR; break;
    case 's': z.act = A_DESTACAR; break;
    case 'u': z.act = A_NOLEIDO; break;
    case 'e': z.act = A_ARCHIVAR; break;
    case 4: case '#': z.act = A_BORRAR; break;
    case '\n': case '\r': if (g_sel_c < 0) elige_vecino(1); return;
    default: return;
    }
    if (g_sel_c < 0 && z.act != A_REDACTAR) return;
    accion(&z);
}
int correo_clip_op(int op)
{
    if (g_foco_clave) {
        if (op == CLIP_OP_PASTE) { uint32_t n = 0; const char *t = clip_text(&n); for (uint32_t i = 0; t && i < n; i++) if ((uint8_t)t[i] >= 32) clave_tecla(t[i]); }
        cambia(); return 1;
    }
    if (!g_foco) return 0;
    if (op == CLIP_OP_PASTE && g_foco == &f_cuerpo) {
        uint32_t n = 0; const char *t = clip_text(&n);
        if (t && n) { char tmp[512]; uint32_t k = 0; for (uint32_t i = 0; i < n; i++) { if (t[i] == '\r') continue; tmp[k++] = t[i] == '\t' ? ' ' : t[i]; if (k == sizeof tmp) { tf_insert(&f_cuerpo, tmp, k); k = 0; } } if (k) tf_insert(&f_cuerpo, tmp, k); }
        cambia(); return 1;
    }
    tf_clip(g_foco, op);
    if (g_foco == &f_email) email_cambia();
    cambia();
    return 1;
}
int correo_escape(void)
{
    cambia();
    if (g_cfg) { if (g_hay_cuenta && !g_cfg_probando) { g_cfg = 0; g_foco = 0; g_foco_clave = 0; } return 1; }
    if (g_comp) { if (g_foco == &f_ia && g_ia_fila) { g_ia_fila = 0; g_foco = &f_cuerpo; return 1; } if (!g_enviando) { g_comp = 0; g_comp_orbe = 0; g_foco = 0; } return 1; }
    if (g_foco == &f_busca) { if (b_busca[0]) { tf_set(&f_busca, ""); } g_foco = 0; return 1; }
    if (g_vista == V_BUSQUEDA) { g_vista = V_CARPETA; g_nres = 0; tf_set(&f_busca, ""); return 1; }
    if (g_sel_c >= 0 && G.estrecho) { g_sel_c = -1; return 1; }
    return 0;
}

/* ================================================================ el orbe: lo que deja preparado para la GUI */
static volatile int g_ob_pend, g_ob_envia, g_ob_envia_fin; static char g_ob_para[600], g_ob_asunto[300], g_ob_irt[200], g_ob_refs[600], g_ob_err[300], g_ob_aviso[300];
static char *g_ob_cuerpo; static int g_ob_modo, g_ob_c; static uint32_t g_ob_uid;

void correo_init(void)
{
    if (g_ini) return;
    g_ini = 1;
    tf_init(&f_busca, b_busca, sizeof b_busca, 0);
    tf_init(&f_para, b_para, sizeof b_para, 0); tf_init(&f_cc, b_cc, sizeof b_cc, 0); tf_init(&f_cco, b_cco, sizeof b_cco, 0);
    tf_init(&f_asunto, b_asunto, sizeof b_asunto, 0); tf_init(&f_ia, b_ia, sizeof b_ia, 0);
    if (!b_cuerpo) b_cuerpo = (char *)co_alloc(CUERPO_CAP);
    if (b_cuerpo) { b_cuerpo[0] = 0; tf_init(&f_cuerpo, b_cuerpo, CUERPO_CAP, 1); }
    tf_init(&f_nombre, b_nombre, sizeof b_nombre, 0); tf_init(&f_email, b_email, sizeof b_email, 0);
    tf_init(&f_imap, b_imap, sizeof b_imap, 0); tf_init(&f_imap_p, b_imap_p, sizeof b_imap_p, 0);
    tf_init(&f_smtp, b_smtp, sizeof b_smtp, 0); tf_init(&f_smtp_p, b_smtp_p, sizeof b_smtp_p, 0); tf_init(&f_usuario, b_usuario, sizeof b_usuario, 0);
    cuenta_carga(); carpetas_carga();
    g_ci = carpeta_entrada();
    cache_carga(g_ci);
    if (g_hay_cuenta) encola(J_SINC, g_ci, 2, 0, 0, 1, 0);
    else abre_ventana_cuenta();
}
uint32_t correo_version(void) { return g_ver; }
int correo_tick(void)
{
    if (!g_ini) return 0;
    int r = 0; uint64_t t = ms();
    static uint64_t t_anim, t_auto; static uint32_t ver_vista;
    /* lo que dejo el orbe: un borrador o "envialo" */
    if (g_ob_pend) {
        abre_redaccion(R_NUEVO);
        g_comp_modo = g_ob_modo; g_comp_c = g_ob_c; g_comp_uid = g_ob_uid;
        tf_set(&f_para, g_ob_para); tf_set(&f_asunto, g_ob_asunto); if (g_ob_cuerpo) { tf_set(&f_cuerpo, g_ob_cuerpo); f_cuerpo.cur = f_cuerpo.anc = 0; f_cuerpo.scroll = 0; }
        scpy(g_comp_irt, g_ob_irt, sizeof g_comp_irt); scpy(g_comp_refs, g_ob_refs, sizeof g_comp_refs);
        g_comp_orbe = 1; g_foco = &f_cuerpo; g_cfg = 0;
        if (g_ob_aviso[0]) { scpy(g_comp_estado, g_ob_aviso, sizeof g_comp_estado); g_comp_err = 1; g_foco = &f_para; }
        g_ob_pend = 0; r = 1;
    }
    if (g_ob_envia == 1) {
        g_ob_envia = 2; g_ob_err[0] = 0;
        if (!g_comp) { scpy(g_ob_err, "No tengo ning\303\272n correo preparado para enviar", sizeof g_ob_err); g_ob_envia_fin = -1; g_ob_envia = 0; }
        else if (envia_redaccion(g_ob_err, sizeof g_ob_err) < 0) { g_ob_envia_fin = -1; g_ob_envia = 0; }
        r = 1;
    }
    /* el envio */
    if (g_enviando && g_envio_res) {
        int ok = g_envio_res > 0; g_enviando = 0;
        if (ok) {
            char t2[300] = "Enviado a "; struct co_dir d[1]; if (co_dirs(b_para, d, 1)) scat(t2, d[0].nombre[0] ? d[0].nombre : d[0].email, sizeof t2); else scat(t2, b_para, sizeof t2);
            toast(t2, 0); g_comp = 0; g_comp_orbe = 0; g_foco = 0; g_borrador_vivo = 0;
            if (g_ob_envia == 2) { scpy(g_ob_err, t2, sizeof g_ob_err); g_ob_envia_fin = 1; g_ob_envia = 0; }
        } else {
            scpy(g_comp_estado, g_envio_err, sizeof g_comp_estado); g_comp_err = 1;
            if (g_ob_envia == 2) { scpy(g_ob_err, g_envio_err, sizeof g_ob_err); g_ob_envia_fin = -1; g_ob_envia = 0; }
        }
        g_envio_res = 0; r = 1;
    }
    /* lo que escribio la IA */
    if (g_ia_res) {
        int ok = g_ia_res > 0; g_ia_res = 0; g_comp_ia = 0;
        if (g_comp && ok) {
            if (g_ia_que == J_REDACTA) {
                if (g_ia_para[0] && !b_para[0]) tf_set(&f_para, g_ia_para);
                if (g_ia_asunto[0] && (!b_asunto[0] || g_comp_modo == R_NUEVO)) tf_set(&f_asunto, g_ia_asunto);
            }
            pon_cuerpo(g_ia_cuerpo ? g_ia_cuerpo : "", 1);
            if (g_ia_err[0]) { scpy(g_comp_estado, g_ia_err, sizeof g_comp_estado); g_comp_err = 1; }
            else { scpy(g_comp_estado, "Escrito por la IA: rev\303\255salo antes de enviarlo", sizeof g_comp_estado); g_comp_err = 0; }
            g_foco = &f_cuerpo;
        } else if (g_comp) { scpy(g_comp_estado, g_ia_err[0] ? g_ia_err : "La IA no ha contestado", sizeof g_comp_estado); g_comp_err = 1; }
        r = 1;
    }
    /* la prueba de la cuenta */
    if (g_cfg_probando && g_prueba_res) {
        g_cfg_probando = 0;
        if (g_prueba_res > 0) { g_hay_cuenta = 1; cuenta_guarda(); g_cfg = 0; g_foco = 0; g_foco_clave = 0; toast("Cuenta lista: ya puedes leer y enviar correo", 0); g_ci = carpeta_entrada(); }
        else { g_c = g_c_vieja; g_hay_cuenta = g_hay_vieja; }
        r = 1;
    }
    /* busqueda terminada */
    if (g_busca_estado == 2) { g_busca_estado = 3; r = 1; }
    /* cada 3 minutos, Recibidos */
    if (g_hay_cuenta && !g_ocupado && t - t_auto > 180000u && t - g_ult_sinc > 170000u) { t_auto = t; encola(J_SINC, carpeta_entrada(), 1, 0, 0, 1, 0); }
    if ((g_ocupado || g_foco_clave || (g_toast[0] && t - g_toast_t < 4600)) && t - t_anim >= 250) { t_anim = t; r = 1; }
    if (g_ver != ver_vista) { ver_vista = g_ver; r = 1; }
#ifdef SOMA_CORREO_HOST
    (void)0;
#endif
    if (r) cambia();
    return r;
}

/* ================================================================ el orbe (hilo del agente) */
static void espera_id(uint32_t id, uint32_t max_ms)
{
    uint64_t t0 = ms();
    while (g_id_hecho < id && ms() - t0 < max_ms) {
#ifdef SOMA_CORREO_HOST
        trabaja_uno();
#else
        thread_sleep(100);
#endif
    }
}
int correo_orbe_hay_borrador(void) { return g_comp && g_comp_orbe; }
static void palabras_sin_orden(const char *m, char *o, uint32_t cap)     /* "busca el correo de amazon del pedido" -> "amazon del pedido" */
{
    static const char *const Q[] = { "busca", "buscame", "busques", "encuentra", "encuentrame", "localiza", "en", "mi", "mis", "el", "los", "la", "las", "un", "unos",
                                     "correo", "correos", "email", "emails", "e-mail", "mail", "mails", "mensaje", "mensajes", "de", "del", "que", "me", "por", "favor", "soma", 0 };
    o[0] = 0;
    const char *p = m; int inicio = 1;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        const char *e = p; while (*e && *e != ' ' && *e != ',') e++;
        char w[80]; co_cpyn(w, p, (uint32_t)(e - p), sizeof w);
        char nw[80]; co_norm(w, nw, sizeof nw);
        int quita = 0; if (inicio) for (int i = 0; Q[i]; i++) if (co_ieq(nw, Q[i])) quita = 1;
        if (!quita) { inicio = 0; if (o[0]) co_cat(o, " ", cap); co_cat(o, w, cap); }
        p = e;
    }
    uint32_t n = co_len(o); while (n && (o[n - 1] == '?' || o[n - 1] == '.' || o[n - 1] == ' ')) o[--n] = 0;
}
int correo_orbe(int que, const char *mision, char *out, uint32_t cap)
{
    out[0] = 0;
    if (!g_ini) correo_init();
    if (!g_hay_cuenta) {
        co_cpy(out, "Todav\303\255a no hay ninguna cuenta de correo en SOMA: te abro Correo para que la a\303\261adas. Si es Gmail, Yahoo o iCloud necesitas una contrase\303\261a de aplicaci\303\263n.", cap);
        gui_open_correo(); return 1;
    }
    if (que == CORREO_ORBE_ABRE) { gui_open_correo(); co_cpy(out, "Abro Correo.", cap); return 1; }
    if (que == CORREO_ORBE_ENVIA) {
        if (!correo_orbe_hay_borrador()) { co_cpy(out, "No tengo ning\303\272n correo preparado para enviar. P\303\255deme primero que lo redacte.", cap); return 1; }
        gui_open_correo();                                                 /* la ventana atiende el envio (si estaba cerrada, se abre) */
        g_ob_envia_fin = 0; g_ob_envia = 1;
        uint64_t t0 = ms();
        while (!g_ob_envia_fin && ms() - t0 < 120000u) {
#ifdef SOMA_CORREO_HOST
            correo_tick(); trabaja_uno(); correo_tick();
#else
            thread_sleep(100);
#endif
        }
        if (g_ob_envia_fin > 0) { co_cpy(out, "Hecho: ", cap); co_cat(out, g_ob_err, cap); co_cat(out, ".", cap); }
        else if (!g_ob_envia_fin) co_cpy(out, "El env\303\255o est\303\241 tardando; mira la ventana de Correo.", cap);
        else { co_cpy(out, "No lo he enviado: ", cap); co_cat(out, g_ob_err, cap); co_cat(out, ". Rev\303\255salo en la ventana de Correo.", cap); }
        return 1;
    }
    if (que == CORREO_ORBE_LEE) {
        uint32_t id = encola(J_LEE_ORBE, carpeta_entrada(), 0, 0, 0, 0, mision);
        espera_id(id, 240000u);
        co_cpy(out, g_lee_txt[0] ? g_lee_txt : "El correo est\303\241 tardando en responder.", cap);
        return 1;
    }
    if (que == CORREO_ORBE_BUSCA) {
        char q[300]; palabras_sin_orden(mision, q, sizeof q);
        if (!q[0]) co_cpy(q, mision, sizeof q);
        tf_set(&f_busca, q); scpy(g_busca_txt, q, sizeof g_busca_txt); g_busca_ia = 1; g_busca_estado = 1; g_vista = V_BUSQUEDA; g_nres = 0;
        uint32_t id = encola(J_BUSCA, carpeta_entrada(), 0, 0, 0, 1, q);
        gui_open_correo();
        espera_id(id, 240000u);
        if (!g_nres) { co_cpy(out, "No he encontrado ning\303\272n correo que encaje con \342\200\234", cap); co_cat(out, q, cap); co_cat(out, "\342\200\235.", cap); return 1; }
        co_cpy(out, "He encontrado ", cap); co_num(out, cap, (uint64_t)g_nres); co_cat(out, g_nres == 1 ? " correo" : " correos", cap); co_cat(out, " (los tienes en Correo):", cap);
        for (int i = 0; i < g_nres && i < 5; i++) {
            struct co_msg *m = msg_de(g_res[i].c, g_res[i].uid); if (!m) continue;
            char f[24]; fecha_corta(m->fecha, f, sizeof f);
            co_cat(out, "\n- ", cap); co_cat(out, m->de[0] ? m->de : m->de_email, cap); co_cat(out, ": ", cap); co_cat(out, m->asunto, cap); co_cat(out, " (", cap); co_cat(out, f, cap); co_cat(out, ")", cap);
        }
        return 1;
    }
    /* redactar (o responder a alguien) */
    static char pa[600], as[300], falta[300], err[300];
    if (!g_ob_cuerpo) g_ob_cuerpo = (char *)co_alloc(CUERPO_CAP);
    if (!g_ob_cuerpo) { co_cpy(out, "Sin memoria para preparar el correo.", cap); return 1; }
    if (!redacta(mision, pa, sizeof pa, as, sizeof as, g_ob_cuerpo, CUERPO_CAP, falta, sizeof falta, err, sizeof err)) {
        co_cpy(out, "No he podido redactar el correo: ", cap); co_cat(out, err, cap); return 1;
    }
    char nm[600]; co_norm(mision, nm, sizeof nm);
    g_ob_modo = R_NUEVO; g_ob_c = -1; g_ob_uid = 0; g_ob_irt[0] = g_ob_refs[0] = 0;
    if (co_iempieza(nm, "responde") || co_iempieza(nm, "contesta") || co_ibusca(nm, co_len(nm), " responde") || co_ibusca(nm, co_len(nm), " contesta")) {
        struct co_dir d[1]; int e = carpeta_entrada();
        if (co_dirs(pa, d, 1) == 1 && g_cache[e].v) for (int i = 0; i < g_cache[e].n; i++) if (co_ieq(g_cache[e].v[i].de_email, d[0].email)) {
            struct co_msg *m = &g_cache[e].v[i];
            g_ob_modo = R_RESPONDER; g_ob_c = e; g_ob_uid = m->uid; scpy(g_ob_irt, m->msgid, sizeof g_ob_irt);
            co_asunto_resp(m->asunto, 0, as, sizeof as);
            break;
        }
    }
    scpy(g_ob_para, pa, sizeof g_ob_para); scpy(g_ob_asunto, as, sizeof g_ob_asunto);
    g_ob_aviso[0] = 0;
    if (falta[0] || !pa[0]) { scpy(g_ob_aviso, "Falta la direcci\303\263n de: ", sizeof g_ob_aviso); scat(g_ob_aviso, falta[0] ? falta : "el destinatario", sizeof g_ob_aviso); }
    __sync_synchronize();
    g_ob_pend = 1;
    gui_open_correo();
#ifdef SOMA_CORREO_HOST
    correo_tick();
#else
    { uint64_t t0 = ms(); while (g_ob_pend && ms() - t0 < 3000) thread_sleep(50); }
#endif
    co_cpy(out, "He preparado el correo", cap);
    if (pa[0]) { co_cat(out, " para ", cap); co_cat(out, pa, cap); }
    if (as[0]) { co_cat(out, " con asunto \302\253", cap); co_cat(out, as, cap); co_cat(out, "\302\273", cap); }
    co_cat(out, ". Lo tienes en Correo para revisarlo", cap);
    if (falta[0] || !pa[0]) { co_cat(out, "; no s\303\251 la direcci\303\263n de ", cap); co_cat(out, falta[0] ? falta : "a qui\303\251n va", cap); co_cat(out, ", escr\303\255bela en Para", cap); }
    co_cat(out, ". Cuando quieras, pulsa Enviar o dime \302\253env\303\255alo\302\273.", cap);
    return 1;
}
int correo_cmd(const char *args, char *out, uint32_t cap)
{
    while (args && *args == ' ') args++;
    if (!args || !*args || co_iempieza(args, "abrir") || co_iempieza(args, "abre")) { return correo_orbe(CORREO_ORBE_ABRE, "", out, cap) ? 0 : 1; }
    if (!g_ini) correo_init();
    if (co_iempieza(args, "estado")) {
        if (!g_hay_cuenta) { co_cpy(out, "Correo: sin cuenta. Abre la app Correo (orden: correo) para a\303\261adirla.\n", cap); return 0; }
        co_cpy(out, "Cuenta: ", cap); co_cat(out, g_c.email, cap); co_cat(out, "\nIMAP: ", cap); co_cat(out, g_c.imap, cap); co_cat(out, ":", cap); co_num(out, cap, g_c.imap_p);
        co_cat(out, g_c.imap_tls == CO_TLS_STARTTLS ? " (STARTTLS)" : " (TLS)", cap); co_cat(out, "\nSMTP: ", cap); co_cat(out, g_c.smtp, cap); co_cat(out, ":", cap); co_num(out, cap, g_c.smtp_p);
        co_cat(out, g_c.smtp_tls == CO_TLS_STARTTLS ? " (STARTTLS)" : " (TLS)", cap); co_cat(out, "\nCarpetas:", cap);
        for (int i = 0; i < g_ncarp; i++) { co_cat(out, "\n  ", cap); co_cat(out, g_carp[i].nombre, cap); co_cat(out, ": ", cap); co_num(out, cap, (uint64_t)g_cache[i].n); co_cat(out, " en la copia local, ", cap); co_num(out, cap, g_carp[i].no_leidos); co_cat(out, " sin leer", cap); }
        if (g_err_conexion[0]) { co_cat(out, "\n\303\232ltimo error: ", cap); co_cat(out, g_err_conexion, cap); }
        co_cat(out, "\n", cap);
        return 0;
    }
    if (co_iempieza(args, "sincroniza")) {
        if (!g_hay_cuenta) { co_cpy(out, "Correo: sin cuenta.\n", cap); return 1; }
        uint32_t id = encola(J_SINC, carpeta_entrada(), 2, 0, 0, 1, 0); espera_id(id, 120000u);
        if (g_sinc_err) { co_cpy(out, "No se pudo sincronizar: ", cap); co_cat(out, g_err_conexion[0] ? g_err_conexion : g_s.err, cap); co_cat(out, "\n", cap); return 1; }
        int e = carpeta_entrada(); co_cpy(out, "Recibidos: ", cap); co_num(out, cap, (uint64_t)g_cache[e].n); co_cat(out, " correos en la copia local, ", cap); co_num(out, cap, g_carp[e].no_leidos); co_cat(out, " sin leer.\n", cap);
        return 0;
    }
    if (co_iempieza(args, "busca ")) {
        const char *q = args + 6; struct co_filtro f; int idx[20]; int e = carpeta_entrada();
        co_filtro_texto(q, ahora_local(), &f); cache_carga(e);
        int n = co_busca(g_cache[e].v, g_cache[e].n, &f, idx, 20);
        co_cpy(out, "", cap);
        for (int i = 0; i < n; i++) { struct co_msg *m = &g_cache[e].v[idx[i]]; char fe[24]; fecha_corta(m->fecha, fe, sizeof fe); co_cat(out, fe, cap); co_cat(out, "  ", cap); co_cat(out, m->de, cap); co_cat(out, ": ", cap); co_cat(out, m->asunto, cap); co_cat(out, "\n", cap); }
        if (!n) co_cpy(out, "Nada en la copia local de Recibidos.\n", cap);
        return 0;
    }
    co_cpy(out, "Uso: correo [abrir | estado | sincroniza | busca <texto>]\n  Tambi\303\251n desde el orbe: \"escribe un correo a Ana diciendo...\", \"\302\277tengo correos nuevos?\", \"busca el correo de...\"\n", cap);
    return 1;
}

/* ================================================================ el transporte en SOMA: TCP y TLS 1.3 */
#ifndef SOMA_CORREO_HOST
/* SOMA_326_TUNEL_TLS: la conexion es un tunel (src/apps/tun_tls.c): en x86_64 el TCP, el TLS y los certificados del servidor de
 * correo se hacen en un proceso de ring 3; aqui solo pasa el texto en claro de IMAP y SMTP. */
struct co_tcp { struct tun *t; };
static void err_tls(char *err, uint32_t ecap, const char *host, const char *m)
{
    co_cpy(err, "Conexi\303\263n segura con ", ecap); co_cat(err, host, ecap); co_cat(err, " rechazada: ", ecap); co_cat(err, m, ecap);
    co_cat(err, " (SOMA solo habla TLS 1.3: puede que ese servidor no lo tenga)", ecap);
}
static int tr_abre(void *c, const char *host, uint16_t port, int tls, char *err, uint32_t ecap)
{
    struct co_tcp *k = (struct co_tcp *)c;
    if (k->t) { tun_cierra(k->t, 1); k->t = 0; }
    uint8_t ip[4]; int isip = 1, parts = 0; uint32_t v = 0, dig = 0;
    for (const char *p = host; ; ++p) {
        if (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); if (++dig > 3 || v > 255) { isip = 0; break; } }
        else if (*p == '.' || !*p) { if (!dig || parts > 3) { isip = 0; break; } ip[parts++] = (uint8_t)v; v = 0; dig = 0; if (!*p) break; }
        else { isip = 0; break; }
    }
    if (!isip || parts != 4) {
        net_client_lock(); int ok = dns_resolve(host, ip, 6000); net_client_unlock();
        if (!ok) { co_cpy(err, "No se encuentra ", ecap); co_cat(err, host, ecap); co_cat(err, " (sin red o sin DNS)", ecap); return -1; }
    }
    char m[160];
    k->t = tun_abre(ip, port, tls, host, 15000, m, sizeof m);
    if (!k->t) {
        int conecta = co_ieq(m, "no se pudo conectar");
        if (!tls || conecta) {
            co_cpy(err, "No se pudo conectar con ", ecap); co_cat(err, host, ecap); co_cat(err, ":", ecap); co_num(err, ecap, port);
            if (!conecta) { co_cat(err, " (", ecap); co_cat(err, m, ecap); co_cat(err, ")", ecap); }
        } else err_tls(err, ecap, host, m);
        return -1;
    }
    return 0;
}
static int tr_starttls(void *c, const char *host, char *err, uint32_t ecap)
{
    struct co_tcp *k = (struct co_tcp *)c; char m[160];
    if (tun_starttls(k->t, host, m, sizeof m)) { err_tls(err, ecap, host, m); return -1; }
    return 0;
}
static int tr_envia(void *c, const void *d, uint32_t n) { struct co_tcp *k = (struct co_tcp *)c; return k->t ? tun_envia(k->t, d, n) : -1; }
static int tr_recibe(void *c, char *b, uint32_t max, uint32_t to)
{
    struct co_tcp *k = (struct co_tcp *)c;
    if (!k->t) return -1;
    int r = tun_recibe(k->t, b, max, to);
    return r < 0 ? -1 : r;
}
static void tr_cierra(void *c) { struct co_tcp *k = (struct co_tcp *)c; if (k->t) tun_cierra(k->t, 0); k->t = 0; }
static uint64_t tr_ms(void) { return timer_get_uptime_ms(); }
void correo_transporte(struct co_tr *t)
{
    struct co_tcp *k = (struct co_tcp *)kmalloc(sizeof *k);
    if (k) k->t = 0;
    t->ctx = k; t->abre = tr_abre; t->starttls = tr_starttls; t->envia = tr_envia; t->recibe = tr_recibe; t->cierra = tr_cierra; t->ms = tr_ms; t->cancela = 0;
}
void correo_transporte_libera(struct co_tr *t)
{
    struct co_tcp *k = (struct co_tcp *)t->ctx;
    if (k) { tr_cierra(k); kfree(k); }
    t->ctx = 0;
}
#endif

/* SOMA_357_CONTACTOS: "Escribir correo" desde la ficha de un contacto: redaccion nueva con el destinatario puesto y el foco en el asunto */
/* SOMA_360_HOJA_GRAFICOS: un correo nuevo con un archivo adjunto (la hoja de calculo, por ejemplo). Solo archivos de /tmp y /usuario
 * y nunca configuracion ni claves; se envia cuando el usuario pulsa Enviar. 0 = abierto; -1 = no (msg dice por que) */
int correo_redactar_adjunto(const char *ruta, const char *para, const char *asunto, char *msg, uint32_t cap)
{
    if (msg && cap) msg[0] = 0;
    if (!g_ini) correo_init();
    if (!ruta || !(co_iempieza(ruta, "/tmp/") || co_iempieza(ruta, "/usuario/"))) { co_cpy(msg, "Solo se adjuntan archivos de /usuario o /tmp", cap); return -1; }
    for (const char *z = ruta; *z; ++z) if (z[0] == '/' && z[1] == '.') { co_cpy(msg, "Ruta no valida", cap); return -1; }
    { static const char *const NO[] = { ".conf", ".key", ".pem", ".p12", ".pfx", ".cfg", ".ini", ".env", ".age", 0 };
      uint32_t L = co_len(ruta); for (int i = 0; NO[i]; ++i) { uint32_t k = co_len(NO[i]); if (L >= k && co_iempieza(ruta + L - k, NO[i])) { co_cpy(msg, "Ese tipo de archivo no se adjunta (configuracion o claves)", cap); return -1; } } }
    int t = vfs_size(ruta);
    if (t <= 0) { co_cpy(msg, "No encuentro el archivo", cap); return -1; }
    if ((uint32_t)t > CORREO_ADJ_MAX) { co_cpy(msg, "El archivo pasa de 20 MB", cap); return -1; }
    if (!g_hay_cuenta) { abre_ventana_cuenta(); cambia(); co_cpy(msg, "Configura antes una cuenta de correo", cap); return -1; }
    abre_redaccion(R_NUEVO);
    scpy(g_comp_adj, ruta, sizeof g_comp_adj); g_comp_adj_tam = (uint32_t)t;
    if (para && para[0]) tf_set(&f_para, para);
    if (asunto && asunto[0]) tf_set(&f_asunto, asunto);
    g_foco = para && para[0] ? &f_cuerpo : &f_para;
    cambia();
    return 0;
}
void correo_redactar_a(const char *email)
{
    if (!g_ini) correo_init();
    if (!g_hay_cuenta) { abre_ventana_cuenta(); cambia(); return; }
    abre_redaccion(R_NUEVO);
    if (email && email[0]) { tf_set(&f_para, email); g_foco = &f_asunto; }
    cambia();
}
