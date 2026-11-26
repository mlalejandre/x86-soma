/* SOMA_PARCHE_NAVEGADOR: decodificador JPEG progresivo / base (enteros). Sobremuestreo por replicacion. */
#include "web_jpeg.h"
#include "web_util.h"
/* PARCHE 086: las reservas que fallan se anotan (el navegador distingue "sin memoria" de "imagen danada") */
static void *wj_alloc(size_t n) { void *p = kmalloc(n); if (!p) wi_oom = 1; return p; }
#define kmalloc(n) wj_alloc(n)

struct jh { uint8_t bits[17]; uint8_t vals[256]; int16_t look[512]; int32_t maxcode[18]; int32_t valptr[17]; int32_t mincode[17]; int ok; };
struct jc { int id, h, v, tq, bw, bh, w, hh; int16_t *coef; int dcpred; int td, ta; };
struct jd {
    const uint8_t *d; uint32_t n, pos;
    int W, H, ncomp, prog, hmax, vmax, mcux, mcuy, ri, adobe;
    struct jc c[4]; uint16_t q[4][64]; struct jh dc[4], ac[4];
    uint32_t bitbuf; int bitcnt, eof; int eobrun; int marker_hit;
};
static const uint8_t zz[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static void jh_build(struct jh *h)
{
    int code = 0, k = 0; memset(h->look, -1, sizeof h->look);
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k; h->mincode[l] = code;
        for (int i = 0; i < h->bits[l]; i++, k++, code++) {
            if (l <= 9 && code < (1 << l)) {   /* PARCHE 029: tabla DHT sobresuscrita => escribia fuera de look[] */
                int sh = 9 - l; for (int f = 0; f < (1 << sh); f++) h->look[(code << sh) | f] = (int16_t)((h->vals[k] << 4) | l); }
        }
        h->maxcode[l] = h->bits[l] ? code - 1 : -1; code <<= 1;
    }
    h->maxcode[17] = 0x7fffffff; h->ok = 1;
}
static void fill(struct jd *J)
{
    while (J->bitcnt <= 24) {
        int b = 0;
        if (J->marker_hit || J->pos >= J->n) { J->marker_hit = 1; b = 0; }
        else {
            b = J->d[J->pos++];
            if (b == 0xFF) {
                int b2 = J->pos < J->n ? J->d[J->pos] : 0xD9;
                if (b2 == 0) J->pos++;
                else { J->pos--; J->marker_hit = 1; b = 0; }
            }
        }
        J->bitbuf |= (uint32_t)b << (24 - J->bitcnt); J->bitcnt += 8;
    }
}
static int getbit(struct jd *J, int n) { if (!n) return 0; if (J->bitcnt < n) fill(J); int v = (int)(J->bitbuf >> (32 - n)); J->bitbuf <<= n; J->bitcnt -= n; return v; }
static int extend(int v, int t) { return v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }
static int hdec(struct jd *J, const struct jh *h)
{
    if (J->bitcnt < 16) fill(J);
    int f = h->look[J->bitbuf >> 23];
    if (f >= 0) { int l = f & 15; J->bitbuf <<= l; J->bitcnt -= l; return f >> 4; }
    int32_t code = (int32_t)(J->bitbuf >> 22);   /* 10 bits */
    int l = 10;
    for (;; l++) {
        if (l > 16) return 0;
        code = (int32_t)(J->bitbuf >> (32 - l));
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) break;
    }
    J->bitbuf <<= l; J->bitcnt -= l;
    int idx = h->valptr[l] + (code - h->mincode[l]); return idx < 256 ? h->vals[idx] : 0;
}
static int rcvx(struct jd *J, int t) { if (!t) return 0; int v = getbit(J, t); return extend(v, t); }

