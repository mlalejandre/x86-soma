/* SOMA_241_VOZ: biblioteca C minima para el programa de reconocimiento de voz (whisper.cpp + ggml) en ring 3.
 *
 * El programa se compila con las cabeceras de C y C++ del sistema anfitrion (glibc/libstdc++) pero SIN enlazar su libc:
 * aqui esta lo poco que whisper.cpp y ggml necesitan de verdad, sobre las llamadas al sistema de SOMA:
 *   - memoria: un monton propio dentro de la region grande bajo demanda que el nucleo da al proceso (ver voz.c del nucleo);
 *   - archivos: solo lectura del almacen BIGFS (SYS_OPEN/READ/SEEK/FSIZE), para el modelo y el audio;
 *   - salida: SYS_WRITE (la consola o el bufer al que el nucleo redirige la salida);
 *   - reloj: SYS_UPTIME_MS;
 *   - matematicas: las funciones de doble precision de third_party/micropython/libm_dbl (musl), y las de float encima;
 *   - hilos: aun no hay hilos de usuario en SOMA; pthread_create falla y ggml calcula con un solo hilo.
 * No se incluye ninguna cabecera de la libc: los tipos que hacen falta se declaran aqui con la misma forma que en glibc. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ llamadas al sistema */
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_YIELD 2
#define SYS_OPEN 5
#define SYS_READ 6
#define SYS_SEEK 7
#define SYS_CLOSE 8
#define SYS_FSIZE 9
#define SYS_UPTIME_MS 19
static inline long sc3_raw(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}
#ifndef VOZ_HOST
static inline long sc3(long nr, long a, long b, long c) { return sc3_raw(nr, a, b, c); }
long voz_getargs(char *buf, long n) { return sc3_raw(10, (long)buf, n, 0); }   /* SYS_GETARGS */
int voz_map_heap(void *va, unsigned long size) { (void)va; (void)size; return 0; }   /* la region ya la puso el nucleo */
#else
/* VOZ_HOST: el mismo programa sobre Linux (prueba en el anfitrion, tests/voz_host.sh): las llamadas de SOMA se traducen a las de Linux.
 * Las rutas del almacen ("/modelos/x.bin") se buscan bajo el directorio de $PWD/voz_raiz. */
