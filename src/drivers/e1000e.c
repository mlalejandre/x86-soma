/* SOMA_170_T480S: Ethernet Intel de la familia e1000/e1000e por sondeo, como interfaz "eth0" de netif.
 *
 *   - ThinkPad T480s: Intel I219-LM / I219-V (MAC del chipset, PHY externo por MDIO). Referencia: Linux
 *     drivers/net/ethernet/intel/e1000e/ (ich8lan.c, netdev.c). Particularidades que se tratan:
 *       * modo de ultra bajo consumo (ULP) que dejan Windows/Linux al apagar sin cable: con el firmware de gestion (ME) se
 *         le pide que lo quite (H2ME/FWSM); sin ME, como e1000_disable_ulp_lpt_lp de Linux (LANPHYPC, SMBus, ULP_CONFIG1);
 *       * semaforo software/firmware (EXTCNF_CTRL.SWFLAG) para el PHY: el ME tambien lo usa;
 *       * ajustes de Sunrise Point en la transmision (IOSFPC, TARC0) como Linux.
 *   - QEMU: 82574L (-device e1000e) y 82540EM (-device e1000) con los mismos descriptores clasicos (legacy).
 * Anillos de 32 descriptores de 16 bytes con buffers de 2 KiB. Sin interrupciones: la pila sondea (netif_poll).
 * Interruptor: soma.ethernet=off. */
#include <stdint.h>
#include "e1000e.h"
#include "netif.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "console.h"
#include "idt.h"
#include "bootfb.h"

#define R_CTRL     0x00000
#define R_STATUS   0x00008
#define R_EECD     0x00010
#define R_CTRL_EXT 0x00018
#define R_MDIC     0x00020
#define R_FEXTNVM3 0x0003C
#define R_ICR      0x000C0
#define R_IMC      0x000D8
#define R_RCTL     0x00100
#define R_TCTL     0x00400
#define R_TIPG     0x00410
#define R_FEXTNVM7 0x000E4
#define R_EXTCNF   0x00F00
#define R_IOSFPC   0x00F28
#define R_RDBAL    0x02800
#define R_RDBAH    0x02804
#define R_RDLEN    0x02808
#define R_RDH      0x02810
#define R_RDT      0x02818
#define R_TDBAL    0x03800
#define R_TDBAH    0x03804
#define R_TDLEN    0x03808
#define R_TDH      0x03810
#define R_TDT      0x03818
#define R_TARC0    0x03840
#define R_MTA      0x05200
#define R_RAL0     0x05400
#define R_RAH0     0x05404
#define R_H2ME     0x05B50
#define R_FWSM     0x05B54

#define CTRL_SLU   (1u << 6)
#define CTRL_ASDE  (1u << 5)
#define CTRL_FRCSPD (1u << 11)
#define CTRL_FRCDPX (1u << 12)
#define CTRL_ILOS  (1u << 7)
#define CTRL_RST   (1u << 26)
#define CTRL_LANPHYPC_OVR (1u << 16)
#define CTRL_LANPHYPC_VAL (1u << 17)
#define NRX 32
#define NTX 32
#define BUFSZ 2048

struct rxd { uint64_t addr; uint16_t len, csum; uint8_t status, errors; uint16_t special; } __attribute__((packed));
struct txd { uint64_t addr; uint16_t len; uint8_t cso, cmd, status, css; uint16_t special; } __attribute__((packed));

static volatile uint8_t *mm;
static int ok, pch;
static uint16_t dev_id;
static uint8_t mac[6], phy_addr = 1;
static volatile struct rxd *rx; static volatile struct txd *tx;
static uint8_t *rxbuf, *txbuf;
static uint32_t rx_next, tx_next;
static uint32_t st_tx, st_rx, st_txerr, st_rxerr; static uint64_t st_txb, st_rxb;
static const char *why = "no iniciada";
static uint32_t ulp_how;                     /* 0 nada, 1 ME, 2 sin ME, 3 sin ME y fallo */
static volatile int tx_lock;

static inline uint32_t rd(uint32_t r) { return *(volatile uint32_t *)(mm + r); }
static inline void wr(uint32_t r, uint32_t v) { *(volatile uint32_t *)(mm + r) = v; }
static void wait_ms(uint32_t ms) { uint64_t t = timer_get_uptime_ms() + ms + 1; while (timer_get_uptime_ms() < t) __asm__ volatile ("pause"); }

