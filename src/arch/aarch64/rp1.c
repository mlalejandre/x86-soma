#include "rp1.h"
#include "pcie.h"
#include "platform.h"
#include "rp1_proto.h"
#include "console.h"
#include "soma_str.h"
/* PARCHE 067: busca el RP1 en PCIe2, toma su BAR1 (perifericos) y lee SYSINFO. */
static int st;                                      /* 0 = sin intentar, 1 = encontrado, -1 = no hay */
static uint64_t bar1_cpu, bar1_size;
static uint32_t chip_id;

int rp1_init(void)
{
    if (st) return st > 0 ? 0 : -1;
    st = -1;
    if (!SOMA_PCIE2_BASE) return -1;
    if (pcie_init_rc(1) != 0) return -1;
    pcie_select(1);
    for (int i = 0; i < pcie_dev_count(); i++) {
        const struct pcie_dev *d = pcie_dev_get(i);
        if (d && rp1_is_rp1(d->vid, d->did) && d->bar[1].size >= RP1_BAR1_MIN) {
            bar1_cpu = d->bar[1].addr + SOMA_PCIE2_MEM_CPU; bar1_size = d->bar[1].size;
            break;
        }
    }
    pcie_select(0);
    if (!bar1_cpu) return -1;
    uint64_t si = rp1_periph_cpu(bar1_cpu, bar1_size, RP1_SYSINFO);
    if (si) chip_id = *(volatile uint32_t *)(uintptr_t)si;
    st = 1;
    return 0;
}

int rp1_present(void) { return st > 0; }
uint64_t rp1_bar1_cpu(void) { return st > 0 ? bar1_cpu : 0; }
uint32_t rp1_chip_id(void) { return chip_id; }
uint64_t rp1_periph_base(int id) { return st > 0 ? rp1_periph_cpu(bar1_cpu, bar1_size, id) : 0; }
uint64_t rp1_xhci_base(int n)
{
    if (st <= 0 || n < 0 || n > 1) return 0;
    return rp1_periph_cpu(bar1_cpu, bar1_size, n ? RP1_USB1 : RP1_USB0);
}

void rp1_format(char *out, uint32_t max)
{
    uint32_t p = 0;
    if (max) out[0] = 0;
    if (rp1_init() != 0) { soma_str_put(out, max, &p, "RP1: no detectado en PCIe2 (QEMU raspi5b no lo emula)\n"); return; }
    soma_str_put(out, max, &p, "RP1: BAR1 en 0x"); soma_str_put_hex(out, max, &p, bar1_cpu, 10, 0); soma_str_put(out, max, &p, ", SYSINFO 0x"); soma_str_put_hex(out, max, &p, chip_id, 8, 0); soma_str_put(out, max, &p, "\n");
    for (int i = RP1_USB0; i <= RP1_USB1; i++) {
        soma_str_put(out, max, &p, "  "); soma_str_put(out, max, &p, rp1_periph_name(i)); soma_str_put(out, max, &p, " en 0x");
        soma_str_put_hex(out, max, &p, rp1_periph_cpu(bar1_cpu, bar1_size, i), 10, 0); soma_str_put(out, max, &p, "\n");
    }
}
