/* SOMA_363_ACCESO: los medios USB externos (pendrives, discos, lectores de tarjetas) en /usb/<nombre>.
 *   - Cada particion exFAT, FAT12/16/32 de cada disco USB que no sea el del sistema se monta en /usb/<etiqueta> (o /usb/USB1...).
 *   - exFAT: lectura y escritura (las unidades de src/core/unidades.c, u >= UNID_EXT0). FAT: lectura (aqui).
 *   - El disco del sistema (el pendrive de SOMA) nunca aparece.
 *   - Al enchufar o quitar un disco (el controlador USB se reexplora) se vuelven a montar solos (hilo "medios").
 * El VFS (src/fs/fs.c) manda aqui lo que es FAT y el listado de /usb. */
#ifndef SOMA_MEDIOS_H
#define SOMA_MEDIOS_H
#include <stdint.h>
#include "fat.h"

#define MEDIOS_MAX 8
enum { MEDIO_EXFAT = 1, MEDIO_FAT = 2, MEDIO_OTRO = 3 };
struct medio_info { char ruta[56]; char etiqueta[40]; int tipo; int solo_lectura; uint64_t total, libre; char fs[16]; char dispositivo[40]; };

void medios_inicia(void);                                   /* el hilo que monta y vuelve a montar (x86_64) */
void medios_refresca(void);                                 /* volver a mirar ahora (tras enchufar algo) */
int  medios_n(void);
int  medios_info(int i, struct medio_info *o);              /* 0 = bien */
uint32_t medios_version(void);                              /* cambia al montar o desmontar */

/* rutas */
int  medios_es_raiz(const char *ruta);                      /* "/usb" o "/usb/" */
int  medios_lista_raiz(char *out, uint32_t max);            /* "Archivos: /usb/KINGSTON/, /usb/FOTOS/" */
int  medios_fat_es(const char *ruta);                       /* /usb/<un medio FAT>[/...] */
int  medios_fat_tam(const char *ruta, uint64_t *tam, int *es_carpeta);   /* 0 = bien */
int  medios_fat_leer(const char *ruta, uint64_t off, void *buf, uint32_t n);   /* bytes o <0 */
int  medios_fat_listar(const char *ruta, struct fat_dirent *out, int max, int *total);
#endif
