#ifndef SOMA_E1000E_H
#define SOMA_E1000E_H
/* SOMA_170_T480S: Ethernet Intel e1000/e1000e (I219 del T480s; 82574L y 82540EM en QEMU). Ver e1000e.c. */
#include "netif.h"
extern const struct netif_ops netif_e1000e;
void e1000e_info(void);
int e1000e_pci_known(uint16_t device);   /* SOMA_228_ARRANQUE: 1 si el driver acepta ese dispositivo Intel */
#endif
