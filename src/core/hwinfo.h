#ifndef SOMA_HWINFO_H
#define SOMA_HWINFO_H
/* PARCHE 032: descripcion del hardware (solo lectura). Analizadores puros de las tablas ACPI (x86) y del arbol de
 * dispositivos FDT/DTB (Raspberry Pi y otras ARM). No tocan el hardware: reciben una funcion `map` que devuelve un
 * puntero a memoria fisica ya mapeada (o 0 si no es accesible), asi que se prueban y se fuzzean en el host.
 * Son la base para escribir despues los controladores (APIC/MCFG/xHCI/AHCI/NVMe en x86; UART/GIC/timer en ARM). */
#include <stdint.h>
#include <stddef.h>

typedef const uint8_t *(*hw_map_fn)(uint64_t phys, size_t len, void *ctx);

#define HW_MAX_IOAPIC 8
#define HW_MAX_MCFG   4
#define HW_MAX_CPU    64              /* SOMA_158: procesadores que se guardan de la MADT */

struct hw_acpi {
    int      ok;                    /* 1 = RSDP valido y al menos una tabla leida */
    uint8_t  revision;              /* revision del RSDP (0 = ACPI 1.0, >=2 usa XSDT) */
    char     oem[7];
    int      ntables;               /* tablas listadas por RSDT/XSDT */
    char     sig[32][5];            /* firmas de las primeras 32 tablas */
    int      ncpu;                  /* procesadores habilitados segun la MADT */
    uint32_t lapic_addr;
    int      nioapic;
    uint32_t ioapic_addr[HW_MAX_IOAPIC];
    int      nmcfg;
    struct { uint64_t base; uint16_t seg; uint8_t bus0, bus1; } mcfg[HW_MAX_MCFG];
    int      has_hpet, has_fadt, has_madt;
    int      nirq_ovr;              /* PARCHE 034: overrides de IRQ ISA -> GSI (MADT tipo 2) */
    struct { uint8_t src; uint32_t gsi; uint16_t flags; } irq_ovr[16];
    /* SOMA_158_T480S: lo necesario para activar el APIC (158) y despertar los nucleos (159) */
    uint8_t  ioapic_id[HW_MAX_IOAPIC];
    uint32_t ioapic_gsi[HW_MAX_IOAPIC];   /* primera GSI que atiende cada IOAPIC */
    int      nlapic;                       /* procesadores de la MADT (tipos 0 y 9), habilitados o no */
    struct { uint32_t apic_id; uint32_t uid; uint8_t enabled, online_capable; } lapic[HW_MAX_CPU];
    int      nmi_lint;                     /* linea LINT del LAPIC conectada a NMI (MADT tipo 4/0xA; -1 = no se dice) */
    uint16_t nmi_flags;
};
/* Analiza desde la direccion fisica del RSDP. Devuelve 0 si lo logro. */
int hw_acpi_parse(uint64_t rsdp_phys, hw_map_fn map, void *ctx, struct hw_acpi *o);
/* SOMA_158_T480S: como llega una IRQ ISA (0-15) al IOAPIC segun la MADT: GSI, polaridad (low=1: activa a nivel bajo) y disparo
 * (level=1: por nivel). Sin redireccion: GSI = IRQ, flanco, activa alta (lo que manda el bus ISA). */
void hw_isa_irq_route(const struct hw_acpi *o, uint8_t irq, uint32_t *gsi, int *low, int *level);
/* Indice del IOAPIC que atiende `gsi` (el de mayor base <= gsi), o -1. */
int hw_ioapic_for_gsi(const struct hw_acpi *o, uint32_t gsi);
/* Busca "RSD PTR " (alineado a 16) en [lo, hi). Devuelve la direccion fisica o 0. */
uint64_t hw_acpi_find_rsdp(uint64_t lo, uint64_t hi, hw_map_fn map, void *ctx);

/* PARCHE 034: capacidades vendor de virtio-pci (virtio 1.x) a partir de una copia de 256 bytes del espacio de configuracion */
#define HW_VIRTIO_COMMON 1
#define HW_VIRTIO_NOTIFY 2
#define HW_VIRTIO_ISR    3
#define HW_VIRTIO_DEVICE 4
#define HW_VIRTIO_PCICFG 5
struct hw_virtio_cap { uint8_t type, bar; uint32_t offset, length, notify_mult; };
int hw_virtio_caps(const uint8_t cfg[256], struct hw_virtio_cap *out, int max);   /* devuelve cuantas encontro */

#define HW_FDT_MAX_MEM 4
struct hw_fdt {
    int      ok;
    uint32_t totalsize, version;
    char     model[64], compat[64], stdout_path[64];
    int      ncpu, nnodes, nmem;
    uint64_t mem_base[HW_FDT_MAX_MEM], mem_size[HW_FDT_MAX_MEM];
};
int hw_fdt_parse(const uint8_t *fdt, size_t avail, struct hw_fdt *o);
/* Primer nodo cuyo "compatible" contiene `compat`: devuelve 0 y su primer par reg (base, tam) si lo tiene. */
int hw_fdt_find_reg(const uint8_t *fdt, size_t avail, const char *compat, uint64_t *base, uint64_t *size);

/* SOMA_156_T480S: SMBIOS (DMI). Modelo del equipo, BIOS, procesador y modulos de memoria tal como los describe el firmware.
 * Analizadores puros (como los de arriba): reciben bytes ya leidos y nunca leen fuera de los limites que se les dan. */
#define HW_SMBIOS_MAX_MEM 8
struct hw_smbios {
    int      ok;
    uint8_t  major, minor;
    int      nstructs;
    char     bios_vendor[48], bios_version[48], bios_date[16];
    char     sys_vendor[48], sys_product[48], sys_version[48], sys_family[48];
    char     board_product[48];
    char     cpu_name[64];
    uint16_t cpu_max_mhz, cpu_cur_mhz;
    uint8_t  cpu_cores, cpu_threads;
    uint64_t mem_max_mb;                /* capacidad maxima de la placa (tipo 16), 0 = desconocida */
    int      nmem;                      /* zocalos (tipo 17), ocupados o no */
    struct { char locator[24], part[24]; uint32_t size_mb; uint16_t mhz; uint8_t type; } mem[HW_SMBIOS_MAX_MEM];
};
/* Punto de entrada ("_SM3_" de 64 bits o "_SM_"). Devuelve 0 y la tabla (direccion fisica y longitud maxima). */
int hw_smbios_entry(const uint8_t *ep, size_t avail, uint64_t *table_phys, uint32_t *table_len, uint8_t *major, uint8_t *minor);
/* Recorre la tabla de estructuras. Devuelve 0 si encontro al menos una estructura valida. */
int hw_smbios_parse(const uint8_t *table, size_t len, uint8_t major, uint8_t minor, struct hw_smbios *o);
const char *hw_smbios_memtype(uint8_t t);   /* "DDR4", "LPDDR3"... */
#endif
