#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "io.h"
#include "ip.h"
#include "mem.h"
#include "net.h"
#include "thread.h"
#include "tcp.h"
#include "tcp_policy.h"
#include "rng.h"

uint64_t timer_get_uptime_ms(void);

#define IP_PROTO_TCP     6
#define TCP_FIN          0x01
#define TCP_SYN          0x02
#define TCP_RST          0x04
#define TCP_PSH          0x08
#define TCP_ACK          0x10

#define TCP_HDR_MIN      20
#define TCP_HDR_MAX      24
#define TCP_MSS_MAX      1460
#define TCP_MSS_DEFAULT  536
#define TCP_RX_BUFFER    32768
#define TCP_MAX_CONN     8      /* PARCHE 133: antes 4 (un solo cliente lento ocupaba la cuarta parte) */
#define TCP_RTO_MS       1000
#define TCP_MAX_RETRIES  5

extern uint64_t tsc_ticks_per_ms;
#define TSC_PER_MS       (tsc_ticks_per_ms > 0 ? tsc_ticks_per_ms : 62500ULL)

enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RCVD,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK
};
_Static_assert((int)TCP_SYN_RCVD == (int)TCPP_SYN_RCVD && (int)TCP_ESTABLISHED == (int)TCPP_ESTABLISHED && (int)TCP_FIN_WAIT_1 == (int)TCPP_FIN_WAIT_1 && (int)TCP_FIN_WAIT_2 == (int)TCPP_FIN_WAIT_2 &&
               (int)TCP_CLOSE_WAIT == (int)TCPP_CLOSE_WAIT && (int)TCP_CLOSING == (int)TCPP_CLOSING && (int)TCP_LAST_ACK == (int)TCPP_LAST_ACK && (int)TCP_LISTEN == (int)TCPP_LISTEN, "tcp_policy.h: estados desalineados");

struct tcp_conn {
    uint8_t           used;
    volatile uint8_t  state;
    volatile uint8_t  peer_fin;
    volatile uint8_t  reset;
    uint8_t           fin_sent;
    uint8_t           pad[3];
    uint32_t          owner_tid;
    uint32_t          t_ms;          /* PARCHE 133: ultima actividad (ms de actividad del sistema, con vuelta) */
    uint32_t          syn_t;         /* PARCHE 142: conexion no bloqueante: cuando se envio el ultimo SYN */
    uint8_t           syn_n;         /* PARCHE 142: SYN enviados */

    uint8_t           remote_ip[4];
    uint16_t          remote_port;
    uint16_t          local_port;
    uint16_t          mss;
    uint16_t          snd_wnd;       /* PARCHE 134: ventana que anuncia el par (sin escalado) */

    volatile uint32_t snd_una;
    uint32_t          snd_nxt;
    uint32_t          rcv_nxt;
    uint16_t          ooo_off, ooo_len; /* PARCHE 135: un unico rango fuera de orden ya guardado en el anillo (relativo a rcv_nxt) */
    uint32_t          cwnd;          /* PARCHE 134: ventana de congestion en bytes (arranque lento) */

    uint16_t          rx_head;
    volatile uint16_t rx_count;
    uint8_t           rx[TCP_RX_BUFFER];
} __attribute__((aligned(8)));

static struct tcp_conn conns[TCP_MAX_CONN] __attribute__((aligned(8)));
static uint16_t next_port = 49152;
static uint64_t isn_secret;                       /* PARCHE 133: secreto por arranque para los ISN (RFC 6528) */
static uint32_t stat_reaped, stat_syn_dropped;

