#ifndef SOMA_USBDISK_H
#define SOMA_USBDISK_H
/* SOMA_162_T480S: disco del sistema en la particion SOMADATA del pendrive (ver usbdisk.c) */
#include <stdint.h>
struct usbdisk_info {
    int active, readonly, lost, dirty, claimed_now, part_index;
    uint64_t part_lba, part_sectors, sectors, syncs;
    uint32_t disk_sig;
    const char *why;
};
int usbdisk_attach(void);                         /* 0 = el disco del sistema es ahora el pendrive */
const struct usbdisk_info *usbdisk_info(void);
void usbdisk_stats(uint64_t *reads, uint64_t *writes, uint64_t *errors, uint64_t *retries);
int usbdisk_flush(void);
#endif
