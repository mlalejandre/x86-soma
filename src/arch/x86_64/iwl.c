/* SOMA_175A_T480S: WiFi Intel 8265 (familia 8000, iwlwifi "mvm"). Fase A: la tarjeta arranca su firmware.
 *
 *   detectar la tarjeta (PCI 8086:24FD) -> preparar (NIC_READY) -> reinicio -> APM (relojes) -> RX (anillo de 256 RBD,
 *   64 buffers de 4 KiB) -> cargar la imagen INIT del firmware por el canal de servicio del FH (trozos de hasta 128 KiB,
 *   CPU1 y luego CPU2, con FH_UCODE_LOAD_STATUS) -> el firmware arranca y manda la notificacion ALIVE (0xCAFE).
 * Sin interrupciones: se sondean CSR_INT y el estado del anillo RX. La interrupcion INTx de la tarjeta queda desactivada.
 * El firmware llega como modulo de GRUB (iwlwifi-8265-36.ucode, lo instala el parche). Las secciones de paginacion de la
 * CPU2 y los comandos (busqueda de redes, conexion) son las fases B y C.
 * Referencia: Linux drivers/net/wireless/intel/iwlwifi/pcie/trans.c, rx.c, iwl-io.c (licencia dual GPL/BSD).
 * Interruptor: soma.wifi=off. */
#include <stdint.h>
#include "iwl.h"
#include "iwl_fw.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "console.h"
#include "idt.h"
#include "bootfb.h"
#include "acpi_sys.h"
#include "thread.h"
#include "sysinfo.h"
#include "wpa.h"         /* SOMA_175C: saludo de 4 pasos */
#include "crypto.h"
#include "ccmp.h"        /* SOMA_175C: cifrado CCMP en software */
#include "dhcp_proto.h"  /* SOMA_175C6: diagnostico del DHCP recibido */
#include "netif.h"
#include "fs.h"
#include "rng.h"

#define CSR_HW_IF_CONFIG   0x000
#define CSR_INT_COALESCING 0x004
#define CSR_INT            0x008
#define CSR_INT_MASK       0x00C
#define CSR_FH_INT_STATUS  0x010
#define CSR_RESET          0x020
#define CSR_GP_CNTRL       0x024
#define CSR_HW_REV         0x028
#define CSR_GIO_REG        0x03C
#define CSR_UCODE_DRV_GP1  0x054
#define CSR_UCODE_DRV_GP1_CLR 0x05C
#define CSR_MBOX_SET       0x088
#define CSR_MAC_SHADOW_CTRL 0x0A8
#define CSR_GIO_CHICKEN    0x100
#define CSR_DBG_HPET_MEM   0x240
#define CSR_DBG_LINK_PWR   0x250
#define HBUS_MEM_RADDR     0x40C
#define HBUS_MEM_RDAT      0x41C
#define HBUS_PRPH_WADDR    0x444
#define HBUS_PRPH_RADDR    0x448
#define HBUS_PRPH_WDAT     0x44C
#define HBUS_PRPH_RDAT     0x450
#define FH_RSCSR_STTS_WPTR 0x1BC0
#define FH_RSCSR_RBDCB_BASE 0x1BC4
#define FH_RSCSR_WPTR      0x1BC8
#define FH_RSCSR_RDPTR     0x1BCC
#define FH_RCSR_CONFIG     0x1C00
#define FH_RCSR_RBDCB_WPTR 0x1C08
#define FH_RCSR_FLUSH      0x1C10
#define FH_KW_MEM_ADDR     0x197C
#define FH_TFDIB_CTRL0_9   0x1948
#define FH_TFDIB_CTRL1_9   0x194C
#define FH_SRVC_SRAM_ADDR_9 0x19C8
#define FH_UCODE_LOAD_STATUS 0x1AF0
#define FH_TCSR_CONFIG_9   0x1E20
#define FH_TCSR_BUF_STS_9  0x1E28
#define PRPH_WFPM_GP2      0xA030B4
#define PRPH_RELEASE_CPU_RESET 0x300C
#define PRPH_LMPM_CHICK    0xA01FF8

#define INT_FH_TX  (1u << 27)
#define INT_FH_RX  (1u << 31)
#define INT_SW_RX  (1u << 3)
#define INT_ALIVE  (1u << 0)
#define INT_SW_ERR (1u << 25)
#define INT_HW_ERR (1u << 29)
#define INT_RFKILL (1u << 7)
#define GP_CLOCK_READY (1u << 0)
#define GP_INIT_DONE   (1u << 2)
#define GP_ACCESS_REQ  (1u << 3)
#define GP_GOING_SLEEP (1u << 4)
#define GP_RFKILL_SW   (1u << 27)

#define RXQ  256
#define NRB  64
#define CHUNK (128u * 1024u)

static volatile uint8_t *mm;
static struct iwl_info st;
static struct iwl_fw_info fw;
static uint32_t *rbd; static volatile uint16_t *rb_stts; static uint8_t *rbuf[NRB];
static uint32_t rx_read, rx_write;
static uint8_t *bounce; static uintptr_t kw;
static volatile int busy;

static inline uint32_t r32(uint32_t r) { return *(volatile uint32_t *)(mm + r); }
static inline void w32(uint32_t r, uint32_t v) { *(volatile uint32_t *)(mm + r) = v; }
static inline void w8(uint32_t r, uint8_t v) { *(volatile uint8_t *)(mm + r) = v; }
static inline void setb(uint32_t r, uint32_t m) { w32(r, r32(r) | m); }
static inline void clrb(uint32_t r, uint32_t m) { w32(r, r32(r) & ~m); }
static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
static void udelay(uint32_t us) { uint64_t per = tsc_ticks_per_ms ? tsc_ticks_per_ms : 2000000ULL; uint64_t e = tsc() + per * us / 1000 + 1; while (tsc() < e) __asm__ volatile ("pause"); }
static int poll(uint32_t reg, uint32_t bits, uint32_t mask, uint32_t us)       /* (reg & mask) == bits */
{
    for (uint32_t t = 0; t < us; t += 10) { if ((r32(reg) & mask) == bits) return 0; udelay(10); }
    return (r32(reg) & mask) == bits ? 0 : -1;
}
static int grab(void)
{
    setb(CSR_GP_CNTRL, GP_ACCESS_REQ);
    udelay(2);
    if (poll(CSR_GP_CNTRL, GP_CLOCK_READY, GP_CLOCK_READY | GP_GOING_SLEEP, 15000)) { st.err_access++; return -1; }
    return 0;
}
static void release(void) { clrb(CSR_GP_CNTRL, GP_ACCESS_REQ); }
static void prph_w(uint32_t a, uint32_t v) { if (grab()) return; w32(HBUS_PRPH_WADDR, (a & 0xFFFFFu) | (3u << 24)); w32(HBUS_PRPH_WDAT, v); release(); }
static uint32_t prph_r(uint32_t a) { if (grab()) return 0xFFFFFFFFu; w32(HBUS_PRPH_RADDR, (a & 0xFFFFFu) | (3u << 24)); uint32_t v = r32(HBUS_PRPH_RDAT); release(); return v; }
static void dir_w(uint32_t r, uint32_t v) { if (grab()) return; w32(r, v); release(); }
static uint32_t dir_r(uint32_t r) { if (grab()) return 0xFFFFFFFFu; uint32_t v = r32(r); release(); return v; }
static uint32_t sram_r(uint32_t a) { if (grab()) return 0xFFFFFFFFu; w32(HBUS_MEM_RADDR, a); uint32_t v = r32(HBUS_MEM_RDAT); release(); return v; }

static int set_hw_ready(void)
{
    setb(CSR_HW_IF_CONFIG, 1u << 22);                                  /* NIC_READY */
    if (poll(CSR_HW_IF_CONFIG, 1u << 22, 1u << 22, 50)) return -1;
    setb(CSR_MBOX_SET, 1u << 5);                                        /* OS_ALIVE */
    return 0;
}
static int prepare_card(void)
{
    if (!set_hw_ready()) return 0;
    setb(CSR_DBG_LINK_PWR, 0x80000000u);
    udelay(1500);
    for (int it = 0; it < 10; it++) {
        setb(CSR_HW_IF_CONFIG, 0x08000000u);                            /* PREPARE */
        for (int t = 0; t < 150000; t += 500) { if (!set_hw_ready()) return 0; udelay(500); }
        udelay(25000);
    }
    return -1;
}
static int apm_init(void)
{
    setb(CSR_GIO_CHICKEN, 0x00800000u);                                 /* sin L0s en RX */
    setb(CSR_DBG_HPET_MEM, 0xFFFF0000u);
    setb(CSR_HW_IF_CONFIG, 0x00080000u);                                /* HAP_WAKE_L1A */
    setb(CSR_GIO_REG, 0x00000002u);                                     /* L0s desactivado (ASPM) */
    setb(CSR_GP_CNTRL, GP_INIT_DONE);
    udelay(2);
    return poll(CSR_GP_CNTRL, GP_CLOCK_READY, GP_CLOCK_READY, 25000);   /* la familia 8000 no tiene APMG */
}
static void nic_config(void)
{
    uint32_t mask = 0xFu | 0xC00u | 0xC000u | 0x3000u | 0x200u | 0x100u;
    uint32_t v = (st.hw_rev & 0xFu) | ((fw.phy_sku & 3u) << 10) | (((fw.phy_sku >> 2) & 3u) << 14) | (((fw.phy_sku >> 4) & 3u) << 12);
    w32(CSR_HW_IF_CONFIG, (r32(CSR_HW_IF_CONFIG) & ~mask) | v);
}
static int rx_init(void)
{
    if (!rbd) {
        uintptr_t p = pmm_alloc_frame(); if (!p) return -1;
        rbd = (uint32_t *)p; rb_stts = (volatile uint16_t *)(p + 2048);     /* 256 RBD (1 KiB, alineado a 256) y el estado */
        for (int i = 0; i < NRB; i++) { uintptr_t b = pmm_alloc_frame(); if (!b) return -1; rbuf[i] = (uint8_t *)b; }
    }
    for (int i = 0; i < RXQ; i++) rbd[i] = 0;
    for (int i = 0; i < 8; i++) rb_stts[i] = 0;
    for (int i = 0; i < NRB; i++) rbd[i] = (uint32_t)((uintptr_t)rbuf[i] >> 8);
    rx_read = 0; rx_write = NRB;
    __asm__ volatile ("mfence" ::: "memory");
    if (grab()) return -2;
    w32(FH_RCSR_CONFIG, 0); w32(FH_RCSR_RBDCB_WPTR, 0); w32(FH_RCSR_FLUSH, 0); w32(FH_RSCSR_RDPTR, 0); w32(FH_RSCSR_WPTR, 0);
    w32(FH_RSCSR_RBDCB_BASE, (uint32_t)((uintptr_t)rbd >> 8));
    w32(FH_RSCSR_STTS_WPTR, (uint32_t)((uintptr_t)rb_stts >> 4));
    w32(FH_RCSR_CONFIG, 0x80000000u | 0x4u | 0x1000u | (0x11u << 4) | (8u << 20));   /* activo, 4 KiB, 256 RBD */
    release();
    w8(CSR_INT_COALESCING, 0x40);
    w32(FH_RSCSR_WPTR, rx_write & ~7u);
    return 0;
}

/* SOMA_175B: como iwl_pcie_tx_init: colas del programador apagadas, pagina keep-warm y un anillo de 256 TFD (32 KiB) por
 * cola (31); programador en modo automatico con 31 colas. La fase A se lo saltaba: el firmware puede necesitarlo para arrancar. */
#define NQ 31
static uintptr_t tfd[NQ];
static uint32_t cbbc(int q) { return q < 16 ? 0x19D0u + 4u * (uint32_t)q : q < 20 ? 0x1BF0u + 4u * (uint32_t)(q - 16) : 0x1B20u + 4u * (uint32_t)(q - 20); }
static int tx_init(void)
{
    for (int q = 0; q < NQ; q++) {
        if (!tfd[q]) { tfd[q] = pmm_alloc_contig(8); if (!tfd[q]) return -1; }
        uint8_t *t = (uint8_t *)tfd[q]; for (int i = 0; i < 32768; i++) t[i] = 0;
    }
    prph_w(0xA02C10, 0);                                                 /* SCD_TXFACT: colas desactivadas */
    dir_w(FH_KW_MEM_ADDR, (uint32_t)(kw >> 4));
    for (int q = 0; q < NQ; q++) dir_w(cbbc(q), (uint32_t)(tfd[q] >> 8));
    prph_w(0xA02DA8, prph_r(0xA02DA8) | (1u << 18) | 1u);               /* SCD_GP_CTRL: AUTO_ACTIVE_MODE, 31 colas */
    return 0;
}

/* un trozo de firmware a la SRAM de la tarjeta por el canal de servicio (9) del FH */
static int load_chunk(uint32_t dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) bounce[i] = src[i];
    __asm__ volatile ("mfence" ::: "memory");
    uint64_t pa = (uintptr_t)bounce;
    w32(CSR_INT, INT_FH_TX); w32(CSR_FH_INT_STATUS, 0x3u);
    if (grab()) return -1;
    w32(FH_TCSR_CONFIG_9, 0);
    w32(FH_SRVC_SRAM_ADDR_9, dst);
    w32(FH_TFDIB_CTRL0_9, (uint32_t)pa);
    w32(FH_TFDIB_CTRL1_9, ((uint32_t)(pa >> 32) & 0xFu) << 28 | n);
    w32(FH_TCSR_BUF_STS_9, (1u << 20) | (1u << 12) | 3u);
    w32(FH_TCSR_CONFIG_9, 0x80000000u | 0x00100000u);
    release();
    uint64_t end = timer_get_uptime_ms() + 5000;
    while (!(r32(CSR_INT) & INT_FH_TX)) {
        if (timer_get_uptime_ms() > end) return -2;
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) return -3;
        udelay(20);
    }
    w32(CSR_INT, INT_FH_TX); w32(CSR_FH_INT_STATUS, 0x3u);
    return 0;
}
static int load_section(const struct iwl_fw_sec *s)
{
    for (uint32_t off = 0; off < s->len; off += CHUNK) {
        uint32_t n = s->len - off < CHUNK ? s->len - off : CHUNK, dst = s->offset + off;
        int ext = dst >= 0x40000 && dst <= 0x57FFF;
        if (ext) prph_w(PRPH_LMPM_CHICK, prph_r(PRPH_LMPM_CHICK) | 1u);
        int r = load_chunk(dst, s->data + off, n);
        if (ext) prph_w(PRPH_LMPM_CHICK, prph_r(PRPH_LMPM_CHICK) & ~1u);
        if (r) return r;
        st.bytes_loaded += n;
    }
    return 0;
}
static int load_cpu(const struct iwl_fw_img *im, int cpu)
{
    int a, e; iwl_fw_cpu_range(im, cpu, &a, &e);
    uint32_t shift = cpu == 1 ? 0 : 16, num = 1;
    for (int i = a; i < e; i++) {
        int r = load_section(&im->sec[i]);
        if (r) { st.fail_section = i; return r; }
        dir_w(FH_UCODE_LOAD_STATUS, dir_r(FH_UCODE_LOAD_STATUS) | (num << shift));
        num = (num << 1) | 1u;
        st.sections_loaded++;
    }
    dir_w(FH_UCODE_LOAD_STATUS, cpu == 1 ? 0xFFFFu : 0xFFFFFFFFu);
    return 0;
}

/* SOMA_175B: respuestas a ordenes (por su numero de secuencia) y avisos que se esperan */
static volatile int resp_wait_seq = -1, resp_got;
static uint8_t resp_buf[512]; static uint32_t resp_len;
static uint32_t init_complete, phy_db_n, phy_db_len[32];
static uint8_t *phy_db[32];
/* ===================================================== SOMA_175C: conexion WPA2-PSK, datos y gestor =========================
 * Como mac80211 + iwlmvm de Linux pero minimo: el driver hace la parte "MLME" (autenticacion abierta y asociacion), el saludo de
 * 4 pasos (src/core/wpa.c) y el cifrado CCMP en software (src/core/ccmp.c); el firmware solo transmite y recibe.
 * Contextos del firmware: PHY 0 (canal), MAC 0 (estacion), binding 0, estacion 1 (el AP) con la cola 5 (FIFO VO).
 * Velocidades fijas sin HT (11a/g; 1 Mbit/s CCK para gestion en 2,4 GHz), sin QoS: el AP nos trata como estacion "legacy".
 * Para reconectar se rearranca el firmware (0,3 s): no hace falta deshacer contextos. */
#define QD 5
#define STA_AP 1
enum { W_IDLE = 0, W_JOIN, W_AUTH, W_ASSOC, W_KEYS, W_UP, W_FAIL };
static const char *const wstate_name[] = { "desconectada", "preparando", "autenticando", "asociando", "negociando claves", "conectada", "fallo" };
static struct {
    volatile int state, lost, keys;
    const char *why;
    char ssid[33]; uint8_t bssid[6], chan, band5, capab_lo, capab_hi; int sec, open, rssi;
    uint8_t pmk[32];
    uint16_t bi, aid; uint8_t dtim_period, dtim_count; uint64_t bcn_tsf; uint32_t bcn_gp2; volatile int have_bcn;
    uint8_t rates[8], nrates, xrates[8], nxrates;
    struct wpa_sm sm; uint8_t tk[16], gtk[16]; int gtk_idx; uint64_t pn_tx, pn_rx_uc, pn_rx_mc;
    volatile int auth_st, assoc_st, te_started;
    volatile uint32_t inflight;
    uint32_t tx_sent, tx_ok, tx_fail, rx_data, rx_mic_fail, rx_eapol, rx_drop, eapol_err, last_tx_status, deauth_reason, missed;
    int rate_lvl, ok_streak;
    uint32_t rx_prot, rx_uc_ok, rx_mc_ok, rx_mic_uc, rx_mic_mc, rx_kid_drop, rx_plain_keys, rx_arp, rx_ip, rx_other, tx_plain, tx_enc, m1, m3, m3_again, last_fail_status, tx_ok_data, tx_fail_data;   /* SOMA_175C2 */
    int first_uc, first_mc, first_mic;
    uint32_t bcn_n, tim_set, tim_nulls; volatile int tim_wake; uint64_t last_wake_ms;   /* SOMA_175C4 */
    uint32_t rx_hw_ok, rx_hw_bad; volatile int fw_keys_pending, fw_gtk_pending; uint32_t fw_key_status, fw_gtk_status; int fw_keys, sw_tx;   /* SOMA_175C3 */
    uint64_t last_tx_ms, up_ms;
    int configured;
    int tid0, seq_sw, qos_tx; uint16_t sw_seq; uint32_t rx_polled, rx_pushed, dhcp_seen; volatile int ipfija; uint32_t rx_to_me, rx_to_me_other; volatile int probe_got; uint8_t probe_ip[4]; uint32_t txr[10];   /* SOMA_175C5 */
} cn;
static volatile uint32_t last_phy_gp2;

static void conn_rx(const uint8_t *f, uint32_t n, uint32_t stat);
static volatile int chub_pending; static volatile uint16_t chub_mcc; static volatile uint8_t chub_src;   /* pais que el firmware deduce de las balizas */
static uint16_t known_mcc; static uint8_t known_src;
static void tx_done(const uint8_t *p, uint32_t plen);
static void wl(const char *s);
static void wlv(const char *s, uint32_t v);

