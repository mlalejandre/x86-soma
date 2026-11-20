/* SOMA_156_T480S: inventario del hardware (orden "hw"). Ver hwscan.h.
 * Referencias: Linux arch/x86/kernel/cpu/common.c y cacheinfo.c (CPUID), drivers/firmware/dmi_scan.c (SMBIOS por EFI). */
#include "hwscan.h"
#include "hwinfo.h"
#include "bootfb.h"
#include "vmm.h"
#include "memprot.h"
#include "pmm.h"
#include "fb.h"
#include "sysinfo.h"
#include "mem.h"
#include "idt.h"
#include "smp.h"
#include "soma_str.h"

/* ------------------------------------------------------------------ texto acotado */
static void tmb(soma_sbuf_t *t, uint64_t mb)            /* "512 MiB" / "7,8 GiB" */
{
    if (mb < 1024) { soma_sbuf_append_u64(t, mb); soma_sbuf_append_str(t, " MiB"); return; }
    soma_sbuf_append_u64(t, mb / 1024); soma_sbuf_append_char(t, ','); soma_sbuf_append_u64(t, (mb % 1024) * 10 / 1024); soma_sbuf_append_str(t, " GiB");
}

/* ------------------------------------------------------------------ memoria fisica de solo lectura */
#define HW_PHYS_LIMIT 0x100000000ULL
#define HW_MAP_MAX    (4u << 20)                      /* una tabla del firmware nunca necesita mas */
const uint8_t *hw_map_phys(uint64_t phys, size_t len, void *ctx)
{
    (void)ctx;
    if (!len || phys + len < phys || phys + len > HW_PHYS_LIMIT || len > HW_MAP_MAX) return 0;
    uint64_t a = phys & ~0xFFFULL, e = (phys + len + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t va = a; va < e; va += 4096) {
        uint64_t cur = vmm_virt_to_phys(va);
        if (cur == va) continue;                              /* ya en identidad (el primer GiB, o proyectada antes) */
        if (cur != 0) return 0;                               /* esa direccion virtual es otra cosa (p. ej. el montón) */
        if (va < 0x40000000ULL) return 0;                     /* hueco dentro del primer GiB: no se toca */
        if (va >= FB_VIRT_ADDR && va < FB_VIRT_ADDR + 0x4000000ULL) return 0;   /* ventana del framebuffer */
        if (vmm_map_page(va, va, VMM_FLAG_PRESENT | VMM_FLAG_NX) != 0) return 0;   /* sin WRITABLE: solo lectura */
    }
    return (const uint8_t *)(uintptr_t)phys;
}

uint64_t hw_find_rsdp(void)
{
    uint64_t rsdp = bootinfo_rsdp();
    if (rsdp) return rsdp;
    uintptr_t ea = (uintptr_t)memprot_page0() + 0x40E; __asm__ volatile ("" : "+r"(ea));   /* SOMA_192: por el alias si la pagina 0 no esta mapeada */                 /* segmento de la EBDA en el area de la BIOS */
    uint64_t ebda = (uint64_t)(*(const volatile uint16_t *)ea) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) rsdp = hw_acpi_find_rsdp(ebda, ebda + 1024, hw_map_phys, 0);
    if (!rsdp) rsdp = hw_acpi_find_rsdp(0xE0000, 0x100000, hw_map_phys, 0);
    return rsdp;
}

