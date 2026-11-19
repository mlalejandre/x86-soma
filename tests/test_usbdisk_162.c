/* SOMA_162_T480S: prueba en el anfitrion de src/arch/x86_64/usbdisk_core.h (la parte pura del disco del sistema en el pendrive).
 * - MBR como el que deja ejecutar.py (diskutil en el Mac): p1 FAT32 "SOMA", p2 FAT32 "SOMADATA", p3 FAT32 "DATOS": solo p2 se
 *   reconoce como SOMADATA; un MBR protector de GPT se rechaza; particiones fuera del disco o diminutas se ignoran.
 * - La marca identifica particion y pendrive: otra firma de disco u otro inicio no la aceptan.
 * - Traduccion de sectores: con millones de peticiones aleatorias (incluidas las de los bordes y desbordamientos de 64 bits),
 *   ninguna sale de la particion; todas las validas se aceptan.
 * - MBR mutados al azar: nunca devuelve una particion que se salga del disco. Uso: test_usbdisk_162 [iteraciones] */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbdisk_core.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FALLO %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static uint64_t rs = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static void entry(uint8_t *mbr, int i, uint8_t type, uint32_t lba, uint32_t n)
{
    uint8_t *e = mbr + 0x1BE + 16 * i;
    memset(e, 0, 16); e[4] = type; ud_put32(e + 8, lba); ud_put32(e + 12, n);
}
static void fat32_boot(uint8_t *s, const char *label)
{
    memset(s, 0, 512);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90; memcpy(s + 3, "BSD  4.4", 8);
    char l[12]; memset(l, ' ', 11); l[11] = 0; memcpy(l, label, strlen(label) > 11 ? 11 : strlen(label));
    memcpy(s + 0x47, l, 11); memcpy(s + 0x52, "FAT32   ", 8);
    s[510] = 0x55; s[511] = 0xAA;
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    const uint64_t disk = 124600000000ULL / 512;                       /* el Lexar de 124,6 GB del usuario */
    uint8_t mbr[512] = { 0 }, s[512];
    entry(mbr, 0, 0x0C, 2048, 2097152);                                 /* SOMA 1 GiB */
    entry(mbr, 1, 0x0C, 2048 + 2097152, 8388608);                       /* SOMADATA 4 GiB */
    entry(mbr, 2, 0x0C, 2048 + 2097152 + 8388608, (uint32_t)(disk - 2048 - 2097152 - 8388608));   /* DATOS, el resto */
    ud_put32(mbr + 0x1B8, 0xC0FFEE11); mbr[510] = 0x55; mbr[511] = 0xAA;
    struct ud_part p[4]; uint32_t sig = 0;
    int n = ud_parse_mbr(mbr, disk, p, &sig);
    CHECK(n == 3, "particiones: %d", n);
    CHECK(sig == 0xC0FFEE11, "firma %08x", sig);
    fat32_boot(s, "SOMA"); CHECK(!ud_is_fat_somadata(s), "SOMA tomada por SOMADATA");
    fat32_boot(s, "DATOS"); CHECK(!ud_is_fat_somadata(s), "DATOS tomada por SOMADATA");
    fat32_boot(s, "SOMADATA"); CHECK(ud_is_fat_somadata(s), "SOMADATA no reconocida");
    s[511] = 0; CHECK(!ud_is_fat_somadata(s), "sin 55AA aceptada");
    /* FAT16 (pendrive pequeno) */
    memset(s, 0, 512); memcpy(s + 0x2B, "SOMADATA   ", 11); memcpy(s + 0x36, "FAT16   ", 8); s[510] = 0x55; s[511] = 0xAA;
    CHECK(ud_is_fat_somadata(s), "FAT16 SOMADATA no reconocida");
    /* GPT y sin MBR */
    uint8_t g[512] = { 0 }; entry(g, 0, 0xEE, 1, 0xFFFFFFFF); g[510] = 0x55; g[511] = 0xAA;
    CHECK(ud_parse_mbr(g, disk, p, 0) == -2, "GPT no rechazado");
    memset(g, 0, 512); CHECK(ud_parse_mbr(g, disk, p, 0) == -1, "sin MBR aceptado");
    /* fuera del disco, diminuta, extendida */
    memset(g, 0, 512); g[510] = 0x55; g[511] = 0xAA;
    entry(g, 0, 0x0C, 2048, 1000); entry(g, 1, 0x0C, (uint32_t)(disk - 100), 50000); entry(g, 2, 0x05, 4096, 100000); entry(g, 3, 0x0C, 0, 100000);
    CHECK(ud_parse_mbr(g, disk, p, 0) == 0, "aceptadas particiones invalidas");
    /* marca */
    ud_mark_build(s, p[1].lba, 8388608, sig);
    n = ud_parse_mbr(mbr, disk, p, &sig);
    CHECK(ud_mark_ok(s, p[1].lba, p[1].n, sig), "marca propia rechazada");
    CHECK(!ud_mark_ok(s, p[1].lba, p[1].n, sig ^ 1), "marca de otro pendrive aceptada");
    CHECK(!ud_mark_ok(s, p[1].lba + 1, p[1].n, sig), "marca de otro inicio aceptada");
    CHECK(!ud_mark_ok(s, p[1].lba, p[1].n - 1, sig), "marca de otro tamano aceptada");
    CHECK(!ud_is_fat_somadata(s), "la marca parece un FAT nuevo (se volveria a reclamar)");
    /* traduccion: limites */
    uint64_t pl = p[1].lba, pn = p[1].n, ds = ud_data_sectors(pn), a;
    CHECK(ds == pn - UD_DATA_OFF, "sectores de datos %llu", (unsigned long long)ds);
    CHECK(ud_data_sectors(100ULL << 30) == UD_MAX_SECT, "sin limite de 8 GiB");
    CHECK(ud_map(pl, pn, ds, 0, 1, &a) == 0 && a == pl + UD_DATA_OFF, "sector 0");
    CHECK(ud_map(pl, pn, ds, ds - 1, 1, &a) == 0 && a == pl + pn - 1, "ultimo sector");
    CHECK(ud_map(pl, pn, ds, ds, 1, &a) == -1, "uno de mas");
    CHECK(ud_map(pl, pn, ds, ds - 1, 2, &a) == -1, "cruza el final");
    CHECK(ud_map(pl, pn, ds, UINT64_MAX, 1, &a) == -1, "desbordamiento");
    CHECK(ud_map(pl, pn, ds, 5, 0, &a) == -1, "cero sectores");
    long ok = 0, rej = 0;
    for (long i = 0; i < iters * 50; i++) {
        uint64_t lba = (rnd() & 1) ? rnd() % (ds + 300) : rnd(); uint32_t k = (uint32_t)(rnd() % 300);
        if (rnd() % 7 == 0) lba = ds - (rnd() % 200);
        int r = ud_map(pl, pn, ds, lba, k, &a);
        int valid = k > 0 && lba < ds && k <= ds - lba;
        CHECK(r == (valid ? 0 : -1), "lba %llu k %u", (unsigned long long)lba, k);
        if (r == 0) { CHECK(a >= pl + UD_DATA_OFF && a + k <= pl + pn, "fuera de la particion"); ok++; } else rej++;
        if (fails > 20) break;
    }
    /* MBR mutados */
    for (long i = 0; i < iters && fails < 20; i++) {
        uint8_t m[512]; memcpy(m, mbr, 512);
        int nm = 1 + (int)(rnd() % 12);
        for (int j = 0; j < nm; j++) m[0x1BE + rnd() % 66] = (uint8_t)rnd();
        uint64_t dsz = 1 + rnd() % (disk * 2);
        int c = ud_parse_mbr(m, dsz, p, 0);
        CHECK(c >= -2 && c <= 4, "resultado %d", c);
        for (int j = 0; j < c; j++) CHECK(p[j].lba > 0 && (uint64_t)p[j].lba + p[j].n <= dsz && p[j].n >= UD_MIN_PART, "particion fuera del disco");
    }
    if (fails) { printf("usbdisk: %d fallos\n", fails); return 1; }
    printf("usbdisk: MBR, SOMADATA, marca, %ld peticiones dentro y %ld rechazadas, %ld MBR mutados: todo correcto\n", ok, rej, iters);
    return 0;
}
