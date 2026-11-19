/* SOMA_201: fuzzing de la maquina de estados del cliente TLS 1.3 (src/core/tls13.c) en el anfitrion, con ASan y UBSan.
 * La criptografia se sustituye por funciones identidad: asi los mensajes "cifrados" del servidor (EncryptedExtensions,
 * Certificate, CertificateVerify, Finished, tiques, KeyUpdate, datos) llegan al analizador tal cual los genera el fuzzer, con
 * longitudes y tipos mutados y troceados al azar. La validacion de certificados tiene su propio fuzzing (test_x509_194).
 * Uso: fuzz_tls_201 [iteraciones] [semilla] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include "tls13.h"
#include "tlscrypto.h"
#include "x509.h"

void sha256_init(struct sha256_ctx *c) { (void)c; }
void sha256_update(struct sha256_ctx *c, const void *p, size_t n) { (void)c; (void)p; (void)n; }
void sha256_final(struct sha256_ctx *c, uint8_t out[32]) { (void)c; memset(out, 0, 32); }
void sha256(const void *p, size_t n, uint8_t out[32]) { (void)p; (void)n; memset(out, 0, 32); }
void hmac_sha256(const uint8_t *k, size_t kl, const void *m, size_t ml, uint8_t out[32]) { (void)k; (void)kl; (void)m; (void)ml; memset(out, 0, 32); }
void hkdf_extract(const uint8_t *s, size_t sl, const uint8_t *i, size_t il, uint8_t out[32]) { (void)s; (void)sl; (void)i; (void)il; memset(out, 0, 32); }
void hkdf_expand_label(const uint8_t s[32], const char *l, const uint8_t *c, size_t cl, uint8_t *out, size_t ol) { (void)s; (void)l; (void)c; (void)cl; memset(out, 0, ol); }
void x25519(uint8_t out[32], const uint8_t s[32], const uint8_t p[32]) { (void)s; (void)p; memset(out, 1, 32); }
void x25519_base(uint8_t pub[32], const uint8_t priv[32]) { (void)priv; memset(pub, 2, 32); }
void tls_random(uint8_t *p, size_t n) { memset(p, 3, n); }
void gcm128_seal(const uint8_t k[16], const uint8_t iv[12], const uint8_t *a, size_t al, const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[16]) { (void)k; (void)iv; (void)a; (void)al; memmove(out, in, n); memset(tag, 0, 16); }
static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
int gcm128_open(const uint8_t k[16], const uint8_t iv[12], const uint8_t *a, size_t al, const uint8_t *in, size_t n, const uint8_t tag[16], uint8_t *out) { (void)k; (void)iv; (void)a; (void)al; (void)tag; memmove(out, in, n); return rn(60) ? 0 : 1; }
const char *x509_err_name(int e) { (void)e; return "x509"; }
int x509_verify_tls_chain(const uint8_t *m, uint32_t len, const char *h, uint64_t now, struct x509_key *leaf) { (void)h; (void)now; (void)leaf; volatile uint8_t s = 0; for (uint32_t i = 0; i < len; i += 31) s ^= m[i]; (void)s; return rn(20) ? 0 : 5; }
int x509_verify_tls_cv(const struct x509_key *leaf, const uint8_t *cv, uint32_t n, const uint8_t th[32]) { (void)leaf; (void)th; volatile uint8_t s = 0; for (uint32_t i = 0; i < n; i++) s ^= cv[i]; (void)s; return rn(20) ? 0 : 7; }

static uint8_t srv[1 << 18]; static uint32_t sl, sp;
static void put(const void *p, uint32_t n) { if (sl + n <= sizeof srv) { memcpy(srv + sl, p, n); sl += n; } }
static void rec(int type, const uint8_t *d, uint32_t n, int enc)
{
    uint8_t h[5]; uint32_t tot = n + (enc ? 17 : 0);
    if (rn(40) == 0) tot = rn(3) ? rnd() & 0xFFFF : tot + rn(9) - 4;                  /* longitud de registro falsa */
    h[0] = (uint8_t)(enc ? 23 : type); h[1] = 3; h[2] = 3; h[3] = (uint8_t)(tot >> 8); h[4] = (uint8_t)tot;
    if (rn(60) == 0) h[0] = (uint8_t)rnd();
    put(h, 5); put(d, n);
    if (enc) { uint8_t t[17]; memset(t, 0, 17); t[0] = (uint8_t)(rn(30) ? type : rnd()); put(t, rn(50) ? 17 : rn(17)); }
}
static uint32_t hs(uint8_t *o, int type, const uint8_t *b, uint32_t n)
{
    uint32_t dl = n; if (rn(30) == 0) dl = rn(2) ? rnd() & 0xFFFFFF : n + rn(7) - 3;   /* longitud de mensaje falsa */
    o[0] = (uint8_t)type; o[1] = (uint8_t)(dl >> 16); o[2] = (uint8_t)(dl >> 8); o[3] = (uint8_t)dl; memcpy(o + 4, b, n); return n + 4;
}
static void gen(void)
{
    static uint8_t m[70000], b[66000]; uint32_t n; sl = 0;
    /* ServerHello */
    n = 0; b[n++] = 3; b[n++] = 3; for (int i = 0; i < 32; i++) b[n++] = (uint8_t)(rn(40) ? 0x55 : rnd());
    { int sid = rn(8) ? 32 : (int)rn(60); b[n++] = (uint8_t)sid; for (int i = 0; i < sid && i < 40; i++) b[n++] = 3; }
    b[n++] = 0x13; b[n++] = (uint8_t)(rn(20) ? 1 : rnd()); b[n++] = 0;
    { uint32_t e0 = n; n += 2;
      if (rn(15)) { b[n++] = 0; b[n++] = 43; b[n++] = 0; b[n++] = 2; b[n++] = 3; b[n++] = (uint8_t)(rn(20) ? 4 : rnd()); }
      if (rn(15)) { uint32_t kl = rn(20) ? 32 : rn(70); b[n++] = 0; b[n++] = 51; b[n++] = (uint8_t)((kl + 4) >> 8); b[n++] = (uint8_t)(kl + 4); b[n++] = 0; b[n++] = (uint8_t)(rn(20) ? 29 : rnd()); b[n++] = (uint8_t)(kl >> 8); b[n++] = (uint8_t)kl; for (uint32_t i = 0; i < kl; i++) b[n++] = 9; }
      for (int k = (int)rn(3); k > 0; k--) { uint32_t el = rn(40); b[n++] = (uint8_t)rnd(); b[n++] = (uint8_t)rnd(); b[n++] = 0; b[n++] = (uint8_t)(rn(8) ? el : rnd()); for (uint32_t i = 0; i < el; i++) b[n++] = (uint8_t)rnd(); }
      uint32_t el = n - e0 - 2; if (rn(25) == 0) el = rnd() & 0xFFFF; b[e0] = (uint8_t)(el >> 8); b[e0 + 1] = (uint8_t)el; }
    rec(22, m, hs(m, rn(30) ? 2 : (int)rn(25), b, n), 0);
    if (rn(3)) { uint8_t c = 1; rec(20, &c, 1, 0); }
    /* vuelo cifrado: uno o varios mensajes por registro */
    static const int ORDER[] = { 8, 11, 15, 20 };
    uint32_t ml = 0;
    for (int k = 0; k < 4; k++) {
        int ty = rn(25) ? ORDER[k] : (int)rn(26); n = 0;
        if (ty == 8) { uint32_t el = rn(6) ? 0 : rn(300); b[n++] = (uint8_t)(el >> 8); b[n++] = (uint8_t)el; for (uint32_t i = 0; i < el; i++) b[n++] = (uint8_t)rnd(); }
        else if (ty == 11) { uint32_t cl = rn(10) ? 300 + rn(1500) : rn(5) ? rn(40) : 20000 + rn(40000); b[n++] = 0; uint32_t l0 = n; n += 3; for (int c = 1 + (int)rn(3); c > 0; c--) { b[n++] = (uint8_t)(cl >> 16); b[n++] = (uint8_t)(cl >> 8); b[n++] = (uint8_t)cl; for (uint32_t i = 0; i < cl && n < 60000; i++) b[n++] = (uint8_t)(i * 7); b[n++] = 0; b[n++] = 0; } uint32_t t = n - l0 - 3; if (rn(20) == 0) t = rnd() & 0xFFFFFF; b[l0] = (uint8_t)(t >> 16); b[l0 + 1] = (uint8_t)(t >> 8); b[l0 + 2] = (uint8_t)t; }
        else if (ty == 15) { uint32_t s = rn(12) ? 256 : rn(900); b[n++] = 8; b[n++] = 4; b[n++] = (uint8_t)(s >> 8); b[n++] = (uint8_t)s; for (uint32_t i = 0; i < s; i++) b[n++] = 0x77; }
        else if (ty == 20) { uint32_t s = rn(15) ? 32 : rn(70); for (uint32_t i = 0; i < s; i++) b[n++] = 0; }
        else { n = rn(200); for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)rnd(); }
        ml += hs(m + ml, ty, b, n);
        if (rn(3) || k == 3) { for (uint32_t off = 0; off < ml; ) { uint32_t part = ml - off; if (part > 16000) part = 16000; if (rn(6) == 0 && part > 3) part = 1 + rn(part); rec(22, m + off, part, 1); off += part; } ml = 0; }
        if (rn(40) == 0) break;
    }
    /* despues del saludo: tiques, KeyUpdate, datos, alertas */
    for (int k = (int)rn(6); k > 0; k--) {
        int w = (int)rn(6); n = 0;
        if (w == 0) { n = rn(300); for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)rnd(); rec(22, m, hs(m, 4, b, n), 1); }
        else if (w == 1) { b[0] = (uint8_t)rn(3); rec(22, m, hs(m, 24, b, rn(8) ? 1 : rn(5)), 1); }
        else if (w == 2) { b[0] = (uint8_t)(1 + rn(2)); b[1] = (uint8_t)rn(120); rec(21, b, rn(6) ? 2 : rn(4), 1); }
        else if (w == 3) { n = rn(4) ? rn(2000) : 16384 + rn(400); for (uint32_t i = 0; i < n; i++) b[i] = 'd'; rec(23, b, n, 1); }
        else if (w == 4) { rec(23, b, 0, 1); }
        else { n = rn(100); for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)rnd(); rec((int)rn(30), b, n, (int)rn(2)); }
    }
    if (rn(5) == 0) for (int k = 1 + (int)rn(3); k > 0 && sl > 8; k--) { uint32_t p = rn(sl); if (rn(2)) srv[p] = (uint8_t)rnd(); else sl = p + 1; }
}
static int io_send(void *c, const uint8_t *d, uint32_t n) { (void)c; (void)d; (void)n; return rn(300) ? 0 : -1; }
static int io_recv(void *c, uint8_t *d, uint32_t max, uint32_t t) { (void)c; (void)t; if (sp >= sl) return rn(3) ? TLS_IO_EOF : TLS_IO_ERR; if (rn(25) == 0) return 0; uint32_t n = 1 + rn(rn(3) ? 3000 : 20); if (n > max) n = max; if (n > sl - sp) n = sl - sp; memcpy(d, srv + sp, n); sp += n; return (int)n; }
static uint64_t g_now; static uint64_t io_now(void) { return g_now += 2; }
static uint32_t g_iter; static long errs[64];
static void on_alarm(int s) { (void)s; fprintf(stderr, "\nCUELGUE: el caso %u lleva mas de 20 s (guardado en /tmp/fuzz_tls_cuelgue.bin)\n", g_iter); FILE *f = fopen("/tmp/fuzz_tls_cuelgue.bin", "wb"); if (f) { fwrite(srv, 1, sl, f); fclose(f); } _exit(3); }
int main(int argc, char **argv)
{
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 20000, seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1; long ok = 0;
    signal(SIGALRM, on_alarm);
    for (g_iter = getenv("FUZZ_START") ? (uint32_t)atoi(getenv("FUZZ_START")) : 0; g_iter < iters; g_iter++) {
        rs = (seed * 2654435761u) ^ (g_iter * 40503u + 1u); if (!rs) rs = 1;
        gen(); sp = 0; alarm(20);
        struct tls13 *t = (struct tls13 *)malloc(sizeof *t);                    /* tamano exacto: ASan ve cualquier byte de mas */
        struct tls_io io = { 0, io_send, io_recv, io_now, 0 };
        tls13_verify = rn(8) != 0;
        if (tls13_connect(t, &io, rn(5) ? "a.test" : 0, 3000) == 0) {
            ok++;
            (void)tls13_send(t, "GET / HTTP/1.1\r\n\r\n", 18);
            for (int k = 0; k < 40; k++) { uint32_t mx = 1 + rn(rn(2) ? 20000 : 50); uint8_t *o = (uint8_t *)malloc(mx); int r = tls13_recv(t, o, mx, 100); free(o); if (r < 0) break; }
            if (rn(2)) { static uint8_t big[40000]; (void)tls13_send(t, big, rn(sizeof big)); }
            tls13_close(t);
        }
        if (getenv("FUZZ_ERRS") && t->err >= 0 && t->err < 64) errs[t->err]++;
        free(t);
    }
    alarm(0);
    if (getenv("FUZZ_ERRS")) for (int e = 0; e < 64; e++) if (errs[e]) printf("  error %d (%s): %ld\n", e, tls13_err_name(e), errs[e]);
    printf("fuzz_tls_201: %u conexiones sin fallos de memoria ni cuelgues (semilla %u; %ld llegaron a establecerse)\n", iters, seed, ok);
    return 0;
}
