/* SOMA_177_T480S: prueba en el anfitrion de la parte de ENTRADA (microfono) de hda.c con un Realtek ALC257 simulado:
 * dos ADC (0x08, 0x09) que cuelgan de selectores (0x23, 0x22) con el microfono digital interno (pin 0x12), el microfono del
 * conector (0x19, con polarizacion y "boost") y una entrada de linea sin conexion fisica (0x1A, debe ignorarse).
 * Comprueba: descubrimiento de rutas ADC -> selector -> pin, orden (interno primero), sin duplicados, configuracion de cada
 * ruta (selectores, pin, polarizacion, ganancias), el flujo de captura (formato, etiqueta, DMA) y el apagado del pin al parar.
 * Se compila con un MMIO de memoria (los registros de flujo se comportan como memoria). */
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
struct w { int type; uint32_t wcap, pincap, cfg, ampout, ampin; int nconn; uint8_t conn[8]; int sel, ctl, eapd, power, strm; uint32_t fmt, amp_out_set, amp_in_set[8]; };
static struct w alc[0x25];
static uint32_t verbs_seen;

static void alc_init(void)
{
    memset(alc, 0, sizeof alc);
    for (int n = 0; n < 0x25; n++) alc[n].type = 0xF;
    alc[0x02] = (struct w){ .type = 0, .wcap = (1u << 2) | (1u << 3), .ampout = 0x00025757u };
    alc[0x03] = alc[0x02];
    /* ADC: amplificador de entrada con 0 dB en el paso 0x17 y 0x4C pasos */
    alc[0x08] = (struct w){ .type = 1, .wcap = (1u << 1) | (1u << 3), .ampin = 0x00274C17u, .nconn = 1, .conn = { 0x23 } };
    alc[0x09] = (struct w){ .type = 1, .wcap = (1u << 1) | (1u << 3), .ampin = 0x00274C17u, .nconn = 1, .conn = { 0x22 } };
    /* selectores de captura: 0x12 (DMIC), 0x19 (microfono), 0x1A (linea, sin conexion), 0x1B (otra linea, tambien sin conexion) */
    alc[0x23] = (struct w){ .type = 3, .wcap = (1u << 1) | (1u << 3), .ampin = 0x00274C17u, .nconn = 4, .conn = { 0x18, 0x19, 0x1A, 0x12 } };
    alc[0x22] = alc[0x23];
    alc[0x12] = (struct w){ .type = 4, .pincap = 0x00000020u, .cfg = 0x90A60130u };                              /* microfono digital interno */
    alc[0x19] = (struct w){ .type = 4, .wcap = (1u << 1) | (1u << 3), .pincap = 0x00003724u, .cfg = 0x04A11040u, .ampin = 0x00280300u };   /* microfono del conector, "boost" de 3 pasos */
    alc[0x18] = (struct w){ .type = 4, .pincap = 0x00000020u, .cfg = 0x411111F0u };                              /* sin conexion fisica */
    alc[0x1A] = (struct w){ .type = 4, .pincap = 0x00000020u, .cfg = 0x411111F0u };
    alc[0x14] = (struct w){ .type = 4, .pincap = 0x00010014u, .cfg = 0x90170110u, .nconn = 2, .conn = { 0x02, 0x03 } };   /* altavoz: no es de entrada */
    for (int n = 0; n < 0x25; n++) if (alc[n].type != 0xF) alc[n].wcap |= (uint32_t)alc[n].type << 20;
}

