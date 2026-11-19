#ifndef SOMA_WEB_BROWSER_TRANSPORT_H
#define SOMA_WEB_BROWSER_TRANSPORT_H
/* SOMA 143A: seleccion explicita del transporte del navegador.
 * 0 (por defecto): ruta original del kernel, sin cambios de comportamiento.
 * 1 (solo x86_64): todas las descargas web de web_page.c usan netsvc en Ring 3.
 * NO hay fallback a wn_fetch al fallar netsvc (fail-closed).
 * ADVERTENCIA: aun NO reproduce todos los campos HTTP del navegador (cookies,
 * Referer, Accept, estado de la pasarela). Solo para pruebas comparativas.
 */
#include "web_net.h"
#ifndef SOMA_BROWSER_NET_RING3
#define SOMA_BROWSER_NET_RING3 0
#endif
#if SOMA_BROWSER_NET_RING3 != 0 && SOMA_BROWSER_NET_RING3 != 1
#error "SOMA_BROWSER_NET_RING3 debe ser 0 o 1"
#endif
#if SOMA_BROWSER_NET_RING3 && !defined(__x86_64__) && !defined(SOMA_BROWSER_NET_TEST)
#error "SOMA 143A: no hay servicio netsvc en AArch64; use BROWSER_NET_RING3=0"
#endif
static inline int wb_network_fetch(const struct wn_req *rq, struct wn_resp *rs)
{
#if SOMA_BROWSER_NET_RING3
    return wn_fetch_isolated(rq, rs);  /* nunca reintentar en Ring 0 */
#else
    return wn_fetch(rq, rs);
#endif
}
#endif
