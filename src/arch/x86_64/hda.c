/* SOMA_PARCHE_020_AUDIO + SOMA_172_T480S: controlador Intel HD Audio. Ver hda.h.
 *
 * 172 (audio real, ThinkPad T480s: controlador Sunrise Point-LP 8086:9D71, codec Realtek ALC257 y codec HDMI de Intel):
 *   - BAR0 de 64 bits (puede estar por encima de 4 GiB);
 *   - DMA coherente: como hda_intel de Linux en los PCH de Intel, sin "no snoop" (DEVC 0x78 bit 11), TCSEL a 0 y, en
 *     Skylake y posteriores, el apagado de reloj MISCBDCGE (CGCTL 0x48 bit 6) desactivado durante el reinicio;
 *   - ordenes al codec por los anillos CORB/RIRB (la interfaz inmediata no es fiable en Skylake+); si fallan, la inmediata;
 *   - eleccion del codec analogico (el de HDMI no tiene altavoces) y dos caminos: ALTAVOZ (pin fijo) y AURICULARES (pin
 *     con conector), cada uno con su DAC si puede; el mismo flujo alimenta los dos DAC;
 *   - selector de conexion (verbo 701) en cada nodo del camino y entradas del mezclador desmuteadas;
 *   - deteccion del conector de auriculares (verbo F09) cada 250 ms: con auriculares se calla el altavoz;
 *   - un cerrojo para las ordenes al codec (las usan el audio, las teclas de volumen y el hilo del conector).
 * 177 (microfono): captura por el primer flujo de ENTRADA (ADC -> mezcladores/selectores -> pin de entrada), con la lista de
 *   fuentes posibles (microfono interno, del conector, linea), seleccion, ganancia, y un anillo DMA propio de 128 KiB.
 * Interruptor: soma.audio=off. */
#include <stdint.h>
#include <stddef.h>
#include "hda.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "console.h"
#include "idt.h"
#include "arch.h"
#include "thread.h"
#include "bootfb.h"
#include "sysinfo.h"

#define REG_GCAP     0x00
#define REG_GCTL     0x08
#define REG_STATESTS 0x0E
#define REG_INTCTL   0x20
#define REG_CORBLBASE 0x40
#define REG_CORBUBASE 0x44
#define REG_CORBWP   0x48
#define REG_CORBRP   0x4A
#define REG_CORBCTL  0x4C
#define REG_CORBSIZE 0x4E
#define REG_RIRBLBASE 0x50
#define REG_RIRBUBASE 0x54
#define REG_RIRBWP   0x58
#define REG_RINTCNT  0x5A
#define REG_RIRBCTL  0x5C
#define REG_RIRBSTS  0x5D
#define REG_RIRBSIZE 0x5E
#define REG_ICOI     0x60
#define REG_IRII     0x64
#define REG_ICIS     0x68
#define SD_CTL   0x00
#define SD_STS   0x03
#define SD_LPIB  0x04
#define SD_CBL   0x08
#define SD_LVI   0x0C
#define SD_FMT   0x12
#define SD_BDPL  0x18
#define SD_BDPU  0x1C

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

#define RING_BYTES   (128 * 1024)
#define BDL_ENTRY    (16 * 1024)                 /* 8 entradas de 16 KiB */
#define STREAM_TAG   1
#define MAXPATH      8

struct out_path {
    uint8_t node[MAXPATH], ci[MAXPATH];         /* del pin al DAC; ci[k] = entrada de node[k] que lleva a node[k+1] */
    int len;
    uint8_t pin, dac;
    uint32_t pincfg, pincap;
};

static volatile uint8_t *mmio;
static int ready;
static uint32_t pci_id, gcap;
static uint64_t mmio_phys;
static uint8_t cad, afg;
static uint32_t codec_vendor, codec_mask;
static struct out_path spk, hp;                  /* altavoz y auriculares (len 0 = no hay) */
static int hp_present = -1, hp_detect;
static uint32_t jack_changes;
static uint32_t sd_base;
static uint8_t *ring; static uintptr_t ring_phys;
static uint8_t *bdl; static uintptr_t bdl_phys;
static uint32_t cur_rate;
static int volume_pct = 100;
static int use_corb;
static volatile uint32_t *corb; static volatile uint32_t *rirb; static uintptr_t cr_phys;
static uint16_t rirb_rp;
static volatile int cmd_lock;
static uint32_t cmd_timeouts, cmd_last_fail;

static inline uint8_t  rd8(uint32_t r)  { return *(volatile uint8_t *)(mmio + r); }
static inline uint16_t rd16(uint32_t r) { return *(volatile uint16_t *)(mmio + r); }
static inline uint32_t rd32(uint32_t r) { return *(volatile uint32_t *)(mmio + r); }
static inline void wr8(uint32_t r, uint8_t v)   { *(volatile uint8_t *)(mmio + r) = v; }
static inline void wr16(uint32_t r, uint16_t v) { *(volatile uint16_t *)(mmio + r) = v; }
static inline void wr32(uint32_t r, uint32_t v) { *(volatile uint32_t *)(mmio + r) = v; }