/* Identificadores PCI de la familia que se aceptan */
static int known(uint16_t id, int *is_pch)
{
    static const uint16_t pchs[] = {      /* I217, I218, I219 (LPT, SPT/KBP, CNP...) */
        0x153A, 0x153B, 0x155A, 0x1559, 0x15A0, 0x15A1, 0x15A2, 0x15A3, 0x156F, 0x1570, 0x15B7, 0x15B8, 0x15B9,
        0x15BB, 0x15BC, 0x15BD, 0x15BE, 0x15D6, 0x15D7, 0x15D8, 0x15E3, 0x15DF, 0x15E0, 0x15E1, 0x15E2, 0x0D4E, 0x0D4F, 0x0D4C, 0x0D4D };
    static const uint16_t classic[] = { 0x100E, 0x100F, 0x10D3, 0x10F6, 0x150C, 0x10D4, 0x10EA, 0x10EB, 0x10EF, 0x10F0 };
    for (unsigned i = 0; i < sizeof pchs / sizeof pchs[0]; i++) if (pchs[i] == id) { *is_pch = 1; return 1; }
    for (unsigned i = 0; i < sizeof classic / sizeof classic[0]; i++) if (classic[i] == id) { *is_pch = 0; return 1; }
    return 0;
}

int e1000e_pci_known(uint16_t id) { int p; return known(id, &p); }   /* SOMA_228_ARRANQUE: para el informe de hardware */

/* ---- PHY por MDIO ---- */
static int swflag_get(void)                  /* PCH: semaforo con el firmware para tocar el PHY */
{
    if (!pch) return 0;
    uint64_t t = timer_get_uptime_ms() + 1000;
    while (rd(R_EXTCNF) & (1u << 5)) { if (timer_get_uptime_ms() > t) return -1; wait_ms(1); }
    wr(R_EXTCNF, rd(R_EXTCNF) | (1u << 5));
    t = timer_get_uptime_ms() + 1000;
    while (!(rd(R_EXTCNF) & (1u << 5))) { if (timer_get_uptime_ms() > t) { wr(R_EXTCNF, rd(R_EXTCNF) & ~(1u << 5)); return -2; } wait_ms(1); }
    return 0;
}
static void swflag_put(void) { if (pch) wr(R_EXTCNF, rd(R_EXTCNF) & ~(1u << 5)); }
static int mdic(int write, uint8_t addr, uint8_t reg, uint16_t *val)
{
    uint32_t c = (uint32_t)(write ? *val : 0) | ((uint32_t)(reg & 0x1F) << 16) | ((uint32_t)(addr & 0x1F) << 21) | (write ? (1u << 26) : (2u << 26));
    wr(R_MDIC, c);
    for (int i = 0; i < 2000; i++) {
        uint32_t v = rd(R_MDIC);
        if (v & (1u << 28)) { if (v & (1u << 30)) return -2; if (!write) *val = (uint16_t)v; return 0; }
        for (volatile int k = 0; k < 200; k++) __asm__ volatile ("pause");
    }
    return -1;
}
/* registros con pagina del PHY de los PCH (PHY_REG(pagina, registro)): paginas >= 768 en la direccion 1 */
static int phy_hv(int write, uint16_t page, uint8_t reg, uint16_t *val)
{
    uint8_t a = page >= 768 ? 1 : 2;
    if (reg > 15) { uint16_t p = (uint16_t)(page << 5); if (mdic(1, a, 0x1F, &p)) return -1; }
    return mdic(write, a, reg, val);
}
static int phy_read(uint8_t reg, uint16_t *v)
{
    if (swflag_get()) return -3;
    int r = mdic(0, phy_addr, reg, v);
    swflag_put();
    return r;
}