/* SOMA_175B2: redes encontradas (balizas y respuestas de sondeo) */
static struct iwl_bss bss[IWL_MAX_BSS]; static int nbss;
static volatile int scan_done; static uint8_t scan_status;
static uint32_t scan_frames, scan_phys, last_phy_chan; static int last_phy_rssi = -128;
static void rx_phy(const uint8_t *p)
{
    last_phy_chan = (uint32_t)(p[22] | p[23] << 8); scan_phys++;
    last_phy_gp2 = *(const uint32_t *)(p + 4);                         /* SOMA_175C: hora del dispositivo (para el DTIM) */
    uint32_t w = *(const uint32_t *)(p + 24 + 4);                       /* non_cfg_phy[1]: energia de las antenas A y B */
    int ea = (int)(w & 0xFF), eb = (int)((w >> 8) & 0xFF);
    int a = ea ? -ea : -128, b = eb ? -eb : -128;
    last_phy_rssi = a > b ? a : b;
}
static void rx_mpdu(const uint8_t *p, uint32_t plen)
{
    uint32_t n = (uint32_t)(p[0] | p[1] << 8);
    if (n < 24 || 4 + n + 4 > plen) return;
    uint32_t stat = *(const uint32_t *)(p + 4 + n);
    if ((stat & 3u) != 3u) return;                                      /* CRC_OK y OVERRUN_OK */
    const uint8_t *f = p + 4;
    if (cn.configured && cn.state >= W_JOIN) conn_rx(f, n, stat);             /* SOMA_175C: nuestra red (gestion y datos) */
    uint8_t fc = f[0];
    if ((fc != 0x80 && fc != 0x50) || n < 24 + 12) return;              /* baliza o respuesta de sondeo */
    scan_frames++;
    const uint8_t *bssid = f + 16;
    uint16_t capab = (uint16_t)(f[24 + 10] | f[24 + 11] << 8), bint = (uint16_t)(f[24 + 8] | f[24 + 9] << 8);
    const uint8_t *ie = f + 36, *end = f + n;
    char ssid[33]; int sl = 0, hidden = 1; uint8_t chan = (uint8_t)last_phy_chan;
    uint8_t rates[8], nr = 0, xr[8], nx = 0;
    ssid[0] = 0;
    while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
        uint8_t id = ie[0], l = ie[1]; const uint8_t *d = ie + 2;
        if (id == 0) { sl = l > 32 ? 32 : l; for (int i = 0; i < sl; i++) { ssid[i] = (char)d[i]; if (d[i]) hidden = 0; } ssid[sl] = 0; }
        else if (id == 3 && l >= 1) chan = d[0];
        else if (id == 1) { nr = l > 8 ? 8 : l; for (int i = 0; i < nr; i++) rates[i] = d[i]; }
        else if (id == 50) { nx = l > 8 ? 8 : l; for (int i = 0; i < nx; i++) xr[i] = d[i]; }
        ie += 2 + l;
    }
    struct rsn_info rsn;
    int sec = wpa_classify(f + 36, n - 36, (capab & 0x10) != 0, &rsn);  /* SOMA_175C: la misma clasificacion que la Pi */
    if (hidden) { ssid[0] = 0; }
    int k = 0; while (k < nbss && !(bss[k].bssid[0] == bssid[0] && bss[k].bssid[1] == bssid[1] && bss[k].bssid[2] == bssid[2] && bss[k].bssid[3] == bssid[3] && bss[k].bssid[4] == bssid[4] && bss[k].bssid[5] == bssid[5])) k++;
    if (k == nbss) { if (nbss >= IWL_MAX_BSS) return; nbss++; bss[k].rssi = -128; bss[k].ssid[0] = 0; }
    for (int i = 0; i < 6; i++) bss[k].bssid[i] = bssid[i];
    if (!hidden || !bss[k].ssid[0]) { for (int i = 0; i <= sl && i < 33; i++) bss[k].ssid[i] = ssid[i]; }
    bss[k].chan = chan; bss[k].sec = (uint8_t)sec; bss[k].capab = capab; bss[k].bi = bint;
    bss[k].group = rsn.present ? rsn.group : 0;
    bss[k].nrates = nr; for (int i = 0; i < nr; i++) bss[k].rates[i] = rates[i];
    bss[k].nxrates = nx; for (int i = 0; i < nx; i++) bss[k].xrates[i] = xr[i];
    if (last_phy_rssi > bss[k].rssi) bss[k].rssi = (int8_t)last_phy_rssi;
    last_phy_rssi = -128;                                               /* SOMA_175B3: cada trama, su propia medida */
}

static void pkt_dispatch(const uint8_t *pkt, uint32_t len)
{
    uint8_t cmd = pkt[4]; uint16_t seq = (uint16_t)(pkt[6] | pkt[7] << 8);
    if (cmd == 0x02 && len >= 8 + 8) {                                  /* SOMA_175B1B: REPLY_ERROR (orden rechazada) */
        st.rerr_type = *(const uint32_t *)(pkt + 8); st.rerr_cmd = pkt[12]; st.rerr_seq = (uint16_t)(pkt[14] | pkt[15] << 8); st.rerr_n++;
        wlv("el firmware rechazo la orden", st.rerr_cmd);
    }
    if (!(seq & 0x8000) && resp_wait_seq >= 0 && (seq & 0x1FFF) == (uint32_t)resp_wait_seq) {
        uint32_t n = len > sizeof resp_buf ? sizeof resp_buf : len;
        for (uint32_t i = 0; i < n; i++) resp_buf[i] = pkt[i];
        resp_len = n; resp_got = 1;
        return;
    }
    if (cmd == 0x1C && !(seq & 0x8000) && ((seq >> 8) & 0x1F) == 5) { tx_done(pkt + 8, len - 8); return; }   /* SOMA_175C: TX en la cola de datos */
    if (cmd == 0x2A && len >= 8 + 24) { if (*(const uint32_t *)(pkt + 8 + 16) & 1u) cn.te_started = 1; return; }   /* TIME_EVENT_NOTIFICATION */
    if (cmd == 0xA2 && len >= 8 + 12) { cn.missed = *(const uint32_t *)(pkt + 8 + 4); if (cn.state == W_UP && cn.missed >= 40) { cn.lost = 1; cn.why = "se perdieron las balizas del AP"; } return; }
    if (cmd == 0xC9 && len >= 8 + 4) { chub_mcc = (uint16_t)(pkt[8] | pkt[9] << 8); chub_src = pkt[10]; chub_pending = 1; return; }   /* MCC_CHUB_UPDATE */
    if (cmd == 0x04) init_complete++;                                   /* INIT_COMPLETE_NOTIF */
    else if (cmd == 0xC0 && len >= 8 + 32) rx_phy(pkt + 8);             /* SOMA_175B2: REPLY_RX_PHY_CMD (canal y senal) */
    else if (cmd == 0xC1 && len >= 8 + 4) rx_mpdu(pkt + 8, len - 8);    /* REPLY_RX_MPDU_CMD (tramas: balizas) */
    else if (cmd == 0x0F && len >= 8 + 8) { scan_done = 1; scan_status = pkt[8 + 6]; }   /* SCAN_COMPLETE_UMAC */
    else if (cmd == 0x6B && phy_db_n < 32 && len > 8) {                 /* CALIB_RES_NOTIF_PHY_DB: se guarda para RUNTIME */
        uint32_t n = len - 8; if (n > 4096) n = 4096;
        if (!phy_db[phy_db_n]) phy_db[phy_db_n] = (uint8_t *)pmm_alloc_frame();
        if (phy_db[phy_db_n]) { for (uint32_t i = 0; i < n; i++) phy_db[phy_db_n][i] = pkt[8 + i]; phy_db_len[phy_db_n] = n; phy_db_n++; }
    }
}

/* paquetes del anillo RX: devuelve 1 si llego ALIVE */
static int rx_handle(void)
{
    uint32_t closed = (uint32_t)(rb_stts[0] & 0xFFFu) & (RXQ - 1);
    int alive = 0;
    while (rx_read != closed) {
        uint8_t *b = rbuf[rx_read % NRB];
        for (uint32_t off = 0; off + 8 <= 4096;) {
            uint32_t lnf = *(uint32_t *)(b + off);
            if (lnf == 0x55550000u || lnf == 0) break;
            uint32_t len = (lnf & 0x3FFFu) + 4;
            uint8_t cmd = b[off + 4], grp = b[off + 5];
            st.rx_packets++; st.last_cmd = (uint32_t)grp << 8 | cmd;
            if (len >= 8 && len <= 4096 - off) pkt_dispatch(b + off, len);   /* SOMA_175B: respuestas y avisos */
            if (cmd == 0x01 && len >= 4 + 4 + 4) {                     /* UCODE_ALIVE_NTFY */
                const uint8_t *p = b + off + 8;
                st.alive_status = (uint16_t)(p[0] | p[1] << 8);
                uint32_t plen = len - 8;
                if (plen >= 4 + 48) {
                    st.lmac_major = *(const uint32_t *)(p + 4); st.lmac_minor = *(const uint32_t *)(p + 8);
                    st.err_table = *(const uint32_t *)(p + 4 + 16); st.scd_base = *(const uint32_t *)(p + 4 + 16 + 20);
                }
                uint32_t lmacs = plen >= 4 + 96 + 16 ? 2 : 1;                  /* v4/v5: dos LMAC */
                if (plen >= 4 + 48 * lmacs + 8) { st.umac_major = *(const uint32_t *)(p + 4 + 48 * lmacs); st.umac_minor = *(const uint32_t *)(p + 8 + 48 * lmacs); }
                st.alive_len = plen;
                alive = 1;
            }
            if (len < 8 || len > 4096) break;
            off += (len + 0x3Fu) & ~0x3Fu;
        }
        for (int i = 0; i < 4096; i += 4) *(uint32_t *)(b + i) = 0;       /* limpio antes de devolverlo */
        rbd[rx_write % RXQ] = (uint32_t)((uintptr_t)b >> 8);
        rx_write++; rx_read = (rx_read + 1) % RXQ;
        __asm__ volatile ("mfence" ::: "memory");
        w32(FH_RSCSR_WPTR, (rx_write % RXQ) & ~7u);
    }
    return alive;
}

static void fail(const char *stage, const char *why) { st.stage = stage; st.why = why; st.ok = 0; }

/* ===================================================== SOMA_175B: hablar con el firmware ==================================== */
#define HBUS_MEM_WADDR 0x410
#define HBUS_MEM_WDAT  0x418
#define HBUS_TARG_WRPTR 0x460
#define PRPH_SCD_SRAM_BASE 0xA02C00
#define PRPH_SCD_DRAM_BASE 0xA02C08
#define PRPH_SCD_TXFACT    0xA02C10
#define PRPH_SCD_AGGR_SEL  0xA02E48
#define PRPH_SCD_EN_CTRL   0xA02E54
#define SCD_STATUS(q) (0xA02C00u + 0x10Cu + 4u * (uint32_t)(q))
#define SCD_RDPTR(q)  (0xA02C00u + 0x68u + 4u * (uint32_t)(q))
static uintptr_t bc_tbl, cmd_mem, paging[8]; static int npaging;
static uint32_t cmd_wp;
static void mem_w(uint32_t a, uint32_t v) { if (grab()) return; w32(HBUS_MEM_WADDR, a); w32(HBUS_MEM_WDAT, v); release(); }

/* como iwl_pcie_tx_start + iwl_trans_pcie_txq_enable (cola de ordenes 0 en el FIFO 7, scd_set_active) */
static int tx_start(void)
{
    uint32_t base = prph_r(PRPH_SCD_SRAM_BASE);
    st.scd_sram = base;
    if (base == 0xFFFFFFFFu) return -1;
    if (grab()) return -1;
    w32(HBUS_MEM_WADDR, base + 0x600);                                  /* contexto, estado y traduccion a cero (135 palabras, 31 colas) */
    for (int i = 0; i < 135; i++) w32(HBUS_MEM_WDAT, 0);
    release();
    if (!bc_tbl) { bc_tbl = pmm_alloc_contig(8); if (!bc_tbl) return -2; }
    for (int i = 0; i < 8 * 4096; i++) ((uint8_t *)bc_tbl)[i] = 0;
    prph_w(PRPH_SCD_DRAM_BASE, (uint32_t)(bc_tbl >> 10));
    prph_w(PRPH_SCD_EN_CTRL, 0);                                        /* programador parado mientras se configura la cola */
    prph_w(SCD_STATUS(0), (0u << 3) | (1u << 19));                      /* cola 0 inactiva */
    cmd_wp = 0;
    w32(HBUS_TARG_WRPTR, 0);
    prph_w(SCD_RDPTR(0), 0);
    mem_w(base + 0x600, 0);
    mem_w(base + 0x604, (64u & 0x7Fu) | ((64u << 16) & 0x7F0000u));    /* ventana y limite de tramas */
    prph_w(SCD_STATUS(0), (1u << 3) | (7u << 0) | (1u << 4) | 0x017F0000u);   /* activa, FIFO 7 (ordenes) */
    prph_w(PRPH_SCD_EN_CTRL, 1u);
    prph_w(PRPH_SCD_TXFACT, 0xFFu);                                     /* FIFOs 0-7 activos */
    for (uint32_t ch = 0; ch < 8; ch++) dir_w(0x1D00u + 0x20u * ch, 0x80000000u | 0x8u);
    dir_w(0x1E98, dir_r(0x1E98) | 2u);                                  /* FH_TX_CHICKEN: reintento automatico */
    return 0;
}

/* Orden al firmware por la cola 0. group 0: cabecera corta (4 B); otro: larga (8 B). Espera la respuesta.
 * 0 = respondio, -1 = sin respuesta, -2 = el firmware se detuvo */
static int send_cmd(uint8_t id, uint8_t group, const void *data, uint32_t len, uint32_t wait_ms)
{
    if (!cmd_mem) { cmd_mem = pmm_alloc_contig(32); if (!cmd_mem) return -9; }   /* SOMA_175B2: 32 huecos de 4 KiB */
    uint32_t idx = cmd_wp & 0xFF, slot = idx & 31;
    uint8_t *c = (uint8_t *)(cmd_mem + slot * 4096u);
    uint16_t seq = (uint16_t)((0u << 8) | idx);                        /* QUEUE_TO_SEQ(0) | INDEX_TO_SEQ(idx) */
    uint32_t h = group ? 8 : 4;
    if (h + len > 4000) return -8;
    for (uint32_t i = 0; i < h + len + 4; i++) c[i] = 0;
    c[0] = id; c[1] = group; c[2] = (uint8_t)seq; c[3] = (uint8_t)(seq >> 8);
    if (group) { c[4] = (uint8_t)len; c[5] = (uint8_t)(len >> 8); }
    for (uint32_t i = 0; i < len; i++) c[h + i] = ((const uint8_t *)data)[i];
    uint32_t tot = h + len;
    uint8_t *tfdp = (uint8_t *)tfd[0] + idx * 128u;
    for (int i = 0; i < 128; i++) tfdp[i] = 0;
    uint64_t a0 = (uintptr_t)c, a1 = (uintptr_t)c + 20;
    uint32_t t0 = tot < 20 ? tot : 20;                                 /* SOMA_175B1B: TB0 = min(tamano, 20) como Linux (tb0_size): */
    *(volatile uint32_t *)(tfdp + 4) = (uint32_t)a0;                    /* con 20 fijos el firmware veia ordenes cortas mas largas */
    *(volatile uint16_t *)(tfdp + 8) = (uint16_t)(((a0 >> 32) & 0xF) | (t0 << 4));   /* y rechazaba PHY_CONFIGURATION (BAD_COMMAND 0x38) */
    int ntb = 1;
    if (tot > 20) { *(volatile uint32_t *)(tfdp + 10) = (uint32_t)a1; *(volatile uint16_t *)(tfdp + 14) = (uint16_t)(((a1 >> 32) & 0xF) | ((tot - 20) << 4)); ntb = 2; }
    tfdp[3] = (uint8_t)ntb;
    __asm__ volatile ("mfence" ::: "memory");
    resp_got = 0; resp_wait_seq = seq;
    cmd_wp = (cmd_wp + 1) & 0xFF;
    w32(HBUS_TARG_WRPTR, cmd_wp | (0u << 8));
    st.cmds_sent++;
    uint64_t end = timer_get_uptime_ms() + wait_ms;
    while (!resp_got && timer_get_uptime_ms() < end) {
        rx_handle();
        if (resp_got) break;
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) { resp_wait_seq = -1; st.last_timeout_cmd = (uint32_t)group << 8 | id; return -2; }
        thread_sleep(1);
    }
    resp_wait_seq = -1;
    if (!resp_got) { st.cmd_timeouts++; st.last_timeout_cmd = (uint32_t)group << 8 | id; return -1; }
    return 0;
}

/* paginacion de la CPU2 (solo imagen RUNTIME, fase B2): pagina CSS + bloques de 32 KiB con la imagen paginada */
static int send_paging(const struct iwl_fw_img *im)
{
    int k = 0; while (k < im->n && im->sec[k].offset != IWL_SEP_PAGING) k++;
    if (k + 2 >= im->n) return -3;
    const struct iwl_fw_sec *css = &im->sec[k + 1], *pg = &im->sec[k + 2];
    uint32_t pages = fw.paging_size / 4096u, nblk = (pages + 7) / 8, last = pages - 8 * (nblk - 1);
    if (!pages || nblk > 7 || css->len > 4096 || pg->len != pages * 4096u) return -3;
    for (uint32_t b = 0; b <= nblk; b++) {
        if (!paging[b]) {
            uintptr_t p = pmm_alloc_contig(b ? 16 : 1); if (!p) return -4;
            paging[b] = b ? ((p + 0x7FFFu) & ~(uintptr_t)0x7FFFu) : p;     /* bloques alineados a 32 KiB */
        }
    }
    npaging = (int)nblk;
    for (uint32_t i = 0; i < 4096; i++) ((uint8_t *)paging[0])[i] = i < css->len ? css->data[i] : 0;
    for (uint32_t b = 1; b <= nblk; b++) {
        uint32_t off = (b - 1) * 32768u, n = b == nblk ? last * 4096u : 32768u;
        for (uint32_t i = 0; i < n; i++) ((uint8_t *)paging[b])[i] = pg->data[off + i];
    }
    __asm__ volatile ("mfence" ::: "memory");
    uint32_t cmd[3 + 33];
    for (int i = 0; i < 36; i++) cmd[i] = 0;
    cmd[0] = (1u << 9) | (1u << 8) | last;                               /* SECURED, ENABLED, paginas del ultimo bloque */
    cmd[1] = 15;                                                         /* 2^15 = 32 KiB por bloque */
    cmd[2] = nblk;
    for (uint32_t b = 0; b <= nblk; b++) cmd[3 + b] = (uint32_t)(paging[b] >> 12);
    return send_cmd(0x4F, 1, cmd, sizeof cmd, 1000);                     /* FW_PAGING_BLOCK_CMD (grupo 1) */
}

/* tabla de errores del firmware (SRAM): valida, codigo, PC, dato */
static void read_err_table(void)
{
    if (!st.err_table) return;
    st.fw_err_valid = sram_r(st.err_table); st.fw_err_id = sram_r(st.err_table + 4);
    st.fw_err_pc = sram_r(st.err_table + 16); st.fw_err_d1 = sram_r(st.err_table + 28);
}

static int post_alive(void)
{
    st.stage = "programador";
    if (tx_start()) { fail("programador", "no se pudo preparar la cola de ordenes"); return -1; }
    /* MAC de la tarjeta (familia 8000: registros WFMP, como iwl_set_hw_address_family_8000) */
    uint32_t m0 = prph_r(0xA03080), m1 = prph_r(0xA03084);
    st.mac[0] = (uint8_t)(m0 >> 24); st.mac[1] = (uint8_t)(m0 >> 16); st.mac[2] = (uint8_t)(m0 >> 8); st.mac[3] = (uint8_t)m0;
    st.mac[4] = (uint8_t)(m1 >> 8); st.mac[5] = (uint8_t)m1;
    /* calibracion de la radio (como iwl_run_init_mvm_ucode): antenas validas y PHY_CONFIGURATION_CMD */
    st.stage = "calibracion";
    init_complete = 0; phy_db_n = 0;
    uint32_t ant = (fw.phy_sku >> 16) & 0xFu;
    int r = send_cmd(0x98, 0, &ant, 4, 500);                             /* TX_ANT_CONFIGURATION_CMD */
    if (r == -2) { fail("calibracion", "el firmware se detuvo con TX_ANT_CONFIGURATION"); return -1; }
    if (r == -1) { fail("calibracion", "el firmware no respondio a la primera orden (TX_ANT_CONFIGURATION)"); return -1; }
    uint32_t phy[3] = { fw.phy_sku, fw.calib_flow[1], fw.calib_event[1] };   /* phy_cfg, flow_trigger, event_trigger */
    r = send_cmd(0x6A, 0, phy, sizeof phy, 500);                         /* PHY_CONFIGURATION_CMD */
    if (r == -2) { fail("calibracion", "el firmware se detuvo con PHY_CONFIGURATION"); return -1; }
    uint64_t end = timer_get_uptime_ms() + 2000;
    while (!init_complete && timer_get_uptime_ms() < end) {
        rx_handle();
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) break;
        thread_sleep(1);
    }
    st.phy_db = phy_db_n;
    if (!init_complete) { fail("calibracion", (r32(CSR_INT) & INT_SW_ERR) ? "el firmware se detuvo durante la calibracion" : "no llego INIT_COMPLETE en 2 s"); return -1; }
    st.calibrated = 1;
    return 0;
}

/* ===================================================== SOMA_175B2: imagen RUNTIME y busqueda de redes ======================== */
static volatile int io_lock;
static void lock(void) { while (__sync_lock_test_and_set(&io_lock, 1)) thread_sleep(1); }
static void unlock(void) { __sync_lock_release(&io_lock); }

/* como _iwl_trans_pcie_stop_device: colas y DMA parados, maestro del bus parado, reinicio */
static void stop_device(void)
{
    w32(CSR_INT_MASK, 0);
    prph_w(PRPH_SCD_TXFACT, 0);
    for (uint32_t ch = 0; ch < 8; ch++) dir_w(0x1D00u + 0x20u * ch, 0);
    dir_w(FH_RCSR_CONFIG, 0);
    for (int i = 0; i < 100 && !(dir_r(0x1C44) & (1u << 24)); i++) udelay(10);   /* RX en reposo */
    setb(CSR_RESET, 1u << 9);                                           /* STOP_MASTER */
    poll(CSR_RESET, 1u << 8, 1u << 8, 100);                             /* MASTER_DISABLED */
    setb(CSR_RESET, 0x80u); udelay(6000);
    clrb(CSR_GP_CNTRL, GP_INIT_DONE);
    w32(CSR_INT, 0xFFFFFFFFu);
}

