#include "vmm.h"
#include "pmm.h"
#include "console.h"
#include "mem.h"

#define PAGE_MASK 0x000FFFFFFFFFF000ULL

/* Indices de 9 bits para cada nivel en arquitectura x86_64 */
#define PML4_INDEX(va) (((va) >> 39) & 0x1FFULL)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FFULL)
#define PD_INDEX(va)   (((va) >> 21) & 0x1FFULL)
#define PT_INDEX(va)   (((va) >> 12) & 0x1FFULL)

/* Simbolos de delimitacion definidos en linker.ld. En pruebas en host (VMM_HOST_TEST) se renombran para no chocar con los
 * del sistema (glibc define __data_start y __bss_start; macOS no define ninguno): el test aporta los suyos. */
#ifdef VMM_HOST_TEST
#define __text_start __host_text_start
#define __text_end __host_text_end
#define __rodata_start __host_rodata_start
#define __rodata_end __host_rodata_end
#define __data_start __host_data_start
#define __data_end __host_data_end
#define __bss_start __host_bss_start
#define __bss_end __host_bss_end
#endif
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];

static uint64_t *kernel_pml4 = 0;

/* PARCHE 104: con VMM_HOST_TEST (solo pruebas en host) las instrucciones privilegiadas se sustituyen; el kernel no cambia. */
#ifdef VMM_HOST_TEST
extern uint64_t vmm_host_cr3(void);
extern void vmm_host_loaded(uint64_t as);
static inline void invlpg(uint64_t va) { (void)va; }
static inline uint64_t read_cr3(void) { return vmm_host_cr3(); }
static inline uint64_t read_cr0(void) { return 0; }
static inline void write_cr0(uint64_t val) { (void)val; }
#else
static inline void invlpg(uint64_t va)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline uint64_t read_cr0(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(uint64_t val)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(val));
}
#endif

void vmm_init(void)
{
    kernel_pml4 = (uint64_t *)(read_cr3() & PAGE_MASK);
    kprint("VMM: Inicializado. PML4 activo en 0x");
    kprint_hex32((uint32_t)(uintptr_t)kernel_pml4);
    kprint("\n");
}

/* Localiza o crea la PT (Nivel 1), desglosando Huge Pages de 2 MiB si existieran */
static uint64_t *vmm_get_pt(uint64_t virt_addr, int allocate)
{
    if (!kernel_pml4) vmm_init();

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);

    /* 1. PML4 -> PDPT */
    if (!(kernel_pml4[pml4_i] & VMM_FLAG_PRESENT)) {
        if (!allocate) return 0;
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, 4096);
        kernel_pml4[pml4_i] = (uint64_t)frame | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    }
    uint64_t *pdpt = (uint64_t *)(kernel_pml4[pml4_i] & PAGE_MASK);

    /* 2. PDPT -> PD */
    if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) {
        if (!allocate) return 0;
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, 4096);
        pdpt[pdpt_i] = (uint64_t)frame | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_i] & PAGE_MASK);

    /* 3. PD -> PT (con desglose si es Huge Page de 2 MiB) */
    if (!(pd[pd_i] & VMM_FLAG_PRESENT)) {
        if (!allocate) return 0;
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, 4096);
        pd[pd_i] = (uint64_t)frame | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    } else if (pd[pd_i] & 0x80) {
        /* Desglosar Huge Page de 2 MiB en 512 entradas de 4 KiB */
        uint64_t base_2mb = pd[pd_i] & 0x000FFFFFFFE00000ULL;
        uint64_t orig_flags = ((pd[pd_i] & 0xFFF) & ~0x80ULL) | (pd[pd_i] & VMM_FLAG_NX);   /* SOMA_192: al partir una pagina de 2 MiB no se pierde el NX */

        uintptr_t new_pt_frame = pmm_alloc_frame();
        if (!new_pt_frame) return 0;
        uint64_t *new_pt = (uint64_t *)new_pt_frame;

        for (uint64_t k = 0; k < 512; ++k) {
            new_pt[k] = (base_2mb + k * 4096) | orig_flags;
        }

        pd[pd_i] = (uint64_t)new_pt_frame | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    }

    return (uint64_t *)(pd[pd_i] & PAGE_MASK);
}

int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    uint64_t *pt = vmm_get_pt(virt_addr, 1);
    if (!pt) return -1;

    uint64_t pt_i = PT_INDEX(virt_addr);
    pt[pt_i] = (phys_addr & PAGE_MASK) | flags;
    invlpg(virt_addr);
    return 0;
}

/* ---- SOMA_PARCHE_017_AS: espacios de direcciones por proceso ---- */
static uint64_t cur_cr3_cache;

