#include "bootfb.h"   /* SOMA_154_T480S */
#include "pmm.h"
#include "console.h"
#include "mem.h"
#include "pmm_scan.h"
#include "io.h"

/* Estructuras de la especificacion Multiboot 1 */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

/* SOMA_157_T480S: el asignador vive en pmm_core.h (puro, probado en el anfitrion: tests/test_pmm_157.c). Antes el mapa de bits
 * era fijo (512 MiB, 131072 marcos); ahora cubre toda la RAM que diga el firmware (el T480s: 15,7 GiB utilizables hasta
 * 0x4_6180_0000) y hay dos reservas: BAJA (< 512 MiB, accesible por identidad: tablas de paginas, DMA) y ALTA (el resto:
 * solo a traves del montón). pmm_alloc_frame() sigue dando marcos bajos, asi que ningun uso existente cambia. */
#include "pmm_core.h"
#define PMM_RESERVED_LOWER PMMC_LOWER
#define PMM_STATIC_FRAMES  ((size_t)(PMMC_LOW_LIMIT / PMM_FRAME_SIZE))
static uint64_t pmm_bitmap_static[PMM_STATIC_FRAMES / 64] __attribute__((aligned(64)));   /* 16 KiB: hasta 512 MiB */
static struct pmmc pmm;
static void *pmm_identity(uint64_t phys) { return (void *)(uintptr_t)phys; }

static int pmm_add(struct pmmc_region *r, int *n, uint64_t base, uint64_t len)
{
    if (!len || *n >= PMMC_MAX_REGIONS) return 0;
    r[*n].base = base; r[*n].len = len; (*n)++;
    return 1;
}

static const struct pmmc_region *g_regs; static int g_nregs;
/* SOMA_159_T480S: 1 si [base, base+len) es RAM utilizable segun el firmware (la zona baja < 16 MiB nunca la reparte el PMM) */
int pmm_range_is_ram(uint64_t base, uint64_t len)
{
    for (int i = 0; i < g_nregs; i++) if (base >= g_regs[i].base && base + len <= g_regs[i].base + g_regs[i].len) return 1;
    return 0;
}

void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr)
{
    static struct pmmc_region regs[PMMC_MAX_REGIONS];
    int n = 0;
    /* 1. Multiboot 1: mapa de memoria (flag bit 6) */
    if (mb_magic == 0x2BADB002 && mb_info_addr != 0) {
        struct multiboot_info *mbi = (struct multiboot_info *)(uintptr_t)mb_info_addr;
        if (mbi->flags & (1 << 6)) {
            uintptr_t cur = (uintptr_t)mbi->mmap_addr, end = cur + mbi->mmap_length;
            while (cur < end) {
                struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)cur;
                if (e->type == 1) pmm_add(regs, &n, e->addr, e->len);       /* 1 = RAM utilizable */
                cur += (e->size + sizeof(uint32_t));
            }
        }
    }
    /* 2. Multiboot 2 (GRUB "multiboot2", UEFI): tag 6 */
    if (!n && bootinfo_is_mb2()) {
        uint32_t it = 0; uint64_t base, len; uint32_t type;
        while (bootinfo_mmap_next(&it, &base, &len, &type)) if (type == 1) pmm_add(regs, &n, base, len);
    }
    /* 3. Sin mapa: los 256 MiB de siempre */
    if (!n) pmm_add(regs, &n, 0, 256 * 1024 * 1024ULL);
    /* soma.ram=512: limita a la reserva baja (comportamiento de antes del 157), por si algo fallara en un equipo */
    if (bootinfo_disabled("ram") || bootinfo_has_option("soma.ram=512")) {
        for (int i = 0; i < n; i++) {
            if (regs[i].base >= PMMC_LOW_LIMIT) { regs[i].len = 0; continue; }
            if (regs[i].base + regs[i].len > PMMC_LOW_LIMIT) regs[i].len = PMMC_LOW_LIMIT - regs[i].base;
        }
    }
    /* SOMA_175A_T480S: la memoria de los modulos de GRUB (firmware WiFi) no se reparte */
    for (int m = 0; m < bootinfo_module_count(); m++) {
        uint64_t ms, ml; if (!bootinfo_module_at(m, &ms, &ml, 0) || !ml) continue;
        uint64_t a = ms & ~0xFFFULL, b = (ms + ml + 0xFFFULL) & ~0xFFFULL;
        for (int i = 0; i < n; i++) {
            uint64_t rs = regs[i].base, re = regs[i].base + regs[i].len;
            if (b <= rs || a >= re) continue;
            if (a > rs && b < re && n < PMMC_MAX_REGIONS) { regs[n].base = b; regs[n].len = re - b; n++; regs[i].len = a - rs; }
            else if (a <= rs && b >= re) regs[i].len = 0;
            else if (a <= rs) { regs[i].base = b; regs[i].len = re - b; }
            else regs[i].len = a - rs;
        }
    }
    g_regs = regs; g_nregs = n;                          /* SOMA_159_T480S: pmm_range_is_ram (trampolin de los nucleos) */
    pmmc_build(&pmm, regs, n, pmm_bitmap_static, PMM_STATIC_FRAMES, pmm_identity);
    kprint("PMM: Inicializado (Bitmap Allocator 4 KiB)\n");
    if (pmm.bm_bytes) {
        kprint("PMM: mapa de bits de "); kprint_dec((uint32_t)(pmm.bm_bytes / 1024)); kprint(" KiB en 0x"); kprint_hex32((uint32_t)pmm.bm_phys);
        kprint(" | reserva baja (identidad, DMA) "); kprint_dec((uint32_t)((uint64_t)pmm.low_end * PMM_FRAME_SIZE >> 20));
        kprint(" MiB | reserva alta (monton) hasta "); kprint_dec((uint32_t)((uint64_t)pmm.frames * PMM_FRAME_SIZE >> 20)); kprint(" MiB\n");
    }
    if (pmm.capped) kprint("PMM: AVISO: no hay sitio para el mapa de bits grande; se usan solo los primeros 512 MiB\n");
    pmm_dump_stats();
}

