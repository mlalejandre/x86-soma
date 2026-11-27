/* SOMA_348_FUENTES_WEB: stdlib.h minimo para compilar el decodificador Brotli en los servicios de ring 3 (sin libc).
 * malloc/free van al monton del proceso (kmalloc/kfree). El decodificador no usa nada mas de stdlib. */
#ifndef SOMA_BROTLI_STDLIB_H
#define SOMA_BROTLI_STDLIB_H
#include <stddef.h>
void *kmalloc(size_t n);
void kfree(void *p);
static inline void *malloc(size_t n) { return kmalloc(n); }
static inline void free(void *p) { if (p) kfree(p); }
#endif
