/* PARCHE 043: ver nvme.h. Sondeo, un solo hilo de uso (el shell). Memoria DMA contigua y persistente (20 marcos). */
#include "nvme.h"
#include "nvme_proto.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "idt.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
enum { E_NO_CTRL = -1, E_BAR = -2, E_MAP = -3, E_CAP = -4, E_MEM = -5, E_DISABLE = -6, E_ENABLE = -7, E_ADMIN = -8, E_NS = -9, E_IO = -10, E_TIMEOUT = -11, E_ARG = -12 };

const char *nvme_err_str(int e)
{
    switch (e) {
    case 0: return "OK";
    case E_NO_CTRL: return "no hay controlador NVMe (en QEMU: SOMA_NVME=1 python3 ejecutar.py)";
    case E_BAR: return "BAR0 invalido";
    case E_MAP: return "no se pudo mapear el MMIO";
    case E_CAP: return "capacidades no soportadas (conjunto NVM / pagina 4 KiB)";
    case E_MEM: return "sin memoria DMA";
    case E_DISABLE: return "el controlador no se desactiva";
    case E_ENABLE: return "el controlador no se activa (CSTS.RDY / CFS)";
    case E_ADMIN: return "orden de administracion fallida";
    case E_NS: return "espacio de nombres 1 no utilizable (se exigen sectores de 512 bytes)";
    case E_IO: return "error de E/S del dispositivo";
    case E_TIMEOUT: return "tiempo agotado";
    case E_ARG: return "argumentos invalidos";
    default: return "error";
    }
}

static volatile uint8_t *regs;
static inline uint32_t r32(uint32_t off) { return *(volatile uint32_t *)(regs + off); }
static inline void w32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(regs + off) = v; }

/* Cada cola ocupa una pagina propia: la especificacion exige que ASQ/ACQ y las colas de E/S (con PC=1) esten alineadas
 * a la pagina del controlador (4 KiB); QEMU rechaza el arranque si no lo estan (CSTS.RDY no llega a 1). */
#define OFF_ADMIN_SQ 0x0000
#define OFF_ADMIN_CQ 0x1000
#define OFF_IO_SQ    0x2000
#define OFF_IO_CQ    0x3000
#define OFF_IDENT    0x4000
#define OFF_LIST     0x5000
#define OFF_DATA     0x6000
#define DMA_FRAMES   22                    /* 6 paginas de control + 16 de datos (64 KiB) */
_Static_assert(((OFF_ADMIN_SQ | OFF_ADMIN_CQ | OFF_IO_SQ | OFF_IO_CQ | OFF_IDENT | OFF_LIST | OFF_DATA) & 0xFFF) == 0, "estructuras NVMe sin alinear a pagina");
#define QDEPTH 16

static uint8_t *dma;
static struct nvme_q aq, ioq;
static uint16_t aq_sqhd, io_sqhd;
static int g_ready;
static uint64_t g_sectors;

static int wait_rdy(uint32_t want, uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 300000000UL;
    while (((r32(0x1C) & 1u) != want) && --guard) {
        if (timer_ticks - t0 > ms) return -1;
        __asm__ volatile ("pause");
    }
    return guard ? 0 : -1;
}

static int run(struct nvme_q *q, uint16_t *sqhd, struct nvme_sqe *e, struct nvme_cqe *c)
{
    int cid = nvme_submit(q, e, *sqhd);
    if (cid < 0) return E_TIMEOUT;
    uint64_t t0 = timer_ticks; unsigned long guard = 300000000UL;
    for (;;) {
        if (nvme_poll(q, c)) break;
        if (!--guard || timer_ticks - t0 > 3000) return E_TIMEOUT;
        __asm__ volatile ("pause");
    }
    *sqhd = c->sqhd;
    if (c->cid != (uint16_t)cid) return E_ADMIN;
    return nvme_status_ok(c) ? 0 : (q == &aq ? E_ADMIN : E_IO);
}

