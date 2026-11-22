/* SOMA_265_CC: biblioteca de C de SOMA para programas de ring 3.
 *
 * La usan dos: el compilador (cc.elf: TinyCC + esto, compilado con gcc al construir SOMA) y los programas que compila el
 * usuario (TinyCC compila este mismo archivo junto con su programa). Sin el nucleo de por medio salvo las llamadas al sistema de
 * soma_sys.h: consola (SYS_WRITE, SYS_SOMA_CON), archivos del VFS (SYS_SOMA_VFS: leer cualquiera permitido, escribir solo en
 * /workspace), argumentos (SYS_GETARGS), reloj y ventana. Las rutas relativas son de /workspace/.
 *
 * Memoria: un monton estatico de SOMA_HEAP_SIZE bytes (en .bss) con listas libres por tamano y fusion de vecinos. La pila: 1 MiB
 * propio (la de SOMA para procesos es de 64 KiB). */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <wchar.h>
#include <soma.h>
#include "soma_sys.h"

#ifndef SOMA_HEAP_SIZE
#define SOMA_HEAP_SIZE (8u << 20)
#endif
#define SOMA_STACK_SIZE (1u << 20)

int errno;

/* ================================================================== cadenas y caracteres */
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d; const unsigned char *b = s;
    if (a == b || !n) return d;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n) { unsigned char *a = d; while (n--) *a++ = (unsigned char)c; return d; }
int memcmp(const void *x, const void *y, size_t n)
{
    const unsigned char *a = x, *b = y;
    for (; n; --n, ++a, ++b) if (*a != *b) return *a - *b;
    return 0;
}
void *memchr(const void *s, int c, size_t n) { const unsigned char *p = s; for (; n; --n, ++p) if (*p == (unsigned char)c) return (void *)p; return 0; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t m) { size_t n = 0; while (n < m && s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n) { for (; n; --n, ++a, ++b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; if (!*a) return 0; } return 0; }
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
char *strncpy(char *d, const char *s, size_t n) { char *r = d; for (; n && *s; --n) *d++ = *s++; for (; n; --n) *d++ = 0; return r; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) { char *e = d + strlen(d); while (n-- && *s) *e++ = *s++; *e = 0; return d; }
char *strchr(const char *s, int c) { for (;; ++s) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strrchr(const char *s, int c) { const char *r = 0; for (;; ++s) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; ++h) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
    return 0;
}
size_t strspn(const char *s, const char *a) { size_t n = 0; while (s[n] && strchr(a, s[n])) n++; return n; }
size_t strcspn(const char *s, const char *r) { size_t n = 0; while (s[n] && !strchr(r, s[n])) n++; return n; }
char *strpbrk(const char *s, const char *a) { s += strcspn(s, a); return *s ? (char *)s : 0; }
char *strtok_r(char *s, const char *d, char **save)
{
    if (!s) s = *save;
    if (!s) return 0;
    s += strspn(s, d);
    if (!*s) { *save = 0; return 0; }
    char *e = s + strcspn(s, d);
    if (*e) { *e = 0; *save = e + 1; } else *save = 0;
    return s;
}
char *strtok(char *s, const char *d) { static char *save; return strtok_r(s, d, &save); }
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); if (d) memcpy(d, s, n); return d; }
char *strndup(const char *s, size_t n) { size_t l = strnlen(s, n); char *d = malloc(l + 1); if (d) { memcpy(d, s, l); d[l] = 0; } return d; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isgraph(int c) { return c > 32 && c < 127; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isascii(int c) { return c >= 0 && c < 128; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int strcasecmp(const char *a, const char *b) { while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; } return tolower((unsigned char)*a) - tolower((unsigned char)*b); }
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; --n, ++a, ++b) { int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if (x != y) return x - y; if (!x) return 0; }
    return 0;
}
char *strerror(int e)
{
    switch (e) {
    case 0: return "sin error";
    case ENOENT: return "no existe";
    case EACCES: return "permiso denegado (solo se escribe en /workspace)";
    case ENOMEM: return "sin memoria";
    case EINVAL: return "argumento no valido";
    case EBADF: return "descriptor no valido";
    case EMFILE: return "demasiados archivos abiertos";
    case EFBIG: return "archivo demasiado grande";
    case ERANGE: return "fuera de rango";
    case EDOM: return "fuera del dominio";
    case ENOSYS: return "no disponible en SOMA";
    case EIO: return "error de entrada/salida";
    case ENAMETOOLONG: return "nombre demasiado largo";
    default: return "error";
    }
}

/* ================================================================== caracteres anchos (UTF-8) */
size_t wcslen(const wchar_t *s) { size_t n = 0; while (s[n]) n++; return n; }
wchar_t *wcscpy(wchar_t *d, const wchar_t *s) { wchar_t *r = d; while ((*d++ = *s++)) { } return r; }
wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n) { wchar_t *r = d; for (; n && *s; --n) *d++ = *s++; for (; n; --n) *d++ = 0; return r; }
int wcscmp(const wchar_t *a, const wchar_t *b) { while (*a && *a == *b) { a++; b++; } return *a < *b ? -1 : *a > *b; }
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) { for (; n; --n, ++a, ++b) { if (*a != *b) return *a < *b ? -1 : 1; if (!*a) return 0; } return 0; }
wchar_t *wcschr(const wchar_t *s, wchar_t c) { for (;; ++s) { if (*s == c) return (wchar_t *)s; if (!*s) return 0; } }
wchar_t *wcscat(wchar_t *d, const wchar_t *s) { wcscpy(d + wcslen(d), s); return d; }
wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n) { return memcpy(d, s, n * sizeof *d); }
wchar_t *wmemset(wchar_t *d, wchar_t c, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = c; return d; }
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
    (void)ps;
    if (!s) return 0;
    if (!n) return (size_t)-2;
    const unsigned char *u = (const unsigned char *)s; unsigned c = u[0], k, v;
    if (c < 0x80) { if (pwc) *pwc = (wchar_t)c; return c ? 1 : 0; }
    if (c >= 0xf0 && c < 0xf5) { k = 3; v = c & 7; } else if (c >= 0xe0) { k = 2; v = c & 15; } else if (c >= 0xc2 && c < 0xe0) { k = 1; v = c & 31; }
    else { errno = EILSEQ; return (size_t)-1; }
    if (n < k + 1) return (size_t)-2;
    for (unsigned i = 1; i <= k; ++i) { if ((u[i] & 0xc0) != 0x80) { errno = EILSEQ; return (size_t)-1; } v = (v << 6) | (u[i] & 63); }
    if (pwc) *pwc = (wchar_t)v;
    return k + 1;
}
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
    (void)ps;
    unsigned v = (unsigned)wc; unsigned char *o = (unsigned char *)s; char tmp[4];
    if (!o) o = (unsigned char *)tmp;
    if (v < 0x80) { o[0] = (unsigned char)v; return 1; }
    if (v < 0x800) { o[0] = (unsigned char)(0xc0 | v >> 6); o[1] = (unsigned char)(0x80 | (v & 63)); return 2; }
    if (v < 0x10000) { if (v >= 0xd800 && v < 0xe000) { errno = EILSEQ; return (size_t)-1; } o[0] = (unsigned char)(0xe0 | v >> 12); o[1] = (unsigned char)(0x80 | (v >> 6 & 63)); o[2] = (unsigned char)(0x80 | (v & 63)); return 3; }
    if (v < 0x110000) { o[0] = (unsigned char)(0xf0 | v >> 18); o[1] = (unsigned char)(0x80 | (v >> 12 & 63)); o[2] = (unsigned char)(0x80 | (v >> 6 & 63)); o[3] = (unsigned char)(0x80 | (v & 63)); return 4; }
    errno = EILSEQ; return (size_t)-1;
}
int mbtowc(wchar_t *pwc, const char *s, size_t n) { if (!s) return 0; size_t r = mbrtowc(pwc, s, n, 0); return r >= (size_t)-2 ? -1 : (int)r; }
int mblen(const char *s, size_t n) { return mbtowc(0, s, n); }
int wctomb(char *s, wchar_t wc) { if (!s) return 0; size_t r = wcrtomb(s, wc, 0); return r == (size_t)-1 ? -1 : (int)r; }
size_t mbstowcs(wchar_t *d, const char *s, size_t n)
{
    size_t k = 0;
    while (!d || k < n) { wchar_t w = 0; size_t r = mbrtowc(&w, s, 4, 0); if (r == (size_t)-1 || r == (size_t)-2) return (size_t)-1; if (d) d[k] = w; if (!r) return k; s += r; k++; }
    return k;
}
size_t wcstombs(char *d, const wchar_t *s, size_t n)
{
    size_t k = 0; char t[4];
    for (;; ++s) {
        size_t r = wcrtomb(t, *s, 0);
        if (r == (size_t)-1) return (size_t)-1;
        if (!*s) { if (d && k < n) d[k] = 0; return k; }
        if (d && k + r > n) return k;
        if (d) memcpy(d + k, t, r);
        k += r;
    }
}
wint_t btowc(int c) { return c >= 0 && c < 128 ? (wint_t)c : WEOF; }
int wctob(wint_t c) { return c < 128 ? (int)c : -1; }

