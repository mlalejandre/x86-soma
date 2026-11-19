/* PARCHE 089: datos de hardware en x86_64 para el Monitor del sistema: CPUID (fabricante, modelo, nucleos, caches, extensiones,
 * hipervisor), frecuencia del TSC calibrada, tarjeta grafica y dispositivos PCI con nombre para los mas habituales. */
#include "hwmon.h"
#include "pci.h"
#include "sysinfo.h"
#include "smp.h"
#include "mem.h"
#include "ramstat.h"
#include "cpupower.h"

static void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "0"(leaf), "2"(sub));
}
static void cat(char *d, const char *s, int max) { int i = 0; while (d[i]) i++; while (*s && i + 1 < max) d[i++] = *s++; d[i] = 0; }
static void put4(char *o, uint32_t v) { o[0] = (char)v; o[1] = (char)(v >> 8); o[2] = (char)(v >> 16); o[3] = (char)(v >> 24); }
static void trim(char *s) { int i = 0, j = 0; while (s[i] == ' ') i++; while (s[i]) { if (!(s[i] == ' ' && (s[i + 1] == ' ' || !s[i + 1]))) s[j++] = s[i]; i++; } s[j] = 0; }

int hwmon_cpu(struct hw_cpu *o)
{
    memset(o, 0, sizeof *o);
    uint32_t a, b, c, d, maxl, maxe;
    cat(o->arch, "x86_64", sizeof o->arch);
    cpuid(0, 0, &maxl, &b, &c, &d); put4(o->vendor, b); put4(o->vendor + 4, d); put4(o->vendor + 8, c); o->vendor[12] = 0;
    cpuid(1, 0, &a, &b, &c, &d);
    int fam = (int)((a >> 8) & 15), mod = (int)((a >> 4) & 15);
    if (fam == 15) fam += (int)((a >> 20) & 255);
    if (fam == 6 || fam == 15) mod += (int)(((a >> 16) & 15) << 4);
    o->family = fam; o->model = mod; o->stepping = (int)(a & 15);
    o->threads = (int)((b >> 16) & 255); if (o->threads < 1) o->threads = 1;
    uint32_t c1 = c, d1 = d;
    struct { int reg, bit; const char *n; } F[] = {
        { 3, 25, "SSE" }, { 3, 26, "SSE2" }, { 2, 0, "SSE3" }, { 2, 9, "SSSE3" }, { 2, 19, "SSE4.1" }, { 2, 20, "SSE4.2" }, { 2, 23, "POPCNT" },
        { 2, 25, "AES" }, { 2, 28, "AVX" }, { 2, 12, "FMA" }, { 2, 30, "RDRAND" }, { 3, 4, "TSC" }, { 2, 5, "VMX" }, { 2, 31, "VM" } };
    for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++) {
        uint32_t r = F[i].reg == 2 ? c1 : d1;
        if (r & (1u << F[i].bit)) { if (o->feats[0]) cat(o->feats, " ", sizeof o->feats); cat(o->feats, F[i].n, sizeof o->feats); }
    }
    if (maxl >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        if (b & (1u << 5)) cat(o->feats, " AVX2", sizeof o->feats);
        if (b & (1u << 16)) cat(o->feats, " AVX-512", sizeof o->feats);
        if (b & (1u << 29)) cat(o->feats, " SHA", sizeof o->feats);
        if (b & (1u << 3)) cat(o->feats, " BMI1", sizeof o->feats);
        if (b & (1u << 8)) cat(o->feats, " BMI2", sizeof o->feats);
    }
    if (c1 & (1u << 31)) {   /* hipervisor */
        cpuid(0x40000000u, 0, &a, &b, &c, &d); put4(o->hyper, b); put4(o->hyper + 4, c); put4(o->hyper + 8, d); o->hyper[12] = 0;
        if (!memcmp(o->hyper, "TCGTCGTCGTCG", 12)) memcpy(o->hyper, "QEMU (TCG)\0", 11);
        else if (!memcmp(o->hyper, "KVMKVMKVM", 9)) memcpy(o->hyper, "KVM\0", 4);
        else if (!memcmp(o->hyper, "VMwareVMware", 12)) memcpy(o->hyper, "VMware\0", 7);
        else if (!memcmp(o->hyper, "Microsoft Hv", 12)) memcpy(o->hyper, "Hyper-V\0", 8);
    }
    cpuid(0x80000000u, 0, &maxe, &b, &c, &d);
    if (maxe >= 0x80000004u) {
        uint32_t *w = (uint32_t *)o->brand;
        for (uint32_t l = 0; l < 3; l++) cpuid(0x80000002u + l, 0, &w[l * 4], &w[l * 4 + 1], &w[l * 4 + 2], &w[l * 4 + 3]);
        o->brand[48] = 0; trim(o->brand);
    }
    if (!o->brand[0]) cat(o->brand, o->vendor, sizeof o->brand);
    /* caches (hoja 4 en Intel; en AMD 0x80000005/6) */
    if (maxl >= 4 && o->vendor[0] == 'G') {
        for (uint32_t i = 0; i < 8; i++) {
            cpuid(4, i, &a, &b, &c, &d);
            uint32_t type = a & 31; if (!type) break;
            uint32_t lvl = (a >> 5) & 7, ways = ((b >> 22) & 1023) + 1, parts = ((b >> 12) & 1023) + 1, line = (b & 4095) + 1, sets = c + 1;
            uint32_t kb = ways * parts * line * sets / 1024;
            if (lvl == 1 && type == 1) o->l1d_kb = kb; else if (lvl == 1 && type == 2) o->l1i_kb = kb; else if (lvl == 2) o->l2_kb = kb; else if (lvl == 3) o->l3_kb = kb;
            if (i == 0) { o->cores = (int)(((a >> 26) & 63) + 1); }
        }
    } else if (maxe >= 0x80000006u) {
        cpuid(0x80000005u, 0, &a, &b, &c, &d); o->l1d_kb = c >> 24; o->l1i_kb = d >> 24;
        cpuid(0x80000006u, 0, &a, &b, &c, &d); o->l2_kb = c >> 16; o->l3_kb = (d >> 18) * 512;
        if (maxe >= 0x80000008u) { cpuid(0x80000008u, 0, &a, &b, &c, &d); o->cores = (int)((c & 255) + 1); }
    }
    if (o->cores < 1) o->cores = 1;
    if (o->cores > o->threads) o->threads = o->cores;
    o->mhz = (uint32_t)tsc_freq_mhz; o->max_mhz = o->mhz;
    o->temp_mc = -1; o->volt_mv = -1;
    return 0;
}

