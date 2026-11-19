/* SOMA_156_T480S: prueba del analizador SMBIOS (src/core/hwinfo.c) en el anfitrion.
 * 1) Tabla sintetica con la forma de la de un ThinkPad T480s (tipos 0, 1, 2, 4, 16, 17 x2, 127): cada campo se lee bien.
 * 2) Puntos de entrada _SM3_ (64 bits) y _SM_ (32 bits), con suma de comprobacion correcta e incorrecta.
 * 3) Miles de mutaciones aleatorias y truncamientos con ASan/UBSan: nunca se lee fuera de los limites.
 * Uso: test_smbios_156 [iteraciones]   (por defecto 20000) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwinfo.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint8_t tab[1024]; static size_t tl;
static void put(const void *d, size_t n) { memcpy(tab + tl, d, n); tl += n; }
static void strs(const char *const *s, int n)
{
    if (!n) { tab[tl++] = 0; tab[tl++] = 0; return; }
    for (int i = 0; i < n; i++) put(s[i], strlen(s[i]) + 1);
    tab[tl++] = 0;
}
static void build(void)
{
    tl = 0;
    { uint8_t h[0x12] = { 0, 0x12, 0x00, 0x00, 1, 2, 0, 0xF0, 3 };                               /* tipo 0: BIOS */
      put(h, sizeof h); const char *s[] = { "LENOVO", "N22ET80W (1.57 )   ", "02/28/2024" }; strs(s, 3); }
    { uint8_t h[0x1B] = { 1, 0x1B, 0x01, 0x00, 1, 2, 3, 4 }; h[0x19] = 5; h[0x1A] = 5;           /* tipo 1: sistema */
      put(h, sizeof h); const char *s[] = { "LENOVO", "20L7001UGE", "ThinkPad T480s", "PF1XXXXX", "ThinkPad T480s" }; strs(s, 5); }
    { uint8_t h[0x0F] = { 2, 0x0F, 0x02, 0x00, 1, 2 };                                            /* tipo 2: placa */
      put(h, sizeof h); const char *s[] = { "LENOVO", "20L7001UGE" }; strs(s, 2); }
    { uint8_t h[0x30] = { 4, 0x30, 0x04, 0x00 }; h[0x10] = 1;                                    /* tipo 4: procesador */
      h[0x14] = (uint8_t)3600; h[0x15] = 3600 >> 8; h[0x16] = (uint8_t)1800; h[0x17] = 1800 >> 8;
      h[0x23] = 4; h[0x25] = 8;
      put(h, sizeof h); const char *s[] = { "Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz" }; strs(s, 1); }
    { uint8_t h[0x17] = { 16, 0x17, 0x05, 0x00 }; uint32_t kb = 32u * 1024 * 1024;               /* tipo 16: 32 GiB max */
      memcpy(h + 7, &kb, 4); put(h, sizeof h); strs(0, 0); }
    for (int k = 0; k < 2; k++) {                                                                  /* tipo 17: 8 GB + 16 GB */
        uint8_t h[0x28] = { 17, 0x28, (uint8_t)(6 + k), 0x00 };
        uint16_t sz = k ? 16384 : 8192; memcpy(h + 0x0C, &sz, 2);
        h[0x10] = 1; h[0x12] = 0x1A; uint16_t sp = 2400; memcpy(h + 0x15, &sp, 2);
        h[0x1A] = 2; uint16_t cs = 2400; memcpy(h + 0x20, &cs, 2);
        put(h, sizeof h);
        const char *s[] = { k ? "ChannelB-DIMM0" : "ChannelA-DIMM0", k ? "M471A2K43CB1-CRC" : "M471A1K43BB1-CRC" }; strs(s, 2);
    }
    { uint8_t h[4] = { 127, 4, 0xFF, 0xFF }; put(h, 4); strs(0, 0); }
}
static void fix_sum(uint8_t *ep, int len) { uint8_t s = 0; ep[len == 24 ? 5 : 4] = 0; for (int i = 0; i < len; i++) s = (uint8_t)(s + ep[i]); ep[len == 24 ? 5 : 4] = (uint8_t)-s; }

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    build();
    struct hw_smbios o;
    CHECK(hw_smbios_parse(tab, tl, 3, 0, &o) == 0, "tabla valida");
    CHECK(o.nstructs == 8, "8 estructuras");
    CHECK(!strcmp(o.bios_vendor, "LENOVO") && !strcmp(o.bios_version, "N22ET80W (1.57 )") && !strcmp(o.bios_date, "02/28/2024"), "BIOS (sin espacios finales)");
    CHECK(!strcmp(o.sys_product, "20L7001UGE") && !strcmp(o.sys_version, "ThinkPad T480s") && !strcmp(o.sys_family, "ThinkPad T480s"), "sistema");
    CHECK(!strcmp(o.board_product, "20L7001UGE"), "placa");
    CHECK(!strcmp(o.cpu_name, "Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz") && o.cpu_max_mhz == 3600 && o.cpu_cores == 4 && o.cpu_threads == 8, "procesador");
    CHECK(o.mem_max_mb == 32768, "capacidad maxima 32 GiB");
    CHECK(o.nmem == 2 && o.mem[0].size_mb == 8192 && o.mem[1].size_mb == 16384 && o.mem[1].mhz == 2400 && o.mem[0].type == 0x1A, "modulos");
    CHECK(!strcmp(o.mem[1].locator, "ChannelB-DIMM0") && !strcmp(o.mem[0].part, "M471A1K43BB1-CRC"), "cadenas de los modulos");
    CHECK(!strcmp(hw_smbios_memtype(o.mem[0].type), "DDR4"), "tipo DDR4");

    /* puntos de entrada */
    uint8_t ep3[24] = { '_', 'S', 'M', '3', '_', 0, 24, 3, 2, 0, 1, 0 };
    uint32_t ml = (uint32_t)tl; uint64_t ad = 0x7B6E2000ULL; memcpy(ep3 + 12, &ml, 4); memcpy(ep3 + 16, &ad, 8); fix_sum(ep3, 24);
    uint64_t tp; uint32_t tlen; uint8_t ma, mi;
    CHECK(hw_smbios_entry(ep3, 24, &tp, &tlen, &ma, &mi) == 0 && tp == ad && tlen == tl && ma == 3 && mi == 2, "entrada _SM3_");
    ep3[9] ^= 1; CHECK(hw_smbios_entry(ep3, 24, &tp, &tlen, &ma, &mi) != 0, "_SM3_ con suma mala rechazada");
    uint8_t ep2[31] = { '_', 'S', 'M', '_', 0, 31, 2, 8 }; memcpy(ep2 + 16, "_DMI_", 5);
    uint16_t l2 = (uint16_t)tl; uint32_t a2 = 0x000E1000; memcpy(ep2 + 22, &l2, 2); memcpy(ep2 + 24, &a2, 4); fix_sum(ep2, 31);
    CHECK(hw_smbios_entry(ep2, 31, &tp, &tlen, &ma, &mi) == 0 && tp == a2 && ma == 2 && mi == 8, "entrada _SM_");
    CHECK(hw_smbios_entry(ep2, 20, &tp, &tlen, &ma, &mi) != 0, "entrada truncada rechazada");

    /* robustez: truncamientos y mutaciones (ASan avisa de cualquier lectura fuera) */
    for (size_t cut = 0; cut <= tl; cut++) {
        uint8_t *c = malloc(cut ? cut : 1); memcpy(c, tab, cut);
        (void)hw_smbios_parse(c, cut, 3, 0, &o); free(c);
    }
    srand(156);
    for (int it = 0; it < iters; it++) {
        size_t n = tl; uint8_t *c = malloc(n); memcpy(c, tab, n);
        int flips = 1 + rand() % 8;
        for (int f = 0; f < flips; f++) c[rand() % n] = (uint8_t)rand();
        size_t cut = (rand() % 4 == 0) ? (size_t)(rand() % (int)n) : n;
        (void)hw_smbios_parse(c, cut, 3, 0, &o);
        for (size_t i = 0; i < sizeof o.cpu_name && o.cpu_name[i]; i++) CHECK(o.cpu_name[i] >= 32 && o.cpu_name[i] < 127, "cadena saneada");
        uint8_t e[31]; for (int i = 0; i < 31; i++) e[i] = (uint8_t)rand(); memcpy(e, it & 1 ? "_SM3_" : "_SM_", it & 1 ? 5 : 4);
        (void)hw_smbios_entry(e, (size_t)(rand() % 32), &tp, &tlen, &ma, &mi);
        free(c);
        if (fails > 5) break;
    }
    if (fails) { printf("SMBIOS: %d fallos\n", fails); return 1; }
    printf("SMBIOS: analizador correcto (T480s sintetico) y %d mutaciones sin lecturas fuera de limites\n", iters);
    return 0;
}
