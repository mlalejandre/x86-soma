#ifndef MYOS_MEM_H
#define MYOS_MEM_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* Inicializacion del Heap Dinamico VMM */
void  kheap_init(void);

/* Asignador y liberador alineados estrictamente a 16 bytes */
void *kmalloc(size_t size);
void  kfree(void *ptr);

/* Telemetria y estadisticas de memoria dinamica */
void  kheap_stats(size_t *used, size_t *free_bytes);
void  kheap_dump_stats(void);

/* SOMA_PARCHE_005_HEAP: contadores y verificacion de integridad */
struct kheap_counters {
    uint64_t alloc_calls;      /* kmalloc con tamano > 0 */
    uint64_t free_calls;       /* kfree con puntero no nulo */
    uint64_t failed_allocs;    /* kmalloc que devolvieron NULL */
    uint64_t rejected_frees;   /* kfree rechazados (fuera del heap, double free, ...) */
    uint64_t corruptions;      /* corrupciones detectadas desde el arranque */
    uint64_t live_blocks;      /* bloques asignados ahora mismo */
    uint64_t used_bytes;       /* bytes asignados (incluye cabeceras) */
    uint64_t peak_used_bytes;  /* maximo historico de used_bytes */
    uint64_t mapped_bytes;     /* bytes de heap mapeados */
    uint64_t reentries;        /* PARCHE 103: llamadas reentrantes rechazadas */
};
void  kheap_get_counters(struct kheap_counters *out);

/* Recorre todo el heap: devuelve el numero de problemas (0 = sano). */
int   kheap_check(void);
int   kheap_busy(void);            /* SOMA_198: 1 = hay una operacion del asignador a medias */
int   kheap_check_quick(void);     /* PARCHE 128: O(1); 0 = plausible */
size_t kheap_largest_free(void);   /* PARCHE 119: mayor hueco libre (pruebas de OOM) */

/* Auto-test con prueba de expansion dinamica en vivo (>16 KiB) */
int   kheap_test_self(void);
int   kheap_test_reentry(void);   /* PARCHE 103 */

size_t kheap_max_bytes(void);   /* PARCHE 086: limite actual del heap */
/* PARCHE 089: dueno de las reservas (0 = sistema; ver acct.h) y uso del heap por dueno */
extern volatile uint8_t kheap_owner;
void kheap_owner_usage(uint64_t out[256], uint32_t blocks[256]);
#endif
