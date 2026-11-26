/* SOMA_331_DISCOS: prueba de src/core/discos_core.c en el anfitrion (sin hardware):
 *   - GPT y MBR: crear, escribir, releer y comparar; la copia del final de la GPT cuando la principal se estropea;
 *   - huecos y limites (alineacion a 1 MiB, disco lleno, MBR con discos de mas de 2 TiB);
 *   - datos hostiles: miles de sectores 0..40 al azar y tablas manipuladas (CRC, tamanos, cadenas de EBR en bucle):
 *     nunca una particion fuera del disco ni un acceso fuera del bufer (con -fsanitize=address);
 *   - reconocimiento de sistemas de archivos y FAT32 (con una imagen opcional para fsck.fat: argumento 1).
 * Uso: test_discos_331 [imagen_para_fsck] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "discos_core.h"

static int fallos, total;
#define CK(c) do { total++; if (!(c)) { fallos++; printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct disco { uint8_t *d; uint64_t n; int escrituras; uint64_t max_lba; };
static int rd(void *c, uint64_t lba, uint32_t n, void *buf)
{
    struct disco *k = (struct disco *)c;
    if (lba > k->n || n > k->n - lba) return -1;                    /* fuera del disco: error, como un controlador */
    memcpy(buf, k->d + lba * 512, (size_t)n * 512);
    return 0;
}
static int wr(void *c, uint64_t lba, uint32_t n, const void *buf)
{
    struct disco *k = (struct disco *)c;
    if (lba > k->n || n > k->n - lba) return -1;
    memcpy(k->d + lba * 512, buf, (size_t)n * 512); k->escrituras++;
    if (lba + n > k->max_lba) k->max_lba = lba + n;
    return 0;
}
static uint8_t g_buf[512 * 34], g_b64[65536];

static void prueba_gpt(struct disco *k)
{
    struct dc_tabla t, u;
    memset(k->d, 0, (size_t)k->n * 512);
    dc_nueva(&t, DC_GPT, k->n, 1234);
    int a = dc_anade(&t, 64 * 2048, DC_TIPO_EFI, "EFI", 1);
    int b = dc_anade(&t, 100 * 2048, DC_TIPO_DATOS, "Datos \303\261", 2);
    int c = dc_anade(&t, 0, DC_TIPO_LINUX, "Resto", 3);
    CK(a == 0 && b == 1 && c == 2);
    CK(t.p[0].lba == 2048 && t.p[0].n == 64 * 2048);
    CK(t.p[1].lba == 2048 + 64 * 2048);
    CK(t.p[2].lba % 2048 == 0 && t.p[2].lba + t.p[2].n - 1 <= t.usable_fin);
    uint64_t hi, hn; CK(dc_hueco(&t, &hi, &hn) != 0);                     /* lleno */
    CK(dc_escribe(wr, k, &t, g_buf) == 0);
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0);
    CK(u.esquema == DC_GPT && u.n == 3 && !u.gpt_respaldo);
    for (int i = 0; i < 3 && i < u.n; i++) { CK(u.p[i].lba == t.p[i].lba && u.p[i].n == t.p[i].n && !memcmp(u.p[i].tipo, t.p[i].tipo, 16) && !memcmp(u.p[i].uuid, t.p[i].uuid, 16)); }
    CK(!strcmp(u.p[1].nombre, "Datos \303\261"));
    CK(!strcmp(dc_tipo_nombre(&u, &u.p[0]), "Sistema EFI") && !strcmp(dc_tipo_nombre(&u, &u.p[2]), "Linux"));
    /* se estropea la GPT principal: se lee la copia del final */
    k->d[512 + 30] ^= 0x5A;
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0);
    CK(u.esquema == DC_GPT && u.n == 3 && u.gpt_respaldo && u.aviso[0]);
    /* y las dos: el disco queda "con GPT danada" (sin particiones), sin inventar nada */
    k->d[(k->n - 1) * 512 + 30] ^= 0x5A;
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0);
    CK(u.esquema == DC_NADA && u.n == 0 && u.aviso[0]);
    /* quitar y volver a anadir reutiliza el hueco */
    CK(dc_quita(&t, 1) == 0 && t.n == 2);
    CK(dc_hueco(&t, &hi, &hn) == 0 && hi == 2048 + 64 * 2048 && hn == 100 * 2048);
    CK(dc_anade(&t, 0, DC_TIPO_DATOS, "otra", 9) == 2 && t.p[2].n == 100 * 2048);
}

