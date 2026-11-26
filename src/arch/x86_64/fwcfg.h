#ifndef SOMA_FWCFG_H
#define SOMA_FWCFG_H
/* PARCHE 138: lectura de ficheros de QEMU fw_cfg (puertos 0x510/0x511). Permite pasar opciones al kernel sin tocar la ISO:
 *   qemu ... -fw_cfg name=opt/soma/selftest,string=sbx
 * Devuelve los bytes copiados (0 si no hay QEMU, no existe el fichero o no cabe); out termina en NUL. */
#include <stdint.h>
uint32_t fwcfg_read_file(const char *name, char *out, uint32_t max);
uint32_t fwcfg_open(const char *name);              /* SOMA_241_VOZ: tamano (0 = no esta) y deja el archivo seleccionado */
void     fwcfg_read_seq(void *buf, uint32_t n);      /* SOMA_241_VOZ: siguientes n bytes del archivo seleccionado */
void     fwcfg_reserva(void);                        /* SOMA_329: para leer un archivo por partes cediendo la CPU entre ellas */
void     fwcfg_libera(void);
#endif
