/* SOMA_191: defensa de la cache ARP sobre la pila REAL (src/net/net.c). Ver docs/PARCHE_191_SEGURIDAD_GRUPO1.md.
 * Compilar con -DARP_CHAL_MS=30 (la ventana de comprobacion real es de 1 s). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "arch.h"
#include "net.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "mutex.h"
#include "thread.h"
#include "netif.h"

#ifndef ARP_CHAL_MS
#define ARP_CHAL_MS 1000u
#endif
const struct netif_ops *netif_active(void) { return 0; } int netif_count(void) { return 0; }   /* net_describe() las referencia */
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

extern uint32_t arp_stat_ignored, arp_stat_conflicts, arp_stat_rejected, arp_stat_changed;

#define QN 64
static uint8_t q[QN][1600]; static uint16_t ql[QN]; static int qh, qt;
static uint8_t last_tx[1600]; static uint16_t last_tx_len; static int tx_count;
static void w16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void push(const uint8_t *f, uint16_t n) { if (qt - qh >= QN) return; memcpy(q[qt % QN], f, n); ql[qt % QN] = n; qt++; }
static void arp_frame(int op, const uint8_t *sha, const uint8_t *spa, const uint8_t *tpa)
{
    uint8_t f[64]; memset(f, 0, sizeof f); memcpy(f, K_MAC, 6); memcpy(f + 6, sha, 6); w16(f + 12, 0x0806);
    uint8_t *a = f + 14; w16(a, 1); w16(a + 2, 0x0800); a[4] = 6; a[5] = 4; w16(a + 6, (uint16_t)op); memcpy(a + 8, sha, 6); memcpy(a + 14, spa, 4); memcpy(a + 18, K_MAC, 6); memcpy(a + 24, tpa, 4);
    push(f, 42);
}
int virtio_net_poll(void *out, uint16_t max, uint16_t *len) {
    if (qh == qt) return 0; uint16_t n = ql[qh % QN]; if (n > max) n = max; memcpy(out, q[qh % QN], n); *len = n; qh++; return 1; }
/* el "cable": si SOMA pregunta por una IP con respuesta preparada, la respuesta entra en la cola */
static uint8_t auto_ip[4], auto_mac[6]; static int auto_on;
int virtio_net_send(const void *f, uint16_t len) {
    if (len > sizeof last_tx) len = sizeof last_tx; memcpy(last_tx, f, len); last_tx_len = len; tx_count++;
    const uint8_t *b = f;
    if (auto_on && len >= 42 && b[12] == 0x08 && b[13] == 0x06 && b[21] == 1 && memcmp(b + 38, auto_ip, 4) == 0) arp_frame(2, auto_mac, auto_ip, net_ip);
    return 1; }
static void drain(void) { while (net_poll()) ; }

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

