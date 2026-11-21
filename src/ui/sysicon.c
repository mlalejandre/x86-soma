/* SOMA_210_SYSICON: iconos del sistema en graficos vectoriales y controles comunes. Ver sysicon.h.
 *
 * Cada icono es una lista de trazos, circulos, arcos, cajas redondeadas y poligonos sobre una rejilla de 48x48. Se rasterizan
 * con cobertura por pixel (distancia con signo a la forma, en 1/16 de pixel; los poligonos con 4x4 muestras por pixel), asi que
 * se ven nitidos a 14 px y a 64 px. Todo se dibuja en el bufer actual de la pantalla respetando el recorte y declarando el dano. */
#include <stdint.h>
#include <stddef.h>
#include "sysicon.h"
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"

/* ------------------------------------------------------------------ utilidades */
uint32_t si_mix(uint32_t a, uint32_t b, uint32_t k)
{
    if (k > 256u) k = 256u;
    uint32_t r = (((a >> 16) & 255u) * (256u - k) + ((b >> 16) & 255u) * k) >> 8;
    uint32_t g = (((a >> 8) & 255u) * (256u - k) + ((b >> 8) & 255u) * k) >> 8;
    uint32_t c = ((a & 255u) * (256u - k) + (b & 255u) * k) >> 8;
    return (r << 16) | (g << 8) | c;
}
static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return r;
}
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ rasterizado */
static uint32_t *R_bb; static int32_t R_w, R_x0, R_y0, R_x1, R_y1;
/* Prepara el dibujo dentro de la caja [x0,x1) x [y0,y1) en pixeles. Devuelve 0 si no queda nada visible. */
static int r_begin(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t c0, c1, c2, c3;
    R_bb = (uint32_t *)fb_get_buffer();
    if (!R_bb || !fb_is_active()) return 0;
    R_w = (int32_t)fb_get_width();
    fb_get_clip(&c0, &c1, &c2, &c3);
    if (*x0 < c0) *x0 = c0;
    if (*y0 < c1) *y0 = c1;
    if (*x1 > c2) *x1 = c2;
    if (*y1 > c3) *y1 = c3;
    if (*x0 >= *x1 || *y0 >= *y1) return 0;
    R_x0 = *x0; R_y0 = *y0; R_x1 = *x1; R_y1 = *y1;
    fb_damage(*x0, *y0, *x1 - *x0, *y1 - *y0);
    return 1;
}
static inline void r_put(int32_t x, int32_t y, uint32_t rgb, uint32_t a)     /* a: 0..256 */
{
    if (!a) return;
    uint32_t *p = R_bb + (uint32_t)y * (uint32_t)R_w + (uint32_t)x;
    if (a >= 256u) { *p = rgb; return; }
    *p = si_mix(*p & 0x00FFFFFFu, rgb, a);
}
static inline uint32_t cov_of(int32_t sd)                                     /* distancia con signo (1/16 px) -> cobertura */
{
    int32_t c = 8 - sd;
    if (c <= 0) return 0;
    if (c >= 16) return 256u;
    return (uint32_t)c * 16u;
}

