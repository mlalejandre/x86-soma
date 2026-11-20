#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "io.h"
#include "mem.h"
#include "net.h"
#include "ip.h"
#include "tcp.h"
#include "virtio_net.h"
#include "thread.h"
#include "netif.h"
#include "mutex.h"
#include "rng.h"
#include "soma_str.h"

#define ETH_HLEN        14
#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IPV4  0x0800

#define ARP_LEN         28
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2
#define ARP_CACHE_SIZE  8

extern uint64_t tsc_ticks_per_ms;
#define NET_TSC_PER_MS  tsc_ticks_per_ms

uint8_t net_mac[6];
uint8_t net_ip[4]      = { 10, 0, 2, 15 };
uint8_t net_gateway[4] = { 10, 0, 2, 2 };

struct arp_entry {
    uint8_t ip[4];
    uint8_t mac[6];
    uint8_t valid;
    uint8_t pad;
} __attribute__((aligned(4)));

static struct arp_entry arp_cache[ARP_CACHE_SIZE] __attribute__((aligned(8)));
static uint8_t arp_next;

static uint8_t rx_frame[2048] __attribute__((aligned(16)));
static volatile int net_rx_depth;
static struct kmutex net_big_lock;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void net_arp_flush(void) { for (int i = 0; i < ARP_CACHE_SIZE; i++) memset(&arp_cache[i], 0, sizeof arp_cache[i]); }   /* SOMA_170: al cambiar de red */
/* SOMA_191 (seguridad): la cache ARP ya no se cree cualquier trama. Antes `arp_learn` aceptaba todo ARP recibido y sobrescribia las
 * entradas: un equipo de la misma red se hacia pasar por la puerta de enlace con una sola trama. Ahora:
 *   - una entrada NUEVA solo entra si es la respuesta a una peticion nuestra en curso (arp_resolve) o si es una peticion dirigida a
 *     nuestra IP (hace falta la MAC del que pregunta para contestarle);
 *   - una entrada EXISTENTE no se cambia al vuelo: se pregunta directamente (unicast) a la MAC antigua y el cambio solo se acepta si
 *     en ARP_CHAL_MS no contesta (el equipo cambio de verdad). Si contesta, es una suplantacion y se descarta;
 *   - las tramas no pedidas no pueden desalojar de la cache a la puerta de enlace. */
#ifndef ARP_CHAL_MS
#define ARP_CHAL_MS 1000u
#endif
/* SOMA_192: varias peticiones propias a la vez (antes una sola ranura: dos hilos resolviendo direcciones distintas se pisaban
 * y uno de los dos esperaba al reintento, 500 ms). */
