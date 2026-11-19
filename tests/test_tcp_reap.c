/* PARCHE 031: fuzzer de la pila de red (ARP, IPv4, ICMP, UDP, TCP con handshake y segmentos mutados) con ASAN+UBSAN.
 * Uso:  make fuzz-net   (FUZZ_ITERS=200000 FUZZ_SEED=3 make fuzz-net para pasadas largas) */
/* PARCHE 133: pruebas de la politica TCP sobre la pila REAL (src/net/tcp.c): limite de semiabiertas, caducidad de conexiones atascadas, ISN imprevisible. */
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
int virtio_net_poll(void *out, uint16_t max, uint16_t *len) {
    if (qh == qt) return 0; uint16_t n = ql[qh % QN]; if (n > max) n = max; memcpy(out, q[qh % QN], n); *len = n; qh++; return 1; }
int virtio_net_send(const void *f, uint16_t len) { if (len > sizeof last_tx) len = sizeof last_tx; memcpy(last_tx, f, len); last_tx_len = len; tx_count++; return 1; }
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
static uint32_t isn_of_last(void) { return r32(last_tx + 14 + 20 + 4); }          /* nuestro SYN-ACK: campo seq */
static uint32_t last_flags(void) { return last_tx[14 + 20 + 13]; }
static void syn(uint16_t sp, uint16_t dp) { uint8_t f[1600]; int n = tcp_seg(f, sp, dp, 1000, 0, 0x02, (const uint8_t *)"", 0); push(f, (uint16_t)n); drain(); }
static void ack_to(uint16_t sp, uint16_t dp, uint32_t cseq, uint32_t sisn, uint8_t fl) { uint8_t f[1600]; int n = tcp_seg(f, sp, dp, cseq, sisn + 1, fl, (const uint8_t *)"", 0); push(f, (uint16_t)n); drain(); }
static int in_use(void) { int u; tcp_get_stats(0, 0, 0, &u); return u; }
static int halfopen(void) { int h; tcp_get_stats(0, 0, &h, 0); return h; }
static uint32_t dropped(void) { uint32_t d; tcp_get_stats(0, &d, 0, 0); return d; }
static uint32_t reaped(void) { uint32_t r; tcp_get_stats(&r, 0, 0, 0); return r; }