/* PARCHE 118: inyeccion de OOM. Con n >= 0, las n primeras asignaciones funcionan y TODAS las siguientes devuelven 0 (memoria agotada) hasta que se desactiva con -1. */
static long pmm_inject_left = -1;
static unsigned long pmm_inject_hits;
void pmm_inject_oom_after(long n) { pmm_inject_left = n; pmm_inject_hits = 0; }
unsigned long pmm_inject_oom_hits(void) { return pmm_inject_hits; }
static int pmm_injected_oom(void)
{
    if (pmm_inject_left < 0) return 0;
    if (pmm_inject_left == 0) { pmm_inject_hits++; return 1; }
    pmm_inject_left--;
    return 0;
}

uintptr_t pmm_alloc_frame(void)
{
    if (pmm_injected_oom()) return 0;
    return (uintptr_t)pmmc_alloc_low(&pmm);
}
uintptr_t pmm_alloc_frame_high(void)
{
    if (pmm_injected_oom()) return 0;
    return (uintptr_t)pmmc_alloc_high(&pmm);
}
void pmm_free_frame(uintptr_t phys_addr) { pmmc_free(&pmm, phys_addr); }

/* SOMA_PARCHE_020_AUDIO: contiguos para DMA (siempre en la reserva baja) */
uintptr_t pmm_alloc_contig(size_t nframes) { return (uintptr_t)pmmc_alloc_contig(&pmm, nframes); }

void pmm_free_contig(uintptr_t phys_addr, size_t nframes)
{
    for (size_t i = 0; i < nframes; ++i) pmm_free_frame(phys_addr + i * PMM_FRAME_SIZE);
}

void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames)
{
    if (total_frames) *total_frames = pmm.usable;
    if (used_frames)  *used_frames  = pmm.used;
    if (free_frames)  *free_frames  = pmm.usable > pmm.used ? pmm.usable - pmm.used : 0;
}
void pmm_get_pools(size_t *low_free, size_t *high_free)     /* SOMA_157: libres en cada reserva (orden hw mem) */
{
    size_t lf = 0, hf = 0;
    for (size_t f = (size_t)(PMMC_LOWER / PMM_FRAME_SIZE); f < pmm.frames; f++)
        if (!pmmc_test(&pmm, f)) { if (f < pmm.low_end) lf++; else hf++; }
    if (low_free) *low_free = lf;
    if (high_free) *high_free = hf;
}

void pmm_dump_stats(void)
{
    size_t f_free = 0, f_used = 0, f_total = 0;
    pmm_get_stats(&f_free, &f_used, &f_total);

    kprint("PMM RAM: Total ");
    kprint_dec((uint32_t)((f_total * PMM_FRAME_SIZE) / (1024 * 1024)));
    kprint(" MiB (");
    kprint_dec((uint32_t)f_total);
    kprint(" frames) | Usados: ");
    kprint_dec((uint32_t)f_used);
    kprint(" | Libres: ");
    kprint_dec((uint32_t)f_free);
    kprint(" (");
    kprint_dec((uint32_t)((f_free * PMM_FRAME_SIZE) / (1024 * 1024)));
    kprint(" MiB disponibles)\n");
}