static void wait_ms(uint32_t ms) { uint64_t t = timer_get_uptime_ms() + ms + 1; while (timer_get_uptime_ms() < t) arch_pause(); }
/* plazos de las ordenes al codec por el TSC: se esperan con las interrupciones cerradas (el reloj del sistema no avanza) */
static inline uint64_t tsc_now(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
static inline uint64_t tsc_after_ms(uint32_t ms) { return tsc_now() + (tsc_ticks_per_ms ? tsc_ticks_per_ms : 3000000ULL) * ms; }

/* ---- comandos al codec ---- */
static uint64_t lock_cmd(void)
{
    uint64_t f; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    while (__sync_lock_test_and_set(&cmd_lock, 1)) arch_pause();
    return f;
}
static void unlock_cmd(uint64_t f) { __sync_lock_release(&cmd_lock); if (f & 0x200) __asm__ volatile ("sti" ::: "memory"); }

static int cmd_immediate(uint32_t verb, uint32_t *resp)
{
    uint64_t t = tsc_after_ms(20);
    while (rd16(REG_ICIS) & 1) { if (tsc_now() > t) return -1; arch_pause(); }
    wr32(REG_ICOI, verb);
    wr16(REG_ICIS, 0x2);                                  /* limpia IRV */
    wr16(REG_ICIS, 0x1);                                  /* envia */
    t = tsc_after_ms(50);
    while ((rd16(REG_ICIS) & 0x3) != 0x2) { if (tsc_now() > t) return -2; arch_pause(); }
    if (resp) *resp = rd32(REG_IRII);
    return 0;
}
static int cmd_corb(uint32_t verb, uint32_t *resp)
{
    uint16_t wp = (uint16_t)((rd16(REG_CORBWP) + 1) & 0xFF);
    corb[wp] = verb;
    __asm__ volatile ("mfence" ::: "memory");
    wr16(REG_CORBWP, wp);
    uint64_t t = tsc_after_ms(50);
    for (;;) {
        uint16_t hw = rd16(REG_RIRBWP) & 0xFF;
        while (rirb_rp != hw) {
            rirb_rp = (uint16_t)((rirb_rp + 1) & 0xFF);
            uint32_t r = rirb[rirb_rp * 2], ex = rirb[rirb_rp * 2 + 1];
            if (ex & 0x10) continue;                      /* respuesta no solicitada: no se usan */
            wr8(REG_RIRBSTS, 0x5);
            if (resp) *resp = r;
            return 0;
        }
        if (tsc_now() > t) {
            cmd_last_fail = (uint32_t)rd16(REG_CORBRP) << 16 | rd16(REG_RIRBWP);
            return -3;
        }
        arch_pause();
    }
}
#ifdef HDA_HOST_TEST
int sim_codec(uint8_t codec, uint32_t verb, uint32_t *resp);     /* tests/test_hda_172.c: codec simulado */
#endif
static int codec_cmd(uint32_t verb_payload, uint32_t *resp)
{
#ifdef HDA_HOST_TEST
    return sim_codec(cad, verb_payload, resp);
#endif
    uint64_t f = lock_cmd();
    uint32_t v = ((uint32_t)cad << 28) | verb_payload;
    int r = use_corb ? cmd_corb(v, resp) : cmd_immediate(v, resp);
    if (r) cmd_timeouts++;
    unlock_cmd(f);
    return r;
}
#define V12(nid, verb, payload) (((uint32_t)(nid) << 20) | ((uint32_t)(verb) << 8) | ((payload) & 0xFF))
#define V4(nid, verb, payload)  (((uint32_t)(nid) << 20) | ((uint32_t)(verb) << 16) | ((payload) & 0xFFFF))

static uint32_t get_param(uint8_t nid, uint8_t param) { uint32_t r = 0; codec_cmd(V12(nid, 0xF00, param), &r); return r; }
static uint32_t get_verb(uint8_t nid, uint32_t verb, uint8_t payload) { uint32_t r = 0; codec_cmd(V12(nid, verb, payload), &r); return r; }
static void set_verb(uint8_t nid, uint32_t verb, uint8_t payload) { codec_cmd(V12(nid, verb, payload), 0); }

/* CORB (256 ordenes de 4 B) y RIRB (256 respuestas de 8 B) en una pagina; devuelve 0 si funcionan */
static int corb_setup(void)
{
    if (!cr_phys) { cr_phys = pmm_alloc_frame(); if (!cr_phys) return -1; }
    uint8_t *pg = (uint8_t *)cr_phys;
    for (int i = 0; i < 4096; i++) pg[i] = 0;
    corb = (volatile uint32_t *)pg; rirb = (volatile uint32_t *)(pg + 2048);
    wr8(REG_CORBCTL, 0); wr8(REG_RIRBCTL, 0);
    uint64_t t = timer_get_uptime_ms() + 20;
    while ((rd8(REG_CORBCTL) & 2) || (rd8(REG_RIRBCTL) & 2)) { if (timer_get_uptime_ms() > t) return -2; arch_pause(); }
    if (!(rd8(REG_CORBSIZE) & 0x40) || !(rd8(REG_RIRBSIZE) & 0x40)) return -3;   /* sin anillos de 256 entradas */
    wr32(REG_CORBLBASE, (uint32_t)cr_phys); wr32(REG_CORBUBASE, (uint32_t)((uint64_t)cr_phys >> 32));
    wr8(REG_CORBSIZE, 2);
    wr16(REG_CORBRP, 0x8000);                             /* reinicio del puntero de lectura */
    t = timer_get_uptime_ms() + 5;
    while (!(rd16(REG_CORBRP) & 0x8000) && timer_get_uptime_ms() <= t) arch_pause();   /* algunos no lo reflejan */
    wr16(REG_CORBRP, 0);
    t = timer_get_uptime_ms() + 5;
    while ((rd16(REG_CORBRP) & 0x8000) && timer_get_uptime_ms() <= t) arch_pause();
    wr16(REG_CORBWP, 0);
    wr32(REG_RIRBLBASE, (uint32_t)(cr_phys + 2048)); wr32(REG_RIRBUBASE, (uint32_t)((uint64_t)(cr_phys + 2048) >> 32));
    wr8(REG_RIRBSIZE, 2);
    wr16(REG_RIRBWP, 0x8000);
    wr16(REG_RINTCNT, 1);
    rirb_rp = 0;
    wr8(REG_RIRBCTL, 3);                                  /* DMA de respuestas + aviso RINTFL (como Linux; INTCTL apagado: no llega IRQ) */
    wr8(REG_CORBCTL, 2);
    use_corb = 1;
    uint32_t v = 0;
    if (codec_cmd(V12(0, 0xF00, 0x00), &v) != 0 || v == 0 || v == 0xFFFFFFFF) {   /* prueba: identificador del codec */
        wr8(REG_CORBCTL, 0); wr8(REG_RIRBCTL, 0); use_corb = 0; return -4;
    }
    return 0;
}

/* lista de conexiones de un widget (entradas de 8 o 16 bits); devuelve cuantas */
static int conn_list(uint8_t nid, uint8_t *out, int max)
{
    uint32_t len = get_param(nid, 0x0E);
    int n = (int)(len & 0x7F), lng = (len >> 7) & 1, got = 0;
    for (int i = 0; i < n && got < max; ) {
        uint32_t r = get_verb(nid, 0xF02, (uint8_t)i);
        if (lng) { for (int k = 0; k < 2 && i < n && got < max; ++k, ++i) out[got++] = (uint8_t)((r >> (16 * k)) & 0xFF); }
        else     { for (int k = 0; k < 4 && i < n && got < max; ++k, ++i) out[got++] = (uint8_t)((r >> (8 * k)) & 0xFF); }
    }
    return got;
}

static int widget_type(uint8_t nid) { return (int)((get_param(nid, 0x09) >> 20) & 0xF); }

/* camino desde p->node[p->len-1] hasta un DAC (a traves de mezcladores/selectores, profundidad 3), evitando 'avoid' */
static int find_dac(struct out_path *p, int depth, uint8_t avoid)
{
    uint8_t nid = p->node[p->len - 1];
    uint8_t cl[16]; int n = conn_list(nid, cl, 16);
    for (int i = 0; i < n; ++i) {
        if (p->len >= MAXPATH) return 0;
        int t = widget_type(cl[i]);
        if (t == 0 && cl[i] != avoid) { p->ci[p->len - 1] = (uint8_t)i; p->node[p->len] = cl[i]; p->ci[p->len] = 0; p->len++; p->dac = cl[i]; return 1; }
        if ((t == 2 || t == 3) && depth < 3) {
            int save = p->len;
            p->ci[p->len - 1] = (uint8_t)i; p->node[p->len] = cl[i]; p->ci[p->len] = 0; p->len++;
            if (find_dac(p, depth + 1, avoid)) return 1;
            p->len = save;
        }
    }
    return 0;
}
static int build_path(struct out_path *p, uint8_t pin, uint8_t avoid)
{
    p->len = 1; p->node[0] = pin; p->ci[0] = 0; p->pin = pin; p->dac = 0;
    p->pincfg = get_verb(pin, 0xF1C, 0); p->pincap = get_param(pin, 0x0C);
    if (find_dac(p, 0, avoid)) return 1;
    if (avoid && (p->len = 1, find_dac(p, 0, 0))) return 1;    /* si no hay otro DAC, se comparte */
    p->len = 0;
    return 0;
}

static void set_amp_out(uint8_t nid, int pct, uint32_t steps)
{
    uint32_t gain = steps ? (uint32_t)pct * steps / 100 : 0;
    uint32_t mute = pct == 0 ? 0x80 : 0;
    codec_cmd(V4(nid, 0x3, 0xB000 | mute | (gain & 0x7F)), 0);        /* salida, izquierdo y derecho */
}
static void set_amp_in(uint8_t nid, int idx, int mute)
{
    codec_cmd(V4(nid, 0x3, 0x7000 | ((uint32_t)idx << 8) | (mute ? 0x80 : 0)), 0);   /* entrada a 0 dB */
}
static uint32_t out_amp_steps(uint8_t nid)
{
    uint32_t caps = get_param(nid, 0x09);
    if (!(caps & (1u << 2))) return 0;                                  /* sin amplificador de salida */
    uint32_t a = (caps & (1u << 3)) ? get_param(nid, 0x12) : get_param(afg, 0x12);
    return (a >> 8) & 0x7F;
}
static void path_volume(const struct out_path *p, int pct)
{
    for (int i = 0; i < p->len; ++i) {
        uint8_t nid = p->node[i];
        uint32_t caps = get_param(nid, 0x09);
        if (caps & (1u << 2)) set_amp_out(nid, pct, out_amp_steps(nid));
        if ((caps & (1u << 1)) && i + 1 < p->len) set_amp_in(nid, p->ci[i], pct == 0);
    }
}

int hda_get_volume(void) { return volume_pct; }   /* SOMA_168_T480S: teclas de volumen */
int hda_set_volume(int pct)
{
    if (!ready) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    volume_pct = pct;
    if (spk.len) path_volume(&spk, pct);
    if (hp.len) path_volume(&hp, pct);
    return 0;
}

/* conecta un camino: energia D0, selectores, amplificadores, pin habilitado y EAPD */
static void path_setup(const struct out_path *p, int headphone)
{
    for (int i = 0; i < p->len; ++i) {
        uint8_t nid = p->node[i];
        set_verb(nid, 0x705, 0);
        if (i + 1 < p->len) {
            int t = widget_type(nid);
            uint8_t cl[16]; int n = conn_list(nid, cl, 16);
            if ((t == 3 || t == 4) && n > 1) set_verb(nid, 0x701, p->ci[i]);    /* selector / pin: la entrada del camino */
            if (t == 2) for (int k = 0; k < n; ++k) set_amp_in(nid, k, k != p->ci[i]);   /* mezclador: solo nuestra entrada */
        }
    }
    uint8_t ctl = 0x40;                                                   /* OUT_EN */
    if (headphone && (p->pincap & (1u << 3))) ctl |= 0x80;                /* amplificador de auriculares */
    set_verb(p->pin, 0x707, ctl);
    if (p->pincap & (1u << 16)) set_verb(p->pin, 0x70C, 2);               /* EAPD (amplificador externo) */
}

/* ---- formato de flujo ---- */
int hda_format_for_rate(uint32_t rate, uint16_t *fmt)
{
    static const struct { uint32_t r; uint16_t f; } T[] = {
        { 48000, 0x0000 }, { 44100, 0x4000 }, { 32000, 0x0A00 /* x2 / 3 */ }, { 24000, 0x0100 }, { 22050, 0x4100 },
        { 16000, 0x0200 }, { 11025, 0x4300 }, { 8000, 0x0500 }, { 96000, 0x0800 }, { 88200, 0x4800 },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i)
        if (T[i].r == rate) { *fmt = (uint16_t)(T[i].f | 0x0011); return 0; }    /* 16 bits, 2 canales */
    return -1;
}

/* ---- flujo de salida ---- */
static int stream_reset(void)
{
    wr8(sd_base + SD_CTL, 0);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
    wr8(sd_base + SD_CTL, 1);                                          /* SRST */
    t = timer_get_uptime_ms() + 20;
    while (!(rd8(sd_base + SD_CTL) & 1)) { if (timer_get_uptime_ms() > t) return -1; arch_pause(); }
    wr8(sd_base + SD_CTL, 0);
    t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 1) { if (timer_get_uptime_ms() > t) return -2; arch_pause(); }
    return 0;
}

