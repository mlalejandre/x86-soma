/* SOMA_337_NOTICIAS: estado y trabajo de la aplicacion de Noticias. Ver noticias.h. */
#include <stdint.h>
#include <stddef.h>
#include "noticias.h"
#include "noticias_rss.h"
#include "mem.h"
#include "mutex.h"
#include "thread.h"
#include "idt.h"
#include "fs.h"
#include "llm.h"
#include "web_net.h"
#include "net.h"
#include "netif.h"
#include "hora.h"
#include "zona.h"
#include "inv_tema.h"
#include "memoria.h"

#define NT_MAX   40                              /* titulares que se guardan por categoria */
#define NT_TMP   120                             /* los que se leen de todos los canales antes de quitar repetidos */
#define NT_RES   24000                           /* resumen */
#define NT_FRESCO_MS (10u * 60u * 1000u)         /* sin "forzar", no se vuelve a descargar antes de 10 min */

struct cat_est {
    struct nt_item *it; int n;
    volatile int estado; char err[120]; uint64_t cargada;
    volatile int pide_carga, pide_res, forzar;
    char *res; volatile int res_estado; uint32_t res_ver; char res_err[120];
    volatile int pide_guarda, guarda_estado; char guarda_msg[200];
};
static struct cat_est g_cat[NTC_N];
static struct nt_conf g_c; static int g_conf_ok;
static struct kmutex g_lk; static int g_lk_ok;
static volatile uint32_t g_ver;
static volatile int g_hilo;

static void lk(void) { if (!g_lk_ok) { kmutex_init(&g_lk, "noticias"); g_lk_ok = 1; } kmutex_lock(&g_lk); }
static void ulk(void) { kmutex_unlock(&g_lk); }
static void scpy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } if (cap) d[i] = 0; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putd(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int k = 0; if (v < 0) { put(o, cap, p, "-"); v = -v; } do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 23); while (k) { char c[2] = { b[--k], 0 }; put(o, cap, p, c); } }
static int cat_ok(int c) { return c >= 0 && c < NTC_N; }

static void conf_carga(void)
{
    if (g_conf_ok) return;
    char b[3200]; int n = vfs_read("/etc/noticias.conf", b, sizeof b - 1);
    if (n > 0) { b[n] = 0; nt_conf_lee(&g_c, b); } else nt_conf_defecto(&g_c);
    g_conf_ok = 1;
}
void noticias_conf(struct nt_conf *c) { lk(); conf_carga(); *c = g_c; ulk(); }
void noticias_pon_conf(const struct nt_conf *c)
{
    static char b[3200];
    lk();
    int lugar_cambia = 0; { const char *a = g_c.lugar, *x = c->lugar; while (*a && *a == *x) { a++; x++; } lugar_cambia = *a != *x; }
    g_c = *c; g_conf_ok = 1;
    if (lugar_cambia) { g_cat[NTC_LOCAL].n = 0; g_cat[NTC_LOCAL].estado = NTE_NADA; g_cat[NTC_LOCAL].cargada = 0; }
    g_cat[NTC_PORTADA].cargada = 0;                               /* los canales propios salen en Portada */
    uint32_t n = nt_conf_escribe(&g_c, b, sizeof b);
    ulk();
    (void)vfs_write("/etc/noticias.conf", b, n);
    g_ver++;
}
int64_t noticias_ahora(void) { return hora_utc_min(); }

/* ---------------------------------------------------------------- leer un canal (el XML, en leesvc: ring 3) */
#if defined(__x86_64__)
#include "sandbox.h"
static int rss_aislado(const char *xml, uint32_t len, struct nt_item *it, int max, const char *fdef)
{
    if (max <= 0 || len + 64u > SBX_IN_MAX) return -1;
    char *in = (char *)kmalloc(len + 64u);
    if (!in) return -1;
    memset(in, 0, 64); scpy(in, fdef ? fdef : "", 64); memcpy(in + 64, xml, len);
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) { kfree(in); return -1; }
    uint32_t cap = (uint32_t)max * (uint32_t)sizeof *it;
    sbx_prepare(c, SBX_LEE_RSS, in, len + 64u, 0);
    c->in.arg[0] = (int32_t)cap; c->in.arg[1] = max;
    int n = -1;
    if (sbx_svc_call(SBX_SVC_LEE, c, 10000u) == 0 && c->out.status == 0) {
        int32_t k = c->out.val[0];
        if (k == 0) n = 0;
        else if (k > 0 && k <= max && c->out.data_len == (uint32_t)k * (uint32_t)sizeof *it && sbx_copy_data(c, it, c->out.data_len) == 0) n = k;   /* tiene que cuadrar exacto */
    }
    sbx_release(c); kfree(c); kfree(in);
    if (n > 0) nt_sanea(it, n);
    return n;
}
#else
static int rss_aislado(const char *xml, uint32_t len, struct nt_item *it, int max, const char *fdef) { int n = nt_rss(xml, len, it, max, fdef); nt_sanea(it, n); return n; }
#endif