int pmm_test_self(void)
{
    kprint("\n[PMM AUTO-TEST] Verificando asignador de marcos fisicos...\n");
    size_t free_before = 0, used_before = 0, total_before = 0;
    pmm_get_stats(&free_before, &used_before, &total_before);

    uintptr_t frame1 = pmm_alloc_frame();
    uintptr_t frame2 = pmm_alloc_frame();
    uintptr_t frame3 = pmm_alloc_frame();

    if (!frame1 || !frame2 || !frame3) {
        kprint("  FALLO: pmm_alloc_frame retorno 0 (OOM prematuro)\n");
        return 0;
    }

    /* Comprobar alineacion estricta a 4096 bytes */
    if ((frame1 & 0xFFF) != 0 || (frame2 & 0xFFF) != 0 || (frame3 & 0xFFF) != 0) {
        kprint("  FALLO: frames no alineados a 4 KiB\n");
        return 0;
    }

    /* Comprobar que estan por encima de los 16 MiB reservados */
    if (frame1 < PMM_RESERVED_LOWER || frame2 < PMM_RESERVED_LOWER || frame3 < PMM_RESERVED_LOWER) {
        kprint("  FALLO: frame asignado invade la zona reservada del principio\n");
        return 0;
    }

    /* Comprobar que son distintos */
    if (frame1 == frame2 || frame2 == frame3 || frame1 == frame3) {
        kprint("  FALLO: frames duplicados detectados\n");
        return 0;
    }

    kprint("  Asignados 3 frames fisicos OK: 0x");
    kprint_hex32((uint32_t)frame1);
    kprint(", 0x");
    kprint_hex32((uint32_t)frame2);
    kprint(", 0x");
    kprint_hex32((uint32_t)frame3);
    kprint("\n");

    /* Liberar */
    pmm_free_frame(frame2);
    pmm_free_frame(frame1);
    pmm_free_frame(frame3);

    size_t free_after = 0, used_after = 0, total_after = 0;
    pmm_get_stats(&free_after, &used_after, &total_after);

    if (free_after != free_before || used_after != used_before) {
        kprint("  FALLO: fuga o inconsistencia en conteo de frames tras free\n");
        return 0;
    }

    kprint("  Liberacion y recontabilizacion verficada OK (0 fugas)\n");
    kprint("[PMM AUTO-TEST] SUPERADO CON EXITO.\n\n");
    return 1;
}

/* PARCHE 127: banco de pruebas. Con 8192 marcos ocupados al principio, compara el coste de asignar 256 marcos con el
 * escaneo antiguo (bit a bit desde los 16 MiB) y con el nuevo (pista + palabras de 64). Devuelve 1 si todo es correcto. */
int pmm_bench(void)
{
    enum { BASE = 8192, N = 256 };
    static uintptr_t held[BASE + N];
    size_t free0, used0, tot0; pmm_get_stats(&free0, &used0, &tot0);
    if (free0 < BASE + 2 * N) { kprint("[PMMBENCH] poca memoria libre para la prueba\n"); return 1; }
    int ok = 1, nb = 0;
    for (int i = 0; i < BASE; ++i) { held[nb] = pmm_alloc_frame(); if (!held[nb]) { ok = 0; break; } nb++; }
    /* escaneo antiguo: bit a bit desde el inicio (de la reserva baja) */
    uint64_t t0 = rdtsc(); int nold = 0; size_t old_first = 0;
    for (int i = 0; i < N; ++i) {
        size_t f = PMM_RESERVED_LOWER / PMM_FRAME_SIZE;
        while (f < pmm.low_end && pmmc_test(&pmm, f)) f++;
        if (f >= pmm.low_end) break;
        pmmc_set(&pmm, f); pmm.used++; held[nb + nold++] = (uintptr_t)(f * PMM_FRAME_SIZE); if (!i) old_first = f;
    }
    uint64_t c_old = rdtsc() - t0;
    for (int i = 0; i < nold; ++i) pmm_free_frame(held[nb + i]);
    pmm.hint = PMM_RESERVED_LOWER / PMM_FRAME_SIZE;      /* misma situacion de partida: la pista aun no sabe nada */
    t0 = rdtsc(); int nnew = 0; size_t new_first = 0;
    for (int i = 0; i < N; ++i) { uintptr_t a = pmm_alloc_frame(); if (!a) break; held[nb + nnew++] = a; if (!i) new_first = a / PMM_FRAME_SIZE; }
    uint64_t c_new = rdtsc() - t0;
    if (nold != N || nnew != N || old_first != new_first) ok = 0;      /* el nuevo devuelve exactamente los mismos marcos */
    for (int i = 0; i < nb + nnew; ++i) pmm_free_frame(held[i]);
    size_t free1, used1, tot1; pmm_get_stats(&free1, &used1, &tot1);
    if (free1 != free0 || used1 != used0) ok = 0;                       /* sin fugas */
    kprint("[PMMBENCH] 256 asignaciones tras 8192 ocupados: antes "); kprint_dec((uint32_t)(c_old / N));
    kprint(" ciclos/marco, ahora "); kprint_dec((uint32_t)(c_new / N)); kprint(" ciclos/marco\n");
    kprint(ok ? "[PMMBENCH] OK\n" : "[PMMBENCH] FALLO\n");
    return ok;
}
