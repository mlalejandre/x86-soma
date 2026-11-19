/* SOMA_175A_T480S: prueba en el anfitrion del lector de firmware iwlwifi (src/core/iwl_fw.c).
 * Con el archivo real (firmware/iwlwifi-8265-36.ucode, si existe) comprueba su estructura; ademas, un firmware sintetico,
 * archivos cortados y 200000 entradas al azar. cc -Isrc/core tests/test_iwl_fw_175.c src/core/iwl_fw.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iwl_fw.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint32_t put_tlv(uint8_t *b, uint32_t off, uint32_t t, const void *data, uint32_t l)
{
    memcpy(b + off, &t, 4); memcpy(b + off + 4, &l, 4); memcpy(b + off + 8, data, l);
    uint32_t s = (l + 3) & ~3u; memset(b + off + 8 + l, 0, s - l);
    return off + 8 + s;
}
static uint32_t put_sec(uint8_t *b, uint32_t off, uint32_t t, uint32_t addr, uint32_t n)
{
    uint8_t tmp[64]; memcpy(tmp, &addr, 4); for (uint32_t i = 0; i < n; i++) tmp[4 + i] = (uint8_t)(i + addr);
    return put_tlv(b, off, t, tmp, 4 + n);
}

int main(int argc, char **argv)
{
    struct iwl_fw_info fi; const char *why = 0;
    /* firmware sintetico: INIT con CPU1 (2), separador, CPU2 (1), separador de paginacion; RT igual */
    static uint8_t b[4096]; memset(b, 0, sizeof b);
    uint32_t magic = 0x0A4C5749u; memcpy(b + 4, &magic, 4); memcpy(b + 8, "release/prueba", 14);
    uint32_t off = 88, v = 0x22;
    off = put_tlv(b, off, 23, &v, 4);
    for (int img = 19; img <= 20; img++) {
        off = put_sec(b, off, (uint32_t)img, 0x00404000, 16); off = put_sec(b, off, (uint32_t)img, 0x00800000, 8);
        off = put_sec(b, off, (uint32_t)img, IWL_SEP_CPU1_CPU2, 4); off = put_sec(b, off, (uint32_t)img, 0x00405000, 12);
        off = put_sec(b, off, (uint32_t)img, IWL_SEP_PAGING, 0); off = put_sec(b, off, (uint32_t)img, 0x01000000, 20);
    }
    v = 4096; off = put_tlv(b, off, 32, &v, 4);
    uint32_t api[2] = { 0, 0x12345678 }; off = put_tlv(b, off, 29, api, 8);
    CHECK(iwl_fw_parse(b, off, &fi, &why) == 0, "firmware sintetico aceptado");
    CHECK(!strcmp(fi.human, "release/prueba") && fi.phy_sku == 0x22 && fi.paging_size == 4096 && fi.api[0] == 0x12345678, "metadatos");
    int a, e;
    CHECK(iwl_fw_cpu_range(&fi.img[IWL_IMG_INIT], 1, &a, &e) == 0 && a == 0 && e == 2, "CPU1: secciones 0 y 1");
    CHECK(iwl_fw_cpu_range(&fi.img[IWL_IMG_INIT], 2, &a, &e) == 0 && a == 3 && e == 4, "CPU2: seccion 3 (para en la paginacion)");
    CHECK(fi.img[IWL_IMG_INIT].sec[0].data[0] == 0x00 && fi.img[IWL_IMG_INIT].sec[3].len == 12, "datos de las secciones");
    for (uint32_t cut = 0; cut < off; cut += 7) iwl_fw_parse(b, cut, &fi, &why);     /* cortado en cualquier punto: sin salirse */
    CHECK(iwl_fw_parse(b, off - 5, &fi, &why) != 0, "archivo cortado: rechazo");
    b[4] = 'X'; CHECK(iwl_fw_parse(b, off, &fi, &why) != 0 && why, "sin la marca IWL: rechazo"); b[4] = 0x49;
    /* el archivo real */
    const char *path = argc > 1 ? argv[1] : "firmware/iwlwifi-8265-36.ucode";
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *d = malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) n = 0; fclose(f);
        int r = iwl_fw_parse(d, (uint32_t)n, &fi, &why);
        CHECK(r == 0, why ? why : "firmware real");
        if (r == 0) {
            int a1, e1, a2, e2;
            iwl_fw_cpu_range(&fi.img[IWL_IMG_INIT], 1, &a1, &e1); iwl_fw_cpu_range(&fi.img[IWL_IMG_INIT], 2, &a2, &e2);
            CHECK(fi.img[IWL_IMG_INIT].n >= 6 && e1 > a1 && e2 > a2, "INIT con secciones para las dos CPU");
            CHECK(fi.paging_size > 0 && fi.paging_size < (1u << 20), "paginacion de la CPU2");
            uint32_t tot = 0; for (int i = a1; i < e1; i++) tot += fi.img[IWL_IMG_INIT].sec[i].len; for (int i = a2; i < e2; i++) tot += fi.img[IWL_IMG_INIT].sec[i].len;
            printf("calibracion INIT: flujo 0x%08X eventos 0x%08X\n", fi.calib_flow[1], fi.calib_event[1]);
            printf("firmware real: %s, %u bloques TLV, INIT %d secciones (CPU1 %d-%d, CPU2 %d-%d, %u bytes a cargar), RT %d, paginacion %u, PHY_SKU 0x%08X\n",
                   fi.human, fi.tlvs, fi.img[IWL_IMG_INIT].n, a1, e1 - 1, a2, e2 - 1, tot, fi.img[IWL_IMG_RT].n, fi.paging_size, fi.phy_sku);
        }
        /* cortes del archivo real */
        for (long cut = 0; cut < n; cut += 9973) iwl_fw_parse(d, (uint32_t)cut, &fi, &why);
        free(d);
    } else printf("(sin %s: se prueba solo el sintetico)\n", path);
    srand(175);
    for (int it = 0; it < 200000; it++) {
        uint32_t n = (uint32_t)(rand() % 700);
        uint8_t *z = malloc(n ? n : 1);
        for (uint32_t i = 0; i < n; i++) z[i] = (uint8_t)(rand() % 3 ? rand() : 0);
        if (n > 8 && (it & 1)) { memset(z, 0, 4); memcpy(z + 4, &magic, 4); }
        iwl_fw_parse(z, n, &fi, &why);
        free(z);
    }
    if (fails) { printf("iwl_fw: %d fallos\n", fails); return 1; }
    printf("iwl_fw: lector de firmware correcto (sintetico, cortes y 200000 entradas al azar)\n");
    return 0;
}
