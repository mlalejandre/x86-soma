#ifndef SOMA_TCP_POLICY_H
#define SOMA_TCP_POLICY_H
/* PARCHE 133: politica pura de la pila TCP (se prueba en el host): caducidad de conexiones atascadas y numero de secuencia inicial (ISN) imprevisible. */
#include <stdint.h>

/* Estados de tcp.c (mismo orden que el enum de alli; tcp.c lo comprueba con _Static_assert). */
enum { TCPP_CLOSED = 0, TCPP_LISTEN, TCPP_SYN_SENT, TCPP_SYN_RCVD, TCPP_ESTABLISHED, TCPP_FIN_WAIT_1, TCPP_FIN_WAIT_2, TCPP_CLOSE_WAIT, TCPP_CLOSING, TCPP_LAST_ACK };

#define TCPP_HALFOPEN_MAX   3u       /* conexiones simultaneas en SYN_RCVD: el resto de ranuras queda para conexiones ya establecidas */
#define TCPP_SYN_RCVD_MS    5000u    /* un SYN al que nadie responde con el ACK final */
#define TCPP_CLOSING_MS     30000u   /* FIN_WAIT_1/2, CLOSING, LAST_ACK: el cierre no avanza */
#define TCPP_CLOSE_WAIT_MS  120000u  /* el par cerro y la aplicacion no ha cerrado */

/* milisegundos sin actividad tras los que una conexion en `state` se descarta (0 = nunca: LISTEN, SYN_SENT lo gestiona su propio tiempo limite,
 * ESTABLISHED es una conexion legitima inactiva y no se toca). */
static inline uint32_t tcp_reap_after_ms(unsigned state)
{
    switch (state) {
    case TCPP_SYN_RCVD:   return TCPP_SYN_RCVD_MS;
    case TCPP_FIN_WAIT_1: case TCPP_FIN_WAIT_2: case TCPP_CLOSING: case TCPP_LAST_ACK: return TCPP_CLOSING_MS;
    case TCPP_CLOSE_WAIT: return TCPP_CLOSE_WAIT_MS;
    default: return 0;
    }
}
static inline int tcp_should_reap(unsigned state, uint32_t now_ms, uint32_t last_ms)
{
    uint32_t lim = tcp_reap_after_ms(state);
    return lim != 0 && (uint32_t)(now_ms - last_ms) >= lim;
}

/* RFC 6528: ISN = reloj de 250 kHz + F(secreto, 4-tupla). Distintas conexiones o secretos dan ISN no relacionados; la misma 4-tupla avanza con el tiempo. */
static inline uint64_t tcpp_mix(uint64_t x) { x += 0x9E3779B97F4A7C15ULL; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL; x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL; return x ^ (x >> 31); }
static inline uint32_t tcp_isn(uint64_t secret, const uint8_t *rip, uint16_t rport, uint16_t lport, uint64_t now_ms)
{
    uint64_t ipv = ((uint64_t)rip[0] << 24) | ((uint64_t)rip[1] << 16) | ((uint64_t)rip[2] << 8) | rip[3];
    uint64_t h = tcpp_mix(secret ^ tcpp_mix((ipv << 32) | ((uint64_t)rport << 16) | lport));
    return (uint32_t)(h >> 32) + (uint32_t)(now_ms * 250u);
}
#endif