/* ================================================================== memoria */
/* Bloques con cabecera y pie (tamano | ocupado); libres en listas por clase de tamano (log2). Alineacion de 16. */
static unsigned char __heap[SOMA_HEAP_SIZE] __attribute__((aligned(16)));
static size_t __heap_top;
struct __blk { size_t size; struct __blk *next, *prev; };
#define BLK_HDR 16
#define BLK_MIN 48
#define NCLASES 48
static struct __blk *__libres[NCLASES];
static size_t *pie(struct __blk *b) { return (size_t *)((unsigned char *)b + (b->size & ~(size_t)15) - sizeof(size_t)); }
static int clase(size_t n) { int c = 0; while (n > 32 && c < NCLASES - 1) { n >>= 1; c++; } return c; }
static void quita(struct __blk *b)
{
    int c = clase(b->size & ~(size_t)15);
    if (b->prev) b->prev->next = b->next; else __libres[c] = b->next;
    if (b->next) b->next->prev = b->prev;
}
static void mete(struct __blk *b)
{
    size_t n = b->size & ~(size_t)15;
    int c = clase(n);
    b->size = n; *pie(b) = n;
    b->prev = 0; b->next = __libres[c];
    if (b->next) b->next->prev = b;
    __libres[c] = b;
}
void *malloc(size_t n)
{
    if (n > SOMA_HEAP_SIZE) { errno = ENOMEM; return 0; }
    size_t need = (n + BLK_HDR + sizeof(size_t) + 15) & ~(size_t)15;
    if (need < BLK_MIN) need = BLK_MIN;
    for (int c = clase(need); c < NCLASES; ++c) {
        for (struct __blk *b = __libres[c]; b; b = b->next) {
            size_t sz = b->size & ~(size_t)15;
            if (sz < need) continue;
            quita(b);
            if (sz - need >= BLK_MIN) {
                struct __blk *r = (struct __blk *)((unsigned char *)b + need);
                r->size = sz - need; mete(r);
                sz = need;
            }
            b->size = sz | 1; *pie(b) = sz | 1;
            return (unsigned char *)b + BLK_HDR;
        }
    }
    if (__heap_top + need > SOMA_HEAP_SIZE) { errno = ENOMEM; return 0; }
    struct __blk *b = (struct __blk *)(__heap + __heap_top);
    __heap_top += need;
    b->size = need | 1; *pie(b) = need | 1;
    return (unsigned char *)b + BLK_HDR;
}
void free(void *p)
{
    if (!p) return;
    struct __blk *b = (struct __blk *)((unsigned char *)p - BLK_HDR);
    size_t sz = b->size & ~(size_t)15;
    if ((unsigned char *)b + sz < __heap + __heap_top) {                 /* fusion con el siguiente */
        struct __blk *n = (struct __blk *)((unsigned char *)b + sz);
        if (!(n->size & 1)) { quita(n); sz += n->size & ~(size_t)15; }
    }
    if ((unsigned char *)b > __heap) {                                     /* ... y con el anterior */
        size_t pp = *(size_t *)((unsigned char *)b - sizeof(size_t));
        if (!(pp & 1)) { struct __blk *a = (struct __blk *)((unsigned char *)b - pp); quita(a); sz += pp; b = a; }
    }
    if ((unsigned char *)b + sz == __heap + __heap_top) { __heap_top -= sz; return; }   /* al final: se devuelve a la cima */
    b->size = sz; mete(b);
}
void *calloc(size_t n, size_t m)
{
    if (m && n > SIZE_MAX / m) { errno = ENOMEM; return 0; }
    void *p = malloc(n * m);
    if (p) memset(p, 0, n * m);
    return p;
}
void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    struct __blk *b = (struct __blk *)((unsigned char *)p - BLK_HDR);
    size_t sz = b->size & ~(size_t)15, cap = sz - BLK_HDR - sizeof(size_t);
    if (n <= cap) return p;
    size_t need = (n + BLK_HDR + sizeof(size_t) + 15) & ~(size_t)15;
    if ((unsigned char *)b + sz == __heap + __heap_top && __heap_top - sz + need <= SOMA_HEAP_SIZE) {   /* el ultimo: crece donde esta */
        __heap_top += need - sz;
        b->size = need | 1; *pie(b) = need | 1;
        return p;
    }
    if ((unsigned char *)b + sz < __heap + __heap_top) {                 /* el siguiente esta libre y basta: se une */
        struct __blk *nx = (struct __blk *)((unsigned char *)b + sz);
        size_t ns = nx->size & ~(size_t)15;
        if (!(nx->size & 1) && sz + ns >= need) {
            quita(nx);
            size_t t = sz + ns;
            if (t - need >= BLK_MIN) { struct __blk *r = (struct __blk *)((unsigned char *)b + need); r->size = t - need; mete(r); t = need; }
            b->size = t | 1; *pie(b) = t | 1;
            return p;
        }
    }
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, cap);
    free(p);
    return q;
}

/* ================================================================== numeros */
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }
div_t div(int a, int b) { div_t r; r.quot = a / b; r.rem = a % b; return r; }
ldiv_t ldiv(long a, long b) { ldiv_t r; r.quot = a / b; r.rem = a % b; return r; }
static unsigned long long __strtou(const char *s, char **e, int base, int *neg, int *over)
{
    const char *p = s; unsigned long long v = 0; int any = 0;
    *neg = 0; *over = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { *neg = *p == '-'; p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) { p += 2; base = 16; }
    else if (base == 0 && p[0] == '0') base = 8;
    else if (base == 0) base = 10;
    for (;; p++) {
        int d = isdigit((unsigned char)*p) ? *p - '0' : isalpha((unsigned char)*p) ? tolower((unsigned char)*p) - 'a' + 10 : 99;
        if (d >= base) break;
        if (v > (ULLONG_MAX - (unsigned)d) / (unsigned)base) *over = 1;
        v = v * (unsigned)base + (unsigned)d; any = 1;
    }
    if (e) *e = (char *)(any ? p : s);
    return v;
}
unsigned long long strtoull(const char *s, char **e, int b)
{
    int n, o; unsigned long long v = __strtou(s, e, b, &n, &o);
    if (o) { errno = ERANGE; return ULLONG_MAX; }
    return n ? 0 - v : v;
}
unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long)strtoull(s, e, b); }
long long strtoll(const char *s, char **e, int b)
{
    int n, o; unsigned long long v = __strtou(s, e, b, &n, &o);
    if (o || v > (unsigned long long)LLONG_MAX + (unsigned)n) { errno = ERANGE; return n ? LLONG_MIN : LLONG_MAX; }
    return n ? (long long)(0 - v) : (long long)v;
}
long strtol(const char *s, char **e, int b) { return (long)strtoll(s, e, b); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }

/* potencia de diez en long double (x87, 64 bits de mantisa) por cuadrados */
static long double __p10(int e)
{
    static const long double t[] = { 1e1L, 1e2L, 1e4L, 1e8L, 1e16L, 1e32L, 1e64L, 1e128L, 1e256L, 1e512L, 1e1024L, 1e2048L, 1e4096L };
    long double r = 1.0L; int n = e < 0 ? -e : e;
    for (int i = 0; n && i < 13; ++i, n >>= 1) if (n & 1) r *= t[i];
    if (n) r = 1e4932L * 10.0L;
    return e < 0 ? 1.0L / r : r;
}
/* Lectura de numeros decimales CORRECTAMENTE REDONDEADA (como glibc): una aproximacion en long double y despues se compara el valor
 * exacto (digitos * 10^K, con enteros grandes) con los puntos medios entre candidatos vecinos hasta dar con el bueno. */
