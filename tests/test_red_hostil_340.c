/* SOMA_340_RED_HOSTIL: lo que cualquier equipo de la red local puede mandar al nucleo y no tenia prueba.
 *   - respuestas DNS (src/net/dns_proto.h): casos a mano y cientos de miles mutadas o al azar;
 *   - ofertas DHCP (src/net/dhcp_proto.c): mutadas, y mascaras falsas (128.0.0.0, no contiguas) que pasan a /24;
 *   - el panel web solo atiende a la red local aunque la mascara sea absurda (src/net/httpd_policy.h).
 * Con ASan/UBSan: cualquier lectura fuera del mensaje para la prueba.
 *   cc -std=gnu11 -fsanitize=address,undefined -Isrc/net tests/test_red_hostil_340.c src/net/dhcp_proto.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dns_proto.h"
#include "dhcp_proto.h"
#include "httpd_policy.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)

static uint64_t semilla = 0x9E3779B97F4A7C15ull;
static uint32_t azar(void) { semilla ^= semilla << 13; semilla ^= semilla >> 7; semilla ^= semilla << 17; return (uint32_t)(semilla >> 11); }

/* ---------------------------------------------------------------- DNS */
static int pon16(uint8_t *b, int o, uint16_t v) { b[o] = (uint8_t)(v >> 8); b[o + 1] = (uint8_t)v; return o + 2; }
static int pon_nombre(uint8_t *b, int o, const char *n)
{
    while (*n) {
        const char *p = n; while (*p && *p != '.') p++;
        b[o++] = (uint8_t)(p - n); memcpy(b + o, n, (size_t)(p - n)); o += (int)(p - n);
        n = *p ? p + 1 : p;
    }
    b[o++] = 0; return o;
}
/* respuesta a "ejemplo.com": opcionalmente un CNAME antes del A, y el A con puntero de compresion al nombre de la pregunta */
static int respuesta(uint8_t *b, uint16_t id, uint16_t flags, int con_cname, int con_a)
{
    memset(b, 0, 512);
    int o = pon16(b, 0, id); o = pon16(b, o, flags); o = pon16(b, o, 1); o = pon16(b, o, (uint16_t)(con_cname + con_a)); o = pon16(b, o, 0); o = pon16(b, o, 0);
    o = pon_nombre(b, o, "ejemplo.com"); o = pon16(b, o, 1); o = pon16(b, o, 1);
    if (con_cname) {
        b[o++] = 0xC0; b[o++] = 12; o = pon16(b, o, 5); o = pon16(b, o, 1); o = pon16(b, o, 0); o = pon16(b, o, 300);
        int lr = o; o += 2; int ini = o; o = pon_nombre(b, o, "cdn.ejemplo.net"); pon16(b, lr, (uint16_t)(o - ini));
    }
    if (con_a) {
        b[o++] = 0xC0; b[o++] = 12; o = pon16(b, o, 1); o = pon16(b, o, 1); o = pon16(b, o, 0); o = pon16(b, o, 300); o = pon16(b, o, 4);
        b[o++] = 93; b[o++] = 184; b[o++] = 216; b[o++] = 34;
    }
    return o;
}