static uint64_t *zeroed_frame(uint64_t *phys_out)
{
    uintptr_t f = pmm_alloc_frame();
    if (!f) return 0;
    memset((void *)f, 0, 4096);
    *phys_out = (uint64_t)f;
    return (uint64_t *)f;
}

uint64_t vmm_kernel_as(void)
{
    if (!kernel_pml4) vmm_init();
    return (uint64_t)(uintptr_t)kernel_pml4;
}

int vmm_kernel_user_region_empty(void)
{
    if (!kernel_pml4) vmm_init();
    return kernel_pml4[VMM_USER_PML4_INDEX] == 0;
}

uint64_t vmm_as_create(void)
{
    if (!kernel_pml4) vmm_init();
    uint64_t phys;
    uint64_t *pml4 = zeroed_frame(&phys);
    if (!pml4) return 0;
    for (uint64_t i = 0; i < 512; ++i)
        if (i != VMM_USER_PML4_INDEX) pml4[i] = kernel_pml4[i];
    return phys;
}

int vmm_as_map(uint64_t as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    if (!as || PML4_INDEX(virt_addr) != VMM_USER_PML4_INDEX) return -1;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)as, ph;
    const uint64_t tbl = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;

    if (!(pml4[VMM_USER_PML4_INDEX] & VMM_FLAG_PRESENT)) {
        if (!zeroed_frame(&ph)) return -1;
        pml4[VMM_USER_PML4_INDEX] = ph | tbl;
    }
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[VMM_USER_PML4_INDEX] & PAGE_MASK);
    if (!(pdpt[PDPT_INDEX(virt_addr)] & VMM_FLAG_PRESENT)) {
        if (!zeroed_frame(&ph)) return -1;
        pdpt[PDPT_INDEX(virt_addr)] = ph | tbl;
    }
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[PDPT_INDEX(virt_addr)] & PAGE_MASK);
    if (!(pd[PD_INDEX(virt_addr)] & VMM_FLAG_PRESENT)) {
        if (!zeroed_frame(&ph)) return -1;
        pd[PD_INDEX(virt_addr)] = ph | tbl;
    }
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[PD_INDEX(virt_addr)] & PAGE_MASK);
    pt[PT_INDEX(virt_addr)] = (phys_addr & PAGE_MASK) | flags | VMM_FLAG_USER;
    return 0;
}

uint64_t vmm_as_get_pte(uint64_t as, uint64_t virt_addr)
{
    if (!as) return 0;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)as;
    uint64_t e = pml4[PML4_INDEX(virt_addr)];
    if (!(e & VMM_FLAG_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(e & PAGE_MASK);
    e = pdpt[PDPT_INDEX(virt_addr)];
    if (!(e & VMM_FLAG_PRESENT) || (e & 0x80)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(e & PAGE_MASK);
    e = pd[PD_INDEX(virt_addr)];
    if (!(e & VMM_FLAG_PRESENT) || (e & 0x80)) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(e & PAGE_MASK);
    return pt[PT_INDEX(virt_addr)];
}

/* PARCHE 139: el espacio no debe estar cargado (los servicios bloqueados en el kernel cambian de CR3 al ceder: la TLB de usuario se vacia). */
uint64_t vmm_as_unmap(uint64_t as, uint64_t virt_addr)
{
    if (!as || PML4_INDEX(virt_addr) != VMM_USER_PML4_INDEX) return 0;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)as, e = pml4[VMM_USER_PML4_INDEX];
    if (!(e & VMM_FLAG_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(e & PAGE_MASK); e = pdpt[PDPT_INDEX(virt_addr)];
    if (!(e & VMM_FLAG_PRESENT) || (e & 0x80)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(e & PAGE_MASK); e = pd[PD_INDEX(virt_addr)];
    if (!(e & VMM_FLAG_PRESENT) || (e & 0x80)) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(e & PAGE_MASK);
    uint64_t f = pt[PT_INDEX(virt_addr)];
    pt[PT_INDEX(virt_addr)] = 0;
    return (f & VMM_FLAG_PRESENT) ? (f & PAGE_MASK) : 0;
}

void vmm_as_destroy(uint64_t as)
{
    if (!as) return;
    /* PARCHE 104: nunca liberar las tablas del espacio activo (CR3 apuntaria a memoria reutilizable): primero volver al del kernel. */
    if (as == cur_cr3_cache && kernel_pml4 && as != (uint64_t)(uintptr_t)kernel_pml4) vmm_load_as((uint64_t)(uintptr_t)kernel_pml4);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)as;
    if (pml4[VMM_USER_PML4_INDEX] & VMM_FLAG_PRESENT) {
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[VMM_USER_PML4_INDEX] & PAGE_MASK);
        for (int i = 0; i < 512; ++i) {
            if (!(pdpt[i] & VMM_FLAG_PRESENT)) continue;
            uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i] & PAGE_MASK);
            for (int j = 0; j < 512; ++j)
                if (pd[j] & VMM_FLAG_PRESENT) pmm_free_frame((uintptr_t)(pd[j] & PAGE_MASK));   /* PT */
            pmm_free_frame((uintptr_t)(pdpt[i] & PAGE_MASK));                                    /* PD */
        }
        pmm_free_frame((uintptr_t)(pml4[VMM_USER_PML4_INDEX] & PAGE_MASK));                      /* PDPT */
    }
    pmm_free_frame((uintptr_t)as);                                                               /* PML4 */
}