#define BG_W 1400
struct __big { int n; uint32_t w[BG_W]; };
static struct __big __ba, __bb;
static void __bg_u64(struct __big *b, uint64_t v) { b->n = 0; if (v) b->w[b->n++] = (uint32_t)v; if (v >> 32) b->w[b->n++] = (uint32_t)(v >> 32); }
static void __bg_mla(struct __big *b, uint32_t m, uint32_t a)
{
    uint64_t c = a;
    for (int i = 0; i < b->n; ++i) { c += (uint64_t)b->w[i] * m; b->w[i] = (uint32_t)c; c >>= 32; }
    if (c && b->n < BG_W) b->w[b->n++] = (uint32_t)c;
}
static void __bg_p10(struct __big *b, int k) { while (k >= 9) { __bg_mla(b, 1000000000u, 0); k -= 9; } while (k-- > 0) __bg_mla(b, 10, 0); }
static void __bg_shl(struct __big *b, int s)
{
    if (!b->n) return;
    int w = s / 32, r = s % 32;
    if (b->n + w + 1 > BG_W) return;
    if (r) { uint32_t c = 0; for (int i = 0; i < b->n; ++i) { uint32_t x = b->w[i]; b->w[i] = (x << r) | c; c = x >> (32 - r); } if (c) b->w[b->n++] = c; }
    if (w) { for (int i = b->n - 1; i >= 0; --i) b->w[i + w] = b->w[i]; for (int i = 0; i < w; ++i) b->w[i] = 0; b->n += w; }
}
static int __bg_cmp(const struct __big *a, const struct __big *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; --i) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}
#define DEC_MAX 780
static char __dd[DEC_MAX + 2];
/* compara digitos(__dd, nd) * 10^K con (M*mul + suma) * 2^E   (suma: -1, 0 o 1) */
static int __dec_cmp(int nd, int K, uint64_t M, uint32_t mul, int suma, int E)
{
    __ba.n = 0;
    for (int i = 0; i < nd;) {                                         /* digitos de 9 en 9 */
        uint32_t c = 0, m = 1; int j = 0;
        for (; j < 9 && i < nd; ++j, ++i) { c = c * 10 + (uint32_t)(__dd[i] - '0'); m *= 10; }
        if (!__ba.n) __bg_u64(&__ba, c); else __bg_mla(&__ba, m, c);
    }
    __bg_u64(&__bb, M);
    __bg_mla(&__bb, mul, suma > 0 ? 1u : 0u);
    if (suma < 0) { for (int i = 0; i < __bb.n; ++i) { if (__bb.w[i]--) break; } while (__bb.n && !__bb.w[__bb.n - 1]) __bb.n--; }
    if (K >= 0) __bg_p10(&__ba, K); else __bg_p10(&__bb, -K);
    if (E >= 0) __bg_shl(&__bb, E); else __bg_shl(&__ba, -E);
    return __bg_cmp(&__ba, &__bb);
}
static long double __strto(const char *s, char **e, int prec, int emin, int emax)
{
    const char *p = s; int neg = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if (!strncasecmp(p, "inf", 3)) { if (e) *e = (char *)(p + (strncasecmp(p, "infinity", 8) ? 3 : 8)); return neg ? -((long double)HUGE_VAL) : ((long double)HUGE_VAL); }
    if (!strncasecmp(p, "nan", 3)) { if (e) *e = (char *)(p + 3); return NAN; }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {                       /* hexadecimal: 0x1.8p3 */
        const char *q = p + 2; unsigned long long m = 0; int ex = 0, any = 0, dot = 0;
        for (;; q++) {
            int d = isdigit((unsigned char)*q) ? *q - '0' : (*q >= 'a' && *q <= 'f') ? *q - 'a' + 10 : (*q >= 'A' && *q <= 'F') ? *q - 'A' + 10 : -1;
            if (*q == '.' && !dot) { dot = 1; continue; }
            if (d < 0) break;
            any = 1;
            if (m < (1ULL << 60)) { m = m * 16 + (unsigned)d; if (dot) ex -= 4; } else if (!dot) ex += 4;
        }
        if (any) {
            if (*q == 'p' || *q == 'P') { char *z; long x = strtol(q + 1, &z, 10); if (z != q + 1) { ex += (int)x; q = z; } }
            if (e) *e = (char *)q;
            long double v = ldexpl((long double)m, ex);
            return neg ? -v : v;
        }
    }
    /* digitos significativos (sin ceros de delante) en __dd; K = exponente decimal del ultimo guardado */
    int nd = 0, K = 0, any = 0, dot = 0, resto = 0;
    for (;; p++) {
        if (*p == '.' && !dot) { dot = 1; continue; }
        if (!isdigit((unsigned char)*p)) break;
        any = 1;
        if (!nd && *p == '0') { if (dot) K--; continue; }
        if (nd < DEC_MAX) { __dd[nd++] = *p; if (dot) K--; }
        else { if (*p != '0') resto = 1; if (!dot) K++; }
    }
    if (!any) { if (e) *e = (char *)s; return 0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1; int en = 0; long x = 0;
        if (*q == '+' || *q == '-') { en = *q == '-'; q++; }
        if (isdigit((unsigned char)*q)) { while (isdigit((unsigned char)*q)) { if (x < 100000) x = x * 10 + (*q - '0'); q++; } K += en ? -(int)x : (int)x; p = q; }
    }
    if (e) *e = (char *)p;
    while (nd > 1 && __dd[nd - 1] == '0') { nd--; K++; }
    if (!nd) return neg ? -0.0L : 0.0L;
    if (resto) { __dd[nd++] = '1'; K--; }                               /* lo que se corto no era cero: ni empate ni exacto */
    int x10 = nd + K;                                                    /* valor en [10^(x10-1), 10^x10) */
    long double v;
    if (x10 > 4940) { errno = ERANGE; return neg ? -((long double)HUGE_VAL) : ((long double)HUGE_VAL); }
    if (x10 < -4960) { errno = ERANGE; return neg ? -0.0L : 0.0L; }
    {   /* aproximacion */
        unsigned long long m = 0; int i = 0;
        for (; i < nd && i < 19; ++i) m = m * 10 + (unsigned)(__dd[i] - '0');
        int k = K + (nd - i);
        v = (long double)m;
        v = k < -4900 ? (v / __p10(4900)) * __p10(k + 4900) : v * __p10(k);
    }
    /* candidato (m, ex): m de 'prec' bits salvo subnormales (ex == emin) */
    uint64_t top = 1ULL << (prec - 1), M;
    int ex;
    {
        long double a = v;
        if (prec == 53) a = (double)a; else if (prec == 24) a = (float)a;
        if (a - a != 0 || a != a) { M = (prec == 64 ? ~0ULL : (top << 1) - 1); ex = emax; }
        else if (a == 0) { M = 0; ex = emin; }
        else {
            int b2 = 0; long double t = a;
            union { long double f; struct { uint64_t m; uint16_t se; } q; } u; u.f = t;
            b2 = ((u.q.se & 0x7fff) ? (u.q.se & 0x7fff) : 1) - 16383 - 63; /* t = u.q.m * 2^b2 */
            uint64_t mm = u.q.m;
            int sh = 64 - prec;                                           /* lo pasamos a 'prec' bits (es exacto: a ya esta redondeado) */
            M = mm >> sh; ex = b2 + sh;
            if (ex < emin) { M >>= (emin - ex); ex = emin; }
        }
    }
    uint64_t Mmax = prec == 64 ? ~0ULL : (top << 1) - 1;
    for (int it = 0; it < 200; ++it) {
        int c = __dec_cmp(nd, K, M, 2, 1, ex - 1);                       /* punto medio de arriba: (2M+1) * 2^(ex-1) */
        if (c > 0 || (c == 0 && (M & 1))) {
            if (M == Mmax) {
                if (ex >= emax) { errno = ERANGE; return neg ? -((long double)HUGE_VAL) : ((long double)HUGE_VAL); }
                M = top; ex++;
            } else M++;
            if (c == 0) break;
            continue;
        }
        if (c == 0 || M == 0) break;                                      /* empate con M par: se queda */
        int frontera = M == top && ex > emin;                             /* por debajo el paso es la mitad */
        int d = frontera ? __dec_cmp(nd, K, M, 4, -1, ex - 2) : __dec_cmp(nd, K, M, 2, -1, ex - 1);
        if (d < 0 || (d == 0 && (M & 1))) {
            if (frontera) { M = Mmax; ex--; } else M--;
            if (d == 0) break;
            continue;
        }
        break;
    }
    if (M == 0) { errno = ERANGE; return neg ? -0.0L : 0.0L; }
    v = ldexpl((long double)M, ex);
    return neg ? -v : v;
}
long double strtold(const char *s, char **e) { return __strto(s, e, 64, -16445, 16320); }
double strtod(const char *s, char **e) { return (double)__strto(s, e, 53, -1074, 971); }
float strtof(const char *s, char **e) { return (float)__strto(s, e, 24, -149, 104); }
double atof(const char *s) { return strtod(s, 0); }

static unsigned long __rnd = 1;
int rand(void) { __rnd = __rnd * 6364136223846793005UL + 1442695040888963407UL; return (int)((__rnd >> 33) & RAND_MAX); }
void srand(unsigned s) { __rnd = s; }

static void __swap(char *a, char *b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }
static void __qs(char *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    while (n > 12) {
        char *m = b + (n / 2) * sz, *l = b + (n - 1) * sz;
        if (cmp(m, b) < 0) __swap(m, b, sz);
        if (cmp(l, m) < 0) { __swap(l, m, sz); if (cmp(m, b) < 0) __swap(m, b, sz); }
        __swap(m, b + sz, sz);
        char *p = b + sz; size_t i = 1, j = n - 1;
        for (;;) {
            do i++; while (i < n && cmp(b + i * sz, p) < 0);
            do j--; while (cmp(b + j * sz, p) > 0);
            if (i >= j) break;
            __swap(b + i * sz, b + j * sz, sz);
        }
        __swap(p, b + j * sz, sz);
        if (j < n - j) { __qs(b, j, sz, cmp); b += (j + 1) * sz; n -= j + 1; }
        else { __qs(b + (j + 1) * sz, n - j - 1, sz, cmp); n = j; }
    }
    for (size_t i = 1; i < n; ++i)
        for (size_t k = i; k > 0 && cmp(b + (k - 1) * sz, b + k * sz) > 0; --k) __swap(b + (k - 1) * sz, b + k * sz, sz);
}
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *)) { if (n > 1 && sz) __qs((char *)b, n, sz, cmp); }
void *bsearch(const void *k, const void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t m = (lo + hi) / 2; int c = cmp(k, (const char *)b + m * sz); if (!c) return (char *)b + m * sz; if (c < 0) hi = m; else lo = m + 1; }
    return 0;
}
char *getenv(const char *n) { (void)n; return 0; }
int system(const char *c) { (void)c; errno = ENOSYS; return -1; }

