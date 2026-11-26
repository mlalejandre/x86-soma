/* SOMA_PARCHE_NAVEGADOR: cliente HTTP/1.1 (redirecciones, chunked, gzip, cookies). HTTPS mediante la pasarela del host. */
#include "web_net.h"
#include "web_inflate.h"
#include "tcp.h"
#include "tls13.h"      /* SOMA_176A: HTTPS directo (TLS 1.3 propio) */
#include "dns.h"
#include "net.h"
#if SOMA_WEB_ASYNC_IMAGES
#include "mutex.h"
#include "thread.h"
/* Protege cookie-jar, DNS cache, estado de pasarela y las peticiones del propio
 * navegador cuando hay hilo de imagenes. El net_client_lock sigue protegiendo TCP.
 * Se registra ANTES de arrancar el hilo para poder liberar el mutex tras un kill. */
static struct kmutex soma151_http_mutex;
static int soma151_http_ready;
void wn_http_lock_init(void)
{
    if (soma151_http_ready) return;
    kmutex_init(&soma151_http_mutex, "web_http_151");
    soma151_http_ready = 1;
}
static void soma151_http_lock(void) { if (soma151_http_ready) kmutex_lock(&soma151_http_mutex); }
static void soma151_http_unlock(void) { if (soma151_http_ready) kmutex_unlock(&soma151_http_mutex); }
#else
static void soma151_http_lock(void) { }
static void soma151_http_unlock(void) { }
#endif
static int soma151_is_cancelled(const volatile int *flag)
{
    return flag && __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

/* ------------------------------ URL ------------------------------ */
int wn_url_parse(const char *url, struct wn_url *u)
{
    memset(u, 0, sizeof *u);
    const char *p = url; while (wu_isspace(*p)) p++;
    const char *c = p; while (wu_isalnum(*c) || *c == '+' || *c == '-' || *c == '.') c++;
    if (*c == ':' && c > p) {
        uint32_t l = (uint32_t)(c - p); if (l >= sizeof u->scheme) return -1;
        for (uint32_t i = 0; i < l; i++) u->scheme[i] = (char)wu_lower(p[i]);
        p = c + 1;
    } else return -1;
    if (p[0] == '/' && p[1] == '/') {
        p += 2; u->has_host = 1; const char *e = p; while (*e && *e != '/' && *e != '?' && *e != '#') e++;
        const char *at = 0; for (const char *q = p; q < e; q++) if (*q == '@') at = q; if (at) p = at + 1;
        const char *colon = 0; for (const char *q = p; q < e; q++) if (*q == ':') colon = q;
        const char *he = colon && e > colon ? colon : e;
        if (p < e && *p == '[') { he = e; colon = 0; }
        uint32_t hl = (uint32_t)(he - p); if (hl >= sizeof u->host) hl = sizeof u->host - 1;
        for (uint32_t i = 0; i < hl; i++) u->host[i] = (char)wu_lower(p[i]);
        u->host[hl] = 0;
        if (colon) u->port = wu_atoi(colon + 1);
        p = e;
    }
    if (!u->port) u->port = wu_streq(u->scheme, "https") ? 443 : (wu_streq(u->scheme, "http") ? 80 : 0);
    const char *f = p; while (*f && *f != '#') f++;
    uint32_t pl = (uint32_t)(f - p); if (pl >= sizeof u->path) pl = sizeof u->path - 1;
    memcpy(u->path, p, pl); u->path[pl] = 0;
    if (*f == '#') wu_copy(u->frag, f + 1, sizeof u->frag);
    if (u->has_host && !u->path[0]) { u->path[0] = '/'; u->path[1] = 0; }
    if (u->has_host && u->path[0] == '?') { memmove(u->path + 1, u->path, wu_strlen(u->path) + 1); u->path[0] = '/'; }
    return 0;
}
static int scheme_is_hier(const char *s) { return wu_streq(s, "http") || wu_streq(s, "https") || wu_streq(s, "file") || wu_streq(s, "ftp"); }
void wn_url_origin(const char *url, char *out, uint32_t max)
{
    struct wn_url u; out[0] = 0; if (wn_url_parse(url, &u) || !u.has_host) return;
    uint32_t n = wu_copy(out, u.scheme, max); wu_copy(out + n, "://", max - n); n = wu_strlen(out); n += wu_copy(out + n, u.host, max - n);
    int def = wu_streq(u.scheme, "https") ? 443 : 80;
    if (u.port && u.port != def) { out[n++] = ':'; char t[12]; wu_itoa(u.port, t); wu_copy(out + n, t, max - n); }
}
/* SOMA 188B: con URL de 8192 la resolucion necesitaba ~80 KiB de pila (la llaman hilos con 60). Los bufer de trabajo viven en el
 * monton: un bloque que se reserva una vez y se reutiliza; si esta ocupado (otro hilo o la llamada recursiva de "//host") se usa
 * uno temporal. */
struct wn_rscr { char r[WN_URL_MAX + 64]; struct wn_url ru, bu; char path[WN_URL_MAX + 64]; char refn[WN_URL_MAX + 64]; char t[WN_URL_MAX + 80]; char bp[WN_URL_MAX]; char o[WN_URL_MAX + 300]; char tail[WN_URL_MAX]; char res[WN_URL_MAX]; };
static void remove_dots(char *path, char *tail, char *res)
{
    char *q = path; while (*q && *q != '?') q++;
    wu_copy(tail, q, WN_URL_MAX); *q = 0;
    uint32_t rl = 0;
    const char *s = path;
    while (*s) {
        if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || !s[2])) { while (rl > 0 && res[rl - 1] != '/') rl--; if (rl > 0) rl--; while (rl > 0 && res[rl - 1] != '/') rl--; s += s[2] ? 3 : 2; continue; }
        if (s[0] == '.' && (s[1] == '/' || !s[1])) { s += s[1] ? 2 : 1; continue; }
        if (s[0] == '/' && s[1] == '.' && s[2] == '.' && (s[3] == '/' || !s[3])) { while (rl > 0 && res[rl - 1] != '/') rl--; if (rl > 0) rl--; s += 3; if (!*s) res[rl++] = '/'; continue; }
        if (s[0] == '/' && s[1] == '.' && (s[2] == '/' || !s[2])) { s += 2; if (!*s) res[rl++] = '/'; continue; }
        if (rl + 1 < WN_URL_MAX) res[rl++] = *s;
        s++;
    }
    res[rl] = 0; if (!res[0]) { res[0] = '/'; res[1] = 0; }
    wu_copy(path, res, WN_URL_MAX); uint32_t l = wu_strlen(path); wu_copy(path + l, tail, WN_URL_MAX - l);
}
int wn_url_resolve(const char *base, const char *ref, char *out, uint32_t max);
static int resolve_impl(const char *base, const char *ref, char *out, uint32_t max, struct wn_rscr *S)
{
    char *r = S->r, *path = S->path, *refn = S->refn, *bp = S->bp, *o = S->o;
#define ru (S->ru)
#define bu (S->bu)
    while (wu_isspace(*ref)) ref++;
    uint32_t rl = wu_strlen(ref); while (rl && wu_isspace(ref[rl - 1])) rl--;
    wu_copyn(r, ref, rl, sizeof S->r);
    if (!wn_url_parse(r, &ru)) {   /* referencia absoluta */
        if (!scheme_is_hier(ru.scheme)) { wu_copy(out, r, max); return 0; }
        if (ru.has_host) { remove_dots(ru.path, S->tail, S->res); char org[200]; wn_url_origin(r, org, sizeof org); wu_copy(o, org, sizeof S->o); uint32_t n = wu_strlen(o); wu_copy(o + n, ru.path, sizeof S->o - n); n = wu_strlen(o); if (ru.frag[0]) { o[n++] = '#'; wu_copy(o + n, ru.frag, sizeof S->o - n); } wu_copy(out, o, max); return 0; }
    }
    if (wn_url_parse(base, &bu)) { wu_copy(out, r, max); return -1; }
    char org[200]; if (bu.has_host) wn_url_origin(base, org, sizeof org); else { wu_copy(org, bu.scheme, sizeof org); uint32_t n = wu_strlen(org); org[n++] = ':'; org[n] = 0; }
    const char *frag = 0;
    char *hash = 0; for (char *q = r; *q; q++) if (*q == '#') { hash = q; break; }
    wu_copy(refn, r, sizeof S->refn); if (hash) { refn[hash - r] = 0; frag = hash + 1; }
    if (r[0] == '/' && r[1] == '/') { char *t = S->t; wu_copy(t, bu.scheme, sizeof S->t); uint32_t n = wu_strlen(t); t[n++] = ':'; wu_copy(t + n, r, sizeof S->t - n); return wn_url_resolve(base, t, out, max); }
    if (!refn[0]) wu_copy(path, bu.path, sizeof S->path);
    else if (refn[0] == '/') wu_copy(path, refn, sizeof S->path);
    else if (refn[0] == '?') { wu_copy(bp, bu.path, sizeof S->bp); for (char *q = bp; *q; q++) if (*q == '?') { *q = 0; break; } wu_copy(path, bp, sizeof S->path); uint32_t n = wu_strlen(path); wu_copy(path + n, refn, sizeof S->path - n); }
    else {
        wu_copy(bp, bu.path, sizeof S->bp); for (char *q = bp; *q; q++) if (*q == '?') { *q = 0; break; }
        char *sl = 0; for (char *q = bp; *q; q++) if (*q == '/') sl = q;
        if (sl) sl[1] = 0; else { bp[0] = '/'; bp[1] = 0; }
        if (!bu.has_host && !bp[0]) { bp[0] = '/'; bp[1] = 0; }
        wu_copy(path, bp, sizeof S->path); uint32_t n = wu_strlen(path); wu_copy(path + n, refn, sizeof S->path - n);
    }
    if (refn[0]) remove_dots(path, S->tail, S->res);
    wu_copy(o, org, sizeof S->o); uint32_t n = wu_strlen(o);
    wu_copy(o + n, path, sizeof S->o - n); n = wu_strlen(o);
    if (frag && *frag) { o[n++] = '#'; wu_copy(o + n, frag, sizeof S->o - n); }
    wu_copy(out, o, max); return 0;
}
#undef ru
#undef bu
int wn_url_resolve(const char *base, const char *ref, char *out, uint32_t max)
{
    static struct wn_rscr *cache; static int busy;
    struct wn_rscr *S = 0; int own = 0;
    if (!__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE)) {
        if (!cache) cache = (struct wn_rscr *)kmalloc(sizeof *cache);
        if (cache) { S = cache; own = 1; } else __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
    }
    if (!S) { S = (struct wn_rscr *)kmalloc(sizeof *S); if (!S) { wu_copy(out, ref, max); return -1; } }
    int rc = resolve_impl(base, ref, out, max, S);
    if (own) __atomic_store_n(&busy, 0, __ATOMIC_RELEASE); else kfree(S);
    return rc;
}
void wn_url_encode(const char *s, char *out, uint32_t max)
{
    static const char hx[] = "0123456789ABCDEF"; uint32_t k = 0;
    for (; *s && k + 4 < max; s++) {
        unsigned char c = (unsigned char)*s;
        if (wu_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
        else { out[k++] = '%'; out[k++] = hx[c >> 4]; out[k++] = hx[c & 15]; }
    }
    out[k] = 0;
}

/* ------------------------------ cookies ------------------------------ */
#define WN_COOKIES 96
struct wcookie { char host[96]; char path[96]; char name[48]; char val[256]; uint8_t used, hostonly, secure, httponly; };
static struct wcookie jar[WN_COOKIES]; static uint32_t jar_rr;
static int domain_match(const char *host, const char *dom, int hostonly)
{
    if (hostonly) return wu_streq(host, dom);
    uint32_t hl = wu_strlen(host), dl = wu_strlen(dom);
    if (hl < dl) return 0;
    if (!wu_streq(host + hl - dl, dom)) return 0;
    return hl == dl || host[hl - dl - 1] == '.';
}
static int path_match(const char *rp, const char *cp)
{
    uint32_t cl = wu_strlen(cp); if (!wu_strneq(rp, cp, cl)) return 0;
    return cl == 0 || cp[cl - 1] == '/' || rp[cl] == '/' || rp[cl] == 0 || rp[cl] == '?';
}
static uint32_t jar_gen;                                                /* SOMA_203: cambia con cada cookie guardada o borrada */
void wn_cookie_clear(void) { soma151_http_lock(); memset(jar, 0, sizeof jar); jar_gen++; soma151_http_unlock(); }
uint32_t wn_cookie_gen(void) { return jar_gen; }
/* SOMA_203: el tarro entero, para que el navegador aislado lo conserve si su proceso se cae. Al importar no se cree nada: cada
 * cadena se termina a la fuerza y los indicadores se reducen a 0/1. */
uint32_t wn_cookie_export(void *out, uint32_t max)
{
    if (max < sizeof jar) return 0;
    soma151_http_lock(); memcpy(out, jar, sizeof jar); soma151_http_unlock();
    return (uint32_t)sizeof jar;
}
int wn_cookie_import(const void *in, uint32_t len)
{
    if (len != sizeof jar) return -1;
    soma151_http_lock();
    memcpy(jar, in, sizeof jar);
    for (int i = 0; i < WN_COOKIES; i++) {
        struct wcookie *k = &jar[i];
        k->host[sizeof k->host - 1] = 0; k->path[sizeof k->path - 1] = 0; k->name[sizeof k->name - 1] = 0; k->val[sizeof k->val - 1] = 0;
        k->used = !!k->used; k->hostonly = !!k->hostonly; k->secure = !!k->secure; k->httponly = !!k->httponly;
        if (!k->host[0] || !k->name[0]) k->used = 0;
    }
    jar_gen++;
    soma151_http_unlock();
    return 0;
}
static void wn_cookie_store_impl(const char *url, const char *sc)
{
    struct wn_url u; if (wn_url_parse(url, &u) || !u.has_host) return;
    struct wcookie c; memset(&c, 0, sizeof c); c.hostonly = 1; wu_copy(c.host, u.host, sizeof c.host);
    /* ruta por defecto */
    wu_copy(c.path, u.path, sizeof c.path); for (char *q = c.path; *q; q++) if (*q == '?') { *q = 0; break; }
    { char *sl = 0; for (char *q = c.path; *q; q++) if (*q == '/') sl = q; if (sl && sl != c.path) *sl = 0; else { c.path[0] = '/'; c.path[1] = 0; } }
    const char *p = sc; const char *e = p; while (*e && *e != ';') e++;
    const char *eq = p; while (eq < e && *eq != '=') eq++;
    if (eq == e) return;
    uint32_t nl = (uint32_t)(eq - p); while (nl && wu_isspace(p[nl - 1])) nl--; while (nl && wu_isspace(*p)) { p++; nl--; }
    if (!nl || nl >= sizeof c.name) return;
    memcpy(c.name, p, nl); c.name[nl] = 0; wu_copyn(c.val, eq + 1, (uint32_t)(e - eq - 1), sizeof c.val);
    int del = 0;
    while (*e) {
        e++; while (wu_isspace(*e)) e++; const char *a = e; while (*e && *e != ';') e++;
        if (wu_startsi(a, "domain=")) { const char *v = a + 7; if (*v == '.') v++; char d[96]; wu_copyn(d, v, (uint32_t)(e - v), sizeof d); for (char *q = d; *q; q++) *q = (char)wu_lower(*q); if (domain_match(u.host, d, 0)) { wu_copy(c.host, d, sizeof c.host); c.hostonly = 0; } else return; }
        else if (wu_startsi(a, "path=")) { wu_copyn(c.path, a + 5, (uint32_t)(e - a - 5), sizeof c.path); if (c.path[0] != '/') { c.path[0] = '/'; c.path[1] = 0; } }
        else if (wu_startsi(a, "max-age=")) { int v = wu_atoi(a + 8); if (v <= 0) del = 1; }
        else if (wu_startsi(a, "expires=")) { if (wu_findi(a, "1970") && a < e) del = 1; }
        else if (wu_startsi(a, "secure")) c.secure = 1;
        else if (wu_startsi(a, "httponly")) c.httponly = 1;
    }
    for (int i = 0; i < WN_COOKIES; i++) {
        struct wcookie *k = &jar[i];
        if (k->used && wu_streq(k->host, c.host) && wu_streq(k->name, c.name) && wu_streq(k->path, c.path)) { if (del) k->used = 0; else { c.used = 1; *k = c; } return; }
    }
    if (del) return;
    c.used = 1; for (int i = 0; i < WN_COOKIES; i++) if (!jar[i].used) { jar[i] = c; return; }
    jar[jar_rr++ % WN_COOKIES] = c;
}
void wn_cookie_store(const char *url, const char *sc)
{ soma151_http_lock(); wn_cookie_store_impl(url, sc); jar_gen++; soma151_http_unlock(); }
static int wn_cookie_header_impl(const char *url, char *out, uint32_t max, int for_js)
{
    struct wn_url u; out[0] = 0; if (wn_url_parse(url, &u) || !u.has_host) return 0;
    uint32_t n = 0;
    for (int i = 0; i < WN_COOKIES; i++) {
        struct wcookie *k = &jar[i]; if (!k->used) continue;
        if (!domain_match(u.host, k->host, k->hostonly) || !path_match(u.path, k->path)) continue;
        if (k->secure && !wu_streq(u.scheme, "https")) continue;
        if (for_js && k->httponly) continue;
        uint32_t need = wu_strlen(k->name) + wu_strlen(k->val) + 3; if (n + need >= max) break;
        if (n) { out[n++] = ';'; out[n++] = ' '; }
        n += wu_copy(out + n, k->name, max - n); out[n++] = '='; n += wu_copy(out + n, k->val, max - n);
    }
    out[n] = 0; return (int)n;
}

int wn_cookie_header(const char *url, char *out, uint32_t max, int for_js)
{ soma151_http_lock(); int n = wn_cookie_header_impl(url, out, max, for_js); soma151_http_unlock(); return n; }
/* ------------------------------ transporte ------------------------------ */
static uint8_t gw_ip[4] = { 10, 0, 2, 2 };
static int raw_gw_state = -1;     /* -1 desconocido, 0 no disponible, 1 disponible (puerto 8096) */
static uint64_t raw_gw_t;         /* SOMA_PARCHE_028_UI: cuando se vio que no estaba (se vuelve a probar pasado un rato) */
static int gw_caps;
int wn_gateway_caps(void) { soma151_http_lock(); int n = raw_gw_state == 1 ? gw_caps : 0; soma151_http_unlock(); return n; }

struct dnse { char host[96]; uint8_t ip[4]; uint64_t t; };
static struct dnse dnsc[16]; static uint32_t dnsc_n;
static int resolve_host(const char *host, uint8_t *ip)
{
    int dots = 0, isip = 1; for (const char *s = host; *s; s++) { if (*s == '.') dots++; else if (!wu_isdigit(*s)) isip = 0; }
    if (isip && dots == 3) { const char *s = host; for (int i = 0; i < 4; i++) { int v = 0; while (wu_isdigit(*s)) v = v * 10 + (*s++ - '0'); ip[i] = (uint8_t)v; if (*s == '.') s++; } return 1; }
    uint64_t now = web_now_ms();
    for (int i = 0; i < 16; i++) if (dnsc[i].host[0] && wu_streq(dnsc[i].host, host) && now - dnsc[i].t < 300000) { memcpy(ip, dnsc[i].ip, 4); return 1; }
    if (!dns_resolve(host, ip, 4000)) { if (!dns_resolve(host, ip, 4000)) return 0; }
    struct dnse *e = &dnsc[dnsc_n++ % 16]; wu_copy(e->host, host, sizeof e->host); memcpy(e->ip, ip, 4); e->t = now; return 1;
}

static int find_hdr_end(const char *p, uint32_t n)
{
    for (uint32_t i = 0; i + 3 < n; i++) if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') return (int)i + 4;
    return -1;
}
/* SOMA_176A: HTTPS directo. En QEMU (red 10.0.2.x) las paginas https siguen yendo por la pasarela del anfitrion; en una red real
 * (el portatil) no existe pasarela y SOMA habla TLS 1.3 directamente con el sitio. El certificado NO se verifica (ver tls13.c). */
#ifdef SOMA_NETSVC      /* SOMA_316_RED_AISLADA: servicio aislado de ring 3 (netsvc.elf): con TLS propio; si la red es real lo dice el nucleo */
extern int netsvc_https_directo;
static int https_direct(void) { return netsvc_https_directo; }
#else
extern uint8_t net_ip[4];
#ifdef __x86_64__
#include "bootfb.h"
#endif
static int https_direct(void)
{
#ifdef __x86_64__
    if (bootinfo_disabled("httpsdirecto")) return 0;                    /* soma.httpsdirecto=off: https solo por la pasarela, como antes */
#endif
#ifdef SOMA_FORCE_HTTPS_DIRECT
    return 1;                                                           /* solo pruebas en QEMU */
#else
    return !(net_ip[0] == 10 && net_ip[1] == 0 && net_ip[2] == 2) && (net_ip[0] | net_ip[1] | net_ip[2] | net_ip[3]);
#endif
}
#endif
#if !defined(SOMA_NETSVC) && !defined(WEB_HOST_TEST)
#include "tls_trust.h"
int wn_https_unverified(void) { return https_direct() && !tls13_verify; }   /* SOMA_194: candado ambar solo con soma.tlsverificar=off; si no, verde = certificado verificado */
int wn_https_directo(void) { return https_direct(); }                    /* SOMA_316_RED_AISLADA: se lo dice a netsvc */
#else
int wn_https_unverified(void) { return https_direct(); }                 /* para el candado del navegador */
#endif
static char tls_last_err[96];
const char *wn_tls_last_error(void) { return tls_last_err; }
struct tioctx { int h; };
static int tio_send(void *c, const uint8_t *d, uint32_t n) { int h = ((struct tioctx *)c)->h; while (n) { uint32_t k = n > 4096 ? 4096 : n; if (tcp_send(h, d, k) < 0) return -1; d += k; n -= k; } return 0; }
static int tio_recv(void *c, uint8_t *d, uint32_t max, uint32_t to) { int r = tcp_recv(((struct tioctx *)c)->h, d, (uint16_t)(max > 16384 ? 16384 : max), to); return r == TCP_EOF ? TLS_IO_EOF : r == TCP_ERR ? TLS_IO_ERR : r; }
static uint64_t tio_now(void) { return web_now_ms(); }
static void tio_yield(void) { web_yield(); }
/* Una conexion completa: envia `req` y recoge la respuesta (cabeceras + cuerpo en bruto, completo segun CL/chunked/EOF). */
static int http_exchange(const uint8_t *ip, uint16_t port, const char *req, uint32_t reqlen, const char *body, uint32_t blen,
                         struct wb *out, uint32_t max_total, uint32_t timeout_ms, int *hdr_len_out,
                         const volatile int *cancel, const char *tls_sni)
{
    uint64_t deadline = web_now_ms() + timeout_ms;
    net_client_lock();
    int h;
#if SOMA_WEB_ASYNC_IMAGES
    if (cancel) {
        h = tcp_connect_start(ip, port);
        uint64_t connect_deadline = web_now_ms() + (timeout_ms > 8000 ? 8000 : timeout_ms);
        if (h >= 0) {
            int st = 0;
            while (!soma151_is_cancelled(cancel) && web_now_ms() < connect_deadline &&
                   (st = tcp_connect_poll(h)) == 0) thread_sleep(1);
            if (st != 1) {
                if (st == 0) tcp_close(h); /* tcp_connect_poll libera por si mismo en error */
                net_client_unlock(); return soma151_is_cancelled(cancel) ? -13 : -1;
            }
        }
    } else
#endif
    h = tcp_connect(ip, port, timeout_ms > 8000 ? 8000 : timeout_ms);
    if (h < 0) { net_client_unlock(); return -1; }
    int rc = 0;
    struct tls13 *tls = 0; struct tioctx tctx = { h };
    if (tls_sni) {                                                       /* SOMA_176A: apreton de manos TLS 1.3 sobre la conexion TCP */
        tls = (struct tls13 *)kmalloc(sizeof *tls);
        if (!tls) { tcp_close(h); net_client_unlock(); return -3; }
        struct tls_io io = { &tctx, tio_send, tio_recv, tio_now, tio_yield };
        uint64_t left = deadline > web_now_ms() ? deadline - web_now_ms() : 1000; if (left > 20000) left = 20000;
#if !defined(SOMA_NETSVC) && !defined(WEB_HOST_TEST)
        tls_trust_setup();                                               /* SOMA_194: autoridades de confianza, reloj y politica */
#endif
        if (tls13_connect(tls, &io, tls_sni, (uint32_t)left)) {
            const char *en = tls13_err_name(tls->err); uint32_t k = 0; while (en[k] && k < 86) { tls_last_err[k] = en[k]; k++; } tls_last_err[k] = 0;
            if (tls->alert) { tls_last_err[k++] = ' '; tls_last_err[k++] = '('; k += wu_itoa(tls->alert, tls_last_err + k); tls_last_err[k++] = ')'; tls_last_err[k] = 0; }
            kfree(tls); tcp_close(h); net_client_unlock(); return -14;
        }
    }
#define XSEND(d, n) (tls ? tls13_send(tls, (d), (n)) : tcp_send(h, (d), (n)))
    if (XSEND(req, reqlen) < 0) rc = -2;
    else if (blen) {
        uint32_t off = 0; while (off < blen && rc == 0) { uint32_t c = blen - off > 1200 ? 1200 : blen - off; if (XSEND(body + off, c) < 0) rc = -2; off += c; }
    }
    int hl = -1; int chunked = 0; int64_t clen = -1;
    char *tmp = (char *)kmalloc(16384); if (!tmp) { tcp_close(h); net_client_unlock(); return -3; }
    while (rc == 0) {
        if (soma151_is_cancelled(cancel)) { rc = -13; break; }
        if (web_now_ms() > deadline) { rc = out->len ? 0 : -4; break; }
        /* 100 ms en el hilo IO permite reaccionar a un cambio de pagina.
         * El contrato de tcp_recv(0) es: no hay datos todavia. */
        int n = tls ? tls13_recv(tls, tmp, 16384, cancel ? 100 : 3000) : tcp_recv(h, tmp, 16384, cancel ? 100 : 3000);
        if (tls) { if (n == TLS_IO_EOF) n = TCP_EOF; else if (n == TLS_IO_ERR) n = TCP_ERR; }
        if (n > 0) {
            if (out->len + (uint32_t)n > max_total) { rc = 0; break; }
            wb_addn(out, tmp, (uint32_t)n); if (out->err) { rc = -3; break; }
            wu_tick();                 /* SOMA_PARCHE_028_UI: con datos llegando sin pausa el bucle no cedia nunca la CPU */
            if (hl < 0) {
                hl = find_hdr_end(out->p, out->len);
                if (hl > 0) {
                    const char *hp = out->p; const char *he = out->p + hl;
                    for (const char *l = hp; l < he; ) {
                        const char *e = l; while (e < he && *e != '\r') e++;
                        if (wu_startsi(l, "content-length:")) clen = wu_atoi(l + 15);
                        else if (wu_startsi(l, "transfer-encoding:") && wu_findi(l, "chunked") && wu_findi(l, "chunked") < e) chunked = 1;
                        l = e + 2;
                    }
                    if (wu_starts(hp, "HTTP/1.1 204") || wu_starts(hp, "HTTP/1.1 304") || wu_starts(hp, "HTTP/1.0 204")) clen = 0;
                }
            }
            if (hl > 0) {
                if (clen >= 0 && out->len >= (uint32_t)hl + (uint32_t)clen) break;
                if (chunked && out->len >= 5 && out->len > (uint32_t)hl && !memcmp(out->p + out->len - 5, "0\r\n\r\n", 5)) break;
            }
            continue;
        }
        if (n == TCP_EOF || n == TCP_ERR) { if (!out->len && n == TCP_ERR) rc = -5; break; }
        web_yield();
    }
    kfree(tmp);
    if (tls) { if (rc == 0 || rc == -4) tls13_close(tls); kfree(tls); }   /* SOMA_176A */
    /* Si se cancela un recurso, evitar esperar los tres RTO de tcp_close.
     * Este hilo de IO solo posee la conexion de esta imagen. */
#if SOMA_WEB_ASYNC_IMAGES
    if (rc == -13 && cancel && thread_current() && tcp_owner(h) == thread_current()->tid)
        tcp_cleanup_for_thread(thread_current()->tid);
    else
#endif
        tcp_close(h);
    net_client_unlock();
    *hdr_len_out = hl;
    if (rc == 0 && hl < 0) rc = -6;
    return rc;
}
static int hexv(int c) { return wu_ishex(c) ? wu_hexval(c) : -1; }
static int dechunk(const char *p, uint32_t n, struct wb *out)
{
    uint32_t i = 0;
    for (;;) {
        int64_t sz = 0; int any = 0;
        while (i < n && hexv(p[i]) >= 0) { sz = sz * 16 + hexv(p[i]); i++; any = 1; if (sz > 0x7fffffff) return -1; }
        if (!any) return out->len ? 0 : -1;
        while (i < n && p[i] != '\n') i++;
        if (i >= n) return out->len ? 0 : -1;         /* SOMA_201: la linea del tamano no termina (respuesta cortada u hostil). Antes i pasaba de n
                                                        * y n - i daba la vuelta: se copiaban 4 GiB desde el final del bufer (escritura fuera del heap del nucleo) */
        i++;
        if (sz == 0) return 0;
        if ((uint64_t)i + (uint64_t)sz > n) { wb_addn(out, p + i, n - i); return 0; }
        wb_addn(out, p + i, (uint32_t)sz); i += (uint32_t)sz;
        if (i < n && p[i] == '\r') i++;
        if (i < n && p[i] == '\n') i++;
    }
}

void wn_free(struct wn_resp *rs) { if (rs && rs->body) { kfree(rs->body); rs->body = 0; rs->len = 0; } }

static void append_hdr(struct wb *r, const char *k, const char *v) { wb_add(r, k); wb_add(r, ": "); wb_add(r, v); wb_add(r, "\r\n"); }

/* un salto de peticion: devuelve 0 con status/cuerpo/cabeceras de interes */
struct hop { int status; char location[WN_URL_MAX]; struct wb body; char ctype[96]; };
/* Escapa espacios, controles y bytes no ASCII de una URL (un formulario o un enlace pueden traer "q=el pais" sin codificar) */
static void url_sanitize(const char *s, char *out, uint32_t max)
{
    static const char hx[] = "0123456789ABCDEF"; uint32_t k = 0;
    for (; *s && k + 4 < max; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>' || c == '\\' || c == '^' || c == '`' || c == '{' || c == '|' || c == '}') { out[k++] = '%'; out[k++] = hx[c >> 4]; out[k++] = hx[c & 15]; }
        else out[k++] = (char)c;
    }
    out[k] = 0;
}
static int do_hop(const struct wn_req *rq, const char *url_in, const char *method, int use_body, struct hop *H, int *via_gw)
{
    if (soma151_is_cancelled(rq->cancel)) return -13;
    char url[WN_URL_MAX * 3]; url_sanitize(url_in, url, sizeof url);
    int gwcmd = wu_starts(url_in, "soma-gw:/");              /* SOMA_PARCHE_028_UI: orden directa a la pasarela */
    struct wn_url u; if (gwcmd) memset(&u, 0, sizeof u); else if (wn_url_parse(url, &u) || !u.has_host) return -10;
    if (raw_gw_state == 0 && web_now_ms() - raw_gw_t > 20000) raw_gw_state = -1;   /* la pasarela pudo arrancar despues */
    char cookie[900]; wn_cookie_header(url, cookie, sizeof cookie, 0);
    if (rq->extra_headers) cookie[0] = 0;                               /* SOMA_195: las peticiones del LLM no llevan las cookies del navegador */
    uint8_t ip[4]; const char *connect_host = u.host; uint16_t port = (uint16_t)u.port; char path[WN_URL_MAX * 3 + 64];
    int gateway = 0; int legacy = 0; int direct_tls = 0;
    if (gwcmd || wu_streq(u.scheme, "https")) {
        if (!gwcmd && !rq->via_gateway && (https_direct() || rq->direct)) direct_tls = 1;   /* SOMA_176A: red real: TLS 1.3 propio, sin pasarela. SOMA_195: o si se pide */
        else gateway = 1;
    } else if (!wu_streq(u.scheme, "http")) return -11;
    else if (rq->via_gateway) gateway = 1;
    if ((gwcmd || rq->via_gateway) && (raw_gw_state == 0 || https_direct())) return -1;
    if (gateway && rq->extra_headers && rq->extra_headers[0]) return -1;   /* SOMA_195: una clave de API no pasa por la pasarela (va en claro hasta el anfitrion) */
    struct wb req = {0};
    char enc[WN_URL_MAX * 3 + 8];
    if (gateway) {
        memcpy(ip, gw_ip, 4); connect_host = "10.0.2.2"; port = 8096;
        wn_url_encode(url, enc, sizeof enc);
        wu_copy(path, "/raw?url=", sizeof path); uint32_t n = wu_strlen(path); wu_copy(path + n, enc, sizeof path - n);
        if (gwcmd) wu_copy(path, url_in + 8, sizeof path);
        if (raw_gw_state == 0) { legacy = 1; port = 8095; if (rq->want_binary) { wu_copy(path, "/fetch?url=", sizeof path); n = wu_strlen(path); wu_copy(path + n, enc, sizeof path - n); } else { wu_copy(path, "/web?q=", sizeof path); n = wu_strlen(path); wu_copy(path + n, url, sizeof path - n); } }
    } else {
        if (!resolve_host(u.host, ip)) { wb_free(&req); return -12; }
        if (soma151_is_cancelled(rq->cancel)) { wb_free(&req); return -13; }
        wu_copy(path, u.path, sizeof path);
    }
    wb_add(&req, method); wb_add(&req, " "); wb_add(&req, path); wb_add(&req, " HTTP/1.1\r\n");
    char hostv[160]; if (gateway) wu_copy(hostv, "10.0.2.2", sizeof hostv); else { wu_copy(hostv, u.host, sizeof hostv); if (u.port != (direct_tls ? 443 : 80)) { uint32_t n = wu_strlen(hostv); hostv[n++] = ':'; wu_itoa(u.port, hostv + n); } }
    append_hdr(&req, "Host", hostv);
    append_hdr(&req, "User-Agent", "Mozilla/5.0 (X11; SOMA) AppleWebKit/537.36 (KHTML, like Gecko) SOMA-Browser/2.0 Safari/537.36");
    append_hdr(&req, "Accept", rq->accept ? rq->accept : "text/html,application/xhtml+xml,application/xml;q=0.9,image/*;q=0.8,*/*;q=0.5");
    append_hdr(&req, "Accept-Language", "es-ES,es;q=0.9,en;q=0.8");
    append_hdr(&req, "Accept-Encoding", legacy ? "identity" : "gzip, deflate");
    if (rq->referer && *rq->referer) append_hdr(&req, "Referer", rq->referer);
    if (rq->extra_headers && !gateway) wb_add(&req, rq->extra_headers);   /* SOMA_195 */
    if (gateway && !legacy) {
        append_hdr(&req, "X-Soma-Method", method);
        if (cookie[0]) append_hdr(&req, "X-Soma-Cookie", cookie);
        if (rq->referer && *rq->referer) append_hdr(&req, "X-Soma-Referer", rq->referer);
        append_hdr(&req, "X-Soma-Accept", rq->accept ? rq->accept : "*/*");
    } else if (cookie[0] && !legacy) append_hdr(&req, "Cookie", cookie);
    if (use_body && rq->body_len) {
        char t[16]; wu_itoa(rq->body_len, t); append_hdr(&req, "Content-Length", t);
        append_hdr(&req, "Content-Type", rq->ctype ? rq->ctype : "application/x-www-form-urlencoded");
    } else if (wu_streq(method, "POST")) append_hdr(&req, "Content-Length", "0");
    append_hdr(&req, "Connection", "close"); wb_add(&req, "\r\n");
    if (req.err) { wb_free(&req); return -3; }
    struct wb raw = {0}; int hl = -1;
    uint32_t tmo = rq->timeout_ms ? rq->timeout_ms : 15000, maxb = rq->max_body ? rq->max_body : (8u << 20);
    int rc = http_exchange(ip, port, req.p, req.len, use_body ? rq->body : 0, use_body ? rq->body_len : 0, &raw, maxb + 65536, tmo, &hl, rq->cancel, direct_tls ? u.host : 0);
    wb_free(&req);
    if (gateway && rc == -1 && raw_gw_state != 0 && !legacy) {
        /* no hay pasarela binaria: se prueba el proxy antiguo */
        raw_gw_state = 0; raw_gw_t = web_now_ms(); wb_free(&raw);
        if (gwcmd || rq->via_gateway) return -1;
        return do_hop(rq, url, method, use_body, H, via_gw);
    }
    if (gateway && rc == 0 && !legacy) raw_gw_state = 1;
    if (rc < 0) { wb_free(&raw); return rc; }
    /* analisis de cabeceras */
    H->status = 0; H->location[0] = 0; H->ctype[0] = 0; int chunked = 0, enc_gz = 0, enc_df = 0;
    const char *p = raw.p; if (raw.len < 12 || !wu_starts(p, "HTTP/1.")) { wb_free(&raw); return -7; }
    const char *sp = p; while (*sp && *sp != ' ') sp++; H->status = wu_atoi(sp);
    const char *he = p + hl;
    for (const char *l = p; l < he; ) {
        const char *e = l; while (e < he && *e != '\r') e++;
        if (wu_startsi(l, "location:")) { const char *v = l + 9; while (*v == ' ') v++; wu_copyn(H->location, v, (uint32_t)(e - v), sizeof H->location); }
        else if (wu_startsi(l, "content-type:")) { const char *v = l + 13; while (*v == ' ') v++; wu_copyn(H->ctype, v, (uint32_t)(e - v), sizeof H->ctype); }
        else if (wu_startsi(l, "transfer-encoding:")) { if (wu_findi(l, "chunked")) chunked = 1; }
        else if (wu_startsi(l, "content-encoding:")) { char t[40]; wu_copyn(t, l + 17, (uint32_t)(e - l - 17), sizeof t); if (wu_findi(t, "gzip")) enc_gz = 1; else if (wu_findi(t, "deflate")) enc_df = 1; }
        else if (wu_startsi(l, "set-cookie:") || wu_startsi(l, "x-soma-set-cookie:")) { const char *v = l + (l[0] == 'x' || l[0] == 'X' ? 18 : 11); while (*v == ' ') v++; char t[512]; wu_copyn(t, v, (uint32_t)(e - v), sizeof t); wn_cookie_store(url, t); }
        else if (gateway && !legacy && wu_startsi(l, "x-soma-status:")) H->status = wu_atoi(l + 14);
        else if (gateway && !legacy && wu_startsi(l, "x-soma-gateway:")) {
            char t[96]; wu_copyn(t, l + 15, (uint32_t)(e - l - 15), sizeof t);
            gw_caps = WN_GW_PRESENT | (wu_findi(t, "webp") ? WN_GW_IMAGES : 0) | (wu_findi(t, "svg") ? WN_GW_SVG : 0);
        }
        l = e + 2;
    }
    const char *bp = p + hl; uint32_t bl = raw.len - (uint32_t)hl;
    struct wb body = {0};
    if (chunked) { if (dechunk(bp, bl, &body) < 0 && !body.len) { wb_addn(&body, bp, bl); } } else wb_addn(&body, bp, bl);
    wb_free(&raw);
    if ((enc_gz || enc_df) && body.len) {
        struct wb un = {0}; int zr = wz_inflate((const uint8_t *)body.p, body.len, enc_gz ? 2 : 3, &un, maxb + 65536);
        if (zr < 0 && !un.len) { wb_free(&un); wb_free(&body); return -8; }
        wb_free(&body); body = un;
    }
    H->body = body; *via_gw = gateway; (void)connect_host;
    return 0;
}

static int wn_fetch_impl(const struct wn_req *rq, struct wn_resp *rs)
{
    memset(rs, 0, sizeof *rs);
    char cur[WN_URL_MAX]; wu_copy(cur, rq->url, sizeof cur);
    const char *method = rq->method && *rq->method ? rq->method : "GET"; char mbuf[8]; wu_copy(mbuf, method, sizeof mbuf); method = mbuf;
    int use_body = rq->body_len > 0 && wu_streq(method, "POST");
    for (int hop = 0; hop < 10; hop++) {
        if (soma151_is_cancelled(rq->cancel)) { wu_copy(rs->err, "cancelada", sizeof rs->err); return -13; }
        struct hop H; memset(&H, 0, sizeof H); int via = 0;
        int rc = do_hop(rq, cur, method, use_body, &H, &via);
        if (rc < 0) {
            const char *m = rc == -13 ? "cancelada" : rc == -1 ? "no se pudo conectar" : rc == -12 ? "no se pudo resolver el nombre" : rc == -4 ? "tiempo agotado" : rc == -14 ? "no se pudo establecer la conexion segura (TLS)" : rc == -11 ? "protocolo no soportado" : "error de red";
            wu_copy(rs->err, m, sizeof rs->err); wu_copy(rs->final_url, cur, sizeof rs->final_url); return rc;
        }
        rs->via_gateway = via;
        if (H.status >= 300 && H.status < 400 && H.location[0] && !rq->no_redirect) {
            char nxt[WN_URL_MAX]; wn_url_resolve(cur, H.location, nxt, sizeof nxt); wb_free(&H.body);
            if (H.status == 303 || ((H.status == 301 || H.status == 302) && wu_streq(method, "POST"))) { wu_copy(mbuf, "GET", sizeof mbuf); use_body = 0; }
            wu_copy(cur, nxt, sizeof cur); rs->redirects++; continue;
        }
        rs->status = H.status; wu_copy(rs->final_url, cur, sizeof rs->final_url); wu_copy(rs->ctype, H.ctype, sizeof rs->ctype);
        const char *cs = wu_findi(H.ctype, "charset="); if (cs) { cs += 8; if (*cs == '"') cs++; uint32_t k = 0; while (cs[k] && cs[k] != ';' && cs[k] != '"' && k + 1 < sizeof rs->charset) { rs->charset[k] = (char)wu_lower(cs[k]); k++; } rs->charset[k] = 0; }
        for (char *q = rs->ctype; *q; q++) if (*q == ';') { *q = 0; break; }
        for (char *q = rs->ctype; *q; q++) *q = (char)wu_lower(*q);
        rs->body = H.body.p; rs->len = H.body.len; if (!rs->body) { rs->body = (char *)kmalloc(1); if (rs->body) rs->body[0] = 0; }
        return 0;
    }
    wu_copy(rs->err, "demasiadas redirecciones", sizeof rs->err); return -9;
}

/* SOMA 188B: con URL de 8192 la peticion necesita unos 150 KiB de pila (bufer de URL, ruta codificada, redirecciones) y un hilo
 * del kernel tiene 60: en el nucleo wn_fetch se ejecuta en una pila propia de 256 KiB (la consola 'tls'/'descargar' lo llama desde
 * su hilo). En las pruebas del anfitrion y en netsvc (URL de 1024) se llama directamente. */
#if (defined(__x86_64__) || defined(__aarch64__)) && !defined(WEB_HOST_TEST) && !defined(SOMA_HOST_TEST) && !defined(SOMA_NETSVC)
#define WN_FETCH_OWN_STACK 1
#include "pila_canario.h"   /* SOMA_306_PILAS */
#if defined(SOMA_WEBSVC)
#define WN_PILA_AVISO(m) ((void)0)                                /* navegador aislado (ring 3): basta con parar el proceso */
#else
#include "console.h"
#define WN_PILA_AVISO(m) kprint(m)
#endif
void web_call_on_stack(char *top, void (*fn)(void *), void *arg);
struct wn_fetch_args { const struct wn_req *rq; struct wn_resp *rs; int rc; };
static void wn_fetch_thunk(void *u) { struct wn_fetch_args *a = (struct wn_fetch_args *)u; a->rc = wn_fetch_impl(a->rq, a->rs); }
#endif
int wn_fetch(const struct wn_req *rq, struct wn_resp *rs)
{
    soma151_http_lock();
#ifdef WN_FETCH_OWN_STACK
    int rc; char *stk = (char *)kmalloc(256u * 1024u);
    if (stk) {
        struct wn_fetch_args a = { rq, rs, 0 };
        pila_canario_pon(stk);                                   /* SOMA_306_PILAS: esta pila no tiene pagina de guarda */
        web_call_on_stack((char *)(((uintptr_t)stk + 256u * 1024u) & ~(uintptr_t)15), wn_fetch_thunk, &a); rc = a.rc;
        if (!pila_canario_ok(stk)) { WN_PILA_AVISO("\n[PILA] desbordada la pila de 256 KiB de wn_fetch (el monton de debajo esta corrupto)\n"); __builtin_trap(); }
        kfree(stk);
    }
    else { memset(rs, 0, sizeof *rs); wu_copy(rs->err, "sin memoria", sizeof rs->err); rc = -3; }
#else
    int rc = wn_fetch_impl(rq, rs);
#endif
    soma151_http_unlock();
    return rc;
}
