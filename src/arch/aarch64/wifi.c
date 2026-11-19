/* PARCHE 079: Wi-Fi de la Pi 5, fase 1. Ver wifi.h.
 *
 * Camino completo, de abajo arriba:
 *   1. Placa: revision por el mailbox del firmware (solo se intenta el Wi-Fi en modelos que lo llevan).
 *   2. Alimentacion: WL_ON (pin 28 del GPIO de la BCM2712) a 0 y luego a 1; funciones de los pines 30-35 (SD2).
 *   3. SDIO2 (SDHCI por sondeo, 1 bit, 400 kHz -> 25 MHz): CMD0, CMD5 (condiciones de E/S), CMD3 (direccion), CMD7 (seleccion).
 *   4. CMD52/CMD53: registros CCCR, CIS (fabricante 0x02d0 = Broadcom), funcion 1 = ventana de 32 KiB al bus interno del chip.
 *   5. Reloj ALP, identificador del chip (0x18000000) y EROM: direcciones de chipcommon, SDIO, ARM CR4, D11...
 *   6. Firmware: ARM CR4 detenido, D11 en reinicio, .bin en la RAM del chip (0x198000), NVRAM al final de la RAM, vector de arranque en 0,
 *      ARM en marcha, reloj HT, funcion 2 lista y estructura compartida publicada por el firmware (version y consola interna).
 * Todos los esperas tienen limite: si el chip no responde se informa y no se cuelga el sistema. */
#include <stdint.h>
#include "wifi.h"
#include "platform.h"
#include "console.h"
#include "idt.h"
#include "sd_proto.h"
#include "sdio_proto.h"
#include "rpi_fb.h"
#include "sdhci.h"
#include "fat.h"
#include "netif.h"
#include "net.h"
#include "brcm_proto.h"
#include "wpa.h"
#include "crypto.h"
#include "dhcp.h"
#include "arch.h"
#include "mem.h"
#include "rng.h"

/* ------------------------------------------------------------------ placa */
static struct rpi_board g_board; static int g_board_ok; static uint32_t g_board_rev;

int board_detect(void)
{
    if (g_board_ok) return 0;
    static uint32_t __attribute__((aligned(16))) b[8];
    b[0] = 8 * 4; b[1] = 0; b[2] = 0x00010002u; b[3] = 4; b[4] = 0; b[5] = 0; b[6] = 0; b[7] = 0;
    if (rpi_mbox_prop(b, sizeof b) != 0 || b[1] != 0x80000000u) return -1;
    g_board_rev = b[5];
    rpi_board_decode(g_board_rev, &g_board);
    g_board_ok = 1;
    return 0;
}
int board_has_wifi(void) { return g_board_ok && g_board.has_wifi; }
void board_print(void)
{
    if (board_detect() != 0) { kprint("placa: el firmware no responde (sin mailbox)\n"); return; }
    kprint("placa: "); kprint(g_board.name); kprint(" | revision 0x"); kprint_hex32(g_board_rev);
    if (g_board.mem_mb) { kprint(" | "); kprint_dec(g_board.mem_mb >= 1024 ? g_board.mem_mb / 1024 : g_board.mem_mb); kprint(g_board.mem_mb >= 1024 ? " GB" : " MB"); }
    kprint(" | placa v1."); kprint_dec(g_board.rev);
    static const char *mk[] = { "Sony UK", "Egoman", "Embest", "Sony Japan", "Embest", "Stadium" };
    if (g_board.maker < 6) { kprint(" | fabricante "); kprint(mk[g_board.maker]); }
    kprint(g_board.has_wifi ? " | con Wi-Fi\n" : " | sin Wi-Fi\n");
}

static int g_state;
int wifi_state(void) { return g_state; }

#if defined(SOMA_SDIO2_BASE) && SOMA_SDIO2_BASE
/* ------------------------------------------------------------------ SDHCI de SDIO2 */
#define REG(o) ((volatile uint32_t *)(uintptr_t)(SOMA_SDIO2_BASE + (o)))
static inline uint32_t rd(uint32_t o) { return *REG(o); }
static inline void wr(uint32_t o, uint32_t v) { *REG(o) = v; }
static void wr8(uint32_t o, uint8_t v) { uint32_t sh = (o & 3) * 8, w = rd(o & ~3u); w = (w & ~(0xFFu << sh)) | ((uint32_t)v << sh); wr(o & ~3u, w); }
static void wr16(uint32_t o, uint16_t v) { uint32_t sh = (o & 2) * 8, w = rd(o & ~3u); w = (w & ~(0xFFFFu << sh)) | ((uint32_t)v << sh); wr(o & ~3u, w); }
static uint16_t rd16(uint32_t o) { return (uint16_t)(rd(o & ~3u) >> ((o & 2) * 8)); }
static uint8_t rd8(uint32_t o) { return (uint8_t)(rd(o & ~3u) >> ((o & 3) * 8)); }

#define BLK_SIZE  0x04
#define ARG       0x08
#define XFER_CMD  0x0C
#define RESP      0x10
#define BUF       0x20
#define PRESENT   0x24
#define HOSTCTL   0x28
#define PWRCTL    0x29
#define CLKCTL    0x2C
#define TIMEOUTC  0x2E
#define RESETREG  0x2F
#define INTSTAT   0x30
#define INTEN     0x34
#define SIGEN     0x38
#define CAPS      0x40
#define VERSION   0xFC

enum { W_OK = 0, W_NOHOST = -1, W_RESET = -2, W_CLOCK = -3, W_NOCARD = -4, W_CMD = -5, W_TIMEOUT = -6, W_R5 = -7, W_DATA = -8,
       W_CHIP = -9, W_EROM = -10, W_NOCORE = -11, W_NOSD = -12, W_NOFILE = -13, W_VERIFY = -14, W_HT = -15, W_F2 = -16, W_NOSHARED = -17 };
static const char *werr(int e)
{
    switch (e) {
    case W_OK: return "bien";
    case W_NOHOST: return "el controlador SDIO2 no responde";
    case W_RESET: return "SDIO2 no sale del reinicio";
    case W_CLOCK: return "el reloj de SDIO2 no se estabiliza";
    case W_NOCARD: return "el chip Wi-Fi no contesta a CMD5 (alimentacion WL_ON o pines SD2)";
    case W_CMD: return "orden SDIO rechazada";
    case W_TIMEOUT: return "tiempo agotado";
    case W_R5: return "el chip devolvio error en CMD52/53";
    case W_DATA: return "error en la transferencia de datos";
    case W_CHIP: return "el chip no da su identificador (reloj ALP)";
    case W_EROM: return "tabla de nucleos (EROM) ilegible";
    case W_NOCORE: return "falta un nucleo necesario (ARM CR4 / SDIO)";
    case W_NOSD: return "no hay tarjeta SD con FAT para leer /wifi";
    case W_NOFILE: return "faltan los ficheros de /wifi en la tarjeta (genera la SD con 'pi5.py sd')";
    case W_VERIFY: return "la RAM del chip no devuelve lo escrito";
    case W_HT: return "el chip no da el reloj HT tras arrancar el firmware";
    case W_F2: return "la funcion 2 no queda lista (el firmware no arranco)";
    case W_NOSHARED: return "el firmware no publico su estructura compartida";
    default: return "error";
    }
}

static void ms(uint32_t n) { timer_sleep_ms(n); }
static void spin_us(uint32_t us) { for (volatile uint32_t i = 0; i < us * 50u; i++) { } }

static int wait_clear(uint32_t off, uint32_t mask, uint32_t lim_ms)
{
    for (uint32_t i = 0; i < lim_ms * 100; i++) { if (!(rd(off) & mask)) return 0; if ((i % 100) == 99) ms(1); }
    return -1;
}

static int host_reset(void)
{
    if (rd(CAPS) == 0xFFFFFFFFu) return W_NOHOST;
    wr8(RESETREG, 0x01);
    for (int i = 0; i < 100; i++) { if (!(rd(CLKCTL) >> 24 & 1)) return 0; ms(1); }
    return W_RESET;
}

static uint32_t g_hz;
static int set_clock(uint32_t hz)
{
    uint32_t caps = rd(CAPS), base = ((caps >> 8) & 0xFF) * 1000000u;
    if (base == 0) base = 200000000u;
    wr16(CLKCTL, 0);
    uint32_t n = sd_clk_div(base, hz);
    wr16(CLKCTL, (uint16_t)sd_clock_reg(n));
    for (int i = 0; i < 100; i++) {
        if (rd16(CLKCTL) & 2) { wr16(CLKCTL, (uint16_t)(sd_clock_reg(n) | 4)); ms(2); g_hz = n ? base / (2 * n) : base; return 0; }
        ms(1);
    }
    return W_CLOCK;
}

