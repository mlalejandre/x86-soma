/* SOMA_265_CC: caracteres anchos (wchar_t de 32 bits, UTF-8 para pasar de y a char) */
#ifndef _SOMA_WCHAR_H
#define _SOMA_WCHAR_H
#include <stddef.h>
typedef unsigned int wint_t;
typedef struct { int __n; } mbstate_t;
#define WEOF ((wint_t)-1)
size_t wcslen(const wchar_t *s); wchar_t *wcscpy(wchar_t *d, const wchar_t *s); wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n);
int wcscmp(const wchar_t *a, const wchar_t *b); int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n); wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcscat(wchar_t *d, const wchar_t *s); wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n); wchar_t *wmemset(wchar_t *d, wchar_t c, size_t n);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps); size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
wint_t btowc(int c); int wctob(wint_t c);
#endif
