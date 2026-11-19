/* PARCHE 088: visor de imagenes rehecho con la estetica del explorador.
 * Abre PNG, JPEG (base, progresivo y CMYK), GIF y BMP de cualquier tamano (los decodificadores del navegador y el VFS sin tope de
 * 16 KiB), por ejemplo los descargados con 'descargar' o guardados desde el navegador. Galeria de todas las imagenes de RamFS con
 * miniaturas, zoom con la rueda hacia el puntero, arrastre, ajustar / 1:1, giro, fondo de cuadros para la transparencia, copiar y pegar. */
#include "imgview.h"
#include "image.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "clipboard.h"
#include "web_img.h"
#include "web_font.h"

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB
#define BAR_H 48
#define ST_H 26
#define STRIP_H 96
#define MAXG 160
#define NMIP 7

/* SOMA_212_IMAGENES: varias ventanas del visor. Lo que es de una ventana (imagen, zoom, desplazamiento, fondo, posicion en la
 * galeria...) vive en una instancia; la galeria y sus miniaturas se comparten. La GUI elige la instancia con imgview_select(). */
struct iv_inst {
    char g_path[128];
    uint32_t *g_px; int g_w, g_h, g_nw, g_nh; uint32_t g_fsize; char g_fmt[16];
    uint32_t *g_mip[NMIP]; int g_mw[NMIP], g_mh[NMIP];
    int g_zoom, g_ox, g_oy;
    int g_bg, g_strip, g_drag, g_dx, g_dy, g_dox, g_doy, g_alpha;
    char g_status[140]; int g_st_ok;
    int g_gi, g_strip_scroll;
    int g_last_vw, g_last_vh;
};
#define IV_INST_MAX 8
static struct iv_inst iv_insts[IV_INST_MAX + 1];
static int iv_used[IV_INST_MAX + 1];
static struct iv_inst *IV = &iv_insts[1];
static void iv_defaults(struct iv_inst *p)
{
    char *b = (char *)p; for (size_t i = 0; i < sizeof *p; ++i) b[i] = 0;
    p->g_strip = 1; p->g_st_ok = 1; p->g_gi = -1; p->g_last_vw = 800; p->g_last_vh = 600;
    const char *t = "Visor de imagenes"; int k = 0; while (t[k] && k < 139) { p->g_status[k] = t[k]; k++; } p->g_status[k] = 0;
}
static int iv_ready;
static void iv_boot(void) { if (!iv_ready) { iv_ready = 1; iv_defaults(&iv_insts[1]); } }
#define g_path (IV->g_path)
#define g_px (IV->g_px)
#define g_w (IV->g_w)
#define g_h (IV->g_h)
#define g_nw (IV->g_nw)
#define g_nh (IV->g_nh)
#define g_fsize (IV->g_fsize)
#define g_fmt (IV->g_fmt)
#define g_mip (IV->g_mip)
#define g_mw (IV->g_mw)
#define g_mh (IV->g_mh)
#define g_zoom (IV->g_zoom)
#define g_ox (IV->g_ox)
#define g_oy (IV->g_oy)
#define g_bg (IV->g_bg)
#define g_strip (IV->g_strip)
#define g_drag (IV->g_drag)
#define g_dx (IV->g_dx)
#define g_dy (IV->g_dy)
#define g_dox (IV->g_dox)
#define g_doy (IV->g_doy)
#define g_alpha (IV->g_alpha)
#define g_status (IV->g_status)
#define g_st_ok (IV->g_st_ok)
#define g_gi (IV->g_gi)
#define g_strip_scroll (IV->g_strip_scroll)
#define g_last_vw (IV->g_last_vw)
#define g_last_vh (IV->g_last_vh)
static char g_gal[MAXG][96]; static int g_ng;
static uint32_t *g_th[MAXG]; static uint8_t g_thw[MAXG], g_thh[MAXG], g_thfail[MAXG];

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int in_r(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void status(const char *m, int ok) { s_cpy(g_status, m, sizeof g_status); g_st_ok = ok; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int has_ext(const char *s, const char *e) { int a = s_len(s), b = s_len(e); if (a <= b || s[a - b - 1] != '.') return 0; for (int i = 0; i < b; i++) if (lower(s[a - b + i]) != e[i]) return 0; return 1; }
static int is_img(const char *s) { return has_ext(s, "png") || has_ext(s, "jpg") || has_ext(s, "jpeg") || has_ext(s, "gif") || has_ext(s, "bmp") || has_ext(s, "webp"); }

/* ---------------------------------------------------------------- imagen */
static void free_mips(void) { for (int i = 1; i < NMIP; i++) { if (g_mip[i]) kfree(g_mip[i]); g_mip[i] = 0; } g_mip[0] = 0; }
static void free_img(void) { free_mips(); if (g_px) kfree(g_px); g_px = 0; g_w = g_h = 0; }
static void build_mips(void)
{
    free_mips();
    g_mip[0] = g_px; g_mw[0] = g_w; g_mh[0] = g_h;
    for (int l = 1; l < NMIP; l++) {
        int pw = g_mw[l - 1], ph = g_mh[l - 1]; if (pw < 64 || ph < 64) break;
        int w = pw / 2, h = ph / 2; uint32_t *m = (uint32_t *)kmalloc((size_t)w * h * 4); if (!m) break;
        const uint32_t *s = g_mip[l - 1];
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            uint32_t a = s[(size_t)(2 * y) * pw + 2 * x], b = s[(size_t)(2 * y) * pw + 2 * x + 1], c = s[(size_t)(2 * y + 1) * pw + 2 * x], d = s[(size_t)(2 * y + 1) * pw + 2 * x + 1];
            uint32_t r = 0;
            for (int sh = 0; sh < 32; sh += 8) r |= ((((a >> sh) & 255) + ((b >> sh) & 255) + ((c >> sh) & 255) + ((d >> sh) & 255) + 2) / 4) << sh;
            m[(size_t)y * w + x] = r;
        }
        g_mip[l] = m; g_mw[l] = w; g_mh[l] = h;
    }
}
static void after_load(void)
{
    g_alpha = 0; for (int i = 0; i < g_w * g_h; i += 13) if ((g_px[i] >> 24) != 0xFF) { g_alpha = 1; break; }
    build_mips(); g_zoom = 0; g_ox = g_oy = 0;
}
void imgview_load_file(const char *path)
{
    iv_boot();
    free_img();
    s_cpy(g_path, path, sizeof g_path);
    g_gi = -1; for (int i = 0; i < g_ng; i++) if (str_eq(g_gal[i], path)) { g_gi = i; break; }
    uint32_t n = 0; uint8_t *d = (uint8_t *)vfs_read_alloc(path, &n);
    if (!d && str_eq(path, "/workspace/logo_soma.bmp")) { d = (uint8_t *)kmalloc(20000); if (d) { int k = image_create_demo_bmp(d, 20000, 80, 40); n = k > 0 ? (uint32_t)k : 0; } }
    if (!d) { status("No se pudo leer el archivo", 0); return; }
    g_fsize = n;
    s_cpy(g_fmt, n >= 8 && d[0] == 0x89 && d[1] == 'P' ? "PNG" : n >= 3 && d[0] == 0xFF && d[1] == 0xD8 ? "JPEG" : n >= 4 && d[0] == 'G' && d[1] == 'I' ? "GIF" : n >= 2 && d[0] == 'B' && d[1] == 'M' ? "BMP" :
          n >= 12 && d[8] == 'W' && d[9] == 'E' ? "WebP" : "?", sizeof g_fmt);
    struct wimg im; memset(&im, 0, sizeof im);
    int r = wi_decode_fit(d, n, &im, 4096, 4096);     /* las enormes se decodifican ya reducidas */
    kfree(d);
    if (r != 0 || !im.px) {
        wi_free(&im);
        status(r == -2 ? "Sin memoria para decodificar la imagen" : g_fmt[0] == 'W' ? "WebP no soportado (descargala desde el navegador, que la convierte)" : "No se pudo decodificar la imagen", 0);
        return;
    }
    g_px = im.px; g_w = im.w; g_h = im.h; g_nw = im.nw ? im.nw : im.w; g_nh = im.nh ? im.nh : im.h;
    after_load();
    char m[140] = ""; const char *b = path; for (const char *q = path; *q; q++) if (*q == '/') b = q + 1;
    s_cat(m, b, sizeof m); status(m, 1);
}

/* ---------------------------------------------------------------- galeria */
static void scan_gallery(void)
{
    static char list[8192];
    for (int i = 0; i < MAXG; i++) { if (g_th[i]) kfree(g_th[i]); g_th[i] = 0; g_thfail[i] = 0; }
    g_ng = 0;
    int n = vfs_format_list_ext(1, 0, list, sizeof list - 1); if (n <= 0) return;
    list[n] = 0; const char *p = list;
    if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p) p++; }
    while (*p && g_ng < MAXG) {
        while (*p == ' ' || *p == ',') p++;
        char f[96]; int k = 0; while (*p && *p != ',' && k < 95) f[k++] = *p++;
        f[k] = 0; while (*p && *p != ',') p++;
        if (k && is_img(f) && !vfs_is_hidden(f)) s_cpy(g_gal[g_ng++], f, 96);
    }
}
static void make_thumb(int i)
{
    if (i < 0 || i >= g_ng || g_th[i] || g_thfail[i]) return;
    uint32_t n = 0; uint8_t *d = (uint8_t *)vfs_read_alloc(g_gal[i], &n);
    struct wimg im; memset(&im, 0, sizeof im);
    if (!d || wi_decode_fit(d, n, &im, 80, 60) != 0 || !im.px) { if (d) kfree(d); wi_free(&im); g_thfail[i] = 1; return; }
    kfree(d);
    int tw_ = 80, th_ = 60; if (im.w * 60 > im.h * 80) th_ = im.h * 80 / im.w; else tw_ = im.w * 60 / im.h;
    if (tw_ < 1) tw_ = 1;
    if (th_ < 1) th_ = 1;
    uint32_t *t = (uint32_t *)kmalloc((size_t)tw_ * th_ * 4);
    if (t) {
        for (int y = 0; y < th_; y++) for (int x = 0; x < tw_; x++) {
            int x0 = x * im.w / tw_, x1 = (x + 1) * im.w / tw_, y0 = y * im.h / th_, y1 = (y + 1) * im.h / th_; if (x1 <= x0) x1 = x0 + 1; if (y1 <= y0) y1 = y0 + 1;
            uint32_t r = 0, g = 0, b = 0, a = 0, c = 0;
            for (int yy = y0; yy < y1; yy += 1 + (y1 - y0) / 4) for (int xx = x0; xx < x1; xx += 1 + (x1 - x0) / 4) { uint32_t p = im.px[(size_t)yy * im.w + xx]; uint32_t al = p >> 24; r += ((p >> 16) & 255) * al / 255 + (255 - al) * 30 / 255; g += ((p >> 8) & 255) * al / 255 + (255 - al) * 38 / 255; b += (p & 255) * al / 255 + (255 - al) * 48 / 255; a++; c++; }
            t[y * tw_ + x] = 0xFF000000u | ((r / c) << 16) | ((g / c) << 8) | (b / c); (void)a;
        }
        g_th[i] = t; g_thw[i] = (uint8_t)tw_; g_thh[i] = (uint8_t)th_;
    }
    wi_free(&im);
}
void imgview_init(void)
{
    iv_boot();
    scan_gallery();
    if (!g_px) {
        if (g_ng > 0) imgview_load_file(g_gal[0]);
        else imgview_load_file("/workspace/logo_soma.bmp");
    }
}

