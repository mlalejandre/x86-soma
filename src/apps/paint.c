/* PARCHE 083: Paint rediseñado con la estetica del explorador de archivos (tema oscuro, texto suavizado, iconos vectoriales,
 * esquinas redondeadas con antialias) y mejorado:
 *  - lienzo de 320x240 por defecto (480x360 y 640x480 a elegir) en vez de 80x60; zoom (ajustar, 25 %..800 %) y herramienta mano para moverse;
 *  - herramientas: lapiz, pincel redondo, aerografo, goma, cubo, cuentagotas, linea, rectangulo, elipse (contorno y relleno) y mano,
 *    con grosor de 1 a 32 y vista previa exacta de las figuras mientras se arrastra;
 *  - color: selector de saturacion/brillo + barra de tono, paleta de 24 colores, colores recientes y codigo hexadecimal;
 *  - deshacer y rehacer (12 pasos), atajos de teclado;
 *  - guarda en PNG comprimido (un dibujo ocupa pocos KiB y cabe en RamFS) o BMP; abre PNG, BMP, JPEG y GIF.
 * El portapapeles (SOMA_PARCHE_PORTAPAPELES) sigue funcionando con el lienzo nuevo. */
#include "acceso.h"      /* SOMA_363_ACCESO */
#include "paint.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "fs.h"
#include "mem.h"
#include "console.h"
#include "clipboard.h"
#include "web_font.h"
#include "web_img.h"
#include "png_enc.h"
#include "paint_svg.h"   /* SOMA_292_PAINT_IA */
#include "llm.h"
#include "thread.h"

uint64_t timer_get_uptime_ms(void);

/* ---------------------------------------------------------------- tema (el del explorador) */
#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_FIELD   0x00090D15
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_HOVER   0x00182133
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB
#define KC_AREA    0x00080B12
#define KC_OK      0x0034D399

#define PMAX_W 640
#define PMAX_H 480
#define UNDO_N 12

enum { T_PENCIL = 0, T_BRUSH, T_SPRAY, T_ERASER, T_BUCKET, T_PICKER, T_LINE, T_RECT, T_RECTF, T_ELL, T_ELLF, T_HAND, T_N };
static const char *const tool_name[T_N] = { "Lapiz", "Pincel", "Aerografo", "Goma", "Cubo de pintura", "Cuentagotas", "Linea",
                                            "Rectangulo", "Rectangulo relleno", "Elipse", "Elipse rellena", "Mano" };
static const char tool_key[T_N] = { 'p', 'b', 'a', 'e', 'g', 'i', 'l', 'r', 'f', 'o', 'u', 'h' };

static const uint32_t PALETTE[24] = {
    0x000F172A, 0x00475569, 0x0094A3B8, 0x00E2E8F0, 0x00FFFFFF, 0x007C2D12,
    0x00DC2626, 0x00F97316, 0x00F59E0B, 0x00FACC15, 0x0084CC16, 0x0016A34A,
    0x0010B981, 0x0014B8A6, 0x0006B6D4, 0x000EA5E9, 0x003B82F6, 0x001E3A8A,
    0x006366F1, 0x008B5CF6, 0x00A855F7, 0x00D946EF, 0x00EC4899, 0x00F43F5E
};