/* ------------------------------------------------------------------ CPU */
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    __asm__ volatile ("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}
struct cpu_topo { uint32_t threads_per_core, logical, cores; };
static void cpu_topology(struct cpu_topo *o)
{
    uint32_t r[4]; cpuid(0, 0, r);
    uint32_t maxl = r[0];
    o->threads_per_core = 1; o->logical = 1; o->cores = 1;
    if (maxl >= 0x0B) {                                       /* hoja 0Bh: niveles SMT (1) y nucleo (2) */
        for (uint32_t sub = 0; sub < 8; sub++) {
            cpuid(0x0B, sub, r);
            uint32_t type = (r[2] >> 8) & 0xFF, cnt = r[1] & 0xFFFF;
            if (!type) break;
            if (type == 1 && cnt) o->threads_per_core = cnt;
            if (type == 2 && cnt) o->logical = cnt;
        }
        if (o->logical < o->threads_per_core) o->logical = o->threads_per_core;
    } else {
        cpuid(1, 0, r);
        uint32_t l = (r[1] >> 16) & 0xFF;
        if ((r[3] >> 28) & 1 && l) o->logical = l;
    }
    o->cores = o->logical / (o->threads_per_core ? o->threads_per_core : 1);
    if (!o->cores) o->cores = 1;
}
static void cpu_text(soma_sbuf_t *t, int brief)
{
    uint32_t r[4], maxl, maxe;
    char vendor[13]; cpuid(0, 0, r); maxl = r[0];
    *(uint32_t *)(vendor + 0) = r[1]; *(uint32_t *)(vendor + 4) = r[3]; *(uint32_t *)(vendor + 8) = r[2]; vendor[12] = 0;
    cpuid(0x80000000u, 0, r); maxe = r[0];
    char brand[49] = { 0 };
    if (maxe >= 0x80000004u) {
        for (uint32_t i = 0; i < 3; i++) { cpuid(0x80000002u + i, 0, r); for (int k = 0; k < 4; k++) *(uint32_t *)(brand + i * 16 + k * 4) = r[k]; }
    }
    const char *bn = brand; while (*bn == ' ') bn++;
    struct cpu_topo tp; cpu_topology(&tp);
    soma_sbuf_append_str(t, "CPU: "); soma_sbuf_append_str(t, *bn ? bn : vendor); soma_sbuf_append_str(t, "\n");
    soma_sbuf_append_str(t, "  nucleos "); soma_sbuf_append_u64(t, tp.cores); soma_sbuf_append_str(t, ", hilos "); soma_sbuf_append_u64(t, tp.logical);
    soma_sbuf_append_str(t, " (SOMA usa "); soma_sbuf_append_u64(t, (uint32_t)smp_cpus()); soma_sbuf_append_str(t, ")");   /* SOMA_159_T480S */
    if (maxl >= 0x16) { cpuid(0x16, 0, r); if (r[0]) { soma_sbuf_append_str(t, " | base "); soma_sbuf_append_u64(t, r[0]); soma_sbuf_append_str(t, " MHz, turbo "); soma_sbuf_append_u64(t, r[1]); soma_sbuf_append_str(t, " MHz"); } }
    soma_sbuf_append_str(t, " | TSC "); soma_sbuf_append_u64(t, tsc_freq_mhz); soma_sbuf_append_str(t, " MHz\n");
    if (brief) return;
    cpuid(1, 0, r);
    uint32_t fam = (r[0] >> 8) & 15, mod = (r[0] >> 4) & 15, step = r[0] & 15;
    if (fam == 15) fam += (r[0] >> 20) & 0xFF;
    if (fam == 6 || fam >= 15) mod |= ((r[0] >> 16) & 15) << 4;
    uint32_t ecx1 = r[2], edx1 = r[3];
    soma_sbuf_append_str(t, "  fabricante "); soma_sbuf_append_str(t, vendor); soma_sbuf_append_str(t, ", familia "); soma_sbuf_append_u64(t, fam); soma_sbuf_append_str(t, ", modelo 0x"); soma_sbuf_append_hex(t, mod, 2, 1);
    soma_sbuf_append_str(t, ", revision "); soma_sbuf_append_u64(t, step); soma_sbuf_append_str(t, "\n");
    uint32_t ebx7 = 0, ecx7 = 0, edx7 = 0, eax6 = 0, edx8e = 0, edx81 = 0;
    if (maxl >= 7) { cpuid(7, 0, r); ebx7 = r[1]; ecx7 = r[2]; edx7 = r[3]; }
    if (maxl >= 6) { cpuid(6, 0, r); eax6 = r[0]; }
    if (maxe >= 0x80000007u) { cpuid(0x80000007u, 0, r); edx8e = r[3]; }
    if (maxe >= 0x80000001u) { cpuid(0x80000001u, 0, r); edx81 = r[3]; }
    struct { int on; const char *n; } f[] = {
        { (int)((edx1 >> 26) & 1), "SSE2" }, { (int)((ecx1 >> 20) & 1), "SSE4.2" }, { (int)((ecx1 >> 28) & 1), "AVX" },
        { (int)((ebx7 >> 5) & 1), "AVX2" }, { (int)((ebx7 >> 16) & 1), "AVX-512" }, { (int)((ecx1 >> 12) & 1), "FMA" },
        { (int)((ecx1 >> 25) & 1), "AES-NI" }, { (int)((ebx7 >> 29) & 1), "SHA" }, { (int)((ecx1 >> 30) & 1), "RDRAND" },
        { (int)((ebx7 >> 18) & 1), "RDSEED" }, { (int)((ecx1 >> 26) & 1), "XSAVE" }, { (int)((ebx7 >> 9) & 1), "ERMS" },
        { (int)((edx7 >> 4) & 1), "FSRM" }, { (int)((ecx1 >> 21) & 1), "x2APIC" }, { (int)((ecx1 >> 24) & 1), "TSC-deadline" },
        { (int)((edx8e >> 8) & 1), "TSC-invariante" }, { (int)((eax6 >> 7) & 1), "HWP" }, { (int)((eax6 >> 1) & 1), "Turbo" },
        { (int)((edx81 >> 26) & 1), "paginas-1GiB" }, { (int)((ecx1 >> 5) & 1), "VMX" }, { (int)((ebx7 >> 20) & 1), "SMAP" },
        { (int)((ebx7 >> 7) & 1), "SMEP" }, { (int)((ecx7 >> 2) & 1), "UMIP" },
    };
    soma_sbuf_append_str(t, "  extensiones:");
    int col = 15;
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) if (f[i].on) {
        int l = 0; while (f[i].n[l]) l++;
        if (col + l + 1 > 92) { soma_sbuf_append_str(t, "\n              "); col = 14; }
        soma_sbuf_append_char(t, ' '); soma_sbuf_append_str(t, f[i].n); col += l + 1;
    }
    soma_sbuf_append_str(t, "\n");
    int amd = vendor[0] == 'A' && vendor[1] == 'u';           /* AMD describe las caches en 8000001Dh (mismo formato) */
    uint32_t cleaf = amd ? 0x8000001Du : 4u;
    if (amd ? maxe >= 0x8000001Du : maxl >= 4) {              /* caches (hoja 4 en Intel) */
        soma_sbuf_append_str(t, "  caches:");
        for (uint32_t sub = 0; sub < 8; sub++) {
            cpuid(cleaf, sub, r);
            uint32_t type = r[0] & 31; if (!type) break;
            uint32_t lvl = (r[0] >> 5) & 7;
            uint64_t sz = (uint64_t)((r[1] >> 22) + 1) * (((r[1] >> 12) & 0x3FF) + 1) * ((r[1] & 0xFFF) + 1) * (r[2] + 1);
            uint32_t share = ((r[0] >> 14) & 0xFFF) + 1;
            soma_sbuf_append_str(t, " L"); soma_sbuf_append_u64(t, lvl); soma_sbuf_append_str(t, type == 1 ? "d " : type == 2 ? "i " : " ");
            if (sz >= (1u << 20)) { soma_sbuf_append_u64(t, sz >> 20); soma_sbuf_append_str(t, " MiB"); } else { soma_sbuf_append_u64(t, sz >> 10); soma_sbuf_append_str(t, " KiB"); }
            if (share > tp.threads_per_core) soma_sbuf_append_str(t, " (compartida)");
            soma_sbuf_append_char(t, ';');
        }
        soma_sbuf_append_str(t, "\n");
    }
}

