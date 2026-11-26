#ifndef SOMA_DHCP_PROTO_H
#define SOMA_DHCP_PROTO_H
/* PARCHE 081: cliente DHCP (RFC 2131/2132), parte pura: construir DISCOVER/REQUEST y leer OFFER/ACK/NAK. Necesario para el Wi-Fi de casa
 * (y para cualquier red real): la direccion, la mascara, la puerta de enlace y el DNS los da el router. Pruebas en tests/test_brcm.c. */
#include <stdint.h>
#include <stddef.h>
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define DHCP_NAK      6
#define DHCP_MAX      576
struct dhcp_lease { uint8_t ip[4], mask[4], router[4], dns[4], server[4]; uint32_t lease_s; };
/* req_ip/server pueden ser NULL (DISCOVER). Devuelve los bytes del mensaje (300). */
size_t dhcp_build(uint8_t *o, int type, uint32_t xid, const uint8_t mac[6], const uint8_t *req_ip, const uint8_t *server, const char *hostname);
/* Devuelve el tipo de mensaje (2, 5, 6) si es una respuesta para (xid, mac); 0 si no es para nosotros; <0 mal formado. */
int dhcp_mask_ok(const uint8_t m[4]);                              /* SOMA_340_RED_HOSTIL: /8../32 y contigua */
int dhcp_parse(const uint8_t *b, size_t len, uint32_t xid, const uint8_t mac[6], struct dhcp_lease *l);
#endif