/* ---------------------------------------------------------------- estado */
static uint32_t *cv;                  /* lienzo (0x00RRGGBB) */
static uint32_t *pv;                  /* copia para la vista previa de figuras */
static int cw = 320, ch = 240;
static uint32_t *undo_buf[UNDO_N]; static int undo_w[UNDO_N], undo_h[UNDO_N];
static int u_top, u_count, r_count;   /* pila circular: u_count pasos de deshacer, r_count de rehacer encima */
static int tool = T_BRUSH, prev_tool = T_BRUSH, bsize = 4;
static uint32_t color = 0x003B82F6;
static uint32_t recent[8]; static int n_recent;
static int hue = 1024, sat = 200, val = 246;   /* tono 0..1535, saturacion y brillo 0..255 */
static int zoom = 0;                  /* 0 = ajustar; si no, porcentaje */
static int pan_x, pan_y;
static char path[96] = "/usuario/Im\303\241genes/Dibujos/dibujo.png";   /* SOMA_363_ACCESO */
static void s_cat(char *d, const char *s, int max);
static void ruta_nueva(void) { acceso_carpeta(ACC_DIBUJOS, path, sizeof path); s_cat(path, "/dibujo.png", sizeof path); }
static char msg[96] = ""; static uint64_t msg_until; static int msg_ok = 1;
static int cur_x = -1, cur_y = -1;
static uint32_t rng = 0x12345678u;
/* arrastres */
enum { D_NONE = 0, D_PAINT, D_SHAPE, D_HAND, D_SV, D_HUE, D_SIZE };
static int drag, sx0, sy0, sx1, sy1, last_x = -1, last_y = -1, hand_mx, hand_my;
static int hover_btn = -1;
static int g_last_z = 100;            /* zoom efectivo del ultimo dibujo (para + y - del teclado) */
static int p_named;                   /* SOMA_215_PAINT: 1 = 'path' es un archivo abierto o guardado */
static int p_dirty;                   /* SOMA_219_AVISO: 1 = hay cambios sin guardar */
static int ia_abierto, ia_len;        /* SOMA_292_PAINT_IA: el campo "que dibujo" de esta ventana */
static char ia_txt[200];

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d); int j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; if (v < 0) { s_cat(d, "-", max); v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static void say(const char *m, int ok) { s_cpy(msg, m, sizeof msg); msg_ok = ok; msg_until = timer_get_uptime_ms() + 4000; }

/* ---------------------------------------------------------------- texto suavizado (como el explorador) */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { wf_draw(x, top + (h + (px * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}

/* ---------------------------------------------------------------- color */
static uint32_t hsv2rgb(int h, int s, int v)
{
    int r, g, b, reg = (h / 256) % 6, f = h % 256;
    int p = v * (255 - s) / 255, q = v * (255 - s * f / 255) / 255, t = v * (255 - s * (255 - f) / 255) / 255;
    switch (reg) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                   case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; break; }
    return (uint32_t)(r << 16 | g << 8 | b);
}
static void rgb2hsv(uint32_t c)
{
    int r = (int)(c >> 16) & 255, g = (int)(c >> 8) & 255, b = (int)c & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
    val = mx; sat = mx ? d * 255 / mx : 0;
    if (!d) return;                                           /* gris: se conserva el tono anterior */
    int h;
    if (mx == r) h = (g - b) * 256 / d; else if (mx == g) h = 512 + (b - r) * 256 / d; else h = 1024 + (r - g) * 256 / d;
    if (h < 0) h += 1536;
    hue = h % 1536;
}
static void set_color(uint32_t c, int sync) { color = c & 0x00FFFFFFu; if (sync) rgb2hsv(color); }
static void push_recent(uint32_t c)
{
    int i, j; for (i = 0; i < n_recent; i++) if (recent[i] == c) break;
    if (i == n_recent && n_recent < 8) n_recent++;
    if (i >= 8) i = 7;
    for (j = i; j > 0; j--) recent[j] = recent[j - 1];
    recent[0] = c;
}
static void hexcol(char *o, uint32_t c) { static const char *h = "0123456789ABCDEF"; o[0] = '#'; for (int i = 0; i < 6; i++) o[1 + i] = h[(c >> (20 - 4 * i)) & 15]; o[7] = 0; }

/* ---------------------------------------------------------------- lienzo, deshacer */
static void canvas_alloc(int w, int h)
{
    if (cv) kfree(cv);
    if (pv) kfree(pv);
    cw = w; ch = h;
    cv = (uint32_t *)kmalloc((size_t)w * h * 4);
    pv = (uint32_t *)kmalloc((size_t)w * h * 4);
    if (cv) for (int i = 0; i < w * h; i++) cv[i] = 0x00FFFFFF;
}
static void undo_clear(void) { u_count = 0; r_count = 0; }
static void undo_push(void)
{
    if (!cv) return;
    int slot = u_top;
    if (undo_buf[slot] && (undo_w[slot] * undo_h[slot] != cw * ch)) { kfree(undo_buf[slot]); undo_buf[slot] = 0; }
    if (!undo_buf[slot]) undo_buf[slot] = (uint32_t *)kmalloc((size_t)cw * ch * 4);
    if (!undo_buf[slot]) return;
    memcpy(undo_buf[slot], cv, (size_t)cw * ch * 4); undo_w[slot] = cw; undo_h[slot] = ch;
    u_top = (u_top + 1) % UNDO_N;
    if (u_count < UNDO_N) u_count++;
    r_count = 0;
    p_dirty = 1;                                                  /* SOMA_219_AVISO: cada cambio guarda antes un paso de deshacer */
}
static void swap_with(int slot)
{
    if (!undo_buf[slot]) return;
    if (undo_w[slot] != cw || undo_h[slot] != ch) {                 /* el paso era de otro tamano de lienzo */
        uint32_t *old = cv; int ow = cw, oh = ch;
        cv = undo_buf[slot]; cw = undo_w[slot]; ch = undo_h[slot];
        undo_buf[slot] = old; undo_w[slot] = ow; undo_h[slot] = oh;
        if (pv) kfree(pv);
        pv = (uint32_t *)kmalloc((size_t)cw * ch * 4);
        return;
    }
    for (int i = 0; i < cw * ch; i++) { uint32_t t = cv[i]; cv[i] = undo_buf[slot][i]; undo_buf[slot][i] = t; }
}
static void do_undo(void)
{
    if (!u_count) { say("No hay nada que deshacer", 0); return; }
    u_top = (u_top + UNDO_N - 1) % UNDO_N; u_count--; r_count++;
    swap_with(u_top); p_dirty = 1; say("Deshecho", 1);
}
static void do_redo(void)
{
    if (!r_count) { say("No hay nada que rehacer", 0); return; }
    swap_with(u_top); u_top = (u_top + 1) % UNDO_N; u_count++; r_count--; p_dirty = 1; say("Rehecho", 1);
}

void paint_init(void)
{
    if (!cv || cw != 320 || ch != 240) canvas_alloc(320, 240);
    else for (int i = 0; i < cw * ch; i++) cv[i] = 0x00FFFFFF;
    undo_clear();
    tool = T_BRUSH; bsize = 4; zoom = 0; pan_x = pan_y = 0; drag = D_NONE;
    set_color(0x003B82F6, 1);
    if (!n_recent) { recent[0] = 0x000F172A; recent[1] = 0x00DC2626; recent[2] = 0x0016A34A; n_recent = 3; }
    ruta_nueva(); p_named = 0; p_dirty = 0;
    say("Lienzo nuevo de 320 x 240", 1);
}

void paint_ensure(void) { if (!cv) paint_init(); }

/* ---------------------------------------------------------------- dibujo sobre un bufer */
static void put_px(uint32_t *b, int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < cw && y < ch) b[y * cw + x] = c; }
static void disc(uint32_t *b, int cx, int cy, int r, uint32_t c)
{
    if (r <= 0) { put_px(b, cx, cy, c); return; }
    int rr = r * r + r;                                           /* disco algo mas redondo en tamanos pequenos */
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= rr) put_px(b, cx + x, cy + y, c);
}
static void stroke(uint32_t *b, int x0, int y0, int x1, int y1, int r, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx - dy;
    for (int guard = 0; guard < 4096; guard++) {
        disc(b, x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 > -dy) { e -= dy; x0 += sx; }
        if (e2 < dx) { e += dx; y0 += sy; }
    }
}
static void spray(uint32_t *b, int cx, int cy, int r, uint32_t c)
{
    int R = r * 2 + 3, n = R * 2;
    for (int i = 0; i < n; i++) {
        rng = rng * 1103515245u + 12345u; int x = (int)((rng >> 8) % (uint32_t)(2 * R + 1)) - R;
        rng = rng * 1103515245u + 12345u; int y = (int)((rng >> 8) % (uint32_t)(2 * R + 1)) - R;
        if (x * x + y * y <= R * R) put_px(b, cx + x, cy + y, c);
    }
}
static void rect_shape(uint32_t *b, int x0, int y0, int x1, int y1, int t, int fill, uint32_t c)
{
    if (x0 > x1) { int k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { int k = y0; y0 = y1; y1 = k; }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
        if (fill || x < x0 + t || x > x1 - t || y < y0 + t || y > y1 - t) put_px(b, x, y, c);
}
static int in_ell(int64_t x2, int64_t y2, int64_t A, int64_t B) { return A > 0 && B > 0 && x2 * x2 * B * B + y2 * y2 * A * A <= A * A * B * B; }
static void ell_shape(uint32_t *b, int x0, int y0, int x1, int y1, int t, int fill, uint32_t c)
{
    if (x0 > x1) { int k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { int k = y0; y0 = y1; y1 = k; }
    int64_t A = x1 - x0 + 1, B = y1 - y0 + 1, Ai = A - 2 * t, Bi = B - 2 * t;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        int64_t X = 2 * (int64_t)x + 1 - (x0 + x1 + 1), Y = 2 * (int64_t)y + 1 - (y0 + y1 + 1);
        if (!in_ell(X, Y, A, B)) continue;
        if (fill || !in_ell(X, Y, Ai, Bi)) put_px(b, x, y, c);
    }
}
static void flood(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;
    uint32_t t = cv[y * cw + x];
    if (t == c) return;
    int cap = cw * ch; int32_t *st = (int32_t *)kmalloc((size_t)cap * 4);
    if (!st) return;
    int sp = 0; st[sp++] = y * cw + x;
    while (sp) {
        int p = st[--sp], py = p / cw, px = p % cw;
        if (cv[p] != t) continue;
        int l = px, r = px;
        while (l > 0 && cv[py * cw + l - 1] == t) l--;
        while (r < cw - 1 && cv[py * cw + r + 1] == t) r++;
        for (int k = l; k <= r; k++) {
            cv[py * cw + k] = c;
            if (py > 0 && cv[(py - 1) * cw + k] == t && sp < cap) st[sp++] = (py - 1) * cw + k;
            if (py < ch - 1 && cv[(py + 1) * cw + k] == t && sp < cap) st[sp++] = (py + 1) * cw + k;
        }
    }
    kfree(st);
}
static void shape_into(uint32_t *b, int x0, int y0, int x1, int y1)
{
    int r = (bsize - 1) / 2, t = bsize;
    switch (tool) {
    case T_LINE: stroke(b, x0, y0, x1, y1, r, color); break;
    case T_RECT: rect_shape(b, x0, y0, x1, y1, t, 0, color); break;
    case T_RECTF: rect_shape(b, x0, y0, x1, y1, t, 1, color); break;
    case T_ELL: ell_shape(b, x0, y0, x1, y1, t, 0, color); break;
    case T_ELLF: ell_shape(b, x0, y0, x1, y1, t, 1, color); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- archivos */
static int ends_with(const char *s, const char *e) { int a = s_len(s), b = s_len(e); if (a < b) return 0; for (int i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return 0; } return 1; }
static int save_bmp(void)
{
    uint32_t stride = (uint32_t)(((cw * 3) + 3) & ~3), total = 54 + (uint32_t)ch * stride;
    if (total > FS_FILE_MAX) return -3;
    uint8_t *b = (uint8_t *)kmalloc(total); if (!b) return -1;
    memset(b, 0, total);
    b[0] = 'B'; b[1] = 'M'; b[2] = (uint8_t)total; b[3] = (uint8_t)(total >> 8); b[4] = (uint8_t)(total >> 16);
    b[10] = 54; b[14] = 40; b[18] = (uint8_t)cw; b[19] = (uint8_t)(cw >> 8); b[22] = (uint8_t)ch; b[23] = (uint8_t)(ch >> 8); b[26] = 1; b[28] = 24;
    for (int y = 0; y < ch; y++) { uint8_t *row = b + 54 + (uint32_t)y * stride; const uint32_t *s = cv + (ch - 1 - y) * cw;
        for (int x = 0; x < cw; x++) { row[3 * x] = (uint8_t)s[x]; row[3 * x + 1] = (uint8_t)(s[x] >> 8); row[3 * x + 2] = (uint8_t)(s[x] >> 16); } }
    int r = vfs_write(path, (const char *)b, total);
    kfree(b);
    return r >= 0 ? (int)total : -1;
}
static int save_png(void)
{
    size_t cap = (size_t)cw * ch * 3 + (size_t)ch * 8 + 4096;    /* PARCHE 087: sin tope de 16 KiB (peor caso: sin comprimir) */
    uint8_t *o = (uint8_t *)kmalloc(cap); void *w = kmalloc(png_work_size(cw, ch));
    if (!o || !w) { if (o) kfree(o); if (w) kfree(w); return -1; }
    size_t n = png_encode_rgb(cv, cw, ch, o, cap, w);
    int r = -1;
    if (n == 0 || n > FS_FILE_MAX) r = -3;
    else r = vfs_write(path, (const char *)o, (uint32_t)n) >= 0 ? (int)n : -1;
    kfree(o); kfree(w);
    return r;
}
static void do_save(void)
{
    if (!cv) return;
    int bmp = ends_with(path, ".bmp");
    int r = bmp ? save_bmp() : save_png();
    if (r == -3 && bmp) {                                         /* el BMP no cabe en RamFS: se guarda como PNG al lado */
        int n = s_len(path); path[n - 3] = 'p'; path[n - 2] = 'n'; path[n - 1] = 'g';
        r = save_png();
        if (r > 0) { p_dirty = 0; char m[96] = "BMP demasiado grande: guardado como "; s_cat(m, path, sizeof m); say(m, 1); return; }
    }
    if (r == -3) { say("No se pudo comprimir la imagen", 0); return; }
    if (r < 0) { say("Error al guardar (sin memoria?)", 0); return; }
    p_named = 1; p_dirty = 0;
    char m[96] = "Guardado: "; s_cat(m, path, sizeof m); s_cat(m, " (", sizeof m); s_num(m, sizeof m, (r + 1023) / 1024); s_cat(m, " KiB)", sizeof m);
    say(m, 1);
}
static int load_file(const char *p)
{
    uint32_t un = 0; uint8_t *raw = (uint8_t *)vfs_read_alloc(p, &un);   /* PARCHE 087 */
    if (!raw) return -1;
    int n = (int)un;
    if (n <= 8) { kfree(raw); return -1; }
    struct wimg im;
    if (wi_decode(raw, (uint32_t)n, &im) != 0 || im.w <= 0 || im.h <= 0) { kfree(raw); return -2; }
    int w = im.w, h = im.h;
    if (w > PMAX_W || h > PMAX_H) {                               /* se reduce manteniendo la proporcion */
        if ((int64_t)w * PMAX_H > (int64_t)h * PMAX_W) { h = h * PMAX_W / w; w = PMAX_W; } else { w = w * PMAX_H / h; h = PMAX_H; }
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }
    undo_push();
    canvas_alloc(w, h);
    if (cv) for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t c = im.px[(int64_t)y * im.h / h * im.w + (int64_t)x * im.w / w];
        uint32_t a = c >> 24;                                     /* transparente sobre blanco */
        if (a < 255) { uint32_t r = ((c >> 16 & 255) * a + 255 * (255 - a)) / 255, g = ((c >> 8 & 255) * a + 255 * (255 - a)) / 255, b = ((c & 255) * a + 255 * (255 - a)) / 255; c = r << 16 | g << 8 | b; }
        cv[y * w + x] = c & 0x00FFFFFFu;
    }
    wi_free(&im); kfree(raw);
    zoom = 0; pan_x = pan_y = 0;
    return 0;
}
void paint_open_file(const char *p)
{
    if (!p || !*p) return;
    if (!cv) paint_init();
    s_cpy(path, p, sizeof path); p_named = 1;
    int r = load_file(path);
    if (r == 0) { p_dirty = 0; char m[96] = "Abierto: "; s_cat(m, path, sizeof m); say(m, 1); }
    else say(r == -2 ? "Formato no reconocido (PNG, BMP, JPEG o GIF)" : "No existe todavia: se guardara con ese nombre", r == -1);
}

/* ---------------------------------------------------------------- disposicion */
enum { B_NEW = 0, B_OPEN, B_SAVE, B_UNDO, B_REDO, B_ZOUT, B_ZIN, B_FIT, B_IA, B_N };   /* SOMA_292_PAINT_IA: + B_IA */
#define TB_H 48
#define ST_H 28
#define BTN 32
struct pl {
    int x, y, w, h;
    int bx[B_N], by, zl_x, path_x, path_w;
    int rail_x, rail_w, rail_cols, area_x, area_y, area_w, area_h, pan_x0, panel_x, panel_w, st_y;
    int dx, dy, dw, dh, z;                               /* lienzo en pantalla */
    int strip;                                           /* ventana estrecha: tira de 12 colores al pie del area */
    int show_sizes;
    int sw_y, sv_x, sv_y, sv_w, sv_h, hue_y, pal_y, pal_cell, rec_y, size_y, sl_x, sl_w, sizes_y;
};
static void layout(struct pl *L, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    L->x = (int)wx; L->y = (int)wy; L->w = (int)ww; L->h = (int)wh;
    L->by = L->y + (TB_H - BTN) / 2;
    int x = L->x + 12;
    L->strip = 0;
    for (int i = 0; i < B_N; i++) {
        L->bx[i] = x; x += BTN + 4;
        if (i == B_SAVE || i == B_REDO || i == B_FIT) x += 14;
        if (i == B_ZOUT) { L->zl_x = x; x += 58; }
    }
    L->path_x = x + 12; L->path_w = L->x + L->w - 12 - L->path_x; if (L->path_w < 0) L->path_w = 0;
    L->st_y = L->y + L->h - ST_H;
    int body_y = L->y + TB_H, body_h = L->st_y - body_y;
    L->rail_cols = (T_N * 44 + 12 > body_h) ? 2 : 1;
    L->rail_x = L->x; L->rail_w = 12 + L->rail_cols * 44;
    L->panel_w = L->w >= 760 ? 220 : L->w >= 600 ? 184 : 0;
    L->panel_x = L->x + L->w - L->panel_w;
    L->area_x = L->rail_x + L->rail_w; L->area_y = body_y; L->area_w = L->panel_x - L->area_x; L->area_h = body_h;
    if (!L->panel_w) { L->strip = 1; L->area_h -= 40; }
    /* lienzo */
    int z = zoom;
    if (z == 0) {
        int zx = (L->area_w - 48) * 100 / (cw ? cw : 1), zy = (L->area_h - 48) * 100 / (ch ? ch : 1);
        z = zx < zy ? zx : zy; if (z > 800) z = 800; if (z < 10) z = 10;
    }
    L->z = z; L->dw = cw * z / 100; L->dh = ch * z / 100;
    if (L->dw < 1) L->dw = 1;
    if (L->dh < 1) L->dh = 1;
    if (zoom == 0) { pan_x = pan_y = 0; }
    /* la mano no puede sacar el lienzo del todo de la vista */
    int mx = L->dw / 2 + L->area_w / 2 - 40, my = L->dh / 2 + L->area_h / 2 - 40;
    if (pan_x > mx) pan_x = mx;
    if (pan_x < -mx) pan_x = -mx;
    if (pan_y > my) pan_y = my;
    if (pan_y < -my) pan_y = -my;
    L->dx = L->area_x + (L->area_w - L->dw) / 2 + pan_x; L->dy = L->area_y + (L->area_h - L->dh) / 2 + pan_y;
    /* panel */
    int px = L->panel_x + 16, pw = L->panel_w - 32, y = body_y + 12;
    L->pal_cell = (pw - 5 * 6) / 6; L->sv_x = px; L->sv_w = pw; L->sl_x = px; L->sl_w = pw - 46;
    for (int pass = 0; pass < 2; pass++) {             /* el selector de color se encoge si la ventana es baja */
        int svh = 110;
        if (pass) { int over = L->sizes_y + 42 - (L->st_y - 8); svh = 110 - over; if (svh < 56) svh = 56; }
        L->sw_y = y + 22;                              /* muestra del color */
        L->sv_y = L->sw_y + 58; L->sv_h = svh;
        L->hue_y = L->sv_y + L->sv_h + 10;
        L->pal_y = L->hue_y + 14 + 30;
        L->rec_y = L->pal_y + 4 * (L->pal_cell + 6) + 26;
        L->size_y = L->rec_y + L->pal_cell + 34;
        L->sizes_y = L->size_y + 56;
        if (L->sizes_y + 42 <= L->st_y - 8) break;
    }
    L->show_sizes = L->sizes_y + 42 <= L->st_y - 4;
    if (!L->show_sizes && L->size_y + 46 <= L->st_y) L->sizes_y = 100000;   /* sin sitio: se oculta "lienzo nuevo" */
}
static void tool_rect(const struct pl *L, int t, int *x, int *y)
{
    *x = L->rail_x + 8 + (t % L->rail_cols) * 44;
    *y = L->area_y + 10 + (t / L->rail_cols) * 44;
}
static int to_canvas(const struct pl *L, int mx, int my, int *cx, int *cy)
{
    int64_t ox = (int64_t)(mx - L->dx) * cw, oy = (int64_t)(my - L->dy) * ch;
    *cx = (int)(ox >= 0 ? ox / L->dw : (ox - L->dw + 1) / L->dw);
    *cy = (int)(oy >= 0 ? oy / L->dh : (oy - L->dh + 1) / L->dh);
    return *cx >= 0 && *cy >= 0 && *cx < cw && *cy < ch;
}

/* ---------------------------------------------------------------- iconos vectoriales */
#define LN(a, b, c, d, w, col) fb_line_aa((a) * 16, (b) * 16, (c) * 16, (d) * 16, (w), (col))
static void ln16(int x0, int y0, int x1, int y1, int w, uint32_t col) { fb_line_aa(x0, y0, x1, y1, w, col); }
static void icon_tool(int t, int cx, int cy, uint32_t col)
{
    int X = cx * 16, Y = cy * 16;
    switch (t) {
    case T_PENCIL: ln16(X - 112, Y + 112, X + 80, Y - 80, 40, col); ln16(X - 112, Y + 112, X - 128, Y + 128, 22, col); LN(cx + 5, cy - 9, cx + 9, cy - 5, 26, col); break;
    case T_BRUSH: ln16(X - 16, Y + 16, X + 128, Y - 128, 30, col); fb_fill_rrect_aa(cx - 10, cy + 1, 10, 10, 5, col); break;
    case T_SPRAY: fb_stroke_rrect_aa(cx - 7, cy - 3, 9, 13, 2, col); fb_fill_rect((uint32_t)(cx - 5), (uint32_t)(cy - 7), 5, 4, col & 0x00FFFFFFu);
        { static const int8_t d[6][2] = { { 6, -9 }, { 9, -6 }, { 9, -1 }, { 6, -4 }, { 10, -10 }, { 5, 0 } }; for (int i = 0; i < 6; i++) fb_fill_rrect_aa(cx + d[i][0], cy + d[i][1], 2, 2, 1, col); } break;
    case T_ERASER: ln16(X - 120, Y + 40, X + 24, Y - 104, 22, col); ln16(X + 24, Y - 104, X + 120, Y - 8, 22, col); ln16(X + 120, Y - 8, X - 24, Y + 136, 22, col);
        ln16(X - 24, Y + 136, X - 120, Y + 40, 22, col); ln16(X - 72, Y - 8, X + 24, Y + 88, 22, col); LN(cx - 2, cy + 9, cx + 10, cy + 9, 20, col); break;
    case T_BUCKET: ln16(X - 120, Y - 24, X - 8, Y - 136, 24, col); ln16(X - 8, Y - 136, X + 104, Y - 24, 24, col); ln16(X + 104, Y - 24, X - 8, Y + 88, 24, col);
        ln16(X - 8, Y + 88, X - 120, Y - 24, 24, col); fb_fill_rrect_aa(cx + 7, cy + 3, 5, 7, 2, col); break;
    case T_PICKER: ln16(X - 128, Y + 128, X + 32, Y - 32, 32, col); fb_fill_rrect_aa(cx + 2, cy - 11, 9, 9, 4, col); ln16(X - 16, Y - 112, X + 112, Y + 16, 30, col); break;
    case T_LINE: LN(cx - 9, cy + 9, cx + 9, cy - 9, 34, col); break;
    case T_RECT: fb_stroke_rrect_aa(cx - 10, cy - 7, 20, 14, 2, col); fb_stroke_rrect_aa(cx - 9, cy - 6, 18, 12, 1, col); break;
    case T_RECTF: fb_fill_rrect_aa(cx - 10, cy - 7, 20, 14, 2, col); break;
    case T_ELL: fb_stroke_rrect_aa(cx - 10, cy - 8, 20, 16, 8, col); fb_stroke_rrect_aa(cx - 9, cy - 7, 18, 14, 7, col); break;
    case T_ELLF: fb_fill_rrect_aa(cx - 10, cy - 8, 20, 16, 8, col); break;
    case T_HAND: for (int i = 0; i < 4; i++) fb_fill_rrect_aa(cx - 7 + i * 4, cy - 9 + (i == 0 || i == 3 ? 3 : 0), 3, 11, 1, col);
        fb_fill_rrect_aa(cx - 8, cy, 16, 9, 4, col); fb_fill_rrect_aa(cx - 12, cy - 2, 4, 8, 2, col); break;
    }
}
static void icon_btn(int b, int cx, int cy, uint32_t col)
{
    int X = cx * 16, Y = cy * 16;
    switch (b) {
    case B_NEW: fb_stroke_rrect_aa(cx - 7, cy - 9, 14, 18, 2, col); LN(cx - 3, cy - 2, cx + 3, cy - 2, 20, col); LN(cx, cy - 5, cx, cy + 1, 20, col); break;
    case B_OPEN: fb_stroke_rrect_aa(cx - 9, cy - 6, 18, 13, 2, col); LN(cx - 9, cy - 3, cx - 2, cy - 3, 20, col); LN(cx - 9, cy - 6, cx - 4, cy - 6, 20, col); break;
    case B_SAVE: fb_stroke_rrect_aa(cx - 8, cy - 8, 16, 16, 2, col); fb_stroke_rrect_aa(cx - 4, cy - 8, 8, 5, 1, col); fb_stroke_rrect_aa(cx - 5, cy + 1, 10, 7, 1, col); break;
    case B_UNDO: case B_REDO: {
        int s = b == B_UNDO ? 1 : -1;
        ln16(X - s * 96, Y - 32, X + s * 48, Y - 32, 24, col);
        ln16(X + s * 48, Y - 32, X + s * 96, Y + 16, 24, col); ln16(X + s * 96, Y + 16, X + s * 48, Y + 80, 24, col); ln16(X + s * 48, Y + 80, X - s * 32, Y + 80, 24, col);
        ln16(X - s * 96, Y - 32, X - s * 40, Y - 88, 24, col); ln16(X - s * 96, Y - 32, X - s * 40, Y + 24, 24, col); break; }
    case B_ZOUT: case B_ZIN:
        fb_stroke_rrect_aa(cx - 8, cy - 8, 13, 13, 6, col); fb_stroke_rrect_aa(cx - 7, cy - 7, 11, 11, 5, col); LN(cx + 4, cy + 4, cx + 9, cy + 9, 30, col);
        LN(cx - 4, cy - 2, cx + 1, cy - 2, 20, col); if (b == B_ZIN) LN(cx - 2, cy - 4, cx - 2, cy + 1, 20, col); break;
    case B_FIT: LN(cx - 9, cy - 9, cx - 4, cy - 9, 22, col); LN(cx - 9, cy - 9, cx - 9, cy - 4, 22, col); LN(cx + 9, cy - 9, cx + 4, cy - 9, 22, col); LN(cx + 9, cy - 9, cx + 9, cy - 4, 22, col);
        LN(cx - 9, cy + 9, cx - 4, cy + 9, 22, col); LN(cx - 9, cy + 9, cx - 9, cy + 4, 22, col); LN(cx + 9, cy + 9, cx + 4, cy + 9, 22, col); LN(cx + 9, cy + 9, cx + 9, cy + 4, 22, col); break;
    }
}
static int btn_enabled(int b) { return (b == B_UNDO && u_count) || (b == B_REDO && r_count) || (b != B_UNDO && b != B_REDO); }

/* SOMA_292_PAINT_IA: dibujar con el modelo (paint_ia.inc, al final de este archivo) */
static void ia_al_pintar(uint64_t now);
static void ia_boton(int x, int y, int hov);
static void ia_campo(int x, int y, int w, uint64_t now);
static const char *ia_estado_txt(uint64_t now);
static int ia_ocupado_aqui(void);
static void ia_tecla(char c);
static void ia_aborta(void);
static void ia_suelta(int slot);
static void ia_abre_campo(void);

/* ---------------------------------------------------------------- dibujo de la ventana */
static uint32_t g_sv[200 * 110]; static int g_sv_hue = -1, g_sv_w;
void paint_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;
    if (!cv) paint_init();
    uint64_t now = timer_get_uptime_ms();
    ia_al_pintar(now);                                            /* SOMA_292_PAINT_IA: el dibujo de la IA, paso a paso */
    struct pl L; layout(&L, win_x, win_y, win_w, win_h);
    g_last_z = L.z;
    char b[96];

    /* barra de herramientas */
    fb_fill_rect(win_x, win_y, win_w, TB_H, KC_BAR);
    fb_fill_rect(win_x, win_y + TB_H - 1, win_w, 1, KC_LINE);
    for (int i = 0; i < B_N; i++) {
        if (i == B_IA) { ia_boton(L.bx[i], L.by, hover_btn == i); continue; }   /* SOMA_292_PAINT_IA */
        int en = btn_enabled(i), hov = hover_btn == i;
        if (en && hov) fb_fill_rounded_rect(L.bx[i], L.by, BTN, BTN, 8, 0x00222B3D);
        icon_btn(i, L.bx[i] + BTN / 2, L.by + BTN / 2, !en ? 0xFF404B5Eu : hov ? 0xFFFFFFFFu : 0xFFC4CDDAu);
        if (i == B_SAVE || i == B_REDO) fb_fill_rect((uint32_t)(L.bx[i] + BTN + 10), (uint32_t)(L.by + 6), 1, BTN - 12, KC_LINE);
    }
    fb_fill_rounded_rect(L.zl_x, L.by + 2, 54, BTN - 4, (BTN - 4) / 2, KC_FIELD);
    b[0] = 0; s_num(b, sizeof b, L.z); s_cat(b, " %", sizeof b);
    tx(L.zl_x + (54 - tw(b, 13, 1)) / 2, L.by, BTN, b, 13, zoom ? KC_TXT : KC_ACC, 1);
    if (ia_abierto && L.path_w > 80) ia_campo(L.path_x, L.by, L.path_w, now);   /* SOMA_292_PAINT_IA: "que dibujo" */
    else if (L.path_w > 80) {
        fb_fill_rounded_rect(L.path_x, L.by, L.path_w, BTN, BTN / 2, KC_FIELD);
        fb_stroke_rrect_aa(L.path_x, L.by, L.path_w, BTN, BTN / 2, 0xFF263142u);
        fb_fill_rrect_aa(L.path_x + 12, L.by + 10, 14, 12, 3, 0xFFF59E0Bu);          /* miniatura de imagen */
        fb_fill_rrect_aa(L.path_x + 15, L.by + 13, 4, 4, 2, 0xFFFFF7D6u);
        fb_set_clip(L.path_x + 32, L.by, L.path_w - 44, BTN);
        tx(L.path_x + 34, L.by, BTN, path, 13, KC_TXT, 0);
        fb_reset_clip();
    }

    /* raíl de herramientas */
    fb_fill_rect((uint32_t)L.rail_x, (uint32_t)L.area_y, (uint32_t)L.rail_w, (uint32_t)L.area_h, KC_SIDE);
    fb_fill_rect((uint32_t)(L.rail_x + L.rail_w - 1), (uint32_t)L.area_y, 1, (uint32_t)L.area_h, KC_LINE);
    for (int t = 0; t < T_N; t++) {
        int x, y; tool_rect(&L, t, &x, &y);
        int on = tool == t, hov = hover_btn == 100 + t;
        if (on) { fb_fill_rounded_rect(x, y, 40, 40, 9, KC_SELBG); fb_stroke_rrect_aa(x, y, 40, 40, 9, 0xFF2F6FB0u); }
        else if (hov) fb_fill_rounded_rect(x, y, 40, 40, 9, KC_HOVER);
        icon_tool(t, x + 20, y + 20, on ? 0xFFFFFFFFu : 0xFFAEB8C8u);
    }

    /* area de trabajo y lienzo */
    fb_fill_rect((uint32_t)L.area_x, (uint32_t)L.area_y, (uint32_t)L.area_w, (uint32_t)L.area_h, KC_AREA);
    fb_set_clip(L.area_x, L.area_y, L.area_w, L.area_h);
    fb_draw_shadow(L.dx, L.dy, L.dw, L.dh, 2, 22, 8, 0xA0000000u);
    const uint32_t *src = cv;
    if (drag == D_SHAPE && pv) { memcpy(pv, cv, (size_t)cw * ch * 4); shape_into(pv, sx0, sy0, sx1, sy1); src = pv; }
    fb_blit_scale(L.dx, L.dy, L.dw, L.dh, src, cw, ch, cw);
    if (L.z >= 600) {                                             /* rejilla de pixeles con mucho zoom */
        for (int x = 1; x < cw; x++) { int sx = L.dx + x * L.dw / cw; if (sx > L.area_x && sx < L.area_x + L.area_w) fb_fill_rect_alpha(sx, L.dy, 1, L.dh, 0x18000000u); }
        for (int y = 1; y < ch; y++) { int sy = L.dy + y * L.dh / ch; if (sy > L.area_y && sy < L.area_y + L.area_h) fb_fill_rect_alpha(L.dx, sy, L.dw, 1, 0x18000000u); }
    }
    fb_reset_clip();
    if (L.strip) {                                                /* ventana estrecha: colores basicos */
        int sy = L.area_y + L.area_h;
        fb_fill_rect((uint32_t)L.area_x, (uint32_t)sy, (uint32_t)L.area_w, 40, KC_SIDE);
        fb_fill_rect((uint32_t)L.area_x, (uint32_t)sy, (uint32_t)L.area_w, 1, KC_LINE);
        fb_fill_rrect_aa(L.area_x + 10, sy + 8, 24, 24, 12, 0xFF000000u | color);
        for (int i = 0; i < 12; i++) { uint32_t c = PALETTE[i * 2]; int x = L.area_x + 44 + i * 24; if (x + 20 > L.area_x + L.area_w) break;
            fb_fill_rrect_aa(x, sy + 10, 20, 20, 6, 0xFF000000u | c); if (c == color) fb_stroke_rrect_aa(x - 2, sy + 8, 24, 24, 8, 0xFFFFFFFFu); }
    }

    /* panel de propiedades */
    if (L.panel_w) {
        int px = L.panel_x + 16, pw = L.panel_w - 32;
        fb_fill_rect((uint32_t)L.panel_x, (uint32_t)L.area_y, (uint32_t)L.panel_w, (uint32_t)L.area_h, KC_SIDE);
        fb_fill_rect((uint32_t)L.panel_x, (uint32_t)L.area_y, 1, (uint32_t)L.area_h, KC_LINE);
        fb_set_clip(L.panel_x + 1, L.area_y, L.panel_w - 1, L.area_h);
        tx(px, L.area_y + 10, 18, "COLOR", 11, KC_FAINT, 1);
        /* muestra + codigo */
        fb_fill_rrect_aa(px, L.sw_y, 46, 46, 10, 0xFF000000u | color);
        fb_stroke_rrect_aa(px, L.sw_y, 46, 46, 10, 0x40FFFFFFu);
        hexcol(b, color); tx(px + 58, L.sw_y + 2, 22, b, 15, KC_TXT, 1);
        b[0] = 0; s_cat(b, "R ", sizeof b); s_num(b, sizeof b, (int)(color >> 16 & 255)); s_cat(b, "  G ", sizeof b); s_num(b, sizeof b, (int)(color >> 8 & 255));
        s_cat(b, "  B ", sizeof b); s_num(b, sizeof b, (int)(color & 255));
        tx(px + 58, L.sw_y + 24, 18, b, 11, KC_DIM, 0);
        /* saturacion / brillo */
        int sw = L.sv_w > 200 ? 200 : L.sv_w;
        if (g_sv_hue != hue || g_sv_w != sw) {
            for (int y = 0; y < L.sv_h; y++) for (int x = 0; x < sw; x++) g_sv[y * sw + x] = hsv2rgb(hue, x * 255 / (sw - 1), 255 - y * 255 / (L.sv_h - 1));
            g_sv_hue = hue; g_sv_w = sw;
        }
        fb_blit(L.sv_x, L.sv_y, g_sv, sw, L.sv_h, sw);
        fb_stroke_rrect_aa(L.sv_x, L.sv_y, sw, L.sv_h, 3, 0x30FFFFFFu);
        { int mx = L.sv_x + sat * (sw - 1) / 255, my = L.sv_y + (255 - val) * (L.sv_h - 1) / 255;
          fb_stroke_rrect_aa(mx - 6, my - 6, 12, 12, 6, 0xFFFFFFFFu); fb_stroke_rrect_aa(mx - 7, my - 7, 14, 14, 7, 0x80000000u); }
        /* tono */
        for (int x = 0; x < sw; x++) fb_fill_rect((uint32_t)(L.sv_x + x), (uint32_t)L.hue_y, 1, 12, hsv2rgb(x * 1535 / (sw - 1), 255, 255));
        { int hx = L.sv_x + hue * (sw - 1) / 1535; fb_stroke_rrect_aa(hx - 3, L.hue_y - 3, 7, 18, 3, 0xFFFFFFFFu); }
        /* paleta */
        tx(px, L.pal_y - 22, 18, "PALETA", 11, KC_FAINT, 1);
        for (int i = 0; i < 24; i++) {
            int x = px + (i % 6) * (L.pal_cell + 6), y = L.pal_y + (i / 6) * (L.pal_cell + 6);
            fb_fill_rrect_aa(x, y, L.pal_cell, L.pal_cell, 6, 0xFF000000u | PALETTE[i]);
            if (PALETTE[i] == color) { fb_stroke_rrect_aa(x - 2, y - 2, L.pal_cell + 4, L.pal_cell + 4, 8, 0xFFFFFFFFu); }
            else if (PALETTE[i] < 0x00202020 || PALETTE[i] == 0x00FFFFFF) fb_stroke_rrect_aa(x, y, L.pal_cell, L.pal_cell, 6, 0x30FFFFFFu);
        }
        tx(px, L.rec_y - 22, 18, "RECIENTES", 11, KC_FAINT, 1);
        for (int i = 0; i < 6; i++) {
            int x = px + i * (L.pal_cell + 6);
            if (i < n_recent) { fb_fill_rrect_aa(x, L.rec_y, L.pal_cell, L.pal_cell, L.pal_cell / 2, 0xFF000000u | recent[i]); fb_stroke_rrect_aa(x, L.rec_y, L.pal_cell, L.pal_cell, L.pal_cell / 2, 0x30FFFFFFu); }
            else fb_stroke_rrect_aa(x, L.rec_y, L.pal_cell, L.pal_cell, L.pal_cell / 2, 0xFF263142u);
        }
        /* grosor */
        tx(px, L.size_y - 22, 18, "GROSOR", 11, KC_FAINT, 1);
        int ty = L.size_y + 12;
        fb_fill_rounded_rect(L.sl_x, ty - 2, L.sl_w, 4, 2, 0x00263142);
        int kx = L.sl_x + (bsize - 1) * (L.sl_w - 1) / 31;
        fb_fill_rounded_rect(L.sl_x, ty - 2, kx - L.sl_x + 1, 4, 2, KC_PRIMARY);
        fb_fill_rrect_aa(kx - 8, ty - 8, 16, 16, 8, 0xFFFFFFFFu);
        b[0] = 0; s_num(b, sizeof b, bsize); s_cat(b, " px", sizeof b);
        tx(L.sl_x + L.sl_w + 10, L.size_y, 24, b, 12, KC_TXT, 1);
        /* lienzo */
        int ly = L.sizes_y + 14;
        if (L.show_sizes) {
        tx(px, ly - 22, 18, "LIENZO NUEVO", 11, KC_FAINT, 1);
        static const char *const lab[3] = { "320x240", "480x360", "640x480" };
        int bw = (pw - 8) / 3;
        for (int i = 0; i < 3; i++) {
            int x = px + i * (bw + 4), cur = (cw == 320 + i * 160 && ch == 240 + i * 120), hov = hover_btn == 200 + i;
            fb_fill_rounded_rect(x, ly, bw, 28, 8, cur ? KC_SELBG : hov ? 0x00222B3D : 0x00182133);
            tx(x + (bw - tw(lab[i], 11, 0)) / 2, ly, 28, lab[i], 11, cur ? 0x00FFFFFF : KC_DIM, 0);
        }
        }
        fb_reset_clip();
    }

    /* barra de estado */
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, ST_H, KC_BAR);
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, 1, KC_LINE);
    fb_fill_rrect_aa(L.x + 12, L.st_y + 9, 10, 10, 5, 0xFF000000u | color);
    b[0] = 0; s_cat(b, tool_name[tool], sizeof b);
    if (tool != T_BUCKET && tool != T_PICKER && tool != T_HAND) { s_cat(b, "  -  ", sizeof b); s_num(b, sizeof b, bsize); s_cat(b, " px", sizeof b); }
    tx(L.x + 30, L.st_y, ST_H, b, 12, KC_TXT, 0);
    int rx = L.x + L.w - 12;
    b[0] = 0; s_num(b, sizeof b, cw); s_cat(b, " x ", sizeof b); s_num(b, sizeof b, ch); s_cat(b, " px", sizeof b);
    rx -= tw(b, 12, 0); tx(rx, L.st_y, ST_H, b, 12, KC_DIM, 0);
    if (cur_x >= 0) { b[0] = 0; s_num(b, sizeof b, cur_x); s_cat(b, ", ", sizeof b); s_num(b, sizeof b, cur_y); rx -= 24 + tw(b, 12, 0); tx(rx, L.st_y, ST_H, b, 12, KC_DIM, 0); }
    const char *iae = ia_estado_txt(now);                         /* SOMA_292_PAINT_IA: "La IA esta dibujando..." */
    if (iae) {
        int mx = L.x + 200; fb_set_clip(mx, L.st_y, rx - 16 - mx, ST_H);
        tx(mx, L.st_y, ST_H, iae, 12, KC_ACC, 1);
        fb_reset_clip();
    } else if (msg[0] && now < msg_until) {
        int mx = L.x + 200; if (mx + tw(msg, 12, 0) > rx - 16) mx = L.x + 170;
        fb_set_clip(mx, L.st_y, rx - 16 - mx, ST_H);
        tx(mx, L.st_y, ST_H, msg, 12, msg_ok ? KC_OK : 0x00F87171, 0);
        fb_reset_clip();
    } else {
        fb_set_clip(L.x + 200, L.st_y, rx - 216 - L.x, ST_H);
        tx(L.x + 200, L.st_y, ST_H, "Ctrl+Z deshacer   Ctrl+S guardar   [ ] grosor   + - zoom   0 ajustar", 11, KC_FAINT, 0);
        fb_reset_clip();
    }
}

