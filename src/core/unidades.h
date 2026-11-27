#ifndef SOMA_UNIDADES_H
#define SOMA_UNIDADES_H
/* SOMA_267_DISCOS: las unidades exFAT del pendrive dentro de SOMA.
 *   /usuario  -> particion 4 (USUARIO): los archivos del usuario. Lectura y escritura.
 *   /modelos  -> particion 3 (MODELOS): modelos de IA. Desde el VFS es de SOLO LECTURA; solo la escriben los programas del sistema
 *               (unid_*_priv) o desde fuera del sistema.
 * Logica pura (sin dependencias del nucleo): la E/S entra por struct exfat_io (relativa a la particion) y el cerrojo por funciones.
 * La prueban tests/test_unidades_267.c sobre pendrives creados con tools/soma_disco.py.
 * El plan de particiones (unid_plan) es el mismo calculo que tools/soma_disco.py:plan_particiones (lo comprueban las pruebas). */
#include <stdint.h>
#include "exfat.h"

#define UNID_USUARIO 0
#define UNID_MODELOS 1
#define UNID_EXT0    2      /* SOMA_363_ACCESO: medios USB externos con exFAT (lectura y escritura), en /usb/<nombre> */
#define UNID_N       6

/* MODELOS y USUARIO de la imagen del repositorio (5 GiB + 1 MiB): se agrandan en el primer arranque si estan vacias */
#define UNID_SERIE_IMG_MOD 0x50AA0003u
#define UNID_SERIE_IMG_USU 0x50AA0004u
#define UNID_SERIE_AGR_MOD 0x50AA0103u     /* ya agrandadas (no se vuelve a hacer; sirven tambien para terminar uno cortado) */
#define UNID_SERIE_AGR_USU 0x50AA0104u

#define UNID_TIPO_FAT32 0x0Cu
#define UNID_TIPO_DATOS 0xDAu
#define UNID_TIPO_EXFAT 0x07u

struct unid_part { uint32_t lba, n; uint8_t tipo; };
/* las cuatro particiones para un disco de `total` sectores con DATOS de datos_n sectores. 0 = bien, -1 = no cabe */
int unid_plan(uint64_t total, uint32_t datos_n, struct unid_part p[4]);

/* ---- montaje (lo llama el pegamento del pendrive: src/arch/x86_64/usbdisk.c) */
void unid_set_lock(void (*lock)(void), void (*unlock)(void));
int  unid_montar(int u, const struct exfat_io *io, uint64_t sectores);   /* 0 = montada; io relativo a la particion */
void unid_desmontar(int u);
/* SOMA_363_ACCESO: un medio externo (u >= UNID_EXT0) en /usb/<nombre> (nombre sin '/') */
int  unid_montar_ext(int u, const struct exfat_io *io, uint64_t sectores, const char *nombre);
int  unid_activa(int u);
const char *unid_nombre(int u);                                          /* "usuario" / "modelos" */
int  unid_info(int u, uint64_t *libre, uint64_t *total, char *etiqueta, uint32_t cap);

/* ---- rutas del VFS: "/usuario", "/usuario/...", "/modelos/..." -> la unidad (o -1). *resto = la ruta dentro ("/..."). */
int  unid_ruta(const char *ruta, const char **resto);

/* ---- operaciones (con el cerrojo). Devuelven 0 / bytes, o un EXFAT_E_* (<0). `priv`: programa del sistema (puede escribir MODELOS). */
int  unid_tam(const char *ruta, uint64_t *tam, int *es_carpeta);
int  unid_leer(const char *ruta, uint64_t off, void *buf, uint32_t n);
int  unid_escribir(const char *ruta, const void *datos, uint64_t n, int priv);
int  unid_anadir(const char *ruta, const void *datos, uint32_t n, int priv);
int  unid_borrar(const char *ruta, int priv);
int  unid_mkdir(const char *ruta, int priv);
int  unid_renombrar(const char *de, const char *a, int priv);
int  unid_borrar_rec(const char *ruta, int priv);                         /* SOMA_268_ACCESO: archivo, o carpeta con todo lo que contiene (no la raiz de la unidad) */            /* las dos en la misma unidad */
int  unid_listar(const char *ruta, struct exfat_dirent *out, int max, int *total);
struct unid_fd { int u; uint32_t gen; struct exfat_file f; };
int  unid_abrir(const char *ruta, struct unid_fd *fd);                  /* para leer mucho (SYS_OPEN de ring 3) */
int  unid_pread(const struct unid_fd *fd, uint64_t off, void *buf, uint32_t n);
int64_t unid_fd_tam(const struct unid_fd *fd);

/* ---- primer arranque: agrandar MODELOS y USUARIO de la imagen hasta el final del pendrive (si estan vacias) */
struct unid_disco {
    int (*read)(void *ctx, uint64_t lba, uint32_t n, void *buf);         /* sectores absolutos del pendrive; n <= 128 */
    int (*write)(void *ctx, uint64_t lba, uint32_t n, const void *buf);  /* el pegamento solo deja escribir el MBR y desde p3 */
    int (*sync)(void *ctx);
    void *ctx;
    uint64_t sectores;
    void *(*alloc)(uint32_t n);
    void (*free)(void *p);
    uint32_t (*now)(void);
};
/* 0 = no habia nada que hacer (*why dice por que), 1 = agrandadas (o terminado un agrandado cortado), <0 = error */
int  unid_agrandar(const struct unid_disco *d, const char **why, uint32_t *mod_mib, uint32_t *usu_mib);
/* una unidad recien hecha: solo las carpetas de serie, vacias */
int  unid_virgen(struct exfat_vol *v, int u);
extern const char *const UNID_CARPETAS_USUARIO[];
extern const char *const UNID_CARPETAS_MODELOS[];
#endif
