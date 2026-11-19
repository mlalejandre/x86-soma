/* SOMA_158_T480S: prueba del analizador de la MADT y del enrutado de IRQ ISA (src/core/hwinfo.c) en el anfitrion.
 * Construye RSDP 2.0 + XSDT + MADT como las de un ThinkPad T480s (8 hilos: APIC 0,2,4,6 + 1,3,5,7; IOAPIC en 0xFEC00000 con
 * GSI base 0; IRQ0 -> GSI 2; IRQ9 -> GSI 9 por nivel; NMI en LINT1) y comprueba lo que se extrae. Despues, mutaciones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwinfo.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

#define BASE 0x8FFF0000ULL                 /* direccion "fisica" de la memoria simulada (como el ACPI del T480s) */
static uint8_t mem[16384]; static size_t memlen = sizeof mem;
static const uint8_t *map(uint64_t phys, size_t len, void *ctx)
{
    (void)ctx;
    if (phys < BASE || phys - BASE + len > memlen) return 0;
    return mem + (phys - BASE);
}
static void sum(uint8_t *p, size_t n, size_t at) { uint8_t s = 0; p[at] = 0; for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); p[at] = (uint8_t)-s; }
static void hdr(uint8_t *t, const char *sig, uint32_t len) { memcpy(t, sig, 4); memcpy(t + 4, &len, 4); t[8] = 1; memcpy(t + 10, "LENOVO", 6); }
static size_t madt_len;
static void build(void)
{
    memset(mem, 0, sizeof mem);
    uint8_t *r = mem;                                           /* RSDP 2.0 en BASE */
    memcpy(r, "RSD PTR ", 8); memcpy(r + 9, "LENOVO", 6); r[15] = 2;
    uint32_t l = 36; memcpy(r + 20, &l, 4);
    uint64_t xs = BASE + 0x100; memcpy(r + 24, &xs, 8);
    sum(r, 20, 8); sum(r, 36, 32);
    uint8_t *x = mem + 0x100; uint32_t xl = 36 + 8; hdr(x, "XSDT", xl);   /* XSDT con una entrada: la MADT */
    uint64_t ma = BASE + 0x200; memcpy(x + 36, &ma, 8); sum(x, xl, 9);
    uint8_t *m = mem + 0x200; size_t p = 44;
    uint32_t la = 0xFEE00000; memcpy(m + 36, &la, 4); uint32_t pcat = 1; memcpy(m + 40, &pcat, 4);
    const uint8_t ids[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
    for (int i = 0; i < 8; i++) { uint8_t e[8] = { 0, 8, (uint8_t)(i + 1), ids[i], 1, 0, 0, 0 }; memcpy(m + p, e, 8); p += 8; }
    { uint8_t e[8] = { 0, 8, 9, 0xFF, 0, 0, 0, 0 }; memcpy(m + p, e, 8); p += 8; }          /* hueco deshabilitado (comun en portatiles) */
    { uint8_t e[16] = { 9, 16, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0 }; memcpy(m + p, e, 16); p += 16; }   /* x2APIC duplicado de la 2 */
    { uint8_t e[12] = { 1, 12, 2, 0 }; uint32_t a = 0xFEC00000, g = 0; memcpy(e + 4, &a, 4); memcpy(e + 8, &g, 4); memcpy(m + p, e, 12); p += 12; }
    { uint8_t e[10] = { 2, 10, 0, 0 }; uint32_t g = 2; memcpy(e + 4, &g, 4); e[8] = 0; e[9] = 0; memcpy(m + p, e, 10); p += 10; }
    { uint8_t e[10] = { 2, 10, 0, 9 }; uint32_t g = 9; memcpy(e + 4, &g, 4); e[8] = 0x0D; e[9] = 0; memcpy(m + p, e, 10); p += 10; }   /* nivel, alta */
    { uint8_t e[6] = { 4, 6, 0xFF, 0x05, 0x00, 1 }; memcpy(m + p, e, 6); p += 6; }          /* NMI: todos, LINT1 */
    madt_len = p; hdr(m, "APIC", (uint32_t)p); sum(m, p, 9);
}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    build();
    struct hw_acpi a;
    CHECK(hw_acpi_parse(BASE, map, 0, &a) == 0, "RSDP/XSDT/MADT validos");
    CHECK(a.has_madt && a.lapic_addr == 0xFEE00000, "LAPIC en 0xFEE00000");
    CHECK(a.ncpu == 8, "8 procesadores habilitados (sin contar el hueco ni el duplicado x2APIC)");
    CHECK(a.nlapic == 9, "9 entradas distintas (8 + el hueco deshabilitado)");
    int en = 0, ids = 0; for (int i = 0; i < a.nlapic; i++) if (a.lapic[i].enabled) { en++; ids |= 1 << a.lapic[i].apic_id; }
    CHECK(en == 8 && ids == 0xFF, "IDs de APIC 0..7 habilitados");
    CHECK(a.nioapic == 1 && a.ioapic_addr[0] == 0xFEC00000 && a.ioapic_id[0] == 2 && a.ioapic_gsi[0] == 0, "IOAPIC 2 en 0xFEC00000, GSI base 0");
    CHECK(a.nmi_lint == 1, "NMI en LINT1");
    uint32_t g; int lo, lv;
    hw_isa_irq_route(&a, 0, &g, &lo, &lv); CHECK(g == 2 && !lo && !lv, "IRQ0 -> GSI 2, flanco, alta");
    hw_isa_irq_route(&a, 1, &g, &lo, &lv); CHECK(g == 1 && !lo && !lv, "IRQ1 (teclado) -> GSI 1, flanco, alta");
    hw_isa_irq_route(&a, 12, &g, &lo, &lv); CHECK(g == 12 && !lo && !lv, "IRQ12 (trackpad) -> GSI 12, flanco, alta");
    hw_isa_irq_route(&a, 9, &g, &lo, &lv); CHECK(g == 9 && !lo && lv, "IRQ9 (SCI) -> GSI 9, por nivel");
    CHECK(hw_ioapic_for_gsi(&a, 12) == 0 && hw_ioapic_for_gsi(&a, 2) == 0, "IOAPIC que atiende cada GSI");
    hw_isa_irq_route(0, 5, &g, &lo, &lv); CHECK(g == 5 && !lo && !lv, "sin MADT: identidad ISA");
    /* robustez: mutaciones de la MADT (con la suma corregida para que pase el filtro y llegue al analizador) */
    uint8_t keep[sizeof mem]; memcpy(keep, mem, sizeof mem);
    srand(158);
    for (int it = 0; it < iters; it++) {
        memcpy(mem, keep, sizeof mem);
        uint8_t *m = mem + 0x200;
        int flips = 1 + rand() % 6;
        for (int f = 0; f < flips; f++) m[44 + rand() % (int)(madt_len - 44)] = (uint8_t)rand();
        if (rand() % 3 == 0) { uint32_t nl = (uint32_t)(36 + rand() % (int)(madt_len + 64)); memcpy(m + 4, &nl, 4); }
        uint32_t tl; memcpy(&tl, m + 4, 4);
        if (tl >= 36 && 0x200 + tl <= sizeof mem) sum(m, tl, 9);
        struct hw_acpi b;
        (void)hw_acpi_parse(BASE, map, 0, &b);
        CHECK(b.nlapic <= HW_MAX_CPU && b.nioapic <= HW_MAX_IOAPIC && b.nirq_ovr <= 16, "limites respetados");
        for (int q = 0; q < 16; q++) { hw_isa_irq_route(&b, (uint8_t)q, &g, &lo, &lv); }
        (void)hw_ioapic_for_gsi(&b, (uint32_t)rand());
        if (fails > 5) break;
    }
    if (fails) { printf("MADT: %d fallos\n", fails); return 1; }
    printf("MADT: T480s sintetico correcto (8 hilos, IOAPIC, IRQ0->GSI2, SCI por nivel, NMI LINT1) y %d mutaciones sin errores\n", iters);
    return 0;
}