#define ARP_WAIT_N 4
static struct { uint8_t ip[4]; uint8_t active; uint64_t until; } arp_wait[ARP_WAIT_N];             /* peticiones propias en curso */
static struct { uint8_t ip[4], mac[6]; uint8_t active, old_seen; uint64_t t0; } arp_chal;          /* cambio de MAC en comprobacion */
uint32_t arp_stat_ignored, arp_stat_conflicts, arp_stat_rejected, arp_stat_changed;
static uint64_t arp_ticks(uint32_t ms) { return (uint64_t)ms * (NET_TSC_PER_MS > 0 ? NET_TSC_PER_MS : 62500ULL); }
static struct arp_entry *arp_find(const uint8_t *ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; ++i)
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) return &arp_cache[i];
    return 0;
}
static void arp_wait_set(const uint8_t *ip, uint64_t until)
{
    uint64_t now = arch_cycle_counter(); int slot = -1;
    for (int i = 0; i < ARP_WAIT_N && slot < 0; ++i) if (arp_wait[i].active && memcmp(arp_wait[i].ip, ip, 4) == 0) slot = i;
    for (int i = 0; i < ARP_WAIT_N && slot < 0; ++i) if (!arp_wait[i].active || now >= arp_wait[i].until) slot = i;
    if (slot < 0) { slot = 0; for (int i = 1; i < ARP_WAIT_N; ++i) if (arp_wait[i].until < arp_wait[slot].until) slot = i; }
    memcpy(arp_wait[slot].ip, ip, 4); arp_wait[slot].until = until; arp_wait[slot].active = 1;
}
static int arp_wait_has(const uint8_t *ip)
{
    uint64_t now = arch_cycle_counter();
    for (int i = 0; i < ARP_WAIT_N; ++i)
        if (arp_wait[i].active && now < arp_wait[i].until && memcmp(arp_wait[i].ip, ip, 4) == 0) return 1;
    return 0;
}
static void arp_chal_settle(void)
{
    if (!arp_chal.active || arch_cycle_counter() - arp_chal.t0 < arp_ticks(ARP_CHAL_MS)) return;
    arp_chal.active = 0;
    if (arp_chal.old_seen) { arp_stat_rejected++; return; }            /* el dueno anterior sigue ahi: era una suplantacion */
    struct arp_entry *e = arp_find(arp_chal.ip);
    if (e) { memcpy(e->mac, arp_chal.mac, 6); arp_stat_changed++; }
}
static void arp_insert(const uint8_t *ip, const uint8_t *mac, int solicited)
{
    struct arp_entry *e = 0;
    for (int i = 0; i < ARP_CACHE_SIZE && !e; ++i) if (!arp_cache[i].valid) e = &arp_cache[i];
    for (int k = 0; k < ARP_CACHE_SIZE && !e; ++k) {
        struct arp_entry *c = &arp_cache[arp_next++ % ARP_CACHE_SIZE];
        if (!solicited && memcmp(c->ip, net_gateway, 4) == 0) continue;                 /* lo no pedido no desaloja a la puerta de enlace */
        if (arp_chal.active && memcmp(c->ip, arp_chal.ip, 4) == 0) continue;
        e = c;
    }
    if (!e) return;
    memcpy(e->ip, ip, 4);
    memcpy(e->mac, mac, 6);
    e->valid = 1;
}

static int arp_lookup(const uint8_t *ip, uint8_t *mac_out)
{
    arp_chal_settle();
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

int arp_format_cache(char *out, uint32_t max)
{
    uint32_t pos = 0;
    soma_str_put(out, max, &pos, "Cache ARP en vivo:\n");
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (!arp_cache[i].valid) continue;
        count++;
        soma_str_put(out, max, &pos, "  ");
        for (int k = 0; k < 4; ++k) {
            soma_str_put_dec(out, max, &pos, arp_cache[i].ip[k]);
            if (k != 3) soma_str_putc(out, max, &pos, '.');
        }
        soma_str_put(out, max, &pos, " -> ");
        for (int k = 0; k < 6; ++k) {
            soma_str_put_hex(out, max, &pos, arp_cache[i].mac[k], 2, 1);
            if (k != 5) soma_str_putc(out, max, &pos, ':');
        }
        soma_str_putc(out, max, &pos, '\n');
    }
    if (count == 0) soma_str_put(out, max, &pos, "  (vacia)\n");
    {   /* SOMA_191: contadores de la defensa */
        static const char *const lbl[4] = { "  ARP no pedido ignorado: ", ", conflictos: ", ", suplantaciones rechazadas: ", ", cambios aceptados: " };
        const uint32_t val[4] = { arp_stat_ignored, arp_stat_conflicts, arp_stat_rejected, arp_stat_changed };
        for (int i = 0; i < 4; ++i) {
            char d[12]; int n = 0; uint32_t v = val[i];
            do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
            soma_str_put(out, max, &pos, lbl[i]);
            while (n) soma_str_putc(out, max, &pos, d[--n]);
        }
        soma_str_putc(out, max, &pos, '\n');
    }
    return (int)pos;
}

static void arp_send(uint16_t op, const uint8_t *eth_dst, const uint8_t *target_mac, const uint8_t *target_ip)
{
    uint8_t f[ETH_HLEN + ARP_LEN + 18];
    memset(f, 0, sizeof(f));
    memcpy(f, eth_dst, 6);
    memcpy(f + 6, net_mac, 6);
    wr16(f + 12, ETHERTYPE_ARP);
    uint8_t *a = f + ETH_HLEN;
    wr16(a, 1);
    wr16(a + 2, ETHERTYPE_IPV4);
    a[4] = 6; a[5] = 4;
    wr16(a + 6, op);
    memcpy(a + 8,  net_mac, 6);
    memcpy(a + 14, net_ip, 4);
    memcpy(a + 18, target_mac, 6);
    memcpy(a + 24, target_ip, 4);
    virtio_net_send(f, sizeof(f));
}

