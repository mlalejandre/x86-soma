#ifndef SOMA_XHCI_PROTO_H
#define SOMA_XHCI_PROTO_H
/* PARCHE 046: logica pura del protocolo xHCI (anillos de TRB con bit de ciclo y enlaces, anillo de eventos, TRBs de
 * transferencia de control, contextos de ranura/EP0). No toca hardware: se prueba en el host (tests/test_xhci.c). */
#include <stdint.h>

struct xhci_trb { uint32_t d0, d1, d2, d3; };                      /* 16 bytes */

struct xhci_ring {                                                  /* anillo de productor (comandos / transferencias) */
    struct xhci_trb *trb; uint32_t size;                            /* size incluye el TRB de enlace final */
    uint32_t enq; uint8_t cycle;
};
struct xhci_evring {                                                /* anillo de eventos (consumidor) */
    struct xhci_trb *trb; uint32_t size, deq; uint8_t cycle;
};

#define TRB_NORMAL   1
#define TRB_SETUP    2
#define TRB_DATA     3
#define TRB_STATUS   4
#define TRB_ISOCH    5
#define TRB_LINK     6
#define TRB_ENABLE_SLOT 9
#define TRB_ADDRESS_DEV 11
#define TRB_STOP_EP  15
#define TRB_EV_TRANSFER 32
#define TRB_EV_CMD_DONE 33
#define TRB_EV_PORT_CHG 34
#define TRB_TYPE(t) ((uint32_t)(t) << 10)
#define TRB_GET_TYPE(d3) (((d3) >> 10) & 0x3F)

void     xhci_ring_init(struct xhci_ring *r, struct xhci_trb *mem, uint32_t size, uint64_t self_phys);
/* Encola un TRB (d3 sin bit de ciclo); devuelve la direccion fisica del TRB colocado (para comparar con eventos). */
uint64_t xhci_ring_push(struct xhci_ring *r, uint64_t base_phys, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3);
void     xhci_ev_init(struct xhci_evring *e, struct xhci_trb *mem, uint32_t size);
int      xhci_ev_pop(struct xhci_evring *e, struct xhci_trb *out);  /* 1 si habia un evento */
uint64_t xhci_ev_deq_phys(const struct xhci_evring *e, uint64_t base_phys);

/* Etapas de una transferencia de control de 8 bytes de configuracion (IN con datos de len bytes): setup + datos + estado. */
void     xhci_ctrl_in(struct xhci_ring *r, uint64_t base_phys, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, uint64_t buf_phys, uint64_t *last_trb);

/* Contexto de entrada para Address Device. csz64 = 1 si los contextos son de 64 bytes. ctx: area de 33 contextos puesta a 0. */
void     xhci_build_addr_input(uint8_t *ctx, int csz64, uint8_t speed, uint8_t root_port, uint16_t max_packet, uint64_t ep0_ring_phys);
uint16_t xhci_default_mps(uint8_t speed);                           /* velocidad PORTSC: 1 FS, 2 LS, 3 HS, 4 SS */
const char *xhci_speed_str(uint8_t speed);

/* PARCHE 047: transferencia de control SIN datos (SET_CONFIGURATION, SET_IDLE...): setup (TRT = 0) + estado IN con IOC. */
void     xhci_ctrl_nodata(struct xhci_ring *r, uint64_t base_phys, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint64_t *last_trb);
/* Intervalo de sondeo del contexto de extremo (potencia de 2 de microtramas de 125 us) a partir de bInterval y la velocidad. */
uint8_t  xhci_ep_interval(uint8_t speed, uint8_t binterval);
/* Contexto de entrada de Configure Endpoint para UN extremo de interrupcion IN (dci = 2*numero+1). out_ctx = contexto de salida del
 * dispositivo (de el se copia el de ranura). ctx: area de 33 contextos. */
void     xhci_build_cfgep_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci, uint16_t mps, uint8_t interval, uint64_t ring_phys);
/* SOMA_178_T480S: extremo ISOCRONO IN de alta velocidad (camaras UVC). mps_raw = wMaxPacketSize del descriptor (bits 10:0 tamano,
 * 12:11 transacciones extra por microtrama). interval = valor del contexto (bInterval - 1). Deja el extremo en el contexto dci. */
void     xhci_build_cfg_isoc_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci, uint16_t mps_raw, uint8_t interval, uint64_t ring_phys);
/* Configure Endpoint que SUELTA el extremo dci (la ranura se copia de out_ctx). */
void     xhci_build_drop_ep_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci);
/* Palabra d3 (sin bit de ciclo) de un TRB isocrono de un solo TRB por TD: SIA, IOC y TLBPC = paquetes - 1 (alta velocidad: TBC = 0). */
uint32_t xhci_isoc_d3(uint32_t packets, int ioc, int sia);
/* PARCHE 049: contexto de entrada de Configure Endpoint para un par de extremos BULK (entrada y salida de una interfaz Mass Storage). */
void     xhci_build_cfg_bulk_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci_in, uint8_t dci_out, uint16_t mps,
                                   uint64_t ring_in_phys, uint64_t ring_out_phys);
/* PARCHE 051: hubs USB2. Address Device con cadena de ruta y datos del concentrador padre (TT para LS/FS tras un hub HS). */
void     xhci_build_addr_input_ex(uint8_t *ctx, int csz64, uint8_t speed, uint32_t route, uint8_t root_port, uint16_t max_packet,
                                  uint64_t ep0_ring_phys, uint8_t parent_slot, uint8_t parent_port);
/* Configure Endpoint (solo A0) que marca la ranura como hub: bit Hub, numero de puertos y tiempo de reflexion TT. */
void     xhci_build_hub_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t nports, uint8_t tt_think, int mtt);
struct usb_hub_desc { uint8_t nports, tt_think, power_on_ms_div2; uint16_t chars; };
int      usb_parse_hub_desc(const uint8_t *d, uint32_t len, struct usb_hub_desc *o);              /* 0 = bien */
struct usb_port_status { uint8_t connected, enabled, reset, power, low, high, c_conn, c_reset; };
void     usb_decode_port_status(uint32_t st, struct usb_port_status *o);                          /* wPortStatus | wPortChange << 16 */
#define TRB_RESET_EP   14
#define TRB_SET_TR_DEQ 16
#define TRB_CONFIG_EP 12
#define TRB_EVAL_CTX  13

/* Analizador del descriptor de configuracion USB: interfaces y sus extremos (para localizar teclados/ratones HID). */
struct usb_ep_info { uint8_t addr, attr, interval; uint16_t mps; };
struct usb_if_info { uint8_t num, alt, cls, sub, proto, n_ep; struct usb_ep_info ep[4]; };
/* Devuelve el numero de interfaces escritas en out (maximo max); ignora descriptores mal formados sin salirse de len. */
int xhci_parse_config(const uint8_t *d, uint32_t len, struct usb_if_info *out, int max);
const char *usb_class_str(uint8_t cls, uint8_t sub, uint8_t proto);
#endif