static void prueba_dns(void)
{
    uint8_t b[512], ip[4]; uint16_t rc;
    int n = respuesta(b, 0x1234, 0x8180, 0, 1);
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) == DNS_R_OK && ip[0] == 93 && ip[3] == 34, "A con puntero de compresion");
    n = respuesta(b, 0x1234, 0x8180, 1, 1);
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) == DNS_R_OK && ip[1] == 184, "CNAME y despues A");
    n = respuesta(b, 0x1234, 0x8180, 1, 0);
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) == DNS_R_SIN_A, "solo CNAME: sin registro A");
    n = respuesta(b, 0x1234, 0x8180, 0, 1);
    CK(dns_respuesta_a(b, n, 0x4321, ip, &rc) == DNS_R_AJENA, "otro ID: se ignora y se sigue esperando");
    CK(dns_respuesta_a(b, 11, 0x1234, ip, &rc) == DNS_R_AJENA, "menos de una cabecera: se ignora");
    n = respuesta(b, 0x1234, 0x8183, 0, 0);
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) == DNS_R_ERROR && rc == 3, "NXDOMAIN: error con su codigo");
    n = respuesta(b, 0x1234, 0x0100, 0, 1);
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) == DNS_R_ERROR, "una consulta (QR=0) no vale como respuesta");
    n = respuesta(b, 0x1234, 0x8180, 0, 1);
    for (int corte = 0; corte < n; corte++) {                        /* cortada en cada byte: nunca un A con datos de fuera */
        uint8_t *c = malloc((size_t)(corte ? corte : 1)); memcpy(c, b, (size_t)corte);
        int r = dns_respuesta_a(c, corte, 0x1234, ip, &rc);
        CK(corte == n || r != DNS_R_OK, "respuesta cortada no da un A");
        free(c);
    }
    /* recuentos enormes de preguntas y respuestas, longitudes de datos imposibles, punteros al final */
    n = respuesta(b, 0x1234, 0x8180, 0, 1); pon16(b, 4, 0xFFFF); pon16(b, 6, 0xFFFF);
    (void)dns_respuesta_a(b, n, 0x1234, ip, &rc);
    n = respuesta(b, 0x1234, 0x8180, 0, 1); b[n - 5] = 0xFF;          /* rdlength 0xFF04 */
    CK(dns_respuesta_a(b, n, 0x1234, ip, &rc) != DNS_R_OK, "rdlength que se sale del mensaje");
    n = respuesta(b, 0x1234, 0x8180, 0, 1); b[n - 1 - 4 - 10 - 1] = 0xC0;
    (void)dns_respuesta_a(b, n - 15, 0x1234, ip, &rc);

    long ok = 0, total = 0;
    uint8_t base[512]; int bn = respuesta(base, 0x1234, 0x8180, 1, 1);
    for (int it = 0; it < 300000; it++) {
        int len; uint8_t *m;
        if (it % 5 == 0) {                                            /* basura al azar con un ID que casa */
            len = (int)(azar() % 513); m = malloc((size_t)(len ? len : 1));
            for (int i = 0; i < len; i++) m[i] = (uint8_t)azar();
            if (len >= 2) { m[0] = 0x12; m[1] = 0x34; }
        } else {                                                      /* una respuesta buena con unos bytes cambiados y quiza cortada */
            len = bn; if (azar() % 3 == 0) len = (int)(azar() % (unsigned)(bn + 1));
            m = malloc((size_t)(len ? len : 1)); memcpy(m, base, (size_t)len);
            int k = 1 + (int)(azar() % 6);
            for (int i = 0; i < k && len > 2; i++) m[2 + azar() % (unsigned)(len - 2)] = (uint8_t)azar();
        }
        uint8_t ipx[4] = { 0, 0, 0, 0 };
        int r = dns_respuesta_a(m, len, 0x1234, ipx, &rc);
        CK(r >= DNS_R_SIN_A && r <= DNS_R_OK, "resultado conocido");
        ok += r == DNS_R_OK; total++;
        free(m);
    }
    printf("  dns: %ld respuestas hostiles, %ld aun daban una IP, ninguna lee fuera\n", total, ok);
}

/* ---------------------------------------------------------------- DHCP */
static const uint8_t MAC[6] = { 2, 0, 0, 0, 0, 1 };
static int oferta(uint8_t *b, const uint8_t mask[4], int con_mask)
{
    memset(b, 0, 600);
    b[0] = 2; b[1] = 1; b[2] = 6; b[4] = 0xCA; b[5] = 0xFE; b[6] = 0xBA; b[7] = 0xBE;
    b[16] = 192; b[17] = 168; b[18] = 1; b[19] = 50;
    memcpy(b + 28, MAC, 6); b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99;
    int p = 240;
    b[p++] = 53; b[p++] = 1; b[p++] = DHCP_OFFER;
    if (con_mask) { b[p++] = 1; b[p++] = 4; memcpy(b + p, mask, 4); p += 4; }
    b[p++] = 3; b[p++] = 4; b[p++] = 192; b[p++] = 168; b[p++] = 1; b[p++] = 1;
    b[p++] = 6; b[p++] = 4; b[p++] = 1; b[p++] = 1; b[p++] = 1; b[p++] = 1;
    b[p++] = 51; b[p++] = 4; b[p++] = 0; b[p++] = 0; b[p++] = 0x0E; b[p++] = 0x10;
    b[p++] = 255;
    return p;
}

