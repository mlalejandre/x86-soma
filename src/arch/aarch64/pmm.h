#ifndef MYOS_PMM_H
#define MYOS_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PMM_FRAME_SIZE 4096

/* Inicializa el PMM parseando el mapa E820 de Multiboot */
void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr);

/* Asigna un marco fisico de 4 KiB (devuelve direccion fisica o 0 si OOM) */
uintptr_t pmm_alloc_frame(void);

/* PARCHE 062: nframes marcos contiguos (0 si no hay) */
uintptr_t pmm_alloc_contig(size_t nframes);

/* PARCHE 071: marcos contiguos para DMA. En la Pi 5 salen de un pool mapeado como memoria NO cacheable; si no hay pool o se agota, de pmm_alloc_contig */
uintptr_t pmm_alloc_dma(size_t nframes);

/* Libera un marco fisico de 4 KiB */
void pmm_free_frame(uintptr_t phys_addr);

/* Telemetria de memoria fisica */
void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames);
void pmm_dump_stats(void);

/* Auto-test de asignacion, alineacion y liberacion */
int pmm_test_self(void);

int pmm_bench(void);   /* PARCHE 127: compara el escaneo antiguo y el nuevo; 1 = correcto */
#endif