static int hay_red(void) { return netif_active() && (net_ip[0] || net_ip[1] || net_ip[2] || net_ip[3]); }

static void carga(int cat)
{
    struct cat_est *e = &g_cat[cat];
    char urls[1 + NT_EXTRA][NT_URL]; int nu = 0;
    lk(); conf_carga();
    if (nt_cat_url(cat, g_c.lugar, urls[0], NT_URL)) nu = 1;
    if (cat == NTC_PORTADA) for (int i = 0; i < g_c.nextra && nu < 1 + NT_EXTRA; i++) scpy(urls[nu++], g_c.extra[i], NT_URL);
    e->estado = NTE_CARGANDO; ulk(); g_ver++;
    if (!nu) { lk(); e->estado = NTE_ERROR; scpy(e->err, "Para las noticias locales, escribe arriba tu ciudad, provincia o region.", sizeof e->err); ulk(); g_ver++; return; }
    if (!hay_red()) { lk(); e->estado = NTE_ERROR; scpy(e->err, "Sin conexion a Internet.", sizeof e->err); ulk(); g_ver++; return; }
    struct nt_item *tmp = (struct nt_item *)kmalloc(NT_TMP * sizeof *tmp);
    if (!tmp) { lk(); e->estado = NTE_ERROR; scpy(e->err, "Sin memoria.", sizeof e->err); ulk(); g_ver++; return; }
    int n = 0, fallos = 0; char err[120]; err[0] = 0;
    for (int i = 0; i < nu && n < NT_TMP; i++) {
        static struct wn_resp rs; struct wn_req rq;
        memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
        rq.url = urls[i]; rq.accept = "application/rss+xml, application/atom+xml, application/xml, text/xml;q=0.9, */*;q=0.5";
        rq.timeout_ms = 12000; rq.max_body = 3u << 20; rq.direct = 1;
        if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body && rs.len) {
            int k = rss_aislado(rs.body, rs.len, tmp + n, NT_TMP - n, "");
            if (k > 0) n += k;
            else { fallos++; scpy(err, k == 0 ? "El canal no trae noticias (o no es RSS)." : "No se pudo leer el canal.", sizeof err); }
        } else {
            fallos++;
            uint32_t p = 0; err[0] = 0;
            if (rs.status > 0) { put(err, sizeof err, &p, "El servidor de noticias respondio "); putd(err, sizeof err, &p, rs.status); put(err, sizeof err, &p, "."); }
            else put(err, sizeof err, &p, "No se pudo descargar (sin red, DNS o tiempo agotado).");
        }
        wn_free_isolated(&rs);
    }
    n = nt_limpia(tmp, n);
    if (n > NT_MAX) n = NT_MAX;
    lk();
    if (!e->it) e->it = (struct nt_item *)kmalloc(NT_MAX * sizeof *e->it);
    if (e->it && n > 0) { memcpy(e->it, tmp, (size_t)n * sizeof *tmp); e->n = n; e->estado = NTE_BIEN; e->cargada = timer_get_uptime_ms(); e->err[0] = 0; if (fallos) scpy(e->err, err, sizeof e->err); }
    else if (!e->it) { e->estado = NTE_ERROR; scpy(e->err, "Sin memoria.", sizeof e->err); }
    else { e->estado = e->n ? NTE_BIEN : NTE_ERROR; scpy(e->err, err[0] ? err : "No hay noticias.", sizeof e->err); }
    ulk();
    kfree(tmp);
    g_ver++;
}

