/* SOMA_194: validacion de certificados (src/core/x509.c) contra vectores generados con la biblioteca `cryptography` de Python
 * (tests/x509_vectors_gen.py) y fuzzing de los analizadores. Uso: test_x509_194 [iteraciones de fuzzing] [semilla] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x509.h"
#include "tls_roots.h"
#include "x509_vectors_194.h"

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static uint64_t S = 88172645463325252ULL;
static uint32_t rnd(void) { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32_t)(S >> 11); }
static void hex(const uint8_t *b, int n, char *o) { for (int i = 0; i < n; i++) sprintf(o + 2 * i, "%02x", b[i]); }

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 3000; if (argc > 2) S ^= (uint64_t)atol(argv[2]) * 0x9E3779B97F4A7C15ULL;
    char hx[130]; uint8_t d[64];

    /* SHA-384 / SHA-512 (FIPS 180-4, "abc" y la cadena vacia) */
    sha384("abc", 3, d); hex(d, 48, hx);
    CK(!strcmp(hx, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"), "SHA-384(abc) = %s", hx);
    sha512("abc", 3, d); hex(d, 64, hx);
    CK(!strcmp(hx, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"), "SHA-512(abc) = %s", hx);
    sha512("", 0, d); hex(d, 64, hx);
    CK(!strncmp(hx, "cf83e1357eefb8bdf1542850d66d8007", 32), "SHA-512(vacio) = %s", hx);
    { static uint8_t big[1000]; memset(big, 'a', sizeof big); sha384(big, 1000, d); hex(d, 48, hx);      /* varios bloques y relleno en dos bloques */
      uint8_t d2[48]; sha384(big, 111, d2); sha384(big, 112, d); CK(memcmp(d, d2, 48) != 0, "relleno de SHA-384"); }

    /* nombres */
    CK(x509_host_match("www.ejemplo.test", 16, "www.ejemplo.test") && x509_host_match("*.ejemplo.test", 14, "a.ejemplo.test"), "coincidencias basicas");
    CK(!x509_host_match("*.test", 6, "ejemplo.test") && !x509_host_match("*.ejemplo.test", 14, ".ejemplo.test") && !x509_host_match("w*.ejemplo.test", 15, "www.ejemplo.test"), "comodines no validos");
    CK(!x509_host_match("www.ejemplo.test\0.atacante.test", 31, "www.ejemplo.test"), "NUL dentro del nombre");

    /* cadenas */
    x509_set_roots(V_ROOTS, sizeof V_ROOTS);
    CK(x509_root_count() == 4, "raices de prueba: %u", x509_root_count());
    int nc = (int)(sizeof V_CASES / sizeof V_CASES[0]), ncv = (int)(sizeof V_CVS / sizeof V_CVS[0]);
    for (int i = 0; i < nc; i++) {
        struct x509_key k; int r = x509_verify_tls_chain(V_CASES[i].msg, V_CASES[i].len, V_CASES[i].host, V_CASES[i].now, &k);
        if (V_CASES[i].expect < 0) CK(r != X509_OK, "caso '%s': aceptado", V_CASES[i].name);
        else CK(r == V_CASES[i].expect, "caso '%s': %d (%s), esperado %d", V_CASES[i].name, r, x509_err_name(r), V_CASES[i].expect);
    }
    /* firma del saludo */
    for (int i = 0; i < ncv; i++) {
        struct x509_key k; int r = x509_verify_tls_chain(V_CVS[i].msg, V_CVS[i].len, V_CVS[i].host, V_NOW, &k);
        CK(r == X509_OK, "cadena de la firma '%s': %d", V_CVS[i].name, r);
        if (r != X509_OK) continue;
        r = x509_verify_tls_cv(&k, V_CVS[i].cv, V_CVS[i].cvlen, V_CVS[i].th);
        CK(r == V_CVS[i].expect, "firma '%s': %d (%s), esperado %d", V_CVS[i].name, r, x509_err_name(r), V_CVS[i].expect);
    }
    /* autoridades del usuario: un autofirmado (el caso del servidor del LLM) y un PEM */
    {
        uint8_t m[2048]; uint32_t l = sizeof V_SELF; m[0] = 0; m[1] = 0; m[2] = (uint8_t)((l + 5) >> 8); m[3] = (uint8_t)(l + 5);
        m[4] = 0; m[5] = (uint8_t)(l >> 8); m[6] = (uint8_t)l; memcpy(m + 7, V_SELF, l); m[7 + l] = 0; m[8 + l] = 0;
        CK(x509_verify_tls_chain(m, l + 9, "llm.local", V_NOW, 0) == X509_E_UNTRUSTED, "autofirmado sin anadir");
        CK(x509_add_anchor(V_SELF, sizeof V_SELF) == 0, "anadir autofirmado");
        CK(x509_verify_tls_chain(m, l + 9, "llm.local", V_NOW, 0) == X509_OK, "autofirmado anadido por el usuario");
        CK(x509_verify_tls_chain(m, l + 9, "192.168.1.200", V_NOW, 0) == X509_OK, "autofirmado por IP");
        CK(x509_verify_tls_chain(m, l + 9, "otro.local", V_NOW, 0) == X509_E_HOST, "autofirmado, otro nombre");
        x509_clear_anchors();
        CK(x509_verify_tls_chain(m, l + 9, "llm.local", V_NOW, 0) == X509_E_UNTRUSTED, "tras quitarlo");
        int untr = -1; for (int i = 0; i < nc; i++) if (strstr(V_CASES[i].name, "raiz que no es de confianza")) untr = i;
        CK(untr >= 0 && x509_add_anchors_pem(V_PEM, sizeof V_PEM) == 1, "PEM del usuario");
        CK(untr >= 0 && x509_verify_tls_chain(V_CASES[untr].msg, V_CASES[untr].len, V_CASES[untr].host, V_NOW, 0) == X509_OK, "cadena hasta la raiz anadida por PEM");
        CK(x509_add_anchors_pem("-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n", 60) == 0 && x509_add_anchors_pem("basura", 6) == 0, "PEM no valido");
        x509_clear_anchors();
    }
    /* la lista real del nucleo se lee entera y no se fia de las cadenas de prueba */
    x509_set_roots(tls_roots_blob, tls_roots_len);
    CK(x509_root_count() >= 100, "raices del nucleo: %u", x509_root_count());
    CK(x509_verify_tls_chain(V_CASES[0].msg, V_CASES[0].len, V_CASES[0].host, V_NOW, 0) == X509_E_UNTRUSTED, "las raices de prueba no estan en la lista real");
    x509_set_roots(V_ROOTS, sizeof V_ROOTS);

    /* fuzzing: mensajes mutados (ASAN/UBSAN vigilan); un mensaje mutado nunca puede validar para OTRO sitio */
    long accepted = 0;
    for (long it = 0; it < iters; it++) {
        int ci = (int)(rnd() % (uint32_t)nc); uint32_t len = V_CASES[ci].len; if (!len) continue;
        uint8_t *m = malloc(len + 64); memcpy(m, V_CASES[ci].msg, len);
        int muts = 1 + (int)(rnd() % 4);
        for (int k = 0; k < muts; k++) {
            uint32_t p = rnd() % len;
            switch (rnd() % 6) {
            case 0: m[p] ^= (uint8_t)(1u << (rnd() % 8)); break;
            case 1: m[p] = (uint8_t)rnd(); break;
            case 2: m[p] = (uint8_t[]){ 0, 1, 0x7f, 0x80, 0x81, 0x82, 0x84, 0xff }[rnd() % 8]; break;
            case 3: len = 1 + rnd() % len; break;
            case 4: { uint32_t l = 1 + rnd() % 8; if (p + l < len) { memmove(m + p, m + p + l, len - p - l); len -= l; } } break;
            default: { uint32_t l = 1 + rnd() % 16; memmove(m + p + l, m + p, len - p); for (uint32_t i = 0; i < l; i++) m[p + i] = (uint8_t)rnd(); len += l; } break;
            }
        }
        struct x509_key k;
        uint8_t *exact = malloc(len ? len : 1); memcpy(exact, m, len);                 /* bufer del tamano exacto: cualquier lectura de mas salta en ASAN */
        if (x509_verify_tls_chain(exact, len, V_CASES[ci].host, V_CASES[ci].now, &k) == X509_OK) accepted++;
        CK(x509_verify_tls_chain(exact, len, "otro.sitio.test", V_CASES[ci].now, &k) != X509_OK, "mensaje mutado aceptado para otro sitio");
        if (rnd() % 8 == 0) { uint8_t th[32]; for (int i = 0; i < 32; i++) th[i] = (uint8_t)rnd(); (void)x509_verify_tls_cv(&k, exact, len > 600 ? 600 : len, th); }
        if (rnd() % 16 == 0) (void)x509_add_anchor(exact, len);
        if (rnd() % 16 == 0) (void)x509_add_anchors_pem((const char *)exact, len);
        free(exact); free(m);
    }
    x509_clear_anchors();
    printf("test_x509_194: %d comprobaciones, %d fallos (%d cadenas, %d firmas de saludo, %ld mutaciones, %ld aceptadas)\n", checks, fails, nc, ncv, iters, accepted);
    return fails ? 1 : 0;
}
