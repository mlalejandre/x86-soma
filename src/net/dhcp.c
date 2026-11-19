/* PARCHE 081: cliente DHCP (DISCOVER -> OFFER -> REQUEST -> ACK) sobre la pila UDP de SOMA. */
#include <stdint.h>
#include "dhcp.h"
#include "dhcp_proto.h"
#include "net.h"
#include "udp.h"
#include "console.h"
#include "idt.h"
#include "mem.h"

static int wait_reply(uint32_t xid, int want, struct dhcp_lease *l, uint32_t ms)
{
    static uint8_t buf[DHCP_MAX + 64];
    uint64_t t0 = timer_ticks;
    while (timer_ticks - t0 < ms) {
        uint8_t from[4]; uint16_t fp = 0;
        int n = udp_recvfrom(68, buf, sizeof buf, from, &fp, (uint32_t)(ms - (timer_ticks - t0)) + 1);
        if (n <= 0) return 0;
        int t = dhcp_parse(buf, (size_t)n, xid, net_mac, l);
        if (t == want || t == DHCP_NAK) return t;
    }
    return 0;
}

int dhcp_run(int verbose)
{
    uint8_t old[4]; memcpy(old, net_ip, 4);
    static const uint8_t bc[4] = { 255, 255, 255, 255 };
    static uint8_t msg[300];
    udp_unbind(68);
    if (udp_bind(68) != 0) { kprint("dhcp: puerto 68 ocupado\n"); return -1; }
    memset(net_ip, 0, 4);
    uint32_t xid = (uint32_t)timer_ticks * 2654435761u ^ ((uint32_t)net_mac[3] << 16 | (uint32_t)net_mac[4] << 8 | net_mac[5]);
    struct dhcp_lease off, ack; int got = 0;
    for (int a = 0; a < 4 && !got; a++) {
        dhcp_build(msg, DHCP_DISCOVER, xid, net_mac, 0, 0, "soma");
        if (verbose) kprint("dhcp: DISCOVER\n");
        udp_sendto(bc, 67, 68, msg, 300);
        got = wait_reply(xid, DHCP_OFFER, &off, 2000) == DHCP_OFFER;
    }
    int r = -1;
    if (got) {
        if (verbose) { kprint("dhcp: OFFER "); kprint_ip(off.ip); kprint(" de "); kprint_ip(off.server); kprint("\n"); }
        for (int a = 0; a < 3 && r != 0; a++) {
            dhcp_build(msg, DHCP_REQUEST, xid, net_mac, off.ip, off.server, "soma");
            udp_sendto(bc, 67, 68, msg, 300);
            int t = wait_reply(xid, DHCP_ACK, &ack, 2000);
            if (t == DHCP_NAK) { kprint("dhcp: el router rechazo la peticion (NAK)\n"); break; }
            if (t == DHCP_ACK) r = 0;
        }
    } else kprint("dhcp: ningun servidor DHCP contesto\n");
    udp_unbind(68);
    if (r != 0) {                                                    /* SOMA_175C7: no pisar una IP puesta mientras tanto (wifi ipfija) */
        if (!(net_ip[0] | net_ip[1] | net_ip[2] | net_ip[3])) memcpy(net_ip, old, 4);
        return -1;
    }
    memcpy(net_ip, ack.ip, 4); memcpy(net_netmask, ack.mask, 4);
    if (ack.router[0]) memcpy(net_gateway, ack.router, 4);
    if (ack.dns[0]) memcpy(net_dns, ack.dns, 4);
    kprint("dhcp: IP "); kprint_ip(net_ip); kprint(" mascara "); kprint_ip(net_netmask); kprint(" puerta "); kprint_ip(net_gateway);
    kprint(" DNS "); kprint_ip(net_dns); kprint(" ("); kprint_dec(ack.lease_s / 60); kprint(" min)\n");
    return 0;
}
