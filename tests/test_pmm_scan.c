/* PARCHE 127: pmm_scan_zero/one/run contra una version bit a bit, con mapas aleatorios y limites no alineados. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/arch/x86_64/pmm_scan.h"
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; if (fails < 20) printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)
#define NB 1024
static uint64_t w[NB / 64 + 1];
static int bit(size_t f) { return (w[f >> 6] >> (f & 63)) & 1; }
static size_t ref_zero(size_t a, size_t b) { for (size_t f = a; f < b; ++f) if (!bit(f)) return f; return (size_t)-1; }
static size_t ref_one(size_t a, size_t b) { for (size_t f = a; f < b; ++f) if (bit(f)) return f; return (size_t)-1; }
static size_t ref_run(size_t a, size_t b, size_t n) {
    size_t run = 0; if (!n) return (size_t)-1;
    for (size_t f = a; f < b; ++f) { if (bit(f)) { run = 0; continue; } if (++run == n) return f + 1 - n; }
    return (size_t)-1;
}
int main(void)
{
    srand(12345);
    for (int round = 0; round < 3000; ++round) {
        int dens = round % 5;                                   /* de casi vacio a casi lleno */
        for (size_t i = 0; i < sizeof w / 8; ++i) {
            uint64_t v = 0;
            for (int k = 0; k < 64; ++k) {
                int r = rand() % 100;
                int on = dens == 0 ? r < 2 : dens == 1 ? r < 30 : dens == 2 ? r < 70 : dens == 3 ? r < 98 : 1;
                if (on) v |= 1ULL << k;
            }
            w[i] = v;
        }
        for (int q = 0; q < 20; ++q) {
            size_t a = (size_t)rand() % NB, b = a + (size_t)rand() % (NB - a + 1), n = 1 + (size_t)rand() % 70;
            CHECK(pmm_scan_zero(w, a, b) == ref_zero(a, b));
            CHECK(pmm_scan_one(w, a, b) == ref_one(a, b));
            CHECK(pmm_scan_run(w, a, b, n) == ref_run(a, b, n));
        }
    }
    memset(w, 0xFF, sizeof w);                                  /* todo ocupado */
    CHECK(pmm_scan_zero(w, 0, NB) == (size_t)-1);
    w[3] &= ~(1ULL << 63);                                      /* un hueco en el ultimo bit de una palabra */
    CHECK(pmm_scan_zero(w, 0, NB) == 3 * 64 + 63);
    CHECK(pmm_scan_zero(w, 3 * 64 + 63, NB) == 3 * 64 + 63);
    CHECK(pmm_scan_zero(w, 3 * 64 + 64, NB) == (size_t)-1);
    CHECK(pmm_scan_zero(w, 0, 3 * 64 + 63) == (size_t)-1);       /* el limite superior es exclusivo */
    memset(w, 0, sizeof w);                                     /* todo libre */
    CHECK(pmm_scan_run(w, 5, NB, 64) == 5);
    CHECK(pmm_scan_run(w, 5, 5 + 63, 64) == (size_t)-1);
    CHECK(pmm_scan_zero(w, 7, 7) == (size_t)-1);                /* rango vacio */
    printf("test_pmm_scan: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
