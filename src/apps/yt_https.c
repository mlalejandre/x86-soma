/* SOMA_252_VIDEO_NATIVO: cliente HTTP/1.1 sobre TCP o TLS 1.3 para YouTube nativo. La interfaz esta al final de yt_inner.h. */
#include "yt_inner.h"
#ifndef YT_HTTPS_PURE_ONLY
#include "tls13.h"
#include "tls_trust.h"
#include "tun_tls.h"     /* SOMA_326_TUNEL_TLS */
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "mem.h"
#include "thread.h"
#include "idt.h"
#endif

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int starts_i(const char *s, uint32_t n, const char *k) { uint32_t i = 0; for (; k[i]; ++i) if (i >= n || lower(s[i]) != k[i]) return 0; return 1; }

int yh_parse_url(const char *url, struct yh_url *u)
{
    const char *p = url;
    if (starts_i(p, 8, "https://")) { u->tls = 1; u->port = 443; p += 8; }
    else if (starts_i(p, 7, "http://")) { u->tls = 0; u->port = 80; p += 7; }
    else return 0;
    uint32_t k = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') { if (k + 1 >= sizeof u->host || *p == '@' || (unsigned char)*p <= 32) return 0; u->host[k++] = (char)lower(*p++); }
    u->host[k] = 0;
    if (!k) return 0;
    if (*p == ':') {
        uint32_t v = 0; int d = 0; p++;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p++ - '0'); if (++d > 5) return 0; }
        if (!d || v == 0 || v > 65535) return 0;
        u->port = (uint16_t)v;
    }
    if (*p && *p != '/') return 0;                             /* "host?x" sin ruta no lo usa nadie aqui */
    u->path = *p ? p : "/";
    return 1;
}

int yh_parse_head(const char *b, uint32_t n, struct yh_head *h)
{
    h->status = 0; h->chunked = 0; h->clen = 0; h->has_clen = 0; h->range_total = 0; h->location[0] = 0;
    if (n < 12 || !(b[0] == 'H' && b[1] == 'T' && b[2] == 'T' && b[3] == 'P' && b[4] == '/')) return 0;
    uint32_t p = 5; while (p < n && b[p] != ' ') p++;
    while (p < n && b[p] == ' ') p++;
    if (p + 3 > n) return 0;
    for (int i = 0; i < 3; ++i) { if (b[p + (uint32_t)i] < '0' || b[p + (uint32_t)i] > '9') return 0; h->status = h->status * 10 + (b[p + (uint32_t)i] - '0'); }
    while (p < n && b[p] != '\n') p++;
    p++;
    while (p < n) {
        uint32_t e = p; while (e < n && b[e] != '\n') e++;
        uint32_t ln = e - p; if (ln && b[p + ln - 1] == '\r') ln--;
        const char *l = b + p;
        if (starts_i(l, ln, "content-length:")) {
            uint32_t q = 15; while (q < ln && l[q] == ' ') q++;
            uint64_t v = 0; int d = 0; while (q < ln && l[q] >= '0' && l[q] <= '9' && d < 18) { v = v * 10 + (uint64_t)(l[q++] - '0'); d++; }
            if (d) { h->clen = v; h->has_clen = 1; }
        } else if (starts_i(l, ln, "transfer-encoding:")) {
            for (uint32_t q = 18; q + 7 <= ln; ++q) if (starts_i(l + q, ln - q, "chunked")) h->chunked = 1;
        } else if (starts_i(l, ln, "content-range:")) {            /* bytes a-b/total */
            uint32_t q = 14; while (q < ln && l[q] != '/') q++;
            if (q < ln) { q++; uint64_t v = 0; int d = 0; while (q < ln && l[q] >= '0' && l[q] <= '9' && d < 18) { v = v * 10 + (uint64_t)(l[q++] - '0'); d++; } if (d) h->range_total = v; }
        } else if (starts_i(l, ln, "location:")) {
            uint32_t q = 9, k = 0; while (q < ln && l[q] == ' ') q++;
            while (q < ln && k + 1 < sizeof h->location) h->location[k++] = l[q++];
            h->location[q < ln ? 0 : k] = 0;                    /* una direccion que no cabe no se sigue */
        }
        p = e + 1;
    }
    return h->status >= 100;
}