static uint32_t g_last_int;
static int cmd(uint8_t idx, uint32_t arg, int resp, uint32_t *r, uint32_t mode, uint32_t blk, int data)
{
    if (wait_clear(PRESENT, data ? 3u : 1u, 100)) return W_TIMEOUT;
    wr(INTSTAT, 0xFFFFFFFFu);
    if (data) wr(BLK_SIZE, (1u << 16) | blk);
    wr(ARG, arg);
    wr(XFER_CMD, (sd_cmd_word(idx, resp, data) << 16) | (mode & 0xFFFFu));
    uint32_t st = 0;
    for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & ((1u << 0) | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
    g_last_int = st;
    if (!(st & 1u) || (st & (1u << 15))) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x02 | 0x04); spin_us(100); return (st & (1u << 16)) || !st ? W_TIMEOUT : W_CMD; }
    wr(INTSTAT, 1u);
    if (r) for (int i = 0; i < 4; i++) r[i] = rd(RESP + 4u * (uint32_t)i);
    if (resp == SD_R1B) { for (uint32_t i = 0; i < 500; i++) { if (rd(PRESENT) & (1u << 20)) break; ms(1); } }
    return 0;
}

/* ------------------------------------------------------------------ CMD52 / CMD53 */
static int c52(int w, unsigned fn, uint32_t addr, uint8_t val, uint8_t *out)
{
    uint32_t r[4];
    int e = cmd(52, sdio_cmd52_arg(w, fn, addr, val, w), SD_R1, r, 0, 0, 0);
    if (e) return e;
    if (!sdio_r5_ok(r[0])) return W_R5;
    if (out) *out = (uint8_t)r[0];
    return 0;
}
static int rb(unsigned fn, uint32_t a, uint8_t *v) { return c52(0, fn, a, 0, v); }
static int wb(unsigned fn, uint32_t a, uint8_t v) { return c52(1, fn, a, v, 0); }

/* CMD53 en modo byte, n <= 64, n multiplo de 4 */
static int c53(int w, unsigned fn, uint32_t addr, uint8_t *buf, uint32_t n)
{
    uint32_t arg = sdio_cmd53_arg(w, fn, 0, 1, addr, n), r[4];
    if (!arg) return W_CMD;
    int e = cmd(53, arg, SD_R1, r, w ? 0u : (1u << 4), (7u << 12) | n, 1);
    if (e) return e;
    if (!sdio_r5_ok(r[0])) return W_R5;
    uint32_t flag = w ? (1u << 4) : (1u << 5), st = 0;
    for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & (flag | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
    if (!(st & flag)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); spin_us(100); return W_DATA; }
    wr(INTSTAT, flag);
    for (uint32_t i = 0; i < n; i += 4) {
        if (w) wr(BUF, (uint32_t)buf[i] | (uint32_t)buf[i + 1] << 8 | (uint32_t)buf[i + 2] << 16 | (uint32_t)buf[i + 3] << 24);
        else { uint32_t v = rd(BUF); buf[i] = (uint8_t)v; buf[i + 1] = (uint8_t)(v >> 8); buf[i + 2] = (uint8_t)(v >> 16); buf[i + 3] = (uint8_t)(v >> 24); }
    }
    for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & ((1u << 1) | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
    if (!(st & 2u)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); spin_us(100); return W_DATA; }
    wr(INTSTAT, 2u);
    return 0;
}

/* ------------------------------------------------------------------ bus interno del chip (backplane) */
static uint32_t g_win = 0xFFFFFFFFu;
static int set_win(uint32_t addr)
{
    uint32_t base = addr & ~(uint32_t)SB_WIN_MASK;
    if (base == g_win) return 0;
    uint8_t w[3]; sb_window(base, w);
    int e = wb(1, SB_F1_SBADDRLOW, w[0]);
    if (!e) e = wb(1, SB_F1_SBADDRMID, w[1]);
    if (!e) e = wb(1, SB_F1_SBADDRHIGH, w[2]);
    g_win = e ? 0xFFFFFFFFu : base;
    return e;
}
static int bp_rd(uint32_t addr, uint32_t *v)
{
    int e = set_win(addr); if (e) return e;
    uint8_t b[4];
    e = c53(0, 1, (addr & SB_WIN_MASK) | SB_ACCESS_4B, b, 4);
    if (!e) *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return e;
}
static int bp_wr(uint32_t addr, uint32_t v)
{
    int e = set_win(addr); if (e) return e;
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return c53(1, 1, (addr & SB_WIN_MASK) | SB_ACCESS_4B, b, 4);
}
/* Bloques de memoria del chip: trozos de 64 bytes que no cruzan la ventana. addr y n multiplos de 4. */
static int bp_mem(int w, uint32_t addr, uint8_t *buf, uint32_t n)
{
    while (n) {
        uint32_t room = (SB_WIN_MASK + 1u) - (addr & SB_WIN_MASK), k = n < 64 ? n : 64;
        if (k > room) k = room;
        int e = set_win(addr); if (e) return e;
        e = c53(w, 1, (addr & SB_WIN_MASK) | SB_ACCESS_4B, buf, k);
        if (e) { e = c53(w, 1, (addr & SB_WIN_MASK) | SB_ACCESS_4B, buf, k); if (e) return e; }   /* un reintento */
        addr += k; buf += k; n -= k;
    }
    return 0;
}

/* ------------------------------------------------------------------ pines y alimentacion */
#define GIO(o) ((volatile uint32_t *)(uintptr_t)(SOMA_GIO_BASE + (o)))
#define PCTL(o) ((volatile uint32_t *)(uintptr_t)(SOMA_PINCTRL_BASE + (o)))
static void wl_on(int on)
{
    *GIO(0x08) &= ~(1u << 28);                                 /* IODIR: 0 = salida */
    if (on) *GIO(0x04) |= 1u << 28; else *GIO(0x04) &= ~(1u << 28);
}
/* Funcion del pin (disposicion de la BCM2712 C1: 8 pines por registro, 4 bits cada uno) */
static uint32_t mux_get(unsigned pin) { return (*PCTL(4u * (pin / 8)) >> ((pin % 8) * 4)) & 0xFu; }
static void mux_set(unsigned pin, uint32_t f) { volatile uint32_t *r = PCTL(4u * (pin / 8)); uint32_t sh = (pin % 8) * 4; *r = (*r & ~(0xFu << sh)) | ((f & 0xFu) << sh); }
static void pull_up(unsigned pin) { unsigned i = pin + 7; volatile uint32_t *r = PCTL(4u * (7 + i / 15)); uint32_t sh = (i % 15) * 2; *r = (*r & ~(3u << sh)) | (2u << sh); }
static void mux_print(void)
{
    kprint("  pines: WL_ON(28)="); kprint_dec(mux_get(28)); kprint(" SD2 30-35=");
    for (unsigned p = 30; p <= 35; p++) { kprint_dec(mux_get(p)); kprint(p < 35 ? "," : ""); }
    kprint(" | GPIO dir 0x"); kprint_hex32(*GIO(0x08)); kprint(" datos 0x"); kprint_hex32(*GIO(0x04)); kprint("\n");
}

/* ------------------------------------------------------------------ enumeracion SDIO */
static uint16_t g_rca; static uint32_t g_ocr; static uint8_t g_cccr_rev, g_nfunc; static uint16_t g_manf, g_card;
static struct brcm_chipid g_chip; static uint32_t g_chipid;
static struct brcm_core g_cores[24]; static int g_ncores;
static uint32_t g_rambase, g_ramsize;

