/* SOMA_326_TUNEL_TLS: ver tun_tls.h y src/core/tun_abi.h. */
#include "tun_tls.h"
#include "tun_abi.h"
#include "mem.h"
#include "thread.h"
#include "tcp.h"
#include "net.h"
#include "tls13.h"
#include "tls_trust.h"

uint64_t timer_get_uptime_ms(void);
static void cpy(char *d, uint32_t cap, const char *a, const char *b)
{
    uint32_t k = 0; if (!d || !cap) return;
    for (; a && *a && k + 1 < cap; ) d[k++] = *a++;
    for (; b && *b && k + 1 < cap; ) d[k++] = *b++;
    d[k] = 0;
}

#if defined(__x86_64__) && !defined(WEB_HOST_TEST) && !defined(SOMA_HOST_TEST)
/* ================================================================ x86_64: un proceso de ring 3 por conexion */
#include "sandbox.h"
#include "usermode.h"
#include "rng.h"
#include "fs.h"
extern const uint8_t _binary_tlstun_elf_start[], _binary_tlstun_elf_end[];
#define BAR() __asm__ volatile ("" ::: "memory")

struct tun { struct sbx_call c; struct uregion sh; int idx; struct tun_ctl *ctl; };

/* la region compartida esta mapeada 1:1 en el nucleo pagina a pagina (los marcos no son contiguos) */
static void sh_cp(struct tun *t, uint32_t off, uint8_t *p, uint32_t n, int escribe)
{
    while (n) {
        uint32_t k = off >> 12, o = off & 0xFFFu, c = 4096u - o; if (c > n) c = n;
        uint8_t *f = (uint8_t *)(uintptr_t)t->sh.frames[k] + o;
        if (escribe) memcpy(f, p, c); else memcpy(p, f, c);
        off += c; p += c; n -= c;
    }
}
static int vivo(const struct tun *t) { return t->idx >= 0 && usermode_proc_alive(t->idx); }

struct tun *tun_abre(const uint8_t ip[4], uint16_t port, int tls, const char *sni, uint32_t ms, char *err, uint32_t ecap)
{
    if (err && ecap) err[0] = 0;
    struct tun *t = (struct tun *)kmalloc(sizeof *t);
    if (!t) { cpy(err, ecap, "sin memoria", 0); return 0; }
    memset(t, 0, sizeof *t); t->idx = -1;
    tls_trust_setup();
    uint32_t pl = 0; char *pem = vfs_read_alloc("/etc/tls_confianza.pem", &pl);
    if (!pem) pl = 0;
    if (pl > TUN_ANCLAS_MAX) pl = TUN_ANCLAS_MAX;
    uint32_t n = (uint32_t)sizeof(struct tun_req) + pl;
    uint8_t *buf = (uint8_t *)kmalloc(n);
    if (!buf) { if (pem) kfree(pem); kfree(t); cpy(err, ecap, "sin memoria", 0); return 0; }
    memset(buf, 0, sizeof(struct tun_req));
    struct tun_req *q = (struct tun_req *)buf;
    for (int i = 0; i < 4; ++i) q->ip[i] = ip[i];
    q->port = port; q->tls = tls ? 1 : 0; q->ms_conecta = ms ? ms : 15000; q->verifica = tls13_verify ? 1 : 0; q->anclas_len = pl;
    q->unix_time = tls13_unix_time ? tls13_unix_time() : 0;
    rng_bytes(q->semilla, sizeof q->semilla);
    for (uint32_t k = 0; sni && sni[k] && k + 1 < sizeof q->sni; ++k) q->sni[k] = sni[k];
    if (pl) memcpy(buf + sizeof *q, pem, pl);
    if (pem) kfree(pem);
    sbx_prepare(&t->c, 0, buf, n, TUN_HEAP);
    const uint32_t caps = USER_SYS_NET_MASK | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS);
    t->idx = sbx_lanza(&t->c, _binary_tlstun_elf_start, (size_t)(_binary_tlstun_elf_end - _binary_tlstun_elf_start), 24u * 3600u * 1000u, caps, &t->sh, TUN_SH_VA, TUN_SH_PAGES);
    { volatile uint8_t *v = buf; for (uint32_t i = 0; i < n; ++i) v[i] = 0; }      /* lleva la semilla: se borra */
    kfree(buf);
    if (t->idx < 0) { cpy(err, ecap, "no se pudo lanzar el tunel aislado (sin procesos libres o memoria)", 0); kfree(t); return 0; }
    t->ctl = (struct tun_ctl *)(uintptr_t)t->sh.frames[0];
    t->ctl->version = TUN_VERSION; BAR(); t->ctl->magic = TUN_MAGIC;
    uint64_t t0 = timer_get_uptime_ms(), plazo = 2u * (uint64_t)(ms ? (ms > 15000 ? 15000 : ms) : 15000) + 3000u;   /* conexion + apreton + arranque */
    while (t->ctl->estado == TUN_E_CONECTANDO && vivo(t) && timer_get_uptime_ms() - t0 < plazo) thread_sleep(2);
    BAR();
    if (t->ctl->estado != TUN_E_LISTO) {
        char m[160]; uint32_t k = 0; while (k + 1 < sizeof m && t->ctl->err[k] && k + 1 < sizeof t->ctl->err) { m[k] = t->ctl->err[k]; k++; } m[k] = 0;
        cpy(err, ecap, m[0] ? m : (vivo(t) ? "el servidor no responde" : "el tunel aislado termino"), 0);
        tun_cierra(t, 1);
        return 0;
    }
    return t;
}