#ifndef YT_HTTPS_PURE_ONLY
static uint32_t sl(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void put(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void putn(char *o, uint32_t max, uint32_t *p, uint64_t v) { char t[24]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k && *p + 1 < max) o[(*p)++] = t[--k]; if (max) o[*p < max ? *p : max - 1] = 0; }
#define RB_CAP 16384u
#define HEAD_MAX 12288u

/* SOMA_326_TUNEL_TLS: la conexion es un tunel (src/apps/tun_tls.c): en x86_64 el TCP, el TLS y los certificados de YouTube y de
 * googlevideo se hacen en un proceso de ring 3; aqui solo pasa el HTTP en claro. */
static int raw_send(struct yh *c, const void *d, uint32_t n) { return c->tn ? tun_envia(c->tn, d, n) : -1; }
/* > 0 bytes, 0 nada aun, -1 cerrado, -2 error */
static int raw_recv(struct yh *c, uint8_t *b, uint32_t max, uint32_t to) { return c->tn ? tun_recibe(c->tn, b, max, to) : -2; }

void yh_close(struct yh *c, int abort_now)
{
    if (c->tn) tun_cierra(c->tn, abort_now);
    if (c->rb) kfree(c->rb);
    c->h = -1; c->tn = 0; c->rb = 0; c->tls_on = 0;
}

static int open_once(struct yh *c, const struct yh_url *u, const char *method, const char *headers, const char *body, uint32_t blen,
                     int64_t range_from, struct yh_head *hd, uint32_t timeout_ms, char *err, uint32_t emax)
{
    uint32_t ep = 0; uint8_t ip[4]; int isip = 1, parts = 0; uint32_t v = 0, dig = 0;
    c->h = -1; c->tn = 0; c->rb = 0; c->tls_on = 0; c->rn = c->rp = 0; c->chunked = 0; c->done = 0; c->left = ~0ULL; c->chunk_left = 0; c->chunk_state = 0;
    for (const char *p = u->host; ; ++p) {                     /* a.b.c.d o un nombre */
        if (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); if (++dig > 3 || v > 255) { isip = 0; break; } }
        else if (*p == '.' || !*p) { if (!dig || parts > 3) { isip = 0; break; } ip[parts++] = (uint8_t)v; v = 0; dig = 0; if (!*p) break; }
        else { isip = 0; break; }
    }
    if (!isip || parts != 4) {
        net_client_lock(); int ok = dns_resolve(u->host, ip, 5000); net_client_unlock();
        if (!ok) { put(err, emax, &ep, "No se encuentra "); put(err, emax, &ep, u->host); put(err, emax, &ep, " (sin red o sin DNS)"); return -1; }
    }
    {   /* SOMA_326_TUNEL_TLS */
        char m[160];
        c->tn = tun_abre(ip, (uint16_t)u->port, u->tls, u->host, timeout_ms > 15000 ? 15000 : timeout_ms, m, sizeof m);
        if (!c->tn) {
            if (!u->tls || (m[0] == 'n' && m[1] == 'o' && m[2] == ' ' && m[3] == 's')) { put(err, emax, &ep, "No se pudo conectar con "); put(err, emax, &ep, u->host); }
            else { put(err, emax, &ep, "Conexion segura con "); put(err, emax, &ep, u->host); put(err, emax, &ep, " rechazada: "); put(err, emax, &ep, m); }
            return -1;
        }
        c->h = 0; c->tls_on = u->tls ? 1 : 0;
    }
    c->rb = (uint8_t *)kmalloc(RB_CAP);
    if (!c->rb) { put(err, emax, &ep, "Sin memoria"); yh_close(c, 1); return -1; }
    uint32_t cap = 2600 + sl(u->path) + (headers ? sl(headers) : 0);
    char *rq = (char *)kmalloc(cap); uint32_t p = 0;
    if (!rq) { put(err, emax, &ep, "Sin memoria"); yh_close(c, 1); return -1; }
    put(rq, cap, &p, method); put(rq, cap, &p, " "); put(rq, cap, &p, u->path); put(rq, cap, &p, " HTTP/1.1\r\nHost: "); put(rq, cap, &p, u->host);
    if (u->port != (u->tls ? 443 : 80)) { put(rq, cap, &p, ":"); putn(rq, cap, &p, u->port); }
    put(rq, cap, &p, "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n");
    if (headers) put(rq, cap, &p, headers);
    if (range_from >= 0) { put(rq, cap, &p, "Range: bytes="); putn(rq, cap, &p, (uint64_t)range_from); put(rq, cap, &p, "-\r\n"); }
    if (body) { put(rq, cap, &p, "Content-Length: "); putn(rq, cap, &p, blen); put(rq, cap, &p, "\r\n"); }
    put(rq, cap, &p, "\r\n");
    int s = raw_send(c, rq, p);
    kfree(rq);
    if (s == 0 && body && blen) s = raw_send(c, body, blen);
    if (s != 0) { put(err, emax, &ep, "No se pudo enviar la peticion a "); put(err, emax, &ep, u->host); yh_close(c, 1); return -1; }
    /* cabeceras */
    uint64_t t0 = timer_get_uptime_ms(); uint32_t hend = 0;
    char *hb = (char *)kmalloc(HEAD_MAX); uint32_t hn = 0;
    if (!hb) { put(err, emax, &ep, "Sin memoria"); yh_close(c, 1); return -1; }
    while (!hend) {
        if (timer_get_uptime_ms() - t0 > timeout_ms || hn >= HEAD_MAX) { put(err, emax, &ep, u->host); put(err, emax, &ep, hn >= HEAD_MAX ? " ha respondido algo que no es HTTP" : " no responde"); kfree(hb); yh_close(c, 1); return -1; }
        int n = raw_recv(c, (uint8_t *)hb + hn, HEAD_MAX - hn, 200);
        if (n < 0) { put(err, emax, &ep, u->host); put(err, emax, &ep, " ha cerrado la conexion"); kfree(hb); yh_close(c, 1); return -1; }
        uint32_t from = hn > 3 ? hn - 3 : 0; hn += (uint32_t)n;
        for (uint32_t i = from; i + 3 < hn; ++i) if (hb[i] == '\r' && hb[i + 1] == '\n' && hb[i + 2] == '\r' && hb[i + 3] == '\n') { hend = i + 4; break; }
    }
    int ok = yh_parse_head(hb, hend, hd);
    if (ok) { c->rn = hn - hend; for (uint32_t i = 0; i < c->rn; ++i) c->rb[i] = (uint8_t)hb[hend + i]; c->rp = 0; }
    kfree(hb);
    if (!ok) { put(err, emax, &ep, u->host); put(err, emax, &ep, " ha respondido algo que no es HTTP"); yh_close(c, 1); return -1; }
    c->chunked = hd->chunked; c->left = hd->chunked ? ~0ULL : hd->has_clen ? hd->clen : ~0ULL;
    if (c->left == 0) c->done = 1;
    return hd->status;
}

