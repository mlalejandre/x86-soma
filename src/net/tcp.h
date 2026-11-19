#ifndef MYOS_TCP_H
#define MYOS_TCP_H

#include <stdint.h>

#define TCP_EOF   (-1)
#define TCP_ERR   (-2)

int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port, uint32_t timeout_ms);
int tcp_send(int h, const void *data, uint32_t len);
int tcp_recv(int h, void *buf, uint16_t max, uint32_t timeout_ms);
int tcp_close(int h);
/* PARCHE 142: conexion no bloqueante y propiedad (syscalls de red de los procesos de ring 3) */
int tcp_connect_start(const uint8_t *dst_ip, uint16_t dst_port);   /* descriptor o -1 */
int tcp_connect_poll(int h);                                       /* 1 establecida, 0 pendiente, -1 fallida (liberada) */
uint32_t tcp_owner(int h);                                         /* tid dueno (0 = no existe) */
int tcp_count_owned(uint32_t tid);
int tcp_listen(uint16_t port);
int tcp_accept(int listen_h, uint32_t timeout_ms);
int tcp_peer_ip(int h, uint8_t ip[4]);                             /* SOMA_197: direccion del otro extremo; 0 = bien */
void tcp_cleanup_for_thread(uint32_t tid);
int  tcp_reap(void);                 /* PARCHE 133: descarta conexiones atascadas; devuelve cuantas */
void tcp_reap_tick(void);            /* idem, como mucho cada 500 ms */
void tcp_get_stats(uint32_t *reaped, uint32_t *syn_dropped, int *halfopen, int *in_use);

void tcp_input(
    const uint8_t *src_ip,
    const uint8_t *dst_ip,
    const uint8_t *tcp,
    uint16_t len
);

void net_run_tcp_test(void);

#endif