int sim_codec(uint8_t codec, uint32_t verb, uint32_t *resp)
{
    verbs_seen++;
    uint32_t nid = (verb >> 20) & 0x7F, v12 = (verb >> 8) & 0xFFF, p8 = verb & 0xFF, v4 = (verb >> 16) & 0xF;
    uint32_t r = 0;
    if (codec != 0) { if (resp) *resp = 0; return -1; }
    if (v12 == 0xF00) {
        if (p8 == 0x00) r = 0x10EC0257u;
        else if (p8 == 0x04) r = nid == 0 ? 0x00010001u : (nid == 1 ? 0x00020023u : 0);
        else if (p8 == 0x05) r = nid == 1 ? 1u : 0u;
        else if (nid >= 2 && nid < 0x25) {
            struct w *x = &alc[nid];
            if (p8 == 0x09) r = x->type == 0xF ? (0xFu << 20) : x->wcap;
            else if (p8 == 0x0C) r = x->pincap;
            else if (p8 == 0x0D) r = x->ampin;
            else if (p8 == 0x0E) r = (uint32_t)x->nconn;
            else if (p8 == 0x12) r = x->ampout;
        } else if (nid == 1 && p8 == 0x0D) r = 0x00274C17u;
        else if (nid == 1 && p8 == 0x12) r = 0x00025757u;
    } else if (nid >= 2 && nid < 0x25) {
        struct w *x = &alc[nid];
        if (v12 == 0xF02) { for (int k = 0; k < 4 && (int)p8 + k < x->nconn; k++) r |= (uint32_t)x->conn[p8 + k] << (8 * k); }
        else if (v12 == 0xF1C) r = x->cfg;
        else if (v12 == 0x701) x->sel = (int)p8;
        else if (v12 == 0x707) x->ctl = (int)p8;
        else if (v12 == 0x70C) x->eapd = (int)p8;
        else if (v12 == 0x705) x->power = 1;
        else if (v12 == 0x706) x->strm = (int)p8;
        else if (v4 == 0x2) x->fmt = verb & 0xFFFF;
        else if (v4 == 0x3) { uint32_t pl = verb & 0xFFFF; if (pl & 0x8000) x->amp_out_set = pl; if (pl & 0x4000) x->amp_in_set[(pl >> 8) & 7] = pl; }
    }
    if (resp) *resp = r;
    return 0;
}

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint8_t fake_mmio[0x4000], fake_ring[RING_BYTES], fake_bdl[4096];