struct pname { uint16_t v, d; const char *n; };
static const struct pname pnames[] = {
    { 0x1234, 0x1111, "Tarjeta grafica Bochs/QEMU (VBE)" }, { 0x1af4, 0x1000, "virtio-net (red)" }, { 0x1af4, 0x1041, "virtio-net (red)" },
    { 0x1af4, 0x1001, "virtio-blk (disco)" }, { 0x1af4, 0x1042, "virtio-blk (disco)" }, { 0x1af4, 0x1050, "virtio-gpu" }, { 0x1af4, 0x1052, "virtio-input" },
    { 0x1b36, 0x000d, "Controlador USB 3 xHCI (QEMU)" }, { 0x1b36, 0x0010, "NVMe (QEMU)" }, { 0x1b36, 0x0008, "Puente PCIe (QEMU)" },
    { 0x8086, 0x29c0, "Intel Q35 (puente de memoria)" }, { 0x8086, 0x2918, "Intel ICH9 (puente LPC)" }, { 0x8086, 0x2922, "Intel ICH9 SATA AHCI" },
    { 0x8086, 0x2930, "Intel ICH9 SMBus" }, { 0x8086, 0x293e, "Intel ICH9 audio HD" }, { 0x8086, 0x2668, "Intel ICH6 audio HD" }, { 0x8086, 0x100e, "Intel e1000 (red)" },
    { 0x8086, 0x10d3, "Intel e1000e (red)" }, { 0x8086, 0x1237, "Intel 440FX (puente de memoria)" }, { 0x8086, 0x7000, "Intel PIIX3 (ISA)" }, { 0x8086, 0x7010, "Intel PIIX3 IDE" },
    { 0x8086, 0x7113, "Intel PIIX4 ACPI" }, { 0x10de, 0, "NVIDIA" }, { 0x1002, 0, "AMD/ATI" }, { 0x8086, 0, "Intel" }, { 0x10ec, 0, "Realtek" }, { 0x15ad, 0, "VMware" },
    { 0, 0, 0 } };
