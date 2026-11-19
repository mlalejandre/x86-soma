/* SOMA_192: net.c usa netif_active()/netif_count() desde el parche 162 (net_describe) y las pruebas de TCP no enlazan netif.c: no enlazaban. */
#include "netif.h"
const struct netif_ops *netif_active(void) { return 0; }
int netif_count(void) { return 0; }
