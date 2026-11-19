/* PARCHE 080: capa de interfaces de red (ver netif.h). Logica pura salvo el registro: se prueba en el host (tests/test_netif.c). */
#include "netif.h"

static const struct netif_ops *g_if[NETIF_MAX];
static int g_n, g_up[NETIF_MAX], g_tried[NETIF_MAX], g_active = -1;
static const uint8_t g_zero[6];

int netif_register(const struct netif_ops *o)
{
    if (!o || g_n >= NETIF_MAX) return -1;
    for (int i = 0; i < g_n; i++) if (g_if[i] == o) return 0;
    g_if[g_n++] = o;
    return 0;
}
static int start(int i)
{
    if (g_up[i]) return 1;
    if (g_tried[i] > 0) return 0;                  /* no se reintenta un dispositivo que fallo (algunos no son reentrantes) */
    g_tried[i] = 1;
    g_up[i] = g_if[i]->init && g_if[i]->init() ? 1 : 0;
    return g_up[i];
}
void netif_retry(int i) { if (i >= 0 && i < g_n && !g_up[i]) g_tried[i] = 0; }   /* SOMA_171: dispositivos que aparecen en caliente */
void netif_mark_down(int i) { if (i >= 0 && i < g_n) { g_up[i] = 0; g_tried[i] = 0; } }
int netif_start(int i) { return i >= 0 && i < g_n ? start(i) : 0; }   /* SOMA_170: arrancar sin activarla */
int netif_start_first(void)
{
    if (g_active >= 0) return 1;
    for (int i = 0; i < g_n; i++) if (start(i)) { g_active = i; return 1; }
    return 0;
}
int netif_use(const char *name)
{
    for (int i = 0; i < g_n; i++) {
        const char *a = g_if[i]->name, *b = name; while (*a && *a == *b) { a++; b++; }
        if (*a || *b) continue;
        if (g_tried[i] && !g_up[i]) g_tried[i] = 0;     /* peticion explicita: se reintenta (p. ej. el Wi-Fi tras conectar) */
        if (!start(i)) return 0;
        g_active = i;
        return 1;
    }
    return 0;
}
const struct netif_ops *netif_active(void) { return g_active >= 0 ? g_if[g_active] : 0; }
int netif_count(void) { return g_n; }
const struct netif_ops *netif_get(int i) { return i >= 0 && i < g_n ? g_if[i] : 0; }
int netif_is_up(int i) { return i >= 0 && i < g_n && g_up[i]; }
int netif_send(const void *frame, uint16_t len) { const struct netif_ops *o = netif_active(); return o && o->send ? o->send(frame, len) : -1; }
int netif_poll(void *out, uint16_t max, uint16_t *len_out) { const struct netif_ops *o = netif_active(); return o && o->poll ? o->poll(out, max, len_out) : 0; }
const uint8_t *netif_mac(void) { const struct netif_ops *o = netif_active(); return o && o->mac ? o->mac() : g_zero; }
void netif_stats(uint32_t *tx, uint32_t *rx, uint64_t *txb, uint64_t *rxb)
{
    const struct netif_ops *o = netif_active();
    if (o && o->stats) { o->stats(tx, rx, txb, rxb); return; }
    if (tx) *tx = 0;
    if (rx) *rx = 0;
    if (txb) *txb = 0;
    if (rxb) *rxb = 0;
}