static int card_init(int verbose)
{
    int e = host_reset(); if (e) return e;
    wr8(PWRCTL, 0x0F);
    wr8(TIMEOUTC, 0x0E);
    wr8(HOSTCTL, (uint8_t)(rd8(HOSTCTL) | 0xC0u));             /* deteccion de tarjeta forzada: el chip va soldado */
    wr(INTEN, 0xFFFFFFFFu); wr(SIGEN, 0);
    if ((e = set_clock(400000))) return e;
    ms(10);
    uint32_t r[4] = { 0 };
    cmd(0, 0, SD_R_NONE, 0, 0, 0, 0);
    ms(2);
    e = cmd(5, 0, SD_R3, r, 0, 0, 0);
    if (e) { if (verbose) { kprint("  CMD5 sin respuesta (estado 0x"); kprint_hex32(g_last_int); kprint(")\n"); } return W_NOCARD; }
    g_ocr = SDIO_R4_OCR(r[0]); g_nfunc = (uint8_t)SDIO_R4_NFUNC(r[0]);
    int ok = 0;
    for (int i = 0; i < 100; i++) {
        if (cmd(5, g_ocr & 0x00300000u ? 0x00300000u : (g_ocr & 0x00FF8000u), SD_R3, r, 0, 0, 0)) break;
        if (SDIO_R4_READY(r[0])) { ok = 1; break; }
        ms(10);
    }
    if (!ok) return W_TIMEOUT;
    g_nfunc = (uint8_t)SDIO_R4_NFUNC(r[0]);
    if ((e = cmd(3, 0, SD_R6, r, 0, 0, 0))) return e;
    g_rca = (uint16_t)(r[0] >> 16);
    if ((e = cmd(7, (uint32_t)g_rca << 16, SD_R1B, 0, 0, 0, 0))) return e;
    if ((e = set_clock(25000000))) return e;
    g_win = 0xFFFFFFFFu;
    return 0;
}

static int read_cis(uint32_t ptr, uint8_t *buf, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { int e = rb(0, ptr + i, &buf[i]); if (e) return e; if (buf[i] == 0xFF && i && buf[i - 1] == 0) break; }
    return 0;
}

static int g_mux_done;
/* Alimenta el chip y lo enumera. Si no contesta y los pines SD2 no tienen funcion asignada, prueba las funciones 1..8. */
static int power_and_enum(int verbose)
{
    wl_on(0); ms(20); wl_on(1); ms(150);
    int e = card_init(verbose);
    if (e != W_NOCARD || g_mux_done) return e;
    uint32_t orig[6]; int all_gpio = 1;
    for (unsigned p = 30; p <= 35; p++) { orig[p - 30] = mux_get(p); if (orig[p - 30]) all_gpio = 0; }
    if (!all_gpio) return e;
    g_mux_done = 1;
    kprint("  los pines 30-35 estan como GPIO: se prueba asignarles la funcion SD2\n");
    for (uint32_t f = 1; f <= 8; f++) {
        for (unsigned p = 30; p <= 35; p++) { mux_set(p, f); if (p > 30) pull_up(p); }
        wl_on(0); ms(20); wl_on(1); ms(150);
        if (card_init(0) == 0) { kprint("  funcion "); kprint_dec(f); kprint(" de los pines 30-35: el chip contesta\n"); return 0; }
    }
    for (unsigned p = 30; p <= 35; p++) mux_set(p, orig[p - 30]);
    kprint("  ninguna funcion de los pines 30-35 da respuesta: se dejan como estaban\n");
    return W_NOCARD;
}

static int probe(int verbose)
{
    g_state = 0; g_ncores = 0;
    if (verbose) { kprint("wifi: SDIO2 version 0x"); kprint_hex16(rd16(VERSION)); kprint(" caps 0x"); kprint_hex32(rd(CAPS)); kprint("\n"); mux_print(); }
    int e = power_and_enum(verbose);
    if (e) return e;
    uint8_t v = 0;
    rb(0, 0x00, &g_cccr_rev);
    if (verbose) { kprint("  chip SDIO enumerado: RCA 0x"); kprint_hex16(g_rca); kprint(", "); kprint_dec(g_nfunc); kprint(" funciones, OCR 0x"); kprint_hex32(g_ocr); kprint(", CCCR 0x"); kprint_hex8(g_cccr_rev); kprint(", reloj "); kprint_dec(g_hz / 1000); kprint(" kHz\n"); }
    {   /* CIS comun: fabricante */
        uint8_t p0 = 0, p1 = 0, p2 = 0; rb(0, 0x09, &p0); rb(0, 0x0A, &p1); rb(0, 0x0B, &p2);
        static uint8_t cis[128];
        uint32_t ptr = (uint32_t)p0 | (uint32_t)p1 << 8 | (uint32_t)p2 << 16;
        if (ptr && read_cis(ptr, cis, sizeof cis) == 0 && sdio_cis_manfid(cis, sizeof cis, &g_manf, &g_card) == 0 && verbose) {
            kprint("  fabricante 0x"); kprint_hex16(g_manf); kprint(g_manf == 0x02D0 ? " (Broadcom/Cypress)" : ""); kprint(", tarjeta 0x"); kprint_hex16(g_card); kprint("\n");
        }
    }
    /* funcion 1 activa, tamanos de bloque F1 = 64 y F2 = 512 */
    if ((e = wb(0, 0x02, 0x02))) return e;
    for (int i = 0; i < 100; i++) { if (rb(0, 0x03, &v) == 0 && (v & 0x02)) break; ms(1); }
    if (!(v & 0x02)) return W_TIMEOUT;
    wb(0, 0x110, 64); wb(0, 0x111, 0); wb(0, 0x210, 0x00); wb(0, 0x211, 0x02);
    /* reloj ALP */
    wb(1, SB_F1_CHIPCLKCSR, CLK_HW_REQ_OFF | CLK_ALP_REQ);
    for (int i = 0; i < 100; i++) { if (rb(1, SB_F1_CHIPCLKCSR, &v) == 0 && (v & CLK_ALP_AVAIL)) break; ms(1); }
    if (!(v & CLK_ALP_AVAIL)) return W_CHIP;
    wb(1, SB_F1_CHIPCLKCSR, CLK_HW_REQ_OFF | CLK_FORCE_ALP);
    wb(1, SB_F1_SDIOPULLUP, 0);
    if ((e = bp_rd(SI_ENUM_BASE, &g_chipid))) return e;
    brcm_chipid_decode(g_chipid, &g_chip);
    g_state = 1;
    if (verbose) { kprint("  chip BCM"); kprint_hex16(g_chip.id); kprint(" revision "); kprint_dec(g_chip.rev); kprint(", "); kprint_dec(g_chip.ncores); kprint(" nucleos, bus "); kprint(g_chip.socitype == 1 ? "AI\n" : "desconocido\n"); }
    if (g_chip.socitype != 1) return W_EROM;
    /* EROM */
    uint32_t ep = 0;
    if ((e = bp_rd(SI_ENUM_BASE + CC_EROMPTR, &ep))) return e;
    static uint32_t erom[256]; unsigned n = 0;
    for (; n < 256; n++) { if ((e = bp_rd(ep + 4 * n, &erom[n]))) return e; if (erom[n] == 0xF) { n++; break; } }
    g_ncores = brcm_erom_parse(erom, n, g_cores, 24);
    if (g_ncores < 0) { g_ncores = 0; return W_EROM; }
    if (g_ncores > 24) g_ncores = 24;
    g_state = 2;
    g_rambase = brcm_rambase(g_chip.id);
    return 0;
}

static const struct brcm_core *core(uint16_t id)
{
    for (int i = 0; i < g_ncores; i++) if (g_cores[i].id == id) return &g_cores[i];
    return 0;
}

static void cores_print(void)
{
    for (int i = 0; i < g_ncores; i++) {
        kprint("  nucleo 0x"); kprint_hex16(g_cores[i].id); kprint(" rev "); kprint_dec(g_cores[i].rev); kprint(" en 0x"); kprint_hex32(g_cores[i].base);
        kprint(" (envoltorio 0x"); kprint_hex32(g_cores[i].wrap); kprint(") "); kprint(brcm_core_name(g_cores[i].id)); kprint("\n");
    }
}

/* ------------------------------------------------------------------ reinicio de nucleos (envoltorio AI) */
static void core_disable(const struct brcm_core *c, uint32_t pre, uint32_t reset)
{
    uint32_t v = 0;
    bp_rd(c->wrap + AI_RESETCTL, &v);
    if (!(v & 1u)) {
        bp_wr(c->wrap + AI_IOCTL, pre | AI_IOCTL_FGC | AI_IOCTL_CLK); bp_rd(c->wrap + AI_IOCTL, &v);
        bp_wr(c->wrap + AI_RESETCTL, 1); spin_us(20);
        for (int i = 0; i < 300; i++) { if (bp_rd(c->wrap + AI_RESETCTL, &v) == 0 && (v & 1u)) break; spin_us(10); }
    }
    bp_wr(c->wrap + AI_IOCTL, reset | AI_IOCTL_FGC | AI_IOCTL_CLK); bp_rd(c->wrap + AI_IOCTL, &v);
}
static void core_reset(const struct brcm_core *c, uint32_t pre, uint32_t reset, uint32_t post)
{
    uint32_t v = 0;
    core_disable(c, pre, reset);
    for (int i = 0; i < 50; i++) { bp_wr(c->wrap + AI_RESETCTL, 0); spin_us(40); if (bp_rd(c->wrap + AI_RESETCTL, &v) == 0 && !(v & 1u)) break; }
    bp_wr(c->wrap + AI_IOCTL, post | AI_IOCTL_CLK); bp_rd(c->wrap + AI_IOCTL, &v);
}

