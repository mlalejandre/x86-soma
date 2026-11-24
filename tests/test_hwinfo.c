/* PARCHE 032: pruebas y fuzzing de los analizadores ACPI / FDT (host). make test-hw; con ASAN+UBSAN en make fuzz-hw. */
/* SOMA_302_CI: snprintf en lugar de sprintf (el SDK de macOS lo marca obsoleto y -Werror lo hace error) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/core/hwinfo.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* memoria fisica simulada */
static uint8_t *mem; static size_t memsz;
static const uint8_t *map(uint64_t phys, size_t len, void *ctx) { (void)ctx; if (phys > memsz || len > memsz - phys) return 0; return mem + phys; }
static void p32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }
static void fixsum(uint8_t *p, size_t n, size_t at) { p[at] = 0; uint8_t s = 0; for (size_t i = 0; i < n; i++) s += p[i]; p[at] = (uint8_t)-s; }
static void hdr(uint8_t *t, const char *sig, uint32_t len) { memcpy(t, sig, 4); p32(t + 4, len); t[8] = 1; memcpy(t + 10, "SOMAOS", 6); }

static size_t build_acpi(int xsdt)
{
    memsz = 0x100000; mem = calloc(1, memsz);
    uint8_t *r = mem + 0xF0010; memcpy(r, "RSD PTR ", 8); memcpy(r + 9, "SOMAOS", 6);
    uint8_t *madt = mem + 0x3000; hdr(madt, "APIC", 36 + 8 + 8 * 3 + 12 + 10);
    p32(madt + 36, 0xFEE00000); uint8_t *e = madt + 44;
    for (int i = 0; i < 3; i++) { e[0] = 0; e[1] = 8; e[2] = i; e[3] = i; p32(e + 4, i == 2 ? 0 : 1); e += 8; }   /* el 3.o esta deshabilitado */
    e[0] = 1; e[1] = 12; p32(e + 4, 0xFEC00000); e += 12; e[0] = 2; e[1] = 10; e[3] = 0; p32(e + 4, 2); e[8] = 5; e += 10;
    fixsum(madt, 36 + 8 + 24 + 12 + 10, 9);
    uint8_t *mcfg = mem + 0x4000; hdr(mcfg, "MCFG", 36 + 8 + 16); p64(mcfg + 44, 0xB0000000ULL); mcfg[44 + 10] = 0; mcfg[44 + 11] = 255; fixsum(mcfg, 60, 9);
    uint8_t *hp = mem + 0x5000; hdr(hp, "HPET", 36); fixsum(hp, 36, 9);
    uint8_t *fa = mem + 0x6000; hdr(fa, "FACP", 36); fixsum(fa, 36, 9);
    uint8_t *rs = mem + 0x2000; int es = xsdt ? 8 : 4; hdr(rs, xsdt ? "XSDT" : "RSDT", 36 + 4 * es);
    uint64_t tb[4] = { 0x3000, 0x4000, 0x5000, 0x6000 };
    for (int i = 0; i < 4; i++) { if (xsdt) p64(rs + 36 + i * 8, tb[i]); else p32(rs + 36 + i * 4, (uint32_t)tb[i]); }
    fixsum(rs, 36 + 4 * es, 9);
    if (xsdt) { r[15] = 2; p32(r + 20, 36); p64(r + 24, 0x2000); fixsum(r, 20, 8); /* la suma extendida va al final */ r[8] = 0; fixsum(r, 20, 8); fixsum(r, 36, 32); }
    else { r[15] = 0; p32(r + 16, 0x2000); fixsum(r, 20, 8); }
    return 0xF0010;
}

