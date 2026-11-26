/* SOMA_340_RED_HOSTIL: lectura de la respuesta DNS, sacada de dns.c para probarla en el anfitrion con datos hostiles
 * (tests/test_red_hostil_340.c). Funcion pura: no toca la red ni la memoria del nucleo. Hace lo mismo que el bucle de antes. */
#ifndef SOMA_DNS_PROTO_H
#define SOMA_DNS_PROTO_H

#include <stdint.h>

#define DNS_HEADER_LEN    12
#define DNS_TYPE_A        1
#define DNS_CLASS_IN      1

#define DNS_R_AJENA       0      /* no es la respuesta a esta consulta (otro ID, demasiado corta): seguir esperando */
#define DNS_R_OK          1      /* registro A en ip_out */
#define DNS_R_ERROR      (-1)    /* el servidor contesta con un error (codigo en *rcode) o no es una respuesta */
#define DNS_R_SIN_A      (-2)    /* respuesta valida sin registro A legible */

static inline uint16_t dns_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* Devuelve el offset posterior al nombre, o -1 si es invalido o se sale del mensaje. */
static inline int dns_skip_name(const uint8_t *m, int len, int off)
{
    while (off >= 0 && off < len) {
        uint8_t l = m[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return (off + 1 < len) ? off + 2 : -1;     /* puntero de compresion: no se sigue */
        if (l & 0xC0) return -1;
        off += 1 + l;
    }
    return -1;
}

static inline int dns_respuesta_a(const uint8_t *resp, int len, uint16_t id, uint8_t ip_out[4], uint16_t *rcode)
{
    if (len < DNS_HEADER_LEN || dns_rd16(resp) != id) return DNS_R_AJENA;
    uint16_t flags = dns_rd16(resp + 2);
    uint16_t rc = flags & 0x000F;
    if (rcode) *rcode = rc;
    if (!(flags & 0x8000) || rc != 0) return DNS_R_ERROR;

    int qd = dns_rd16(resp + 4);
    int an = dns_rd16(resp + 6);
    int off = DNS_HEADER_LEN;
    for (int i = 0; i < qd; ++i) {
        off = dns_skip_name(resp, len, off);
        if (off < 0) break;
        off += 4;
    }
    for (int i = 0; i < an && off > 0; ++i) {
        off = dns_skip_name(resp, len, off);
        if (off < 0 || off + 10 > len) break;
        uint16_t type  = dns_rd16(resp + off);
        uint16_t cls   = dns_rd16(resp + off + 2);
        uint16_t rdlen = dns_rd16(resp + off + 8);
        off += 10;
        if (off + rdlen > len) break;
        if (type == DNS_TYPE_A && cls == DNS_CLASS_IN && rdlen == 4) {
            for (int k = 0; k < 4; ++k) ip_out[k] = resp[off + k];
            return DNS_R_OK;
        }
        off += rdlen;
    }
    return DNS_R_SIN_A;
}

#endif