static void prueba_mbr(struct disco *k)
{
    struct dc_tabla t, u;
    memset(k->d, 0, (size_t)k->n * 512);
    dc_nueva(&t, DC_MBR, k->n, 99);
    for (int i = 0; i < 4; i++) CK(dc_anade(&t, 20 * 2048, DC_TIPO_DATOS, 0, (uint32_t)i) == i);
    CK(dc_anade(&t, 0, DC_TIPO_DATOS, 0, 7) < 0);                        /* MBR: solo 4 primarias */
    t.p[0].activa = 1;
    CK(dc_escribe(wr, k, &t, g_buf) == 0);
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0);
    CK(u.esquema == DC_MBR && u.n == 4 && u.p[0].activa && u.p[3].lba == t.p[3].lba && u.mbr_firma == t.mbr_firma);
    /* unidades logicas: extendida en la 4 con dos EBR y despues una cadena que apunta a si misma */
    uint8_t *m = k->d;
    memset(m + 446 + 48, 0, 16);
    uint64_t ext = 70 * 2048; m[446 + 48 + 4] = 0x0F; uint32_t v = (uint32_t)ext; memcpy(m + 446 + 56, &v, 4); v = 40 * 2048; memcpy(m + 446 + 60, &v, 4);
    uint8_t *e1 = k->d + ext * 512; memset(e1, 0, 512); e1[510] = 0x55; e1[511] = 0xAA;
    e1[446 + 4] = 0x07; v = 2048; memcpy(e1 + 446 + 8, &v, 4); v = 8 * 2048; memcpy(e1 + 446 + 12, &v, 4);
    e1[462 + 4] = 0x05; v = 10 * 2048; memcpy(e1 + 462 + 8, &v, 4); v = 10 * 2048; memcpy(e1 + 462 + 12, &v, 4);
    uint8_t *e2 = k->d + (ext + 10 * 2048) * 512; memset(e2, 0, 512); e2[510] = 0x55; e2[511] = 0xAA;
    e2[446 + 4] = 0x83; v = 2048; memcpy(e2 + 446 + 8, &v, 4); v = 4 * 2048; memcpy(e2 + 446 + 12, &v, 4);
    e2[462 + 4] = 0x05; v = 10 * 2048; memcpy(e2 + 462 + 8, &v, 4); v = 2048; memcpy(e2 + 462 + 12, &v, 4);   /* vuelve a la misma */
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0);
    CK(u.esquema == DC_MBR && u.n == 6 && u.p[4].logica && u.p[5].logica && u.p[5].mbr_tipo == 0x83);
    struct dc_tabla w = u; CK(dc_escribe(wr, k, &w, g_buf) < 0);          /* las logicas no se escriben (se avisa) */
    /* un disco de mas de 2 TiB no admite MBR */
    dc_nueva(&t, DC_MBR, 5000000000ull, 1);
    CK(dc_anade(&t, 0, DC_TIPO_DATOS, 0, 1) < 0);
}

static uint64_t g_x = 88172645463325252ull;
static uint64_t aleat(void) { g_x ^= g_x << 13; g_x ^= g_x >> 7; g_x ^= g_x << 17; return g_x; }

static void prueba_hostil(struct disco *k)
{
    struct dc_tabla t, u;
    int malos = 0;
    for (int ronda = 0; ronda < 3000; ronda++) {
        memset(k->d, 0, 64 * 512);
        if (ronda % 3 == 0) {                         /* una GPT valida con bytes cambiados (y a veces el CRC rehecho) */
            dc_nueva(&t, DC_GPT, k->n, (uint32_t)ronda); dc_anade(&t, 10 * 2048, DC_TIPO_DATOS, "x", 1); dc_anade(&t, 0, DC_TIPO_DATOS, "y", 2);
            dc_escribe(wr, k, &t, g_buf);
            int nmut = 1 + (int)(aleat() % 8);
            for (int i = 0; i < nmut; i++) k->d[512 + aleat() % (34 * 512)] = (uint8_t)aleat();
            if (ronda % 2) {                          /* el atacante rehace los CRC: la GPT "es valida" pero con valores hostiles */
                uint8_t *h = k->d + 512; uint32_t hs = h[12] | (h[13] << 8);
                if (hs >= 92 && hs <= 512) {
                    uint32_t ne = h[80] | (h[81] << 8) | (h[82] << 16) | ((uint32_t)h[83] << 24), es = h[84] | (h[85] << 8) | (h[86] << 16) | ((uint32_t)h[87] << 24);
                    if (ne && ne <= 128 && es && es <= 512 && ne * es <= 32 * 512) { uint32_t c = dc_crc32(k->d + 1024, ne * es); memcpy(h + 88, &c, 4); }
                    memset(h + 16, 0, 4); uint32_t c = dc_crc32(h, hs); memcpy(h + 16, &c, 4);
                }
            }
        } else {                                     /* sector 0 (y algo mas) al azar, con la firma 55 AA casi siempre */
            for (int i = 0; i < 40 * 512; i++) k->d[i] = (uint8_t)aleat();
            if (ronda % 5) { k->d[510] = 0x55; k->d[511] = 0xAA; }
            if (ronda % 7 == 0) k->d[446 + 4] = 0xEE;
        }
        if (dc_lee(rd, k, k->n, &u, g_buf) != 0) continue;
        if (u.n < 0 || u.n > DC_PART_MAX) { malos++; continue; }
        for (int i = 0; i < u.n; i++) if (!u.p[i].n || u.p[i].lba >= k->n || u.p[i].n > k->n - u.p[i].lba) malos++;
        uint64_t hi, hn;
        if (dc_hueco(&u, &hi, &hn) == 0 && (hi + hn > k->n || hi % 2048)) malos++;
        char et[64]; for (int i = 0; i < u.n; i++) (void)dc_fs(rd, k, u.p[i].lba, u.p[i].n, et, sizeof et, g_buf);
    }
    CK(malos == 0);
}

