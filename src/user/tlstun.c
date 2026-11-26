/* SOMA_326_TUNEL_TLS: el tunel TLS de ring 3. Una conexion por proceso: TCP, TLS 1.3 (apreton, certificados, cifrado) y STARTTLS.
 * El nucleo (src/apps/tun_tls.c) le pasa y recibe TEXTO EN CLARO por dos anillos en la region compartida (src/core/tun_abi.h).
 * Capacidades: red, dormir y la hora; nada mas (ni archivos ni pantalla). Si un servidor hostil rompe el TLS o los certificados,
 * solo cae este proceso y su conexion. El nucleo manda el azar, la hora, si se verifica y las autoridades del usuario. */
#include "sbxrt.h"
#include "../core/tun_abi.h"
#include "../core/tls13.h"
#include "../core/x509.h"
#include "../core/tlscrypto.h"
#include "../core/tls_roots.h"
#include "../net/tcp.h"
#include "../arch/x86_64/usermode.h"

#define TCP_EOF (-1)
#define TCP_ERR (-2)
#define BAR() __asm__ volatile ("" ::: "memory")

uint64_t timer_get_uptime_ms(void);
static inline void duerme(long ms) { long a = ms, r; __asm__ volatile ("syscall" : "=a"(r), "+D"(a) : "0"((long)SYS_SLEEP) : "rcx", "r11", "rsi", "rdx", "r8", "r9", "r10", "memory"); (void)r; }

/* ---- lo que tls13.c pide al sistema (como netsvc) */
static uint8_t s_semilla[32]; static uint64_t s_ctr;
void rng_bytes(void *out, size_t n)
{
    uint8_t *o = (uint8_t *)out, b[40], h[32];
    while (n) { memcpy(b, s_semilla, 32); memcpy(b + 32, &s_ctr, 8); s_ctr++; sha256(b, 40, h); size_t c = n < 32 ? n : 32; memcpy(o, h, c); o += c; n -= c; }
}
static uint64_t s_unix0, s_up0;
static uint64_t unix_now(void) { return s_unix0 ? s_unix0 + (timer_get_uptime_ms() - s_up0) / 1000u : 0; }

static struct tun_ctl *const C = (struct tun_ctl *)(uintptr_t)TUN_SH_VA;
static uint8_t *const TX = (uint8_t *)(uintptr_t)(TUN_SH_VA + TUN_TX_OFF);
static uint8_t *const RX = (uint8_t *)(uintptr_t)(TUN_SH_VA + TUN_RX_OFF);
static int s_h = -1, s_tls;
static struct tls13 *s_t;

static void pon_err(const char *a, const char *b)
{
    uint32_t k = 0;
    for (; a && *a && k + 1 < sizeof C->err; ) C->err[k++] = *a++;
    for (; b && *b && k + 1 < sizeof C->err; ) C->err[k++] = *b++;
    C->err[k] = 0;
}
static int tio_send(void *c, const uint8_t *d, uint32_t n) { (void)c; while (n) { uint32_t k = n > 4096 ? 4096 : n; if (tcp_send(s_h, d, k) < 0) return -1; d += k; n -= k; } return 0; }
static int tio_recv(void *c, uint8_t *d, uint32_t max, uint32_t to) { (void)c; int r = tcp_recv(s_h, d, (uint16_t)(max > 16384 ? 16384 : max), to); return r == TCP_EOF ? TLS_IO_EOF : r == TCP_ERR ? TLS_IO_ERR : r; }
static uint64_t tio_now(void) { return timer_get_uptime_ms(); }
static void tio_yield(void) { }