static void decode_block(struct jd *J, struct jc *c, int16_t *blk, int Ss, int Se, int Ah, int Al)
{
    if (!J->prog) {
        int t = hdec(J, &J->dc[c->td]); int diff = t ? rcvx(J, t & 15) : 0; c->dcpred += diff; blk[0] = (int16_t)c->dcpred;
        for (int k = 1; k < 64; ) { int rs = hdec(J, &J->ac[c->ta]), r = rs >> 4, s = rs & 15; if (!s) { if (r == 15) { k += 16; continue; } break; } k += r; if (k > 63) break; blk[zz[k++]] = (int16_t)rcvx(J, s); }
        return;
    }
    if (Ss == 0) {
        if (Ah == 0) { int t = hdec(J, &J->dc[c->td]); int diff = t ? rcvx(J, t & 15) : 0; c->dcpred += diff; blk[0] = (int16_t)(c->dcpred * (1 << Al)); }
        else { if (getbit(J, 1)) blk[0] |= (int16_t)(1 << Al); }
        return;
    }
    if (Ah == 0) {
        if (J->eobrun > 0) { J->eobrun--; return; }
        for (int k = Ss; k <= Se; ) {
            int rs = hdec(J, &J->ac[c->ta]), r = rs >> 4, s = rs & 15;
            if (!s) { if (r < 15) { J->eobrun = (1 << r) - 1; if (r) J->eobrun += getbit(J, r); break; } k += 16; continue; }
            k += r; if (k > 63) break; blk[zz[k]] = (int16_t)(rcvx(J, s) * (1 << Al)); k++;
        }
        return;
    }
    /* refinamiento AC */
    int p1 = 1 << Al, m1 = -1 * (1 << Al), k = Ss;
    if (J->eobrun <= 0) {
        for (; k <= Se; k++) {
            int rs = hdec(J, &J->ac[c->ta]), r = rs >> 4, s = rs & 15, val = 0;
            if (s) { val = getbit(J, 1) ? p1 : m1; }
            else if (r != 15) { J->eobrun = 1 << r; if (r) J->eobrun += getbit(J, r); break; }
            do {
                int16_t *cf = &blk[zz[k]];
                if (*cf) { if (getbit(J, 1)) { if ((*cf & p1) == 0) *cf = (int16_t)(*cf >= 0 ? *cf + p1 : *cf + m1); } }
                else { if (--r < 0) break; }
                k++;
            } while (k <= Se);
            if (val && k <= Se) blk[zz[k]] = (int16_t)val;
        }
    }
    if (J->eobrun > 0) {
        for (; k <= Se; k++) { int16_t *cf = &blk[zz[k]]; if (*cf) { if (getbit(J, 1)) { if ((*cf & p1) == 0) *cf = (int16_t)(*cf >= 0 ? *cf + p1 : *cf + m1); } } }
        J->eobrun--;
    }
}

static int scan(struct jd *J, int *cs, int ncs, int Ss, int Se, int Ah, int Al)
{
    J->bitbuf = 0; J->bitcnt = 0; J->marker_hit = 0; J->eobrun = 0;
    for (int i = 0; i < J->ncomp; i++) J->c[i].dcpred = 0;
    int restarts = 0, total, mx, my;
    if (ncs == 1) { struct jc *c = &J->c[cs[0]]; mx = (c->w + 7) / 8; my = (c->hh + 7) / 8; total = mx * my; }
    else { mx = J->mcux; my = J->mcuy; total = mx * my; }
    for (int m = 0; m < total; m++) {
        if ((m & 63) == 0) wu_tick();
        if (J->ri && m && m % J->ri == 0) {
            J->bitbuf = 0; J->bitcnt = 0; J->eobrun = 0;
            /* busca marcador RSTn */
            while (J->pos + 1 < J->n && !(J->d[J->pos] == 0xFF && J->d[J->pos + 1] >= 0xD0 && J->d[J->pos + 1] <= 0xD7)) { if (J->d[J->pos] == 0xFF && J->d[J->pos + 1] != 0 && J->d[J->pos + 1] != 0xFF) break; J->pos++; }
            if (J->pos + 1 < J->n && J->d[J->pos] == 0xFF && J->d[J->pos + 1] >= 0xD0 && J->d[J->pos + 1] <= 0xD7) J->pos += 2;
            J->marker_hit = 0; for (int i = 0; i < J->ncomp; i++) J->c[i].dcpred = 0; restarts++;
        }
        if (ncs == 1) {
            struct jc *c = &J->c[cs[0]]; int bx = m % mx, by = m / mx;
            decode_block(J, c, c->coef + ((size_t)by * c->bw + bx) * 64, Ss, Se, Ah, Al);
        } else {
            int mxx = m % mx, myy = m / mx;
            for (int k = 0; k < ncs; k++) {
                struct jc *c = &J->c[cs[k]];
                for (int v = 0; v < c->v; v++) for (int h = 0; h < c->h; h++) {
                    int bx = mxx * c->h + h, by = myy * c->v + v;
                    decode_block(J, c, c->coef + ((size_t)by * c->bw + bx) * 64, Ss, Se, Ah, Al);
                }
            }
        }
        if (J->marker_hit && J->bitcnt <= 0 && J->pos >= J->n) break;
    }
    (void)restarts;
    return 0;
}

