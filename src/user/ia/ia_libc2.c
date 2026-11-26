/* SOMA_329_IA_LOCAL: lo que llama.cpp y la parte de libstdc++ que arrastra (excepciones, std::regex, std::locale de "C")
 * necesitan de la libc ademas de voz_libc.c. Casi todo es la configuracion regional "C" (no hay otras en SOMA) y avisos de
 * que no hay mmap ni descriptores POSIX: llama.cpp carga el modelo con fopen/fread (modo de carga "none"). */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef int wchar_t_;
typedef unsigned int wint_t_;
struct _IO_FILE;
int fseek(struct _IO_FILE *f, long off, int wh);
long ftell(struct _IO_FILE *f);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strncpy(char *d, const char *s, size_t n);
void *malloc(size_t n);
double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
int vsnprintf(char *out, size_t cap, const char *f, va_list ap);
int isspace(int c);
long voz_uptime_ms(void);
float exp2f(float x);


/* ---- archivos: solo FILE* (voz_libc); los descriptores POSIX no existen */
int fseeko(struct _IO_FILE *f, long off, int wh) { return fseek(f, off, wh); }
long ftello(struct _IO_FILE *f) { return ftell(f); }
int fileno(struct _IO_FILE *f) { (void)f; return -1; }
int open(const char *p, int fl, ...) { (void)p; (void)fl; return -1; }
long lseek(int fd, long off, int wh) { (void)fd; (void)off; (void)wh; return -1; }
int fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
void *mmap(void *a, size_t n, int p, int f, int fd, long o) { (void)a; (void)n; (void)p; (void)f; (void)fd; (void)o; return (void *)-1; }
int munmap(void *a, size_t n) { (void)a; (void)n; return 0; }
int madvise(void *a, size_t n, int x) { (void)a; (void)n; (void)x; return 0; }
int posix_madvise(void *a, size_t n, int x) { (void)a; (void)n; (void)x; return 0; }
int posix_fadvise(int fd, long o, long n, int x) { (void)fd; (void)o; (void)n; (void)x; return 0; }
int mlock(const void *a, size_t n) { (void)a; (void)n; return -1; }
int getrlimit(int r, void *l) { (void)r; (void)l; return -1; }
int ioctl(int fd, unsigned long r, ...) { (void)fd; (void)r; return -1; }
long syscall(long n, ...) { (void)n; return -1; }
long __read_chk(int fd, void *b, size_t n, size_t cap) { (void)fd; (void)b; (void)n; (void)cap; return -1; }

/* ---- varios */
char __libc_single_threaded = 0;
int pthread_once(int *o, void (*f)(void)) { if (*o == 0) { *o = 1; f(); } return 0; }   /* solo el hilo principal */
char *secure_getenv(const char *n) { (void)n; return 0; }
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); if (d) memcpy(d, s, n); return d; }
int strerror_r(int e, char *b, size_t n) { (void)e; if (n) { strncpy(b, "error", n); b[n - 1] = 0; } return 0; }
int gettimeofday(void *tv, void *tz) { (void)tz; long ms = voz_uptime_ms(); long *t = tv; if (t) { t[0] = ms / 1000; t[1] = (ms % 1000) * 1000; } return 0; }
static uint64_t mezcla(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33; return x; }
static uint64_t g_azar = 0x9E3779B97F4A7C15ULL;
static uint64_t azar(void)
{
    uint64_t t;
    __asm__ volatile ("rdtsc; shlq $32, %%rdx; orq %%rdx, %%rax" : "=a"(t) : : "rdx");
    g_azar = mezcla(g_azar + t);
    return g_azar;
}
int getentropy(void *b, size_t n) { uint8_t *p = b; while (n) { uint64_t v = azar(); for (int i = 0; i < 8 && n; i++, n--) *p++ = (uint8_t)(v >> (8 * i)); } return 0; }
uint32_t arc4random(void) { return (uint32_t)azar(); }
int _dl_find_object(void *pc, void *res) { (void)pc; (void)res; return -1; }   /* el desenrollador usa la tabla registrada al arrancar */
float expf(float x);
float exp2f(float x) { return expf(x * 0.69314718055994530942f); }

