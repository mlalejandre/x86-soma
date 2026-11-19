#ifndef MYOS_VMM_H
#define MYOS_VMM_H

#include <stdint.h>
#include <stddef.h>

#define VMM_PAGE_SIZE 4096ULL

/* Banderas de entrada en tabla de paginas x86_64 */
#define VMM_FLAG_PRESENT  (1ULL << 0)
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER     (1ULL << 2)
#define VMM_FLAG_NX       (1ULL << 63)

/* Inicializa el VMM enlazando con el CR3 activo */
void vmm_init(void);

/* Aplica protecciones NX y RO a las secciones .text, .rodata, .data y .bss */
void vmm_apply_protections(void);

/* Mapea una pagina virtual de 4 KiB a una direccion fisica */
int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/* Desmapea una pagina virtual de 4 KiB */
int vmm_unmap_page(uint64_t virt_addr);
uint64_t vmm_as_unmap(uint64_t as, uint64_t virt_addr);   /* PARCHE 139: quita la pagina de un espacio de proceso (inactivo); devuelve el marco o 0 */

/* SOMA_PARCHE_017_AS: espacios de direcciones por proceso.
 * Cada proceso tiene su propia PML4 (CR3). Copia las entradas del kernel (se comparten por puntero: lo que
 * el kernel mapee despues en sus tablas se ve en todos) y tiene la entrada VMM_USER_PML4_INDEX propia, que en
 * la PML4 del kernel esta SIEMPRE vacia: las paginas de usuario no existen en el espacio del kernel. */
#define VMM_USER_PML4_INDEX 1ULL                       /* 512 GiB .. 1 TiB */
#define VMM_USER_REGION_BASE 0x8000000000ULL

uint64_t vmm_as_create(void);                           /* devuelve la direccion fisica de la PML4 (0 si no hay memoria) */
int      vmm_as_map(uint64_t as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);   /* pone el bit U en cada nivel */
uint64_t vmm_as_get_pte(uint64_t as, uint64_t virt_addr);
void     vmm_as_destroy(uint64_t as);                   /* libera sus tablas (NO las paginas hoja: son del proceso) */
void     vmm_load_as(uint64_t as);                      /* carga CR3 (solo si cambia) */
uint64_t vmm_kernel_as(void);
int      vmm_kernel_user_region_empty(void);            /* 1 si la PML4 del kernel no tiene nada en la region de usuario */

/* Cambia las banderas de permisos de una pagina virtual mapeada */
int vmm_protect_page(uint64_t virt_addr, uint64_t flags);

/* Traduce una direccion virtual a direccion fisica caminando las tablas */
uint64_t vmm_virt_to_phys(uint64_t virt_addr);

/* Devuelve la entrada de tabla de paginas (PTE) de nivel 1 completa */
uint64_t vmm_get_pte(uint64_t virt_addr);

/* Auto-test integral del VMM (traduccion MMU, 2 GiB y verificacion NX/RO) */
int vmm_test_self(void);

#endif
