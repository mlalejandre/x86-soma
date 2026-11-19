/* SOMA_166_T480S: energia y temperatura del procesador (Intel, como el i5-8350U del T480s). Referencia: Linux
 * drivers/idle/intel_idle.c, drivers/cpufreq/intel_pstate.c, drivers/hwmon/coretemp.c, drivers/powercap/intel_rapl.c.
 *  - Reposo: MWAIT con un estado C profundo (C6 por defecto) en vez de HLT (C1), en el hilo inactivo del nucleo 0 y en los
 *    nucleos de calculo. Sin ARAT (el temporizador del LAPIC se pararia en C3+), el nucleo 0 se queda en C1.
 *  - HWP / Speed Shift: el procesador elige la frecuencia (con turbo) segun una preferencia (EPP): rendimiento, equilibrado o
 *    ahorro. Sin HWP el firmware deja una frecuencia fija.
 *  - Lecturas: temperatura de cada nucleo y del paquete (sensor digital), frecuencia real (APERF/MPERF) y consumo del
 *    paquete, de los nucleos y de la grafica (RAPL).
 * Todos los MSR se leen con msr_read_safe: en QEMU o en otra CPU, lo que no existe se da por no disponible (sin colgar).
 * Interruptores: soma.hwp=off, soma.mwait=off. Perfil guardado en /etc/energia.conf. */
#include <stdint.h>
#include "cpupower.h"
#include "msr.h"
#include "smp.h"
#include "apic.h"
#include "console.h"
#include "sysinfo.h"
#include "bootfb.h"
#include "fs.h"

#define MSR_APERF 0xE8
#define MSR_MPERF 0xE7
#define MSR_THERM_STATUS 0x19C
#define MSR_TEMP_TARGET 0x1A2
#define MSR_PKG_THERM_STATUS 0x1B1
#define MSR_PM_ENABLE 0x770
#define MSR_HWP_CAPS 0x771
#define MSR_HWP_REQUEST 0x774
#define MSR_RAPL_UNIT 0x606
#define MSR_PKG_ENERGY 0x611
#define MSR_PP0_ENERGY 0x639
#define MSR_PP1_ENERGY 0x641
#define MSR_PKG_CST_CONFIG 0xE2

static struct cpupower_info g;
static volatile uint32_t g_hint_bsp, g_hint_ap;             /* pistas de MWAIT */
static volatile int g_mwait_ok;
static volatile uint8_t g_mon_dummy[64] __attribute__((aligned(64)));

static void cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    __asm__ volatile ("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "0"(leaf), "2"(sub));
}
static inline uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }

/* ------------------------------------------------------------------ reposo */
static inline void mwait_now(volatile void *addr, uint32_t hint)
{
    __asm__ volatile ("monitor" : : "a"(addr), "c"(0), "d"(0));
    __asm__ volatile ("sti\n mwait" : : "a"(hint), "c"(0) : "memory");
}
/* Nucleo 0 (hilo inactivo): interrupciones abiertas al salir */
void cpu_idle(void)
{
    if (g_mwait_ok) mwait_now(g_mon_dummy, g_hint_bsp);
    else __asm__ volatile ("sti\n hlt" ::: "memory");
}
/* Nucleos de calculo: vigila su numero de secuencia (una escritura del nucleo 0 ya los despierta, ademas de la IPI) y
 * duerme si sigue sin trabajo. Entra y sale con las interrupciones cerradas. */
void cpu_idle_ap(volatile uint32_t *seq, uint32_t last)
{
    if (g_mwait_ok) {
        __asm__ volatile ("monitor" : : "a"(seq), "c"(0), "d"(0));
        if (__atomic_load_n(seq, __ATOMIC_ACQUIRE) == last) __asm__ volatile ("sti\n mwait\n cli" : : "a"(g_hint_ap), "c"(0) : "memory");
    } else if (__atomic_load_n(seq, __ATOMIC_ACQUIRE) == last) __asm__ volatile ("sti\n hlt\n cli" ::: "memory");
}

