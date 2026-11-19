/* SOMA_PARCHE_NAVEGADOR: decodificadores PNG / GIF / BMP (+ enlace con el JPEG del sistema). Solo enteros. */
#include "web_img.h"
#include "web_inflate.h"
#include "web_util.h"
#include "image.h"
#include "jpeg.h"
#include "web_jpeg.h"
int wi_oom;
static void *wi_alloc(size_t n) { void *p = kmalloc(n); if (!p) wi_oom = 1; return p; }   /* PARCHE 086 */
#define kmalloc(n) wi_alloc(n)

void wi_free(struct wimg *o) { if (o && o->px) kfree(o->px); if (o && o->delay) kfree(o->delay); if (o) { o->px = 0; o->w = o->h = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0; } }

/* ------------------------------ PNG ------------------------------ */
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p; return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }

/* des-filtra una imagen (o pasada) de w x h: cada fila = 1 byte de filtro + stride bytes */
static int unfilter(uint8_t *d, int h, int stride, int bpp)
{
    uint8_t *prev = 0;
    for (int y = 0; y < h; y++) {
        uint8_t *row = d + (size_t)y * (stride + 1); int f = row[0]; row++;
        switch (f) {
        case 0: break;
        case 1: for (int i = bpp; i < stride; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]); break;
        case 2: if (prev) for (int i = 0; i < stride; i++) row[i] = (uint8_t)(row[i] + prev[i]); break;
        case 3: for (int i = 0; i < stride; i++) { int a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0; row[i] = (uint8_t)(row[i] + ((a + b) >> 1)); } break;
        case 4: for (int i = 0; i < stride; i++) { int a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0, c = (i >= bpp && prev) ? prev[i - bpp] : 0; row[i] = (uint8_t)(row[i] + paeth(a, b, c)); } break;
        default: return -1;
        }
        prev = row;
    }
    return 0;
}

struct pngi { int w, h, depth, ctype, interlace; uint32_t pal[256]; int npal; int has_trns; uint16_t trns_g, trns_r, trns_gg, trns_b; uint8_t trns_a[256]; };

