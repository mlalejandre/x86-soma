/* PARCHE 085: dialogo "Guardar imagen como". Ver savedlg.h. */
#include "savedlg.h"
#include "fb.h"
#include "fs.h"
#include "mem.h"
#include "web_font.h"
#include "gui_font.h"
#include "png_enc.h"
#include "bigfs_glue.h"
#include "acceso.h"     /* SOMA_363_ACCESO */
#include "medios.h"
#include "agent_policy.h"
#include "clipboard.h"

uint64_t timer_get_uptime_ms(void);

#define KC_CARD    0x00111827
#define KC_LINE    0x00212B3B
#define KC_FIELD   0x00090D15
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_PRIMARY 0x000B6BCB
#define KC_OK      0x0034D399
#define KC_BAD     0x00F87171

static int g_on;
static uint32_t *g_px; static int g_w, g_h;
static char g_name[80], g_dir[96] = "";
static int g_focus;                                   /* 0 nombre, 1 carpeta */
/* PARCHE 092: campos de texto de verdad: cursor, seleccion (lo que se escriba la reemplaza), flechas, Inicio/Fin, Supr, UTF-8 */
static int g_cur[2], g_sa[2], g_sb[2];                 /* cursor y seleccion [sa,sb) por campo */
static uint8_t g_u8[4]; static int g_u8n, g_u8need;
static int g_fmt, g_scale_i;                          /* fmt 0 PNG, 1 BMP; escala 100/75/50/25 */
static int g_est = -1, g_est_key = -1;
static char g_msg[120]; static int g_msg_ok = 1;
static char g_chips[6][64]; static int g_nchips;
static const int SC[4] = { 100, 75, 50, 25 };

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; if (v < 0) v = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (wf_init()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static int tw(const char *s, int px, int bold) { return wf_init() ? wf_width(s, (uint32_t)s_len(s), px, bold ? WF_BOLD : 0) : s_len(s) * 8; }

/* ---------------------------------------------------------------- carpetas propuestas */
static void add_chip(const char *d)
{
    for (int i = 0; i < g_nchips; i++) { const char *a = g_chips[i], *b = d; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return; }
    if (g_nchips < 6) s_cpy(g_chips[g_nchips++], d, 64);
}
/* SOMA_363_ACCESO: solo las carpetas de la persona (Mi unidad y sus carpetas) y los medios USB donde se puede escribir */
static void collect_dirs(void)
{
    g_nchips = 0;
    char d[64];
    if (acceso_carpeta(ACC_IMAGENES, d, sizeof d) != 0) { add_chip("/tmp"); return; }
    add_chip(d);
    if (acceso_carpeta(ACC_DESCARGAS, d, sizeof d) == 0) add_chip(d);
    if (acceso_carpeta(ACC_DOCUMENTOS, d, sizeof d) == 0) add_chip(d);
    add_chip("/usuario");
    for (int i = 0; i < medios_n() && g_nchips < 6; ++i) { struct medio_info mi; if (!medios_info(i, &mi) && mi.tipo == MEDIO_EXFAT) add_chip(mi.ruta); }
}

static void fix_ext(void)
{
    int n = s_len(g_name), dot = -1;
    for (int i = n - 1; i >= 0; i--) { if (g_name[i] == '.') { dot = i; break; } if (g_name[i] == '/') break; }
    if (dot > 0) g_name[dot] = 0;
    if (!g_name[0]) s_cpy(g_name, "imagen", sizeof g_name);
    s_cat(g_name, g_fmt ? ".bmp" : ".png", sizeof g_name);
}

void savedlg_open(const uint32_t *argb, int w, int h, const char *suggest)
{
    if (!argb || w <= 0 || h <= 0) return;
    if (g_px) kfree(g_px);
    g_px = (uint32_t *)kmalloc((size_t)w * h * 4);
    if (!g_px) { clip_toast("Sin memoria para copiar la imagen"); return; }
    for (int i = 0; i < w * h; i++) g_px[i] = argb[i];
    g_w = w; g_h = h;
    /* nombre propuesto: ultimo trozo de la URL sin parametros */
    char nm[80] = ""; int st = 0, n = s_len(suggest);
    for (int i = 0; i < n; i++) if (suggest[i] == '/') st = i + 1;
    int k = 0; for (int i = st; i < n && suggest[i] != '?' && suggest[i] != '#' && k < 60; i++) { char c = suggest[i]; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') nm[k++] = c; }
    nm[k] = 0;
    s_cpy(g_name, nm[0] ? nm : "imagen", sizeof g_name);
    g_fmt = 0; g_scale_i = 0; g_focus = 0; g_est_key = -1; g_msg[0] = 0;
    /* PARCHE 087: ya no hace falta reducirla para que quepa: se guarda al tamano original */
    fix_ext();
    collect_dirs();
    if (!acceso_ruta_ok(g_dir) || !g_dir[0]) s_cpy(g_dir, g_chips[0], sizeof g_dir);   /* SOMA_363: la ultima carpeta si sigue valiendo; si no, Imagenes */
    { int n = s_len(g_name), d = n; for (int i = n - 1; i > 0; i--) if (g_name[i] == '.') { d = i; break; }
      g_sa[0] = 0; g_sb[0] = d; g_cur[0] = d; int m = s_len(g_dir); g_sa[1] = g_sb[1] = g_cur[1] = m; g_u8n = 0; }
    g_on = 1;
}
int savedlg_active(void) { return g_on; }

/* pixeles a la escala elegida (vecino mas cercano con promedio 2x2 si reduce) */
static uint32_t *scaled(int *ow, int *oh)
{
    int s = SC[g_scale_i], w = g_w * s / 100, h = g_h * s / 100;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    uint32_t *o = (uint32_t *)kmalloc((size_t)w * h * 4); if (!o) return 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int sx = x * g_w / w, sy = y * g_h / h;
        uint32_t c = g_px[sy * g_w + sx], a = c >> 24;
        if (a < 255) { uint32_t r = ((c >> 16 & 255) * a + 255 * (255 - a)) / 255, g = ((c >> 8 & 255) * a + 255 * (255 - a)) / 255, b = ((c & 255) * a + 255 * (255 - a)) / 255; c = r << 16 | g << 8 | b; }
        o[y * w + x] = c & 0x00FFFFFFu;
    }
    *ow = w; *oh = h; return o;
}
static uint8_t *encode(uint32_t *len)
{
    int w, h; uint32_t *px = scaled(&w, &h); if (!px) return 0;
    uint8_t *out = 0; *len = 0;
    if (g_fmt == 1) {
        uint32_t stride = (uint32_t)(((w * 3) + 3) & ~3), total = 54 + (uint32_t)h * stride;
        out = (uint8_t *)kmalloc(total);
        if (out) {
            for (uint32_t i = 0; i < 54; i++) out[i] = 0;
            out[0] = 'B'; out[1] = 'M'; out[2] = (uint8_t)total; out[3] = (uint8_t)(total >> 8); out[4] = (uint8_t)(total >> 16); out[5] = (uint8_t)(total >> 24);
            out[10] = 54; out[14] = 40; out[18] = (uint8_t)w; out[19] = (uint8_t)(w >> 8); out[22] = (uint8_t)h; out[23] = (uint8_t)(h >> 8); out[26] = 1; out[28] = 24;
            for (int y = 0; y < h; y++) { uint8_t *row = out + 54 + (uint32_t)y * stride; const uint32_t *s = px + (h - 1 - y) * w;
                for (int x = 0; x < w; x++) { row[3 * x] = (uint8_t)s[x]; row[3 * x + 1] = (uint8_t)(s[x] >> 8); row[3 * x + 2] = (uint8_t)(s[x] >> 16); }
                for (uint32_t x = (uint32_t)w * 3; x < stride; x++) row[x] = 0; }
            *len = total;
        }
    } else {
        size_t cap = (size_t)w * h * 3 + (size_t)h + 1024;
        out = (uint8_t *)kmalloc(cap); void *work = kmalloc(png_work_size(w, h));
        if (out && work) *len = (uint32_t)png_encode_rgb(px, w, h, out, cap, work);
        if (work) kfree(work);
        if (out && !*len) { kfree(out); out = 0; }
    }
    kfree(px);
    return out;
}
static void estimate(void)
{
    int key = g_fmt * 10 + g_scale_i;
    if (key == g_est_key) return;
    g_est_key = key;
    uint32_t n = 0; uint8_t *b = encode(&n);
    g_est = b ? (int)n : -1;
    if (b) kfree(b);
}
static int to_big(void) { return 0; }                 /* SOMA_363_ACCESO: ya no se guarda en el almacen BIGFS (no se ve desde Archivos) */

static void do_save(void)
{
    { int bad = !g_name[0]; for (int i = 0; g_name[i]; i++) if (g_name[i] == '/') bad = 1;
      if (bad) { s_cpy(g_msg, "Escribe un nombre de archivo (sin /)", sizeof g_msg); g_msg_ok = 0; g_focus = 0; return; }
      int n = s_len(g_name), dot = -1; for (int i = n - 1; i > 0; i--) if (g_name[i] == '.') { dot = i; break; }
      if (dot < 0 || g_name[dot + 1] == 0) { if (dot > 0) g_name[dot] = 0; s_cat(g_name, g_fmt ? ".bmp" : ".png", sizeof g_name); }
      g_sa[0] = g_sb[0] = g_cur[0] = s_len(g_name); }
    char path[200]; s_cpy(path, to_big() ? g_dir + 4 : g_dir, sizeof path);
    int n = s_len(path); if (!n || path[0] != '/') { s_cpy(g_msg, "La carpeta debe empezar por /", sizeof g_msg); g_msg_ok = 0; return; }
    if (!usuario_ruta_escribible(path) || (!acceso_ruta_ok(path) && acceso_usuario())) {   /* SOMA_363_ACCESO */
        s_cpy(g_msg, acceso_usuario() ? "Solo se puede guardar en Mi unidad (/usuario) o en un USB" : "Sin unidad del usuario: solo en /tmp", sizeof g_msg); g_msg_ok = 0; return;
    }
    if (path[n - 1] != '/') s_cat(path, "/", sizeof path);
    s_cat(path, g_name, sizeof path);
    uint32_t len = 0; uint8_t *b = encode(&len);
    if (!b) { s_cpy(g_msg, "Sin memoria para convertir la imagen", sizeof g_msg); g_msg_ok = 0; return; }
    int ok;
    if (to_big()) {
        int err = 0; struct bigfs *fs = bigfs_sys(&err); uint32_t ino;
        ok = 0;
        if (fs) {
            char dir[200]; s_cpy(dir, g_dir + 4, sizeof dir);
            for (int i = 1; dir[i]; i++) if (dir[i] == '/') { dir[i] = 0; (void)bigfs_mkdir(fs, dir); dir[i] = '/'; }
            (void)bigfs_mkdir(fs, dir);
            (void)bigfs_unlink(fs, path);
            if (bigfs_create(fs, path, &ino) >= 0 && bigfs_write(fs, ino, 0, b, len) == (int64_t)len) ok = 1;
        }
    } else {
        if (len > FS_FILE_MAX) { kfree(b); s_cpy(g_msg, "Demasiado grande (maximo 32 MiB por archivo)", sizeof g_msg); g_msg_ok = 0; return; }
        ok = vfs_write(path, (const char *)b, len) >= 0;
    }
    kfree(b);
    if (!ok) { s_cpy(g_msg, "No se pudo guardar en ", sizeof g_msg); s_cat(g_msg, path, sizeof g_msg); g_msg_ok = 0; return; }
    char t[200] = "Imagen guardada en "; s_cat(t, to_big() ? "BIGFS " : "", sizeof t); s_cat(t, path, sizeof t);
    clip_toast(t);
    g_on = 0;
    if (g_px) { kfree(g_px); g_px = 0; }
}

/* ---------------------------------------------------------------- dibujo */
struct dl { int x, y, w, h, th_x, th_y, th_w, th_h, fx, fw, name_y, dir_y, chips_y, fmt_y, sc_y, info_y, btn_y, save_x, cancel_x; };
static void lay(struct dl *L, int wx, int wy, int ww, int wh)
{
    /* tamano fijo, centrado sobre la ventana pero sin salirse de la pantalla (cabe aunque la ventana sea pequena) */
    int sw = (int)fb_get_width(), sh = (int)fb_get_height();
    L->w = 660 < sw - 16 ? 660 : sw - 16; L->h = 450 < sh - 16 ? 450 : sh - 16;
    L->x = wx + (ww - L->w) / 2; L->y = wy + (wh - L->h) / 2;
    if (L->x + L->w > sw - 8) L->x = sw - 8 - L->w;
    if (L->y + L->h > sh - 8) L->y = sh - 8 - L->h;
    if (L->x < 8) L->x = 8;
    if (L->y < 8) L->y = 8;
    L->th_x = L->x + 24; L->th_y = L->y + 64; L->th_w = 180; L->th_h = 150;
    L->fx = L->th_x + L->th_w + 24; L->fw = L->x + L->w - 24 - L->fx;
    L->name_y = L->y + 82; L->dir_y = L->y + 146; L->chips_y = L->y + 184; L->fmt_y = L->y + 244; L->sc_y = L->y + 300;
    L->info_y = L->sc_y + 48; L->btn_y = L->y + L->h - 56;
    L->save_x = L->x + L->w - 24 - 120; L->cancel_x = L->save_x - 12 - 110;
}
static int tw_n(const char *s, int n, int px) { char t[128]; if (n > 127) n = 127; for (int i = 0; i < n; i++) t[i] = s[i]; t[n] = 0; return tw(t, px, 0); }
static void field2(int x, int y, int w, const char *label, const char *val, int focus, int fi)
{
    tx(x, y - 22, 18, label, 11, KC_FAINT, 1);
    fb_fill_rounded_rect(x, y, w, 34, 10, KC_FIELD);
    fb_stroke_rrect_aa(x, y, w, 34, 10, focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
    fb_set_clip(x + 10, y, w - 20, 34);
    int cx = tw_n(val, g_cur[fi], 13), sh = 0;                 /* desplaza el texto si el cursor se sale */
    if (cx > w - 30) sh = cx - (w - 30);
    if (focus && g_sb[fi] > g_sa[fi]) { int a = tw_n(val, g_sa[fi], 13), b = tw_n(val, g_sb[fi], 13);
        fb_fill_rect((uint32_t)(x + 12 + a - sh), (uint32_t)(y + 7), (uint32_t)(b - a), 20, 0xFF1D4E89u); }
    tx(x + 12 - sh, y, 34, val, 13, KC_TXT, 0);
    if (focus && ((timer_get_uptime_ms() / 530) & 1) == 0) fb_fill_rect((uint32_t)(x + 12 + cx - sh), (uint32_t)(y + 9), 2, 16, KC_ACC);
    fb_reset_clip();
}
static void seg(int x, int y, int w, int n, int sel, const char *const *lab)
{
    fb_fill_rounded_rect(x, y, w, 30, 9, 0x000C1119);
    int bw = (w - 4) / n;
    for (int i = 0; i < n; i++) {
        if (i == sel) fb_fill_rounded_rect(x + 2 + i * bw, y + 2, bw, 26, 7, 0x00283449);
        tx(x + 2 + i * bw + (bw - tw(lab[i], 12, i == sel)) / 2, y + 2, 26, lab[i], 12, i == sel ? 0x00FFFFFF : KC_DIM, i == sel);
    }
}
void savedlg_render(int wx, int wy, int ww, int wh)
{
    if (!g_on) return;
    struct dl L; lay(&L, wx, wy, ww, wh);
    fb_fill_rect_alpha(wx, wy, ww, wh, 0x90000000u);                       /* velo sobre la ventana */
    fb_draw_shadow(L.x, L.y, L.w, L.h, 14, 30, 10, 0xC0000000u);
    fb_fill_rrect_aa(L.x, L.y, L.w, L.h, 14, 0xFF000000u | KC_CARD);
    fb_stroke_rrect_aa(L.x, L.y, L.w, L.h, 14, 0xFF263142u);
    fb_tr_exclude(L.x, L.y, L.w, L.h);                                    /* opaco aunque la ventana sea translucida */
    tx(L.x + 24, L.y + 16, 30, "Guardar imagen como", 17, KC_TXT, 1);
    /* cerrar */
    { int cx = L.x + L.w - 34, cy = L.y + 31; fb_line_aa((cx - 6) * 16, (cy - 6) * 16, (cx + 6) * 16, (cy + 6) * 16, 24, 0xFF8592A8u); fb_line_aa((cx + 6) * 16, (cy - 6) * 16, (cx - 6) * 16, (cy + 6) * 16, 24, 0xFF8592A8u); }
    /* miniatura */
    fb_fill_rounded_rect(L.th_x, L.th_y, L.th_w, L.th_h, 10, 0x00090D15);
    { int tw_ = L.th_w - 16, th_ = L.th_h - 16, dw = tw_, dh = g_h * tw_ / (g_w ? g_w : 1);
      if (dh > th_) { dh = th_; dw = g_w * th_ / (g_h ? g_h : 1); }
      if (dw < 1) dw = 1;
      if (dh < 1) dh = 1;
      fb_blit_scale(L.th_x + (L.th_w - dw) / 2, L.th_y + (L.th_h - dh) / 2, dw, dh, g_px, g_w, g_h, g_w); }
    char b[120] = ""; s_num(b, sizeof b, g_w); s_cat(b, " x ", sizeof b); s_num(b, sizeof b, g_h); s_cat(b, " px", sizeof b);
    tx(L.th_x, L.th_y + L.th_h + 6, 20, b, 12, KC_DIM, 0);
    /* campos */
    field2(L.fx, L.name_y, L.fw, "NOMBRE", g_name, g_focus == 0, 0);
    field2(L.fx, L.dir_y, L.fw, "CARPETA", g_dir, g_focus == 1, 1);
    { int x = L.fx; for (int i = 0; i < g_nchips; i++) {
        const char *c = g_chips[i]; int cw = tw(c, 11, 0) + 22; if (x + cw > L.fx + L.fw) break;
        int sel = 1; { const char *a = c, *d = g_dir; while (*a && *a == *d) { a++; d++; } sel = !*a && !*d; }
        fb_fill_rounded_rect(x, L.chips_y, cw, 24, 12, sel ? 0x001A3558 : 0x00182133);
        tx(x + 11, L.chips_y, 24, c, 11, sel ? 0x00FFFFFF : (starts(c, "big:") ? 0x00FBBF24 : KC_DIM), 0);
        x += cw + 6; } }
    tx(L.fx, L.fmt_y - 22, 18, "FORMATO", 11, KC_FAINT, 1);
    static const char *const F[2] = { "PNG", "BMP" };
    seg(L.fx, L.fmt_y, L.fw, 2, g_fmt, F);
    tx(L.fx, L.sc_y - 22, 18, "TAMANO", 11, KC_FAINT, 1);
    static const char *const S[4] = { "100 %", "75 %", "50 %", "25 %" };
    seg(L.fx, L.sc_y, L.fw, 4, g_scale_i, S);
    /* peso estimado */
    estimate();
    b[0] = 0;
    int fits = g_est >= 0 && (uint32_t)g_est <= FS_FILE_MAX;
    if (g_est < 0) s_cat(b, "No se pudo calcular el tamano", sizeof b);
    else {
        s_cat(b, "Ocupara ", sizeof b); s_num(b, sizeof b, (g_est + 1023) / 1024); s_cat(b, " KiB (", sizeof b);
        s_num(b, sizeof b, g_w * SC[g_scale_i] / 100); s_cat(b, " x ", sizeof b); s_num(b, sizeof b, g_h * SC[g_scale_i] / 100); s_cat(b, " px)  -  ", sizeof b);
        { int pers = vfs_big_persist();
          s_cat(b, to_big() ? "almacen BIGFS" : !fits ? "demasiado grande (max. 32 MiB)" : (g_est < FS_DATA_MAX || pers > 0) ? "se guarda en disco" : pers < 0 ? "en memoria (sin disco)" : "en memoria (BIGFS sin formatear)", sizeof b); }
    }
    fb_fill_rrect_aa(L.x + 24, L.info_y + 8, 8, 8, 4, fits ? 0xFF34D399u : 0xFFF87171u);
    fb_set_clip(L.x + 40, L.info_y, L.w - 64, 24);
    tx(L.x + 40, L.info_y, 24, b, 12, fits ? KC_TXT : KC_BAD, 0);
    fb_reset_clip();
    if (g_msg[0]) { fb_set_clip(L.x + 24, L.info_y + 24, L.w - 48, 22); tx(L.x + 24, L.info_y + 24, 22, g_msg, 11, g_msg_ok ? KC_OK : KC_BAD, 0); fb_reset_clip(); }
    /* botones */
    fb_fill_rounded_rect(L.cancel_x, L.btn_y, 110, 34, 17, 0x00182133);
    fb_stroke_rrect_aa(L.cancel_x, L.btn_y, 110, 34, 17, 0xFF263142u);
    tx(L.cancel_x + (110 - tw("Cancelar", 13, 1)) / 2, L.btn_y, 34, "Cancelar", 13, KC_TXT, 1);
    fb_fill_rounded_rect(L.save_x, L.btn_y, 120, 34, 17, fits ? KC_PRIMARY : 0x00334155);
    tx(L.save_x + (120 - tw("Guardar", 13, 1)) / 2, L.btn_y, 34, "Guardar", 13, 0x00FFFFFF, 1);
    tx(L.x + 24, L.btn_y, 34, "Intro guarda  -  Tab  -  Esc", 11, KC_FAINT, 0);
}

static int in(int mx, int my, int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; }
int savedlg_click(int32_t mx, int32_t my, int wx, int wy, int ww, int wh)
{
    if (!g_on) return 0;
    struct dl L; lay(&L, wx, wy, ww, wh);
    if (in(mx, my, L.x + L.w - 50, L.y + 14, 34, 34) || in(mx, my, L.cancel_x, L.btn_y, 110, 34)) { g_on = 0; return 1; }
    if (in(mx, my, L.save_x, L.btn_y, 120, 34)) { do_save(); return 1; }
    for (int fi = 0; fi < 2; fi++) {
        int fy = fi ? L.dir_y : L.name_y;
        if (!in(mx, my, L.fx, fy, L.fw, 34)) continue;
        const char *v = fi ? g_dir : g_name; int n = s_len(v), best = n, bd = 1 << 30;
        int cx = tw_n(v, g_cur[fi], 13), sh = cx > L.fw - 30 ? cx - (L.fw - 30) : 0, rel = mx - (L.fx + 12) + sh;
        for (int i = 0; i <= n; i++) { if (i < n && ((uint8_t)v[i] & 0xC0) == 0x80) continue; int d = tw_n(v, i, 13) - rel; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
        g_focus = fi; g_cur[fi] = best; g_sa[fi] = g_sb[fi] = best; g_u8n = 0; return 1;
    }
    { int x = L.fx; for (int i = 0; i < g_nchips; i++) { int cw = tw(g_chips[i], 11, 0) + 22; if (x + cw > L.fx + L.fw) break;
        if (in(mx, my, x, L.chips_y, cw, 24)) { s_cpy(g_dir, g_chips[i], sizeof g_dir); g_cur[1] = g_sa[1] = g_sb[1] = s_len(g_dir); g_msg[0] = 0; return 1; } x += cw + 6; } }
    if (in(mx, my, L.fx, L.fmt_y, L.fw, 30)) { g_fmt = (mx - L.fx) * 2 / L.fw; if (g_fmt > 1) g_fmt = 1; fix_ext(); g_cur[0] = g_sa[0] = g_sb[0] = s_len(g_name); g_msg[0] = 0; return 1; }
    if (in(mx, my, L.fx, L.sc_y, L.fw, 30)) { g_scale_i = (mx - L.fx) * 4 / L.fw; if (g_scale_i > 3) g_scale_i = 3; g_msg[0] = 0; return 1; }
    return 1;                                                             /* modal: nada pasa a la pagina */
}
static void f_del(char *f, int a, int b) { int n = s_len(f); if (b <= a) return; for (int i = a; i <= n - (b - a); i++) f[i] = f[i + (b - a)]; }
static int prev_cp(const char *f, int i) { if (i > 0) { i--; while (i > 0 && ((uint8_t)f[i] & 0xC0) == 0x80) i--; } return i; }
static int next_cp(const char *f, int i) { int n = s_len(f); if (i < n) { i++; while (i < n && ((uint8_t)f[i] & 0xC0) == 0x80) i++; } return i; }
int savedlg_key(char c)
{
    if (!g_on) return 0;
    int k = (uint8_t)c, fi = g_focus ? 1 : 0;
    char *f = fi ? g_dir : g_name; int max = fi ? (int)sizeof g_dir : (int)sizeof g_name;
    int *cur = &g_cur[fi], *sa = &g_sa[fi], *sb = &g_sb[fi];
    int n = s_len(f);
    if (*cur > n) *cur = n;
    if (*sb > n) *sb = n;
    if (*sa > *sb) *sa = *sb;
    int sel = *sb > *sa;
    if (k == 27) { g_on = 0; return 1; }
    if (k == '\t') { g_focus = !g_focus; g_u8n = 0; return 1; }
    if (k == '\r' || k == '\n') { do_save(); return 1; }
    g_msg[0] = 0;
    if (k == 8 || k == 127) { if (sel) { f_del(f, *sa, *sb); *cur = *sa; } else if (*cur > 0) { int a = prev_cp(f, *cur); f_del(f, a, *cur); *cur = a; } *sa = *sb = *cur; return 1; }
    if (k == 4) { if (sel) { f_del(f, *sa, *sb); *cur = *sa; } else f_del(f, *cur, next_cp(f, *cur)); *sa = *sb = *cur; return 1; }   /* Supr */
    if (k == 2) { *cur = sel ? *sa : prev_cp(f, *cur); *sa = *sb = *cur; return 1; }                                           /* flecha izq. */
    if (k == 6) { *cur = sel ? *sb : next_cp(f, *cur); *sa = *sb = *cur; return 1; }                                           /* flecha der. */
    if (k == 1) { *cur = 0; *sa = *sb = 0; return 1; }                                                                           /* Inicio */
    if (k == 5) { *cur = n; *sa = *sb = n; return 1; }                                                                           /* Fin */
    char u[5]; int un = 0;
    if (k >= 0x80) {                                                                                                             /* tildes y enes en UTF-8 */
        if (g_u8n == 0) { g_u8need = k >= 0xF0 ? 4 : k >= 0xE0 ? 3 : k >= 0xC0 ? 2 : 0; if (!g_u8need) return 1; }
        else if ((k & 0xC0) != 0x80) { g_u8n = 0; return 1; }
        g_u8[g_u8n++] = (uint8_t)k; if (g_u8n < g_u8need) return 1;
        for (int i = 0; i < g_u8n; i++) u[i] = (char)g_u8[i];
        un = g_u8n; g_u8n = 0;
    } else if (k >= 32) { u[0] = c; un = 1; }
    else return 1;
    if (fi == 0 && un == 1 && (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')) return 1;
    if (sel) { f_del(f, *sa, *sb); *cur = *sa; n = s_len(f); }
    if (n + un >= max) return 1;
    for (int i = n; i >= *cur; i--) f[i + un] = f[i];
    for (int i = 0; i < un; i++) f[*cur + i] = u[i];
    *cur += un; *sa = *sb = *cur;
    return 1;
}
