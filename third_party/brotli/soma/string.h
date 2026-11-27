/* SOMA_348_FUENTES_WEB: string.h minimo para Brotli en ring 3: las funciones las da memfn.h del propio servicio. */
#ifndef SOMA_BROTLI_STRING_H
#define SOMA_BROTLI_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif
