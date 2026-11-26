#ifndef SOMA_BLK_H
#define SOMA_BLK_H
/* PARCHE 045: capa comun de dispositivos de bloque (sectores de 512 bytes). Los controladores (virtio-blk, AHCI, NVMe)
 * se registran con funciones de lectura/escritura; blk_* trocea las peticiones, comprueba limites y protege las escrituras:
 * solo se permite escribir en dispositivos marcados como "de prueba" (discos de QEMU con firma), nunca en discos reales. */
#include <stdint.h>

#define BLK_MAX_DEV 8
#define BLK_CHUNK 128                                   /* sectores por peticion al controlador */
struct blkdev {
    char name[12];
    const char *driver;
    uint64_t sectors;
    int (*read)(uint64_t lba, uint32_t n, void *buf);   /* n <= BLK_CHUNK; 0 = OK */
    int (*write)(uint64_t lba, uint32_t n, const void *buf);
    int test_disk;                                      /* 1 = se puede escribir (disco de prueba) */
};
#define BLK_E_ARG   (-1)
#define BLK_E_RANGE (-2)
#define BLK_E_IO    (-3)
#define BLK_E_RO    (-4)
#define BLK_E_VERIFY (-5)

void blk_reset(void);
int  blk_register(const struct blkdev *d);              /* devuelve el indice o <0 */
int  blk_count(void);
const struct blkdev *blk_get(int i);
int  blk_find(const char *name);                        /* indice o -1 */
int  blk_read(int dev, uint64_t lba, uint32_t n, void *buf);
int  blk_write(int dev, uint64_t lba, uint32_t n, const void *buf);
/* Copia n sectores entre dispositivos (destino de prueba), troceando; con verify relee el destino y compara. */
int  blk_copy(int src, uint64_t slba, int dst, uint64_t dlba, uint64_t n, int verify);
const char *blk_err_str(int e);
/* SOMA_331_DISCOS: permite escribir en un disco real hasta reiniciar. Solo la llama el gestor de discos (discos.c) despues de que
 * el usuario escriba el nombre del disco, y nunca para el disco del sistema. 0 = hecho; <0 = no tiene escritura */
int  blk_desbloquea(int dev);
int  blk_scan(void);                                    /* arch: detecta y registra los discos del sistema; devuelve cuantos */
#endif
