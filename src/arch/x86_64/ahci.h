#ifndef SOMA_AHCI_H
#define SOMA_AHCI_H
#include <stdint.h>
/* PARCHE 040: controlador AHCI (SATA) en modo SONDEO y SOLO LECTURA: detecta el controlador, inicializa un puerto con
 * disco ATA, ejecuta IDENTIFY y lee sectores por DMA. Opt-in (comando "ahci"); no se usa en el arranque. */
struct ahci_info {
    int      found;                    /* controlador PCI 01:06:01 encontrado */
    uint8_t  bus, slot, fn;
    uint32_t abar, cap, pi, vs;
    int      nports_impl;
    uint8_t  port_state[32];           /* 0 sin dispositivo, 1 disco ATA, 2 ATAPI, 3 otro */
    int      port;                     /* puerto usado (-1 ninguno) */
    char     model[41];
    uint64_t sectors;
    int      lba48;
    int      read_ok;                  /* sector 0 leido por DMA */
    uint8_t  sector0[16];              /* primeros bytes del sector 0 */
    int      err;                      /* 0 ok; <0 codigo de fallo (ver ahci_err_str) */
};
int ahci_probe(struct ahci_info *o);                                  /* 0 = disco leido */
const char *ahci_err_str(int err);

/* PARCHE 041: el controlador queda inicializado tras ahci_probe (puerto en marcha, memoria DMA persistente de 64 KiB)
 * y expone lectura, escritura y vaciado de cache por sondeo. count <= AHCI_MAX_SECTORS por llamada. */
#define AHCI_MAX_SECTORS 128
int ahci_read(uint64_t lba, uint32_t count, void *buf);          /* 0 ok, <0 error (ver ahci_err_str) */
int ahci_write(uint64_t lba, uint32_t count, const void *buf);
int ahci_flush(void);

/* Autotest de escritura: SOLO sobre el disco de prueba de QEMU (modelo "QEMU HARDDISK" y sector 0 con la firma
 * SOMASATA). Escribe un patron en 8 sectores a partir de LBA 1024, lo lee y compara, restaura el contenido original y
 * mide la lectura secuencial de 1 MiB. */
struct ahci_selftest { int allowed, wrote_ok, verify_ok, restore_ok, sector0_ok; uint32_t read_ms, read_kib; int err; };
int ahci_selftest(struct ahci_selftest *t);                       /* 0 = todo correcto */
#endif
