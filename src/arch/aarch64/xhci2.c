/* PARCHE 077: el RP1 de la Pi 5 tiene DOS controladores xHCI (usbhost0 y usbhost1); cada uno lleva parte de los cuatro puertos USB de la placa.
 * Hasta ahora solo se manejaba el primero. xhci.c guarda su estado en variables estaticas (un controlador), asi que el segundo se obtiene
 * compilando el mismo fichero otra vez con sus funciones publicas renombradas a xhci2_*: estado propio, mismo codigo. Solo teclado, raton y hubs
 * llegan al sistema desde el segundo (los discos USB se siguen leyendo por el primero). */
#define XHCI_SECOND 1
#define xhci_autostart  xhci2_autostart
#define xhci_err_str    xhci2_err_str
#define xhci_hid_active xhci2_hid_active
#define xhci_hid_start  xhci2_hid_start
#define xhci_hid_status xhci2_hid_status
#define xhci_hid_stop   xhci2_hid_stop
#define xhci_last       xhci2_last
#define xhci_msc_get    xhci2_msc_get
#define xhci_msc_read   xhci2_msc_read
#define xhci_msc_ready  xhci2_msc_ready
#define xhci_msc_start  xhci2_msc_start
#define xhci_msc_write  xhci2_msc_write
#define xhci_probe      xhci2_probe
#define xhci_rescan     xhci2_rescan
#define xhci_msc_write_nosync xhci2_msc_write_nosync   /* SOMA_162_T480S */
#define xhci_msc_sync         xhci2_msc_sync
#define xhci_msc_generation   xhci2_msc_generation
#define xhci_rescanning       xhci2_rescanning
#define xhci_autostart_early  xhci2_autostart_early
#define xhci_msc_report       xhci2_msc_report       /* SOMA_175A: la del 171B (en ARM se compilaba dos veces) */
#include "../x86_64/xhci.c"