static void prueba_fat32(struct disco *k, const char *imagen)
{
    struct dc_tabla t, u;
    memset(k->d, 0, (size_t)k->n * 512);
    dc_nueva(&t, DC_MBR, k->n, 5);
    CK(dc_anade(&t, 0, DC_TIPO_DATOS, 0, 1) == 0);
    t.p[0].mbr_tipo = 0x0C;
    CK(dc_escribe(wr, k, &t, g_buf) == 0);
    CK(dc_fat32(wr, k, t.p[0].lba, 1000, "x", 1, g_b64) == -2);           /* demasiado pequena */
    CK(dc_fat32(wr, k, t.p[0].lba, 32768, "x", 1, g_b64) == -2);          /* 16 MiB: no llega a 65525 clusters */
    {                                               /* 40 MiB: clusters de 512 bytes, y se reconoce */
        char e0[16];
        CK(dc_fat32(wr, k, t.p[0].lba, 81920, "Chica", 7, g_b64) == 0);
        CK(dc_fs(rd, k, t.p[0].lba, 81920, e0, sizeof e0, g_buf) == DC_FS_FAT32 && k->d[t.p[0].lba * 512 + 13] == 1);
    }
    CK(dc_fat32(wr, k, t.p[0].lba, t.p[0].n, "Soma Prueba", 0x1234ABCD, g_b64) == 0);
    CK(dc_lee(rd, k, k->n, &u, g_buf) == 0 && u.n == 1);
    char et[32];
    CK(dc_fs(rd, k, u.p[0].lba, u.p[0].n, et, sizeof et, g_buf) == DC_FS_FAT32);
    CK(!strcmp(et, "SOMA PRUEBA"));
    if (imagen) {                                   /* la particion sola, para fsck.fat */
        FILE *f = fopen(imagen, "wb");
        if (f) { fwrite(k->d + u.p[0].lba * 512, 512, (size_t)u.p[0].n, f); fclose(f); }
    }
    /* otros reconocimientos */
    uint8_t *p = k->d + u.p[0].lba * 512;
    memset(p, 0, 4096); memcpy(p + 3, "NTFS    ", 8);
    CK(dc_fs(rd, k, u.p[0].lba, u.p[0].n, et, sizeof et, g_buf) == DC_FS_NTFS);
    memset(p, 0, 4096); p[1024 + 56] = 0x53; p[1024 + 57] = 0xEF; memcpy(p + 1024 + 120, "raiz", 4);
    CK(dc_fs(rd, k, u.p[0].lba, u.p[0].n, et, sizeof et, g_buf) == DC_FS_EXT && !strcmp(et, "raiz"));
    memset(p, 0, 4096);
    CK(dc_fs(rd, k, u.p[0].lba, u.p[0].n, et, sizeof et, g_buf) == DC_FS_VACIO);
}

int main(int argc, char **argv)
{
    struct disco k; k.n = 400 * 2048; k.d = calloc((size_t)k.n, 512); k.escrituras = 0; k.max_lba = 0;   /* 400 MiB */
    if (!k.d) return 2;
    prueba_gpt(&k);
    prueba_mbr(&k);
    prueba_hostil(&k);
    prueba_fat32(&k, argc > 1 ? argv[1] : 0);
    free(k.d);
    printf("test_discos_331: %d comprobaciones, %d fallos\n", total, fallos);
    return fallos ? 1 : 0;
}
