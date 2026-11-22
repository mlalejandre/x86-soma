/* SOMA_252_VIDEO_NATIVO: la biblioteca de C minima que necesitan libavc, libxaac y el reproductor en ring 3.
 * Memoria: un monton con lista de libres sobre la region VP_HEAP_VA (el nucleo la da pagina a pagina, bajo demanda).
 * Sin E/S: el proceso solo puede dormir, mirar la hora y escribir audio (la entrada del proceso esta al final). */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "vp_abi.h"
#include "../ulib.h"

void *memcpy(void *d, const void *s, size_t n) { void *r = d; __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }
void *memset(void *d, int c, size_t n) { void *r = d; __asm__ volatile ("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
    if (a == b || !n) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    while (n--) a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, size_t n)
{
    const uint8_t *a = (const uint8_t *)x, *b = (const uint8_t *)y;
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t m) { size_t n = 0; while (n < m && s[n]) n++; return n; }

/* ---- monton: bloques con cabecera de 16 bytes; lista de libres ordenada por direccion, primer ajuste, fusion al liberar */
struct blk { uint64_t size; struct blk *next; };
#define USED ((struct blk *)0x5553454455534544ULL)
static struct blk *free_list;
static uint64_t brk_ = VP_HEAP_VA;

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    if (n > VP_HEAP_MAX) return 0;
    uint64_t need = ((uint64_t)n + 16u + 15u) & ~15ULL;
    struct blk **pp = &free_list;
    for (struct blk *b = free_list; b; pp = &b->next, b = b->next) {
        if (b->size < need) continue;
        if (b->size - need >= 64) { struct blk *r = (struct blk *)((uint8_t *)b + need); r->size = b->size - need; r->next = b->next; *pp = r; b->size = need; }
        else *pp = b->next;
        b->next = USED;
        return (uint8_t *)b + 16;
    }
    if (VP_HEAP_VA + VP_HEAP_MAX - brk_ < need) return 0;
    struct blk *b = (struct blk *)brk_; brk_ += need;
    b->size = need; b->next = USED;
    return (uint8_t *)b + 16;
}
void free(void *p)
{
    if (!p) return;
    struct blk *b = (struct blk *)((uint8_t *)p - 16);
    if (b->next != USED) return;                              /* doble liberacion o puntero ajeno: se ignora */
    struct blk *prev = 0, *cur = free_list;
    while (cur && cur < b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b; else free_list = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) { b->size += cur->size; b->next = cur->next; }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) { prev->size += b->size; prev->next = b->next; }
}
void *calloc(size_t a, size_t b) { if (b && a > (size_t)VP_HEAP_MAX / b) return 0; void *p = malloc(a * b); if (p) memset(p, 0, a * b); return p; }

/* ---- qsort: ordenacion por insercion con saltos (Shell); libavc solo ordena listas de unas decenas de elementos */
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    uint8_t *b = (uint8_t *)base; uint8_t tmp[256];
    if (sz == 0 || sz > sizeof tmp) return;
    for (size_t gap = n / 2; gap > 0; gap /= 2)
        for (size_t i = gap; i < n; ++i) {
            memcpy(tmp, b + i * sz, sz);
            size_t j = i;
            for (; j >= gap && cmp(b + (j - gap) * sz, tmp) > 0; j -= gap) memcpy(b + j * sz, b + (j - gap) * sz, sz);
            memcpy(b + j * sz, tmp, sz);
        }
}