static uint32_t now_ms32(void) { return (uint32_t)timer_get_uptime_ms(); }
static uint32_t make_isn(const uint8_t *rip, uint16_t rport, uint16_t lport)
{
    if (!isn_secret) {
        uint64_t v = 0;
#ifdef __x86_64__
        /* PARCHE 136: RDRAND solo si CPUID lo anuncia (sin esa comprobacion, una CPU sin RDRAND -p. ej. el QEMU por defecto- daba #UD al abrir la primera conexion) */
        uint32_t ca = 1, cb, cc = 0, cd; __asm__ volatile ("cpuid" : "+a"(ca), "=b"(cb), "+c"(cc), "=d"(cd));
        if (cc & (1u << 30))
            for (int i = 0; i < 8 && !v; ++i) { uint64_t r; unsigned char ok; __asm__ volatile ("rdrand %0; setc %1" : "=r"(r), "=qm"(ok) : : "cc"); if (ok) v = r; }
#endif
        v ^= tcpp_mix(arch_cycle_counter() ^ ((uint64_t)(uintptr_t)&isn_secret << 20) ^ timer_get_uptime_ms());
        v ^= rng_u64();                           /* SOMA_192: sin RDRAND el secreto solo dependia del contador de ciclos */
        isn_secret = v ? v : 0x5DEECE66DULL;
    }
    return tcp_isn(isn_secret, rip, rport, lport, timer_get_uptime_ms());
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static int seq_le(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static uint32_t csum_add(uint32_t sum, const uint8_t *p, uint16_t len)
{
    while (len > 1) { sum += (uint32_t)(((uint32_t)p[0] << 8) | p[1]); p += 2; len = (uint16_t)(len - 2); }
    if (len == 1) sum += (uint32_t)p[0] << 8;
    return sum;
}

static uint16_t tcp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip, const uint8_t *seg, uint16_t len)
{
    uint8_t ph[12];
    memcpy(ph, src_ip, 4); memcpy(ph + 4, dst_ip, 4);
    ph[8] = 0; ph[9] = IP_PROTO_TCP; wr16(ph + 10, len);
    uint32_t sum = csum_add(0, ph, sizeof(ph));
    sum = csum_add(sum, seg, len);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_emit_raw(const uint8_t *dst_ip, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window, int mss_opt, const uint8_t *payload, uint16_t plen)
{
    if (plen > TCP_MSS_MAX) return;
    uint8_t seg[TCP_HDR_MAX + TCP_MSS_MAX];
    uint16_t hlen = mss_opt ? TCP_HDR_MAX : TCP_HDR_MIN;
    wr16(seg, sport); wr16(seg + 2, dport); wr32(seg + 4, seq); wr32(seg + 8, ack);
    seg[12] = (uint8_t)((hlen / 4) << 4); seg[13] = flags; wr16(seg + 14, window);
    wr16(seg + 16, 0); wr16(seg + 18, 0);
    if (mss_opt) { seg[20] = 2; seg[21] = 4; wr16(seg + 22, TCP_MSS_MAX); }
    if (plen > 0) memcpy(seg + hlen, payload, plen);
    uint16_t total = (uint16_t)(hlen + plen);
    wr16(seg + 16, tcp_checksum(net_ip, dst_ip, seg, total));
    ip_send(dst_ip, IP_PROTO_TCP, seg, total);
}

static uint16_t rx_window(const struct tcp_conn *c) { return (uint16_t)(TCP_RX_BUFFER - c->rx_count); }

static void conn_emit(struct tcp_conn *c, uint8_t flags, uint32_t seq, int mss_opt, const uint8_t *payload, uint16_t plen)
{
    tcp_emit_raw(c->remote_ip, c->local_port, c->remote_port, seq, (flags & TCP_ACK) ? c->rcv_nxt : 0, flags, rx_window(c), mss_opt, payload, plen);
}

static void send_reset_unknown(const uint8_t *remote_ip, uint16_t remote_port, uint16_t local_port, uint32_t seq, uint32_t ack, uint8_t flags, uint16_t plen)
{
    if (flags & TCP_RST) return;
    if (flags & TCP_ACK) tcp_emit_raw(remote_ip, local_port, remote_port, ack, 0, TCP_RST, 0, 0, 0, 0);
    else {
        uint32_t a = seq + plen + ((flags & TCP_SYN) ? 1U : 0U) + ((flags & TCP_FIN) ? 1U : 0U);
        tcp_emit_raw(remote_ip, local_port, remote_port, 0, a, TCP_RST | TCP_ACK, 0, 0, 0, 0);
    }
}

static struct tcp_conn *get_conn(int h)
{
    if (h < 0 || h >= TCP_MAX_CONN || !conns[h].used) return 0;
    return &conns[h];
}

static struct tcp_conn *find_conn(const uint8_t *remote_ip, uint16_t remote_port, uint16_t local_port)
{
    for (int i = 0; i < TCP_MAX_CONN; ++i) {
        struct tcp_conn *c = &conns[i];
        if (c->used && c->local_port == local_port && c->remote_port == remote_port && memcmp(c->remote_ip, remote_ip, 4) == 0) return c;
    }
    return 0;
}

static uint16_t rx_store(struct tcp_conn *c, const uint8_t *p, uint16_t n)
{
    uint16_t space = rx_window(c);
    if (n > space) n = space;
    for (uint16_t i = 0; i < n; ++i) c->rx[(uint16_t)((c->rx_head + c->rx_count + i) % TCP_RX_BUFFER)] = p[i];
    c->rx_count = (uint16_t)(c->rx_count + n);
    return n;
}

static int conn_alive(const struct tcp_conn *c) { return c->state != TCP_CLOSED && c->state != TCP_SYN_SENT; }

static uint16_t parse_peer_mss(const uint8_t *tcp, uint16_t hlen)
{
    uint16_t i = TCP_HDR_MIN;
    while (i < hlen) {
        uint8_t kind = tcp[i];
        if (kind == 0) break;
        if (kind == 1) { ++i; continue; }
        if (i + 1 >= hlen) break;
        uint8_t l = tcp[i + 1];
        if (l < 2) break;
        if (kind == 2 && l == 4 && i + 4 <= hlen) return rd16(tcp + i + 2);
        i = (uint16_t)(i + l);
    }
    return 0;
}

void tcp_input(const uint8_t *src_ip, const uint8_t *dst_ip, const uint8_t *tcp, uint16_t len)
{
    if (len < TCP_HDR_MIN) return;
    uint16_t hlen = (uint16_t)((tcp[12] >> 4) * 4);
    if (hlen < TCP_HDR_MIN || hlen > len) return;
    if (tcp_checksum(src_ip, dst_ip, tcp, len) != 0) return;

    uint16_t sport = rd16(tcp);
    uint16_t dport = rd16(tcp + 2);
    uint32_t seq   = rd32(tcp + 4);
    uint32_t ack   = rd32(tcp + 8);
    uint8_t  flags = tcp[13];
    const uint8_t *payload = tcp + hlen;
    uint16_t plen = (uint16_t)(len - hlen);

    struct tcp_conn *c = find_conn(src_ip, sport, dport);
    if (!c) {
        struct tcp_conn *l = 0;
        for (int i = 0; i < TCP_MAX_CONN; ++i) {
            if (conns[i].used && conns[i].state == TCP_LISTEN && conns[i].local_port == dport) { l = &conns[i]; break; }
        }
        if (l && (flags & TCP_SYN) && !(flags & TCP_ACK)) {
            int free_idx = -1, halfopen = 0;
            for (int i = 0; i < TCP_MAX_CONN; ++i) { if (!conns[i].used) { if (free_idx < 0) free_idx = i; } else if (conns[i].state == TCP_SYN_RCVD) halfopen++; }
            if ((unsigned)halfopen >= TCPP_HALFOPEN_MAX) { stat_syn_dropped++; return; }     /* PARCHE 133: limite de semiabiertas; el cliente reintenta el SYN */
            if (free_idx >= 0) {
                struct tcp_conn *nc = &conns[free_idx];
                memset(nc, 0, sizeof(*nc));
                nc->used = 1;
                memcpy(nc->remote_ip, src_ip, 4);
                nc->remote_port = sport;
                nc->local_port = dport;
                nc->owner_tid = l->owner_tid;
                uint16_t peer_mss = parse_peer_mss(tcp, hlen);
                nc->mss = (peer_mss > 0 && peer_mss < TCP_MSS_MAX) ? peer_mss : TCP_MSS_DEFAULT;
                nc->rcv_nxt = seq + 1;
                nc->snd_wnd = rd16(tcp + 14);
                uint32_t iss = make_isn(src_ip, sport, dport);                 /* PARCHE 133: imprevisible */
                nc->t_ms = now_ms32();
                nc->snd_una = iss;
                nc->snd_nxt = iss + 1;
                nc->state = TCP_SYN_RCVD;
                conn_emit(nc, TCP_SYN | TCP_ACK, iss, 1, 0, 0);
                return;
            }
        }
        send_reset_unknown(src_ip, sport, dport, seq, ack, flags, plen);
        return;
    }
    if (c->state == TCP_CLOSED) return;
    c->t_ms = now_ms32();                                    /* PARCHE 133: cualquier segmento de la conexion cuenta como actividad */

    if (c->state == TCP_SYN_SENT) {
        if (flags & TCP_ACK) {
            if (ack != c->snd_nxt) {
                if (!(flags & TCP_RST)) tcp_emit_raw(src_ip, dport, sport, ack, 0, TCP_RST, 0, 0, 0, 0);
                return;
            }
        }
        if (flags & TCP_RST) { if (flags & TCP_ACK) { c->reset = 1; c->state = TCP_CLOSED; } return; }
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            uint16_t peer_mss = parse_peer_mss(tcp, hlen);
            c->mss = (peer_mss > 0 && peer_mss < TCP_MSS_MAX) ? peer_mss : TCP_MSS_DEFAULT;
            c->rcv_nxt = seq + 1; c->snd_una = ack; c->snd_wnd = rd16(tcp + 14); c->state = TCP_ESTABLISHED;
            conn_emit(c, TCP_ACK, c->snd_nxt, 0, 0, 0);
        }
        return;
    }

    if (flags & TCP_RST) {
        if (seq_le(c->rcv_nxt, seq) && seq_lt(seq, c->rcv_nxt + TCP_RX_BUFFER)) { c->reset = 1; c->state = TCP_CLOSED; }
        return;
    }
    if (flags & TCP_SYN) { conn_emit(c, TCP_ACK, c->snd_nxt, 0, 0, 0); return; }
    if (!(flags & TCP_ACK)) return;

    if (c->state == TCP_SYN_RCVD) { if (ack != c->snd_nxt) return; c->snd_una = ack; c->state = TCP_ESTABLISHED; }
    if (seq_lt(c->snd_una, ack) && !seq_lt(c->snd_nxt, ack)) c->snd_una = ack;
    if (!seq_lt(ack, c->snd_una) && !seq_lt(c->snd_nxt, ack)) c->snd_wnd = rd16(tcp + 14);     /* PARCHE 134: la ventana del par solo se acepta de un ACK valido */

    if (c->fin_sent && c->snd_una == c->snd_nxt) {
        if (c->state == TCP_FIN_WAIT_1) c->state = TCP_FIN_WAIT_2;
        else if (c->state == TCP_CLOSING || c->state == TCP_LAST_ACK) { c->state = TCP_CLOSED; return; }
    }

    if (plen == 0 && !(flags & TCP_FIN)) return;

    int can_rx = (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2);
    if (can_rx) {
        /* PARCHE 135: recorte del tramo ya recibido, un rango fuera de orden y union al llegar el hueco */
        const uint8_t *pp = payload; uint16_t pl = plen; int fin = (flags & TCP_FIN) != 0;
        if (seq_lt(seq, c->rcv_nxt)) {
            uint32_t skip = c->rcv_nxt - seq;
            if (skip > (uint32_t)pl) { fin = 0; pl = 0; }
            else { pp += skip; pl = (uint16_t)(pl - skip); }
            seq = c->rcv_nxt;
        }
        if (seq == c->rcv_nxt) {
            uint16_t n = rx_store(c, pp, pl);
            c->rcv_nxt += n;
            if (c->ooo_len) {
                if (n >= c->ooo_off) {
                    uint32_t end = (uint32_t)c->ooo_off + c->ooo_len;
                    if (end > n) { uint16_t extra = (uint16_t)(end - n); c->rx_count = (uint16_t)(c->rx_count + extra); c->rcv_nxt += extra; }
                    c->ooo_off = c->ooo_len = 0;
                    n = pl;                                    /* lo guardado fuera de orden ya esta; el FIN solo cuenta si no sobra nada */
                    fin = 0;                                   /* con datos posteriores el FIN no puede ser este: se espera su retransmision */
                } else { c->ooo_off = (uint16_t)(c->ooo_off - n); fin = 0; }
            }
            if (n == pl && fin) {
                c->rcv_nxt += 1; c->peer_fin = 1;
                if (c->state == TCP_ESTABLISHED) c->state = TCP_CLOSE_WAIT;
                else if (c->state == TCP_FIN_WAIT_1) c->state = TCP_CLOSING;
                else if (c->state == TCP_FIN_WAIT_2) c->state = TCP_CLOSED;
            }
        } else if (pl > 0) {
            uint32_t rel = seq - c->rcv_nxt, win = rx_window(c);
            if (rel < win) {
                uint32_t len = pl; if (len > win - rel) len = win - rel;
                int ok = 1;
                if (c->ooo_len) {
                    uint32_t o0 = c->ooo_off, o1 = o0 + c->ooo_len;
                    if (rel > o1 || rel + len < o0) ok = 0;                         /* hueco con el rango ya guardado: se descarta (solo un rango) */
                    else { uint32_t n0 = rel < o0 ? rel : o0, n1 = rel + len > o1 ? rel + len : o1; c->ooo_off = (uint16_t)n0; c->ooo_len = (uint16_t)(n1 - n0); }
                } else { c->ooo_off = (uint16_t)rel; c->ooo_len = (uint16_t)len; }
                if (ok) for (uint32_t i = 0; i < len; ++i) c->rx[(uint16_t)((c->rx_head + c->rx_count + rel + i) % TCP_RX_BUFFER)] = pp[i];
            }
        }
    }
    conn_emit(c, TCP_ACK, c->snd_nxt, 0, 0, 0);
}

/* PARCHE 142: conexion NO bloqueante (la usan los procesos de ring 3 por syscall: el navegador aislado no se congela mientras conecta).
 * tcp_connect_start envia el SYN y devuelve el descriptor; tcp_connect_poll reenvia el SYN cada TCP_SYN_RETRY_MS (3 intentos) y devuelve
 * 1 establecida, 0 pendiente, -1 fallida (y la libera). */
#define TCP_SYN_RETRY_MS 2000u
int tcp_connect_start(const uint8_t *dst_ip, uint16_t dst_port)
{
    int h = -1;
    for (int i = 0; i < TCP_MAX_CONN; ++i) if (!conns[i].used) { h = i; break; }
    if (h < 0) return -1;
    struct tcp_conn *c = &conns[h];
    memset(c, 0, sizeof(*c));
    c->used = 1; memcpy(c->remote_ip, dst_ip, 4); c->remote_port = dst_port;
    c->local_port = next_port++;
    if (next_port >= 65000) next_port = 49152;
    uint32_t iss = make_isn(dst_ip, dst_port, c->local_port);
    c->t_ms = now_ms32();
    c->mss = TCP_MSS_DEFAULT; c->snd_una = iss; c->snd_nxt = iss + 1; c->state = TCP_SYN_SENT;
    struct tcb *cur_t = thread_current();
    c->owner_tid = cur_t ? cur_t->tid : 0;
    conn_emit(c, TCP_SYN, iss, 1, 0, 0);
    c->syn_t = now_ms32(); c->syn_n = 1;
    return h;
}
int tcp_connect_poll(int h)
{
    if (h < 0 || h >= TCP_MAX_CONN || !conns[h].used) return -1;
    struct tcp_conn *c = &conns[h];
    net_wait_step();
    if (c->state == TCP_ESTABLISHED) return 1;
    if (c->reset || c->state == TCP_CLOSED) { c->used = 0; c->owner_tid = 0; return -1; }
    if (c->state != TCP_SYN_SENT) return -1;
    if (now_ms32() - c->syn_t >= TCP_SYN_RETRY_MS) {
        if (c->syn_n >= 3) { c->state = TCP_CLOSED; c->used = 0; c->owner_tid = 0; return -1; }
        conn_emit(c, TCP_SYN, c->snd_una, 1, 0, 0);
        c->syn_t = now_ms32(); c->syn_n++;
    }
    return 0;
}
uint32_t tcp_owner(int h) { return (h >= 0 && h < TCP_MAX_CONN && conns[h].used) ? conns[h].owner_tid : 0; }
int tcp_count_owned(uint32_t tid) { int n = 0; for (int i = 0; i < TCP_MAX_CONN; ++i) if (conns[i].used && conns[i].owner_tid == tid) n++; return n; }

int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port, uint32_t timeout_ms)
{
    int h = -1;
    for (int i = 0; i < TCP_MAX_CONN; ++i) if (!conns[i].used) { h = i; break; }
    if (h < 0) return -1;

    struct tcp_conn *c = &conns[h];
    memset(c, 0, sizeof(*c));
    c->used = 1; memcpy(c->remote_ip, dst_ip, 4); c->remote_port = dst_port;
    c->local_port = next_port++;
    if (next_port >= 65000) next_port = 49152;
    uint32_t iss = make_isn(dst_ip, dst_port, c->local_port);       /* PARCHE 133 */
    c->t_ms = now_ms32();
    c->mss = TCP_MSS_DEFAULT; c->snd_una = iss; c->snd_nxt = iss + 1; c->state = TCP_SYN_SENT;
    struct tcb *cur_t = thread_current();
    c->owner_tid = cur_t ? cur_t->tid : 0;

    uint32_t per_attempt = timeout_ms / 3;
    if (per_attempt == 0) per_attempt = 1;

    for (int attempt = 0; attempt < 3; ++attempt) {
        conn_emit(c, TCP_SYN, iss, 1, 0, 0);
        uint64_t start = arch_cycle_counter();
        uint64_t limit = (uint64_t)per_attempt * TSC_PER_MS;
        while ((arch_cycle_counter() - start) < limit) {
            if (c->state == TCP_ESTABLISHED) return h;
            if (c->reset || c->state == TCP_CLOSED) { c->used = 0; return -1; }
            net_wait_step();
            arch_pause();
        }
    }
    c->used = 0;
    return -1;
}

