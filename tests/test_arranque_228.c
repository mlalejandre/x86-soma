/* SOMA_228_ARRANQUE: prueba en el anfitrion de
 *  - ud_plan_grow (usbdisk_core.h): cuando y cuanto se amplia SOMADATA, con MBR reales y al azar;
 *  - fat_overwrite (fat.c): el informe se escribe SOLO en los sectores de INFORME.TXT, con su tamano intacto.
 * Uso: test_arranque_228 <imagen de tests/mk_informe_228.py> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "usbdisk_core.h"
#include "fat.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static void ent(uint8_t *s, int i, uint8_t t, uint32_t lba, uint32_t n) { uint8_t *e = s + 0x1BE + 16 * i; e[4] = t; ud_put32(e + 8, lba); ud_put32(e + 12, n); }
static void mbr0(uint8_t *s) { memset(s, 0, 512); s[510] = 0x55; s[511] = 0xAA; }

static FILE *img;
static uint64_t img_sec;
static int rdf(void *c, uint64_t lba, uint32_t n, void *b) { (void)c; if (fseek(img, (long)(lba * 512), SEEK_SET)) return -1; return fread(b, 512, n, img) == n ? 0 : -1; }
static int wrf(void *c, uint64_t lba, uint32_t n, const void *b) { (void)c; if (fseek(img, (long)(lba * 512), SEEK_SET)) return -1; return fwrite(b, 512, n, img) == n ? 0 : -1; }

int main(int argc, char **argv)
{
    uint8_t s[512];
    const uint64_t G = 2048ull * 1024;                      /* sectores por GiB */
    /* la imagen de GitHub: SOMA 1 GiB + SOMADATA 4 GiB */
    uint32_t p1 = 2048, n1 = 1u << 21, p2 = 2048 + (1u << 21), n2 = 1u << 23;
    mbr0(s); ent(s, 0, 0x0C, p1, n1); ent(s, 1, 0x0C, p2, n2);
    CHECK(ud_plan_grow(s, 1, p2 + n2) == 0, "pendrive justo: nada que ampliar");
    CHECK(ud_plan_grow(s, 1, p2 + n2 + 1000) == 0, "menos de 64 MiB libres: no se amplia");
    uint64_t d8 = 15633408;                                 /* un pendrive de 8 GB real (7,45 GiB) */
    uint32_t g8 = ud_plan_grow(s, 1, d8);
    CHECK(g8 > n2 && (uint64_t)p2 + g8 <= d8 && ((uint64_t)p2 + g8) % 2048 == 0, "8 GB: hasta el final, alineado a 1 MiB");
    uint32_t g32 = ud_plan_grow(s, 1, 32 * G);
    CHECK(ud_data_sectors(g32) <= UD_MAX_SECT && ud_data_sectors(g32) + 2048 > UD_MAX_SECT && (uint64_t)p2 + g32 <= 32 * G, "32 GB: como mucho el limite de datos de SOMA");
    uint32_t g2t = ud_plan_grow(s, 1, 4096 * G);
    CHECK(g2t && (uint64_t)p2 + g2t <= 0xFFFFFFFFull, "disco enorme: nunca pasa de 2^32 sectores");
    CHECK(ud_plan_grow(s, 0, 32 * G) == 0, "la particion de arranque nunca crece (hay otra detras)");
    ent(s, 2, 0x0C, p2 + n2, 1u << 21);
    CHECK(ud_plan_grow(s, 1, 32 * G) == 0, "con una tercera particion detras (ejecutar.py en macOS): no se toca");
    ent(s, 2, 0, 0, 0); ent(s, 2, 0xEE, 1, 100);
    CHECK(ud_plan_grow(s, 1, 32 * G) == 0, "GPT protectora: no se toca");
    ent(s, 2, 0, 0, 0);
    { uint8_t c[512]; memcpy(c, s, 512); ud_apply_grow(c, 1, g8);
      struct ud_part pt[4]; uint32_t sig; int np = ud_parse_mbr(c, d8, pt, &sig);
      CHECK(np == 2 && pt[1].n == g8 && pt[1].lba == p2 && pt[0].n == n1, "el MBR ampliado se lee bien y la otra particion sigue igual");
      CHECK(memcmp(c, s, 0x1BE + 16) == 0 && memcmp(c + 0x1BE + 32, s + 0x1BE + 32, 512 - 0x1BE - 32) == 0, "solo cambia la entrada de SOMADATA"); }
    /* al azar: nunca crece por encima del disco, del limite, ni solapa otra particion */
    srand(228); long checked = 0;
    for (int it = 0; it < 300000; it++) {
        mbr0(s);
        uint64_t top = 0;
        for (int i = 0; i < 4; i++) if (rand() % 3) {
            uint32_t l = (uint32_t)(rand() % (1 << 26)), n = (uint32_t)(rand() % (1 << 25)) + 1;
            ent(s, i, (uint8_t)(rand() % 5 ? 0x0C : rand() & 0xFF), l, n);
            if ((uint64_t)l + n > top) top = (uint64_t)l + n;
        }
        uint64_t disk = top + (uint64_t)(rand() % 3 ? rand() % (1 << 26) : rand()) * (rand() % 2 ? 1 : 300);
        int idx = rand() % 4;
        uint32_t n = ud_plan_grow(s, idx, disk);
        if (!n) continue;
        checked++;
        uint8_t *e = s + 0x1BE + 16 * idx; uint64_t lba = ud_le32(e + 8), old = ud_le32(e + 12);
        CHECK(n > old && lba + n <= disk && lba + n <= 0xFFFFFFFFull && ud_data_sectors(n) <= UD_MAX_SECT, "ampliacion dentro de los limites");
        for (int i = 0; i < 4; i++) if (i != idx && s[0x1BE + 16 * i + 4]) {
            uint64_t ol = ud_le32(s + 0x1BE + 16 * i + 8), on = ud_le32(s + 0x1BE + 16 * i + 12);
            CHECK(ol + on <= lba, "ninguna otra particion dentro ni detras");
        }
        if (fails > 5) break;
    }

    /* fat_overwrite sobre una FAT32 hecha por construyeimagen.py */
    if (argc < 2 || !(img = fopen(argv[1], "r+b"))) { printf("falta la imagen (tests/mk_informe_228.py)\n"); return 1; }
    fseek(img, 0, SEEK_END); img_sec = (uint64_t)ftell(img) / 512;
    static struct fat_vol v; struct fat_io io = { rdf, 0, img_sec };
    CHECK(fat_mount(&v, &io) == 0 && v.type == 32, "monta la FAT32");
    struct fat_dirent d; CHECK(fat_stat(&v, "INFORME.TXT", &d) == 0 && d.size == 256 * 1024, "INFORME.TXT de 256 KiB");
    /* foto de todo lo que NO es INFORME.TXT (la imagen usa los primeros MiB) */
    uint32_t span = 48u << 20; uint8_t *before = malloc(span), *after = malloc(span), *txt = malloc(300000), *back = malloc(300000);
    fseek(img, 0, SEEK_SET); CHECK(fread(before, 1, span, img) == span, "leer antes");
    uint64_t f0 = (uint64_t)v.data_lba * 512 + (uint64_t)(d.cluster - 2) * v.cluster_bytes, f1 = f0 + d.size;
    for (uint32_t i = 0; i < 100000; i++) txt[i] = (uint8_t)("informe de prueba\n"[i % 18]);
    CHECK(fat_overwrite(&v, "INFORME.TXT", txt, 100000, '\n', wrf) == 0, "sobrescribe");
    CHECK(fat_overwrite(&v, "INFORME.TXT", txt, 300000, '\n', wrf) == FAT_E_ARG, "rechaza mas datos que el archivo");
    CHECK(fat_overwrite(&v, "EFI", txt, 10, '\n', wrf) == FAT_E_ISDIR, "rechaza un directorio");
    CHECK(fat_overwrite(&v, "NOEXISTE.TXT", txt, 10, '\n', wrf) == FAT_E_NOENT, "no crea archivos");
    fflush(img); fseek(img, 0, SEEK_SET); CHECK(fread(after, 1, span, img) == span, "leer despues");
    long outside = 0; for (uint64_t i = 0; i < span; i++) if ((i < f0 || i >= f1) && before[i] != after[i]) outside++;
    CHECK(outside == 0, "nada fuera de INFORME.TXT ha cambiado");
    CHECK(fat_stat(&v, "INFORME.TXT", &d) == 0 && d.size == 256 * 1024, "el tamano sigue igual");
    CHECK(fat_read(&v, "INFORME.TXT", 0, back, d.size) == (int)d.size, "se relee");
    int okc = memcmp(back, txt, 100000) == 0; for (uint32_t i = 100000; i < d.size; i++) if (back[i] != '\n') okc = 0;
    CHECK(okc, "contenido = informe + saltos de linea");
    uint8_t zz[76800]; CHECK(fat_read(&v, "ZZ.BIN", 0, zz, sizeof zz) == (int)sizeof zz, "el archivo de detras se lee");
    int zok = 1; for (unsigned i = 0; i < sizeof zz; i++) if (zz[i] != (uint8_t)(i & 255)) zok = 0;
    CHECK(zok, "el archivo de detras sigue intacto");
    fclose(img); free(before); free(after); free(txt); free(back);
    if (fails) { printf("arranque_228: %d fallos\n", fails); return 1; }
    printf("arranque_228: ampliacion de SOMADATA (%ld casos al azar) e INFORME.TXT en su sitio: todo correcto\n", checked);
    return 0;
}
