#ifndef SOMA_CPUPOWER_H
#define SOMA_CPUPOWER_H
/* SOMA_166_T480S: energia y temperatura del procesador (ver cpupower.c) */
#include <stdint.h>
enum { CP_RENDIMIENTO = 0, CP_EQUILIBRADO = 1, CP_AHORRO = 2 };
#define CP_MAXCORE 16
struct cpupower_info {
    int ready, intel, dts, ptm, arat, aperf, hwp, epp_ok, hwp_on, mwait, rapl, rapl_pp0, rapl_pp1, pkg_cst_known;
    int tjmax, deep_c, profile, msr_fix_ok, msr_unknown_gp, hypervisor;
    uint32_t deep_hint, energy_shift, hwp_highest, hwp_guaranteed, hwp_efficient, hwp_lowest, pkg_cst_limit;
};
struct cpupower_sample {
    int ncores, temp[CP_MAXCORE], temp_max, pkg_temp;           /* grados; -1000 = no disponible */
    uint32_t mhz[CP_MAXCORE], busy_pm[CP_MAXCORE];             /* frecuencia real; tiempo despierto (por mil) */
    uint32_t pkg_mw, cores_mw, gfx_mw, ms;
};
void cpupower_init(void);                         /* tras smp_init y el sistema de archivos */
const struct cpupower_info *cpupower_info(void);
int  cpupower_set_profile(int p, int save);
const char *cpupower_profile_name(int p);
int  cpupower_measure(struct cpupower_sample *o, uint32_t ms);     /* espera ms y mide (todos los nucleos) */
int  cpupower_live(int *temp_c, uint32_t *mhz, uint32_t *pkg_mw);  /* lectura rapida (Monitor) */
void cpu_idle(void);                               /* hilo inactivo del nucleo 0 */
void cpu_idle_ap(volatile uint32_t *seq, uint32_t last);
#endif