static uint32_t png_px(const struct pngi *I, const uint8_t *row, int x)
{
    int d = I->depth;
    switch (I->ctype) {
    case 0: {
        int v, maxv = (1 << d) - 1;
        if (d == 16) { v = (row[x * 2] << 8) | row[x * 2 + 1]; int g = row[x * 2]; uint32_t a = (I->has_trns && v == I->trns_g) ? 0 : 255; return (a << 24) | (g << 16) | (g << 8) | g; }
        int bit = x * d; v = (row[bit >> 3] >> (8 - d - (bit & 7))) & maxv; int g = v * 255 / maxv;
        uint32_t a = (I->has_trns && v == I->trns_g) ? 0 : 255; return (a << 24) | (g << 16) | (g << 8) | g; }
    case 2: {
        int r, g, b, rv, gv, bv;
        if (d == 16) { rv = (row[x * 6] << 8) | row[x * 6 + 1]; gv = (row[x * 6 + 2] << 8) | row[x * 6 + 3]; bv = (row[x * 6 + 4] << 8) | row[x * 6 + 5]; r = row[x * 6]; g = row[x * 6 + 2]; b = row[x * 6 + 4]; }
        else { r = rv = row[x * 3]; g = gv = row[x * 3 + 1]; b = bv = row[x * 3 + 2]; }
        uint32_t a = (I->has_trns && rv == I->trns_r && gv == I->trns_gg && bv == I->trns_b) ? 0 : 255;
        return (a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
    case 3: {
        int bit = x * d; int v = (row[bit >> 3] >> (8 - d - (bit & 7))) & ((1 << d) - 1);
        if (v >= I->npal) return 0xFF000000u;
        uint32_t c = I->pal[v] & 0xFFFFFF; uint32_t a = I->has_trns ? I->trns_a[v] : 255; return (a << 24) | c; }
    case 4: {
        int g, a; if (d == 16) { g = row[x * 4]; a = row[x * 4 + 2]; } else { g = row[x * 2]; a = row[x * 2 + 1]; }
        return ((uint32_t)a << 24) | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g; }
    default: {
        int r, g, b, a; if (d == 16) { r = row[x * 8]; g = row[x * 8 + 2]; b = row[x * 8 + 4]; a = row[x * 8 + 6]; } else { r = row[x * 4]; g = row[x * 4 + 1]; b = row[x * 4 + 2]; a = row[x * 4 + 3]; }
        return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
    }
}

static int png_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    struct pngi I; memset(&I, 0, sizeof I); struct wb idat = {0};
    uint32_t pos = 8; int got_hdr = 0, rc = -1;
    while (pos + 12 <= n) {
        uint32_t len = be32(d + pos); const uint8_t *t = d + pos + 4, *c = d + pos + 8;
        if (len > n || pos + 12 + len > n) break;
        if (!memcmp(t, "IHDR", 4) && len >= 13) {
            I.w = (int)be32(c); I.h = (int)be32(c + 4); I.depth = c[8]; I.ctype = c[9]; I.interlace = c[12]; got_hdr = 1;
            if (I.w <= 0 || I.h <= 0 || (uint64_t)I.w * (uint64_t)I.h > WI_MAX_PIXELS) goto out;
        } else if (!memcmp(t, "PLTE", 4)) { I.npal = (int)(len / 3); if (I.npal > 256) I.npal = 256; for (int i = 0; i < I.npal; i++) I.pal[i] = 0xFF000000u | (c[i * 3] << 16) | (c[i * 3 + 1] << 8) | c[i * 3 + 2]; }
        else if (!memcmp(t, "tRNS", 4)) {
            I.has_trns = 1;
            if (I.ctype == 3) { for (uint32_t i = 0; i < len && i < 256; i++) I.trns_a[i] = c[i]; for (uint32_t i = len; i < 256; i++) I.trns_a[i] = 255; }
            else if (I.ctype == 0 && len >= 2) I.trns_g = (uint16_t)((c[0] << 8) | c[1]);
            else if (I.ctype == 2 && len >= 6) { I.trns_r = (uint16_t)((c[0] << 8) | c[1]); I.trns_gg = (uint16_t)((c[2] << 8) | c[3]); I.trns_b = (uint16_t)((c[4] << 8) | c[5]); }
        } else if (!memcmp(t, "IDAT", 4)) { if (idat.len + len > (48u << 20)) goto out; wb_addn(&idat, (const char *)c, len); if (idat.err) goto out; }
        else if (!memcmp(t, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!got_hdr || !idat.len) goto out;
    int chans = I.ctype == 0 ? 1 : I.ctype == 2 ? 3 : I.ctype == 3 ? 1 : I.ctype == 4 ? 2 : 4;
    if (I.ctype != 0 && I.ctype != 2 && I.ctype != 3 && I.ctype != 4 && I.ctype != 6) goto out;
    if (I.depth != 1 && I.depth != 2 && I.depth != 4 && I.depth != 8 && I.depth != 16) goto out;
    int bits = chans * I.depth, bpp = (bits + 7) / 8;
    struct wb raw = {0};
    if (wz_inflate((const uint8_t *)idat.p, idat.len, 1, &raw, 160u << 20) < 0 && raw.len == 0) { wb_free(&raw); goto out; }
    o->px = (uint32_t *)kmalloc((size_t)I.w * I.h * 4); if (!o->px) { wb_free(&raw); goto out; }
    memset(o->px, 0, (size_t)I.w * I.h * 4); o->w = I.w; o->h = I.h;
    uint8_t *data = (uint8_t *)raw.p; size_t off = 0;
    static const int sx[7] = { 0, 4, 0, 2, 0, 1, 0 }, sy[7] = { 0, 0, 4, 0, 2, 0, 1 }, dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = I.interlace ? 7 : 1; rc = 0;
    for (int p = 0; p < passes && rc == 0; p++) {
        int pw = I.interlace ? (I.w - sx[p] + dx[p] - 1) / dx[p] : I.w, ph = I.interlace ? (I.h - sy[p] + dy[p] - 1) / dy[p] : I.h;
        if (pw <= 0 || ph <= 0) continue;
        int stride = (pw * bits + 7) / 8; size_t need = (size_t)ph * (stride + 1);
        if (off + need > raw.len) { /* datos truncados: se rellena lo que haya */ int avail = (int)((raw.len - off) / (stride + 1)); if (avail <= 0) { rc = 0; break; } ph = avail; need = (size_t)ph * (stride + 1); rc = 0; }
        if (unfilter(data + off, ph, stride, bpp) < 0) { rc = -1; break; }
        for (int y = 0; y < ph; y++) {
            if ((y & 15) == 0) wu_tick();
            const uint8_t *row = data + off + (size_t)y * (stride + 1) + 1;
            int Y = I.interlace ? sy[p] + y * dy[p] : y;
            for (int x = 0; x < pw; x++) { int X = I.interlace ? sx[p] + x * dx[p] : x; o->px[(size_t)Y * I.w + X] = png_px(&I, row, x); }
        }
        off += need;
    }
    wb_free(&raw);
    if (rc < 0) { wi_free(o); }
out:
    wb_free(&idat);
    return rc;
}

/* ------------------------------ GIF ------------------------------ */
/* SOMA_180: fotogramas de una animacion ya compuestos. f = factor de reduccion (promedio de bloques) */
static void anim_emit(const uint32_t *canvas, int W, int H, int f, uint32_t *dst)
{
    if (f <= 1) { memcpy(dst, canvas, (size_t)W * H * 4); return; }
    int ow = (W + f - 1) / f, oh = (H + f - 1) / f;
    for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) {
        uint32_t a = 0, r = 0, g = 0, b = 0, c = 0;
        for (int yy = y * f; yy < y * f + f && yy < H; yy++) for (int xx = x * f; xx < x * f + f && xx < W; xx++) {
            uint32_t v = canvas[(size_t)yy * W + xx], al = v >> 24; a += al; r += ((v >> 16) & 255) * al; g += ((v >> 8) & 255) * al; b += (v & 255) * al; c++; }
        dst[(size_t)y * ow + x] = a ? ((a / c) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a) : 0;
    }
}
/* reparte el presupuesto: factor de reduccion y numero de fotogramas que caben */
static void anim_plan(int W, int H, int total, uint32_t maxpix, int *f, int *nfr)
{
    int ff = 1; if (total > WI_ANIM_MAX_FRAMES) total = WI_ANIM_MAX_FRAMES;
    while (ff < 4 && (uint64_t)((W + ff - 1) / ff) * ((H + ff - 1) / ff) * (uint64_t)total > maxpix) ff++;
    uint64_t per = (uint64_t)((W + ff - 1) / ff) * ((H + ff - 1) / ff); int fit = (int)(maxpix / (per ? per : 1));
    if (fit < 1) fit = 1;
    *f = ff; *nfr = total < fit ? total : fit;
}
static int gif_lzw(const uint8_t *src, size_t slen, int minc, uint8_t *idx, size_t total)
{
    uint16_t *prefix = (uint16_t *)kmalloc(4096 * 2 + 4096 + 4096); if (!prefix) return -1;
    uint8_t *suffix = (uint8_t *)(prefix + 4096), *stack = suffix + 4096;
    int clear = 1 << minc, eoi = clear + 1, next = clear + 2, csz = minc + 1, prev = -1; uint32_t bitbuf = 0; int bitcnt = 0; size_t sp = 0, outp = 0, ip = 0; uint8_t first = 0;
    for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
    while (outp < total) {
        while (bitcnt < csz && ip < slen) { bitbuf |= (uint32_t)src[ip++] << bitcnt; bitcnt += 8; }
        if (bitcnt < csz) break;
        int code = (int)(bitbuf & ((1u << csz) - 1)); bitbuf >>= csz; bitcnt -= csz;
        if (code == clear) { next = clear + 2; csz = minc + 1; prev = -1; continue; }
        if (code == eoi) break;
        int cur = code;
        if (prev < 0) { if (code >= clear) break; idx[outp++] = (uint8_t)code; prev = code; first = (uint8_t)code; continue; }
        if (code >= next) { if (code > next) break; stack[sp++] = first; cur = prev; }
        while (cur >= clear && sp < 4095) { stack[sp++] = suffix[cur]; cur = prefix[cur]; if (cur == 0xFFFF) break; }
        if (cur != 0xFFFF) { first = (uint8_t)cur; stack[sp++] = first; }
        while (sp && outp < total) idx[outp++] = stack[--sp];
        sp = 0;
        if (next < 4096) { prefix[next] = (uint16_t)prev; suffix[next] = first; next++; if (next == (1 << csz) && csz < 12) csz++; }
        prev = code;
    }
    kfree(prefix); return 0;
}
static int gif_count(const uint8_t *d, uint32_t n)
{
    if (n < 13 || memcmp(d, "GIF8", 4)) return 0;
    uint32_t pos = 13; int c = 0; if (d[10] & 0x80) pos += 3u * (2u << (d[10] & 7));
    while (pos < n) {
        int b = d[pos++];
        if (b == 0x21) { pos++; while (pos < n && d[pos]) pos += d[pos] + 1u; pos++; }
        else if (b == 0x2C) { if (pos + 9 > n) break; int lf = d[pos + 8]; pos += 9; if (lf & 0x80) pos += 3u * (2u << (lf & 7)); pos++; while (pos < n && d[pos]) pos += d[pos] + 1u; pos++; if (++c > WI_ANIM_MAX_FRAMES) break; }
        else break;
    }
    return c;
}
/* maxpix = 0: solo el primer fotograma (como siempre). Si no, la animacion compuesta (disposicion 1, 2 y 3). */
static int gif_decode(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t maxpix)
{
    if (n < 13) return -1;
    int W = d[6] | (d[7] << 8), H = d[8] | (d[9] << 8), flags = d[10]; uint32_t pos = 13;
    if (W <= 0 || H <= 0 || (uint64_t)W * H > WI_MAX_PIXELS) return -1;
    uint32_t gpal[256]; int gn = 0;
    if (flags & 0x80) { gn = 2 << (flags & 7); if (pos + (uint32_t)gn * 3 > n) return -1; for (int i = 0; i < gn; i++) gpal[i] = 0xFF000000u | (d[pos + i * 3] << 16) | (d[pos + i * 3 + 1] << 8) | d[pos + i * 3 + 2]; pos += (uint32_t)gn * 3; }
    int f = 1, nfr = 1;
    if (maxpix) { int tot = gif_count(d, n); if (tot < 1) return -1; anim_plan(W, H, tot, maxpix, &f, &nfr); }
    int ow = (W + f - 1) / f, oh = (H + f - 1) / f, rc = -1, k = 0;
    size_t cpx = (size_t)W * H;
    uint32_t *canvas = (uint32_t *)kmalloc(cpx * 4), *back = 0, *out = 0; uint16_t *delay = 0;
    if (!canvas) return -1;
    memset(canvas, 0, cpx * 4);
    if (maxpix) { out = (uint32_t *)kmalloc((size_t)ow * oh * 4 * (size_t)nfr); delay = (uint16_t *)kmalloc((size_t)nfr * 2); if (!out || !delay) goto done; }
    int trans = -1, disp = 0, dly = 0;
    while (pos < n && k < nfr) {
        int b = d[pos++];
        if (b == 0x21) {
            if (pos >= n) break;
            int lab = d[pos++];
            if (lab == 0xF9 && pos + 5 < n && d[pos] >= 4) { trans = (d[pos + 1] & 1) ? d[pos + 4] : -1; disp = (d[pos + 1] >> 2) & 7; dly = (d[pos + 2] | (d[pos + 3] << 8)) * 10; }
            while (pos < n && d[pos]) pos += d[pos] + 1u;
            pos++;
        } else if (b == 0x2C) {
            if (pos + 9 > n) break;
            int ix = d[pos] | (d[pos + 1] << 8), iy = d[pos + 2] | (d[pos + 3] << 8), iw = d[pos + 4] | (d[pos + 5] << 8), ih = d[pos + 6] | (d[pos + 7] << 8), lf = d[pos + 8]; pos += 9;
            uint32_t lpal[256]; uint32_t *pal = gpal; int pn = gn;
            if (lf & 0x80) { pn = 2 << (lf & 7); if (pos + (uint32_t)pn * 3 > n) break; for (int i = 0; i < pn; i++) lpal[i] = 0xFF000000u | (d[pos + i * 3] << 16) | (d[pos + i * 3 + 1] << 8) | d[pos + i * 3 + 2]; pal = lpal; pos += (uint32_t)pn * 3; }
            if (pos >= n || iw <= 0 || ih <= 0 || (uint64_t)iw * ih > WI_MAX_PIXELS) break;
            int minc = d[pos++]; if (minc < 2 || minc > 11) break;
            struct wb sub = {0};
            while (pos < n && d[pos]) { uint32_t l = d[pos]; if (pos + 1 + l > n) l = n - pos - 1; wb_addn(&sub, (const char *)d + pos + 1, l); pos += d[pos] + 1u; }
            pos++;
            if (sub.err) { wb_free(&sub); break; }
            uint8_t *idx = (uint8_t *)kmalloc((size_t)iw * ih + 1); if (!idx) { wb_free(&sub); break; }
            memset(idx, 0, (size_t)iw * ih);
            if (gif_lzw((const uint8_t *)sub.p, sub.len, minc, idx, (size_t)iw * ih) != 0) { kfree(idx); wb_free(&sub); break; }
            wb_free(&sub);
            if (disp == 3 && maxpix) { if (!back) back = (uint32_t *)kmalloc(cpx * 4); if (back) memcpy(back, canvas, cpx * 4); }
            int inter = lf & 0x40;
            static const int istart[4] = { 0, 4, 2, 1 }, istep[4] = { 8, 8, 4, 2 };
            int pass = 0, y = 0;
            for (int row = 0; row < ih; row++) {
                int Y = inter ? y : row;
                if (inter) { y += istep[pass]; while (y >= ih && pass < 3) { pass++; y = istart[pass]; } }
                int DY = iy + Y; if (DY < 0 || DY >= H) continue;
                for (int x = 0; x < iw; x++) { int DX = ix + x; if (DX < 0 || DX >= W) continue; int v = idx[(size_t)row * iw + x]; if (v == trans || v >= pn) continue; canvas[(size_t)DY * W + DX] = pal[v]; }
            }
            kfree(idx);
            if (!maxpix) { rc = 0; break; }
            anim_emit(canvas, W, H, f, out + (size_t)ow * oh * (size_t)k); delay[k] = (uint16_t)(dly < 20 ? 100 : dly > 60000 ? 60000 : dly); k++;
            if (disp == 2) { for (int yy = iy < 0 ? 0 : iy; yy < iy + ih && yy < H; yy++) for (int xx = ix < 0 ? 0 : ix; xx < ix + iw && xx < W; xx++) canvas[(size_t)yy * W + xx] = 0; }
            else if (disp == 3 && back) memcpy(canvas, back, cpx * 4);
            trans = -1; disp = 0; dly = 0;
        } else break;
    }
    if (maxpix && k > 0) rc = 0;
done:
    if (back) kfree(back);
    if (rc == 0 && !maxpix) { o->px = canvas; o->w = W; o->h = H; return 0; }
    kfree(canvas);
    if (rc == 0) { o->px = out; o->w = ow; o->h = oh; o->frames = k; o->delay = delay; if (f > 1) { o->nw = W; o->nh = H; } return 0; }
    if (out) kfree(out);
    if (delay) kfree(delay);
    return -1;
}

#ifndef WI_NO_WEB_FORMATS      /* SOMA_180: pdfsvc (PDF) no necesita WebP/SVG/ICO: se compila sin ellos para no engordar la imagen del kernel */
/* ------------------------------ WebP (SOMA_180) ------------------------------ */
/* Decodificador: simplewebp (Google Inc. / Miku AuahDark, BSD de 3 clausulas), derivado de libwebp: VP8 con perdida, VP8L sin perdida y alfa.
 * Aqui se anade el contenedor extendido (VP8X) animado: cada fotograma ANMF se decodifica como un WebP suelto y se compone. */
#define SIMPLEWEBP_NO_LIBC
static int swp_abs(int v) { return v < 0 ? -v : v; }
#define abs(v) swp_abs(v)
#define SIMPLEWEBP_DISABLE_STDIO
#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"
#undef abs
static void *swp_alloc(void *u, size_t n) { (void)u; return kmalloc(n); }
static void swp_free(void *u, void *p) { (void)u; if (p) kfree(p); }
static uint32_t le24(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
static uint32_t le32(const uint8_t *p) { return le24(p) | ((uint32_t)p[3] << 24); }
/* un WebP no animado -> ARGB */
static int webp_one(const uint8_t *d, uint32_t n, uint32_t **px, int *w, int *h)
{
    simplewebp_allocator al = { swp_alloc, swp_free, 0 }; simplewebp *sw = 0; size_t W = 0, H = 0;
    if (simplewebp_load_from_memory((void *)(uintptr_t)d, n, &al, &sw) != SIMPLEWEBP_NO_ERROR || !sw) return -1;
    simplewebp_get_dimensions(sw, &W, &H);
    if (!W || !H || W > 16384 || H > 16384 || (uint64_t)W * H > WI_MAX_PIXELS) { simplewebp_unload(sw); return -1; }
    uint32_t *p = (uint32_t *)kmalloc(W * H * 4); if (!p) { simplewebp_unload(sw); return -1; }
    if (simplewebp_decode(sw, p, 0) != SIMPLEWEBP_NO_ERROR) { kfree(p); simplewebp_unload(sw); return -1; }
    simplewebp_unload(sw);
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < W * H; i++) { uint32_t r = b[i * 4], g = b[i * 4 + 1], bl = b[i * 4 + 2], a = b[i * 4 + 3]; p[i] = (a << 24) | (r << 16) | (g << 8) | bl; }
    *px = p; *w = (int)W; *h = (int)H; return 0;
}
/* recorre los trozos RIFF: devuelve el desplazamiento del siguiente con esa etiqueta (o 0) */
static uint32_t webp_next(const uint8_t *d, uint32_t n, uint32_t pos, const char *tag, uint32_t *len)
{
    while (pos + 8 <= n) {
        uint32_t l = le32(d + pos + 4); if (l > n - pos - 8) return 0;
        if (!memcmp(d + pos, tag, 4)) { *len = l; return pos; }
        pos += 8 + l + (l & 1);
    }
    return 0;
}
static int webp_count(const uint8_t *d, uint32_t n)
{
    uint32_t l, p;
    if (n < 30 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) return 0;
    if (memcmp(d + 12, "VP8X", 4) || !(d[20] & 0x02)) return 1;
    int c = 0; p = 12;
    while ((p = webp_next(d, n, p, "ANMF", &l)) != 0) { c++; p += 8 + l + (l & 1); if (c > WI_ANIM_MAX_FRAMES) break; }
    return c ? c : 1;
}
static int webp_decode(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t maxpix)
{
    if (n < 20 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) return -1;
    int anim = n >= 30 && !memcmp(d + 12, "VP8X", 4) && (d[20] & 0x02);
    if (!anim) { if (webp_one(d, n, &o->px, &o->w, &o->h) != 0) return -1; return 0; }
    int W = (int)le24(d + 24) + 1, H = (int)le24(d + 27) + 1;
    if ((uint64_t)W * H > WI_MAX_PIXELS) return -1;
    int total = webp_count(d, n), f = 1, nfr = 1;
    if (maxpix) anim_plan(W, H, total, maxpix, &f, &nfr);
    int ow = (W + f - 1) / f, oh = (H + f - 1) / f, k = 0; size_t cpx = (size_t)W * H;
    uint32_t *canvas = (uint32_t *)kmalloc(cpx * 4), *out = 0; uint16_t *delay = 0;
    if (!canvas) return -1;
    memset(canvas, 0, cpx * 4);
    if (maxpix) { out = (uint32_t *)kmalloc((size_t)ow * oh * 4 * (size_t)nfr); delay = (uint16_t *)kmalloc((size_t)nfr * 2); if (!out || !delay) { kfree(canvas); if (out) kfree(out); if (delay) kfree(delay); return -1; } }
    uint32_t p = 12, l;
    while (k < nfr && (p = webp_next(d, n, p, "ANMF", &l)) != 0) {
        const uint8_t *a = d + p + 8; uint32_t nextp = p + 8 + l + (l & 1);
        if (l < 16 + 8) break;
        int fx = (int)le24(a) * 2, fy = (int)le24(a + 3) * 2, fw = (int)le24(a + 6) + 1, fh = (int)le24(a + 9) + 1, dur = (int)le24(a + 12), fl = a[15];
        uint32_t pl = l - 16, al_len; int has_alpha = webp_next(a + 16, pl, 0, "ALPH", &al_len) || !memcmp(a + 16, "ALPH", 4);
        uint32_t tot = 12 + (has_alpha ? 18 : 0) + pl; uint8_t *tmp = (uint8_t *)kmalloc(tot + 1); if (!tmp) break;
        memcpy(tmp, "RIFF", 4); uint32_t rs = tot - 8; tmp[4] = (uint8_t)rs; tmp[5] = (uint8_t)(rs >> 8); tmp[6] = (uint8_t)(rs >> 16); tmp[7] = (uint8_t)(rs >> 24); memcpy(tmp + 8, "WEBP", 4);
        uint32_t q = 12;
        if (has_alpha) { memcpy(tmp + q, "VP8X", 4); tmp[q + 4] = 10; tmp[q + 5] = tmp[q + 6] = tmp[q + 7] = 0; tmp[q + 8] = 0x10; tmp[q + 9] = tmp[q + 10] = tmp[q + 11] = 0;
                         tmp[q + 12] = (uint8_t)(fw - 1); tmp[q + 13] = (uint8_t)((fw - 1) >> 8); tmp[q + 14] = (uint8_t)((fw - 1) >> 16); tmp[q + 15] = (uint8_t)(fh - 1); tmp[q + 16] = (uint8_t)((fh - 1) >> 8); tmp[q + 17] = (uint8_t)((fh - 1) >> 16); q += 18; }
        memcpy(tmp + q, a + 16, pl);
        uint32_t *fp = 0; int rw = 0, rh = 0; int r = webp_one(tmp, tot, &fp, &rw, &rh);
        kfree(tmp);
        if (r != 0) break;
        for (int y = 0; y < rh; y++) { int Y = fy + y; if (Y < 0 || Y >= H) continue;
            for (int x = 0; x < rw; x++) { int X = fx + x; if (X < 0 || X >= W) continue; uint32_t s = fp[(size_t)y * rw + x], sa = s >> 24, *dp = &canvas[(size_t)Y * W + X];
                if ((fl & 2) || sa == 255 || (*dp >> 24) == 0) *dp = s;
                else if (sa) { uint32_t dd = *dp, da = dd >> 24, oa = sa + da * (255 - sa) / 255; if (!oa) { *dp = 0; continue; }
                    uint32_t rr = (((s >> 16) & 255) * sa + ((dd >> 16) & 255) * da * (255 - sa) / 255) / oa, gg = (((s >> 8) & 255) * sa + ((dd >> 8) & 255) * da * (255 - sa) / 255) / oa, bb = ((s & 255) * sa + (dd & 255) * da * (255 - sa) / 255) / oa;
                    *dp = (oa << 24) | (rr << 16) | (gg << 8) | bb; } } }
        kfree(fp);
        if (!maxpix) { k = 1; break; }
        anim_emit(canvas, W, H, f, out + (size_t)ow * oh * (size_t)k); delay[k] = (uint16_t)(dur < 20 ? 100 : dur > 60000 ? 60000 : dur); k++;
        if (fl & 1) { for (int y = fy < 0 ? 0 : fy; y < fy + fh && y < H; y++) for (int x = fx < 0 ? 0 : fx; x < fx + fw && x < W; x++) canvas[(size_t)y * W + x] = 0; }
        p = nextp;
    }
    if (!k) { kfree(canvas); if (out) kfree(out); if (delay) kfree(delay); return -1; }
    if (!maxpix) { o->px = canvas; o->w = W; o->h = H; return 0; }
    kfree(canvas);
    o->px = out; o->w = ow; o->h = oh; o->frames = k; o->delay = delay; if (f > 1) { o->nw = W; o->nh = H; }
    return 0;
}

#endif /* WI_NO_WEB_FORMATS */
/* ------------------------------ JPEG / BMP ------------------------------ */
/* PARCHE 086: tamano natural de un JPEG (SOF) sin decodificarlo */
static int jpeg_dims(const uint8_t *d, uint32_t n, int *w, int *h, int *nc)
{
    uint32_t i = 2;
    while (i + 9 < n) {
        if (d[i] != 0xFF) { i++; continue; }
        int m = d[i + 1]; if (m == 0xFF || m == 0 || (m >= 0xD0 && m <= 0xD8) || m == 0x01) { i++; continue; }
        uint32_t l = ((uint32_t)d[i + 2] << 8) | d[i + 3];
        if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { *h = (d[i + 5] << 8) | d[i + 6]; *w = (d[i + 7] << 8) | d[i + 8]; if (nc) *nc = d[i + 9]; return *w > 0 && *h > 0; }
        if (m == 0xDA || m == 0xD9 || l < 2) return 0;
        i += 2 + l;
    }
    return 0;
}

static int jpeg_to_img(const uint8_t *d, uint32_t n, struct wimg *o, int scale)
{
    struct jpeg_info inf;
    int jw = 0, jh = 0, nc = 3;
    if (scale > 1) return wj_decode(d, n, o, scale);              /* PARCHE 086: reducida al decodificar */
    /* PARCHE 086: CMYK/YCCK y las fotos normales (hasta 2,5 Mpx) van por el decodificador propio: croma con interpolacion
     * (el del sistema repite pixeles: bordes en escalera). Las enormes, por el del sistema, que gasta menos memoria. */
    if (jpeg_dims(d, n, &jw, &jh, &nc) && (nc == 4 || (uint64_t)jw * jh <= 2500000u)) {
        if (wj_decode(d, n, o, 1) == 0) return 0;
        if (wi_oom || nc == 4) return -1;
    }
    int r = jpeg_get_info(d, n, &inf);
    if (r == JPEG_E_UNSUPPORTED) return wj_decode(d, n, o, 1);
    if (r != JPEG_OK) return wi_oom ? -1 : wj_decode(d, n, o, 1);   /* PARCHE 086: CMYK y raros -> decodificador propio */
    if (inf.width == 0 || inf.height == 0 || (uint64_t)inf.width * inf.height > WI_MAX_PIXELS) return -1;
    o->px = (uint32_t *)kmalloc((size_t)inf.width * inf.height * 4); if (!o->px) return -1;
    r = jpeg_decode(d, n, o->px, inf.width, inf.height, &inf);
    if (r != JPEG_OK && r != JPEG_E_TRUNC && r != JPEG_E_DATA) { kfree(o->px); o->px = 0; return -1; }
    o->w = (int)inf.width; o->h = (int)inf.height;
    for (size_t i = 0; i < (size_t)o->w * o->h; i++) o->px[i] |= 0xFF000000u;
    return 0;
}

static int wi_decode_s(const uint8_t *d, uint32_t n, struct wimg *o, int scale);
int wi_decode_local(const uint8_t *d, uint32_t n, struct wimg *o) { return wi_decode_s(d, n, o, 1); }
int wi_decode_fit_local(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh)
{
    int scale = 1, w, h;
    if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8 && maxw > 0 && maxh > 0 && jpeg_dims(d, n, &w, &h, 0))
        while (scale < 8 && (w + scale * 2 - 1) / (scale * 2) >= maxw && (h + scale * 2 - 1) / (scale * 2) >= maxh) scale *= 2;
    wi_oom = 0;
    int r = wi_decode_s(d, n, o, scale);
    if (r != 0 && wi_oom) return -2;
    return r;
}
#ifndef WI_NO_WEB_FORMATS
static int png_decode(const uint8_t *d, uint32_t n, struct wimg *o);
#include "web_svg.inc"                                                          /* SOMA_180: rasterizador SVG */
/* SOMA_180: ICO/CUR: la entrada mas grande; dentro hay un PNG o un BMP sin cabecera de fichero (con mascara AND) */
static int ico_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    int cnt = d[4] | (d[5] << 8), best = -1, bw = 0; if (cnt < 1 || 6u + (uint32_t)cnt * 16u > n) return -1;
    for (int i = 0; i < cnt && i < 64; i++) { const uint8_t *e = d + 6 + i * 16; int w = e[0] ? e[0] : 256; uint32_t sz = le32(e + 8), off = le32(e + 12); if (off >= n || sz > n - off || sz < 40) continue; if (w > bw) { bw = w; best = i; } }
    if (best < 0) return -1;
    const uint8_t *e = d + 6 + best * 16, *b = d + le32(e + 12); uint32_t sz = le32(e + 8);
    if (sz >= 8 && !memcmp(b, "\x89PNG", 4)) return png_decode(b, sz, o);
    uint32_t hs = le32(b); int w = (int)le32(b + 4), h2 = (int)le32(b + 8), bpp = b[14] | (b[15] << 8), comp = (int)le32(b + 16);
    int h = h2 / 2; if (hs < 40 || hs > sz || w <= 0 || h <= 0 || w > 1024 || h > 1024 || comp != 0) return -1;
    uint32_t ncol = bpp <= 8 ? le32(b + 32) : 0; if (bpp <= 8 && (ncol == 0 || ncol > 256)) ncol = 1u << bpp;
    uint32_t pal = hs, xo = hs + ncol * 4, row = ((uint32_t)w * (uint32_t)bpp + 31) / 32 * 4, mrow = ((uint32_t)w + 31) / 32 * 4, ao = xo + row * (uint32_t)h;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return -1;
    if (ao > sz) return -1;
    int have_mask = ao + mrow * (uint32_t)h <= sz, any_alpha = 0;
    uint32_t *px = (uint32_t *)kmalloc((size_t)w * h * 4); if (!px) return -1;
    for (int y = 0; y < h; y++) { const uint8_t *r = b + xo + row * (uint32_t)(h - 1 - y), *m = b + ao + mrow * (uint32_t)(h - 1 - y);
        for (int x = 0; x < w; x++) { uint32_t c;
            if (bpp == 32) { c = le32(r + x * 4); if (c >> 24) any_alpha = 1; }
            else if (bpp == 24) c = 0xFF000000u | le24(r + x * 3);
            else { uint32_t ix = bpp == 8 ? r[x] : bpp == 4 ? (r[x / 2] >> ((x & 1) ? 0 : 4)) & 15 : (r[x / 8] >> (7 - (x & 7))) & 1; c = ix < ncol ? 0xFF000000u | le24(b + pal + ix * 4) : 0xFF000000u; }
            if (bpp != 32 && have_mask && ((m[x / 8] >> (7 - (x & 7))) & 1)) c = 0;
            px[(size_t)y * w + x] = c; } }
    if (bpp == 32 && !any_alpha) for (int y = 0; y < h; y++) { const uint8_t *m = b + ao + mrow * (uint32_t)(h - 1 - y); for (int x = 0; x < w; x++) px[(size_t)y * w + x] = (have_mask && ((m[x / 8] >> (7 - (x & 7))) & 1)) ? 0 : (px[(size_t)y * w + x] | 0xFF000000u); }
    o->px = px; o->w = w; o->h = h; return 0;
}
int wi_is_svg(const uint8_t *d, uint32_t n)
{
    uint32_t i = 0; if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
    for (int guard = 0; guard < 64 && i < n; guard++) {
        while (i < n && (d[i] == ' ' || d[i] == '\n' || d[i] == '\r' || d[i] == '\t')) i++;
        if (i + 4 >= n || d[i] != '<') return 0;
        if (!memcmp(d + i, "<svg", 4) && (d[i + 4] == ' ' || d[i + 4] == '>' || d[i + 4] == '\n' || d[i + 4] == '\r' || d[i + 4] == '\t' || d[i + 4] == '/')) return 1;
        if (d[i + 1] == '?' ) { while (i + 1 < n && !(d[i] == '?' && d[i + 1] == '>')) i++; i += 2; continue; }
        if (i + 3 < n && !memcmp(d + i, "<!--", 4)) { while (i + 2 < n && memcmp(d + i, "-->", 3)) i++; i += 3; continue; }
        if (d[i + 1] == '!') { int dep = 0; while (i < n) { if (d[i] == '[') dep++; else if (d[i] == ']') dep--; else if (d[i] == '>' && dep <= 0) break; i++; } i++; continue; }
        return 0;
    }
    return 0;
}
int wi_anim_frames(const uint8_t *d, uint32_t n)
{
    if (n >= 13 && !memcmp(d, "GIF8", 4)) { int c = gif_count(d, n); return c > 1 ? c : 1; }
    if (n >= 30 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return webp_count(d, n);
    return 1;
}
int wi_decode_anim_local(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels)
{
    o->w = o->h = 0; o->px = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0; wi_oom = 0;
    if (!max_pixels || max_pixels > WI_ANIM_MAX_PIXELS) max_pixels = WI_ANIM_MAX_PIXELS;
    int r = -1;
    if (n >= 13 && !memcmp(d, "GIF8", 4)) r = gif_decode(d, n, o, max_pixels);
    else if (n >= 30 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4) && webp_count(d, n) > 1) r = webp_decode(d, n, o, max_pixels);
    if (r != 0 && wi_oom) return -2;
    return r;
}
int wi_decode_svg_local(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color)
{
    o->w = o->h = 0; o->px = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0; wi_oom = 0;
    int r = svg_render(d, n, o, dw, dh, color);
    if (r != 0 && wi_oom) return -2;
    return r;
}
#endif /* WI_NO_WEB_FORMATS */
static int wi_decode_s(const uint8_t *d, uint32_t n, struct wimg *o, int scale)
{
    o->w = o->h = 0; o->px = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0;
    if (n < 8) return -1;
    if (!memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return png_decode(d, n, o);
    if (!memcmp(d, "GIF8", 4)) return gif_decode(d, n, o, 0);
#ifndef WI_NO_WEB_FORMATS
    if (n >= 20 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return webp_decode(d, n, o, 0);
    if (n >= 22 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || d[2] == 2) && d[3] == 0) return ico_decode(d, n, o);
    if (wi_is_svg(d, n)) return svg_render(d, n, o, 0, 0, 0);
#endif
    if (d[0] == 0xFF && d[1] == 0xD8) return jpeg_to_img(d, n, o, scale);
    if (d[0] == 'B' && d[1] == 'M') {                             /* PARCHE 138: bmp_decode es pura: tambien en las pruebas de host */
        struct image im; memset(&im, 0, sizeof im);
        if (bmp_decode(d, n, &im) == 0 && im.pixels && im.width && im.height && (uint64_t)im.width * im.height <= WI_MAX_PIXELS) {
            o->px = (uint32_t *)kmalloc((size_t)im.width * im.height * 4);
            if (o->px) { for (size_t i = 0; i < (size_t)im.width * im.height; i++) o->px[i] = im.pixels[i] | 0xFF000000u; o->w = (int)im.width; o->h = (int)im.height; }
            kfree(im.pixels); return o->px ? 0 : -1;
        }
        if (im.pixels) kfree(im.pixels);
    }
    return -1;
}

/* PARCHE 138: en las pruebas de host los nombres publicos son los decodificadores directos (en el kernel x86_64 los da web_img_sbx.c,
 * que llama al servicio aislado imgdec; en aarch64, web_img_sbx.c llama a estas mismas funciones). */
#ifdef WEB_HOST_TEST
int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o) { return wi_decode_local(d, n, o); }
int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh) { return wi_decode_fit_local(d, n, o, maxw, maxh); }
int wi_decode_anim(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels) { return wi_decode_anim_local(d, n, o, max_pixels); }
int wi_decode_svg(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color) { return wi_decode_svg_local(d, n, o, dw, dh, color); }
#endif
