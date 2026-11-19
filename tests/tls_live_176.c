/* SOMA_176A: prueba en vivo del cliente TLS 1.3 contra un servidor real: tls_live_176 host puerto [ruta] [sni]
 * Con HTTPS_PROXY definido (http://host:puerto) hace un tunel CONNECT. No forma parte de make test (necesita red / openssl s_server). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "tls13.h"
#include "tlscrypto.h"
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static int io_send(void *c, const uint8_t *d, uint32_t n) { int fd = *(int *)c; while (n) { ssize_t k = send(fd, d, n, 0); if (k <= 0) return -1; d += k; n -= (uint32_t)k; } return 0; }
static int io_recv(void *c, uint8_t *d, uint32_t max, uint32_t to) {
    int fd = *(int *)c; fd_set s; FD_ZERO(&s); FD_SET(fd, &s); struct timeval tv = { to / 1000, (to % 1000) * 1000 };
    int r = select(fd + 1, &s, 0, 0, &tv); if (r <= 0) return 0;
    ssize_t k = recv(fd, d, max, 0); if (k == 0) return TLS_IO_EOF; if (k < 0) return TLS_IO_ERR; return (int)k;
}
static int dial(const char *host, int port)
{
    const char *px = getenv("HTTPS_PROXY"); char ph[200] = "", tgt[200]; int pp = 0;
    if (px && strncmp(host, "127.", 4) && strcmp(host, "localhost")) { const char *p = strstr(px, "://"); p = p ? p + 3 : px; const char *at = strrchr(p, '@'); if (at) p = at + 1; const char *col = strrchr(p, ':'); if (col) { memcpy(ph, p, (size_t)(col - p)); ph[col - p] = 0; pp = atoi(col + 1); } }
    snprintf(tgt, sizeof tgt, "%s", ph[0] ? ph : host);
    struct addrinfo hints = { 0 }, *ai; hints.ai_socktype = SOCK_STREAM; char ps[16]; snprintf(ps, sizeof ps, "%d", ph[0] ? pp : port);
    if (getaddrinfo(tgt, ps, &hints, &ai)) { perror("getaddrinfo"); return -1; }
    int fd = socket(ai->ai_family, SOCK_STREAM, 0); if (connect(fd, ai->ai_addr, ai->ai_addrlen)) { perror("connect"); return -1; }
    if (ph[0]) {
        char req[600]; int n = snprintf(req, sizeof req, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", host, port, host, port);
        const char *px2 = px; const char *p = strstr(px2, "://"); p = p ? p + 3 : px2; const char *at = strrchr(p, '@');
        if (at) { char cred[300]; size_t cl = (size_t)(at - p); memcpy(cred, p, cl); cred[cl] = 0; static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; char enc[450]; size_t o = 0; for (size_t i = 0; i < cl; i += 3) { unsigned v = (unsigned char)cred[i] << 16 | (i + 1 < cl ? (unsigned char)cred[i + 1] << 8 : 0) | (i + 2 < cl ? (unsigned char)cred[i + 2] : 0); enc[o++] = b64[v >> 18]; enc[o++] = b64[(v >> 12) & 63]; enc[o++] = i + 1 < cl ? b64[(v >> 6) & 63] : '='; enc[o++] = i + 2 < cl ? b64[v & 63] : '='; } enc[o] = 0; n += snprintf(req + n, sizeof req - (size_t)n, "Proxy-Authorization: Basic %s\r\n", enc); }
        n += snprintf(req + n, sizeof req - (size_t)n, "\r\n"); send(fd, req, (size_t)n, 0);
        char buf[1024]; int k = 0; while (k < 1000) { ssize_t r = recv(fd, buf + k, 1, 0); if (r <= 0) return -1; k++; if (k >= 4 && !memcmp(buf + k - 4, "\r\n\r\n", 4)) break; } buf[k] = 0;
        if (!strstr(buf, " 200")) { fprintf(stderr, "proxy CONNECT: %.60s\n", buf); return -1; }
    }
    return fd;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: %s host puerto [ruta] [sni]\n", argv[0]); return 2; }
    const char *host = argv[1], *path = argc > 3 ? argv[3] : "/", *sni = argc > 4 ? argv[4] : host;
    int fd = dial(host, atoi(argv[2])); if (fd < 0) return 3;
    static struct tls13 t; struct tls_io io = { &fd, io_send, io_recv, now_ms, 0 };
    if (tls13_connect(&t, &io, sni, 15000)) { printf("TLS FALLO: %s (alerta %d)\n", tls13_err_name(t.err), t.alert); return 1; }
    char req[600]; int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: soma-tls-test\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", path, sni);
    if (tls13_send(&t, req, (uint32_t)n)) { printf("envio fallo\n"); return 1; }
    static uint8_t buf[65536]; uint64_t total = 0; int first = 1; uint64_t t0 = now_ms();
    for (;;) { int r = tls13_recv(&t, buf, sizeof buf, 10000); if (r > 0) { if (first) { first = 0; int k = 0; while (k < r && buf[k] != '\r' && buf[k] != '\n' && k < 100) k++; printf("respuesta: %.*s\n", k, buf); if (getenv("TLS_DUMP")) printf("%.*s\n", r > 600 ? 600 : r, buf); } total += (uint64_t)r; continue; } if (r == 0) { printf("timeout\n"); break; } if (r == TLS_IO_EOF) break; printf("recv error: %s alerta %d\n", tls13_err_name(t.err), t.alert); return 1; }
    printf("OK: %llu bytes de aplicacion en %llu ms, estado %d\n", (unsigned long long)total, (unsigned long long)(now_ms() - t0), t.state);
    return 0;
}
