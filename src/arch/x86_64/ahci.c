/* PARCHE 040: ver ahci.h. Sondeo (sin interrupciones), un puerto, ranura 0, una pagina de DMA por estructura. */
#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "idt.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

enum { E_NO_CTRL = -1, E_BAR = -2, E_MAP = -3, E_NO_DISK = -4, E_MEM = -5, E_STOP = -6, E_BUSY = -7, E_CMD = -8, E_TFES = -9, E_TIMEOUT = -10 };

const char *ahci_err_str(int e)
{
    switch (e) {
    case 0: return "OK";
    case E_NO_CTRL: return "no hay controlador AHCI (en QEMU: SOMA_SATA=1 python3 ejecutar.py)";
    case E_BAR: return "BAR5 invalido";
    case E_MAP: return "no se pudo mapear el MMIO";
    case E_NO_DISK: return "ningun puerto con disco ATA";
    case E_MEM: return "sin memoria DMA";
    case E_STOP: return "el puerto no se detiene";
    case E_BUSY: return "el disco sigue ocupado";
    case E_CMD: return "el comando no se completa";
    case E_TFES: return "error del dispositivo (TFES)";
    case E_TIMEOUT: return "tiempo agotado";
    default: return "error";
    }
}

static volatile uint32_t *hba;        /* registros del HBA */
#define H(off) (hba[(off) / 4])
#define P(n, off) (hba[(0x100 + (n) * 0x80 + (off)) / 4])

/* espera hasta `ms` milisegundos del PIT (el sondeo tiene tope de iteraciones por si el PIT no avanza) */
static int wait_clear(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 200000000UL;
    while ((*reg & mask) && --guard) { if (timer_ticks - t0 > ms) return -1; __asm__ volatile ("pause"); }
    return guard ? 0 : -1;
}