static int cr4_ramsize(const struct brcm_core *cr4, uint32_t *out)
{
    uint32_t cap = 0, tot = 0;
    int e = bp_rd(cr4->base + CR4_CAP, &cap); if (e) return e;
    uint32_t nb = (cap & 0xFu) + ((cap >> 4) & 0xFu);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t info = 0;
        bp_wr(cr4->base + CR4_BANKIDX, i);
        if ((e = bp_rd(cr4->base + CR4_BANKINFO, &info))) return e;
        tot += brcm_cr4_bank_bytes(info);
    }
    *out = tot;
    return 0;
}

/* ------------------------------------------------------------------ ficheros de la tarjeta SD */
static int sd_fat_read(void *ctx, uint64_t lba, uint32_t n, void *buf) { (void)ctx; return sd_read(lba, n, buf); }
static struct fat_vol g_vol; static int g_vol_ok;
static int vol_mount(void)
{
    if (g_vol_ok) return 0;
    struct sd_info si;
    if (sd_probe(&si) != 0) return W_NOSD;
    struct fat_io io = { sd_fat_read, 0, si.sectors };
    if (fat_mount(&g_vol, &io) != 0) return W_NOSD;
    g_vol_ok = 1;
    return 0;
}
static const char *FW_BIN = "/wifi/brcmfmac43455-sdio.bin";
static const char *FW_TXT = "/wifi/brcmfmac43455-sdio.txt";

/* ------------------------------------------------------------------ consola interna del firmware */
static struct brcm_shared g_sh; static uint32_t g_sh_addr;
static void fw_log(uint32_t max)
{
    if (g_state < 3 || !g_sh.console_addr) { kprint("wifi log: el firmware no esta en marcha\n"); return; }
    uint32_t hdr[3];
    for (int i = 0; i < 3; i++) if (bp_rd(g_sh.console_addr + 8 + 4u * (uint32_t)i, &hdr[i])) { kprint("wifi log: lectura fallida\n"); return; }
    uint32_t buf = hdr[0], size = hdr[1], idx = hdr[2];
    if (!buf || size == 0 || size > 0x10000u || idx >= size) { kprint("wifi log: consola no valida\n"); return; }
    static uint8_t cb[4096];
    uint32_t n = size < sizeof cb ? size : sizeof cb;
    if (n > max) n = max;
    uint32_t start = (idx + size - n) % size;                  /* los ultimos n bytes antes de idx */
    for (uint32_t i = 0; i < n; i += 64) {
        uint32_t off = (start + i) % size, k = n - i < 64 ? n - i : 64;
        uint32_t a = (buf + off) & ~3u, skip = (buf + off) & 3u;
        uint8_t t[72];
        if (bp_mem(0, a, t, (k + skip + 3) & ~3u)) { kprint("wifi log: lectura fallida\n"); return; }
        for (uint32_t j = 0; j < k; j++) cb[i + j] = t[skip + j];
    }
    kprint("---- consola del firmware ----\n");
    char line[2] = { 0, 0 };
    for (uint32_t i = 0; i < n; i++) { char c = (char)cb[i]; if (c == '\n' || (c >= 32 && c < 127)) { line[0] = c; kprint(line); } }
    kprint("\n------------------------------\n");
}

/* ------------------------------------------------------------------ firmware */
static int fw_load(void)
{
    int e;
    if (g_state < 2 && (e = probe(0))) return e;
    const struct brcm_core *cr4 = core(CORE_ARM_CR4), *d11 = core(CORE_D11), *sdc = core(CORE_SDIO_DEV);
    if (!cr4 || !sdc || !g_rambase) return W_NOCORE;
    if ((e = vol_mount())) return e;
    struct fat_dirent fb, ft;
    if (fat_stat(&g_vol, FW_BIN, &fb) != 0 || fat_stat(&g_vol, FW_TXT, &ft) != 0 || fb.size < 1024 || ft.size == 0) return W_NOFILE;
    if ((e = cr4_ramsize(cr4, &g_ramsize))) return e;
    kprint("  RAM del chip: 0x"); kprint_hex32(g_rambase); kprint(" + "); kprint_dec(g_ramsize / 1024); kprint(" KiB | firmware "); kprint_dec(fb.size / 1024); kprint(" KiB\n");
    if (fb.size > g_ramsize) return W_NOFILE;
    /* ARM detenido y D11 en reinicio */
    uint32_t io = 0; bp_rd(cr4->wrap + AI_IOCTL, &io);
    core_reset(cr4, io & CR4_IOCTL_HALT, CR4_IOCTL_HALT, CR4_IOCTL_HALT);
    if (d11) core_reset(d11, D11_IOCTL_PHYRST | D11_IOCTL_PHYCLK, D11_IOCTL_PHYCLK, D11_IOCTL_PHYCLK);
    /* firmware */
    static uint8_t chunk[4096]; uint32_t rstvec = 0, off = 0;
    uint64_t t0 = timer_ticks;
    while (off < fb.size) {
        int got = fat_read(&g_vol, FW_BIN, off, chunk, sizeof chunk);
        if (got <= 0) return W_NOFILE;
        uint32_t k = ((uint32_t)got + 3u) & ~3u;
        for (uint32_t i = (uint32_t)got; i < k; i++) chunk[i] = 0;
        if (off == 0) rstvec = (uint32_t)chunk[0] | (uint32_t)chunk[1] << 8 | (uint32_t)chunk[2] << 16 | (uint32_t)chunk[3] << 24;
        if ((e = bp_mem(1, g_rambase + off, chunk, k))) return e;
        off += (uint32_t)got;
        if ((off & 0x1FFFFu) < sizeof chunk) { kprint("  ."); }
    }
    kprint(" "); kprint_dec((uint32_t)(timer_ticks - t0)); kprint(" ms\n");
    {   /* comprobacion: principio y final */
        static uint8_t a[64], b[64];
        if ((e = bp_mem(0, g_rambase, b, 64))) return e;
        if (fat_read(&g_vol, FW_BIN, 0, a, 64) != 64) return W_NOFILE;
        for (int i = 0; i < 64; i++) if (a[i] != b[i]) return W_VERIFY;
        uint32_t last = (fb.size - 64) & ~3u;
        if ((e = bp_mem(0, g_rambase + last, b, 64))) return e;
        if (fat_read(&g_vol, FW_BIN, last, a, 64) != 64) return W_NOFILE;
        for (int i = 0; i < 64; i++) if (a[i] != b[i]) return W_VERIFY;
    }
    /* NVRAM al final de la RAM */
    {
        static char txt[8192]; static uint8_t nv[8192];
        int got = fat_read(&g_vol, FW_TXT, 0, txt, sizeof txt);
        if (got <= 0) return W_NOFILE;
        int nl = brcm_nvram_pack(txt, (uint32_t)got, nv, sizeof nv);
        if (nl <= 0) return W_NOFILE;
        uint32_t at = g_rambase + g_ramsize - (uint32_t)nl;
        if ((e = bp_mem(1, at, nv, (uint32_t)nl))) return e;
        kprint("  NVRAM: "); kprint_dec((uint32_t)nl); kprint(" bytes en 0x"); kprint_hex32(at); kprint("\n");
    }
    /* arranque: vector en 0, interrupciones del nucleo SDIO limpias, ARM en marcha */
    if ((e = bp_wr(0, rstvec))) return e;
    bp_wr(sdc->base + 0x20, 0xFFFFFFFFu);
    core_reset(cr4, CR4_IOCTL_HALT, 0, 0);
    kprint("  ARM CR4 en marcha (vector 0x"); kprint_hex32(rstvec); kprint(")\n");
    /* reloj HT */
    uint8_t v = 0;
    wb(1, SB_F1_CHIPCLKCSR, CLK_HT_REQ);
    for (int i = 0; i < 1000; i++) { if (rb(1, SB_F1_CHIPCLKCSR, &v) == 0 && (v & CLK_HT_AVAIL)) break; ms(1); }
    if (!(v & CLK_HT_AVAIL)) return W_HT;
    /* funcion 2 (datos) */
    wb(0, 0x02, 0x06);
    for (int i = 0; i < 3000; i++) { if (rb(0, 0x03, &v) == 0 && (v & 0x04)) break; ms(1); }
    if (!(v & 0x04)) return W_F2;
    /* estructura compartida: la ultima palabra de la RAM deja de ser la marca de la NVRAM */
    uint32_t last = 0;
    for (int i = 0; i < 200; i++) { if (bp_rd(g_rambase + g_ramsize - 4, &last) == 0 && !brcm_is_nvram_token(last)) break; ms(10); }
    if (brcm_is_nvram_token(last) || last < g_rambase || last >= g_rambase + g_ramsize) return W_NOSHARED;
    g_sh_addr = last;
    static uint8_t sb[32];
    if ((e = bp_mem(0, last, sb, 32))) return e;
    if (brcm_shared_parse(sb, 32, &g_sh) != 0) return W_NOSHARED;
    g_state = 3;
    kprint("  firmware vivo: estructura compartida en 0x"); kprint_hex32(last); kprint(", version "); kprint_dec(g_sh.flags & SHARED_VERSION_MASK);
    kprint((g_sh.flags & SHARED_TRAP) ? ", FALLO (trap)\n" : (g_sh.flags & SHARED_ASSERT) ? ", aserto\n" : "\n");
    return 0;
}