/* PARCHE 134: envio con ventana. Antes cada segmento esperaba su ACK (parada y espera: ~1 segmento por RTT, ignorando la ventana del par).
 * Ahora se mantienen en vuelo hasta min(ventana del par, ventana de congestion, TCP_SEND_CAP) bytes; ante un tiempo agotado se retransmite desde el
 * primer byte sin confirmar (retroceder N) y la ventana de congestion vuelve a un segmento. Con ventana cero se envia un sondeo de 1 byte cada RTO. */
#define TCP_SEND_CAP   (16u * 1024u)
static uint32_t tcp_flight_limit(const struct tcp_conn *c)
{
    uint32_t w = c->snd_wnd, cw = c->cwnd ? c->cwnd : 2u * c->mss;
    if (cw < w) w = cw;
    if (w > TCP_SEND_CAP) w = TCP_SEND_CAP;
    return w;
}

int tcp_send(int h, const void *data, uint32_t len)
{
    struct tcp_conn *c = get_conn(h);
    if (!c || (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT)) return -1;
    if (len == 0) return 0;

    const uint8_t *p = (const uint8_t *)data;
    const uint32_t base = c->snd_nxt;            /* numero de secuencia del primer byte */
    uint32_t sent = 0;                           /* bytes ya emitidos (relativo a base) */
    uint32_t last_una = c->snd_una;
    int retries = 0;
    uint64_t t_prog = arch_cycle_counter();
    const uint64_t rto = (uint64_t)TCP_RTO_MS * TSC_PER_MS;
    if (c->cwnd == 0) c->cwnd = 2u * c->mss;

    for (;;) {
        if (c->reset || c->state == TCP_CLOSED) return -1;
        uint32_t acked = c->snd_una - base;
        if (acked > len) acked = len;
        if (acked >= len) return (int)len;
        if (c->snd_una != last_una) {            /* hubo progreso: arranque lento y se reinicia el temporizador */
            uint32_t d = c->snd_una - last_una;
            if (d < 0x10000000u) { c->cwnd += d; if (c->cwnd > TCP_SEND_CAP) c->cwnd = TCP_SEND_CAP; }
            last_una = c->snd_una; retries = 0; t_prog = arch_cycle_counter();
        }
        uint32_t lim = tcp_flight_limit(c);
        while (sent < len && sent - acked < lim) {
            uint32_t chunk = len - sent;
            if (chunk > c->mss) chunk = c->mss;
            if (chunk > lim - (sent - acked)) chunk = lim - (sent - acked);
            if (chunk == 0) break;
            uint32_t seq = base + sent;
            if (seq_lt(c->snd_nxt, seq + chunk)) c->snd_nxt = seq + chunk;
            conn_emit(c, TCP_PSH | TCP_ACK, seq, 0, p + sent, (uint16_t)chunk);
            sent += chunk;
        }
        if (arch_cycle_counter() - t_prog >= rto) {
            if (++retries > TCP_MAX_RETRIES) return -1;
            c->cwnd = c->mss;                                  /* perdida: vuelta al arranque lento */
            if (sent - acked == 0 || c->snd_wnd == 0) {         /* ventana cero: sondeo de 1 byte (no cuenta como enviado) */
                uint32_t seq = base + acked;
                if (seq_lt(c->snd_nxt, seq + 1)) c->snd_nxt = seq + 1;
                conn_emit(c, TCP_PSH | TCP_ACK, seq, 0, p + acked, 1);
            } else sent = acked;                                /* retroceder N: se reenvia desde el primer byte sin confirmar */
            t_prog = arch_cycle_counter();
        }
        net_wait_step();
        arch_pause();
    }
}

