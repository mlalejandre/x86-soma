/* SOMA_363_ACCESO: donde guardan y de donde cargan los programas. Todo lo de la persona vive en su unidad (/usuario y sus carpetas de
 * serie: Documentos, Imagenes, Audio, Videos, Descargas, Proyectos) y en los medios USB externos (/usb/<nombre>, ver medios.h).
 * Los programas proponen siempre una de esas carpetas; sin unidad del usuario (arranque sin pendrive de datos) usan /tmp. */
#ifndef SOMA_ACCESO_H
#define SOMA_ACCESO_H
#include <stdint.h>
enum { ACC_DOCUMENTOS = 0, ACC_IMAGENES, ACC_AUDIO, ACC_VIDEOS, ACC_DESCARGAS, ACC_PROYECTOS,    /* las 6 de serie (unidades.c) */
       ACC_FOTOS, ACC_DIBUJOS, ACC_CAPTURAS, ACC_HOJAS,                                            /* subcarpetas: Imagenes/Fotos, */
       ACC_N };                                                   /* Imagenes/Dibujos, Imagenes/Capturas, Documentos/Hojas de calculo */
int  acceso_usuario(void);                                            /* 1 = la unidad /usuario esta montada */
/* "/usuario/Imagenes" o "/usuario/Imagenes/Fotos" (las crea si faltan). Sin unidad: "/tmp" y devuelve -1 */
int  acceso_carpeta(int tipo, char *out, uint32_t cap);
/* dir/pre001ext ... el primero que no exista. 0 = bien */
int  acceso_libre(const char *dir, const char *pre, const char *ext, char *out, uint32_t cap);
/* escritura de un archivo grande por trozos seguidos (AVI, WAV...): off tiene que ser lo ya escrito */
struct acc_sec { char ruta[192]; uint64_t pos; int err; };
int  acceso_sec_abre(struct acc_sec *s, const char *ruta);           /* 0 = bien (lo deja vacio) */
int  acceso_sec_pon(void *s, uint64_t off, const void *b, uint32_t n);   /* 0 = bien (sirve de sumidero de avi_write) */
int  acceso_sec_fin(struct acc_sec *s, int ok);                       /* !ok: lo borra. 0 = bien */
/* 1 si `ruta` es de la persona: /usuario/... o /usb/... (sin "..") */
int  acceso_ruta_ok(const char *ruta);
#endif