#define DESC(x, n) (((x) + (1 << ((n) - 1))) >> (n))
/* PARCHE 029: coeficiente desquantizado acotado (con tablas o datos corruptos el producto desbordaba int32). */
static inline int32_t DQ(int16_t c, uint16_t q) { int32_t v = (int32_t)c * (int32_t)q; return v > 16383 ? 16383 : (v < -16383 ? -16383 : v); }
static void idct(const int16_t *in, const uint16_t *q, uint8_t *out, int stride)
{
    int32_t ws[64], t0, t1, t2, t3, t10, t11, t12, t13, z1, z2, z3, z4, z5;
    for (int c = 0; c < 8; c++) {
        const int16_t *p = in + c; const uint16_t *qq = q + c; int32_t *w = ws + c;
        if (!p[8] && !p[16] && !p[24] && !p[32] && !p[40] && !p[48] && !p[56]) { int32_t dc = (DQ(p[0], qq[0])) * 4; for (int r = 0; r < 8; r++) w[r * 8] = dc; continue; }
        z2 = DQ(p[16], qq[16]); z3 = DQ(p[48], qq[48]); z1 = (z2 + z3) * 4433; t2 = z1 + z3 * -15137; t3 = z1 + z2 * 6270;
        z2 = DQ(p[0], qq[0]); z3 = DQ(p[32], qq[32]); t0 = (z2 + z3) * 8192; t1 = (z2 - z3) * 8192;
        t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2;
        t0 = DQ(p[56], qq[56]); t1 = DQ(p[40], qq[40]); t2 = DQ(p[24], qq[24]); t3 = DQ(p[8], qq[8]);
        z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3; z5 = (z3 + z4) * 9633;
        t0 *= 2446; t1 *= 16819; t2 *= 25172; t3 *= 12299; z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196; z3 += z5; z4 += z5;
        t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
        w[0] = DESC(t10 + t3, 11); w[56] = DESC(t10 - t3, 11); w[8] = DESC(t11 + t2, 11); w[48] = DESC(t11 - t2, 11);
        w[16] = DESC(t12 + t1, 11); w[40] = DESC(t12 - t1, 11); w[24] = DESC(t13 + t0, 11); w[32] = DESC(t13 - t0, 11);
    }
    /* PARCHE 029: con coeficientes corruptos el segundo paso desbordaba int32 (comportamiento indefinido). Los datos validos
       nunca se acercan a este limite, asi que no cambia ninguna imagen buena. */
    for (int i = 0; i < 64; i++) { if (ws[i] > 16383) ws[i] = 16383; else if (ws[i] < -16383) ws[i] = -16383; }
    for (int r = 0; r < 8; r++) {
        const int32_t *w = ws + r * 8; uint8_t *o = out + r * stride;
        z2 = w[2]; z3 = w[6]; z1 = (z2 + z3) * 4433; t2 = z1 + z3 * -15137; t3 = z1 + z2 * 6270;
        t0 = (w[0] + w[4]) * 8192; t1 = (w[0] - w[4]) * 8192; t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2;
        t0 = w[7]; t1 = w[5]; t2 = w[3]; t3 = w[1];
        z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3; z5 = (z3 + z4) * 9633;
        t0 *= 2446; t1 *= 16819; t2 *= 25172; t3 *= 12299; z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196; z3 += z5; z4 += z5;
        t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
        int32_t v[8] = { DESC(t10 + t3, 18), DESC(t11 + t2, 18), DESC(t12 + t1, 18), DESC(t13 + t0, 18), DESC(t13 - t0, 18), DESC(t12 - t1, 18), DESC(t11 - t2, 18), DESC(t10 - t3, 18) };
        for (int i = 0; i < 8; i++) { int x = v[i] + 128; o[i] = (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x)); }
    }
}

