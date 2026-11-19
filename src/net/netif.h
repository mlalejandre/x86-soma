#ifndef SOMA_NETIF_H
#define SOMA_NETIF_H
/* PARCHE 080: capa de interfaces de red. La pila IP (net.c) sigue llamando a virtio_net_*; esas funciones reenvian al dispositivo activo:
 * virtio-net en QEMU, la Ethernet del RP1 (eth0) o el Wi-Fi (wlan0) en la Pi 5. Tramas Ethernet completas en los dos sentidos
 * (el firmware del chip Wi-Fi tambien entrega y recibe tramas Ethernet). */
#include <stdint.h>

struct netif_ops {
    const char *name, *desc;
    int  (*init)(void);                                           /* 1 = operativa */
    int  (*send)(const void *frame, uint16_t len);                /* 0 = bien */
    int  (*poll)(void *out, uint16_t max, uint16_t *len_out);     /* 1 = trama recibida */
    const uint8_t *(*mac)(void);
    int  (*link)(void);                                           /* 1 = enlace arriba */
    void (*stats)(uint32_t *tx, uint32_t *rx, uint64_t *txb, uint64_t *rxb);
};
#define NETIF_MAX 4
int  netif_register(const struct netif_ops *o);                   /* por orden de preferencia; 0 = bien */
int  netif_start(int i);                                          /* SOMA_170: arranca la i-esima sin cambiar la activa; 1 = operativa */
void netif_retry(int i);                                         /* SOMA_171: permite volver a intentar arrancarla */
void netif_mark_down(int i);                                     /* SOMA_171: el dispositivo desaparecio (USB desenchufado) */
int  netif_start_first(void);                                     /* arranca el primero que funcione; 1 = hay interfaz activa */
int  netif_use(const char *name);                                 /* 1 = cambiada; 0 = no existe o no arranca (se queda la anterior) */
const struct netif_ops *netif_active(void);
int  netif_count(void);
const struct netif_ops *netif_get(int i);
int  netif_is_up(int i);                                          /* arrancada alguna vez con exito */
int  netif_send(const void *frame, uint16_t len);
int  netif_poll(void *out, uint16_t max, uint16_t *len_out);
const uint8_t *netif_mac(void);
void netif_stats(uint32_t *tx, uint32_t *rx, uint64_t *txb, uint64_t *rxb);
#if defined(PLATFORM_rpi5)
extern const struct netif_ops netif_wlan0;                        /* src/arch/aarch64/wifi.c */
#endif
#endif
