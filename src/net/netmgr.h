#ifndef SOMA_NETMGR_H
#define SOMA_NETMGR_H
/* SOMA_170_T480S: eleccion automatica de la interfaz (cable > movil por USB) y DHCP al aparecer el enlace. Ver netmgr.c. */
#include <stdint.h>
void netmgr_start(void);
const char *netmgr_state(void);
void netmgr_counts(uint32_t *switches, uint32_t *dhcp_ok, uint32_t *dhcp_fail);
#endif
