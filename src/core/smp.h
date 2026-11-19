#ifndef SOMA_SMP_H
#define SOMA_SMP_H
/* PARCHE 082: varios nucleos. Modelo asimetrico y seguro para un nucleo de sistema que no tiene cerrojos en todas partes:
 *  - el nucleo 0 sigue ejecutando todo el sistema (hilos, interrupciones, red, escritorio) exactamente como antes;
 *  - los nucleos 1..N-1 arrancan por PSCI, se quedan dormidos (wfe) y solo ejecutan trabajos de calculo que les manda el nucleo 0.
 * Un trabajo NO debe llamar a kprint, kmalloc, red, ficheros ni nada que no sea calcular sobre su propio trozo de memoria.
 * En x86_64 (SOMA_159_T480S) los secundarios arrancan con INIT-SIPI por el LAPIC y duermen con hlt; mpidr = ID del LAPIC. */
#include <stdint.h>

typedef void (*smp_fn)(int idx, int n, void *arg);
void smp_init(void);                     /* arranca los secundarios (una vez) e informa */
int  smp_cpus(void);                     /* nucleos disponibles para smp_parallel (>= 1) */
int  smp_enabled(void);                  /* 0 si el usuario lo apago con 'cpus 1' */
void smp_set_enabled(int on);
/* Ejecuta fn(i, n, arg) para i = 0..n-1 repartido entre los nucleos (i = 0 en el nucleo 0) y espera a que terminen todos. */
void smp_parallel(smp_fn fn, void *arg);
/* Igual pero con n trozos concretos (n <= smp_cpus()); devuelve los trozos usados. */
int  smp_parallel_n(smp_fn fn, void *arg, int n);
int  smp_busy(void);                     /* SOMA_221_RESCATE: 1 = hay un reparto en curso (x86_64) */
/* Informacion para 'cpus' */
struct smp_core_info { int online; uint32_t mpidr; uint64_t jobs; uint64_t busy_ticks; };
int  smp_core_info(int i, struct smp_core_info *o);
const char *smp_method(void);
#ifdef __x86_64__
const char *smp_why(void);               /* SOMA_159: por que no hay mas nucleos (o "activo") */
int smp_found(void);                     /* procesadores que declara el firmware (MADT) */
uint64_t smp_core_wakes(int i);          /* veces que el secundario i desperto de hlt */
uint32_t smp_core_boot_ms(int i);        /* lo que tardo en arrancar */
uint32_t smp_tramp_addr(void);
#endif
#endif