/* ---------------------------------------------------------------- resumen con IA */
static const char SYS_RES[] =
    "Eres el redactor de noticias de SOMA. Te doy titulares recientes numerados [1], [2]... (medio, cuando y entradilla). Escribe en "
    "espanol un resumen claro de lo mas importante, en Markdown: empieza con dos o tres frases con lo principal; despues agrupa por "
    "temas con ### y vinetas breves. Pon la cita [n] detras de cada dato. No inventes nada que no este en los titulares ni lo completes "
    "con lo que sepas; si algo solo lo cuenta un medio, dilo. Termina con una seccion '## Fuentes' con la lista numerada: medio, "
    "titular y enlace de las que hayas usado.";
static void resume(int cat)
{
    struct cat_est *e = &g_cat[cat];
    lk();
    int n = e->n < 25 ? e->n : 25;
    if (!n) { e->res_estado = NTE_ERROR; scpy(e->res_err, "No hay titulares que resumir: actualiza primero.", sizeof e->res_err); ulk(); g_ver++; return; }
    uint32_t pc = 2048u + (uint32_t)n * (NT_TIT + NT_URL + NT_FTE + NT_DESC + 64u);
    char *pr = (char *)kmalloc(pc);
    if (!pr) { e->res_estado = NTE_ERROR; scpy(e->res_err, "Sin memoria.", sizeof e->res_err); ulk(); g_ver++; return; }
    uint32_t p = 0; int64_t ya = noticias_ahora();
    put(pr, pc, &p, "Categoria: "); put(pr, pc, &p, nt_cat_nombre(cat));
    if (cat == NTC_LOCAL) { put(pr, pc, &p, " ("); put(pr, pc, &p, g_c.lugar); put(pr, pc, &p, ")"); }
    {   struct rtc_time t; hora_local(&t); put(pr, pc, &p, "\nFecha de hoy: "); putd(pr, pc, &p, t.year); put(pr, pc, &p, "-"); if (t.month < 10) put(pr, pc, &p, "0"); putd(pr, pc, &p, t.month); put(pr, pc, &p, "-"); if (t.day < 10) put(pr, pc, &p, "0"); putd(pr, pc, &p, t.day); }
    put(pr, pc, &p, "\n\nTitulares:\n");
    for (int i = 0; i < n; i++) {
        const struct nt_item *x = &e->it[i]; char h[32]; nt_hace(ya, x->t, h, sizeof h);
        put(pr, pc, &p, "["); putd(pr, pc, &p, i + 1); put(pr, pc, &p, "] ("); put(pr, pc, &p, x->fuente[0] ? x->fuente : "?");
        if (h[0]) { put(pr, pc, &p, ", "); put(pr, pc, &p, h); }
        put(pr, pc, &p, ") "); put(pr, pc, &p, x->tit);
        if (x->desc[0]) { put(pr, pc, &p, ". "); put(pr, pc, &p, x->desc); }
        if (x->url[0]) { put(pr, pc, &p, " <"); put(pr, pc, &p, x->url); put(pr, pc, &p, ">"); }
        put(pr, pc, &p, "\n");
    }
    e->res_estado = NTE_CARGANDO;
    ulk(); g_ver++;
    char *r = (char *)kmalloc(NT_RES);
    if (!r) { kfree(pr); lk(); e->res_estado = NTE_ERROR; scpy(e->res_err, "Sin memoria.", sizeof e->res_err); ulk(); g_ver++; return; }
    r[0] = 0;
    llm_trace_tag("noticias");
    int ok = llm_chat_ex(SYS_RES, pr, 0, 0, 0, r, NT_RES, 300000u);
    kfree(pr);
    if (ok == 1 && it_sin_razonamiento(r) == 0) ok = 0;            /* solo el resumen, sin el razonamiento */
    lk();
    if (ok == 1) { if (e->res) kfree(e->res); e->res = r; r = 0; e->res_estado = NTE_BIEN; e->res_ver++; e->res_err[0] = 0; }
    else { e->res_estado = NTE_ERROR; scpy(e->res_err, llm_last_error[0] ? llm_last_error : "El modelo de lenguaje no ha respondido.", sizeof e->res_err); }
    ulk();
    if (r) kfree(r);
    g_ver++;
}

