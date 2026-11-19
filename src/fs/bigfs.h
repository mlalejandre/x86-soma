#ifndef MYOS_BIGFS_H
#define MYOS_BIGFS_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_018_BIGFS: almacen para archivos grandes (audio, imagenes, video, programas).
 *
 * Formato SOMABIG1 (todo en sectores de 512 B; el dispositivo es una REGION del disco):
 *   sector 0            superbloque (CRC32)
 *   sectores 1..        tabla de inodos: 2 inodos de 256 B por sector
 *   data_start..        clusteres de 4 KiB (8 sectores)
 * Cada inodo cabe en UN sector y lleva su CRC32: cada operacion de metadatos se confirma con UNA escritura
 * atomica de sector, asi que un corte de energia en cualquier punto deja el disco consistente
 * (sin diario). Los datos se escriben ANTES que el inodo que los referencia (con `flush` opcional como barrera entre ambos); el bitmap de clusteres no se
 * guarda: se reconstruye al montar a partir de los extents de los inodos (no hay fugas por cortes).
 * Archivos = hasta BIGFS_MAX_EXTENTS extents contiguos. Directorios reales (puntero al padre en el inodo).
 * El nucleo es neutro de arquitectura: el dispositivo y la memoria los aporta quien lo use (struct bigfs_dev). */

#define BIGFS_SECTOR        512u
#define BIGFS_CLUSTER_SECT  8u
#define BIGFS_CLUSTER       (BIGFS_SECTOR * BIGFS_CLUSTER_SECT)
#define BIGFS_INODE_SIZE    256u
#define BIGFS_NAME_MAX      63
#define BIGFS_PATH_MAX      255
#define BIGFS_MAX_EXTENTS   20
#define BIGFS_MAX_INODES    65536u

#define BIGFS_T_FREE 0u
#define BIGFS_T_FILE 1u
#define BIGFS_T_DIR  2u

/* codigos de error (negativos) */
#define BIGFS_OK          0
#define BIGFS_E_NOENT   (-2)
#define BIGFS_E_EXIST   (-3)
#define BIGFS_E_NOTDIR  (-4)
#define BIGFS_E_ISDIR   (-5)
#define BIGFS_E_NOTEMPTY (-6)
#define BIGFS_E_NAME    (-7)
#define BIGFS_E_NOSPC   (-8)    /* sin clusteres libres */
#define BIGFS_E_IO      (-9)
#define BIGFS_E_CORRUPT (-10)
#define BIGFS_E_INVAL   (-11)
#define BIGFS_E_NOINODE (-12)
#define BIGFS_E_FBIG    (-13)   /* demasiado fragmentado (limite de extents) */
#define BIGFS_E_NOTMNT  (-14)
#define BIGFS_E_NOTFMT  (-15)   /* no hay un SOMABIG1 en la region */

struct bigfs_dev {
    void *ctx;
    int (*read)(void *ctx, uint64_t lba, void *buf, uint32_t nsec);          /* 0 = OK */
    int (*write)(void *ctx, uint64_t lba, const void *buf, uint32_t nsec);   /* 0 = OK */
    uint64_t total_sectors;                                                    /* tamano de la region */
    void *(*alloc)(size_t n);
    void (*free)(void *p);
    uint64_t (*now)(void *ctx);            /* opcional */
    void (*lock)(void *ctx);               /* opcionales */
    void (*unlock)(void *ctx);
    int (*flush)(void *ctx);               /* opcional (PARCHE 115): barrera de escritura; vuelve cuando todo lo escrito antes es DURABLE */
};

struct bigfs_stat {
    uint32_t type, ino, parent, nextents;
    uint64_t size, mtime;
    char name[BIGFS_NAME_MAX + 1];
};

struct bigfs_info {
    uint64_t total_clusters, free_clusters;
    uint32_t total_inodes, used_inodes;
    uint32_t cluster_bytes;
    uint32_t repaired;       /* inodos descartados al montar (CRC malo, fuera de rango, solapados, huerfanos) */
    uint64_t cache_hits, cache_misses;   /* PARCHE 131: cache de lectura */
};

struct bigfs_fsck_report {
    uint32_t problems;       /* total */
    uint32_t bad_crc, bad_extent, overlap, orphan, bad_dir, bad_name, dup_name, bad_super;
};

struct bigfs;   /* opaco */

int  bigfs_format(const struct bigfs_dev *dev, uint32_t inode_count);   /* inode_count 0 = automatico */
int  bigfs_repair_super(const struct bigfs_dev *dev);   /* PARCHE 129: 0 sano, 1 desde la copia, 2 por geometria automatica, <0 no hay SOMABIG1 */
struct bigfs *bigfs_mount(const struct bigfs_dev *dev, int *err);
void bigfs_unmount(struct bigfs *fs);
int  bigfs_fsck(const struct bigfs_dev *dev, struct bigfs_fsck_report *rep);   /* lee el disco; no modifica nada */
void bigfs_info(struct bigfs *fs, struct bigfs_info *out);

int  bigfs_lookup(struct bigfs *fs, const char *path, uint32_t *ino_out);       /* >=0 tipo, <0 error */
int  bigfs_stat(struct bigfs *fs, uint32_t ino, struct bigfs_stat *st);
int  bigfs_create(struct bigfs *fs, const char *path, uint32_t *ino_out);       /* archivo; falla si existe */
int  bigfs_mkdir(struct bigfs *fs, const char *path);
int  bigfs_unlink(struct bigfs *fs, const char *path);                          /* archivo */
int  bigfs_rmdir(struct bigfs *fs, const char *path);                           /* directorio vacio */
int  bigfs_rename(struct bigfs *fs, const char *from, const char *to);          /* "to" no debe existir */
/* devuelve los bytes leidos/escritos (>=0) o un error (<0) */
int64_t bigfs_read(struct bigfs *fs, uint32_t ino, uint64_t off, void *buf, uint64_t len);
int64_t bigfs_write(struct bigfs *fs, uint32_t ino, uint64_t off, const void *buf, uint64_t len);
int  bigfs_truncate(struct bigfs *fs, uint32_t ino, uint64_t size);
/* siguiente hijo de `dir`: *cursor empieza en 0; devuelve 1 si hay entrada, 0 si no hay mas, <0 error */
int  bigfs_readdir(struct bigfs *fs, uint32_t dir, uint32_t *cursor, struct bigfs_stat *out);
const char *bigfs_strerror(int code);

#endif