int tcp_recv(int h, void *buf, uint16_t max, uint32_t timeout_ms)
{
    struct tcp_conn *c = get_conn(h);
    if (!c) return TCP_ERR;
    if (max == 0) return 0;
    uint8_t *out = (uint8_t *)buf;
    uint64_t start = arch_cycle_counter();
    uint64_t limit = (uint64_t)timeout_ms * TSC_PER_MS;

    for (;;) {
        if (c->rx_count > 0) {
            uint16_t free_before = rx_window(c);
            uint16_t n = c->rx_count < max ? c->rx_count : max;
            for (uint16_t i = 0; i < n; ++i) out[i] = c->rx[(uint16_t)((c->rx_head + i) % TCP_RX_BUFFER)];
            c->rx_head = (uint16_t)((c->rx_head + n) % TCP_RX_BUFFER);
            c->rx_count = (uint16_t)(c->rx_count - n);
            if (free_before < TCP_MSS_MAX && conn_alive(c)) conn_emit(c, TCP_ACK, c->snd_nxt, 0, 0, 0);
            return n;
        }
        if (c->reset) return TCP_ERR;
        if (c->peer_fin || c->state == TCP_CLOSED) return TCP_EOF;
        if ((arch_cycle_counter() - start) >= limit) return 0;
        net_wait_step();
        arch_pause();
    }
}

