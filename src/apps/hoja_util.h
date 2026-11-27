/* SOMA_358_HOJA: utilidades comunes de los archivos de la hoja (sin libc). */
#ifndef HOJA_UTIL_H
#define HOJA_UTIL_H
#include "hoja_fmt.h"

#define HU __attribute__((unused)) static
HU void zero(void *p, uint32_t n) { uint8_t *q = (uint8_t *)p; while (n--) *q++ = 0; }
HU void copia(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }
HU uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
HU int igual(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
HU int igualn(const char *a, const char *b, uint32_t n) { for (uint32_t i = 0; i < n; ++i) if (a[i] != b[i]) return 0; return 1; }
HU int empieza(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
HU void cpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; if (!cap) return; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; ++i; } d[i] = 0; }
HU int64_t num(const char *s) { int64_t v = 0; int neg = 0, k = 0; while (*s == ' ') ++s; if (*s == '-') { neg = 1; ++s; } while (*s >= '0' && *s <= '9' && k < 18) { v = v * 10 + (*s++ - '0'); ++k; } return neg ? -v : v; }
HU int es_si(const char *s) { return s[0] == '1' || s[0] == 't' || s[0] == 'T'; }
HU f64 Fx(int64_t v) { return f64_from_i64(v); }
#endif
