/* SOMA_192 (seguridad): W^X de verdad en el nucleo y pagina 0 protegida.
 *
 * Antes: boot.s mapea el primer GiB con paginas de 2 MiB escribibles Y ejecutables, y vmm_apply_protections() solo corregia la
 * imagen del nucleo (.text/.rodata/.data/.bss). Toda la RAM restante de ese mapa (los marcos que reparte el PMM: datos de las
 * aplicaciones, bufferes de red, DMA) y los mapeos de dispositivos hechos sin NX seguian siendo ejecutables en ring 0: un fallo
 * de memoria en cualquier analizador podia saltar a datos controlados por el atacante. Ademas la direccion 0 estaba mapeada y se
 * podia escribir: un puntero nulo no fallaba, corrompia en silencio.
 *
 * Ahora:
 *   - todo lo que cuelga de PML4[0] (el espacio del nucleo) queda NO ejecutable, salvo .text. PML4[1] (procesos de usuario) no se toca;
 *   - la pagina 0 pasa a SOLO LECTURA y no ejecutable: escribir a traves de un puntero nulo detiene el sistema con su backtrace en
 *     vez de corromper. Con soma.pagina0=estricta se quita del mapa (tambien fallan las lecturas): util para cazar punteros nulos;
 *   - soma.memprot=off (o el modo seguro del menu) lo desactiva todo, por si un equipo real diera problemas.
 * Se aplica dos veces: nada mas activar las protecciones del nucleo y al final del arranque (cuando los drivers ya han mapeado
 * sus dispositivos). La orden `memprot` repite la pasada y muestra el resultado. */
#include "memprot.h"
#include "bootfb.h"
#include "vmm.h"
#include "console.h"

extern char __text_start[], __text_end[];

#define PTE_P    (1ULL << 0)
#define PTE_W    (1ULL << 1)
#define PTE_PS   (1ULL << 7)
#define PTE_NX   (1ULL << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define PAGE0_ALIAS 0x0000007FFFFFF000ULL      /* ultima pagina de PML4[0], justo debajo de la region de usuario */

static struct memprot_info g;
static const uint8_t *g_page0;

static inline uint64_t rd_cr3(void) { uint64_t v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void wr_cr3(uint64_t v) { __asm__ volatile ("mov %0, %%cr3" : : "r"(v) : "memory"); }

void memprot_get_info(struct memprot_info *i) { *i = g; }
const uint8_t *memprot_page0(void) { return g_page0; }

void memprot_apply(void)
{
    if (bootinfo_disabled("memprot")) { g.mode = 0; return; }
    int strict = bootinfo_has_option("soma.pagina0=estricta");
    uint64_t ts = (uint64_t)(uintptr_t)__text_start & ~0xFFFULL, te = ((uint64_t)(uintptr_t)__text_end + 0xFFF) & ~0xFFFULL;

    if (strict && !g_page0) {                    /* antes de quitar la pagina 0: un alias de solo lectura para quien lee el area de la BIOS */
        if (vmm_map_page(PAGE0_ALIAS, 0, VMM_FLAG_PRESENT | VMM_FLAG_NX) == 0) g_page0 = (const uint8_t *)(uintptr_t)PAGE0_ALIAS;
        else strict = 0;
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(rd_cr3() & PTE_ADDR);
    uint64_t fixed = 0, left = 0, text = 0, wx = 0;
    if (pml4[0] & PTE_P) {
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[0] & PTE_ADDR);
        for (uint64_t i = 0; i < 512; i++) {
            if (!(pdpt[i] & PTE_P)) continue;
            uint64_t va1 = i << 30;
            if (pdpt[i] & PTE_PS) {                                           /* pagina de 1 GiB */
                if (va1 < te && ts < va1 + (1ULL << 30)) { left += 1ULL << 20; continue; }     /* contiene el codigo: no deberia existir */
                if (!(pdpt[i] & PTE_NX)) { pdpt[i] |= PTE_NX; fixed += 1ULL << 20; }
                continue;
            }
            uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i] & PTE_ADDR);
            for (uint64_t j = 0; j < 512; j++) {
                if (!(pd[j] & PTE_P)) continue;
                uint64_t va2 = va1 | (j << 21);
                if (pd[j] & PTE_PS) {                                         /* pagina de 2 MiB */
                    if (va2 < te && ts < va2 + (1ULL << 21)) { if (!(pd[j] & PTE_NX)) left += 2048; continue; }
                    if (!(pd[j] & PTE_NX)) { pd[j] |= PTE_NX; fixed += 2048; }
                    continue;
                }
                uint64_t *pt = (uint64_t *)(uintptr_t)(pd[j] & PTE_ADDR);
                for (uint64_t k = 0; k < 512; k++) {
                    if (!(pt[k] & PTE_P)) continue;
                    uint64_t va = va2 | (k << 12);
                    if (va >= ts && va < te) { text += 4; if (pt[k] & PTE_W) wx += 4; continue; }
                    if (!(pt[k] & PTE_NX)) { pt[k] |= PTE_NX; fixed += 4; }
                    if (va == 0) { if (strict) pt[k] = 0; else pt[k] &= ~PTE_W; }
                }
            }
        }
    }
    wr_cr3(rd_cr3());                                                         /* vacia la TLB de este nucleo */
    g.mode = strict ? 2 : 1; g.sweeps++; g.exec_fixed += fixed; g.exec_left = left; g.text_kib = text; g.wx_text = wx;
    if (g.sweeps <= 2) {
        kprint("MEMPROT: "); kprint_dec((uint32_t)(fixed / 1024)); kprint(" MiB ("); kprint_dec((uint32_t)fixed);
        kprint(" KiB) del espacio del nucleo dejan de ser ejecutables; solo .text ("); kprint_dec((uint32_t)text);
        kprint(" KiB) lo es. Pagina 0: "); kprint(strict ? "sin mapear" : "solo lectura");
        if (left || wx) kprint("  AVISO: quedan zonas W+X");
        kprint("\n");
    }
}