int tcp_close(int h)
{
    struct tcp_conn *c = get_conn(h);
    if (!c) return -1;
    if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) {
        uint32_t fin_seq = c->snd_nxt;
        conn_emit(c, TCP_FIN | TCP_ACK, fin_seq, 0, 0, 0);
        c->snd_nxt = fin_seq + 1; c->fin_sent = 1;
        c->state = (c->state == TCP_ESTABLISHED) ? TCP_FIN_WAIT_1 : TCP_LAST_ACK;
        for (int round = 0; round < 3; ++round) {
            uint64_t start = arch_cycle_counter();
            uint64_t limit = (uint64_t)TCP_RTO_MS * TSC_PER_MS;
            while ((arch_cycle_counter() - start) < limit) {
                if (c->state == TCP_CLOSED) break;
                net_wait_step();
                arch_pause();
            }
            if (c->state == TCP_CLOSED) break;
            conn_emit(c, TCP_FIN | TCP_ACK, fin_seq, 0, 0, 0);
        }
        if (c->state != TCP_CLOSED) conn_emit(c, TCP_RST | TCP_ACK, c->snd_nxt, 0, 0, 0);
    }
    c->state = TCP_CLOSED; c->used = 0; c->owner_tid = 0;
    return 0;
}

/* SOMA_250_YOUTUBE: cierre inmediato. tcp_close espera el cierre ordenado del otro extremo (hasta 3 s si este sigue enviando,
 * como al cancelar una descarga a medias); aqui se corta con RST y la conexion queda libre al momento. */
