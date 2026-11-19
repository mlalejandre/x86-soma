#ifndef MYOS_PMM_H
#define MYOS_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PMM_FRAME_SIZE 4096

/* Inicializa el PMM parseando el mapa E820 de Multiboot */
void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr);

/* Asigna un marco fisico de 4 KiB (devuelve direccion fisica o 0 si OOM) */
uintptr_t pmm_alloc_frame(void);

/* PARCHE 118: prueba de memoria agotada. pmm_inject_oom_after(n): tras n asignaciones todas fallan (0); -1 lo desactiva. hits = cuantas se denegaron. */
void pmm_inject_oom_after(long n);
unsigned long pmm_inject_oom_hits(void);

/* SOMA_157_T480S: marco de la reserva ALTA (por encima de 512 MiB; si no hay, de la baja). Solo para memoria que se usa a traves
 * de una proyeccion (el montón): NO es accesible por identidad. Las tablas de paginas y el DMA siguen usando pmm_alloc_frame. */
uintptr_t pmm_alloc_frame_high(void);
void pmm_get_pools(size_t *low_free, size_t *high_free);

/* Libera un marco fisico de 4 KiB */
void pmm_free_frame(uintptr_t phys_addr);

/* SOMA_PARCHE_020_AUDIO: n marcos CONTIGUOS (buffers DMA). Devuelve la direccion fisica o 0 si no hay un hueco. */
uintptr_t pmm_alloc_contig(size_t nframes);
void pmm_free_contig(uintptr_t phys_addr, size_t nframes);

/* Telemetria de memoria fisica */
void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames);
void pmm_dump_stats(void);

/* Auto-test de asignacion, alineacion y liberacion */
int pmm_test_self(void);

int pmm_bench(void);   /* PARCHE 127: compara el escaneo antiguo y el nuevo; 1 = correcto */
int pmm_range_is_ram(uint64_t base, uint64_t len);   /* SOMA_159: RAM utilizable segun el mapa del firmware */
#endif
