/* SOMA_194 (seguridad): validacion de certificados X.509 para el TLS 1.3 propio (tls13.c).
 *
 * Hasta ahora el cliente TLS cifraba pero no comprobaba con quien hablaba: cualquiera en la misma red podia hacerse pasar por
 * cualquier sitio. Este modulo comprueba, en cada conexion:
 *   1. que la cadena de certificados del servidor llega, firma a firma, a una autoridad de confianza (las raices de Mozilla que van
 *      en el nucleo, src/core/tls_roots.c, o las que el usuario anada en /etc/tls_confianza.pem);
 *   2. que el certificado es de ESE sitio (subjectAltName, con comodines de una etiqueta) y esta en fecha;
 *   3. que el servidor tiene la clave privada del certificado (firma CertificateVerify del saludo).
 * Algoritmos: RSA (PKCS#1 v1.5 en certificados, PSS en el saludo) hasta 4096 bits y ECDSA P-256/P-384, con SHA-256/384/512.
 * No hace: revocacion (CRL/OCSP), restricciones de nombres (nameConstraints), Ed25519, P-521, SHA-1 (se rechaza).
 * Todo lo que llega del servidor se trata como hostil: cada longitud se comprueba antes de leer (ver tests/test_x509_194.c y el
 * fuzzing del mismo archivo). La aritmetica no es de tiempo constante: solo se verifican firmas (datos publicos). */
#include "x509.h"
#include "tlscrypto.h"

static void mcpy(void *d, const void *s, size_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }
static int mcmp(const void *a, const void *b, size_t n) { const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b; while (n--) { if (*x != *y) return *x < *y ? -1 : 1; x++; y++; } return 0; }
static void mzero(void *d, size_t n) { uint8_t *a = (uint8_t *)d; while (n--) *a++ = 0; }

const char *x509_err_name(int e)
{
    switch (e) {
    case X509_OK: return "certificado valido";
    case X509_E_FORMAT: return "certificado mal formado";
    case X509_E_ALG: return "el certificado usa un algoritmo no admitido";
    case X509_E_SIG: return "una firma de la cadena de certificados no es valida";
    case X509_E_UNTRUSTED: return "el certificado no lo firma ninguna autoridad de confianza";
    case X509_E_EXPIRED: return "el certificado esta caducado o aun no es valido (comprueba la fecha del equipo)";
    case X509_E_HOST: return "el certificado es de otro sitio";
    case X509_E_NOTCA: return "un certificado intermedio no es de autoridad";
    case X509_E_TOOLONG: return "cadena de certificados demasiado larga";
    case X509_E_CV: return "el servidor no demuestra tener la clave del certificado";
    default: return "error de certificado";
    }
}

