#ifndef _SOMA_STDLIB_H
#define _SOMA_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
void *malloc(size_t n); void *calloc(size_t n, size_t m); void *realloc(void *p, size_t n); void free(void *p);
void exit(int code) __attribute__((noreturn)); void _exit(int code) __attribute__((noreturn)); void abort(void) __attribute__((noreturn));
int atexit(void (*f)(void));
int atoi(const char *s); long atol(const char *s); long long atoll(const char *s); double atof(const char *s);
long strtol(const char *s, char **e, int b); unsigned long strtoul(const char *s, char **e, int b);
long long strtoll(const char *s, char **e, int b); unsigned long long strtoull(const char *s, char **e, int b);
double strtod(const char *s, char **e); float strtof(const char *s, char **e); long double strtold(const char *s, char **e);
int abs(int x); long labs(long x); long long llabs(long long x); div_t div(int a, int b); ldiv_t ldiv(long a, long b);
int rand(void); void srand(unsigned s);
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *));
void *bsearch(const void *k, const void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *));
char *getenv(const char *n); int system(const char *c); char *realpath(const char *p, char *r);
#define MB_CUR_MAX 4
int mblen(const char *s, size_t n); int mbtowc(wchar_t *pwc, const char *s, size_t n); int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *d, const char *s, size_t n); size_t wcstombs(char *d, const wchar_t *s, size_t n);
#endif
