/* SOMA_201: fuzzing del cliente HTTP del navegador (src/apps/web_net.c, ring 0) en el anfitrion, con ASan y UBSan.
 * La red se sustituye por un "servidor" que devuelve respuestas generadas y mutadas (cabeceras rotas, Content-Length falso,
 * trozos con tamanos absurdos, gzip corrupto, redirecciones en cadena, cookies enormes), entregadas en fragmentos de tamano
 * aleatorio. Tambien se prueban el analisis de URL y el tarro de cookies. Uso: fuzz_http_201 [iteraciones] [semilla] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include "web_net.h"
#include "web_util.h"
#include "tcp.h"
#include "tls13.h"

static size_t g_alloc;
void *kmalloc(size_t n) { if (n > (64u << 20) || g_alloc + n > (200u << 20)) return 0; g_alloc += n; return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now; uint64_t web_now_ms(void) { return g_now += 7; }
void web_yield(void) {}
void net_client_lock(void) {} void net_client_unlock(void) {}
uint8_t net_ip[4] = { 10, 0, 2, 15 };
int bootinfo_disabled(const char *f) { (void)f; return 0; }
int dns_resolve(const char *name, uint8_t *ip, uint32_t t) { (void)t; if (name[0] == 'x') return 0; ip[0] = 93; ip[1] = 184; ip[2] = 216; ip[3] = 34; return 1; }

static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
static unsigned char resp[1 << 17]; static uint32_t rl, rp; static int conns;
static void add(const char *s) { while (*s && rl < sizeof resp - 2) resp[rl++] = (unsigned char)*s++; }
static void addn(const void *p, uint32_t n) { if (rl + n < sizeof resp) { memcpy(resp + rl, p, n); rl += n; } }
static int feed(void *buf, uint32_t max)
{
    if (rp >= rl) return rn(4) ? TCP_EOF : TCP_ERR;
    uint32_t n = 1 + rn(rn(3) ? 1500 : 40); if (n > max) n = max; if (n > rl - rp) n = rl - rp;
    memcpy(buf, resp + rp, n); rp += n; return (int)n;
}
int tcp_connect(const uint8_t *ip, uint16_t port, uint32_t t) { (void)ip; (void)port; (void)t; rp = 0; return ++conns > 40 ? -1 : 3; }
int tcp_send(int h, const void *d, uint32_t len) { (void)h; (void)d; return rn(200) ? (int)len : -1; }
int tcp_recv(int h, void *buf, uint16_t max, uint32_t t) { (void)h; (void)t; return max ? feed(buf, max) : 0; }
int tcp_close(int h) { (void)h; return 0; }
int tls13_verify = 1;
const char *tls13_err_name(int e) { (void)e; return "tls"; }
int tls13_connect(struct tls13 *t, const struct tls_io *io, const char *sni, uint32_t to) { (void)t; (void)io; (void)sni; (void)to; return rn(30) ? 0 : -3; }
int tls13_send(struct tls13 *t, const void *d, uint32_t n) { (void)t; (void)d; (void)n; return 0; }
int tls13_recv(struct tls13 *t, void *buf, uint32_t max, uint32_t to) { (void)t; (void)to; int r = max ? feed(buf, max > 65535 ? 65535 : max) : 0; return r == TCP_EOF ? 0 : r; }
void tls13_close(struct tls13 *t) { (void)t; }

#define PICK(a) a[rn(sizeof a / sizeof a[0])]
static const char *const ST[] = { "HTTP/1.1 200 OK\r\n", "HTTP/1.0 200\r\n", "HTTP/1.1 301 Moved\r\n", "HTTP/1.1 302 Found\r\n", "HTTP/1.1 307 X\r\n", "HTTP/1.1 304 NM\r\n", "HTTP/1.1 204 No Content\r\n", "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\n",
    "HTTP/1.1 99999999999 OK\r\n", "HTTP/2 200\r\n", "HTTP/1.1\r\n", "HTTP/1.1 200 OK\n", "ICY 200 OK\r\n", "HTTP/1.1 -1 X\r\n", "HTTP/1.1 429 Too Many\r\n", "HTTP/1.1 401 U\r\n", "" };
static const char *const HD[] = { "Content-Length: 5\r\n", "Content-Length: 0\r\n", "Content-Length: 99999999999999999999\r\n", "Content-Length: -1\r\n", "Content-Length: 4294967295\r\n", "Content-Length: 10\r\nContent-Length: 3\r\n", "Transfer-Encoding: chunked\r\n",
    "transfer-encoding:  CHUNKED \r\n", "Content-Encoding: gzip\r\n", "Content-Encoding: deflate\r\n", "Content-Encoding: br\r\n", "Content-Type: text/html; charset=utf-8\r\n", "Content-Type: text/html;charset=\"iso-8859-1\r\n", "Content-Type: \r\n",
    "Content-Type: image/png\r\n", "Location: /otra\r\n", "Location: http://h2.test/a?b#c\r\n", "Location: https://x.fail/\r\n", "Location: //h3.test\r\n", "Location: \r\n", "Location: javascript:1\r\n", "Location: file:///etc/llm.conf\r\n", "Location: ../../../../x\r\n",
    "Set-Cookie: a=b\r\n", "Set-Cookie: a=b; Path=/; Domain=.test; Max-Age=-1; Secure; HttpOnly; SameSite=None\r\n", "Set-Cookie: =\r\n", "Set-Cookie: ;;;===\r\n", "Set-Cookie: k=v; Domain=com\r\n", "Set-Cookie: k=v; Expires=Wed, 99 Zzz 99999 99:99:99 GMT\r\n",
    "Connection: close\r\n", "Connection: keep-alive\r\n", "X-Soma-Gateway: 7\r\n", "Refresh: 0;url=/r\r\n", ":\r\n", "Sin-Dos-Puntos\r\n", " continuacion\r\n", "X: \0y\r\n", "Content-Range: bytes 0-1/*\r\n", "Retry-After: 99999999999\r\n", "WWW-Authenticate: Basic\r\n" };
static void long_header(void) { static const char *const N[] = { "Set-Cookie: c", "Location: /", "Content-Type: text/", "X-L: ", "Content-Encoding: " }; add(PICK(N)); uint32_t n = rn(3) ? rn(300) : 3000 + rn(9000); for (uint32_t i = 0; i < n && rl < sizeof resp - 8; i++) resp[rl++] = (unsigned char)("ab=;,/. %\"x"[rn(11)]); add("\r\n"); }
static void body(int chunked)
{
    static const struct { const char *p; uint32_t n; } B[] = { { "hola", 4 }, { "<html><body>x</body></html>", 27 }, { "", 0 },
        { "\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03\xcb\xc8\xcf\x49\x04\x00\x84\x16\xf7\x8d\x04\x00\x00\x00", 24 }, { "\x1f\x8b\x08\x08zzzz", 8 }, { "\x78\x9c\x4b\xcc\x04\x00", 6 }, { "\x1f\x8b", 2 } };
    if (!chunked) { uint32_t b = rn(sizeof B / sizeof B[0]); addn(B[b].p, B[b].n); for (uint32_t n = rn(4) ? 0 : rn(60000); n > 0 && rl < sizeof resp - 2; n--) resp[rl++] = (unsigned char)rnd(); return; }
    static const char *const SZ[] = { "5\r\nhola!\r\n", "0\r\n\r\n", "ffffffff\r\n", "FFFFFFFFFFFFFFFFF\r\nx", "-1\r\n", "5;ext=1\r\nhola!\r\n", "3\r\nab", "zz\r\n", "\r\n", "1\r\nx\r\n", "10000\r\n", "0\r\nTrailer: x\r\n\r\n", "00000000000000000001\r\ny\r\n", "5\nhola!\n", "7fffffff\r\nabc" };
    for (int k = (int)rn(8); k >= 0; k--) add(PICK(SZ));
    if (rn(2)) add("0\r\n\r\n");
}
static void gen(void)
{
    rl = 0;
    for (int hops = 1 + (int)rn(3); hops > 0; hops--) {              /* varias respuestas seguidas: redirecciones y reintentos leen la misma cola */
        add(PICK(ST)); int ch = 0;
        for (int k = (int)rn(7); k > 0; k--) { if (rn(12) == 0) long_header(); else { const char *h = PICK(HD); if (strstr(h, "hunked") || strstr(h, "HUNKED")) ch = 1; add(h); } }
        add(rn(15) ? "\r\n" : rn(2) ? "\n" : "");
        body(ch);
        if (rn(3)) break;
    }
    if (rn(4) == 0) for (int m = 1 + (int)rn(4); m > 0 && rl > 4; m--) { uint32_t p = rn(rl); int k = (int)rn(4); if (k == 0) resp[p] = (unsigned char)rnd(); else if (k == 1) resp[p] = (unsigned char)"\r\n: \0;=,"[rn(8)]; else if (k == 2) rl = p + 1; else if (p + 9 < rl) { memmove(resp + p, resp + p + 8, rl - p - 8); rl -= 8; } }
}
static void url_tests(void)
{
    static const char *const P[] = { "http://", "https://", "file://", "HTTP://", "//", "/", "?", "#", "a", "b.c", ":", "@", "[::1]", "%", "%zz", "%00", "..", "/./", "/../", "\\", " ", "\t", "host", ":99999", ":0", ":-1", "user:pw@", "javascript:", "data:text/html,", "about:blank", "soma-gw:/x", "\xc3\xb1", "\xff", "&", "=", "+" };
    char u[5000], r[5000], o[64]; uint32_t n = 0, m = 0;
    for (int k = (int)rn(rn(8) ? 9 : 700); k >= 0 && n < sizeof u - 12; k--) { const char *p = PICK(P); while (*p) u[n++] = *p++; } u[n] = 0;
    for (int k = (int)rn(rn(8) ? 6 : 700); k >= 0 && m < sizeof r - 12; k--) { const char *p = PICK(P); while (*p) r[m++] = *p++; } r[m] = 0;
    struct wn_url pu; (void)wn_url_parse(u, &pu);
    uint32_t om = rn(4) ? WN_URL_MAX : 1 + rn(40); char *out = malloc(om); (void)wn_url_resolve(u, r, out, om); free(out);
    wn_url_origin(u, o, 1 + rn(sizeof o - 1)); om = 1 + rn(300); out = malloc(om); wn_url_encode(r, out, om); free(out);
    wn_cookie_store(u, r); wn_cookie_store("http://a.test/p/q", r); om = 1 + rn(1200); out = malloc(om); (void)wn_cookie_header(rn(2) ? u : "http://a.test/p/q", out, om, (int)rn(2)); free(out);
    if (rn(50) == 0) wn_cookie_clear();
    if (rn(15) == 0) {   /* SOMA_203: el tarro vuelve de una copia que pudo escribir un proceso comprometido: bytes cualesquiera, sin terminadores */
        uint32_t cap = 64u << 10; uint8_t *b = malloc(cap); uint32_t n = wn_cookie_export(b, cap);
        if (!n) { fprintf(stderr, "wn_cookie_export no cabe en 64 KiB\n"); abort(); }
        if (wn_cookie_export(b, n - 1) != 0) { fprintf(stderr, "wn_cookie_export acepto un buffer corto\n"); abort(); }
        uint32_t mode = rn(3);
        if (mode == 0) for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)(1 + rn(255)); else if (mode == 1) for (uint32_t k = 0; k < 400; k++) b[rn(n)] = (uint8_t)rn(256);
        if (wn_cookie_import(b, n - 1) == 0 || wn_cookie_import(b, n + 1) == 0) { fprintf(stderr, "wn_cookie_import acepto una longitud distinta\n"); abort(); }
        if (wn_cookie_import(b, n) != 0) { fprintf(stderr, "wn_cookie_import rechazo su propio tamano\n"); abort(); }
        free(b);
        for (int k = 0; k < 6; k++) { uint32_t om2 = 1 + rn(1200); char *o2 = malloc(om2); (void)wn_cookie_header(rn(2) ? u : "https://a.test/p/q", o2, om2, (int)rn(2)); free(o2); }
    }
}
static uint32_t g_iter;
static void on_alarm(int s) { (void)s; fprintf(stderr, "\nCUELGUE: el caso %u lleva mas de 20 s (respuesta guardada en /tmp/fuzz_http_cuelgue.bin)\n", g_iter); FILE *f = fopen("/tmp/fuzz_http_cuelgue.bin", "wb"); if (f) { fwrite(resp, 1, rl, f); fclose(f); } _exit(3); }
int main(int argc, char **argv)
{
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 20000, seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    static const char *const URLS[] = { "http://a.test/", "https://a.test/x?y=1", "http://a.test:8080/p/q.html#f", "https://b.a.test/", "http://10.0.2.2/", "http://xfalla.test/" };
    signal(SIGALRM, on_alarm);
    {   /* casos fijos: los que rompian el nucleo antes del parche 201 (linea de tamano de trozo sin terminar) y parientes */
        static const char *const FIX[] = { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffff", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nho",
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n7fffffff\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n4", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;x" };
        for (unsigned k = 0; k < sizeof FIX / sizeof FIX[0]; k++) for (int rep = 0; rep < 30; rep++) {
            rs = 77u + k * 131u + (uint32_t)rep; rl = 0; add(FIX[k]); g_alloc = 0; conns = 0;
            static struct wn_req rq; static struct wn_resp rsq; memset(&rq, 0, sizeof rq); memset(&rsq, 0, sizeof rsq);
            rq.url = "http://a.test/"; rq.timeout_ms = 2000; rq.max_body = 1u << 20; rq.no_redirect = 1;
            (void)wn_fetch(&rq, &rsq); wn_free(&rsq);
        }
    }
    for (g_iter = getenv("FUZZ_START") ? (uint32_t)atoi(getenv("FUZZ_START")) : 0; g_iter < iters; g_iter++) {
        rs = (seed * 2654435761u) ^ (g_iter * 40503u + 1u); if (!rs) rs = 1;
        gen(); g_alloc = 0; conns = 0;
        FILE *f = fopen("/tmp/fuzz_http_ultimo.bin", "wb"); if (f) { fwrite(resp, 1, rl, f); fclose(f); }
        alarm(20);
        static struct wn_req rq; static struct wn_resp rsq; memset(&rq, 0, sizeof rq); memset(&rsq, 0, sizeof rsq);
        static const char H[] = "X-Api: k\r\n";
        rq.url = PICK(URLS); rq.method = rn(4) ? 0 : "POST"; rq.body = rq.method ? "a=b" : 0; rq.body_len = rq.method ? 3 : 0; rq.ctype = rq.method ? "application/x-www-form-urlencoded" : 0;
        rq.timeout_ms = 2000; rq.max_body = rn(3) ? (1u << 20) : rn(5000); rq.no_redirect = (int)rn(4) == 0; rq.want_binary = (int)rn(2); rq.referer = rn(2) ? "http://ref.test/" : 0;
        rq.extra_headers = rn(5) ? 0 : H; rq.direct = rq.extra_headers != 0;
        (void)wn_fetch(&rq, &rsq);
        if (rsq.body) { volatile unsigned char sink = 0; for (uint32_t i = 0; i < rsq.len; i += 97) sink ^= (unsigned char)rsq.body[i]; (void)sink; }   /* el cuerpo declarado debe ser legible entero */
        wn_free(&rsq);
        url_tests();
    }
    alarm(0);
    printf("fuzz_http_201: %u respuestas sin fallos de memoria ni cuelgues (semilla %u)\n", iters, seed);
    return 0;
}
