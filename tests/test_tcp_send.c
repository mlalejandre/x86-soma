/* PARCHE 031: fuzzer de la pila de red (ARP, IPv4, ICMP, UDP, TCP con handshake y segmentos mutados) con ASAN+UBSAN.
 * Uso:  make fuzz-net   (FUZZ_ITERS=200000 FUZZ_SEED=3 make fuzz-net para pasadas largas) */
/* PARCHE 134: envio TCP con ventana sobre la pila REAL (src/net/tcp.c) contra un par simulado: segmentacion, ventana del par, perdidas (retroceder N), ventana cero. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "net.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "tcp_policy.h"
#include "mutex.h"
#include "thread.h"

/* ---- stubs del kernel ---- */
uint64_t tsc_ticks_per_ms = 1000000; static uint64_t g_ms = 1000;
/* SOMA_267_TCP_MAC: en arm64 el contador (cntvct_el0) va a la frecuencia de cntfrq_el0, no a 1 GHz: se ajustan los ticks por ms
 * para que el RTO (1000 ms) dure lo mismo que en Linux x86_64 (~0,3 s reales) en vez de ~42 s. */
static void ajusta_ticks_host(void)
{
#if defined(__aarch64__)
    uint64_t f; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(f));
    if (f >= 1000) { tsc_ticks_per_ms = f / 1000 / 3; if (tsc_ticks_per_ms == 0) tsc_ticks_per_ms = 1; }
#endif
}
uint64_t timer_get_uptime_ms(void) { return g_ms; }
void *kmalloc(size_t n) { return malloc(n ? n : 1); } void kfree(void *p) { free(p); }
void kheap_stats(size_t *u, size_t *f) { if (u) *u = 0; if (f) *f = 0; }
void pmm_get_stats(size_t *a, size_t *b, size_t *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void kprint(const char *s) { (void)s; } void kprint_dec(uint32_t v) { (void)v; } void kprint_ip(const uint8_t *p) { (void)p; } void kprint_mac(const uint8_t *p) { (void)p; }
void kprint_hex32(uint32_t v) { (void)v; }
void kmutex_init(struct kmutex *m, const char *nm) { (void)m; (void)nm; } void kmutex_lock(struct kmutex *m) { (void)m; } void kmutex_unlock(struct kmutex *m) { (void)m; } int kmutex_lock_timeout(struct kmutex *m, uint32_t t) { (void)m; (void)t; return 1; }
static struct tcb g_tcb = { .tid = 1 };
struct tcb *thread_current(void) { return &g_tcb; }
struct tcb *thread_create(const char *n, thread_func_t f, void *a) { (void)n; (void)f; (void)a; return &g_tcb; }
void thread_yield(void) { g_ms++; } void thread_sleep(uint32_t ms) { g_ms += ms; }
int vfs_format_list_ext(char *o, uint32_t m, int x) { (void)o; (void)m; (void)x; return 0; }
int vfs_get_stats(void *a, void *b) { (void)a; (void)b; return 0; }
int vfs_read(const char *p, void *b, uint32_t m) { (void)p; (void)b; (void)m; return -1; }
int virtio_net_init(void) { return 1; }
static const uint8_t K_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
const uint8_t *virtio_net_mac(void) { return K_MAC; }
void virtio_net_stats(uint32_t *a, uint32_t *b, uint64_t *c, uint64_t *d) { (void)a; (void)b; (void)c; (void)d; }

/* cola de entrada y captura de salida */
#define QN 64
static uint8_t q[QN][1600]; static uint16_t ql[QN]; static int qh, qt;
static uint8_t last_tx[1600]; static uint16_t last_tx_len; static int tx_count;
static int g_burst, g_max_burst; static uint32_t g_una; static int g_una_valid;
int virtio_net_poll(void *out, uint16_t max, uint16_t *len) { if (g_burst > g_max_burst) g_max_burst = g_burst; g_burst = 0;
    if (qh == qt) return 0; uint16_t n = ql[qh % QN]; if (n > max) n = max; memcpy(out, q[qh % QN], n); *len = n; g_una = ((uint32_t)q[qh % QN][42]<<24)|((uint32_t)q[qh % QN][43]<<16)|((uint32_t)q[qh % QN][44]<<8)|q[qh % QN][45]; g_una_valid = 1; qh++; return 1; }
static void peer_hook(const uint8_t *f, uint16_t len);
int virtio_net_send(const void *f, uint16_t len) { if (len > sizeof last_tx) len = sizeof last_tx; memcpy(last_tx, f, len); last_tx_len = len; tx_count++; peer_hook(f, len); return 1; }
static void push(const uint8_t *f, uint16_t n) { if (qt - qh >= QN) return; if (n > 1600) n = 1600; memcpy(q[qt % QN], f, n); ql[qt % QN] = n; qt++; }
static void drain(void) { while (net_poll()) ; }

static uint64_t S = 88172645463325252ULL;
static uint32_t rnd(void) { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32_t)(S >> 11); }
static uint16_t cks(const uint8_t *d, int n, uint32_t init) { uint32_t s = init; for (int i = 0; i + 1 < n; i += 2) s += (d[i] << 8) | d[i + 1]; if (n & 1) s += d[n - 1] << 8; while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return (uint16_t)~s; }
static void w16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void w32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static uint32_t r32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static const uint8_t PEER[4] = { 10, 0, 2, 2 };
static const uint8_t PMAC[6] = { 0x52, 0x55, 10, 0, 2, 2 };

