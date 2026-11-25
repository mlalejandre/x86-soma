#ifndef SOMA_NVME_H
#define SOMA_NVME_H
#include <stdint.h>
/* PARCHE 043: controlador NVMe (PCI 01:08:02) en modo SONDEO: una cola de administracion y una de E/S de 16 entradas,
 * espacio de nombres 1 con sectores de 512 bytes. Opt-in (comando "nvme"); no se usa en el arranque.
 * La logica de colas/PRP vive en src/core/nvme_proto.c (probada en el host contra un controlador simulado). */
struct nvme_info {
    int      found, ready;
    uint8_t  bus, slot, fn;
    uint64_t bar;
    uint32_t vs, mqes, to_ms, dstrd;
    char     model[41], serial[21];
    uint64_t sectors;
    uint32_t lba_size;
    uint8_t  sector0[16];
    int      err;
};
#define NVME_MAX_SECTORS 128
int nvme_probe(struct nvme_info *o);                              /* 0 = inicializado y sector 0 leido */
const char *nvme_err_str(int err);
int nvme_read(uint64_t lba, uint32_t count, void *buf);           /* hasta NVME_MAX_SECTORS; buf debe tener count*512 bytes */
int nvme_write(uint64_t lba, uint32_t count, const void *buf);
int nvme_flush(void);

/* Autotest de escritura: SOLO en el disco de prueba de QEMU (modelo "QEMU NVMe" y firma SOMANVME en el sector 0). */
struct nvme_selftest { int allowed, wrote_ok, verify_ok, restore_ok, sector0_ok; uint32_t read_ms, read_kib; int err; };
int nvme_selftest(struct nvme_selftest *t);
#endif