enum { M_FULL, M_BOTTOM, M_TOP, M_RIGHT, M_POWER, M_RESTART, M_LEFT };
static int mask_ok(int mask, int32_t dx, int32_t dy)
{
    switch (mask) {
    case M_BOTTOM: return dy >= 0;
    case M_TOP:    return dy <= 0;
    case M_RIGHT:  return dx * 10 > iabs(dy) * 6;
    case M_POWER:  return !(dy < 0 && iabs(dx) * 10 < -dy * 7);
    case M_RESTART: return !(dy < 0 && dx >= 0 && -dy * 10 > dx * 5);          /* SOMA_218: hueco arriba a la derecha */
    case M_LEFT:   return -dx * 10 > iabs(dy) * 6;                                /* SOMA_240 */
    default:       return 1;
    }
}
/* Disco (w16 = 0) o anillo de grosor w16. Todo en 1/16 px. */
static void r_circle(int32_t cx, int32_t cy, int32_t r, int32_t w16, int mask, uint32_t rgb)
{
    int32_t ext = r + w16 / 2 + 16;
    int32_t x0 = (cx - ext) >> 4, y0 = (cy - ext) >> 4, x1 = ((cx + ext) >> 4) + 1, y1 = ((cy + ext) >> 4) + 1;
    if (!r_begin(&x0, &y0, &x1, &y1)) return;
    for (int32_t y = y0; y < y1; ++y) {
        int32_t dy = y * 16 + 8 - cy;
        for (int32_t x = x0; x < x1; ++x) {
            int32_t dx = x * 16 + 8 - cx;
            if (!mask_ok(mask, dx, dy)) continue;
            int32_t d = (int32_t)isqrt32((uint32_t)(dx * dx + dy * dy));
            int32_t sd = w16 ? iabs(d - r) - w16 / 2 : d - r;
            r_put(x, y, rgb, cov_of(sd));
        }
    }
}
/* Caja redondeada con centro (cx,cy), semiejes (hx,hy) y radio rr; relleno (w16 = 0) o contorno. */
static void r_rbox(int32_t cx, int32_t cy, int32_t hx, int32_t hy, int32_t rr, int32_t w16, uint32_t rgb)
{
    if (rr > hx) rr = hx;
    if (rr > hy) rr = hy;
    int32_t ext = w16 / 2 + 16;
    int32_t x0 = (cx - hx - ext) >> 4, y0 = (cy - hy - ext) >> 4, x1 = ((cx + hx + ext) >> 4) + 1, y1 = ((cy + hy + ext) >> 4) + 1;
    if (!r_begin(&x0, &y0, &x1, &y1)) return;
    for (int32_t y = y0; y < y1; ++y) {
        int32_t qy = iabs(y * 16 + 8 - cy) - hy + rr;
        for (int32_t x = x0; x < x1; ++x) {
            int32_t qx = iabs(x * 16 + 8 - cx) - hx + rr;
            int32_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            int32_t in = qx > qy ? qx : qy; if (in > 0) in = 0;
            int32_t sd = (int32_t)isqrt32((uint32_t)(ox * ox + oy * oy)) + in - rr;
            if (w16) sd = iabs(sd) - w16 / 2;
            r_put(x, y, rgb, cov_of(sd));
        }
    }
}
/* Poligono cualquiera (par-impar), vertices en 1/16 px, 4x4 muestras por pixel. */
static void r_poly(const int32_t *pt, int n, uint32_t rgb)
{
    int32_t mnx = pt[0], mxx = pt[0], mny = pt[1], mxy = pt[1];
    for (int i = 1; i < n; ++i) {
        if (pt[2 * i] < mnx) mnx = pt[2 * i];
        if (pt[2 * i] > mxx) mxx = pt[2 * i];
        if (pt[2 * i + 1] < mny) mny = pt[2 * i + 1];
        if (pt[2 * i + 1] > mxy) mxy = pt[2 * i + 1];
    }
    int32_t x0 = mnx >> 4, y0 = mny >> 4, x1 = (mxx >> 4) + 1, y1 = (mxy >> 4) + 1;
    if (!r_begin(&x0, &y0, &x1, &y1)) return;
    for (int32_t y = y0; y < y1; ++y) for (int32_t x = x0; x < x1; ++x) {
        uint32_t hits = 0;
        for (int sy = 0; sy < 4; ++sy) {
            int32_t py = y * 16 + 2 + sy * 4;
            for (int sx = 0; sx < 4; ++sx) {
                int32_t px = x * 16 + 2 + sx * 4; int in = 0;
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    int32_t xi = pt[2 * i], yi = pt[2 * i + 1], xj = pt[2 * j], yj = pt[2 * j + 1];
                    if ((yi > py) != (yj > py)) {
                        int64_t xc = (int64_t)xi + (int64_t)(xj - xi) * (py - yi) / (yj - yi);
                        if (px < xc) in = !in;
                    }
                }
                hits += (uint32_t)in;
            }
        }
        r_put(x, y, rgb, hits * 16u);
    }
}