/* PHY_DB_CMD: resultados de la calibracion INIT, en el orden de Linux (CFG, NCH, PAPD, TXP) */
static int send_phy_db(void)
{
    static const uint8_t order[4] = { 1, 2, 4, 5 };
    int sent = 0;
    for (int o = 0; o < 4; o++)
        for (uint32_t i = 0; i < phy_db_n; i++) {
            const uint8_t *e = phy_db[i];
            if (!e || phy_db_len[i] < 4 || e[0] != order[o]) continue;
            uint32_t l = (uint32_t)(e[2] | e[3] << 8);
            if (4 + l > phy_db_len[i] || 4 + l > 3900) continue;
            int r = send_cmd(0x6C, 0, e, 4 + l, 500);
            if (r) return r;
            sent++;
        }
    st.phy_db_sent = (uint32_t)sent;
    return sent ? 0 : -3;
}

static const uint8_t scan_chans[] = { 1,2,3,4,5,6,7,8,9,10,11,12,13, 36,40,44,48,52,56,60,64, 100,104,108,112,116,120,124,128,132,136,140,144, 149,153,157,161,165 };

static int rt_up(void)
{
    int r;
    st.stage = "programador RT";
    if (tx_start()) { fail("programador RT", "no se pudo preparar la cola de ordenes"); return -1; }
    st.stage = "paginacion";
    r = send_paging(&fw.img[IWL_IMG_RT]);
    if (r) { fail("paginacion", r == -1 ? "el firmware no confirmo la paginacion" : r == -2 ? "el firmware se detuvo al recibir la paginacion" : "imagen de paginacion inesperada"); return -1; }
    st.stage = "configuracion";
    uint32_t ant = (fw.phy_sku >> 16) & 0xFu;
    if ((r = send_cmd(0x98, 0, &ant, 4, 500))) { fail("configuracion", r == -2 ? "el firmware se detuvo con TX_ANT" : "sin respuesta a TX_ANT"); return -1; }
    if ((r = send_phy_db())) { fail("configuracion", r == -3 ? "no hay resultados de calibracion para enviar" : r == -2 ? "el firmware se detuvo con PHY_DB" : "sin respuesta a PHY_DB"); return -1; }
    uint32_t phy[3] = { fw.phy_sku, fw.calib_flow[0], fw.calib_event[0] };
    if ((r = send_cmd(0x6A, 0, phy, sizeof phy, 500))) { fail("configuracion", r == -2 ? "el firmware se detuvo con PHY_CONFIGURATION (RT)" : "sin respuesta a PHY_CONFIGURATION (RT)"); return -1; }
    uint32_t bt[2] = { 1u, (1u << 2) | (1u << 0) | (1u << 4) };     /* BT_COEX_NW; SYNC2SCO, MPLUT, HIGH_BAND_RET */
    if ((r = send_cmd(0x9B, 0, bt, sizeof bt, 500))) { fail("configuracion", r == -2 ? "el firmware se detuvo con BT_CONFIG" : "sin respuesta a BT_CONFIG"); return -1; }
    uint32_t dpw = 0;                                                    /* SOMA_175C4: POWER_TABLE_CMD: dispositivo sin ahorro (CAM) */
    if ((r = send_cmd(0x77, 0, &dpw, 4, 500))) { fail("configuracion", r == -2 ? "el firmware se detuvo con POWER_TABLE" : "sin respuesta a POWER_TABLE"); return -1; }
    uint32_t dqa = 0;                                                    /* DQA_ENABLE_CMD: cola de ordenes 0 */
    if ((r = send_cmd(0x00, 5, &dqa, 4, 500))) { fail("configuracion", r == -2 ? "el firmware se detuvo con DQA_ENABLE" : "sin respuesta a DQA_ENABLE"); return -1; }
    /* estacion auxiliar para buscar (como iwl_mvm_add_aux_sta): cola 1 en el FIFO 5 y ADD_STA */
    st.stage = "estacion auxiliar";
    w32(HBUS_TARG_WRPTR, 0u | (1u << 8));
    uint8_t q[12] = { 0, 0 /*sta*/, 8 /*tid*/, 1 /*cola*/, 1 /*activar*/, 0, 5 /*FIFO*/, 64, 0, 0, 0, 0 };
    if ((r = send_cmd(0x1D, 0, q, sizeof q, 500))) { fail("estacion auxiliar", r == -2 ? "el firmware se detuvo con SCD_QUEUE_CFG" : "sin respuesta a SCD_QUEUE_CFG"); return -1; }
    uint8_t sta[48];
    for (int i = 0; i < 48; i++) sta[i] = 0;
    sta[2] = 0xFF; sta[3] = 0xFF;                                        /* tid_disable_tx */
    sta[4] = 4;                                                          /* MAC_INDEX_AUX, color 0 */
    sta[16] = 0;                                                         /* sta_id */
    sta[35] = 4;                                                         /* IWL_STA_AUX_ACTIVITY */
    sta[40] = 1u << 1;                                                   /* cola 1 */
    if ((r = send_cmd(0x18, 0, sta, sizeof sta, 500))) { fail("estacion auxiliar", r == -2 ? "el firmware se detuvo con ADD_STA" : "sin respuesta a ADD_STA"); return -1; }
    st.sta_status = resp_len >= 12 ? *(uint32_t *)(resp_buf + 8) : 0xFFFFFFFFu;
    if ((st.sta_status & 0xFF) != 1) { fail("estacion auxiliar", "ADD_STA rechazado"); return -1; }
    /* region (LAR): se pregunta al firmware la actual, como iwl_mvm_init_mcc con "ZZ" */
    st.stage = "region";
    uint8_t mcc[28];
    for (int i = 0; i < 28; i++) mcc[i] = 0;
    mcc[0] = 'Z'; mcc[1] = 'Z'; mcc[2] = 0x10;                           /* "ZZ" (le16 = 'Z'<<8|'Z'), MCC_SOURCE_GET_CURRENT */
    if ((r = send_cmd(0xC8, 0, mcc, sizeof mcc, 1000))) { fail("region", r == -2 ? "el firmware se detuvo con MCC_UPDATE" : "sin respuesta a MCC_UPDATE"); return -1; }
    if (resp_len >= 8 + 16) {
        st.mcc_status = *(uint32_t *)(resp_buf + 8);
        uint16_t m = (uint16_t)(resp_buf[12] | resp_buf[13] << 8);
        st.mcc[0] = (char)(m >> 8); st.mcc[1] = (char)m; st.mcc[2] = 0;
        st.mcc_chans = *(uint32_t *)(resp_buf + 20);
    }
    if (known_mcc) {                                                     /* SOMA_175C: el pais que ya dedujo antes (como iwl_mvm_rx_chub_update_mcc) */
        mcc[0] = (uint8_t)known_mcc; mcc[1] = (uint8_t)(known_mcc >> 8); mcc[2] = known_src;
        if (send_cmd(0xC8, 0, mcc, sizeof mcc, 1000) == 0 && resp_len >= 8 + 16) {
            uint16_t m = (uint16_t)(resp_buf[12] | resp_buf[13] << 8);
            st.mcc[0] = (char)(m >> 8); st.mcc[1] = (char)m; st.mcc[2] = 0; st.mcc_chans = *(uint32_t *)(resp_buf + 20);
        }
    }
    /* configuracion de la busqueda (SCAN_CFG_CMD v1, como iwl_mvm_legacy_config_scan) */
    st.stage = "config busqueda";
    uint32_t nch = sizeof scan_chans, nsc = fw.n_scan_channels ? fw.n_scan_channels : 40;
    if (nch > nsc) nch = nsc;
    uint8_t cfg[36 + 64];
    if (36 + nsc > sizeof cfg) { fail("config busqueda", "demasiados canales"); return -1; }
    for (uint32_t i = 0; i < sizeof cfg; i++) cfg[i] = 0;
    uint32_t flags = (1u << 0) | (1u << 3) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 14) | (1u << 15) | (1u << 13) | (nch << 26) | (1u << 17);
    *(uint32_t *)(cfg + 0) = flags;
    *(uint32_t *)(cfg + 4) = ant;                                        /* tx_chains */
    *(uint32_t *)(cfg + 8) = (fw.phy_sku >> 20) & 0xFu;                  /* rx_chains */
    *(uint32_t *)(cfg + 12) = 0xFFFu | (0xFFFu << 16);                   /* todas las tasas basicas y soportadas */
    *(uint32_t *)(cfg + 16) = 0; *(uint32_t *)(cfg + 20) = 0;            /* sin asociar: sin tiempos fuera de canal */
    cfg[24] = 10; cfg[25] = 110; cfg[26] = 44; cfg[27] = 90;             /* permanencia activa, pasiva, fragmentada, extendida */
    for (int i = 0; i < 6; i++) cfg[28 + i] = st.mac[i];
    cfg[34] = 0;                                                         /* estacion auxiliar */
    cfg[35] = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);             /* EBS, ACCURATE_EBS, EBS_ADD, PASSIVE2ACTIVE */
    for (uint32_t i = 0; i < nch; i++) cfg[36 + i] = scan_chans[i];
    if ((r = send_cmd(0x0C, 1, cfg, 36 + nsc, 1000))) { fail("config busqueda", r == -2 ? "el firmware se detuvo con SCAN_CFG" : "sin respuesta a SCAN_CFG"); return -1; }
    st.up = 1;
    return 0;
}

/* busqueda pasiva en todos los canales (SCAN_REQ_UMAC v7, como iwl_mvm_scan_umac con adaptive dwell) */
static int do_scan(void)
{
    uint32_t nsc = fw.n_scan_channels ? fw.n_scan_channels : 40, nch = sizeof scan_chans;
    if (nch > nsc) nch = nsc;
    uint32_t size = 48 + 8 * nsc + 1220;
    static uint8_t cmd[48 + 8 * 64 + 1220];
    if (size > sizeof cmd) return -5;
    for (uint32_t i = 0; i < size; i++) cmd[i] = 0;
    *(uint32_t *)(cmd + 0) = 0;                                          /* flags */
    *(uint32_t *)(cmd + 4) = 0;                                          /* uid */
    *(uint32_t *)(cmd + 8) = 6;                                          /* ooc_priority EXT_6 */
    *(uint16_t *)(cmd + 12) = (1u << 3) | (1u << 2) | (1u << 13);        /* PASSIVE, PASS_ALL, ADAPTIVE_DWELL */
    cmd[15] = 0;                                                         /* scan_start_mac_id */
    cmd[16] = 10; cmd[17] = 110; cmd[18] = 44;                           /* permanencia activa, pasiva, fragmentada */
    cmd[19] = 2; cmd[20] = 10;                                           /* adaptive dwell: APs por defecto y canales sociales */
    *(uint16_t *)(cmd + 22) = 300;                                       /* presupuesto maximo (busqueda completa) */
    *(uint32_t *)(cmd + 40) = 6;                                         /* scan_priority EXT_6 */
    cmd[44] = 0; cmd[45] = (uint8_t)nch;                                 /* canales: sin EBS, numero */
    for (uint32_t i = 0; i < nch; i++) { uint8_t *c = cmd + 48 + 8 * i; c[4] = scan_chans[i]; c[5] = 1; }
    uint8_t *tail = cmd + 48 + 8 * nsc;
    tail[0] = 0; tail[1] = 0; tail[2] = 1;                               /* plan 0: intervalo 0, 1 iteracion */
    nbss = 0; scan_done = 0; scan_status = 0xFF; scan_frames = 0; scan_phys = 0; last_phy_rssi = -128;
    for (int i = 0; i < IWL_MAX_BSS; i++) bss[i].ssid[0] = 0;
    int r = send_cmd(0x0D, 1, cmd, size, 1000);                          /* SCAN_REQ_UMAC */
    if (r) return r;
    uint64_t end = timer_get_uptime_ms() + 15000;
    while (!scan_done && timer_get_uptime_ms() < end) {
        rx_handle();
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) return -2;
        thread_sleep(5);
    }
    return scan_done ? 0 : -4;
}

static int no_reset;
/* SOMA_175B2: preparar la tarjeta, cargar una imagen (INIT o RUNTIME) y esperar ALIVE */
static int start_image(int img, uint64_t t0)
{
    /* preparar, reiniciar, relojes */
    st.stage = "preparar";
    if (prepare_card()) { fail("preparar", "la tarjeta no queda lista (NIC_READY): puede estar en uso por el firmware de gestion"); return -1; }
    if (!no_reset) { setb(CSR_RESET, 0x80u); udelay(6000); }            /* SW_RESET (wifi arrancar sinreset: sin el) */
    st.csr_reset = r32(CSR_RESET);
    if (prepare_card()) { fail("reinicio", "no se recupero tras el reinicio"); return -1; }
    st.stage = "relojes";
    if (apm_init()) { fail("relojes", "el reloj del MAC no arranca (MAC_CLOCK_READY)"); return -1; }
    st.rfkill = !(r32(CSR_GP_CNTRL) & GP_RFKILL_SW);
    /* como iwl_trans_pcie_start_fw */
    w32(CSR_INT, 0xFFFFFFFFu); w32(CSR_INT_MASK, 0); w32(CSR_FH_INT_STATUS, 0xFFFFFFFFu);
    w32(CSR_UCODE_DRV_GP1_CLR, 2u); w32(CSR_UCODE_DRV_GP1_CLR, 4u);
    w32(CSR_INT, 0xFFFFFFFFu);
    st.stage = "nic";
    if (apm_init()) { fail("nic", "el reloj del MAC no arranca (segunda vez)"); return -1; }
    nic_config();
    if (rx_init()) { fail("nic", "no se pudo preparar el anillo de recepcion"); return -1; }
    if (tx_init()) { fail("nic", "sin memoria para las colas de transmision"); return -1; }
    setb(CSR_MAC_SHADOW_CTRL, 0x800FFFFFu);
    w32(CSR_INT, 0xFFFFFFFFu);
    w32(CSR_INT_MASK, INT_FH_TX);                                       /* aviso de fin de carga (sin INTx: solo se sondea) */
    w32(CSR_UCODE_DRV_GP1_CLR, 2u); w32(CSR_UCODE_DRV_GP1_CLR, 2u);
    /* carga de la imagen (como iwl_pcie_load_given_ucode_8000) */
    st.stage = img == IWL_IMG_INIT ? "carga" : "carga RT";
    st.alive = 0; st.bytes_loaded = 0; st.sections_loaded = 0; st.fail_section = -1;
    uint64_t tl = timer_get_uptime_ms();
    prph_w(PRPH_WFPM_GP2, 0x01010101u);
    prph_w(PRPH_RELEASE_CPU_RESET, 1u << 24);
    const struct iwl_fw_img *im = &fw.img[img];
    int r = load_cpu(im, 1);
    w32(CSR_INT_MASK, INT_FH_TX | INT_FH_RX | INT_SW_RX | INT_ALIVE | INT_SW_ERR | INT_HW_ERR | INT_RFKILL);
    if (!r) r = load_cpu(im, 2);
    st.load_ms = (uint32_t)(timer_get_uptime_ms() - tl);
    if (r) { fail("carga", r == -2 ? "la tarjeta no confirmo un trozo (FH_TX)" : r == -3 ? "error del firmware o del hardware durante la carga" : "sin acceso a la tarjeta"); return -1; }
    /* ALIVE */
    st.stage = "alive";
    uint64_t end = timer_get_uptime_ms() + 5000; uint32_t seen = 0;
    w32(FH_RSCSR_WPTR, rx_write & ~7u);                                  /* otra vez, ya con los registros sombra */
    while (timer_get_uptime_ms() < end) {
        if (rx_handle()) { st.alive = 1; break; }
        uint32_t ci = r32(CSR_INT); seen |= ci;
        if (ci & (INT_SW_ERR | INT_HW_ERR)) break;
        thread_sleep(1);
    }
    st.int_seen = seen;
    /* diagnostico como Linux al no llegar ALIVE: arranque seguro de cada CPU, si el firmware toco WFPM_GP2, registros RX */
    st.sb1 = prph_r(0xA01E30); st.sb2 = prph_r(0xA01E34); st.wfpm = prph_r(PRPH_WFPM_GP2);
    st.rx_cfg = dir_r(FH_RCSR_CONFIG); st.rx_base = dir_r(FH_RSCSR_RBDCB_BASE); st.rx_wptr = dir_r(FH_RSCSR_WPTR); st.rx_status = dir_r(0x1C44);
    st.stts0 = ((const volatile uint32_t *)rb_stts)[0]; st.stts1 = ((const volatile uint32_t *)rb_stts)[1];
    st.csr_int = r32(CSR_INT); st.fh_int = r32(CSR_FH_INT_STATUS); st.gp_cntrl = r32(CSR_GP_CNTRL);
    st.load_status = dir_r(FH_UCODE_LOAD_STATUS); st.drv_gp1 = r32(CSR_UCODE_DRV_GP1); st.rb_closed = rb_stts[0] & 0xFFF;
    st.total_ms = (uint32_t)(timer_get_uptime_ms() - t0);
    if (!st.alive) {
        fail("alive", st.csr_int & INT_SW_ERR ? "el firmware se detuvo con un error (SW_ERR)" : st.csr_int & INT_HW_ERR ? "error de hardware (HW_ERR)" : "el firmware no aviso de que arranco (sin ALIVE en 3 s)");
        return -1;
    }
    if (st.alive_status != 0xCAFE) { fail("alive", "ALIVE con estado de error"); return -1; }
    return 0;
}

static int bringup(void)
{
    uint64_t t0 = timer_get_uptime_ms();
    st.ok = 0; st.alive = 0; st.why = ""; st.calibrated = 0; st.cmds_sent = 0; st.cmd_timeouts = 0; st.last_timeout_cmd = 0; st.phy_db = 0; st.fw_err_valid = 0; st.fw_err_id = 0; st.rerr_n = 0; st.up = 0; st.init_alive = 0; st.rt_alive = 0; st.phy_db_sent = 0; st.mcc[0] = 0; st.bytes_loaded = 0; st.sections_loaded = 0; st.fail_section = -1; st.rx_packets = 0;
    if (bootinfo_disabled("wifi")) { fail("inicio", "desactivada (soma.wifi=off o modo seguro)"); return -1; }
    /* firmware */
    st.stage = "firmware";
    uint64_t ms = 0, ml = 0;
    if (!bootinfo_module("iwlwifi", &ms, &ml)) { fail("firmware", "no se cargo el firmware (falta firmware/iwlwifi-8265-36.ucode al compilar)"); return -1; }
    const uint8_t *fd = (const uint8_t *)acpi_sys_map_mmio(ms, ml);
    const char *why = 0;
    if (!fd || iwl_fw_parse(fd, (uint32_t)ml, &fw, &why) != 0) { fail("firmware", why ? why : "no se pudo leer el modulo"); return -1; }
    st.fw_size = (uint32_t)ml;
    /* tarjeta */
    st.stage = "tarjeta";
    int found = 0; uint8_t b = 0, s = 0, f = 0;
    for (int bus = 0; bus < 64 && !found; bus++)
        for (int sl = 0; sl < 32 && !found; sl++) {
            uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)sl, 0);
            if (v != 0x8086) continue;
            uint16_t d = pci_device_id((uint8_t)bus, (uint8_t)sl, 0);
            if (d == 0x24FD || d == 0x24F3 || d == 0x24F4 || d == 0x24F5 || d == 0x24F6) { b = (uint8_t)bus; s = (uint8_t)sl; found = 1; st.dev_id = d; }
        }
    if (!found) { fail("tarjeta", "no hay tarjeta Intel 8260/8265 (QEMU no la emula)"); return -1; }
    st.subsys = pci_config_read32(b, s, f, 0x2C) >> 16;
    uint32_t lo = pci_config_read32(b, s, f, 0x10);
    uint64_t bar = lo & ~0xFULL;
    if (((lo >> 1) & 3) == 2) bar |= (uint64_t)pci_config_read32(b, s, f, 0x14) << 32;
    if (!bar || (lo & 1)) { fail("tarjeta", "BAR0 no valida"); return -1; }
    pci_config_write32(b, s, f, 0x04, (pci_config_read32(b, s, f, 0x04) | 0x6u | (1u << 10)));     /* memoria, bus master, sin INTx */
    uint32_t rt = pci_config_read32(b, s, f, 0x40); pci_config_write32(b, s, f, 0x40, rt & ~0x0000FF00u);   /* RETRY_TIMEOUT (0x41) = 0 */
    for (uint64_t o = 0; o < 0x2000; o += 4096)                         /* sin cache (PCD | PWT), como el resto de MMIO */
        if (vmm_map_page(bar + o, bar + o, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | (1ULL << 3) | (1ULL << 4)) != 0) { fail("tarjeta", "no se pudo proyectar el MMIO"); return -1; }
    mm = (volatile uint8_t *)(uintptr_t)bar; st.bar = bar;
    st.hw_rev = r32(CSR_HW_REV);
    if (st.hw_rev == 0xFFFFFFFFu) { fail("tarjeta", "la tarjeta no responde (registros a FF)"); return -1; }
    if (!bounce) { uintptr_t p = pmm_alloc_contig(CHUNK / 4096); if (!p) { fail("memoria", "sin 128 KiB contiguos para la carga"); return -1; } bounce = (uint8_t *)p; }
    if (!kw) { kw = pmm_alloc_frame(); if (!kw) { fail("memoria", "sin memoria"); return -1; } }
    if (start_image(IWL_IMG_INIT, t0)) return -1;
    st.ok = 1; st.stage = "firmware vivo"; st.why = "";
    /* SOMA_175B: cola de ordenes, MAC y calibracion de la radio con la imagen INIT */
    if (bootinfo_disabled("wifi_calib")) { setb(CSR_RESET, 0x80u); return 0; }   /* soma.wifi_calib=off: como la fase A */
    int pr = post_alive();
    st.csr_int = r32(CSR_INT); st.fh_int = r32(CSR_FH_INT_STATUS); st.gp_cntrl = r32(CSR_GP_CNTRL);
    st.total_ms = (uint32_t)(timer_get_uptime_ms() - t0);
    if (pr != 0) { read_err_table(); setb(CSR_RESET, 0x80u); return -1; }
    st.stage = "calibrada"; st.ok = 1;
    /* SOMA_175B2: imagen RUNTIME con la calibracion y preparacion para buscar (soma.wifi_rt=off: se para tras calibrar) */
    if (bootinfo_disabled("wifi_rt")) { setb(CSR_RESET, 0x80u); return 0; }
    stop_device();
    st.init_alive = 1;
    if (start_image(IWL_IMG_RT, t0)) { st.ok = 0; read_err_table(); return -1; }
    st.rt_alive = 1;
    int ru = rt_up();
    st.csr_int = r32(CSR_INT); st.fh_int = r32(CSR_FH_INT_STATUS); st.gp_cntrl = r32(CSR_GP_CNTRL);
    st.total_ms = (uint32_t)(timer_get_uptime_ms() - t0);
    if (ru) { st.ok = 0; read_err_table(); stop_device(); return -1; }
    st.stage = "lista"; st.ok = 1;
    return 0;
}

