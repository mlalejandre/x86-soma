/* SOMA_171_T480S: prueba en el anfitrion de src/core/usbnet_proto.c.
 * cc -std=gnu11 -Wall -Wextra -Isrc/core tests/test_usbnet_171.c src/core/usbnet_proto.c -o /tmp/tu && /tmp/tu */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usbnet_proto.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

/* Android con depuracion USB (ADB delante) y anclaje de red por RNDIS (con IAD) */
static const uint8_t cfg_android_rndis[] = {
    0x09,0x02,0x5B,0x00,0x03,0x01,0x00,0xC0,0xFA,
    0x09,0x04,0x00,0x00,0x02,0xFF,0x42,0x01,0x00, 0x07,0x05,0x83,0x02,0x00,0x02,0x00, 0x07,0x05,0x03,0x02,0x00,0x02,0x00,   /* ADB */
    0x08,0x0B,0x01,0x02,0xE0,0x01,0x03,0x05,                                                                                  /* IAD */
    0x09,0x04,0x01,0x00,0x01,0xE0,0x01,0x03,0x05, 0x05,0x24,0x00,0x10,0x01, 0x05,0x24,0x01,0x00,0x02, 0x04,0x24,0x02,0x00,
    0x05,0x24,0x06,0x01,0x02, 0x07,0x05,0x81,0x03,0x08,0x00,0x09,
    0x09,0x04,0x02,0x00,0x02,0x0A,0x00,0x00,0x06, 0x07,0x05,0x82,0x02,0x00,0x02,0x00, 0x07,0x05,0x01,0x02,0x00,0x02,0x00,
};
/* Pixel: NCM (datos: ajuste 0 sin extremos, ajuste 1 con dos bulk) */
static const uint8_t cfg_pixel_ncm[] = {
    0x09,0x02,0x6B,0x00,0x02,0x01,0x00,0x80,0xFA,
    0x08,0x0B,0x00,0x02,0x02,0x0D,0x00,0x00,
    0x09,0x04,0x00,0x00,0x01,0x02,0x0D,0x00,0x04, 0x05,0x24,0x00,0x10,0x01, 0x05,0x24,0x06,0x00,0x01,
    0x0D,0x24,0x0F,0x05,0x00,0x00,0x00,0x00,0xEA,0x05,0x00,0x00,0x00, 0x06,0x24,0x1A,0x00,0x01,0x00,
    0x07,0x05,0x81,0x03,0x10,0x00,0x09,
    0x09,0x04,0x01,0x00,0x00,0x0A,0x00,0x01,0x00,
    0x09,0x04,0x01,0x01,0x02,0x0A,0x00,0x01,0x00, 0x07,0x05,0x82,0x02,0x00,0x02,0x00, 0x07,0x05,0x02,0x02,0x00,0x02,0x00,
};
/* adaptador USB-Ethernet en su configuracion ECM (USB 3: extremos de 1024) */
static const uint8_t cfg_ecm[] = {
    0x09,0x02,0x5F,0x00,0x02,0x02,0x00,0xA0,0x32,
    0x09,0x04,0x00,0x00,0x01,0x02,0x06,0x00,0x05, 0x05,0x24,0x00,0x10,0x01, 0x05,0x24,0x06,0x00,0x01,
    0x0D,0x24,0x0F,0x03,0x00,0x00,0x00,0x00,0xEA,0x05,0x00,0x00,0x00, 0x07,0x05,0x83,0x03,0x10,0x00,0x08,
    0x09,0x04,0x01,0x00,0x00,0x0A,0x00,0x00,0x00,
    0x09,0x04,0x01,0x01,0x02,0x0A,0x00,0x00,0x00, 0x07,0x05,0x81,0x02,0x00,0x04,0x00, 0x07,0x05,0x02,0x02,0x00,0x04,0x00,
};
static const uint8_t cfg_msc[] = { 0x09,0x02,0x20,0x00,0x01,0x01,0x00,0x80,0x32, 0x09,0x04,0x00,0x00,0x02,0x08,0x06,0x50,0x00,
                                   0x07,0x05,0x81,0x02,0x00,0x02,0x00, 0x07,0x05,0x02,0x02,0x00,0x02,0x00 };

static uint8_t got[16][1600]; static uint32_t gotn[16]; static int ngot;
static void collect(const uint8_t *f, uint32_t n, void *u) { (void)u; if (ngot < 16) { memcpy(got[ngot], f, n); gotn[ngot++] = n; } }
static void sink(const uint8_t *f, uint32_t n, void *u) { (void)u; volatile uint8_t x = f[0] ^ f[n - 1]; (void)x; }