int tun_starttls(struct tun *t, const char *sni, char *err, uint32_t ecap)
{
    if (err && ecap) err[0] = 0;
    if (!t || !vivo(t) || t->ctl->estado != TUN_E_LISTO) { cpy(err, ecap, "la conexion no esta abierta", 0); return -1; }
    uint32_t k = 0; for (; sni && sni[k] && k + 1 < sizeof t->ctl->sni; ++k) t->ctl->sni[k] = sni[k];
    t->ctl->sni[k] = 0;
    t->ctl->cmd = TUN_CMD_STARTTLS; BAR();
    uint32_t seq = t->ctl->cmd_seq + 1; t->ctl->cmd_seq = seq;
    uint64_t t0 = timer_get_uptime_ms();
    while (t->ctl->cmd_hecho != seq && vivo(t) && timer_get_uptime_ms() - t0 < 20000) thread_sleep(2);
    BAR();
    if (t->ctl->cmd_hecho != seq || t->ctl->cmd_res != 0) {
        char m[160]; uint32_t j = 0; while (j + 1 < sizeof m && t->ctl->err[j]) { m[j] = t->ctl->err[j]; j++; } m[j] = 0;
        cpy(err, ecap, m[0] ? m : "el servidor no responde", 0);
        return -1;
    }
    return 0;
}

int tun_envia(struct tun *t, const void *d, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)d;
    uint64_t t0 = timer_get_uptime_ms();
    while (n) {
        BAR();
        if (!t || t->ctl->estado != TUN_E_LISTO || !vivo(t)) return -1;
        uint32_t w = t->ctl->tx_w, libre = TUN_RING - (w - t->ctl->tx_r);
        if (!libre) { if (timer_get_uptime_ms() - t0 > 30000) return -1; thread_sleep(1); continue; }
        uint32_t off = w % TUN_RING, c = TUN_RING - off;
        if (c > libre) c = libre;
        if (c > n) c = n;
        sh_cp(t, TUN_TX_OFF + off, (uint8_t *)(uintptr_t)p, c, 1);
        BAR(); t->ctl->tx_w = w + c;
        p += c; n -= c; t0 = timer_get_uptime_ms();
    }
    return 0;
}

int tun_recibe(struct tun *t, void *b, uint32_t max, uint32_t ms)
{
    if (!t || !max) return -2;
    uint8_t *o = (uint8_t *)b;
    uint64_t t0 = timer_get_uptime_ms();
    for (;;) {
        BAR();
        uint32_t r = t->ctl->rx_r, hay = t->ctl->rx_w - r;
        if (hay > TUN_RING) return -2;                                    /* el proceso no puede mentir sobre el anillo */
        if (hay) {
            uint32_t c = hay < max ? hay : max, off = r % TUN_RING, k = TUN_RING - off;
            if (k > c) k = c;
            sh_cp(t, TUN_RX_OFF + off, o, k, 0);
            if (c > k) sh_cp(t, TUN_RX_OFF, o + k, c - k, 0);
            BAR(); t->ctl->rx_r = r + c;
            return (int)c;
        }
        int e = t->ctl->estado;
        if (e == TUN_E_CERRADO) return -1;
        if (e == TUN_E_ERROR || !vivo(t)) return -2;
        if (timer_get_uptime_ms() - t0 >= ms) return 0;
        thread_sleep(1);
    }
}