static int apreton(const char *sni, uint32_t ms)
{
    if (!s_t) s_t = (struct tls13 *)kmalloc(sizeof *s_t);
    if (!s_t) { pon_err("sin memoria", 0); return -1; }
    struct tls_io io = { 0, tio_send, tio_recv, tio_now, tio_yield };
    if (tls13_connect(s_t, &io, sni, ms)) {
        char al[16]; al[0] = 0;
        if (s_t->alert) { int v = s_t->alert, k = 0; char d[8]; al[k++] = ' '; al[k++] = '('; int m = 0; do { d[m++] = (char)('0' + v % 10); v /= 10; } while (v && m < 6); while (m) al[k++] = d[--m]; al[k++] = ')'; al[k] = 0; }
        pon_err(tls13_err_name(s_t->err), al);
        return -1;
    }
    s_tls = 1;
    return 0;
}
static int envia(const uint8_t *d, uint32_t n) { return s_tls ? tls13_send(s_t, d, n) : tio_send(0, d, n); }
static int recibe(uint8_t *b, uint32_t max, uint32_t ms)       /* > 0, 0 nada aun, -1 cerrado, -2 error */
{
    if (s_tls) { int r = tls13_recv(s_t, b, max, ms); return r == TLS_IO_EOF ? -1 : r == TLS_IO_ERR ? -2 : r; }
    int r = tcp_recv(s_h, b, (uint16_t)(max > 16384 ? 16384 : max), ms);
    return r == TCP_EOF ? -1 : r == TCP_ERR ? -2 : r;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    out->status = 0;
    if (in->len < sizeof(struct tun_req)) return 1;
    const struct tun_req *q = (const struct tun_req *)data;
    if (q->anclas_len > TUN_ANCLAS_MAX || sizeof *q + (uint64_t)q->anclas_len > in->len) return 1;
    for (int i = 0; i < 2000 && C->magic != TUN_MAGIC; ++i) duerme(1);      /* el nucleo escribe la cabecera al lanzar */
    if (C->magic != TUN_MAGIC || C->version != TUN_VERSION) return 1;
    memcpy(s_semilla, q->semilla, sizeof s_semilla); s_ctr = 0;
    s_unix0 = q->unix_time; s_up0 = timer_get_uptime_ms();
    tls13_verify = q->verifica ? 1 : 0;
    tls13_unix_time = unix_now;
    x509_set_roots(tls_roots_blob, tls_roots_len);
    x509_clear_anchors();
    if (q->anclas_len) (void)x509_add_anchors_pem((const char *)(q + 1), q->anclas_len);
    char sni[256]; uint32_t k = 0; while (k + 1 < sizeof sni && q->sni[k]) { sni[k] = q->sni[k]; k++; } sni[k] = 0;

    s_h = tcp_connect(q->ip, q->port, q->ms_conecta > 15000 ? 15000 : q->ms_conecta);
    if (s_h < 0) { pon_err("no se pudo conectar", 0); BAR(); C->estado = TUN_E_ERROR; return 0; }
    if (q->tls && apreton(sni, q->ms_conecta > 15000 ? 15000 : q->ms_conecta)) { tcp_close(s_h); BAR(); C->estado = TUN_E_ERROR; return 0; }
    BAR(); C->estado = TUN_E_LISTO;

    uint8_t *buf = (uint8_t *)kmalloc(16384);
    if (!buf) { pon_err("sin memoria", 0); C->estado = TUN_E_ERROR; return 0; }
    for (;;) {
        BAR();
        uint32_t seq = C->cmd_seq;
        if (seq != C->cmd_hecho) {
            int res = -1;
            if (C->cmd == TUN_CMD_CIERRA) {
                if (s_tls) tls13_close(s_t);
                tcp_close(s_h);
                C->cmd_res = 0; BAR(); C->cmd_hecho = seq; C->estado = TUN_E_CERRADO;
                return 0;
            }
            if (C->cmd == TUN_CMD_STARTTLS && !s_tls && C->estado == TUN_E_LISTO) {
                char s2[256]; uint32_t j = 0; while (j + 1 < sizeof s2 && C->sni[j]) { s2[j] = C->sni[j]; j++; } s2[j] = 0;
                res = apreton(s2, 15000);
                if (res) C->estado = TUN_E_ERROR;
            }
            C->cmd_res = res; BAR(); C->cmd_hecho = seq;
        }
        int hizo = 0;
        if (C->estado == TUN_E_LISTO) {
            uint32_t w = C->tx_w, r = C->tx_r, n = w - r;
            if (n > TUN_RING) { pon_err("anillo de salida incoherente", 0); C->estado = TUN_E_ERROR; continue; }
            if (n) {
                uint32_t off = r % TUN_RING, c = TUN_RING - off;
                if (c > n) c = n;
                if (c > 16384) c = 16384;
                memcpy(buf, TX + off, c);                                   /* copia: el nucleo no escribe ahi hasta que avance tx_r */
                if (envia(buf, c)) { pon_err("no se pudo enviar", 0); C->estado = TUN_E_ERROR; continue; }
                BAR(); C->tx_r = r + c; hizo = 1;
            }
        }
        if (C->estado == TUN_E_LISTO && TUN_RING - (C->rx_w - C->rx_r) >= 16384) {
            int g = recibe(buf, 16384, hizo ? 0 : 2);
            if (g > 0) {
                uint32_t w = C->rx_w, off = w % TUN_RING, c = TUN_RING - off;
                if (c > (uint32_t)g) c = (uint32_t)g;
                memcpy(RX + off, buf, c);
                if ((uint32_t)g > c) memcpy(RX, buf + c, (uint32_t)g - c);
                BAR(); C->rx_w = w + (uint32_t)g; hizo = 1;
            } else if (g < 0) { if (g == -2) pon_err("la conexion fallo", 0); BAR(); C->estado = g == -1 ? TUN_E_CERRADO : TUN_E_ERROR; }
        }
        if (!hizo) duerme(1);
    }
}