int yh_open(struct yh *c, const char *url, const char *method, const char *headers, const char *body, uint32_t blen,
            int64_t range_from, uint64_t *total, uint32_t timeout_ms, char *err, uint32_t emax)
{
    static char next[4][1400]; static int slot;
    struct yh_url u; struct yh_head *hd = (struct yh_head *)kmalloc(sizeof *hd);
    if (total) *total = 0;
    if (err && emax) err[0] = 0;
    c->h = -1; c->tn = 0; c->rb = 0;
    if (!hd) return -1;
    int st = -1;
    for (int hop = 0; hop < 5; ++hop) {
        if (!yh_parse_url(url, &u)) { uint32_t ep = 0; put(err, emax, &ep, "Direccion no valida"); st = -1; break; }
        st = open_once(c, &u, method ? method : "GET", headers, body, blen, range_from, hd, timeout_ms, err, emax);
        if (st < 0) break;
        if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) && hd->location[0] && (hd->location[0] == 'h' || hd->location[0] == 'H')) {
            char *nx = next[slot++ & 3]; uint32_t k = 0;
            while (hd->location[k] && k + 1 < 1400) { nx[k] = hd->location[k]; k++; }
            nx[k] = 0;
            yh_close(c, 1);
            url = nx; st = -1;
            if (hop == 4) { uint32_t ep = 0; put(err, emax, &ep, "Demasiadas redirecciones"); }
            continue;
        }
        if (total) *total = hd->range_total ? hd->range_total : (st == 200 && hd->has_clen ? hd->clen : 0);
        break;
    }
    kfree(hd);
    return st;
}

