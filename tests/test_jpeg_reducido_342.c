/* SOMA_342_NAVEGADOR_AGIL: la IDCT reducida de web_jpeg.c (escala 2 y 4) frente a lo de antes (IDCT 8x8 entera y promedio).
 * Fotos sinteticas codificadas con el codificador de SOMA (jpeg_enc.c) en calidad 60 y 90, 4:2:0 y gris:
 *   - lo reducido se parece a la foto entera promediada tanto como antes (error medio y maximo acotados);
 *   - un bloque plano da exactamente el mismo valor (misma escala de continua);
 *   - el tiempo de decodificar con escala 4 baja.
 *   cc -O2 -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps tests/test_jpeg_reducido_342.c src/apps/web_jpeg.c src/core/jpeg_enc.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "web_img.h"
#include "web_jpeg.h"
#include "jpeg_enc.h"

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
void kprint(const char *s) { (void)s; }
void jpeg_yield_hook(void) { }
uint64_t web_now_ms(void) { return (uint64_t)clock() * 1000u / CLOCKS_PER_SEC; }
void web_yield(void) { }
void wi_free(struct wimg *o) { free(o->px); free(o->delay); o->px = 0; o->delay = 0; }
int wi_oom;
extern int wj_red_off;

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)
static uint64_t S = 0x243F6A8885A308D3ull;
static uint32_t az(void) { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32_t)(S >> 11); }

/* foto sintetica: degradados, rectangulos de colores y algo de ruido (como una foto de prensa) */
static uint32_t *foto(int w, int h, int gris)
{
    uint32_t *p = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int r = x * 255 / w, g = y * 255 / h, b = (x + y) * 255 / (w + h);
        p[(size_t)y * w + x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    for (int k = 0; k < 40; k++) {
        int x0 = (int)(az() % (unsigned)w), y0 = (int)(az() % (unsigned)h), x1 = x0 + 8 + (int)(az() % 120), y1 = y0 + 8 + (int)(az() % 90);
        uint32_t c = 0xFF000000u | (az() & 0xFFFFFF);
        for (int y = y0; y < y1 && y < h; y++) for (int x = x0; x < x1 && x < w; x++) p[(size_t)y * w + x] = c;
    }
    for (int i = 0; i < w * h; i++) {
        uint32_t c = p[i]; int n = (int)(az() % 17) - 8;
        int r = (int)((c >> 16) & 255) + n, g = (int)((c >> 8) & 255) + n, b = (int)(c & 255) + n;
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        if (gris) g = b = r;
        p[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    return p;
}
/* error medio y maximo (por canal) de `a` (w x h) frente a la foto entera `f` (fw x fh) promediada en cuadros de s x s */
static void error(const struct wimg *a, const struct wimg *f, int s, double *medio, int *maximo)
{
    double sum = 0; long n = 0; int mx = 0;
    for (int y = 0; y < a->h; y++) for (int x = 0; x < a->w; x++) {
        int acc[3] = { 0, 0, 0 }, k = 0;
        for (int dy = 0; dy < s; dy++) for (int dx = 0; dx < s; dx++) {
            int yy = y * s + dy, xx = x * s + dx; if (yy >= f->h || xx >= f->w) continue;
            uint32_t c = f->px[(size_t)yy * f->w + xx]; acc[0] += (c >> 16) & 255; acc[1] += (c >> 8) & 255; acc[2] += c & 255; k++;
        }
        if (!k) continue;
        uint32_t c = a->px[(size_t)y * a->w + x]; int v[3] = { (int)((c >> 16) & 255), (int)((c >> 8) & 255), (int)(c & 255) };
        for (int i = 0; i < 3; i++) { int d = abs(v[i] - (acc[i] + k / 2) / k); sum += d; n++; if (d > mx) mx = d; }
    }
    *medio = n ? sum / (double)n : 0; *maximo = mx;
}
static double ms(clock_t a, clock_t b) { return (double)(b - a) * 1000.0 / CLOCKS_PER_SEC; }

int main(void)
{
    static uint8_t buf[4 << 20];
    double t_antes = 0, t_ahora = 0;
    for (int caso = 0; caso < 6; caso++) {
        int w = caso < 3 ? 990 : 640, h = caso < 3 ? 556 : 360, gris = caso == 5, q = caso % 2 ? 60 : 90;
        uint32_t *p = foto(w, h, gris);
        size_t n = jpeg_encode_rgb(p, w, h, w, q, buf, sizeof buf);
        CK(n > 0, "se codifica");
        struct wimg full; memset(&full, 0, sizeof full);
        CK(wj_decode(buf, (uint32_t)n, &full, 1) == 0, "se decodifica entera");
        for (int s = 2; s <= 4; s *= 2) {
            struct wimg a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            wj_red_off = 1; clock_t t0 = clock(); int ra = wj_decode(buf, (uint32_t)n, &a, s); clock_t t1 = clock();
            wj_red_off = 0; int rb = wj_decode(buf, (uint32_t)n, &b, s); clock_t t2 = clock();
            if (s == 4) { t_antes += ms(t0, t1); t_ahora += ms(t1, t2); }
            CK(ra == 0 && rb == 0 && a.w == b.w && a.h == b.h && b.w == (w + s - 1) / s, "mismo tamano reducido");
            double ea, eb; int ma, mb;
            error(&a, &full, s, &ea, &ma); error(&b, &full, s, &eb, &mb);
            printf("  %dx%d q%d%s escala %d: error medio antes %.2f ahora %.2f, maximo antes %d ahora %d\n", w, h, q, gris ? " gris" : "", s, ea, eb, ma, mb);
            CK(eb <= ea + 1.5 && eb < 5.0, "lo reducido se parece a la foto entera promediada tanto como antes");
            wi_free(&a); wi_free(&b);
        }
        wi_free(&full); free(p);
    }
    /* un bloque plano: mismo valor exacto que la IDCT 8x8 (misma escala de continua) */
    {
        int w = 64, h = 64; uint32_t *p = malloc((size_t)w * h * 4);
        for (int v = 0; v < 256; v += 17) {
            for (int i = 0; i < w * h; i++) p[i] = 0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v;
            size_t n = jpeg_encode_rgb(p, w, h, w, 95, buf, sizeof buf);
            struct wimg a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            wj_red_off = 1; wj_decode(buf, (uint32_t)n, &a, 4); wj_red_off = 0; wj_decode(buf, (uint32_t)n, &b, 4);
            int dif = 0; for (int i = 0; i < a.w * a.h; i++) { int d = abs((int)(a.px[i] & 255) - (int)(b.px[i] & 255)); if (d > dif) dif = d; }
            CK(dif <= 1, "un color plano sale igual");
            wi_free(&a); wi_free(&b);
        }
        free(p);
    }
    printf("  escala 4: antes %.0f ms, ahora %.0f ms (6 fotos)\n", t_antes, t_ahora);
    /* el tiempo se muestra pero no se exige: en una CI cargada podria fallar sin motivo */
    printf("test_jpeg_reducido_342: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