int main(void)
{
    struct usbnet_fn f;
    CHECK(usbnet_find(cfg_android_rndis, sizeof cfg_android_rndis, &f) == 1, "Android RNDIS reconocido");
    CHECK(f.kind == USBNET_RNDIS && f.comm_if == 1 && f.data_if == 2 && f.in_addr == 0x82 && f.out_addr == 0x01 && f.in_mps == 512, "RNDIS: interfaces 1/2 y extremos (ADB no confunde)");
    CHECK(usbnet_find(cfg_pixel_ncm, sizeof cfg_pixel_ncm, &f) == 1, "Pixel NCM reconocido");
    CHECK(f.kind == USBNET_NCM && f.comm_if == 0 && f.data_if == 1 && f.data_alt == 1 && f.in_addr == 0x82 && f.out_addr == 0x02 && f.mac_str == 5 && f.max_seg == 1514, "NCM: ajuste 1, MAC en la cadena 5");
    CHECK(usbnet_find(cfg_ecm, sizeof cfg_ecm, &f) == 1, "ECM reconocido");
    CHECK(f.kind == USBNET_ECM && f.data_alt == 1 && f.in_mps == 1024 && f.out_addr == 0x02 && f.mac_str == 3, "ECM: extremos USB 3 y MAC en la cadena 3");
    CHECK(usbnet_find(cfg_msc, sizeof cfg_msc, &f) == 0, "un disco no es una red");
    /* cadena de la MAC */
    uint8_t s[26] = { 26, 3 }; const char *hex = "02A1b2C3d4E5";
    for (int i = 0; i < 12; i++) { s[2 + 2 * i] = (uint8_t)hex[i]; s[3 + 2 * i] = 0; }
    uint8_t mac[6];
    CHECK(usbnet_mac_from_string(s, sizeof s, mac) == 0 && mac[0] == 0x02 && mac[1] == 0xA1 && mac[5] == 0xE5, "MAC desde la cadena USB");
    s[4] = 'G'; CHECK(usbnet_mac_from_string(s, sizeof s, mac) != 0, "cadena de MAC invalida");
    /* RNDIS: mensajes de control */
    uint8_t b[2048], r[128];
    CHECK(rndis_build_init(b, 1, 16384) == 24 && b[0] == 2 && b[20] == 0x00 && b[21] == 0x40, "INIT");
    memset(r, 0, sizeof r);
    r[0] = 0x02; r[3] = 0x80; r[4] = 52; r[8] = 1; r[32] = 0x00; r[33] = 0x40; r[36] = 3;            /* INIT_CMPLT: 16384, alineacion 2^3 */
    uint32_t mx = 0, al = 0;
    CHECK(rndis_parse_init_cmplt(r, 52, 1, &mx, &al) == 0 && mx == 16384 && al == 3, "INIT_CMPLT");
    CHECK(rndis_parse_init_cmplt(r, 52, 2, &mx, &al) != 0, "id de peticion distinto: rechazo");
    CHECK(rndis_build_query(b, 2, RNDIS_OID_PERM_ADDR, 48) == 76 && b[16] == 48 && b[20] == 20, "QUERY con bufer de 48 bytes (como Linux)");
    memset(r, 0, sizeof r);
    r[0] = 0x04; r[3] = 0x80; r[4] = 30; r[8] = 2; r[16] = 6; r[20] = 16;                            /* QUERY_CMPLT: 6 bytes en 8+16 */
    memcpy(r + 24, "\x02\x11\x22\x33\x44\x55", 6);
    const uint8_t *info; uint32_t il;
    CHECK(rndis_parse_cmplt(r, 30, RNDIS_MSG_QUERY, 2, &info, &il) == 0 && il == 6 && info[0] == 2 && info[5] == 0x55, "QUERY_CMPLT con la MAC");
    r[16] = 200; CHECK(rndis_parse_cmplt(r, 30, RNDIS_MSG_QUERY, 2, &info, &il) != 0, "informacion fuera del mensaje: rechazo");
    r[16] = 6; r[12] = 1; CHECK(rndis_parse_cmplt(r, 30, RNDIS_MSG_QUERY, 2, &info, &il) != 0, "estado de error: rechazo");
    CHECK(rndis_build_set32(b, 3, RNDIS_OID_FILTER, 0x0B) == 32 && b[28] == 0x0B, "SET del filtro");
    /* RNDIS: datos (dos paquetes en una transferencia) */
    uint8_t fr1[60], fr2[1514];
    for (int i = 0; i < 60; i++) fr1[i] = (uint8_t)i;
    for (int i = 0; i < 1514; i++) fr2[i] = (uint8_t)(i * 7);
    uint32_t n1 = rndis_wrap(b, fr1, 60), n2 = rndis_wrap(b + n1, fr2, 1514);
    ngot = 0;
    CHECK(rndis_unwrap(b, n1 + n2, collect, 0) == 2 && gotn[0] == 60 && gotn[1] == 1514 && !memcmp(got[1], fr2, 1514), "RNDIS: dos tramas en una transferencia");
    ngot = 0; CHECK(rndis_unwrap(b, n1 + n2 - 1, collect, 0) == 1, "transferencia cortada: solo la trama completa");
    /* NCM */
    uint8_t pr[28] = { 28, 0, 1, 0, 0x00, 0x40, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0x00, 0x40, 0, 0, 4, 0, 0, 0, 4, 0, 1, 0 };
    struct ncm_params np;
    CHECK(ncm_parse_params(pr, 28, &np) == 0 && np.out_max == 16384 && np.out_div == 4 && np.out_align == 4, "GET_NTB_PARAMETERS");
    np.out_div = 32; np.out_rem = 2;
    uint32_t nt = ncm_build_ntb(b, sizeof b, &np, 7, fr2, 1514);
    CHECK(nt > 1514 && nt <= np.out_max, "NTB16 construido");
    uint32_t dgi = (uint32_t)(b[12 + 8] | b[12 + 9] << 8);
    CHECK(dgi % 32 == 2, "la trama respeta divisor y resto");
    ngot = 0; CHECK(ncm_parse_ntb(b, nt, collect, 0) == 1 && gotn[0] == 1514 && !memcmp(got[0], fr2, 1514), "NTB16: ida y vuelta");
    /* NTB con dos tramas y dos NDP encadenados */
    uint8_t m[4096]; memset(m, 0, sizeof m);
    memcpy(m, "NCMH", 4); m[4] = 12; m[8] = 0x00; m[9] = 0x08; m[10] = 16;
    memcpy(m + 16, "NCM0", 4); m[20] = 16; m[22] = 64;  m[24] = 0x00; m[25] = 0x01; m[26] = 60;          /* trama en 256, 60 bytes; siguiente NDP en 64 */
    memcpy(m + 64, "NCM0", 4); m[68] = 16; m[72] = 0x00; m[73] = 0x02; m[74] = 100;                       /* trama en 512, 100 bytes */
    memset(m + 256, 0xAA, 60);
    memset(m + 512, 0xBB, 100);
    ngot = 0; CHECK(ncm_parse_ntb(m, 2048, collect, 0) == 2 && gotn[0] == 60 && gotn[1] == 100 && got[1][0] == 0xBB, "NTB16 con dos NDP");
    m[66 + 4] = 0; m[64 + 6] = 16;                                                                         /* NDP que apunta hacia atras: no hay bucle */
    ngot = 0; (void)ncm_parse_ntb(m, 2048, collect, 0);
    /* datos al azar: los analizadores no se salen de los limites (con ASan) */
    srand(171);
    for (int it = 0; it < 300000; it++) {
        uint32_t n = (uint32_t)(rand() % 600);
        uint8_t *z = malloc(n ? n : 1);
        for (uint32_t i = 0; i < n; i++) z[i] = (uint8_t)rand();
        if (n > 12 && (it & 3) == 0) { memcpy(z, "NCMH", 4); z[4] = 12; z[10] = (uint8_t)(rand() % 64); }
        if (n > 44 && (it & 3) == 1) { z[0] = 1; z[1] = z[2] = z[3] = 0; }
        if (n > 4 && (it & 3) == 2) { z[1] = 2; }
        struct usbnet_fn ff; struct ncm_params pp; const uint8_t *ii; uint32_t ll, a1, a2;
        usbnet_find(z, n, &ff); ncm_parse_ntb(z, n, sink, 0); rndis_unwrap(z, n, sink, 0);
        rndis_parse_cmplt(z, n, RNDIS_MSG_QUERY, 0, &ii, &ll); rndis_parse_init_cmplt(z, n, 0, &a1, &a2); ncm_parse_params(z, n, &pp);
        usbnet_mac_from_string(z, n, mac);
        free(z);
    }
    if (fails) { printf("usbnet: %d fallos\n", fails); return 1; }
    printf("usbnet: RNDIS (Android con ADB), NCM (Pixel) y ECM reconocidos; RNDIS, NTB16 y MAC correctos; 300000 entradas al azar sin problemas\n");
    return 0;
}