/* ------------------------------------------------------------------ HWP y perfiles */
static const char *pname[3] = { "rendimiento", "equilibrado", "ahorro" };
static void hwp_apply_one(int idx, int n, void *arg)
{
    (void)idx; (void)n; (void)arg;
    if (!g.hwp_on) return;
    uint32_t lo = g.hwp_lowest, hi = g.profile == CP_AHORRO ? g.hwp_guaranteed : g.hwp_highest;
    uint32_t epp = g.profile == CP_RENDIMIENTO ? 0x00 : g.profile == CP_EQUILIBRADO ? 0x80 : 0xC0;
    uint64_t req = (uint64_t)lo | ((uint64_t)hi << 8) | (0ULL << 16) | ((uint64_t)(g.epp_ok ? epp : 0) << 24);
    (void)msr_write_safe(MSR_HWP_REQUEST, req);
}
static void apply_profile(void)
{
    /* reposo: rendimiento = C1 (despertar inmediato), equilibrado/ahorro = el estado profundo elegido */
    g_hint_bsp = (g.profile == CP_RENDIMIENTO || !g.arat) ? 0x00 : g.deep_hint;
    g_hint_ap = g.profile == CP_RENDIMIENTO ? 0x00 : g.deep_hint;
    if (g.hwp_on) smp_parallel_n(hwp_apply_one, 0, 64);
}
int cpupower_set_profile(int p, int save)
{
    if (p < 0 || p > 2) return -1;
    g.profile = p;
    apply_profile();
    if (save) { const char *s = pname[p]; uint32_t n = 0; while (s[n]) n++; vfs_write("/etc/energia.conf", s, n); }
    return 0;
}
const char *cpupower_profile_name(int p) { return (p >= 0 && p <= 2) ? pname[p] : "?"; }

/* ------------------------------------------------------------------ arranque */
void cpupower_init(void)
{
    uint32_t r[4];
    {   /* autoprueba de los MSR seguros: este registro no existe en ninguna CPU -> #GP -> debe volver con error, sin panico */
        uint64_t x = 0x1234;
        g.msr_unknown_gp = msr_read_safe(0xDEAD0000u, &x) == -1 && x == 0x1234;
        /* el mecanismo en si (por si el emulador no da #GP con un MSR desconocido): una lectura de una direccion no canonica
         * siempre da #GP; con la misma tabla de arreglos debe volver con error */
        uint64_t err, val = 0;
        __asm__ volatile ("xorl %%esi, %%esi\n movabsq $0x8000000000000000, %%rdx\n"
                          "1: movq (%%rdx), %%rax\n"
                          "2:\n"
                          ".pushsection msr_fix, \"a\"\n .balign 8\n .quad 1b, 2b\n .popsection"
                          : "=S"(err), "=a"(val) : : "rdx", "memory");
        g.msr_fix_ok = err == 1;
    }
    cpuid(0, 0, r);
    g.intel = r[1] == 0x756E6547u && r[3] == 0x49656E69u && r[2] == 0x6C65746Eu;   /* GenuineIntel */
    uint32_t maxl = r[0];
    cpuid(1, 0, r);
    int monitor = (r[2] >> 3) & 1;
    /* bajo un hipervisor MWAIT puede no dormir (QEMU TCG con varios procesadores lo trata como una instruccion vacia: los
     * nucleos de calculo girarian sin parar); Linux tampoco lo usa ahi. soma.mwait=on lo fuerza. */
    g.hypervisor = (r[2] >> 31) & 1;
    if (g.hypervisor && !bootinfo_has_option("soma.mwait=on")) monitor = 0;
    if (maxl >= 6) {
        cpuid(6, 0, r);
        g.dts = r[0] & 1; g.arat = (r[0] >> 2) & 1; g.ptm = (r[0] >> 6) & 1; g.hwp = (r[0] >> 7) & 1; g.epp_ok = (r[0] >> 10) & 1;
        g.aperf = r[2] & 1;
    }
    /* MWAIT: estados C disponibles (hoja 5: subestados por estado C en EDX, 4 bits cada uno) */
    g.deep_hint = 0x00; g.deep_c = 1;
    if (monitor && maxl >= 5 && !bootinfo_disabled("mwait")) {
        cpuid(5, 0, r);
        if ((r[2] & 3) == 3) {                                       /* extensiones + interrupcion despierta */
            uint32_t edx = r[3];
            /* indice n del campo = pista (n << 4). En Skylake/Kaby Lake: 1 = C1, 2 = C3, 3 = C6, 4 = C7s, 5 = C8... (intel_idle) */
            static const uint8_t cname[8] = { 0, 1, 3, 6, 7, 8, 9, 10 };
            int want = 3;                                            /* C6: salida ~85 us, buena con el tic de 1 ms */
            for (int n = want; n >= 1; n--) if ((edx >> (4 * n)) & 15) { g.deep_hint = (uint32_t)((n - 1) << 4); g.deep_c = cname[n]; break; }
            g_mwait_ok = 1; g.mwait = 1;
        }
    }
    /* temperatura */
    uint64_t v;
    g.tjmax = 100;
    if (g.dts && msr_read_safe(MSR_TEMP_TARGET, &v) == 0 && ((v >> 16) & 0xFF) >= 60) g.tjmax = (int)((v >> 16) & 0xFF);
    if (g.dts && msr_read_safe(MSR_THERM_STATUS, &v) != 0) g.dts = 0;
    if (g.ptm && msr_read_safe(MSR_PKG_THERM_STATUS, &v) != 0) g.ptm = 0;
    if (g.aperf && msr_read_safe(MSR_APERF, &v) != 0) g.aperf = 0;
    /* RAPL */
    if (g.intel && msr_read_safe(MSR_RAPL_UNIT, &v) == 0 && msr_read_safe(MSR_PKG_ENERGY, &v) == 0) {
        uint64_t u = 0; msr_read_safe(MSR_RAPL_UNIT, &u);
        g.rapl = 1; g.energy_shift = (uint32_t)((u >> 8) & 0x1F);
        g.rapl_pp0 = msr_read_safe(MSR_PP0_ENERGY, &v) == 0;
        g.rapl_pp1 = msr_read_safe(MSR_PP1_ENERGY, &v) == 0;
    }
    /* HWP */
    if (g.hwp && !bootinfo_disabled("hwp")) {
        if (msr_read_safe(MSR_PM_ENABLE, &v) == 0) {
            if (!(v & 1)) msr_write_safe(MSR_PM_ENABLE, 1);          /* se queda activo hasta reiniciar (asi lo define Intel) */
            if (msr_read_safe(MSR_PM_ENABLE, &v) == 0 && (v & 1) && msr_read_safe(MSR_HWP_CAPS, &v) == 0) {
                g.hwp_on = 1;
                g.hwp_highest = (uint32_t)(v & 0xFF); g.hwp_guaranteed = (uint32_t)((v >> 8) & 0xFF);
                g.hwp_efficient = (uint32_t)((v >> 16) & 0xFF); g.hwp_lowest = (uint32_t)((v >> 24) & 0xFF);
            }
        }
    }
    if (msr_read_safe(MSR_PKG_CST_CONFIG, &v) == 0) { g.pkg_cst_limit = (uint32_t)(v & 0xF); g.pkg_cst_known = 1; }
    /* perfil guardado */
    g.profile = CP_EQUILIBRADO;
    char b[24]; int n = vfs_read("/etc/energia.conf", b, sizeof b - 1);
    if (n > 0) { b[n] = 0; if (b[0] == 'r') g.profile = CP_RENDIMIENTO; else if (b[0] == 'a') g.profile = CP_AHORRO; }
    apply_profile();
    g.ready = 1;
}
const struct cpupower_info *cpupower_info(void) { return &g; }