/* ---- ULP (I217/I218/I219) ---- */
static void disable_ulp(void)
{
    if (rd(R_FWSM) & (1u << 15)) {                                          /* firmware de gestion (ME) presente */
        ulp_how = 1;
        uint32_t h = rd(R_H2ME);
        wr(R_H2ME, (h & ~0x800u) | 0x1000u);                                /* quitar ULP, imponer el ajuste */
        for (int i = 0; i < 250 && (rd(R_FWSM) & 0x400u); i++) wait_ms(10);   /* ULP_CFG_DONE */
        wr(R_H2ME, rd(R_H2ME) & ~0x1000u);
        return;
    }
    ulp_how = 2;
    if (swflag_get()) { ulp_how = 3; return; }
    /* LANPHYPC: reinicia la alimentacion del PHY (contador de configuracion a 50 ms) */
    wr(R_FEXTNVM3, (rd(R_FEXTNVM3) & ~0x0C000000u) | 0x04000000u);
    uint32_t c = rd(R_CTRL);
    wr(R_CTRL, (c | CTRL_LANPHYPC_OVR) & ~CTRL_LANPHYPC_VAL); (void)rd(R_STATUS);
    wait_ms(1);
    wr(R_CTRL, rd(R_CTRL) & ~CTRL_LANPHYPC_OVR); (void)rd(R_STATUS);
    for (int i = 0; i < 20 && !(rd(R_CTRL_EXT) & 4u); i++) wait_ms(6);      /* LPCD */
    wait_ms(30);
    uint16_t v;
    if (phy_hv(0, 769, 23, &v)) {                                           /* CV_SMB_CTRL: si no responde, forzar SMBus en el MAC */
        wr(R_CTRL_EXT, rd(R_CTRL_EXT) | 0x800u); wait_ms(50);
        if (phy_hv(0, 769, 23, &v)) { ulp_how = 3; swflag_put(); return; }
    }
    v &= (uint16_t)~1u; phy_hv(1, 769, 23, &v);                             /* sin SMBus forzado en el PHY */
    wr(R_CTRL_EXT, rd(R_CTRL_EXT) & ~0x800u);                               /* ni en el MAC */
    if (!phy_hv(0, 770, 17, &v)) { v |= 0x4000; phy_hv(1, 770, 17, &v); }    /* K1 de nuevo */
    if (!phy_hv(0, 779, 16, &v)) {                                          /* I218_ULP_CONFIG1: sin ULP, y aplicar */
        v &= (uint16_t)~(0x0004 | 0x0010 | 0x0100 | 0x0040 | 0x0020 | 0x0400 | 0x0800 | 0x1000);
        phy_hv(1, 779, 16, &v);
        v |= 1; phy_hv(1, 779, 16, &v);
    }
    wr(R_FEXTNVM7, rd(R_FEXTNVM7) & ~0x20u);
    swflag_put();
}

/* ---- arranque ---- */
static int find(uint8_t *b, uint8_t *s, uint8_t *f)
{
    for (int bus = 0; bus < 8; bus++)
        for (int sl = 0; sl < 32; sl++)
            for (int fn = 0; fn < 8; fn++) {
                uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)sl, (uint8_t)fn);
                if (v == 0xFFFF) { if (fn == 0) break; continue; }
                if (v != 0x8086) continue;
                int p = 0;
                if (!known(pci_device_id((uint8_t)bus, (uint8_t)sl, (uint8_t)fn), &p)) continue;
                *b = (uint8_t)bus; *s = (uint8_t)sl; *f = (uint8_t)fn; return 1;
            }
    return 0;
}

