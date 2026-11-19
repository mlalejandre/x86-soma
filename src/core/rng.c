/* SOMA_192: generador aleatorio unico del nucleo. Ver rng.h. */
#define SOMA_RNG_IMPL 1
#include "rng.h"
#include "arch.h"
#include "tlscrypto.h"

extern uint64_t timer_get_uptime_ms(void);

#define RNG_RESEED_MS     60000u
#define RNG_RESEED_BYTES  (1u << 20)

static uint32_t g_key[8];
static uint32_t g_ctr;
static int g_seeded;
static uint64_t g_last_seed_ms, g_since_seed;
static uint64_t g_pool[16]; static uint32_t g_pool_i;
static struct rng_info g_info;
static volatile char g_lock;

static uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
static uint64_t rotl64(uint64_t v, int n) { n &= 63; return n ? (v << n) | (v >> (64 - n)) : v; }
#define QR(a, b, c, d) do { a += b; d ^= a; d = rotl32(d, 16); c += d; b ^= c; b = rotl32(b, 12); a += b; d ^= a; d = rotl32(d, 8); c += d; b ^= c; b = rotl32(b, 7); } while (0)

/* Bloque de ChaCha20 (RFC 8439, 20 rondas). */
static void chacha_block(const uint32_t key[8], uint32_t counter, const uint32_t nonce[3], uint8_t out[64])
{
    uint32_t s[16] = { 0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
                       counter, nonce[0], nonce[1], nonce[2] };
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = s[i];
    for (int r = 0; r < 10; r++) {
        QR(x[0], x[4], x[8], x[12]); QR(x[1], x[5], x[9], x[13]); QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]); QR(x[2], x[7], x[8], x[13]); QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + s[i];
        out[4 * i] = (uint8_t)v; out[4 * i + 1] = (uint8_t)(v >> 8); out[4 * i + 2] = (uint8_t)(v >> 16); out[4 * i + 3] = (uint8_t)(v >> 24);
    }
}

/* ---------------------------------------------------------------- fuentes de entropia */
#if defined(__x86_64__)
static void hw_detect(void)
{
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    uint32_t maxleaf = a;
    a = 1; c = 0; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    g_info.rdrand = (c >> 30) & 1;
    if (maxleaf >= 7) { a = 7; c = 0; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d)); g_info.rdseed = (b >> 18) & 1; }
}
static int hw_word(uint64_t *v)
{
    for (int i = 0; g_info.rdseed && i < 16; i++) {
        unsigned char ok; uint64_t x; __asm__ volatile ("rdseed %0; setc %1" : "=r"(x), "=qm"(ok) : : "cc");
        if (ok) { *v = x; return 1; }
        arch_pause();
    }
    for (int i = 0; g_info.rdrand && i < 16; i++) {
        unsigned char ok; uint64_t x; __asm__ volatile ("rdrand %0; setc %1" : "=r"(x), "=qm"(ok) : : "cc");
        if (ok) { *v = x; return 1; }
    }
    return 0;
}
#else
static void hw_detect(void) { }
static int hw_word(uint64_t *v) { (void)v; return 0; }
#endif