static void dac_stream(uint8_t dac, uint16_t fmt)
{
    if (!dac) return;
    codec_cmd(V4(dac, 0x2, fmt), 0);
    codec_cmd(V12(dac, 0x706, (STREAM_TAG << 4) | 0), 0);
}
static int hw_start(void *ctx, uint32_t rate)
{
    (void)ctx;
    uint16_t fmt;
    if (!ready || hda_format_for_rate(rate, &fmt) != 0) return -1;
    if (stream_reset() != 0) return -2;
    for (uint32_t i = 0; i < RING_BYTES; ++i) ring[i] = 0;
    const uint32_t nent = RING_BYTES / BDL_ENTRY;
    for (uint32_t e = 0; e < nent; ++e) {
        uint32_t *d = (uint32_t *)(bdl + e * 16);
        uintptr_t a = ring_phys + (uintptr_t)e * BDL_ENTRY;
        d[0] = (uint32_t)a; d[1] = (uint32_t)((uint64_t)a >> 32); d[2] = BDL_ENTRY; d[3] = 0;     /* sin IOC */
    }
    __asm__ volatile ("mfence" ::: "memory");
    wr32(sd_base + SD_BDPL, (uint32_t)bdl_phys); wr32(sd_base + SD_BDPU, (uint32_t)((uint64_t)bdl_phys >> 32));
    wr32(sd_base + SD_CBL, RING_BYTES);
    wr16(sd_base + SD_LVI, (uint16_t)(nent - 1));
    wr16(sd_base + SD_FMT, fmt);
    wr8(sd_base + SD_CTL + 2, (uint8_t)(STREAM_TAG << 4));             /* numero de flujo (bits 23:20) */
    /* codec: formato y flujo en los DAC de los dos caminos (el mismo sonido por altavoz y auriculares) */
    if (spk.len) dac_stream(spk.dac, fmt);
    if (hp.len && (!spk.len || hp.dac != spk.dac)) dac_stream(hp.dac, fmt);
    cur_rate = rate;
    wr8(sd_base + SD_CTL, rd8(sd_base + SD_CTL) | 2);                  /* RUN */
    return 0;
}