/* ---------------------------------------------------------------- raton */
static void begin_canvas(const struct pl *L, int mx, int my)
{
    int x, y; int in = to_canvas(L, mx, my, &x, &y);
    if (tool == T_HAND) { drag = D_HAND; hand_mx = mx; hand_my = my; if (!zoom) zoom = L->z; return; }
    if (!in) return;
    if (ia_ocupado_aqui() == 2) { say("Espera: la IA esta terminando el dibujo", 0); return; }   /* SOMA_292_PAINT_IA */
    cur_x = x; cur_y = y;
    if (tool == T_PICKER) { set_color(cv[y * cw + x], 1); push_recent(color); tool = prev_tool; say("Color tomado del lienzo", 1); return; }
    undo_push();
    if (tool != T_ERASER) push_recent(color);
    if (tool == T_BUCKET) { flood(x, y, color); return; }
    if (tool == T_PENCIL || tool == T_BRUSH || tool == T_ERASER || tool == T_SPRAY) {
        drag = D_PAINT; last_x = x; last_y = y;
        if (tool == T_SPRAY) spray(cv, x, y, bsize, color);
        else disc(cv, x, y, tool == T_PENCIL ? 0 : tool == T_ERASER ? bsize : (bsize - 1) / 2, tool == T_ERASER ? 0x00FFFFFF : color);
        return;
    }
    drag = D_SHAPE; sx0 = sx1 = x; sy0 = sy1 = y;
}
static void panel_pick(const struct pl *L, int mx, int my)
{
    int sw = L->sv_w > 200 ? 200 : L->sv_w;
    if (drag == D_SV) {
        int x = mx - L->sv_x, y = my - L->sv_y;
        if (x < 0) x = 0;
        if (x > sw - 1) x = sw - 1;
        if (y < 0) y = 0;
        if (y > L->sv_h - 1) y = L->sv_h - 1;
        sat = x * 255 / (sw - 1); val = 255 - y * 255 / (L->sv_h - 1);
        set_color(hsv2rgb(hue, sat, val), 0);
    } else if (drag == D_HUE) {
        int x = mx - L->sv_x; if (x < 0) x = 0;
        if (x > sw - 1) x = sw - 1;
        hue = x * 1535 / (sw - 1); if (sat < 40) sat = 200;
        if (val < 40) val = 230;
        set_color(hsv2rgb(hue, sat, val), 0);
    } else if (drag == D_SIZE) {
        int x = mx - L->sl_x; if (x < 0) x = 0;
        if (x > L->sl_w - 1) x = L->sl_w - 1;
        bsize = 1 + x * 31 / (L->sl_w - 1);
    }
}
static void new_canvas(int w, int h)
{
    if (ia_ocupado_aqui() == 2) ia_aborta();                       /* SOMA_292_PAINT_IA */
    undo_push();
    canvas_alloc(w, h);
    zoom = 0; pan_x = pan_y = 0;
    char m[64] = "Lienzo nuevo de "; s_num(m, sizeof m, w); s_cat(m, " x ", sizeof m); s_num(m, sizeof m, h); say(m, 1);
}
static void zoom_step(int z, int dir)
{
    static const int Z[] = { 25, 50, 75, 100, 150, 200, 300, 400, 600, 800 };
    int i;
    if (dir > 0) { for (i = 0; i < 10 && Z[i] <= z; i++) { } zoom = i < 10 ? Z[i] : 800; }
    else { for (i = 9; i >= 0 && Z[i] >= z; i--) { } zoom = i >= 0 ? Z[i] : 25; }
}

