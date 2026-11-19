/* SOMA_175C: prueba del CCMP en software (src/core/ccmp.c): vector oficial de IEEE 802.11 y 40 vectores de AES-CCM de referencia
 * (datos y datos QoS, con bits Retry/PwrMgt/MoreData que deben quedar fuera del AAD), descifrado, MIC alterado y tramas cortas. */
#include <stdio.h>
#include <string.h>
#include "ccmp.h"
#include "ccmp_vectors_175.h"

static int hex(const char *s, unsigned char *o) { int n = 0; while (s[0] && s[1]) { unsigned v; sscanf(s, "%2x", &v); o[n++] = (unsigned char)v; s += 2; } return n; }

int main(void)
{
    int fails = 0;
    /* IEEE 802.11-2016, J.6.4 */
    unsigned char tk[16], hdr[32], pt[64], out[128], back[128];
    hex("c97c1f67ce371185514a8a19f2bdd52f", tk);
    int hl = hex("0848c32c0fd2e128a57c5030f1844408abaea5b8fcba8033", hdr);
    int pl = hex("f8ba1a55d02f85ae967bb62fb6cda8eb7e78a050", pt);
    static const unsigned char E[] = { 0x0c,0xe7,0x00,0x20,0x76,0x97,0x03,0xb5, 0xf3,0xd0,0xa2,0xfe,0x9a,0x3d,0xbf,0x23,0x42,0xa6,0x43,0xe4,0x32,0x46,0xe8,0x0c,0x3c,0x04,0xd0,0x19, 0x78,0x45,0xce,0x0b,0x16,0xf9,0x76,0x23 };
    size_t n = ccmp_encrypt(tk, hdr, (size_t)hl, 0xB5039776E70CULL, 0, pt, (size_t)pl, out);
    if (n != sizeof E || memcmp(out, E, n)) { printf("FALLO: vector oficial de 802.11\n"); fails++; }
    uint64_t pn = 0; int kid = -1;
    int d = ccmp_decrypt(tk, hdr, (size_t)hl, E, sizeof E, back, &pn, &kid);
    if (d != pl || memcmp(back, pt, (size_t)pl) || pn != 0xB5039776E70CULL || kid != 0) { printf("FALLO: descifrado del vector oficial\n"); fails++; }
    unsigned char bad[sizeof E]; memcpy(bad, E, sizeof E); bad[sizeof E - 1] ^= 1;
    if (ccmp_decrypt(tk, hdr, (size_t)hl, bad, sizeof bad, back, 0, 0) != -1) { printf("FALLO: MIC alterado aceptado\n"); fails++; }
    memcpy(bad, E, sizeof E); bad[10] ^= 0x80;
    if (ccmp_decrypt(tk, hdr, (size_t)hl, bad, sizeof bad, back, 0, 0) != -1) { printf("FALLO: cifrado alterado aceptado\n"); fails++; }
    if (ccmp_decrypt(tk, hdr, (size_t)hl, E, 15, back, 0, 0) != -1) { printf("FALLO: trama corta aceptada\n"); fails++; }
    /* vectores de referencia */
    static unsigned char buf[2048], dec[2048];
    for (unsigned i = 0; i < sizeof CCMP_VECS / sizeof CCMP_VECS[0]; i++) {
        const struct ccmp_vec *v = &CCMP_VECS[i];
        size_t m = ccmp_encrypt((const uint8_t *)v->tk, (const uint8_t *)v->hdr, v->hdrlen, v->pn, v->keyid, (const uint8_t *)v->pt, v->plen, buf);
        if (m != v->ctlen || memcmp(buf, v->ct, m)) { printf("FALLO: cifrado del vector %u (hdr %u, %u bytes)\n", i, v->hdrlen, v->plen); fails++; continue; }
        int r = ccmp_decrypt((const uint8_t *)v->tk, (const uint8_t *)v->hdr, v->hdrlen, (const uint8_t *)v->ct, v->ctlen, dec, &pn, &kid);
        if (r != (int)v->plen || memcmp(dec, v->pt, v->plen) || pn != v->pn || kid != v->keyid) { printf("FALLO: descifrado del vector %u\n", i); fails++; }
    }
    if (fails) { printf("ccmp: %d fallos\n", fails); return 1; }
    printf("ccmp: correcto (vector oficial de 802.11, %u vectores de referencia, MIC y cifrado alterados rechazados)\n", (unsigned)(sizeof CCMP_VECS / sizeof CCMP_VECS[0]));
    return 0;
}
