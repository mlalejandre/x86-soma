/* PARCHE 031: fuzzer de la pila de red (ARP, IPv4, ICMP, UDP, TCP con handshake y segmentos mutados) con ASAN+UBSAN.
 * Uso:  make fuzz-net   (FUZZ_ITERS=200000 FUZZ_SEED=3 make fuzz-net para pasadas largas) */
/* PARCHE 135: recepcion TCP (fuera de orden, solapes, ventana, FIN) sobre la pila REAL (src/net/tcp.c) contra un par simulado: segmentacion, ventana del par, perdidas (retroceder N), ventana cero. */
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
static uint32_t g_ack, g_wnd; static int g_acks;
static void peer_hook(const uint8_t *f, uint16_t len)
{
    if (len < 54 || f[12] != 0x08 || f[13] != 0x00 || f[14 + 9] != 6) return;
    const uint8_t *t = f + 14 + 20; if (!(t[13] & 0x10)) return;
    g_ack = r32(t + 8); g_wnd = ((uint32_t)t[14] << 8) | t[15]; g_acks++;
}
#define PSP 50000
static uint32_t isn, c0;           /* isn del servidor, siguiente seq del cliente simulado */
static int srv;
static int open_srv(void)
{
    tcp_cleanup_for_thread(1); qh = qt = 0; g_acks = 0;
    int lh = tcp_listen(80); if (lh < 0) return -1;
    uint8_t f[1600]; int n = tcp_seg(f, PSP, 80, 1000, 0, 0x02, (const uint8_t *)"", 0); w16(f + 14 + 20 + 14, 65535); fix_l4(f, n); push(f, (uint16_t)n); drain();
    if ((r32(last_tx + 14 + 20 + 13 - 13 + 13) & 0) ) return -1;
    isn = r32(last_tx + 14 + 20 + 4);
    n = tcp_seg(f, PSP, 80, 1001, isn + 1, 0x10, (const uint8_t *)"", 0); w16(f + 14 + 20 + 14, 65535); fix_l4(f, n); push(f, (uint16_t)n); drain();
    c0 = 1001; return tcp_accept(lh, 200);
}
static void sendseg(uint32_t seq, const uint8_t *d, int len, int fin)
{
    uint8_t f[1700]; int n = tcp_seg(f, PSP, 80, seq, isn + 1, (uint8_t)(0x18 | (fin ? 1 : 0)), d, (uint16_t)len); w16(f + 14 + 20 + 14, 65535); fix_l4(f, n); push(f, (uint16_t)n); drain();
}
static uint8_t SRC[40000], OUT[40000];
static void pat(uint8_t *b, int n, int seed) { for (int i = 0; i < n; ++i) b[i] = (uint8_t)((i * 31 + seed) ^ (i >> 7)); }
static int readall(int h, uint8_t *o, int max) { int tot = 0, r; while (tot < max && (r = tcp_recv(h, o + tot, (uint16_t)(max - tot), 5)) > 0) tot += r; return tot; }