static void clean_str(char *d, const uint8_t *s, int n)
{
    int last = -1;
    for (int i = 0; i < n; i++) { uint8_t c = s[i]; d[i] = (c >= 32 && c < 127) ? (char)c : (c ? '?' : ' '); if (d[i] != ' ') last = i; }
    d[last + 1] = 0;
}

int nvme_probe(struct nvme_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    for (int bus = 0; bus < 256 && !o->found; bus++)
        for (int slot = 0; slot < 32 && !o->found; slot++) {
            if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0) == 0xFFFF) continue;
            int multi = (int)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0x80);
            for (int fn = 0; fn < (multi ? 8 : 1); fn++) {
                if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn) == 0xFFFF) continue;
                if ((pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x08) >> 8) == 0x010802u) { o->found = 1; o->bus = (uint8_t)bus; o->slot = (uint8_t)slot; o->fn = (uint8_t)fn; break; }
            }
        }
    if (!o->found) return o->err = E_NO_CTRL;
    uint32_t cmd = pci_config_read32(o->bus, o->slot, o->fn, 0x04);
    pci_config_write32(o->bus, o->slot, o->fn, 0x04, (cmd & 0xFFFFu) | 0x6u);
    uint32_t b0 = pci_config_read32(o->bus, o->slot, o->fn, 0x10);
    if (b0 & 1u) return o->err = E_BAR;
    uint64_t bar = b0 & 0xFFFFFFF0u;
    if (((b0 >> 1) & 3u) == 2u) bar |= (uint64_t)pci_config_read32(o->bus, o->slot, o->fn, 0x14) << 32;
    if (bar < 0x40000000ULL) return o->err = E_BAR;                       /* debe quedar fuera del mapa de identidad de 1 GiB */
    o->bar = bar;
    if (!g_ready) {
        for (uint64_t off = 0; off < 0x2000; off += 0x1000)
            if (vmm_map_page(bar + off, bar + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) return o->err = E_MAP;
        regs = (volatile uint8_t *)(uintptr_t)bar;
    }
    uint32_t cap_lo = r32(0x00), cap_hi = r32(0x04);
    o->vs = r32(0x08); o->mqes = (cap_lo & 0xFFFF) + 1; o->to_ms = ((cap_lo >> 24) & 0xFF) * 500u; o->dstrd = cap_hi & 0xF;
    if (!(((cap_hi >> 5) & 1u)) || ((cap_hi >> 16) & 0xF) != 0 || o->dstrd > 4 || o->mqes < QDEPTH) return o->err = E_CAP;
    uint32_t tmo = o->to_ms < 1000 ? 1000 : (o->to_ms > 4000 ? 4000 : o->to_ms);
    uint32_t stride = 4u << o->dstrd;

    if (!g_ready) {
        uintptr_t pg = pmm_alloc_contig(DMA_FRAMES);
        if (!pg) return o->err = E_MEM;
        dma = (uint8_t *)pg;
        for (uint32_t i = 0; i < OFF_DATA; i++) dma[i] = 0;
        w32(0x14, r32(0x14) & ~1u);                                          /* CC.EN = 0 */
        if (wait_rdy(0, tmo) != 0) { pmm_free_contig(pg, DMA_FRAMES); return o->err = E_DISABLE; }
        nvme_q_init(&aq, (struct nvme_sqe *)(dma + OFF_ADMIN_SQ), (struct nvme_cqe *)(dma + OFF_ADMIN_CQ),
                    (volatile uint32_t *)(regs + 0x1000), (volatile uint32_t *)(regs + 0x1000 + stride), QDEPTH);
        aq_sqhd = 0;
        w32(0x24, (QDEPTH - 1u) | ((QDEPTH - 1u) << 16));                    /* AQA */
        w32(0x28, (uint32_t)(uintptr_t)(dma + OFF_ADMIN_SQ)); w32(0x2C, 0);  /* ASQ */
        w32(0x30, (uint32_t)(uintptr_t)(dma + OFF_ADMIN_CQ)); w32(0x34, 0);  /* ACQ */
        w32(0x14, 1u | (6u << 16) | (4u << 20));                             /* EN, IOSQES=64 B, IOCQES=16 B, pagina 4 KiB */
        if (wait_rdy(1, tmo) != 0 || (r32(0x1C) & 2u)) {
            w32(0x14, r32(0x14) & ~1u);                                       /* deja el controlador desactivado y libera la memoria */
            pmm_free_contig(pg, DMA_FRAMES); dma = 0;
            return o->err = E_ENABLE;
        }
    }
    struct nvme_sqe e; struct nvme_cqe c; int r;
    /* IDENTIFY controlador */
    for (unsigned i = 0; i < sizeof e; i++) ((uint8_t *)&e)[i] = 0;
    e.cdw0 = NVME_ADMIN_IDENTIFY; e.prp1 = (uint64_t)(uintptr_t)(dma + OFF_IDENT); e.cdw10 = 1;
    if ((r = run(&aq, &aq_sqhd, &e, &c)) != 0) return o->err = r;
    clean_str(o->model, dma + OFF_IDENT + 24, 40); clean_str(o->serial, dma + OFF_IDENT + 4, 20);
    /* IDENTIFY espacio de nombres 1 */
    for (unsigned i = 0; i < sizeof e; i++) ((uint8_t *)&e)[i] = 0;
    e.cdw0 = NVME_ADMIN_IDENTIFY; e.nsid = 1; e.prp1 = (uint64_t)(uintptr_t)(dma + OFF_IDENT); e.cdw10 = 0;
    if ((r = run(&aq, &aq_sqhd, &e, &c)) != 0) return o->err = r;
    const uint8_t *ns = dma + OFF_IDENT;
    o->sectors = (uint64_t)ns[0] | (uint64_t)ns[1] << 8 | (uint64_t)ns[2] << 16 | (uint64_t)ns[3] << 24 | (uint64_t)ns[4] << 32 | (uint64_t)ns[5] << 40 | (uint64_t)ns[6] << 48 | (uint64_t)ns[7] << 56;
    uint32_t lbads = ns[128 + 4 * (ns[26] & 0xF) + 2];
    if (lbads != 9 || o->sectors == 0) return o->err = E_NS;
    o->lba_size = 1u << lbads;
    if (!g_ready) {
        /* colas de E/S: primero la de finalizacion y luego la de envio */
        for (unsigned i = 0; i < sizeof e; i++) ((uint8_t *)&e)[i] = 0;
        e.cdw0 = NVME_ADMIN_CREATE_CQ; e.prp1 = (uint64_t)(uintptr_t)(dma + OFF_IO_CQ); e.cdw10 = ((QDEPTH - 1u) << 16) | 1u; e.cdw11 = 1u;
        if ((r = run(&aq, &aq_sqhd, &e, &c)) != 0) return o->err = r;
        for (unsigned i = 0; i < sizeof e; i++) ((uint8_t *)&e)[i] = 0;
        e.cdw0 = NVME_ADMIN_CREATE_SQ; e.prp1 = (uint64_t)(uintptr_t)(dma + OFF_IO_SQ); e.cdw10 = ((QDEPTH - 1u) << 16) | 1u; e.cdw11 = (1u << 16) | 1u;
        if ((r = run(&aq, &aq_sqhd, &e, &c)) != 0) return o->err = r;
        nvme_q_init(&ioq, (struct nvme_sqe *)(dma + OFF_IO_SQ), (struct nvme_cqe *)(dma + OFF_IO_CQ),
                    (volatile uint32_t *)(regs + 0x1000 + 2 * stride), (volatile uint32_t *)(regs + 0x1000 + 3 * stride), QDEPTH);
        io_sqhd = 0;
        g_ready = 1;
    }
    g_sectors = o->sectors; o->ready = 1;
    static uint8_t s0[512];
    if ((r = nvme_read(0, 1, s0)) != 0) return o->err = r;
    for (int i = 0; i < 16; i++) o->sector0[i] = s0[i];
    return o->err = 0;
}