static int bringup(void);
/* ---------------------------------------------------------------- registro de sucesos (wifi registro) */
#define WLOG_N 48
static struct { uint32_t ms; const char *s; uint32_t v; int hasv; } wlog[WLOG_N];
static uint32_t wlog_n;
static void wl(const char *s) { uint32_t i = wlog_n++ % WLOG_N; wlog[i].ms = (uint32_t)timer_get_uptime_ms(); wlog[i].s = s; wlog[i].hasv = 0; }
static void wlv(const char *s, uint32_t v) { uint32_t i = wlog_n++ % WLOG_N; wlog[i].ms = (uint32_t)timer_get_uptime_ms(); wlog[i].s = s; wlog[i].v = v; wlog[i].hasv = 1; }

static int maceq(const uint8_t *a, const uint8_t *b) { for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0; return 1; }
static void mcpy(uint8_t *d, const void *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = ((const uint8_t *)s)[i]; }
static void mzero(void *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) ((uint8_t *)d)[i] = 0; }

/* ---------------------------------------------------------------- tramas recibidas hacia la pila IP */
#define RXQ_N 48
static uint8_t (*rxq_buf)[1600]; static uint16_t rxq_len[RXQ_N];         /* en memoria del PMM (la imagen del kernel no puede crecer) */
static volatile uint32_t rxq_head, rxq_tail;
static void rxq_push(const uint8_t *dst, const uint8_t *src, const uint8_t *type_and_payload, uint32_t n)
{
    if (!rxq_buf) { rxq_buf = (uint8_t (*)[1600])pmm_alloc_contig((RXQ_N * 1600 + 4095) / 4096); if (!rxq_buf) { cn.rx_drop++; return; } }
    if (rxq_head - rxq_tail >= RXQ_N || 12 + n > 1600) { cn.rx_drop++; return; }
    uint8_t *b = rxq_buf[rxq_head % RXQ_N];
    mcpy(b, dst, 6); mcpy(b + 6, src, 6); mcpy(b + 12, type_and_payload, n);
    rxq_len[rxq_head % RXQ_N] = (uint16_t)(12 + n);
    __asm__ volatile ("" ::: "memory");
    rxq_head++; cn.rx_pushed++;
}

/* ---------------------------------------------------------------- SOMA_175C5: registro de tramas (wifi tramas) */
#define FLOG_N 96
struct flog_e { uint32_t ms; uint8_t dir, prot, why, proto, op; uint8_t sip[4], dip[4], da[6], sa[6]; uint16_t et, len, sp, dp; };
static struct flog_e flog[FLOG_N]; static uint32_t flog_n;
static struct { uint8_t ip[4]; uint8_t ap; uint16_t n; } seen[8];
static void flog_add(int dir, int prot, int why, const uint8_t *da, const uint8_t *sa, uint16_t et, const uint8_t *pl, uint32_t pn)
{
    struct flog_e *e = &flog[flog_n % FLOG_N]; flog_n++;
    mzero(e, sizeof *e);
    e->ms = (uint32_t)timer_get_uptime_ms(); e->dir = (uint8_t)dir; e->prot = (uint8_t)prot; e->why = (uint8_t)why;
    mcpy(e->da, da, 6); mcpy(e->sa, sa, 6); e->et = et; e->len = (uint16_t)pn;
    if (!pl) return;
    if (et == 0x0806 && pn >= 28) { e->op = pl[7]; mcpy(e->sip, pl + 14, 4); mcpy(e->dip, pl + 24, 4); }
    else if (et == 0x0800 && pn >= 20) {
        uint32_t ihl = (uint32_t)(pl[0] & 15) * 4u; e->proto = pl[9]; mcpy(e->sip, pl + 12, 4); mcpy(e->dip, pl + 16, 4);
        if ((e->proto == 17 || e->proto == 6) && pn >= ihl + 4) { e->sp = (uint16_t)(pl[ihl] << 8 | pl[ihl + 1]); e->dp = (uint16_t)(pl[ihl + 2] << 8 | pl[ihl + 3]); }
    }
}
/* SOMA_175C6: que ve la pila IP cuando llega un paquete UDP al puerto 68 (DHCP): comprobaciones iguales a ip_input/udp_input/dhcp_parse */
extern uint8_t net_mac[6];
#define DDIAG_N 8
static struct { uint32_t ms, xid; uint16_t total, plen, udplen, ucks; uint8_t ipck, udpck, ret, typ, ch[3], op; } ddiag[DDIAG_N]; static uint32_t ddiag_n;
static uint32_t csum16(const uint8_t *p, uint32_t n, uint32_t sum) { while (n > 1) { sum += (uint32_t)(p[0] << 8 | p[1]); p += 2; n -= 2; } if (n) sum += (uint32_t)p[0] << 8; return sum; }
static uint16_t csum_fold(uint32_t sum) { while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16); return (uint16_t)~sum; }
static void dhcp_diag(const uint8_t *ip, uint32_t n)                    /* ip = cabecera IPv4, n = bytes disponibles */
{
    uint32_t ihl = (uint32_t)(ip[0] & 15) * 4u;
    typeof(ddiag[0]) *d = &ddiag[ddiag_n % DDIAG_N]; ddiag_n++; mzero(d, sizeof *d);
    d->ms = (uint32_t)timer_get_uptime_ms(); d->plen = (uint16_t)n; d->total = (uint16_t)(ip[2] << 8 | ip[3]);
    d->ipck = (ihl >= 20 && ihl <= n && csum_fold(csum16(ip, ihl, 0)) == 0) ? 1 : 0;
    if (ihl < 20 || n < ihl + 8 || d->total > n) { d->ret = 0xFE; return; }       /* ip_input lo descartaria: total mayor que lo recibido */
    const uint8_t *u = ip + ihl; uint32_t ul = (uint32_t)(u[4] << 8 | u[5]); d->udplen = (uint16_t)ul; d->ucks = (uint16_t)(u[6] << 8 | u[7]);
    if (ul < 8 || ul > d->total - ihl) { d->ret = 0xFD; return; }
    if (d->ucks == 0) d->udpck = 2;
    else { uint32_t sm = csum16(ip + 12, 8, 0) + 17u + ul; d->udpck = csum_fold(csum16(u, ul, sm)) == 0 ? 1 : 0; }
    const uint8_t *b = u + 8; uint32_t bl = ul - 8;
    if (bl < 240) { d->ret = 0xFC; return; }
    d->op = b[0]; d->xid = (uint32_t)b[4] << 24 | (uint32_t)b[5] << 16 | (uint32_t)b[6] << 8 | b[7]; mcpy(d->ch, b + 31, 3);
    for (uint32_t i = 240; i + 2 <= bl && b[i] != 255; i += b[i] ? 2u + b[i + 1] : 1u) if (b[i] == 53) { d->typ = b[i + 2]; break; }
    struct dhcp_lease l; int r = dhcp_parse(b, bl, d->xid, net_mac, &l); d->ret = (uint8_t)(r < 0 ? 0xF0 : r);   /* 0 = no es para net_mac, F0 = mal formado, 2/5/6 = bien */
}
static void flog_mgmt(const uint8_t *f, uint32_t n, int why)             /* gestion u otra trama para nosotros (desautenticacion...) */
{
    flog_add(1, (f[1] & 0x40) != 0, why, f + 4, f + 10, 0, 0, n);
    flog[(flog_n - 1) % FLOG_N].op = f[0];
}
static void seen_learn(const uint8_t *ip, int ap)                       /* direcciones IP vistas en la red (para deducir el router) */
{
    if (!ip[0] || ip[0] >= 224 || (ip[0] == 169 && ip[1] == 254)) return;
    int k = 0; while (k < 8 && seen[k].n && !(seen[k].ip[0] == ip[0] && seen[k].ip[1] == ip[1] && seen[k].ip[2] == ip[2] && seen[k].ip[3] == ip[3])) k++;
    if (k == 8) return;
    if (!seen[k].n) mcpy(seen[k].ip, ip, 4);
    if (seen[k].n < 60000) seen[k].n++;
    if (ap) seen[k].ap = 1;
}

/* ---------------------------------------------------------------- transmision por la cola 5 */
static uintptr_t dq_mem; static uint32_t dq_wp;
static const uint8_t ofdm_plcp[6] = { 13, 5, 9, 11, 1, 3 };          /* 6, 12, 24, 36, 48, 54 Mbit/s */
static const uint8_t ofdm_mbps[6] = { 6, 12, 24, 36, 48, 54 };
static uint32_t rate_flags(int mgmt)
{
    if (mgmt) return cn.band5 ? (13u | (1u << 14)) : (10u | (1u << 9) | (1u << 14));   /* 6 Mbit/s OFDM o 1 Mbit/s CCK; antena A */
    int l = cn.rate_lvl; if (l < 0) l = 0;
    if (l > 5) l = 5;
    return ofdm_plcp[l] | (1u << 14);
}
static int tx_frame_k(const uint8_t *f, uint32_t flen, int mgmt, int ccm);
static int tx_pm_mgmt;                                                   /* SOMA_175C5: la trama de datos es EAPOL */
static int tx_frame(const uint8_t *f, uint32_t flen, int mgmt) { return tx_frame_k(f, flen, mgmt, 0); }
static int tx_frame_k(const uint8_t *f, uint32_t flen, int mgmt, int ccm)
{
    if (!dq_mem) { dq_mem = pmm_alloc_contig(32); if (!dq_mem) return -9; }
    if (cn.inflight >= 28) {                                            /* sin rx_handle aqui: se puede estar dentro de el (EAPOL) */
        if (timer_get_uptime_ms() - cn.last_tx_ms > 2000) { wlv("aviso: respuestas TX perdidas, se reinicia la cuenta", cn.inflight); cn.inflight = 0; }
        else { cn.rx_drop++; return -3; }
    }
    if (4 + 56 + flen > 4000) return -8;
    uint32_t idx = dq_wp & 0xFF, slot = idx & 31;
    uint8_t *c = (uint8_t *)(dq_mem + slot * 4096u);
    mzero(c, 4 + 56);
    uint16_t seq = (uint16_t)((QD << 8) | idx);
    c[0] = 0x1C; c[1] = 0; c[2] = (uint8_t)seq; c[3] = (uint8_t)(seq >> 8);       /* TX_CMD */
    uint8_t *t = c + 4;
    int ucast = !(f[4] & 1);
    uint8_t fc0 = f[0];
    uint32_t flags = (ucast ? (1u << 3) : 0) | (1u << 13);              /* ACK, SEQ_CTL (sin QoS: el firmware numera) */
    int qosf = !mgmt && fc0 == 0x88;
    if (qosf || (!mgmt && cn.seq_sw)) flags &= ~(1u << 13);             /* SOMA_175C5: QoS o numeracion nuestra: el firmware no toca el numero de secuencia */
    *(uint16_t *)(t + 0) = (uint16_t)flen;
    *(uint32_t *)(t + 4) = flags;
    *(uint32_t *)(t + 12) = rate_flags(mgmt);
    t[16] = STA_AP;
    if (ccm) { t[17] = 2; mcpy(t + 20, cn.tk, 16); }                     /* SOMA_175C3: TX_CMD_SEC_CCM: la tarjeta cifra y anade el MIC (como Linux) */
    *(uint32_t *)(t + 40) = 0xFFFFFFFFu;                                 /* life_time */
    *(uint32_t *)(t + 44) = (uint32_t)((uintptr_t)t + 8);                /* scratch */
    t[48] = (uint8_t)(((uintptr_t)t + 8) >> 32);
    t[49] = 60; t[50] = mgmt ? 3 : 15;                                   /* reintentos RTS y datos */
    t[51] = (uint8_t)(mgmt ? 8 : qosf ? 0 : cn.tid0 ? 0 : 8);            /* SOMA_175C5: como Linux, datos sin QoS = IWL_TID_NON_QOS (8), el mismo TID de la cola */
    *(uint16_t *)(t + 52) = (uint16_t)(!mgmt ? (tx_pm_mgmt ? 2 : 0) : (fc0 == 0x00 || fc0 == 0x20) ? 3 : 2);   /* PM_FRAME_ASSOC / MGMT (EAPOL como gestion, igual que Linux) */
    mcpy(t + 56, f, flen);
    uint32_t tot = 4 + 56 + flen;
    uint8_t *tfdp = (uint8_t *)tfd[QD] + idx * 128u;
    for (int i = 0; i < 128; i++) tfdp[i] = 0;
    uint64_t a0 = (uintptr_t)c, a1 = (uintptr_t)c + 20;
    *(volatile uint32_t *)(tfdp + 4) = (uint32_t)a0; *(volatile uint16_t *)(tfdp + 8) = (uint16_t)(((a0 >> 32) & 0xF) | (20u << 4));
    *(volatile uint32_t *)(tfdp + 10) = (uint32_t)a1; *(volatile uint16_t *)(tfdp + 14) = (uint16_t)(((a1 >> 32) & 0xF) | ((tot - 20) << 4));
    tfdp[3] = 2;
    uint16_t bc = (uint16_t)(((flen + 4 + 4 + (ccm ? 8 : 0) + 3) / 4) | ((uint32_t)STA_AP << 12));   /* recuento en palabras + estacion */
    volatile uint16_t *bct = (volatile uint16_t *)(bc_tbl + QD * 640u);
    bct[idx] = bc; if (idx < 64) bct[256 + idx] = bc;
    __asm__ volatile ("mfence" ::: "memory");
    dq_wp = (dq_wp + 1) & 0xFF;
    w32(HBUS_TARG_WRPTR, dq_wp | ((uint32_t)QD << 8));
    cn.inflight++; cn.tx_sent++; cn.last_tx_ms = timer_get_uptime_ms();
    return 0;
}
static void tx_done(const uint8_t *p, uint32_t plen)                  /* respuesta TX (iwl_mvm_tx_resp_v3) */
{
    if (cn.inflight) cn.inflight--;
    if (plen < 40) return;
    uint32_t s = (uint32_t)(p[36] | p[37] << 8) & 0xFF;
    cn.last_tx_status = s;
    for (int i = 0; i < 10; i++) cn.txr[i] = *(const uint32_t *)(p + 4 * i);   /* SOMA_175C5: ultima respuesta cruda (wifi tramas) */
    if (s == 1 || s == 2) { cn.tx_ok++; if (++cn.ok_streak >= 20 && cn.rate_lvl < 5) { cn.rate_lvl++; cn.ok_streak = 0; } }
    else { cn.tx_fail++; cn.ok_streak = 0; cn.last_fail_status = s; if (cn.rate_lvl > 0) cn.rate_lvl--; if (cn.tx_fail <= 3) wlv("transmision sin ACK del AP (estado)", s); }
}

/* cabecera 802.11 de 24 bytes */
static uint32_t hdr80211(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *a1, const uint8_t *a3)
{
    f[0] = fc0; f[1] = fc1; f[2] = 0; f[3] = 0;
    mcpy(f + 4, a1, 6); mcpy(f + 10, st.mac, 6); mcpy(f + 16, a3, 6);
    f[22] = 0; f[23] = 0;
    return 24;
}
/* trama de datos hacia el AP (ToDS), cifrada con CCMP si ya hay claves */
static int tx_data_x(const uint8_t *dst, uint16_t type, const uint8_t *payload, uint32_t plen, int force_plain);
static int tx_data(const uint8_t *dst, uint16_t type, const uint8_t *payload, uint32_t plen) { return tx_data_x(dst, type, payload, plen, 0); }
static int tx_data_x(const uint8_t *dst, uint16_t type, const uint8_t *payload, uint32_t plen, int force_plain)
{
    static uint8_t f[1700], plain[1600];
    if (plen + 8 > sizeof plain - 16) return -8;
    int prot = cn.keys && !cn.open && !force_plain;
    if (prot) cn.tx_enc++; else cn.tx_plain++;
    int qos = cn.qos_tx && type != 0x888E;                              /* SOMA_175C5: interruptores de diagnostico (wifi qos, wifi seq) */
    uint32_t h = hdr80211(f, qos ? 0x88 : 0x08, (uint8_t)(0x01 | (prot ? 0x40 : 0)), cn.bssid, dst);
    if (qos) { f[24] = 0; f[25] = 0; h = 26; }
    if (cn.seq_sw) { uint16_t sq = (uint16_t)(++cn.sw_seq & 0xFFF); f[22] = (uint8_t)(sq << 4); f[23] = (uint8_t)(sq >> 4); }
    tx_pm_mgmt = type == 0x888E;
    flog_add(0, prot, 0, dst, st.mac, type, payload, plen);
    plain[0] = 0xAA; plain[1] = 0xAA; plain[2] = 0x03; plain[3] = 0; plain[4] = 0; plain[5] = 0;
    plain[6] = (uint8_t)(type >> 8); plain[7] = (uint8_t)type;
    mcpy(plain + 8, payload, plen);
    uint32_t n;
    if (prot && cn.sw_tx) n = h + (uint32_t)ccmp_encrypt(cn.tk, f, h, ++cn.pn_tx, 0, plain, plen + 8, f + h);   /* cifrado en el procesador */
    else if (prot) {                                                    /* SOMA_175C3: cabecera CCMP nuestra (PN) y la tarjeta cifra */
        uint64_t pn = ++cn.pn_tx; uint8_t *c = f + h;
        c[0] = (uint8_t)pn; c[1] = (uint8_t)(pn >> 8); c[2] = 0; c[3] = 0x20; c[4] = (uint8_t)(pn >> 16); c[5] = (uint8_t)(pn >> 24); c[6] = (uint8_t)(pn >> 32); c[7] = (uint8_t)(pn >> 40);
        mcpy(c + 8, plain, plen + 8); n = h + 8 + plen + 8;
        int rr = tx_frame_k(f, n, 0, 1); tx_pm_mgmt = 0; return rr;
    }
    else { mcpy(f + h, plain, plen + 8); n = h + plen + 8; }
    int rr = tx_frame(f, n, 0); tx_pm_mgmt = 0; return rr;
}

