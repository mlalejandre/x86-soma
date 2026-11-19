/* SOMA_176A: pruebas de las primitivas TLS (SHA-256, HMAC, X25519, AES-GCM) con vectores de python-cryptography y de la RFC 7748. */
#include <stdio.h>
#include <string.h>
#include "tlscrypto.h"
#include "tls_vectors_176.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FALLO: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(void)
{
    for (unsigned i = 0; i < sizeof SHA_V / sizeof SHA_V[0]; i++) { uint8_t o[32]; sha256(SHA_V[i].in, SHA_V[i].n, o); CHECK(!memcmp(o, SHA_V[i].out, 32), "sha256 n=%u", SHA_V[i].n);
        struct sha256_ctx c; sha256_init(&c); for (unsigned k = 0; k < SHA_V[i].n; k++) sha256_update(&c, SHA_V[i].in + k, 1); sha256_final(&c, o); CHECK(!memcmp(o, SHA_V[i].out, 32), "sha256 byte a byte n=%u", SHA_V[i].n); }
    for (unsigned i = 0; i < sizeof HMAC_V / sizeof HMAC_V[0]; i++) { uint8_t o[32]; hmac_sha256(HMAC_V[i].key, HMAC_V[i].kl, HMAC_V[i].msg, HMAC_V[i].ml, o); CHECK(!memcmp(o, HMAC_V[i].out, 32), "hmac %u", i); }
    for (unsigned i = 0; i < sizeof X_V / sizeof X_V[0]; i++) { uint8_t o[32]; x25519(o, X_V[i].sc, X_V[i].pt); CHECK(!memcmp(o, X_V[i].out, 32), "x25519 %u", i); }
    { uint8_t a[32], b[32], pa[32], pb[32], s1[32], s2[32]; for (int i = 0; i < 32; i++) { a[i] = (uint8_t)(i * 7 + 1); b[i] = (uint8_t)(i * 13 + 5); }
      x25519_base(pa, a); x25519_base(pb, b); x25519(s1, a, pb); x25519(s2, b, pa); CHECK(!memcmp(s1, s2, 32), "x25519 acuerdo DH"); }
    for (unsigned i = 0; i < sizeof GCM_V / sizeof GCM_V[0]; i++) {
        uint8_t ct[220], tag[16], pt[220];
        gcm128_seal(GCM_V[i].key, GCM_V[i].iv, GCM_V[i].aad, GCM_V[i].al, GCM_V[i].pt, GCM_V[i].pl, ct, tag);
        CHECK(!memcmp(ct, GCM_V[i].ct, GCM_V[i].pl) && !memcmp(tag, GCM_V[i].ct + GCM_V[i].pl, 16), "gcm seal %u", i);
        CHECK(gcm128_open(GCM_V[i].key, GCM_V[i].iv, GCM_V[i].aad, GCM_V[i].al, GCM_V[i].ct, GCM_V[i].pl, GCM_V[i].ct + GCM_V[i].pl, pt) == 0 && !memcmp(pt, GCM_V[i].pt, GCM_V[i].pl), "gcm open %u", i);
        uint8_t bad[16]; memcpy(bad, GCM_V[i].ct + GCM_V[i].pl, 16); bad[3] ^= 1;
        CHECK(gcm128_open(GCM_V[i].key, GCM_V[i].iv, GCM_V[i].aad, GCM_V[i].al, GCM_V[i].ct, GCM_V[i].pl, bad, pt) == -1, "gcm etiqueta mala %u", i);
    }
    { uint8_t r1[64], r2[64]; tls_random(r1, 64); tls_random(r2, 64); CHECK(memcmp(r1, r2, 64), "aleatorios repetidos"); }
    if (fails) { printf("test_tls_176: %d fallos\n", fails); return 1; }
    printf("test_tls_176: SHA-256, HMAC, X25519 y AES-GCM correctos (vectores de python-cryptography y RFC 7748)\n");
    return 0;
}