/* ---------------------------------------------------------------- disposicion */
struct lay { int x, y, w, h, vx, vy, vw, vh, sy, st_y; };
static void lay_of(struct lay *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->st_y = L->y + L->h - ST_H;
    int strip = g_strip && g_ng > 1 ? STRIP_H : 0;
    L->sy = L->st_y - strip;
    L->vx = L->x; L->vy = L->y + BAR_H; L->vw = L->w; L->vh = L->sy - L->vy;
    g_last_vw = L->vw; g_last_vh = L->vh;
}
static int fit_zoom(int vw, int vh)   /* 1/1000 */
{
    if (!g_px) return 1000;
    int a = (vw - 48) * 1000 / g_w, b = (vh - 48) * 1000 / g_h, z = a < b ? a : b;
    if (z > 1000 && g_w > 48 && g_h > 48) z = 1000;            /* las pequenas no se agrandan al ajustar... */
    if (g_w <= 48 || g_h <= 48) z = z > 8000 ? 8000 : z;      /* ...salvo los iconos */
    return z < 10 ? 10 : z;
}
static int cur_zoom(void) { return g_zoom > 0 ? g_zoom : fit_zoom(g_last_vw, g_last_vh); }
enum { B_PREV = 1, B_NEXT, B_ZOUT, B_ZIN, B_FIT, B_ONE, B_ROT, B_BG, B_COPY, B_STRIP };
struct btn { int id, x, w; const char *label; };
static int bar_buttons(const struct lay *L, struct btn *b)
{
    int n = 0, x = L->x + 12;
    b[n++] = (struct btn){ B_PREV, x, 34, 0 }; x += 34 + 6;
    b[n++] = (struct btn){ B_NEXT, x, 34, 0 }; x += 34 + 14;
    b[n++] = (struct btn){ B_STRIP, x, 34, 0 }; x += 34 + 6;
    int r = L->x + L->w - 12;
    r -= 76; b[n++] = (struct btn){ B_COPY, r, 76, "Copiar" }; r -= 8;
    r -= 34; b[n++] = (struct btn){ B_BG, r, 34, 0 }; r -= 6;
    r -= 34; b[n++] = (struct btn){ B_ROT, r, 34, 0 }; r -= 10;
    r -= 48; b[n++] = (struct btn){ B_ONE, r, 48, "1:1" }; r -= 6;
    r -= 72; b[n++] = (struct btn){ B_FIT, r, 72, "Ajustar" }; r -= 10;
    r -= 34; b[n++] = (struct btn){ B_ZIN, r, 34, 0 }; r -= 62;
    r -= 34; b[n++] = (struct btn){ B_ZOUT, r, 34, 0 };
    return n;
}
static void icon(int id, int cx, int cy, uint32_t col)
{
    uint32_t a = 0xFF000000u | col; int s = 16;
    switch (id) {
    case B_PREV: fb_line_aa((cx + 3) * s, (cy - 6) * s, (cx - 3) * s, cy * s, 30, a); fb_line_aa((cx - 3) * s, cy * s, (cx + 3) * s, (cy + 6) * s, 30, a); break;
    case B_NEXT: fb_line_aa((cx - 3) * s, (cy - 6) * s, (cx + 3) * s, cy * s, 30, a); fb_line_aa((cx + 3) * s, cy * s, (cx - 3) * s, (cy + 6) * s, 30, a); break;
    case B_ZOUT: fb_line_aa((cx - 6) * s, cy * s, (cx + 6) * s, cy * s, 30, a); break;
    case B_ZIN: fb_line_aa((cx - 6) * s, cy * s, (cx + 6) * s, cy * s, 30, a); fb_line_aa(cx * s, (cy - 6) * s, cx * s, (cy + 6) * s, 30, a); break;
    case B_ROT: fb_stroke_rrect_aa(cx - 6, cy - 6, 12, 12, 6, a); fb_fill_rect((uint32_t)(cx + 2), (uint32_t)(cy - 8), 6, 6, KC_BAR); fb_line_aa((cx + 2) * s, (cy - 7) * s, (cx + 6) * s, (cy - 3) * s, 22, a); fb_line_aa((cx + 6) * s, (cy - 3) * s, (cx + 7) * s, (cy - 9) * s, 22, a); break;
    case B_BG: for (int i = 0; i < 4; i++) fb_fill_rect((uint32_t)(cx - 7 + (i & 1) * 7), (uint32_t)(cy - 7 + (i >> 1) * 7), 7, 7, ((i == 0 || i == 3) ? 0x00CBD5E1 : 0x00475569)); break;
    case B_STRIP: fb_stroke_rrect_aa(cx - 8, cy - 6, 16, 12, 2, a); fb_fill_rect((uint32_t)(cx - 8), (uint32_t)(cy + 2), 16, 1, col); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- dibujo */
static void draw_image(const struct lay *L)
{
    int z = cur_zoom();
    int dw = (int)((int64_t)g_w * z / 1000), dh = (int)((int64_t)g_h * z / 1000); if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int cx = L->vx + L->vw / 2 + g_ox, cy = L->vy + L->vh / 2 + g_oy;
    int x0 = cx - dw / 2, y0 = cy - dh / 2;
    fb_draw_shadow(x0, y0, dw, dh, 2, 16, 6, 0x80000000u);
    /* nivel de mip: el menor que siga siendo >= que lo que se pinta */
    int lv = 0; while (lv + 1 < NMIP && g_mip[lv + 1] && g_mw[lv + 1] >= dw && g_mh[lv + 1] >= dh) lv++;
    const uint32_t *src = g_mip[lv]; int sw = g_mw[lv], sh = g_mh[lv];
    int vx0 = x0 < L->vx ? L->vx : x0, vy0 = y0 < L->vy ? L->vy : y0, vx1 = x0 + dw > L->vx + L->vw ? L->vx + L->vw : x0 + dw, vy1 = y0 + dh > L->vy + L->vh ? L->vy + L->vh : y0 + dh;
    if (vx1 <= vx0 || vy1 <= vy0) return;
    int rw = vx1 - vx0; uint32_t *row = (uint32_t *)kmalloc((size_t)rw * 4); if (!row) return;
    int smooth = dw < sw;                                     /* al reducir: bilineal; al ampliar: pixeles nitidos */
    for (int y = vy0; y < vy1; y++) {
        int64_t fy = smooth ? (((int64_t)(y - y0) * 2 + 1) * sh * 128 / dh) - 128 : (int64_t)(y - y0) * sh * 256 / dh;
        if (fy < 0) fy = 0;
        int sy = (int)(fy >> 8), ay = (int)(fy & 255); if (sy >= sh - 1) { sy = sh - 1; ay = 0; }
        const uint32_t *r0 = src + (size_t)sy * sw, *r1 = ay ? r0 + sw : r0;
        for (int x = vx0; x < vx1; x++) {
            int64_t fx = smooth ? (((int64_t)(x - x0) * 2 + 1) * sw * 128 / dw) - 128 : (int64_t)(x - x0) * sw * 256 / dw;
            if (fx < 0) fx = 0;
            int sx = (int)(fx >> 8), ax = (int)(fx & 255); if (sx >= sw - 1) { sx = sw - 1; ax = 0; }
            uint32_t p;
            if (!smooth || (!ax && !ay)) p = r0[sx];
            else {
                uint32_t a = r0[sx], b = r0[sx + (ax ? 1 : 0)], c = r1[sx], d = r1[sx + (ax ? 1 : 0)]; p = 0;
                for (int s = 0; s < 32; s += 8) { int t = (int)((a >> s) & 255) * (256 - ax) + (int)((b >> s) & 255) * ax, u = (int)((c >> s) & 255) * (256 - ax) + (int)((d >> s) & 255) * ax; p |= (uint32_t)(((t * (256 - ay) + u * ay) >> 16) & 255) << s; }
            }
            uint32_t al = p >> 24;
            if (al != 255) {   /* fondo para la transparencia */
                uint32_t bgc = g_bg == 1 ? 0x00FFFFFF : g_bg == 2 ? 0x00101418 : ((((x - x0) >> 3) + ((y - y0) >> 3)) & 1) ? 0x00CBD5E1 : 0x00F1F5F9;
                uint32_t ia = 255 - al;
                p = (((p & 0xFF00FF) * al + (bgc & 0xFF00FF) * ia) >> 8 & 0xFF00FF) | (((p & 0xFF00) * al + (bgc & 0xFF00) * ia) >> 8 & 0xFF00);
            }
            row[x - vx0] = p & 0xFFFFFF;
        }
        fb_blit(vx0, y, row, rw, 1, rw);
    }
    kfree(row);
}
void imgview_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int show_cursor)
{
    (void)show_cursor;
    struct lay L; lay_of(&L, x, y, w, h);
    fb_fill_rect(x, y, w, h, KC_WIN);
    fb_fill_rect(x, y, w, BAR_H, KC_BAR); fb_fill_rect(x, y + BAR_H - 1, w, 1, KC_LINE);
    struct btn b[12]; int nb = bar_buttons(&L, b);
    for (int i = 0; i < nb; i++) {
        int by = L.y + 8, bh = 32;
        int on = (b[i].id == B_FIT && g_zoom == 0) || (b[i].id == B_ONE && g_zoom == 1000) || (b[i].id == B_STRIP && g_strip);
        fb_fill_rounded_rect(b[i].x, by, b[i].w, bh, 9, on ? KC_SELBG : 0x00182133);
        fb_stroke_rrect_aa(b[i].x, by, b[i].w, bh, 9, on ? 0xFF2B5C8Au : 0xFF263142u);
        int dis = !g_px && b[i].id != B_PREV && b[i].id != B_NEXT && b[i].id != B_STRIP;
        uint32_t c = dis ? KC_FAINT : KC_TXT;
        if (b[i].label) tx(b[i].x + (b[i].w - tw(b[i].label, 12, 0)) / 2, by, bh, b[i].label, 12, c, 0);
        else icon(b[i].id, b[i].x + b[i].w / 2, by + bh / 2, c);
        if (b[i].id == B_ZOUT) { char t[16] = ""; s_num(t, sizeof t, (cur_zoom() + 5) / 10); s_cat(t, " %", sizeof t); tx(b[i].x + b[i].w + 31 - tw(t, 12, 1) / 2, by, bh, t, 12, g_px ? KC_TXT : KC_FAINT, 1); }
    }
    /* titulo en la barra */
    {
        char t[100] = ""; const char *bn = g_path; for (const char *q = g_path; *q; q++) if (*q == '/') bn = q + 1;
        s_cat(t, bn, sizeof t);
        int tx0 = L.x + 12 + 34 * 3 + 6 + 14 + 6 + 14, maxw = (b[nb - 1].x - 20) - tx0;
        if (maxw > 60) { fb_set_clip(tx0, L.y, maxw, BAR_H); tx(tx0, L.y, BAR_H, t, 14, KC_TXT, 1); if (g_ng > 1 && g_gi >= 0) { char c2[24] = ""; s_num(c2, sizeof c2, g_gi + 1); s_cat(c2, " de ", sizeof c2); s_num(c2, sizeof c2, g_ng); tx(tx0 + tw(t, 14, 1) + 12, L.y, BAR_H, c2, 12, KC_DIM, 0); } fb_reset_clip(); }
    }
    /* vista */
    fb_set_clip(L.vx, L.vy, L.vw, L.vh);
    fb_fill_rect((uint32_t)L.vx, (uint32_t)L.vy, (uint32_t)L.vw, (uint32_t)L.vh, g_bg == 1 ? 0x00E2E8F0 : 0x0020262F);
    if (g_px) draw_image(&L);
    else {
        int cx = L.vx + L.vw / 2, cy = L.vy + L.vh / 2;
        fb_stroke_rrect_aa(cx - 40, cy - 70, 80, 60, 10, 0xFF475569u);
        fb_fill_rrect_aa(cx - 22, cy - 56, 14, 14, 7, 0xFF475569u);
        fb_line_aa((cx - 36) * 16, (cy - 16) * 16, (cx - 10) * 16, (cy - 40) * 16, 32, 0xFF475569u); fb_line_aa((cx - 10) * 16, (cy - 40) * 16, (cx + 36) * 16, (cy - 16) * 16, 32, 0xFF475569u);
        tx(cx - tw(g_status, 15, 1) / 2, cy + 4, 28, g_status, 15, g_st_ok ? KC_TXT : 0x00F87171, 1);
        const char *t2 = "PNG, JPEG, GIF y BMP de cualquier tamano. Descarga con: descargar <url>";
        tx(cx - tw(t2, 12, 0) / 2, cy + 34, 22, t2, 12, KC_DIM, 0);
    }
    fb_reset_clip();
    /* tira de miniaturas */
    if (g_strip && g_ng > 1) {
        fb_fill_rect(x, (uint32_t)L.sy, w, STRIP_H, KC_SIDE); fb_fill_rect(x, (uint32_t)L.sy, w, 1, KC_LINE);
        int cellw = 96, n_vis = (L.w - 16) / cellw;
        if (g_gi >= 0) { if (g_gi < g_strip_scroll) g_strip_scroll = g_gi; if (g_gi >= g_strip_scroll + n_vis) g_strip_scroll = g_gi - n_vis + 1; }
        if (g_strip_scroll > g_ng - n_vis) g_strip_scroll = g_ng - n_vis;
        if (g_strip_scroll < 0) g_strip_scroll = 0;
        int made = 0;
        fb_set_clip(L.x, L.sy + 1, L.w, STRIP_H - 1);
        for (int k = 0; k < n_vis && k + g_strip_scroll < g_ng; k++) {
            int i = k + g_strip_scroll, cx = L.x + 8 + k * cellw, cy = L.sy + 8;
            if (!g_th[i] && !g_thfail[i] && made < 2) { make_thumb(i); made++; }
            if (i == g_gi) fb_fill_rrect_aa(cx, cy - 2, cellw - 8, STRIP_H - 12, 8, 0xFF1A3558u);
            if (g_th[i]) { int tx_ = cx + (cellw - 8 - g_thw[i]) / 2, ty_ = cy + 2 + (60 - g_thh[i]) / 2; fb_blit(tx_, ty_, g_th[i], g_thw[i], g_thh[i], g_thw[i]); fb_stroke_rrect_aa(tx_ - 1, ty_ - 1, g_thw[i] + 2, g_thh[i] + 2, 2, i == g_gi ? 0xFF38BDF8u : 0xFF334155u); }
            else fb_stroke_rrect_aa(cx + 10, cy + 6, cellw - 28, 52, 6, 0xFF334155u);
            const char *bn = g_gal[i]; for (const char *q = g_gal[i]; *q; q++) if (*q == '/') bn = q + 1;
            fb_set_clip(cx + 2, cy + 64, cellw - 12, 18); tx(cx + 4, cy + 64, 18, bn, 10, i == g_gi ? KC_ACC : KC_DIM, 0); fb_set_clip(L.x, L.sy + 1, L.w, STRIP_H - 1);
        }
        fb_reset_clip();
    }
    /* estado */
    fb_fill_rect(x, (uint32_t)L.st_y, w, ST_H, KC_BAR); fb_fill_rect(x, (uint32_t)L.st_y, w, 1, KC_LINE);
    char st[180] = "";
    if (g_px) {
        s_num(st, sizeof st, g_nw); s_cat(st, " x ", sizeof st); s_num(st, sizeof st, g_nh); s_cat(st, " px   |   ", sizeof st);
        s_cat(st, g_fmt, sizeof st); s_cat(st, "   |   ", sizeof st);
        if (g_fsize >= 1048576) { s_num(st, sizeof st, (int)(g_fsize / 1048576)); s_cat(st, ".", sizeof st); s_num(st, sizeof st, (int)(g_fsize % 1048576 * 10 / 1048576)); s_cat(st, " MiB", sizeof st); }
        else { s_num(st, sizeof st, (int)((g_fsize + 1023) / 1024)); s_cat(st, " KiB", sizeof st); }
        if (g_alpha) s_cat(st, "   |   con transparencia", sizeof st);
        if (g_nw != g_w) { s_cat(st, "   |   vista a ", sizeof st); s_num(st, sizeof st, g_w); s_cat(st, " px", sizeof st); }
    } else s_cpy(st, "Visor de imagenes de SOMA", sizeof st);
    tx(L.x + 12, L.st_y, ST_H, st, 11, KC_DIM, 0);
    if (g_px && g_status[0]) { int mw = tw(g_status, 11, 0); tx(L.x + L.w - 14 - mw, L.st_y, ST_H, g_status, 11, g_st_ok ? KC_DIM : 0x00F87171, 0); }
}

/* ---------------------------------------------------------------- acciones */
static void go(int d)
{
    if (g_ng <= 0) return;
    int i = g_gi < 0 ? 0 : (g_gi + d + g_ng) % g_ng;
    imgview_load_file(g_gal[i]);
}
static void set_zoom(int z, int fx, int fy)   /* fx, fy: punto fijo relativo al centro de la vista */
{
    int old = cur_zoom(); if (z < 10) z = 10;
    if (z > 16000) z = 16000;
    g_ox = (int)(fx - (int64_t)(fx - g_ox) * z / old); g_oy = (int)(fy - (int64_t)(fy - g_oy) * z / old);
    g_zoom = z;
}
static void zoom_step(int dir, int fx, int fy)
{
    static const int st[] = { 10, 25, 50, 75, 100, 150, 200, 300, 400, 600, 800, 1200, 1600 };
    int cur = (cur_zoom() + 5) / 10, n = sizeof st / sizeof st[0], z = cur;
    if (dir > 0) { z = st[n - 1]; for (int i = 0; i < n; i++) if (st[i] > cur) { z = st[i]; break; } }
    else { z = st[0]; for (int i = n - 1; i >= 0; i--) if (st[i] < cur) { z = st[i]; break; } }
    set_zoom(z * 10, fx, fy);
}
static void rotate(void)
{
    if (!g_px) return;
    uint32_t *r = (uint32_t *)kmalloc((size_t)g_w * g_h * 4); if (!r) { status("Sin memoria para girar", 0); return; }
    for (int y = 0; y < g_h; y++) for (int x = 0; x < g_w; x++) r[(size_t)x * g_h + (g_h - 1 - y)] = g_px[(size_t)y * g_w + x];
    free_mips(); kfree(g_px); g_px = r; int t = g_w; g_w = g_h; g_h = t; t = g_nw; g_nw = g_nh; g_nh = t;
    build_mips(); g_ox = g_oy = 0;
    status("Girada 90 grados (solo en pantalla)", 1);
}
int imgview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    struct lay L; lay_of(&L, win_x, win_y, win_w, win_h);
    if (my < L.y + BAR_H) {
        struct btn b[12]; int nb = bar_buttons(&L, b);
        for (int i = 0; i < nb; i++) if (in_r(mx, my, b[i].x, L.y + 8, b[i].w, 32)) {
            switch (b[i].id) {
            case B_PREV: go(-1); break;
            case B_NEXT: go(1); break;
            case B_ZOUT: zoom_step(-1, 0, 0); break;
            case B_ZIN: zoom_step(1, 0, 0); break;
            case B_FIT: g_zoom = 0; g_ox = g_oy = 0; break;
            case B_ONE: set_zoom(1000, 0, 0); break;
            case B_ROT: rotate(); break;
            case B_BG: g_bg = (g_bg + 1) % 3; break;
            case B_COPY: imgview_clip_op(CLIP_OP_COPY); break;
            case B_STRIP: g_strip = !g_strip; if (g_strip) scan_gallery(); break;
            }
            return 0;
        }
        return 0;
    }
    if (g_strip && g_ng > 1 && in_r(mx, my, L.x, L.sy, L.w, STRIP_H)) {
        int k = (mx - L.x - 8) / 96; int i = k + g_strip_scroll;
        if (k >= 0 && i < g_ng) imgview_load_file(g_gal[i]);
        return 0;
    }
    if (in_r(mx, my, L.vx, L.vy, L.vw, L.vh) && g_px) { g_drag = 1; g_dx = mx; g_dy = my; g_dox = g_ox; g_doy = g_oy; }
    return 0;
}
int imgview_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held)
{
    struct lay L; lay_of(&L, win_x, win_y, win_w, win_h);
    int r = 0;
    if (g_drag) {
        if (!held) { g_drag = 0; mouse_set_cursor(CURSOR_ARROW); }
        else { int nx = g_dox + (mx - g_dx), ny = g_doy + (my - g_dy); if (nx != g_ox || ny != g_oy) { g_ox = nx; g_oy = ny; r = 1; } mouse_set_cursor(CURSOR_MOVE); }
    }
    if (wheel) {
        if (g_strip && g_ng > 1 && in_r(mx, my, L.x, L.sy, L.w, STRIP_H)) { g_strip_scroll += wheel > 0 ? 2 : -2; if (g_strip_scroll < 0) g_strip_scroll = 0; g_gi = g_gi; return 1; }
        if (g_px && in_r(mx, my, L.vx, L.vy, L.vw, L.vh)) { zoom_step(wheel > 0 ? -1 : 1, mx - (L.vx + L.vw / 2), my - (L.vy + L.vh / 2)); r = 1; }
    }
    return r;
}
void imgview_handle_key(char ch)
{
    int c = (uint8_t)ch;
    switch (c) {
    case 2: case 28: case 'p': go(-1); break;
    case 6: case 29: case 'n': case ' ': go(1); break;
    case '+': case '=': zoom_step(1, 0, 0); break;
    case '-': zoom_step(-1, 0, 0); break;
    case '0': case 'f': g_zoom = 0; g_ox = g_oy = 0; break;
    case '1': set_zoom(1000, 0, 0); break;
    case 'r': case 'R': rotate(); break;
    case 'b': case 'B': g_bg = (g_bg + 1) % 3; break;
    case 'g': case 'G': g_strip = !g_strip; if (g_strip) scan_gallery(); break;
    case 16: g_oy += 60; break;
    case 14: g_oy -= 60; break;
    default: break;
    }
}
int imgview_clip_op(int op)
{
    char msg[64];
    if (op == CLIP_OP_PASTE) {
        int w = 0, h = 0; const uint32_t *px = clip_image(&w, &h);
        if (!px) {
            uint32_t n = 0; const char *t = clip_text(&n);
            if (clip_kind() == CLIP_FILE && t && n > 4 && is_img(t)) { imgview_load_file(t); clip_toast("Imagen abierta desde el archivo copiado"); return 1; }
            clip_toast("No hay ninguna imagen en el portapapeles"); return 1;
        }
        uint32_t cnt = (uint32_t)w * (uint32_t)h;
        uint32_t *copy = (uint32_t *)kmalloc(cnt * 4u);
        if (!copy) { clip_toast("Memoria insuficiente para pegar la imagen"); return 1; }
        for (uint32_t i = 0; i < cnt; i++) copy[i] = px[i] | 0xFF000000u;
        free_img(); g_px = copy; g_w = g_nw = w; g_h = g_nh = h; g_fsize = cnt * 4; s_cpy(g_fmt, "portapapeles", sizeof g_fmt);
        s_cpy(g_path, "(portapapeles)", sizeof g_path); g_gi = -1;
        after_load(); status("Imagen pegada desde el portapapeles", 1);
        clip_toast("Imagen pegada"); return 1;
    }
    if (!g_px) { clip_toast("No hay imagen que copiar"); return 1; }
    uint32_t *flat = (uint32_t *)kmalloc((size_t)g_w * g_h * 4);
    if (!flat) { clip_toast("Memoria insuficiente"); return 1; }
    for (int i = 0; i < g_w * g_h; i++) { uint32_t p = g_px[i], a = p >> 24; flat[i] = (((p & 0xFF00FF) * a + 0xFF00FF * (255 - a)) >> 8 & 0xFF00FF) | (((p & 0xFF00) * a + 0xFF00 * (255 - a)) >> 8 & 0xFF00); }
    int r = clip_set_image(flat, g_w, g_h); kfree(flat);
    if (r == -2) { clip_toast("Imagen demasiado grande para el portapapeles"); return 1; }
    if (r < 0) { clip_toast("No se pudo copiar la imagen"); return 1; }
    s_cpy(msg, "Imagen copiada ", sizeof msg); s_num(msg, sizeof msg, g_w); s_cat(msg, "x", sizeof msg); s_num(msg, sizeof msg, g_h);
    clip_toast(msg); status(msg, 1);
    return 1;
}



