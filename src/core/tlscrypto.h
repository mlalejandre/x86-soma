#ifndef SOMA_TLSCRYPTO_H
#define SOMA_TLSCRYPTO_H
/* SOMA_176A: primitivas para TLS 1.3 sin dependencias: SHA-256, HMAC-SHA256, HKDF (RFC 5869/8446), X25519 (RFC 7748) y AES-128-GCM
 * (SP 800-38D, sobre aes128 de crypto.c). Probadas en el host con vectores oficiales y contra OpenSSL (tests/test_tls_176.c).
 * No son de tiempo constante en GHASH/AES: valen para un cliente, no para un servidor expuesto. */
#include <stdint.h>
#include <stddef.h>

struct sha256_ctx { uint32_t h[8]; uint64_t len; uint8_t buf[64]; uint32_t n; };
void sha256_init(struct sha256_ctx *c);
void sha256_update(struct sha256_ctx *c, const void *p, size_t n);
void sha256_final(struct sha256_ctx *c, uint8_t out[32]);
void sha256(const void *p, size_t n, uint8_t out[32]);
void hmac_sha256(const uint8_t *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32]);

void hkdf_extract(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen, uint8_t out[32]);
/* HKDF-Expand-Label de TLS 1.3 (etiqueta sin "tls13 "); olen <= 32 */
void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *ctx, size_t clen, uint8_t *out, size_t olen);

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t pub[32], const uint8_t priv[32]);

/* AES-128-GCM con IV de 12 bytes y etiqueta de 16. open devuelve 0 si la etiqueta es correcta, -1 si no (no escribe el texto claro). */
void gcm128_seal(const uint8_t key[16], const uint8_t iv[12], const uint8_t *aad, size_t alen, const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[16]);
int  gcm128_open(const uint8_t key[16], const uint8_t iv[12], const uint8_t *aad, size_t alen, const uint8_t *in, size_t n, const uint8_t tag[16], uint8_t *out);

/* bytes aleatorios: RDRAND (x86) mezclado con el contador de ciclos; en el host puede sustituirse con tls_rng_hook */
extern void (*tls_rng_hook)(uint8_t *p, size_t n);
void tls_random(uint8_t *p, size_t n);
#endif
