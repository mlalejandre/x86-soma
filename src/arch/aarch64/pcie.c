#include "pcie.h"
#include "platform.h"
#include "pcie_proto.h"
#include "console.h"
#include "idt.h"
#include "soma_str.h"

/* PARCHE 062. Registros del root complex (Broadcom STB, como en el controlador pcie-brcmstb de Linux). Valores de la BCM2712 tomados del
 * emulador raspi5b y del driver de Linux; NO verificados en una placa real (no hay datasheet publico). */
#define R_RGR1_SW_INIT_1   0x9210u   /* bit 1: reset del puente */
#define R_EXT_CFG_INDEX    0x9000u
#define R_EXT_CFG_DATA     0x8000u
#define R_MISC_CTRL        0x4008u   /* bit 13: lecturas de config. sin dispositivo devuelven unos */
#define R_OUT_WIN0_LO      0x400Cu
#define R_OUT_WIN0_HI      0x4010u
#define R_OUT_BASE_LIMIT   0x4070u
#define R_OUT_BASE_HI      0x4080u
#define R_OUT_LIMIT_HI     0x4084u
#define R_RC_BAR2_LO       0x4034u
#define R_UBUS_CTRL        0x40A4u   /* bits 13 y 19: no abortar en errores de respuesta */
#define R_UBUS_BAR2_REMAP  0x40B4u
#define R_PCIE_CTRL        0x4064u   /* bit 2: PERST# liberado */
#define R_PCIE_STATUS      0x4068u   /* bits 4 y 5: enlace arriba y capa de enlace activa */
#define R_HARD_DEBUG       0x4304u   /* bit 3: PERST# forzado, bit 27: SerDes apagado */

/* PARCHE 067: dos root complex (0 = PCIe1, el conector externo; 1 = PCIe2, el enlace del RP1). Las funciones trabajan sobre el seleccionado
 * con pcie_select(); por defecto el 0, de modo que el resto del sistema no cambia. */
struct pcie_rc {
    uint64_t base, cpu;
    int init, up, maxbus, ndev, next_bus;
    struct pcie_dev devs[PCIE_MAX_DEVS];
    struct pcie_alloc mem;
};
static struct pcie_rc rcs[2] = { { .base = SOMA_PCIE_BASE, .cpu = SOMA_PCIE_MEM_CPU }, { .base = SOMA_PCIE2_BASE, .cpu = SOMA_PCIE2_MEM_CPU } };
static struct pcie_rc *s = &rcs[0];
#define g_init  (s->init)
#define g_rc    (s->up)
#define g_maxbus (s->maxbus)
#define devs    (s->devs)
#define ndev    (s->ndev)
#define mem     (s->mem)
#define next_bus (s->next_bus)

void pcie_select(int rc) { s = &rcs[rc == 1 ? 1 : 0]; }

static inline volatile uint32_t *reg(uint32_t off) { return (volatile uint32_t *)(uintptr_t)(s->base + off); }
static inline uint32_t rr(uint32_t off) { return *reg(off); }
static inline void wr(uint32_t off, uint32_t v) { *reg(off) = v; }
static inline void dsb(void) { __asm__ volatile ("dsb sy" ::: "memory"); }

int pcie_present(void) { return SOMA_PCIE_BASE != 0; }
uint64_t pcie_dma_offset(void) { return SOMA_PCIE_DMA_PCI; }

uint32_t pcie_cfg_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off)
{
    if (!g_rc || bus > g_maxbus || slot > 31 || fn > 7) return 0xFFFFFFFFu;
    uint8_t devfn = (uint8_t)((slot << 3) | fn);
    if (bus == 0) return devfn == 0 ? rr(off & 0xFFCu) : 0xFFFFFFFFu;           /* en el bus 0 solo esta el puerto raiz */
    wr(R_EXT_CFG_INDEX, pcie_ecfg_index(bus, devfn)); dsb();
    return rr(R_EXT_CFG_DATA + (off & 0xFFCu));
}

