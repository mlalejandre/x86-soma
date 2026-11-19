/* SOMA_172_T480S: prueba en el anfitrion de la parte del codec de hda.c, con un Realtek ALC257 simulado como el del
 * ThinkPad T480s (topologia de un volcado alsa-info de Linux) y el codec HDMI de Intel en la direccion 2.
 * Comprueba: eleccion del codec analogico, caminos altavoz (0x14 -> DAC 0x02) y auriculares (0x21 -> DAC 0x03, con el
 * selector del pin en la entrada 1), EAPD, amplificador de auriculares, volumen, conector (con auriculares el altavoz
 * calla) y que un codec solo-HDMI se descarta.
 * Se compila con una copia de hda.c en un directorio aparte (ver el --test del parche 172). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "hda.c"

/* ---------- sustitutos del nucleo ---------- */
uint64_t tsc_ticks_per_ms = 1000000;
uint32_t pci_config_read32(uint8_t b, uint8_t s, uint8_t f, uint8_t o) { (void)b; (void)s; (void)f; (void)o; return 0xFFFFFFFF; }
void pci_config_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v) { (void)b; (void)s; (void)f; (void)o; (void)v; }
uint16_t pci_vendor_id(uint8_t b, uint8_t s, uint8_t f) { (void)b; (void)s; (void)f; return 0xFFFF; }
uintptr_t pmm_alloc_frame(void) { return 0; }
uintptr_t pmm_alloc_contig(size_t n) { (void)n; return 0; }
int vmm_map_page(uint64_t v, uint64_t p, uint64_t f) { (void)v; (void)p; (void)f; return -1; }
void kprint(const char *s) { (void)s; }
void kprint_dec(uint32_t v) { (void)v; }
void kprint_hex16(uint16_t v) { (void)v; }
void kprint_hex32(uint32_t v) { (void)v; }
uint64_t timer_get_uptime_ms(void) { static uint64_t t; return t += 1; }
struct tcb *thread_create(const char *n, thread_func_t e, void *a) { (void)n; (void)e; (void)a; return 0; }
void thread_sleep(uint32_t ms) { (void)ms; }
int bootinfo_disabled(const char *f) { (void)f; return 0; }

/* ---------- codec simulado ---------- */
struct w { int type; uint32_t wcap, pincap, cfg, ampout; int nconn; uint8_t conn[4]; int sel, ctl, eapd, power; uint32_t amp_out_set, amp_in_set[4]; };
static struct w alc[0x25], hdmi[0x10];
static int hp_plugged;
static uint32_t verbs_seen;

static void alc_init(void)
{
    memset(alc, 0, sizeof alc);
    for (int n = 0; n < 0x25; n++) alc[n].type = 0xF;            /* "otro" (sin interes) */
    alc[0x02] = (struct w){ .type = 0, .wcap = 0x00000005u | (1u << 2) | (1u << 3), .ampout = 0x00025757u };   /* DAC con amp de salida (0x57 pasos) */
    alc[0x03] = alc[0x02];
    alc[0x08].type = 1; alc[0x09].type = 1;                      /* ADC */
    alc[0x14] = (struct w){ .type = 4, .wcap = (4u << 20) | (1u << 2) | (1u << 8), .pincap = 0x00010014u, .cfg = 0x90170110u, .nconn = 2, .conn = { 0x02, 0x03 } };
    alc[0x21] = (struct w){ .type = 4, .wcap = (4u << 20) | (1u << 2) | (1u << 8), .pincap = 0x0001001Cu, .cfg = 0x04211020u, .nconn = 2, .conn = { 0x02, 0x03 } };
    alc[0x12] = (struct w){ .type = 4, .wcap = 4u << 20, .pincap = 0x00000020u, .cfg = 0x90A60130u };          /* microfono interno (solo entrada) */
    alc[0x19] = (struct w){ .type = 4, .wcap = 4u << 20, .pincap = 0x00003724u, .cfg = 0x04A11040u };          /* microfono del conector */
    alc[0x13] = (struct w){ .type = 4, .wcap = 4u << 20, .pincap = 0x00000010u, .cfg = 0x40000000u };          /* sin conexion fisica */
    alc[0x1E] = (struct w){ .type = 4, .wcap = 4u << 20, .pincap = 0x00000010u, .cfg = 0x411111F0u, .nconn = 1, .conn = { 0x06 } };
    for (int n = 0; n < 0x25; n++) if (alc[n].type != 0xF) alc[n].wcap |= (uint32_t)alc[n].type << 20;
    alc[0x02].wcap = (0u << 20) | (1u << 2) | (1u << 3); alc[0x03].wcap = alc[0x02].wcap;
    memset(hdmi, 0, sizeof hdmi);
    for (int n = 0; n < 0x10; n++) hdmi[n].type = 0xF;
    hdmi[0x02] = (struct w){ .type = 0, .wcap = 0 };
    hdmi[0x05] = (struct w){ .type = 4, .wcap = 4u << 20, .pincap = 0x0B000094u, .cfg = 0x18560010u, .nconn = 1, .conn = { 0x02 } };   /* HDMI/DP */
}