int tcp_abort(int h)
{
    struct tcp_conn *c = get_conn(h);
    if (!c) return -1;
    if (c->state != TCP_CLOSED && c->state != TCP_LISTEN) conn_emit(c, TCP_RST | TCP_ACK, c->snd_nxt, 0, 0, 0);
    c->state = TCP_CLOSED; c->used = 0; c->owner_tid = 0;
    return 0;
}

void tcp_cleanup_for_thread(uint32_t tid)
{
    for (int i = 0; i < TCP_MAX_CONN; ++i) {
        struct tcp_conn *c = &conns[i];
        if (c->used && c->owner_tid == tid) {
            if (c->state != TCP_CLOSED) conn_emit(c, TCP_RST | TCP_ACK, c->snd_nxt, 0, 0, 0);
            c->state = TCP_CLOSED; c->used = 0; c->owner_tid = 0;
        }
    }
}

int tcp_listen(uint16_t port)
{
    for (int i = 0; i < TCP_MAX_CONN; ++i) {
        if (conns[i].used && conns[i].state == TCP_LISTEN && conns[i].local_port == port) return -1;
    }
    int h = -1;
    for (int i = 0; i < TCP_MAX_CONN; ++i) if (!conns[i].used) { h = i; break; }
    if (h < 0) return -1;
    struct tcp_conn *c = &conns[h];
    memset(c, 0, sizeof(*c));
    c->used = 1; c->state = TCP_LISTEN; c->local_port = port;
    struct tcb *cur = thread_current();
    c->owner_tid = cur ? cur->tid : 0;
    return h;
}

