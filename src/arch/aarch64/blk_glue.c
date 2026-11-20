/* PARCHE 061: registra en la capa de bloque los discos de aarch64 (virtio-blk en QEMU virt, tarjeta SD y disco USB en la Pi 5). */
#include "blk.h"
#include "sysdisk.h"
#include "sdhci.h"
#include "xhci.h"

static int v_read(uint64_t lba, uint32_t n, void *b) { return sysdisk_read_n(lba, b, n); }
static int s_read(uint64_t lba, uint32_t n, void *b) { return sd_read(lba, n, b); }
static int s_write(uint64_t lba, uint32_t n, const void *b) { return sd_write(lba, n, b); }

static int u_read(uint64_t lba, uint32_t n, void *b) { return xhci_msc_read(lba, n, b); }
static int u_write(uint64_t lba, uint32_t n, const void *b) { return xhci_msc_write(lba, n, b); }

static int sig_is(const uint8_t *s0, const char *sig) { for (int i = 0; i < 8; i++) if (s0[i] != (uint8_t)sig[i]) return 0; return 1; }

int blk_scan(void)
{
    blk_reset();
    uint64_t cap = sysdisk_capacity();
    if (cap) { struct blkdev d = { "vda", "virtio-blk", cap, v_read, 0, 0 }; blk_register(&d); }
    struct sd_info si;
    if (sd_probe(&si) == 0) {                                                    /* solo escribe si es la tarjeta de prueba de QEMU (firma SOMASD--) */
        struct blkdev d = { "sd0", "sdhci", si.sectors, s_read, s_write, sig_is(si.sector0, "SOMASD--") };
        blk_register(&d);
    }
    if (xhci_msc_ready()) {                                                      /* PARCHE 062: disco USB por PCIe; solo escribe si es el de prueba (QEMU + firma SOMAUSB-) */
        const struct xhci_msc_info *ui = xhci_msc_get();
        int qemu = ui->vendor[0] == 'Q' && ui->vendor[1] == 'E' && ui->vendor[2] == 'M' && ui->vendor[3] == 'U';
        struct blkdev d = { "usb0", "usb-msc", ui->sectors, u_read, u_write, qemu && sig_is(ui->sector0, "SOMAUSB-") };
        blk_register(&d);
    }
    return blk_count();
}