static void report(const char *what, int e) { kprint("wifi "); kprint(what); kprint(": "); kprint(werr(e)); kprint("\n"); }

/* ================================================================== PARCHE 081: fases 2 y 3 (escrito sin placa, a verificar)
 * Funcion 2 = tramas SDPCM. Canal 0: ordenes BCDC y sus respuestas; canal 1: eventos; canal 2: datos (tramas Ethernet).
 * Lectura por sondeo del registro intstatus del nucleo SDIO del chip. */
#define SD_INTSTATUS    0x20u
#define SD_HOSTINTMASK  0x24u
#define SD_TOSBMAILBOX  0x40u
#define SD_TOHOSTMBDATA 0x4Cu
#define I_HMB_FRAME_IND 0x40u
#define I_HMB_HOST_INT  0x80u
#define F2_ADDR         (0x8000u)                 /* ventana en chipcommon con acceso de 32 bits, como hace Linux */
#define FIRSTREAD       64u

static uint8_t g_wmac[6];
static uint8_t g_txseq, g_credit = 4;
static uint16_t g_reqid;
static int g_up, g_link, g_joined, g_join_status = -1, g_scan_done;
static uint8_t g_bssid[6];
static uint32_t g_frames_rx, g_frames_tx, g_events, g_bad;

/* transferencia SDIO generica: modo bloque (nblk bloques de bsz) o modo byte (n <= 512) */
static int sdio_xfer(int w, unsigned fn, uint32_t addr, int incr, uint8_t *buf, uint32_t bsz, uint32_t nblk, int blockmode)
{
    uint32_t count = blockmode ? nblk : bsz;
    uint32_t arg = sdio_cmd53_arg(w, fn, blockmode, incr, addr, count), r[4];
    if (!arg) return W_CMD;
    uint32_t mode = (w ? 0u : (1u << 4)) | (blockmode && nblk > 1 ? (1u << 1) | (1u << 5) : 0u);
    if (wait_clear(PRESENT, 3u, 100)) return W_TIMEOUT;
    wr(INTSTAT, 0xFFFFFFFFu);
    wr(BLK_SIZE, ((blockmode ? nblk : 1u) << 16) | (7u << 12) | bsz);
    wr(ARG, arg);
    wr(XFER_CMD, (sd_cmd_word(53, SD_R1, 1) << 16) | mode);
    uint32_t st = 0;
    for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & ((1u << 0) | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
    if (!(st & 1u) || (st & (1u << 15))) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); spin_us(100); return W_CMD; }
    wr(INTSTAT, 1u);
    r[0] = rd(RESP);
    if (!sdio_r5_ok(r[0])) { wr8(RESETREG, 0x06); return W_R5; }
    uint32_t blocks = blockmode ? nblk : 1u, flag = w ? (1u << 4) : (1u << 5);
    for (uint32_t b = 0; b < blocks; b++) {
        for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & (flag | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
        if (!(st & flag)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); spin_us(100); return W_DATA; }
        wr(INTSTAT, flag);
        uint8_t *p = buf + b * bsz;
        for (uint32_t i = 0; i < bsz; i += 4) {
            if (w) wr(BUF, (uint32_t)p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24);
            else { uint32_t v = rd(BUF); p[i] = (uint8_t)v; p[i + 1] = (uint8_t)(v >> 8); p[i + 2] = (uint8_t)(v >> 16); p[i + 3] = (uint8_t)(v >> 24); }
        }
    }
    for (uint32_t i = 0; i < 100000; i++) { st = rd(INTSTAT); if (st & ((1u << 1) | (1u << 15))) break; if ((i % 1000) == 999) ms(1); }
    if (!(st & 2u)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); spin_us(100); return W_DATA; }
    wr(INTSTAT, 2u);
    return 0;
}

static uint8_t g_txb[2048] __attribute__((aligned(4)));
/* Envia una trama SDPCM ya montada en g_txb (len bytes, cabecera incluida) */
static int f2_send(uint32_t len, uint8_t chan)
{
    for (int i = 0; i < 500 && !sdpcm_can_send(g_credit, g_txseq); i++) { extern void wifi_service(void); wifi_service(); ms(1); }
    sdpcm_build(g_txb, (uint16_t)len, g_txseq, chan);
    uint32_t n = (len + 3u) & ~3u;
    for (uint32_t i = len; i < n; i++) g_txb[i] = 0;
    int e = set_win(SI_ENUM_BASE); if (e) return e;
    if (n <= 512) e = sdio_xfer(1, 2, F2_ADDR, 1, g_txb, n, 1, 0);
    else { uint32_t nb = (n + 511u) / 512u; for (uint32_t i = n; i < nb * 512u; i++) g_txb[i] = 0; e = sdio_xfer(1, 2, F2_ADDR, 1, g_txb, 512, nb, 1); }
    if (!e) { g_txseq++; g_frames_tx++; }
    return e;
}

/* ------------------------------------------------ recepcion */
static uint8_t g_ctrl[2048]; static uint32_t g_ctrl_len; static int g_ctrl_ready;
#define RXQ 8
static uint8_t g_rxq[RXQ][1600]; static uint16_t g_rxl[RXQ]; static int g_rxh, g_rxt;
static struct wifi_bss g_bss[24]; static int g_nbss;
static struct wpa_sm g_sm; static int g_sm_on;
static uint8_t g_eapol_out[512];

static int send_eth(const uint8_t *eth, uint32_t n)
{
    if (n + SDPCM_HDR + BCDC_DATA_HDR > sizeof g_txb) return -1;
    bcdc_data_build(g_txb + SDPCM_HDR);
    for (uint32_t i = 0; i < n; i++) g_txb[SDPCM_HDR + BCDC_DATA_HDR + i] = eth[i];
    return f2_send(SDPCM_HDR + BCDC_DATA_HDR + n, SDPCM_CH_DATA);
}

static void handle_eapol(const uint8_t *eth, uint32_t n)
{
    if (!g_sm_on || n <= 14) return;
    size_t ol = 0;
    int r = wpa_sm_rx(&g_sm, eth + 14, n - 14, g_eapol_out + 14, &ol);
    kprint("  EAPOL: "); kprint(wpa_err_name(r)); kprint("\n");
    if (r == WPA_R_SEND || r == WPA_R_DONE) {
        for (int i = 0; i < 6; i++) { g_eapol_out[i] = eth[6 + i]; g_eapol_out[6 + i] = g_wmac[i]; }
        g_eapol_out[12] = 0x88; g_eapol_out[13] = 0x8E;
        send_eth(g_eapol_out, (uint32_t)(14 + ol));
    }
}