/* ---------------------------------------------------------------- EAPOL (saludo de 4 pasos) */
static void eapol_rx(const uint8_t *e, uint32_t n)
{
    static uint8_t out[512];
    size_t ol = 0;
    cn.rx_eapol++;
    uint16_t ki = n >= 7 ? (uint16_t)(e[5] << 8 | e[6]) : 0;
    int is_m3 = (ki & KI_PAIRWISE) && (ki & KI_ACK) && (ki & KI_MIC) && (ki & KI_INSTALL), is_m1 = (ki & KI_PAIRWISE) && (ki & KI_ACK) && !(ki & KI_MIC);
    if (is_m1) cn.m1++;
    if (is_m3) cn.m3++;
    if (is_m3 && cn.keys && cn.sm.state == WPA_DONE) {                  /* SOMA_175C2: el AP repite el mensaje 3 (no le llego el 4): se repite el 4 */
        cn.m3_again++; cn.sm.state = WPA_SENT2;
        int r2 = wpa_sm_rx(&cn.sm, e, n, out, &ol);
        wlv("EAPOL: el AP repitio el mensaje 3; se reenvia el 4", (uint32_t)r2);
        if (r2 == WPA_R_DONE) tx_data_x(cn.bssid, 0x888E, out, (uint32_t)ol, 1);
        else cn.sm.state = WPA_DONE;
        return;
    }
    int r = wpa_sm_rx(&cn.sm, e, n, out, &ol);
    wlv(r == WPA_R_SEND ? "EAPOL: mensaje 1 recibido, se envia el 2" : r == WPA_R_DONE ? (cn.keys ? "EAPOL: clave de grupo renovada" : "EAPOL: mensaje 3 recibido, se envia el 4: claves listas")
        : r == WPA_R_IGNORE ? "EAPOL: trama ignorada" : wpa_err_name(r), (uint32_t)r);
    if (r < 0) { cn.eapol_err = (uint32_t)-r; if (r == WPA_E_MIC) cn.why = "contrasena incorrecta (el MIC del mensaje 3 no cuadra)"; return; }
    if (r == WPA_R_SEND || r == WPA_R_DONE) tx_data_x(cn.bssid, 0x888E, out, (uint32_t)ol, is_m3);   /* el 4 siempre en claro (el AP aun no tiene la clave) */
    if (r == WPA_R_DONE) {
        if (cn.sm.gtk_len != 16) { cn.why = "la red usa TKIP para la clave de grupo (no soportado)"; wl("GTK que no es CCMP"); return; }
        mcpy(cn.gtk, cn.sm.gtk, 16); cn.gtk_idx = cn.sm.gtk_idx; cn.pn_rx_mc = 0;
        if (!cn.keys) { mcpy(cn.tk, cn.sm.ptk + 32, 16); cn.pn_rx_uc = 0; cn.pn_tx = 0; cn.keys = 1; cn.fw_keys_pending = 1; }
        else cn.fw_gtk_pending = 1;                                     /* SOMA_175C3: se instalan en la tarjeta fuera del bucle RX */
    }
}

/* ---------------------------------------------------------------- recepcion de nuestra red (llamado desde rx_mpdu) */
static void conn_rx(const uint8_t *f, uint32_t n, uint32_t stat)
{
    static uint8_t dec[1700];
    uint8_t fc0 = f[0], fc1 = f[1], type = fc0 & 0x0C;
    if (n >= 24 && maceq(f + 4, st.mac)) { cn.rx_to_me++; if (!maceq(f + 10, cn.bssid)) { cn.rx_to_me_other++; flog_mgmt(f, n, 0x11); } }   /* SOMA_175C5 */
    if (n < 24 || !maceq(f + 10, cn.bssid)) return;                    /* A2 = el AP */
    if (type == 0x00) {                                                 /* gestion */
        const uint8_t *b = f + 24; uint32_t bl = n - 24;
        if (fc0 == 0x80 && bl >= 12) {                                  /* baliza: sincronizacion (TSF, DTIM) */
            cn.bcn_tsf = 0; for (int i = 7; i >= 0; i--) cn.bcn_tsf = cn.bcn_tsf << 8 | b[i];
            cn.bcn_gp2 = last_phy_gp2; cn.bi = (uint16_t)(b[8] | b[9] << 8);
            for (uint32_t i = 12; i + 2 <= bl && i + 2 + b[i + 1] <= bl; i += 2u + b[i + 1])
                if (b[i] == 5 && b[i + 1] >= 2) {
                    cn.dtim_count = b[i + 2]; cn.dtim_period = b[i + 3] ? b[i + 3] : 1;
                    if (cn.aid && b[i + 1] >= 4) {                      /* SOMA_175C4: el AP guarda tramas para nosotros? (bit de nuestro AID en el TIM) */
                        uint32_t n1 = b[i + 4] & 0xFEu, by = cn.aid / 8u, plen = (uint32_t)b[i + 1] - 3u;
                        if (by >= n1 && by - n1 < plen && (b[i + 5 + (by - n1)] & (1u << (cn.aid % 8u)))) { cn.tim_set++; cn.tim_wake = 1; }
                    }
                }
            cn.have_bcn = 1; cn.bcn_n++;
            return;
        }
        if (!maceq(f + 4, st.mac)) return;
        flog_mgmt(f, n, 0x10);
        if (fc0 == 0xB0 && bl >= 6) { uint16_t sq = (uint16_t)(b[2] | b[3] << 8), s = (uint16_t)(b[4] | b[5] << 8); if (sq == 2) { cn.auth_st = s; wlv("autenticacion: respuesta del AP (estado)", s); } }
        else if ((fc0 == 0x10 || fc0 == 0x30) && bl >= 6) { cn.assoc_st = (uint16_t)(b[2] | b[3] << 8); cn.aid = (uint16_t)((b[4] | b[5] << 8) & 0x3FFF); wlv("asociacion: respuesta del AP (estado)", (uint32_t)cn.assoc_st); }
        else if ((fc0 == 0xC0 || fc0 == 0xA0) && bl >= 2 && cn.state >= W_AUTH) {
            cn.deauth_reason = (uint32_t)(b[0] | b[1] << 8);
            wlv(fc0 == 0xC0 ? "el AP nos desautentico (motivo)" : "el AP nos desasocio (motivo)", cn.deauth_reason);
            if (cn.state == W_UP) cn.lost = 1;
            else { cn.auth_st = 0xFFFF; cn.assoc_st = 0xFFFF; }
            cn.why = cn.deauth_reason == 15 || cn.deauth_reason == 2 ? "el AP rechazo las claves (contrasena incorrecta?)" : "el AP corto la conexion";
        }
        return;
    }
    if (type != 0x08 || (fc1 & 3) != 2 || (fc0 & 0x40)) return;        /* datos desde el AP (FromDS); sin las tramas nulas */
    uint32_t hl = (fc0 & 0x80) ? 26 : 24;
    if (n <= hl) return;
    const uint8_t *body = f + hl; uint32_t bl = n - hl;
    if (fc1 & 0x40) {
        cn.rx_prot++;
        if (!cn.keys) { cn.rx_drop++; return; }
        int mc = f[4] & 1, kid = (bl >= 4) ? body[3] >> 6 : -1;
        if ((stat & 0x700u) == 0x200u && (stat & 0x40u) && bl >= 16) {   /* SOMA_175C3: la tarjeta ya lo descifro (CCM, MIC correcto) */
            uint64_t pn = ccmp_hdr_pn(body), *last = mc ? &cn.pn_rx_mc : &cn.pn_rx_uc;
            if (pn <= *last && *last) { cn.rx_drop++; flog_add(1, 1, 1, f + 4, f + 16, 0, 0, bl); return; }
            *last = pn; cn.rx_hw_ok++;
            if (mc) cn.rx_mc_ok++; else { cn.rx_uc_ok++; if (!cn.first_uc) { cn.first_uc = 1; wl("primera trama individual (descifrada por la tarjeta)"); } }
            mcpy(dec, body + 8, bl - 16); body = dec; bl -= 16;
            goto plain;
        }
        if ((stat & 0x700u) == 0x200u || (stat & 0x700u) == 0x700u) cn.rx_hw_bad++;
        if (mc && kid != cn.gtk_idx) { cn.rx_drop++; cn.rx_kid_drop++; flog_add(1, 1, 3, f + 4, f + 16, 0, 0, bl); if (cn.rx_kid_drop == 1) wlv("trama de grupo con otro indice de clave", (uint32_t)kid); return; }
        uint64_t pn = 0;
        int r = ccmp_decrypt(mc ? cn.gtk : cn.tk, f, hl, body, bl, dec, &pn, 0);
        if (r < 0) {
            cn.rx_mic_fail++; if (mc) cn.rx_mic_mc++; else cn.rx_mic_uc++; flog_add(1, 1, 2, f + 4, f + 16, 0, 0, bl);
            if (!cn.first_mic) { cn.first_mic = 1; wlv(mc ? "CCMP: MIC incorrecto en trama de grupo (bytes)" : "CCMP: MIC incorrecto en trama individual (bytes)", bl); }
            return;
        }
        if (mc) { cn.rx_mc_ok++; if (!cn.first_mc) { cn.first_mc = 1; wl("CCMP: primera trama de grupo descifrada"); } }
        else { cn.rx_uc_ok++; if (!cn.first_uc) { cn.first_uc = 1; wl("CCMP: primera trama individual descifrada"); } }
        uint64_t *last = mc ? &cn.pn_rx_mc : &cn.pn_rx_uc;
        if (pn <= *last && *last) { cn.rx_drop++; flog_add(1, 1, 1, f + 4, f + 16, 0, 0, bl); return; }            /* repeticion */
        *last = pn; body = dec; bl = (uint32_t)r;
    }
plain:
    if (bl < 8 || body[0] != 0xAA || body[1] != 0xAA || body[2] != 0x03) return;
    uint16_t et = (uint16_t)(body[6] << 8 | body[7]);
    flog_add(1, (fc1 & 0x40) != 0, 0, f + 4, f + 16, et, body + 8, bl - 8);   /* SOMA_175C5 */
    if (et == 0x0800 && bl >= 8 + 28 && flog[(flog_n - 1) % FLOG_N].proto == 17 && flog[(flog_n - 1) % FLOG_N].dp == 68) { dhcp_diag(body + 8, bl - 8); cn.dhcp_seen++; }   /* SOMA_175C6 */
    if (et == 0x0806 && bl >= 8 + 28) {
        seen_learn(body + 8 + 14, maceq(f + 16, cn.bssid));
        if (body[8 + 7] == 2 && !(f[4] & 1) && cn.probe_ip[0]) cn.probe_got = 1;   /* respuesta ARP individual del router a nuestra consulta */
    } else if (et == 0x0800 && bl >= 8 + 20) seen_learn(body + 8 + 12, maceq(f + 16, cn.bssid));
    if (et == 0x888E) { eapol_rx(body + 8, bl - 8); return; }
    if (!cn.keys && !cn.open) { cn.rx_drop++; return; }
    if (cn.keys && !(fc1 & 0x40)) cn.rx_plain_keys++;
    if (maceq(f + 16, st.mac)) return;                                  /* nuestro propio multicast reenviado por el AP */
    cn.rx_data++;
    if (et == 0x0806) cn.rx_arp++; else if (et == 0x0800) cn.rx_ip++; else cn.rx_other++;
    rxq_push(f + 4, f + 16, body + 6, bl - 6);
}

/* SOMA_175C3: claves en la tarjeta (ADD_STA_KEY v1, como iwl_mvm_send_sta_key). El firmware de las mvm no deja las tramas
 * individuales cifradas en manos del procesador: sin la clave instalada no las entrega. */
static int install_key(int gtk)
{
    uint8_t k[64]; mzero(k, sizeof k);
    k[0] = STA_AP; k[1] = gtk ? 1 : 0;
    uint16_t fl = (uint16_t)(((gtk ? cn.gtk_idx : 0) & 3) << 8) | (1u << 3) | 2u | (gtk ? (1u << 14) : 0);   /* KEYID, KEY_MAP, CCM, MULTICAST */
    k[2] = (uint8_t)fl; k[3] = (uint8_t)(fl >> 8);
    mcpy(k + 4, gtk ? cn.gtk : cn.tk, 16);
    int r = send_cmd(0x17, 0, k, sizeof k, 600);
    uint32_t stv = (r == 0 && resp_len >= 12) ? *(uint32_t *)(resp_buf + 8) : 0xFFFF0000u | (uint32_t)(-r);
    if (gtk) cn.fw_gtk_status = stv; else cn.fw_key_status = stv;
    wlv(gtk ? "clave de grupo instalada en la tarjeta (estado)" : "clave individual instalada en la tarjeta (estado)", stv);
    return (r == 0 && (stv & 0xFF) == 1) ? 0 : -1;
}
static void install_pending_keys(void)                                 /* con el cerrojo, fuera de rx_handle */
{
    if (cn.fw_keys_pending) { cn.fw_keys_pending = 0; cn.fw_gtk_pending = 0; int a = install_key(0), b = install_key(1); cn.fw_keys = !a && !b; }
    if (cn.fw_gtk_pending) { cn.fw_gtk_pending = 0; install_key(1); }
}

/* ---------------------------------------------------------------- ordenes de contexto */
static void mac_cmd(uint8_t *m, uint32_t action, int assoc)
{
    mzero(m, 148);
    *(uint32_t *)(m + 4) = action; *(uint32_t *)(m + 8) = 5;            /* FW_MAC_TYPE_BSS_STA */
    mcpy(m + 16, st.mac, 6); mcpy(m + 24, cn.bssid, 6);
    *(uint32_t *)(m + 32) = cn.band5 ? 0 : 0xFu;                        /* tasas de ACK: CCK 1-11 */
    *(uint32_t *)(m + 36) = 0x15u;                                       /* OFDM 6, 12, 24 */
    *(uint32_t *)(m + 44) = (cn.capab_lo & 0x20) ? (1u << 5) : 0;       /* preambulo corto */
    *(uint32_t *)(m + 48) = (cn.band5 || (cn.capab_hi & 0x04)) ? (1u << 4) : 0;   /* ranura corta */
    *(uint32_t *)(m + 52) = (1u << 2) | (1u << 6);                     /* ACCEPT_GRP e IN_BEACON (SOMA_175C4: tambien asociados, para leer el TIM) */
    for (int i = 0; i < 4; i++) { uint8_t *a = m + 60 + 8 * i; *(uint16_t *)a = 15; *(uint16_t *)(a + 2) = 1023; a[4] = 2; a[5] = (uint8_t)(1u << i); }
    uint8_t *s = m + 100;
    uint32_t bi = cn.bi ? cn.bi : 100, dp = cn.dtim_period ? cn.dtim_period : 1;
    *(uint32_t *)(s + 16) = bi; *(uint32_t *)(s + 24) = bi * dp; *(uint32_t *)(s + 32) = 10; *(uint32_t *)(s + 36) = cn.aid;
    if (assoc) {
        uint32_t offs = (uint32_t)cn.dtim_count * bi * 1024u;
        *(uint32_t *)(s + 0) = 1;
        *(uint32_t *)(s + 4) = cn.bcn_gp2 + offs;
        *(uint64_t *)(s + 8) = cn.bcn_tsf + offs;
        *(uint32_t *)(s + 40) = cn.bcn_gp2;
    }
}
static int ccmd(uint8_t id, uint8_t grp, const void *d, uint32_t l, const char *what)
{
    int r = send_cmd(id, grp, d, l, 600);
    if (r) { cn.why = what; wlv(what, (uint32_t)-r); }
    return r;
}
static int wait_until(volatile int *v, int bad, uint32_t ms)          /* espera a que *v deje de valer bad (procesando RX) */
{
    uint64_t end = timer_get_uptime_ms() + ms;
    while (*v == bad && timer_get_uptime_ms() < end) {
        rx_handle();
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) return -2;
        thread_sleep(2);
    }
    return *v == bad ? -1 : 0;
}

static int fresh_fw(void)                                               /* firmware limpio (sin contextos) */
{
    if (st.up && !cn.configured) return 0;
    if (mm) stop_device();
    st.up = 0; cn.configured = 0;
    bringup();
    return st.up ? 0 : -1;
}