/* ------------------------------------------------------------------ memoria */
static const char *mmap_type(uint32_t ty)
{
    switch (ty) { case 1: return "libre"; case 2: return "reservada"; case 3: return "ACPI (recuperable)"; case 4: return "ACPI NVS";
                  case 5: return "defectuosa"; default: return "otra"; }
}
static void mem_text(soma_sbuf_t *t, int brief, int verbose)
{
    uint64_t tot[6] = { 0 }, top = 0, big_base = 0, big_len = 0; int n = 0;
    uint32_t it = 0; uint64_t base, len; uint32_t ty;
    while (bootinfo_mmap_next(&it, &base, &len, &ty)) {
        n++;
        tot[ty <= 5 ? ty : 0] += len;
        if (ty == 1) { if (base + len > top) top = base + len; if (len > big_len) { big_len = len; big_base = base; } }
    }
    size_t fr = 0, us = 0, totf = 0; pmm_get_stats(&fr, &us, &totf);
    soma_sbuf_append_str(t, "RAM: ");
    if (n) { tmb(t, tot[1] >> 20); soma_sbuf_append_str(t, " utilizables segun el firmware"); }
    else soma_sbuf_append_str(t, "(sin mapa de Multiboot 2)");
    soma_sbuf_append_str(t, " | SOMA gestiona "); tmb(t, (uint64_t)totf * 4096 >> 20); soma_sbuf_append_str(t, ", libres "); tmb(t, (uint64_t)fr * 4096 >> 20); soma_sbuf_append_str(t, "\n");
    if (brief || !n) return;
    soma_sbuf_append_str(t, "  mapa del firmware: "); soma_sbuf_append_u64(t, (uint64_t)n); soma_sbuf_append_str(t, " regiones; RAM hasta 0x"); soma_sbuf_append_hex(t, top, 10, 1);
    soma_sbuf_append_str(t, " ("); tmb(t, top >> 20); soma_sbuf_append_str(t, ")\n");
    for (uint32_t k = 1; k <= 5; k++) if (tot[k]) {
        soma_sbuf_append_str(t, "  "); soma_sbuf_append_str(t, mmap_type(k)); soma_sbuf_append_str(t, ": ");
        if (tot[k] < (1u << 20)) { soma_sbuf_append_u64(t, tot[k] >> 10); soma_sbuf_append_str(t, " KiB"); } else tmb(t, (tot[k] + (1u << 19)) >> 20);
        if (k == 2) soma_sbuf_append_str(t, " (rangos de direcciones: incluye dispositivos, no es RAM)");
        soma_sbuf_append_str(t, "\n");
    }
    soma_sbuf_append_str(t, "  mayor bloque libre: 0x"); soma_sbuf_append_hex(t, big_base, 10, 1); soma_sbuf_append_str(t, " + "); tmb(t, big_len >> 20); soma_sbuf_append_str(t, "\n");
    {   /* SOMA_157: reservas del PMM y limite del montón */
        size_t lf = 0, hf = 0; pmm_get_pools(&lf, &hf);
        soma_sbuf_append_str(t, "  reserva baja (identidad, DMA) libre: "); tmb(t, (uint64_t)lf * 4096 >> 20);
        soma_sbuf_append_str(t, " | reserva alta (monton) libre: "); tmb(t, (uint64_t)hf * 4096 >> 20); soma_sbuf_append_str(t, "\n");
        soma_sbuf_append_str(t, "  monton: limite "); tmb(t, (uint64_t)kheap_max_bytes() >> 20); soma_sbuf_append_str(t, " (ramtest [MiB] comprueba la memoria alta)\n");
    }
    if (verbose) {
        it = 0; int shown = 0;
        while (bootinfo_mmap_next(&it, &base, &len, &ty) && shown < 64) {
            soma_sbuf_append_str(t, "    0x"); soma_sbuf_append_hex(t, base, 10, 1); soma_sbuf_append_str(t, " - 0x"); soma_sbuf_append_hex(t, base + len, 10, 1); soma_sbuf_append_str(t, "  ");
            if (len >= (1u << 20)) { tmb(t, len >> 20); } else { soma_sbuf_append_u64(t, len >> 10); soma_sbuf_append_str(t, " KiB"); }
            soma_sbuf_append_str(t, "  "); soma_sbuf_append_str(t, mmap_type(ty)); soma_sbuf_append_str(t, "\n"); shown++;
        }
    } else soma_sbuf_append_str(t, "  (hw mem -v: lista completa de regiones)\n");
}

