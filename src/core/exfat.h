#ifndef SOMA_EXFAT_H
#define SOMA_EXFAT_H
/* SOMA_267_DISCOS: exFAT de lectura y escritura (las particiones MODELOS y USUARIO del pendrive).
 * Logica pura, como fat.c: la E/S de sectores entra por funciones; se prueba en el anfitrion (tests/test_exfat_267.c) contra
 * volumenes de mkfs.exfat y con fsck.exfat si esta (tests/test_exfat_267.py). Sectores de 512 bytes.
 *
 * Que hace: montar (con la region de arranque de reserva si la principal esta danada), listar, leer por trozos, crear y
 * reescribir archivos enteros, anadir al final, carpetas, borrar, renombrar y mover, y formatear (tabla de mayusculas propia,
 * src/core/exfat_upcase.inc). Los nombres son UTF-8 (hasta 255 caracteres) y se comparan sin mayusculas con la tabla del volumen.
 * Orden de escritura pensado para cortes de luz: primero los datos, luego la entrada del directorio y al final se liberan los
 * cluster viejos (un corte deja como mucho cluster perdidos, nunca un archivo que apunte a datos ajenos).
 * No hace: TexFAT (dos FAT; se monta solo de lectura), sectores distintos de 512 ni el bit de volumen sucio. */
#include <stdint.h>

#define EXFAT_NAME_MAX 768                       /* bytes UTF-8 de un nombre (255 caracteres) + 0 */
#define EXFAT_PATH_MAX 1024

#define EXFAT_E_IO      (-1)
#define EXFAT_E_NOFS    (-2)    /* no es exFAT */
#define EXFAT_E_UNSUP   (-3)    /* exFAT valido pero no soportado (sector != 512...) */
#define EXFAT_E_NOENT   (-4)
#define EXFAT_E_NOTDIR  (-5)
#define EXFAT_E_ISDIR   (-6)
#define EXFAT_E_CORRUPT (-7)
#define EXFAT_E_ARG     (-8)
#define EXFAT_E_EXIST   (-9)
#define EXFAT_E_NOSPC   (-10)
#define EXFAT_E_NOTEMPTY (-11)
#define EXFAT_E_RO      (-12)
#define EXFAT_E_NOMEM   (-13)
#define EXFAT_E_NAME    (-14)   /* nombre no valido (vacio, demasiado largo o con " * / : < > ? \ |) */

struct exfat_io {
    int (*read)(void *ctx, uint64_t lba, uint32_t n, void *buf);           /* 0 = bien; n <= 128 */
    int (*write)(void *ctx, uint64_t lba, uint32_t n, const void *buf);    /* 0 = solo lectura */
    void *ctx;
    void *(*alloc)(uint32_t n);
    void (*free)(void *p);
    uint32_t (*now)(void);                                                 /* fecha y hora en formato DOS (0 = 1980) */
};
struct exfat_dirent {
    char name[EXFAT_NAME_MAX];
    uint64_t size;
    uint32_t first_cluster, mtime;                                         /* mtime: formato DOS */
    uint16_t attr;
    uint8_t is_dir, nofat;
};
struct exfat_vol {
    struct exfat_io io;
    uint64_t part_lba, sectors;                                            /* sectores del volumen (VolumeLength) */
    uint32_t fat_off, fat_len, heap_off, clusters, root, serial, cluster_bytes, spc_shift;
    uint32_t free_count, hint;
    int writable, nfats;
    uint8_t *bitmap; uint32_t bitmap_first; uint64_t bitmap_bytes;
    uint16_t *upcase;                                                      /* 65536 entradas */
    char label[48];
    uint64_t cache_lba; uint8_t cache[512];                                /* un sector (directorios) */
    uint64_t fcache_lba; uint8_t fcache[512];                              /* un sector de la FAT */
};

/* un archivo abierto (para leer mucho sin resolver la ruta cada vez). Deja de valer si el archivo se reescribe o se borra. */
struct exfat_file { uint32_t first, serial; uint64_t size, valid; uint8_t nofat; };

int  exfat_mount(struct exfat_vol *v, const struct exfat_io *io, uint64_t part_lba, uint64_t part_sectors);
void exfat_umount(struct exfat_vol *v);                                     /* libera la memoria (no hay nada pendiente de escribir) */
int  exfat_stat(struct exfat_vol *v, const char *path, struct exfat_dirent *out);
/* lista una carpeta: hasta max entradas; *total = las que hay aunque no quepan. Devuelve las escritas o <0 */
int  exfat_readdir(struct exfat_vol *v, const char *path, struct exfat_dirent *out, int max, int *total);
/* lee hasta len bytes desde off; devuelve los leidos (0 al final) o <0 */
int  exfat_read(struct exfat_vol *v, const char *path, uint64_t off, void *buf, uint32_t len);
int  exfat_open(struct exfat_vol *v, const char *path, struct exfat_file *f);              /* archivos (no carpetas) */
int  exfat_pread(struct exfat_vol *v, const struct exfat_file *f, uint64_t off, void *buf, uint32_t len);
/* crea o reescribe un archivo con estos datos (la carpeta donde va tiene que existir) */
int  exfat_write(struct exfat_vol *v, const char *path, const void *data, uint64_t len);
/* anade al final (lo crea si no existe) */
int  exfat_append(struct exfat_vol *v, const char *path, const void *data, uint32_t len);
int  exfat_mkdir(struct exfat_vol *v, const char *path);                   /* EXFAT_E_EXIST si ya esta */
int  exfat_remove(struct exfat_vol *v, const char *path);                  /* archivo, o carpeta vacia */
int  exfat_rename(struct exfat_vol *v, const char *from, const char *to);  /* tambien mueve de carpeta; el destino no puede existir */
uint64_t exfat_free_bytes(const struct exfat_vol *v);
uint64_t exfat_total_bytes(const struct exfat_vol *v);
const char *exfat_err_str(int e);
/* formatea sectores [part_lba, part_lba + sectors) como exFAT vacio con esa etiqueta (UTF-8, hasta 11 caracteres) */
int  exfat_format(const struct exfat_io *io, uint64_t part_lba, uint64_t sectors, const char *label, uint32_t serial);
/* reconocer un sector de arranque exFAT (sin montar) */
int  exfat_is_boot(const uint8_t *s0);
#endif