/* ---- comprobaciones de _FORTIFY_SOURCE que trae libstdc++.a compilada */
void *__memcpy_chk(void *d, const void *s, size_t n, size_t cap) { (void)cap; return memcpy(d, s, n); }
int __sprintf_chk(char *o, int fl, size_t cap, const char *f, ...) { (void)fl; va_list ap; va_start(ap, f); int r = vsnprintf(o, cap, f, ap); va_end(ap); return r; }

/* ---- configuracion regional: solo "C" (y textos en UTF-8 sin conversion) */
/* la forma de struct __locale_struct de glibc: libstdc++ (modelo "gnu") lee de ella las tablas de ctype<char> */
struct loc_c { void *datos[13]; const unsigned short *ctype_b; const int *ctype_tolower; const int *ctype_toupper; const char *nombres[13]; };
static unsigned short g_ctb[384]; static int g_tlo[384], g_tup[384];
static struct loc_c g_loc;
static void loc_init(void)
{
    if (g_loc.ctype_b) return;
    for (int i = -128; i < 256; i++) {
        int c = i < 0 ? i + 256 : i; unsigned short m = 0;          /* bits de glibc en little endian (_ISupper = 0x100 ...) */
        if (c < 128) {
            int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', di = c >= '0' && c <= '9';
            int al = up || lo, xd = di || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            int sp = c == ' ' || (c >= '\t' && c <= '\r'), pr = c >= 32 && c < 127, gr = c > 32 && c < 127;
            int ct = c < 32 || c == 127, bl = c == ' ' || c == '\t', pu = gr && !al && !di;
            m = (unsigned short)((up ? 0x100 : 0) | (lo ? 0x200 : 0) | (al ? 0x400 : 0) | (di ? 0x800 : 0) | (xd ? 0x1000 : 0) | (sp ? 0x2000 : 0)
              | (pr ? 0x4000 : 0) | (gr ? 0x8000 : 0) | (bl ? 0x1 : 0) | (ct ? 0x2 : 0) | (pu ? 0x4 : 0) | (al || di ? 0x8 : 0));
        }
        g_ctb[i + 128] = m;
        g_tlo[i + 128] = c >= 'A' && c <= 'Z' ? c + 32 : i;
        g_tup[i + 128] = c >= 'a' && c <= 'z' ? c - 32 : i;
    }
    for (int k = 0; k < 13; k++) g_loc.nombres[k] = "C";
    g_loc.ctype_tolower = g_tlo + 128; g_loc.ctype_toupper = g_tup + 128; g_loc.ctype_b = g_ctb + 128;
}
void *__newlocale(int m, const char *n, void *b) { (void)m; (void)n; (void)b; loc_init(); return &g_loc; }
void __freelocale(void *l) { (void)l; }
void *__duplocale(void *l) { (void)l; loc_init(); return &g_loc; }
void *__uselocale(void *l) { (void)l; loc_init(); return &g_loc; }
const char *__nl_langinfo_l(int item, void *l)
{
    (void)l;
    switch (item) {
        case 0x10000: return ".";          /* RADIXCHAR / DECIMAL_POINT */
        case 0x10001: return "";           /* THOUSEP */
        case 14: return "UTF-8";           /* CODESET */
        default: return "";
    }
}
size_t __ctype_get_mb_cur_max(void) { return 1; }
char *gettext(const char *m) { return (char *)m; }
char *dgettext(const char *d, const char *m) { (void)d; return (char *)m; }
char *bind_textdomain_codeset(const char *d, const char *c) { (void)d; (void)c; return 0; }
void *iconv_open(const char *a, const char *b) { (void)a; (void)b; return (void *)-1; }
size_t iconv(void *cd, char **i, size_t *il, char **o, size_t *ol) { (void)cd; (void)i; (void)il; (void)o; (void)ol; return (size_t)-1; }
int iconv_close(void *cd) { (void)cd; return 0; }
int __strcoll_l(const char *a, const char *b, void *l) { (void)l; return strcmp(a, b); }
size_t __strxfrm_l(char *d, const char *s, size_t n, void *l) { (void)l; size_t k = strlen(s); if (n) { size_t c = k < n - 1 ? k : n - 1; memcpy(d, s, c); d[c] = 0; } return k; }
double __strtod_l(const char *s, char **e, void *l) { (void)l; return strtod(s, e); }
float __strtof_l(const char *s, char **e, void *l) { (void)l; return (float)strtod(s, e); }
long double strtold_l(const char *s, char **e, void *l) { (void)l; return strtod(s, e); }
size_t __strftime_l(char *s, size_t n, const char *f, const void *tm, void *l) { (void)f; (void)tm; (void)l; if (n) s[0] = 0; return 0; }
size_t __wcsftime_l(wchar_t_ *s, size_t n, const wchar_t_ *f, const void *tm, void *l) { (void)f; (void)tm; (void)l; if (n) s[0] = 0; return 0; }
/* caracteres anchos: solo ASCII (std::regex de llama.cpp trabaja con char; esto es lo que instancia libstdc++ para wchar_t) */
int __iswctype_l(wint_t_ c, unsigned long t, void *l) { (void)t; (void)l; return c < 128; }
unsigned long __wctype_l(const char *n, void *l) { (void)n; (void)l; return 1; }
wint_t_ __towupper_l(wint_t_ c, void *l) { (void)l; return c >= 'a' && c <= 'z' ? c - 32 : c; }
wint_t_ __towlower_l(wint_t_ c, void *l) { (void)l; return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int __wcscoll_l(const wchar_t_ *a, const wchar_t_ *b, void *l) { (void)l; while (*a && *a == *b) { a++; b++; } return *a - *b; }
size_t __wcsxfrm_l(wchar_t_ *d, const wchar_t_ *s, size_t n, void *l) { (void)l; size_t k = 0; while (s[k]) k++; if (n) { size_t c = k < n - 1 ? k : n - 1; for (size_t i = 0; i < c; i++) d[i] = s[i]; d[c] = 0; } return k; }
wint_t_ btowc(int c) { return c >= 0 && c < 128 ? (wint_t_)c : 0xFFFFFFFFu; }
int wctob(wint_t_ c) { return c < 128 ? (int)c : -1; }
size_t wcslen(const wchar_t_ *s) { size_t n = 0; while (s[n]) n++; return n; }
int wcscmp(const wchar_t_ *a, const wchar_t_ *b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }
wchar_t_ *wmemcpy(wchar_t_ *d, const wchar_t_ *s, size_t n) { return memcpy(d, s, n * sizeof *d); }
wchar_t_ *wmemmove(wchar_t_ *d, const wchar_t_ *s, size_t n) { return memmove(d, s, n * sizeof *d); }
wchar_t_ *wmemset(wchar_t_ *d, wchar_t_ c, size_t n) { for (size_t i = 0; i < n; i++) d[i] = c; return d; }
wchar_t_ *__wmemset_chk(wchar_t_ *d, wchar_t_ c, size_t n, size_t cap) { (void)cap; return wmemset(d, c, n); }
int wmemcmp(const wchar_t_ *a, const wchar_t_ *b, size_t n) { for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }
wchar_t_ *wmemchr(const wchar_t_ *s, wchar_t_ c, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] == c) return (wchar_t_ *)s + i; return 0; }
size_t mbrtowc(wchar_t_ *pw, const char *s, size_t n, void *ps) { (void)ps; if (!s) return 0; if (!n) return (size_t)-2; if (pw) *pw = (unsigned char)*s; return *s ? 1 : 0; }
size_t wcrtomb(char *s, wchar_t_ w, void *ps) { (void)ps; if (!s) return 1; if (w >= 256) return (size_t)-1; *s = (char)w; return 1; }
size_t mbsnrtowcs(wchar_t_ *d, const char **s, size_t nms, size_t n, void *ps)
{
    (void)ps; size_t k = 0; const char *p = *s;
    while (k < nms && (!d || k < n)) { if (d) d[k] = (unsigned char)p[k]; if (!p[k]) { *s = 0; return k; } k++; }
    if (d) *s = p + k;
    return k;
}
size_t wcsnrtombs(char *d, const wchar_t_ **s, size_t nwc, size_t n, void *ps)
{
    (void)ps; size_t k = 0; const wchar_t_ *p = *s;
    while (k < nwc && (!d || k < n)) { if (p[k] >= 256) return (size_t)-1; if (d) d[k] = (char)p[k]; if (!p[k]) { *s = 0; return k; } k++; }
    if (d) *s = p + k;
    return k;
}
size_t __mbsnrtowcs_chk(wchar_t_ *d, const char **s, size_t nms, size_t n, void *ps, size_t cap) { (void)cap; return mbsnrtowcs(d, s, nms, n, ps); }
size_t __mbsrtowcs_chk(wchar_t_ *d, const char **s, size_t n, void *ps, size_t cap) { (void)cap; return mbsnrtowcs(d, s, (size_t)-1, n, ps); }
/* libstdc++ compilada sin _FORTIFY_SOURCE (Debian, Fedora...) pide la version normal en lugar de la _chk */
size_t mbsrtowcs(wchar_t_ *d, const char **s, size_t n, void *ps) { return mbsnrtowcs(d, s, (size_t)-1, n, ps); }