/* ------------------------------------------------------------------ SMBIOS */
static int meq(const uint8_t *a, const uint8_t *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static int smbios_load(struct hw_smbios *o, const char **how)
{
    static const uint8_t g3[16] = { 0x44, 0x15, 0xFD, 0xF2, 0x94, 0x97, 0x2C, 0x4A, 0x99, 0x2E, 0xE5, 0xBB, 0xCF, 0x20, 0xE3, 0x94 };
    static const uint8_t g2[16] = { 0x31, 0x2D, 0x9D, 0xEB, 0x88, 0x2D, 0xD3, 0x11, 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D };
    uint64_t ep = 0;
    uint64_t st = bootinfo_efi_systab();
    if (st) {                                                 /* UEFI: tabla de configuracion de la tabla de sistema */
        const uint8_t *s = hw_map_phys(st, 0x78, 0);
        if (s && meq(s, (const uint8_t *)"IBI SYST", 8)) {
            uint64_t ne = *(const uint64_t *)(s + 0x68), ct = *(const uint64_t *)(s + 0x70);
            if (ne > 64) ne = 64;
            const uint8_t *c = ne ? hw_map_phys(ct, (size_t)ne * 24, 0) : 0;
            for (int pass = 0; c && pass < 2 && !ep; pass++)
                for (uint64_t i = 0; i < ne && !ep; i++)
                    if (meq(c + i * 24, pass ? g2 : g3, 16)) ep = *(const uint64_t *)(c + i * 24 + 16);
            if (ep) *how = "tabla de configuracion EFI";
        }
    }
    if (!ep) {                                                /* BIOS heredada: zona F0000-FFFFF */
        const uint8_t *z = hw_map_phys(0xF0000, 0x10000, 0);
        for (uint32_t off = 0; z && off + 32 <= 0x10000 && !ep; off += 16)
            if (meq(z + off, (const uint8_t *)"_SM3_", 5) || meq(z + off, (const uint8_t *)"_SM_", 4)) { ep = 0xF0000 + off; *how = "zona BIOS F0000"; }
    }
    if (!ep) return -1;
    const uint8_t *e = hw_map_phys(ep, 32, 0);
    uint64_t tp; uint32_t tl; uint8_t ma, mi;
    if (!e || hw_smbios_entry(e, 32, &tp, &tl, &ma, &mi) != 0) return -1;
    if (tl > 65536) tl = 65536;
    const uint8_t *tab = hw_map_phys(tp, tl, 0);
    if (!tab) return -1;
    return hw_smbios_parse(tab, tl, ma, mi, o);
}
static void smbios_text(soma_sbuf_t *t, int brief)
{
    static struct hw_smbios s;
    const char *how = "";
    if (smbios_load(&s, &how) != 0) { soma_sbuf_append_str(t, "Equipo: SMBIOS no disponible\n"); return; }
    soma_sbuf_append_str(t, "Equipo: "); soma_sbuf_append_str(t, s.sys_vendor); soma_sbuf_append_char(t, ' '); soma_sbuf_append_str(t, s.sys_version[0] ? s.sys_version : s.sys_family);
    soma_sbuf_append_str(t, " ("); soma_sbuf_append_str(t, s.sys_product); soma_sbuf_append_str(t, ")\n");
    if (brief) return;
    soma_sbuf_append_str(t, "  SMBIOS "); soma_sbuf_append_u64(t, s.major); soma_sbuf_append_char(t, '.'); soma_sbuf_append_u64(t, s.minor); soma_sbuf_append_str(t, ", "); soma_sbuf_append_u64(t, (uint64_t)s.nstructs);
    soma_sbuf_append_str(t, " estructuras ("); soma_sbuf_append_str(t, how); soma_sbuf_append_str(t, ")\n");
    soma_sbuf_append_str(t, "  BIOS: "); soma_sbuf_append_str(t, s.bios_vendor); soma_sbuf_append_char(t, ' '); soma_sbuf_append_str(t, s.bios_version); soma_sbuf_append_str(t, ", "); soma_sbuf_append_str(t, s.bios_date); soma_sbuf_append_str(t, "\n");
    if (s.board_product[0]) { soma_sbuf_append_str(t, "  placa: "); soma_sbuf_append_str(t, s.board_product); soma_sbuf_append_str(t, "\n"); }
    if (s.cpu_name[0]) {
        soma_sbuf_append_str(t, "  procesador: "); soma_sbuf_append_str(t, s.cpu_name);
        if (s.cpu_cores) { soma_sbuf_append_str(t, " | "); soma_sbuf_append_u64(t, s.cpu_cores); soma_sbuf_append_str(t, " nucleos / "); soma_sbuf_append_u64(t, s.cpu_threads); soma_sbuf_append_str(t, " hilos"); }
        if (s.cpu_max_mhz) { soma_sbuf_append_str(t, " | max "); soma_sbuf_append_u64(t, s.cpu_max_mhz); soma_sbuf_append_str(t, " MHz"); }
        soma_sbuf_append_str(t, "\n");
    }
    uint64_t inst = 0;
    for (int i = 0; i < s.nmem; i++) inst += s.mem[i].size_mb;
    soma_sbuf_append_str(t, "  memoria instalada: "); tmb(t, inst);
    if (s.mem_max_mb) { soma_sbuf_append_str(t, " (la placa admite hasta "); tmb(t, s.mem_max_mb); soma_sbuf_append_char(t, ')'); }
    soma_sbuf_append_str(t, "\n");
    for (int i = 0; i < s.nmem; i++) {
        soma_sbuf_append_str(t, "    "); soma_sbuf_append_str(t, s.mem[i].locator[0] ? s.mem[i].locator : "zocalo"); soma_sbuf_append_str(t, ": ");
        if (!s.mem[i].size_mb) { soma_sbuf_append_str(t, "vacio\n"); continue; }
        tmb(t, s.mem[i].size_mb); soma_sbuf_append_char(t, ' '); soma_sbuf_append_str(t, hw_smbios_memtype(s.mem[i].type));
        if (s.mem[i].mhz) { soma_sbuf_append_char(t, ' '); soma_sbuf_append_u64(t, s.mem[i].mhz); soma_sbuf_append_str(t, " MT/s"); }
        if (s.mem[i].part[0]) { soma_sbuf_append_str(t, " ["); soma_sbuf_append_str(t, s.mem[i].part); soma_sbuf_append_char(t, ']'); }
        soma_sbuf_append_str(t, "\n");
    }
}

/* ------------------------------------------------------------------ PCI: nombres de lo que lleva un T480s */
const char *hw_vendor_name(uint16_t v)
{
    switch (v) {
    case 0x8086: return "Intel"; case 0x10EC: return "Realtek"; case 0x144D: return "Samsung"; case 0x1C5C: return "SK hynix";
    case 0x1179: return "Toshiba"; case 0x15B7: return "WD/SanDisk"; case 0x17AA: return "Lenovo"; case 0x1987: return "Phison";
    case 0x1E0F: return "Kioxia"; case 0x10DE: return "NVIDIA"; case 0x1B36: return "QEMU"; case 0x1AF4: return "virtio"; case 0x1234: return "Bochs";
    default: return 0;
    }
}
const char *hw_pci_name(uint16_t v, uint16_t d)
{
    if (v != 0x8086) {
        if (v == 0x10EC && d == 0x525A) return "lector de tarjetas RTS525A";
        if (v == 0x144D && (d == 0xA808 || d == 0xA804 || d == 0xA809)) return "SSD NVMe Samsung";
        return 0;
    }
    switch (d) {
    case 0x5914: return "puente de la CPU (Kaby Lake-R)";
    case 0x5917: return "grafica UHD 620";
    case 0x1903: return "control termico de la CPU";
    case 0x1911: return "acelerador Gaussiano (GNA)";
    case 0x9D2F: return "controlador USB 3.0 (xHCI)";
    case 0x9D31: return "control termico del chipset";
    case 0x9D3A: return "Management Engine (HECI)";
    case 0x9D4E: case 0x9D58: case 0x9D48: return "puente LPC (EC, teclado, TPM)";
    case 0x9D21: return "gestion de energia (PMC)";
    case 0x9D23: return "SMBus (el trackpad RMI4 cuelga de aqui)";
    case 0x9D71: return "audio HD (Intel HDA)";
    case 0x9D60: case 0x9D61: case 0x9D62: case 0x9D63: return "I2C (Serial IO)";
    case 0x9D10: case 0x9D11: case 0x9D12: case 0x9D13: case 0x9D14: case 0x9D15: case 0x9D16: case 0x9D17:
    case 0x9D18: case 0x9D19: case 0x9D1A: case 0x9D1B: return "puerto raiz PCIe";
    case 0x15D7: case 0x15D8: case 0x15E3: return "Ethernet I219 (e1000e)";
    case 0x24FD: return "WiFi Wireless 8265 (iwlwifi)";
    case 0x24FB: return "WiFi Wireless 3168";
    case 0x15D2: case 0x15D3: case 0x15DA: case 0x15C0: return "Thunderbolt 3 (puente)";
    case 0x15D9: case 0x15D1: case 0x15BF: return "Thunderbolt 3 (NHI)";
    case 0x15DB: case 0x15D4: case 0x15C1: return "Thunderbolt 3 (USB)";
    case 0xF1A5: case 0xF1A6: return "SSD NVMe Intel";
    case 0x29C0: return "puente de QEMU q35"; case 0x2918: return "LPC de QEMU q35"; case 0x2922: return "AHCI de QEMU q35";
    default: return 0;
    }
}

/* ------------------------------------------------------------------ secciones */
static int seq(const char *a, const char *w) { while (*w && *a == *w) { a++; w++; } return !*w && (*a == 0 || *a == ' '); }
int hw_section_text(const char *sec, char *out, uint32_t max)
{
    soma_sbuf_t t = { out, max, 0 };
    if (max) out[0] = 0;
    while (sec && *sec == ' ') sec++;
    const char *rest = sec; while (rest && *rest && *rest != ' ') rest++; while (rest && *rest == ' ') rest++;
    int verbose = rest && rest[0] == '-' && rest[1] == 'v';
    if (!sec || !*sec || seq(sec, "resumen")) {
        smbios_text(&t, 1); cpu_text(&t, 1); mem_text(&t, 1, 0);
        if (bootfb_use_firmware()) { soma_sbuf_append_str(&t, "Pantalla: "); soma_sbuf_append_u64(&t, bootfb_get()->width); soma_sbuf_append_char(&t, 'x'); soma_sbuf_append_u64(&t, bootfb_get()->height); soma_sbuf_append_str(&t, " (firmware)\n"); }
        return 1;
    }
    if (seq(sec, "cpu")) { cpu_text(&t, 0); return 1; }
    if (seq(sec, "mem") || seq(sec, "memoria") || seq(sec, "ram")) { mem_text(&t, 0, verbose); return 1; }
    if (seq(sec, "smbios") || seq(sec, "equipo")) { smbios_text(&t, 0); return 1; }
    return 0;
}

/* ------------------------------------------------------------------ SOMA_157_T480S: ramtest */
/* Reserva `mib` MiB en el montón en bloques de 1 MiB, escribe un patron que depende de la direccion, lo verifica y libera.
 * Informa de las direcciones fisicas usadas (deben estar por encima de 4 GiB en un equipo con mucha RAM) y la velocidad. */
int hw_ramtest(uint32_t mib, char *out, uint32_t max)
{
    soma_sbuf_t t = { out, max, 0 };
    if (max) out[0] = 0;
    if (mib == 0) mib = 1024;
    if (mib > 16384) mib = 16384;
    enum { BLK = 1 << 20 };
    uint64_t **blk = (uint64_t **)kmalloc((size_t)mib * sizeof(uint64_t *));   /* en el montón: no engorda la imagen */
    if (!blk) { soma_sbuf_append_str(&t, "ramtest: sin memoria para la tabla de bloques\n"); return 0; }
    uint32_t got = 0, bad = 0;
    uint64_t pmin = ~0ULL, pmax = 0, above4g = 0;
    uint64_t t0 = timer_get_uptime_ms();
    for (; got < mib; got++) {
        uint64_t *b = (uint64_t *)kmalloc(BLK);
        if (!b) break;
        blk[got] = b;
        for (uint32_t pg = 0; pg < BLK; pg += 4096) {        /* direccion fisica de cada pagina */
            uint64_t ph = vmm_virt_to_phys((uint64_t)(uintptr_t)b + pg);
            if (ph < pmin) pmin = ph;
            if (ph > pmax) pmax = ph;
            if (ph >= 0x100000000ULL) above4g++;
        }
        for (uint32_t i = 0; i < BLK / 8; i++) b[i] = ((uint64_t)(uintptr_t)(b + i) * 0x9E3779B97F4A7C15ULL) ^ i;
    }
    uint64_t t1 = timer_get_uptime_ms();
    for (uint32_t k = 0; k < got; k++) {
        uint64_t *b = blk[k];
        for (uint32_t i = 0; i < BLK / 8; i++) if (b[i] != (((uint64_t)(uintptr_t)(b + i) * 0x9E3779B97F4A7C15ULL) ^ i)) { bad++; break; }
    }
    uint64_t t2 = timer_get_uptime_ms();
    for (uint32_t k = 0; k < got; k++) kfree(blk[k]);
    kfree(blk);
    soma_sbuf_append_str(&t, "ramtest: "); soma_sbuf_append_u64(&t, got); soma_sbuf_append_str(&t, " de "); soma_sbuf_append_u64(&t, mib); soma_sbuf_append_str(&t, " MiB reservados y escritos");
    if (got < mib) soma_sbuf_append_str(&t, " (el montón no dio mas: limite o RAM agotada)");
    soma_sbuf_append_str(&t, "\n  verificacion: "); soma_sbuf_append_str(&t, bad ? "FALLO en " : "correcta"); if (bad) { soma_sbuf_append_u64(&t, bad); soma_sbuf_append_str(&t, " bloques"); }
    soma_sbuf_append_str(&t, "\n  direcciones fisicas: 0x"); soma_sbuf_append_hex(&t, got ? pmin : 0, 10, 1); soma_sbuf_append_str(&t, " .. 0x"); soma_sbuf_append_hex(&t, pmax, 10, 1);
    soma_sbuf_append_str(&t, " | por encima de 4 GiB: "); tmb(&t, above4g * 4096 >> 20);
    uint64_t dw = t1 > t0 ? t1 - t0 : 1, dr = t2 > t1 ? t2 - t1 : 1;
    soma_sbuf_append_str(&t, "\n  escritura (con reserva) "); soma_sbuf_append_u64(&t, (uint64_t)got * 1000 / dw); soma_sbuf_append_str(&t, " MiB/s, lectura ");
    soma_sbuf_append_u64(&t, (uint64_t)got * 1000 / dr); soma_sbuf_append_str(&t, " MiB/s\n");
    return bad ? 0 : 1;
}