/* asociacion completa con la red de cn (rellenada por quien llama); se ejecuta con el cerrojo tomado */
static int do_connect(void)
{
    cn.state = W_JOIN; cn.why = ""; cn.keys = 0; cn.lost = 0; cn.inflight = 0; cn.have_bcn = 0; cn.aid = 0; cn.ipfija = 0;
    cn.tx_sent = cn.tx_ok = cn.tx_fail = cn.rx_data = cn.rx_mic_fail = cn.rx_eapol = cn.rx_drop = cn.eapol_err = 0; cn.deauth_reason = 0;
    cn.rx_prot = cn.rx_uc_ok = cn.rx_mc_ok = cn.rx_mic_uc = cn.rx_mic_mc = cn.rx_kid_drop = cn.rx_plain_keys = cn.rx_arp = cn.rx_ip = cn.rx_other = 0;
    cn.tx_plain = cn.tx_enc = cn.m1 = cn.m3 = cn.m3_again = cn.last_fail_status = 0; cn.first_uc = cn.first_mc = cn.first_mic = 0;
    cn.rx_hw_ok = cn.rx_hw_bad = 0; cn.bcn_n = cn.tim_set = cn.tim_nulls = 0; cn.tim_wake = 0; cn.fw_keys_pending = cn.fw_gtk_pending = 0; cn.fw_key_status = cn.fw_gtk_status = 0; cn.fw_keys = 0;
    cn.rate_lvl = cn.rssi > -62 ? 5 : cn.rssi > -68 ? 4 : cn.rssi > -74 ? 2 : cn.rssi > -80 ? 1 : 0; cn.ok_streak = 0;
    wl("conectar: rearrancando el firmware");
    if (fresh_fw()) { cn.why = "el firmware no arranco"; return -1; }
    cn.configured = 1;
    uint32_t ant = (fw.phy_sku >> 16) & 0xFu, rxa = (fw.phy_sku >> 20) & 0xFu;
    /* PHY 0 en el canal de la red (PHY_CONTEXT_CMD v1, 20 MHz) */
    uint8_t phy[36]; mzero(phy, sizeof phy);
    *(uint32_t *)(phy + 4) = 1;
    phy[16] = cn.band5 ? 0 : 1; phy[17] = cn.chan; phy[18] = 0; phy[19] = 0;
    *(uint32_t *)(phy + 20) = ant;
    *(uint32_t *)(phy + 24) = (rxa << 1) | (2u << 10) | (2u << 12);
    if (ccmd(0x08, 0, phy, sizeof phy, "PHY_CONTEXT rechazado")) return -1;
    uint8_t mac[148]; mac_cmd(mac, 1, 0);
    if (ccmd(0x28, 0, mac, sizeof mac, "MAC_CONTEXT rechazado")) return -1;
    uint32_t bind[6] = { 0, 1, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0 };
    if (ccmd(0x2B, 0, bind, sizeof bind, "BINDING rechazado")) return -1;
    /* cola 5 y estacion del AP */
    dq_wp = 0; w32(HBUS_TARG_WRPTR, 0u | ((uint32_t)QD << 8));
    uint8_t q[12] = { 0, STA_AP, 8, QD, 1, 0, 3, 64, 0, 0, 0, 0 };
    if (ccmd(0x1D, 0, q, sizeof q, "SCD_QUEUE_CFG (cola de datos) rechazado")) return -1;
    uint8_t sta[48]; mzero(sta, sizeof sta);
    sta[2] = 0xFF; sta[3] = 0xFF; mcpy(sta + 8, cn.bssid, 6); sta[16] = STA_AP; sta[35] = 0;
    *(uint32_t *)(sta + 40) = 1u << QD;
    if (ccmd(0x18, 0, sta, sizeof sta, "ADD_STA (AP) sin respuesta")) return -1;
    if ((resp_len < 12 || (resp_buf[8] & 0xFF) != 1)) { cn.why = "ADD_STA (AP) rechazado"; wlv(cn.why, resp_len >= 12 ? resp_buf[8] : 0xFFFF); return -1; }
    wl("contextos PHY, MAC, binding, cola y estacion listos");
    /* tiempo reservado en el canal para autenticar y asociar (TIME_EVENT, como iwl_mvm_protect_session) */
    uint8_t te[36]; mzero(te, sizeof te);
    *(uint32_t *)(te + 4) = 1; *(uint32_t *)(te + 16) = 500; *(uint32_t *)(te + 24) = 1; *(uint32_t *)(te + 28) = 3000;
    te[32] = 1; *(uint16_t *)(te + 34) = (1u << 0) | (1u << 1) | (1u << 11);
    cn.te_started = 0;
    if (ccmd(0x29, 0, te, sizeof te, "TIME_EVENT rechazado")) return -1;
    if (wait_until(&cn.te_started, 0, 1000)) wl("aviso: no llego el inicio del TIME_EVENT (se sigue)"); else wl("en el canal (TIME_EVENT empezado)");
    /* autenticacion abierta */
    cn.state = W_AUTH;
    uint8_t fr[256]; uint32_t n;
    int ok = 0;
    for (int at = 0; at < 4 && !ok; at++) {
        n = hdr80211(fr, 0xB0, 0x00, cn.bssid, cn.bssid);
        fr[n++] = 0; fr[n++] = 0; fr[n++] = 1; fr[n++] = 0; fr[n++] = 0; fr[n++] = 0;
        cn.auth_st = -1; wlv("autenticacion: enviada (intento)", (uint32_t)at + 1);
        if (tx_frame(fr, n, 1)) { cn.why = "no se pudo enviar la autenticacion"; return -1; }
        int w = wait_until(&cn.auth_st, -1, 400);
        if (w == -2) { cn.why = "el firmware se detuvo al autenticar"; return -2; }
        if (!w) ok = 1;
    }
    if (!ok) { cn.why = "el AP no respondio a la autenticacion"; wlv("ultimo estado de transmision", cn.last_tx_status); return -1; }
    if (cn.auth_st != 0) { cn.why = "el AP rechazo la autenticacion"; return -1; }
    /* asociacion (sin HT ni WMM: estacion 11a/g) */
    cn.state = W_ASSOC;
    static uint8_t rsn_ie[64]; uint32_t rl = 0;
    if (!cn.open) { rl = cn.sm.ie_len; mcpy(rsn_ie, cn.sm.ie, rl); }            /* el mismo IE RSN que va en el mensaje 2 */
    ok = 0;
    for (int at = 0; at < 3 && !ok; at++) {
        n = hdr80211(fr, 0x00, 0x00, cn.bssid, cn.bssid);
        uint16_t cap = 0x0001 | (cn.open ? 0 : 0x0010) | (cn.capab_lo & 0x20) | (cn.band5 ? 0 : (uint16_t)((cn.capab_hi & 0x04) << 8));
        fr[n++] = (uint8_t)cap; fr[n++] = (uint8_t)(cap >> 8); fr[n++] = 10; fr[n++] = 0;
        uint32_t sl = 0; while (cn.ssid[sl]) sl++;
        fr[n++] = 0; fr[n++] = (uint8_t)sl; mcpy(fr + n, cn.ssid, sl); n += sl;
        if (cn.nrates) { fr[n++] = 1; fr[n++] = cn.nrates; mcpy(fr + n, cn.rates, cn.nrates); n += cn.nrates; }
        else if (cn.band5) { static const uint8_t r5[8] = { 0x8C, 0x12, 0x98, 0x24, 0xB0, 0x48, 0x60, 0x6C }; fr[n++] = 1; fr[n++] = 8; mcpy(fr + n, r5, 8); n += 8; }
        else { static const uint8_t r2[8] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 }; fr[n++] = 1; fr[n++] = 8; mcpy(fr + n, r2, 8); n += 8; }
        if (cn.nxrates) { fr[n++] = 50; fr[n++] = cn.nxrates; mcpy(fr + n, cn.xrates, cn.nxrates); n += cn.nxrates; }
        if (rl) { mcpy(fr + n, rsn_ie, rl); n += rl; }
        cn.assoc_st = -1; wlv("asociacion: enviada (intento)", (uint32_t)at + 1);
        if (tx_frame(fr, n, 1)) { cn.why = "no se pudo enviar la asociacion"; return -1; }
        int w = wait_until(&cn.assoc_st, -1, 600);
        if (w == -2) { cn.why = "el firmware se detuvo al asociar"; return -2; }
        if (!w) ok = 1;
    }
    if (!ok) { cn.why = "el AP no respondio a la asociacion"; return -1; }
    if (cn.assoc_st != 0) { cn.why = cn.assoc_st == 0xFFFF ? "el AP corto durante la asociacion" : "el AP rechazo la asociacion"; return -1; }
    /* asociados: contexto MAC con AID y DTIM, y todo el tiempo de radio para nuestro binding */
    if (!cn.have_bcn) { uint64_t e2 = timer_get_uptime_ms() + 400; while (!cn.have_bcn && timer_get_uptime_ms() < e2) { rx_handle(); thread_sleep(2); } }
    mac_cmd(mac, 2, 1);
    if (ccmd(0x28, 0, mac, sizeof mac, "MAC_CONTEXT (asociado) rechazado")) return -1;
    uint32_t quota[16];
    for (int i = 0; i < 16; i++) quota[i] = 0;
    quota[0] = 0; quota[1] = 128;
    for (int i = 1; i < 4; i++) quota[4 * i] = 0xFFFFFFFFu;
    if (ccmd(0x2C, 0, quota, sizeof quota, "TIME_QUOTA rechazado")) return -1;
    uint8_t pw[40]; mzero(pw, sizeof pw);                                /* MAC_PM_POWER_TABLE: sin ahorro (CAM), keep-alive 25 s */
    *(uint16_t *)(pw + 4) = 0; *(uint16_t *)(pw + 6) = 25;             /* SOMA_175C2: flags 0 = CAM de verdad (como Linux con ps desactivado) */
    if (ccmd(0xA9, 0, pw, sizeof pw, "MAC_PM_POWER_TABLE rechazado")) return -1;
    wlv("asociado (AID)", cn.aid);
    if (cn.open) { cn.state = W_UP; cn.up_ms = timer_get_uptime_ms(); wl("CONECTADA (red abierta)"); return 0; }
    /* saludo de 4 pasos: lo lleva conn_rx al llegar las tramas EAPOL */
    cn.state = W_KEYS;
    uint64_t end = timer_get_uptime_ms() + 6000;
    while (!cn.keys && timer_get_uptime_ms() < end && cn.assoc_st == 0) {
        rx_handle();
        if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) { cn.why = "el firmware se detuvo durante el saludo WPA2"; return -2; }
        if (cn.eapol_err == (uint32_t)-WPA_E_MIC) break;
        thread_sleep(2);
    }
    if (!cn.keys) { if (!cn.why || !cn.why[0]) cn.why = cn.rx_eapol ? "el saludo WPA2 no termino" : "el AP no envio el saludo WPA2 (EAPOL)"; return -1; }
    { uint64_t e3 = timer_get_uptime_ms() + 30; while (timer_get_uptime_ms() < e3) { rx_handle(); thread_sleep(2); } }   /* que salga el mensaje 4 */
    install_pending_keys();
    { uint8_t nf[24]; hdr80211(nf, 0x48, 0x01, cn.bssid, cn.bssid); tx_frame(nf, 24, 0); }   /* SOMA_175C4: trama nula sin PM: "estoy despierto" */
    cn.state = W_UP; cn.up_ms = timer_get_uptime_ms();
    wl("CONECTADA: claves WPA2 instaladas (cifrado CCMP en el procesador)");
    return 0;
}

/* ---------------------------------------------------------------- redes guardadas: /etc/wifi.conf ("<ssid en hex> <pmk en hex|abierta>") */
#define SAVED_MAX 16
static struct { char ssid[33]; uint8_t pmk[32]; int open; } saved[SAVED_MAX];
static int nsaved = -1;
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void saved_load(void)
{
    if (nsaved >= 0) return;
    nsaved = 0;
    static char b[SAVED_MAX * 140 + 8];
    int n = vfs_read("/etc/wifi.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    for (char *p = b; *p && nsaved < SAVED_MAX; ) {
        char *e = p; while (*e && *e != '\n') e++;
        char *sp = p; while (sp < e && *sp != ' ') sp++;
        int sl = (int)(sp - p) / 2;
        if (sp < e && sl > 0 && sl <= 32) {
            int okl = 1;
            for (int i = 0; i < sl; i++) { int a = hexv(p[2 * i]), c = hexv(p[2 * i + 1]); if (a < 0 || c < 0) okl = 0; saved[nsaved].ssid[i] = (char)(a * 16 + c); }
            saved[nsaved].ssid[sl] = 0;
            char *v = sp + 1;
            if (v[0] == 'a') saved[nsaved].open = 1;
            else if (e - v >= 64) { saved[nsaved].open = 0; for (int i = 0; i < 32; i++) { int a = hexv(v[2 * i]), c = hexv(v[2 * i + 1]); if (a < 0 || c < 0) okl = 0; saved[nsaved].pmk[i] = (uint8_t)(a * 16 + c); } }
            else okl = 0;
            if (okl) nsaved++;
        }
        p = *e ? e + 1 : e;
    }
}
static int saved_write(void)
{
    static char b[SAVED_MAX * 140 + 8];
    static const char hx[] = "0123456789abcdef";
    int k = 0;
    for (int i = 0; i < nsaved; i++) {
        for (int j = 0; saved[i].ssid[j]; j++) { b[k++] = hx[(uint8_t)saved[i].ssid[j] >> 4]; b[k++] = hx[saved[i].ssid[j] & 15]; }
        b[k++] = ' ';
        if (saved[i].open) { const char *a = "abierta"; while (*a) b[k++] = *a++; }
        else for (int j = 0; j < 32; j++) { b[k++] = hx[saved[i].pmk[j] >> 4]; b[k++] = hx[saved[i].pmk[j] & 15]; }
        b[k++] = '\n';
    }
    return vfs_write("/etc/wifi.conf", b, (uint32_t)k);
}
static int sseq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
static int saved_find(const char *ssid) { saved_load(); for (int i = 0; i < nsaved; i++) if (sseq(saved[i].ssid, ssid)) return i; return -1; }
static void saved_put(const char *ssid, const uint8_t *pmk, int open)
{
    int i = saved_find(ssid);
    if (i < 0) { if (nsaved >= SAVED_MAX) { for (int k = 1; k < nsaved; k++) saved[k - 1] = saved[k]; nsaved--; } i = nsaved++; }
    int j = 0; while (ssid[j] && j < 32) { saved[i].ssid[j] = ssid[j]; j++; } saved[i].ssid[j] = 0;
    saved[i].open = open; if (!open) mcpy(saved[i].pmk, pmk, 32);
    saved_write();
}

/* ---------------------------------------------------------------- peticiones (UI y terminal) y gestor */
static volatile int req_scan, req_conn, req_disc, scanning, connecting, wifi_off, user_disc;
static char req_ssid[33], req_pass[65]; static volatile int req_save;
static uint64_t next_auto; static volatile int need_restart;

static void random_bytes(uint8_t *o, int n)
{
    struct sha1_ctx c; uint8_t h[20]; uint64_t t;
    for (int k = 0; k < n; k += 20) {
        sha1_init(&c);
        for (int i = 0; i < 8; i++) { t = tsc(); sha1_update(&c, &t, 8); udelay(1); }
        uint32_t ea = 1, eb, ec, ed; __asm__ volatile ("cpuid" : "+a"(ea), "=b"(eb), "=c"(ec), "=d"(ed));
        if (ec & (1u << 30)) for (int i = 0; i < 4; i++) { uint32_t r; uint8_t okr; __asm__ volatile ("rdrand %0; setc %1" : "=r"(r), "=qm"(okr)); if (okr) sha1_update(&c, &r, 4); }
        sha1_update(&c, st.mac, 6); sha1_update(&c, o, (size_t)n);
        sha1_final(&c, h);
        for (int i = 0; i < 20 && k + i < n; i++) o[k + i] = h[i];
    }
    for (int k = 0; k < n; k++) o[k] ^= (uint8_t)rng_u32();          /* SOMA_192: ademas, el generador unico del nucleo */
}

static const struct iwl_bss *pick_bss(const char *ssid)
{
    const struct iwl_bss *best = 0;
    for (int i = 0; i < nbss; i++) if (sseq(bss[i].ssid, ssid) && (!best || bss[i].rssi > best->rssi)) best = &bss[i];
    return best;
}
static int sec_ok(int s) { return s == SEC_OPEN || s == SEC_WPA2_PSK || s == SEC_WPA2_WPA3; }

/* con el cerrojo tomado: prepara cn a partir de la red elegida y se conecta */
static int connect_to(const struct iwl_bss *b, const uint8_t *pmk)
{
    for (int i = 0; i < 33; i++) cn.ssid[i] = b->ssid[i];
    mcpy(cn.bssid, b->bssid, 6); cn.chan = b->chan; cn.band5 = b->chan > 14; cn.sec = b->sec; cn.open = b->sec == SEC_OPEN; cn.rssi = b->rssi;
    cn.capab_lo = (uint8_t)b->capab; cn.capab_hi = (uint8_t)(b->capab >> 8); cn.bi = b->bi;
    cn.nrates = b->nrates; mcpy(cn.rates, b->rates, b->nrates); cn.nxrates = b->nxrates; mcpy(cn.xrates, b->xrates, b->nxrates);
    cn.dtim_period = 1; cn.dtim_count = 0;
    if (!cn.open) {
        mcpy(cn.pmk, pmk, 32);
        uint8_t ie[32], sn[32]; size_t il = wpa_build_rsn_ie(b->group ? b->group : 4, ie);
        random_bytes(sn, 32);
        wpa_sm_init(&cn.sm, cn.pmk, cn.bssid, st.mac, sn, ie, il);
    }
    int r = do_connect();
    if (r) { cn.state = W_FAIL; wl(cn.why && cn.why[0] ? cn.why : "fallo al conectar"); }
    return r;
}

static int scan_locked(void)
{
    if (cn.state == W_UP) return -11;
    if (fresh_fw()) return -10;
    int r = do_scan();
    if (r == -2) { st.up = 0; st.ok = 0; st.stage = "busqueda"; st.why = "el firmware se detuvo durante la busqueda"; read_err_table(); }
    st.scan_result = r;
    return r;
}

static void mgr_connect(const char *ssid, const char *pass, int save)
{
    connecting = 1; cn.why = ""; cn.state = W_JOIN;
    for (int i = 0; i < 33; i++) cn.ssid[i] = 0;
    int j = 0; while (ssid[j] && j < 32) { cn.ssid[j] = ssid[j]; j++; }
    lock();
    const struct iwl_bss *b = pick_bss(ssid);
    if (!b) { wl("la red no esta en la lista: buscando"); scanning = 1; scan_locked(); scanning = 0; b = pick_bss(ssid); }
    /* candidatos con ese nombre (varios AP o 2,4 y 5 GHz): primero los que no son canales DFS (52-144), por senal */
    const struct iwl_bss *cand[4]; int nc = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nbss && nc < 4; i++) {
            const struct iwl_bss *e = &bss[i];
            int dfs = e->chan >= 52 && e->chan <= 144;
            if (!sseq(e->ssid, ssid) || !sec_ok(e->sec) || (e->sec != SEC_OPEN && e->group != 4) || dfs != pass) continue;
            int k = nc++; while (k > 0 && ((cand[k - 1]->chan >= 52 && cand[k - 1]->chan <= 144) == dfs) && cand[k - 1]->rssi < e->rssi) { cand[k] = cand[k - 1]; k--; }
            cand[k] = e;
        }
    if (!b) { cn.state = W_FAIL; cn.why = "no se encuentra esa red (fuera de alcance o nombre distinto)"; unlock(); connecting = 0; return; }
    if (!sec_ok(b->sec)) { cn.state = W_FAIL; cn.why = b->sec == SEC_WPA3_SAE ? "la red es solo WPA3 (SAE): aun no soportado" : b->sec == SEC_ENTERPRISE ? "la red es WPA2-Enterprise: no soportado" : "seguridad no soportada (WEP o WPA antiguo)"; unlock(); connecting = 0; return; }
    if (b->sec != SEC_OPEN && b->group != 4) { cn.state = W_FAIL; cn.why = "la red usa TKIP para el grupo: no soportado"; unlock(); connecting = 0; return; }
    uint8_t pmk[32]; int si = saved_find(ssid);
    if (b->sec != SEC_OPEN) {
        if (pass && pass[0]) {
            uint32_t sl = 0; while (ssid[sl]) sl++;
            wl("calculando la clave de la red (PBKDF2)");
            if (wpa_psk_to_pmk(pass, (const uint8_t *)ssid, sl, pmk)) { cn.state = W_FAIL; cn.why = "contrasena no valida (8 a 63 caracteres)"; unlock(); connecting = 0; return; }
        } else if (si >= 0 && !saved[si].open) mcpy(pmk, saved[si].pmk, 32);
        else { cn.state = W_FAIL; cn.why = "hace falta la contrasena"; unlock(); connecting = 0; return; }
    }
    int r = -1;
    for (int i = 0; i < nc; i++) {
        if (i) wlv("se prueba otro punto de acceso de la misma red (canal)", cand[i]->chan);
        r = connect_to(cand[i], pmk);
        if (r == 0 || cn.eapol_err == (uint32_t)-WPA_E_MIC || cn.deauth_reason == 15 || r == -2) break;
    }
    if (r == 0 && save) saved_put(ssid, pmk, b->sec == SEC_OPEN);
    if (r && (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR))) { read_err_table(); wlv("el firmware se detuvo: codigo", st.fw_err_id); wlv("  PC", st.fw_err_pc); }
    if (r) { if (mm) stop_device(); st.up = 0; cn.configured = 0; bringup(); }    /* firmware limpio para buscar otra vez */
    unlock();
    user_disc = 0;
    connecting = 0;
}

static void mgr_disconnect(void)
{
    lock();
    if (cn.state == W_UP) {
        uint8_t fr[32]; uint32_t n = hdr80211(fr, 0xC0, 0x00, cn.bssid, cn.bssid);
        fr[n++] = 3; fr[n++] = 0;                                         /* motivo 3: la estacion se va */
        tx_frame(fr, n, 1); uint64_t e = timer_get_uptime_ms() + 50; while (timer_get_uptime_ms() < e) { rx_handle(); thread_sleep(2); }
    }
    cn.state = W_IDLE; cn.keys = 0; wl("desconectada por el usuario");
    if (mm) stop_device();
    st.up = 0; cn.configured = 0;
    if (!wifi_off) bringup();
    unlock();
}

static void mgr_thread(void *a)
{
    (void)a;
    thread_sleep(1500);
    while (busy) thread_sleep(100);                                      /* el arranque inicial (hilo wifi) primero */
    next_auto = timer_get_uptime_ms() + 2000;
    for (;;) {
        thread_sleep(cn.state == W_UP ? 8 : 100);
        if (st.up && !__sync_lock_test_and_set(&io_lock, 1)) {
            rx_handle();
            if (r32(CSR_INT) & (INT_SW_ERR | INT_HW_ERR)) {
                st.up = 0; st.ok = 0; st.stage = "en marcha"; st.why = "el firmware se detuvo con un error"; read_err_table();
                if (cn.state == W_UP) { cn.state = W_FAIL; cn.why = "el firmware se detuvo"; }
                wlv("el firmware se detuvo (codigo)", st.fw_err_id);
                cn.configured = 1; need_restart = 1; next_auto = timer_get_uptime_ms() + 3000;
            }
            unlock();
        }
        if ((cn.fw_keys_pending || cn.fw_gtk_pending) && cn.state == W_UP) { lock(); install_pending_keys(); unlock(); }   /* SOMA_175C3 */
        if (cn.tim_wake && cn.state == W_UP && timer_get_uptime_ms() - cn.last_wake_ms > 150) {   /* SOMA_175C4: el AP nos guarda tramas: */
            cn.tim_wake = 0; cn.last_wake_ms = timer_get_uptime_ms(); cn.tim_nulls++;          /* nula sin PM para que las suelte */
            lock(); uint8_t nf[24]; hdr80211(nf, 0x48, 0x01, cn.bssid, cn.bssid); tx_frame(nf, 24, 0); unlock();
        }
        uint64_t now = timer_get_uptime_ms();
        if (cn.lost) {
            cn.lost = 0; cn.keys = 0; cn.state = W_IDLE;
            wl("conexion perdida: se reintentara");
            lock(); if (mm) stop_device(); st.up = 0; cn.configured = 0; bringup(); unlock();
            next_auto = now + 3000;
        }
        if (chub_pending && st.up && cn.state != W_UP && !connecting) {   /* el firmware dedujo el pais: se le confirma (MCC_UPDATE) */
            chub_pending = 0;
            uint8_t m[28]; mzero(m, sizeof m); m[0] = (uint8_t)chub_mcc; m[1] = (uint8_t)(chub_mcc >> 8); m[2] = chub_src;
            lock();
            if (!cn.configured && send_cmd(0xC8, 0, m, sizeof m, 1000) == 0) {
                known_mcc = chub_mcc; known_src = chub_src;
                if (resp_len >= 8 + 16) { uint16_t mm2 = (uint16_t)(resp_buf[12] | resp_buf[13] << 8); st.mcc[0] = (char)(mm2 >> 8); st.mcc[1] = (char)mm2; st.mcc[2] = 0; st.mcc_chans = *(uint32_t *)(resp_buf + 20); }
                wlv("region actualizada por el firmware (canales)", st.mcc_chans);
            }
            unlock();
        }
        if (req_disc) { req_disc = 0; user_disc = 1; mgr_disconnect(); }
        if (req_conn) { req_conn = 0; char s[33], p[65]; for (int i = 0; i < 33; i++) s[i] = req_ssid[i]; for (int i = 0; i < 65; i++) p[i] = req_pass[i]; for (int i = 0; i < 65; i++) req_pass[i] = 0; mgr_connect(s, p, req_save); }
        if (req_scan) { req_scan = 0; scanning = 1; lock(); scan_locked(); unlock(); scanning = 0; }
        if (wifi_off) { if (st.up || cn.state == W_UP) { lock(); if (mm) stop_device(); st.up = 0; cn.state = W_IDLE; cn.keys = 0; cn.configured = 0; unlock(); wl("WiFi apagada"); } continue; }
        if (need_restart && !connecting && now >= next_auto) {           /* el firmware se paro con un error: se rearranca */
            need_restart = 0; next_auto = now + 15000;
            wl("rearrancando el firmware");
            lock(); if (mm) stop_device(); cn.configured = 0; bringup(); unlock();
            if (!st.up) need_restart = 1;
            continue;
        }
        /* conexion automatica a una red guardada */
        saved_load();
        if (cn.state != W_UP && !connecting && !user_disc && nsaved > 0 && st.up && now >= next_auto) {
            next_auto = now + 30000;
            scanning = 1; lock(); int r = scan_locked(); unlock(); scanning = 0;
            if (r == 0) {
                const struct iwl_bss *best = 0; int bi = -1;
                for (int i = 0; i < nbss; i++) { int k = saved_find(bss[i].ssid); if (k >= 0 && bss[i].ssid[0] && sec_ok(bss[i].sec) && (!best || bss[i].rssi > best->rssi)) { best = &bss[i]; bi = k; } }
                if (best) { wl("conexion automatica a una red guardada"); char s[33]; for (int i = 0; i < 33; i++) s[i] = saved[bi].ssid[i]; mgr_connect(s, 0, 0); if (cn.state != W_UP) next_auto = timer_get_uptime_ms() + 30000; }
            }
        }
        /* mantener viva la asociacion: trama nula si no se envia nada en 25 s */
        if (cn.state == W_UP && now - cn.last_tx_ms > 25000) {
            lock(); uint8_t fr[24]; hdr80211(fr, 0x48, 0x01, cn.bssid, cn.bssid); tx_frame(fr, 24, 0); unlock();
        }
    }
}