/* ---------------------------------------------------------------- hilo de trabajo */
static void hilo(void *a)
{
    (void)a;
    uint64_t libre = timer_get_uptime_ms();
    for (;;) {
        int hecho = 0;
        for (int c = 0; c < NTC_N; c++) {
            if (g_cat[c].pide_carga) {
                g_cat[c].pide_carga = 0; hecho = 1;
                if (!g_cat[c].forzar && g_cat[c].estado == NTE_BIEN && g_cat[c].cargada && timer_get_uptime_ms() - g_cat[c].cargada < NT_FRESCO_MS) continue;
                g_cat[c].forzar = 0;
                carga(c);
            }
            if (g_cat[c].pide_res) { g_cat[c].pide_res = 0; hecho = 1; resume(c); }
            if (g_cat[c].pide_guarda) {                               /* guardar calcula vectores: aqui, no en la ventana */
                g_cat[c].pide_guarda = 0; hecho = 1;
                char m[200]; int r = noticias_guarda(c, m, sizeof m);
                lk(); scpy(g_cat[c].guarda_msg, m, sizeof g_cat[c].guarda_msg); g_cat[c].guarda_estado = r == 0 ? NTE_BIEN : NTE_ERROR; ulk(); g_ver++;
            }
        }
        if (hecho) { libre = timer_get_uptime_ms(); continue; }
        if (timer_get_uptime_ms() - libre > 60000u) break;
        thread_sleep(50);
    }
    g_hilo = 0;
}
static void despierta(void)
{
    if (__atomic_exchange_n(&g_hilo, 1, __ATOMIC_ACQ_REL)) return;
    if (!thread_create("noticias", hilo, 0)) g_hilo = 0;
}
void noticias_carga(int cat, int forzar)
{
    if (!cat_ok(cat)) return;
    if (forzar) g_cat[cat].forzar = 1;
    if (g_cat[cat].estado != NTE_CARGANDO) { if (g_cat[cat].estado == NTE_NADA || forzar) g_cat[cat].estado = NTE_CARGANDO; }
    g_cat[cat].pide_carga = 1; g_ver++;
    despierta();
}
int noticias_guarda_pide(int cat)
{
    if (!cat_ok(cat) || g_cat[cat].guarda_estado == NTE_CARGANDO) return -1;
    g_cat[cat].guarda_estado = NTE_CARGANDO; g_cat[cat].pide_guarda = 1; g_ver++;
    despierta();
    return 0;
}
int noticias_resume(int cat)
{
    if (!cat_ok(cat)) return -1;
    if (g_cat[cat].res_estado == NTE_CARGANDO) return 0;
    g_cat[cat].res_estado = NTE_CARGANDO; g_cat[cat].pide_res = 1; g_ver++;
    despierta();
    return 0;
}
void noticias_vista(int cat, struct nt_vista *v)
{
    v->estado = NTE_NADA; v->n = 0; v->err[0] = 0; v->cargada_ms = 0; v->res_estado = NTE_NADA; v->res_ver = 0; v->res_err[0] = 0;
    v->guarda_estado = NTE_NADA; v->guarda_msg[0] = 0;
    if (!cat_ok(cat)) return;
    lk();
    const struct cat_est *e = &g_cat[cat];
    v->estado = e->estado; v->n = e->n; scpy(v->err, e->err, sizeof v->err); v->cargada_ms = e->cargada;
    v->res_estado = e->res_estado; v->res_ver = e->res_ver; scpy(v->res_err, e->res_err, sizeof v->res_err);
    v->guarda_estado = e->guarda_estado; scpy(v->guarda_msg, e->guarda_msg, sizeof v->guarda_msg);
    ulk();
}
int noticias_item(int cat, int i, struct nt_item *o)
{
    if (!cat_ok(cat)) return 0;
    lk();
    int ok = g_cat[cat].it && i >= 0 && i < g_cat[cat].n;
    if (ok) *o = g_cat[cat].it[i];
    ulk();
    return ok;
}
uint32_t noticias_resumen(int cat, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    if (!cat_ok(cat)) return 0;
    lk(); if (g_cat[cat].res) put(o, cap, &p, g_cat[cat].res); ulk();
    return p;
}
uint32_t noticias_version(void) { return g_ver; }