static int fill(struct yh *c, uint32_t to)                     /* mas bytes en rb: > 0, 0, -1, -2 */
{
    if (c->rp < c->rn) return (int)(c->rn - c->rp);
    c->rp = c->rn = 0;
    int n = raw_recv(c, c->rb, RB_CAP, to);
    if (n > 0) c->rn = (uint32_t)n;
    return n;
}
int yh_read(struct yh *c, uint8_t *buf, uint32_t max, uint32_t timeout_ms)
{
    if (c->h < 0) return -2;
    if (c->done || !max) return c->done ? -1 : 0;
    if (!c->chunked) {
        int n = fill(c, timeout_ms);
        if (n <= 0) { if (n == -1) { c->done = 1; return c->left == ~0ULL || c->left == 0 ? -1 : -2; } return n; }
        uint32_t k = (uint32_t)n < max ? (uint32_t)n : max;
        if (c->left != ~0ULL && k > c->left) k = (uint32_t)c->left;
        for (uint32_t i = 0; i < k; ++i) buf[i] = c->rb[c->rp + i];
        c->rp += k;
        if (c->left != ~0ULL) { c->left -= k; if (!c->left) c->done = 1; }
        return (int)k;
    }
    for (;;) {                                                  /* por trozos: "<hex>[;ext]\r\n" datos "\r\n" ... "0\r\n\r\n" */
        if (c->chunk_state == 2) {                              /* dentro de un trozo */
            int n = fill(c, timeout_ms);
            if (n <= 0) { if (n == -1) { c->done = 1; return -2; } return n; }
            uint32_t k = (uint32_t)n < max ? (uint32_t)n : max;
            if (k > c->chunk_left) k = c->chunk_left;
            for (uint32_t i = 0; i < k; ++i) buf[i] = c->rb[c->rp + i];
            c->rp += k; c->chunk_left -= k;
            if (!c->chunk_left) c->chunk_state = 3;
            return (int)k;
        }
        int n = fill(c, timeout_ms);
        if (n <= 0) { if (n == -1) { c->done = 1; return -2; } return n; }
        uint8_t ch = c->rb[c->rp++];
        if (c->chunk_state == 3) { if (ch == '\n') c->chunk_state = 0; continue; }          /* el \r\n de despues de los datos */
        if (c->chunk_state == 0) {                              /* cifras hexadecimales del tamano */
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d >= 0) { if (c->chunk_left > 0x0FFFFFFFu) return -2; c->chunk_left = c->chunk_left * 16 + (uint32_t)d; continue; }
            c->chunk_state = 1;
        }
        if (c->chunk_state == 1 && ch == '\n') {                /* fin de la linea del tamano */
            if (c->chunk_left == 0) { c->done = 1; return -1; }
            c->chunk_state = 2;
        }
    }
}

int yh_fetch(const char *url, const char *method, const char *headers, const char *body, uint32_t blen, uint8_t *out, uint32_t cap, uint32_t *len,
             uint32_t timeout_ms, const volatile uint32_t *stop, uint32_t stop_val, char *err, uint32_t emax)
{
    struct yh c; uint64_t total = 0; uint32_t n = 0, ep = 0;
    *len = 0;
    int st = yh_open(&c, url, method, headers, body, blen, -1, &total, timeout_ms, err, emax);
    if (st < 0) return st;
    uint64_t t0 = timer_get_uptime_ms(), last = t0;
    for (;;) {
        if (stop && *stop != stop_val) { yh_close(&c, 1); return -3; }
        if (n + 1 >= cap) break;                                /* lleno: lo demas no interesa */
        int r = yh_read(&c, out + n, cap - 1 - n, 100);
        if (r > 0) { n += (uint32_t)r; last = timer_get_uptime_ms(); }
        else if (r == -1) break;
        else if (r == -2) { if (!n) { put(err, emax, &ep, "La respuesta se ha cortado"); yh_close(&c, 1); return -1; } break; }
        else if (timer_get_uptime_ms() - last > timeout_ms) { put(err, emax, &ep, "El servidor ha dejado de responder"); yh_close(&c, 1); return -1; }
    }
    (void)t0;
    out[n] = 0; *len = n;
    yh_close(&c, n + 1 >= cap);
    return st;
}
#endif
