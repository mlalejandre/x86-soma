#ifndef SOMA_USBNET_PROTO_H
#define SOMA_USBNET_PROTO_H
/* SOMA_171_T480S: red por USB (el movil compartiendo su conexion, o un adaptador USB-Ethernet estandar). Logica pura, sin
 * hardware (tests/test_usbnet_171.c):
 *   - reconocer la funcion de red en un descriptor de configuracion: RNDIS (Android, Windows), CDC-ECM (adaptadores, algunos
 *     moviles) y CDC-NCM (Android moderno, Pixel);
 *   - mensajes de control y de datos de RNDIS;
 *   - bloques NTB16 de NCM (varias tramas por transferencia).
 * Referencias: Linux drivers/net/usb/rndis_host.c, cdc_ether.c, cdc_ncm.c; especificaciones USB CDC 1.2, ECM 1.2, NCM 1.0. */
#include <stdint.h>
#include <stddef.h>

enum { USBNET_NONE = 0, USBNET_RNDIS, USBNET_ECM, USBNET_NCM };

struct usbnet_fn {
    int kind;
    uint8_t comm_if, data_if, data_alt;          /* interfaz de control, de datos y su ajuste alternativo con los extremos */
    uint8_t in_addr, out_addr;                   /* extremos bulk */
    uint16_t in_mps, out_mps;
    uint8_t mac_str;                             /* ECM/NCM: indice de la cadena con la MAC (descriptor funcional Ethernet) */
    uint16_t max_seg;                            /* ECM/NCM: wMaxSegmentSize */
};
/* Busca la funcion de red en un descriptor de configuracion completo. 1 = encontrada */
int usbnet_find(const uint8_t *cfg, uint32_t len, struct usbnet_fn *o);
const char *usbnet_kind_str(int kind);
/* MAC escrita como cadena USB (UTF-16LE, 12 cifras hexadecimales). 0 = bien */
int usbnet_mac_from_string(const uint8_t *desc, uint32_t len, uint8_t mac[6]);

/* ---- RNDIS ---- */
#define RNDIS_MSG_PACKET     0x00000001u
#define RNDIS_MSG_INIT       0x00000002u
#define RNDIS_MSG_QUERY      0x00000004u
#define RNDIS_MSG_SET        0x00000005u
#define RNDIS_MSG_INDICATE   0x00000007u
#define RNDIS_MSG_KEEPALIVE  0x00000008u
#define RNDIS_CMPLT          0x80000000u
#define RNDIS_OID_PERM_ADDR  0x01010101u
#define RNDIS_OID_FILTER     0x0001010Eu
#define RNDIS_OID_MEDIA_STATUS 0x00010114u
#define RNDIS_PKT_HDR        44u
uint32_t rndis_build_init(uint8_t *b, uint32_t rid, uint32_t max_transfer);
uint32_t rndis_build_query(uint8_t *b, uint32_t rid, uint32_t oid, uint32_t in_len);
uint32_t rndis_build_set32(uint8_t *b, uint32_t rid, uint32_t oid, uint32_t value);
uint32_t rndis_build_keepalive_cmplt(uint8_t *b, uint32_t rid);
/* Respuesta de control: comprueba tipo (msg | CMPLT), id y estado. Para QUERY deja el bufer de informacion. 0 = bien */
int rndis_parse_cmplt(const uint8_t *b, uint32_t len, uint32_t want_type, uint32_t rid, const uint8_t **info, uint32_t *info_len);
/* INIT_CMPLT: tamano maximo de transferencia que acepta el dispositivo y alineacion de paquetes */
int rndis_parse_init_cmplt(const uint8_t *b, uint32_t len, uint32_t rid, uint32_t *max_xfer, uint32_t *align_exp);
uint32_t rndis_wrap(uint8_t *out, const uint8_t *frame, uint32_t flen);      /* PACKET_MSG + trama */
/* Recorre los PACKET_MSG de una transferencia; cb(trama, longitud). Devuelve cuantas tramas validas */
int rndis_unwrap(const uint8_t *b, uint32_t len, void (*cb)(const uint8_t *f, uint32_t n, void *u), void *u);

/* ---- NCM (NTB16) ---- */
struct ncm_params { uint32_t in_max, out_max; uint16_t out_div, out_rem, out_align, out_maxdg; };
int ncm_parse_params(const uint8_t *b, uint32_t len, struct ncm_params *p);   /* GET_NTB_PARAMETERS (28 bytes) */
/* Un NTB16 con una sola trama. 0 si no cabe en out_max */
uint32_t ncm_build_ntb(uint8_t *out, uint32_t out_cap, const struct ncm_params *p, uint16_t seq, const uint8_t *frame, uint32_t flen);
int ncm_parse_ntb(const uint8_t *b, uint32_t len, void (*cb)(const uint8_t *f, uint32_t n, void *u), void *u);
#endif
