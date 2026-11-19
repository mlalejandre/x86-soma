#ifndef SOMA_HWSCAN_H
#define SOMA_HWSCAN_H
/* SOMA_156_T480S: inventario del hardware real (orden "hw"): CPU por CPUID, mapa de memoria del firmware, SMBIOS (modelo,
 * BIOS, modulos de RAM) y acceso de solo lectura a las tablas del firmware por encima de 1 GiB (en UEFI, ACPI y SMBIOS
 * suelen estar entre 2 y 4 GiB y el nucleo solo tenia proyectado el primer GiB). */
#include <stdint.h>
#include <stddef.h>

/* hw_map_fn de hwinfo.h: devuelve un puntero a [phys, phys+len) o 0. Por debajo de 4 GiB, proyecta en identidad y en solo
 * lectura las paginas que falten; nunca toca paginas ya usadas para otra cosa (montón, framebuffer). */
const uint8_t *hw_map_phys(uint64_t phys, size_t len, void *ctx);
uint64_t hw_find_rsdp(void);                         /* RSDP de ACPI: el que pasa GRUB o el de la zona BIOS */
/* Texto de una seccion: "cpu", "mem" (o "mem -v"), "smbios", "resumen". Devuelve 1 si la seccion existe. */
int hw_section_text(const char *section, char *out, uint32_t max);
const char *hw_pci_name(uint16_t vendor, uint16_t device);   /* nombre de los dispositivos tipicos del T480s, o 0 */
const char *hw_vendor_name(uint16_t vendor);
int hw_ramtest(uint32_t mib, char *out, uint32_t max);       /* SOMA_157: orden ramtest */                 /* "Intel", "Realtek"... o 0 */

#endif
