/* SOMA_267_DISCOS: el disco de SOMA (particion DATOS) escrito y leido con el codigo REAL del nucleo, para compararlo con
 * tools/somafs.py (tests/test_datos_267.py):
 *   test_datos_267 nuevo IMG MIB            disco vacio de SOMA: superbloque SOMAFS02 y BIGFS formateado (bigfs_format)
 *   test_datos_267 escribe IMG SEMILLA N    N archivos al azar como los guarda el nucleo: ranura con diario (slotio_commit) si caben,
 *                                           BIGFS /.ramfs/NNN (spill_put_atomic) si no; imprime "nombre tamano huella"
 *   test_datos_267 corta IMG SLOT NOMBRE    deja una escritura de ranura COMPROMETIDA en el diario pero sin aplicar (un corte)
 *   test_datos_267 lee IMG                  lo que veria el nucleo al arrancar (slotio_recover + spill_recover + carga de ranuras
 *                                           como load_file_from_disk / load_spill_slot) y "FSCK n" (bigfs_fsck)
 * La imagen es el disco de SOMA tal cual (lba 0 = el primer sector tras la marca de DATOS). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "slotio.h"
#include "bigfs.h"
#include "spillio.h"

#define FS_SUPER 1024u
#define FS_DATA 1025u
#define FS_SPF 33u
#define FS_SLOTS 64
#define FS_MAX 256
#define FS_NAME_MAX 48
#define FS_DATA_MAX 16384u
#define FS_JOURNAL 3200u
#define BIG_START 16384u

static FILE *img; static uint64_t nsec;
static int rd1(void *c, uint64_t lba, void *b) { (void)c; if (lba >= nsec) return -1; fseek(img, (long)(lba * 512), SEEK_SET); return fread(b, 512, 1, img) == 1 ? 0 : -1; }
static int wr1(void *c, uint64_t lba, const void *b) { (void)c; if (lba >= nsec) return -1; fseek(img, (long)(lba * 512), SEEK_SET); return fwrite(b, 512, 1, img) == 1 ? 0 : -1; }
static struct slotdev SD = { rd1, wr1, 0, 0 };
static int brd(void *c, uint64_t lba, void *b, uint32_t n) { (void)c; fseek(img, (long)((BIG_START + lba) * 512), SEEK_SET); return fread(b, 512, n, img) == n ? 0 : -1; }
static int bwr(void *c, uint64_t lba, const void *b, uint32_t n) { (void)c; fseek(img, (long)((BIG_START + lba) * 512), SEEK_SET); return fwrite(b, 512, n, img) == n ? 0 : -1; }
static struct bigfs_dev BD = { 0, brd, bwr, 0, malloc, free, 0, 0, 0, 0 };

static unsigned long long fnv(const unsigned char *p, size_t n) { unsigned long long h = 1469598103934665603ULL; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; } return h; }
static uint64_t sem = 0x9E3779B97F4A7C15ULL;
static uint64_t azar(void) { sem ^= sem << 13; sem ^= sem >> 7; sem ^= sem << 17; return sem; }

static void spill_path(int s, char *p) { sprintf(p, "/.ramfs/%03d", s); }
static void spill_tmp(int s, char *p) { sprintf(p, "/.ramfs/t%03d", s); }

static void cabecera(uint8_t *h, const char *nombre, uint32_t size, int spilled, const uint8_t *datos)
{
    memset(h, 0, 512);
    strncpy((char *)h, nombre, FS_NAME_MAX - 1);
    memcpy(h + 48, &size, 4); h[52] = 1; h[53] = spilled ? 'B' : 0;
    if (!spilled) { uint32_t crc = slotio_crc32(0, datos ? (const void *)datos : (const void *)"", datos ? size : 0); memcpy(h + 54, &crc, 4); h[58] = 'C'; }
}
static void superbloque(int nfiles)
{
    uint8_t sb[512] = { 0 };
    memcpy(sb, "SOMAFS02", 8); uint32_t v[4] = { 2, (uint32_t)nfiles, FS_SLOTS, FS_DATA_MAX }; memcpy(sb + 8, v, 16);
    wr1(0, FS_SUPER, sb);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: nuevo|escribe|corta|lee IMG ...\n"); return 2; }
    const char *modo = argv[1];
    if (!strcmp(modo, "nuevo")) {
        FILE *f = fopen(argv[2], "wb"); fclose(f);
        if (truncate(argv[2], (long)(strtoull(argv[3], 0, 10) << 20))) return 2;
    }
    img = fopen(argv[2], "r+b"); if (!img) return 2;
    fseek(img, 0, SEEK_END); nsec = (uint64_t)ftell(img) / 512; BD.total_sectors = nsec - BIG_START;
    if (!strcmp(modo, "nuevo")) {
        superbloque(0);
        int r = bigfs_format(&BD, 0); if (r) { printf("bigfs_format: %s\n", bigfs_strerror(r)); return 1; }
        int e; struct bigfs *fs = bigfs_mount(&BD, &e); if (!fs) return 1;
        bigfs_mkdir(fs, "/.ramfs"); bigfs_mkdir(fs, "/modelos"); bigfs_unmount(fs);
        return 0;
    }
    if (!strcmp(modo, "escribe")) {
        sem ^= strtoull(argv[3], 0, 10) * 2654435761ULL; int n = atoi(argv[4]);
        int e; struct bigfs *fs = bigfs_mount(&BD, &e); if (!fs) { printf("bigfs_mount: %s\n", bigfs_strerror(e)); return 1; }
        int nfiles = 0;
        for (int i = 0; i < n && i < FS_MAX; i++) {
            char nombre[FS_NAME_MAX]; snprintf(nombre, sizeof nombre, "/%s/archivo_%d.%s", (const char *[]){ "etc", "workspace", "mem", "agent" }[azar() % 4], i, azar() % 2 ? "txt" : "bin");
            uint32_t size = azar() % 5 == 0 ? (uint32_t)(azar() % 300000) : (uint32_t)(azar() % 16000);
            uint8_t *d = malloc(size + 1); for (uint32_t k = 0; k < size; k++) d[k] = (uint8_t)azar();
            int slot = i;                                            /* el nucleo llena en orden */
            uint8_t h[512];
            if (slot < FS_SLOTS && size < FS_DATA_MAX) {
                cabecera(h, nombre, size, 0, d);
                if (slotio_commit(&SD, FS_JOURNAL, FS_DATA + (uint64_t)slot * FS_SPF, (uint32_t)slot, h, d, size, (uint32_t)i + 1)) { printf("commit fallo\n"); return 1; }
            } else {
                char p[32], t[32]; spill_path(slot, p); spill_tmp(slot, t);
                if (spill_put_atomic(fs, p, t, nombre, FS_NAME_MAX, (const char *)d, size)) { printf("spill fallo\n"); return 1; }
                if (slot < FS_SLOTS) { cabecera(h, nombre, size, 1, 0); wr1(0, FS_DATA + (uint64_t)slot * FS_SPF, h); }
            }
            if (slot < FS_SLOTS) nfiles++;
            printf("%s %u %016llx\n", nombre, size, fnv(d, size));
            free(d);
        }
        superbloque(nfiles);
        bigfs_unmount(fs);
        return 0;
    }
    if (!strcmp(modo, "corta")) {                                    /* diario comprometido, sin aplicar */
        int slot = atoi(argv[3]); const char *nombre = argv[4];
        uint32_t size = 3000; uint8_t d[3000]; for (uint32_t k = 0; k < size; k++) d[k] = (uint8_t)(k * 7 + 1);
        uint8_t h[512]; cabecera(h, nombre, size, 0, d);
        uint32_t nd = slotio_nd(size), crc = slotio_crc32(0, h, 512); uint8_t sec[512];
        for (uint32_t s = 0; s < nd; s++) { slotio_data_sector(d, size, s, sec); crc = slotio_crc32(crc, sec, 512); wr1(0, FS_JOURNAL + 2 + s, sec); }
        wr1(0, FS_JOURNAL + 1, h);
        struct slotio_jhdr j; memset(&j, 0, sizeof j); memcpy(j.magic, SLOTIO_JMAGIC, 8); j.seq = 99; j.slot = (uint32_t)slot; j.nd = nd; j.payload_crc = crc;
        j.hdr_crc = slotio_crc32(0, &j, 24); wr1(0, FS_JOURNAL, &j);
        printf("%s %u %016llx\n", nombre, size, fnv(d, size));
        return 0;
    }
    if (!strcmp(modo, "lee")) {
        int jr = slotio_recover(&SD, FS_JOURNAL, FS_DATA, FS_SPF, FS_SLOTS);
        if (jr) fprintf(stderr, "diario: %d\n", jr);
        int e; struct bigfs *fs = bigfs_mount(&BD, &e);
        if (!fs) { printf("bigfs_mount: %s\n", bigfs_strerror(e)); return 1; }
        for (int s = 0; s < FS_MAX; s++) { char p[32], t[32]; spill_path(s, p); spill_tmp(s, t); spill_recover(fs, p, t); }
        for (int s = 0; s < FS_MAX; s++) {
            uint8_t h[512]; char nombre[FS_NAME_MAX] = { 0 }; uint32_t size = 0; uint8_t *d = 0;
            if (s < FS_SLOTS) {
                rd1(0, FS_DATA + (uint64_t)s * FS_SPF, h);
                if (h[52] != 1) continue;
                if (!memchr(h, 0, FS_NAME_MAX) || !h[0]) { printf("MAL ranura %d nombre\n", s); continue; }
                memcpy(nombre, h, FS_NAME_MAX); memcpy(&size, h + 48, 4);
                if (h[53] != 'B') {
                    if (size >= FS_DATA_MAX) { printf("MAL ranura %d tamano\n", s); continue; }
                    d = malloc(size + 1);
                    for (uint32_t k = 0; k < 32; k++) { uint8_t sec[512]; rd1(0, FS_DATA + (uint64_t)s * FS_SPF + 1 + k, sec); if (k * 512 < size) memcpy(d + k * 512, sec, size - k * 512 < 512 ? size - k * 512 : 512); }
                    if (h[58] == 'C') { uint32_t want; memcpy(&want, h + 54, 4); if (slotio_crc32(0, d, size) != want) printf("MAL CRC %s\n", nombre); }
                    else printf("MAL sin CRC %s\n", nombre);
                    printf("%s %u %016llx\n", nombre, size, fnv(d, size)); free(d); continue;
                }
            }
            char p[32]; spill_path(s, p); uint32_t ino, len;
            if (!spill_valid(fs, p, &len)) { if (s < FS_SLOTS) printf("MAL ranura %d sin BIGFS\n", s); continue; }
            bigfs_lookup(fs, p, &ino);
            uint8_t hh[64]; bigfs_read(fs, ino, 0, hh, 64);
            memcpy(nombre, hh + 4, FS_NAME_MAX); nombre[FS_NAME_MAX - 1] = 0;
            d = malloc(len + 1);
            if (bigfs_read(fs, ino, 64, d, len) != (int64_t)len) printf("MAL lectura %s\n", nombre);
            printf("%s %u %016llx\n", nombre, len, fnv(d, len)); free(d);
        }
        bigfs_unmount(fs);
        struct bigfs_fsck_report rep; int fr = bigfs_fsck(&BD, &rep);
        printf("FSCK %u %d\n", rep.problems, fr);
        return 0;
    }
    return 2;
}
