#ifndef SOMA_MSR_H
#define SOMA_MSR_H
/* SOMA_166_T480S: lectura/escritura de MSR que no cuelga el sistema si el registro no existe (QEMU, otra CPU): la #GP se
 * convierte en un error (ver la tabla msr_fix en idt.c). 0 = bien, -1 = la CPU no tiene ese registro. */
#include <stdint.h>
static inline int msr_read_safe(uint32_t msr, uint64_t *v)
{
    uint32_t lo = 0, hi = 0; uint64_t err;
    __asm__ volatile ("xorl %%esi, %%esi\n"
                      "1: rdmsr\n"
                      "2:\n"
                      ".pushsection msr_fix, \"a\"\n .balign 8\n .quad 1b, 2b\n .popsection"
                      : "=a"(lo), "=d"(hi), "=S"(err) : "c"(msr) : "memory");
    if (err) return -1;
    *v = (uint64_t)hi << 32 | lo;
    return 0;
}
static inline int msr_write_safe(uint32_t msr, uint64_t v)
{
    uint64_t err;
    __asm__ volatile ("xorl %%esi, %%esi\n"
                      "1: wrmsr\n"
                      "2:\n"
                      ".pushsection msr_fix, \"a\"\n .balign 8\n .quad 1b, 2b\n .popsection"
                      : "=S"(err) : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)) : "memory");
    return err ? -1 : 0;
}
#endif