int paint_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    if (!cv) paint_init();
    struct pl L; layout(&L, win_x, win_y, win_w, win_h);
    drag = D_NONE;
    /* barra superior */
    if (my >= L.by && my < L.by + BTN) {
        for (int i = 0; i < B_N; i++) if (mx >= L.bx[i] && mx < L.bx[i] + BTN) {
            switch (i) {
            case B_NEW: new_canvas(cw, ch); break;
            case B_OPEN: paint_open_file(path); break;
            case B_SAVE: do_save(); break;
            case B_UNDO: do_undo(); break;
            case B_REDO: do_redo(); break;
            case B_ZOUT: zoom_step(L.z, -1); break;
            case B_ZIN: zoom_step(L.z, 1); break;
            case B_FIT: zoom = 0; break;
            case B_IA: if (ia_abierto) ia_abierto = 0; else ia_abre_campo(); break;   /* SOMA_292_PAINT_IA */
            }
            return 0;
        }
        if (mx >= L.zl_x && mx < L.zl_x + 54) { zoom = zoom ? 0 : 100; return 0; }
        return 0;
    }
    /* herramientas */
    if (mx >= L.rail_x && mx < L.rail_x + L.rail_w && my >= L.area_y && my < L.st_y) {
        for (int t = 0; t < T_N; t++) { int x, y; tool_rect(&L, t, &x, &y); if (mx >= x && mx < x + 40 && my >= y && my < y + 40) { if (t == T_PICKER && tool != T_PICKER) prev_tool = tool; tool = t; char m[64]; s_cpy(m, tool_name[t], sizeof m); s_cat(m, "  (tecla ", sizeof m); char k[2] = { tool_key[t], 0 }; s_cat(m, k, sizeof m); s_cat(m, ")", sizeof m); say(m, 1); } }
        return 0;
    }
    /* panel */
    if (L.panel_w && mx >= L.panel_x && my >= L.area_y && my < L.st_y) {
        int px = L.panel_x + 16, pw = L.panel_w - 32, sw = L.sv_w > 200 ? 200 : L.sv_w;
        if (mx >= L.sv_x && mx < L.sv_x + sw && my >= L.sv_y && my < L.sv_y + L.sv_h) { drag = D_SV; panel_pick(&L, mx, my); return 0; }
        if (mx >= L.sv_x && mx < L.sv_x + sw && my >= L.hue_y - 4 && my < L.hue_y + 16) { drag = D_HUE; panel_pick(&L, mx, my); return 0; }
        for (int i = 0; i < 24; i++) { int x = px + (i % 6) * (L.pal_cell + 6), y = L.pal_y + (i / 6) * (L.pal_cell + 6);
            if (mx >= x && mx < x + L.pal_cell && my >= y && my < y + L.pal_cell) { set_color(PALETTE[i], 1); return 0; } }
        for (int i = 0; i < n_recent && i < 6; i++) { int x = px + i * (L.pal_cell + 6);
            if (mx >= x && mx < x + L.pal_cell && my >= L.rec_y && my < L.rec_y + L.pal_cell) { set_color(recent[i], 1); return 0; } }
        if (mx >= L.sl_x - 8 && mx < L.sl_x + L.sl_w + 8 && my >= L.size_y && my < L.size_y + 26) { drag = D_SIZE; panel_pick(&L, mx, my); return 0; }
        int ly = L.sizes_y + 14, bw = (pw - 8) / 3;
        if (L.show_sizes) for (int i = 0; i < 3; i++) { int x = px + i * (bw + 4); if (mx >= x && mx < x + bw && my >= ly && my < ly + 28) { new_canvas(320 + i * 160, 240 + i * 120); return 0; } }
        return 0;
    }
    if (L.strip && my >= L.area_y + L.area_h && my < L.st_y) {
        for (int i = 0; i < 12; i++) { int x = L.area_x + 44 + i * 24; if (mx >= x && mx < x + 20) { set_color(PALETTE[i * 2], 1); return 0; } }
        return 0;
    }
    /* lienzo */
    if (mx >= L.area_x && mx < L.area_x + L.area_w && my >= L.area_y && my < L.area_y + L.area_h) begin_canvas(&L, mx, my);
    return 0;
}