/* ================================================================ SHA-512 / SHA-384 (FIPS 180-4) */
static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};
static const uint64_t H512_INIT[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};
static const uint64_t H384_INIT[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};
struct sha512_ctx { uint64_t h[8]; uint64_t len; uint8_t buf[128]; uint32_t n; };
static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
static void sha512_block(struct sha512_ctx *c, const uint8_t *p)
{
    uint64_t w[80], s[8];
    for (int i = 0; i < 16; i++) { uint64_t v = 0; for (int k = 0; k < 8; k++) v = (v << 8) | p[8 * i + k]; w[i] = v; }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7), s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 8; i++) s[i] = c->h[i];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(s[4], 14) ^ rotr64(s[4], 18) ^ rotr64(s[4], 41), ch = (s[4] & s[5]) ^ (~s[4] & s[6]);
        uint64_t t1 = s[7] + S1 + ch + K512[i] + w[i];
        uint64_t S0 = rotr64(s[0], 28) ^ rotr64(s[0], 34) ^ rotr64(s[0], 39), mj = (s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]);
        uint64_t t2 = S0 + mj;
        s[7] = s[6]; s[6] = s[5]; s[5] = s[4]; s[4] = s[3] + t1; s[3] = s[2]; s[2] = s[1]; s[1] = s[0]; s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) c->h[i] += s[i];
}
static void sha512_start(struct sha512_ctx *c, const uint64_t *iv) { for (int i = 0; i < 8; i++) c->h[i] = iv[i]; c->len = 0; c->n = 0; }
static void sha512_update(struct sha512_ctx *c, const void *d, size_t n)
{
    const uint8_t *p = (const uint8_t *)d; c->len += n;
    while (n) {
        uint32_t k = 128 - c->n; if (k > n) k = (uint32_t)n;
        mcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
        if (c->n == 128) { sha512_block(c, c->buf); c->n = 0; }
    }
}
static void sha512_finish(struct sha512_ctx *c, uint8_t *out, int outlen)
{
    uint64_t bits = c->len * 8;
    c->buf[c->n++] = 0x80;
    if (c->n > 112) { while (c->n < 128) c->buf[c->n++] = 0; sha512_block(c, c->buf); c->n = 0; }
    while (c->n < 120) c->buf[c->n++] = 0;
    for (int i = 0; i < 8; i++) c->buf[120 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha512_block(c, c->buf);
    for (int i = 0; i < outlen; i++) out[i] = (uint8_t)(c->h[i / 8] >> (56 - 8 * (i % 8)));
}
void sha512(const void *p, size_t n, uint8_t out[64]) { struct sha512_ctx c; sha512_start(&c, H512_INIT); sha512_update(&c, p, n); sha512_finish(&c, out, 64); }
void sha384(const void *p, size_t n, uint8_t out[48]) { struct sha512_ctx c; sha512_start(&c, H384_INIT); sha512_update(&c, p, n); sha512_finish(&c, out, 48); }

/* ---------------------------------------------------------------- huella generica */
enum { H_SHA256 = 1, H_SHA384, H_SHA512 };
struct hctx { int alg; struct sha256_ctx s256; struct sha512_ctx s512; };
static uint32_t h_len(int alg) { return alg == H_SHA256 ? 32u : alg == H_SHA384 ? 48u : 64u; }
static void h_init(struct hctx *h, int alg) { h->alg = alg; if (alg == H_SHA256) sha256_init(&h->s256); else sha512_start(&h->s512, alg == H_SHA384 ? H384_INIT : H512_INIT); }
static void h_update(struct hctx *h, const void *p, size_t n) { if (h->alg == H_SHA256) sha256_update(&h->s256, p, n); else sha512_update(&h->s512, p, n); }
static void h_final(struct hctx *h, uint8_t *out) { if (h->alg == H_SHA256) sha256_final(&h->s256, out); else sha512_finish(&h->s512, out, (int)h_len(h->alg)); }
static void h_all(int alg, const void *p, size_t n, uint8_t *out) { struct hctx h; h_init(&h, alg); h_update(&h, p, n); h_final(&h, out); }

/* ================================================================ enteros grandes (Montgomery, palabras de 32 bits) */
#define BN_L 129                       /* 4096 bits + 1 palabra */
struct mont { uint32_t n[BN_L], r2[BN_L], one[BN_L]; uint32_t n0; int len; uint32_t bits; };

static int bn_cmp(const uint32_t *a, const uint32_t *b, int len) { for (int i = len - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }
static uint32_t bn_sub(uint32_t *o, const uint32_t *a, const uint32_t *b, int len)
{ uint64_t br = 0; for (int i = 0; i < len; i++) { uint64_t v = (uint64_t)a[i] - b[i] - br; o[i] = (uint32_t)v; br = (v >> 32) & 1; } return (uint32_t)br; }
static uint32_t bn_add(uint32_t *o, const uint32_t *a, const uint32_t *b, int len)
{ uint64_t c = 0; for (int i = 0; i < len; i++) { uint64_t v = (uint64_t)a[i] + b[i] + c; o[i] = (uint32_t)v; c = v >> 32; } return (uint32_t)c; }
static int bn_is_zero(const uint32_t *a, int len) { for (int i = 0; i < len; i++) if (a[i]) return 0; return 1; }
/* big-endian -> palabras (len palabras). 0 si no cabe. */
static int bn_from_be(uint32_t *o, int len, const uint8_t *p, uint32_t plen)
{
    while (plen && !*p) { p++; plen--; }
    if (plen > (uint32_t)len * 4) return 0;
    for (int i = 0; i < len; i++) o[i] = 0;
    for (uint32_t i = 0; i < plen; i++) o[i / 4] |= (uint32_t)p[plen - 1 - i] << (8 * (i % 4));
    return 1;
}
static void bn_to_be(const uint32_t *a, int len, uint8_t *o, uint32_t olen)
{ for (uint32_t i = 0; i < olen; i++) { uint32_t w = i / 4; o[olen - 1 - i] = w < (uint32_t)len ? (uint8_t)(a[w] >> (8 * (i % 4))) : 0; } }

static void mont_mul(const struct mont *m, const uint32_t *a, const uint32_t *b, uint32_t *o)
{
    uint32_t t[BN_L + 2]; int len = m->len;
    for (int i = 0; i < BN_L + 2; i++) t[i] = 0;                        /* SOMA_197: entero (el compilador no puede probar que len + 2 basta) */
    for (int i = 0; i < len; i++) {
        uint64_t c = 0, v;
        for (int j = 0; j < len; j++) { v = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + c; t[j] = (uint32_t)v; c = v >> 32; }
        v = (uint64_t)t[len] + c; t[len] = (uint32_t)v; t[len + 1] = (uint32_t)(v >> 32);
        uint32_t q = t[0] * m->n0;
        v = (uint64_t)t[0] + (uint64_t)q * m->n[0]; c = v >> 32;
        for (int j = 1; j < len; j++) { v = (uint64_t)t[j] + (uint64_t)q * m->n[j] + c; t[j - 1] = (uint32_t)v; c = v >> 32; }
        v = (uint64_t)t[len] + c; t[len - 1] = (uint32_t)v; t[len] = t[len + 1] + (uint32_t)(v >> 32);
    }
    if (t[len] || bn_cmp(t, m->n, len) >= 0) bn_sub(t, t, m->n, len);
    for (int i = 0; i < len; i++) o[i] = t[i];
}
/* Prepara el modulo (impar, > 2, hasta 4096 bits). 0 si no vale. */
static int mont_init(struct mont *m, const uint8_t *be, uint32_t belen)
{
    while (belen && !*be) { be++; belen--; }
    if (belen == 0 || belen > 512) return 0;
    m->len = (int)((belen + 3) / 4);
    bn_from_be(m->n, m->len, be, belen);
    if (!(m->n[0] & 1) || (m->len == 1 && m->n[0] < 3)) return 0;
    m->bits = belen * 8; for (uint8_t top = be[0]; !(top & 0x80); top <<= 1) m->bits--;
    uint32_t inv = 1; for (int i = 0; i < 6; i++) inv *= 2u - m->n[0] * inv;
    m->n0 = 0u - inv;
    uint32_t x[BN_L]; for (int i = 0; i < m->len; i++) x[i] = 0;
    x[0] = 1;
    for (int k = 0; k < 64 * m->len; k++) {                      /* x = 2^(64*len) mod n = R^2 mod n */
        uint32_t carry = 0;
        for (int i = 0; i < m->len; i++) { uint32_t nc = x[i] >> 31; x[i] = (x[i] << 1) | carry; carry = nc; }
        if (carry || bn_cmp(x, m->n, m->len) >= 0) bn_sub(x, x, m->n, m->len);
    }
    for (int i = 0; i < m->len; i++) m->r2[i] = x[i];
    uint32_t one[BN_L]; for (int i = 0; i < m->len; i++) one[i] = 0;
    one[0] = 1;
    mont_mul(m, one, m->r2, m->one);
    return 1;
}
/* o = base^exp mod n (base y o en forma normal, base < n; exp en big-endian) */
static void mont_exp(const struct mont *m, const uint32_t *base, const uint8_t *exp, uint32_t elen, uint32_t *o)
{
    uint32_t x[BN_L], acc[BN_L], one[BN_L];
    mont_mul(m, base, m->r2, x);
    for (int i = 0; i < m->len; i++) { acc[i] = m->one[i]; one[i] = 0; }
    one[0] = 1;
    for (uint32_t i = 0; i < elen; i++)
        for (int b = 7; b >= 0; b--) { mont_mul(m, acc, acc, acc); if ((exp[i] >> b) & 1) mont_mul(m, acc, x, acc); }
    mont_mul(m, acc, one, o);
}

/* ================================================================ RSA (solo verificacion) */
static const uint8_t DI_SHA256[] = { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
static const uint8_t DI_SHA384[] = { 0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 };
static const uint8_t DI_SHA512[] = { 0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40 };

/* em = firma^e mod n, en *klen bytes. 0 si la clave o la firma no valen. */
static int rsa_public(const struct x509_key *k, const uint8_t *sig, uint32_t siglen, uint8_t *em, uint32_t *klen, uint32_t *bits)
{
    struct mont m; uint32_t s[BN_L], r[BN_L];
    const uint8_t *e = k->b; uint32_t elen = k->blen;
    while (elen && !*e) { e++; elen--; }
    if (elen == 0 || elen > 4 || !(e[elen - 1] & 1) || (elen == 1 && e[0] < 3)) return 0;
    if (!mont_init(&m, k->a, k->alen) || m.bits < 2000) return 0;                   /* menos de 2048 bits: no */
    *klen = (m.bits + 7) / 8; *bits = m.bits;
    if (siglen != *klen || !bn_from_be(s, m.len, sig, siglen) || bn_cmp(s, m.n, m.len) >= 0) return 0;
    mont_exp(&m, s, e, elen, r);
    bn_to_be(r, m.len, em, *klen);
    return 1;
}
static int rsa_pkcs1_verify(const struct x509_key *k, int alg, const uint8_t *hash, const uint8_t *sig, uint32_t siglen)
{
    uint8_t em[512]; uint32_t klen, bits, hl = h_len(alg);
    const uint8_t *di = alg == H_SHA256 ? DI_SHA256 : alg == H_SHA384 ? DI_SHA384 : DI_SHA512;
    if (!rsa_public(k, sig, siglen, em, &klen, &bits)) return 0;
    uint32_t tl = 19 + hl;
    if (klen < tl + 11) return 0;
    uint8_t bad = em[0] | (uint8_t)(em[1] ^ 1);
    uint32_t ps = klen - tl - 3;
    for (uint32_t i = 0; i < ps; i++) bad |= (uint8_t)(em[2 + i] ^ 0xFF);
    bad |= em[2 + ps];
    for (uint32_t i = 0; i < 19; i++) bad |= (uint8_t)(em[3 + ps + i] ^ di[i]);
    for (uint32_t i = 0; i < hl; i++) bad |= (uint8_t)(em[3 + ps + 19 + i] ^ hash[i]);
    return bad == 0;
}
/* RSASSA-PSS (RFC 8017, 9.1.2), MGF1 con la misma huella y sal de la longitud de la huella (lo que fija TLS 1.3). */
static int rsa_pss_verify(const struct x509_key *k, int alg, const uint8_t *mhash, const uint8_t *sig, uint32_t siglen)
{
    uint8_t em0[512], db[512], hp[64]; uint32_t klen, bits, hl = h_len(alg);
    if (!rsa_public(k, sig, siglen, em0, &klen, &bits)) return 0;
    uint32_t embits = bits - 1, emlen = (embits + 7) / 8;
    const uint8_t *em = em0;
    if (emlen < klen) { if (em0[0]) return 0; em = em0 + 1; }
    if (emlen < 2 * hl + 2 || em[emlen - 1] != 0xBC) return 0;
    uint32_t dblen = emlen - hl - 1; const uint8_t *H = em + dblen;
    uint8_t topmask = (uint8_t)(0xFF >> (8 * emlen - embits));
    if (em[0] & (uint8_t)~topmask) return 0;
    for (uint32_t ctr = 0, off = 0; off < dblen; ctr++) {                             /* MGF1 */
        struct hctx h; uint8_t c4[4] = { (uint8_t)(ctr >> 24), (uint8_t)(ctr >> 16), (uint8_t)(ctr >> 8), (uint8_t)ctr }, d[64];
        h_init(&h, alg); h_update(&h, H, hl); h_update(&h, c4, 4); h_final(&h, d);
        for (uint32_t i = 0; i < hl && off < dblen; i++, off++) db[off] = (uint8_t)(em[off] ^ d[i]);
    }
    db[0] &= topmask;
    uint8_t bad = 0;
    for (uint32_t i = 0; i + hl + 1 < dblen; i++) bad |= db[i];
    bad |= (uint8_t)(db[dblen - hl - 1] ^ 1);
    if (bad) return 0;
    struct hctx h; static const uint8_t z8[8] = { 0 };
    h_init(&h, alg); h_update(&h, z8, 8); h_update(&h, mhash, hl); h_update(&h, db + dblen - hl, hl); h_final(&h, hp);
    return mcmp(hp, H, hl) == 0;
}

/* ================================================================ ECDSA P-256 / P-384 (solo verificacion) */
#define EC_L 13
struct curve { uint32_t bytes; const uint8_t *p, *b, *gx, *gy, *n; };
static const uint8_t P256_P[] = { 0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t P256_B[] = { 0x5A,0xC6,0x35,0xD8,0xAA,0x3A,0x93,0xE7,0xB3,0xEB,0xBD,0x55,0x76,0x98,0x86,0xBC,0x65,0x1D,0x06,0xB0,0xCC,0x53,0xB0,0xF6,0x3B,0xCE,0x3C,0x3E,0x27,0xD2,0x60,0x4B };
static const uint8_t P256_GX[] = { 0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96 };
static const uint8_t P256_GY[] = { 0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5 };
static const uint8_t P256_N[] = { 0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51 };
static const uint8_t P384_P[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF };
static const uint8_t P384_B[] = { 0xB3,0x31,0x2F,0xA7,0xE2,0x3E,0xE7,0xE4,0x98,0x8E,0x05,0x6B,0xE3,0xF8,0x2D,0x19,0x18,0x1D,0x9C,0x6E,0xFE,0x81,0x41,0x12,0x03,0x14,0x08,0x8F,0x50,0x13,0x87,0x5A,0xC6,0x56,0x39,0x8D,0x8A,0x2E,0xD1,0x9D,0x2A,0x85,0xC8,0xED,0xD3,0xEC,0x2A,0xEF };
static const uint8_t P384_GX[] = { 0xAA,0x87,0xCA,0x22,0xBE,0x8B,0x05,0x37,0x8E,0xB1,0xC7,0x1E,0xF3,0x20,0xAD,0x74,0x6E,0x1D,0x3B,0x62,0x8B,0xA7,0x9B,0x98,0x59,0xF7,0x41,0xE0,0x82,0x54,0x2A,0x38,0x55,0x02,0xF2,0x5D,0xBF,0x55,0x29,0x6C,0x3A,0x54,0x5E,0x38,0x72,0x76,0x0A,0xB7 };
static const uint8_t P384_GY[] = { 0x36,0x17,0xDE,0x4A,0x96,0x26,0x2C,0x6F,0x5D,0x9E,0x98,0xBF,0x92,0x92,0xDC,0x29,0xF8,0xF4,0x1D,0xBD,0x28,0x9A,0x14,0x7C,0xE9,0xDA,0x31,0x13,0xB5,0xF0,0xB8,0xC0,0x0A,0x60,0xB1,0xCE,0x1D,0x7E,0x81,0x9D,0x7A,0x43,0x1D,0x7C,0x90,0xEA,0x0E,0x5F };
static const uint8_t P384_N[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC7,0x63,0x4D,0x81,0xF4,0x37,0x2D,0xDF,0x58,0x1A,0x0D,0xB2,0x48,0xB0,0xA7,0x7A,0xEC,0xEC,0x19,0x6A,0xCC,0xC5,0x29,0x73 };
static const struct curve CURVE_P256 = { 32, P256_P, P256_B, P256_GX, P256_GY, P256_N };
static const struct curve CURVE_P384 = { 48, P384_P, P384_B, P384_GX, P384_GY, P384_N };

struct ecpt { uint32_t x[EC_L], y[EC_L], z[EC_L]; int inf; };       /* Jacobianas, en forma de Montgomery */
static void f_add(const struct mont *m, uint32_t *o, const uint32_t *a, const uint32_t *b) { if (bn_add(o, a, b, m->len) || bn_cmp(o, m->n, m->len) >= 0) bn_sub(o, o, m->n, m->len); }
static void f_sub(const struct mont *m, uint32_t *o, const uint32_t *a, const uint32_t *b) { if (bn_sub(o, a, b, m->len)) bn_add(o, o, m->n, m->len); }
static void f_cpy(const struct mont *m, uint32_t *o, const uint32_t *a) { for (int i = 0; i < m->len; i++) o[i] = a[i]; }

static void ec_dbl(const struct mont *m, struct ecpt *r, const struct ecpt *p)
{
    if (p->inf || bn_is_zero(p->y, m->len)) { r->inf = 1; return; }
    uint32_t d[EC_L], g[EC_L], be[EC_L], al[EC_L], t[EC_L], u[EC_L];
    mont_mul(m, p->z, p->z, d); mont_mul(m, p->y, p->y, g); mont_mul(m, p->x, g, be);
    f_sub(m, t, p->x, d); f_add(m, u, p->x, d); mont_mul(m, t, u, al); f_add(m, t, al, al); f_add(m, al, t, al);     /* alfa = 3(X-d)(X+d) */
    f_add(m, t, p->y, p->z); mont_mul(m, t, t, t); f_sub(m, t, t, g); f_sub(m, t, t, d);                            /* Z3 */
    uint32_t b4[EC_L], b8[EC_L], x3[EC_L];
    f_add(m, b4, be, be); f_add(m, b4, b4, b4); f_add(m, b8, b4, b4);
    mont_mul(m, al, al, x3); f_sub(m, x3, x3, b8);
    f_sub(m, u, b4, x3); mont_mul(m, al, u, u);
    mont_mul(m, g, g, g); f_add(m, g, g, g); f_add(m, g, g, g); f_add(m, g, g, g);                                  /* 8 gamma^2 */
    f_sub(m, r->y, u, g); f_cpy(m, r->x, x3); f_cpy(m, r->z, t); r->inf = 0;
}
static void ec_add(const struct mont *m, struct ecpt *r, const struct ecpt *p, const struct ecpt *q)
{
    if (p->inf) { *r = *q; return; }
    if (q->inf) { *r = *p; return; }
    uint32_t z1z1[EC_L], z2z2[EC_L], u1[EC_L], u2[EC_L], s1[EC_L], s2[EC_L], h[EC_L], rr[EC_L], hh[EC_L], hhh[EC_L], v[EC_L], t[EC_L];
    mont_mul(m, p->z, p->z, z1z1); mont_mul(m, q->z, q->z, z2z2);
    mont_mul(m, p->x, z2z2, u1); mont_mul(m, q->x, z1z1, u2);
    mont_mul(m, p->y, q->z, s1); mont_mul(m, s1, z2z2, s1);
    mont_mul(m, q->y, p->z, s2); mont_mul(m, s2, z1z1, s2);
    f_sub(m, h, u2, u1); f_sub(m, rr, s2, s1);
    if (bn_is_zero(h, m->len)) { if (bn_is_zero(rr, m->len)) { struct ecpt c = *p; ec_dbl(m, r, &c); } else r->inf = 1; return; }
    mont_mul(m, h, h, hh); mont_mul(m, h, hh, hhh); mont_mul(m, u1, hh, v);
    uint32_t x3[EC_L], y3[EC_L], z3[EC_L];
    mont_mul(m, rr, rr, x3); f_sub(m, x3, x3, hhh); f_sub(m, x3, x3, v); f_sub(m, x3, x3, v);
    f_sub(m, t, v, x3); mont_mul(m, rr, t, y3); mont_mul(m, s1, hhh, t); f_sub(m, y3, y3, t);
    mont_mul(m, p->z, q->z, z3); mont_mul(m, z3, h, z3);
    f_cpy(m, r->x, x3); f_cpy(m, r->y, y3); f_cpy(m, r->z, z3); r->inf = 0;
}
/* 1 si (r, s) es una firma ECDSA valida de `hash` con la clave `pub` (punto 04||X||Y). */
static int ecdsa_verify(const struct curve *c, const uint8_t *pub, uint32_t publen, const uint8_t *hash, uint32_t hlen,
                        const uint8_t *rb, uint32_t rlen, const uint8_t *sb, uint32_t slen)
{
    struct mont mp, mn; uint32_t nb = c->bytes;
    if (publen != 1 + 2 * nb || pub[0] != 4) return 0;
    if (!mont_init(&mp, c->p, nb) || !mont_init(&mn, c->n, nb)) return 0;
    int L = mp.len;
    uint32_t r[EC_L], s[EC_L], e[EC_L], w[EC_L], u1[EC_L], u2[EC_L], t[EC_L];
    if (!bn_from_be(r, L, rb, rlen) || !bn_from_be(s, L, sb, slen)) return 0;
    if (bn_is_zero(r, L) || bn_is_zero(s, L) || bn_cmp(r, mn.n, L) >= 0 || bn_cmp(s, mn.n, L) >= 0) return 0;
    if (hlen > nb) hlen = nb;                                                       /* huella mas larga que la curva: sus bits de la izquierda */
    bn_from_be(e, L, hash, hlen);
    if (bn_cmp(e, mn.n, L) >= 0) bn_sub(e, e, mn.n, L);
    /* clave publica: coordenadas < p y sobre la curva y^2 = x^3 - 3x + b */
    struct ecpt G, Q, GQ, R;
    uint32_t qx[EC_L], qy[EC_L], gx[EC_L], gy[EC_L], bb[EC_L];
    if (!bn_from_be(qx, L, pub + 1, nb) || !bn_from_be(qy, L, pub + 1 + nb, nb) || bn_cmp(qx, mp.n, L) >= 0 || bn_cmp(qy, mp.n, L) >= 0) return 0;
    bn_from_be(gx, L, c->gx, nb); bn_from_be(gy, L, c->gy, nb); bn_from_be(bb, L, c->b, nb);
    mont_mul(&mp, qx, mp.r2, Q.x); mont_mul(&mp, qy, mp.r2, Q.y); f_cpy(&mp, Q.z, mp.one); Q.inf = 0;
    mont_mul(&mp, gx, mp.r2, G.x); mont_mul(&mp, gy, mp.r2, G.y); f_cpy(&mp, G.z, mp.one); G.inf = 0;
    mont_mul(&mp, bb, mp.r2, bb);
    {
        uint32_t lhs[EC_L], rhs[EC_L], x3[EC_L];
        mont_mul(&mp, Q.y, Q.y, lhs);
        mont_mul(&mp, Q.x, Q.x, x3); mont_mul(&mp, x3, Q.x, x3);
        f_sub(&mp, rhs, x3, Q.x); f_sub(&mp, rhs, rhs, Q.x); f_sub(&mp, rhs, rhs, Q.x); f_add(&mp, rhs, rhs, bb);
        if (bn_cmp(lhs, rhs, L) != 0) return 0;
    }
    /* w = s^-1 mod n (Fermat); u1 = e w; u2 = r w */
    uint8_t ex[48]; mcpy(ex, c->n, nb); ex[nb - 1] = (uint8_t)(ex[nb - 1] - 2);
    mont_exp(&mn, s, ex, nb, w);
    mont_mul(&mn, e, mn.r2, t); mont_mul(&mn, t, w, u1);
    mont_mul(&mn, r, mn.r2, t); mont_mul(&mn, t, w, u2);
    ec_add(&mp, &GQ, &G, &Q);
    R.inf = 1;
    for (int i = (int)nb * 8 - 1; i >= 0; i--) {
        struct ecpt d = R; ec_dbl(&mp, &R, &d);
        int b1 = (u1[i / 32] >> (i % 32)) & 1, b2 = (u2[i / 32] >> (i % 32)) & 1;
        if (b1 | b2) { struct ecpt a = R; ec_add(&mp, &R, &a, b1 && b2 ? &GQ : b1 ? &G : &Q); }
    }
    if (R.inf) return 0;
    /* x afin = X / Z^2 */
    uint32_t zn[EC_L], zi[EC_L], x[EC_L], one[EC_L];
    for (int i = 0; i < L; i++) one[i] = 0;
    one[0] = 1;
    mont_mul(&mp, R.z, one, zn);
    mcpy(ex, c->p, nb); ex[nb - 1] = (uint8_t)(ex[nb - 1] - 2);
    mont_exp(&mp, zn, ex, nb, zi);                                                  /* Z^-1, forma normal */
    mont_mul(&mp, zi, mp.r2, zi); mont_mul(&mp, zi, zi, zi);                        /* Z^-2, Montgomery */
    mont_mul(&mp, R.x, zi, x); mont_mul(&mp, x, one, x);
    if (bn_cmp(x, mn.n, L) >= 0) bn_sub(x, x, mn.n, L);
    return bn_cmp(x, r, L) == 0;
}

/* ================================================================ DER */
struct der { const uint8_t *p, *end; };
/* Lee un elemento: etiqueta y contenido. 1 = bien. Solo longitudes definidas de hasta 4 bytes y etiquetas de un byte. */
static int der_get(struct der *d, uint8_t *tag, struct der *val)
{
    if (d->end - d->p < 2) return 0;
    uint8_t t = d->p[0], l0 = d->p[1]; const uint8_t *q = d->p + 2; uint32_t len;
    if ((t & 0x1F) == 0x1F) return 0;
    if (l0 < 0x80) len = l0;
    else {
        uint32_t nb = l0 & 0x7F;
        if (nb == 0 || nb > 4 || (uint32_t)(d->end - q) < nb) return 0;
        len = 0; for (uint32_t i = 0; i < nb; i++) len = (len << 8) | *q++;
    }
    if (len > (uint32_t)(d->end - q)) return 0;
    *tag = t; val->p = q; val->end = q + len; d->p = q + len;
    return 1;
}
static int der_expect(struct der *d, uint8_t tag, struct der *val) { uint8_t t; return der_get(d, &t, val) && t == tag; }
static uint32_t der_len(const struct der *d) { return (uint32_t)(d->end - d->p); }
static int der_is(const struct der *d, const uint8_t *oid, uint32_t n) { return der_len(d) == n && mcmp(d->p, oid, n) == 0; }

static const uint8_t OID_RSA[]        = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
static const uint8_t OID_RSA_SHA256[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B };
static const uint8_t OID_RSA_SHA384[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C };
static const uint8_t OID_RSA_SHA512[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D };
static const uint8_t OID_EC_SHA256[]  = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02 };
static const uint8_t OID_EC_SHA384[]  = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03 };
static const uint8_t OID_EC_SHA512[]  = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x04 };
static const uint8_t OID_EC_PUB[]     = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01 };
static const uint8_t OID_P256[]       = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };
static const uint8_t OID_P384[]       = { 0x2B, 0x81, 0x04, 0x00, 0x22 };
static const uint8_t OID_BASIC[]      = { 0x55, 0x1D, 0x13 };
static const uint8_t OID_SAN[]        = { 0x55, 0x1D, 0x11 };