static const char *cls_name(uint32_t cls)
{
    switch (cls >> 8) {
    case 0x0100: return "SCSI"; case 0x0101: return "IDE"; case 0x0106: return "SATA"; case 0x0108: return "NVMe"; case 0x0200: return "Ethernet"; case 0x0280: return "Red";
    case 0x0300: return "VGA"; case 0x0380: return "Pantalla"; case 0x0403: return "Audio"; case 0x0600: return "Puente host"; case 0x0601: return "Puente ISA"; case 0x0604: return "Puente PCI";
    case 0x0c03: return "USB"; case 0x0c05: return "SMBus"; case 0x0880: return "Sistema"; default: return "Otro";
    }
}
int hwmon_devices(struct hw_dev *o, int max)
{
    int n = 0;
    for (int bus = 0; bus < 4 && n < max; bus++) for (int slot = 0; slot < 32 && n < max; slot++) for (int fn = 0; fn < 8 && n < max; fn++) {
        uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
        if (v == 0xFFFF || v == 0) { if (fn == 0) break; continue; }
        uint16_t dv = pci_device_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
        uint32_t cls = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 8) >> 8;
        struct hw_dev *e = &o[n++]; memset(e, 0, sizeof *e);
        cat(e->bus, "PCI", sizeof e->bus); e->vendor = v; e->device = dv; e->cls = cls;
        const char *nm = 0;
        for (const struct pname *p = pnames; p->n; p++) if (p->v == v && (p->d == dv || p->d == 0)) { nm = p->n; if (p->d == dv) break; }
        cat(e->name, nm ? nm : cls_name(cls), sizeof e->name);
        cat(e->extra, cls_name(cls), sizeof e->extra);
        char t[24] = " "; const char *hx = "0123456789abcdef"; int k = 1;
        t[k++] = (char)('0' + bus); t[k++] = ':'; t[k++] = hx[slot >> 4]; t[k++] = hx[slot & 15]; t[k++] = '.'; t[k++] = (char)('0' + fn); t[k] = 0;
        cat(e->extra, t, sizeof e->extra);
        if (fn == 0) { uint32_t h = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 12); if (!((h >> 16) & 0x80)) break; }
    }
    return n;
}
int hwmon_gpu(struct hw_gpu *o)
{
    memset(o, 0, sizeof *o); o->temp_mc = -1;
    struct hw_dev d[48]; int n = hwmon_devices(d, 48);
    for (int i = 0; i < n; i++) if ((d[i].cls >> 16) == 0x03) { cat(o->name, d[i].name, sizeof o->name); cat(o->vendor, d[i].vendor == 0x1234 ? "QEMU" : d[i].vendor == 0x10de ? "NVIDIA" : d[i].vendor == 0x1002 ? "AMD" : d[i].vendor == 0x8086 ? "Intel" : "?", sizeof o->vendor); break; }
    if (!o->name[0]) cat(o->name, "Framebuffer VGA", sizeof o->name);
    o->mem_mb = 16;
    return 0;
}
int hwmon_board(struct hw_board *o)
{
    memset(o, 0, sizeof *o);
    struct hw_cpu c; hwmon_cpu(&c);
    cat(o->model, c.hyper[0] ? "Maquina virtual " : "PC ", sizeof o->model); cat(o->model, c.hyper[0] ? c.hyper : "x86_64", sizeof o->model);
    struct ramstat_info r; ramstat_get(&r); o->ram_mb = (uint32_t)(r.total >> 20);
    cat(o->firmware, "BIOS + GRUB (multiboot)", sizeof o->firmware);
    return 0;
}
/* SOMA_166_T480S: temperatura del paquete y frecuencia real del nucleo 0 (antes: no disponible y la del TSC) */
int hwmon_live(int *temp_mc, uint32_t *mhz)
{
    int t; uint32_t f, mw;
    int r = cpupower_live(&t, &f, &mw);
    *temp_mc = t > -1000 ? t * 1000 : -1;
    *mhz = f ? f : (uint32_t)tsc_freq_mhz;
    return r;
}