int main(void)
{
    memcpy(net_ip, (uint8_t[]){ 10, 0, 2, 15 }, 4); memcpy(net_gateway, (uint8_t[]){ 10, 0, 2, 2 }, 4); net_init();
    {   /* el par se anuncia por ARP: sin esto nuestras respuestas esperan a resolver su MAC */
        uint8_t f[64]; memset(f, 0, sizeof f); memcpy(f, K_MAC, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0806);
        uint8_t *a = f + 14; w16(a, 1); w16(a + 2, 0x0800); a[4] = 6; a[5] = 4; w16(a + 6, 1); memcpy(a + 8, PMAC, 6); memcpy(a + 14, PEER, 4); memcpy(a + 24, net_ip, 4);
        push(f, 42); drain();
    }
    int lh = tcp_listen(80); CK(lh >= 0, "listen");
    /* 1) inundacion de SYN: solo TCPP_HALFOPEN_MAX quedan semiabiertas, el resto se descarta sin ocupar ranuras */
    for (uint16_t p = 40000; p < 40020; ++p) syn(p, 80);
    CK(halfopen() == (int)TCPP_HALFOPEN_MAX, "semiabiertas = %d, esperado %u", halfopen(), TCPP_HALFOPEN_MAX);
    CK(in_use() == 1 + (int)TCPP_HALFOPEN_MAX, "ranuras en uso = %d", in_use());
    CK(dropped() == 20 - TCPP_HALFOPEN_MAX, "SYN descartados = %u", dropped());
    /* 2) nada caduca antes de tiempo; pasados 5 s caducan las semiabiertas y el listener sigue */
    g_ms += 4900; CK(tcp_reap() == 0, "no debe caducar nada a los 4,9 s");
    g_ms += 200;  CK(tcp_reap() == (int)TCPP_HALFOPEN_MAX, "caducan las semiabiertas"); CK(halfopen() == 0 && in_use() == 1, "ranuras liberadas, el listener sigue");
    /* 3) un cliente legitimo completa el saludo; una inundacion posterior NO lo desaloja */
    syn(41000, 80); uint32_t isn = isn_of_last(); CK((last_flags() & 0x12) == 0x12, "SYN-ACK");
    ack_to(41000, 80, 1001, isn, 0x10); CK(halfopen() == 0 && in_use() == 2, "conexion establecida");
    for (uint16_t p = 42000; p < 42030; ++p) syn(p, 80);
    CK(in_use() == 2 + (int)TCPP_HALFOPEN_MAX, "la inundacion no ocupa mas que el cupo de semiabiertas (%d)", in_use());
    g_ms += 6000; tcp_reap(); CK(in_use() == 2, "tras caducar, solo listener y la conexion establecida");
    /* 4) CLOSE_WAIT: el par cierra y la aplicacion no; caduca a los 120 s, no antes */
    ack_to(41000, 80, 1001, isn, 0x11);                                      /* FIN del cliente */
    g_ms += 119000; CK(tcp_reap() == 0 && in_use() == 2, "CLOSE_WAIT a los 119 s sigue");
    g_ms += 2000;   CK(tcp_reap() == 1 && in_use() == 1, "CLOSE_WAIT a los 121 s caduca");
    /* 5) ISN: distintos entre si y no son una progresion aritmetica aunque el reloj avance un ms */
    uint32_t isns[16]; uint32_t diffs[16]; int nd = 0;
    for (int i = 0; i < 12; ++i) { g_ms += 6000; tcp_reap(); syn((uint16_t)(43000 + i), 80); isns[i] = isn_of_last(); }
    for (int i = 1; i < 12; ++i) { diffs[nd++] = isns[i] - isns[i - 1]; CK(isns[i] != isns[i - 1], "ISN repetido"); }
    int distinct = 0; for (int i = 0; i < nd; ++i) { int dup = 0; for (int j = 0; j < i; ++j) if (diffs[j] == diffs[i]) dup = 1; if (!dup) distinct++; }
    CK(distinct >= nd - 1, "los ISN consecutivos siguen una progresion (diferencias distintas: %d de %d)", distinct, nd);
    /* 6) funciones puras */
    uint8_t ip1[4] = { 10, 0, 2, 2 }, ip2[4] = { 10, 0, 2, 3 };
    CK(tcp_isn(7, ip1, 1000, 80, 100) + 250u == tcp_isn(7, ip1, 1000, 80, 101), "el reloj de 250 kHz");
    CK(tcp_isn(7, ip1, 1000, 80, 5) != tcp_isn(8, ip1, 1000, 80, 5), "otro secreto, otro ISN");
    CK(tcp_isn(7, ip1, 1000, 80, 5) != tcp_isn(7, ip2, 1000, 80, 5) && tcp_isn(7, ip1, 1000, 80, 5) != tcp_isn(7, ip1, 1001, 80, 5), "otra 4-tupla, otro ISN");
    CK(tcp_reap_after_ms(TCPP_ESTABLISHED) == 0 && tcp_reap_after_ms(TCPP_LISTEN) == 0 && tcp_reap_after_ms(TCPP_SYN_SENT) == 0, "ESTABLISHED, LISTEN y SYN_SENT nunca caducan");
    CK(tcp_should_reap(TCPP_SYN_RCVD, 5000u + 10u, 10u) && !tcp_should_reap(TCPP_SYN_RCVD, 4999u + 10u, 10u), "limite exacto");
    CK(tcp_should_reap(TCPP_SYN_RCVD, 3000u, 0xFFFFF000u), "con vuelta del contador de 32 bits");
    printf("%s: %d comprobaciones, %d fallos (caducadas %u)\n", fails ? "FALLO" : "OK", checks, fails, reaped());
    return fails != 0;
}