static void copy_bytes(void *d, const void *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s)[i];
}

static int io(uint8_t op, uint64_t lba, uint32_t count)
{
    struct nvme_sqe e; struct nvme_cqe c; uint64_t p1 = 0, p2 = 0;
    if (op != NVME_IO_FLUSH && nvme_prp((uint64_t)(uintptr_t)(dma + OFF_DATA), (uint64_t)(uintptr_t)(dma + OFF_LIST), count * 512u, &p1, &p2) != 0) return E_ARG;
    nvme_rw_sqe(&e, op, 1, lba, count, p1, p2);
    return run(&ioq, &io_sqhd, &e, &c);
}

int nvme_read(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_ready) return E_NO_CTRL;
    if (count == 0 || count > NVME_MAX_SECTORS || lba >= g_sectors || count > g_sectors - lba) return E_ARG;
    int r = io(NVME_IO_READ, lba, count);
    if (r == 0) copy_bytes(buf, dma + OFF_DATA, count * 512u);
    return r;
}

int nvme_write(uint64_t lba, uint32_t count, const void *buf)
{
    if (!g_ready) return E_NO_CTRL;
    if (count == 0 || count > NVME_MAX_SECTORS || lba >= g_sectors || count > g_sectors - lba) return E_ARG;
    copy_bytes(dma + OFF_DATA, buf, count * 512u);
    return io(NVME_IO_WRITE, lba, count);
}