static int e_init(void)
{
    if (ok) return 1;
    if (bootinfo_disabled("ethernet")) { why = "desactivada (soma.ethernet=off o modo seguro)"; return 0; }
    uint8_t b, s, f;
    if (!find(&b, &s, &f)) { why = "no hay tarjeta Intel e1000/e1000e"; return 0; }
    dev_id = pci_device_id(b, s, f);
    known(dev_id, &pch);
    uint32_t lo = pci_config_read32(b, s, f, 0x10);
    if (lo & 1) { why = "BAR0 no es de memoria"; return 0; }
    uint64_t bar = lo & ~0xFULL;
    if (((lo >> 1) & 3) == 2) bar |= (uint64_t)pci_config_read32(b, s, f, 0x14) << 32;
    pci_config_write32(b, s, f, 0x04, pci_config_read32(b, s, f, 0x04) | 0x6);   /* memoria + bus master */
    for (uint64_t off = 0; off < 0x20000; off += 4096)
        if (vmm_map_page(bar + off, bar + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | (1ULL << 3) | (1ULL << 4)) != 0) { why = "no se pudo proyectar el MMIO"; return 0; }
    mm = (volatile uint8_t *)(uintptr_t)bar;
    if (rd(R_STATUS) == 0xFFFFFFFFu) { why = "la tarjeta no responde (registros a FF)"; return 0; }
    phy_addr = pch ? 2 : 1;
    wr(R_IMC, 0xFFFFFFFFu);
    if (pch) disable_ulp();
    /* reinicio del MAC (en los PCH con el semaforo del firmware) */
    wr(R_RCTL, 0); wr(R_TCTL, 0); (void)rd(R_STATUS); wait_ms(10);
    int sw = pch ? swflag_get() : 0;
    wr(R_CTRL, rd(R_CTRL) | CTRL_RST);
    wait_ms(20);
    for (int i = 0; i < 100 && (rd(R_CTRL) & CTRL_RST); i++) wait_ms(1);
    if (pch && sw == 0) swflag_put();
    wr(R_IMC, 0xFFFFFFFFu); (void)rd(R_ICR);
    for (int i = 0; i < 100 && !(rd(R_EECD) & (1u << 9)); i++) wait_ms(1);   /* lectura automatica de la NVM */
    if (pch) for (int i = 0; i < 150 && !(rd(R_STATUS) & (1u << 9)); i++) wait_ms(1);   /* LAN_INIT_DONE */
    if (pch) wr(R_STATUS, rd(R_STATUS) & ~(1u << 9));
    /* enlace: el PHY negocia solo */
    uint32_t c = rd(R_CTRL);
    c |= CTRL_SLU | CTRL_ASDE; c &= ~(CTRL_ILOS | CTRL_FRCSPD | CTRL_FRCDPX | CTRL_LANPHYPC_OVR | CTRL_LANPHYPC_VAL);
    wr(R_CTRL, c);
    /* MAC de la NVM (la deja el hardware en RAL0/RAH0) */
    uint32_t ral = rd(R_RAL0), rah = rd(R_RAH0);
    for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(ral >> (8 * i));
    mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    if (!(rah & (1u << 31)) || (!ral && !(rah & 0xFFFF))) { why = "sin direccion MAC en la NVM"; return 0; }
    for (int i = 0; i < 128; i++) wr(R_MTA + 4u * (uint32_t)i, 0);
    /* memoria de los anillos */
    uintptr_t rp = pmm_alloc_frame(), tp = pmm_alloc_frame();
    uintptr_t rb = pmm_alloc_contig(NRX * BUFSZ / 4096), tb = pmm_alloc_contig(NTX * BUFSZ / 4096);
    if (!rp || !tp || !rb || !tb) { why = "sin memoria para los anillos"; return 0; }
    rx = (volatile struct rxd *)rp; tx = (volatile struct txd *)tp; rxbuf = (uint8_t *)rb; txbuf = (uint8_t *)tb;
    for (int i = 0; i < NRX; i++) { rx[i].addr = (uint64_t)(rb + (uintptr_t)i * BUFSZ); rx[i].status = 0; rx[i].len = 0; rx[i].errors = 0; }
    for (int i = 0; i < NTX; i++) { tx[i].addr = (uint64_t)(tb + (uintptr_t)i * BUFSZ); tx[i].status = 1; tx[i].cmd = 0; tx[i].len = 0; }
    __asm__ volatile ("mfence" ::: "memory");
    wr(R_RDBAL, (uint32_t)rp); wr(R_RDBAH, (uint32_t)((uint64_t)rp >> 32)); wr(R_RDLEN, NRX * 16);
    wr(R_RDH, 0); wr(R_RDT, NRX - 1); rx_next = 0;
    wr(R_TDBAL, (uint32_t)tp); wr(R_TDBAH, (uint32_t)((uint64_t)tp >> 32)); wr(R_TDLEN, NTX * 16);
    wr(R_TDH, 0); wr(R_TDT, 0); tx_next = 0;
    if (pch) {                                                              /* Sunrise Point y posteriores (como Linux) */
        wr(R_IOSFPC, rd(R_IOSFPC) | 0x00010000u);
        wr(R_TARC0, rd(R_TARC0) | 0x30000000u);
    }
    wr(R_TIPG, 0x00602008u);
    wr(R_TCTL, (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x3Fu << 12));       /* EN, PSP, umbral de colisiones, distancia */
    wr(R_RCTL, (1u << 1) | (1u << 15) | (1u << 26));                        /* EN, difusion (BAM), quitar el CRC; buffers de 2 KiB */
    ok = 1; why = "operativa";
    return 1;
}