/* ---------------------------------------------------------------- interfaz de red wlan0 */
static int wl_init(void) { return st.dev_id != 0; }
static int wl_link(void) { return cn.state == W_UP; }
static const uint8_t *wl_mac(void) { return st.mac; }
static int wl_send(const void *frame, uint16_t len)
{
    if (cn.state != W_UP || len < 14) return -1;
    const uint8_t *e = (const uint8_t *)frame;
    lock();
    int r = tx_data(e, (uint16_t)(e[12] << 8 | e[13]), e + 14, (uint32_t)len - 14);
    unlock();
    return r ? -1 : 0;
}
static int wl_poll(void *out, uint16_t max, uint16_t *len_out)
{
    if (rxq_tail == rxq_head && cn.state == W_UP && !__sync_lock_test_and_set(&io_lock, 1)) { rx_handle(); unlock(); }
    if (rxq_tail == rxq_head) return 0;
    uint32_t i = rxq_tail % RXQ_N; uint16_t n = rxq_len[i];
    if (n > max) n = max;
    mcpy((uint8_t *)out, rxq_buf[i], n);
    *len_out = n; cn.rx_polled++;
    __asm__ volatile ("" ::: "memory");
    rxq_tail++;
    return 1;
}
static void wl_stats(uint32_t *t, uint32_t *r, uint64_t *tb, uint64_t *rb) { if (t) *t = cn.tx_ok; if (r) *r = cn.rx_data; if (tb) *tb = 0; if (rb) *rb = 0; }
const struct netif_ops netif_iwl = { "wlan0", "WiFi Intel 8265", wl_init, wl_send, wl_poll, wl_mac, wl_link, wl_stats };

/* ---------------------------------------------------------------- API publica (Ajustes y terminal) */
void iwl_wifi_status(struct iwl_wifi_status *o)
{
    mzero(o, sizeof *o);
    o->present = st.dev_id != 0; o->ready = st.up; o->enabled = !wifi_off;
    o->state = cn.state; o->state_name = wstate_name[cn.state < 7 ? cn.state : 6];
    o->why = cn.why ? cn.why : ""; if (!o->present) o->why = st.why ? st.why : "";
    if (o->present && !st.up && cn.state != W_UP && st.why && st.why[0] && !cn.why) o->why = st.why;
    for (int i = 0; i < 33; i++) o->ssid[i] = cn.ssid[i];
    mcpy(o->bssid, cn.bssid, 6); o->chan = cn.chan; o->rssi = cn.rssi;
    o->scanning = scanning || req_scan; o->connecting = connecting || req_conn; o->nbss = nbss;
    o->tx = cn.tx_ok; o->rx = cn.rx_data; o->rate_mbps = ofdm_mbps[cn.rate_lvl < 0 ? 0 : cn.rate_lvl > 5 ? 5 : cn.rate_lvl];
}
int iwl_wifi_request_scan(void) { if (!st.dev_id || wifi_off) return -1; if (cn.state == W_UP) return -2; req_scan = 1; return 0; }
int iwl_wifi_request_connect(const char *ssid, const char *pass, int save)
{
    if (!st.dev_id || wifi_off || connecting || req_conn) return -1;
    int i = 0; while (ssid[i] && i < 32) { req_ssid[i] = ssid[i]; i++; } req_ssid[i] = 0;
    i = 0; if (pass) while (pass[i] && i < 64) { req_pass[i] = pass[i]; i++; } req_pass[i] = 0;
    req_save = save; connecting = 1; req_conn = 1;
    return 0;
}
void iwl_wifi_request_disconnect(void) { req_disc = 1; }
int iwl_wifi_is_saved(const char *ssid) { return saved_find(ssid) >= 0; }
int iwl_wifi_forget(const char *ssid)
{
    int i = saved_find(ssid); if (i < 0) return -1;
    for (int k = i + 1; k < nsaved; k++) saved[k - 1] = saved[k];
    nsaved--; saved_write();
    return 0;
}
int iwl_wifi_saved_count(void) { saved_load(); return nsaved; }
const char *iwl_wifi_saved_name(int i) { saved_load(); return (i >= 0 && i < nsaved) ? saved[i].ssid : ""; }
void iwl_wifi_enable(int on) { wifi_off = !on; if (on) { next_auto = timer_get_uptime_ms() + 500; user_disc = 0; } }

static void boot_thread(void *a)
{
    (void)a;
    busy = 1;
    lock(); int r = bringup(); unlock();
    busy = 0;
    kprint("WIFI: ");
    if (r == 0) {
        kprint("Intel 8265: firmware "); kprint(fw.human); kprint(st.up ? " LISTA PARA BUSCAR REDES (ALIVE 0x" : st.calibrated ? " ARRANCADO Y RADIO CALIBRADA (ALIVE 0x" : " ARRANCADO (ALIVE 0x"); kprint_hex32(st.alive_status);
        kprint(", "); kprint_dec(st.bytes_loaded / 1024); kprint(" KiB en "); kprint_dec(st.load_ms); kprint(" ms) | orden wifi\n");
    } else if (st.dev_id) { kprint("Intel 8265: fallo en '"); kprint(st.stage); kprint("': "); kprint(st.why); kprint(" | orden wifi\n"); }
    else { kprint(st.why ? st.why : "?"); kprint("\n"); }
}
void iwl_start(void) { thread_create("wifi", boot_thread, 0); thread_create("wifi-gestor", mgr_thread, 0); }   /* SOMA_175C: gestor */

int iwl_scan(void)
{
    if (!st.dev_id) return -10;
    if (cn.state == W_UP) return -11;
    scanning = 1; lock(); int r = scan_locked(); unlock(); scanning = 0;
    return r;
}
int iwl_ip_is_static(void) { return cn.ipfija && cn.state == W_UP; }   /* SOMA_175C6: netmgr no pide DHCP si hay IP fija puesta con wifi ipfija */
const struct iwl_bss *iwl_bss_list(int *n) { if (n) *n = nbss; return bss; }

const struct iwl_info *iwl_get(void) { return &st; }

/* SOMA_175C: palabra n-esima de la orden (admite "nombres con espacios" entre comillas) */
static int arg_word(const char *a, int idx, char *out, int max)
{
    int k = 0;
    for (int w = 0; ; w++) {
        while (*a == ' ') a++;
        if (!*a) { out[0] = 0; return 0; }
        int q = *a == '"'; if (q) a++;
        k = 0;
        while (*a && (q ? *a != '"' : *a != ' ')) { if (w == idx && k < max - 1) out[k++] = *a; a++; }
        if (q && *a == '"') a++;
        if (w == idx) { out[k] = 0; return 1; }
    }
}
static int weq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return !*a && !*b; }
extern const char *netmgr_state(void);

static void print_ip(void) { extern uint8_t net_ip[4]; kprint_ip(net_ip); }

/* ---------------------------------------------------------------- SOMA_175C5: bateria de pruebas (wifi prueba) y IP fija (wifi ipfija) */
extern int dhcp_run(int verbose);
extern uint8_t net_ip[4], net_gateway[4], net_netmask[4], net_dns[4];
extern void net_arp_flush(void);
static int pick_router(uint8_t *gw)                                     /* IP del router segun lo visto en la red */
{
    int best = -1;
    for (int i = 0; i < 8 && seen[i].n; i++)
        if (best < 0 || seen[i].ap > seen[best].ap || (seen[i].ap == seen[best].ap && seen[i].n > seen[best].n)) best = i;
    if (best >= 0) { mcpy(gw, seen[best].ip, 4); return 1; }
    gw[0] = 192; gw[1] = 168; gw[2] = 1; gw[3] = 1; return 0;
}
static void probe_arp(const uint8_t *tip, const uint8_t *sip)
{
    static const uint8_t bc[6] = { 255, 255, 255, 255, 255, 255 };
    uint8_t a[28]; mzero(a, sizeof a);
    a[1] = 1; a[2] = 8; a[4] = 6; a[5] = 4; a[7] = 1;
    mcpy(a + 8, st.mac, 6); mcpy(a + 14, sip, 4); mcpy(a + 24, tip, 4);
    lock(); tx_data(bc, 0x0806, a, 28); unlock();
}
static void iwl_probe(int fix);
static void iwl_probe(int fix)
{
    if (cn.state != W_UP || !cn.keys) { kprint("wifi: no hay conexion con claves (conecta primero con wifi conectar)\n"); return; }
    uint8_t gw[4], sip[4]; int known = pick_router(gw);
    mcpy(sip, gw, 4); sip[3] = gw[3] == 250 ? 249 : 250;
    if (fix) {
        mcpy(net_ip, sip, 4); mcpy(net_gateway, gw, 4); mcpy(net_dns, gw, 4);
        net_netmask[0] = 255; net_netmask[1] = 255; net_netmask[2] = 255; net_netmask[3] = 0; net_arp_flush(); cn.ipfija = 1;
        kprint("wifi: IP fija "); kprint_ip(net_ip); kprint(" puerta/DNS "); kprint_ip(gw); kprint(known ? " (router deducido de la red)\n" : " (router supuesto: no se ha visto trafico)\n");
        return;
    }
    kprint("wifi prueba: router "); kprint_ip(gw); kprint(known ? " (visto en la red)" : " (supuesto)"); kprint(", nuestra IP de prueba "); kprint_ip(sip); kprint("\n");
    static const struct { uint8_t sw, tid0, seq, qos; } combos[9] = { {0,0,0,0}, {1,0,0,0}, {1,0,1,0}, {0,0,1,0}, {1,1,0,0}, {0,1,0,0}, {1,0,0,1}, {0,0,0,1}, {1,0,1,1} };
    int o_sw = cn.sw_tx, o_t = cn.tid0, o_s = cn.seq_sw, o_q = cn.qos_tx, found = -1;
    mcpy(cn.probe_ip, gw, 4);
    for (int i = 0; i < 9 && found < 0 && cn.state == W_UP; i++) {
        cn.sw_tx = combos[i].sw; cn.tid0 = combos[i].tid0; cn.seq_sw = combos[i].seq; cn.qos_tx = combos[i].qos;
        cn.probe_got = 0;
        for (int k = 0; k < 3 && !cn.probe_got; k++) { probe_arp(gw, sip); for (int t = 0; t < 40 && !cn.probe_got; t++) thread_sleep(10); }
        kprint("  "); kprint_dec((uint32_t)i + 1); kprint(") cifrado "); kprint(combos[i].sw ? "sw" : "hw"); kprint(", TID "); kprint(combos[i].tid0 ? "0" : "8");
        kprint(", secuencia "); kprint(combos[i].seq ? "driver" : "firmware"); kprint(", QoS "); kprint(combos[i].qos ? "si" : "no"); kprint(": ");
        kprint(cn.probe_got ? "EL ROUTER RESPONDE\n" : "sin respuesta\n");
        if (cn.probe_got) found = i;
    }
    cn.probe_ip[0] = 0;
    if (found < 0) {
        cn.sw_tx = o_sw; cn.tid0 = o_t; cn.seq_sw = o_s; cn.qos_tx = o_q;
        kprint("wifi prueba: ninguna combinacion obtuvo respuesta. Mira 'wifi tramas' y mandame la foto.\n");
        return;
    }
    kprint("wifi prueba: ajustes que funcionan (quedan puestos). Probando DHCP...\n");
    if (dhcp_run(1) != 0) { kprint("wifi prueba: el ARP funciona pero el DHCP no: se pone IP fija para poder navegar ('wifi tramas' dice por que falla el DHCP)\n"); iwl_probe(1); }
}