/* SOMA_191: decide si una trama ARP puede tocar la cache (ver el comentario de arp_wait/arp_chal). */
static void arp_input(uint16_t op, const uint8_t *sha, const uint8_t *spa, const uint8_t *tpa)
{
    static const uint8_t zero_ip[4] = { 0, 0, 0, 0 }, zero_mac[6] = { 0, 0, 0, 0, 0, 0 };
    arp_chal_settle();
    if (memcmp(spa, zero_ip, 4) == 0 || memcmp(spa, net_ip, 4) == 0) return;            /* sondas, o alguien que dice tener NUESTRA IP */
    if ((sha[0] & 1) || memcmp(sha, zero_mac, 6) == 0) return;                          /* MAC de origen de difusion/grupo o nula */
    struct arp_entry *e = arp_find(spa);
    if (e) {
        if (memcmp(e->mac, sha, 6) == 0) {
            if (arp_chal.active && memcmp(arp_chal.ip, spa, 4) == 0) arp_chal.old_seen = 1;
            return;
        }
        arp_stat_conflicts++;
        if (arp_chal.active) return;                                                    /* una comprobacion cada vez (tampoco amplificamos) */
        uint8_t old[6]; memcpy(old, e->mac, 6);
        memcpy(arp_chal.ip, spa, 4); memcpy(arp_chal.mac, sha, 6);
        arp_chal.old_seen = 0; arp_chal.t0 = arch_cycle_counter(); arp_chal.active = 1;
        arp_send(ARP_OP_REQUEST, old, zero_mac, spa);                                   /* "sigues ahi?" solo a la MAC antigua */
        return;
    }
    int solicited = op == ARP_OP_REPLY && arp_wait_has(spa);
    int to_us = op == ARP_OP_REQUEST && memcmp(tpa, net_ip, 4) == 0 && memcmp(net_ip, zero_ip, 4) != 0;
    if (!solicited && !to_us) { arp_stat_ignored++; return; }
    arp_insert(spa, sha, solicited);
}

static void handle_arp(const uint8_t *a, uint16_t len)
{
    if (len < ARP_LEN) return;
    if (rd16(a) != 1 || rd16(a + 2) != ETHERTYPE_IPV4 || a[4] != 6 || a[5] != 4) return;
    uint16_t op = rd16(a + 6);
    const uint8_t *sha = a + 8;
    const uint8_t *spa = a + 14;
    const uint8_t *tpa = a + 24;
    arp_input(op, sha, spa, tpa);                                   /* SOMA_191 */
    if (op == ARP_OP_REQUEST && memcmp(tpa, net_ip, 4) == 0) {
        arp_send(ARP_OP_REPLY, sha, sha, spa);
    }
}

static void handle_frame(const uint8_t *f, uint16_t len)
{
    if (len < ETH_HLEN) return;
    uint16_t type = rd16(f + 12);
    if (type == ETHERTYPE_ARP) handle_arp(f + ETH_HLEN, (uint16_t)(len - ETH_HLEN));
    if (type == ETHERTYPE_IPV4) ip_input(f, len);
}

int net_init(void)
{
    /* PARCHE 047: idempotente. Re-ejecutar virtio_net_init() re-inicializa las colas
     * y deja el dispositivo en FAILED. */
    static int net_inited = 0;
    if (net_inited) return 1;
    if (!virtio_net_init()) return 0;
    net_inited = 1;
    memcpy(net_mac, virtio_net_mac(), 6);
    kprint("\nNET: MAC "); kprint_mac(net_mac);
    kprint("  IP "); kprint_ip(net_ip);
    kprint("  GW "); kprint_ip(net_gateway); kprint("\n");
    return 1;
}

