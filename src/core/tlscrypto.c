/* SOMA_176A: ver tlscrypto.h */
#include "tlscrypto.h"
#include "crypto.h"
#include "rng.h"

/* ---------------------------------------------------------------- SHA-256 */
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(struct sha256_ctx *c, const uint8_t *p)
{
    uint32_t w[64], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g), t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), mj = (a & b) ^ (a & cc) ^ (b & cc), t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
void sha256_init(struct sha256_ctx *c)
{
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    for (int i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0; c->n = 0;
}
void sha256_update(struct sha256_ctx *c, const void *vp, size_t n)
{
    const uint8_t *p = (const uint8_t *)vp;
    c->len += n;
    if (c->n) { while (n && c->n < 64) { c->buf[c->n++] = *p++; n--; } if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; } }
    while (n >= 64) { sha256_block(c, p); p += 64; n -= 64; }
    while (n) c->buf[c->n++] = *p++, n--;
}
void sha256_final(struct sha256_ctx *c, uint8_t out[32])
{
    uint64_t bits = c->len * 8; uint8_t pad[72]; size_t pn = (c->n < 56 ? 56 : 120) - c->n;
    pad[0] = 0x80; for (size_t i = 1; i < pn; i++) pad[i] = 0;
    for (int i = 0; i < 8; i++) pad[pn + i] = (uint8_t)(bits >> (56 - 8 * i));
    uint64_t keep = c->len; sha256_update(c, pad, pn + 8); c->len = keep;
    for (int i = 0; i < 8; i++) { out[4 * i] = (uint8_t)(c->h[i] >> 24); out[4 * i + 1] = (uint8_t)(c->h[i] >> 16); out[4 * i + 2] = (uint8_t)(c->h[i] >> 8); out[4 * i + 3] = (uint8_t)c->h[i]; }
}
void sha256(const void *p, size_t n, uint8_t out[32]) { struct sha256_ctx c; sha256_init(&c); sha256_update(&c, p, n); sha256_final(&c, out); }

void hmac_sha256(const uint8_t *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32])
{
    uint8_t k[64], ip[64], op[64], in[32]; struct sha256_ctx c;
    for (int i = 0; i < 64; i++) k[i] = 0;
    if (klen > 64) sha256(key, klen, k); else for (size_t i = 0; i < klen; i++) k[i] = key[i];
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha256_init(&c); sha256_update(&c, ip, 64); sha256_update(&c, msg, mlen); sha256_final(&c, in);
    sha256_init(&c); sha256_update(&c, op, 64); sha256_update(&c, in, 32); sha256_final(&c, out);
}
void hkdf_extract(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen, uint8_t out[32])
{
    static const uint8_t z[32] = { 0 };
    if (!salt || !slen) { salt = z; slen = 32; }
    hmac_sha256(salt, slen, ikm, ilen, out);
}
void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *ctx, size_t clen, uint8_t *out, size_t olen)
{
    uint8_t info[2 + 1 + 6 + 12 + 1 + 64 + 1]; size_t n = 0, ll = 0;
    while (label[ll]) ll++;
    info[n++] = (uint8_t)(olen >> 8); info[n++] = (uint8_t)olen;
    info[n++] = (uint8_t)(6 + ll);
    static const char pre[6] = { 't','l','s','1','3',' ' };
    for (int i = 0; i < 6; i++) info[n++] = (uint8_t)pre[i];
    for (size_t i = 0; i < ll; i++) info[n++] = (uint8_t)label[i];
    info[n++] = (uint8_t)clen;
    for (size_t i = 0; i < clen; i++) info[n++] = ctx[i];
    info[n++] = 1;                                                      /* T(1) = HMAC(secret, info || 0x01); olen <= 32 */
    uint8_t t[32]; hmac_sha256(secret, 32, info, n, t);
    for (size_t i = 0; i < olen; i++) out[i] = t[i];
}

/* ---------------------------------------------------------------- X25519 (TweetNaCl) */
typedef int64_t gf[16];
static const gf gf121665 = { 0xDB41, 1 };
static void car25519(gf o) { int64_t c; for (int i = 0; i < 16; i++) { o[i] += (1LL << 16); c = o[i] >> 16; o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15); o[i] -= c * 65536; } }
static void sel25519(gf p, gf q, int b) { int64_t t, c = ~((int64_t)b - 1); for (int i = 0; i < 16; i++) { t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; } }
static void pack25519(uint8_t *o, const gf n)
{
    int b; gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1); m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2 * i] = (uint8_t)(t[i] & 0xff); o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}