/* ================================================================ X.509 */
enum { SIG_NONE = 0, SIG_RSA, SIG_EC };
struct x509 {
    struct der tbs, issuer, subject, spki, sig, san;      /* tbs/issuer/subject/spki: elemento completo (con cabecera) */
    int sig_kind, sig_hash, is_ca, has_san;
    uint64_t not_before, not_after;
    struct x509_key key;
};

/* SubjectPublicKeyInfo (elemento completo) -> clave. X509_OK, X509_E_FORMAT o X509_E_ALG. */
static int spki_parse(struct der full, struct x509_key *k)
{
    struct der spki, alg, oid, bits; uint8_t t;
    k->type = X509_K_NONE;
    if (!der_expect(&full, 0x30, &spki) || !der_expect(&spki, 0x30, &alg) || !der_expect(&alg, 0x06, &oid) || !der_expect(&spki, 0x03, &bits)) return X509_E_FORMAT;
    if (der_len(&bits) < 2 || bits.p[0] != 0) return X509_E_FORMAT;
    bits.p++;
    if (der_is(&oid, OID_RSA, sizeof OID_RSA)) {
        struct der seq, n, e;
        if (!der_expect(&bits, 0x30, &seq) || !der_expect(&seq, 0x02, &n) || !der_expect(&seq, 0x02, &e) || !der_len(&n) || !der_len(&e)) return X509_E_FORMAT;
        if ((n.p[0] & 0x80) || (e.p[0] & 0x80)) return X509_E_FORMAT;               /* negativos */
        k->type = X509_K_RSA; k->a = n.p; k->alen = der_len(&n); k->b = e.p; k->blen = der_len(&e);
        return X509_OK;
    }
    if (der_is(&oid, OID_EC_PUB, sizeof OID_EC_PUB)) {
        struct der cv;
        if (!der_get(&alg, &t, &cv) || t != 0x06) return X509_E_ALG;
        if (der_is(&cv, OID_P256, sizeof OID_P256) && der_len(&bits) == 65) k->type = X509_K_P256;
        else if (der_is(&cv, OID_P384, sizeof OID_P384) && der_len(&bits) == 97) k->type = X509_K_P384;
        else return X509_E_ALG;
        k->a = bits.p; k->alen = der_len(&bits); k->b = 0; k->blen = 0;
        return X509_OK;
    }
    return X509_E_ALG;
}