static void hw_stop(void *ctx)
{
    (void)ctx;
    if (!ready) return;
    wr8(sd_base + SD_CTL, rd8(sd_base + SD_CTL) & (uint8_t)~2);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
}

static uint32_t hw_pos(void *ctx) { (void)ctx; return rd32(sd_base + SD_LPIB) % RING_BYTES; }

static struct audio_hw hw_ops;
const struct audio_hw *hda_hw(void) { return &hw_ops; }
int hda_ready(void) { return ready; }

/* ---- conector de auriculares ---- */
static int hp_sense(void)
{
    if (!hp.len || !hp_detect) return -1;
    if (hp.pincap & (1u << 1)) set_verb(hp.pin, 0x709, 0);              /* algunos codecs necesitan disparar la medida */
    return (int)(get_verb(hp.pin, 0xF09, 0) >> 31);
}
static void apply_jack(int present)
{
    if (!spk.len) return;
    if (present) { set_verb(spk.pin, 0x707, 0); if (spk.pincap & (1u << 16)) set_verb(spk.pin, 0x70C, 0); }
    else path_setup(&spk, 0);
}
static void jack_thread(void *a)
{
    (void)a;
    for (;;) {
        thread_sleep(250);
        int s = hp_sense();
        if (s < 0 || s == hp_present) continue;
        hp_present = s; jack_changes++;
        apply_jack(s);
        hda_set_volume(volume_pct);
        kprint(s ? "audio: auriculares conectados (altavoz en silencio)\n" : "audio: auriculares quitados (suena el altavoz)\n");
    }
}

/* ---- SOMA_177_T480S: entrada (microfono) ---- */
#define CAP_TAG    2
#define MAXCAND    8
struct in_path {
    uint8_t node[MAXPATH], ci[MAXPATH];         /* del ADC al pin: ci[k] = entrada de node[k] que lleva a node[k+1] */
    int len;
    uint8_t adc, pin;
    uint32_t pincfg, pincap;
    int score;
};
static struct in_path cand[MAXCAND];
static int ncand, cand_sel = -1;
static int mic_gain_pct = 66;
static uint32_t csd_base;
static uint8_t *cring; static uintptr_t cring_phys;
static uint8_t *cbdl; static uintptr_t cbdl_phys;
static int cap_running;
static uint32_t cap_rate_cur;

static uint32_t in_amp_caps(uint8_t nid)
{
    uint32_t c = get_param(nid, 0x09);
    return (c & (1u << 3)) ? get_param(nid, 0x0D) : get_param(afg, 0x0D);
}
static void in_amp_set(uint8_t nid, int idx, uint32_t gain, int mute)
{
    codec_cmd(V4(nid, 0x3, 0x7000 | ((uint32_t)idx << 8) | (mute ? 0x80 : 0) | (gain & 0x7F)), 0);
}

static int pin_is_input(uint8_t nid, uint32_t *cfg_out, uint32_t *cap_out)
{
    uint32_t pc = get_param(nid, 0x0C);
    if (!(pc & (1u << 5))) return 0;                                   /* sin capacidad de entrada */
    uint32_t cfg = get_verb(nid, 0xF1C, 0);
    if (((cfg >> 30) & 3) == 1) return 0;                              /* sin conexion fisica */
    uint32_t dev = (cfg >> 20) & 0xF;
    if (dev == 12 || dev == 13) return 0;                              /* entradas digitales (S/PDIF) */
    *cfg_out = cfg; *cap_out = pc;
    return 1;
}
static int in_score(uint32_t cfg)
{
    uint32_t dev = (cfg >> 20) & 0xF, gross = (cfg >> 28) & 3;
    int s = dev == 10 ? 100 : dev == 8 ? 40 : dev == 9 ? 30 : 10;
    if (gross == 1) s += 30;                                           /* interno: el microfono del portatil */
    return s;
}
static void in_dfs(struct in_path *p, int depth)
{
    uint8_t nid = p->node[p->len - 1];
    uint8_t cl[16]; int n = conn_list(nid, cl, 16);
    for (int i = 0; i < n && ncand < MAXCAND; ++i) {
        if (p->len >= MAXPATH) return;
        int t = widget_type(cl[i]);
        if (t == 4) {
            uint32_t cfg, pc;
            int dup = 0;
            for (int k = 0; k < ncand; ++k) if (cand[k].pin == cl[i]) dup = 1;
            if (dup || !pin_is_input(cl[i], &cfg, &pc)) continue;
            struct in_path *c = &cand[ncand];
            *c = *p;
            c->ci[p->len - 1] = (uint8_t)i; c->node[p->len] = cl[i]; c->ci[p->len] = 0; c->len = p->len + 1;
            c->pin = cl[i]; c->pincfg = cfg; c->pincap = pc; c->score = in_score(cfg);
            ncand++;
        } else if ((t == 2 || t == 3) && depth < 3) {
            int save = p->len;
            p->ci[p->len - 1] = (uint8_t)i; p->node[p->len] = cl[i]; p->ci[p->len] = 0; p->len++;
            in_dfs(p, depth + 1);
            p->len = save;
        }
    }
}
/* Busca en el codec activo todas las fuentes de entrada alcanzables desde algun ADC; las ordena (microfono interno primero) */
static void mic_scan(void)
{
    ncand = 0; cand_sel = -1;
    if (!afg) return;
    uint32_t wsub = get_param(afg, 0x04);
    uint8_t w0 = (uint8_t)((wsub >> 16) & 0xFF); int nw = (int)(wsub & 0xFF);
    for (int i = 0; i < nw && ncand < MAXCAND; ++i) {
        uint8_t nid = (uint8_t)(w0 + i);
        if (widget_type(nid) != 1) continue;                           /* conversor de entrada (ADC) */
        struct in_path p;
        p.len = 1; p.node[0] = nid; p.ci[0] = 0; p.adc = nid; p.pin = 0; p.pincfg = 0; p.pincap = 0; p.score = 0;
        in_dfs(&p, 0);
        for (int k = 0; k < ncand; ++k) cand[k].adc = cand[k].node[0];
    }
    for (int a = 1; a < ncand; ++a) {                                  /* orden estable por puntuacion descendente */
        struct in_path x = cand[a]; int b = a - 1;
        while (b >= 0 && cand[b].score < x.score) { cand[b + 1] = cand[b]; b--; }
        cand[b + 1] = x;
    }
}