int tcp_accept(int listen_h, uint32_t timeout_ms)
{
    struct tcp_conn *l = get_conn(listen_h);
    if (!l || l->state != TCP_LISTEN) return -1;
    uint64_t start = arch_cycle_counter();
    uint64_t limit = (uint64_t)timeout_ms * TSC_PER_MS;
    while ((arch_cycle_counter() - start) < limit) {
        net_wait_step();
        for (int i = 0; i < TCP_MAX_CONN; ++i) {
            struct tcp_conn *c = &conns[i];
            if (c->used && i != listen_h && c->local_port == l->local_port &&
                (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT)) return i;
        }
        arch_pause();
    }
    return -1;
}

int tcp_peer_ip(int h, uint8_t ip[4])                              /* SOMA_197 */
{
    struct tcp_conn *c = get_conn(h);
    if (!c) return -1;
    for (int i = 0; i < 4; i++) ip[i] = c->remote_ip[i];
    return 0;
}

/* PARCHE 133: descarta conexiones atascadas (semiabiertas sin ACK final, cierres que no avanzan, cierre del par sin respuesta de la aplicacion). */
int tcp_reap(void)
{
    int n = 0; uint32_t now = now_ms32();
    for (int i = 0; i < TCP_MAX_CONN; ++i) {
        struct tcp_conn *c = &conns[i];
        if (!c->used || !tcp_should_reap(c->state, now, c->t_ms)) continue;
        if (c->state != TCP_SYN_RCVD) conn_emit(c, TCP_RST | TCP_ACK, c->snd_nxt, 0, 0, 0);
        c->state = TCP_CLOSED; c->used = 0; c->owner_tid = 0;
        stat_reaped++; n++;
    }
    return n;
}

/* se llama con frecuencia desde la espera de red; solo trabaja cada 500 ms */
void tcp_reap_tick(void)
{
    static uint32_t last;
    uint32_t now = now_ms32();
    if ((uint32_t)(now - last) < 500) return;
    last = now;
    tcp_reap();
}

void tcp_get_stats(uint32_t *reaped, uint32_t *syn_dropped, int *halfopen, int *in_use)
{
    int h = 0, u = 0;
    for (int i = 0; i < TCP_MAX_CONN; ++i) if (conns[i].used) { u++; if (conns[i].state == TCP_SYN_RCVD) h++; }
    if (reaped) *reaped = stat_reaped;
    if (syn_dropped) *syn_dropped = stat_syn_dropped;
    if (halfopen) *halfopen = h;
    if (in_use) *in_use = u;
}