static int map_hba(uint32_t abar)
{
    for (uint32_t off = 0; off < 0x2000; off += 0x1000)
        if (vmm_map_page(abar + off, abar + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) return -1;
    return 0;
}

static int port_stop(int n)
{
    P(n, 0x18) &= ~0x1u;                                     /* ST = 0 */
    P(n, 0x18) &= ~0x10u;                                    /* FRE = 0 */
    if (wait_clear(&P(n, 0x18), 0x8000u | 0x4000u, 500) != 0) return -1;   /* CR y FR */
    return 0;
}

static int port_start(int n)
{
    if (wait_clear(&P(n, 0x18), 0x8000u, 500) != 0) return -1;
    P(n, 0x18) |= 0x10u;                                     /* FRE */
    P(n, 0x18) |= 0x1u;                                      /* ST */
    return 0;
}

/* dma: pagina 0 = CLB(0x000) + FIS(0x400) + CT(0x800); paginas 1..16 = datos (64 KiB contiguos). PARCHE 041: persistentes. */
static uint8_t *dma_pg, *dma_data;
static int g_port = -1, g_lba48;
static uint64_t g_sectors;
#define DMA_FRAMES 17

static int issue(int n, uint8_t cmd, uint64_t lba, uint32_t count, int lba_mode48, int write, int has_data)
{
    volatile uint32_t *clb = (volatile uint32_t *)dma_pg;
    uint8_t *ct = dma_pg + 0x800;
    for (int i = 0; i < 0x80 + 16; i++) ct[i] = 0;
    ct[0] = 0x27; ct[1] = 0x80; ct[2] = cmd; ct[7] = 0x40;                      /* H2D, C=1, comando, dispositivo LBA */
    ct[4] = (uint8_t)lba; ct[5] = (uint8_t)(lba >> 8); ct[6] = (uint8_t)(lba >> 16);
    if (lba_mode48) { ct[8] = (uint8_t)(lba >> 24); ct[9] = (uint8_t)(lba >> 32); ct[10] = (uint8_t)(lba >> 40); }
    ct[12] = (uint8_t)count; ct[13] = (uint8_t)(count >> 8);
    if (has_data) {
        volatile uint32_t *prd = (volatile uint32_t *)(ct + 0x80);
        prd[0] = (uint32_t)(uintptr_t)dma_data; prd[1] = 0; prd[2] = 0; prd[3] = (count * 512u - 1u);
    }
    clb[0] = 5u | (write ? 1u << 6 : 0) | (has_data ? 1u << 16 : 0);            /* CFL=5 dwords, W, PRDTL */
    clb[1] = 0; clb[2] = (uint32_t)(uintptr_t)ct; clb[3] = 0;
    P(n, 0x10) = 0xFFFFFFFFu; P(n, 0x30) = 0xFFFFFFFFu;                         /* limpia IS y SERR */
    if (wait_clear(&P(n, 0x20), 0x80u | 0x08u, 1000) != 0) return E_BUSY;       /* TFD.BSY / DRQ */
    P(n, 0x38) = 1u;                                                            /* CI: ranura 0 */
    uint64_t t0 = timer_ticks; unsigned long guard = 400000000UL;
    while ((P(n, 0x38) & 1u) && --guard) {
        if (P(n, 0x10) & (1u << 30)) return E_TFES;
        if (timer_ticks - t0 > 3000) return E_TIMEOUT;
        __asm__ volatile ("pause");
    }
    if (!guard) return E_TIMEOUT;
    if (P(n, 0x10) & (1u << 30) || (P(n, 0x20) & 1u)) return E_TFES;
    return 0;
}

int ahci_probe(struct ahci_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    o->port = -1;
    for (int bus = 0; bus < 256 && !o->found; bus++)
        for (int slot = 0; slot < 32 && !o->found; slot++) {
            if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0) == 0xFFFF) continue;
            int multi = (int)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0x80);
            for (int fn = 0; fn < (multi ? 8 : 1); fn++) {
                if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn) == 0xFFFF) continue;
                uint32_t cc = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x08) >> 8;
                if (cc == 0x010601u) { o->found = 1; o->bus = (uint8_t)bus; o->slot = (uint8_t)slot; o->fn = (uint8_t)fn; break; }
            }
        }
    if (!o->found) return o->err = E_NO_CTRL;
    uint32_t cmd = pci_config_read32(o->bus, o->slot, o->fn, 0x04);
    pci_config_write32(o->bus, o->slot, o->fn, 0x04, (cmd & 0xFFFFu) | 0x6u);        /* memoria + maestro de bus */
    uint32_t bar5 = pci_config_read32(o->bus, o->slot, o->fn, 0x24);
    if ((bar5 & 1u) || (bar5 & 0xFFFFF000u) < 0xC0000000u || (bar5 & 0xFFFFF000u) > 0xFFFF0000u) return o->err = E_BAR;
    o->abar = bar5 & 0xFFFFF000u;
    if (map_hba(o->abar) != 0) return o->err = E_MAP;
    hba = (volatile uint32_t *)(uintptr_t)o->abar;
    H(0x04) |= 0x80000000u;                                                          /* GHC.AE */
    o->cap = H(0x00); o->pi = H(0x0C); o->vs = H(0x10);
    for (int n = 0; n < 32; n++) {
        if (!(o->pi & (1u << n))) continue;
        o->nports_impl++;
        uint32_t ssts = P(n, 0x28), sig = P(n, 0x24);
        if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) { o->port_state[n] = 0; continue; }
        o->port_state[n] = sig == 0x00000101u ? 1 : sig == 0xEB140101u ? 2 : 3;
        if (o->port_state[n] == 1 && o->port < 0) o->port = n;
    }
    if (o->port < 0) return o->err = E_NO_DISK;
    int n = o->port;
    if (g_port != n) {                                                               /* primera vez: memoria DMA y puerto en marcha */
        uintptr_t pg = pmm_alloc_contig(DMA_FRAMES);
        if (!pg) return o->err = E_MEM;
        dma_pg = (uint8_t *)pg; dma_data = dma_pg + 4096;
        for (int i = 0; i < 4096; i++) dma_pg[i] = 0;
        if (port_stop(n) != 0) { pmm_free_contig(pg, DMA_FRAMES); return o->err = E_STOP; }
        P(n, 0x00) = (uint32_t)pg; P(n, 0x04) = 0;                                   /* CLB */
        P(n, 0x08) = (uint32_t)pg + 0x400; P(n, 0x0C) = 0;                           /* FB */
        P(n, 0x14) = 0;                                                              /* sin interrupciones: sondeo */
        if (port_start(n) != 0) { pmm_free_contig(pg, DMA_FRAMES); return o->err = E_STOP; }
    }
    int r = issue(n, 0xEC, 0, 1, 0, 0, 1);                                           /* IDENTIFY DEVICE */
    if (r == 0) {
        const uint16_t *id = (const uint16_t *)dma_data;
        for (int i = 0; i < 20; i++) { o->model[2 * i] = (char)(id[27 + i] >> 8); o->model[2 * i + 1] = (char)id[27 + i]; }
        o->model[40] = 0;
        for (int i = 39; i >= 0 && (o->model[i] == ' ' || o->model[i] == 0); i--) o->model[i] = 0;
        for (int i = 0; o->model[i]; i++) if ((uint8_t)o->model[i] < 32 || (uint8_t)o->model[i] > 126) o->model[i] = '?';
        o->lba48 = (id[83] >> 10) & 1;
        o->sectors = o->lba48 ? ((uint64_t)id[103] << 48 | (uint64_t)id[102] << 32 | (uint64_t)id[101] << 16 | id[100])
                              : ((uint64_t)id[61] << 16 | id[60]);
        if (!o->lba48) { r = E_NO_DISK; }                                            /* la E/S usa comandos EXT: exige LBA48 */
        else {
            g_port = n; g_lba48 = 1; g_sectors = o->sectors;
            static uint8_t s0buf[512];                                              /* ahci_read copia SIEMPRE count*512 bytes */
            r = ahci_read(0, 1, s0buf);
            if (r == 0) { o->read_ok = 1; for (int i = 0; i < 16; i++) o->sector0[i] = s0buf[i]; }
        }
    }
    return o->err = r;
}