/* ================================================================== matematicas (x87) */
long double ldexpl(long double x, int e) { long double r, ee = e; __asm__ volatile ("fldt %2\n\tfldt %1\n\tfscale\n\tfstp %%st(1)\n\tfstpt %0" : "=m"(r) : "m"(x), "m"(ee)); return r; }
double ldexp(double x, int e) { return (double)ldexpl(x, e); }
/* x87 con operandos en memoria: TinyCC no conoce las restricciones "t"/"u" de gcc */
#define X87_1(ins, r, x) do { long double __a = (x); __asm__ volatile ("fldt %1\n\t" ins "\n\tfstpt %0" : "=m"(r) : "m"(__a)); } while (0)
double sqrt(double x) { long double r; if (x < 0) { errno = EDOM; return NAN; } X87_1("fsqrt", r, x); return (double)r; }
double fabs(double x) { return x < 0 ? -x : x; }
double sin(double x) { long double r; X87_1("fsin", r, x); return (double)r; }
double cos(double x) { long double r; X87_1("fcos", r, x); return (double)r; }
double tan(double x) { return sin(x) / cos(x); }
double atan2(double y, double x) { long double r, a = y, b = x; __asm__ volatile ("fldt %1\n\tfldt %2\n\tfpatan\n\tfstpt %0" : "=m"(r) : "m"(a), "m"(b)); return (double)r; }
double atan(double x) { return atan2(x, 1.0); }
double asin(double x) { return atan2(x, sqrt(1.0 - x * x)); }
double acos(double x) { return atan2(sqrt(1.0 - x * x), x); }
static long double __log2l(long double x) { long double r; __asm__ volatile ("fld1\n\tfldt %1\n\tfyl2x\n\tfstpt %0" : "=m"(r) : "m"(x)); return r; }
static long double __exp2l(long double x)
{
    long double i, f, r;
    X87_1("frndint", i, x);
    f = x - i;
    X87_1("f2xm1", r, f);
    return ldexpl(r + 1.0L, (int)i);
}
double log(double x) { if (x < 0) { errno = EDOM; return NAN; } if (x == 0) return -HUGE_VAL; return (double)(__log2l(x) * 0.693147180559945309417232121458176568L); }
double log2(double x) { return log(x) / 0.693147180559945309417232121458176568; }
double log10(double x) { return log(x) / 2.302585092994045684017991454684364208; }
double exp(double x) { if (x > 11356) return HUGE_VAL; if (x < -11400) return 0; return (double)__exp2l((long double)x * 1.442695040888963407359924681001892137L); }
double floor(double x) { if (!(x - x == 0)) return x; double t = (double)(long long)x; if (fabs(x) >= 4503599627370496.0) return x; return t > x ? t - 1 : t; }
double ceil(double x) { double f = floor(x); return f == x ? f : f + 1; }
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
double fmod(double x, double y)
{
    long double r, a = x, b = y;
    if (y == 0) { errno = EDOM; return NAN; }
    __asm__ volatile ("fldt %2\n\tfldt %1\n1:\n\tfprem\n\tfnstsw %%ax\n\ttestb $4, %%ah\n\tjnz 1b\n\tfstp %%st(1)\n\tfstpt %0" : "=m"(r) : "m"(a), "m"(b) : "ax", "cc");
    return (double)r;
}
double pow(double x, double y)
{
    if (y == 0) return 1;
    if (x == 0) return y > 0 ? 0 : HUGE_VAL;
    if (y == (double)(long long)y && fabs(y) < 1e18) {                    /* potencia entera: exacta cuando se puede */
        long long n = (long long)y; long double r = 1, b = x; int neg = n < 0; if (neg) n = -n;
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return (double)(neg ? 1 / r : r);
    }
    if (x < 0) { errno = EDOM; return NAN; }
    return (double)__exp2l((long double)y * __log2l(x));
}
double sinh(double x) { double e = exp(x); return (e - 1 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1 / e) / 2; }
double tanh(double x) { if (x > 20) return 1; if (x < -20) return -1; double e = exp(2 * x); return (e - 1) / (e + 1); }
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3) : pow(x, 1.0 / 3); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
double frexp(double x, int *e)
{
    *e = 0;
    if (x == 0 || !(x - x == 0)) return x;
    long double v = x; int k = 0; long double a = v < 0 ? -v : v;
    while (a >= 1) { a /= 2; k++; }
    while (a < 0.5L) { a *= 2; k--; }
    *e = k;
    return (double)(v < 0 ? -a : a);
}
double modf(double x, double *i) { double t = trunc(x); *i = t; return x - t; }
float sqrtf(float x) { return (float)sqrt(x); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float fabsf(float x) { return x < 0 ? -x : x; }
float floorf(float x) { return (float)floor(x); }
float powf(float x, float y) { return (float)pow(x, y); }

/* ================================================================== archivos */
/* Embebidos (solo el compilador: cabeceras y fuentes de la biblioteca, en /tcc/...), del VFS de SOMA, la consola (0, 1, 2) y
 * "@programa.c": el codigo que el nucleo le pasa al compilador sin escribirlo en disco (programador). */
struct __fs_ent { const char *path; const unsigned char *data; unsigned long len; };
extern const struct __fs_ent __soma_fs[] __attribute__((weak));
#define NFD 32
struct __fd { int used, wr, dirty; unsigned char *data; size_t len, cap, pos; int owned; char path[256]; };
static struct __fd __fds[NFD];
static char __path_tmp[256];

static const char *__ruta(const char *p)
{
    if (p[0] == '/' || p[0] == '@') return p;
    size_t k = 0; const char *w = "/workspace/";
    while (*w && k < sizeof __path_tmp - 1) __path_tmp[k++] = *w++;
    while (*p && k < sizeof __path_tmp - 1) __path_tmp[k++] = *p++;
    __path_tmp[k] = 0;
    return __path_tmp;
}
static long __vfs(int op, const char *path, void *buf, unsigned long len)
{
    struct soma_vfs_req r; r.path = path; r.plen = strlen(path); r.buf = buf; r.len = len;
    return __soma_sys(SOMA_SYS_VFS, op, (long)&r, 0);
}
static int __err(long e) { errno = e == -13 ? EACCES : e == -2 ? ENOENT : e == -12 ? ENOMEM : e == -27 ? EFBIG : e == -36 ? ENAMETOOLONG : EIO; return -1; }
static const struct __fs_ent *__embebido(const char *p)
{
    if (!__soma_fs) return 0;
    for (const struct __fs_ent *e = __soma_fs; e->path; ++e) if (!strcmp(e->path, p)) return e;
    return 0;
}
int open(const char *path, int flags, ...)
{
    int fd = 3;
    while (fd < NFD && __fds[fd].used) fd++;
    if (fd >= NFD) { errno = EMFILE; return -1; }
    const char *p = __ruta(path);
    if (strlen(p) >= sizeof __fds[fd].path) { errno = ENAMETOOLONG; return -1; }
    struct __fd *f = &__fds[fd];
    memset(f, 0, sizeof *f);
    strcpy(f->path, p);
    if (flags & (O_WRONLY | O_RDWR | O_CREAT)) {
        f->used = 1; f->wr = 1; f->owned = 1; f->dirty = 1;
        if (!(flags & O_TRUNC) && !(flags & O_CREAT)) { /* abrir para escribir sin truncar: se parte de lo que hay */
            long n = __vfs(2, p, 0, 0);
            if (n > 0) { f->data = malloc((size_t)n); if (f->data) { long r = __vfs(0, p, f->data, (unsigned long)n); f->len = r > 0 ? (size_t)r : 0; f->cap = (size_t)n; } }
        }
        if (flags & O_APPEND) f->pos = f->len;
        long w = __vfs(1, p, f->data, f->len);                            /* se crea ya: si no se puede escribir ahi, falla aqui */
        if (w < 0) { free(f->data); memset(f, 0, sizeof *f); return __err(w); }
        return fd;
    }
    const struct __fs_ent *e = __embebido(p);
    if (e) { f->used = 1; f->data = (unsigned char *)e->data; f->len = e->len; return fd; }
    if (!strcmp(p, "@programa.c")) {
        long n = __soma_sys(SOMA_SYS_CON, 8, 0, 0);
        if (n < 0) return __err(n);
        f->data = malloc((size_t)n + 1);
        if (!f->data) { errno = ENOMEM; return -1; }
        long r = __soma_sys(SOMA_SYS_CON, 8, (long)f->data, n);
        f->len = r > 0 ? (size_t)r : 0; f->owned = 1; f->used = 1;
        return fd;
    }
    long n = __vfs(2, p, 0, 0);
    if (n < 0) return __err(n);
    f->data = malloc((size_t)n + 1);
    if (!f->data) { errno = ENOMEM; return -1; }
    long r = n ? __vfs(0, p, f->data, (unsigned long)n) : 0;
    if (r < 0) { free(f->data); f->data = 0; return __err(r); }
    f->len = (size_t)r; f->owned = 1; f->used = 1;
    return fd;
}
static void __stdout_flush(void);
ssize_t write(int fd, const void *b, size_t n)
{
    if (fd == 1 || fd == 2) {
        const char *s = b; size_t t = n;
        while (n) { long w = __soma_sys(SOMA_SYS_WRITE, (long)s, (long)n, 0); if (w <= 0) break; s += w; n -= (size_t)w; }
        return (ssize_t)(t - n);
    }
    if (fd < 3 || fd >= NFD || !__fds[fd].used || !__fds[fd].wr) { errno = EBADF; return -1; }
    struct __fd *f = &__fds[fd];
    if (f->pos + n > f->cap) {
        size_t c = f->cap ? f->cap * 2 : 4096;
        while (c < f->pos + n) c *= 2;
        unsigned char *d = realloc(f->data, c);
        if (!d) { errno = ENOMEM; return -1; }
        f->data = d; f->cap = c;
    }
    if (f->pos > f->len) memset(f->data + f->len, 0, f->pos - f->len);
    memcpy(f->data + f->pos, b, n);
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    f->dirty = 1;
    return (ssize_t)n;
}
static long __leer_linea(char *buf, long max)                         /* una linea de la consola, sin el salto; <0 = fin */
{
    __stdout_flush();
    return __soma_sys(SOMA_SYS_CON, 0, (long)buf, max);
}
static char __in_buf[1100]; static size_t __in_len, __in_pos; static int __in_eof;
ssize_t read(int fd, void *b, size_t n)
{
    if (fd == 0) {
        if (__in_pos >= __in_len) {
            if (__in_eof) return 0;
            long r = __leer_linea(__in_buf, 1024);
            if (r < 0) { __in_eof = 1; return 0; }
            __in_buf[r] = '\n'; __in_len = (size_t)r + 1; __in_pos = 0;
        }
        size_t m = __in_len - __in_pos; if (m > n) m = n;
        memcpy(b, __in_buf + __in_pos, m); __in_pos += m;
        return (ssize_t)m;
    }
    if (fd < 3 || fd >= NFD || !__fds[fd].used) { errno = EBADF; return -1; }
    struct __fd *f = &__fds[fd];
    if (f->pos >= f->len) return 0;
    size_t m = f->len - f->pos; if (m > n) m = n;
    memcpy(b, f->data + f->pos, m); f->pos += m;
    return (ssize_t)m;
}
off_t lseek(int fd, off_t o, int w)
{
    if (fd < 3 || fd >= NFD || !__fds[fd].used) { errno = fd < 3 ? ESPIPE : EBADF; return -1; }
    struct __fd *f = &__fds[fd];
    off_t base = w == SEEK_SET ? 0 : w == SEEK_CUR ? (off_t)f->pos : (off_t)f->len;
    if (base + o < 0) { errno = EINVAL; return -1; }
    f->pos = (size_t)(base + o);
    return (off_t)f->pos;
}
int close(int fd)
{
    if (fd < 3) return 0;
    if (fd >= NFD || !__fds[fd].used) { errno = EBADF; return -1; }
    struct __fd *f = &__fds[fd];
    int rc = 0;
    if (f->wr && f->dirty) { long r = __vfs(1, f->path, f->data, f->len); if (r < 0) rc = __err(r); }
    if (f->owned) free(f->data);
    memset(f, 0, sizeof *f);
    return rc;
}
int unlink(const char *p) { (void)p; return 0; }                       /* el VFS no borra desde ring 3: se sobrescribe al guardar */
int remove(const char *p) { return unlink(p); }
int rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
char *getcwd(char *b, size_t n) { if (!b || n < 11) { errno = ERANGE; return 0; } strcpy(b, "/workspace"); return b; }
char *realpath(const char *p, char *r) { const char *q = __ruta(p); if (!r) return strdup(q); strcpy(r, q); return r; }
int isatty(int fd) { return fd < 3; }
int access(const char *p, int m)
{
    (void)m;
    const char *q = __ruta(p);
    if (__embebido(q)) return 0;
    long r = __vfs(2, q, 0, 0);
    return r >= 0 ? 0 : __err(r);
}
int stat(const char *p, struct stat *s)
{
    const char *q = __ruta(p); const struct __fs_ent *e = __embebido(q);
    long n = e ? (long)e->len : __vfs(2, q, 0, 0);
    if (n < 0) return __err(n);
    memset(s, 0, sizeof *s); s->st_size = n; s->st_mode = S_IFREG | 0644;
    return 0;
}

/* ================================================================== stdio */
struct _soma_file { int fd, eof, err, unget; char obuf[512]; size_t on; };
static FILE __f_in = { 0, 0, 0, -1, {0}, 0 }, __f_out = { 1, 0, 0, -1, {0}, 0 }, __f_err = { 2, 0, 0, -1, {0}, 0 };
FILE *stdin = &__f_in, *stdout = &__f_out, *stderr = &__f_err;
static FILE *__abiertos[NFD];
int fflush(FILE *f)
{
    if (!f) { fflush(stdout); fflush(stderr); for (int i = 0; i < NFD; ++i) if (__abiertos[i]) fflush(__abiertos[i]); return 0; }
    if (f->on) { write(f->fd, f->obuf, f->on); f->on = 0; }
    return 0;
}
static void __stdout_flush(void) { fflush(stdout); fflush(stderr); }
FILE *fdopen(int fd, const char *m)
{
    (void)m;
    FILE *f = calloc(1, sizeof *f);
    if (!f) return 0;
    f->fd = fd; f->unget = -1;
    if (fd >= 0 && fd < NFD) __abiertos[fd] = f;
    return f;
}
FILE *fopen(const char *p, const char *m)
{
    int fl = O_RDONLY;
    if (m[0] == 'w') fl = O_WRONLY | O_CREAT | O_TRUNC;
    else if (m[0] == 'a') fl = O_WRONLY | O_APPEND;
    else if (strchr(m, '+')) fl = O_RDWR;
    int fd = open(p, fl);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, m);
    if (!f) close(fd);
    return f;
}
int fclose(FILE *f)
{
    if (!f) return EOF;
    fflush(f);
    int r = close(f->fd);
    if (f->fd >= 0 && f->fd < NFD) __abiertos[f->fd] = 0;
    if (f != stdin && f != stdout && f != stderr) free(f);
    return r ? EOF : 0;
}
FILE *freopen(const char *p, const char *m, FILE *f) { if (f) fclose(f); return fopen(p, m); }
size_t fwrite(const void *b, size_t s, size_t n, FILE *f)
{
    size_t t = s * n; const char *c = b;
    if (!t) return 0;
    if (f->fd >= 3) { fflush(f); ssize_t w = write(f->fd, b, t); if (w < 0) { f->err = 1; return 0; } return (size_t)w / s; }
    for (size_t i = 0; i < t; ++i) {
        f->obuf[f->on++] = c[i];
        if (f->on == sizeof f->obuf || (c[i] == '\n' && f->fd == 1) || f->fd == 2) fflush(f);
    }
    return n;
}
int fputc(int ch, FILE *f) { unsigned char c = (unsigned char)ch; return fwrite(&c, 1, 1, f) == 1 ? c : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { size_t n = strlen(s); return fwrite(s, 1, n, f) == n ? 0 : EOF; }
int puts(const char *s) { if (fputs(s, stdout) == EOF) return EOF; return fputc('\n', stdout) == EOF ? EOF : 0; }
int fgetc(FILE *f)
{
    if (f->unget >= 0) { int c = f->unget; f->unget = -1; return c; }
    unsigned char c;
    if (f == stdout || f == stderr) return EOF;
    ssize_t r = read(f->fd, &c, 1);
    if (r <= 0) { f->eof = 1; return EOF; }
    return c;
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
int ungetc(int c, FILE *f) { if (c == EOF) return EOF; f->unget = (unsigned char)c; f->eof = 0; return c; }
char *fgets(char *s, int n, FILE *f)
{
    int i = 0;
    while (i < n - 1) { int c = fgetc(f); if (c == EOF) break; s[i++] = (char)c; if (c == '\n') break; }
    if (!i) return 0;
    s[i] = 0;
    return s;
}
size_t fread(void *b, size_t s, size_t n, FILE *f)
{
    size_t t = s * n, k = 0; unsigned char *d = b;
    while (k < t) { int c = fgetc(f); if (c == EOF) break; d[k++] = (unsigned char)c; }
    return s ? k / s : 0;
}
int fseek(FILE *f, long o, int w) { fflush(f); f->unget = -1; f->eof = 0; return lseek(f->fd, o, w) < 0 ? -1 : 0; }
long ftell(FILE *f) { fflush(f); long p = lseek(f->fd, 0, SEEK_CUR); return f->unget >= 0 && p > 0 ? p - 1 : p; }
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->err = 0; }
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = f->err = 0; }
int fileno(FILE *f) { return f->fd; }
int setvbuf(FILE *f, char *b, int m, size_t s) { (void)f; (void)b; (void)m; (void)s; return 0; }
void setbuf(FILE *f, char *b) { (void)f; (void)b; }
void perror(const char *s) { if (s && *s) { fputs(s, stderr); fputs(": ", stderr); } fputs(strerror(errno), stderr); fputc('\n', stderr); }