void paint_handle_mouse(int32_t mx, int32_t my, int is_down, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    if (!cv) return;
    struct pl L; layout(&L, win_x, win_y, win_w, win_h);
    int x, y; int in = to_canvas(&L, mx, my, &x, &y);
    if (in) { cur_x = x; cur_y = y; }
    if (!is_down) {
        if (drag == D_SHAPE) {
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= cw) x = cw - 1;
            if (y >= ch) y = ch - 1;
            shape_into(cv, sx0, sy0, x, y);
        }
        drag = D_NONE; last_x = last_y = -1;
        return;
    }
    switch (drag) {
    case D_PAINT: {
        uint32_t c = tool == T_ERASER ? 0x00FFFFFF : color;
        int r = tool == T_PENCIL ? 0 : tool == T_ERASER ? bsize : (bsize - 1) / 2;
        if (tool == T_SPRAY) spray(cv, x, y, bsize, color);
        else if (last_x >= 0) stroke(cv, last_x, last_y, x, y, r, c);
        last_x = x; last_y = y;
        break; }
    case D_SHAPE: sx1 = x; sy1 = y; break;
    case D_HAND: pan_x += mx - hand_mx; pan_y += my - hand_my; hand_mx = mx; hand_my = my; break;
    case D_SV: case D_HUE: case D_SIZE: panel_pick(&L, mx, my); break;
    default: break;
    }
}