/* ------------------------------------------------------------------ lecturas */
struct core_sample { uint64_t aperf, mperf, tsc; int temp; int ok; };
static struct core_sample s_now[64];
static void sample_one(int idx, int n, void *arg)
{
    (void)n; (void)arg;
    if (idx < 0 || idx >= 64) return;
    struct core_sample *c = &s_now[idx];
    uint64_t v; c->ok = 1;
    c->tsc = rdtsc_();
    if (g.aperf) { msr_read_safe(MSR_APERF, &c->aperf); msr_read_safe(MSR_MPERF, &c->mperf); }
    c->temp = -1000;
    if (g.dts && msr_read_safe(MSR_THERM_STATUS, &v) == 0 && (v >> 31) & 1) c->temp = g.tjmax - (int)((v >> 16) & 0x7F);
}
static int take(struct core_sample *out, int max)
{
    int n = smp_cpus(); if (n > max) n = max; if (n > 64) n = 64;
    for (int i = 0; i < n; i++) s_now[i].ok = 0;
    int used = smp_parallel_n(sample_one, 0, n);
    for (int i = 0; i < used; i++) out[i] = s_now[i];
    return used;
}
static uint64_t energy_raw(uint32_t msr) { uint64_t v = 0; msr_read_safe(msr, &v); return v & 0xFFFFFFFFULL; }
static uint32_t energy_mw(uint64_t e0, uint64_t e1, uint64_t ms)   /* contador de 32 bits que da la vuelta */
{
    uint64_t d = (e1 - e0) & 0xFFFFFFFFULL;
    if (!ms) return 0;
    return (uint32_t)(((d * 1000000ULL) >> g.energy_shift) / ms);  /* (d / 2^shift) J por (ms/1000) s, en mW */
}