static int cached(const uint8_t *ip, const uint8_t *mac)
{
    char buf[2048], want[64]; arp_format_cache(buf, sizeof buf);
    snprintf(want, sizeof want, "%u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x", ip[0], ip[1], ip[2], ip[3], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    for (char *p = buf; *p; ++p) if (*p >= 'A' && *p <= 'F') *p = (char)(*p + 32);
    return strstr(buf, want) != 0;
}
static int has_ip(const uint8_t *ip)
{
    char buf[2048], want[32]; arp_format_cache(buf, sizeof buf);
    snprintf(want, sizeof want, "  %u.%u.%u.%u -> ", ip[0], ip[1], ip[2], ip[3]);
    return strstr(buf, want) != 0;
}
static void wait_chal(void) { uint64_t t = arch_cycle_counter(); while (arch_cycle_counter() - t < (uint64_t)(ARP_CHAL_MS + 5) * tsc_ticks_per_ms) ; }

int main(void)
{
    static const uint8_t GW[4] = { 10, 0, 2, 2 }, GWMAC[6] = { 0x52, 0x55, 10, 0, 2, 2 }, EVIL[6] = { 0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0x01 };
    static const uint8_t H1[4] = { 10, 0, 2, 50 }, H1MAC[6] = { 0x02, 0x11, 0x11, 0x11, 0x11, 0x11 }, H1NEW[6] = { 0x02, 0x22, 0x22, 0x22, 0x22, 0x22 };
    static const uint8_t OTHER[4] = { 10, 0, 2, 77 }, BC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t mac[6];
    memcpy(net_ip, (uint8_t[]){ 10, 0, 2, 15 }, 4); memcpy(net_gateway, GW, 4); net_init(); net_arp_flush();

    /* 1) respuesta no pedida y ARP gratuito (peticion que no va a nuestra IP): no entran */
    arp_frame(2, EVIL, GW, net_ip); drain();
    CK(!has_ip(GW), "una respuesta ARP no pedida entro en la cache");
    arp_frame(1, EVIL, GW, GW); drain();
    arp_frame(1, EVIL, GW, OTHER); drain();
    CK(!has_ip(GW), "un ARP gratuito entro en la cache");
    CK(arp_stat_ignored == 3, "ignoradas = %u, esperado 3", arp_stat_ignored);

    /* 2) MAC de origen de difusion o nula, y alguien que dice tener nuestra IP: fuera */
    arp_frame(1, BC, H1, net_ip); drain();
    CK(!has_ip(H1), "se acepto una MAC de difusion");
    arp_frame(1, EVIL, net_ip, net_ip); drain();
    CK(!has_ip(net_ip), "se aprendio nuestra propia IP");

    /* 3) resolucion normal: la respuesta a NUESTRA peticion si entra */
    memcpy(auto_ip, GW, 4); memcpy(auto_mac, GWMAC, 6); auto_on = 1;
    CK(arp_resolve(GW, mac, 200) == 1 && memcmp(mac, GWMAC, 6) == 0, "la puerta de enlace no se resolvio");
    auto_on = 0;
    CK(cached(GW, GWMAC), "la puerta de enlace no quedo en la cache");

    /* 4) suplantacion: otra MAC dice ser la puerta de enlace. No se cambia; se pregunta a la MAC antigua, que contesta */
    int tx0 = tx_count;
    arp_frame(2, EVIL, GW, net_ip); drain();
    CK(cached(GW, GWMAC), "la entrada se sobrescribio al vuelo");
    CK(tx_count == tx0 + 1 && memcmp(last_tx, GWMAC, 6) == 0 && last_tx[21] == 1, "no se envio la comprobacion unicast a la MAC antigua");
    arp_frame(2, EVIL, GW, net_ip); arp_frame(1, EVIL, GW, net_ip); drain();          /* insistir no sirve */
    CK(tx_count == tx0 + 2, "la insistencia provoco mas comprobaciones (amplificacion): %d", tx_count - tx0);   /* +1: respuesta a la peticion dirigida */
    arp_frame(2, GWMAC, GW, net_ip); drain();                                         /* el dueno real contesta */
    wait_chal();
    CK(arp_resolve(GW, mac, 10) == 1 && memcmp(mac, GWMAC, 6) == 0, "la suplantacion cambio la MAC de la puerta de enlace");
    CK(arp_stat_rejected == 1 && arp_stat_changed == 0, "rechazadas = %u, cambios = %u", arp_stat_rejected, arp_stat_changed);

    /* 5) un equipo pregunta por nosotros: se aprende (hay que contestarle) y se le contesta */
    tx0 = tx_count;
    arp_frame(1, H1MAC, H1, net_ip); drain();
    CK(cached(H1, H1MAC), "no se aprendio al equipo que pregunta por nosotros");
    CK(tx_count == tx0 + 1 && last_tx[21] == 2 && memcmp(last_tx, H1MAC, 6) == 0, "no se contesto a la peticion ARP");

    /* 6) cambio REAL de MAC: el antiguo ya no contesta -> tras la ventana se acepta */
    arp_frame(1, H1NEW, H1, net_ip); drain();
    CK(cached(H1, H1MAC), "el cambio se acepto sin esperar a la comprobacion");
    wait_chal();
    CK(arp_resolve(H1, mac, 10) == 1 && memcmp(mac, H1NEW, 6) == 0, "un cambio real de MAC no se acepto");
    CK(arp_stat_changed == 1, "cambios = %u, esperado 1", arp_stat_changed);

    /* 7) inundacion de peticiones dirigidas con IP inventadas: la puerta de enlace sigue en la cache */
    for (int i = 0; i < 40; ++i) { uint8_t ip[4] = { 10, 0, 3, (uint8_t)(1 + i) }, m[6] = { 0x02, 0x33, 0, 0, 0, (uint8_t)i }; arp_frame(1, m, ip, net_ip); drain(); }
    CK(cached(GW, GWMAC), "una inundacion de ARP desalojo a la puerta de enlace");

    printf("test_arp_191: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