static int two(const uint8_t *p) { return (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') ? (p[0] - '0') * 10 + (p[1] - '0') : -1; }
/* UTCTime (AAMMDDHHMMSSZ) o GeneralizedTime (AAAAMMDDHHMMSSZ) -> segundos Unix. 0 = no valido. */
static uint64_t der_time(uint8_t tag, const struct der *v)
{
    const uint8_t *p = v->p; uint32_t n = der_len(v); int y;
    if (tag == 0x17 && n == 13) { y = two(p); if (y < 0) return 0; y += y < 50 ? 2000 : 1900; p += 2; }
    else if (tag == 0x18 && n == 15) { int a = two(p), b = two(p + 2); if (a < 0 || b < 0) return 0; y = a * 100 + b; p += 4; }
    else return 0;
    int mo = two(p), d = two(p + 2), h = two(p + 4), mi = two(p + 6), s = two(p + 8);
    if (p[10] != 'Z' || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60 || y < 1970) return 0;
    int64_t yy = y - (mo <= 2), era = yy / 400, yoe = yy - era * 400;                /* dias desde 1970 (algoritmo civil) */
    int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return (uint64_t)days * 86400u + (uint64_t)h * 3600u + (uint64_t)mi * 60u + (uint64_t)s;
}

static int x509_parse(const uint8_t *cert, uint32_t len, struct x509 *c)
{
    struct der top = { cert, cert + len }, body, tbs, alg, oid, sig, v; uint8_t t;
    mzero(c, sizeof *c);
    if (!der_expect(&top, 0x30, &body)) return X509_E_FORMAT;
    c->tbs.p = body.p;
    if (!der_expect(&body, 0x30, &tbs)) return X509_E_FORMAT;
    c->tbs.end = body.p;
    if (!der_expect(&body, 0x30, &alg) || !der_expect(&alg, 0x06, &oid) || !der_expect(&body, 0x03, &sig)) return X509_E_FORMAT;
    if (der_len(&sig) < 2 || sig.p[0] != 0) return X509_E_FORMAT;
    c->sig.p = sig.p + 1; c->sig.end = sig.end;
    if      (der_is(&oid, OID_RSA_SHA256, 9)) { c->sig_kind = SIG_RSA; c->sig_hash = H_SHA256; }
    else if (der_is(&oid, OID_RSA_SHA384, 9)) { c->sig_kind = SIG_RSA; c->sig_hash = H_SHA384; }
    else if (der_is(&oid, OID_RSA_SHA512, 9)) { c->sig_kind = SIG_RSA; c->sig_hash = H_SHA512; }
    else if (der_is(&oid, OID_EC_SHA256, 8))  { c->sig_kind = SIG_EC;  c->sig_hash = H_SHA256; }
    else if (der_is(&oid, OID_EC_SHA384, 8))  { c->sig_kind = SIG_EC;  c->sig_hash = H_SHA384; }
    else if (der_is(&oid, OID_EC_SHA512, 8))  { c->sig_kind = SIG_EC;  c->sig_hash = H_SHA512; }
    else c->sig_kind = SIG_NONE;                                                     /* SHA-1, PSS en certificados, Ed25519...: no admitido */
    /* tbsCertificate */
    const uint8_t *save = tbs.p;
    if (!der_get(&tbs, &t, &v)) return X509_E_FORMAT;
    if (t == 0xA0) { if (!der_expect(&tbs, 0x02, &v)) return X509_E_FORMAT; }        /* version explicita; lo siguiente es el numero de serie */
    else if (t != 0x02) return X509_E_FORMAT;
    (void)save;
    if (!der_expect(&tbs, 0x30, &v)) return X509_E_FORMAT;                           /* algoritmo de firma (repetido) */
    c->issuer.p = tbs.p; if (!der_expect(&tbs, 0x30, &v)) return X509_E_FORMAT; c->issuer.end = tbs.p;
    struct der val, tm;
    if (!der_expect(&tbs, 0x30, &val)) return X509_E_FORMAT;
    if (!der_get(&val, &t, &tm) || !(c->not_before = der_time(t, &tm))) return X509_E_FORMAT;
    if (!der_get(&val, &t, &tm) || !(c->not_after = der_time(t, &tm))) return X509_E_FORMAT;
    c->subject.p = tbs.p; if (!der_expect(&tbs, 0x30, &v)) return X509_E_FORMAT; c->subject.end = tbs.p;
    c->spki.p = tbs.p; if (!der_expect(&tbs, 0x30, &v)) return X509_E_FORMAT; c->spki.end = tbs.p;
    int kr = spki_parse(c->spki, &c->key);
    if (kr == X509_E_FORMAT) return kr;
    /* extensiones: [3] EXPLICIT SEQUENCE OF SEQUENCE { OID, [BOOLEAN], OCTET STRING } */
    while (der_len(&tbs)) {
        struct der ex, exts;
        if (!der_get(&tbs, &t, &ex)) return X509_E_FORMAT;
        if (t != 0xA3) continue;
        if (!der_expect(&ex, 0x30, &exts)) return X509_E_FORMAT;
        while (der_len(&exts)) {
            struct der one, eo, ev;
            if (!der_expect(&exts, 0x30, &one) || !der_expect(&one, 0x06, &eo) || !der_get(&one, &t, &ev)) return X509_E_FORMAT;
            if (t == 0x01) { if (!der_get(&one, &t, &ev)) return X509_E_FORMAT; }     /* critical */
            if (t != 0x04) return X509_E_FORMAT;
            if (der_is(&eo, OID_BASIC, 3)) {
                struct der bc, b;
                if (!der_expect(&ev, 0x30, &bc)) return X509_E_FORMAT;
                if (der_len(&bc) && bc.p[0] == 0x01) { if (!der_get(&bc, &t, &b) || der_len(&b) != 1) return X509_E_FORMAT; c->is_ca = b.p[0] != 0; }
            } else if (der_is(&eo, OID_SAN, 3)) {
                if (!der_expect(&ev, 0x30, &c->san)) return X509_E_FORMAT;
                c->has_san = 1;
            }
        }
    }
    return X509_OK;
}

static char lc(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }
/* Compara un nombre del certificado (puede empezar por "*.": cubre exactamente una etiqueta) con el host. */
int x509_host_match(const char *pat, uint32_t plen, const char *host)
{
    uint32_t hl = 0; while (host[hl]) hl++;
    while (hl && host[hl - 1] == '.') hl--;
    if (!plen || !hl) return 0;
    for (uint32_t i = 0; i < plen; i++) if (pat[i] == 0) return 0;                    /* un NUL dentro del nombre: truco clasico */
    if (plen > 2 && pat[0] == '*' && pat[1] == '.') {
        int dots = 0; for (uint32_t i = 2; i < plen; i++) if (pat[i] == '.') dots++;
        if (!dots) return 0;                                                          /* "*.com": no */
        uint32_t fl = 0; while (fl < hl && host[fl] != '.') fl++;
        if (fl == 0 || fl == hl) return 0;
        pat++; plen--; host += fl; hl -= fl;
    }
    if (plen != hl) return 0;
    for (uint32_t i = 0; i < plen; i++) { if (pat[i] == '*') return 0; if (lc(pat[i]) != lc(host[i])) return 0; }
    return 1;
}
static int host_is_ipv4(const char *h, uint8_t ip[4])
{
    for (int k = 0; k < 4; k++) {
        uint32_t v = 0; int nd = 0;
        while (*h >= '0' && *h <= '9' && nd < 4) { v = v * 10 + (uint32_t)(*h - '0'); h++; nd++; }
        if (!nd || nd > 3 || v > 255) return 0;
        ip[k] = (uint8_t)v;
        if (k < 3) { if (*h != '.') return 0; h++; }
    }
    return *h == 0;
}
static int x509_host_ok(const struct x509 *c, const char *host)
{
    if (!c->has_san) return 0;                                                        /* sin subjectAltName no se acepta (el CN ya no cuenta) */
    uint8_t ip[4]; int is_ip = host_is_ipv4(host, ip);
    struct der s = c->san, v; uint8_t t;
    while (der_len(&s)) {
        if (!der_get(&s, &t, &v)) return 0;
        if (!is_ip && t == 0x82 && x509_host_match((const char *)v.p, der_len(&v), host)) return 1;
        if (is_ip && t == 0x87 && der_len(&v) == 4 && mcmp(v.p, ip, 4) == 0) return 1;
    }
    return 0;
}

/* firma del certificado `c` con la clave `k` */
static int x509_sig_ok(const struct x509 *c, const struct x509_key *k)
{
    uint8_t h[64];
    if (c->sig_kind == SIG_NONE || k->type == X509_K_NONE) return 0;
    h_all(c->sig_hash, c->tbs.p, der_len(&c->tbs), h);
    if (c->sig_kind == SIG_RSA) return k->type == X509_K_RSA && rsa_pkcs1_verify(k, c->sig_hash, h, c->sig.p, der_len(&c->sig));
    if (k->type != X509_K_P256 && k->type != X509_K_P384) return 0;
    struct der d = c->sig, seq, r, s;
    if (!der_expect(&d, 0x30, &seq) || !der_expect(&seq, 0x02, &r) || !der_expect(&seq, 0x02, &s) || der_len(&seq)) return 0;
    return ecdsa_verify(k->type == X509_K_P256 ? &CURVE_P256 : &CURVE_P384, k->a, k->alen, h, h_len(c->sig_hash), r.p, der_len(&r), s.p, der_len(&s));
}

/* ================================================================ autoridades de confianza */
#define USER_ANCHORS 8
static const uint8_t *g_roots; static uint32_t g_roots_len;
static struct { uint8_t dn[512], spki[600]; uint16_t dn_len, spki_len; } g_user[USER_ANCHORS];
static int g_user_n;

void x509_set_roots(const uint8_t *blob, uint32_t len) { g_roots = blob; g_roots_len = len; }
void x509_clear_anchors(void) { g_user_n = 0; }
int x509_add_anchor(const uint8_t *cert, uint32_t len)
{
    struct x509 c;
    if (x509_parse(cert, len, &c) != X509_OK || c.key.type == X509_K_NONE) return -1;
    uint32_t dl = der_len(&c.subject), sl = der_len(&c.spki);
    if (g_user_n >= USER_ANCHORS || dl > sizeof g_user[0].dn || sl > sizeof g_user[0].spki) return -1;
    mcpy(g_user[g_user_n].dn, c.subject.p, dl); g_user[g_user_n].dn_len = (uint16_t)dl;
    mcpy(g_user[g_user_n].spki, c.spki.p, sl); g_user[g_user_n].spki_len = (uint16_t)sl;
    g_user_n++;
    return 0;
}
uint32_t x509_root_count(void)
{
    uint32_t n = (uint32_t)g_user_n, off = 0;
    while (off + 4 <= g_roots_len) {
        uint32_t dl = (uint32_t)g_roots[off] << 8 | g_roots[off + 1]; if (off + 2 + dl + 2 > g_roots_len) break;
        uint32_t sl = (uint32_t)g_roots[off + 2 + dl] << 8 | g_roots[off + 3 + dl]; if (off + 4 + dl + sl > g_roots_len) break;
        off += 4 + dl + sl; n++;
    }
    return n;
}
/* 1 si alguna autoridad de confianza con el nombre del emisor de `c` firma `c`. *seen = habia alguna con ese nombre. */
static int anchor_signs(const struct x509 *c, int *seen)
{
    const uint8_t *iss = c->issuer.p; uint32_t il = der_len(&c->issuer);
    struct x509_key k; struct der sp;
    for (int i = 0; i < g_user_n; i++) {
        if (g_user[i].dn_len != il || mcmp(g_user[i].dn, iss, il)) continue;
        *seen = 1; sp.p = g_user[i].spki; sp.end = sp.p + g_user[i].spki_len;
        if (spki_parse(sp, &k) == X509_OK && x509_sig_ok(c, &k)) return 1;
    }
    uint32_t off = 0;
    while (off + 4 <= g_roots_len) {
        uint32_t dl = (uint32_t)g_roots[off] << 8 | g_roots[off + 1]; if (off + 2 + dl + 2 > g_roots_len) break;
        uint32_t sl = (uint32_t)g_roots[off + 2 + dl] << 8 | g_roots[off + 3 + dl]; if (off + 4 + dl + sl > g_roots_len) break;
        if (dl == il && mcmp(g_roots + off + 2, iss, il) == 0) {
            *seen = 1; sp.p = g_roots + off + 4 + dl; sp.end = sp.p + sl;
            if (spki_parse(sp, &k) == X509_OK && x509_sig_ok(c, &k)) return 1;
        }
        off += 4 + dl + sl;
    }
    return 0;
}

static int b64v(char ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1; }
int x509_add_anchors_pem(const char *pem, uint32_t len)
{
    static const char B[] = "-----BEGIN CERTIFICATE-----", E[] = "-----END CERTIFICATE-----";
    static uint8_t der[4096];
    int added = 0; uint32_t i = 0;
    while (i + sizeof B - 1 <= len) {
        if (mcmp(pem + i, B, sizeof B - 1)) { i++; continue; }
        i += sizeof B - 1;
        uint32_t n = 0, acc = 0; int nb = 0, ok = 0;
        while (i < len) {
            if (i + sizeof E - 1 <= len && mcmp(pem + i, E, sizeof E - 1) == 0) { ok = 1; i += sizeof E - 1; break; }
            int v = b64v(pem[i++]);
            if (v < 0) continue;                                                      /* saltos de linea, relleno '=' */
            acc = (acc << 6) | (uint32_t)v; nb += 6;
            if (nb >= 8) { nb -= 8; if (n < sizeof der) der[n++] = (uint8_t)(acc >> nb); else { ok = -1; } }
        }
        if (ok == 1 && x509_add_anchor(der, n) == 0) added++;
    }
    return added;
}

/* ================================================================ cadena de TLS 1.3 */
#define CHAIN_MAX 8
int x509_verify_tls_chain(const uint8_t *msg, uint32_t len, const char *host, uint64_t now, struct x509_key *leaf)
{
    const uint8_t *cp[CHAIN_MAX]; uint32_t cl[CHAIN_MAX]; int n = 0;
    if (len < 4 || msg[0] != 0) return X509_E_FORMAT;                                 /* certificate_request_context vacio */
    uint32_t ll = (uint32_t)msg[1] << 16 | (uint32_t)msg[2] << 8 | msg[3], off = 4;
    if (ll != len - 4) return X509_E_FORMAT;
    while (off < len) {
        if (len - off < 3) return X509_E_FORMAT;
        uint32_t l = (uint32_t)msg[off] << 16 | (uint32_t)msg[off + 1] << 8 | msg[off + 2]; off += 3;
        if (l == 0 || l > len - off) return X509_E_FORMAT;
        if (n < CHAIN_MAX) { cp[n] = msg + off; cl[n] = l; n++; }
        off += l;
        if (len - off < 2) return X509_E_FORMAT;
        uint32_t el = (uint32_t)msg[off] << 8 | msg[off + 1]; off += 2;
        if (el > len - off) return X509_E_FORMAT;
        off += el;
    }
    if (n == 0) return X509_E_FORMAT;
    struct x509 cur, cand;
    int r = x509_parse(cp[0], cl[0], &cur);
    if (r != X509_OK) return r;
    if (cur.key.type == X509_K_NONE) return X509_E_ALG;
    if (!host || !x509_host_ok(&cur, host)) return X509_E_HOST;
    if (leaf) *leaf = cur.key;
    uint8_t used[CHAIN_MAX] = { 1 };
    for (int depth = 0; depth < CHAIN_MAX; depth++) {
        if (now && (now < cur.not_before || now > cur.not_after)) return X509_E_EXPIRED;
        if (cur.sig_kind == SIG_NONE) return X509_E_ALG;
        int seen = 0, next = -1, notca = 0;
        if (anchor_signs(&cur, &seen)) return X509_OK;
        for (int i = 1; i < n && next < 0; i++) {
            if (used[i] || x509_parse(cp[i], cl[i], &cand) != X509_OK) continue;
            if (der_len(&cand.subject) != der_len(&cur.issuer) || mcmp(cand.subject.p, cur.issuer.p, der_len(&cur.issuer))) continue;
            seen = 1;
            if (!x509_sig_ok(&cur, &cand.key)) continue;
            if (!cand.is_ca) { notca = 1; continue; }
            next = i;
        }
        if (next < 0) return notca ? X509_E_NOTCA : seen ? X509_E_SIG : X509_E_UNTRUSTED;
        used[next] = 1; cur = cand;
    }
    return X509_E_TOOLONG;
}

int x509_verify_tls_cv(const struct x509_key *leaf, const uint8_t *cv, uint32_t cvlen, const uint8_t th[32])
{
    static const char ctx[] = "TLS 1.3, server CertificateVerify";
    if (cvlen < 4) return X509_E_FORMAT;
    uint32_t scheme = (uint32_t)cv[0] << 8 | cv[1], sl = (uint32_t)cv[2] << 8 | cv[3];
    if (sl != cvlen - 4) return X509_E_FORMAT;
    const uint8_t *sig = cv + 4;
    int alg = (scheme == 0x0804 || scheme == 0x0403) ? H_SHA256 : (scheme == 0x0805 || scheme == 0x0503) ? H_SHA384 : scheme == 0x0806 ? H_SHA512 : 0;
    if (!alg) return X509_E_ALG;
    uint8_t content[64 + sizeof ctx + 32], h[64];
    for (int i = 0; i < 64; i++) content[i] = 0x20;
    mcpy(content + 64, ctx, sizeof ctx);                                              /* incluye el 0 final como separador */
    mcpy(content + 64 + sizeof ctx, th, 32);
    h_all(alg, content, sizeof content, h);
    if ((scheme & 0xFF00) == 0x0800) return (leaf->type == X509_K_RSA && rsa_pss_verify(leaf, alg, h, sig, sl)) ? X509_OK : X509_E_CV;
    if ((scheme == 0x0403 && leaf->type != X509_K_P256) || (scheme == 0x0503 && leaf->type != X509_K_P384)) return X509_E_CV;
    struct der d = { sig, sig + sl }, seq, r, s;
    if (!der_expect(&d, 0x30, &seq) || !der_expect(&seq, 0x02, &r) || !der_expect(&seq, 0x02, &s) || der_len(&seq)) return X509_E_CV;
    return ecdsa_verify(leaf->type == X509_K_P256 ? &CURVE_P256 : &CURVE_P384, leaf->a, leaf->alen, h, h_len(alg), r.p, der_len(&r), s.p, der_len(&s)) ? X509_OK : X509_E_CV;
}