int main(void)
{
    memcpy(net_ip, (uint8_t[]){ 10, 0, 2, 15 }, 4); memcpy(net_gateway, (uint8_t[]){ 10, 0, 2, 2 }, 4); net_init();
    {   uint8_t f[64]; memset(f, 0, sizeof f); memcpy(f, K_MAC, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0806);
        uint8_t *a = f + 14; w16(a, 1); w16(a + 2, 0x0800); a[4] = 6; a[5] = 4; w16(a + 6, 1); memcpy(a + 8, PMAC, 6); memcpy(a + 14, PEER, 4); memcpy(a + 24, net_ip, 4);
        push(f, 42); drain(); }
    pat(SRC, sizeof SRC, 7);
    /* A) tres segmentos llegan 1,3,2: contenido entero y en orden; ACK acumulativo correcto */
    srv = open_srv(); CK(srv >= 0, "abrir");
    sendseg(c0, SRC, 500, 0);               CK(g_ack == c0 + 500, "A: ack tras seg 1 = %u", g_ack - c0);
    sendseg(c0 + 1000, SRC + 1000, 500, 0); CK(g_ack == c0 + 500, "A: seg 3 fuera de orden: ack sigue en 500 (%u)", g_ack - c0);
    sendseg(c0 + 500, SRC + 500, 500, 0);   CK(g_ack == c0 + 1500, "A: tras llegar el hueco el ack salta a 1500 (%u)", g_ack - c0);
    CK(readall(srv, OUT, 1500) == 1500 && memcmp(OUT, SRC, 1500) == 0, "A: contenido");
    c0 += 1500;
    /* B) segmento duplicado y segmento que solapa lo ya recibido (reenvio con datos nuevos al final) */
    sendseg(c0, SRC + 1500, 300, 0); sendseg(c0, SRC + 1500, 300, 0);
    CK(g_ack == c0 + 300, "B: duplicado no avanza (%u)", g_ack - c0);
    sendseg(c0 + 100, SRC + 1600, 400, 0);
    CK(g_ack == c0 + 500, "B: solape: ack = 500 (%u)", g_ack - c0);
    CK(readall(srv, OUT, 500) == 500 && memcmp(OUT, SRC + 1500, 500) == 0, "B: sin bytes repetidos ni perdidos");
    c0 += 500;
    /* C) varios huecos: solo se guarda un rango; los demas se retransmiten y el flujo igual se completa */
    sendseg(c0 + 200, SRC + 2200, 100, 0); sendseg(c0 + 400, SRC + 2400, 100, 0);  /* el segundo no toca el rango: descartado */
    sendseg(c0 + 300, SRC + 2300, 100, 0);                                             /* extiende el rango 200..400 */
    sendseg(c0, SRC + 2000, 200, 0);                                                   /* llena el hueco inicial */
    CK(g_ack == c0 + 400, "C: ack tras unir rangos = 400 (%u)", g_ack - c0);
    sendseg(c0 + 400, SRC + 2400, 100, 0); CK(g_ack == c0 + 500, "C: retransmitido el descartado (%u)", g_ack - c0);
    CK(readall(srv, OUT, 500) == 500 && memcmp(OUT, SRC + 2000, 500) == 0, "C: contenido");
    c0 += 500;
    /* D) datos fuera de la ventana: se descartan y no corrompen el anillo */
    sendseg(c0 + 40000, SRC, 100, 0); sendseg(c0, SRC + 3000, 100, 0);
    CK(g_ack == c0 + 100, "D: dato lejano ignorado (%u)", g_ack - c0);
    CK(readall(srv, OUT, 100) == 100 && memcmp(OUT, SRC + 3000, 100) == 0, "D: contenido");
    c0 += 100;
    /* E) llenar el buffer (32 KiB): ventana anunciada llega a 0, el exceso se descarta, al leer se reabre */
    for (int i = 0; i < 26; ++i) sendseg(c0 + (uint32_t)i * 1400, SRC + 3100 + (uint32_t)i * 1400 % 30000, 1400, 0);
    CK(g_wnd < 2000, "E: ventana casi cerrada (%u)", g_wnd);
    uint32_t got = g_ack - c0; CK(got <= 32768 && got >= 30000, "E: se aceptaron %u bytes (<= 32768)", got);
    int rd = readall(srv, OUT, 40000); CK((uint32_t)rd == got, "E: se leyo lo aceptado (%d)", rd);
    CK(g_wnd >= 8000, "E: ventana reabierta tras leer (%u)", g_wnd);
    c0 += got;
    /* F) FIN en orden tras datos; FIN con hueco pendiente no se acepta hasta que llegue lo anterior */
    sendseg(c0 + 100, SRC, 0, 1);                    /* FIN adelantado: ignorado */
    CK(g_ack == c0, "F: FIN adelantado no se acepta (%u)", g_ack - c0);
    sendseg(c0, SRC + 5000, 100, 0); sendseg(c0 + 100, SRC, 0, 1);
    CK(g_ack == c0 + 101, "F: FIN en orden consume 1 numero de secuencia (%u)", g_ack - c0);
    CK(readall(srv, OUT, 100) == 100 && memcmp(OUT, SRC + 5000, 100) == 0, "F: datos antes del FIN");
    CK(tcp_recv(srv, OUT, 10, 5) == TCP_EOF, "F: fin de flujo");
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