/* ---------------------------------------------------------------- printf */
struct __sal { char *s; size_t n, cap; FILE *f; };
static void __pon(struct __sal *o, const char *p, size_t n)
{
    if (o->f) { fwrite(p, 1, n, o->f); o->n += n; return; }
    for (size_t i = 0; i < n; ++i, ++o->n) if (o->n + 1 < o->cap) o->s[o->n] = p[i];
}
static void __rep(struct __sal *o, char c, int n) { while (n-- > 0) __pon(o, &c, 1); }
/* Coma flotante EXACTA, como glibc: el valor binario se pasa a decimal con enteros grandes (v = m * 2^e -> m * 5^-e / 10^-e) y se
 * redondea al par en los empates exactos. Vale para double y long double (x87, 64 bits de mantisa). */
#define BN_W 1300
static uint32_t __bn[BN_W]; static int __bnn;
static char __dg[12600];                                              /* digitos del valor exacto */
static void __bn_mul(uint32_t m)
{
    uint64_t c = 0;
    for (int i = 0; i < __bnn; ++i) { c += (uint64_t)__bn[i] * m; __bn[i] = (uint32_t)c; c >>= 32; }
    if (c && __bnn < BN_W) __bn[__bnn++] = (uint32_t)c;
}
static void __bn_shl(int s)
{
    int w = s / 32, b = s % 32;
    if (__bnn + w + 1 > BN_W) return;
    if (b) { uint32_t c = 0; for (int i = 0; i < __bnn; ++i) { uint32_t x = __bn[i]; __bn[i] = (x << b) | c; c = x >> (32 - b); } if (c) __bn[__bnn++] = c; }
    if (w) { for (int i = __bnn - 1; i >= 0; --i) __bn[i + w] = __bn[i]; for (int i = 0; i < w; ++i) __bn[i] = 0; __bnn += w; }
}
static uint32_t __bn_div(uint32_t d)
{
    uint64_t r = 0;
    for (int i = __bnn - 1; i >= 0; --i) { r = (r << 32) | __bn[i]; __bn[i] = (uint32_t)(r / d); r %= d; }
    while (__bnn && !__bn[__bnn - 1]) __bnn--;
    return (uint32_t)r;
}
/* v > 0 finito -> __dg (sin ceros al final), *nd digitos; devuelve el exponente decimal del primero */
static int __exacto(long double v, int *nd)
{
    union { long double f; struct { uint64_t m; uint16_t se; } p; } u;
    u.f = v;
    uint64_t m = u.p.m; int ex = u.p.se & 0x7fff;
    int e = (ex ? ex : 1) - 16383 - 63;
    while (!(m & 1)) { m >>= 1; e++; }
    __bnn = 0; __bn[__bnn++] = (uint32_t)m; if (m >> 32) __bn[__bnn++] = (uint32_t)(m >> 32);
    int k = 0;
    if (e > 0) __bn_shl(e);
    else if (e < 0) { k = -e; int r = k; while (r >= 13) { __bn_mul(1220703125u); r -= 13; } while (r--) __bn_mul(5); }
    int L = 0; char tmp[9];
    while (__bnn) {                                                   /* trozos de 9 cifras, del final al principio */
        uint32_t c = __bn_div(1000000000u);
        int t = 0; for (; t < 9; ++t) { tmp[t] = (char)('0' + c % 10); c /= 10; }
        for (t = 0; t < 9 && L < (int)sizeof __dg; ++t) __dg[L++] = tmp[t];
    }
    while (L > 1 && __dg[L - 1] == '0') L--;                          /* ceros de delante (estan al final: va al reves) */
    for (int i = 0; i < L / 2; ++i) { char c = __dg[i]; __dg[i] = __dg[L - 1 - i]; __dg[L - 1 - i] = c; }
    int x = L - 1 - k;
    while (L > 1 && __dg[L - 1] == '0') L--;
    *nd = L;
    return x;
}
/* deja n cifras en __dg (L validas) redondeando al par: 0 = bien, 1 = acarreo (sube el exponente), -1 = queda cero */
static int __redondea(int L, int n)
{
    if (n < 0) return -1;
    if (n >= L) { for (int i = L; i < n && i < (int)sizeof __dg; ++i) __dg[i] = '0'; return 0; }
    char c = __dg[n]; int up;
    if (c != '5') up = c > '5';
    else { int resto = 0; for (int i = n + 1; i < L; ++i) if (__dg[i] != '0') { resto = 1; break; } up = resto ? 1 : n > 0 ? ((__dg[n - 1] - '0') & 1) : 0; }
    if (!up) return n == 0 ? -1 : 0;
    if (n == 0) { __dg[0] = '1'; return 1; }
    int i = n - 1;
    while (i >= 0 && __dg[i] == '9') __dg[i--] = '0';
    if (i >= 0) { __dg[i]++; return 0; }
    __dg[0] = '1'; for (int j = 1; j < n; ++j) __dg[j] = '0';
    return 1;
}
#define FMT_PREC_MAX 4000
static char __fo[FMT_PREC_MAX + 5000 + 64];
static int __fmt_e(char *b, int x, int prec, int alt, int may)
{
    int n = 0;
    b[n++] = __dg[0];
    if (prec || alt) b[n++] = '.';
    for (int i = 1; i <= prec; ++i) b[n++] = __dg[i];
    b[n++] = may ? 'E' : 'e';
    b[n++] = x < 0 ? '-' : '+';
    if (x < 0) x = -x;
    char t[8]; int k = 0; do { t[k++] = (char)('0' + x % 10); x /= 10; } while (x);
    if (k < 2) t[k++] = '0';
    while (k) b[n++] = t[--k];
    return n;
}
static int __fmt_f(char *b, int x, int prec, int alt)               /* __dg tiene x+1+prec cifras (o es cero si x+1+prec <= 0) */
{
    int n = 0;
    if (x < 0) b[n++] = '0';
    else for (int i = 0; i <= x; ++i) b[n++] = __dg[i];
    if (prec || alt) b[n++] = '.';
    for (int j = 0; j < prec; ++j) { int i = x + 1 + j; b[n++] = i < 0 ? '0' : __dg[i]; }
    return n;
}
static int __fmt_float(char **out, long double v, char conv, int prec, int alt)
{
    char *b = __fo; *out = b;
    int may = isupper((unsigned char)conv) != 0;
    if (v != v) { strcpy(b, may ? "NAN" : "nan"); return 3; }
    if (v - v != 0) { strcpy(b, may ? "INF" : "inf"); return 3; }
    if (prec < 0) prec = conv == 'a' || conv == 'A' ? -1 : 6;
    if (prec > FMT_PREC_MAX) prec = FMT_PREC_MAX;
    char c = (char)tolower((unsigned char)conv);
    if (c == 'a') {                                                   /* hexadecimal, como glibc con double: 0x1.8p+1 */
        double d = (double)v; union { double f; uint64_t u; } q; q.f = d;
        int ex = (int)((q.u >> 52) & 0x7ff); uint64_t mm = q.u & ((1ULL << 52) - 1);
        int n = 0; const char *hx = may ? "0123456789ABCDEF" : "0123456789abcdef";
        b[n++] = '0'; b[n++] = may ? 'X' : 'x';
        if (d == 0) { b[n++] = '0'; ex = 0; }
        else { b[n++] = ex ? '1' : '0'; ex = ex ? ex - 1023 : -1022; }
        char t[13]; int k = 13; for (int i = 0; i < 13; ++i) t[i] = hx[(mm >> (48 - 4 * i)) & 15];
        if (prec < 0) { while (k && t[k - 1] == '0') k--; } else k = prec < 13 ? prec : 13;
        if (k || alt) b[n++] = '.';
        for (int i = 0; i < k; ++i) b[n++] = t[i];
        for (int i = 13; i < prec; ++i) b[n++] = '0';
        b[n++] = may ? 'P' : 'p'; b[n++] = ex < 0 ? '-' : '+'; if (ex < 0) ex = -ex;
        char z[8]; int m = 0; do { z[m++] = (char)('0' + ex % 10); ex /= 10; } while (ex);
        while (m) b[n++] = z[--m];
        return n;
    }
    int L = 1, x = 0;
    if (v == 0) __dg[0] = '0'; else x = __exacto(v, &L);
    if (c == 'e') {
        int r = __redondea(L, prec + 1);
        if (r == 1) x++;
        if (v == 0) x = 0;
        return __fmt_e(b, x, prec, alt, may);
    }
    if (c == 'f') {
        int r = __redondea(L, x + 1 + prec);
        if (r == 1) { int habia = x + 1 + prec > 1 ? x + 1 + prec : 1; x++; __redondea(habia, x + 1 + prec); }
        if (r == -1 || v == 0) { x = 0; for (int i = 0; i <= prec; ++i) __dg[i] = '0'; }
        return __fmt_f(b, x, prec, alt);
    }
    /* %g */
    int P = prec ? prec : 1;
    int r = __redondea(L, P);
    if (r == 1) x++;
    if (v == 0) x = 0;
    int n;
    if (x < -4 || x >= P) n = __fmt_e(b, x, P - 1, alt, may);
    else {
        int fp = P - 1 - x;                                            /* __dg ya tiene P = x+1+fp cifras */
        for (int i = P; i < x + 1 + fp; ++i) __dg[i] = '0';
        n = __fmt_f(b, x, fp, alt);
    }
    if (!alt) {                                                       /* sin '#', %g quita los ceros del final */
        int ep = n; for (int i = 0; i < n; ++i) if (b[i] == 'e' || b[i] == 'E') { ep = i; break; }
        int dot = -1; for (int i = 0; i < ep; ++i) if (b[i] == '.') { dot = i; break; }
        if (dot >= 0) {
            int z = ep; while (z > dot + 1 && b[z - 1] == '0') z--;
            if (z == dot + 1) z = dot;
            memmove(b + z, b + ep, (size_t)(n - ep)); n -= ep - z;
        }
    }
    return n;
}
static int __vfmt(struct __sal *o, const char *f, va_list ap)
{
    for (; *f; ++f) {
        if (*f != '%') { const char *q = f; while (*q && *q != '%') q++; __pon(o, f, (size_t)(q - f)); f = q - 1; continue; }
        ++f;
        int menos = 0, mas = 0, esp = 0, alt = 0, cero = 0, ancho = 0, prec = -1, lng = 0;
        for (;; ++f) { if (*f == '-') menos = 1; else if (*f == '+') mas = 1; else if (*f == ' ') esp = 1; else if (*f == '#') alt = 1; else if (*f == '0') cero = 1; else break; }
        if (*f == '*') { ancho = va_arg(ap, int); if (ancho < 0) { menos = 1; ancho = -ancho; } ++f; }
        else while (isdigit((unsigned char)*f)) ancho = ancho * 10 + (*f++ - '0');
        if (*f == '.') { ++f; prec = 0; if (*f == '*') { prec = va_arg(ap, int); ++f; } else while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0'); }
        for (;; ++f) { if (*f == 'l') lng++; else if (*f == 'h') lng--; else if (*f == 'z' || *f == 'j' || *f == 't') lng = 2; else if (*f == 'L') lng = 3; else break; }
        char tmp[200]; const char *s = tmp; int n = 0; char signo = 0; const char *pre = "";
        char c = *f;
        if (!c) break;
        if (c == 'd' || c == 'i') {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            if (lng == -1) v = (short)v; else if (lng <= -2) v = (signed char)v;
            unsigned long long u = v < 0 ? 0 - (unsigned long long)v : (unsigned long long)v;
            signo = v < 0 ? '-' : mas ? '+' : esp ? ' ' : 0;
            char r[24]; int k = 0; do { r[k++] = (char)('0' + u % 10); u /= 10; } while (u);
            while (n < prec - k) tmp[n++] = '0';
            while (k) tmp[n++] = r[--k];
            if (prec == 0 && v == 0) n = 0;
            if (prec >= 0) cero = 0;
        } else if (c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p') {
            unsigned long long u;
            if (c == 'p') {
                u = (unsigned long long)(uintptr_t)va_arg(ap, void *);
                if (!u) { s = "(nil)"; n = 5; cero = 0; goto poner; }          /* como glibc */
                alt = 1; c = 'x'; lng = 2;
            }
            else u = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            if (lng == -1) u = (unsigned short)u; else if (lng <= -2) u = (unsigned char)u;
            unsigned b = c == 'o' ? 8 : c == 'u' ? 10 : 16;
            const char *dig = c == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char r[24]; int k = 0; unsigned long long w = u; do { r[k++] = dig[w % b]; w /= b; } while (w);
            while (n < prec - k) tmp[n++] = '0';
            while (k) tmp[n++] = r[--k];
            if (prec == 0 && u == 0) n = 0;
            if (alt && u && b == 16) pre = c == 'X' ? "0X" : "0x";
            if (alt && b == 8 && (n == 0 || tmp[0] != '0')) pre = "0";
            if (prec >= 0) cero = 0;
        } else if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' || c == 'A') {
            long double v = lng == 3 ? va_arg(ap, long double) : va_arg(ap, double);
            if (v < 0 || (v == 0 && 1 / v < 0)) { signo = '-'; v = -v; } else signo = mas ? '+' : esp ? ' ' : 0;
            char *fb; n = __fmt_float(&fb, v, c, prec, alt); s = fb;
            if (!(v - v == 0)) cero = 0;
        } else if (c == 'c') { tmp[0] = (char)va_arg(ap, int); n = 1; }
        else if (c == 's') { s = va_arg(ap, const char *); if (!s) s = "(null)"; n = (int)(prec >= 0 ? strnlen(s, (size_t)prec) : strlen(s)); cero = 0; }
        else if (c == 'n') { int *p = va_arg(ap, int *); if (p) *p = (int)o->n; continue; }
        else { tmp[0] = c; n = 1; }
    poner:;
        int total = n + (signo ? 1 : 0) + (int)strlen(pre);
        if (!menos && !cero) __rep(o, ' ', ancho - total);
        if (signo) __pon(o, &signo, 1);
        __pon(o, pre, strlen(pre));
        if (!menos && cero) __rep(o, '0', ancho - total);
        __pon(o, s, (size_t)n);
        if (menos) __rep(o, ' ', ancho - total);
    }
    return (int)o->n;
}
int vsnprintf(char *s, size_t n, const char *f, va_list ap)
{
    struct __sal o; o.s = s; o.n = 0; o.cap = n; o.f = 0;
    int r = __vfmt(&o, f, ap);
    if (n) s[o.n < n ? o.n : n - 1] = 0;
    return r;
}
int vsprintf(char *s, const char *f, va_list ap) { return vsnprintf(s, (size_t)INT_MAX, f, ap); }
int vfprintf(FILE *fp, const char *f, va_list ap) { struct __sal o; o.s = 0; o.n = 0; o.cap = 0; o.f = fp; return __vfmt(&o, f, ap); }
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int snprintf(char *s, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(s, n, f, ap); va_end(ap); return r; }
int sprintf(char *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsprintf(s, f, ap); va_end(ap); return r; }
int fprintf(FILE *fp, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(fp, f, ap); va_end(ap); return r; }
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(stdout, f, ap); va_end(ap); return r; }