void tun_cierra(struct tun *t, int sin_despedida)
{
    if (!t) return;
    if (vivo(t)) {                                    /* se le pide que cierre; si no responde (o hay prisa) se para */
        t->ctl->cmd = TUN_CMD_CIERRA; BAR();
        uint32_t seq = t->ctl->cmd_seq + 1; t->ctl->cmd_seq = seq;
        uint64_t t0 = timer_get_uptime_ms(), plazo = sin_despedida ? 300u : 2000u;
        while (vivo(t) && timer_get_uptime_ms() - t0 < plazo) thread_sleep(2);
    }
    sbx_termina(&t->c, t->idx, &t->sh);
    kfree(t);
}

#else
/* ================================================================ aarch64 (sin EL0) y pruebas del anfitrion: aqui mismo, como antes */
#include "dns.h"
struct tun { int h, on; struct tls13 *tls; };
static int tio_send(void *c, const uint8_t *d, uint32_t n) { int h = ((struct tun *)c)->h; while (n) { uint32_t k = n > 4096 ? 4096 : n; if (tcp_send(h, d, k) < 0) return -1; d += k; n -= k; } return 0; }
static int tio_recv(void *c, uint8_t *d, uint32_t max, uint32_t to) { int r = tcp_recv(((struct tun *)c)->h, d, (uint16_t)(max > 16384 ? 16384 : max), to); return r == TCP_EOF ? TLS_IO_EOF : r == TCP_ERR ? TLS_IO_ERR : r; }
static uint64_t tio_now(void) { return timer_get_uptime_ms(); }
static void tio_yield(void) { thread_yield(); }
static int apreton(struct tun *t, const char *sni, uint32_t ms, char *err, uint32_t ecap)
{
    if (!t->tls) t->tls = (struct tls13 *)kmalloc(sizeof(struct tls13));
    if (!t->tls) { cpy(err, ecap, "sin memoria", 0); return -1; }
    struct tls_io io = { t, tio_send, tio_recv, tio_now, tio_yield };
    tls_trust_setup();
    if (tls13_connect(t->tls, &io, sni, ms)) { cpy(err, ecap, tls13_err_name(t->tls->err), 0); return -1; }
    t->on = 1; return 0;
}
struct tun *tun_abre(const uint8_t ip[4], uint16_t port, int tls, const char *sni, uint32_t ms, char *err, uint32_t ecap)
{
    if (err && ecap) err[0] = 0;
    struct tun *t = (struct tun *)kmalloc(sizeof *t);
    if (!t) { cpy(err, ecap, "sin memoria", 0); return 0; }
    t->h = -1; t->on = 0; t->tls = 0;
    net_client_lock(); t->h = tcp_connect(ip, port, ms > 10000 || !ms ? 10000 : ms); net_client_unlock();
    if (t->h < 0) { cpy(err, ecap, "no se pudo conectar", 0); kfree(t); return 0; }
    if (tls && apreton(t, sni, ms > 15000 || !ms ? 15000 : ms, err, ecap)) { tcp_abort(t->h); if (t->tls) kfree(t->tls); kfree(t); return 0; }
    return t;
}
int tun_starttls(struct tun *t, const char *sni, char *err, uint32_t ecap) { if (err && ecap) err[0] = 0; return t && !t->on ? apreton(t, sni, 15000, err, ecap) : -1; }
int tun_envia(struct tun *t, const void *d, uint32_t n) { if (!t || t->h < 0) return -1; return t->on ? tls13_send(t->tls, d, n) : tio_send(t, (const uint8_t *)d, n); }
int tun_recibe(struct tun *t, void *b, uint32_t max, uint32_t ms)
{
    if (!t || t->h < 0) return -2;
    if (t->on) { int r = tls13_recv(t->tls, b, max, ms); return r == TLS_IO_EOF ? -1 : r == TLS_IO_ERR ? -2 : r; }
    int r = tcp_recv(t->h, b, (uint16_t)(max > 16384 ? 16384 : max), ms);
    return r == TCP_EOF ? -1 : r == TCP_ERR ? -2 : r;
}
void tun_cierra(struct tun *t, int sin_despedida)
{
    if (!t) return;
    if (t->h >= 0) { if (t->on && !sin_despedida) tls13_close(t->tls); if (sin_despedida) tcp_abort(t->h); else tcp_close(t->h); }
    if (t->tls) kfree(t->tls);
    kfree(t);
}
#endif
