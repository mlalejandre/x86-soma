/* PARCHE 045: registra en la capa de bloque los discos que hay en el sistema (virtio-blk, AHCI, NVMe). Solo x86_64. */
#include "blk.h"
#include "sysdisk.h"
#include "ahci.h"
#include "nvme.h"
#include "xhci.h"

static int v_read(uint64_t lba, uint32_t n, void *b) { return sysdisk_read_n(lba, b, n); }
static int a_read(uint64_t lba, uint32_t n, void *b) { return ahci_read(lba, n, b); }
static int a_write(uint64_t lba, uint32_t n, const void *b) { return ahci_write(lba, n, b); }
static int n_read(uint64_t lba, uint32_t n, void *b) { return nvme_read(lba, n, b); }
static int n_write(uint64_t lba, uint32_t n, const void *b) { return nvme_write(lba, n, b); }

static int u_read(uint64_t lba, uint32_t n, void *b) { return xhci_msc_read(lba, n, b); }
static int u_write(uint64_t lba, uint32_t n, const void *b) { return xhci_msc_write(lba, n, b); }

static int sig_ok(const uint8_t *s0, const char *model, const char *sig)
{
    if (!(model[0] == 'Q' && model[1] == 'E' && model[2] == 'M' && model[3] == 'U')) return 0;
    for (int i = 0; i < 8; i++) if (s0[i] != (uint8_t)sig[i]) return 0;
    return 1;
}

int blk_scan(void)
{
    static struct ahci_info ai; static struct nvme_info ni;
    blk_reset();
    uint64_t cap = sysdisk_capacity();
    if (cap) { struct blkdev d = { "vda", "virtio-blk", cap, v_read, 0, 0 }; blk_register(&d); }   /* disco del sistema: solo lectura aqui */
    if (ahci_probe(&ai) == 0) { struct blkdev d = { "sda", "ahci", ai.sectors, a_read, a_write, sig_ok(ai.sector0, ai.model, "SOMASATA") }; blk_register(&d); }
    if (nvme_probe(&ni) == 0) { struct blkdev d = { "nvme0", "nvme", ni.sectors, n_read, n_write, sig_ok(ni.sector0, ni.model, "SOMANVME") }; blk_register(&d); }
    if (xhci_msc_ready()) {                                                              /* PARCHE 049: USB; solo escribe si es el disco de prueba de QEMU */
        const struct xhci_msc_info *ui = xhci_msc_get();
        struct blkdev d = { "usb0", "usb-msc", ui->sectors, u_read, u_write, sig_ok(ui->sector0, ui->vendor, "SOMAUSB-") };
        blk_register(&d);
    }
    return blk_count();
}
