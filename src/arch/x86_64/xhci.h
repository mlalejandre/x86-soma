#ifndef SOMA_XHCI_H
#define SOMA_XHCI_H
#include <stdint.h>
#include "xhci_proto.h"
/* PARCHE 046: controlador USB xHCI (PCI 0C:03:30), FASE 1, en modo SONDEO y opt-in (comando "usb"): reinicia el controlador,
 * reclama la propiedad al BIOS, configura anillos de comandos/eventos, reinicia los puertos con dispositivo, hace Enable Slot +
 * Address Device y lee los descriptores de dispositivo y de configuracion. No instala controladores de clase todavia (fase 2: HID).
 * La logica de anillos/contextos/descriptores vive en src/core/xhci_proto.c (probada en el host). */
#define XHCI_MAX_DEVS 8
#define XHCI_MAX_PORTS_INFO 32
struct usb_dev_info {
    uint8_t  port, speed, slot, mps0, cls, sub, proto, n_if, cfg_value;
    uint8_t  root_port, parent, is_hub, hub_ports, hub_note, hub_err, hub_cc, hub_cc2;   /* parent: indice del hub padre (0xFF = puerto raiz); hub_note: 1 hub SS no soportado, 2-4 fallo de configuracion */
    uint32_t route;                                              /* cadena de ruta (un nibble por nivel de hub) */
    uint16_t vid, pid, bcd_usb, bcd_dev;
    struct usb_if_info ifs[4];
    int      err;                                  /* 0 = bien; si no, codigo del fallo de este dispositivo */
};
struct xhci_port_info { uint8_t num, ccs, ped, speed, major; uint32_t portsc; };
struct xhci_info {
    int      found, ready;
    uint8_t  bus, slot, fn;
    uint64_t bar;
    uint16_t hciversion;
    uint8_t  max_slots, max_ports, csz64;
    uint32_t scratch;
    int      bios_owned;                           /* 1 si el BIOS tenia el controlador y se lo pedimos */
    int      n_ports, n_conn;
    struct xhci_port_info port[XHCI_MAX_PORTS_INFO];
    int      ndev;
    struct usb_dev_info dev[XHCI_MAX_DEVS];
    int      err;
};
struct xhci_hid_status { int active, nkbd, nmouse; uint32_t reports, keys, mouse_ev, errors, recovered; int dead; };
/* PARCHE 049: almacenamiento masivo USB (Bulk-Only, SCSI transparente, sectores de 512 bytes, < 2 TiB), por sondeo. */
struct xhci_msc_info { char vendor[9], product[17]; uint64_t sectors; uint8_t sector0[16]; };
int xhci_msc_start(const struct xhci_info *o, struct xhci_msc_info *out);   /* 0 = disco listo (tras xhci_probe) */
int xhci_msc_ready(void);
void xhci_msc_report(void);                                                  /* SOMA_172B: dispositivos de almacenamiento probados */
const struct xhci_msc_info *xhci_msc_get(void);
int xhci_msc_read(uint64_t lba, uint32_t n, void *buf);                     /* n <= 128; 0 = bien */
int xhci_msc_write(uint64_t lba, uint32_t n, const void *buf);
int xhci_msc_write_nosync(uint64_t lba, uint32_t n, const void *buf);   /* SOMA_162: sin SYNCHRONIZE CACHE */
int xhci_msc_sync(void);
/* SOMA_363_ACCESO: varios discos USB. El "primario" es el del sistema (lo elige usbdisk); las funciones sin _n usan ese. */
int xhci_msc_count(void);
const struct xhci_msc_info *xhci_msc_get_n(int i);
int xhci_msc_read_n(int i, uint64_t lba, uint32_t n, void *buf);
int xhci_msc_write_n(int i, uint64_t lba, uint32_t n, const void *buf);     /* sin SYNCHRONIZE CACHE (xhci_msc_sync_n) */
int xhci_msc_sync_n(int i);
int xhci_msc_primario(void);
void xhci_msc_pon_primario(int i);
uint32_t xhci_msc_generation(void);                                  /* cambia cada vez que el disco USB se reinicia */
int xhci_rescanning(void);
int xhci_probe(struct xhci_info *o);               /* 0 = controlador activo y puertos enumerados */
const char *xhci_err_str(int err);
/* PARCHE 047 (fase 2): teclado y raton USB HID (protocolo boot) sobre un controlador ya enumerado con xhci_probe. Configura el extremo
 * de interrupcion IN, SET_CONFIGURATION / SET_PROTOCOL(boot) / SET_IDLE y lanza el hilo "usbd", que sondea el anillo de eventos y
 * entrega los scancodes PS/2 a la consola (keyboard_inject_scancode) y el movimiento al raton (mouse_inject). */
int xhci_hid_start(const struct xhci_info *o, struct xhci_hid_status *st);   /* 0 = al menos un dispositivo en marcha */
void xhci_hid_stop(void);
void xhci_hid_status(struct xhci_hid_status *st);
int xhci_hid_active(void);
/* PARCHE 052: hot-plug de puertos raiz (hilo usbmon) y reexploracion manual. */
int xhci_rescan(void);                                  /* 0 = hecho, -1 = ya en curso, otro = error del controlador */
const struct xhci_info *xhci_last(void);                /* ultima exploracion (sin reiniciar el controlador) */
/* Arranque: si hay un xHCI con teclado o raton USB boot, los activa (silencioso si no hay controlador). Se omite si existe /etc/nousb. */
void xhci_autostart(void);
void xhci_autostart_early(void);                        /* SOMA_162: sin mirar /etc/nousb (antes del VFS) */
#endif