int main(void)
{
    alc_init();
    cad = 0; afg = 1;
    mic_scan();
    CHECK(ncand == 2, "dos fuentes (la linea sin conexion se ignora, sin duplicados entre los dos ADC)");
    CHECK(cand[0].pin == 0x12 && cand[0].adc == 0x08 && cand[0].len == 3, "primero el microfono interno 0x12 por ADC 0x08 -> selector 0x23");
    CHECK(cand[0].ci[0] == 0 && cand[0].ci[1] == 3, "ADC entrada 0 (0x23); selector entrada 3 (0x12)");
    CHECK(cand[1].pin == 0x19 && cand[1].ci[1] == 1, "segundo el microfono del conector 0x19 (entrada 1 del selector)");
    char d[96]; hda_mic_describe(0, d, sizeof d);
    CHECK(strstr(d, "interno") && strstr(d, "0x12") && strstr(d, "0x08"), "descripcion de la fuente 0");
    hda_mic_describe(1, d, sizeof d);
    CHECK(strstr(d, "conector") && strstr(d, "0x19"), "descripcion de la fuente 1");

    /* ruta del microfono interno */
    in_path_setup(&cand[0]);
    CHECK(alc[0x23].sel == 3, "selector 0x23 hacia el pin 0x12");
    CHECK(alc[0x12].ctl == 0x20, "pin 0x12: entrada habilitada, sin polarizacion (digital)");
    CHECK(alc[0x08].power && alc[0x23].power && alc[0x12].power, "camino en D0");
    CHECK(!(alc[0x23].amp_in_set[3] & 0x80) && (alc[0x23].amp_in_set[3] & 0x7F) == 0x17, "selector: entrada 3 desmuteada a 0 dB");
    CHECK((alc[0x23].amp_in_set[0] & 0x80) && (alc[0x23].amp_in_set[1] & 0x80), "selector: las otras entradas en mute");
    CHECK((alc[0x08].amp_in_set[0] & 0x7F) == 0x17 + (0x4C - 0x17) * 66 / 200, "pin digital sin boost: la ganancia del 66 % sube en el ADC");
    in_path_off(&cand[0]);
    CHECK(alc[0x12].ctl == 0, "pin apagado");

    /* ruta del microfono del conector: polarizacion al 80 % y "boost" */
    mic_gain_pct = 66;
    in_path_setup(&cand[1]);
    CHECK(alc[0x23].sel == 1, "selector 0x23 hacia el pin 0x19");
    CHECK(alc[0x19].ctl == (0x20 | 4), "pin 0x19: entrada + polarizacion 80 %");
    CHECK(!(alc[0x19].amp_in_set[0] & 0x80) && (alc[0x19].amp_in_set[0] & 0x7F) == 66 * 3 / 100, "pin 0x19: boost del 66 % de 3 pasos");
    CHECK((alc[0x08].amp_in_set[0] & 0x7F) == 0x17, "ADC a 0 dB");
    mic_gain_pct = 100; in_gain_apply(&cand[1]);
    CHECK((alc[0x19].amp_in_set[0] & 0x7F) == 3, "ganancia 100 %: boost maximo");
    in_path_off(&cand[1]);

    /* flujo de captura con un MMIO de memoria */
    mmio = fake_mmio; ready = 1; csd_base = 0x80;
    cring = fake_ring; cring_phys = (uintptr_t)fake_ring; cbdl = fake_bdl; cbdl_phys = (uintptr_t)fake_bdl;
    cand_sel = 0;
    CHECK(hda_mic_ready(), "micrófono listo");
    CHECK(hda_cap_start(7) != 0, "frecuencia no valida rechazada");
    CHECK(hda_cap_start(16000) == 0 && cap_running, "captura a 16 kHz iniciada");
    CHECK(alc[0x08].fmt == 0x0211 && alc[0x08].strm == 0x20, "ADC: formato 16 kHz/16 bits/estereo y flujo 2 canal 0");
    CHECK(*(uint16_t *)(fake_mmio + 0x80 + SD_FMT) == 0x0211, "descriptor de flujo: formato");
    CHECK(*(uint32_t *)(fake_mmio + 0x80 + SD_CBL) == RING_BYTES && *(uint16_t *)(fake_mmio + 0x80 + SD_LVI) == 7, "descriptor: anillo de 128 KiB y 8 entradas");
    CHECK((fake_mmio[0x80 + SD_CTL] & 2) && (fake_mmio[0x80 + SD_CTL + 2] >> 4) == CAP_TAG, "flujo en marcha con su etiqueta");
    CHECK(alc[0x12].ctl == 0x20, "pin interno encendido al capturar");
    uint32_t *e = (uint32_t *)fake_bdl;
    CHECK(e[2] == BDL_ENTRY && e[16 / 4 * 7 + 2] == BDL_ENTRY, "lista de descriptores (8 x 16 KiB)");
    CHECK(hda_mic_select(1) == 0 && cap_running && alc[0x12].ctl == 0 && alc[0x19].ctl == (0x20 | 4), "cambiar de fuente en caliente: apaga una y enciende otra");
    CHECK(alc[0x09].fmt == 0 && alc[0x09].strm == 0 && alc[0x08].strm == 0x20, "el otro ADC (0x09) no se toca; el 0x08 sigue con su flujo");
    hda_cap_stop();
    CHECK(!cap_running && !(fake_mmio[0x80 + SD_CTL] & 2) && alc[0x19].ctl == 0, "parar: flujo y pin apagados");
    CHECK(hda_mic_select(5) != 0 && hda_mic_select(-1) != 0, "fuentes fuera de rango rechazadas");
    hda_mic_set_gain(150); CHECK(hda_mic_get_gain() == 100, "ganancia acotada a 100");
    hda_mic_set_gain(-5);  CHECK(hda_mic_get_gain() == 0, "ganancia acotada a 0");

    /* un codec sin ADC no ofrece fuentes */
    alc_init(); alc[0x08].type = 0xF; alc[0x08].wcap = 0xFu << 20; alc[0x09].type = 0xF; alc[0x09].wcap = 0xFu << 20;
    mic_scan();
    CHECK(ncand == 0 && hda_mic_ready() == 0, "sin ADC: sin microfono");
    if (fails) { printf("mic: %d fallos (%u ordenes)\n", fails, verbs_seen); return 1; }
    printf("mic: ALC257 simulado: 2 fuentes (interno 0x12, conector 0x19), rutas, polarizacion, boost, flujo de captura y apagado correctos (%u ordenes)\n", verbs_seen);
    return 0;
}