static void prueba_dhcp(void)
{
    uint8_t b[600]; struct dhcp_lease l;
    static const struct { uint8_t m[4]; uint8_t ok; } casos[] = {
        { { 255, 255, 255, 0 }, 1 }, { { 255, 255, 0, 0 }, 1 }, { { 255, 0, 0, 0 }, 1 }, { { 255, 255, 255, 255 }, 1 },
        { { 255, 255, 255, 252 }, 1 }, { { 255, 255, 240, 0 }, 1 },
        { { 128, 0, 0, 0 }, 0 }, { { 0, 0, 0, 0 }, 0 }, { { 255, 0, 255, 0 }, 0 }, { { 1, 2, 3, 4 }, 0 }, { { 255, 255, 255, 253 }, 0 },
        { { 254, 0, 0, 0 }, 0 }, { { 0, 255, 255, 255 }, 0 },
    };
    for (unsigned i = 0; i < sizeof casos / sizeof casos[0]; i++) {
        CK(dhcp_mask_ok(casos[i].m) == casos[i].ok, "mascara valida o no");
        int n = oferta(b, casos[i].m, 1);
        CK(dhcp_parse(b, (size_t)n, 0xCAFEBABE, MAC, &l) == DHCP_OFFER, "la oferta se lee");
        if (casos[i].ok) CK(!memcmp(l.mask, casos[i].m, 4), "mascara buena: se respeta");
        else CK(l.mask[0] == 255 && l.mask[1] == 255 && l.mask[2] == 255 && l.mask[3] == 0, "mascara falsa: /24");
    }
    int n = oferta(b, casos[0].m, 0);
    CK(dhcp_parse(b, (size_t)n, 0xCAFEBABE, MAC, &l) == DHCP_OFFER && l.mask[2] == 255 && l.mask[3] == 0, "sin mascara: /24 como antes");

    uint8_t base[600]; int bn = oferta(base, casos[0].m, 1);
    long leidas = 0;
    for (int it = 0; it < 200000; it++) {
        size_t len = (size_t)bn; if (azar() % 3 == 0) len = azar() % (unsigned)(bn + 40);
        uint8_t *m = malloc(len ? len : 1); memcpy(m, base, len < (size_t)bn ? len : (size_t)bn);
        for (size_t i = (size_t)bn; i < len; i++) m[i] = (uint8_t)azar();
        int k = 1 + (int)(azar() % 6);
        for (int i = 0; i < k && len > 240; i++) m[240 + azar() % (unsigned)(len - 240)] = (uint8_t)azar();
        if (azar() % 8 == 0 && len > 8) m[azar() % (len < 240 ? len : 240)] = (uint8_t)azar();
        int r = dhcp_parse(m, len, 0xCAFEBABE, MAC, &l);
        if (r == DHCP_OFFER || r == DHCP_ACK) { leidas++; CK(dhcp_mask_ok(l.mask), "toda oferta aceptada lleva una mascara valida"); }
        free(m);
    }
    printf("  dhcp: 200000 ofertas mutadas (%ld aceptadas, todas con mascara valida), ninguna lee fuera\n", leidas);
}

/* ---------------------------------------------------------------- panel web */
static void prueba_panel(void)
{
    unsigned char ip[4] = { 192, 168, 1, 50 };
    unsigned char m24[4] = { 255, 255, 255, 0 }, mala[4] = { 128, 0, 0, 0 }, nada[4] = { 0, 0, 0, 0 };
    unsigned char vecino[4] = { 192, 168, 1, 7 }, fuera[4] = { 8, 8, 8, 8 }, lo[4] = { 127, 0, 0, 1 };
    CK(httpd_peer_allowed(vecino, ip, m24), "vecino de la red local: si");
    CK(!httpd_peer_allowed(fuera, ip, m24), "Internet: no");
    CK(!httpd_peer_allowed(fuera, ip, mala), "mascara 128.0.0.0: Internet sigue sin entrar");
    CK(!httpd_peer_allowed(vecino, ip, mala), "mascara 128.0.0.0: nadie de fuera de la maquina");
    CK(!httpd_peer_allowed(vecino, ip, nada), "sin mascara: nadie");
    CK(httpd_peer_allowed(lo, ip, mala), "la propia maquina: si");
}

int main(void)
{
    prueba_dns();
    prueba_dhcp();
    prueba_panel();
    printf("test_red_hostil_340: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