int net_poll(void)
{
    uint16_t len = 0;
    if (!virtio_net_poll(rx_frame, sizeof(rx_frame), &len)) return 0;
    net_rx_depth++;
    rng_add_event(((uint64_t)len << 48) ^ rx_frame[len ? len - 1 : 0]);   /* SOMA_192: el instante de llegada de cada trama alimenta el generador aleatorio */
    handle_frame(rx_frame, len);
    net_rx_depth--;
    return 1;
}

void net_wait_step(void)
{
    net_lock();
    int got = net_poll();
    net_unlock();
    tcp_reap_tick();                         /* PARCHE 133 */
    if (net_rx_depth == 0) {
        /* SOMA_PARCHE_007_IDLE: con trafico se sigue sin demora; si no habia nada que
         * procesar se duerme 1 ms para que corra idle (antes: 100% de CPU esperando). */
        if (got) thread_yield();
        else thread_sleep(1);
    }
}

static int net_lock_ready;
void net_lock(void)
{
    if (!net_lock_ready) { kmutex_init(&net_big_lock, "net_lock"); net_lock_ready = 1; }
    kmutex_lock(&net_big_lock);
}
void net_unlock(void) { kmutex_unlock(&net_big_lock); }

/* SOMA_PARCHE_013_NETLOCK: dos niveles.
 *  net_client_lock : serializa las PETICIONES salientes (LLM, HTTP, DNS, ping). Se mantiene durante toda
 *                    la peticion, que puede durar segundos esperando al servidor.
 *  net_lock        : protege la pila (cola RX/TX, tablas). Solo se toma en secciones cortas
 *                    (net_poll, envio), asi netd y httpd siguen atendiendo trafico mientras un hilo espera
 *                    una respuesta. Orden de adquisicion: net_client_lock -> net_lock (nunca al reves). */
static struct kmutex net_client_mutex;
static int net_client_ready;
void net_client_lock(void)
{
    if (!net_client_ready) { kmutex_init(&net_client_mutex, "net_client"); net_client_ready = 1; }
    kmutex_lock(&net_client_mutex);
}
void net_client_unlock(void) { kmutex_unlock(&net_client_mutex); }

int arp_resolve(const uint8_t *ip, uint8_t *mac_out, uint32_t timeout_ms)
{
    static const uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t zero_mac[6]  = { 0, 0, 0, 0, 0, 0 };
    if (arp_lookup(ip, mac_out)) return 1;

    const int attempts = 3;
    uint64_t per_attempt = (uint64_t)timeout_ms * (NET_TSC_PER_MS > 0 ? NET_TSC_PER_MS : 62500ULL) / attempts;

    for (int n = 0; n < attempts; ++n) {
        arp_wait_set(ip, arch_cycle_counter() + 2 * per_attempt);   /* SOMA_191/192: solo vale la respuesta a una peticion nuestra; se anota ANTES de enviar */
        arp_send(ARP_OP_REQUEST, broadcast, zero_mac, ip);
        uint64_t start = arch_cycle_counter();
        while ((arch_cycle_counter() - start) < per_attempt) {
            net_wait_step();
            if (arp_lookup(ip, mac_out)) return 1;
            arch_pause();
        }
    }
    return 0;
}

/* SOMA_162_T480S: una linea para la portada (antes era un texto fijo "10.0.2.15 (VirtIO-NET NAT)") */
const char *net_describe(void)
{
    static char b[96]; uint32_t k = 0;
    const struct netif_ops *o = netif_active();
    if (!o || !netif_count()) { const char *t = "sin tarjeta de red reconocida"; while (*t) b[k++] = *t++; b[k] = 0; return b; }
    for (int i = 0; i < 4; i++) {
        uint32_t v = net_ip[i]; char d[4]; int n = 0; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) b[k++] = d[--n];
        if (i < 3) b[k++] = '.';
    }
    b[k++] = ' '; b[k++] = '(';
    const char *t = o->desc ? o->desc : o->name; while (t && *t && k < sizeof b - 2) b[k++] = *t++;
    b[k++] = ')'; b[k] = 0;
    return b;
}