static void test_acpi(void)
{
    for (int x = 0; x < 2; x++) {
        uint64_t rp = build_acpi(x);
        CHECK(hw_acpi_find_rsdp(0xE0000, 0x100000, map, 0) == rp);
        struct hw_acpi a; CHECK(hw_acpi_parse(rp, map, 0, &a) == 0);
        CHECK(a.ok && a.ntables == 4 && a.ncpu == 2 && a.lapic_addr == 0xFEE00000 && a.nioapic == 1 && a.ioapic_addr[0] == 0xFEC00000);
        CHECK(a.nmcfg == 1 && a.mcfg[0].base == 0xB0000000ULL && a.mcfg[0].bus1 == 255 && a.has_hpet && a.has_fadt && a.has_madt);
        CHECK(a.nirq_ovr == 1 && a.irq_ovr[0].src == 0 && a.irq_ovr[0].gsi == 2 && a.irq_ovr[0].flags == 5);
        CHECK(!strcmp(a.sig[0], "APIC") && !strcmp(a.sig[3], "FACP") && !strcmp(a.oem, "SOMAOS"));
        mem[0x3000 + 20] ^= 1; CHECK(hw_acpi_parse(rp, map, 0, &a) == 0 && a.ntables == 3 && !a.has_madt);   /* suma de la MADT rota: se ignora */
        mem[0x3000 + 20] ^= 1;
        mem[rp + 3] ^= 1; CHECK(hw_acpi_parse(rp, map, 0, &a) != 0);
        free(mem);
    }
}

static void test_virtio(void)
{
    uint8_t c[256]; memset(c, 0, sizeof c); c[6] = 0x10; c[0x34] = 0x40;
    unsigned at[3] = { 0x40, 0x54, 0x68 };
    for (int i = 0; i < 3; i++) {
        uint8_t *q = c + at[i]; q[0] = 9; q[1] = i < 2 ? at[i + 1] : 0; q[2] = i == 1 ? 20 : 16; q[3] = (uint8_t)(i == 0 ? 1 : i == 1 ? 2 : 4); q[4] = (uint8_t)(i + 1);
        p32(q + 8, 0x1000 * (i + 1)); p32(q + 12, 0x100); if (i == 1) p32(q + 16, 4);
    }
    struct hw_virtio_cap v[8]; int n = hw_virtio_caps(c, v, 8);
    CHECK(n == 3 && v[0].type == 1 && v[0].bar == 1 && v[0].offset == 0x1000 && v[1].type == 2 && v[1].notify_mult == 4 && v[2].type == 4 && v[2].offset == 0x3000);
    c[0x40 + 1] = 0x40;                                        /* lista circular: debe terminar */
    n = hw_virtio_caps(c, v, 8); CHECK(n <= 8);
    c[6] = 0; CHECK(hw_virtio_caps(c, v, 8) == 0);
}

