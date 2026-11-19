/* SOMA_168_T480S: brillo del panel y teclas especiales del ThinkPad.
 *
 * Brillo: PWM del chipset en la grafica Intel (ver backlight_core.h). Se proyecta solo la pagina de los registros de la
 * PWM (BAR0 + 0xC8000), sin cache. Solo se escribe el ciclo de trabajo, nunca el periodo ni la activacion.
 *
 * Teclas (todas pasan por hotkey_post, que solo anota; el hilo "teclas" actua):
 *   - volumen (Fn+F1/F2/F3 en el T480s): el teclado manda los codigos E0 20 / E0 2E / E0 30 (console.c);
 *   - brillo (Fn+F5/F6): el EC levanta SCI_EVT, SOMA hace la consulta QR_EC y ejecuta el metodo _Qxx del firmware,
 *     que avisa al dispositivo de video (Notify 0x86/0x87) y/o al dispositivo HKEY del ThinkPad (eventos 0x1010/0x1011);
 *     acpi_sys.c entrega los dos y aqui se toma solo una de las fuentes para no subir dos pasos;
 *   - silencio del microfono (Fn+F4, HKEY 0x101B): aviso en pantalla;
 *   - tapa: cerrada apaga la luz del panel, abierta la devuelve (acpi_sys.c llama a backlight_blank).
 * Interruptores: soma.brillo=off (no toca la PWM), soma.hkey=off (no activa el HKEY), modo seguro. */
#include <stdint.h>
#include "backlight.h"
#include "backlight_core.h"
#include "acpi_sys.h"
#include "pci.h"
#include "console.h"
#include "thread.h"
#include "idt.h"
#include "fs.h"
#include "bootfb.h"
#include "hda.h"
#include "clipboard.h"

static struct backlight_info g;
static volatile uint32_t *regs;            /* BAR0 + 0xC8000 */
static uint32_t saved_duty;                /* antes de apagar por la tapa */
static volatile uint32_t pend[HK_OTHER + 1];
static volatile int save_due; static uint64_t save_at;
static uint32_t recent_code[8]; static int recent_kind[8], recent_n;
static int vol_before_mute = -1;

static inline uint32_t rd(uint32_t off) { return regs[(off - 0xC8000u) / 4]; }
static inline void wr(uint32_t off, uint32_t v) { regs[(off - 0xC8000u) / 4] = v; }

void hotkey_post(int code) { if (code > 0 && code <= HK_OTHER) { __sync_fetch_and_add(&pend[code], 1); if (code != HK_OTHER) __sync_fetch_and_add(&g.key_events, 1); } }
void hotkey_note_source(int kind, uint32_t code)
{
    uint64_t f; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    if (kind == 1) g.ec_queries++; else if (kind == 2) g.hkey_events++; else if (kind == 3) { g.video_events++; g.src_video = 1; }
    for (int i = 7; i > 0; i--) { recent_code[i] = recent_code[i - 1]; recent_kind[i] = recent_kind[i - 1]; }
    recent_code[0] = code; recent_kind[0] = kind; if (recent_n < 8) recent_n++;
    if (f & 0x200) __asm__ volatile ("sti" ::: "memory");
}
/* Una pulsacion de Fn+brillo puede llegar dos veces (aviso de video y evento HKEY, del mismo metodo _Qxx): si la otra
 * fuente acaba de dar la misma tecla hace menos de 60 ms, es la misma pulsacion y no se repite */
void hotkey_brightness(int src, int up)
{
    static uint64_t last_t; static int last_src, last_up;
    uint64_t now = timer_get_uptime_ms();
    if (last_t && now - last_t < 60 && last_src != src && last_up == up) return;
    last_t = now; last_src = src; last_up = up;
    hotkey_post(up ? HK_BRIGHT_UP : HK_BRIGHT_DOWN);
}
void hotkey_recent(uint32_t *codes, int *kinds, int *n)
{
    for (int i = 0; i < recent_n; i++) { codes[i] = recent_code[i]; kinds[i] = recent_kind[i]; }
    *n = recent_n;
}

int backlight_init(void)
{
    g.why = "";
    if (bootinfo_disabled("brillo")) { g.why = "desactivado (soma.brillo=off o modo seguro)"; return -1; }
    g.gpu_vendor = pci_vendor_id(0, 2, 0); g.gpu_device = pci_device_id(0, 2, 0);
    uint32_t cls = pci_config_read32(0, 2, 0, 0x08) >> 16;
    if (g.gpu_vendor != 0x8086 || (cls >> 8) != 0x03) { g.why = "no hay una grafica Intel en 00:02.0 (QEMU, maquina virtual)"; return -1; }
    if (!(pci_config_read32(0, 2, 0, 0x04) & 2)) { g.why = "la grafica no tiene la memoria activada"; return -1; }
    uint32_t lo = pci_config_read32(0, 2, 0, 0x10);
    if (lo & 1) { g.why = "BAR0 de la grafica no es de memoria"; return -1; }
    uint64_t bar = lo & ~0xFULL;
    if (((lo >> 1) & 3) == 2) bar |= (uint64_t)pci_config_read32(0, 2, 0, 0x14) << 32;
    g.bar = bar;
    if (!bar) { g.why = "BAR0 de la grafica sin asignar"; return -1; }
    void *p = acpi_sys_map_mmio(bar + 0xC8000u, 0x1000);
    if (!p) { g.why = "no se pudo proyectar la pagina de la PWM"; return -1; }
    regs = (volatile uint32_t *)p;
    g.ctl1 = rd(BL_CTL1); g.ctl2 = rd(BL_CTL2);
    const char *bad = bl_regs_check(g.ctl1, g.ctl2);
    if (bad) { g.why = bad; regs = 0; return -1; }
    g.max = bl_max_of(g.ctl2); g.duty = bl_duty_of(g.ctl2); g.pct = bl_pct_for(g.duty, g.max);
    g.ok = 1; g.why = "PWM del chipset (PCH)";
    return 0;
}

