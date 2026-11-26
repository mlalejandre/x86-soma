/* SOMA_329_IA_LOCAL: lo poco que el motor de IA anade a la libc de la voz (voz_libc.c) */
#include <stdint.h>
#define IA_HEAP_VA 0x8040000000UL            /* USER_HEAP_VA: la region grande bajo demanda que pone el nucleo */
unsigned long ia_heap_va(void) { return IA_HEAP_VA; }
static inline long sc3(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}
long ia_sleep_ms(long ms)
{
#ifdef IA_HOST
    struct { long s, ns; } ts = { ms / 1000, (ms % 1000) * 1000000L };
    return sc3(35, (long)&ts, 0, 0);          /* nanosleep de Linux */
#else
    return sc3(3, ms, 0, 0);                  /* SYS_SLEEP */
#endif
}

/* SOMA_329_IA_LOCAL: base de FS (bloque de hilo). libstdc++.a viene compilada con protector de pila, que lee el canario en %fs:0x28;
 * sin base de FS eso leia la direccion 0x28 y el programa moria al arrancar. Se apunta FS a un bloque propio (sin TLS de verdad). */
long ia_set_fs(void *tcb)
{
#ifdef IA_HOST
    return sc3(158, 0x1002, (long)tcb, 0);   /* arch_prctl(ARCH_SET_FS) de Linux */
#else
    return sc3(33, (long)tcb, 0, 0);          /* SYS_SOMA_FS */
#endif
}