static long lx(long nr, long a, long b, long c) { return sc3_raw(nr, a, b, c); }
static long lx6(long nr, long a, long b, long c, long d, long e, long f)
{
    long r; register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
    __asm__ volatile ("syscall" : "=a"(r) : "0"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
static long sc3(long nr, long a, long b, long c)
{
    switch (nr) {
    case SYS_EXIT: return lx(231, a, 0, 0);
    case SYS_WRITE: return lx(1, 1, a, b);
    case SYS_YIELD: return 0;
    case SYS_OPEN: { char p[512]; const char *pre = "voz_raiz"; long i = 0; while (pre[i]) { p[i] = pre[i]; i++; }
        for (long k = 0; k < b && i < 510; ++k) p[i++] = ((const char *)a)[k];
        p[i] = 0;
        return lx(2, (long)p, 0, 0); }
    case SYS_READ: return lx(0, a, b, c);
    case SYS_SEEK: return lx(8, a, b, c);
    case SYS_CLOSE: return lx(3, a, 0, 0);
    case SYS_FSIZE: { long cur = lx(8, a, 0, 1), end = lx(8, a, 0, 2); lx(8, a, cur, 0); return end; }
    case SYS_UPTIME_MS: { long ts[2]; lx(228, 1, (long)ts, 0); return ts[0] * 1000 + ts[1] / 1000000; }
    }
    return -38;
}
long voz_getargs(char *buf, long n)
{   /* /proc/self/cmdline sin argv[0], separado por espacios */
    char tmp[1024]; long fd = lx(2, (long)"/proc/self/cmdline", 0, 0); if (fd < 0) return -1;
    long r = lx(0, fd, (long)tmp, sizeof tmp - 1); lx(3, fd, 0, 0); if (r <= 0) return -1;
    long i = 0; while (i < r && tmp[i]) i++; i++;
    long o = 0;
    for (; i < r && o + 1 < n; ++i) buf[o++] = tmp[i] ? tmp[i] : ' ';
    while (o && buf[o - 1] == ' ') o--;
    buf[o] = 0; return o;
}
int voz_map_heap(void *va, unsigned long size) { long r = lx6(9, (long)va, (long)size, 3, 0x22 | 0x100000, -1, 0); return r == (long)va ? 0 : -1; }   /* MAP_PRIVATE|ANON|FIXED_NOREPLACE */
#endif
/* SOMA_247_HABLA: salida de audio (PCM 16 bits estereo). En SOMA, las llamadas 11-13 del nucleo; en el anfitrion (VOZ_HOST) se
 * escribe voz_raiz/habla.wav para escucharlo y comprobarlo. */
#ifndef VOZ_HOST
long voz_audio_open(long rate) { return sc3_raw(11, rate, 0, 0); }
long voz_audio_write(const void *buf, long len) { return sc3_raw(12, (long)buf, len, 0); }
long voz_audio_close(long wait) { return sc3_raw(13, wait, 0, 0); }
#else
static long a_fd = -1, a_bytes, a_rate;
static void a_hdr(void)
{
    unsigned char h[44]; long d = a_bytes; unsigned rate = (unsigned)a_rate, br = rate * 4;
    const char *t = "RIFF____WAVEfmt ";
    for (int i = 0; i < 16; i++) h[i] = (unsigned char)t[i];
    unsigned v = (unsigned)(36 + d); h[4] = v; h[5] = v >> 8; h[6] = v >> 16; h[7] = v >> 24;
    h[16] = 16; h[17] = h[18] = h[19] = 0; h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    h[24] = rate; h[25] = rate >> 8; h[26] = rate >> 16; h[27] = rate >> 24; h[28] = br; h[29] = br >> 8; h[30] = br >> 16; h[31] = br >> 24;
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0; h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    h[40] = d; h[41] = d >> 8; h[42] = d >> 16; h[43] = d >> 24;
    lx(8, a_fd, 0, 0); lx(1, a_fd, (long)h, 44); lx(8, a_fd, 0, 2);
}
long voz_audio_open(long rate)
{
    if (rate != 22050 && rate != 44100 && rate != 48000 && rate != 16000) return -22;
    a_fd = lx(2, (long)"voz_raiz/habla.wav", 0x241, 0644); if (a_fd < 0) return -19;     /* O_WRONLY|O_CREAT|O_TRUNC */
    a_rate = rate; a_bytes = 0; a_hdr(); return 0;
}
long voz_audio_write(const void *buf, long len) { if (a_fd < 0) return -9; long r = lx(1, a_fd, (long)buf, len); if (r > 0) a_bytes += r; return r; }
long voz_audio_close(long wait) { (void)wait; if (a_fd >= 0) { a_hdr(); lx(3, a_fd, 0, 0); a_fd = -1; } return 0; }
#endif

void voz_exit(int code) __attribute__((noreturn));
void voz_exit(int code) { sc3(SYS_EXIT, code, 0, 0); for (;;) { } }
uint64_t voz_uptime_ms(void) { return (uint64_t)sc3(SYS_UPTIME_MS, 0, 0, 0); }

/* ------------------------------------------------------------------ cadenas y memoria basica */
void *memcpy(void *d, const void *s, size_t n)
{
    void *r = d;
    __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d; const unsigned char *b = s;
    if (a == b || n == 0) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    while (n--) a[n] = b[n];
    return d;
}
void *memset(void *d, int c, size_t n)
{
    void *r = d;
    __asm__ volatile ("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) { if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i]; }
    return 0;
}
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strrchr(const char *s, int c) { const char *r = 0; for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; ++i) d[i] = s[i]; for (; i < n; ++i) d[i] = 0; return d; }
char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
    return 0;
}
void *memchr(const void *s, int c, size_t n) { const unsigned char *p = s; for (size_t i = 0; i < n; ++i) if (p[i] == (unsigned char)c) return (void *)(p + i); return 0; }
/* Partes de libstdc++.a que se enlazan (std::string, arboles de std::map) estan compiladas con protector de pila y excepciones:
 * leen el canario en %fs:0x28 (el nucleo pone la base de FS al principio de la region del monton) y tienen rutas de limpieza
 * que solo se usan si se lanza una excepcion, cosa que aqui no ocurre nunca (las __throw_* terminan el programa). */
void __stack_chk_fail(void) { static const char m[] = "voz: pila corrompida\n"; sc3(SYS_WRITE, (long)m, sizeof m - 1, 0); voz_exit(134); }
void abort(void);
void _Unwind_Resume(void *e) { (void)e; abort(); }
char *strerror(int e) { (void)e; return (char *)"error"; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int isxdigit(int c) { return isdigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

long strtol(const char *s, char **end, int base)
{
    while (isspace((unsigned char)*s)) s++;
    int neg = 0; if (*s == '-' || *s == '+') neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    long v = 0;
    for (;; s++) {
        int d = isdigit((unsigned char)*s) ? *s - '0' : isalpha((unsigned char)*s) ? (*s | 32) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}
long __isoc23_strtol(const char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }
unsigned long __isoc23_strtoul(const char *s, char **end, int base) { return strtoul(s, end, base); }
long long strtoll(const char *s, char **end, int base) { return strtol(s, end, base); }
long long __isoc23_strtoll(const char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long long strtoull(const char *s, char **end, int base) { return (unsigned long long)strtol(s, end, base); }
unsigned long long __isoc23_strtoull(const char *s, char **end, int base) { return strtoull(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
double strtod(const char *s, char **end)
{
    while (isspace((unsigned char)*s)) s++;
    int neg = 0; if (*s == '-' || *s == '+') neg = *s++ == '-';
    double v = 0, f = 1;
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    if (*s == '.') { s++; while (isdigit((unsigned char)*s)) { f /= 10; v += (*s++ - '0') * f; } }
    if ((*s | 32) == 'e') {
        char *e2; long ex = strtol(s + 1, &e2, 10);
        if (e2 != s + 1) { s = e2; while (ex > 0) { v *= 10; ex--; } while (ex < 0) { v /= 10; ex++; } }
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}
float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, 0); }

/* ------------------------------------------------------------------ monton (region bajo demanda del nucleo)
 * Bloques de 64 bytes de cabecera + carga util multiplo de 64 (la carga queda alineada a 64: lo que pide ggml).
 * Libres: lista doblemente enlazada; al liberar se funden con los vecinos (cabecera con el tamano del anterior).
 * El final del monton baja si se libera el ultimo bloque: las paginas que nunca se tocan no gastan memoria. */
#define HDR 64u
#define F_USED 1u
#define F_ALIGNED 2u                                   /* cabecera falsa delante de un bloque con alineacion > 64 */
struct blk { size_t size; size_t prev_size; size_t flags; struct blk *next, *prev; void *real; size_t pad[2]; };
_Static_assert(sizeof(struct blk) == HDR, "cabecera de 64 bytes");
static uint8_t *h_base, *h_brk, *h_end;
/* SOMA_244_HILOS: el monton se comparte con los hilos de los nucleos secundarios: cerrojo de espera activa. Y con hilos, cada pagina
 * nueva se toca al reservarla (en el hilo principal): un hilo secundario no puede provocar fallos de pagina que el nucleo sirva. */
int voz_pretouch;
static volatile int h_lock;
static void hl(void) { while (__atomic_exchange_n(&h_lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile ("pause"); }
static void hu(void) { __atomic_store_n(&h_lock, 0, __ATOMIC_RELEASE); }
static struct blk *h_free;
static size_t h_peak, h_inuse;
void voz_heap_init(void *base, size_t size)
{
    h_base = h_brk = (uint8_t *)(((uintptr_t)base + 63) & ~(uintptr_t)63);
    h_end = (uint8_t *)base + size;
    h_free = 0;
}
size_t voz_heap_peak(void) { return h_peak; }
static void fl_remove(struct blk *b) { if (b->prev) b->prev->next = b->next; else h_free = b->next; if (b->next) b->next->prev = b->prev; }
static void fl_push(struct blk *b) { b->prev = 0; b->next = h_free; if (h_free) h_free->prev = b; h_free = b; }
static struct blk *nextb(struct blk *b) { return (struct blk *)((uint8_t *)b + b->size); }
static void *malloc_nolock(size_t n);
void *malloc(size_t n) { hl(); void *p = malloc_nolock(n); hu(); return p; }
static void *malloc_nolock(size_t n)
{
    if (!h_base) return 0;
    size_t need = ((n + 63) & ~(size_t)63) + HDR;
    if (need < 2 * HDR) need = 2 * HDR;
    struct blk *best = 0;
    for (struct blk *b = h_free; b; b = b->next) if (b->size >= need && (!best || b->size < best->size)) { best = b; if (b->size == need) break; }
    if (best) {
        fl_remove(best);
        if (best->size - need >= 2 * HDR) {                 /* parte el sobrante */
            struct blk *r = (struct blk *)((uint8_t *)best + need);
            r->size = best->size - need; r->prev_size = need; r->flags = 0;
            struct blk *nx = nextb(r); if ((uint8_t *)nx < h_brk) nx->prev_size = r->size;
            best->size = need;
            fl_push(r);
        }
        best->flags = F_USED;
        h_inuse += best->size;
        if (h_inuse > h_peak) h_peak = h_inuse;
        return (uint8_t *)best + HDR;
    }
    if ((size_t)(h_end - h_brk) < need) return 0;
    if (voz_pretouch) {                                   /* paginas nuevas (y la cabecera del siguiente bloque) */
        uintptr_t a = ((uintptr_t)h_brk + 4095) & ~(uintptr_t)4095, e = (uintptr_t)h_brk + need + HDR;
        for (; a < e && a < (uintptr_t)h_end; a += 4096) *(volatile uint8_t *)a = 0;
    }
    struct blk *b = (struct blk *)h_brk;
    b->prev_size = h_brk == h_base ? 0 : ((struct blk *)h_brk)->prev_size;
    b->size = need; b->flags = F_USED;
    h_brk += need;
    if (h_brk < h_end - HDR) ((struct blk *)h_brk)->prev_size = need;   /* el siguiente bloque que se cree sabra donde empieza este */
    h_inuse += need;
    if (h_inuse > h_peak) h_peak = h_inuse;
    return (uint8_t *)b + HDR;
}
static void free_nolock(void *p);
void free(void *p) { if (!p) return; hl(); free_nolock(p); hu(); }
static void free_nolock(void *p)
{
    if (!p) return;
    struct blk *b = (struct blk *)((uint8_t *)p - HDR);
    if (b->flags & F_ALIGNED) { free_nolock(b->real); return; }
    if (!(b->flags & F_USED)) return;                     /* doble liberacion: se ignora */
    h_inuse -= b->size;
    b->flags = 0;
    struct blk *nx = nextb(b);
    if ((uint8_t *)nx < h_brk && !(nx->flags & F_USED)) { fl_remove(nx); b->size += nx->size; }
    if ((uint8_t *)b > h_base) {
        struct blk *pv = (struct blk *)((uint8_t *)b - b->prev_size);
        if (b->prev_size && !(pv->flags & F_USED)) { fl_remove(pv); pv->size += b->size; b = pv; }
    }
    nx = nextb(b);
    if ((uint8_t *)nx >= h_brk) {                         /* era el ultimo: el monton encoge */
        h_brk = (uint8_t *)b;
        if (h_brk < h_end - HDR) ((struct blk *)h_brk)->prev_size = b->prev_size;
        return;
    }
    nx->prev_size = b->size;
    fl_push(b);
}
void *calloc(size_t a, size_t b)
{
    size_t n = a * b;
    if (b && n / b != a) return 0;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}
void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    struct blk *b = (struct blk *)((uint8_t *)p - HDR);
    size_t have = (b->flags & F_ALIGNED) ? 0 : b->size - HDR;
    if (have >= n) return p;
    void *q = malloc(n);
    if (!q) return 0;
    if (b->flags & F_ALIGNED) { struct blk *r = (struct blk *)((uint8_t *)b->real - HDR); have = r->size - HDR - (size_t)((uint8_t *)p - (uint8_t *)b->real); }
    memcpy(q, p, have < n ? have : n);
    free(p);
    return q;
}
int posix_memalign(void **out, size_t align, size_t n)
{
    if (align <= 64) { *out = malloc(n); return *out ? 0 : 12; }
    uint8_t *raw = malloc(n + align + HDR);
    if (!raw) return 12;
    uint8_t *p = (uint8_t *)(((uintptr_t)raw + HDR + align - 1) & ~(uintptr_t)(align - 1));
    struct blk *f = (struct blk *)(p - HDR);
    f->flags = F_ALIGNED; f->real = raw; f->size = 0;
    *out = p;
    return 0;
}
void *aligned_alloc(size_t align, size_t n) { void *p; return posix_memalign(&p, align, n) ? 0 : p; }
void *memalign(size_t align, size_t n) { return aligned_alloc(align, n); }

/* ------------------------------------------------------------------ printf */
struct ob { char *p; size_t cap, len; };
static void ob_c(struct ob *o, char c) { if (o->len + 1 < o->cap) o->p[o->len] = c; o->len++; }
static void ob_s(struct ob *o, const char *s, size_t n) { for (size_t i = 0; i < n; ++i) ob_c(o, s[i]); }
static void ob_pad(struct ob *o, int n, char c) { while (n-- > 0) ob_c(o, c); }
static int fmt_u(char *b, unsigned long long v, int base, int up)
{
    const char *dg = up ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[32]; int n = 0;
    do { t[n++] = dg[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    for (int i = 0; i < n; ++i) b[i] = t[n - 1 - i];
    return n;
}
/* %f / %e con precision p (redondeo correcto para lo que se imprime aqui: tiempos, probabilidades, tamanos) */
static int fmt_f(char *b, double v, int prec, int expo)
{
    int n = 0;
    if (v != v) { b[0] = 'n'; b[1] = 'a'; b[2] = 'n'; return 3; }
    if (v < 0) { b[n++] = '-'; v = -v; }
    if (v > 1e300) { b[n++] = 'i'; b[n++] = 'n'; b[n++] = 'f'; return n; }
    if (prec > 17) prec = 17;
    int ex = 0;
    if (expo && v != 0) { while (v >= 10) { v /= 10; ex++; } while (v < 1) { v *= 10; ex--; } }
    double r = 0.5; for (int i = 0; i < prec; ++i) r /= 10;
    v += r;
    if (expo && v >= 10) { v /= 10; ex++; }
    if (v >= 1e19) { b[n++] = '?'; return n; }
    unsigned long long ip = (unsigned long long)v;
    n += fmt_u(b + n, ip, 10, 0);
    double fr = v - (double)ip;
    if (prec) {
        b[n++] = '.';
        for (int i = 0; i < prec; ++i) { fr *= 10; int d = (int)fr; if (d > 9) d = 9; b[n++] = (char)('0' + d); fr -= d; }
    }
    if (expo) {
        b[n++] = 'e'; b[n++] = ex < 0 ? '-' : '+';
        if (ex < 0) ex = -ex;
        if (ex < 10) b[n++] = '0';
        n += fmt_u(b + n, (unsigned)ex, 10, 0);
    }
    return n;
}
int vsnprintf(char *out, size_t cap, const char *f, va_list ap)
{
    struct ob o = { out, cap, 0 };
    for (; *f; f++) {
        if (*f != '%') { ob_c(&o, *f); continue; }
        f++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;; f++) {
            if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1; else if (*f == '#') alt = 1; else break;
        }
        int width = 0, prec = -1;
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        int lng = 0;
        for (;; f++) { if (*f == 'l') lng++; else if (*f == 'z' || *f == 'j' || *f == 't') lng = 2; else if (*f == 'h') { } else if (*f == 'L') lng = 3; else break; }
        char buf[400]; int n = 0; const char *s = buf; char sign = 0;
        switch (*f) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
            if (v < 0) sign = '-'; else if (plus) sign = '+'; else if (space) sign = ' ';
            n = fmt_u(buf, u, 10, 0);
            break; }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long u = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            n = fmt_u(buf, u, *f == 'o' ? 8 : *f == 'u' ? 10 : 16, *f == 'X');
            (void)alt;
            break; }
        case 'p': { uintptr_t u = (uintptr_t)va_arg(ap, void *); buf[0] = '0'; buf[1] = 'x'; n = 2 + fmt_u(buf + 2, u, 16, 0); break; }
        case 'c': buf[0] = (char)va_arg(ap, int); n = 1; break;
        case 's': { s = va_arg(ap, const char *); if (!s) s = "(null)"; n = (int)strlen(s); if (prec >= 0 && n > prec) n = prec; break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = lng == 3 ? (double)va_arg(ap, long double) : va_arg(ap, double);
            if (prec < 0) prec = 6;
            int e = (*f | 32) == 'e';
            if ((*f | 32) == 'g') {                         /* %g: el formato mas corto razonable */
                double a = v < 0 ? -v : v;
                e = a != 0 && (a < 1e-4 || a >= 1e15);
                if (!e) { int ip = 0; double t = a; while (t >= 10) { t /= 10; ip++; } prec = prec > ip ? prec - ip - 1 : 0; if (prec > 6) prec = 6; }
            }
            if (v < 0) { sign = '-'; v = -v; } else if (plus) sign = '+'; else if (space) sign = ' ';
            n = fmt_f(buf, v, prec, e);
            if ((*f | 32) == 'g' && !e) { int dot = -1; for (int i = 0; i < n; ++i) if (buf[i] == '.') dot = i; if (dot >= 0) { while (n > dot && buf[n - 1] == '0') n--; if (n - 1 == dot) n--; } }
            break; }
        case '%': buf[0] = '%'; n = 1; break;
        case 'n': break;
        default: buf[0] = '%'; buf[1] = *f; n = 2; if (!*f) f--; break;
        }
        int total = n + (sign ? 1 : 0);
        if (prec > n && (*f == 'd' || *f == 'i' || *f == 'u' || *f == 'x' || *f == 'X')) { /* precision de enteros: ceros */
            int z = prec - n; total += z;
            if (!left) ob_pad(&o, width - total, ' ');
            if (sign) ob_c(&o, sign);
            ob_pad(&o, z, '0'); ob_s(&o, s, (size_t)n);
            if (left) ob_pad(&o, width - total, ' ');
            continue;
        }
        if (!left && !zero) ob_pad(&o, width - total, ' ');
        if (sign) ob_c(&o, sign);
        if (!left && zero) ob_pad(&o, width - total, '0');
        ob_s(&o, s, (size_t)n);
        if (left) ob_pad(&o, width - total, ' ');
    }
    if (cap) out[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}
int snprintf(char *out, size_t cap, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(out, cap, f, ap); va_end(ap); return r; }
int sprintf(char *out, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(out, 1u << 20, f, ap); va_end(ap); return r; }

/* ------------------------------------------------------------------ FILE: stdout/stderr y lectura de BIGFS */
struct _IO_FILE { int fd; int kind; int eof; int err; long pos; long size; };   /* kind: 1 salida estandar, 2 error, 3 archivo */
static struct _IO_FILE f_out = { -1, 1, 0, 0, 0, 0 }, f_err = { -1, 2, 0, 0, 0, 0 };
struct _IO_FILE *stdout = &f_out, *stderr = &f_err, *stdin = 0;
int voz_quiet_stderr = 1;                              /* los mensajes de depuracion de whisper/ggml no salen salvo con -v */
static char o_buf[4096]; static size_t o_len;
static void o_flush(void) { if (o_len) { sc3(SYS_WRITE, (long)o_buf, (long)o_len, 0); o_len = 0; } }
static void o_put(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (o_len == sizeof o_buf) o_flush();
        o_buf[o_len++] = s[i];
        if (s[i] == '\n') o_flush();
    }
}
void voz_flush(void) { o_flush(); }
static int f_write(struct _IO_FILE *f, const char *s, size_t n)
{
    if (!f || f->kind == 3) return -1;
    if (f->kind == 2 && voz_quiet_stderr) return (int)n;
    o_put(s, n);
    return (int)n;
}
int vfprintf(struct _IO_FILE *f, const char *fmt, va_list ap)
{
    char b[1024];
    va_list a2; va_copy(a2, ap);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    if (n >= (int)sizeof b) { char *big = malloc((size_t)n + 1); if (big) { vsnprintf(big, (size_t)n + 1, fmt, a2); f_write(f, big, (size_t)n); free(big); va_end(a2); return n; } n = sizeof b - 1; }
    va_end(a2);
    f_write(f, b, (size_t)n);
    return n;
}
int fprintf(struct _IO_FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }
int __printf_chk(int fl, const char *fmt, ...) { (void)fl; va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }
int __fprintf_chk(struct _IO_FILE *f, int fl, const char *fmt, ...) { (void)fl; va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }
size_t fwrite(const void *p, size_t sz, size_t n, struct _IO_FILE *f) { f_write(f, p, sz * n); return n; }
int fputs(const char *s, struct _IO_FILE *f) { return f_write(f, s, strlen(s)); }
int puts(const char *s) { f_write(stdout, s, strlen(s)); return f_write(stdout, "\n", 1); }
int fputc(int c, struct _IO_FILE *f) { char ch = (char)c; f_write(f, &ch, 1); return c; }
int putc(int c, struct _IO_FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fflush(struct _IO_FILE *f) { (void)f; o_flush(); return 0; }

struct _IO_FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode || mode[0] != 'r') return 0;        /* solo lectura */
    long fd = sc3(SYS_OPEN, (long)path, (long)strlen(path), 0);
    if (fd < 0) return 0;
    struct _IO_FILE *f = malloc(sizeof *f);
    if (!f) { sc3(SYS_CLOSE, fd, 0, 0); return 0; }
    f->fd = (int)fd; f->kind = 3; f->eof = 0; f->err = 0; f->pos = 0;
    f->size = sc3(SYS_FSIZE, fd, 0, 0);
    return f;
}
int fclose(struct _IO_FILE *f) { if (!f || f->kind != 3) return -1; sc3(SYS_CLOSE, f->fd, 0, 0); free(f); return 0; }
size_t fread(void *buf, size_t sz, size_t cnt, struct _IO_FILE *f)
{
    if (!f || f->kind != 3 || !sz) return 0;
    size_t want = sz * cnt, got = 0;
    uint8_t *p = buf;
    while (got < want) {
        size_t n = want - got; if (n > 65536) n = 65536;
        /* el nucleo solo escribe en paginas ya presentes: se tocan antes (la region del monton se reserva bajo demanda) */
        for (size_t o = 0; o < n; o += 4096) ((volatile uint8_t *)p)[got + o] = 0;
        ((volatile uint8_t *)p)[got + n - 1] = 0;
        long r = sc3(SYS_READ, f->fd, (long)(p + got), (long)n);
        if (r < 0) { f->err = 1; break; }
        if (r == 0) { f->eof = 1; break; }
        got += (size_t)r; f->pos += r;
    }
    if (f->pos >= f->size) f->eof = 1;
    return got / sz;
}
int feof(struct _IO_FILE *f) { return f ? f->eof : 1; }
int ferror(struct _IO_FILE *f) { return f ? f->err : 1; }
int fseek(struct _IO_FILE *f, long off, int wh)
{
    if (!f || f->kind != 3) return -1;
    long r = sc3(SYS_SEEK, f->fd, off, wh);
    if (r < 0) return -1;
    f->pos = r; f->eof = 0;
    return 0;
}
long ftell(struct _IO_FILE *f) { return f ? f->pos : -1; }
char *fgets(char *s, int n, struct _IO_FILE *f) { (void)s; (void)n; (void)f; return 0; }
long __getdelim(char **l, size_t *n, int d, struct _IO_FILE *f) { (void)l; (void)n; (void)d; (void)f; return -1; }
long getdelim(char **l, size_t *n, int d, struct _IO_FILE *f) { return __getdelim(l, n, d, f); }

/* ------------------------------------------------------------------ proceso, reloj y sistema */
void _Exit(int c) { o_flush(); voz_exit(c); }
void exit(int c) { o_flush(); voz_exit(c); }
void abort(void) { o_put("voz: abort\n", 11); o_flush(); voz_exit(134); }
char *getenv(const char *n) { (void)n; return 0; }
int getpid(void) { return 1; }
struct timespec_ { long tv_sec, tv_nsec; };
int clock_gettime(int clk, struct timespec_ *ts) { (void)clk; uint64_t ms = voz_uptime_ms(); ts->tv_sec = (long)(ms / 1000); ts->tv_nsec = (long)(ms % 1000) * 1000000L; return 0; }
long clock(void) { return (long)(voz_uptime_ms() * 1000); }
long sysconf(int n) { (void)n; return 1; }
int sched_yield(void) { __asm__ volatile ("pause"); return 0; }   /* sin llamada al sistema: lo usan los hilos secundarios */
int getcpu(unsigned *c, unsigned *nd) { if (c) *c = 0; if (nd) *nd = 0; return 0; }
int prctl(int o, ...) { (void)o; return -1; }
int stat(const char *p, void *st) { (void)p; (void)st; return -1; }
long read(int fd, void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }
int close(int fd) { (void)fd; return -1; }
int pipe(int *fds) { (void)fds; return -1; }
int fork(void) { return -1; }
int execlp(const char *f, const char *a, ...) { (void)f; (void)a; return -1; }
int waitpid(int p, int *s, int o) { (void)p; (void)s; (void)o; return -1; }
int backtrace(void **b, int n) { (void)b; (void)n; return 0; }
void backtrace_symbols_fd(void *const *b, int n, int fd) { (void)b; (void)n; (void)fd; }
void *__sched_cpualloc(size_t n) { return calloc(1, (n + 63) / 64 * 8); }
void __sched_cpufree(void *p) { free(p); }
static unsigned long long r_state = 88172645463325252ULL;
int rand(void) { r_state ^= r_state << 13; r_state ^= r_state >> 7; r_state ^= r_state << 17; return (int)(r_state & 0x7FFFFFFF); }
void srand(unsigned s) { r_state = s ? s : 1; }

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{   /* ordenacion por montículo: sin recursion ni memoria extra */
    uint8_t *a = base, tmp[256];
    if (sz > sizeof tmp || n < 2) { if (n < 2) return; }
    #define EL(i) (a + (size_t)(i) * sz)
    #define SWP(x, y) do { uint8_t *_x = EL(x), *_y = EL(y); for (size_t _k = 0; _k < sz; ++_k) { uint8_t _t = _x[_k]; _x[_k] = _y[_k]; _y[_k] = _t; } } while (0)
    for (size_t start = n / 2; start-- > 0;) {
        size_t r = start;
        for (;;) { size_t c = 2 * r + 1; if (c >= n) break; if (c + 1 < n && cmp(EL(c), EL(c + 1)) < 0) c++; if (cmp(EL(r), EL(c)) >= 0) break; SWP(r, c); r = c; }
    }
    for (size_t end = n - 1; end > 0; --end) {
        SWP(0, end);
        size_t r = 0;
        for (;;) { size_t c = 2 * r + 1; if (c >= end) break; if (c + 1 < end && cmp(EL(c), EL(c + 1)) < 0) c++; if (cmp(EL(r), EL(c)) >= 0) break; SWP(r, c); r = c; }
    }
    #undef EL
    #undef SWP
    (void)tmp;
}

/* ------------------------------------------------------------------ hilos (SOMA_244_HILOS)
 * pthread_create lanza el hilo en un nucleo secundario libre (llamada 32 del nucleo); si no hay, devuelve EAGAIN. El hilo NO puede
 * hacer llamadas al sistema: los mutex son cerrojos de espera activa y pthread_cond_wait suelta el mutex, espera un poco y lo
 * vuelve a coger (despertar espurio, permitido: quien espera siempre comprueba su condicion en un bucle, como hace ggml). */
#define SYS_SOMA_THR 32
struct vthr { void *(*fn)(void *); void *arg; void *ret; void *stack; long core; };
void voz_thread_body(struct vthr *t);
void voz_thread_body(struct vthr *t) { t->ret = t->fn(t->arg); }
__asm__(".text\n.global voz_thread_entry, voz_thread_exit\n"
        "voz_thread_entry:\n"            /* rdi = struct vthr *, rsp alineada a 16 */
        "    call voz_thread_body\n"
        "voz_thread_exit:\n"
        "    ud2\n");                     /* el nucleo lo reconoce como fin del hilo */
extern char voz_thread_entry[], voz_thread_exit[];
#define VTHR_STACK (512u * 1024u)
int voz_threads_free(void) { return (int)sc3(SYS_SOMA_THR, 0, 0, 0); }
int pthread_create(unsigned long *out, const void *attr, void *(*fn)(void *), void *arg)
{
    (void)attr;
#ifdef VOZ_HOST
    (void)out; (void)fn; (void)arg; return 11;
#else
    if (!voz_pretouch) return 11;                          /* sin memoria tocada de antemano no hay hilos */
    struct vthr *t = malloc(sizeof *t);
    uint8_t *st = malloc(VTHR_STACK);
    if (!t || !st) { free(t); free(st); return 11; }
    t->fn = fn; t->arg = arg; t->ret = 0; t->stack = st;
    uint64_t req[4];
    req[0] = (uint64_t)(uintptr_t)voz_thread_entry;
    req[1] = ((uint64_t)(uintptr_t)st + VTHR_STACK) & ~(uint64_t)15;   /* alineada a 16: voz_thread_entry hace un call */
    req[2] = (uint64_t)(uintptr_t)t;
    req[3] = (uint64_t)(uintptr_t)voz_thread_exit;
    long c = sc3(SYS_SOMA_THR, 1, (long)req, 0);
    if (c <= 0) { free(st); free(t); return 11; }
    t->core = c;
    *out = (unsigned long)(uintptr_t)t;
    return 0;
#endif
}
int pthread_join(unsigned long th, void **ret)
{
    struct vthr *t = (struct vthr *)(uintptr_t)th;
    if (!t) return 3;
    long r = sc3(SYS_SOMA_THR, 2, t->core, 0);
    if (r != 0) { static const char m[] = "[voz] un hilo de calculo fallo\n"; o_flush(); sc3(SYS_WRITE, (long)m, sizeof m - 1, 0); voz_exit(9); }
    if (ret) *ret = t->ret;
    free(t->stack); free(t);
    return 0;
}
unsigned long pthread_self(void) { return 1; }
struct vmtx { volatile int l; };
int pthread_mutex_init(void *m, const void *a) { (void)a; ((struct vmtx *)m)->l = 0; return 0; }
int pthread_mutex_destroy(void *m) { (void)m; return 0; }
int pthread_mutex_lock(void *m) { struct vmtx *x = m; while (__atomic_exchange_n(&x->l, 1, __ATOMIC_ACQUIRE)) __asm__ volatile ("pause"); return 0; }
int pthread_mutex_unlock(void *m) { __atomic_store_n(&((struct vmtx *)m)->l, 0, __ATOMIC_RELEASE); return 0; }
int pthread_cond_init(void *c, const void *a) { (void)c; (void)a; return 0; }
int pthread_cond_destroy(void *c) { (void)c; return 0; }
int pthread_cond_wait(void *c, void *m) { (void)c; pthread_mutex_unlock(m); for (int i = 0; i < 64; ++i) __asm__ volatile ("pause"); pthread_mutex_lock(m); return 0; }
int pthread_cond_broadcast(void *c) { (void)c; return 0; }
int pthread_cond_signal(void *c) { (void)c; return 0; }
int pthread_getaffinity_np(unsigned long t, size_t n, void *s) { (void)t; (void)n; (void)s; return -1; }
int pthread_setaffinity_np(unsigned long t, size_t n, const void *s) { (void)t; (void)n; (void)s; return -1; }
int pthread_setschedparam(unsigned long t, int p, const void *s) { (void)t; (void)p; (void)s; return -1; }
int __cxa_atexit(void (*f)(void *), void *a, void *d) { (void)f; (void)a; (void)d; return 0; }
int atexit(void (*f)(void)) { (void)f; return 0; }
void *__dso_handle = &__dso_handle;
int *__errno_location(void) { static int e; return &e; }

/* ------------------------------------------------------------------ matematicas en float (sobre libm_dbl) */
double exp(double); double log(double); double pow(double, double); double sin(double); double cos(double); double tanh(double);
double erf(double); double expm1(double); double log1p(double); double log10(double); double frexp(double, int *); double ldexp(double, int);
double floor(double); double round(double);
float expf(float x) { return (float)exp(x); }
float logf(float x) { return (float)log(x); }
float log2f(float x) { return (float)(log(x) * 1.4426950408889634); }
double log2(double x) { return log(x) * 1.4426950408889634; }
float log10f(float x) { return (float)log10(x); }
float powf(float a, float b) { return (float)pow(a, b); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
void sincosf(float x, float *s, float *c) { *s = (float)sin(x); *c = (float)cos(x); }
void sincos(double x, double *s, double *c) { *s = sin(x); *c = cos(x); }
float tanhf(float x) { return (float)tanh(x); }
float erff(float x) { return (float)erf(x); }
float expm1f(float x) { return (float)expm1(x); }
float log1pf(float x) { return (float)log1p(x); }
float sqrtf(float x) { __asm__ ("sqrtss %1, %0" : "=x"(x) : "x"(x)); return x; }
double sqrt(double);
float fabsf(float x) { return __builtin_fabsf(x); }
float fmaxf(float a, float b) { return a != a ? b : b != b ? a : a > b ? a : b; }
float fminf(float a, float b) { return a != a ? b : b != b ? a : a < b ? a : b; }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return -(float)floor(-(double)x); }
float roundf(float x) { return (float)round(x); }
static double round_away(double x) { volatile double r = round(x); return r; }
long lroundf(float x) { return (long)round_away(x); }
long lround(double x) { return (long)round_away(x); }
float frexpf(float x, int *e) { return (float)frexp(x, e); }
float ldexpf(float x, int e) { return (float)ldexp(x, e); }
