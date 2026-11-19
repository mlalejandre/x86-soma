/* PARCHE 138: servicio HOSTIL, solo para las pruebas de sandbox.c (sbx_selftest). Cada operacion intenta romper una garantia
 * del aislamiento; la prueba comprueba que el kernel lo impide y sigue sano. Nunca se instala en /bin. */
#include "sbxrt.h"

static long raw_sys(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    volatile uint8_t *p;
    switch (in->op) {
    case SBX_EVIL_SYSCALL:
        raw_sys(1, (long)"hola", 4, 0);                    /* SYS_WRITE: debe matarlo aqui */
        raw_sys(5, (long)"/x", 2, 0);
        return 0;
    case SBX_EVIL_LOOP:
        for (;;) { __asm__ volatile ("" ::: "memory"); }
    case SBX_EVIL_KREAD:
        p = (volatile uint8_t *)0x100000; return *p;        /* memoria del kernel */
    case SBX_EVIL_WRITE_IN:
        p = (volatile uint8_t *)(uintptr_t)(SBX_IN_VA + 64); *p = 1; return 0;
    case SBX_EVIL_HEAP_PAST:
        p = (volatile uint8_t *)(uintptr_t)(SBX_HEAP_VA + in->heap_bytes); *p = 1; return 0;
    case SBX_EVIL_HEAP_ALL:
        for (uint64_t a = SBX_HEAP_VA; a < SBX_HEAP_VA + in->heap_bytes; a += 4096) *(volatile uint8_t *)(uintptr_t)a = 1;
        return 0;
    case SBX_EVIL_BAD_PTR:
        out->data_va = 0x100000; out->data_len = 64; return 0;
    case SBX_EVIL_BAD_LEN: {
        uint8_t *b = (uint8_t *)kmalloc(64); b[0] = 1;
        out->data_va = (uint64_t)(uintptr_t)b; out->data_len = 0xFFFFFFFFFFFF0000ULL; return 0; }
    case SBX_EVIL_UNTOUCHED: {
        uint8_t *b = (uint8_t *)kmalloc(64); b[0] = 1;   /* toca la primera pagina; anuncia 1 MiB a partir de ahi */
        out->data_va = (uint64_t)(uintptr_t)b; out->data_len = 1u << 20; return 0; }
    case SBX_EVIL_BAD_MAGIC:
        sbx_exit(0);                                         /* sale sin escribir out->magic */
    case SBX_EVIL_EXEC_HEAP: {
        uint8_t *b = (uint8_t *)kmalloc(16); b[0] = 0xC3;   /* ret */
        ((void (*)(void))(uintptr_t)b)(); return 0; }
    case SBX_EVIL_PLANT: {
        uint8_t *b = (uint8_t *)kmalloc(1u << 20); if (!b) return 0;
        for (uint32_t i = 0; i < (1u << 20); ++i) b[i] = 0xA5;
        kfree(b); return 0; }
    case SBX_EVIL_SNOOP: {
        volatile uint8_t *h = (volatile uint8_t *)(uintptr_t)SBX_HEAP_VA; uint32_t nz = 0;
        for (uint32_t i = 64; i < (1u << 20); ++i) if (h[i]) nz++;     /* (los primeros bytes son la cabecera del bloque recien pedido) */
        out->val[0] = (int32_t)nz; return 0; }
    case SBX_EVIL_STATIC: {
        static int served; out->val[0] = ++served; return 0; }
    case SBX_EVIL_NET_NOCAP:
        raw_sys(22, 0x0A000202, 80, 0); return 0;                       /* SYS_TCP_OPEN sin la capacidad */
    case SBX_EVIL_NET_STEAL: {
        int bad = 0; static char b[64];
        for (long h = 0; h < 12; ++h) {
            if (raw_sys(24, h, (long)b, 8) != -9) bad++;              /* send */
            if (raw_sys(25, h, (long)b, 8) != -9) bad++;              /* recv */
            if (raw_sys(23, h, 0, 0) != -9) bad++;                    /* poll */
        }
        if (raw_sys(26, in->arg[0], 0, 0) != -9) bad++;               /* close del ajeno */
        out->val[0] = bad; return 0; }
    case SBX_EVIL_NET_FLOOD: {
        int ok = 0; long fifth = 0;
        for (int i = 0; i < 6; ++i) { long r = raw_sys(22, in->arg[0], in->arg[1], 0); if (r >= 0) ok++; if (i == 4) fifth = r; }
        out->val[0] = ok; out->val[1] = (int32_t)fifth; return 0; }      /* sale sin cerrar: el kernel debe limpiarlos */
    case SBX_EVIL_NET_DNS: {
        static char n1[] = "evil/../etc", n2[] = "a b", n3[300];
        static uint8_t ip[4]; int notrej = 0;
        for (int i = 0; i < 299; ++i) n3[i] = 'a';
        if (raw_sys(21, (long)n1, sizeof n1 - 1, (long)ip) != -22) notrej++;
        if (raw_sys(21, (long)n2, 3, (long)ip) != -22) notrej++;
        if (raw_sys(21, (long)n3, 299, (long)ip) != -22) notrej++;
        if (raw_sys(21, (long)n1, 0, (long)ip) != -22) notrej++;
        out->val[0] = notrej; return 0; }
    case SBX_EVIL_ECHO: {
        uint8_t *b = (uint8_t *)kmalloc(in->len ? in->len : 1);
        if (!b) { out->status = -2; return 0; }
        for (uint32_t i = 0; i < in->len; ++i) b[i] = data[in->len - 1 - i];
        out->data_va = (uint64_t)(uintptr_t)b; out->data_len = in->len;
        out->val[0] = (int32_t)in->len; return 0; }
    }
    return 3;
}
