#ifndef SOMA_SANDBOX_H
#define SOMA_SANDBOX_H
/* PARCHE 138: servicios aislados en ring 3 (ver src/core/sbx_abi.h).
 *
 *   struct sbx_call c; sbx_prepare(&c, op, datos, len, monton);   c.arg[...] = ...;
 *   int r = sbx_run(&c, programa_elf, len_elf, plazo_ms);         -> 0 si el proceso termino con exit(0) y la respuesta es valida
 *   ... c.out (copia de la pagina de respuesta), sbx_copy_data(&c, dst, n) para el resultado grande ...
 *   sbx_release(&c);                                              -> libera TODO (siempre, haya ido bien o mal)
 *
 * Garantias: el proceso no puede hacer ninguna syscall salvo exit, no ve memoria del kernel ni de otros procesos, no puede
 * escribir su entrada ni ejecutar su monton, tiene plazo y su monton no crece si el sistema bajaria de SBX_RESERVE_FRAMES.
 * Lo que devuelve se valida antes de usarlo. Si el proceso muere, el llamante recibe un error y el kernel sigue. */
#include <stdint.h>
#include <stddef.h>
#include "sbx_abi.h"
#include "usermode.h"

#define SBX_RESERVE_FRAMES ((32u * 1024u * 1024u) / 4096u)   /* 32 MiB que un servicio nunca puede consumir */

struct sbx_call {
    struct sbx_in  in;                   /* cabecera de entrada (op, arg, heap_bytes...) */
    const void    *data; uint32_t len;   /* datos de entrada (se copian) */
    struct sbx_out out;                  /* copia de la respuesta (valida si sbx_run devolvio 0) */
    int64_t        exit_code;            /* codigo del proceso (USER_EXIT_* si murio) */
    struct uregion r_in, r_out, r_heap;  /* privado */
    struct uregion *heap;                /* PARCHE 139: monton del que leer out.data (propio o del servicio) */
    struct sbx_svc *svc;                 /* PARCHE 139: servicio ocupado por esta llamada hasta sbx_release */
};

#define SBX_E_ARGS    (-1)   /* argumentos no validos */
#define SBX_E_NOMEM   (-2)   /* sin memoria para preparar las regiones */
#define SBX_E_SPAWN   (-3)   /* no se pudo crear el proceso (sin ranuras tras esperar) */
#define SBX_E_DIED    (-4)   /* el proceso murio (fallo, plazo, syscall prohibida); ver exit_code */
#define SBX_E_REPLY   (-5)   /* respuesta mal formada (magic, tamanos, punteros) */
#define SBX_E_HEAPOOM (-6)   /* el proceso agoto su monton o la reserva del sistema */

void sbx_prepare(struct sbx_call *c, uint32_t op, const void *data, uint32_t len, uint32_t heap_bytes);
int  sbx_run(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms);
int  sbx_run_caps(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms, uint32_t extra_sysmask);   /* PARCHE 142 */
int  sbx_copy_data(const struct sbx_call *c, void *dst, uint64_t n);   /* copia out.data (ya validado) */
void sbx_release(struct sbx_call *c);

/* PARCHE 139: SERVICIOS PERSISTENTES. Mismo programa y mismas garantias, pero el proceso no se crea en cada llamada: espera en
 * SYS_SVC_NEXT. Entre peticiones su monton se pone a cero y se recorta; cada SBX_SVC_RECYCLE peticiones (o ante cualquier error) el
 * proceso se destruye y se crea otro limpio. Hay SBX_SVC_POOL procesos por servicio (llamadas concurrentes).
 *   sbx_prepare(&c, op, datos, len, 0); c.in.arg[..] = ...;
 *   int r = sbx_svc_call(SBX_SVC_IMG, &c, plazo_ms);     -> como sbx_run
 *   ... sbx_copy_data ...;  sbx_release(&c);              -> OBLIGATORIO: libera el servicio para la siguiente llamada */
#define SBX_SVC_IMG   0     /* imgdec.elf: imagenes y JPEG de video */
#define SBX_SVC_ZLIB  1     /* zdec.elf: inflate (HTTP gzip/deflate, PDF FlateDecode) */
#define SBX_SVC_TEST  2     /* sbxevil.elf (solo pruebas) */
#define SBX_SVC_PDF   3     /* PARCHE 141: pdfsvc.elf: analizador PDF */
#define SBX_SVC_NET   4     /* PARCHE 142: netsvc.elf: cliente HTTP (con la capacidad de red) */
#define SBX_SVC_N     5
#define SBX_SVC_POOL  2
#define SBX_SVC_RECYCLE 256
#define SBX_SVC_KEEP_HEAP_PAGES 2048   /* 8 MiB de monton se conservan (a cero) entre peticiones; el resto se devuelve */
int     sbx_svc_call(int svc_id, struct sbx_call *c, uint32_t max_ms);
int64_t sbx_svc_next(int proc_idx, int64_t result);      /* SYS_SVC_NEXT (usermode.c) */
void    sbx_svc_stop_all(void);                          /* para pruebas: destruye todos los procesos de servicio */
int     sbx_svc_running(int svc_id);                     /* procesos vivos de ese servicio */

/* Estadisticas (comando 'sbx'). */
struct sbx_stats { uint64_t runs, ok, died, bad_reply, oom, spawn_fail, last_ms, max_ms, total_ms, svc_calls, svc_starts, svc_recycled; };
void sbx_get_stats(struct sbx_stats *s);

/* Programas de servicio enlazados en el kernel */
extern const uint8_t _binary_imgdec_elf_start[], _binary_imgdec_elf_end[];
extern const uint8_t _binary_sbxevil_elf_start[], _binary_sbxevil_elf_end[];
extern const uint8_t _binary_zdec_elf_start[], _binary_zdec_elf_end[];
extern const uint8_t _binary_pdfsvc_elf_start[], _binary_pdfsvc_elf_end[];
extern const uint8_t _binary_netsvc_elf_start[], _binary_netsvc_elf_end[];

int sbx_selftest(void);   /* 0 = todo bien */
int sbx_webtest(const char *url);
int sbx_srcfstest(void);
int sbx_nettest(const char *base);   /* PARCHE 142: netsvc + capacidad de red (tools/sbx_check.py --web) */
int sbx_webref(const char *url);     /* PARCHE 142: huella del navegador */           /* PARCHE 140: SRCFS v02 (necesita el disco empaquetado: tools/sbx_check.py --srcfs) */   /* navegador real + red + servicio (QEMU, tools/sbx_check.py --web) */
#endif
