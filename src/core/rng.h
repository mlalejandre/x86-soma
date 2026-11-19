/* SOMA_192 (seguridad): generador aleatorio UNICO del nucleo (CSPRNG). Antes habia seis generadores distintos y casi todos
 * predecibles: el DNS sembraba un xorshift de 32 bits con el tiempo de arranque, /dev/urandom salia de una constante fija
 * (la misma secuencia en cada arranque), los nonces de WPA de la Pi 5 del contador de ciclos, y crypto.getRandomValues de
 * Math.random. Todo lo que necesite un numero impredecible (IDs y puertos de DNS, claves y nonces de TLS y WPA, ISN de TCP,
 * /dev/urandom, JavaScript) lo pide aqui.
 *
 * Diseno: ChaCha20 con borrado rapido de clave (la clave se sustituye tras cada peticion: lo ya entregado no se puede
 * reconstruir aunque despues se lea la memoria). Semilla = SHA-256 de RDSEED y RDRAND (si la CPU los tiene), ruido del
 * contador de ciclos, reloj, RTC y los instantes de llegada de tramas de red; se vuelve a sembrar cada minuto o cada MiB.
 * Sin RDRAND/RDSEED (QEMU por defecto, Raspberry Pi 5) la calidad depende del ruido de tiempos: `rng` lo dice. */
#ifndef SOMA_RNG_H
#define SOMA_RNG_H
#include <stdint.h>
#include <stddef.h>

struct rng_info {
    int rdseed, rdrand;            /* fuentes de hardware disponibles */
    uint32_t reseeds;              /* veces que se ha sembrado */
    uint64_t bytes_out;            /* bytes entregados */
    uint32_t events;               /* sucesos externos mezclados (tramas de red...) */
};

void rng_bytes(void *out, size_t n);       /* n bytes impredecibles; nunca falla */
void rng_add_event(uint64_t v);            /* mezcla un suceso externo (barato: se puede llamar por cada trama) */
void rng_get_info(struct rng_info *i);
int  rng_selftest(void);                   /* 0 = correcto (vector de ChaCha20 del RFC 8439 y salidas distintas) */

static inline uint32_t rng_u32(void) { uint32_t v; rng_bytes(&v, sizeof v); return v; }
static inline uint64_t rng_u64(void) { uint64_t v; rng_bytes(&v, sizeof v); return v; }

/* Pruebas en el HOST (make test): los fuentes del nucleo se compilan sueltos, sin rng.c. Estas definiciones DEBILES solo
 * existen en compilaciones alojadas; el nucleo (-ffreestanding) no las tiene y enlaza siempre con rng.c. NO son seguras. */
#if __STDC_HOSTED__ && !defined(SOMA_RNG_IMPL)
__attribute__((weak)) void rng_bytes(void *out, size_t n)
{
    static uint64_t s = 0x9E3779B97F4A7C15ULL;
    uint8_t *o = (uint8_t *)out;
    for (size_t i = 0; i < n; i++) {
        if ((i & 7) == 0) { s += 0x9E3779B97F4A7C15ULL; }
        uint64_t z = s + i; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; z ^= z >> 31;
        o[i] = (uint8_t)(z >> ((i & 7) * 8));
    }
}
__attribute__((weak)) void rng_add_event(uint64_t v) { (void)v; }
#endif
#endif
