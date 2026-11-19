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

void vfs_list(void);
void vfs_list_ext(int show_all, const char *filter_dir);
int  vfs_format_list(char *out_buf, uint32_t max);
int  vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max);

int  vfs_sync(void);
void vfs_format(void);
void vfs_tree(char *out_buf, uint32_t max);
void vfs_tree_ext(int show_all, char *out_buf, uint32_t max);

void vfs_get_stats(uint32_t *files_used, uint32_t *bytes_used, uint32_t *dirty_count);
int  vfs_is_hidden(const char *name);
int  vfs_agent_secret(const char *name);   /* SOMA_193: 1 si el hilo actual es un agente y `name` es un archivo con secretos */

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