int sim_codec(uint8_t codec, uint32_t verb, uint32_t *resp)
{
    verbs_seen++;
    uint32_t nid = (verb >> 20) & 0x7F, v12 = (verb >> 8) & 0xFFF, p8 = verb & 0xFF, v4 = (verb >> 16) & 0xF;
    uint32_t r = 0;
    struct w *W = codec == 0 ? alc : codec == 2 ? hdmi : 0;
    int nmax = codec == 0 ? 0x25 : 0x10;
    if (!W) { if (resp) *resp = 0; return -1; }                 /* no hay codec en esa direccion */
    if (v12 == 0xF00) {
        if (p8 == 0x00) r = codec == 0 ? 0x10EC0257u : 0x8086280Bu;
        else if (p8 == 0x04) r = nid == 0 ? 0x00010001u : (nid == 1 ? (codec == 0 ? 0x00020023u : 0x0002000Eu) : 0);
        else if (p8 == 0x05) r = nid == 1 ? 1u : 0u;
        else if (nid >= 2 && (int)nid < nmax) {
            struct w *x = &W[nid];
            if (p8 == 0x09) r = x->type == 0xF ? (0xFu << 20) : x->wcap;
            else if (p8 == 0x0C) r = x->pincap;
            else if (p8 == 0x0E) r = (uint32_t)x->nconn;
            else if (p8 == 0x12) r = x->ampout;
        } else if (nid == 1 && p8 == 0x12) r = 0x00025757u;
    } else if (nid >= 2 && (int)nid < nmax) {
        struct w *x = &W[nid];
        if (v12 == 0xF02) { for (int k = 0; k < 4 && (int)p8 + k < x->nconn; k++) r |= (uint32_t)x->conn[p8 + k] << (8 * k); }
        else if (v12 == 0xF1C) r = x->cfg;
        else if (v12 == 0x701) x->sel = (int)p8;
        else if (v12 == 0x707) x->ctl = (int)p8;
        else if (v12 == 0x70C) x->eapd = (int)p8;
        else if (v12 == 0x705) x->power = 1;
        else if (v12 == 0xF09) r = (nid == 0x21 && hp_plugged) ? 0x80000000u : 0;
        else if (v4 == 0x3) { uint32_t pl = verb & 0xFFFF; if (pl & 0x8000) x->amp_out_set = pl; if (pl & 0x4000) x->amp_in_set[(pl >> 8) & 3] = pl; }
    }
    if (resp) *resp = r;
    return 0;
}

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

int main(void)
{
    alc_init();
    /* el codec HDMI (direccion 2) no tiene caminos analogicos */
    CHECK(scan_codec(2) == 0, "codec HDMI descartado");
    /* el ALC257 (direccion 0) */
    int n = scan_codec(0);
    CHECK(n == 2, "dos caminos: altavoz y auriculares");
    CHECK(spk.pin == 0x14 && spk.dac == 0x02 && spk.len == 2 && spk.ci[0] == 0, "altavoz: pin 0x14 -> DAC 0x02");
    CHECK(hp.pin == 0x21 && hp.dac == 0x03 && hp.len == 2 && hp.ci[0] == 1, "auriculares: pin 0x21 -> DAC 0x03 (otro DAC)");
    path_setup(&spk, 0); path_setup(&hp, 1);
    CHECK(alc[0x14].ctl == 0x40 && alc[0x14].eapd == 2, "altavoz habilitado con EAPD");
    CHECK(alc[0x21].ctl == 0xC0 && alc[0x21].eapd == 2, "auriculares habilitados con su amplificador y EAPD");
    CHECK(alc[0x21].sel == 1 && alc[0x14].sel == 0, "selectores de los pines hacia su DAC");
    CHECK(alc[0x02].power && alc[0x03].power && alc[0x14].power && alc[0x21].power, "todo el camino en D0");
    hp_detect = (hp.pincap & (1u << 2)) && !((hp.pincfg >> 8) & 1);
    CHECK(hp_detect, "deteccion del conector disponible");
    ready = 1;
    hda_set_volume(50);
    CHECK((alc[0x02].amp_out_set & 0x7F) == 0x57 / 2 && !(alc[0x02].amp_out_set & 0x80), "volumen 50 %: DAC del altavoz a la mitad");
    CHECK((alc[0x03].amp_out_set & 0x7F) == 0x57 / 2, "volumen 50 %: DAC de los auriculares a la mitad");
    hda_set_volume(0);
    CHECK((alc[0x02].amp_out_set & 0x80) && (alc[0x03].amp_out_set & 0x80), "volumen 0: silencio");
    hda_set_volume(100);
    /* conector */
    hp_plugged = 0; CHECK(hp_sense() == 0, "sin auriculares");
    hp_plugged = 1; CHECK(hp_sense() == 1, "auriculares detectados");
    apply_jack(1);
    CHECK(alc[0x14].ctl == 0 && alc[0x14].eapd == 0, "con auriculares el altavoz calla (pin y EAPD apagados)");
    CHECK(alc[0x21].ctl == 0xC0, "los auriculares siguen activos");
    apply_jack(0);
    CHECK(alc[0x14].ctl == 0x40 && alc[0x14].eapd == 2, "sin auriculares vuelve el altavoz");
    /* formato */
    uint16_t f = 0;
    CHECK(hda_format_for_rate(48000, &f) == 0 && f == 0x0011, "48 kHz, 16 bits, estereo");
    CHECK(hda_format_for_rate(44100, &f) == 0 && f == 0x4011, "44,1 kHz");
    CHECK(hda_format_for_rate(12345, &f) != 0, "frecuencia no soportada");
    /* codec con un solo DAC: los dos caminos lo comparten */
    alc_init(); alc[0x03].type = 0xF; alc[0x03].wcap = 0xFu << 20;
    n = scan_codec(0);
    CHECK(n == 2 && spk.dac == 0x02 && hp.dac == 0x02, "un solo DAC: compartido");
    if (fails) { printf("hda: %d fallos (%u ordenes)\n", fails, verbs_seen); return 1; }
    printf("hda: ALC257 simulado: codec HDMI descartado, altavoz 0x14->0x02, auriculares 0x21->0x03, EAPD, volumen y conector correctos (%u ordenes)\n", verbs_seen);
    return 0;
}