static int set_duty(uint32_t duty)
{
    uint32_t c2 = rd(BL_CTL2);
    if (bl_regs_check(rd(BL_CTL1), c2)) return -1;               /* algo cambio (otro control, la grafica se apago) */
    wr(BL_CTL2, bl_ctl2_with(c2, duty));
    g.ctl2 = rd(BL_CTL2); g.duty = bl_duty_of(g.ctl2);
    return g.duty == (duty & 0xFFFFu) ? 0 : -1;
}
int backlight_set_pct(uint32_t pct, int save)
{
    if (!g.ok) return -1;
    if (pct > 100) pct = 100;
    if (g.blanked) { g.blanked = 0; }
    int r = set_duty(bl_duty_for(pct, g.max));
    if (r == 0) g.pct = pct;
    if (save) { save_due = 1; save_at = timer_get_uptime_ms() + 2000; }
    return r;
}
int backlight_blank(int off)
{
    if (!g.ok) return -1;
    if (off && !g.blanked) { saved_duty = g.duty; if (set_duty(0) == 0) g.blanked = 1; return 0; }
    if (!off && g.blanked) { g.blanked = 0; return set_duty(saved_duty ? saved_duty : bl_duty_for(g.pct, g.max)); }
    return 0;
}
void backlight_get(struct backlight_info *o)
{
    if (g.ok) { g.ctl1 = rd(BL_CTL1); g.ctl2 = rd(BL_CTL2); g.duty = bl_duty_of(g.ctl2); }
    *o = g;
}

static void bar_text(char *b, const char *name, uint32_t pct)
{
    int k = 0; while (*name) b[k++] = *name++;
    b[k++] = ' ';
    if (pct >= 100) { b[k++] = '1'; b[k++] = '0'; b[k++] = '0'; }
    else { if (pct >= 10) b[k++] = (char)('0' + pct / 10); b[k++] = (char)('0' + pct % 10); }
    b[k++] = ' '; b[k++] = '%'; b[k++] = ' '; b[k++] = ' '; b[k++] = '[';
    for (uint32_t i = 0; i < 20; i++) b[k++] = i * 5 < pct ? '#' : '-';
    b[k++] = ']'; b[k] = 0;
}
static uint32_t take(int code) { return __sync_lock_test_and_set(&pend[code], 0); }

static void hotkey_thread(void *a)
{
    (void)a;
    if (g.ok) {                                                     /* el ultimo brillo guardado */
        char b[32]; int n = vfs_read("/etc/brillo.conf", b, sizeof b - 1);
        if (n > 0) {
            b[n] = 0; const char *s = b; while (*s && (*s < '0' || *s > '9')) s++;
            uint32_t v = 0; int dig = 0; while (*s >= '0' && *s <= '9' && dig < 4) { v = v * 10 + (uint32_t)(*s++ - '0'); dig++; }
            if (dig && v <= 100) backlight_set_pct(v, 0);
        }
    }
    for (;;) {
        thread_sleep(20);
        char t[64];
        int br = (int)take(HK_BRIGHT_UP) - (int)take(HK_BRIGHT_DOWN);
        if (br) {
            if (g.ok) {
                uint32_t p = g.pct;
                for (int i = 0; i < (br > 0 ? br : -br) && i < 4; i++) p = bl_step(p, br > 0 ? 1 : -1);
                backlight_set_pct(p, 1);
                bar_text(t, "Brillo", g.pct); clip_toast(t);
            } else clip_toast("Brillo: SOMA no controla el brillo de esta pantalla (orden brillo)");
        }
        int vol = (int)take(HK_VOL_UP) - (int)take(HK_VOL_DOWN);
        uint32_t mute = take(HK_MUTE);
        if (vol || mute) {
            if (!hda_ready()) clip_toast("Volumen: no hay salida de audio");
            else {
                int v = hda_get_volume();
                if (mute & 1) { if (v > 0) { vol_before_mute = v; v = 0; } else v = vol_before_mute > 0 ? vol_before_mute : 50; }
                v += vol * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                if (vol && v > 0) vol_before_mute = -1;
                hda_set_volume(v);
                if (v == 0) clip_toast("Volumen: silencio"); else { bar_text(t, "Volumen", (uint32_t)v); clip_toast(t); }
            }
        }
        if (take(HK_MIC_MUTE)) { clip_toast("Microfono: SOMA aun no graba sonido (nada que silenciar)"); }
        take(HK_OTHER);
        if (save_due && timer_get_uptime_ms() >= save_at) {
            save_due = 0;
            char o[16]; int k = 0; const char *h = "brillo="; while (*h) o[k++] = *h++;
            uint32_t p = g.pct; if (p >= 100) { o[k++] = '1'; o[k++] = '0'; o[k++] = '0'; } else { if (p >= 10) o[k++] = (char)('0' + p / 10); o[k++] = (char)('0' + p % 10); }
            o[k++] = '\n';
            vfs_write("/etc/brillo.conf", o, (uint32_t)k);
        }
    }
}
void backlight_start(void) { thread_create("teclas", hotkey_thread, 0); }
