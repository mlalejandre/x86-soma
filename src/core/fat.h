#ifndef SOMA_FAT_H
#define SOMA_FAT_H
/* PARCHE 050: lector FAT12/FAT16/FAT32 de SOLO LECTURA sobre cualquier dispositivo de bloque de 512 bytes. Autodetecta disco sin
 * particiones (superfloppy) o MBR (primera particion FAT), nombres largos (LFN, UTF-8) y nombres 8.3 con su capitalizacion.
 * Logica pura: la E/S entra por un callback; se prueba en el host (tests/test_fat.c) con imagenes de tools/mkfat.py y con fuzzing. */
#include <stdint.h>

#define FAT_NAME_MAX 256
struct fat_io {
    int (*read)(void *ctx, uint64_t lba, uint32_t n, void *buf);     /* 0 = bien; n <= 128 */
    void *ctx;
    uint64_t sectors;                                                /* tamano del dispositivo */
};
struct fat_dirent {
    char name[FAT_NAME_MAX];
    uint32_t size, cluster;
    uint16_t date, time;                                             /* formato FAT */
    uint8_t is_dir, attr;
};
struct fat_vol {
    struct fat_io io;
    int type;                                                        /* 12, 16 o 32 */
    uint64_t part_lba, part_sectors;
    int part_index;                                                  /* 0 = sin particiones; 1..4 = entrada MBR */
    uint32_t fat_lba, fat_sectors, nfats, root_lba, root_entries, data_lba, spc, clusters, root_cluster, cluster_bytes;
    char label[12];
    uint32_t fcs;                                                    /* primer sector de la FAT en la cache (0xFFFFFFFF = vacia) */
    uint8_t fbuf[1024];
    uint8_t dbuf[512];
};
#define FAT_E_IO      (-1)
#define FAT_E_NOFAT   (-2)    /* no hay un sistema FAT reconocible */
#define FAT_E_UNSUP   (-3)    /* FAT valido pero no soportado (sector != 512, GPT...) */
#define FAT_E_NOENT   (-4)
#define FAT_E_NOTDIR  (-5)
#define FAT_E_ISDIR   (-6)
#define FAT_E_CORRUPT (-7)
#define FAT_E_ARG     (-8)

int  fat_mount(struct fat_vol *v, const struct fat_io *io);
/* Lista el directorio path en out (maximo max entradas, sin '.' ni '..'); *total = entradas reales aunque no quepan. Devuelve las escritas o <0. */
int  fat_readdir(struct fat_vol *v, const char *path, struct fat_dirent *out, int max, int *total);
int  fat_stat(struct fat_vol *v, const char *path, struct fat_dirent *out);
/* Lee hasta len bytes desde offset; devuelve los leidos (0 al final del fichero) o <0. */
int  fat_read(struct fat_vol *v, const char *path, uint32_t offset, void *buf, uint32_t len);
const char *fat_err_str(int e);
/* SOMA_228_ARRANQUE: reescribe en su sitio un fichero existente (sin cambiar tamano, FAT ni directorios; ver fat.c) */
int  fat_overwrite(struct fat_vol *v, const char *path, const void *data, uint32_t len, uint8_t pad,
                   int (*wr)(void *ctx, uint64_t lba, uint32_t n, const void *buf));
#endif