/* SOMA_342_NAVEGADOR_AGIL: IDCT reducida (como jidctred de libjpeg): con scale 2 o 4 se calculaba la IDCT 8x8 entera y despues se
 * promediaban los pixeles, asi que reducir apenas ahorraba trabajo. Ahora sale directamente un bloque de NxN (N = 8/scale) usando solo
 * los coeficientes de frecuencia baja: T[x][u] = a(u) cos((2x+1)u pi / 2N) * 4096, con a(0) = 1 y a(u) = raiz de 2, la misma escala
 * de continua que la IDCT 8x8 (un bloque plano da el mismo valor). Solo enteros: tambien vale en el nucleo de arm64. */
int wj_red_off;                     /* 1 = como antes (IDCT 8x8 y promedio): solo para comparar en las pruebas */
static const int32_t IDR4[4][4] = { { 4096, 5352, 4096, 2217 }, { 4096, 2217, -4096, -5352 }, { 4096, -2217, -4096, 5352 }, { 4096, -5352, 4096, -2217 } };
static const int32_t IDR2[2][2] = { { 4096, 4096 }, { 4096, -4096 } };
static void idct_red(const int16_t *in, const uint16_t *q, uint8_t *out, int stride, int N)
{
    const int32_t *T = N == 4 ? &IDR4[0][0] : &IDR2[0][0];
    int32_t r[4][4];
    for (int v = 0; v < N; v++)
        for (int x = 0; x < N; x++) {
            int32_t s = 0;
            for (int u = 0; u < N; u++) s += DQ(in[v * 8 + u], q[v * 8 + u]) * T[x * N + u];
            r[v][x] = DESC(s, 8);
        }
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int64_t s = 0;
            for (int v = 0; v < N; v++) s += (int64_t)r[v][x] * T[y * N + v];
            int32_t p = (int32_t)((s + (1 << 18)) >> 19) + 128;
            out[y * stride + x] = (uint8_t)(p < 0 ? 0 : (p > 255 ? 255 : p));
        }
}

/* PARCHE 086: salida con reduccion en el dominio DCT (scale 1, 2, 4 u 8: la imagen sale ya pequena y gasta 1/scale^2 de memoria),
 * croma con interpolacion bilineal (antes se repetia el pixel: bordes de color en escalera) y JPEG de 4 componentes (CMYK / YCCK de Adobe). */
