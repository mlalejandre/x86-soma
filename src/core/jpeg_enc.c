/* SOMA_179_T480S: codificador JPEG baseline. Ver jpeg_enc.h */
#include <stdint.h>
#include <stddef.h>
#include "jpeg_enc.h"
#include "uvc.h"

static const uint8_t ZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const uint8_t QL[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const uint8_t QC[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };

struct hcode { uint16_t code; uint8_t len; };
struct enc {
    uint8_t *o; size_t cap, n; int fail;
    uint32_t acc; int nb;
    struct hcode dc[2][16], ac[2][256];
    uint32_t rq[2][64];            /* reciprocos de 8*q en orden natural */
    uint8_t qz[2][64];             /* q en orden zigzag (para DQT) */
    int pred[3];
};

static void put8(struct enc *e, uint8_t b) { if (e->n < e->cap) e->o[e->n++] = b; else e->fail = 1; }
static void put16(struct enc *e, uint32_t v) { put8(e, (uint8_t)(v >> 8)); put8(e, (uint8_t)v); }
static void bits(struct enc *e, uint32_t v, int n)
{
    if (!n) return;
    e->acc = (e->acc << n) | (v & ((1u << n) - 1u)); e->nb += n;
    while (e->nb >= 8) { uint8_t b = (uint8_t)(e->acc >> (e->nb - 8)); e->nb -= 8; put8(e, b); if (b == 0xFF) put8(e, 0); }
}
static void flush(struct enc *e) { if (e->nb) bits(e, 0x7F, 8 - e->nb); e->acc = 0; e->nb = 0; }

/* tabla canonica a partir de los contadores y valores del segmento DHT estandar (uvc_std_dht_build) */
static const uint8_t *build_table(const uint8_t *p, struct hcode *t, int nsym)
{
    const uint8_t *cnt = p, *val = p + 16; int total = 0; uint32_t code = 0; int k = 0;
    for (int i = 0; i < nsym; i++) { t[i].code = 0; t[i].len = 0; }
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < cnt[l - 1]; i++) { if (val[k] < nsym) { t[val[k]].code = (uint16_t)code; t[val[k]].len = (uint8_t)l; } code++; k++; }
        code <<= 1; total += cnt[l - 1];
    }
    return val + total;
}

/* LL&M (jfdctint de libjpeg): la salida queda multiplicada por 8 */
#define CB 13
#define P1 2
#define DESC(x, n) (((x) + (1 << ((n) - 1))) >> (n))
static void fdct(int32_t *d)
{
    for (int pass = 0; pass < 2; pass++) {
        int32_t *q = d; int step = pass ? 8 : 1, stride2 = pass ? 1 : 8;
        for (int r = 0; r < 8; r++, q += stride2) {
            int32_t d0 = q[0], d1 = q[step], d2 = q[2 * step], d3 = q[3 * step], d4 = q[4 * step], d5 = q[5 * step], d6 = q[6 * step], d7 = q[7 * step];
            int32_t t0 = d0 + d7, t7 = d0 - d7, t1 = d1 + d6, t6 = d1 - d6, t2 = d2 + d5, t5 = d2 - d5, t3 = d3 + d4, t4 = d3 - d4;
            int32_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
            int sh = pass ? CB + P1 : CB - P1;
            q[0] = pass ? DESC(t10 + t11, P1) : (t10 + t11) * (1 << P1);
            q[4 * step] = pass ? DESC(t10 - t11, P1) : (t10 - t11) * (1 << P1);
            int32_t z1 = (t12 + t13) * 4433;
            q[2 * step] = DESC(z1 + t13 * 6270, sh);
            q[6 * step] = DESC(z1 + t12 * -15137, sh);
            z1 = t4 + t7; int32_t z2 = t5 + t6, z3 = t4 + t6, z4 = t5 + t7, z5 = (z3 + z4) * 9633;
            t4 *= 2446; t5 *= 16819; t6 *= 25172; t7 *= 12299;
            z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196;
            z3 += z5; z4 += z5;
            q[7 * step] = DESC(t4 + z1 + z3, sh); q[5 * step] = DESC(t5 + z2 + z4, sh);
            q[3 * step] = DESC(t6 + z2 + z3, sh); q[1 * step] = DESC(t7 + z1 + z4, sh);
        }
    }
}

static void block(struct enc *e, int32_t *b, int comp, int tq)
{
    fdct(b);
    int td = tq;                                           /* tabla Huffman: 0 luminancia, 1 crominancia */
    int32_t qv[64];
    for (int i = 0; i < 64; i++) {
        int32_t c = b[ZZ[i]]; uint32_t r = e->rq[tq][ZZ[i]];
        int64_t m = (int64_t)(c < 0 ? -c : c) * r + 32768; int32_t a = (int32_t)(m >> 16);
        qv[i] = c < 0 ? -a : a;
    }
    int diff = qv[0] - e->pred[comp]; e->pred[comp] = qv[0];
    int a = diff < 0 ? -diff : diff, nbit = 0; while (a) { nbit++; a >>= 1; }
    bits(e, e->dc[td][nbit].code, e->dc[td][nbit].len);
    if (nbit) bits(e, (uint32_t)(diff < 0 ? diff - 1 : diff), nbit);
    int run = 0;
    for (int i = 1; i < 64; i++) {
        int v = qv[i];
        if (!v) { run++; continue; }
        while (run > 15) { bits(e, e->ac[td][0xF0].code, e->ac[td][0xF0].len); run -= 16; }
        a = v < 0 ? -v : v; nbit = 0; while (a) { nbit++; a >>= 1; }
        int sym = (run << 4) | nbit;
        bits(e, e->ac[td][sym].code, e->ac[td][sym].len);
        bits(e, (uint32_t)(v < 0 ? v - 1 : v), nbit);
        run = 0;
    }
    if (run) bits(e, e->ac[td][0].code, e->ac[td][0].len);
}

