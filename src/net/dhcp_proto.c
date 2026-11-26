/* PARCHE 081: mensajes DHCP. Ver dhcp_proto.h. */
#include "dhcp_proto.h"

size_t dhcp_build(uint8_t *o, int type, uint32_t xid, const uint8_t mac[6], const uint8_t *req_ip, const uint8_t *server, const char *hostname)
{
    for (int i = 0; i < 300; i++) o[i] = 0;
    o[0] = 1; o[1] = 1; o[2] = 6; o[3] = 0;                          /* peticion, Ethernet, 6 bytes de MAC */
    o[4] = (uint8_t)(xid >> 24); o[5] = (uint8_t)(xid >> 16); o[6] = (uint8_t)(xid >> 8); o[7] = (uint8_t)xid;
    o[10] = 0x80;                                                    /* respuesta por difusion: aun no tenemos IP */
    for (int i = 0; i < 6; i++) o[28 + i] = mac[i];
    o[236] = 99; o[237] = 130; o[238] = 83; o[239] = 99;             /* cookie magica */
    size_t p = 240;
    o[p++] = 53; o[p++] = 1; o[p++] = (uint8_t)type;
    o[p++] = 61; o[p++] = 7; o[p++] = 1; for (int i = 0; i < 6; i++) o[p++] = mac[i];
    if (req_ip) { o[p++] = 50; o[p++] = 4; for (int i = 0; i < 4; i++) o[p++] = req_ip[i]; }
    if (server) { o[p++] = 54; o[p++] = 4; for (int i = 0; i < 4; i++) o[p++] = server[i]; }
    if (hostname) { size_t n = 0; while (hostname[n] && n < 32) n++; o[p++] = 12; o[p++] = (uint8_t)n; for (size_t i = 0; i < n; i++) o[p++] = (uint8_t)hostname[i]; }
    o[p++] = 55; o[p++] = 4; o[p++] = 1; o[p++] = 3; o[p++] = 6; o[p++] = 51;
    o[p++] = 255;
    return 300;
}

/* SOMA_340_RED_HOSTIL: una mascara de verdad es una fila de unos seguida de ceros, de /8 a /32. Un servidor DHCP falso podia mandar
 * 128.0.0.0 (o 1.2.3.4): el panel web, que solo atiende a la red local segun la mascara, habria atendido a media Internet. */
int dhcp_mask_ok(const uint8_t m[4])
{
    uint32_t v = (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
    if ((v & 0xFF000000u) != 0xFF000000u) return 0;                 /* menos de /8 (o sin mascara) */
    uint32_t inv = ~v;
    return (inv & (inv + 1u)) == 0;                                  /* los ceros, todos seguidos al final */
}

int dhcp_parse(const uint8_t *b, size_t len, uint32_t xid, const uint8_t mac[6], struct dhcp_lease *l)
{
    if (len < 240) return -1;
    if (b[0] != 2) return 0;
    uint32_t x = (uint32_t)b[4] << 24 | (uint32_t)b[5] << 16 | (uint32_t)b[6] << 8 | b[7];
    if (x != xid) return 0;
    for (int i = 0; i < 6; i++) if (b[28 + i] != mac[i]) return 0;
    if (b[236] != 99 || b[237] != 130 || b[238] != 83 || b[239] != 99) return -1;
    for (int i = 0; i < (int)sizeof *l; i++) ((uint8_t *)l)[i] = 0;
    for (int i = 0; i < 4; i++) l->ip[i] = b[16 + i];
    int type = -1;
    for (size_t p = 240; p < len; ) {
        uint8_t c = b[p];
        if (c == 255) break;
        if (c == 0) { p++; continue; }
        if (p + 2 > len) return -1;
        uint8_t n = b[p + 1];
        if (p + 2 + n > len) return -1;
        const uint8_t *v = b + p + 2;
        if (c == 53 && n >= 1) type = v[0];
        else if (c == 1 && n >= 4) for (int i = 0; i < 4; i++) l->mask[i] = v[i];
        else if (c == 3 && n >= 4) for (int i = 0; i < 4; i++) l->router[i] = v[i];
        else if (c == 6 && n >= 4) for (int i = 0; i < 4; i++) l->dns[i] = v[i];
        else if (c == 54 && n >= 4) for (int i = 0; i < 4; i++) l->server[i] = v[i];
        else if (c == 51 && n >= 4) l->lease_s = (uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 | (uint32_t)v[2] << 8 | v[3];
        p += 2u + n;
    }
    if (type != DHCP_OFFER && type != DHCP_ACK && type != DHCP_NAK) return -1;
    if (type != DHCP_NAK && !dhcp_mask_ok(l->mask)) { l->mask[0] = 255; l->mask[1] = 255; l->mask[2] = 255; l->mask[3] = 0; }
    return type;
}