/* ---------------------------------------------------------------- scanf (lo habitual: %d %i %u %x %o %ld %lld %f %lf %s %c %[ ] %n %%) */
struct __ent { const char *s; FILE *f; int c, usado; };
static int __mira(struct __ent *e) { if (e->c == -2) e->c = e->s ? (*e->s ? (unsigned char)*e->s : EOF) : fgetc(e->f); return e->c; }
static void __toma(struct __ent *e) { if (e->s && *e->s) e->s++; e->c = -2; e->usado++; }
static void __devuelve(struct __ent *e) { if (e->f && e->c >= 0) ungetc(e->c, e->f); e->c = -2; }
static int __vscan(struct __ent *e, const char *f, va_list ap)
{
    int asig = 0;
    for (; *f; ++f) {
        if (isspace((unsigned char)*f)) { while (__mira(e) != EOF && isspace(__mira(e))) __toma(e); continue; }
        if (*f != '%' || f[1] == '%') { if (*f == '%') f++; if (__mira(e) != (unsigned char)*f) break; __toma(e); continue; }
        ++f;
        int salta = 0, ancho = 0, lng = 0;
        if (*f == '*') { salta = 1; ++f; }
        while (isdigit((unsigned char)*f)) ancho = ancho * 10 + (*f++ - '0');
        for (;; ++f) { if (*f == 'l') lng++; else if (*f == 'h') lng--; else if (*f == 'z' || *f == 'j') lng = 2; else if (*f == 'L') lng = 3; else break; }
        char c = *f;
        if (c == 'n') { if (!salta) *va_arg(ap, int *) = e->usado; continue; }
        if (c != 'c' && c != '[') while (__mira(e) != EOF && isspace(__mira(e))) __toma(e);
        if (__mira(e) == EOF) return asig ? asig : EOF;
        if (!ancho) ancho = c == 'c' ? 1 : 400;
        char b[401]; int k = 0;
        if (c == 'c') {
            char *d = salta ? 0 : va_arg(ap, char *);
            while (k < ancho && __mira(e) != EOF) { if (d) d[k] = (char)__mira(e); k++; __toma(e); }
            if (!salta) asig++;
            continue;
        }
        if (c == 's' || c == '[') {
            int neg = 0; char set[256]; memset(set, 0, sizeof set);
            if (c == '[') {
                ++f; if (*f == '^') { neg = 1; ++f; }
                if (*f == ']') { set[']'] = 1; ++f; }
                while (*f && *f != ']') { if (f[1] == '-' && f[2] && f[2] != ']') { for (int x = (unsigned char)f[0]; x <= (unsigned char)f[2]; ++x) set[x] = 1; f += 3; } else set[(unsigned char)*f++] = 1; }
            }
            char *d = salta ? 0 : va_arg(ap, char *);
            while (k < ancho && __mira(e) != EOF) {
                int x = __mira(e);
                if (c == 's' ? isspace(x) : (neg ? set[x] : !set[x])) break;
                if (d) d[k] = (char)x;
                k++; __toma(e);
            }
            if (!k) break;
            if (d) { d[k] = 0; asig++; }
            continue;
        }
        while (k < ancho && __mira(e) != EOF) {                           /* numeros: se recoge lo que pueda serlo */
            int x = __mira(e);
            int ok = isxdigit(x) || x == '-' || x == '+' || x == '.' || x == 'x' || x == 'X' || ((c == 'f' || c == 'e' || c == 'g') && (x == 'e' || x == 'E'));
            if (c == 'd' || c == 'u') ok = isdigit(x) || ((x == '-' || x == '+') && k == 0);
            if (c == 'f' || c == 'e' || c == 'g') ok = isdigit(x) || x == '.' || ((x == 'e' || x == 'E') && k) || ((x == '-' || x == '+') && (k == 0 || b[k - 1] == 'e' || b[k - 1] == 'E'));
            if (!ok) break;
            b[k++] = (char)x; __toma(e);
        }
        b[k] = 0;
        if (!k) break;
        char *fin;
        if (c == 'f' || c == 'e' || c == 'g' || c == 'E' || c == 'G') {
            long double v = strtold(b, &fin);
            if (fin == b) break;
            if (!salta) { if (lng == 1) *va_arg(ap, double *) = (double)v; else if (lng == 3) *va_arg(ap, long double *) = v; else *va_arg(ap, float *) = (float)v; asig++; }
        } else {
            int base = c == 'x' || c == 'X' ? 16 : c == 'o' ? 8 : c == 'i' ? 0 : 10;
            long long v = (c == 'u' || c == 'x' || c == 'X' || c == 'o') ? (long long)strtoull(b, &fin, base) : strtoll(b, &fin, base);
            if (fin == b) break;
            if (!salta) {
                if (lng >= 2) *va_arg(ap, long long *) = v; else if (lng == 1) *va_arg(ap, long *) = (long)v;
                else if (lng == -1) *va_arg(ap, short *) = (short)v; else if (lng <= -2) *va_arg(ap, char *) = (char)v; else *va_arg(ap, int *) = (int)v;
                asig++;
            }
        }
    }
    __devuelve(e);
    return asig;
}
int vsscanf(const char *s, const char *f, va_list ap) { struct __ent e; e.s = s; e.f = 0; e.c = -2; e.usado = 0; return __vscan(&e, f, ap); }
int vfscanf(FILE *in, const char *f, va_list ap) { struct __ent e; e.s = 0; e.f = in; e.c = -2; e.usado = 0; return __vscan(&e, f, ap); }
int sscanf(const char *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r; }
int fscanf(FILE *in, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfscanf(in, f, ap); va_end(ap); return r; }
int scanf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vfscanf(stdin, f, ap); va_end(ap); return r; }