/* ---------------------------------------------------------------- guardar en el cartucho "Noticias" */
int noticias_guarda(int cat, char *msg, uint32_t cap)
{
    uint32_t p = 0; if (cap) msg[0] = 0;
    if (!cat_ok(cat)) return -1;
    char fuente[96]; { uint32_t q = 0; struct rtc_time t; hora_local(&t); put(fuente, sizeof fuente, &q, "Noticias: "); put(fuente, sizeof fuente, &q, nt_cat_nombre(cat));
        put(fuente, sizeof fuente, &q, ", "); putd(fuente, sizeof fuente, &q, t.day); put(fuente, sizeof fuente, &q, "/"); putd(fuente, sizeof fuente, &q, t.month); put(fuente, sizeof fuente, &q, "/"); putd(fuente, sizeof fuente, &q, t.year); }
    int guardados = 0, fallos = 0; char m[160]; m[0] = 0;
    char *res = (char *)kmalloc(NT_RES);
    if (!res) { put(msg, cap, &p, "Sin memoria."); return -1; }
    noticias_resumen(cat, res, NT_RES);
    {   /* el resumen, por trozos de parrafos (una celda de la memoria admite unos 1200 bytes); sin la lista de fuentes */
        char *f = res; for (char *s = res; *s; s++) if (s[0] == '#' && s[1] == '#' && s[2] == ' ' && s[3] == 'F' && (s == res || s[-1] == '\n')) { *s = 0; break; }
        (void)f;
        uint32_t L = 0; while (res[L]) L++;
        uint32_t a = 0;
        while (a < L) {
            uint32_t b = a + 1000u; if (b >= L) b = L;
            else { uint32_t c = b; while (c > a + 300u && !(res[c] == '\n' && res[c - 1] == '\n')) c--; if (c > a + 300u) b = c; else { while (b > a && ((unsigned char)res[b] & 0xC0) == 0x80) b--; } }
            char t[1100]; uint32_t k = 0; for (uint32_t i = a; i < b && k + 1 < sizeof t; i++) t[k++] = res[i]; t[k] = 0;
            uint32_t z = 0; while (t[z] == '\n' || t[z] == ' ') z++;
            if (t[z]) { if (memoria_guarda_en_cartucho("Noticias", "Noticias guardadas desde la aplicacion Noticias", t + z, fuente, m, sizeof m) == 0) guardados++; else fallos++; }
            a = b;
        }
    }
    kfree(res);
    for (int i = 0; i < 10 && !fallos; i++) {                     /* y los diez primeros titulares */
        struct nt_item x; if (!noticias_item(cat, i, &x)) break;
        char t[1000]; uint32_t q = 0; char h[24]; nt_hace(noticias_ahora(), x.t, h, sizeof h);
        put(t, sizeof t, &q, x.tit); put(t, sizeof t, &q, " ("); put(t, sizeof t, &q, x.fuente); put(t, sizeof t, &q, ")");
        if (x.desc[0]) { put(t, sizeof t, &q, ": "); put(t, sizeof t, &q, x.desc); }
        if (x.url[0]) { put(t, sizeof t, &q, " "); put(t, sizeof t, &q, x.url); }
        if (memoria_guarda_en_cartucho("Noticias", "Noticias guardadas desde la aplicacion Noticias", t, fuente, m, sizeof m) == 0) guardados++; else fallos++;
    }
    if (!guardados) { put(msg, cap, &p, m[0] ? m : "No hay nada que guardar: actualiza y resume primero."); return -1; }
    put(msg, cap, &p, "Guardado en el cartucho de memoria Noticias: "); putd(msg, cap, &p, guardados); put(msg, cap, &p, " recuerdos.");
    if (fallos) { put(msg, cap, &p, " Fallaron "); putd(msg, cap, &p, fallos); put(msg, cap, &p, " ("); put(msg, cap, &p, m); put(msg, cap, &p, ")."); }
    return 0;
}

