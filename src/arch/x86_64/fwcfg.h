#ifndef SOMA_FWCFG_H
#define SOMA_FWCFG_H
/* PARCHE 138: lectura de ficheros de QEMU fw_cfg (puertos 0x510/0x511). Permite pasar opciones al kernel sin tocar la ISO:
 *   qemu ... -fw_cfg name=opt/soma/selftest,string=sbx
 * Devuelve los bytes copiados (0 si no hay QEMU, no existe el fichero o no cabe); out termina en NUL. */
#include <stdint.h>
uint32_t fwcfg_read_file(const char *name, char *out, uint32_t max);
#endif