/* construye una trama Ethernet+IPv4+L4 valida */
static int build(uint8_t *f, uint8_t proto, const uint8_t *l4, int l4n) {
    memcpy(f, K_MAC, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0800);
    uint8_t *ip = f + 14; ip[0] = 0x45; ip[1] = 0; w16(ip + 2, 20 + l4n); w16(ip + 4, rnd()); w16(ip + 6, 0); ip[8] = 64; ip[9] = proto; w16(ip + 10, 0);
    memcpy(ip + 12, PEER, 4); memcpy(ip + 16, net_ip, 4); w16(ip + 10, cks(ip, 20, 0)); memcpy(ip + 20, l4, l4n); return 14 + 20 + l4n; }
static void fix_l4(uint8_t *f, int n) {   /* recalcula cabeceras IP y TCP/UDP tras mutar */
    if (n < 34) return; uint8_t *ip = f + 14; int ihl = (ip[0] & 15) * 4; if (ihl < 20 || 14 + ihl > n) return;
    w16(ip + 10, 0); w16(ip + 10, cks(ip, ihl, 0));
    int l4n = n - 14 - ihl; if (l4n < 8 || (ip[9] != 6 && ip[9] != 17 && ip[9] != 1)) return; uint8_t *l4 = ip + ihl;
    if (ip[9] == 1) { w16(l4 + 2, 0); w16(l4 + 2, cks(l4, l4n, 0)); return; }
    int co = ip[9] == 6 ? 16 : 6; if (l4n < co + 2) return; w16(l4 + co, 0);
    uint32_t ps = 0; for (int i = 0; i < 4; i += 2) ps += (ip[12 + i] << 8) | ip[13 + i]; for (int i = 0; i < 4; i += 2) ps += (ip[16 + i] << 8) | ip[17 + i]; ps += ip[9] + l4n;
    w16(l4 + co, cks(l4, l4n, ps)); }

static int tcp_seg(uint8_t *f, uint16_t sp, uint16_t dp, uint32_t seq, uint32_t ack, uint8_t fl, const uint8_t *data, int dn) {
    uint8_t t[1500]; memset(t, 0, 20); w16(t, sp); w16(t + 2, dp); w32(t + 4, seq); w32(t + 8, ack); t[12] = 5 << 4; t[13] = fl; w16(t + 14, 8192);
    if (dn > 1400) dn = 1400; memcpy(t + 20, data, dn); int n = build(f, 6, t, 20 + dn); fix_l4(f, n); return n; }
static int udp_dg(uint8_t *f, uint16_t sp, uint16_t dp, const uint8_t *data, int dn) {
    uint8_t t[1500]; w16(t, sp); w16(t + 2, dp); w16(t + 4, 8 + dn); w16(t + 6, 0); if (dn > 1400) dn = 1400; memcpy(t + 8, data, dn); int n = build(f, 17, t, 8 + dn); fix_l4(f, n); return n; }