/* ------------------------------------------------------------------ iconos */
/* Rejilla 48x48 centrada en (cx,cy) con lado `size` px. Grosor del trazo: 4/48 del lado (1,5 px a 18 px). */
static int32_t I_cx, I_cy, I_s, I_sw; static uint32_t I_col;
#define GX(u) (I_cx + ((int32_t)(u) * 2 - 48) * I_s / 96)
#define GY(u) (I_cy + ((int32_t)(u) * 2 - 48) * I_s / 96)
#define GR(u) ((int32_t)((u) * 2) * I_s / 96)
static void L(int x0, int y0, int x1, int y1) { fb_line_aa(GX(x0), GY(y0), GX(x1), GY(y1), I_sw, I_col); }
static void D(int x, int y, int r) { r_circle(GX(x), GY(y), GR(r), 0, M_FULL, I_col); }
static void O(int x, int y, int r, int mask) { r_circle(GX(x), GY(y), GR(r), I_sw, mask, I_col); }
static void B(int x0, int y0, int x1, int y1, int rad, int fill)
{
    r_rbox((GX(x0) + GX(x1)) / 2, (GY(y0) + GY(y1)) / 2, (GX(x1) - GX(x0)) / 2, (GY(y1) - GY(y0)) / 2, GR(rad), fill ? 0 : I_sw, I_col);
}
static void P(const int8_t *g, int n)
{
    int32_t pt[24];
    if (n > 12) n = 12;
    for (int i = 0; i < n; ++i) { pt[2 * i] = GX(g[2 * i]); pt[2 * i + 1] = GY(g[2 * i + 1]); }
    r_poly(pt, n, I_col);
}
static void mic(void) { B(17, 5, 31, 29, 7, 0); O(24, 22, 12, M_BOTTOM); L(24, 34, 24, 41); L(17, 42, 31, 42); }
static void camera(void) { B(5, 14, 43, 40, 5, 0); L(16, 14, 19, 8); L(19, 8, 29, 8); L(29, 8, 32, 14); O(24, 27, 7, M_FULL); D(37, 20, 2); }
static void screen(void) { B(4, 7, 44, 34, 4, 0); L(24, 34, 24, 41); L(15, 41, 33, 41); }
static void file_page(void) { L(11, 5, 28, 5); L(28, 5, 37, 14); L(37, 14, 37, 43); L(37, 43, 11, 43); L(11, 43, 11, 5); L(28, 5, 28, 14); L(28, 14, 37, 14); }
static void speaker_body(void) { static const int8_t g[] = { 4, 18, 12, 18, 22, 9, 22, 39, 12, 30, 4, 30 }; P(g, 6); }

