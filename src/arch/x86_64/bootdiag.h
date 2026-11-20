#ifndef SOMA_BOOTDIAG_H
#define SOMA_BOOTDIAG_H
/* SOMA_228_ARRANQUE: diagnostico del arranque en equipos que no son el T480s.
 *  - Etapas: kernel_main marca cada paso ("[ARRANQUE 4/13] ACPI..."). Si el equipo se cuelga, la ultima linea en pantalla dice donde.
 *  - Si el nucleo cae durante el arranque, la pantalla de panico dice la etapa y que opcion de GRUB la evita.
 *  - Memoria entre arranques: desde que hay disco, la etapa se guarda en el sector 26 del disco de SOMA. Si un arranque no llego al
 *    escritorio, el siguiente lo detecta y lo cuenta (consola, Ajustes > Sistema, informe).
 *  - Informe: texto con el equipo, las etapas, que dispositivos PCI tienen driver en SOMA y el registro del nucleo. Se guarda en
 *    INFORME.TXT de la particion de arranque del pendrive (se lee desde Windows, macOS o Linux) y en /informe.txt de SOMA. */
#include <stdint.h>

#define BOOTDIAG_LBA 26u                      /* libre en src/fs/disk_layout.h (24 puerta, 25 reanudacion, 30 killtest, 31 checkpoint) */

void bootdiag_stage(const char *name, const char *grub_opt);   /* grub_opt: opcion que salta esta etapa ("soma.acpi=off") o 0 */
void bootdiag_disk_ready(void);              /* hay disco del sistema: lee el arranque anterior y empieza a guardar las etapas */
void bootdiag_complete(void);                /* el escritorio esta en marcha: el arranque se da por bueno */
int  bootdiag_in_boot(void);                 /* 1 mientras el arranque no ha terminado */
void bootdiag_panic_hint(void);              /* desde el panico: etapa y consejo (sin memoria dinamica ni cerrojos) */
const char *bootdiag_prev_failure(void);     /* "" o la etapa en la que se quedo el arranque anterior */
uint32_t bootdiag_boot_ms(void);             /* duracion del arranque (0 si no ha terminado) */

/* Informe completo en out (devuelve los bytes). Usa las ordenes de la consola: llamar desde un hilo, nunca desde una interrupcion. */
uint32_t bootdiag_report(char *out, uint32_t max);
void bootdiag_report_save_async(void);       /* genera y guarda el informe en segundo plano */
const char *bootdiag_report_status(void);    /* texto del ultimo guardado ("guardado en ...", "generando...", motivo del fallo) */
#endif
