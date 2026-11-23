/* Soporte de pruebas en host: captura de consola y entrada simulada. */
#ifndef SOMA_HOST_STUBS_H
#define SOMA_HOST_STUBS_H
#include <stdint.h>

void        host_out_reset(void);
const char *host_out(void);
void        host_in_set(const char *lines);   /* texto que devolvera kgetline/kgetc */
void        host_time_set(uint64_t ms);
void        host_agent_mode(int on);          /* simula hilo bajo politica del Agente */
void        host_disk_reset(void);            /* RAM-disk de 16 MiB a cero */
int         host_disk_writes(void);
int         host_disk_reads(void);            /* SOMA_269_RAM: lecturas de sectores hechas al disco */
void        host_disk_fail_after(int n_writes); /* -1 = nunca falla */
int         host_disk_save(const char *path); /* persistir el RAM-disk (simula reinicio) */
int         host_disk_load(const char *path);
void        host_pmm_fail_after(int n_allocs);  /* tras n marcos, el PMM devuelve 0; -1 = nunca */
int         host_pmm_frames_live(void);
#endif
