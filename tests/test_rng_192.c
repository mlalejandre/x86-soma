/* SOMA_192: generador aleatorio unico del nucleo (src/core/rng.c) en el host. Ver docs/PARCHE_192_SEGURIDAD_GRUPO2.md. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define SOMA_RNG_IMPL 1
#include "rng.h"

static uint64_t g_ms = 1000;
uint64_t timer_get_uptime_ms(void) { return g_ms; }

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    CK(rng_selftest() == 0, "autoprueba (vector de ChaCha20 del RFC 8439)");

    /* tamanos raros: se escribe exactamente lo pedido */
    for (size_t n = 0; n <= 200; n += (n < 70 ? 1 : 13)) {
        uint8_t b[232]; memset(b, 0xA5, sizeof b);
        rng_bytes(b + 16, n);
        int guard = 1; for (int i = 0; i < 16; i++) if (b[i] != 0xA5 || b[16 + n + i] != 0xA5) guard = 0;
        CK(guard, "rng_bytes(%zu) escribio fuera de su zona", n);
    }

    /* dos peticiones nunca coinciden y no hay bloques repetidos dentro de una peticion grande */
    static uint8_t big[1 << 20];
    uint8_t a[64], b[64];
    rng_bytes(a, sizeof a); rng_bytes(b, sizeof b);
    CK(memcmp(a, b, sizeof a) != 0, "dos peticiones seguidas dieron lo mismo");
    rng_bytes(big, sizeof big);
    int rep = 0; for (size_t i = 64; i < sizeof big; i += 64) if (memcmp(big, big + i, 64) == 0) rep++;
    CK(rep == 0, "bloques de 64 bytes repetidos: %d", rep);

    /* estadistica basica sobre 1 MiB: proporcion de bits a 1 y chi-cuadrado por byte (255 grados de libertad, media 255, desv. 22,6) */
    long ones = 0; static long cnt[256];
    for (size_t i = 0; i < sizeof big; i++) { cnt[big[i]]++; ones += __builtin_popcount(big[i]); }
    double frac = (double)ones / (8.0 * sizeof big), chi = 0, e = sizeof big / 256.0;
    for (int i = 0; i < 256; i++) chi += ((double)cnt[i] - e) * ((double)cnt[i] - e) / e;
    CK(frac > 0.498 && frac < 0.502, "proporcion de bits a 1: %.5f", frac);
    CK(chi > 150 && chi < 400, "chi-cuadrado por byte: %.1f (esperado ~255)", chi);

    /* se vuelve a sembrar al pasar 1 MiB y al pasar un minuto */
    struct rng_info i0, i1, i2;
    rng_get_info(&i0);
    rng_bytes(big, sizeof big); rng_bytes(a, 1);
    rng_get_info(&i1);
    CK(i1.reseeds > i0.reseeds, "no se resembro tras 1 MiB (%u -> %u)", i0.reseeds, i1.reseeds);
    g_ms += 61000; rng_bytes(a, 1);
    rng_get_info(&i2);
    CK(i2.reseeds > i1.reseeds, "no se resembro tras un minuto (%u -> %u)", i1.reseeds, i2.reseeds);
    CK(i2.bytes_out >= 2u * (1 << 20), "contador de bytes entregados: %llu", (unsigned long long)i2.bytes_out);

    /* los sucesos externos se cuentan y no rompen nada */
    for (int i = 0; i < 100; i++) rng_add_event((uint64_t)i * 0x9E3779B97F4A7C15ULL);
    rng_get_info(&i0);
    CK(i0.events >= 100, "sucesos mezclados: %u", i0.events);
    CK(rng_u32() != rng_u32() || rng_u64() != rng_u64(), "rng_u32/rng_u64 repetidos");

    printf("test_rng_192: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