/* ---- sscanf minimo: %d %i %u %x %ld %lu %lld %llu %f %lf %s %c %n y texto literal (lo que usa llama.cpp) */
int __isoc23_sscanf(const char *s, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    const char *p = s; int n = 0;
    while (*f) {
        if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*p)) p++; f++; continue; }
        if (*f != '%') { if (*p != *f) break; p++; f++; continue; }
        f++;
        if (*f == '%') { if (*p != '%') break; p++; f++; continue; }
        int largo = 0; while (*f == 'l' || *f == 'h' || *f == 'z') { if (*f == 'l') largo++; f++; }
        char c = *f++;
        if (c == 'n') { *va_arg(ap, int *) = (int)(p - s); continue; }
        if (c != 'c') while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *e = 0;
        if (c == 'd' || c == 'i') { long long v = strtoll(p, &e, c == 'i' ? 0 : 10); if (e == p) break; if (largo >= 2) *va_arg(ap, long long *) = v; else if (largo) *va_arg(ap, long *) = (long)v; else *va_arg(ap, int *) = (int)v; }
        else if (c == 'u' || c == 'x') { unsigned long long v = strtoull(p, &e, c == 'x' ? 16 : 10); if (e == p) break; if (largo >= 2) *va_arg(ap, unsigned long long *) = v; else if (largo) *va_arg(ap, unsigned long *) = (unsigned long)v; else *va_arg(ap, unsigned *) = (unsigned)v; }
        else if (c == 'f' || c == 'g' || c == 'e') { double v = strtod(p, &e); if (e == p) break; if (largo) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v; }
        else if (c == 's') { char *d = va_arg(ap, char *); e = (char *)p; while (*e && !isspace((unsigned char)*e)) *d++ = *e++; *d = 0; }
        else if (c == 'c') { *va_arg(ap, char *) = *p; e = (char *)p + 1; }
        else break;
        p = e; n++;
    }
    va_end(ap);
    return n;
}
int sscanf(const char *s, const char *f, ...) __attribute__((alias("__isoc23_sscanf")));
int __isoc99_sscanf(const char *s, const char *f, ...) __attribute__((alias("__isoc23_sscanf")));   /* glibc < 2.38 */
