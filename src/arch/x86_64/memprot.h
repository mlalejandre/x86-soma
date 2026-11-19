/* SOMA_192 (seguridad): W^X de verdad en el nucleo y pagina 0 protegida. Ver memprot.c. */
#ifndef SOMA_MEMPROT_H
#define SOMA_MEMPROT_H
#include <stdint.h>

struct memprot_info {
    int mode;                 /* 0 = desactivado (soma.memprot=off), 1 = activo, 2 = activo y pagina 0 sin mapear (soma.pagina0=estricta) */
    uint32_t sweeps;          /* pasadas hechas */
    uint64_t exec_fixed;      /* KiB que eran ejecutables fuera de .text y ya no lo son (acumulado) */
    uint64_t exec_left;       /* KiB ejecutables fuera de .text tras la ultima pasada (debe ser 0) */
    uint64_t text_kib;        /* KiB de codigo del nucleo (lo unico ejecutable) */
    uint64_t wx_text;         /* KiB de .text escribibles (debe ser 0) */
};

void memprot_apply(void);                         /* idempotente: se llama al arrancar y al final del arranque */
void memprot_get_info(struct memprot_info *i);
const uint8_t *memprot_page0(void);               /* por donde leer la pagina fisica 0 (area de datos de la BIOS): 0 o un alias de solo lectura */
#endif