void si_icon(int icon, int32_t cx, int32_t cy, int32_t size, uint32_t rgb)
{
    if (size < 6) return;
    I_cx = cx * 16 + 8; I_cy = cy * 16 + 8; I_s = size * 16; I_col = rgb & 0x00FFFFFFu;
    I_sw = I_s * 4 / 48; if (I_sw < 18) I_sw = 18;
    switch (icon) {
    case SI_REC:    D(24, 24, 13); break;
    case SI_STOP:   B(13, 13, 35, 35, 4, 1); break;
    case SI_PLAY:   { static const int8_t g[] = { 16, 10, 38, 24, 16, 38 }; P(g, 3); break; }
    case SI_PAUSE:  B(13, 11, 21, 37, 2, 1); B(27, 11, 35, 37, 2, 1); break;
    case SI_MIC:    mic(); break;
    case SI_MIC_OFF: mic(); L(8, 6, 40, 42); break;
    case SI_CAMERA: camera(); break;
    case SI_CAMERA_OFF: camera(); L(6, 6, 42, 42); break;
    case SI_VIDEO:  { static const int8_t g[] = { 34, 21, 44, 15, 44, 35, 34, 29 }; B(4, 14, 31, 36, 5, 0); P(g, 4); break; }
    case SI_SAVE:   L(24, 6, 24, 29); L(15, 21, 24, 30); L(33, 21, 24, 30); L(8, 31, 8, 40); L(8, 40, 40, 40); L(40, 40, 40, 31); break;
    case SI_NEW:    B(10, 5, 38, 43, 5, 0); L(24, 16, 24, 32); L(16, 24, 32, 24); break;
    case SI_TRASH:  L(9, 13, 39, 13); L(19, 13, 19, 8); L(19, 8, 29, 8); L(29, 8, 29, 13); L(13, 14, 15, 41); L(15, 41, 33, 41); L(33, 41, 35, 14); L(21, 20, 21, 34); L(27, 20, 27, 34); break;
    case SI_LEVEL:  B(7, 28, 13, 41, 2, 1); B(16, 20, 22, 41, 2, 1); B(25, 10, 31, 41, 2, 1); B(34, 24, 40, 41, 2, 1); break;
    case SI_SCREEN: screen(); break;
    case SI_SPEAKER: { static const int8_t g[] = { 5, 18, 13, 18, 23, 9, 23, 39, 13, 30, 5, 30 }; P(g, 6); O(25, 24, 8, M_RIGHT); O(25, 24, 15, M_RIGHT); break; }
    case SI_SCREEN_AUDIO: screen(); B(13, 18, 16, 24, 1, 1); B(19, 13, 22, 29, 1, 1); B(25, 15, 28, 27, 1, 1); B(31, 18, 34, 24, 1, 1); break;
    case SI_TIMER:  O(24, 27, 15, M_FULL); L(24, 27, 24, 18); L(24, 27, 30, 31); L(19, 5, 29, 5); L(24, 5, 24, 11); L(37, 12, 40, 15); break;
    case SI_SCALE:  L(12, 36, 36, 12); L(25, 12, 36, 12); L(36, 12, 36, 23); L(12, 25, 12, 36); L(12, 36, 23, 36); break;
    case SI_GAUGE:  O(24, 31, 17, M_TOP); L(24, 31, 33, 20); D(24, 31, 4); L(7, 38, 41, 38); break;
    case SI_FOLDER: L(6, 13, 19, 13); L(19, 13, 23, 18); L(23, 18, 42, 18); L(42, 18, 42, 38); L(42, 38, 6, 38); L(6, 38, 6, 13); break;
    case SI_PREV:   L(29, 12, 17, 24); L(17, 24, 29, 36); break;
    case SI_NEXT:   L(19, 12, 31, 24); L(31, 24, 19, 36); break;
    case SI_AUTO:   { static const int8_t g[] = { 22, 8, 26, 20, 38, 24, 26, 28, 22, 40, 18, 28, 6, 24, 18, 20 }; P(g, 8); D(38, 10, 3); D(39, 39, 2); break; }
    case SI_POWER:  O(24, 26, 15, M_POWER); L(24, 5, 24, 22); break;
    case SI_MINUS:  L(12, 24, 36, 24); break;
    case SI_PLUS:   L(12, 24, 36, 24); L(24, 12, 24, 36); break;
    case SI_IMAGE:  { static const int8_t g[] = { 9, 37, 19, 23, 26, 31, 31, 26, 39, 37 }; B(5, 8, 43, 40, 5, 0); D(32, 17, 4); P(g, 5); break; }
    case SI_WAVE:   B(7, 20, 10, 28, 1, 1); B(13, 13, 16, 35, 1, 1); B(19, 6, 22, 42, 1, 1); B(25, 15, 28, 33, 1, 1); B(31, 10, 34, 38, 1, 1); B(37, 19, 40, 29, 1, 1); break;
    case SI_CHECK:  L(11, 25, 20, 34); L(20, 34, 37, 15); break;
    case SI_RESTART: { static const int8_t g[] = { 22, 3, 33, 10, 22, 17 }; O(24, 25, 15, M_RESTART); P(g, 3); break; }   /* SOMA_218 */
    case SI_SHUTTER: { int32_t w = I_sw; I_sw = I_s * 3 / 48; O(24, 24, 21, M_FULL); I_sw = w; D(24, 24, 16); break; }
    /* SOMA_240_ICONOS */
    case SI_MUSIC:  D(15, 36, 6); D(35, 32, 6); L(20, 36, 20, 10); L(40, 32, 40, 6); { static const int8_t g[] = { 19, 9, 41, 4, 41, 11, 19, 16 }; P(g, 4); } break;
    case SI_FILM:   B(6, 7, 42, 41, 4, 0); L(14, 7, 14, 41); L(34, 7, 34, 41); L(6, 17, 14, 17); L(6, 31, 14, 31); L(34, 17, 42, 17); L(34, 31, 42, 31); break;
    case SI_REWIND: { static const int8_t a[] = { 23, 11, 23, 37, 5, 24 }, b[] = { 42, 11, 42, 37, 24, 24 }; P(a, 3); P(b, 3); break; }
    case SI_FFWD:   { static const int8_t a[] = { 6, 11, 6, 37, 24, 24 }, b[] = { 25, 11, 25, 37, 43, 24 }; P(a, 3); P(b, 3); break; }
    case SI_SKIP_PREV: { static const int8_t g[] = { 38, 11, 38, 37, 16, 24 }; B(9, 11, 14, 37, 1, 1); P(g, 3); break; }
    case SI_SKIP_NEXT: { static const int8_t g[] = { 10, 11, 10, 37, 32, 24 }; P(g, 3); B(34, 11, 39, 37, 1, 1); break; }
    case SI_LIST:   D(10, 13, 3); D(10, 24, 3); D(10, 35, 3); L(18, 13, 40, 13); L(18, 24, 40, 24); L(18, 35, 40, 35); break;
    case SI_CODE:   L(16, 13, 6, 24); L(6, 24, 16, 35); L(32, 13, 42, 24); L(42, 24, 32, 35); L(27, 9, 21, 39); break;
    case SI_UNDO:   { static const int8_t g[] = { 5, 16, 15, 8, 15, 24 }; L(14, 16, 28, 16); O(28, 26, 10, M_RIGHT); L(28, 36, 15, 36); P(g, 3); break; }
    case SI_REDO:   { static const int8_t g[] = { 43, 16, 33, 8, 33, 24 }; L(34, 16, 20, 16); O(20, 26, 10, M_LEFT); L(20, 36, 33, 36); P(g, 3); break; }
    case SI_SEARCH: O(20, 20, 12, M_FULL); L(29, 29, 41, 41); break;
    case SI_REPLACE: { static const int8_t a[] = { 42, 16, 33, 9, 33, 23 }, b[] = { 6, 32, 15, 25, 15, 39 }; L(8, 16, 34, 16); P(a, 3); L(40, 32, 14, 32); P(b, 3); break; }
    case SI_GOTO:   { static const int8_t g[] = { 42, 30, 32, 22, 32, 38 }; L(11, 7, 11, 30); L(11, 30, 33, 30); P(g, 3); break; }
    case SI_COMMENT: { static const int8_t g[] = { 13, 31, 23, 31, 11, 42 }; B(5, 7, 43, 33, 7, 0); P(g, 3); L(13, 17, 35, 17); L(13, 24, 28, 24); break; }
    case SI_FILE:   file_page(); L(17, 22, 31, 22); L(17, 29, 31, 29); L(17, 36, 26, 36); break;
    case SI_FILE_CODE: file_page(); L(21, 23, 16, 29); L(16, 29, 21, 35); L(27, 23, 32, 29); L(32, 29, 27, 35); break;
    case SI_DISK:   B(6, 6, 42, 42, 5, 0); B(15, 6, 31, 17, 1, 0); B(13, 26, 35, 42, 2, 0); break;
    case SI_EDIT:   { static const int8_t g[] = { 31, 9, 39, 17, 18, 38, 8, 40, 10, 30 }; P(g, 5); L(8, 43, 40, 43); break; }
    case SI_GRID:   B(6, 6, 21, 21, 4, 0); B(27, 6, 42, 21, 4, 0); B(6, 27, 21, 42, 4, 0); B(27, 27, 42, 42, 4, 0); break;
    case SI_COPY:   B(16, 16, 42, 42, 5, 0); L(10, 32, 7, 32); L(7, 32, 7, 7); L(7, 7, 32, 7); L(32, 7, 32, 10); break;
    case SI_CLOSE:  L(12, 12, 36, 36); L(36, 12, 12, 36); break;
    case SI_SPEAKER_OFF: speaker_body(); L(30, 17, 43, 31); L(43, 17, 30, 31); break;
    case SI_LOOP:   { static const int8_t a[] = { 38, 7, 44, 13, 38, 19 }, b[] = { 10, 29, 4, 35, 10, 41 };
                      L(9, 24, 9, 17); L(9, 17, 16, 13); L(16, 13, 39, 13); P(a, 3); L(39, 24, 39, 31); L(39, 31, 32, 35); L(32, 35, 9, 35); P(b, 3); break; }
    case SI_TERMINAL: B(4, 7, 44, 41, 5, 0); L(12, 17, 19, 24); L(19, 24, 12, 31); L(23, 32, 34, 32); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ texto */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
int si_text_w(const char *s, int px, int bold)
{
    int n = s_len(s);
    if (!n) return 0;
    return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8;
}
static void text_n(int32_t x, int32_t top, int32_t h, const char *s, int n, int px, uint32_t rgb, int bold)
{
    if (n <= 0) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, rgb, bold ? WF_BOLD : 0, 255); return; }
    char b[160]; int k = n < 159 ? n : 159;
    for (int i = 0; i < k; ++i) b[i] = s[i];
    b[k] = 0;
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), b, rgb, 1);
}
void si_text(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t rgb, int bold) { text_n(x, top, h, s, s_len(s), px, rgb, bold); }
void si_text_r(int32_t right, int32_t top, int32_t h, const char *s, int px, uint32_t rgb, int bold) { si_text(right - si_text_w(s, px, bold), top, h, s, px, rgb, bold); }
void si_text_c(int32_t x, int32_t top, int32_t w, int32_t h, const char *s, int px, uint32_t rgb, int bold) { si_text(x + (w - si_text_w(s, px, bold)) / 2, top, h, s, px, rgb, bold); }
void si_text_fit(int32_t x, int32_t top, int32_t h, const char *s, int maxw, int px, uint32_t rgb, int bold)
{
    int n = s_len(s);
    if (maxw <= 0 || !n) return;
    if (si_text_w(s, px, bold) <= maxw) { text_n(x, top, h, s, n, px, rgb, bold); return; }
    char b[160]; if (n > 150) n = 150;
    int dots = si_text_w("...", px, bold);
    while (n > 0) {
        for (int i = 0; i < n; ++i) b[i] = s[i];
        b[n] = 0;
        if (si_text_w(b, px, bold) + dots <= maxw) break;
        --n;
    }
    b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '.'; b[n + 3] = 0;
    text_n(x, top, h, b, n + 3, px, rgb, bold);
}

