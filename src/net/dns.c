#include <stdint.h>

#include "console.h"
#include "rng.h"
#include "dns.h"
#include "net.h"
#include "udp.h"
#include "idt.h"
#include "mem.h"
#include "dns_proto.h"                      /* SOMA_340_RED_HOSTIL: la respuesta se lee con una funcion pura probada con datos hostiles */


#define DNS_PORT          53
#define DNS_LOCAL_PORT    40053


uint8_t net_dns[4] = { 10, 0, 2, 3 };

/* PARCHE 031: el ID de consulta era secuencial (0x1000, 0x1001, ...) y el puerto local fijo: cualquiera que pudiera enviar
 * paquetes al sistema podia falsificar una respuesta DNS y redirigir al navegador. Ahora ID y puerto local son impredecibles
 * y solo se aceptan respuestas que vienen del servidor DNS y de su puerto 53. */
/* SOMA_192: el ID y el puerto salian de un xorshift de 32 bits sembrado con el tiempo de arranque (adivinable). Ahora, del
 * generador unico del nucleo (rng.h). */
static uint32_t dns_rand(void) { return rng_u32(); }


static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}


static int dns_build_query(const char *name, uint16_t id, uint8_t *q, int cap)
{
    int n = 0;

    wr16(q + 0, id);
    wr16(q + 2, 0x0100);        /* recursion deseada */
    wr16(q + 4, 1);             /* QDCOUNT */
    wr16(q + 6, 0);
    wr16(q + 8, 0);
    wr16(q + 10, 0);

    n = DNS_HEADER_LEN;

    int label_pos = n++;
    int count = 0;

    for (const char *p = name; ; ++p) {

        if (*p == '.' || *p == '\0') {

            if (count == 0) {
                return -1;
            }

            q[label_pos] = (uint8_t)count;

            if (*p == '\0') {
                break;
            }

            if (n >= cap - 6) {
                return -1;
            }

            label_pos = n++;
            count = 0;

        } else {

            if (count >= 63 || n >= cap - 6) {
                return -1;
            }

            q[n++] = (uint8_t)*p;
            ++count;
        }
    }

    q[n++] = 0;
    wr16(q + n, DNS_TYPE_A);
    n += 2;
    wr16(q + n, DNS_CLASS_IN);
    n += 2;

    return n;
}


static int dns_resolve_unlocked(const char *name, uint8_t *ip_out, uint32_t timeout_ms)
{
    uint16_t lport = (uint16_t)(20000u + dns_rand() % 40000u);
    if (udp_bind(lport) != 0) {
        kprint("DNS: cannot bind local port\n");
        return 0;
    }

    uint32_t per_try = timeout_ms / 3;
    if (per_try < 1000) {
        per_try = 1000;
    }

    int ok = 0;
    uint8_t query[256];
    uint8_t resp[512];
    uint8_t from_ip[4];
    uint16_t from_port;

    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        uint16_t id = (uint16_t)(dns_rand() >> 8);
        int qlen = dns_build_query(name, id, query, sizeof(query));

        if (qlen < 0) {
            kprint("DNS: invalid name\n");
            udp_unbind(lport);
            return 0;
        }

        if (udp_sendto(net_dns, DNS_PORT, lport, query,
                       (uint16_t)qlen) != 0)
        {
            kprint("DNS: send failed\n");
            continue;
        }

        for (int tries = 0; tries < 4 && !ok; ++tries) {

            int len = udp_recvfrom(lport, resp, sizeof(resp),
                                   from_ip, &from_port, per_try);

            if (len <= 0) {
                break;
            }

            if (from_port != DNS_PORT || memcmp(from_ip, net_dns, 4) != 0) {
                continue;       /* no viene del servidor DNS configurado */
            }

            uint16_t rcode = 0;
            int r = dns_respuesta_a(resp, len, id, ip_out, &rcode);
            if (r == DNS_R_AJENA) {
                continue;       /* respuesta ajena: seguir esperando */
            }
            if (r == DNS_R_ERROR) {
                kprint("DNS: server error, rcode=");
                kprint_dec(rcode);
                kprint("\n");
                udp_unbind(lport);
                return 0;
            }
            ok = r == DNS_R_OK;

            if (!ok) {
                kprint("DNS: no A record in response\n");
                udp_unbind(lport);
                return 0;
            }
        }
    }

    if (!ok) {
        kprint("DNS: timeout\n");
    }

    udp_unbind(lport);

    return ok;
}



int dns_resolve(const char *name, uint8_t *ip_out, uint32_t timeout_ms)
{
    net_client_lock();
    int r = dns_resolve_unlocked(name, ip_out, timeout_ms);
    net_client_unlock();
    return r;
}
