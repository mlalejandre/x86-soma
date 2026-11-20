#ifndef MYOS_VIRTIO_BLK_H
#define MYOS_VIRTIO_BLK_H
/* Driver del disco virtio de QEMU. Lo que usan los demas es el disco del sistema (sysdisk.h), que pasa por aqui. */
#include <stdint.h>
#include "sysdisk.h"

/* Inicializa el controlador de disco duro VirtIO. Devuelve 1 si OK. */
int virtio_blk_init(void);

#endif