static void in_gain_apply(const struct in_path *p)
{
    int pin_has_amp = (get_param(p->pin, 0x09) & (1u << 1)) != 0;
    for (int i = 0; i < p->len; ++i) {
        uint8_t nid = p->node[i];
        uint32_t caps = get_param(nid, 0x09);
        if (!(caps & (1u << 1))) continue;                             /* sin amplificador de entrada */
        uint32_t ac = in_amp_caps(nid);
        uint32_t offset = ac & 0x7F, steps = (ac >> 8) & 0x7F;
        uint32_t gain = offset;
        if (i == p->len - 1) gain = steps ? (uint32_t)mic_gain_pct * steps / 100 : 0;        /* pin: "boost" del microfono */
        else if (i == 0 && !pin_has_amp && steps > offset) gain = offset + (steps - offset) * (uint32_t)mic_gain_pct / 200;   /* sin boost en el pin (digital): la ganancia sube en el ADC */
        if (i == p->len - 1) in_amp_set(nid, 0, gain, 0);
        else {
            uint8_t cl[16]; int n = conn_list(nid, cl, 16);
            for (int k = 0; k < n; ++k) in_amp_set(nid, k, gain, k != p->ci[i]);
        }
    }
}

static void in_path_setup(const struct in_path *p)
{
    for (int i = 0; i < p->len; ++i) {
        uint8_t nid = p->node[i];
        set_verb(nid, 0x705, 0);                                       /* D0 */
        if (i + 1 < p->len) {
            int t = widget_type(nid);
            uint8_t cl[16]; int n = conn_list(nid, cl, 16);
            if ((t == 1 || t == 3) && n > 1) set_verb(nid, 0x701, p->ci[i]);
        }
    }
    uint8_t ctl = 0x20;                                                /* IN_EN */
    uint32_t vref = (p->pincap >> 8) & 0xFF;
    if (vref & (1u << 4)) ctl |= 4;                                    /* polarizacion del microfono: 80 % */
    else if (vref & (1u << 1)) ctl |= 1;                               /* o 50 % */
    set_verb(p->pin, 0x707, ctl);
    in_gain_apply(p);
}
static void in_path_off(const struct in_path *p) { set_verb(p->pin, 0x707, 0); }

int hda_mic_ready(void) { return ready && ncand > 0 && cring != 0; }
int hda_mic_count(void) { return ncand; }
int hda_mic_selected(void) { return cand_sel; }
int hda_mic_select(int idx)
{
    if (!ready || idx < 0 || idx >= ncand) return -1;
    int was = cap_running; uint32_t r = cap_rate_cur;
    if (was) hda_cap_stop();                                           /* apaga el pin de la fuente anterior */
    cand_sel = idx;                                                    /* el camino se enciende al empezar a capturar */
    if (was) hda_cap_start(r);
    return 0;
}
int hda_mic_set_gain(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    mic_gain_pct = pct;
    if (ready && cand_sel >= 0 && cap_running) in_gain_apply(&cand[cand_sel]);
    return 0;
}
int hda_mic_get_gain(void) { return mic_gain_pct; }

static void put_hex8(char *o, uint32_t max, uint32_t *p, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    if (*p + 5 >= max) return;
    o[(*p)++] = '0'; o[(*p)++] = 'x'; o[(*p)++] = H[(v >> 4) & 15]; o[(*p)++] = H[v & 15]; o[*p] = 0;
}
static void put_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p] = 0; }
void hda_mic_describe(int idx, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    if (idx < 0 || idx >= ncand) { put_s(out, max, &p, "(sin fuente)"); return; }
    const struct in_path *c = &cand[idx];
    uint32_t dev = (c->pincfg >> 20) & 0xF, gross = (c->pincfg >> 28) & 3;
    put_s(out, max, &p, dev == 10 ? "microfono" : dev == 8 ? "entrada de linea" : dev == 9 ? "AUX" : "entrada");
    put_s(out, max, &p, gross == 1 ? " interno" : " del conector");
    put_s(out, max, &p, " (pin "); put_hex8(out, max, &p, c->pin);
    put_s(out, max, &p, ", ADC "); put_hex8(out, max, &p, c->adc); put_s(out, max, &p, ")");
}