size_t jpeg_encode_rgb(const uint32_t *argb, int w, int h, int stride, int quality, uint8_t *out, size_t cap)
{
    if (!argb || !out || w < 1 || h < 1 || w > 4096 || h > 4096 || stride < w || cap < 1024) return 0;
    struct enc E; struct enc *e = &E;
    e->o = out; e->cap = cap; e->n = 0; e->fail = 0; e->acc = 0; e->nb = 0; e->pred[0] = e->pred[1] = e->pred[2] = 0;
    if (quality < 1) { quality = 1; }
    if (quality > 100) { quality = 100; }
    int sc = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    for (int t = 0; t < 2; t++) {
        const uint8_t *base = t ? QC : QL;
        for (int i = 0; i < 64; i++) {
            e->qz[t][i] = 0; e->rq[t][i] = 0;
        }
        for (int i = 0; i < 64; i++) { int v = (base[ZZ[i]] * sc + 50) / 100; if (v < 1) { v = 1; } if (v > 255) { v = 255; } e->qz[t][i] = (uint8_t)v; e->rq[t][ZZ[i]] = (uint32_t)((65536 + v * 4) / (v * 8)); }
    }
    uint8_t dht[420]; uvc_std_dht_build(dht);
    const uint8_t *p = dht + 4;                            /* tras FFC4 + longitud */
    p = build_table(p + 1, e->dc[0], 16); p = build_table(p + 1, e->ac[0], 256);
    p = build_table(p + 1, e->dc[1], 16); (void)build_table(p + 1, e->ac[1], 256);
    put8(e, 0xFF); put8(e, 0xD8);
    put8(e, 0xFF); put8(e, 0xE0); put16(e, 16); put8(e, 'J'); put8(e, 'F'); put8(e, 'I'); put8(e, 'F'); put8(e, 0); put16(e, 0x0101); put8(e, 0); put16(e, 1); put16(e, 1); put8(e, 0); put8(e, 0);
    for (int t = 0; t < 2; t++) { put8(e, 0xFF); put8(e, 0xDB); put16(e, 67); put8(e, (uint8_t)t); for (int i = 0; i < 64; i++) put8(e, e->qz[t][i]); }
    put8(e, 0xFF); put8(e, 0xC0); put16(e, 17); put8(e, 8); put16(e, (uint32_t)h); put16(e, (uint32_t)w); put8(e, 3);
    put8(e, 1); put8(e, 0x22); put8(e, 0); put8(e, 2); put8(e, 0x11); put8(e, 1); put8(e, 3); put8(e, 0x11); put8(e, 1);
    for (int i = 0; i < 420; i++) put8(e, dht[i]);
    put8(e, 0xFF); put8(e, 0xDA); put16(e, 12); put8(e, 3); put8(e, 1); put8(e, 0x00); put8(e, 2); put8(e, 0x11); put8(e, 3); put8(e, 0x11); put8(e, 0); put8(e, 63); put8(e, 0);
    int32_t y[4][64], cb[64], cr[64];
    for (int my = 0; my < h; my += 16) {
        for (int mx = 0; mx < w; mx += 16) {
            int32_t cbs[16][16], crs[16][16];
            for (int j = 0; j < 16; j++) {
                int sy = my + j; if (sy >= h) sy = h - 1;
                for (int i = 0; i < 16; i++) {
                    int sx = mx + i; if (sx >= w) sx = w - 1;
                    uint32_t px = argb[(size_t)sy * (size_t)stride + (size_t)sx];
                    int r = (int)((px >> 16) & 255), g = (int)((px >> 8) & 255), bl = (int)(px & 255);
                    y[(j >> 3) * 2 + (i >> 3)][(j & 7) * 8 + (i & 7)] = ((19595 * r + 38470 * g + 7471 * bl + 32768) >> 16) - 128;
                    cbs[j][i] = (-11059 * r - 21709 * g + 32768 * bl + 32768) >> 16;
                    crs[j][i] = (32768 * r - 27439 * g - 5329 * bl + 32768) >> 16;
                }
            }
            for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
                cb[j * 8 + i] = (cbs[2 * j][2 * i] + cbs[2 * j][2 * i + 1] + cbs[2 * j + 1][2 * i] + cbs[2 * j + 1][2 * i + 1] + 2) >> 2;
                cr[j * 8 + i] = (crs[2 * j][2 * i] + crs[2 * j][2 * i + 1] + crs[2 * j + 1][2 * i] + crs[2 * j + 1][2 * i + 1] + 2) >> 2;
            }
            for (int k = 0; k < 4; k++) block(e, y[k], 0, 0);
            block(e, cb, 1, 1); block(e, cr, 2, 1);
            if (e->fail) return 0;
        }
    }
    flush(e);
    put8(e, 0xFF); put8(e, 0xD9);
    return e->fail ? 0 : e->n;
}