void iwl_command(const char *args)
{
    while (args && *args == ' ') args++;
    char w0[16], w1[40], w2[72];
    arg_word(args ? args : "", 0, w0, sizeof w0);
    if (weq(w0, "registro")) {                                          /* SOMA_175C: ultimos sucesos de la conexion */
        uint32_t n = wlog_n < WLOG_N ? wlog_n : WLOG_N;
        kprint("WiFi: ultimos "); kprint_dec(n); kprint(" sucesos\n");
        for (uint32_t i = wlog_n - n; i < wlog_n; i++) {
            const typeof(wlog[0]) *e = &wlog[i % WLOG_N];
            kprint("  "); kprint_dec(e->ms / 1000); kprint("."); uint32_t f = e->ms % 1000; if (f < 100) kprint("0"); if (f < 10) kprint("0"); kprint_dec(f);
            kprint(" s  "); kprint(e->s); if (e->hasv) { kprint(": "); kprint_dec(e->v); } kprint("\n");
        }
        return;
    }
    if (weq(w0, "guardadas")) {
        int n = iwl_wifi_saved_count();
        kprint("Redes guardadas (/etc/wifi.conf, sin la contrasena en claro): "); kprint_dec((uint32_t)n); kprint("\n");
        for (int i = 0; i < n; i++) { kprint("  "); kprint(iwl_wifi_saved_name(i)); kprint("\n"); }
        return;
    }
    if (weq(w0, "olvidar")) {
        if (!arg_word(args, 1, w1, sizeof w1)) { kprint("uso: wifi olvidar <red>   (con comillas si tiene espacios)\n"); return; }
        kprint(iwl_wifi_forget(w1) == 0 ? "wifi: red olvidada\n" : "wifi: esa red no estaba guardada\n");
        return;
    }
    if (weq(w0, "cifrado")) {                                           /* SOMA_175C3: diagnostico: wifi cifrado sw | hw */
        arg_word(args, 1, w1, sizeof w1);
        if (weq(w1, "sw")) cn.sw_tx = 1; else if (weq(w1, "hw")) cn.sw_tx = 0;
        kprint("wifi: cifrado de lo que se envia: "); kprint(cn.sw_tx ? "en el procesador (sw)\n" : "en la tarjeta (hw, como Linux)\n");
        return;
    }
    if (weq(w0, "tramas")) {                                            /* SOMA_175C5: registro de tramas de datos y gestion */
        arg_word(args, 1, w1, sizeof w1);
        if (weq(w1, "limpiar")) { flog_n = 0; for (int i = 0; i < 8; i++) seen[i].n = 0; kprint("wifi: registro de tramas vaciado\n"); return; }
        uint32_t n = flog_n < FLOG_N ? flog_n : FLOG_N, show = n < 40 ? n : 40;
        kprint("WiFi: tramas (> enviada, < recibida; P = cifrada, d1 = repeticion, d2 = MIC malo, d3 = otra clave) total "); kprint_dec(flog_n); kprint("\n");
        for (uint32_t i = flog_n - show; i < flog_n; i++) {
            const struct flog_e *e = &flog[i % FLOG_N];
            kprint(" "); kprint_dec(e->ms / 1000); kprint("."); kprint_dec((e->ms % 1000) / 100); kprint(e->dir ? " < " : " > "); kprint(e->prot ? "P " : "  ");
            if (e->why >= 0x10) { kprint("gestion/otra de "); kprint_hex32(e->sa[3] << 16 | e->sa[4] << 8 | e->sa[5]); kprint(" tipo 0x"); kprint_hex32(e->op); kprint(e->why == 0x11 ? " (no es del AP)" : ""); kprint("\n"); continue; }
            if (e->why) { kprint("DESCARTADA d"); kprint_dec(e->why); kprint(" de ..:"); kprint_hex32(e->sa[3] << 16 | e->sa[4] << 8 | e->sa[5]); kprint("\n"); continue; }
            kprint(e->dir ? "para ..:" : "a ..:"); kprint_hex32(e->da[3] << 16 | e->da[4] << 8 | e->da[5]);
            kprint(" de ..:"); kprint_hex32(e->sa[3] << 16 | e->sa[4] << 8 | e->sa[5]); kprint(" ");
            if (e->et == 0x0806) { kprint("ARP "); kprint(e->op == 1 ? "pide " : e->op == 2 ? "responde " : "? "); kprint_ip(e->dip); kprint(" dice "); kprint_ip(e->sip); }
            else if (e->et == 0x0800) { kprint("IPv4 proto "); kprint_dec(e->proto); kprint(" "); kprint_ip(e->sip); if (e->sp || e->dp) { kprint(":"); kprint_dec(e->sp); } kprint(" -> "); kprint_ip(e->dip); if (e->sp || e->dp) { kprint(":"); kprint_dec(e->dp); } }
            else if (e->et == 0x888E) kprint("EAPOL"); else { kprint("tipo 0x"); kprint_hex32(e->et); }
            kprint(" ("); kprint_dec(e->len); kprint(" B)\n");
        }
        kprint("  vistas en la red (IP, veces, * = la envia el AP):");
        for (int i = 0; i < 8 && seen[i].n; i++) { kprint(" "); kprint_ip(seen[i].ip); kprint("x"); kprint_dec(seen[i].n); if (seen[i].ap) kprint("*"); }
        kprint("\n  tramas dirigidas a nuestra MAC: "); kprint_dec(cn.rx_to_me); kprint(" (de otro emisor que el AP: "); kprint_dec(cn.rx_to_me_other); kprint(")\n");
        kprint("  cola hacia la pila: puestas "); kprint_dec(cn.rx_pushed); kprint(", recogidas "); kprint_dec(cn.rx_polled); kprint(", pendientes "); kprint_dec(rxq_head - rxq_tail); kprint(" | net_mac ..:"); kprint_hex32(net_mac[3] << 16 | net_mac[4] << 8 | net_mac[5]); kprint(" | wlan ..:"); kprint_hex32(st.mac[3] << 16 | st.mac[4] << 8 | st.mac[5]); kprint("\n");
        kprint("  DHCP recibido (para la pila): "); kprint_dec(cn.dhcp_seen); kprint("\n");
        for (uint32_t i = ddiag_n > DDIAG_N ? ddiag_n - DDIAG_N : 0; i < ddiag_n; i++) {
            const typeof(ddiag[0]) *d = &ddiag[i % DDIAG_N];
            kprint("   "); kprint_dec(d->ms / 1000); kprint("."); kprint_dec((d->ms % 1000) / 100); kprint(" op "); kprint_dec(d->op); kprint(" tipo "); kprint_dec(d->typ); kprint(" xid "); kprint_hex32(d->xid);
            kprint(" chaddr ..:"); kprint_hex32(d->ch[0] << 16 | d->ch[1] << 8 | d->ch[2]); kprint(" | IP total "); kprint_dec(d->total); kprint(" de "); kprint_dec(d->plen); kprint(" | cs IP "); kprint(d->ipck ? "ok" : "MAL");
            kprint(" UDP "); kprint(d->udpck == 2 ? "0" : d->udpck ? "ok" : "MAL"); kprint(" | dhcp_parse: ");
            if (d->ret == 0xFE) kprint("DESCARTADO por total>recibido"); else if (d->ret == 0xFD) kprint("DESCARTADO udp len"); else if (d->ret == 0xFC) kprint("corto"); else if (d->ret == 0xF0) kprint("MAL FORMADO"); else if (d->ret == 0) kprint("no es para net_mac"); else { kprint("ACEPTADO tipo "); kprint_dec(d->ret); }
            kprint("\n");
        }
        kprint("  ultima respuesta TX (palabras):"); for (int i = 0; i < 10; i++) { kprint(" "); kprint_hex32(cn.txr[i]); } kprint("\n");
        kprint("  ajustes TX: cifrado "); kprint(cn.sw_tx ? "sw" : "hw"); kprint(" | TID datos "); kprint(cn.tid0 ? "0" : "8"); kprint(" | secuencia "); kprint(cn.seq_sw ? "del driver" : "del firmware"); kprint(" | QoS "); kprint(cn.qos_tx ? "si" : "no"); kprint("\n");
        return;
    }
    if (weq(w0, "qos") || weq(w0, "tid") || weq(w0, "seq")) {            /* SOMA_175C5: interruptores de diagnostico */
        arg_word(args, 1, w1, sizeof w1);
        if (weq(w0, "qos")) { if (weq(w1, "on")) cn.qos_tx = 1; else if (weq(w1, "off")) cn.qos_tx = 0; kprint("wifi: QoS en lo que se envia: "); kprint(cn.qos_tx ? "si (on)\\n" : "no (off)\\n"); }
        else if (weq(w0, "tid")) { if (weq(w1, "0")) cn.tid0 = 1; else if (weq(w1, "8")) cn.tid0 = 0; kprint("wifi: TID de los datos sin QoS: "); kprint(cn.tid0 ? "0\\n" : "8 (como Linux)\\n"); }
        else { if (weq(w1, "sw")) cn.seq_sw = 1; else if (weq(w1, "fw")) cn.seq_sw = 0; kprint("wifi: numero de secuencia lo pone: "); kprint(cn.seq_sw ? "el driver (sw)\\n" : "el firmware (fw)\\n"); }
        return;
    }
    if (weq(w0, "prueba") || weq(w0, "ipfija")) { iwl_probe(weq(w0, "ipfija")); return; }   /* SOMA_175C5 */
    if (weq(w0, "desactivar")) { iwl_wifi_enable(0); kprint("wifi: desactivada (wifi activar para volver)\n"); return; }
    if (weq(w0, "activar")) { iwl_wifi_enable(1); kprint("wifi: activada\n"); return; }
    if (weq(w0, "desconectar")) {
        if (cn.state != W_UP) { kprint("wifi: no hay conexion\n"); return; }
        iwl_wifi_request_disconnect();
        for (int i = 0; i < 300 && cn.state == W_UP; i++) thread_sleep(10);
        kprint("wifi: desconectada (no se reconectara sola hasta que conectes otra vez)\n");
        return;
    }
    if (weq(w0, "conectar")) {
        if (!arg_word(args, 1, w1, sizeof w1) || !w1[0]) { kprint("uso: wifi conectar <red> [contrasena]   (comillas si el nombre tiene espacios)\n"); return; }
        arg_word(args, 2, w2, sizeof w2);
        if (!st.dev_id) { kprint("wifi: no hay tarjeta\n"); return; }
        if (iwl_wifi_request_connect(w1, w2[0] ? w2 : 0, 1)) { kprint("wifi: ya hay una conexion en curso o la WiFi esta desactivada\n"); return; }
        kprint("wifi: conectando a "); kprint(w1); kprint("...\n");
        int last = -1;
        for (int t = 0; t < 4000 && (connecting || req_conn); t++) {
            if (cn.state != last) { last = cn.state; kprint("  "); kprint(wstate_name[last < 7 ? last : 6]); kprint("\n"); }
            thread_sleep(10);
        }
        if (cn.state != W_UP) {
            kprint("wifi: NO conectada: "); kprint(cn.why && cn.why[0] ? cn.why : "?"); kprint("\n  detalles: wifi registro\n");
            return;
        }
        kprint("wifi: CONECTADA a "); kprint(cn.ssid); kprint(" (canal "); kprint_dec(cn.chan); kprint(", "); kprint(wpa_sec_name(cn.sec)); kprint("); red guardada\n");
        kprint("  pidiendo direccion IP (DHCP)...\n");
        for (int t = 0; t < 1500; t++) {
            const struct netif_ops *a = netif_active();
            if (a == &netif_iwl && netmgr_state()[0] == 'c') { kprint("  IP "); print_ip(); kprint(" por wlan0\n"); return; }
            if (a && a != &netif_iwl && netmgr_state()[0] == 'c' && t > 300) { kprint("  la WiFi esta lista, pero se sigue usando "); kprint(a->name); kprint(" (el cable tiene preferencia)\n"); return; }
            thread_sleep(10);
        }
        kprint("  aun sin IP (orden red para ver el estado)\n");
        return;
    }
    if (weq(w0, "buscar")) {                                           /* SOMA_175B2: "buscar" */
        if (cn.state == W_UP) { kprint("wifi: conectada a "); kprint(cn.ssid); kprint(": desconecta (wifi desconectar) para buscar otras redes\n"); return; }
        if (!st.up && !st.dev_id) { kprint("wifi: la tarjeta no esta lista para buscar (orden wifi para ver por que)\n"); return; }
        kprint("wifi: buscando redes en "); kprint_dec(sizeof scan_chans); kprint(" canales (2,4 y 5 GHz, pasiva: unos segundos)...\n");
        int r = iwl_scan();
        if (r) {
            kprint("wifi: la busqueda fallo: ");
            kprint(r == -1 ? "el firmware no acepto la orden (sin respuesta)" : r == -2 ? "el firmware se detuvo" : r == -4 ? "no termino en 15 s" : r == -10 ? "el firmware no arranco" : "error interno");
            kprint(" | tramas recibidas "); kprint_dec(scan_frames); kprint(" | redes "); kprint_dec((uint32_t)nbss);
            if (r == -2) { kprint("\n  error del firmware 0x"); kprint_hex32(st.fw_err_id); kprint(" PC 0x"); kprint_hex32(st.fw_err_pc); }
            if (st.rerr_n) { kprint("\n  orden rechazada: tipo 0x"); kprint_hex32(st.rerr_type); kprint(" orden 0x"); kprint_hex8(st.rerr_cmd); }
            kprint("\n");
            if (!nbss) return;
        }
        /* ordenadas por senal */
        int idx[IWL_MAX_BSS]; for (int i = 0; i < nbss; i++) idx[i] = i;
        for (int i = 1; i < nbss; i++) { int v = idx[i], j = i; while (j > 0 && bss[idx[j - 1]].rssi < bss[v].rssi) { idx[j] = idx[j - 1]; j--; } idx[j] = v; }
        kprint("Redes encontradas: "); kprint_dec((uint32_t)nbss); kprint(" (estado de la busqueda "); kprint_dec(scan_status); kprint(", "); kprint_dec(scan_frames); kprint(" tramas, "); kprint_dec(scan_phys); kprint(" medidas)\n");
        kprint("  SENAL  CANAL  SEGURIDAD  BSSID              RED\n");
        for (int i = 0; i < nbss; i++) {
            const struct iwl_bss *e = &bss[idx[i]];
            int rs = e->rssi; kprint("  ");
            if (rs <= -128) kprint("   ?    ");                             /* SOMA_175B3: sin medida de la radio para esta red */
            else { if (rs < 0) { kprint("-"); rs = -rs; } else kprint(" "); kprint_dec((uint32_t)rs); kprint(rs < 100 ? " dBm " : "dBm "); }
            kprint(e->chan < 10 ? "    " : e->chan < 100 ? "   " : "  "); kprint_dec(e->chan); kprint("   ");
            const char *sn = wpa_sec_name(e->sec); kprint(sn);
            int pad = 11; for (const char *t = sn; *t; t++) pad--; while (pad-- > 0) kprint(" ");
            for (int k = 0; k < 6; k++) { kprint_hex8(e->bssid[k]); if (k < 5) kprint(":"); }
            kprint("  "); kprint(e->ssid[0] ? e->ssid : "(oculta)"); kprint("\n");
        }
        return;
    }
    if (weq(w0, "arrancar")) {                                         /* repetir ("arrancar sinreset": sin SW_RESET) */
        if (busy) { kprint("wifi: ya esta arrancando\n"); return; }
        if (cn.state == W_UP) { kprint("wifi: conectada: primero wifi desconectar\n"); return; }
        arg_word(args, 1, w1, sizeof w1);
        no_reset = (w1[0] == 's');
        busy = 1; lock(); if (mm) stop_device(); st.up = 0; cn.configured = 0; bringup(); unlock(); busy = 0; no_reset = 0;
    } else if (w0[0]) { kprint("wifi: orden desconocida\n"); }
    kprint("WiFi (Intel Dual Band Wireless-AC 8265): ");
    if (!st.dev_id) {
        kprint(st.why && st.why[0] ? st.why : "sin iniciar");
        uint64_t ms, ml;
        if (bootinfo_module("iwlwifi", &ms, &ml)) {
            kprint("\n  firmware: modulo de GRUB en 0x"); kprint_hex32((uint32_t)ms); kprint(" ("); kprint_dec((uint32_t)(ml / 1024)); kprint(" KiB)");
            if (fw.human[0]) { kprint(", "); kprint(fw.human); kprint(", INIT "); kprint_dec((uint32_t)fw.img[IWL_IMG_INIT].n); kprint(" secciones, RUNTIME ");
                               kprint_dec((uint32_t)fw.img[IWL_IMG_RT].n); kprint(", paginacion "); kprint_dec(fw.paging_size / 1024); kprint(" KiB: CORRECTO"); }
        } else kprint("\n  firmware: NO cargado (falta firmware/iwlwifi-8265-36.ucode al compilar)");
        kprint("\n"); return;
    }
    kprint("8086:"); kprint_hex16(st.dev_id); kprint(" subsistema "); kprint_hex16((uint16_t)st.subsys);
    kprint(" | HW_REV 0x"); kprint_hex32(st.hw_rev); kprint(" | BAR 0x"); kprint_hex32((uint32_t)(st.bar >> 32)); kprint_hex32((uint32_t)st.bar);
    kprint("\n  firmware: ");
    if (fw.human[0]) { kprint(fw.human); kprint(" ("); kprint_dec(st.fw_size / 1024); kprint(" KiB, INIT "); kprint_dec((uint32_t)fw.img[IWL_IMG_INIT].n);
                       kprint(" secciones, RUNTIME "); kprint_dec((uint32_t)fw.img[IWL_IMG_RT].n); kprint(", paginacion "); kprint_dec(fw.paging_size / 1024); kprint(" KiB)"); }
    else kprint("no cargado");
    kprint("\n  estado: ");
    if (st.ok) {
        kprint(st.up ? "LISTA: firmware RUNTIME en marcha, radio calibrada (ALIVE 0x" : st.calibrated ? "FIRMWARE VIVO Y RADIO CALIBRADA (ALIVE 0x" : "FIRMWARE VIVO (ALIVE 0x"); kprint_hex32(st.alive_status); kprint(") LMAC "); kprint_dec(st.lmac_major); kprint("."); kprint_dec(st.lmac_minor);
        kprint(" UMAC "); kprint_dec(st.umac_major); kprint("."); kprint_dec(st.umac_minor);
        kprint("\n  cargados "); kprint_dec(st.sections_loaded); kprint(" secciones, "); kprint_dec(st.bytes_loaded / 1024); kprint(" KiB en "); kprint_dec(st.load_ms);
        kprint(" ms (total "); kprint_dec(st.total_ms); kprint(" ms) | tabla de errores 0x"); kprint_hex32(st.err_table); kprint(" | SCD 0x"); kprint_hex32(st.scd_base);
        if (st.calibrated) {
            kprint("\n  CALIBRADA: "); kprint_dec(st.phy_db); kprint(" resultados de calibracion guardados | MAC ");
            for (int i = 0; i < 6; i++) { kprint_hex8(st.mac[i]); if (i < 5) kprint(":"); }
            kprint(" | ordenes "); kprint_dec(st.cmds_sent);
        }
        if (st.up) {
            kprint("\n  RUNTIME: paginacion "); kprint_dec((uint32_t)npaging); kprint(" bloques | calibracion enviada "); kprint_dec(st.phy_db_sent);
            kprint(" | region "); kprint(st.mcc[0] ? st.mcc : "?"); kprint(" ("); kprint_dec(st.mcc_chans); kprint(" canales, estado "); kprint_dec(st.mcc_status); kprint(")");
            kprint("\n  ultima busqueda: "); kprint_dec((uint32_t)nbss); kprint(" redes   -> wifi buscar");
        }
    } else {
        kprint("FALLO en '"); kprint(st.stage); kprint("': "); kprint(st.why);
        if (st.fail_section >= 0) { kprint(" (seccion "); kprint_dec((uint32_t)st.fail_section); kprint(")"); }
        kprint("\n  cargados "); kprint_dec(st.sections_loaded); kprint(" secciones, "); kprint_dec(st.bytes_loaded / 1024); kprint(" KiB");
    }
    kprint(st.rfkill ? "\n  RADIO DESACTIVADA por hardware (modo avion del firmware?)" : "");
    {                                                                   /* SOMA_175C: conexion */
        kprint("\n  conexion: "); kprint(wifi_off ? "WiFi DESACTIVADA (wifi activar)" : wstate_name[cn.state < 7 ? cn.state : 6]);
        if (cn.ssid[0] && cn.state != W_IDLE) { kprint(" | red "); kprint(cn.ssid); kprint(" ("); for (int i = 0; i < 6; i++) { kprint_hex8(cn.bssid[i]); if (i < 5) kprint(":"); } kprint(", canal "); kprint_dec(cn.chan); kprint(")"); }
        if (cn.state == W_UP) {
            kprint("\n  "); kprint(wpa_sec_name(cn.sec)); kprint(" | AID "); kprint_dec(cn.aid); kprint(" | tasa "); kprint_dec(ofdm_mbps[cn.rate_lvl < 0 ? 0 : cn.rate_lvl > 5 ? 5 : cn.rate_lvl]); kprint(" Mbit/s");
            kprint(" | enviadas "); kprint_dec(cn.tx_ok); kprint(" (fallos "); kprint_dec(cn.tx_fail); kprint(") | recibidas "); kprint_dec(cn.rx_data);
            kprint(" | MIC malos "); kprint_dec(cn.rx_mic_fail); kprint(" | descartadas "); kprint_dec(cn.rx_drop);
            kprint("\n  claves en la tarjeta: individual 0x"); kprint_hex32(cn.fw_key_status); kprint(", grupo 0x"); kprint_hex32(cn.fw_gtk_status);
            kprint(" | cifrado TX "); kprint(cn.sw_tx ? "en el procesador" : "en la tarjeta"); kprint(" | RX descifradas por la tarjeta "); kprint_dec(cn.rx_hw_ok); kprint(" (fallidas "); kprint_dec(cn.rx_hw_bad); kprint(")");
            kprint("\n  balizas del AP "); kprint_dec(cn.bcn_n); kprint(" | con tramas guardadas para nosotros (TIM) "); kprint_dec(cn.tim_set); kprint(" | nulas para recogerlas "); kprint_dec(cn.tim_nulls);
            kprint("\n  TX datos: cifradas "); kprint_dec(cn.tx_enc); kprint(", en claro "); kprint_dec(cn.tx_plain); kprint(" | ultimo fallo 0x"); kprint_hex8((uint8_t)cn.last_fail_status);
            kprint("\n  RX: protegidas "); kprint_dec(cn.rx_prot); kprint(" (individuales ok "); kprint_dec(cn.rx_uc_ok); kprint(", MIC mal "); kprint_dec(cn.rx_mic_uc);
            kprint("; grupo ok "); kprint_dec(cn.rx_mc_ok); kprint(", MIC mal "); kprint_dec(cn.rx_mic_mc); kprint(", otra clave "); kprint_dec(cn.rx_kid_drop); kprint(")");
            kprint("\n      en claro tras las claves "); kprint_dec(cn.rx_plain_keys); kprint(" | ARP "); kprint_dec(cn.rx_arp); kprint(" IPv4 "); kprint_dec(cn.rx_ip); kprint(" otras "); kprint_dec(cn.rx_other);
            kprint(" | EAPOL: M1 "); kprint_dec(cn.m1); kprint(" M3 "); kprint_dec(cn.m3); kprint(" (repetidos "); kprint_dec(cn.m3_again); kprint(") | clave de grupo "); kprint_dec((uint32_t)cn.gtk_idx);
            kprint("\n  IP "); print_ip(); kprint(" ("); kprint(netmgr_state()); kprint(")");
        } else if (cn.why && cn.why[0]) { kprint("\n  ultimo fallo: "); kprint(cn.why); kprint("   (wifi registro)"); }
        kprint("\n  redes guardadas: "); kprint_dec((uint32_t)iwl_wifi_saved_count());
    }
    if (!st.ok && st.alive) {                                           /* SOMA_175B: fallo despues de ALIVE */
        kprint("\n  ordenes enviadas "); kprint_dec(st.cmds_sent); kprint(", sin respuesta "); kprint_dec(st.cmd_timeouts);
        if (st.last_timeout_cmd) { kprint(" (orden 0x"); kprint_hex32(st.last_timeout_cmd); kprint(")"); }
        kprint(" | SCD SRAM 0x"); kprint_hex32(st.scd_sram); kprint(" | calibracion: "); kprint_dec(st.phy_db); kprint(" resultados");
        kprint(" | MAC "); for (int i = 0; i < 6; i++) { kprint_hex8(st.mac[i]); if (i < 5) kprint(":"); }
        kprint("\n  INIT "); kprint(st.init_alive ? "ok" : "-"); kprint(", RUNTIME "); kprint(st.rt_alive ? "vivo" : "-");
        kprint(" | paginacion "); kprint_dec((uint32_t)npaging); kprint(" | calibracion enviada "); kprint_dec(st.phy_db_sent);
        kprint(" | ADD_STA 0x"); kprint_hex32(st.sta_status); kprint(" | region "); kprint(st.mcc[0] ? st.mcc : "-");
        kprint("\n  error del firmware: valida 0x"); kprint_hex32(st.fw_err_valid); kprint(" codigo 0x"); kprint_hex32(st.fw_err_id);
        kprint(" PC 0x"); kprint_hex32(st.fw_err_pc); kprint(" dato 0x"); kprint_hex32(st.fw_err_d1);
        kprint(st.fw_err_id == 0x38 || st.fw_err_id == 0x39 ? " (BAD_COMMAND)" : st.fw_err_id == 0x35 ? " (SYSASSERT)" : st.fw_err_id == 0x34 ? " (NMI_INTERRUPT_WDG)" : st.fw_err_id == 0x3D ? " (FATAL_ERROR)" : "");
        if (st.rerr_n) { kprint("\n  orden rechazada (REPLY_ERROR): tipo 0x"); kprint_hex32(st.rerr_type); kprint(" orden 0x"); kprint_hex8(st.rerr_cmd); kprint(" secuencia 0x"); kprint_hex16(st.rerr_seq); }
    } else if (!st.ok && st.sections_loaded) {
        kprint("\n  arranque seguro: CPU1 0x"); kprint_hex32(st.sb1); kprint(" CPU2 0x"); kprint_hex32(st.sb2);
        kprint(" | WFPM_GP2 0x"); kprint_hex32(st.wfpm); kprint(st.wfpm == 0x01010101u ? " (el firmware no lo toco)" : " (el firmware lo cambio: llego a ejecutarse)");
        kprint("\n  RX: config 0x"); kprint_hex32(st.rx_cfg); kprint(" base 0x"); kprint_hex32(st.rx_base); kprint(" wptr "); kprint_dec(st.rx_wptr);
        kprint(" estado 0x"); kprint_hex32(st.rx_status); kprint(" stts 0x"); kprint_hex32(st.stts0); kprint(" 0x"); kprint_hex32(st.stts1);
        kprint("\n  CSR_RESET 0x"); kprint_hex32(st.csr_reset); kprint(" | CSR_INT visto 0x"); kprint_hex32(st.int_seen);
    }
    kprint("\n  CSR_INT 0x"); kprint_hex32(st.csr_int); kprint(" FH_INT 0x"); kprint_hex32(st.fh_int); kprint(" GP_CNTRL 0x"); kprint_hex32(st.gp_cntrl);
    kprint("\n  LOAD_STATUS 0x"); kprint_hex32(st.load_status); kprint(" DRV_GP1 0x"); kprint_hex32(st.drv_gp1); kprint(" RX cerrados "); kprint_dec(st.rb_closed);
    kprint(" paquetes "); kprint_dec(st.rx_packets); kprint(" ultimo 0x"); kprint_hex32(st.last_cmd); kprint(" accesos fallidos "); kprint_dec(st.err_access);
    kprint("\nuso: wifi | wifi buscar | wifi conectar <red> [clave] | wifi desconectar | wifi guardadas | wifi olvidar <red>\n     wifi activar | wifi desactivar | wifi registro | wifi arrancar [sinreset]   (o Ajustes > Redes)\n     diagnostico: wifi prueba | wifi tramas [limpiar] | wifi ipfija | wifi cifrado sw|hw | wifi tid 0|8 | wifi seq fw|sw | wifi qos on|off\n");
}