/* Mezcla en `h` todo lo que hay: clave anterior, hardware, ruido de tiempos, relojes y sucesos externos. */
static void reseed(int samples)
{
    struct sha256_ctx h; uint8_t d[32];
    sha256_init(&h);
    sha256_update(&h, g_key, sizeof g_key);
    for (int i = 0; i < 8; i++) { uint64_t w; if (hw_word(&w)) sha256_update(&h, &w, sizeof w); }
    /* ruido de tiempos: el contador de ciclos entre tramos de trabajo de duracion variable (cache, interrupciones, el anfitrion) */
    volatile uint64_t sink = 0;
    for (int i = 0; i < samples; i++) {
        uint64_t t = arch_cycle_counter();
        for (uint32_t k = 0; k < 16 + (uint32_t)(t & 15); k++) sink += rotl64(t, (int)k) ^ g_pool[k & 15];
        uint64_t t2 = arch_cycle_counter();
        uint64_t s[2] = { t2, t2 - t };
        sha256_update(&h, s, sizeof s);
    }
    uint64_t misc[4] = { timer_get_uptime_ms(), (uint64_t)(uintptr_t)&h, sink, g_info.bytes_out };
    sha256_update(&h, misc, sizeof misc);
    sha256_update(&h, g_pool, sizeof g_pool);
    sha256_final(&h, d);
    for (int i = 0; i < 8; i++) g_key[i] = (uint32_t)d[4 * i] | ((uint32_t)d[4 * i + 1] << 8) | ((uint32_t)d[4 * i + 2] << 16) | ((uint32_t)d[4 * i + 3] << 24);
    for (int i = 0; i < 32; i++) d[i] = 0;
    g_ctr = 0; g_seeded = 1; g_since_seed = 0; g_last_seed_ms = timer_get_uptime_ms();
    g_info.reseeds++;
}

void rng_add_event(uint64_t v)
{
    uint32_t i = g_pool_i++;
    g_pool[i & 15] ^= rotl64(v ^ arch_cycle_counter(), (int)(i * 7));
    g_info.events++;
}

void rng_bytes(void *out, size_t n)
{
    static const uint32_t nonce[3] = { 0, 0, 0 };
    uint8_t *o = (uint8_t *)out, blk[64];
    while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)) arch_pause();
    if (!g_seeded) { hw_detect(); reseed(256); }
    else if (g_since_seed >= RNG_RESEED_BYTES || timer_get_uptime_ms() - g_last_seed_ms >= RNG_RESEED_MS) reseed(32);
    g_info.bytes_out += n; g_since_seed += n;
    while (n) {
        chacha_block(g_key, g_ctr++, nonce, blk);
        size_t k = n < 64 ? n : 64;
        for (size_t i = 0; i < k; i++) o[i] = blk[i];
        o += k; n -= k;
    }
    /* borrado rapido de clave: la siguiente peticion parte de una clave nueva */
    chacha_block(g_key, 0xFFFFFFFFu, nonce, blk);
    for (int i = 0; i < 8; i++) g_key[i] = (uint32_t)blk[4 * i] | ((uint32_t)blk[4 * i + 1] << 8) | ((uint32_t)blk[4 * i + 2] << 16) | ((uint32_t)blk[4 * i + 3] << 24);
    g_ctr = 0;
    for (int i = 0; i < 64; i++) blk[i] = 0;
    __asm__ volatile ("" : : "r"(blk) : "memory");               /* que el compilador no elimine el borrado */
    __atomic_clear(&g_lock, __ATOMIC_RELEASE);
}

void rng_get_info(struct rng_info *i)
{
    if (!g_seeded) { uint8_t b; rng_bytes(&b, 1); }
    *i = g_info;
}

int rng_selftest(void)
{
    /* RFC 8439, seccion 2.3.2 */
    static const uint8_t want[16] = { 0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4 };
    static const uint32_t nonce[3] = { 0x09000000u, 0x4a000000u, 0x00000000u };
    uint32_t key[8]; uint8_t blk[64];
    for (int i = 0; i < 8; i++) key[i] = (uint32_t)(4 * i) | ((uint32_t)(4 * i + 1) << 8) | ((uint32_t)(4 * i + 2) << 16) | ((uint32_t)(4 * i + 3) << 24);
    chacha_block(key, 1, nonce, blk);
    for (int i = 0; i < 16; i++) if (blk[i] != want[i]) return -1;
    uint8_t a[32], b[32]; int same = 1, zero = 1;
    rng_bytes(a, sizeof a); rng_bytes(b, sizeof b);
    for (int i = 0; i < 32; i++) { if (a[i] != b[i]) same = 0; if (a[i]) zero = 0; }
    return (same || zero) ? -2 : 0;
}