void pcie_cfg_write32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off, uint32_t v)
{
    if (!g_rc || bus > g_maxbus || slot > 31 || fn > 7) return;
    uint8_t devfn = (uint8_t)((slot << 3) | fn);
    if (bus == 0) { if (devfn == 0) wr(off & 0xFFCu, v); dsb(); return; }
    wr(R_EXT_CFG_INDEX, pcie_ecfg_index(bus, devfn)); dsb();
    wr(R_EXT_CFG_DATA + (off & 0xFFCu), v); dsb();
}

static void delay_ms(uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 100000000UL;
    while (timer_ticks - t0 < ms && --guard) __asm__ volatile ("yield");
}

static int link_wait(uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 100000000UL;
    while (((rr(R_PCIE_STATUS) >> 4) & 3u) != 3u) {
        if (timer_ticks - t0 > ms || !--guard) return -1;
        __asm__ volatile ("yield");
    }
    return 0;
}

static void cfg_bits(uint8_t bus, uint8_t devfn, uint8_t off, uint32_t clr, uint32_t set)
{
    uint32_t v = pcie_cfg_read32(bus, devfn >> 3, devfn & 7, off);
    pcie_cfg_write32(bus, devfn >> 3, devfn & 7, off, (v & ~clr) | set);
}

static void assign_bars(struct pcie_dev *d, int nbar)
{
    for (int i = 0; i < nbar; i++) {
        uint8_t off = (uint8_t)(0x10 + 4 * i);
        uint32_t old = pcie_cfg_read32(d->bus, d->devfn >> 3, d->devfn & 7, off);
        pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off, 0xFFFFFFFFu);
        uint32_t lo = pcie_cfg_read32(d->bus, d->devfn >> 3, d->devfn & 7, off), hi = 0;
        int w = ((lo >> 1) & 3u) == 2u && !(lo & 1u);
        if (w) {
            uint32_t oh = pcie_cfg_read32(d->bus, d->devfn >> 3, d->devfn & 7, off + 4);
            pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off + 4, 0xFFFFFFFFu);
            hi = pcie_cfg_read32(d->bus, d->devfn >> 3, d->devfn & 7, off + 4);
            pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off + 4, oh);
        }
        int is64 = 0, pf = 0;
        uint64_t size = pcie_bar_size(lo, hi, &is64, &pf), addr = 0;
        if (!size || pcie_alloc_bar(&mem, size, &addr) != 0) {
            pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off, old);        /* no existe, es de E/S o no cabe: se deja como estaba */
            if (w) i++;
            continue;
        }
        pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off, (uint32_t)addr | (lo & 0xF));
        if (w) pcie_cfg_write32(d->bus, d->devfn >> 3, d->devfn & 7, off + 4, (uint32_t)(addr >> 32));
        d->bar[i].addr = addr; d->bar[i].size = size; d->bar[i].is64 = (uint8_t)is64; d->bar[i].prefetch = (uint8_t)pf;
        if (w) i++;
    }
}

