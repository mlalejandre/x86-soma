/* SOMA 181: framebuffer de anfitrion para lay_host: implementa las primitivas de fb.h sobre un mapa de bits en memoria. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
uint32_t *g_fb; int g_fbw, g_fbh;
static int cx0, cy0, cx1 = 1 << 30, cy1 = 1 << 30;
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h) { cx0 = x; cy0 = y; cx1 = x + w; cy1 = y + h; }
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1) { *x0 = cx0; *y0 = cy0; *x1 = cx1; *y1 = cy1; }
/* SOMA_200: los bucles solo recorren lo visible. Antes recorrian la caja entera aunque quedase fuera: una caja de 99999999 px
 * (el fuzzing las genera) tardaba minutos. El nucleo real ya recorta antes de recorrer (clip_rect en fb.c). */
static inline void vis(long long a, long long n, int lo, int hi, int lim, int *i0, int *i1)
{
    long long b = a + n, l = lo > 0 ? lo : 0, h = hi < lim ? hi : lim;
    if (a < l) a = l;
    if (b > h) b = h;
    if (b < a) b = a;
    *i0 = (int)a; *i1 = (int)b;
}
#define VIS_X(x, w) int vx0, vx1; vis((x), (w), cx0, cx1, g_fbw, &vx0, &vx1)
#define VIS_Y(y, h) int vy0, vy1; vis((y), (h), cy0, cy1, g_fbh, &vy0, &vy1)
static inline void put(int x, int y, uint32_t c, int a)   /* mezcla c (RGB) con alfa a (0..255) */
{
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || x < 0 || y < 0 || x >= g_fbw || y >= g_fbh || a <= 0) return;
    uint32_t d = g_fb[y * g_fbw + x];
    if (a >= 255) { g_fb[y * g_fbw + x] = 0xFF000000u | (c & 0xFFFFFF); return; }
    uint32_t r = (((c >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (d & 255) * (255 - a)) / 255;
    g_fb[y * g_fbw + x] = 0xFF000000u | r << 16 | g << 8 | b;
}
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) { VIS_X((int32_t)x, w); VIS_Y((int32_t)y, h); for (int j = vy0; j < vy1; j++) for (int i = vx0; i < vx1; i++) put(i, j, color, 255); }
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) { int a = (int)(color >> 24); VIS_X(x, w); VIS_Y(y, h); for (int j = vy0; j < vy1; j++) for (int i = vx0; i < vx1; i++) put(i, j, color, a); }
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    int a0 = (int)(argb >> 24); if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
    VIS_X(x, w); VIS_Y(y, h);
    for (int j = vy0 - y; j < vy1 - y; j++) for (int i = vx0 - x; i < vx1 - x; i++) {
        int a = a0;
        if (r > 0) {
            int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0), dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            if (dx && dy) { int d2 = dx * dx + dy * dy, R2 = r * r; if (d2 > (r + 1) * (r + 1)) a = 0; else if (d2 > R2) a = a0 * ((r + 1) * (r + 1) - d2) / ((r + 1) * (r + 1) - R2); }
        }
        put(x + i, y + j, argb, a);
    }
}
void fb_draw_box_shadow(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t dx, int32_t dy, int32_t spread, int32_t blur, uint32_t argb)
{
    /* SOMA_188: como drivers/fb.c: la sombra exterior no se pinta debajo de la propia caja (con fondo transparente se veia) */
    int a0 = (int)(argb >> 24); (void)r; if (blur < 1) blur = 1;
    int sx0 = x + dx - spread, sy0 = y + dy - spread, sx1 = x + w + dx + spread, sy1 = y + h + dy + spread;
    VIS_X((long long)sx0 - blur, (long long)sx1 - sx0 + 2LL * blur); VIS_Y((long long)sy0 - blur, (long long)sy1 - sy0 + 2LL * blur);
    for (int py = vy0; py < vy1; py++) for (int px = vx0; px < vx1; px++) {
        if (px >= x && px < x + w && py >= y && py < y + h) continue;
        int ddx = px < sx0 ? sx0 - px : px >= sx1 ? px - sx1 + 1 : 0, ddy = py < sy0 ? sy0 - py : py >= sy1 ? py - sy1 + 1 : 0;
        int d = ddx > ddy ? ddx : ddy; if (d >= blur) continue;
        int a = a0 * (blur - d) / blur; if (a > 0) put(px, py, argb & 0xFFFFFF, a);
    }
}
int fb_corners_save(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t *buf) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)buf; return 0; }
void fb_corners_restore(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const uint32_t *buf) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)buf; }
void fb_blit(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride) { for (int j = 0; j < sh; j++) for (int i = 0; i < sw; i++) put(dx + i, dy + j, src[j * stride + i], 255); }
int fb_read_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t *out) { if (!g_fb || x < 0 || y < 0 || x + w > g_fbw || y + h > g_fbh) return 0; for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) out[j * w + i] = g_fb[(y + j) * g_fbw + x + i]; return 1; }   /* SOMA_347 */
void fb_blit_alpha(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride) { for (int j = 0; j < sh; j++) for (int i = 0; i < sw; i++) { uint32_t p = src[j * stride + i]; put(dx + i, dy + j, p, (int)(p >> 24)); } }
void fb_draw_mask8(int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h, uint32_t rgb, uint32_t alpha) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, rgb, (int)(mask[j * w + i] * alpha / 255)); }
int font_vec_available(void) { return 0; }
void font_vec_draw(int32_t x, int32_t y, uint32_t cp, uint32_t fg, int em, int cell_w, int baseline) { (void)x; (void)y; (void)cp; (void)fg; (void)em; (void)cell_w; (void)baseline; }
