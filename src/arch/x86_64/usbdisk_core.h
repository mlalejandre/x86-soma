#ifndef SOMA_USBDISK_CORE_H
#define SOMA_USBDISK_CORE_H
/* SOMA_162_T480S: la parte pura de usbdisk.c (sin E/S), para poder probarla en el anfitrion (tests/test_usbdisk_162.c):
 * tabla MBR, reconocimiento de la particion SOMADATA, la marca que la identifica y la traduccion de sectores con limites. */
#include <stdint.h>

#define UD_MARK      "SOMADATA-DISCO-1"
#define UD_DATA_OFF  8u                                    /* el disco de SOMA empieza 4 KiB despues de la marca */
#define UD_MAX_SECT  (8ULL * 1024 * 1024 * 2)              /* 8 GiB como mucho */
#define UD_ZERO_SECT 16392u                                /* 8 MiB + la cabecera de BIGFS: se ponen a cero al reclamarla */
#define UD_MIN_PART  (UD_ZERO_SECT + UD_DATA_OFF + 2048u)  /* ~9 MiB */

struct ud_part { int index; uint32_t lba, n; uint8_t type; };

static inline uint32_t ud_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void ud_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline int ud_eq(const uint8_t *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != (uint8_t)b[i]) return 0; return 1; }

/* Particiones primarias utilizables (dentro del disco, sin solaparse con el MBR, de tamano suficiente).
 * Devuelve cuantas (0..4), -1 sin MBR, -2 si es GPT (MBR protector). */
static inline int ud_parse_mbr(const uint8_t *s0, uint64_t disk_sectors, struct ud_part out[4], uint32_t *disk_sig)
{
    if (s0[510] != 0x55 || s0[511] != 0xAA) return -1;
    if (disk_sig) *disk_sig = ud_le32(s0 + 0x1B8);
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = s0 + 0x1BE + 16 * i;
        uint8_t t = e[4]; uint32_t lba = ud_le32(e + 8), cnt = ud_le32(e + 12);
        if (t == 0xEE) return -2;
        if (!t || t == 0x05 || t == 0x0F || t == 0x85) continue;                  /* vacia o extendida */
        if (lba == 0 || cnt < UD_MIN_PART) continue;
        if ((uint64_t)lba + cnt > disk_sectors || (uint64_t)lba + cnt < lba) continue;
        out[n].index = i; out[n].lba = lba; out[n].n = cnt; out[n].type = t; n++;
    }
    return n;
}

/* Sector de arranque FAT recien creado con la etiqueta de volumen SOMADATA (diskutil / mkfs.vfat / mformat) */
static inline int ud_is_fat_somadata(const uint8_t *s)
{
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (ud_eq(s + 0x52, "FAT32   ", 8) && ud_eq(s + 0x47, "SOMADATA   ", 11)) return 1;
    if (ud_eq(s + 0x36, "FAT", 3) && ud_eq(s + 0x2B, "SOMADATA   ", 11)) return 1;
    return 0;
}

/* La marca: texto + inicio y tamano de la particion + firma del disco del MBR (identifica ESTE pendrive y ESTA particion) */
static inline void ud_mark_build(uint8_t *s, uint64_t part_lba, uint64_t part_n, uint32_t sig)
{
    for (int i = 0; i < 512; i++) s[i] = 0;
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)UD_MARK[i];
    ud_put32(s + 16, (uint32_t)part_lba); ud_put32(s + 20, (uint32_t)part_n); ud_put32(s + 24, sig);
    const char *t = "SOMA: disco del sistema (no formatear si quieres conservar tus archivos)";
    for (int i = 0; t[i] && 32 + i < 510; i++) s[32 + i] = (uint8_t)t[i];
    s[510] = 0x55; s[511] = 0xAA;
}
static inline int ud_mark_ok(const uint8_t *s, uint64_t part_lba, uint64_t part_n, uint32_t sig)
{
    return ud_eq(s, UD_MARK, 16) && ud_le32(s + 16) == (uint32_t)part_lba && ud_le32(s + 20) == (uint32_t)part_n && ud_le32(s + 24) == sig;
}

static inline uint64_t ud_data_sectors(uint64_t part_n)
{
    if (part_n <= UD_DATA_OFF) return 0;
    uint64_t s = part_n - UD_DATA_OFF;
    return s > UD_MAX_SECT ? UD_MAX_SECT : s;
}

/* Sector del disco de SOMA -> sector absoluto del pendrive. 0 = dentro; -1 = la peticion se saldria de la particion. */
static inline int ud_map(uint64_t part_lba, uint64_t part_n, uint64_t data_sectors, uint64_t lba, uint32_t k, uint64_t *abs)
{
    if (k == 0 || lba >= data_sectors || k > data_sectors - lba) return -1;
    uint64_t a = part_lba + UD_DATA_OFF + lba;
    if (a < part_lba + UD_DATA_OFF || a + k > part_lba + part_n || a + k < a) return -1;
    *abs = a;
    return 0;
}
#endif