static void scan_bus(uint8_t bus, int depth)
{
    for (int slot = 0; slot < 32; slot++) {
        for (int fn = 0; fn < 8; fn++) {
            uint32_t id = pcie_cfg_read32(bus, (uint8_t)slot, (uint8_t)fn, 0);
            if ((id & 0xFFFFu) == 0xFFFFu || (id & 0xFFFFu) == 0) { if (fn == 0) break; continue; }
            uint8_t devfn = (uint8_t)((slot << 3) | fn);
            uint32_t cls = pcie_cfg_read32(bus, (uint8_t)slot, (uint8_t)fn, 8) >> 8;
            uint32_t hdr = (pcie_cfg_read32(bus, (uint8_t)slot, (uint8_t)fn, 0x0C) >> 16) & 0xFFu;
            if (ndev < PCIE_MAX_DEVS) {
                struct pcie_dev *d = &devs[ndev++];
                *d = (struct pcie_dev){ 0 };
                d->bus = bus; d->devfn = devfn; d->hdr = (uint8_t)(hdr & 0x7F); d->vid = (uint16_t)id; d->did = (uint16_t)(id >> 16); d->cls = cls;
                cfg_bits(bus, devfn, 0x04, 0x7, 0);                                  /* sin decodificar mientras se reparten los BAR */
                if ((hdr & 0x7F) == 0) {
                    assign_bars(d, 6);
                    cfg_bits(bus, devfn, 0x04, 0, 0x6);                              /* memoria + bus master */
                } else if ((hdr & 0x7F) == 1 && depth < 3 && next_bus < 255) {
                    assign_bars(d, 2);
                    uint8_t sec = (uint8_t)next_bus++;
                    if (sec > g_maxbus) g_maxbus = sec;
                    pcie_cfg_write32(bus, (uint8_t)slot, (uint8_t)fn, 0x18, (uint32_t)bus | ((uint32_t)sec << 8) | (0xFFu << 16));
                    mem.next = pcie_align_up(mem.next, 1u << 20);
                    uint64_t wbase = mem.next;
                    scan_bus(sec, depth + 1);
                    mem.next = pcie_align_up(mem.next, 1u << 20);
                    pcie_cfg_write32(bus, (uint8_t)slot, (uint8_t)fn, 0x18, (uint32_t)bus | ((uint32_t)sec << 8) | ((uint32_t)(next_bus - 1) << 16));
                    if (mem.next > wbase)
                        pcie_cfg_write32(bus, (uint8_t)slot, (uint8_t)fn, 0x20, (uint32_t)((wbase >> 16) & 0xFFF0u) | (uint32_t)(((mem.next - 1) >> 16) & 0xFFF0u) << 16);
                    else
                        pcie_cfg_write32(bus, (uint8_t)slot, (uint8_t)fn, 0x20, 0x0000FFF0u);   /* base > limite: ventana cerrada */
                    cfg_bits(bus, devfn, 0x04, 0, 0x6);
                }
            }
            if (fn == 0 && !(hdr & 0x80)) break;
        }
    }
}

static int rc_init(void)
{
    if (g_init) return ndev ? 0 : -1;
    g_init = 1;
    if (!s->base) return -1;
    g_rc = 1; g_maxbus = 1;
    wr(R_RGR1_SW_INIT_1, rr(R_RGR1_SW_INIT_1) | 2u); dsb(); delay_ms(2);              /* reset del puente y suelta */
    wr(R_RGR1_SW_INIT_1, rr(R_RGR1_SW_INIT_1) & ~2u); dsb(); delay_ms(2);
    wr(R_HARD_DEBUG, rr(R_HARD_DEBUG) & ~((1u << 3) | (1u << 27)));                     /* SerDes encendido, PERST# no forzado */
    wr(R_PCIE_CTRL, rr(R_PCIE_CTRL) | (1u << 2)); dsb();                              /* PERST# liberado */
    wr(R_MISC_CTRL, rr(R_MISC_CTRL) | (1u << 13));
    wr(R_UBUS_CTRL, rr(R_UBUS_CTRL) | (1u << 13) | (1u << 19));
    if (link_wait(250) != 0) { g_rc = 0; if (s == &rcs[0]) kprint("PCIe: sin enlace (nada conectado al conector)\n"); return -1; }
    struct pcie_outwin ow;                                                              /* CPU -> PCI: SOMA_PCIE_MEM_CPU .. +1 GiB  ->  PCI 0 */
    if (pcie_outwin_regs(s->cpu, 0, SOMA_PCIE_MEM_SIZE, &ow) != 0) { g_rc = 0; return -2; }
    wr(R_OUT_WIN0_LO, ow.lo); wr(R_OUT_WIN0_HI, ow.hi);
    wr(R_OUT_BASE_LIMIT, ow.base_limit); wr(R_OUT_BASE_HI, ow.base_hi); wr(R_OUT_LIMIT_HI, ow.limit_hi);
    uint32_t cfg[2], rm[2];                                                             /* PCI SOMA_PCIE_DMA_PCI .. +64 GiB -> RAM desde 0 */
    if (pcie_inbar_regs(SOMA_PCIE_DMA_PCI, 1ULL << 36, 0, cfg, rm) != 0) { g_rc = 0; return -2; }
    wr(R_RC_BAR2_LO, cfg[0]); wr(R_RC_BAR2_LO + 4, cfg[1]); wr(R_UBUS_BAR2_REMAP, rm[0]); wr(R_UBUS_BAR2_REMAP + 4, rm[1]);
    dsb();
    /* Puerto raiz: bus 0 -> secundario 1; ventana de memoria PCI 1 MiB .. 1 GiB; memoria y bus master */
    pcie_cfg_write32(0, 0, 0, 0x18, 0x00FF0100u);
    pcie_cfg_write32(0, 0, 0, 0x20, 0x3FF00010u);
    cfg_bits(0, 0, 0x04, 0, 0x6);
    mem.next = 0x100000; mem.limit = SOMA_PCIE_MEM_SIZE;
    next_bus = 2; ndev = 0;
    scan_bus(1, 0);
    pcie_cfg_write32(0, 0, 0, 0x18, 0x00000100u | ((uint32_t)(next_bus - 1) << 16));
    if (!ndev) return -1;
    return 0;
}