int cpupower_measure(struct cpupower_sample *o, uint32_t ms)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    if (!g.ready) return -1;
    static struct core_sample a[64], b[64];
    uint64_t p0 = g.rapl ? energy_raw(MSR_PKG_ENERGY) : 0, c0 = g.rapl_pp0 ? energy_raw(MSR_PP0_ENERGY) : 0, x0 = g.rapl_pp1 ? energy_raw(MSR_PP1_ENERGY) : 0;
    uint64_t t0 = rdtsc_();
    int n = take(a, 64);
    extern void thread_sleep(uint32_t);
    thread_sleep(ms);
    int m = take(b, 64);
    if (m < n) n = m;
    uint64_t dt = rdtsc_() - t0, tpm = tsc_ticks_per_ms ? tsc_ticks_per_ms : 1;
    uint64_t real_ms = dt / tpm; if (!real_ms) real_ms = 1;
    o->ncores = n;
    for (int i = 0; i < n && i < CP_MAXCORE; i++) {
        uint64_t da = b[i].aperf - a[i].aperf, dm = b[i].mperf - a[i].mperf, dtsc = b[i].tsc - a[i].tsc;
        o->temp[i] = b[i].temp;
        if (g.aperf && dm) { o->mhz[i] = (uint32_t)((uint64_t)tsc_freq_mhz * da / dm); o->busy_pm[i] = dtsc ? (uint32_t)(dm * 1000 / dtsc) : 0; }
        if (o->busy_pm[i] > 1000) o->busy_pm[i] = 1000;
        if (b[i].temp > o->temp_max) o->temp_max = b[i].temp;
    }
    o->pkg_temp = -1000;
    uint64_t v;
    if (g.ptm && msr_read_safe(MSR_PKG_THERM_STATUS, &v) == 0 && (v >> 31) & 1) o->pkg_temp = g.tjmax - (int)((v >> 16) & 0x7F);
    if (g.rapl) {
        o->pkg_mw = energy_mw(p0, energy_raw(MSR_PKG_ENERGY), real_ms);
        if (g.rapl_pp0) o->cores_mw = energy_mw(c0, energy_raw(MSR_PP0_ENERGY), real_ms);
        if (g.rapl_pp1) o->gfx_mw = energy_mw(x0, energy_raw(MSR_PP1_ENERGY), real_ms);
    }
    o->ms = (uint32_t)real_ms;
    return 0;
}

/* Lectura rapida para el Monitor (cada segundo, sin esperar): diferencias desde la llamada anterior, solo el nucleo 0 y el
 * paquete (no despierta a los demas). */
int cpupower_live(int *temp_c, uint32_t *mhz, uint32_t *pkg_mw)
{
    *temp_c = -1000; *mhz = 0; *pkg_mw = 0;
    if (!g.ready) return -1;
    static uint64_t la, lm, le, lt; static int first = 1;
    uint64_t v, a = 0, m = 0, e = g.rapl ? energy_raw(MSR_PKG_ENERGY) : 0, t = rdtsc_();
    if (g.ptm && msr_read_safe(MSR_PKG_THERM_STATUS, &v) == 0 && (v >> 31) & 1) *temp_c = g.tjmax - (int)((v >> 16) & 0x7F);
    else if (g.dts && msr_read_safe(MSR_THERM_STATUS, &v) == 0 && (v >> 31) & 1) *temp_c = g.tjmax - (int)((v >> 16) & 0x7F);
    if (g.aperf) { msr_read_safe(MSR_APERF, &a); msr_read_safe(MSR_MPERF, &m); }
    if (!first) {
        uint64_t tpm = tsc_ticks_per_ms ? tsc_ticks_per_ms : 1, ms = (t - lt) / tpm;
        if (g.aperf && m > lm) *mhz = (uint32_t)((uint64_t)tsc_freq_mhz * (a - la) / (m - lm));
        if (g.rapl && ms) *pkg_mw = energy_mw(le, e, ms);
    }
    la = a; lm = m; le = e; lt = t; first = 0;
    return (*temp_c > -1000 || *mhz) ? 0 : -1;
}