/* ================================================================== tiempo */
static long __dias(int y, int m, int d)                               /* dias desde 1970-01-01 */
{
    y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
time_t time(time_t *t)
{
    long c[6] = { 1970, 1, 1, 0, 0, 0 };
    __soma_sys(SOMA_SYS_CON, 1, (long)c, 0);
    time_t r = __dias((int)c[0], (int)c[1], (int)c[2]) * 86400 + c[3] * 3600 + c[4] * 60 + c[5];
    if (t) *t = r;
    return r;
}
static struct tm __tm;
struct tm *gmtime(const time_t *tp)
{
    long t = (long)*tp, d = t / 86400, s = t % 86400;
    if (s < 0) { s += 86400; d--; }
    __tm.tm_hour = (int)(s / 3600); __tm.tm_min = (int)(s / 60 % 60); __tm.tm_sec = (int)(s % 60);
    __tm.tm_wday = (int)((d + 4) % 7); if (__tm.tm_wday < 0) __tm.tm_wday += 7;
    long z = d + 719468, era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9;
    y += mm <= 2;
    __tm.tm_year = (int)(y - 1900); __tm.tm_mon = (int)mm - 1; __tm.tm_mday = (int)dd;
    __tm.tm_yday = (int)(d - __dias((int)y, 1, 1)); __tm.tm_isdst = 0;
    return &__tm;
}
struct tm *localtime(const time_t *t) { return gmtime(t); }          /* el reloj de SOMA ya es la hora local */
time_t mktime(struct tm *t) { return __dias(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec; }
double difftime(time_t a, time_t b) { return (double)(a - b); }
clock_t clock(void) { return (clock_t)__soma_sys(SOMA_SYS_UPTIME, 0, 0, 0); }
int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    unsigned long ms = (unsigned long)__soma_sys(SOMA_SYS_UPTIME, 0, 0, 0);
    tv->tv_sec = time(0); tv->tv_usec = (suseconds_t)(ms % 1000) * 1000;
    return 0;
}
size_t strftime(char *s, size_t n, const char *f, const struct tm *t)
{
    static const char *dia[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mes[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    size_t k = 0; char b[32];
    for (; *f && k + 1 < n; ++f) {
        if (*f != '%') { s[k++] = *f; continue; }
        ++f;
        switch (*f) {
        case 'Y': snprintf(b, sizeof b, "%d", t->tm_year + 1900); break;
        case 'm': snprintf(b, sizeof b, "%02d", t->tm_mon + 1); break;
        case 'd': snprintf(b, sizeof b, "%02d", t->tm_mday); break;
        case 'H': snprintf(b, sizeof b, "%02d", t->tm_hour); break;
        case 'M': snprintf(b, sizeof b, "%02d", t->tm_min); break;
        case 'S': snprintf(b, sizeof b, "%02d", t->tm_sec); break;
        case 'a': snprintf(b, sizeof b, "%s", dia[t->tm_wday % 7]); break;
        case 'b': snprintf(b, sizeof b, "%s", mes[t->tm_mon % 12]); break;
        case '%': strcpy(b, "%"); break;
        default: b[0] = 0;
        }
        for (char *p = b; *p && k + 1 < n; ++p) s[k++] = *p;
    }
    if (n) s[k] = 0;
    return k;
}
unsigned sleep(unsigned s) { __soma_sys(SOMA_SYS_SLEEP, (long)s * 1000, 0, 0); return 0; }
int usleep(unsigned us) { __soma_sys(SOMA_SYS_SLEEP, (long)(us / 1000), 0, 0); return 0; }

/* ================================================================== saltos, fin y arranque */
__asm__(".text\n.globl setjmp\nsetjmp:\n"
        "  movq %rbx, 0(%rdi)\n  movq %rbp, 8(%rdi)\n  movq %r12, 16(%rdi)\n  movq %r13, 24(%rdi)\n"
        "  movq %r14, 32(%rdi)\n  movq %r15, 40(%rdi)\n  leaq 8(%rsp), %rdx\n  movq %rdx, 48(%rdi)\n"
        "  movq (%rsp), %rdx\n  movq %rdx, 56(%rdi)\n  xorl %eax, %eax\n  ret\n"
        ".globl longjmp\nlongjmp:\n"
        "  movl %esi, %eax\n  testl %eax, %eax\n  jnz 1f\n  incl %eax\n1:\n"
        "  movq 0(%rdi), %rbx\n  movq 8(%rdi), %rbp\n  movq 16(%rdi), %r12\n  movq 24(%rdi), %r13\n"
        "  movq 32(%rdi), %r14\n  movq 40(%rdi), %r15\n  movq 48(%rdi), %rsp\n  jmp *56(%rdi)\n");

static void (*__alsalir[32])(void); static int __nsalir;
int atexit(void (*f)(void)) { if (__nsalir >= 32) return -1; __alsalir[__nsalir++] = f; return 0; }
void _exit(int code) { __soma_sys(SOMA_SYS_EXIT, code & 255, 0, 0); for (;;) { } }   /* 0..255, como en POSIX */
void exit(int code)
{
    while (__nsalir) __alsalir[--__nsalir]();
    fflush(0);
    for (int i = 3; i < NFD; ++i) if (__fds[i].used) { if (__abiertos[i]) fclose(__abiertos[i]); else close(i); }
    _exit(code);
}
void abort(void) { fflush(0); fputs("abort()\n", stderr); _exit(134); }
void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func)
{
    fprintf(stderr, "%s:%u: %s: no se cumple '%s'\n", file, line, func ? func : "?", expr);
    abort();
}

/* argumentos: los que da el nucleo (SYS_GETARGS), separados por espacios; "entre comillas" para uno con espacios */
#define MAXARGS 64
static char __argbuf[1024]; static char *__argv[MAXARGS + 2];
static int __partir(char *s, const char *nombre)
{
    int n = 0;
    __argv[n++] = (char *)nombre;
    while (*s && n < MAXARGS) {
        while (*s == ' ') s++;
        if (!*s) break;
        char q = (*s == '"' || *s == '\'') ? *s++ : 0;
        __argv[n++] = s;
        while (*s && (q ? *s != q : *s != ' ')) s++;
        if (*s) *s++ = 0;
    }
    __argv[n] = 0;
    return n;
}
extern int main(int argc, char **argv);
void __soma_start(void)
{
    long n = __soma_sys(SOMA_SYS_GETARGS, (long)__argbuf, sizeof __argbuf - 1, 0);
    __argbuf[n > 0 ? n : 0] = 0;
    int argc = __partir(__argbuf, "programa");
    exit(main(argc, __argv));
}
#ifndef SOMA_SIN_ARRANQUE
static unsigned char __pila[SOMA_STACK_SIZE] __attribute__((aligned(16)));
__asm__(".text\n.globl _start\n_start:\n"
        "  leaq __pila+" "1048576" "(%rip), %rsp\n"
        "  andq $-16, %rsp\n"
        "  xorl %ebp, %ebp\n"
        "  call __soma_start\n"
        "  hlt\n");
void *__soma_pila_ref(void) { return __pila; }
#endif

/* ================================================================== soma.h: la ventana (como soma.win de Python) */
#define WG_CLEAR 1
#define WG_PIXEL 2
#define WG_RECT 3
#define WG_LINE 4
#define WG_CIRC 5
#define WG_TEXT 6
static int __gb[4096]; static unsigned __gn;
static void __gb_flush(void) { if (__gn) __soma_sys(SOMA_SYS_CON, 4, (long)__gb, (long)(__gn * 4)); __gn = 0; }
static void __gb_pon(const int *w, unsigned n) { if (__gn + n > 4096) __gb_flush(); if (n > 4096) return; memcpy(__gb + __gn, w, n * 4); __gn += n; }
int soma_win_create(const char *title, int w, int h)
{
    unsigned long q[4]; q[0] = (unsigned long)title; q[1] = strlen(title); q[2] = (unsigned long)(w > 0 ? w : 400); q[3] = (unsigned long)(h > 0 ? h : 300);
    return __soma_sys(SOMA_SYS_CON, 2, (long)q, 0) == 1;
}
void soma_win_close(void) { __gb_flush(); __soma_sys(SOMA_SYS_CON, 3, 0, 0); }
static void __win_estado(long st[8], int consumir) { for (int i = 0; i < 8; ++i) st[i] = 0; __soma_sys(SOMA_SYS_CON, 6, (long)st, consumir); }
int soma_win_active(void) { long st[8]; __win_estado(st, 0); return st[0] != 0; }
void soma_win_size(int *w, int *h) { long st[8]; __win_estado(st, 0); if (w) *w = (int)st[1]; if (h) *h = (int)st[2]; }
struct soma_mouse soma_win_mouse(void)
{
    long st[8]; __win_estado(st, 1);
    struct soma_mouse m; m.x = (int)st[3]; m.y = (int)st[4]; m.button = (int)st[5]; m.inside = (int)st[6]; m.clicked = (int)st[7];
    return m;
}
int soma_win_key(void) { return (int)__soma_sys(SOMA_SYS_CON, 7, 0, 0); }
void soma_win_clear(unsigned c) { int w[2] = { WG_CLEAR, (int)c }; __gb_pon(w, 2); }
void soma_win_pixel(int x, int y, unsigned c) { int w[4] = { WG_PIXEL, x, y, (int)c }; __gb_pon(w, 4); }
void soma_win_rect(int x, int y, int ww, int hh, unsigned c) { int w[6] = { WG_RECT, x, y, ww, hh, (int)c }; __gb_pon(w, 6); }
void soma_win_line(int x0, int y0, int x1, int y1, unsigned c) { int w[6] = { WG_LINE, x0, y0, x1, y1, (int)c }; __gb_pon(w, 6); }
void soma_win_circle(int x, int y, int r, unsigned c) { int w[5] = { WG_CIRC, x, y, r, (int)c }; __gb_pon(w, 5); }
void soma_win_text(int x, int y, const char *s, unsigned c)
{
    int n = (int)strlen(s); if (n > 1024) n = 1024;
    int w[5 + 256]; w[0] = WG_TEXT; w[1] = x; w[2] = y; w[3] = (int)c; w[4] = n;
    memset(w + 5, 0, (size_t)((n + 3) / 4) * 4); memcpy(w + 5, s, (size_t)n);
    __gb_pon(w, 5 + (unsigned)(n + 3) / 4);
}
void soma_win_present(void) { __gb_flush(); __soma_sys(SOMA_SYS_CON, 5, 0, 0); }
unsigned long soma_ms(void) { return (unsigned long)__soma_sys(SOMA_SYS_UPTIME, 0, 0, 0); }
void soma_sleep(unsigned ms) { __soma_sys(SOMA_SYS_SLEEP, ms, 0, 0); }
void soma_clock(int out[6]) { long c[6] = { 0 }; __soma_sys(SOMA_SYS_CON, 1, (long)c, 0); for (int i = 0; i < 6; ++i) out[i] = (int)c[i]; }