static void handle_event(const uint8_t *eth, uint32_t n)
{
    struct brcm_event ev;
    if (brcm_event_parse(eth, n, &ev) != 0) { g_bad++; return; }
    g_events++;
    switch (ev.type) {
    case BRCM_E_ESCAN_RESULT:
        if (ev.status == BRCM_ESCAN_PARTIAL) { if (wifi_escan_parse(ev.data, ev.datalen, g_bss, &g_nbss, 24) < 0) g_bad++; }
        else g_scan_done = 1;
        break;
    case BRCM_E_SET_SSID:
        g_join_status = (int)ev.status;
        if (ev.status == 0) { g_joined = 1; for (int i = 0; i < 6; i++) g_bssid[i] = ev.addr[i]; }
        break;
    case BRCM_E_LINK:
        g_link = ev.flags & 1;
        if (g_link) for (int i = 0; i < 6; i++) g_bssid[i] = ev.addr[i];
        kprint(g_link ? "  wifi: enlace arriba\n" : "  wifi: enlace abajo\n");
        break;
    case BRCM_E_DEAUTH: case BRCM_E_DEAUTH_IND: case BRCM_E_DISASSOC: case BRCM_E_DISASSOC_IND:
        g_link = 0; g_joined = 0;
        kprint("  wifi: desconectado por el punto de acceso (motivo "); kprint_dec(ev.reason); kprint(")\n");
        break;
    default: break;
    }
}

static uint8_t g_rxb[2048 + 512] __attribute__((aligned(4)));
static void dispatch(uint32_t len)
{
    struct sdpcm s;
    if (sdpcm_parse(g_rxb, len, &s) != 0 || s.len == 0) { g_bad++; return; }
    g_credit = s.credit;
    g_frames_rx++;
    const uint8_t *p = g_rxb + s.doff; uint32_t n = s.len - s.doff;
    if (s.chan == SDPCM_CH_CTRL) {
        if (n > sizeof g_ctrl) n = sizeof g_ctrl;
        for (uint32_t i = 0; i < n; i++) g_ctrl[i] = p[i];
        g_ctrl_len = n; g_ctrl_ready = 1;
        return;
    }
    int k = bcdc_data_skip(p, n);
    if (k < 0) { g_bad++; return; }
    p += k; n -= (uint32_t)k;
    if (s.chan == SDPCM_CH_EVENT) { handle_event(p, n); return; }
    if (s.chan == SDPCM_CH_DATA && n >= 14) {
        if (p[12] == 0x88 && p[13] == 0x8E) { handle_eapol(p, n); return; }
        if (p[12] == 0x88 && p[13] == 0x6C) { handle_event(p, n); return; }
        int nx = (g_rxh + 1) % RXQ;
        if (nx == g_rxt || n > 1600) return;                       /* cola llena: se pierde */
        for (uint32_t i = 0; i < n; i++) g_rxq[g_rxh][i] = p[i];
        g_rxl[g_rxh] = (uint16_t)n; g_rxh = nx;
    }
}

static int f2_read_one(void)
{
    int e = set_win(SI_ENUM_BASE); if (e) return -1;
    if (sdio_xfer(0, 2, F2_ADDR, 0, g_rxb, FIRSTREAD, 1, 0)) return -1;
    struct sdpcm s;
    if (sdpcm_parse(g_rxb, FIRSTREAD, &s) != 0) { g_bad++; return -1; }
    if (s.len == 0) return 0;
    if (s.len > 2048) { g_bad++; return -1; }
    uint32_t got = FIRSTREAD;
    while (got < s.len) {
        uint32_t k = ((s.len - got) + 3u) & ~3u; if (k > 512) k = 512;
        if (sdio_xfer(0, 2, F2_ADDR, 0, g_rxb + got, k, 1, 0)) return -1;
        got += k;
    }
    dispatch(s.len);
    return 1;
}

static int g_in_service;
void wifi_service(void)
{
    if (!g_up || g_in_service) return;
    g_in_service = 1;
    net_lock();
    uint32_t st = 0;
    if (bp_rd(core(CORE_SDIO_DEV)->base + SD_INTSTATUS, &st) == 0 && st) {
        bp_wr(core(CORE_SDIO_DEV)->base + SD_INTSTATUS, st);
        if (st & I_HMB_HOST_INT) { uint32_t mb = 0; bp_rd(core(CORE_SDIO_DEV)->base + SD_TOHOSTMBDATA, &mb); bp_wr(core(CORE_SDIO_DEV)->base + SD_TOSBMAILBOX, 2); }
        for (int i = 0; i < 16; i++) if (f2_read_one() <= 0) break;
    }
    net_unlock();
    g_in_service = 0;
}

/* ------------------------------------------------ ordenes al firmware */
static int fw_ioctl(uint32_t cmd, int set, const void *in, uint32_t inlen, void *out, uint32_t outcap)
{
    if (!g_up) return -100;
    uint32_t plen = inlen > outcap ? inlen : outcap;
    if (SDPCM_HDR + BCDC_HDR + plen > sizeof g_txb) return -101;
    net_lock();
    uint16_t id = ++g_reqid;
    uint8_t *b = g_txb + SDPCM_HDR;
    bcdc_build(b, cmd, set, id, 0, plen);
    const uint8_t *s = (const uint8_t *)in;
    for (uint32_t i = 0; i < inlen; i++) b[BCDC_HDR + i] = s[i];
    g_ctrl_ready = 0;
    int e = f2_send(SDPCM_HDR + BCDC_HDR + plen, SDPCM_CH_CTRL);
    net_unlock();
    if (e) return -102;
    for (int t = 0; t < 2000; t++) {
        wifi_service();
        if (g_ctrl_ready) {
            struct bcdc c;
            g_ctrl_ready = 0;
            if (bcdc_parse(g_ctrl, g_ctrl_len, &c) != 0 || c.id != id) continue;
            if (out) { uint8_t *o = (uint8_t *)out; uint32_t n = g_ctrl_len - BCDC_HDR; if (n > outcap) n = outcap; for (uint32_t i = 0; i < n; i++) o[i] = g_ctrl[BCDC_HDR + i]; }
            return (c.flags & BCDC_F_ERROR) ? -(int)(c.status ? c.status : 1) : 0;
        }
        ms(1);
    }
    return -103;                                                     /* sin respuesta */
}
static int ioctl_u32(uint32_t cmd, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; return fw_ioctl(cmd, 1, b, 4, 0, 0); }
static uint8_t g_iob[1600];
static int iov_set(const char *name, const void *val, uint32_t vlen)
{
    size_t n = brcm_iovar(g_iob, sizeof g_iob, name, val, vlen);
    return n ? fw_ioctl(WLC_SET_VAR, 1, g_iob, (uint32_t)n, 0, 0) : -104;
}
static int iov_set_u32(const char *name, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; return iov_set(name, b, 4); }
static int iov_get(const char *name, void *out, uint32_t olen)
{
    size_t n = brcm_iovar(g_iob, sizeof g_iob, name, 0, 0);
    return n ? fw_ioctl(WLC_GET_VAR, 0, g_iob, (uint32_t)n, out, olen) : -104;
}
static void ioerr(const char *what, int e) { kprint("  "); kprint(what); kprint(": error "); kprint_dec((uint32_t)(-e)); kprint(e == -103 ? " (el firmware no contesta)\n" : "\n"); }

/* CLM: tabla de regulacion (paises, canales, potencias). Sin ella el firmware no deja emitir. */
static int clm_load(void)
{
    static const char *CLM = "/wifi/brcmfmac43455-sdio.clm_blob";
    struct fat_dirent st;
    if (vol_mount() || fat_stat(&g_vol, CLM, &st) || st.size == 0) { kprint("  CLM: no esta en /wifi (sigue sin el)\n"); return -1; }
    static uint8_t chunk[1400], msg[1412];
    for (uint32_t off = 0; off < st.size; ) {
        int got = fat_read(&g_vol, CLM, off, chunk, sizeof chunk);
        if (got <= 0) return -1;
        size_t n = brcm_clm_chunk(msg, chunk, (uint16_t)got, off == 0, off + (uint32_t)got >= st.size);
        int e = iov_set("clmload", msg, (uint32_t)n);
        if (e) { ioerr("CLM", e); return e; }
        off += (uint32_t)got;
    }
    uint32_t status = 0xFFFFFFFFu;
    iov_get("clmload_status", &status, 4);
    kprint("  CLM cargado ("); kprint_dec(st.size); kprint(" bytes), estado "); kprint_dec(status); kprint(status ? " (fallo)\n" : " (bien)\n");
    return status ? -1 : 0;
}

