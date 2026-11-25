#ifndef SOMA_SYSDISK_H
#define SOMA_SYSDISK_H
/* SOMA_231_SYSDISK: el disco del sistema. Es lo que usan los sistemas de archivos (VFS, diario, checkpoints, SRCFS, BIGFS) y
 * no les importa que dispositivo hay debajo:
 *   - el disco virtio de QEMU (src/drivers/virtio_blk.c), por defecto, o
 *   - otro dispositivo registrado con sysdisk_set_device(): en el portatil, la particion SOMADATA del pendrive
 *     (src/arch/x86_64/usbdisk.c).
 * Sectores de 512 bytes. Todas devuelven 0 si va bien y -1 si hay error. (La implementacion sigue en virtio_blk.c, junto al
 * driver virtio, porque decide entre los dos.) */
#include <stdint.h>

int sysdisk_read(uint64_t sector, void *buf);                  /* un sector */
int sysdisk_write(uint64_t sector, const void *buf);

/* SOMA_PARCHE_018_BIGFS: E/S de varios sectores en UNA peticion (hasta 128; trocea si son mas) */
int sysdisk_read_n(uint64_t sector, void *buf, uint32_t nsec);
int sysdisk_write_n(uint64_t sector, const void *buf, uint32_t nsec);
/* PARCHE 115: barrera de escritura. Vuelve cuando todo lo escrito antes es durable. El driver virtio NO negocia VIRTIO_BLK_F_FLUSH,
 * asi que por la especificacion de virtio el dispositivo se comporta como write-through y cada escritura completada ya es durable y
 * ordenada: no hay nada que vaciar. Un dispositivo con cache volatil (el pendrive, AHCI, NVMe, SD) emite aqui su FLUSH CACHE. */
int sysdisk_flush(void);
uint64_t sysdisk_capacity(void);                               /* sectores de 512 B; 0 si no hay disco */

/* SOMA_162_T480S: otro dispositivo como disco del sistema cuando no hay disco virtio */
struct sysdisk_ops {
    int (*read)(uint64_t lba, void *buf, uint32_t n);          /* 0 = bien */
    int (*write)(uint64_t lba, const void *buf, uint32_t n);
    int (*flush)(void);
    uint64_t sectors;
    const char *name;
};
int sysdisk_set_device(const struct sysdisk_ops *o);           /* 0 = aceptado; -1 si ya hay disco virtio */
const char *sysdisk_describe(void);                            /* "SOMAFS en el pendrive (SOMADATA, 79 MiB)"... */
#endif
