#ifndef SOMA_PMM_SCAN_H
#define SOMA_PMM_SCAN_H
/* PARCHE 127: busqueda de marcos libres por palabras de 64 bits (funciones puras: se prueban en el host). Bit 1 = ocupado. */
#include <stdint.h>
#include <stddef.h>

/* Primer bit a 0 en [from, to) o (size_t)-1. Salta 64 marcos de golpe cuando la palabra esta llena. */
static inline size_t pmm_scan_zero(const uint64_t *w, size_t from, size_t to)
{
    size_t f = from;
    while (f < to) {
        uint64_t v = w[f >> 6] | ((1ULL << (f & 63)) - 1);      /* los bits por debajo de `f` cuentan como ocupados */
        if (v != ~0ULL) {
            size_t r = (f & ~(size_t)63) + (size_t)__builtin_ctzll(~v);
            return r < to ? r : (size_t)-1;
        }
        f = (f | 63) + 1;
    }
    return (size_t)-1;
}
/* Primer bit a 1 en [from, to) o (size_t)-1 (fin de un tramo libre). */
static inline size_t pmm_scan_one(const uint64_t *w, size_t from, size_t to)
{
    size_t f = from;
    while (f < to) {
        uint64_t v = w[f >> 6] & ~((1ULL << (f & 63)) - 1);
        if (v) {
            size_t r = (f & ~(size_t)63) + (size_t)__builtin_ctzll(v);
            return r < to ? r : (size_t)-1;
        }
        f = (f | 63) + 1;
    }
    return (size_t)-1;
}
/* Primer tramo de `n` bits a 0 consecutivos en [from, to) o (size_t)-1. */
static inline size_t pmm_scan_run(const uint64_t *w, size_t from, size_t to, size_t n)
{
    size_t f = from;
    if (n == 0) return (size_t)-1;
    while (f < to) {
        size_t z = pmm_scan_zero(w, f, to);
        if (z == (size_t)-1) return (size_t)-1;
        size_t e = pmm_scan_one(w, z, to);
        size_t end = e == (size_t)-1 ? to : e;
        if (end - z >= n) return z;
        f = end;
    }
    return (size_t)-1;
}
#endif
