/* SOMA_157_T480S: prueba en el anfitrion del nucleo del PMM (src/arch/x86_64/pmm_core.h), el mismo codigo que usa el nucleo.
 * Mapas: T480s real (15,7 GiB, techo 0x4_6180_0000), QEMU 512 MiB, QEMU q35 4 GiB, sin sitio para el mapa, mapa corrupto.
 * En el del T480s se agota TODA la memoria: ningun marco duplicado, ninguno en huecos ni en la zona reservada. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_core.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)
static uint64_t bm_static[(512ULL << 20) / 4096 / 64];
static void *dyn; static uint64_t dyn_phys;
static void *p2p(uint64_t phys) { free(dyn); dyn = calloc(1, 4u << 20);   /* el mapa mas grande posible (64 GiB) ocupa 2 MiB */ dyn_phys = phys; return dyn; }
#define MiB (1ULL << 20)
#define GiB (1ULL << 30)

static int in_regions(const struct pmmc_region *r, int n, uint64_t a)
{
    for (int i = 0; i < n; i++) if (a >= r[i].base && a + 4096 <= r[i].base + r[i].len) return 1;
    return 0;
}

static void t480s(void)
{
    /* mapa con la forma del de la foto: RAM baja ~2,2 GiB troceada, hueco PCI, 13,5 GiB por encima de 4 GiB */
    struct pmmc_region r[] = {
        { 0x0, 0x58000 }, { 0x59000, 0x45000 }, { 0x100000, 0x7A000000 - 0x100000 },
        { 0x7A100000, 0x8C000000 - 0x7A100000 }, { 0x100000000ULL, 0x461800000ULL - 0x100000000ULL },
    };
    int n = sizeof r / sizeof r[0];
    struct pmmc s; memset(&s, 0, sizeof s);
    pmmc_build(&s, r, n, bm_static, sizeof bm_static * 8, p2p);
    CHECK(s.frames == 0x461800000ULL / 4096, "el mapa cubre hasta 0x4_6180_0000");
    CHECK(s.bm_bytes == pmmc_bm_bytes(s.frames) && s.bm_bytes > 500 * 1024 && s.bm_bytes < 600 * 1024, "mapa de ~575 KiB");
    CHECK(s.bm_phys >= PMMC_LOWER && s.bm_phys + s.bm_bytes <= PMMC_LOW_LIMIT, "mapa dentro de la reserva baja");
    CHECK(!s.capped, "sin recorte");
    uint64_t usable = 0; for (int i = 0; i < n; i++) usable += r[i].len;
    CHECK(s.usable >= usable / 4096 - 4 && s.usable <= usable / 4096, "RAM utilizable contada");
    double gib = (double)s.usable * 4096 / GiB;
    printf("  T480s: %.1f GiB utilizables, mapa de %llu KiB en 0x%llx, reserva baja hasta %llu MiB\n", gib,
           (unsigned long long)(s.bm_bytes / 1024), (unsigned long long)s.bm_phys, (unsigned long long)((uint64_t)s.low_end * 4096 / MiB));
    size_t free0 = s.usable - s.used;
    /* baja y alta */
    uint64_t a = pmmc_alloc_low(&s), h = pmmc_alloc_high(&s);
    CHECK(a >= PMMC_LOWER && a < PMMC_LOW_LIMIT, "marco bajo en [16 MiB, 512 MiB)");
    CHECK(h >= PMMC_LOW_LIMIT, "marco alto por encima de 512 MiB");
    CHECK(!(a >= s.bm_phys && a < s.bm_phys + s.bm_bytes), "no entrega el propio mapa");
    pmmc_free(&s, a); pmmc_free(&s, h);
    CHECK(s.usable - s.used == free0, "liberar deja la cuenta igual");
    pmmc_free(&s, h); CHECK(s.usable - s.used == free0, "doble liberacion ignorada");
    pmmc_free(&s, s.bm_phys); CHECK(s.usable - s.used == free0, "el mapa no se puede liberar");
    pmmc_free(&s, 0x1000); CHECK(s.usable - s.used == free0, "la zona baja no se puede liberar");
    /* contiguos para DMA: siempre en la reserva baja */
    uint64_t c = pmmc_alloc_contig(&s, 64);
    CHECK(c >= PMMC_LOWER && c + 64 * 4096 <= PMMC_LOW_LIMIT, "DMA contiguo en la reserva baja");
    for (int i = 0; i < 64; i++) pmmc_free(&s, c + (uint64_t)i * 4096);
    /* agotar TODO: altas primero (con caida a la baja), comprobando cada marco */
    uint8_t *seen = calloc(s.frames / 8 + 1, 1);
    size_t got = 0, highs = 0, dup = 0, bad = 0;
    for (;;) {
        uint64_t p = pmmc_alloc_high(&s);
        if (!p) break;
        size_t f = (size_t)(p / 4096);
        if (seen[f >> 3] & (1 << (f & 7))) dup++;
        seen[f >> 3] |= (uint8_t)(1 << (f & 7));
        if (!in_regions(r, n, p) || p < PMMC_LOWER || (p >= s.bm_phys && p < s.bm_phys + s.bm_bytes)) bad++;
        if (p >= PMMC_LOW_LIMIT) highs++;
        got++;
    }
    CHECK(dup == 0, "ningun marco entregado dos veces");
    CHECK(bad == 0, "ningun marco en huecos, zona reservada o el mapa");
    CHECK(got == free0, "se entregan exactamente todos los libres");
    CHECK(s.used == s.usable, "contador lleno al final");
    CHECK(pmmc_alloc_low(&s) == 0 && pmmc_alloc_contig(&s, 1) == 0, "agotada: devuelve 0");
    printf("  T480s: %zu marcos entregados (%.1f GiB), %zu de la reserva alta, 0 duplicados\n", got, (double)got * 4096 / GiB, highs);
    /* liberar todo y repetir: la cuenta vuelve al principio */
    for (size_t f = 0; f < s.frames; f++) if (seen[f >> 3] & (1 << (f & 7))) pmmc_free(&s, (uint64_t)f * 4096);
    CHECK(s.usable - s.used == free0, "todo liberado");
    CHECK(pmmc_alloc_high(&s) >= PMMC_LOW_LIMIT, "tras liberar, vuelve a dar marcos altos");
    free(seen);
}
static void qemu512(void)
{
    struct pmmc_region r[] = { { 0, 0x9F000 }, { 0x100000, 512 * MiB - 0x100000 - 0x20000 } };
    struct pmmc s; memset(&s, 0, sizeof s);
    pmmc_build(&s, r, 2, bm_static, sizeof bm_static * 8, p2p);
    CHECK(s.bm == bm_static && s.bm_bytes == 0, "512 MiB: mapa estatico de siempre");
    CHECK(s.low_end == s.frames, "512 MiB: sin reserva alta");
    uint64_t h = pmmc_alloc_high(&s);
    CHECK(h >= PMMC_LOWER && h < PMMC_LOW_LIMIT, "512 MiB: el monton cae a la reserva baja (como antes)");
}
static void qemu4g(void)
{
    struct pmmc_region r[] = { { 0, 0xA0000 }, { 0x100000, 0x700000 }, { 0x900000, 0x7ED3F000 - 0x900000 },
                               { 0x7EE00000, 0xAED000 }, { 0x100000000ULL, 2 * GiB } };
    struct pmmc s; memset(&s, 0, sizeof s);
    pmmc_build(&s, r, 5, bm_static, sizeof bm_static * 8, p2p);
    CHECK(s.frames == 0x180000000ULL / 4096 && s.bm_bytes, "4 GiB: mapa dinamico hasta 6 GiB de direcciones");
    uint64_t h = 0; for (int i = 0; i < 100000; i++) h = pmmc_alloc_high(&s);
    CHECK(h >= PMMC_LOW_LIMIT, "4 GiB: el monton usa la reserva alta");
}
static void no_room(void)
{
    /* toda la RAM baja ocupada por el firmware salvo 16 MiB: el mapa (de 2 MiB) no cabe -> se recorta a 512 MiB */
    struct pmmc_region r[] = { { 0x1000000, 0x10000 }, { 0x100000000ULL, 60 * GiB } };
    struct pmmc s; memset(&s, 0, sizeof s);
    pmmc_build(&s, r, 2, bm_static, sizeof bm_static * 8, p2p);
    CHECK(s.capped && s.frames == sizeof bm_static * 8 && s.bm == bm_static, "sin sitio: recorte seguro al mapa estatico");
}
static void corrupt(void)
{
    struct pmmc_region r[] = { { 0xFFFFFFFFFFFFF000ULL, 0x10000 }, { 0x100000, 64 * MiB }, { 0x200000000000ULL, 1 } };
    struct pmmc s; memset(&s, 0, sizeof s);
    pmmc_build(&s, r, 3, bm_static, sizeof bm_static * 8, p2p);
    CHECK(s.frames <= PMMC_MAX_PHYS / 4096, "region corrupta: el mapa no pasa de 64 GiB");
    uint64_t a = pmmc_alloc_low(&s);
    CHECK(a >= PMMC_LOWER && a < 0x100000 + 64 * MiB, "region corrupta: solo se entrega RAM real");
}

int main(void)
{
    t480s(); qemu512(); qemu4g(); no_room(); corrupt();
    free(dyn);
    if (fails) { printf("PMM: %d fallos\n", fails); return 1; }
    printf("PMM: reservas baja/alta correctas en los 5 mapas (T480s agotado entero sin errores)\n");
    return 0;
}