/* ---- snprintf: %s %d %u %x %c %% (libavc solo lo usa para su cadena de version) */
int vsnprintf(char *o, size_t max, const char *f, va_list ap)
{
    size_t p = 0;
#define PUT(c) do { if (p + 1 < max) o[p] = (char)(c); p++; } while (0)
    for (; *f; ++f) {
        if (*f != '%') { PUT(*f); continue; }
        ++f;
        while (*f == '-' || *f == '0' || (*f >= '1' && *f <= '9') || *f == '.' || *f == 'l' || *f == 'z') ++f;
        if (*f == 's') { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; while (*s) PUT(*s++); }
        else if (*f == 'c') PUT(va_arg(ap, int));
        else if (*f == 'd' || *f == 'i' || *f == 'u' || *f == 'x' || *f == 'X') {
            char t[24]; int k = 0; unsigned long v; int neg = 0;
            if (*f == 'd' || *f == 'i') { int x = va_arg(ap, int); neg = x < 0; v = neg ? (unsigned long)(-(long)x) : (unsigned long)x; } else v = va_arg(ap, unsigned int);
            unsigned base = (*f == 'x' || *f == 'X') ? 16u : 10u;
            do { t[k++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            if (neg) PUT('-');
            while (k) PUT(t[--k]);
        } else if (*f == '%') PUT('%');
        else if (!*f) break;
    }
#undef PUT
    if (max) o[p < max ? p : max - 1] = 0;
    return (int)p;
}
int snprintf(char *o, size_t max, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(o, max, f, ap); va_end(ap); return r; }
int sprintf(char *o, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(o, 512, f, ap); va_end(ap); return r; }
int printf(const char *f, ...) { (void)f; return 0; }

void abort(void) { for (;;) __asm__ volatile ("xor %%edi, %%edi; dec %%rdi; xor %%eax, %%eax; syscall" ::: "rax", "rdi", "rcx", "r11", "memory"); }
void __assert_fail(const char *a, const char *b, unsigned c, const char *d) { (void)a; (void)b; (void)c; (void)d; abort(); }

/* ---- matematicas que no trae libm_dbl */
double pow(double, double);
double fabs(double x) { return __builtin_fabs(x); }
double cbrt(double x) { if (x == 0) return x; double r = pow(x < 0 ? -x : x, 1.0 / 3.0); return x < 0 ? -r : r; }
float fabsf(float x) { return __builtin_fabsf(x); }

/* ---- setjmp / longjmp (libxaac sale asi de un cuadro corrupto): rbx, rbp, r12-r15, rsp y la direccion de retorno */
__asm__(
    ".text\n.globl _setjmp\n.globl setjmp\n.type _setjmp,@function\n_setjmp:\nsetjmp:\n"
    "  mov %rbx, 0(%rdi)\n  mov %rbp, 8(%rdi)\n  mov %r12, 16(%rdi)\n  mov %r13, 24(%rdi)\n  mov %r14, 32(%rdi)\n  mov %r15, 40(%rdi)\n"
    "  lea 8(%rsp), %rdx\n  mov %rdx, 48(%rdi)\n  mov (%rsp), %rdx\n  mov %rdx, 56(%rdi)\n  xor %eax, %eax\n  ret\n"
    ".globl longjmp\n.type longjmp,@function\nlongjmp:\n"
    "  mov %esi, %eax\n  test %eax, %eax\n  jnz 1f\n  inc %eax\n1:\n"
    "  mov 0(%rdi), %rbx\n  mov 8(%rdi), %rbp\n  mov 16(%rdi), %r12\n  mov 24(%rdi), %r13\n  mov 32(%rdi), %r14\n  mov 40(%rdi), %r15\n"
    "  mov 48(%rdi), %rsp\n  jmp *56(%rdi)\n");

/* ---- hilos de libavc: no hay (un solo nucleo de decodificacion) */
int ithread_get_handle_size(void) { return 8; }
int ithread_get_mutex_lock_size(void) { return 8; }
int ithread_get_cond_struct_size(void) { return 8; }
int ithread_create(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int ithread_join(void *a, void **b) { (void)a; (void)b; return 0; }
int ithread_mutex_init(void *a) { (void)a; return 0; }
int ithread_mutex_destroy(void *a) { (void)a; return 0; }
int ithread_mutex_lock(void *a) { (void)a; return 0; }
int ithread_mutex_unlock(void *a) { (void)a; return 0; }
int ithread_cond_init(void *a) { (void)a; return 0; }
int ithread_cond_destroy(void *a) { (void)a; return 0; }
int ithread_cond_wait(void *a, void *b) { (void)a; (void)b; return 0; }
int ithread_cond_signal(void *a) { (void)a; return 0; }
void ithread_yield(void) { }
void ithread_usleep(unsigned u) { (void)u; }
void ithread_set_name(char *n) { (void)n; }

/* ================================================================== vplay.elf: la entrada del proceso
 * No abre archivos ni red: el MP4 le llega por la memoria que comparte con el nucleo (vp_abi.h) y por ahi devuelve los
 * fotogramas. Sus unicas llamadas al sistema son salir, dormir, la hora y el audio (el nucleo se lo limita con un filtro).
 * Un video corrupto o malicioso solo puede romper este proceso. La logica esta en vp_core.c. */

static uint64_t e_now(void) { return (uint64_t)u_uptime_ms(); }
static void e_sleep(uint32_t ms) { u_sleep(ms ? ms : 1); }
static int e_aopen(uint32_t rate) { return (int)u_audio_open(rate); }
static int64_t e_awrite(const void *p, uint32_t n) { return (int64_t)u_audio_write(p, n); }
static int e_astatus(uint64_t *fr, uint64_t *us) { u64 st[3]; i64 r = u_audio_status(st); if (r) return (int)r; *fr = st[0]; *us = st[1]; return 0; }
static void e_aclose(void) { (void)u_audio_close(0); }

/* La pila llega alineada a 16 SIN direccion de retorno: se alinea como espera el codigo SSE (movdqa sobre la pila) y se llama. */
long vp_main(void);
__asm__(".section .text._start,\"ax\"\n.global _start\n_start:\n  andq $-16, %rsp\n  call vp_main\n  movq %rax, %rdi\n  xorq %rax, %rax\n  syscall\n  jmp .\n.text\n");

long vp_main(void)
{
    static struct vp_env env = { e_now, e_sleep, e_aopen, e_awrite, e_astatus, e_aclose };
    return vp_run(&env, (void *)VP_SH_VA);
}