static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static int seq_lt32(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

/* ---- par simulado: receptor TCP con ventana fija, perdidas y ventana cero ---- */
#define PSP 50000
static struct {
    uint32_t expect;                 /* siguiente byte que espera (rcv_nxt del par) */
    uint32_t base;                   /* seq del primer byte de datos */
    uint8_t  data[300000]; uint32_t got;
    uint32_t wnd;                    /* ventana que anuncia */
    int drop_every, nseg, ndrop;     /* pierde cada drop_every-esimo segmento de datos nuevo */
    int zero_probes;                 /* mientras >0 anuncia ventana 0 e ignora datos; cada segmento recibido lo decrementa */
    uint32_t max_inflight; int violations; int ndata, nretx;
    uint32_t our_seq_seen_max;
    int active;
} P;
static void peer_ack(uint32_t wnd)
{
    uint8_t f[1600]; int n = tcp_seg(f, PSP, 80, 1001, P.expect, 0x10, (const uint8_t *)"", 0);
    w16(f + 14 + 20 + 14, (uint16_t)wnd); fix_l4(f, n); push(f, (uint16_t)n);
}
static void peer_hook(const uint8_t *f, uint16_t len)
{
    if (!P.active || len < 54 || f[12] != 0x08 || f[13] != 0x00 || f[14 + 9] != 6) return;
    const uint8_t *t = f + 14 + 20; uint8_t fl = t[13]; uint32_t seq = r32(t + 4); uint16_t hl = (uint16_t)((t[12] >> 4) * 4);
    int plen = len - 14 - 20 - hl; if (plen <= 0 || (fl & 0x04)) return;
    const uint8_t *pl = t + hl;
    P.ndata++; g_burst++;
    if (P.zero_probes > 0) { if (--P.zero_probes == 0) peer_ack(P.wnd); else peer_ack(0); return; }
    uint32_t end = seq + (uint32_t)plen;
    if (g_una_valid && !seq_lt32(end, g_una)) { uint32_t fl = end - g_una; if (fl > P.max_inflight) P.max_inflight = fl; if (P.wnd && fl > P.wnd) P.violations++; }      /* el emisor excedio la ventana anunciada */
    if (seq == P.expect) {
        if (P.drop_every && (++P.nseg % P.drop_every) == 0) { P.ndrop++; return; }                  /* perdido en la red */
        if (P.got + (uint32_t)plen <= sizeof P.data) memcpy(P.data + P.got, pl, (size_t)plen);
        P.got += (uint32_t)plen; P.expect += (uint32_t)plen;
    } else if (seq_lt32(seq, P.expect)) P.nretx++;                                                   /* duplicado */
    peer_ack(P.wnd);
}
static uint32_t isn_of_last(void) { return r32(last_tx + 14 + 20 + 4); }
static uint32_t last_flags(void) { return last_tx[14 + 20 + 13]; }
static int open_conn(void)
{
    tcp_cleanup_for_thread(1); memset(&P, 0, sizeof P); g_una_valid = 0; g_max_burst = 0; g_burst = 0; qh = qt = 0;
    int lh = tcp_listen(80); if (lh < 0) return -1;
    uint8_t f[1600]; int n = tcp_seg(f, PSP, 80, 1000, 0, 0x02, (const uint8_t *)"", 0); w16(f + 14 + 20 + 14, 65535); fix_l4(f, n); push(f, (uint16_t)n); drain();
    if ((last_flags() & 0x12) != 0x12) return -1;
    uint32_t isn = isn_of_last();
    P.expect = isn + 1; P.wnd = 65535; P.active = 0;
    n = tcp_seg(f, PSP, 80, 1001, isn + 1, 0x10, (const uint8_t *)"", 0); w16(f + 14 + 20 + 14, 65535); fix_l4(f, n); push(f, (uint16_t)n); drain();
    int h = tcp_accept(lh, 200); P.active = 1; return h;
}
static void pattern(uint8_t *b, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; ++i) b[i] = (uint8_t)((i * 2654435761u + seed) >> 13); }
static uint8_t SRC[300000];