void vmm_load_as(uint64_t as)
{
    if (!cur_cr3_cache) cur_cr3_cache = read_cr3() & PAGE_MASK;
    if (as == cur_cr3_cache) return;
#ifndef VMM_HOST_TEST
    __asm__ volatile ("mov %0, %%cr3" : : "r"(as) : "memory");
#else
    vmm_host_loaded(as);
#endif
    cur_cr3_cache = as;
}

int vmm_unmap_page(uint64_t virt_addr)
{
    uint64_t *pt = vmm_get_pt(virt_addr, 0);
    if (!pt) return -1;

    uint64_t pt_i = PT_INDEX(virt_addr);
    pt[pt_i] = 0;
    invlpg(virt_addr);
    return 0;
}

int vmm_protect_page(uint64_t virt_addr, uint64_t flags)
{
    uint64_t *pt = vmm_get_pt(virt_addr, 1);
    if (!pt) return -1;

    uint64_t pt_i = PT_INDEX(virt_addr);
    if (!(pt[pt_i] & VMM_FLAG_PRESENT)) return -1;

    uint64_t phys = pt[pt_i] & PAGE_MASK;
    pt[pt_i] = phys | flags;
    invlpg(virt_addr);
    return 0;
}

uint64_t vmm_virt_to_phys(uint64_t virt_addr)
{
    if (!kernel_pml4) vmm_init();

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    if (!(kernel_pml4[pml4_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(kernel_pml4[pml4_i] & PAGE_MASK);

    if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_i] & PAGE_MASK);

    if (!(pd[pd_i] & VMM_FLAG_PRESENT)) return 0;

    if (pd[pd_i] & 0x80) {
        uint64_t base_2mb = pd[pd_i] & 0x000FFFFFFFE00000ULL;
        return base_2mb + (virt_addr & 0x1FFFFFULL);
    }

    uint64_t *pt = (uint64_t *)(pd[pd_i] & PAGE_MASK);
    if (!(pt[pt_i] & VMM_FLAG_PRESENT)) return 0;

    return (pt[pt_i] & PAGE_MASK) + (virt_addr & 0xFFFULL);
}

uint64_t vmm_get_pte(uint64_t virt_addr)
{
    uint64_t *pt = vmm_get_pt(virt_addr, 0);
    if (!pt) return 0;
    return pt[PT_INDEX(virt_addr)];
}

static void vmm_protect_range(uint64_t start, uint64_t end, uint64_t flags)
{
    uint64_t va_start = start & PAGE_MASK;
    uint64_t va_end   = (end + VMM_PAGE_SIZE - 1) & PAGE_MASK;

    for (uint64_t va = va_start; va < va_end; va += VMM_PAGE_SIZE) {
        vmm_protect_page(va, flags);
    }
}

void vmm_apply_protections(void)
{
    uint64_t text_s   = (uint64_t)(uintptr_t)__text_start;
    uint64_t text_e   = (uint64_t)(uintptr_t)__text_end;
    uint64_t rodata_s = (uint64_t)(uintptr_t)__rodata_start;
    uint64_t rodata_e = (uint64_t)(uintptr_t)__rodata_end;
    uint64_t data_s   = (uint64_t)(uintptr_t)__data_start;
    uint64_t bss_e    = (uint64_t)(uintptr_t)__bss_end;

    /* 1. Proteger .text como Read-Only + Executable */
    vmm_protect_range(text_s, text_e, VMM_FLAG_PRESENT);

    /* 2. Proteger .rodata como Read-Only + No-Execute (NX) */
    vmm_protect_range(rodata_s, rodata_e, VMM_FLAG_PRESENT | VMM_FLAG_NX);

    /* 3. Proteger .data y .bss como Read-Write + No-Execute (NX) */
    vmm_protect_range(data_s, bss_e, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX);

    /* 4. Activar CR0.WP (Write Protect - bit 16) para que Ring 0 respete las paginas Read-Only */
    write_cr0(read_cr0() | (1ULL << 16));

    kprint("VMM: Protecciones activas (CR0.WP=1, NX habilitado):\n");
    kprint("  .text   [0x"); kprint_hex32((uint32_t)text_s); kprint("-0x"); kprint_hex32((uint32_t)text_e); kprint("] -> Read-Only + Executable\n");
    kprint("  .rodata [0x"); kprint_hex32((uint32_t)rodata_s); kprint("-0x"); kprint_hex32((uint32_t)rodata_e); kprint("] -> Read-Only + NX\n");
    kprint("  .data   [0x"); kprint_hex32((uint32_t)data_s); kprint("-0x"); kprint_hex32((uint32_t)bss_e); kprint("] -> Read-Write + NX\n");
}

int vmm_test_self(void)
{
    kprint("\n[VMM AUTO-TEST] Verificando traduccion MMU, 4 KiB y permisos NX/RO...\n");

    /* 1. Comprobar que la memoria identity-mapped resuelve correctamente */
    uint64_t p_kernel = vmm_virt_to_phys(0x00100000);
    if (p_kernel != 0x00100000) {
        kprint("  FALLO: 0x00100000 no identity-mapped\n");
        return 0;
    }
    kprint("  Identity-mapping base (0x00100000 -> 0x00100000) OK\n");

    /* 2. Mapeo dinamico en 2 GiB y verificacion de traduccion */
    uintptr_t test_frame = pmm_alloc_frame();
    if (!test_frame) {
        kprint("  FALLO: PMM sin frames\n");
        return 0;
    }

    uint64_t test_vaddr = 0x0000000080000000ULL;
    if (vmm_map_page(test_vaddr, test_frame, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE) != 0) {
        kprint("  FALLO: vmm_map_page en 2 GiB\n");
        pmm_free_frame(test_frame);
        return 0;
    }

    volatile uint32_t *vptr = (volatile uint32_t *)test_vaddr;
    volatile uint32_t *pptr = (volatile uint32_t *)test_frame;
    *vptr = 0xCAFEBABE;
    if (*pptr != 0xCAFEBABE) {
        kprint("  FALLO: discrepancia MMU\n");
        vmm_unmap_page(test_vaddr);
        pmm_free_frame(test_frame);
        return 0;
    }
    kprint("  Mapeo virtual dinamico 0x80000000 -> frame 0x");
    kprint_hex32((uint32_t)test_frame);
    kprint(" OK (Dato: 0xCAFEBABE)\n");

    vmm_unmap_page(test_vaddr);
    pmm_free_frame(test_frame);

    /* 3. Verificacion de protecciones NX / RO en tablas de paginas */
    uint64_t text_pte   = vmm_get_pte((uint64_t)(uintptr_t)__text_start);
    uint64_t rodata_pte = vmm_get_pte((uint64_t)(uintptr_t)__rodata_start);
    uint64_t data_pte   = vmm_get_pte((uint64_t)(uintptr_t)__data_start);

    /* .text debe ser Presente (1), NO Writable (bit 1 == 0), NO NX (bit 63 == 0) */
    if (!(text_pte & VMM_FLAG_PRESENT) || (text_pte & VMM_FLAG_WRITABLE) || (text_pte & VMM_FLAG_NX)) {
        kprint("  FALLO: flags invalidos en .text\n");
        return 0;
    }
    kprint("  Permisos .text verificados: Read-Only + Executable (PTE=0x");
    kprint_hex32((uint32_t)text_pte);
    kprint(") OK\n");

    /* .rodata debe ser Presente (1), NO Writable (bit 1 == 0), CON NX (bit 63 == 1) */
    if (!(rodata_pte & VMM_FLAG_PRESENT) || (rodata_pte & VMM_FLAG_WRITABLE) || !(rodata_pte & VMM_FLAG_NX)) {
        kprint("  FALLO: flags invalidos en .rodata\n");
        return 0;
    }
    kprint("  Permisos .rodata verificados: Read-Only + No-Execute (NX) OK\n");

    /* .data debe ser Presente (1), CON Writable (bit 1 == 1), CON NX (bit 63 == 1) */
    if (!(data_pte & VMM_FLAG_PRESENT) || !(data_pte & VMM_FLAG_WRITABLE) || !(data_pte & VMM_FLAG_NX)) {
        kprint("  FALLO: flags invalidos en .data\n");
        return 0;
    }
    kprint("  Permisos .data/.bss verificados: Read-Write + No-Execute (NX) OK\n");

    /* Verificar que CR0.WP esta encendido */
    if (!(read_cr0() & (1ULL << 16))) {
        kprint("  FALLO: CR0.WP no esta activo\n");
        return 0;
    }
    kprint("  Hardware Ring 0 Write-Protect (CR0.WP): ACTIVO\n");

    kprint("[VMM AUTO-TEST] SUPERADO CON EXITO (Aislamiento Total OK).\n\n");
    return 1;
}
