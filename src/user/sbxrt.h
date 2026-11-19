#ifndef SOMA_SBXRT_H
#define SOMA_SBXRT_H
/* PARCHE 138: runtime de los SERVICIOS AISLADOS (ring 3, solo SYS_EXIT). Incluir en UN solo .c por programa.
 * Da: kmalloc/kfree/memcpy/memmove/memset/memcmp sobre el monton bajo demanda (SBX_HEAP_VA), la entrada y la respuesta.
 * El programa define:  int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out);  (0 = exit(0)) */
#include <stdint.h>
#include <stddef.h>
#include "sbx_abi.h"

__attribute__((noreturn)) static inline void sbx_exit(long code) { long a = code; __asm__ volatile ("syscall" : "+D"(a) : "a"(0L) : "rcx", "r11", "memory"); for (;;) { } }

#include "memfn.h"            /* SOMA_199: memcpy, memmove, memset y memcmp (antes, un byte por vuelta) */

/* ---- monton: lista de bloques libres ordenada por direccion, primer ajuste, fusion con vecinos al liberar.
 * Cabecera de 16 bytes (tamano total del bloque y marca). Lo nunca usado queda sin tocar: no consume memoria fisica. */
struct sbx_blk { uint64_t size; struct sbx_blk *next; };    /* next solo en libres; en usados guarda la marca */
#define SBX_USED_MARK ((struct sbx_blk *)0x5553454455534544ULL)
static struct sbx_blk *sbx_free_list;
static uint64_t sbx_brk, sbx_end;

void *kmalloc(size_t n)
{
    if (n == 0 || n > SBX_HEAP_MAX) return 0;
    uint64_t need = ((uint64_t)n + 16u + 15u) & ~15ULL;
    struct sbx_blk **pp = &sbx_free_list;
    for (struct sbx_blk *b = sbx_free_list; b; pp = &b->next, b = b->next) {
        if (b->size < need) continue;
        if (b->size - need >= 64) {                         /* partir: el resto sigue libre en el mismo sitio de la lista */
            struct sbx_blk *r = (struct sbx_blk *)((uint8_t *)b + need);
            r->size = b->size - need; r->next = b->next; *pp = r; b->size = need;
        } else *pp = b->next;
        b->next = SBX_USED_MARK;
        return (uint8_t *)b + 16;
    }
    if (sbx_end - sbx_brk < need) return 0;                 /* el decodificador vera "sin memoria" (wi_oom) */
    struct sbx_blk *b = (struct sbx_blk *)sbx_brk; sbx_brk += need;
    b->size = need; b->next = SBX_USED_MARK;
    return (uint8_t *)b + 16;
}

void kfree(void *p)
{
    if (!p) return;
    struct sbx_blk *b = (struct sbx_blk *)((uint8_t *)p - 16);
    if (b->next != SBX_USED_MARK) sbx_exit(-77);            /* doble liberacion o puntero ajeno: el servicio se detiene */
    struct sbx_blk *prev = 0, *cur = sbx_free_list;
    while (cur && cur < b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b; else sbx_free_list = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) { b->size += cur->size; b->next = cur->next; }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) { prev->size += b->size; prev->next = b->next; }
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out);

/* PARCHE 139: servicio persistente. svc_next entrega el resultado anterior y bloquea hasta la siguiente peticion (0) o la orden de
 * terminar (1). El kernel pone a cero el monton y la respuesta entre peticiones; aqui se reinicia el asignador. */
static inline long sbx_svc_next(long prev) { long a = prev; long r; __asm__ volatile ("syscall" : "=a"(r), "+D"(a) : "0"(20L) : "rcx", "r11", "rsi", "rdx", "r8", "r9", "r10", "memory"); return r; }

__attribute__((section(".text._start"), used)) void _start(void)
{
    const struct sbx_in *in = (const struct sbx_in *)SBX_IN_VA;
    struct sbx_out *out = (struct sbx_out *)SBX_OUT_VA;
    if (in->magic != SBX_IN_MAGIC) {                         /* la entrada no esta escrita: modo servicio (sandbox.c, svc_start) */
        long prev = 0;
        while (sbx_svc_next(prev) == 0) {
            if (in->magic != SBX_IN_MAGIC || in->heap_bytes > SBX_HEAP_MAX) sbx_exit(-78);
            sbx_brk = SBX_HEAP_VA; sbx_end = SBX_HEAP_VA + in->heap_bytes; sbx_free_list = 0;
            prev = sbx_main(in, (const uint8_t *)SBX_IN_VA + 64, out);
            out->magic = SBX_OUT_MAGIC;
        }
        sbx_exit(0);
    }
    if (in->heap_bytes > SBX_HEAP_MAX) sbx_exit(-78);         /* llamada unica (sbx_run) */
    sbx_brk = SBX_HEAP_VA; sbx_end = SBX_HEAP_VA + in->heap_bytes; sbx_free_list = 0;
    int r = sbx_main(in, (const uint8_t *)SBX_IN_VA + 64, out);
    out->magic = SBX_OUT_MAGIC;
    sbx_exit(r);
}
#endif