/* ---- PARCHE 041: E/S ---- */
static void copy_bytes(void *d, const void *s2, uint32_t n)
{
    uint64_t *dd = (uint64_t *)d; const uint64_t *ss = (const uint64_t *)s2;
    uint32_t w = n / 8;
    if ((((uintptr_t)d | (uintptr_t)s2) & 7) == 0) { for (uint32_t i = 0; i < w; i++) dd[i] = ss[i]; for (uint32_t i = w * 8; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s2)[i]; }
    else for (uint32_t i = 0; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s2)[i];
}

int ahci_read(uint64_t lba, uint32_t count, void *buf)
{
    if (g_port < 0 && !(dma_pg && dma_data)) return E_NO_DISK;
    if (count == 0 || count > AHCI_MAX_SECTORS) return E_CMD;
    if (g_sectors && (lba >= g_sectors || count > g_sectors - lba)) return E_CMD;
    int r = issue(g_port >= 0 ? g_port : 0, 0x25, lba, count, 1, 0, 1);              /* READ DMA EXT */
    if (r == 0) copy_bytes(buf, dma_data, count * 512u);
    return r;
}

int ahci_write(uint64_t lba, uint32_t count, const void *buf)
{
    if (g_port < 0) return E_NO_DISK;
    if (count == 0 || count > AHCI_MAX_SECTORS) return E_CMD;
    if (lba >= g_sectors || count > g_sectors - lba) return E_CMD;
    copy_bytes(dma_data, buf, count * 512u);
    return issue(g_port, 0x35, lba, count, 1, 1, 1);                                  /* WRITE DMA EXT */
}

int ahci_flush(void)
{
    if (g_port < 0) return E_NO_DISK;
    return issue(g_port, 0xEA, 0, 0, 1, 0, 0);                                        /* FLUSH CACHE EXT */
}

int ahci_selftest(struct ahci_selftest *t)
{
    static uint8_t orig[8 * 512], pat[8 * 512], back[8 * 512], s0[512], big[AHCI_MAX_SECTORS * 512];
    for (unsigned i = 0; i < sizeof *t; i++) ((uint8_t *)t)[i] = 0;
    static struct ahci_info inf;
    int r = ahci_probe(&inf);
    if (r != 0) return t->err = r;
    static const char sig[] = "SOMASATA";
    int okm = inf.model[0] == 'Q' && inf.model[1] == 'E' && inf.model[2] == 'M' && inf.model[3] == 'U';
    int oks = 1; for (int i = 0; i < 8; i++) if (inf.sector0[i] != (uint8_t)sig[i]) oks = 0;
    t->allowed = okm && oks;
    if (!t->allowed) return t->err = -11;                                              /* negativa: no es el disco de prueba */
    if (inf.sectors < 4096) return t->err = E_CMD;
    if ((r = ahci_read(0, 1, s0)) != 0) return t->err = r;
    if ((r = ahci_read(1024, 8, orig)) != 0) return t->err = r;
    for (int i = 0; i < 8 * 512; i++) pat[i] = (uint8_t)(i * 31u + (i >> 9) * 7u + 0x5A);
    if ((r = ahci_write(1024, 8, pat)) != 0) return t->err = r;
    if ((r = ahci_flush()) != 0) return t->err = r;
    t->wrote_ok = 1;
    if ((r = ahci_read(1024, 8, back)) != 0) return t->err = r;
    t->verify_ok = 1; for (int i = 0; i < 8 * 512; i++) if (back[i] != pat[i]) t->verify_ok = 0;
    r = ahci_write(1024, 8, orig);                                                     /* restaura siempre */
    if (r == 0) r = ahci_flush();
    if (r == 0) r = ahci_read(1024, 8, back);
    if (r != 0) return t->err = r;
    t->restore_ok = 1; for (int i = 0; i < 8 * 512; i++) if (back[i] != orig[i]) t->restore_ok = 0;
    if ((r = ahci_read(0, 1, back)) != 0) return t->err = r;
    t->sector0_ok = 1; for (int i = 0; i < 512; i++) if (back[i] != s0[i]) t->sector0_ok = 0;
    uint64_t t0 = timer_ticks;
    for (int i = 0; i < 8; i++) if ((r = ahci_read((uint64_t)i * AHCI_MAX_SECTORS, AHCI_MAX_SECTORS, big)) != 0) return t->err = r;
    t->read_ms = (uint32_t)(timer_ticks - t0); t->read_kib = 8u * AHCI_MAX_SECTORS * 512u / 1024u;
    return (t->verify_ok && t->restore_ok && t->sector0_ok) ? 0 : (t->err = E_CMD);
}
