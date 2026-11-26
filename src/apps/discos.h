#ifndef SOMA_DISCOS_H
#define SOMA_DISCOS_H
/* SOMA_331_DISCOS: gestor de discos (ver discos.c; lo que se lee del disco lo interpreta discos_core.c) */
#include <stdint.h>
#include "discos_core.h"

struct disco_info {
    int dev;                                     /* indice en la capa de bloque */
    char nombre[12];                             /* "sda", "nvme0", "usb0"... */
    const char *driver;
    uint64_t sectores;
    int sistema;                                 /* 1 = el disco del sistema: no se toca */
    int escribible;                              /* 1 = desbloqueado (o disco de prueba de QEMU) */
    int error_lectura;
    struct dc_tabla tabla;
    int fs[DC_PART_MAX];                         /* DC_FS_* de cada particion */
    char etiqueta[DC_PART_MAX][24];
    int fs_disco; char etiqueta_disco[24];       /* sin tabla: lo que haya en el disco entero */
};
enum { DISCOS_LIBRE = 0, DISCOS_TRABAJANDO = 1, DISCOS_HECHO = 2, DISCOS_ERROR = 3 };
enum { DISCOS_OP_DISCO = 1, DISCOS_OP_NUEVA, DISCOS_OP_BORRA, DISCOS_OP_FORMATEA, DISCOS_OP_GRABA };

int  discos_lista(void);                                     /* vuelve a leer los discos; cuantos */
int  discos_reescanea(void);                                 /* busca otra vez los discos (se pierden los desbloqueos) */
const struct disco_info *discos_info(int i);
uint32_t discos_version(void);                               /* cambia cuando hay algo nuevo que pintar */
int  discos_es_sistema(int dev);
int  discos_desbloquea(int i, const char *confirmacion, const char **why);   /* confirmacion = el nombre del disco */
/* una operacion que escribe (en un hilo): 0 = empezo */
int  discos_op(int tipo, int dev, int part, int esquema, int fs, uint64_t mib, const char *etiqueta, const char *ruta, const char **why);
int  discos_estado(char *msg, uint32_t cap, int *pct);      /* DISCOS_* */
void discos_acepta(void);                                    /* el resultado ya se ha visto */
void discos_tam(char *o, uint32_t cap, uint64_t sectores);   /* "32,0 GB" */
int  discos_cmd(const char *args, char *out, uint32_t cap);  /* orden "discos" */
#endif