#undef g_path
#undef g_px
#undef g_w
#undef g_h
#undef g_nw
#undef g_nh
#undef g_fsize
#undef g_fmt
#undef g_mip
#undef g_mw
#undef g_mh
#undef g_zoom
#undef g_ox
#undef g_oy
#undef g_bg
#undef g_strip
#undef g_drag
#undef g_dx
#undef g_dy
#undef g_dox
#undef g_doy
#undef g_alpha
#undef g_status
#undef g_st_ok
#undef g_gi
#undef g_strip_scroll
#undef g_last_vw
#undef g_last_vh
/* SOMA_212_IMAGENES: instancias */
int imgview_inst_new(void)
{
    iv_boot();
    for (int k = 1; k <= IV_INST_MAX; ++k) {
        if (iv_used[k]) continue;
        iv_used[k] = 1;
        IV = &iv_insts[k];
        if (IV->g_px) free_img();
        iv_defaults(IV);
        return k;
    }
    return 0;
}
void imgview_select(int slot) { iv_boot(); if (slot >= 1 && slot <= IV_INST_MAX) IV = &iv_insts[slot]; }
void imgview_inst_drop(int slot)
{
    if (slot < 1 || slot > IV_INST_MAX || !iv_used[slot]) return;
    struct iv_inst *keep = IV;
    IV = &iv_insts[slot]; if (IV->g_px) free_img(); iv_defaults(IV);
    iv_used[slot] = 0;
    IV = keep == &iv_insts[slot] ? &iv_insts[1] : keep;
}
int imgview_inst_find(const char *path)
{
    for (int k = 1; k <= IV_INST_MAX; ++k)
        if (iv_used[k] && iv_insts[k].g_px && path && str_eq(iv_insts[k].g_path, path)) return k;
    return 0;
}
const char *imgview_inst_name(int slot)
{
    if (slot < 1 || slot > IV_INST_MAX || !iv_insts[slot].g_px) return "";
    const char *p = iv_insts[slot].g_path, *n = p;
    for (; *p; ++p) if (*p == '/') n = p + 1;
    return n;
}