int main(void)
{
    ajusta_ticks_host();                                       /* SOMA_267_TCP_MAC */
    memcpy(net_ip, (uint8_t[]){ 10, 0, 2, 15 }, 4); memcpy(net_gateway, (uint8_t[]){ 10, 0, 2, 2 }, 4); net_init();
    {   uint8_t f[64]; memset(f, 0, sizeof f); memcpy(f, K_MAC, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0806);
        uint8_t *a = f + 14; w16(a, 1); w16(a + 2, 0x0800); a[4] = 6; a[5] = 4; w16(a + 6, 1); memcpy(a + 8, PMAC, 6); memcpy(a + 14, PEER, 4); memcpy(a + 24, net_ip, 4);
        push(f, 42); drain(); }
    /* A) sin perdidas, ventana grande: 100 KB exactos, y hay segmentos en vuelo a la vez (no parada y espera) */
    int h = open_conn(); CK(h >= 0, "no se pudo abrir la conexion");
    pattern(SRC, 100000, 1); P.base = P.expect;
    CK(tcp_send(h, SRC, 100000) == 100000, "A: envio");
    CK(P.got == 100000 && memcmp(P.data, SRC, 100000) == 0, "A: el par recibio %u bytes exactos", P.got);
    CK(g_max_burst >= 2, "A: sin segmentos en vuelo simultaneos (rafaga maxima %d)", g_max_burst);
    CK(P.violations == 0, "A: violaciones de ventana %d", P.violations);
    printf("  A) 100 KB: %d segmentos de datos, rafaga maxima %d, maximo en vuelo %u bytes\n", P.ndata, g_max_burst, P.max_inflight);
    /* B) ventana pequena del par (3000 bytes): nunca se excede */
    h = open_conn(); CK(h >= 0, "B: abrir"); pattern(SRC, 60000, 2); P.wnd = 3000;
    CK(tcp_send(h, SRC, 60000) == 60000, "B: envio"); CK(P.got == 60000 && memcmp(P.data, SRC, 60000) == 0, "B: contenido");
    CK(P.violations == 0 && P.max_inflight <= 3000, "B: en vuelo hasta %u bytes con ventana 3000 (violaciones %d)", P.max_inflight, P.violations);
    /* C) perdidas: cada 7o segmento nuevo se pierde; el flujo llega entero y en orden */
    h = open_conn(); CK(h >= 0, "C: abrir"); pattern(SRC, 50000, 3); P.drop_every = 7;
    CK(tcp_send(h, SRC, 50000) == 50000, "C: envio con perdidas"); CK(P.got == 50000 && memcmp(P.data, SRC, 50000) == 0, "C: el flujo llego entero y en orden (%u bytes)", P.got);
    CK(P.ndrop >= 3, "C: se simularon %d perdidas", P.ndrop);
    printf("  C) 50 KB con %d perdidas: %d segmentos duplicados recibidos por el par\n", P.ndrop, P.nretx);
    /* D) ventana cero: el emisor espera y sondea; cuando el par abre la ventana, termina */
    h = open_conn(); CK(h >= 0, "D: abrir"); pattern(SRC, 20000, 4); P.zero_probes = 2;
    CK(tcp_send(h, SRC, 20000) == 20000, "D: envio con ventana cero inicial"); CK(P.got == 20000 && memcmp(P.data, SRC, 20000) == 0, "D: contenido");
    /* E) el par se reinicia (RST) a mitad: el envio falla, no se cuelga */
    h = open_conn(); CK(h >= 0, "E: abrir"); pattern(SRC, 40000, 5);
    { uint8_t f[1600]; int n = tcp_seg(f, PSP, 80, P.expect - P.expect + 1001, 0, 0x04, (const uint8_t *)"", 0); push(f, (uint16_t)n); }
    CK(tcp_send(h, SRC, 40000) == -1, "E: tras un RST el envio debe fallar");
    /* F) tamanos extremos */
    h = open_conn(); CK(h >= 0, "F: abrir"); CK(tcp_send(h, SRC, 0) == 0, "F: 0 bytes"); CK(tcp_send(h, SRC, 1) == 1 && P.got == 1 && P.data[0] == SRC[0], "F: 1 byte");
    pattern(SRC, 1461, 6); CK(tcp_send(h, SRC, 1461) == 1461 && P.got == 1462, "F: 1461 bytes (un segmento y un byte)");
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