static int e_link(void) { return ok && (rd(R_STATUS) & 2u); }
static const uint8_t *e_mac(void) { return mac; }
static int e_send(const void *frame, uint16_t len)
{
    if (!ok || len > BUFSZ) return -1;
    while (__sync_lock_test_and_set(&tx_lock, 1)) __asm__ volatile ("pause");
    uint32_t i = tx_next;
    uint64_t t = timer_get_uptime_ms() + 50;
    while (!(tx[i].status & 1)) { if (timer_get_uptime_ms() > t) { st_txerr++; __sync_lock_release(&tx_lock); return -1; } __asm__ volatile ("pause"); }
    uint8_t *d = txbuf + (uintptr_t)i * BUFSZ; const uint8_t *s = (const uint8_t *)frame;
    for (uint16_t k = 0; k < len; k++) d[k] = s[k];
    uint16_t n = len;
    while (n < 60) d[n++] = 0;                                              /* trama minima */
    tx[i].len = n; tx[i].cso = 0; tx[i].css = 0; tx[i].special = 0; tx[i].status = 0;
    tx[i].cmd = (1u << 0) | (1u << 1) | (1u << 3);                          /* EOP, IFCS (CRC), RS (avisar al terminar) */
    __asm__ volatile ("mfence" ::: "memory");
    tx_next = (i + 1) % NTX;
    wr(R_TDT, tx_next);
    st_tx++; st_txb += n;
    __sync_lock_release(&tx_lock);
    return 0;
}
static int e_poll(void *out, uint16_t max, uint16_t *len_out)
{
    if (!ok) return 0;
    for (int guard = 0; guard < NRX; guard++) {
        uint32_t i = rx_next;
        if (!(rx[i].status & 1)) return 0;                                  /* DD: nada nuevo */
        __asm__ volatile ("lfence" ::: "memory");
        uint16_t l = rx[i].len; uint8_t stv = rx[i].status, er = rx[i].errors;
        int good = (stv & 2) && !er && l >= 14;                             /* EOP y sin errores */
        if (good) {
            if (l > max) l = max;
            const uint8_t *s = rxbuf + (uintptr_t)i * BUFSZ; uint8_t *d = (uint8_t *)out;
            for (uint16_t k = 0; k < l; k++) d[k] = s[k];
            *len_out = l; st_rx++; st_rxb += l;
        } else st_rxerr++;
        rx[i].status = 0;
        __asm__ volatile ("mfence" ::: "memory");
        wr(R_RDT, i);                                                       /* el descriptor vuelve al hardware */
        rx_next = (i + 1) % NRX;
        if (good) return 1;
    }
    return 0;
}
static void e_stats(uint32_t *t, uint32_t *r, uint64_t *tb, uint64_t *rb) { if (t) *t = st_tx; if (r) *r = st_rx; if (tb) *tb = st_txb; if (rb) *rb = st_rxb; }

const struct netif_ops netif_e1000e = { "eth0", "Ethernet Intel (cable)", e_init, e_send, e_poll, e_mac, e_link, e_stats };

void e1000e_info(void)
{
    kprint("eth0 (Ethernet Intel): ");
    if (!ok) { kprint(why); kprint("\n"); return; }
    uint32_t stv = rd(R_STATUS);
    kprint(pch ? "I217/I218/I219 del chipset" : "e1000/82574"); kprint(" (8086:"); kprint_hex16(dev_id); kprint(")");
    kprint(" | enlace ");
    if (stv & 2) {
        uint32_t sp = (stv >> 6) & 3;
        kprint(sp == 0 ? "10" : sp == 1 ? "100" : "1000"); kprint(" Mbit/s "); kprint(stv & 1 ? "duplex" : "semiduplex");
    } else kprint("ABAJO (sin cable o sin negociar)");
    kprint("\n  enviadas "); kprint_dec(st_tx); kprint(" (errores "); kprint_dec(st_txerr); kprint("), recibidas "); kprint_dec(st_rx);
    kprint(" (descartadas "); kprint_dec(st_rxerr); kprint(")");
    if (pch) { kprint(" | ULP: "); kprint(ulp_how == 1 ? "lo quito el ME" : ulp_how == 2 ? "quitado sin ME" : ulp_how == 3 ? "FALLO al quitarlo" : "no"); }
    uint16_t bmcr = 0, bmsr = 0, id1 = 0, id2 = 0;
    int r = phy_read(0, &bmcr) | phy_read(1, &bmsr) | phy_read(2, &id1) | phy_read(3, &id2);
    kprint("\n  PHY: ");
    if (r) kprint("no responde");
    else { kprint("id "); kprint_hex16(id1); kprint(":"); kprint_hex16(id2); kprint(" BMCR "); kprint_hex16(bmcr); kprint(" BMSR "); kprint_hex16(bmsr);
           kprint(bmsr & 0x20 ? " (negociacion hecha)" : " (negociando o sin cable)"); }
    kprint("\n  CTRL "); kprint_hex32(rd(R_CTRL)); kprint(" STATUS "); kprint_hex32(stv); kprint(" FWSM "); kprint_hex32(rd(R_FWSM)); kprint("\n");
}