static int wifi_up(void)
{
    if (g_up) return 0;
    int e;
    if (g_state < 3 && (e = fw_load())) return e;
    const struct brcm_core *sdc = core(CORE_SDIO_DEV);
    bp_wr(sdc->base + SD_HOSTINTMASK, 0x000000F0u);
    wb(1, 0x10008, 8);                                               /* marca de agua de la FIFO de lectura */
    g_up = 1; g_txseq = 0; g_credit = 4;
    for (int i = 0; i < 50; i++) { wifi_service(); ms(2); }          /* tramas iniciales (eventos de arranque) */
    uint8_t mac[6] = { 0 };
    e = iov_get("cur_etheraddr", mac, 6);
    if (e) { ioerr("cur_etheraddr", e); g_up = 0; return W_NOSHARED; }
    for (int i = 0; i < 6; i++) g_wmac[i] = mac[i];
    kprint("  MAC Wi-Fi "); kprint_mac(g_wmac); kprint("\n");
    { static char ver[256]; for (int i = 0; i < 256; i++) ver[i] = 0;
      if (iov_get("ver", ver, 255) == 0) { for (int i = 0; ver[i]; i++) if (ver[i] == '\n') ver[i] = ' '; kprint("  "); kprint(ver); kprint("\n"); } }
    clm_load();
    iov_set_u32("bus:txglom", 0);
    { uint32_t ev[] = { BRCM_E_SET_SSID, BRCM_E_JOIN, BRCM_E_AUTH, BRCM_E_DEAUTH, BRCM_E_DEAUTH_IND, BRCM_E_ASSOC, BRCM_E_DISASSOC,
                        BRCM_E_DISASSOC_IND, BRCM_E_LINK, BRCM_E_PSK_SUP, BRCM_E_ESCAN_RESULT };
      uint8_t m[16]; brcm_event_mask(m, ev, (int)(sizeof ev / sizeof ev[0]));
      if ((e = iov_set("event_msgs", m, 16))) ioerr("event_msgs", e); }
    if ((e = fw_ioctl(WLC_UP, 1, 0, 0, 0, 0))) { ioerr("WLC_UP", e); return W_NOSHARED; }
    ioctl_u32(WLC_SET_PM, 0);                                        /* sin ahorro de energia: mas fiable */
    g_state = 4;
    kprint("  Wi-Fi listo\n");
    return 0;
}

static int wifi_scan(int quiet)
{
    int e = wifi_up(); if (e) return e;
    static uint16_t sync = 0x100;
    uint8_t p[ESCAN_PARAMS_LEN];
    brcm_escan_params(p, ++sync, 0, 0);
    g_nbss = 0; g_scan_done = 0;
    if ((e = iov_set("escan", p, sizeof p))) { ioerr("escan", e); return W_CMD; }
    for (int t = 0; t < 8000 && !g_scan_done; t += 5) { wifi_service(); ms(5); }
    wifi_bss_sort(g_bss, g_nbss);
    if (quiet) return 0;
    kprint("  "); kprint_dec((uint32_t)g_nbss); kprint(g_scan_done ? " redes:\n" : " redes (el escaneo no termino a tiempo):\n");
    for (int i = 0; i < g_nbss; i++) {
        const struct wifi_bss *b = &g_bss[i];
        kprint("  "); kprint(b->rssi > -60 ? "####" : b->rssi > -70 ? "### " : b->rssi > -80 ? "##  " : "#   ");
        kprint(" "); kprint(b->rssi < 0 ? "-" : ""); kprint_dec((uint32_t)(b->rssi < 0 ? -b->rssi : b->rssi)); kprint(" dBm  canal ");
        kprint_dec(b->channel); kprint(b->band5 ? " (5 GHz)  " : " (2,4 GHz)  "); kprint(wpa_sec_name(b->sec)); kprint("  ");
        kprint(b->ssid_len ? b->ssid : "(oculta)"); kprint("\n");
    }
    return 0;
}

static void random_bytes(uint8_t *o, int n)
{
    struct sha1_ctx c; uint8_t h[20];
    for (int k = 0; k < n; k += 20) {
        uint64_t t[3] = { arch_cycle_counter(), timer_ticks, (uint64_t)k };
        sha1_init(&c); sha1_update(&c, t, sizeof t); sha1_update(&c, g_wmac, 6); sha1_update(&c, o, (size_t)n); sha1_final(&c, h);
        for (int i = 0; i < 20 && k + i < n; i++) o[k + i] = h[i];
    }
    for (int k = 0; k < n; k++) o[k] ^= (uint8_t)rng_u32();          /* SOMA_192: antes solo contador de ciclos, ticks y MAC (adivinable) */
}

static int wifi_connect(const char *ssid, const char *pass)
{
    int e = wifi_up(); if (e) return e;
    size_t sl = 0; while (ssid[sl]) sl++;
    if (sl == 0 || sl > 32) { kprint("  nombre de red no valido\n"); return W_CMD; }
    /* seguridad de la red segun el escaneo */
    wifi_scan(1);
    const struct wifi_bss *t = 0;
    for (int i = 0; i < g_nbss; i++) if (g_bss[i].ssid_len == sl) { int d = 0; for (size_t k = 0; k < sl; k++) if (g_bss[i].ssid[k] != ssid[k]) d = 1; if (!d) { t = &g_bss[i]; break; } }
    int sec = t ? t->sec : (pass && *pass ? SEC_WPA2_PSK : SEC_OPEN);
    if (!t) kprint("  (la red no aparece en el escaneo: se intenta igualmente)\n");
    else { kprint("  red "); kprint(t->ssid); kprint(": "); kprint(wpa_sec_name(sec)); kprint(", canal "); kprint_dec(t->channel); kprint("\n"); }
    if (sec == SEC_WPA3_SAE || sec == SEC_ENTERPRISE || sec == SEC_WEP || sec == SEC_WPA) { kprint("  esa seguridad aun no esta soportada (solo abierta y WPA2-PSK)\n"); return W_CMD; }
    int psk = sec != SEC_OPEN;
    uint8_t pmk[32];
    if (psk && wpa_psk_to_pmk(pass ? pass : "", (const uint8_t *)ssid, sl, pmk)) { kprint("  contrasena no valida (8 a 63 caracteres)\n"); return W_CMD; }
    /* configuracion: infraestructura, autenticacion abierta, cifrado */
    fw_ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
    ioctl_u32(WLC_SET_INFRA, 1);
    ioctl_u32(WLC_SET_AUTH, 0);
    iov_set_u32("wsec", psk ? 4u : 0u);                              /* AES */
    ioctl_u32(WLC_SET_WPA_AUTH, psk ? 0x80u : 0u);                    /* WPA2_AUTH_PSK */
    uint8_t ie[32]; size_t il = wpa_build_rsn_ie(t && t->group ? t->group : 4, ie);
    if (psk) { e = iov_set("wpaie", ie, (uint32_t)il); if (e) ioerr("wpaie", e); }
    g_link = 0; g_joined = 0; g_join_status = -1; g_sm_on = 0;
    if (psk) {
        uint8_t sn[32]; random_bytes(sn, 32);
        static const uint8_t any[6] = { 0 };
        wpa_sm_init(&g_sm, pmk, any, g_wmac, sn, ie, il);           /* la direccion del AP se fija al asociarse */
        g_sm_on = 1;
    }
    uint8_t sb[36]; brcm_ssid(sb, (const uint8_t *)ssid, sl);
    if ((e = fw_ioctl(WLC_SET_SSID, 1, sb, 36, 0, 0))) { ioerr("SET_SSID", e); return W_CMD; }
    kprint("  asociando...\n");
    for (int i = 0; i < 10000 && g_join_status < 0; i += 5) {
        wifi_service();
        if (g_sm_on && g_sm.state == WPA_IDLE && (g_link || g_joined)) for (int k = 0; k < 6; k++) g_sm.aa[k] = g_bssid[k];
        ms(5);
    }
    if (g_join_status != 0) { kprint("  no se pudo asociar (estado "); kprint_dec((uint32_t)g_join_status); kprint(")\n"); g_sm_on = 0; return W_CMD; }
    for (int k = 0; k < 6; k++) g_sm.aa[k] = g_bssid[k];
    kprint("  asociado a "); kprint_mac(g_bssid); kprint("\n");
    if (psk) {
        for (int i = 0; i < 10000 && g_sm.state != WPA_DONE; i += 5) { wifi_service(); ms(5); }
        if (g_sm.state != WPA_DONE) { kprint("  el saludo WPA2 no termino (contrasena equivocada o tramas perdidas)\n"); g_sm_on = 0; return W_CMD; }
        uint8_t kb[WSEC_KEY_LEN];
        brcm_wsec_key(kb, 0, g_sm.ptk + 32, 16, CRYPTO_ALGO_AES_CCM, 1, g_bssid);
        if ((e = fw_ioctl(WLC_SET_KEY, 1, kb, WSEC_KEY_LEN, 0, 0))) ioerr("clave par", e);
        brcm_wsec_key(kb, g_sm.gtk_idx, g_sm.gtk, g_sm.gtk_len, CRYPTO_ALGO_AES_CCM, 0, 0);
        if ((e = fw_ioctl(WLC_SET_KEY, 1, kb, WSEC_KEY_LEN, 0, 0))) ioerr("clave de grupo", e);
        kprint("  claves WPA2 instaladas\n");
    }
    g_link = 1;
    return 0;
}

