#ifndef SOMA_DISCOS_CORE_H
#define SOMA_DISCOS_CORE_H
/* SOMA_331_DISCOS: herramientas de disco sin dependencias del nucleo (las prueba tests/test_discos_331.c con datos hostiles):
 *   - tablas de particiones MBR y GPT: leer (comprobando todo: firmas, CRC32, limites y solapes), crear, anadir y quitar;
 *   - reconocer el sistema de archivos de una particion (exFAT, FAT12/16/32, NTFS, ext2/3/4, BIGFS de SOMA, ISO 9660);
 *   - formatear en FAT32 (exFAT lo hace exfat_format de src/core/exfat.c).
 * La E/S entra por funciones (sectores de 512 bytes absolutos del disco). */
#include <stdint.h>

#define DC_SECTOR 512u
#define DC_PART_MAX 16
#define DC_ALINEA 2048u                          /* las particiones nuevas empiezan en multiplos de 1 MiB */

enum { DC_NADA = 0, DC_MBR = 1, DC_GPT = 2 };
enum { DC_FS_DESCONOCIDO = 0, DC_FS_VACIO, DC_FS_EXFAT, DC_FS_FAT12, DC_FS_FAT16, DC_FS_FAT32, DC_FS_NTFS, DC_FS_EXT,
       DC_FS_BIGFS, DC_FS_ISO9660, DC_FS_N };
enum { DC_TIPO_DATOS = 0, DC_TIPO_EFI = 1, DC_TIPO_LINUX = 2 };   /* para crear particiones */

typedef int (*dc_leer_fn)(void *ctx, uint64_t lba, uint32_t n, void *buf);          /* 0 = bien */
typedef int (*dc_escribir_fn)(void *ctx, uint64_t lba, uint32_t n, const void *buf);

struct dc_part {
    uint64_t lba, n;                             /* primer sector y cuantos */
    uint8_t  mbr_tipo;                           /* MBR: byte de tipo (0x07, 0x0C, 0xEF...) */
    uint8_t  tipo[16], uuid[16];                 /* GPT: GUID de tipo y unico (en el orden del disco) */
    uint64_t attrs;
    char     nombre[40];                         /* GPT: nombre (UTF-16 pasado a UTF-8, recortado) */
    int      activa, logica;                     /* MBR: arrancable; dentro de una extendida */
};
struct dc_tabla {
    int      esquema;                            /* DC_NADA, DC_MBR o DC_GPT */
    uint64_t sectores;                           /* tamano del disco */
    uint64_t usable_ini, usable_fin;             /* donde pueden ir las particiones (incluidos) */
    uint8_t  disco_guid[16];
    uint32_t mbr_firma;
    int      n;
    struct dc_part p[DC_PART_MAX];
    int      gpt_respaldo;                       /* 1 = la GPT principal estaba mal y se leyo la copia del final */
    char     aviso[96];                          /* algo raro que no impide leerla ("" si nada) */
};

uint32_t dc_crc32(const void *d, uint32_t n);
/* lee la tabla: 0 = bien (tambien si no hay tabla: esquema DC_NADA); <0 = error de lectura. buf: 512 * 34 bytes de trabajo */
int  dc_lee(dc_leer_fn rd, void *ctx, uint64_t sectores, struct dc_tabla *t, uint8_t *buf);
/* tabla nueva y vacia (semilla para los GUID) */
void dc_nueva(struct dc_tabla *t, int esquema, uint64_t sectores, uint32_t semilla);
/* el hueco libre mas grande (alineado a 1 MiB). 0 = hay */
int  dc_hueco(const struct dc_tabla *t, uint64_t *ini, uint64_t *n);
/* anade una particion de n sectores (0 = todo el hueco mas grande) del tipo dado. Devuelve su indice o <0 */
int  dc_anade(struct dc_tabla *t, uint64_t n, int tipo, const char *nombre, uint32_t semilla);
int  dc_quita(struct dc_tabla *t, int i);
/* escribe la tabla (MBR, o GPT con su MBR protector y la copia del final). buf: 512 * 33 bytes de trabajo. 0 = bien */
int  dc_escribe(dc_escribir_fn wr, void *ctx, const struct dc_tabla *t, uint8_t *buf);
/* comprueba que la tabla en memoria es coherente (dentro del disco, sin solapes). 0 = bien; si no, motivo en *why */
int  dc_valida(const struct dc_tabla *t, const char **why);

/* sistema de archivos de la particion (o del disco entero con lba 0): DC_FS_*; etiqueta si la tiene */
int  dc_fs(dc_leer_fn rd, void *ctx, uint64_t lba, uint64_t n, char *etiqueta, uint32_t cap, uint8_t *buf4k);
const char *dc_fs_nombre(int fs);
const char *dc_tipo_nombre(const struct dc_tabla *t, const struct dc_part *p);   /* "Datos", "EFI", "Linux", "Extendida"... */

/* FAT32 en una particion: 0 = bien; < 0 = no cabe o error de escritura. buf: 64 KiB de trabajo */
int  dc_fat32(dc_escribir_fn wr, void *ctx, uint64_t lba, uint64_t n, const char *etiqueta, uint32_t serie, uint8_t *buf64k);
#define DC_FAT32_MIN 66600ull                   /* sectores (unos 33 MiB): con menos no caben los 65525 clusters de FAT32 */
#endif
