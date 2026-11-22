/* SOMA_265_CC: llamadas al sistema de SOMA para la biblioteca de C (src/user/cc/libc.c). Las mismas que usa Python
 * (src/user/python.c): SYS_WRITE, SYS_EXIT, SYS_GETARGS, SYS_UPTIME_MS, SYS_SLEEP y las del interprete (SYS_SOMA_CON, SYS_SOMA_VFS).
 * En el ordenador de desarrollo los mismos ejecutables se prueban con tests/somaemu_265.c, que atiende estas llamadas. */
#ifndef SOMA_CC_SYS_H
#define SOMA_CC_SYS_H

#define SOMA_SYS_EXIT     0
#define SOMA_SYS_WRITE    1
#define SOMA_SYS_YIELD    2
#define SOMA_SYS_SLEEP    3
#define SOMA_SYS_GETARGS  10
#define SOMA_SYS_UPTIME   19
#define SOMA_SYS_CON      30
#define SOMA_SYS_VFS      31

struct soma_vfs_req { const char *path; unsigned long plen; void *buf; unsigned long len; };

static inline long __soma_sys(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

#endif