/* ------------------------------------------------ wlan0 */
static int wl_if_init(void)
{
    if (!g_up || !g_link) { kprint("wlan0: sin conectar (usa 'wifi conectar <red> <clave>')\n"); return 0; }
    return 1;
}
static int wl_if_send(const void *f, uint16_t n) { if (!g_link) return -1; net_lock(); int r = send_eth((const uint8_t *)f, n); net_unlock(); return r ? -1 : 0; }
static int wl_if_poll(void *o, uint16_t m, uint16_t *l)
{
    wifi_service();
    if (g_rxt == g_rxh) return 0;
    uint16_t n = g_rxl[g_rxt]; if (n > m) n = m;
    uint8_t *d = (uint8_t *)o; for (uint16_t i = 0; i < n; i++) d[i] = g_rxq[g_rxt][i];
    *l = n; g_rxt = (g_rxt + 1) % RXQ;
    return 1;
}
static const uint8_t *wl_if_mac(void) { return g_wmac; }
static int wl_if_link(void) { return g_link; }
static void wl_if_stats(uint32_t *tx, uint32_t *rx, uint64_t *txb, uint64_t *rxb) { if (tx) *tx = g_frames_tx; if (rx) *rx = g_frames_rx; if (txb) *txb = 0; if (rxb) *rxb = 0; }
const struct netif_ops netif_wlan0 = { "wlan0", "Wi-Fi CYW43455 (SDIO)", wl_if_init, wl_if_send, wl_if_poll, wl_if_mac, wl_if_link, wl_if_stats };

/* Conectar y pasar la red de SOMA al Wi-Fi: interfaz wlan0 + DHCP */
static void connect_and_route(const char *ssid, const char *pass)
{
    int e = wifi_connect(ssid, pass);
    if (e) { report("conectar", e); return; }
    if (!netif_use("wlan0")) { kprint("wifi: wlan0 no arranca\n"); return; }
    memcpy(net_mac, g_wmac, 6);
    kprint("wifi: conectado. Pidiendo direccion por DHCP...\n");
    if (dhcp_run(1) == 0) kprint("wifi: listo. Prueba 'ping 8.8.8.8' o abre el navegador.\n");
    else kprint("wifi: sin direccion; puedes probar 'dhcp' otra vez\n");
}

/* ------------------------------------------------------------------ shell */
static const char *skip(const char *s) { while (*s == ' ') s++; return s; }
static int word(const char *s, const char *w) { while (*w) if (*s++ != *w++) return 0; return *s == 0 || *s == ' '; }
static uint32_t num(const char **ps)
{
    const char *s = skip(*ps); uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; for (;;) { char c = *s; uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 99; if (d > 15) break; v = v * 16 + d; s++; } }
    else while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    *ps = s; return v;
}

void wifi_command(const char *args)
{
    const char *a = skip(args ? args : "");
    if (board_detect() == 0 && !g_board.has_wifi) { kprint("wifi: esta placa no tiene Wi-Fi\n"); return; }
    if (!*a) {
        static const char *st[] = { "apagado", "chip respondiendo", "chip identificado", "firmware en marcha", "listo" };
        kprint("wifi: "); kprint(st[g_state > 4 ? 4 : g_state]);
        if (g_link) { kprint(" | conectado a "); kprint_mac(g_bssid); }
        if (g_state >= 1) { kprint(" | BCM"); kprint_hex16(g_chip.id); kprint(" rev "); kprint_dec(g_chip.rev); }
        if (g_up) { kprint(" | tramas rx "); kprint_dec(g_frames_rx); kprint(" tx "); kprint_dec(g_frames_tx); kprint(" eventos "); kprint_dec(g_events); kprint(" malas "); kprint_dec(g_bad); }
        kprint("\nordenes: wifi scan | conectar <red> <clave> | desconectar | prueba | sondear | nucleos | firmware | log | mux [n] | rd | wr\n");
        kprint("         (si el nombre de la red tiene espacios: wifi conectar \"Mi red\" clave)\n");
        return;
    }
    if (word(a, "prueba")) {                                      /* todo seguido: lo que hay que fotografiar si algo falla */
        board_print();
        int e = probe(1);
        if (e) { report("sondear", e); return; }
        cores_print();
        e = fw_load();
        if (e) { report("firmware", e); return; }
        fw_log(1200);
        e = wifi_up();
        if (e) { report("iniciar", e); return; }
        wifi_scan(0);
        return;
    }
    if (word(a, "scan") || word(a, "escanear")) { int e = wifi_scan(0); if (e) report("scan", e); return; }
    if (word(a, "iniciar")) { int e = wifi_up(); report("iniciar", e); return; }
    if (word(a, "desconectar")) {
        if (g_up) fw_ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
        g_link = 0; g_joined = 0; g_sm_on = 0;
        if (netif_use("eth0")) memcpy(net_mac, netif_mac(), 6);
        kprint("wifi: desconectado (la red vuelve a eth0)\n");
        return;
    }
    if (word(a, "conectar") || word(a, "connect")) {
        a = skip(a + (a[4] == 'e' ? 7 : 8));
        static char ssid[34], pass[66]; int n = 0;
        if (*a == '"') { a++; while (*a && *a != '"' && n < 32) ssid[n++] = *a++; if (*a == '"') a++; }
        else while (*a && *a != ' ' && n < 32) ssid[n++] = *a++;
        ssid[n] = 0; a = skip(a); n = 0;
        while (*a && n < 64) pass[n++] = *a++;
        while (n > 0 && pass[n - 1] == ' ') n--;
        pass[n] = 0;
        if (!ssid[0]) { kprint("uso: wifi conectar <red> <clave>   (wifi scan muestra las redes)\n"); return; }
        connect_and_route(ssid, pass);
        return;
    }
    if (word(a, "sondear") || word(a, "probe")) { int e = probe(1); report("sondear", e); return; }
    if (word(a, "nucleos")) { if (g_state < 2) { int e = probe(0); if (e) { report("sondear", e); return; } } cores_print(); return; }
    if (word(a, "firmware")) { int e = fw_load(); report("firmware", e); return; }
    if (word(a, "log")) { fw_log(4096); return; }
    if (word(a, "mux")) {
        a = skip(a + 3);
        if (*a) { uint32_t f = num(&a); for (unsigned p = 30; p <= 35; p++) { mux_set(p, f); if (p > 30) pull_up(p); } g_mux_done = 1; kprint("pines 30-35 con funcion "); kprint_dec(f); kprint("\n"); }
        mux_print();
        return;
    }
    if (word(a, "rd") || word(a, "wr")) {
        int w = a[0] == 'w'; a += 2;
        if (g_state < 1) { int e = probe(0); if (e) { report("sondear", e); return; } }
        uint32_t ad = num(&a), v = w ? num(&a) : 0;
        int e = w ? bp_wr(ad & ~3u, v) : bp_rd(ad & ~3u, &v);
        if (e) { report(w ? "wr" : "rd", e); return; }
        kprint("0x"); kprint_hex32(ad & ~3u); kprint(" = 0x"); kprint_hex32(v); kprint("\n");
        return;
    }
    kprint("wifi: orden desconocida (escribe 'wifi')\n");
}

#else  /* sin SDIO2: QEMU virt */
void wifi_command(const char *args) { (void)args; kprint("wifi: sin adaptador Wi-Fi en esta plataforma\n"); }
#endif
