#ifndef MYOS_FS_H
#define MYOS_FS_H

#include <stdint.h>

/* PARCHE 087: los archivos ya no tienen tope de 16 KiB. Cada uno ocupa en memoria lo que mide (antes 16 KiB fijos) y puede llegar a
 * FS_FILE_MAX (lo que permita el heap). En disco, los de menos de 16 KiB siguen en su ranura de SOMAFS02 (compatible); los mayores, y los
 * archivos por encima de las 64 ranuras, se guardan solos en el almacen BIGFS (/.ramfs/NNN). Sin disco (Pi 5) viven en memoria. */
#define FS_MAX_FILES  256
#define FS_DISK_SLOTS 64          /* ranuras fijas del disco (SOMAFS02) */
#define FS_NAME_MAX   48
#define FS_DATA_MAX   16384       /* tamano de una ranura del disco; ya NO es el maximo de un archivo */
#define FS_FILE_MAX   (32u * 1024u * 1024u)

struct vfs_file {
    char     name[FS_NAME_MAX]; /* 48 bytes (offset 0..47) */
    uint32_t size;              /* 4 bytes (offset 48..51) */
    uint8_t  used;              /* 1 byte (offset 52) */
    uint8_t  dirty;             /* 1 byte (offset 53) */
    uint8_t  spilled;           /* PARCHE 087: el contenido esta en BIGFS (/.ramfs/NNN) */
    uint8_t  ram_only;          /* PARCHE 087: no se pudo guardar en disco (sin BIGFS): solo en memoria */
    uint8_t  pad[4];
    uint32_t cap;               /* PARCHE 087: bytes reservados en data */
    char     *data;             /* contenido (+ '\0') en KHeap */
} __attribute__((aligned(8)));

void vfs_init(void);
int  vfs_create(const char *name, const char *initial_data);
int  vfs_write(const char *name, const char *data, uint32_t len);

/* SOMA_PARCHE_LIVE_V1: hook que el GUI registra para ver en vivo lo que
 * SOMA escribe en archivos de texto. NULL = desactivado. */
extern void (*vfs_live_hook)(const char *name, const char *data, uint32_t len);
int  vfs_read(const char *name, char *buf_out, uint32_t max_len);
int  vfs_delete(const char *name);
/* PARCHE 087: tamano de un archivo (-1 si no existe) y lectura completa en un bufer nuevo (kfree al acabar; termina en '\0') */
int   vfs_size(const char *name);
char *vfs_read_alloc(const char *name, uint32_t *len_out);
/* PARCHE 087: estado del guardado de archivos grandes: 1 en disco (BIGFS), 0 solo memoria, -1 sin disco */
int   vfs_big_persist(void);
/* PARCHE 087: almacen para lo que no cabe en las ranuras (lo implementa bigfs_glue.c; las pruebas usan uno en memoria) */
int  fs_spill_ready(void);
int  fs_spill_put(int slot, const char *name, const char *data, uint32_t len);
int  fs_spill_stat(int slot, char *name_out, uint32_t *len_out);
int  fs_spill_get(int slot, char *buf, uint32_t len);
void fs_spill_del(int slot);

void vfs_list_ext(int show_all, const char *filter_dir);
int  vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max);

int  vfs_sync(void);
/* SOMA_269_RAM: modo RAM. Con el modo activo las escrituras en RamFS no van al pendrive: quedan marcadas como pendientes y se guardan en
 * vfs_sync() (apagar, reiniciar, `disco sync`). Un corte de luz pierde lo no guardado. vfs_sin_guardar() = archivos pendientes. */
void     vfs_modo_ram(int on);
int      vfs_modo_ram_activo(void);
uint32_t vfs_sin_guardar(void);
void vfs_format(void);
void vfs_tree_ext(int show_all, char *out_buf, uint32_t max);

void vfs_get_stats(uint32_t *files_used, uint32_t *bytes_used, uint32_t *dirty_count);
int  vfs_is_hidden(const char *name);
int  vfs_agent_secret(const char *name);
/* SOMA_268_ACCESO: sesion de USUARIO. Mientras el hilo actual esta en sesion de usuario (el terminal ejecutando lo que ha escrito la persona),
 * las ordenes y editores genericos solo pueden escribir en /usuario y /tmp; los programas del sistema (Ajustes, memoria, instalador...) no
 * pasan por aqui y siguen escribiendo en DATOS. vfs_sesion_usuario(on) devuelve la sesion anterior (0/1) para poder anidar. */
int  vfs_sesion_usuario(int on);
int  vfs_sesion_usuario_activa(void);
int  vfs_usuario_puede_escribir(const char *name);   /* 1 si se permite escribir `name` en la sesion actual */
/* SOMA_268_ACCESO: carpetas de la unidad del usuario (/usuario) para el gestor de archivos. 0 = bien, -1 = error o no es de una unidad */
int  vfs_unid_mkdir(const char *name);
int  vfs_unid_rename(const char *de, const char *a);          /* archivos y carpetas */
int  vfs_unid_borrar_rec(const char *name);
/* SOMA_363_ACCESO: los medios USB externos (/usb/<nombre>). Los exFAT son unidades (como /usuario); el listado de /usb y los medios FAT
 * (solo lectura) los resuelve src/core/medios.c a traves de estas funciones (0 = sin medios). Bajo /usb nunca se crean archivos de RamFS. */
struct vfs_medios_ops {
    int (*es)(const char *ruta);                                           /* 1 = "/usb" o un medio FAT */
    int (*tam)(const char *ruta, uint64_t *tam, int *es_carpeta);          /* 0 = bien */
    int (*leer)(const char *ruta, uint64_t off, void *buf, uint32_t n);    /* bytes o <0 */
    int (*lista)(int todo, const char *dir, char *out, uint32_t max, int consola);
};
extern const struct vfs_medios_ops *vfs_medios;
int  vfs_es_usb(const char *name);                            /* la ruta empieza por /usb */
/* SOMA_363_ACCESO: leer un trozo (para archivos grandes de /usuario y /usb sin cargarlos enteros). Bytes leidos o <0 */
int  vfs_read_at(const char *name, uint64_t off, void *buf, uint32_t n);                   /* archivo, o carpeta con todo lo que contiene */   /* SOMA_193: 1 si el hilo actual es un agente y `name` es un archivo con secretos */

/* Motor de Búsqueda Semántica y Ponderada BM25 */
int  vfs_search_bm25(const char *query, const char *path_prefix, char *out, uint32_t max);

int  vfs_grep_all(const char *pat, int ignore_case, int list_only,
                  char *out, uint32_t max);

int  vfs_checkpoint_save(void);
int  vfs_checkpoint_restore(void);
int  vfs_checkpoint_selftest(void);   /* PARCHE 120: checkpoint real: guardar/restaurar/rechazar corrupto; 1 = OK */
int  vfs_crash_selftest(void);   /* PARCHE 112: cortes simulados; 1 = ningun archivo a medias */
void vfs_killtest_boot(void);    /* PARCHE 114: si el disco lleva la marca de prueba, verifica y escribe sin parar (no vuelve) */
int  vfs_fscheck(void);          /* PARCHE 112: comprueba cabeceras y CRC en disco; devuelve los problemas */

#endif
