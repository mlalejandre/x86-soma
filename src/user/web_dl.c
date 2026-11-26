/* SOMA_343_DESCARGAS: corrutinas de descarga del proceso del navegador. Ver src/apps/web_dl.h.
 * Reutiliza el cambio de pila de las corrutinas de JavaScript (soma_co_switch, web_js_co.c) con un trampolin propio. */
#include <stdint.h>
#include <stddef.h>
#include "../apps/web_dl.h"

void *kmalloc(size_t n);
void soma_co_switch(void **save_sp, void *new_sp);
extern char *g_co_lo, *g_co_hi;    /* pila en curso para web_stack_left() (web_js_co.c) */

#define WDL_STACK (128u * 1024u)   /* wn_fetch se cambia a su propia pila de 256 KiB: aqui basta con poco */

struct wdl {
    void (*fn)(void *); void *arg;
    char *stack;
    void *sp, *caller_sp;
    int live;
};
static struct wdl D[WDL_N];
static struct wdl *cur;

void soma_dl_main(struct wdl *d);
void soma_dl_tramp(void);
__asm__(".pushsection .text\n"
        ".globl soma_dl_tramp\n"
        "soma_dl_tramp:\n"
        "  movq %r12, %rdi\n"
        "  andq $-16, %rsp\n"
        "  call soma_dl_main\n"
        "  ud2\n"
        ".popsection\n");

void soma_dl_main(struct wdl *d)
{
    d->fn(d->arg);
    d->live = 0;
    for (;;) soma_co_switch(&d->sp, d->caller_sp);   /* terminada: no se vuelve a reanudar */
}

int wdl_start(void (*fn)(void *), void *arg)
{
    if (!fn) return -1;
    for (int i = 0; i < WDL_N; i++) {
        struct wdl *d = &D[i];
        if (d->live) continue;
        if (!d->stack) { d->stack = (char *)kmalloc(WDL_STACK); if (!d->stack) return -1; }   /* la pila se queda para la siguiente */
        uintptr_t top = ((uintptr_t)d->stack + WDL_STACK) & ~(uintptr_t)15;
        uint64_t *sp = (uint64_t *)(top - 64);    /* r15 r14 r13 r12 rbx rbp ret relleno: lo que soma_co_switch saca al entrar */
        for (int k = 0; k < 8; k++) sp[k] = 0;
        sp[3] = (uint64_t)(uintptr_t)d; sp[6] = (uint64_t)(uintptr_t)soma_dl_tramp;
        d->sp = sp; d->fn = fn; d->arg = arg; d->live = 1;
        return 0;
    }
    return -1;
}

void wdl_pump(void)
{
    if (cur) return;                               /* desde una descarga no se anidan otras */
    for (int i = 0; i < WDL_N; i++) {
        struct wdl *d = &D[i];
        if (!d->live) continue;
        char *lo = g_co_lo, *hi = g_co_hi;
        g_co_lo = d->stack; g_co_hi = d->stack + WDL_STACK;
        cur = d;
        soma_co_switch(&d->caller_sp, d->sp);
        cur = 0;
        g_co_lo = lo; g_co_hi = hi;
    }
}

int wdl_live(void) { int n = 0; for (int i = 0; i < WDL_N; i++) n += D[i].live; return n; }
int wdl_inside(void) { return cur != 0; }
void wdl_yield(void) { struct wdl *d = cur; if (d) soma_co_switch(&d->sp, d->caller_sp); }