/* --- flujo de entrada --- */
static int cap_stream_reset(void)
{
    wr8(csd_base + SD_CTL, 0);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(csd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
    wr8(csd_base + SD_CTL, 1);
    t = timer_get_uptime_ms() + 20;
    while (!(rd8(csd_base + SD_CTL) & 1)) { if (timer_get_uptime_ms() > t) return -1; arch_pause(); }
    wr8(csd_base + SD_CTL, 0);
    t = timer_get_uptime_ms() + 20;
    while (rd8(csd_base + SD_CTL) & 1) { if (timer_get_uptime_ms() > t) return -2; arch_pause(); }
    return 0;
}
int hda_cap_start(uint32_t rate)
{
    if (!hda_mic_ready() || cand_sel < 0) return -1;
    uint16_t fmt;
    if (hda_format_for_rate(rate, &fmt) != 0) return -2;
    if (cap_running) hda_cap_stop();
    if (cap_stream_reset() != 0) return -3;
    for (uint32_t i = 0; i < RING_BYTES; ++i) cring[i] = 0;
    const uint32_t nent = RING_BYTES / BDL_ENTRY;
    for (uint32_t e = 0; e < nent; ++e) {
        uint32_t *d = (uint32_t *)(cbdl + e * 16);
        uintptr_t a = cring_phys + (uintptr_t)e * BDL_ENTRY;
        d[0] = (uint32_t)a; d[1] = (uint32_t)((uint64_t)a >> 32); d[2] = BDL_ENTRY; d[3] = 0;
    }
    __asm__ volatile ("mfence" ::: "memory");
    wr32(csd_base + SD_BDPL, (uint32_t)cbdl_phys); wr32(csd_base + SD_BDPU, (uint32_t)((uint64_t)cbdl_phys >> 32));
    wr32(csd_base + SD_CBL, RING_BYTES);
    wr16(csd_base + SD_LVI, (uint16_t)(nent - 1));
    wr16(csd_base + SD_FMT, fmt);
    wr8(csd_base + SD_CTL + 2, (uint8_t)(CAP_TAG << 4));
    codec_cmd(V4(cand[cand_sel].adc, 0x2, fmt), 0);                    /* formato del ADC */
    codec_cmd(V12(cand[cand_sel].adc, 0x706, (CAP_TAG << 4) | 0), 0); /* flujo 2, canal 0 */
    in_path_setup(&cand[cand_sel]);                                    /* pin, selectores y ganancias de la fuente elegida */
    cap_rate_cur = rate;
    wr8(csd_base + SD_CTL, rd8(csd_base + SD_CTL) | 2);                /* RUN */
    cap_running = 1;
    return 0;
}
void hda_cap_stop(void)
{
    if (!ready || !csd_base) return;
    wr8(csd_base + SD_CTL, rd8(csd_base + SD_CTL) & (uint8_t)~2);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(csd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
    if (cand_sel >= 0) { codec_cmd(V12(cand[cand_sel].adc, 0x706, 0), 0); in_path_off(&cand[cand_sel]); }   /* pin apagado: sin captura no hay microfono activo */
    cap_running = 0;
}
uint32_t hda_cap_pos(void) { return csd_base ? rd32(csd_base + SD_LPIB) % RING_BYTES : 0; }
const uint8_t *hda_cap_ring(uint32_t *bytes) { if (bytes) *bytes = RING_BYTES; return cring; }
int hda_cap_running(void) { return cap_running; }

/* volcado para diagnostico: todos los widgets del grupo de audio activo */
int hda_raw(uint8_t nid, uint32_t verb, uint32_t payload, uint32_t *resp)
{
    if (!ready) return -1;
    uint32_t r = 0;
    int rc = verb < 0x10 ? codec_cmd(V4(nid, verb, payload), &r) : codec_cmd(V12(nid, verb, payload), &r);
    if (resp) *resp = r;
    return rc;
}
void hda_dump(void)
{
    if (!ready || !afg) { kprint("audio: sin dispositivo\n"); return; }
    uint32_t wsub = get_param(afg, 0x04);
    uint8_t w0 = (uint8_t)((wsub >> 16) & 0xFF); int nw = (int)(wsub & 0xFF);
    kprint("audio: widgets del codec "); kprint_dec(cad); kprint(" (nid tipo caps [pincap cfg] conexiones)\n");
    static const char *const TN[16] = { "DAC", "ADC", "MIX", "SEL", "PIN", "POT", "VOL", "BEEP", "?", "?", "?", "?", "?", "?", "?", "?" };
    for (int i = 0; i < nw; ++i) {
        uint8_t nid = (uint8_t)(w0 + i);
        uint32_t caps = get_param(nid, 0x09);
        int t = (int)((caps >> 20) & 0xF);
        if (t > 7) continue;
        kprint("  "); kprint_hex16(nid); kprint(" "); kprint(TN[t]); kprint(" caps "); kprint_hex32(caps);
        if (t == 4) { kprint(" pincap "); kprint_hex32(get_param(nid, 0x0C)); kprint(" cfg "); kprint_hex32(get_verb(nid, 0xF1C, 0)); }
        if (caps & (1u << 1)) { kprint(" ampin "); kprint_hex32(in_amp_caps(nid)); }
        uint8_t cl[16]; int n = conn_list(nid, cl, 16);
        if (n) { kprint(" <-"); for (int k = 0; k < n; ++k) { kprint(" "); kprint_hex16(cl[k]); } }
        kprint("\n");
    }
}

/* ---- inicializacion ---- */
static int find_controller(uint8_t *bus, uint8_t *slot, uint8_t *fn)
{
    for (int b = 0; b < 8; ++b)
        for (int s = 0; s < 32; ++s)
            for (int f = 0; f < 8; ++f) {
                if (pci_vendor_id((uint8_t)b, (uint8_t)s, (uint8_t)f) == 0xFFFF) { if (f == 0) break; continue; }
                if ((pci_config_read32((uint8_t)b, (uint8_t)s, (uint8_t)f, 0x08) >> 16) == 0x0403) {
                    *bus = (uint8_t)b; *slot = (uint8_t)s; *fn = (uint8_t)f; return 1;
                }
            }
    return 0;
}

static const char *devname(uint32_t cfg)
{
    static const char *n[16] = { "salida de linea", "altavoz", "auriculares", "CD", "S/PDIF", "digital", "modem", "telefono",
                                 "entrada de linea", "AUX", "microfono", "telefonia", "entrada S/PDIF", "entrada digital", "?", "otro" };
    return n[(cfg >> 20) & 0xF];
}
/* Explora el codec 'c': grupo de audio y pines de salida. Rellena spk/hp. Devuelve cuantos caminos */
static int scan_codec(uint8_t c)
{
    cad = c;
    codec_vendor = get_param(0, 0x00);
    uint32_t sub = get_param(0, 0x04);
    uint8_t fg0 = (uint8_t)((sub >> 16) & 0xFF); int nfg = (int)(sub & 0xFF);
    afg = 0;
    for (int i = 0; i < nfg; ++i) if ((get_param((uint8_t)(fg0 + i), 0x05) & 0xFF) == 1) { afg = (uint8_t)(fg0 + i); break; }
    if (!afg) return 0;
    set_verb(afg, 0x705, 0);                                          /* D0 */
    wait_ms(2);
    uint32_t wsub = get_param(afg, 0x04);
    uint8_t w0 = (uint8_t)((wsub >> 16) & 0xFF); int nw = (int)(wsub & 0xFF);
    uint8_t spk_pin = 0, hp_pin = 0, line_pin = 0, any_pin = 0;
    for (int i = 0; i < nw; ++i) {
        uint8_t nid = (uint8_t)(w0 + i);
        if (widget_type(nid) != 4) continue;
        uint32_t pc = get_param(nid, 0x0C);
        if (!(pc & (1u << 4))) continue;                              /* sin capacidad de salida */
        if (pc & ((1u << 7) | (1u << 24))) continue;                  /* HDMI / DisplayPort */
        uint32_t cfg = get_verb(nid, 0xF1C, 0);
        if (((cfg >> 30) & 3) == 1) continue;                         /* sin conexion fisica */
        uint32_t dev = (cfg >> 20) & 0xF;
        if (dev == 1 && !spk_pin) spk_pin = nid;
        else if (dev == 2 && !hp_pin) hp_pin = nid;
        else if (dev == 0 && !line_pin) line_pin = nid;
        if (dev <= 2 && !any_pin) any_pin = nid;
        if (!any_pin && dev != 4 && dev != 5) any_pin = nid;
    }
    if (!spk_pin) spk_pin = line_pin ? line_pin : (hp_pin ? 0 : any_pin);   /* sin altavoz: la salida de linea hace de altavoz */
    spk.len = 0; hp.len = 0;
    if (spk_pin) build_path(&spk, spk_pin, 0);
    if (hp_pin) build_path(&hp, hp_pin, spk.len ? spk.dac : 0);
    return (spk.len ? 1 : 0) + (hp.len ? 1 : 0);
}

int hda_init(void)
{
    kprint("\nAUDIO (Intel HDA)\n-----------------\n");
    if (bootinfo_disabled("audio")) { kprint("hda: desactivado (soma.audio=off o modo seguro)\n"); return -1; }
    uint8_t bus, slot, fn;
    if (!find_controller(&bus, &slot, &fn)) { kprint("hda: no hay controlador de audio (QEMU: -device intel-hda -device hda-duplex)\n"); return -1; }
    pci_id = pci_config_read32(bus, slot, fn, 0x00);
    uint32_t lo = pci_config_read32(bus, slot, fn, 0x10);
    if (lo & 1) { kprint("hda: BAR0 no es de memoria\n"); return -2; }
    mmio_phys = lo & ~0xFULL;
    if (((lo >> 1) & 3) == 2) mmio_phys |= (uint64_t)pci_config_read32(bus, slot, fn, 0x14) << 32;   /* BAR de 64 bits */
    pci_config_write32(bus, slot, fn, 0x04, pci_config_read32(bus, slot, fn, 0x04) | 0x6);      /* memoria + bus master */
    int intel = (pci_id & 0xFFFF) == 0x8086;
    if (intel) {                                                       /* DMA coherente con la cache (hda_intel de Linux) */
        uint32_t tc = pci_config_read32(bus, slot, fn, 0x44);
        pci_config_write32(bus, slot, fn, 0x44, tc & ~7u);              /* TCSEL = TC0 */
        uint32_t dv = pci_config_read32(bus, slot, fn, 0x78);
        if (dv & (1u << 11)) pci_config_write32(bus, slot, fn, 0x78, dv & ~(1u << 11));   /* sin "no snoop" */
    }
    for (uint64_t off = 0; off < 0x4000; off += 4096)
        if (vmm_map_page(mmio_phys + off, mmio_phys + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) {
            kprint("hda: no se pudo mapear el MMIO\n"); return -3;
        }
    mmio = (volatile uint8_t *)(uintptr_t)mmio_phys;
    kprint("hda: controlador "); kprint_hex16((uint16_t)(pci_id & 0xFFFF)); kprint(":"); kprint_hex16((uint16_t)(pci_id >> 16));
    kprint(" en PCI "); kprint_dec(bus); kprint(":"); kprint_dec(slot); kprint(" MMIO 0x");
    if (mmio_phys >> 32) kprint_hex32((uint32_t)(mmio_phys >> 32));
    kprint_hex32((uint32_t)mmio_phys); kprint("\n");

    /* reinicio del controlador (en Intel, sin el apagado de reloj MISCBDCGE mientras tanto) */
    uint32_t cg = intel ? pci_config_read32(bus, slot, fn, 0x48) : 0;
    if (intel) pci_config_write32(bus, slot, fn, 0x48, cg & ~(1u << 6));
    wr8(REG_CORBCTL, 0); wr8(REG_RIRBCTL, 0);
    wr32(REG_GCTL, 0);
    uint64_t t = timer_get_uptime_ms() + 100;
    while (rd32(REG_GCTL) & 1) { if (timer_get_uptime_ms() > t) { kprint("hda: no sale de CRST\n"); return -4; } arch_pause(); }
    wait_ms(2);
    wr32(REG_GCTL, 1);
    t = timer_get_uptime_ms() + 100;
    while (!(rd32(REG_GCTL) & 1)) { if (timer_get_uptime_ms() > t) { kprint("hda: no entra en CRST\n"); return -5; } arch_pause(); }
    t = timer_get_uptime_ms() + 100;                                   /* los codecs tardan unos instantes en anunciarse */
    wait_ms(2);
    while (!rd16(REG_STATESTS) && timer_get_uptime_ms() < t) arch_pause();
    wait_ms(2);
    if (intel) pci_config_write32(bus, slot, fn, 0x48, cg);
    wr32(REG_INTCTL, 0);
    gcap = rd16(REG_GCAP);
    uint32_t iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF;
    if (oss == 0) { kprint("hda: el controlador no tiene flujos de salida\n"); return -6; }
    sd_base = 0x80 + 0x20 * iss;                                      /* primer flujo de salida */
    csd_base = iss ? 0x80 : 0;                                         /* SOMA_177_T480S: primer flujo de entrada */
    uint16_t st = rd16(REG_STATESTS);
    codec_mask = st;
    if (!st) { kprint("hda: ningun codec (STATESTS=0)\n"); return -7; }
    wr16(REG_STATESTS, st);
    int cr = corb_setup();
    kprint("hda: ordenes al codec por "); kprint(use_corb ? "CORB/RIRB" : "la interfaz inmediata");
    if (!use_corb) { kprint(" (CORB: "); kprint_dec((uint32_t)-cr); kprint(")"); }
    kprint("\n");

    /* codecs: el primero con caminos de salida analogicos (el de HDMI de Intel no tiene) */
    int found = 0;
    for (uint8_t c = 0; c < 15; ++c) {
        if (!(st & (1u << c))) continue;
        uint8_t save = cad; cad = c;
        uint32_t ven = get_param(0, 0x00);
        kprint("hda: codec "); kprint_dec(c); kprint(" fabricante 0x"); kprint_hex32(ven); kprint("\n");
        cad = save;
        if (!found && scan_codec(c) > 0) found = 1;
    }
    if (!found) { kprint("hda: ningun codec tiene un camino pin de salida -> DAC\n"); return -9; }
    kprint("hda: usando el codec "); kprint_dec(cad); kprint(" (0x"); kprint_hex32(codec_vendor); kprint("), flujos: ");
    kprint_dec(iss); kprint(" entrada / "); kprint_dec(oss); kprint(" salida\n");

    /* caminos */
    if (spk.len) path_setup(&spk, 0);
    if (hp.len) {
        path_setup(&hp, 1);
        hp_detect = (hp.pincap & (1u << 2)) && !((hp.pincfg >> 8) & 1);      /* deteccion de presencia y sin anulacion */
    }
    ready = 1;
    hda_set_volume(100);
    ready = 0;
    if (spk.len) { kprint("hda: "); kprint(devname(spk.pincfg)); kprint(": pin "); kprint_dec(spk.pin); kprint(" -> DAC "); kprint_dec(spk.dac); kprint("\n"); }
    if (hp.len) {
        kprint("hda: auriculares: pin "); kprint_dec(hp.pin); kprint(" -> DAC "); kprint_dec(hp.dac);
        kprint(hp_detect ? " (deteccion del conector)\n" : " (sin deteccion del conector)\n");
        int s = hp_sense();
        if (s >= 0) { hp_present = s; apply_jack(s); if (s) kprint("hda: auriculares conectados: altavoz en silencio\n"); }
    }

    mic_scan();                                                        /* SOMA_177_T480S: fuentes de entrada */
    if (ncand) {
        kprint("hda: entradas de captura: "); kprint_dec((uint32_t)ncand); kprint("\n");
        for (int k = 0; k < ncand; ++k) { char d[96]; hda_mic_describe(k, d, sizeof d); kprint("hda:   "); kprint_dec((uint32_t)k); kprint(": "); kprint(d); kprint("\n"); }
    } else kprint("hda: sin entradas de captura\n");
    /* anillo contiguo de 128 KiB y lista de descriptores */
    ring_phys = pmm_alloc_contig(RING_BYTES / 4096);
    bdl_phys = pmm_alloc_frame();
    if (!ring_phys || !bdl_phys) { kprint("hda: sin memoria para el DMA\n"); return -10; }
    ring = (uint8_t *)ring_phys; bdl = (uint8_t *)bdl_phys;
    for (uint32_t i = 0; i < RING_BYTES; ++i) ring[i] = 0;
    if (ncand && csd_base) {                                           /* SOMA_177_T480S: anillo de captura (si falta memoria, solo se pierde el microfono) */
        cring_phys = pmm_alloc_contig(RING_BYTES / 4096); cbdl_phys = pmm_alloc_frame();
        if (cring_phys && cbdl_phys) { cring = (uint8_t *)cring_phys; cbdl = (uint8_t *)cbdl_phys; for (uint32_t i = 0; i < RING_BYTES; ++i) cring[i] = 0; }
        else { cring = 0; kprint("hda: sin memoria para la captura\n"); }
    }
    hw_ops.ctx = 0; hw_ops.ring = ring; hw_ops.ring_bytes = RING_BYTES;
    hw_ops.start = hw_start; hw_ops.stop = hw_stop; hw_ops.pos = hw_pos;
    ready = 1;
    if (cring) cand_sel = 0;                                           /* SOMA_177_T480S: microfono interno por defecto (el pin se enciende solo al capturar) */
    if (hp_detect) thread_create("hdajack", jack_thread, 0);
    kprint("hda: salida lista, anillo DMA de "); kprint_dec(RING_BYTES / 1024); kprint(" KiB\n");
    return 0;
}

void hda_info(char *out, uint32_t max)
{
    uint32_t pos = 0;
    #define S(str) do { const char *_s = (str); while (*_s && pos + 1 < max) out[pos++] = *_s++; } while (0)
    #define N(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } S(_b + _i); } while (0)
    #define H(v) do { static const char _h[] = "0123456789ABCDEF"; uint32_t _x = (uint32_t)(v); char _b[9]; for (int _k = 0; _k < 8; _k++) _b[_k] = _h[(_x >> (28 - 4 * _k)) & 15]; _b[8] = 0; S(_b); } while (0)
    if (!ready) { S("audio: sin dispositivo"); }
    else {
        S("audio: Intel HDA "); H(pci_id); S(", codec "); N(cad); S(" (0x"); H(codec_vendor); S(")");
        S(use_corb ? ", ordenes por CORB/RIRB" : ", ordenes inmediatas");
        if (spk.len) { S("\n  "); S(devname(spk.pincfg)); S(": pin "); N(spk.pin); S(" -> DAC "); N(spk.dac); }
        if (hp.len) {
            S("\n  auriculares: pin "); N(hp.pin); S(" -> DAC "); N(hp.dac);
            S(hp_present == 1 ? " (CONECTADOS: altavoz en silencio)" : hp_present == 0 ? " (no conectados)" : " (sin deteccion)");
            if (jack_changes) { S(", cambios "); N(jack_changes); }
        }
        if (ncand) {
            S("\n  microfono: "); N(ncand); S(" entrada(s), activa "); N(cand_sel < 0 ? 0 : (uint32_t)cand_sel);
            S(", ganancia "); N(mic_gain_pct); S(" %");
            if (!cring) S(" (SIN anillo DMA)");
            else if (cap_running) { S(", capturando a "); N(cap_rate_cur); S(" Hz"); }
        } else S("\n  microfono: sin entradas de captura");
        S("\n  frecuencia actual "); N(cur_rate); S(" Hz, volumen "); N(volume_pct); S(" %");
        if (cmd_timeouts) { S(", ordenes sin respuesta "); N(cmd_timeouts); S(" (ultima: CORBRP/RIRBWP 0x"); H(cmd_last_fail); S(")"); }
    }
    out[pos < max ? pos : max - 1] = 0;
    #undef S
    #undef N
    #undef H
}
