/* SOMA_175C: CCMP de IEEE 802.11 (CCM con AES-128, M = 8, L = 2). Ver ccmp.h. Referencia: IEEE 802.11-2016, 12.5.3. */
#include "ccmp.h"
#include "crypto.h"

static void xor16(uint8_t *d, const uint8_t *s, size_t n) { for (size_t i = 0; i < n; i++) d[i] ^= s[i]; }

/* nonce (13) y AAD (devuelve su longitud) a partir de la cabecera 802.11 */
static size_t nonce_aad(const uint8_t *h, size_t hl, uint64_t pn, uint8_t nonce[13], uint8_t aad[32])
{
    int qos = (h[0] & 0x8C) == 0x88;                      /* datos QoS */
    int a4 = (h[1] & 3) == 3;
    uint8_t prio = 0;
    if (qos) { size_t qo = a4 ? 30 : 24; if (qo < hl) prio = h[qo] & 0x0F; }
    int mgmt = (h[0] & 0x0C) == 0x00;
    nonce[0] = (uint8_t)(prio | (mgmt ? 0x10 : 0));
    for (int i = 0; i < 6; i++) nonce[1 + i] = h[10 + i];  /* A2 */
    for (int i = 0; i < 6; i++) nonce[7 + i] = (uint8_t)(pn >> (8 * (5 - i)));
    size_t n = 0;
    uint8_t fc0 = h[0], fc1 = h[1];
    if ((fc0 & 0x0C) == 0x08) fc0 &= 0x8F;                 /* datos: subtipo (bits 4-6) a cero */
    fc1 &= (uint8_t)~(0x08 | 0x10 | 0x20);                /* Retry, PwrMgt, MoreData */
    fc1 |= 0x40;                                           /* Protected */
    if (qos) fc1 &= 0x7F;                                  /* Order */
    aad[n++] = fc0; aad[n++] = fc1;
    for (int i = 0; i < 18; i++) aad[n++] = h[4 + i];      /* A1 A2 A3 */
    aad[n++] = (uint8_t)(h[22] & 0x0F); aad[n++] = 0;     /* SC: solo el numero de fragmento */
    if (a4) for (int i = 0; i < 6; i++) aad[n++] = h[24 + i];
    if (qos) { aad[n++] = prio; aad[n++] = 0; }
    return n;
}

/* CBC-MAC (T de 16 bytes) sobre B0, el AAD y el texto claro */
static void cbc_mac(const struct aes128 *a, const uint8_t nonce[13], const uint8_t *aad, size_t al, const uint8_t *p, size_t pl, uint8_t x[16])
{
    uint8_t b[16];
    b[0] = 0x59; for (int i = 0; i < 13; i++) b[1 + i] = nonce[i]; b[14] = (uint8_t)(pl >> 8); b[15] = (uint8_t)pl;
    aes128_encrypt(a, b, x);
    /* AAD con su longitud en 2 bytes delante, en bloques de 16 rellenos con ceros */
    uint8_t blk[16]; size_t k = 0; int first = 1;
    while (first || k < al) {
        for (int i = 0; i < 16; i++) blk[i] = 0;
        int o = 0;
        if (first) { blk[0] = (uint8_t)(al >> 8); blk[1] = (uint8_t)al; o = 2; first = 0; }
        while (o < 16 && k < al) blk[o++] = aad[k++];
        xor16(x, blk, 16); aes128_encrypt(a, x, x);
    }
    for (size_t off = 0; off < pl; off += 16) {
        size_t n = pl - off < 16 ? pl - off : 16;
        xor16(x, p + off, n); aes128_encrypt(a, x, x);
    }
}
static void ctr(const struct aes128 *a, const uint8_t nonce[13], uint16_t i, uint8_t s[16])
{
    uint8_t b[16]; b[0] = 0x01; for (int k = 0; k < 13; k++) b[1 + k] = nonce[k]; b[14] = (uint8_t)(i >> 8); b[15] = (uint8_t)i;
    aes128_encrypt(a, b, s);
}

size_t ccmp_encrypt(const uint8_t tk[16], const uint8_t *hdr, size_t hdrlen, uint64_t pn, int keyid,
                    const uint8_t *plain, size_t plen, uint8_t *out)
{
    struct aes128 a; aes128_init(&a, tk);
    uint8_t nonce[13], aad[32];
    size_t al = nonce_aad(hdr, hdrlen, pn, nonce, aad);
    out[0] = (uint8_t)pn; out[1] = (uint8_t)(pn >> 8); out[2] = 0; out[3] = (uint8_t)(0x20 | ((keyid & 3) << 6));
    out[4] = (uint8_t)(pn >> 16); out[5] = (uint8_t)(pn >> 24); out[6] = (uint8_t)(pn >> 32); out[7] = (uint8_t)(pn >> 40);
    uint8_t x[16], s[16];
    cbc_mac(&a, nonce, aad, al, plain, plen, x);
    uint8_t *c = out + CCMP_HDR_LEN;
    for (size_t off = 0, i = 1; off < plen; off += 16, i++) {
        ctr(&a, nonce, (uint16_t)i, s);
        size_t n = plen - off < 16 ? plen - off : 16;
        for (size_t k = 0; k < n; k++) c[off + k] = plain[off + k] ^ s[k];
    }
    ctr(&a, nonce, 0, s);
    for (int k = 0; k < CCMP_MIC_LEN; k++) c[plen + k] = x[k] ^ s[k];
    return CCMP_HDR_LEN + plen + CCMP_MIC_LEN;
}

uint64_t ccmp_hdr_pn(const uint8_t *h)
{
    return (uint64_t)h[0] | (uint64_t)h[1] << 8 | (uint64_t)h[4] << 16 | (uint64_t)h[5] << 24 | (uint64_t)h[6] << 32 | (uint64_t)h[7] << 40;
}

int ccmp_decrypt(const uint8_t tk[16], const uint8_t *hdr, size_t hdrlen, const uint8_t *in, size_t inlen,
                 uint8_t *out, uint64_t *pn_out, int *keyid)
{
    if (inlen < CCMP_HDR_LEN + CCMP_MIC_LEN || !(in[3] & 0x20)) return -1;
    uint64_t pn = ccmp_hdr_pn(in);
    if (pn_out) *pn_out = pn;
    if (keyid) *keyid = in[3] >> 6;
    size_t plen = inlen - CCMP_HDR_LEN - CCMP_MIC_LEN;
    struct aes128 a; aes128_init(&a, tk);
    uint8_t nonce[13], aad[32], s[16], x[16];
    size_t al = nonce_aad(hdr, hdrlen, pn, nonce, aad);
    const uint8_t *c = in + CCMP_HDR_LEN;
    for (size_t off = 0, i = 1; off < plen; off += 16, i++) {
        ctr(&a, nonce, (uint16_t)i, s);
        size_t n = plen - off < 16 ? plen - off : 16;
        for (size_t k = 0; k < n; k++) out[off + k] = c[off + k] ^ s[k];
    }
    cbc_mac(&a, nonce, aad, al, out, plen, x);
    ctr(&a, nonce, 0, s);
    uint8_t d = 0;
    for (int k = 0; k < CCMP_MIC_LEN; k++) d |= (uint8_t)((x[k] ^ s[k]) ^ c[plen + k]);
    return d ? -1 : (int)plen;
}
