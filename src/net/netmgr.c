/* SOMA_170_T480S: gestor de red. Cada segundo mira las interfaces "reales" (cable eth0, con el 171 el movil por USB
 * usb0 y con el 175C la WiFi wlan0) por orden de preferencia: usa la primera con enlace, y al aparecer el enlace pide direccion por DHCP. Si se
 * quita el cable y hay otra con enlace, cambia a ella. En QEMU con virtio-net (IP fija) no hace nada.
 * Interruptor: soma.redauto=off (se maneja a mano con netif usar y dhcp). */
#include <stdint.h>
#include "netmgr.h"
#include "netif.h"
#include "net.h"
#include "dhcp.h"
#include "console.h"
#include "thread.h"
#include "idt.h"
#include "mem.h"
#ifdef __x86_64__
#include "bootfb.h"
#endif

static volatile int g_state;           /* 0 sin interfaz real, 1 sin enlace, 2 pidiendo direccion, 3 con direccion, 4 DHCP fallo */
static const struct netif_ops *g_cfg_if;
static uint32_t g_switches, g_dhcp_ok, g_dhcp_fail;

static int is_auto(const struct netif_ops *o)
{
    if (!o || !o->name) return 0;
    if ((o->name[0] == 'e' && o->name[1] == 't' && o->name[2] == 'h') || (o->name[0] == 'u' && o->name[1] == 's' && o->name[2] == 'b')) return 1;
#ifdef __x86_64__
    if (o->name[0] == 'w' && o->name[1] == 'l' && o->name[2] == 'a') return 1;   /* SOMA_175C_T480S: WiFi (la ultima en preferencia) */
#endif
    return 0;
}
static int link_of(const struct netif_ops *o) { return o && o->link ? o->link() : 0; }

static void netmgr_main(void *a)
{
    (void)a;
    uint64_t retry_at = 0;
    int was_link = 0;
    thread_sleep(500);
    for (;;) {
        int best = -1, any = 0;
        for (int i = 0; i < netif_count(); i++) {
            const struct netif_ops *o = netif_get(i);
            if (!is_auto(o)) continue;
            if (netif_is_up(i) && o->name[0] == 'u' && o->init && !o->init()) netif_mark_down(i);   /* SOMA_171: el movil se desenchufo */
            if (!netif_is_up(i)) { netif_retry(i); if (!netif_start(i)) continue; }   /* arrancarla (sin activarla); USB: en caliente */
            any = 1;
            if (best < 0 && link_of(o)) best = i;
        }
        const struct netif_ops *act = netif_active();
        if (!any) {
            if (was_link) { kprint("RED: "); kprint(act ? act->name : "?"); kprint(": desconectada\n"); was_link = 0; g_cfg_if = 0; }
            g_state = 0; thread_sleep(1000); continue;
        }
        if (best >= 0 && netif_get(best) != act) {
            const struct netif_ops *o = netif_get(best);
            if (netif_use(o->name)) {
                memcpy(net_mac, netif_mac(), 6); net_arp_flush(); g_switches++;
                kprint("RED: se usa "); kprint(o->name); kprint(" ("); kprint(o->desc); kprint(")\n");
                act = o; g_cfg_if = 0; was_link = 0;
            }
        }
        int ln = is_auto(act) && link_of(act);
        if (!ln) {
            if (was_link) { kprint("RED: "); kprint(act ? act->name : "?"); kprint(": enlace perdido\n"); }
            was_link = 0; g_cfg_if = 0; g_state = 1;
        } else {
            if (!was_link) { was_link = 1; kprint("RED: "); kprint(act->name); kprint(": enlace arriba, pidiendo direccion (DHCP)\n"); retry_at = 0; }
#ifdef __x86_64__
            { extern int iwl_ip_is_static(void); if (iwl_ip_is_static()) { g_cfg_if = act; g_state = 3; } }   /* SOMA_175C6: IP fija de wifi ipfija */
#endif
            if (g_cfg_if != act && timer_get_uptime_ms() >= retry_at) {
                g_state = 2;
                if (dhcp_run(0) == 0) { g_cfg_if = act; g_state = 3; g_dhcp_ok++; net_arp_flush(); }
                else { g_state = 4; g_dhcp_fail++; retry_at = timer_get_uptime_ms() + 10000; }
            }
        }
        thread_sleep(1000);
    }
}

void netmgr_start(void)
{
#ifdef __x86_64__
    if (bootinfo_disabled("redauto")) { kprint("RED: gestion automatica desactivada (soma.redauto=off)\n"); return; }
#endif
    thread_create("netmgr", netmgr_main, 0);
}

const char *netmgr_state(void)
{
    switch (g_state) {
    case 0: return "sin cable, movil por USB ni WiFi (en QEMU con virtio: IP fija)";
    case 1: return "sin enlace (enchufa el cable o el movil, o conecta una WiFi en Ajustes > Redes)";
    case 2: return "pidiendo direccion (DHCP)";
    case 3: return "conectado";
    default: return "el router no dio direccion (se reintenta cada 10 s)";
    }
}
void netmgr_counts(uint32_t *sw, uint32_t *ok, uint32_t *fail) { *sw = g_switches; *ok = g_dhcp_ok; *fail = g_dhcp_fail; }
