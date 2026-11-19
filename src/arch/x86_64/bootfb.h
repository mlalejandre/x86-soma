#ifndef SOMA_BOOTFB_H
#define SOMA_BOOTFB_H
/* SOMA_154_T480S: informacion de arranque (Multiboot 1 y 2) y pantalla del firmware para equipos reales.
 *
 * En QEMU la pantalla es la tarjeta Bochs (BGA) y SOMA elige la resolucion. En un portatil real (Lenovo ThinkPad T480s:
 * Intel UHD 620) no hay BGA: el firmware (GOP en UEFI, VBE en BIOS) deja la pantalla en un modo lineal y GRUB nos pasa
 * direccion, pitch, tamano y formato. Es lo mismo que hacen efifb/simplefb en Linux: usar ese framebuffer tal cual,
 * sin programar la GPU. Ademas Multiboot 2 entrega el mapa de memoria y la tabla ACPI (RSDP), que en UEFI no esta en las
 * zonas de BIOS donde se buscaba. */
#include <stdint.h>

struct bootfb_info {
    uint64_t addr;                 /* direccion fisica del framebuffer (puede estar por encima de 4 GiB) */
    uint32_t pitch, width, height; /* pitch en bytes */
    uint8_t  bpp, type;            /* type 1 = RGB directo */
    uint8_t  r_pos, g_pos, b_pos;  /* posicion de cada canal en el pixel */
    int      valid;
};

/* Lee la informacion de arranque (antes de pmm_init). magic = EAX de GRUB, addr = EBX (copiado por boot.s). */
void bootinfo_parse(uint32_t magic, uint32_t addr);
int  bootinfo_is_mb2(void);
const char *bootinfo_cmdline(void);                 /* "" si no hay */
const char *bootinfo_loader(void);                  /* nombre del cargador, "" si no hay */
int  bootinfo_has_option(const char *opt);          /* p.ej. "soma.fb=bga" en la linea de ordenes */
uint64_t bootinfo_rsdp(void);                       /* direccion fisica del RSDP de ACPI, 0 si no se paso */
int  bootinfo_efi(void);                            /* 1 si se arranco desde UEFI */
uint64_t bootinfo_efi_systab(void);                 /* SOMA_156: direccion fisica de la tabla de sistema EFI, 0 si no */
int  bootinfo_disabled(const char *feature);       /* SOMA_156: 1 si "soma.<feature>=off" o "soma.seguro" */
/* SOMA_175A_T480S: modulos de GRUB (module2): el firmware de la WiFi */
int  bootinfo_module_count(void);
int  bootinfo_module_at(int i, uint64_t *start, uint64_t *len, const char **name);
int  bootinfo_module(const char *name_part, uint64_t *start, uint64_t *len);
/* Mapa de memoria Multiboot 2: devuelve 1 y la siguiente entrada, 0 al terminar (*it empieza en 0). */
int  bootinfo_mmap_next(uint32_t *it, uint64_t *base, uint64_t *len, uint32_t *type);

const struct bootfb_info *bootfb_get(void);
int  bootfb_use_firmware(void);                     /* 1 = usar la pantalla del firmware (y no BGA) */
int  bootfb_map(void);                              /* proyecta el framebuffer en FB_VIRT_ADDR (combinacion de escritura) */
int  bootfb_swap_rb(void);                          /* 1 si el rojo esta en los bits bajos (RGBX) */

/* Consola de texto sobre el framebuffer del firmware (en el portatil no hay puerto serie ni modo texto VGA). */
void bootfb_console_start(void);                    /* tras vmm_init: pinta lo acumulado desde el arranque */
void bootfb_console_stop(void);                     /* la GUI toma la pantalla */
void bootfb_console_resume(void);                   /* la GUI la devuelve */
void bootfb_console_panic(void);                    /* panico: volver a mostrar texto aunque la GUI estuviera */
int  bootfb_console_active(void);
void bootfb_putc(char c);                           /* desde kputc */
void bootfb_print_info(void);                       /* orden "pantalla" */
#endif
