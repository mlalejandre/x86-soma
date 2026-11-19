/* SOMA_179: el codificador JPEG produce archivos que decodifica el decodificador del sistema (y PIL en la prueba manual) con poco error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jpeg_enc.h"
#include "jpeg.h"

static uint32_t *make(int w, int h)
{
    uint32_t *p = (uint32_t *)malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int r = x * 255 / (w > 1 ? w - 1 : 1), g = y * 255 / (h > 1 ? h - 1 : 1), b = 128;
        if (((x / 16) + (y / 16)) & 1) { r = 255 - r; }
        if (x > w / 3 && x < w / 3 + 40 && y > h / 3 && y < h / 3 + 12) { r = g = b = 255; }   /* "texto" */
        if (x == 5 || y == 7) { r = 0; g = 0; b = 0; }
        p[(size_t)y * w + x] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    return p;
}
static double psnr(const uint32_t *a, const uint32_t *b, int w, int h)
{
    double s = 0; for (size_t i = 0; i < (size_t)w * h; i++) for (int sh = 0; sh < 24; sh += 8) { int d = (int)((a[i] >> sh) & 255) - (int)((b[i] >> sh) & 255); s += (double)d * d; }
    s /= (double)w * h * 3; return s < 1e-9 ? 99 : 10 * log10(255.0 * 255.0 / s);
}
int main(void)
{
    static const int dims[][2] = { {64, 64}, {100, 37}, {17, 1}, {1, 1}, {320, 180}, {1920, 1080} };
    for (unsigned k = 0; k < sizeof dims / sizeof dims[0]; k++) {
        int w = dims[k][0], h = dims[k][1];
        uint32_t *src = make(w, h), *dec = (uint32_t *)calloc((size_t)w * h, 4);
        size_t cap = (size_t)w * h * 3 + 4096; uint8_t *buf = (uint8_t *)malloc(cap);
        for (int q = 30; q <= 90; q += 30) {
            size_t n = jpeg_encode_rgb(src, w, h, w, q, buf, cap);
            if (!n) { printf("jpeg_enc: fallo al codificar %dx%d q%d\n", w, h, q); return 1; }
            struct jpeg_info in; int r = jpeg_decode(buf, n, dec, (uint32_t)w, (uint32_t)h, &in);
            if (r != 0 || (int)in.width != w || (int)in.height != h) { printf("jpeg_enc: no decodifica %dx%d q%d (%d)\n", w, h, q, r); return 1; }
            double ps = psnr(src, dec, w, h);
            if (w >= 64 && ps < (q >= 90 ? 27.0 : 20.0)) { printf("jpeg_enc: PSNR bajo %.1f en %dx%d q%d\n", ps, w, h, q); return 1; }
            if (w == 1920 && q == 60) { FILE *f = fopen("/tmp/enc_1080.jpg", "wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); } }
            if (w == 100 && q == 90) { FILE *f = fopen("/tmp/enc_100.jpg", "wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); } FILE *g = fopen("/tmp/enc_100.raw", "wb"); if (g) { fwrite(src, 4, (size_t)w * h, g); fclose(g); } }
        }
        /* capacidad insuficiente: devuelve 0 sin escribir fuera */
        if (jpeg_encode_rgb(src, w, h, w, 50, buf, 1024) != 0 && w >= 64) { printf("jpeg_enc: no respeta cap\n"); return 1; }
        free(src); free(dec); free(buf);
    }
    uint32_t px[4] = { 0 }; uint8_t b2[2048];
    if (jpeg_encode_rgb(px, 0, 1, 1, 50, b2, sizeof b2) || jpeg_encode_rgb(px, 5000, 1, 5000, 50, b2, sizeof b2) || jpeg_encode_rgb(0, 2, 2, 2, 50, b2, sizeof b2)) { printf("jpeg_enc: argumentos invalidos aceptados\n"); return 1; }
    printf("jpeg_enc: codifica y decodifica bien (6 tamanos, 3 calidades, limites y argumentos)\n");
    return 0;
}