void paint_handle_key(char c)
{
    if (!cv) paint_init();
    if (ia_abierto && ((unsigned char)c >= 32 || c == 8 || c == 10 || c == 13 || c == 27 || c == 127)) { ia_tecla(c); return; }   /* SOMA_292_PAINT_IA */
    if (c == 26) { do_undo(); return; }                          /* Ctrl+Z */
    if (c == 25) { do_redo(); return; }                          /* Ctrl+Y */
    if (c == 19) { do_save(); return; }                          /* Ctrl+S */
    if (c == 15) { paint_open_file(path); return; }              /* Ctrl+O */
    if (c == 14) { new_canvas(cw, ch); return; }                 /* Ctrl+N */
    if (c == '[') { if (bsize > 1) bsize--; return; }
    if (c == ']') { if (bsize < 32) bsize++; return; }
    if (c == '+' || c == '=') { zoom_step(g_last_z, 1); return; }
    if (c == '-') { zoom_step(g_last_z, -1); return; }
    if (c == '0') { zoom = 0; return; }
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    for (int t = 0; t < T_N; t++) if (tool_key[t] == c) { if (t == T_PICKER && tool != T_PICKER) prev_tool = tool; tool = t; say(tool_name[t], 1); return; }
}

/* SOMA_PARCHE_PORTAPAPELES: copiar, cortar y pegar con el lienzo nuevo */
int paint_clip_op(int op)
{
    if (!cv) { clip_toast("El lienzo no esta listo"); return 1; }
    if (op == CLIP_OP_PASTE) {
        int w = 0, h = 0; const uint32_t *px = clip_image(&w, &h);
        if (!px) { clip_toast("No hay ninguna imagen en el portapapeles"); return 1; }
        undo_push();
        int dw = w, dh = h;
        if (dw > cw || dh > ch) {
            if ((int64_t)w * ch > (int64_t)h * cw) { dw = cw; dh = h * cw / w; } else { dh = ch; dw = w * ch / h; }
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;
        }
        int ox = (cw - dw) / 2, oy = (ch - dh) / 2;
        for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++)
            cv[(oy + y) * cw + ox + x] = px[(int64_t)y * h / dh * w + (int64_t)x * w / dw] & 0x00FFFFFFu;
        say("Imagen pegada en el centro (Ctrl+Z la quita)", 1);
        clip_toast(dw == w && dh == h ? "Imagen pegada" : "Imagen pegada (reducida al lienzo)");
        return 1;
    }
    if (clip_set_image(cv, cw, ch) < 0) { clip_toast("No se pudo copiar el lienzo"); return 1; }
    if (op == CLIP_OP_CUT) { undo_push(); for (int i = 0; i < cw * ch; i++) cv[i] = 0x00FFFFFF; say("Lienzo cortado", 1); clip_toast("Lienzo cortado"); }
    else { say("Lienzo copiado", 1); clip_toast("Lienzo copiado"); }
    return 1;
}