/* constructor de FDT */
static uint8_t fb[4096]; static size_t fo; static uint8_t sb[512]; static size_t sl;
static void w32(uint32_t v) { fb[fo] = v >> 24; fb[fo + 1] = v >> 16; fb[fo + 2] = v >> 8; fb[fo + 3] = v; fo += 4; }
static void begin(const char *n) { w32(1); size_t l = strlen(n) + 1; memcpy(fb + fo, n, l); fo += (l + 3) & ~3u; }
static void endn(void) { w32(2); }
static void prop(const char *n, const void *v, uint32_t l)
{
    size_t off = sl; memcpy(sb + sl, n, strlen(n) + 1); sl += strlen(n) + 1;
    w32(3); w32(l); w32((uint32_t)off); memcpy(fb + fo, v, l); fo += (l + 3) & ~3u;
}
static void pcell(const char *n, uint32_t v) { uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v }; prop(n, b, 4); }
static size_t build_fdt(void)
{
    fo = 0x38 * 0 + 0; memset(fb, 0, sizeof fb); sl = 0; fo = 0;
    begin(""); pcell("#address-cells", 2); pcell("#size-cells", 1);
    prop("model", "Raspberry Pi 4 Model B", 23); prop("compatible", "raspberrypi,4-model-b\0brcm,bcm2711", 33);
    begin("chosen"); prop("stdout-path", "serial0:115200n8", 17); endn();
    begin("memory@0"); prop("device_type", "memory", 7); uint8_t r[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x3B, 0x40, 0, 0 }; prop("reg", r, 12); endn();
    begin("cpus"); for (int i = 0; i < 4; i++) { char n[16]; snprintf(n, sizeof n, "cpu@%d", i); begin(n); prop("device_type", "cpu", 4); endn(); } endn();
    begin("soc"); pcell("#address-cells", 1); pcell("#size-cells", 1);
    begin("serial@7e201000"); prop("compatible", "arm,pl011\0arm,primecell", 24); uint8_t g[8] = { 0x7e, 0x20, 0x10, 0, 0, 0, 0x02, 0 }; prop("reg", g, 8); endn();
    endn(); endn(); w32(9);
    size_t structsz = fo, off_struct = 64, off_str = off_struct + structsz, total = off_str + sl;
    uint8_t out[4096]; memset(out, 0, sizeof out);
    memcpy(out + off_struct, fb, structsz); memcpy(out + off_str, sb, sl);
    uint32_t h[10] = { 0xD00DFEED, (uint32_t)total, (uint32_t)off_struct, (uint32_t)off_str, 48, 17, 16, 0, (uint32_t)sl, (uint32_t)structsz };
    for (int i = 0; i < 10; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
    memcpy(fb, out, total); return total;
}

static void test_fdt(void)
{
    size_t n = build_fdt(); struct hw_fdt f;
    CHECK(hw_fdt_parse(fb, n, &f) == 0 && f.ok);
    CHECK(!strcmp(f.model, "Raspberry Pi 4 Model B") && !strcmp(f.compat, "raspberrypi,4-model-b") && !strcmp(f.stdout_path, "serial0:115200n8"));
    CHECK(f.ncpu == 4 && f.nmem == 1 && f.mem_base[0] == 0 && f.mem_size[0] == 0x3B400000ULL && f.nnodes == 10);
    uint64_t b, s; CHECK(hw_fdt_find_reg(fb, n, "arm,pl011", &b, &s) == 0 && b == 0x7e201000ULL && s == 0x200);
    CHECK(hw_fdt_find_reg(fb, n, "arm,primecell", &b, &s) == 0);
    CHECK(hw_fdt_find_reg(fb, n, "no,existe", &b, &s) != 0);
    CHECK(hw_fdt_parse(fb, 20, &f) != 0);
    fb[0] ^= 1; CHECK(hw_fdt_parse(fb, n, &f) != 0); fb[0] ^= 1;
}

/* mutaciones aleatorias: nunca debe salirse de los limites (se ejecuta bajo ASAN/UBSAN) */
static uint32_t rs = 12345; static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static void fuzz(int iters)
{
    size_t n = build_fdt(); uint8_t *base = malloc(n); memcpy(base, fb, n);
    for (int it = 0; it < iters; it++) {
        uint8_t *c = malloc(n); memcpy(c, base, n);               /* exacto: ASAN detecta cualquier lectura mas alla */
        int k = 1 + rnd() % 6; while (k--) { size_t i = rnd() % n; c[i] = (rnd() & 3) ? (uint8_t)rnd() : (rnd() & 1 ? 0xFF : 0); }
        size_t av = (rnd() & 3) ? n : rnd() % (n + 1);
        struct hw_fdt f; uint64_t b, s; uint8_t *cc = malloc(av ? av : 1); memcpy(cc, c, av);
        hw_fdt_parse(cc, av, &f); hw_fdt_find_reg(cc, av, "arm,pl011", &b, &s);
        free(cc); free(c);
    }
    free(base);
    for (int it = 0; it < iters; it++) {
        size_t rp = build_acpi(it & 1); size_t sz = memsz; uint8_t *c = mem;
        int k = 1 + rnd() % 8; while (k--) { size_t i = 0x2000 + rnd() % 0x5000; c[i] = (uint8_t)rnd(); if (rnd() & 1) fixsum(c + 0x3000, 36, 9); }
        if (rnd() & 1) { uint8_t *t = c + 0x3000 + 4; t[0] = rnd(); t[1] = rnd(); }     /* longitud absurda de la MADT */
        struct hw_acpi a; (void)sz; hw_acpi_parse(rp, map, 0, &a); hw_acpi_find_rsdp(0xE0000, 0x100000, map, 0);
        free(mem);
    }
}

static void fuzz_virtio(int iters)
{
    for (int i = 0; i < iters; i++) { uint8_t *c = malloc(256); for (int k = 0; k < 256; k++) c[k] = (rnd() & 7) ? 0 : (uint8_t)rnd(); c[6] = 0x10; c[0x34] = (uint8_t)rnd(); struct hw_virtio_cap v[4]; hw_virtio_caps(c, v, 4); free(c); }
}

int main(int argc, char **argv)
{
    test_acpi(); test_fdt(); test_virtio();
    fuzz_virtio(20000);
    fuzz(argc > 1 ? atoi(argv[1]) : 20000);
    printf(fails ? "hwinfo: %d FALLOS\n" : "hwinfo: todo correcto\n", fails);
    return fails != 0;
}