int pcie_init(void) { struct pcie_rc *o = s; s = &rcs[0]; int r = rc_init(); s = o; return r; }
int pcie_init_rc(int rc) { struct pcie_rc *o = s; s = &rcs[rc == 1 ? 1 : 0]; int r = rc_init(); s = o; return r; }

int pcie_dev_count(void) { return ndev; }
const struct pcie_dev *pcie_dev_get(int i) { return i >= 0 && i < ndev ? &devs[i] : 0; }

void pcie_format(char *out, uint32_t max)
{
    uint32_t p = 0;
    if (max) out[0] = 0;
    if (!pcie_present()) { soma_str_put(out, max, &p, "PCIe: esta plataforma no tiene root complex (usa el comando de x86 o ECAM de QEMU virt)\n"); return; }
    struct pcie_rc *saved = s;
    for (int r = 0; r < 2; r++) {
    s = &rcs[r];
    if (!s->base) continue;
    if (!g_rc) { if (r == 0) soma_str_put(out, max, &p, "PCIe1: sin enlace (nada conectado)\n"); continue; }
    soma_str_put(out, max, &p, r == 0 ? "PCIe1 (BCM2712): enlace activo\n" : "PCIe2 (BCM2712, enlace del RP1): enlace activo\n");
    for (int i = 0; i < ndev; i++) {
        const struct pcie_dev *d = &devs[i];
        soma_str_put(out, max, &p, "  "); soma_str_put_hex(out, max, &p, d->bus, 2, 0); soma_str_put(out, max, &p, ":"); soma_str_put_hex(out, max, &p, d->devfn >> 3, 2, 0);
        soma_str_put(out, max, &p, "."); soma_str_put_hex(out, max, &p, d->devfn & 7, 1, 0); soma_str_put(out, max, &p, "  ");
        soma_str_put_hex(out, max, &p, d->vid, 4, 0); soma_str_put(out, max, &p, ":"); soma_str_put_hex(out, max, &p, d->did, 4, 0); soma_str_put(out, max, &p, "  ");
        soma_str_put(out, max, &p, pcie_class_name(d->cls)); soma_str_put(out, max, &p, "\n");
        for (int b = 0; b < 6; b++) if (d->bar[b].size) {
            soma_str_put(out, max, &p, "        BAR"); char n[2] = { (char)('0' + b), 0 }; soma_str_put(out, max, &p, n);
            soma_str_put(out, max, &p, " 0x"); soma_str_put_hex(out, max, &p, d->bar[b].addr, 8, 0); soma_str_put(out, max, &p, " (");
            uint64_t sz = d->bar[b].size; char u = 'B'; if (sz >= 1024) { sz >>= 10; u = 'K'; } if (sz >= 1024) { sz >>= 10; u = 'M'; }
            char num[12]; int k = 0; if (!sz) num[k++] = '0'; while (sz && k < 10) { num[k++] = (char)('0' + sz % 10); sz /= 10; }
            while (k) { char c[2] = { num[--k], 0 }; soma_str_put(out, max, &p, c); }
            char us[3] = { u, u == 'B' ? 0 : 'i', 0 }; soma_str_put(out, max, &p, us); if (u != 'B') soma_str_put(out, max, &p, "B"); soma_str_put(out, max, &p, ")\n");
        }
    }
    }
    s = saved;
}