int nvme_flush(void) { return g_ready ? io(NVME_IO_FLUSH, 0, 0) : E_NO_CTRL; }

int nvme_selftest(struct nvme_selftest *t)
{
    static uint8_t orig[8 * 512], pat[8 * 512], back[8 * 512], s0[512], big[NVME_MAX_SECTORS * 512];
    static struct nvme_info inf;
    for (unsigned i = 0; i < sizeof *t; i++) ((uint8_t *)t)[i] = 0;
    int r = nvme_probe(&inf);
    if (r != 0) return t->err = r;
    static const char sig[] = "SOMANVME";
    int okm = inf.model[0] == 'Q' && inf.model[1] == 'E' && inf.model[2] == 'M' && inf.model[3] == 'U';
    int oks = 1; for (int i = 0; i < 8; i++) if (inf.sector0[i] != (uint8_t)sig[i]) oks = 0;
    t->allowed = okm && oks;
    if (!t->allowed) return t->err = E_ARG;
    if (inf.sectors < 4096) return t->err = E_ARG;
    if ((r = nvme_read(0, 1, s0)) != 0) return t->err = r;
    if ((r = nvme_read(1024, 8, orig)) != 0) return t->err = r;
    for (int i = 0; i < 8 * 512; i++) pat[i] = (uint8_t)(i * 29u + (i >> 9) * 11u + 0x33);
    if ((r = nvme_write(1024, 8, pat)) != 0) return t->err = r;
    if ((r = nvme_flush()) != 0) return t->err = r;
    t->wrote_ok = 1;
    if ((r = nvme_read(1024, 8, back)) != 0) return t->err = r;
    t->verify_ok = 1; for (int i = 0; i < 8 * 512; i++) if (back[i] != pat[i]) t->verify_ok = 0;
    r = nvme_write(1024, 8, orig);
    if (r == 0) r = nvme_flush();
    if (r == 0) r = nvme_read(1024, 8, back);
    if (r != 0) return t->err = r;
    t->restore_ok = 1; for (int i = 0; i < 8 * 512; i++) if (back[i] != orig[i]) t->restore_ok = 0;
    if ((r = nvme_read(0, 1, back)) != 0) return t->err = r;
    t->sector0_ok = 1; for (int i = 0; i < 512; i++) if (back[i] != s0[i]) t->sector0_ok = 0;
    uint64_t t0 = timer_ticks;
    for (int i = 0; i < 8; i++) if ((r = nvme_read((uint64_t)i * NVME_MAX_SECTORS, NVME_MAX_SECTORS, big)) != 0) return t->err = r;
    t->read_ms = (uint32_t)(timer_ticks - t0); t->read_kib = 8u * NVME_MAX_SECTORS * 512u / 1024u;
    return (t->verify_ok && t->restore_ok && t->sector0_ok) ? 0 : (t->err = E_IO);
}