/* SOMA_215_PAINT: varias ventanas de Paint. Cada ventana tiene su lienzo, su deshacer, su herramienta, sus colores, su zoom y su
 * archivo. Al cambiar de ventana se guarda el estado de la anterior y se carga el de la nueva (unos cientos de bytes; el lienzo
 * y los pasos de deshacer van por puntero y nunca se copian). */
#define PT_FIELDS X(cv) X(pv) X(cw) X(ch) X(undo_buf) X(undo_w) X(undo_h) X(u_top) X(u_count) X(r_count) X(tool) X(prev_tool) \
    X(bsize) X(color) X(recent) X(n_recent) X(hue) X(sat) X(val) X(zoom) X(pan_x) X(pan_y) X(path) X(msg) X(msg_until) X(msg_ok) \
    X(cur_x) X(cur_y) X(drag) X(sx0) X(sy0) X(sx1) X(sy1) X(last_x) X(last_y) X(hand_mx) X(hand_my) X(hover_btn) X(g_last_z) X(p_named) X(p_dirty) \
    X(ia_abierto) X(ia_len) X(ia_txt)
struct pt_inst {
#define X(f) __typeof__(f) f;
    PT_FIELDS
#undef X
};
#define PT_INST_MAX 8
static struct pt_inst pt_insts[PT_INST_MAX + 1];
static int pt_used[PT_INST_MAX + 1], pt_live;
static void pt_save(struct pt_inst *q)
{
#define X(f) memcpy(&q->f, &f, sizeof f);
    PT_FIELDS
#undef X
}
static void pt_load(const struct pt_inst *q)
{
#define X(f) memcpy(&f, &q->f, sizeof f);
    PT_FIELDS
#undef X
}
static void pt_blank(void)                                  /* estado de un Paint sin lienzo (lo crea paint_ensure) */
{
    cv = 0; pv = 0; cw = 320; ch = 240;
    for (int i = 0; i < UNDO_N; ++i) { undo_buf[i] = 0; undo_w[i] = undo_h[i] = 0; }
    u_top = u_count = r_count = 0; tool = prev_tool = T_BRUSH; bsize = 4; color = 0x003B82F6; n_recent = 0;
    hue = 1024; sat = 200; val = 246; zoom = 0; pan_x = pan_y = 0;
    ruta_nueva(); msg[0] = 0; msg_until = 0; msg_ok = 1;
    cur_x = cur_y = -1; drag = 0; sx0 = sy0 = sx1 = sy1 = 0; last_x = last_y = -1; hand_mx = hand_my = 0; hover_btn = -1; g_last_z = 100; p_named = 0; p_dirty = 0;
    ia_abierto = 0; ia_len = 0; ia_txt[0] = 0;
}
void paint_select(int slot)
{
    if (slot < 1 || slot > PT_INST_MAX || !pt_used[slot] || slot == pt_live) return;
    if (pt_live) pt_save(&pt_insts[pt_live]);
    pt_load(&pt_insts[slot]);
    pt_live = slot;
}
int paint_inst_new(void)
{
    for (int k = 1; k <= PT_INST_MAX; ++k) {
        if (pt_used[k]) continue;
        if (pt_live) pt_save(&pt_insts[pt_live]);
        else if (cv) { if (cv) kfree(cv); if (pv) kfree(pv); for (int i = 0; i < UNDO_N; ++i) if (undo_buf[i]) kfree(undo_buf[i]); }   /* lienzo sin ventana */
        pt_blank();
        pt_used[k] = 1; pt_live = k;
        pt_save(&pt_insts[k]);
        return k;
    }
    return 0;
}
void paint_inst_drop(int slot)
{
    if (slot < 1 || slot > PT_INST_MAX || !pt_used[slot]) return;
    ia_suelta(slot);                                               /* SOMA_292_PAINT_IA: un dibujo pendiente de esta ventana se descarta */
    int back = pt_live != slot ? pt_live : 0;
    paint_select(slot);
    if (cv) kfree(cv);
    if (pv) kfree(pv);
    for (int i = 0; i < UNDO_N; ++i) if (undo_buf[i]) kfree(undo_buf[i]);
    pt_blank();
    pt_used[slot] = 0; pt_live = 0;
    if (back) paint_select(back);
}
static const char *pt_path_of(int k) { return k == pt_live ? path : pt_insts[k].path; }
static int pt_named_of(int k) { return k == pt_live ? p_named : pt_insts[k].p_named; }
int paint_inst_dirty(int slot) { return slot >= 1 && slot <= PT_INST_MAX && pt_used[slot] ? (slot == pt_live ? p_dirty : pt_insts[slot].p_dirty) : 0; }   /* SOMA_219_AVISO */
int paint_inst_find(const char *file)
{
    for (int k = 1; k <= PT_INST_MAX; ++k) {
        if (!pt_used[k] || !pt_named_of(k) || !file) continue;
        const char *a = pt_path_of(k), *b = file;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return k;
    }
    return 0;
}
const char *paint_inst_name(int slot)
{
    if (slot < 1 || slot > PT_INST_MAX || !pt_used[slot] || !pt_named_of(slot)) return "";
    const char *p = pt_path_of(slot), *n = p;
    for (; *p; ++p) if (*p == '/') n = p + 1;
    return n;
}

#include "paint_ia.inc"   /* SOMA_292_PAINT_IA */