int wj_decode_progressive(const uint8_t *d, uint32_t n, struct wimg *o) { return wj_decode(d, n, o, 1); }
static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
int wj_decode(const uint8_t *d, uint32_t n, struct wimg *o, int scale)
{
    if (scale != 2 && scale != 4 && scale != 8) scale = 1;
    struct jd *J = (struct jd *)kmalloc(sizeof(*J)); if (!J) return -1;
    memset(J, 0, sizeof *J); J->d = d; J->n = n; J->pos = 2; J->adobe = -1;
    int rc = -1, got_sof = 0; uint8_t *planes[4] = { 0, 0, 0, 0 };
    while (J->pos + 4 <= n) {
        if (d[J->pos] != 0xFF) { J->pos++; continue; }
        int m = d[J->pos + 1]; J->pos += 2;
        if (m == 0xFF || m == 0 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { if (m == 0xFF) J->pos--; continue; }
        if (m == 0xD9) break;
        if (J->pos + 2 > n) break;
        uint32_t len = ((uint32_t)d[J->pos] << 8) | d[J->pos + 1]; if (len < 2 || J->pos + len > n) break;
        const uint8_t *p = d + J->pos + 2; uint32_t pl = len - 2;
        if (m == 0xDB) {
            while (pl >= 65 || (pl >= 1 && pl > 0)) { int pq = p[0] >> 4, tq = p[0] & 15; uint32_t need = pq ? 129u : 65u; if (pl < need || tq > 3) break; for (int i = 0; i < 64; i++) J->q[tq][zz[i]] = pq ? (uint16_t)((p[1 + i * 2] << 8) | p[2 + i * 2]) : p[1 + i]; p += need; pl -= need; }
        } else if (m == 0xC4) {
            while (pl >= 17) {
                int tc = p[0] >> 4, th = p[0] & 15; if (th > 3) break; struct jh *h = tc ? &J->ac[th] : &J->dc[th]; int tot = 0;
                memset(h->bits, 0, sizeof h->bits); for (int i = 1; i <= 16; i++) { h->bits[i] = p[i]; tot += p[i]; }
                if (tot > 256 || pl < 17u + (uint32_t)tot) break;
                memcpy(h->vals, p + 17, (size_t)tot); jh_build(h); p += 17 + tot; pl -= 17 + (uint32_t)tot;
            }
        } else if (m == 0xDD) { if (pl >= 2) J->ri = (p[0] << 8) | p[1]; }
        else if (m == 0xEE && pl >= 12 && !memcmp(p, "Adobe", 5)) J->adobe = p[11];
        else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            if (pl < 6 || p[0] != 8) goto out;
            J->prog = (m == 0xC2); J->H = (p[1] << 8) | p[2]; J->W = (p[3] << 8) | p[4]; J->ncomp = p[5];
            if (J->W <= 0 || J->H <= 0 || (uint64_t)J->W * J->H > WI_MAX_PIXELS || (J->ncomp != 1 && J->ncomp != 3 && J->ncomp != 4) || pl < 6u + J->ncomp * 3u) goto out;
            for (int i = 0; i < J->ncomp; i++) { struct jc *c = &J->c[i]; c->id = p[6 + i * 3]; c->h = p[7 + i * 3] >> 4; c->v = p[7 + i * 3] & 15; c->tq = p[8 + i * 3] & 3; if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) goto out; if (c->h > J->hmax) J->hmax = c->h; if (c->v > J->vmax) J->vmax = c->v; }
            J->mcux = (J->W + 8 * J->hmax - 1) / (8 * J->hmax); J->mcuy = (J->H + 8 * J->vmax - 1) / (8 * J->vmax);
            uint64_t tot = 0;
            for (int i = 0; i < J->ncomp; i++) {
                struct jc *c = &J->c[i]; c->bw = J->mcux * c->h; c->bh = J->mcuy * c->v; c->w = (J->W * c->h + J->hmax - 1) / J->hmax; c->hh = (J->H * c->v + J->vmax - 1) / J->vmax;
                tot += (uint64_t)c->bw * c->bh * 64;
            }
            if (tot > (24u << 20)) goto out;
            for (int i = 0; i < J->ncomp; i++) { struct jc *c = &J->c[i]; c->coef = (int16_t *)kmalloc((size_t)c->bw * c->bh * 64 * 2); if (!c->coef) goto out; memset(c->coef, 0, (size_t)c->bw * c->bh * 64 * 2); }
            got_sof = 1;
        } else if (m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC && m != 0xC4)) goto out;
        else if (m == 0xDA) {
            if (!got_sof || pl < 1) goto out;
            int ns = p[0]; if (ns < 1 || ns > 4 || pl < 4u + ns * 2u) goto out; int cs[4];
            for (int i = 0; i < ns; i++) { int id = p[1 + i * 2], tt = p[2 + i * 2], k; for (k = 0; k < J->ncomp; k++) if (J->c[k].id == id) break; if (k == J->ncomp) k = i < J->ncomp ? i : 0; cs[i] = k; J->c[k].td = tt >> 4 & 3; J->c[k].ta = tt & 3; }
            int Ss = p[1 + ns * 2], Se = p[2 + ns * 2], Ah = p[3 + ns * 2] >> 4, Al = p[3 + ns * 2] & 15;
            if (!J->prog) { Ss = 0; Se = 63; Ah = Al = 0; }
            if (Se > 63) Se = 63;
            J->pos += len; scan(J, cs, ns, Ss, Se, Ah, Al);
            /* tras el escaneo, la posicion esta al final de los datos entropicos o en el marcador siguiente */
            continue;
        }
        J->pos += len;
    }
    if (!got_sof) goto out;
    {
        const int S = scale, OW = (J->W + S - 1) / S, OH = (J->H + S - 1) / S;
        int pw_[4], cw_[4], ch_[4];
        o->px = (uint32_t *)kmalloc((size_t)OW * OH * 4); if (!o->px) goto out;
        for (int i = 0; i < J->ncomp; i++) {
            struct jc *c = &J->c[i]; int bs = 8 / S, pw = c->bw * bs, ph = c->bh * bs; pw_[i] = pw;
            cw_[i] = (c->w + S - 1) / S; ch_[i] = (c->hh + S - 1) / S; if (cw_[i] < 1) cw_[i] = 1; if (ch_[i] < 1) ch_[i] = 1;
            if (cw_[i] > pw) cw_[i] = pw;
            if (ch_[i] > ph) ch_[i] = ph;
            planes[i] = (uint8_t *)kmalloc((size_t)pw * ph); if (!planes[i]) goto out2;
            uint8_t tmp[64];
            for (int by = 0; by < c->bh; by++) {
                if ((by & 7) == 0) wu_tick();
                for (int bx = 0; bx < c->bw; bx++) {
                    const int16_t *cf = c->coef + ((size_t)by * c->bw + bx) * 64;
                    uint8_t *dst = planes[i] + (size_t)by * bs * pw + bx * bs;
                    if (S == 1) { idct(cf, J->q[c->tq], dst, pw); continue; }
                    if (S == 8) { dst[0] = (uint8_t)clamp255(((DQ(cf[0], J->q[c->tq][0]) + 4) >> 3) + 128); continue; }
                    if (!wj_red_off) { idct_red(cf, J->q[c->tq], dst, pw, bs); continue; }   /* SOMA_342_NAVEGADOR_AGIL */
                    idct(cf, J->q[c->tq], tmp, 8);
                    for (int yy = 0; yy < bs; yy++) for (int xx = 0; xx < bs; xx++) {
                        int sum = 0; for (int a = 0; a < S; a++) for (int b = 0; b < S; b++) sum += tmp[(yy * S + a) * 8 + xx * S + b];
                        dst[(size_t)yy * pw + xx] = (uint8_t)((sum + S * S / 2) / (S * S));
                    }
                }
            }
            kfree(c->coef); c->coef = 0;
        }
        /* posicion de cada pixel de salida en cada plano (1/256 de muestra), con centrado de JPEG/JFIF */
        for (int y = 0; y < OH; y++) {
            if ((y & 31) == 0) wu_tick();
            int comp[4];
            for (int x = 0; x < OW; x++) {
                for (int i = 0; i < J->ncomp; i++) {
                    struct jc *c = &J->c[i]; const uint8_t *pl = planes[i]; int pw = pw_[i];
                    if (c->h == J->hmax && c->v == J->vmax) { comp[i] = pl[(size_t)y * pw + x]; continue; }
                    int fx = ((2 * x + 1) * c->h * 128) / J->hmax - 128, fy = ((2 * y + 1) * c->v * 128) / J->vmax - 128;
                    if (fx < 0) fx = 0;
                    if (fy < 0) fy = 0;
                    int x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
                    if (x0 >= cw_[i] - 1) { x0 = cw_[i] - 1; ax = 0; }
                    if (y0 >= ch_[i] - 1) { y0 = ch_[i] - 1; ay = 0; }
                    const uint8_t *r0 = pl + (size_t)y0 * pw, *r1 = ay ? r0 + pw : r0;
                    int x1 = ax ? x0 + 1 : x0;
                    int top = r0[x0] * (256 - ax) + r0[x1] * ax, bot = r1[x0] * (256 - ax) + r1[x1] * ax;
                    comp[i] = (top * (256 - ay) + bot * ay + 32768) >> 16;
                }
                int r, g, b;
                if (J->ncomp == 1) r = g = b = comp[0];
                else {
                    int ycc = J->ncomp == 3 ? (J->adobe != 0) : (J->adobe == 2);
                    if (ycc) {
                        int Y = comp[0], cb = comp[1] - 128, cr = comp[2] - 128;
                        r = clamp255(Y + ((91881 * cr + 32768) >> 16)); g = clamp255(Y - ((22554 * cb + 46802 * cr - 32768) >> 16)); b = clamp255(Y + ((116130 * cb + 32768) >> 16));
                    } else { r = comp[0]; g = comp[1]; b = comp[2]; }
                    if (J->ncomp == 4) {   /* CMYK: con marcador Adobe los valores van invertidos (lo habitual de Photoshop) */
                        int k = comp[3];
                        if (ycc) { r = 255 - r; g = 255 - g; b = 255 - b; }   /* YCCK: la transformada da el complemento */
                        if (J->adobe >= 0) { r = r * k / 255; g = g * k / 255; b = b * k / 255; }
                        else { r = (255 - r) * (255 - k) / 255; g = (255 - g) * (255 - k) / 255; b = (255 - b) * (255 - k) / 255; }
                    }
                }
                o->px[(size_t)y * OW + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        o->w = OW; o->h = OH; o->nw = J->W; o->nh = J->H; rc = 0;
    }
out2:
    if (rc < 0 && o->px) { kfree(o->px); o->px = 0; }
out:
    for (int i = 0; i < 4; i++) { if (planes[i]) kfree(planes[i]); if (J->c[i].coef) kfree(J->c[i].coef); }
    kfree(J);
    return rc;
}
