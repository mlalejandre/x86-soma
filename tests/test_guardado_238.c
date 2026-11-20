/* SOMA_238_GUARDADO: prueba en el anfitrion de
 *  - ud_mark_loose (usbdisk_core.h): una SOMADATA nuestra se reconoce aunque el tamano o la firma del MBR hayan cambiado
 *    (antes se ignoraba y SOMA trabajaba en memoria: no se guardaba nada), y nunca se confunde con otra cosa;
 *  - fat_contiguous (fat.c): el fichero SOMADATA.IMG solo vale como disco si sus clusters van seguidos.
 * Uso: test_guardado_238 <imagen de tests/mk_informe_228.py> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "usbdisk_core.h"
#include "fat.h"

static int fails, checks;
#define CHECK(c, m) do { checks++; if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint8_t *img; static uint64_t img_sec;
static int rdm(void *c, uint64_t lba, uint32_t n, void *b)
{
    (void)c;
    if (lba > img_sec || n > img_sec - lba) return -1;
    memcpy(b, img + lba * 512, (size_t)n * 512);
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t s[512];
    /* marca */
    ud_mark_build(s, 2099200, 8388608, 0xC884006Bu);
    CHECK(ud_mark_ok(s, 2099200, 8388608, 0xC884006Bu), "marca exacta");
    CHECK(!ud_mark_ok(s, 2099200, 10483712, 0xC884006Bu), "estricta: otro tamano no vale");
    CHECK(!ud_mark_ok(s, 2099200, 8388608, 0x12345678u), "estricta: otra firma no vale");
    CHECK(ud_mark_loose(s, 2099200), "flexible: mismo sitio, aunque cambien tamano y firma");
    CHECK(!ud_mark_loose(s, 2048), "flexible: otro sitio no vale");
    uint8_t f[512]; memcpy(f, s, 512); f[3] ^= 1;
    CHECK(!ud_mark_loose(f, 2099200), "flexible: sin el texto de la marca no vale");
    memset(f, 0, 512);
    CHECK(!ud_mark_loose(f, 0), "flexible: un sector a cero nunca es la marca");
    CHECK(!ud_is_fat_somadata(s), "la marca no parece un FAT SOMADATA");

    /* fat_contiguous */
    if (argc < 2) { printf("uso: test_guardado_238 <imagen>\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { printf("no se puede abrir %s\n", argv[1]); return 2; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    img = malloc((size_t)sz); img_sec = (uint64_t)sz / 512;
    if (!img || fread(img, 1, (size_t)sz, fp) != (size_t)sz) { printf("lectura\n"); return 2; }
    fclose(fp);
    static struct fat_vol v;
    struct fat_io io = { rdm, 0, img_sec };
    CHECK(fat_mount(&v, &io) == 0, "monta la imagen");
    uint64_t lba = 0, n = 0;
    CHECK(fat_contiguous(&v, "SOMADATA.IMG", &lba, &n) == FAT_E_NOENT, "sin el fichero: NOENT");
    CHECK(fat_contiguous(&v, "EFI", &lba, &n) == FAT_E_ISDIR, "un directorio no es un disco");
    CHECK(fat_contiguous(&v, "ZZ.BIN", &lba, &n) == 0, "fichero contiguo");
    CHECK(n == 76800 / 512, "sectores enteros del fichero");
    {   /* el primer sector que devuelve es el contenido del fichero */
        uint8_t b[512]; rdm(0, lba, 1, b); int ok = 1;
        for (int i = 0; i < 512; i++) if (b[i] != (uint8_t)i) ok = 0;
        CHECK(ok, "el sector devuelto es el principio de ZZ.BIN");
        rdm(0, lba + n - 1, 1, b); ok = 1;
        for (int i = 0; i < 512; i++) if (b[i] != (uint8_t)((76800 - 512 + i) % 256)) ok = 0;
        CHECK(ok, "y el ultimo, su final");
    }
    struct fat_dirent d;
    CHECK(fat_stat(&v, "ZZ.BIN", &d) == 0 && d.cluster >= 2, "cluster de ZZ.BIN");
    uint32_t c0 = d.cluster;
    for (uint32_t k = 0; k < v.nfats; k++) {                 /* fragmentarlo: el 2o cluster salta 5 mas alla */
        uint8_t *e = img + ((uint64_t)v.fat_lba + (uint64_t)k * v.fat_sectors) * 512 + (uint64_t)c0 * 4;
        uint32_t nx = c0 + 6; memcpy(e, &nx, 4);
    }
    v.fcs = 0xFFFFFFFFu;                                       /* olvidar la cache de la FAT */
    CHECK(fat_mount(&v, &io) == 0, "vuelve a montar");
    CHECK(fat_contiguous(&v, "ZZ.BIN", &lba, &n) == FAT_E_UNSUP, "fragmentado: no vale como disco");
    for (uint32_t k = 0; k < v.nfats; k++) {                 /* cadena cortada: mas corta que el fichero */
        uint8_t *e = img + ((uint64_t)v.fat_lba + (uint64_t)k * v.fat_sectors) * 512 + (uint64_t)c0 * 4;
        uint32_t nx = 0x0FFFFFFFu; memcpy(e, &nx, 4);
    }
    CHECK(fat_mount(&v, &io) == 0, "monta otra vez");
    CHECK(fat_contiguous(&v, "ZZ.BIN", &lba, &n) == FAT_E_CORRUPT, "cadena corta: corrupto");
    CHECK(fat_contiguous(0, "ZZ.BIN", &lba, &n) == FAT_E_ARG, "argumentos nulos");
    free(img);
    printf("[guardado_238] %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
