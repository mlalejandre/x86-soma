#ifndef SOMA_USBDISK_H
#define SOMA_USBDISK_H
/* SOMA_162_T480S: disco del sistema en la particion SOMADATA del pendrive (ver usbdisk.c) */
#include <stdint.h>
struct usbdisk_info {
    int active, readonly, lost, dirty, claimed_now, part_index;
    uint64_t part_lba, part_sectors, sectors, syncs;
    uint32_t disk_sig;
    const char *why;
    uint32_t grown_from_mb, grown_to_mb;           /* SOMA_228_ARRANQUE: SOMADATA ampliada en este arranque (0 = no) */
    const char *grow_why;                          /* por que no se amplio (o "") */
    int in_file;                                   /* SOMA_238_GUARDADO: el disco es el fichero SOMADATA.IMG de la particion de arranque */
    int remarked;                                  /* SOMA_238: la marca no cuadraba (tamano o firma) y se rehizo */
    uint32_t wait_ms;                              /* SOMA_238: lo que se espero a que el pendrive respondiera */
};
int usbdisk_attach(void);                         /* 0 = el disco del sistema es ahora el pendrive */
const struct usbdisk_info *usbdisk_info(void);
void usbdisk_stats(uint64_t *reads, uint64_t *writes, uint64_t *errors, uint64_t *retries);
int usbdisk_flush(void);
/* SOMA_228_ARRANQUE: reescribe en su sitio un fichero que ya existe en la particion FAT de arranque del pendrive (la "SOMA", nunca
 * SOMADATA), por ejemplo "INFORME.TXT". No crea ficheros ni cambia su tamano (ver fat_overwrite). 0 = escrito; si no, *why dice por que. */
int usbdisk_bootfile_write(const char *name, const char *data, uint32_t len, const char **why);
/* SOMA_241_VOZ: lectura de la particion FAT de arranque (para instalar el modelo de voz copiado al pendrive) */
struct fat_dirent;
int usbdisk_bootfile_read(const char *name, uint32_t off, void *buf, uint32_t len, uint32_t *size_out, const char **why);
int usbdisk_bootdir(const char *path, struct fat_dirent *out, int max, const char **why);
/* SOMA_267_DISCOS: estado de las unidades /usuario y /modelos (particiones 4 y 3, exFAT) para la orden "disco" */
void usbdisk_unidades_info(void);
/* SOMA_363_ACCESO: numero (xhci_msc_*_n) del disco USB del sistema, comprobado ahora; -1 si no hay o no responde */
int usbdisk_disco_sistema(void);
#endif