/* ---------------------------------------------------------------- orden "noticias" */
static int pal(const char *a, const char *w) { while (*w && *a == *w) { a++; w++; } return !*w && (*a == 0 || *a == ' '); }
static const char *tras(const char *a) { while (*a && *a != ' ') a++; while (*a == ' ') a++; return a; }
static int espera(int cat, int resumen, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 100) {
        thread_sleep(100);
        int st = resumen ? g_cat[cat].res_estado : g_cat[cat].estado;
        int pend = resumen ? g_cat[cat].pide_res : g_cat[cat].pide_carga;
        if (st != NTE_CARGANDO && !pend) return st;
    }
    return NTE_CARGANDO;
}
static void lista(int cat, char *o, uint32_t cap, uint32_t *p, int max)
{
    int64_t ya = noticias_ahora();
    for (int i = 0; i < max; i++) {
        struct nt_item x; if (!noticias_item(cat, i, &x)) break;
        char h[24]; nt_hace(ya, x.t, h, sizeof h);
        put(o, cap, p, "  "); putd(o, cap, p, i + 1); put(o, cap, p, ". "); put(o, cap, p, x.tit); put(o, cap, p, "  ("); put(o, cap, p, x.fuente);
        if (h[0]) { put(o, cap, p, ", "); put(o, cap, p, h); }
        put(o, cap, p, ")\n");
    }
}
int noticias_cmd(const char *a, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    while (a && *a == ' ') a++;
    if (!a) a = "";
    struct nt_conf c; noticias_conf(&c);
    if (!*a) {
        put(o, cap, &p, "Noticias. Categorias activas:");
        for (int i = 0; i < NTC_N; i++) if (c.cats & (1u << i)) { put(o, cap, &p, " "); put(o, cap, &p, nt_cat_id(i)); }
        put(o, cap, &p, "\nLugar (para Local): "); put(o, cap, &p, c.lugar[0] ? c.lugar : "(sin elegir)");
        put(o, cap, &p, "\nCanales propios: "); putd(o, cap, &p, c.nextra);
        put(o, cap, &p, "\nuso: noticias <categoria> | resumen <categoria> | guardar <categoria> | categorias a,b,c | lugar <ciudad> | fuente anadir <url> | fuente quitar <n>\n");
        return 1;
    }
    if (pal(a, "categorias")) {
        const char *v = tras(a);
        if (!*v) { put(o, cap, &p, "uso: noticias categorias portada,espana,local,mundo,economia,tecnologia,ciencia,salud,deportes,cultura\n"); return 1; }
        uint32_t m = 0; const char *s = v;
        while (*s) { int i = nt_cat_busca(s); if (i >= 0) m |= 1u << i; while (*s && *s != ',' && *s != ' ') s++; while (*s == ',' || *s == ' ') s++; }
        if (!m) { put(o, cap, &p, "noticias: ninguna categoria conocida\n"); return 1; }
        c.cats = m; noticias_pon_conf(&c); put(o, cap, &p, "Categorias guardadas.\n"); return 1;
    }
    if (pal(a, "lugar")) {
        const char *v = tras(a); scpy(c.lugar, v, sizeof c.lugar); c.cats |= *v ? (1u << NTC_LOCAL) : 0; noticias_pon_conf(&c);
        put(o, cap, &p, *v ? "Lugar guardado: Local buscara noticias de " : "Lugar borrado."); put(o, cap, &p, v); put(o, cap, &p, "\n"); return 1;
    }
    if (pal(a, "fuente")) {
        const char *v = tras(a);
        if (pal(v, "anadir") || pal(v, "a\303\261adir")) {
            const char *u = tras(v);
            if (!(u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p')) { put(o, cap, &p, "noticias: la direccion del canal RSS empieza por http:// o https://\n"); return 1; }
            if (c.nextra >= NT_EXTRA) { put(o, cap, &p, "noticias: ya hay 8 canales propios; quita alguno antes\n"); return 1; }
            uint32_t k = 0; while (u[k] && u[k] != ' ' && k + 1 < NT_URL) { c.extra[c.nextra][k] = u[k]; k++; } c.extra[c.nextra][k] = 0; c.nextra++;
            noticias_pon_conf(&c); put(o, cap, &p, "Canal anadido (sale en Portada).\n"); return 1;
        }
        if (pal(v, "quitar")) {
            const char *u = tras(v); int i = 0; while (*u >= '0' && *u <= '9') i = i * 10 + (*u++ - '0');
            if (i < 1 || i > c.nextra) { put(o, cap, &p, "noticias: no hay un canal con ese numero\n"); return 1; }
            for (int k = i - 1; k + 1 < c.nextra; k++) scpy(c.extra[k], c.extra[k + 1], NT_URL);
            c.nextra--; noticias_pon_conf(&c); put(o, cap, &p, "Canal quitado.\n"); return 1;
        }
        for (int i = 0; i < c.nextra; i++) { putd(o, cap, &p, i + 1); put(o, cap, &p, ". "); put(o, cap, &p, c.extra[i]); put(o, cap, &p, "\n"); }
        if (!c.nextra) put(o, cap, &p, "Sin canales propios. noticias fuente anadir <url del RSS>\n");
        return 1;
    }
    if (pal(a, "leer")) {                                          /* un canal guardado en un archivo, por el lector aislado */
        const char *ruta = tras(a);
        uint32_t L = 0; char *x = vfs_read_alloc(ruta, &L);
        if (!x) { put(o, cap, &p, "noticias: no se puede leer "); put(o, cap, &p, ruta); put(o, cap, &p, "\n"); return 1; }
        struct nt_item *it = (struct nt_item *)kmalloc(30 * sizeof *it);
        int n = it ? rss_aislado(x, L, it, 30, "") : -1;
        kfree(x);
        if (n < 0) { put(o, cap, &p, "noticias: el lector no ha podido con ese archivo\n"); if (it) kfree(it); return 1; }
        n = nt_limpia(it, n);
        put(o, cap, &p, "Leidos (en ring 3): "); putd(o, cap, &p, n); put(o, cap, &p, "\n");
        int64_t ya = noticias_ahora();
        for (int i = 0; i < n; i++) { char h[24]; nt_hace(ya, it[i].t, h, sizeof h);
            put(o, cap, &p, "  "); putd(o, cap, &p, i + 1); put(o, cap, &p, ". "); put(o, cap, &p, it[i].tit); put(o, cap, &p, " | "); put(o, cap, &p, it[i].fuente);
            put(o, cap, &p, " | "); put(o, cap, &p, h[0] ? h : "sin fecha"); put(o, cap, &p, " | "); put(o, cap, &p, it[i].url[0] ? it[i].url : "sin enlace"); put(o, cap, &p, "\n"); }
        kfree(it);
        return 1;
    }
    int resumen = pal(a, "resumen"), guardar = pal(a, "guardar");
    const char *nom = (resumen || guardar) ? tras(a) : a;
    int cat = *nom ? nt_cat_busca(nom) : NTC_PORTADA;
    if (cat < 0) { put(o, cap, &p, "noticias: categoria desconocida: "); put(o, cap, &p, nom); put(o, cap, &p, "\n"); return 1; }
    if (guardar) { char m[200]; noticias_guarda(cat, m, sizeof m); put(o, cap, &p, m); put(o, cap, &p, "\n"); return 1; }
    noticias_carga(cat, 0);
    int st = espera(cat, 0, 30000);
    struct nt_vista v; noticias_vista(cat, &v);
    put(o, cap, &p, nt_cat_nombre(cat)); put(o, cap, &p, ": ");
    if (st == NTE_CARGANDO) { put(o, cap, &p, "sigue cargando...\n"); return 1; }
    if (v.estado == NTE_ERROR || !v.n) { put(o, cap, &p, v.err[0] ? v.err : "sin noticias"); put(o, cap, &p, "\n"); return 1; }
    putd(o, cap, &p, v.n); put(o, cap, &p, " titulares\n");
    if (!resumen) { lista(cat, o, cap, &p, 12); return 1; }
    noticias_resume(cat);
    st = espera(cat, 1, 300000);
    noticias_vista(cat, &v);
    if (v.res_estado != NTE_BIEN) { put(o, cap, &p, "Sin resumen: "); put(o, cap, &p, v.res_err[0] ? v.res_err : "el modelo no ha respondido"); put(o, cap, &p, "\n"); lista(cat, o, cap, &p, 12); return 1; }
    char *r = (char *)kmalloc(NT_RES); if (!r) return 1;
    noticias_resumen(cat, r, NT_RES); put(o, cap, &p, r); put(o, cap, &p, "\n"); kfree(r);
    return 1;
}