/* ------------------------------------------------------------------ controles */
void si_card(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t fill)
{
    if (w < 4 || h < 4) return;
    fb_fill_rrect_aa(x, y, w, h, 12, fill);
    fb_stroke_rrect_aa(x, y, w, h, 12, SI_LINE);
}
struct sty { uint32_t fill, edge, fg, ic; };
static struct sty style_of(int kind, uint32_t accent, int disabled)
{
    struct sty s;
    switch (kind) {
    case SIB_ACCENT:  s.fill = si_mix(SI_WIN, accent, 60); s.edge = si_mix(SI_WIN, accent, 150); s.fg = 0x00F8FAFCu; s.ic = si_mix(accent, 0x00FFFFFFu, 90); break;
    case SIB_ON:      s.fill = si_mix(SI_WIN, accent, 84); s.edge = accent; s.fg = 0x00FFFFFFu; s.ic = si_mix(accent, 0x00FFFFFFu, 120); break;
    case SIB_DANGER:  s.fill = 0x00BE123Cu; s.edge = 0x00FB7185u; s.fg = 0x00FFFFFFu; s.ic = 0x00FFFFFFu; break;
    case SIB_SUCCESS: s.fill = si_mix(SI_WIN, SI_GREEN, 58); s.edge = si_mix(SI_WIN, SI_GREEN, 150); s.fg = 0x00F8FAFCu; s.ic = SI_GREEN; break;
    case SIB_GHOST:   s.fill = SI_WIN; s.edge = SI_LINE; s.fg = SI_DIM; s.ic = SI_DIM; break;
    default:          s.fill = 0x00172131u; s.edge = 0x002A3648u; s.fg = SI_TXT; s.ic = 0x00CBD5E1u; break;
    }
    if (disabled) { s.fill = SI_CARD; s.edge = SI_LINE; s.fg = SI_FAINT; s.ic = SI_FAINT; }
    return s;
}
void si_button(int32_t x, int32_t y, int32_t w, int32_t h, int icon, const char *label, int kind, uint32_t accent, int disabled)
{
    if (w < 8 || h < 8) return;
    struct sty s = style_of(kind, accent, disabled);
    int32_t r = h >= 30 ? 8 : 6;
    fb_fill_rrect_aa(x, y, w, h, r, s.fill);
    fb_stroke_rrect_aa(x, y, w, h, r, s.edge);
    int32_t is = h * 9 / 16; if (is > 20) is = 20;
    int lw = label && label[0] ? si_text_w(label, 13, 1) : 0;
    int32_t tot = (icon ? is : 0) + (icon && lw ? 8 : 0) + lw;
    if (lw && tot > w - 16) { lw = 0; tot = icon ? is : 0; }        /* no cabe el texto: solo el icono */
    if (!lw && !icon && label && label[0]) { si_text_fit(x + 8, y, h, label, w - 16, 13, s.fg, 1); return; }
    int32_t cx = x + (w - tot) / 2;
    if (icon) { si_icon(icon, cx + is / 2, y + h / 2, is, s.ic); cx += is + (lw ? 8 : 0); }
    if (lw) si_text(cx, y, h, label, 13, s.fg, 1);
}
void si_round(int32_t cx, int32_t cy, int32_t r, int icon, int kind, uint32_t accent, int disabled)
{
    struct sty s = style_of(kind, accent, disabled);
    if (kind == SIB_DANGER && !disabled) {                          /* boton de grabar: aro tenue y disco rojo */
        r_circle(cx * 16 + 8, cy * 16 + 8, r * 16, 0, M_FULL, si_mix(SI_WIN, SI_RED, 70));
        r_circle(cx * 16 + 8, cy * 16 + 8, (r - 5) * 16, 0, M_FULL, 0x00E11D48u);
        r_circle(cx * 16 + 8, cy * 16 + 8, (r - 5) * 16, 20, M_FULL, 0x00FB7185u);
    } else {
        r_circle(cx * 16 + 8, cy * 16 + 8, r * 16, 0, M_FULL, s.fill);
        r_circle(cx * 16 + 8, cy * 16 + 8, r * 16 - 8, 16, M_FULL, s.edge);
    }
    if (icon) si_icon(icon, cx, cy, r * 11 / 10, s.ic);
}
void si_round_label(int32_t cx, int32_t cy, int32_t r, const char *label, uint32_t rgb)
{
    int w = si_text_w(label, 12, 0);
    si_text(cx - w / 2, cy + r + 6, 16, label, 12, rgb, 0);
}
int si_chip_w(int32_t h, const char *text, int dot) { return 10 + (dot ? 12 : 0) + si_text_w(text, 12, 1) + 10 + (h > 24 ? 2 : 0); }
int si_chip(int32_t x, int32_t y, int32_t h, const char *text, uint32_t rgb, int dot)
{
    int w = si_chip_w(h, text, dot);
    fb_fill_rrect_aa(x, y, w, h, h / 2, si_mix(SI_WIN, rgb, 46));
    fb_stroke_rrect_aa(x, y, w, h, h / 2, si_mix(SI_WIN, rgb, 120));
    int32_t tx = x + 10 + (h > 24 ? 1 : 0);
    if (dot) { r_circle((tx + 3) * 16 + 8, (y + h / 2) * 16, 56, 0, M_FULL, rgb); tx += 12; }
    si_text(tx, y, h, text, 12, si_mix(rgb, 0x00FFFFFFu, 110), 1);
    return w;
}
void si_badge(int32_t x, int32_t y, int32_t size, int icon, uint32_t accent)
{
    fb_fill_rrect_aa(x, y, size, size, size / 4, si_mix(SI_WIN, accent, 52));
    fb_stroke_rrect_aa(x, y, size, size, size / 4, si_mix(SI_WIN, accent, 110));
    si_icon(icon, x + size / 2, y + size / 2, size * 14 / 25, si_mix(accent, 0x00FFFFFFu, 60));
}
void si_header(int32_t x, int32_t y, int32_t w, int icon, uint32_t accent, const char *title, const char *subtitle)
{
    si_badge(x, y, 44, icon, accent);
    si_text_fit(x + 58, y + 1, 24, title, w - 58, 18, SI_TXT, 1);
    if (subtitle && subtitle[0]) si_text_fit(x + 58, y + 25, 18, subtitle, w - 58, 12, SI_DIM, 0);
}
void si_meter(int32_t x, int32_t y, int32_t w, int32_t h, int pct, int hold, int active)
{
    int n = (w + 2) / 7;
    if (n < 4) return;
    int on = active ? n * pct / 100 : 0, hs = active && hold > 0 ? n * hold / 100 : -1;
    if (hs >= n) hs = n - 1;
    for (int i = 0; i < n; ++i) {
        int f = i * 100 / n;
        uint32_t c = f < 60 ? SI_GREEN : f < 85 ? SI_AMBER : SI_RED;
        uint32_t col = i < on ? c : i == hs ? si_mix(c, 0x00FFFFFFu, 60) : 0x001A2333u;
        fb_fill_rrect_aa(x + i * 7, y, 5, h, 2, col);
    }
}