static void unpack25519(gf o, const uint8_t *n) { for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8); o[15] &= 0x7fff; }
static void fA(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fZ(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fM(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fS(gf o, const gf a) { fM(o, a, a); }
static void inv25519(gf o, const gf i)
{
    gf c; for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) { fS(c, c); if (a != 2 && a != 4) fM(c, c, i); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t z[32]; int64_t x[80], r; gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (scalar[31] & 127) | 64; z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, (int)r); sel25519(c, d, (int)r);
        fA(e, a, c); fZ(a, a, c); fA(c, b, d); fZ(b, b, d);
        fS(d, e); fS(f, a); fM(a, c, a); fM(c, b, e);
        fA(e, a, c); fZ(a, a, c); fS(b, a); fZ(c, d, f);
        fM(a, c, gf121665); fA(a, a, d); fM(c, c, a); fM(a, d, f); fM(d, b, x); fS(b, e);
        sel25519(a, b, (int)r); sel25519(c, d, (int)r);
    }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    inv25519(x + 32, x + 32);
    fM(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
}
void x25519_base(uint8_t pub[32], const uint8_t priv[32]) { uint8_t base[32] = { 9 }; x25519(pub, priv, base); }

/* ---------------------------------------------------------------- AES-128-GCM */
struct u128 { uint64_t hi, lo; };
static struct u128 ld128(const uint8_t *p) { struct u128 r = { 0, 0 }; for (int i = 0; i < 8; i++) { r.hi = r.hi << 8 | p[i]; r.lo = r.lo << 8 | p[8 + i]; } return r; }
static void st128(uint8_t *p, struct u128 v) { for (int i = 0; i < 8; i++) { p[i] = (uint8_t)(v.hi >> (56 - 8 * i)); p[8 + i] = (uint8_t)(v.lo >> (56 - 8 * i)); } }
static struct u128 gmul(struct u128 x, struct u128 h)
{
    struct u128 z = { 0, 0 }, v = h;
    for (int i = 0; i < 128; i++) {
        uint64_t bit = i < 64 ? (x.hi >> (63 - i)) & 1 : (x.lo >> (127 - i)) & 1;
        if (bit) { z.hi ^= v.hi; z.lo ^= v.lo; }
        uint64_t lsb = v.lo & 1;
        v.lo = (v.lo >> 1) | (v.hi << 63); v.hi >>= 1;
        if (lsb) v.hi ^= 0xe100000000000000ULL;
    }
    return z;
}
static void ghash_upd(struct u128 *y, struct u128 h, const uint8_t *p, size_t n)
{
    while (n) {
        uint8_t blk[16]; size_t k = n < 16 ? n : 16;
        for (size_t i = 0; i < 16; i++) blk[i] = i < k ? p[i] : 0;
        struct u128 b = ld128(blk); y->hi ^= b.hi; y->lo ^= b.lo; *y = gmul(*y, h);
        p += k; n -= k;
    }
}
static void gcm_ctr(const struct aes128 *a, const uint8_t iv[12], const uint8_t *in, size_t n, uint8_t *out)
{
    uint8_t cb[16], ks[16]; uint32_t ctr = 2;
    for (int i = 0; i < 12; i++) cb[i] = iv[i];
    for (size_t off = 0; off < n; off += 16, ctr++) {
        cb[12] = (uint8_t)(ctr >> 24); cb[13] = (uint8_t)(ctr >> 16); cb[14] = (uint8_t)(ctr >> 8); cb[15] = (uint8_t)ctr;
        aes128_encrypt(a, cb, ks);
        size_t k = n - off < 16 ? n - off : 16;
        for (size_t i = 0; i < k; i++) out[off + i] = in[off + i] ^ ks[i];
    }
}
static void gcm_tag(const struct aes128 *a, const uint8_t iv[12], const uint8_t *aad, size_t alen, const uint8_t *ct, size_t n, uint8_t tag[16])
{
    uint8_t z[16] = { 0 }, hb[16], j0[16], ek[16];
    aes128_encrypt(a, z, hb);
    struct u128 h = ld128(hb), y = { 0, 0 };
    ghash_upd(&y, h, aad, alen); ghash_upd(&y, h, ct, n);
    struct u128 lens = { (uint64_t)alen * 8, (uint64_t)n * 8 }; y.hi ^= lens.hi; y.lo ^= lens.lo; y = gmul(y, h);
    for (int i = 0; i < 12; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    aes128_encrypt(a, j0, ek);
    uint8_t yb[16]; st128(yb, y);
    for (int i = 0; i < 16; i++) tag[i] = yb[i] ^ ek[i];
}
void gcm128_seal(const uint8_t key[16], const uint8_t iv[12], const uint8_t *aad, size_t alen, const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[16])
{
    struct aes128 a; aes128_init(&a, key);
    gcm_ctr(&a, iv, in, n, out);
    gcm_tag(&a, iv, aad, alen, out, n, tag);
}
int gcm128_open(const uint8_t key[16], const uint8_t iv[12], const uint8_t *aad, size_t alen, const uint8_t *in, size_t n, const uint8_t tag[16], uint8_t *out)
{
    struct aes128 a; aes128_init(&a, key);
    uint8_t t[16]; gcm_tag(&a, iv, aad, alen, in, n, t);
    uint8_t d = 0; for (int i = 0; i < 16; i++) d |= (uint8_t)(t[i] ^ tag[i]);
    if (d) return -1;
    gcm_ctr(&a, iv, in, n, out);
    return 0;
}

/* ---------------------------------------------------------------- aleatorios */
void (*tls_rng_hook)(uint8_t *p, size_t n);
#if !__STDC_HOSTED__
/* SOMA_192: en el nucleo, las claves y nonces de TLS salen del generador unico (rng.h). El generador de abajo (que sin RDRAND solo
 * dependia del contador de ciclos) queda para las herramientas del host (tests/tls_live_176.c). */
void tls_random(uint8_t *p, size_t n)
{
    if (tls_rng_hook) { tls_rng_hook(p, n); return; }
    rng_bytes(p, n);
}
#else
static uint64_t cycles(void)
{
#if defined(__x86_64__)
    uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo;
#elif defined(__aarch64__)
    uint64_t v; __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(v)); return v;
#else
    return 0;
#endif
}
static int rdrand64(uint64_t *v)
{
#if defined(__x86_64__)
    uint32_t a = 1, b, c = 0, d; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (!(c & (1u << 30))) return 0;
    for (int i = 0; i < 10; i++) { unsigned char ok; uint64_t x; __asm__ volatile ("rdrand %0; setc %1" : "=r"(x), "=qm"(ok)); if (ok) { *v = x; return 1; } }
#endif
    (void)v; return 0;
}
void tls_random(uint8_t *p, size_t n)
{
    if (tls_rng_hook) { tls_rng_hook(p, n); return; }
    static uint64_t ctr; static uint8_t pool[32];
    for (size_t off = 0; off < n; ) {
        uint8_t blk[64]; uint64_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
        int ok = rdrand64(&r0) && rdrand64(&r1) && rdrand64(&r2) && rdrand64(&r3);
        uint64_t t = cycles(), stk = (uint64_t)(uintptr_t)blk;
        for (int i = 0; i < 32; i++) blk[i] = pool[i];
        for (int i = 0; i < 8; i++) { blk[32 + i] = (uint8_t)(r0 >> (8 * i)); blk[40 + i] = (uint8_t)(r1 >> (8 * i)); blk[48 + i] = (uint8_t)(r2 >> (8 * i)); blk[56 + i] = (uint8_t)(r3 >> (8 * i)); }
        (void)ok; blk[0] ^= (uint8_t)t; blk[1] ^= (uint8_t)(t >> 8); blk[2] ^= (uint8_t)(t >> 16); blk[3] ^= (uint8_t)(stk >> 4); blk[4] ^= (uint8_t)ctr; blk[5] ^= (uint8_t)(ctr >> 8);
        ctr++;
        sha256(blk, sizeof blk, pool);
        uint8_t out[32]; uint8_t tag[33]; for (int i = 0; i < 32; i++) tag[i] = pool[i]; tag[32] = 1; sha256(tag, 33, out);
        for (size_t i = 0; i < 32 && off < n; i++) p[off++] = out[i];
    }
}
#endif
