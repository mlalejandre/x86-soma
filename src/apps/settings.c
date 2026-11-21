/* PARCHE 085: Ajustes de la interfaz con la estetica del explorador. Apariencia: transparencia de las ventanas (40-100 %, con opcion
 * de dejar opaca la activa). SOMA_184: pagina Fuentes: fuente del escritorio, de la interfaz y los documentos (90-120 %) y del editor de
 * texto, elegidas del catalogo general (las mismas fuentes que usa el navegador). Los cambios se aplican al momento y se guardan. */
#include "settings.h"
#include "fb.h"
#include "fs.h"
#include "web_font.h"
#include "gui_font.h"
#include "netif.h"          /* SOMA_175C: pagina Redes */
#include "net.h"
#include "idt.h"           /* timer_get_uptime_ms */
#include "llm.h"           /* SOMA_195: pagina IA */
#include "llm_proto.h"
#include "thread.h"
#include "mouse.h"          /* SOMA_225_AJUSTES: velocidad del puntero */
#include "sysicon.h"         /* SOMA_225_AJUSTES: pagina Iconos */
#include "hwmon.h"           /* SOMA_226_SISTEMA: pagina Sistema */
#include "audio_sys.h"        /* SOMA_250_TTS: pagina Sonido */
#include "hda.h"
#ifdef __x86_64__
#include "voz_sys.h"          /* SOMA_246_VOZ_MODELOS: pagina Voz */
#include "usermode.h"
#include "bootdiag.h"         /* SOMA_228_ARRANQUE: arranque, SOMADATA e informe en la pagina Sistema */
#include "usbdisk.h"
#include "sysdisk.h"
#endif
#if defined(__x86_64__)
#include "netmgr.h"
#include "iwl.h"
#include "wpa.h"
#endif

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_CARD    0x00111827
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB

static int g_opacity = 100, g_active_opaque = 0, g_loaded;
static int g_editor_face;                         /* SOMA_184: cara del editor de texto (FFACE_*) */
static char g_desk[40];                           /* SOMA_184: fuente del escritorio elegida en Ajustes (nombre del catalogo; vacio = la de siempre) */
static int g_drag;
static char g_msg[96] = "Los cambios se aplican al momento y se guardan en /etc/ui.conf";
static const int SCALES[4] = { 90, 100, 110, 120 };
static const char *const SCALE_NAME[4] = { "Pequena", "Normal", "Grande", "Muy grande" };
/* SOMA_225_AJUSTES: pagina Pantalla (resolucion de trabajo y velocidad del puntero) */
static int g_res;                                  /* 0 nativa, 1 = 5/6, 2 = 2/3, 3 = 1/2 de la pantalla */
static int g_speed = 100;                          /* % */
static const int SPEEDS[6] = { 50, 75, 100, 140, 180, 240 };
static const char *const SPEED_NAME[6] = { "Muy lenta", "Lenta", "Normal", "Rapida", "Muy rapida", "Maxima" };
static int g_icon_tab;                             /* pagina Iconos: 0 del sistema, 1 de las aplicaciones */
void gui_request_resolution(uint32_t w, uint32_t h);   /* gui.c: se aplica en el bucle del escritorio */
static void res_of(int k, uint32_t *w, uint32_t *h)
{
    uint32_t hw = fb_hw_width(), hh = fb_hw_height();
    static const int NUM[4] = { 6, 5, 4, 3 }, DEN = 6;
    *w = hw * (uint32_t)NUM[k] / DEN & ~1u; *h = hh * (uint32_t)NUM[k] / DEN & ~1u;
}
static int res_count(void)
{
    int n = 1; for (int k = 1; k < 4; k++) { uint32_t w, h; res_of(k, &w, &h); if (w >= 640u && h >= 400u) n = k + 1; }
    return n;
}
static void res_apply(void) { uint32_t w, h; if (g_res >= res_count()) g_res = 0; res_of(g_res, &w, &h); gui_request_resolution(g_res ? w : 0, g_res ? h : 0); }

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}

int settings_opacity(void) { return g_opacity; }
int settings_active_opaque(void) { return g_active_opaque; }

static void save(void)
{
    char b[320] = "opacidad="; s_num(b, sizeof b, g_opacity);
    s_cat(b, "\nactiva_opaca=", sizeof b); s_num(b, sizeof b, g_active_opaque);
    s_cat(b, "\nfuente=", sizeof b); s_cat(b, wf_ui_mono() ? "mono" : "sans", sizeof b);      /* compatible con versiones anteriores */
    s_cat(b, "\nfamilia=", sizeof b); s_num(b, sizeof b, wf_ui_family());                       /* SOMA_184 */
    s_cat(b, "\nescala=", sizeof b); s_num(b, sizeof b, wf_ui_scale());
    s_cat(b, "\neditor=", sizeof b); s_num(b, sizeof b, g_editor_face);
    if (g_desk[0]) { s_cat(b, "\nescritorio=", sizeof b); s_cat(b, g_desk, sizeof b); }
    s_cat(b, "\nresolucion=", sizeof b); s_num(b, sizeof b, g_res);          /* SOMA_225_AJUSTES */
    s_cat(b, "\nraton=", sizeof b); s_num(b, sizeof b, g_speed);
    s_cat(b, "\n", sizeof b);
    (void)vfs_write("/etc/ui.conf", b, (uint32_t)s_len(b));
}
static int val_of(const char *t, const char *key, int def)
{
    int kl = s_len(key);
    for (int i = 0; t[i]; i++) {
        if ((i == 0 || t[i - 1] == '\n')) {
            int j = 0; while (j < kl && t[i + j] == key[j]) j++;
            if (j == kl && t[i + j] == '=') { const char *v = t + i + j + 1; if (v[0] == 'm') return 1; if (v[0] == 's') return 0; int n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0'); return n; }
        }
    }
    return def;
}
static int str_of(const char *t, const char *key, char *out, int max)       /* SOMA_184: valor de texto de una clave */
{
    int kl = s_len(key);
    out[0] = 0;
    for (int i = 0; t[i]; i++) {
        if (i == 0 || t[i - 1] == '\n') {
            int j = 0; while (j < kl && t[i + j] == key[j]) j++;
            if (j == kl && t[i + j] == '=') { const char *v = t + i + j + 1; int n = 0; while (v[n] && v[n] != '\n' && v[n] != '\r' && n < max - 1) { out[n] = v[n]; n++; } out[n] = 0; return n; }
        }
    }
    return 0;
}
int settings_editor_face(void) { return font_sysfont_off() ? FFACE_MONO : g_editor_face; }
static void settings_load_sound(void);
void settings_load(void)
{
    settings_load_sound();                                              /* SOMA_250_TTS: volumen y microfono guardados */
    if (g_loaded) return;
    g_loaded = 1;
    char b[320]; int n = vfs_read("/etc/ui.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    g_opacity = val_of(b, "opacidad", 100); if (g_opacity < 40) g_opacity = 40;
    if (g_opacity > 100) g_opacity = 100;
    g_active_opaque = val_of(b, "activa_opaca", 0) ? 1 : 0;
    int fam = val_of(b, "familia", -1);                                    /* SOMA_184: 0 DejaVu Sans, 1 Mono, 2 Liberation; antes solo "fuente=" */
    if (fam < 0) fam = val_of(b, "fuente", 0);
    int off = font_sysfont_off();                                           /* soma.sysfont=off: solo las fuentes de siempre */
    if (off && fam != WF_FAM_MONO) fam = WF_FAM_SANS;
    wf_ui_set_family(fam, val_of(b, "escala", 100));
    g_editor_face = val_of(b, "editor", 0); if (g_editor_face < 0 || g_editor_face >= FFACE_COUNT || g_editor_face == FFACE_LIBERATION_BOLD || (off && g_editor_face)) g_editor_face = FFACE_MONO;
    if (!off && str_of(b, "escritorio", g_desk, sizeof g_desk) && !font_set_by_name(g_desk)) g_desk[0] = 0;
    g_speed = mouse_set_speed(val_of(b, "raton", 100));                  /* SOMA_225_AJUSTES */
    g_res = val_of(b, "resolucion", 0); if (g_res < 0 || g_res > 3) g_res = 0;
    if (g_res) res_apply();
}
static void snd_load(void);
static void settings_load_sound(void) { static int done; if (!done) { done = 1; snd_load(); } }   /* SOMA_250_TTS */

/* ---------------------------------------------------------------- disposicion */
/* SOMA_227_IMAGEN: desplazamiento vertical de las paginas. El contenido se dibuja desplazado y recortado al area de la pagina;
 * los recortes internos de cada pagina se cruzan con ese area (antes un fb_reset_clip interno lo deshacia). */
static int g_clon, g_clx, g_cly, g_clw, g_clh, g_cbot;
static void sclip_set(int x, int y, int w, int h)
{
    if (g_clon) {
        int x1 = x + w, y1 = y + h, bx1 = g_clx + g_clw, by1 = g_cly + g_clh;
        if (x < g_clx) x = g_clx;
        if (y < g_cly) y = g_cly;
        if (x1 > bx1) x1 = bx1;
        if (y1 > by1) y1 = by1;
        w = x1 > x ? x1 - x : 0; h = y1 > y ? y1 - y : 0;
        if (!w || !h) { (fb_set_clip)(0, 0, 1, 1); return; }
    }
    (fb_set_clip)(x, y, w, h);
}
static void sclip_reset(void) { if (g_clon) (fb_set_clip)(g_clx, g_cly, g_clw, g_clh); else (fb_reset_clip)(); }
#define fb_set_clip(x, y, w, h) sclip_set((x), (y), (w), (h))
#define fb_reset_clip() sclip_reset()
static void cbot(int y) { if (y > g_cbot) g_cbot = y; }
struct sl { int x, y, w, h, side_w, cx, cw, c1_y, c1_h, sl_x, sl_w, sl_y, tg_x, tg_y, prev_y, seg_w, reset_x, reset_y;
            int f1_y, f1_h, f1_seg, f1_demo, f2_y, f2_h, fam_y, sz_y, demo_y, f3_y, f3_h, ed_y, reset2_y; };   /* SOMA_184: pagina Fuentes */
static void lay(struct sl *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->side_w = w > 640 ? 200 : 150;
    L->cx = L->x + L->side_w + 24; L->cw = L->x + L->w - 24 - L->cx;
    int cy = L->y + 84;
    L->c1_y = cy; L->c1_h = 190;
    L->sl_x = L->cx + 20; L->sl_w = L->cw - 40 - 70; L->sl_y = cy + 62;
    L->tg_x = L->cx + L->cw - 20 - 44; L->tg_y = cy + 100;
    L->prev_y = cy + 136;
    L->seg_w = L->cw - 40;
    L->reset_x = L->cx + L->cw - 130; L->reset_y = L->c1_y + L->c1_h + 16;
    L->f1_y = cy; L->f1_h = 128; L->f1_seg = cy + 56; L->f1_demo = cy + 94;                  /* escritorio */
    L->f2_y = L->f1_y + L->f1_h + 12; L->f2_h = 190;                                         /* interfaz y documentos */
    L->fam_y = L->f2_y + 56; L->sz_y = L->f2_y + 96; L->demo_y = L->f2_y + 140;
    L->f3_y = L->f2_y + L->f2_h + 12; L->f3_h = 100; L->ed_y = L->f3_y + 56;                  /* editor de texto */
    L->reset2_y = L->f3_y + L->f3_h + 14;
}
static void card(int x, int y, int w, int h) { cbot(y + h); fb_fill_rrect_aa(x, y, w, h, 12, 0xFF000000u | KC_CARD); fb_stroke_rrect_aa(x, y, w, h, 12, 0xFF1E293Bu); }
static void seg(int x, int y, int w, int n, int sel, const char *const *lab)
{
    fb_fill_rounded_rect(x, y, w, 32, 10, 0x000C1119);
    int bw = (w - 4) / n;
    for (int i = 0; i < n; i++) {
        if (i == sel) fb_fill_rounded_rect(x + 2 + i * bw, y + 2, bw, 28, 8, 0x00283449);
        tx(x + 2 + i * bw + (bw - tw(lab[i], 12, i == sel)) / 2, y + 2, 28, lab[i], 12, i == sel ? 0x00FFFFFF : KC_DIM, i == sel);
    }
}

/* ================================================================ SOMA_250_TTS: listas desplegables
 * Una caja con el valor elegido; al pulsarla se abre la lista ENCIMA de todo (se dibuja al final), con rueda y barra si son muchas.
 * Las cajas se registran al dibujar (en coordenadas de pantalla, ya desplazadas): el clic usa exactamente lo que se ve. */
typedef const char *(*dd_label_fn)(int i, char *buf, int max);
struct ddbox { int id, x, y, w, n, sel; dd_label_fn lab; };
#define DD_MAX 8
#define DD_H 32
#define DD_ROW 28
#define DD_ROWS 9
enum { DD_NONE = -1, DD_DESK = 1, DD_UIFAM, DD_EDITOR, DD_VOICE, DD_MIC };
static struct ddbox g_dd[DD_MAX]; static int g_ndd;
static int g_dd_open = DD_NONE, g_dd_first;
static int g_ddl_x, g_ddl_y, g_ddl_w, g_ddl_h, g_ddl_rows;           /* lista abierta (la ultima vez que se dibujo) */
static void cbot(int y);
static void dd(int id, int x, int y, int w, int n, int sel, dd_label_fn lab)
{
    if (g_ndd < DD_MAX) { struct ddbox *b = &g_dd[g_ndd++]; b->id = id; b->x = x; b->y = y; b->w = w; b->n = n; b->sel = sel; b->lab = lab; }
    int open = g_dd_open == id;
    fb_fill_rounded_rect(x, y, w, DD_H, 10, 0x000C1119);
    fb_stroke_rrect_aa(x, y, w, DD_H, 10, open ? (0xFF000000u | KC_ACC) : 0xFF263142u);
    char b[96]; const char *t = (n > 0 && sel >= 0 && sel < n) ? lab(sel, b, sizeof b) : (n > 0 ? "(elige una)" : "(ninguna)");
    fb_set_clip(x + 10, y, w - 44, DD_H);
    tx(x + 14, y, DD_H, t, 13, n > 0 ? KC_TXT : KC_FAINT, 0);
    fb_reset_clip();
    int cx = x + w - 20, cy = y + DD_H / 2;
    if (open) { fb_line_aa((cx - 5) * 16, (cy + 2) * 16, cx * 16, (cy - 3) * 16, 22, 0xFF38BDF8u); fb_line_aa(cx * 16, (cy - 3) * 16, (cx + 5) * 16, (cy + 2) * 16, 22, 0xFF38BDF8u); }
    else { fb_line_aa((cx - 5) * 16, (cy - 2) * 16, cx * 16, (cy + 3) * 16, 22, 0xFF8592A8u); fb_line_aa(cx * 16, (cy + 3) * 16, (cx + 5) * 16, (cy - 2) * 16, 22, 0xFF8592A8u); }
    cbot(y + DD_H);
}
static struct ddbox *dd_find(int id) { for (int i = 0; i < g_ndd; i++) if (g_dd[i].id == id) return &g_dd[i]; return 0; }
static void dd_draw_open(int wx, int wy, int ww, int wh)
{
    struct ddbox *b = g_dd_open != DD_NONE ? dd_find(g_dd_open) : 0;
    if (!b || b->n <= 0) { g_ddl_h = 0; return; }
    int rows = b->n < DD_ROWS ? b->n : DD_ROWS;
    if (g_dd_first > b->n - rows) g_dd_first = b->n - rows;
    if (g_dd_first < 0) g_dd_first = 0;
    int lh = rows * DD_ROW + 8, ly = b->y + DD_H + 4;
    if (ly + lh > wy + wh - 4 && b->y - 4 - lh >= wy + 4) ly = b->y - 4 - lh;      /* no cabe debajo: hacia arriba */
    if (ly + lh > wy + wh - 4) ly = wy + wh - 4 - lh;
    if (ly < wy + 4) ly = wy + 4;
    g_ddl_x = b->x; g_ddl_y = ly; g_ddl_w = b->w; g_ddl_h = lh; g_ddl_rows = rows;
    (fb_set_clip)(wx, wy, ww, wh);
    fb_draw_shadow(b->x, ly, b->w, lh, 10, 18, 6, 0xA0000000u);
    fb_fill_rrect_aa(b->x, ly, b->w, lh, 10, 0xFF0F1726u);
    fb_stroke_rrect_aa(b->x, ly, b->w, lh, 10, 0xFF38BDF8u);
    for (int r = 0; r < rows; r++) {
        int i = g_dd_first + r, ry = ly + 4 + r * DD_ROW;
        if (i == b->sel) fb_fill_rounded_rect(b->x + 4, ry, b->w - 8, DD_ROW - 2, 7, KC_SELBG);
        char t[96]; const char *l = b->lab(i, t, sizeof t);
        (fb_set_clip)(b->x + 8, ry, b->w - (b->n > rows ? 26 : 16), DD_ROW);
        tx(b->x + 14, ry, DD_ROW - 2, l, 13, i == b->sel ? 0x00FFFFFF : KC_TXT, i == b->sel);
        (fb_set_clip)(wx, wy, ww, wh);
        if (i == b->sel) { int cx = b->x + b->w - 24, cy = ry + DD_ROW / 2 - 1; fb_line_aa((cx - 4) * 16, cy * 16, (cx - 1) * 16, (cy + 3) * 16, 22, 0xFF38BDF8u); fb_line_aa((cx - 1) * 16, (cy + 3) * 16, (cx + 5) * 16, (cy - 4) * 16, 22, 0xFF38BDF8u); }
    }
    if (b->n > rows) {                                                  /* barra */
        int th = lh - 12, kh = th * rows / b->n; if (kh < 20) kh = 20;
        int ky = ly + 6 + (th - kh) * g_dd_first / (b->n - rows);
        fb_fill_rrect_aa(b->x + b->w - 9, ly + 6, 4, th, 2, 0x22FFFFFFu);
        fb_fill_rrect_aa(b->x + b->w - 10, ky, 6, kh, 3, 0x90A5B4CBu);
    }
    (fb_reset_clip)();
}

/* ================================================================ SOMA_175C: pagina Redes (cable, movil por USB y WiFi) */
static int g_page;                                    /* 0 Apariencia, 1 Redes */
__attribute__((unused)) static char g_sel[33], g_pw[65];
__attribute__((unused)) static int g_pw_len, g_pw_focus, g_pw_show;
__attribute__((unused)) static char g_nmsg[120];
struct nrow { char ssid[33]; int rssi, sec, saved; };
#define NROWS_MAX 12
__attribute__((unused)) static struct nrow g_rows[NROWS_MAX]; static int g_nrows;
__attribute__((unused)) static int g_list_y, g_row_h = 30, g_btn_y, g_scan_x, g_disc_x, g_tg_x, g_tg_y, g_pw_y, g_conn_x, g_forget_x, g_eye_x, g_pw_x, g_pw_w, g_panel;
__attribute__((unused)) static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int s_eq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
__attribute__((unused)) static void ip_str(char *b, int max, const uint8_t *ip) { b[0] = 0; for (int i = 0; i < 4; i++) { s_num(b, max, ip[i]); if (i < 3) s_cat(b, ".", max); } }
__attribute__((unused)) static void dot(int x, int y, uint32_t c) { fb_fill_rrect_aa(x, y, 10, 10, 5, 0xFF000000u | c); }
__attribute__((unused)) static void bars(int x, int y, int rssi, uint32_t on)
{
    int lv = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -128 ? 1 : 0;
    for (int i = 0; i < 4; i++) { int bh = 4 + i * 3; fb_fill_rect((uint32_t)(x + i * 5), (uint32_t)(y + 14 - bh), 3, (uint32_t)bh, i < lv ? on : 0x00334155); }
}
__attribute__((unused)) static void button(int x, int y, int w, const char *t, int primary)
{
    fb_fill_rounded_rect(x, y, w, 30, 15, primary ? KC_PRIMARY : 0x00182133);
    if (!primary) fb_stroke_rrect_aa(x, y, w, 30, 15, 0xFF263142u);
    tx(x + (w - tw(t, 12, 1)) / 2, y, 30, t, 12, 0x00FFFFFF, 1);
}
/* ================================================================ SOMA_195: pagina IA (con que modelo de lenguaje habla SOMA) */
#define PAGE_IA 3
#define PAGE_VOZ     6     /* SOMA_246_VOZ_MODELOS */
#define PAGE_SONIDO  7     /* SOMA_250_TTS */
#define PAGE_SISTEMA 8
#define PAGE_ACERCA  9
#define NITEMS 10
static struct llm_conf g_ia;                          /* copia de trabajo; se guarda con "Guardar y probar" */
static char g_ia_f[4][LLM_KEY_MAX];                   /* lo que se esta escribiendo: servidor, modelo, clave y (SOMA_196) clave del lanzador */
static const int IA_FMAX[4] = { LLM_BASE_MAX - 1, LLM_MODEL_MAX - 1, LLM_KEY_MAX - 1, LLM_KEY_MAX - 1 };
static const char *const IA_FIELD[4] = { "servidor", "modelo", "clave", "clave-lanzador" };
static int g_ia_listmode;                             /* SOMA_196: 0 = modelos del servidor en marcha; 1 = modelos que el lanzador puede arrancar */
static char g_ia_pick[LLM_MODEL_MAX];
static int ia_nf(void) { return g_ia.prov == LLM_LOCAL ? 4 : 3; }      /* el lanzador solo existe para el servidor local */
static int g_ia_loaded, g_ia_focus = -1, g_ia_show, g_ia_first;
static char g_ia_msg[220]; static int g_ia_msg_ok;
static volatile int g_ia_busy, g_ia_job, g_ia_ver; static int g_ia_seen;
static char g_ia_models[4096]; static int g_ia_nmodels;
static void ia_fields_from(void) { for (int k = 0; k < 4; k++) { const char *s = k == 0 ? g_ia.base[g_ia.prov] : k == 1 ? g_ia.model[g_ia.prov] : k == 2 ? g_ia.key[g_ia.prov] : g_ia.launcher_key; g_ia_f[k][0] = 0; s_cat(g_ia_f[k], s, LLM_KEY_MAX); } }
static void ia_load(void)
{
    if (g_ia_loaded) return;
    g_ia = *llm_conf_get(); ia_fields_from(); g_ia_loaded = 1;
    g_ia_msg[0] = 0; s_cat(g_ia_msg, "Elige proveedor, servidor y modelo y pulsa Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1;
}
static int ia_fields_to(int report)                    /* 1 = todo valido */
{
    static const char *const bad[4] = { "El servidor no es valido (ejemplos: 192.168.1.200:8087, https://api.openai.com)", "El nombre del modelo no es valido", "La clave no es valida", "La clave del lanzador no es valida" };
    for (int k = 0; k < 4; k++) if (llm_conf_set(&g_ia, g_ia.prov, IA_FIELD[k], g_ia_f[k]) != 0) { if (report) { g_ia_msg[0] = 0; s_cat(g_ia_msg, bad[k], sizeof g_ia_msg); g_ia_msg_ok = 0; } return 0; }
    return 1;
}
static void ia_progress(uint32_t secs)                 /* SOMA_196: el modelo esta cargando en el servidor */
{
    g_ia_msg[0] = 0; s_cat(g_ia_msg, "Arrancando ", sizeof g_ia_msg); s_cat(g_ia_msg, g_ia_pick, sizeof g_ia_msg); s_cat(g_ia_msg, ": cargando el modelo en el servidor... ", sizeof g_ia_msg);
    s_num(g_ia_msg, sizeof g_ia_msg, (int)secs); s_cat(g_ia_msg, " s", sizeof g_ia_msg); g_ia_msg_ok = -1; g_ia_ver++;
}
static void ia_worker(void *arg)                       /* la red va en su hilo: el escritorio no se queda parado esperando al modelo */
{
    (void)arg;
    static char reply[400];
    if (g_ia_job == 3) {                               /* SOMA_196: que modelos puede arrancar el lanzador */
        int n = llm_launcher_models(g_ia_models, sizeof g_ia_models);
        g_ia_nmodels = n > 0 ? n : 0; g_ia_first = 0; g_ia_listmode = 1; g_ia_msg[0] = 0;
        if (n < 0) { s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (!n) { s_cat(g_ia_msg, "El lanzador no encuentra modelos .gguf en el servidor.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else { s_cat(g_ia_msg, "Pulsa el modelo que quieres arrancar en el servidor.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
        g_ia_busy = 0; g_ia_ver++;
        thread_exit();
    }
    if (g_ia_job == 4) {                               /* SOMA_196: arrancar, esperar a que cargue y probar */
        if (!llm_launcher_start(g_ia_pick, 240000, ia_progress)) {
            g_ia_msg[0] = 0; s_cat(g_ia_msg, "No arranco. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0;
            g_ia_busy = 0; g_ia_ver++;
            thread_exit();
        }
        g_ia = *llm_conf_get(); ia_fields_from(); g_ia_nmodels = 0; g_ia_listmode = 0;
        g_ia_job = 1;                                  /* y ahora la prueba de siempre */
    }
    if (g_ia_job == 1) {
        uint64_t t0 = timer_get_uptime_ms();
        int ok = llm_chat("Responde unicamente con estas tres palabras: SOMA TEST OK", reply, sizeof reply, 60000);
        g_ia_msg[0] = 0;
        if (ok) { s_cat(g_ia_msg, "Funciona (", sizeof g_ia_msg); s_num(g_ia_msg, sizeof g_ia_msg, (int)(timer_get_uptime_ms() - t0)); s_cat(g_ia_msg, " ms). El modelo responde: ", sizeof g_ia_msg); s_cat(g_ia_msg, reply, sizeof g_ia_msg); }
        else { s_cat(g_ia_msg, "No funciona. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); if (g_ia.prov == LLM_LOCAL) s_cat(g_ia_msg, ". Si esta apagado: Arrancar servidor.", sizeof g_ia_msg); }
        for (int i = 0; g_ia_msg[i]; i++) if (g_ia_msg[i] == '\n' || g_ia_msg[i] == '\r') g_ia_msg[i] = ' ';
        g_ia_msg_ok = ok;
    } else {
        int n = llm_list_models(g_ia_models, sizeof g_ia_models);
        g_ia_nmodels = n > 0 ? n : 0; g_ia_first = 0; g_ia_msg[0] = 0;
        if (n < 0 && g_ia.prov == LLM_LOCAL) { s_cat(g_ia_msg, "El servidor no responde. Si esta apagado, pulsa Arrancar servidor.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (n < 0) { s_cat(g_ia_msg, "No se pudo pedir la lista. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (!n) { s_cat(g_ia_msg, "El servidor no devolvio ningun modelo: escribe el nombre a mano.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else { s_num(g_ia_msg, sizeof g_ia_msg, n); s_cat(g_ia_msg, " modelos. Pulsa uno para elegirlo y despues Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
    }
    g_ia_busy = 0; g_ia_ver++;
    thread_exit();
}
static void ia_start(int job)
{
    if (g_ia_busy) return;
    if (!ia_fields_to(1)) return;
    g_ia_msg[0] = 0; g_ia_msg_ok = -1;
    if (llm_conf_save(&g_ia) != 0) s_cat(g_ia_msg, "(no se pudo guardar /etc/llm.conf: vale hasta reiniciar) ", sizeof g_ia_msg);
    ia_fields_from();
    s_cat(g_ia_msg, job == 1 ? "Guardado. Probando el modelo..." : job == 2 ? "Guardado. Pidiendo la lista de modelos..." : job == 3 ? "Preguntando al lanzador del servidor..." : "Pidiendo al servidor que arranque el modelo...", sizeof g_ia_msg);
    g_ia_job = job; g_ia_busy = 1;
    if (!thread_create("ajustes_ia", ia_worker, 0)) { g_ia_busy = 0; g_ia_msg[0] = 0; s_cat(g_ia_msg, "No se pudo crear el hilo de la prueba", sizeof g_ia_msg); g_ia_msg_ok = 0; }
}
static int ia_tick(void) { if (g_ia_seen != g_ia_ver) { g_ia_seen = g_ia_ver; return 1; } return 0; }

static int voz_page_tick(void);   /* SOMA_246_VOZ_MODELOS (abajo) */
static uint32_t g_sig; static uint64_t g_last_tick;
int settings_tick(void)
{
    if (g_page == PAGE_IA) return ia_tick();
    if (g_page == PAGE_VOZ) return voz_page_tick();                     /* SOMA_246_VOZ_MODELOS */
    if (g_page != 1) return 0;
    static uint64_t last_check;                                         /* SOMA_175C2: como mucho 2 veces por segundo (antes en cada vuelta */
    uint64_t t = timer_get_uptime_ms();                                  /* del bucle de la GUI, ~1000 veces/s, y consultaba el enlace de cada */
    if (t - last_check < 500) return 0;                                  /* interfaz: el escritorio se quedaba sin tiempo) */
    last_check = t;
    uint32_t sig = 0;
#if defined(__x86_64__)
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    sig = (uint32_t)w.state * 7u + (uint32_t)w.scanning * 131u + (uint32_t)w.connecting * 1031u + (uint32_t)w.nbss * 17u + (uint32_t)w.ready * 3u + (uint32_t)w.enabled * 5u;
#endif
    for (int i = 0; i < netif_count(); i++) sig = sig * 31u + (uint32_t)netif_is_up(i) + (netif_get(i) == netif_active() ? 7u : 0u);
    sig = sig * 31u + net_ip[3] + net_ip[2] * 7u;
    uint64_t now = timer_get_uptime_ms();
    if (sig != g_sig || now - g_last_tick > 5000) { g_sig = sig; g_last_tick = now; return 1; }
    return 0;
}

static void render_redes(const struct sl *L)
{
    char b[140];
    tx(L->cx, L->y + 18, 30, "Redes", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Cable, movil por USB y WiFi. Se usa la primera con enlace (cable > movil > WiFi).", 12, KC_DIM, 0);
    /* tarjeta 1: conexiones */
    int y1 = L->y + 84, h1 = 128;
    card(L->cx, y1, L->cw, h1);
    tx(L->cx + 20, y1 + 10, 22, "Conexiones", 14, KC_TXT, 1);
    const struct netif_ops *act = netif_active();
    static const char *const names[3] = { "eth0", "usb0", "wlan0" };
    static const char *const label[3] = { "Cable", "Movil por USB", "WiFi" };
    for (int k = 0; k < 3; k++) {
        int ry = y1 + 36 + k * 22, link = 0, found = 0, up = 0;
        const struct netif_ops *o = 0;
        for (int i = 0; i < netif_count(); i++) { const struct netif_ops *q = netif_get(i); if (q && s_eq(q->name, names[k])) { o = q; found = 1; up = netif_is_up(i); link = up && q->link ? q->link() : 0; } }
        int active = o && o == act && link;
        dot(L->cx + 22, ry + 6, active ? 0x0022C55E : link ? 0x0038BDF8 : 0x00475569);
        tx(L->cx + 40, ry, 22, label[k], 12, KC_TXT, active);
        b[0] = 0;
        if (!found) s_cat(b, "no disponible", sizeof b);
        else if (k == 0 && !up) s_cat(b, "no detectado", sizeof b);
        else if (active) s_cat(b, "en uso", sizeof b);
        else if (link) s_cat(b, "con enlace (en reserva)", sizeof b);
        else s_cat(b, k == 0 ? "sin cable" : k == 1 ? "sin movil (activa compartir conexion por USB)" : "sin conectar", sizeof b);
        tx(L->cx + 170, ry, 22, b, 12, active ? 0x0022C55E : KC_DIM, 0);
    }
    b[0] = 0; s_cat(b, "IP ", sizeof b); { char ip[20]; ip_str(ip, sizeof ip, net_ip); s_cat(b, ip, sizeof b); }
#if defined(__x86_64__)
    s_cat(b, "   -   ", sizeof b); s_cat(b, netmgr_state(), sizeof b);
#endif
    fb_set_clip(L->cx + 20, y1 + h1 - 30, L->cw - 40, 24);
    tx(L->cx + 20, y1 + h1 - 30, 22, b, 11, KC_FAINT, 0);
    fb_reset_clip();
    /* tarjeta 2: WiFi */
    int y2 = y1 + h1 + 14, h2 = L->y + L->h - 16 - y2;
    if (h2 < 140) h2 = 140;
    card(L->cx, y2, L->cw, h2);
    tx(L->cx + 20, y2 + 10, 22, "WiFi", 14, KC_TXT, 1);
#if !defined(__x86_64__)
    tx(L->cx + 20, y2 + 40, 22, "En esta plataforma la WiFi se maneja con la orden wifi del terminal.", 12, KC_DIM, 0);
    g_nrows = 0; g_panel = 0;
#else
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    g_tg_x = L->cx + L->cw - 20 - 44; g_tg_y = y2 + 12;
    fb_fill_rrect_aa(g_tg_x, g_tg_y, 44, 24, 12, w.enabled ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
    fb_fill_rrect_aa(g_tg_x + (w.enabled ? 22 : 2), g_tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    b[0] = 0;
    if (!w.present) { s_cat(b, "Sin tarjeta WiFi: ", sizeof b); s_cat(b, w.why, sizeof b); }
    else if (!w.enabled) s_cat(b, "Desactivada", sizeof b);
    else if (w.state == IWL_W_UP) { s_cat(b, "Conectada a ", sizeof b); s_cat(b, w.ssid, sizeof b); s_cat(b, "  (canal ", sizeof b); s_num(b, sizeof b, w.chan); s_cat(b, ", ", sizeof b); s_num(b, sizeof b, w.rate_mbps); s_cat(b, " Mbit/s)", sizeof b); }
    else if (w.connecting) { s_cat(b, "Conectando a ", sizeof b); s_cat(b, w.ssid, sizeof b); s_cat(b, ": ", sizeof b); s_cat(b, w.state_name, sizeof b); s_cat(b, "...", sizeof b); }
    else if (w.scanning) s_cat(b, "Buscando redes (unos segundos)...", sizeof b);
    else if (w.state == IWL_W_FAIL && w.why[0]) { s_cat(b, "No se pudo conectar: ", sizeof b); s_cat(b, w.why, sizeof b); }
    else if (!w.ready) { s_cat(b, "Preparando la tarjeta... ", sizeof b); s_cat(b, w.why, sizeof b); }
    else s_cat(b, "Lista. Elige una red de la lista.", sizeof b);
    fb_set_clip(L->cx + 20, y2 + 34, L->cw - 40, 22);
    tx(L->cx + 20, y2 + 34, 22, b, 12, w.state == IWL_W_UP ? 0x0022C55E : w.state == IWL_W_FAIL ? 0x00F87171 : KC_DIM, w.state == IWL_W_UP);
    fb_reset_clip();
    g_btn_y = y2 + 62; g_scan_x = L->cx + 20; g_disc_x = -1;
    button(g_scan_x, g_btn_y, 130, w.scanning ? "Buscando..." : "Buscar redes", 0);
    if (w.state == IWL_W_UP) { g_disc_x = g_scan_x + 142; button(g_disc_x, g_btn_y, 120, "Desconectar", 0); }
    if (g_nmsg[0]) { fb_set_clip(L->cx + 300, g_btn_y, L->cw - 320, 30); tx(L->cx + 300, g_btn_y, 30, g_nmsg, 11, KC_FAINT, 0); fb_reset_clip(); }
    /* lista: una fila por nombre de red (la de mejor senal), ordenada por senal */
    int n = 0; const struct iwl_bss *bl = iwl_bss_list(&n);
    g_nrows = 0;
    for (int i = 0; i < n; i++) {
        if (!bl[i].ssid[0]) continue;
        int k = 0; while (k < g_nrows && !s_eq(g_rows[k].ssid, bl[i].ssid)) k++;
        if (k < g_nrows) { if (bl[i].rssi > g_rows[k].rssi) g_rows[k].rssi = bl[i].rssi; continue; }
        if (g_nrows >= NROWS_MAX) {                      /* sustituye a la peor si esta es mejor */
            int worst = 0; for (int j = 1; j < g_nrows; j++) if (g_rows[j].rssi < g_rows[worst].rssi) worst = j;
            if (bl[i].rssi <= g_rows[worst].rssi) continue;
            k = worst;
        } else k = g_nrows++;
        s_cpy(g_rows[k].ssid, bl[i].ssid, 33); g_rows[k].rssi = bl[i].rssi; g_rows[k].sec = bl[i].sec; g_rows[k].saved = iwl_wifi_is_saved(bl[i].ssid);
    }
    for (int i = 1; i < g_nrows; i++) { struct nrow v = g_rows[i]; int j = i; while (j > 0 && g_rows[j - 1].rssi < v.rssi) { g_rows[j] = g_rows[j - 1]; j--; } g_rows[j] = v; }
    g_list_y = g_btn_y + 42;
    int panel_h = 46, avail = y2 + h2 - 12 - panel_h - g_list_y, maxr = avail / g_row_h;
    if (maxr < 1) maxr = 1;
    if (g_nrows > maxr) g_nrows = maxr;
    if (!g_nrows) tx(L->cx + 20, g_list_y, 24, w.ready ? "Sin redes en la lista: pulsa Buscar redes." : "", 12, KC_FAINT, 0);
    for (int i = 0; i < g_nrows; i++) {
        int ry = g_list_y + i * g_row_h, sel = s_eq(g_rows[i].ssid, g_sel), conn = w.state == IWL_W_UP && s_eq(g_rows[i].ssid, w.ssid);
        if (sel) fb_fill_rounded_rect(L->cx + 12, ry, L->cw - 24, g_row_h - 2, 8, KC_SELBG);
        bars(L->cx + 22, ry + 6, g_rows[i].rssi, conn ? 0x0022C55E : KC_ACC);
        fb_set_clip(L->cx + 50, ry, L->cw - 260, g_row_h);
        tx(L->cx + 50, ry, g_row_h - 2, g_rows[i].ssid, 12, KC_TXT, conn || sel);
        fb_reset_clip();
        b[0] = 0; s_cat(b, wpa_sec_name(g_rows[i].sec), sizeof b);
        if (conn) s_cat(b, "  - conectada", sizeof b); else if (g_rows[i].saved) s_cat(b, "  - guardada", sizeof b);
        tx(L->cx + L->cw - 200, ry, g_row_h - 2, b, 11, conn ? 0x0022C55E : KC_DIM, 0);
    }
    /* panel de la red elegida */
    g_panel = 0; g_pw_y = y2 + h2 - 12 - panel_h + 8;
    int si = -1; for (int i = 0; i < g_nrows; i++) if (s_eq(g_rows[i].ssid, g_sel)) si = i;
    if (si >= 0 && !(w.state == IWL_W_UP && s_eq(g_sel, w.ssid))) {
        g_panel = 1;
        int needpw = g_rows[si].sec != SEC_OPEN && !g_rows[si].saved;
        int x0 = L->cx + 20;
        g_pw_x = x0; g_pw_w = needpw ? L->cw - 40 - (g_rows[si].saved ? 250 : 140) : 0;
        if (needpw) {
            fb_fill_rounded_rect(g_pw_x, g_pw_y, g_pw_w, 30, 8, 0x00090D15);
            fb_stroke_rrect_aa(g_pw_x, g_pw_y, g_pw_w, 30, 8, g_pw_focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
            char shown[70]; int k = 0;
            if (!g_pw_len) { s_cpy(shown, "Contrasena de la red", sizeof shown); }
            else { for (int i = 0; i < g_pw_len && k < 64; i++) shown[k++] = g_pw_show ? g_pw[i] : '*'; shown[k] = 0; }
            if (g_pw_focus && g_pw_len) { shown[k] = '_'; shown[k + 1] = 0; }
            fb_set_clip(g_pw_x + 8, g_pw_y, g_pw_w - 50, 30);
            tx(g_pw_x + 12, g_pw_y, 30, shown, 12, g_pw_len ? KC_TXT : KC_FAINT, 0);
            fb_reset_clip();
            g_eye_x = g_pw_x + g_pw_w - 44;
            tx(g_eye_x, g_pw_y, 30, g_pw_show ? "ocultar" : "ver", 10, KC_ACC, 0);
        } else {
            g_eye_x = -1;
            tx(x0, g_pw_y, 30, g_rows[si].sec == SEC_OPEN ? "Red abierta (sin contrasena)" : "Red guardada: se usara la contrasena guardada", 12, KC_DIM, 0);
        }
        g_conn_x = L->cx + L->cw - 20 - 120;
        g_forget_x = g_rows[si].saved ? g_conn_x - 112 : -1;
        button(g_conn_x, g_pw_y, 120, w.connecting ? "Conectando..." : "Conectar", 1);
        if (g_forget_x >= 0) button(g_forget_x, g_pw_y, 100, "Olvidar", 0);
    }
#endif
}

static int click_redes(const struct sl *L, int32_t mx, int32_t my)
{
    (void)L;
#if defined(__x86_64__)
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    if (mx >= g_tg_x && mx < g_tg_x + 44 && my >= g_tg_y && my < g_tg_y + 24) {
        iwl_wifi_enable(!w.enabled); s_cpy(g_nmsg, w.enabled ? "WiFi desactivada" : "WiFi activada", sizeof g_nmsg); return 0;
    }
    if (my >= g_btn_y && my < g_btn_y + 30) {
        if (mx >= g_scan_x && mx < g_scan_x + 130) {
            int r = iwl_wifi_request_scan();
            s_cpy(g_nmsg, r == 0 ? "" : r == -2 ? "Desconecta primero para buscar otras redes" : "La WiFi no esta lista", sizeof g_nmsg);
            return 0;
        }
        if (g_disc_x >= 0 && mx >= g_disc_x && mx < g_disc_x + 120) { iwl_wifi_request_disconnect(); s_cpy(g_nmsg, "Desconectando...", sizeof g_nmsg); return 0; }
    }
    for (int i = 0; i < g_nrows; i++) {
        int ry = g_list_y + i * g_row_h;
        if (my >= ry && my < ry + g_row_h && mx >= L->cx + 12 && mx < L->cx + L->cw - 12) {
            if (!s_eq(g_sel, g_rows[i].ssid)) { s_cpy(g_sel, g_rows[i].ssid, sizeof g_sel); g_pw_len = 0; g_pw[0] = 0; g_pw_show = 0; }
            g_pw_focus = g_rows[i].sec != SEC_OPEN && !g_rows[i].saved;
            g_nmsg[0] = 0;
            return 0;
        }
    }
    if (g_panel && my >= g_pw_y && my < g_pw_y + 30) {
        if (g_eye_x >= 0 && mx >= g_eye_x && mx < g_eye_x + 44) { g_pw_show = !g_pw_show; return 0; }
        if (g_pw_w && mx >= g_pw_x && mx < g_pw_x + g_pw_w) { g_pw_focus = 1; return 0; }
        if (mx >= g_conn_x && mx < g_conn_x + 120) {
            g_pw[g_pw_len] = 0;
            int r = iwl_wifi_request_connect(g_sel, g_pw_len ? g_pw : 0, 1);
            s_cpy(g_nmsg, r ? "Espera: ya hay una conexion en curso" : "", sizeof g_nmsg);
            if (!r) { for (int i = 0; i < 65; i++) g_pw[i] = 0; g_pw_len = 0; g_pw_focus = 0; }
            return 0;
        }
        if (g_forget_x >= 0 && mx >= g_forget_x && mx < g_forget_x + 100) { iwl_wifi_forget(g_sel); s_cpy(g_nmsg, "Red olvidada", sizeof g_nmsg); return 0; }
    }
    g_pw_focus = 0;
#else
    (void)mx; (void)my;
#endif
    return 0;
}

/* SOMA_195: pagina IA */
static int seg_index(const struct sl *L, int32_t mx, int n);
struct ial { int c1_y, c1_h, seg_y, f_y[4], nf, b3_x, b3_w, f_x, f_w, eye_x, btn_y, b1_x, b1_w, b2_x, b2_w, msg_y, c2_y, c2_h, list_y, rows, pg_x; };
static void ia_lay(const struct sl *L, struct ial *A)
{
    A->c1_y = L->y + 84; A->seg_y = A->c1_y + 40;
    A->f_x = L->cx + 110; A->f_w = L->cw - 130;
    A->nf = ia_nf();
    for (int k = 0; k < 4; k++) A->f_y[k] = A->seg_y + 46 + k * 36;
    A->eye_x = A->f_x + A->f_w - 44;
    A->btn_y = A->f_y[A->nf - 1] + 42; A->b1_x = L->cx + 20; A->b1_w = 150; A->b2_x = A->b1_x + A->b1_w + 10; A->b2_w = 120;
    A->b3_x = A->b2_x + A->b2_w + 10; A->b3_w = 150;
    A->msg_y = A->btn_y + 36; A->c1_h = A->msg_y + 44 - A->c1_y;
    A->c2_y = A->c1_y + A->c1_h + 12; A->c2_h = L->y + L->h - 16 - A->c2_y;
    A->list_y = A->c2_y + 36; A->rows = (A->c2_h - 44) / 24; if (A->rows < 0) A->rows = 0;
    A->pg_x = L->cx + L->cw - 20 - 150;
}
static const char *ia_model_at(int idx, char *out, int max)
{
    const char *m = g_ia_models; int k = 0;
    for (int i = 0; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
    while (m[k] && m[k] != '\n' && k < max - 1) { out[k] = m[k]; k++; }
    out[k] = 0; return out;
}
static void ia_field(int x, int y, int w, const char *s, int focus, const char *hint)
{
    fb_fill_rounded_rect(x, y, w, 30, 8, 0x000C1119);
    fb_stroke_rrect_aa(x, y, w, 30, 8, focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
    int avail = w - 24, n = s_len(s), st = 0;
    while (st < n && tw(s + st, 12, 0) > avail) st++;                       /* si no cabe se ve el final (donde se escribe) */
    fb_set_clip(x + 10, y, w - 20, 30);
    if (n) tx(x + 10, y, 30, s + st, 12, KC_TXT, 0); else if (!focus) tx(x + 10, y, 30, hint, 12, KC_FAINT, 0);
    if (focus) fb_fill_rect((uint32_t)(x + 11 + (n ? tw(s + st, 12, 0) : 0)), (uint32_t)(y + 7), 1, 16, KC_ACC);
    fb_reset_clip();
}
static void render_ia(const struct sl *L)
{
    struct ial A; ia_load(); ia_lay(L, &A);
    static const char *const prov[4] = { "Local / red", "ChatGPT", "Claude", "Gemini" };
    static const char *const lab[4] = { "Servidor", "Modelo", "Clave", "Lanzador" };
    static const char *const hint[4] = { "ip:puerto o https://servidor", "pulsa Ver modelos o escribe el nombre", "clave de la API", "clave que muestra soma_lanzador.py en el servidor" };
    tx(L->cx, L->y + 18, 30, "Inteligencia artificial", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "El modelo de lenguaje del orbe y los agentes: local o por API.", 12, KC_DIM, 0);
    card(L->cx, A.c1_y, L->cw, A.c1_h);
    tx(L->cx + 20, A.c1_y + 10, 22, "Proveedor", 14, KC_TXT, 1);
    seg(L->cx + 20, A.seg_y, L->seg_w, 4, g_ia.prov, prov);
    for (int k = 0; k < A.nf; k++) {
        tx(L->cx + 20, A.f_y[k], 30, lab[k], 13, KC_DIM, 0);
        if (k >= 2) {
            static char m[LLM_KEY_MAX]; int n = s_len(g_ia_f[k]);
            for (int i = 0; i < n; i++) m[i] = (g_ia_show || (g_ia_focus != k && i >= n - 4 && n > 8)) ? g_ia_f[k][i] : '*';
            m[n] = 0;
            ia_field(A.f_x, A.f_y[k], A.f_w - 52, m, g_ia_focus == k, k == 2 && g_ia.prov == LLM_LOCAL ? "no hace falta (solo si tu servidor la pide)" : hint[k]);
            if (k == 2) tx(A.eye_x + 4, A.f_y[k], 30, g_ia_show ? "ocultar" : "ver", 11, KC_ACC, 0);
        } else ia_field(A.f_x, A.f_y[k], A.f_w, g_ia_f[k], g_ia_focus == k, hint[k]);
    }
    button(A.b1_x, A.btn_y, A.b1_w, g_ia_busy && g_ia_job == 1 ? "Probando..." : "Guardar y probar", 1);
    button(A.b2_x, A.btn_y, A.b2_w, g_ia_busy && g_ia_job == 2 ? "Pidiendo..." : "Ver modelos", 0);
    if (A.nf == 4) button(A.b3_x, A.btn_y, A.b3_w, g_ia_busy && g_ia_job >= 3 ? "Arrancando..." : "Arrancar servidor", 0);   /* SOMA_196 */
    fb_set_clip(L->cx + 20, A.msg_y, L->cw - 40, 40);
    {   /* el mensaje en dos renglones como mucho */
        uint32_t col = g_ia_msg_ok == 1 ? 0x004ADE80 : g_ia_msg_ok == 0 ? 0x00F87171 : KC_DIM;
        static char l1[220]; uint32_t n = 0, cut, best = 0;
        while (n < sizeof l1 - 1 && g_ia_msg[n]) { l1[n] = g_ia_msg[n]; n++; }
        l1[n] = 0; cut = n;
        if (tw(l1, 12, 0) > L->cw - 40) {
            for (uint32_t i = 1; i < n; i++) { if (g_ia_msg[i] != ' ') continue; l1[i] = 0; int fits = tw(l1, 12, 0) <= L->cw - 40; l1[i] = ' '; if (!fits) break; best = i; }
            if (best) { cut = best; l1[cut] = 0; }
        }
        tx(L->cx + 20, A.msg_y, 20, l1, 12, col, 0);
        if (cut < n) tx(L->cx + 20, A.msg_y + 18, 20, g_ia_msg + cut + 1, 12, col, 0);
    }
    fb_reset_clip();
    if (A.c2_h < 70) return;
    card(L->cx, A.c2_y, L->cw, A.c2_h);
    tx(L->cx + 20, A.c2_y + 8, 22, g_ia_listmode && g_ia_nmodels ? "Modelos que el servidor puede arrancar" : "Modelos del servidor", 14, KC_TXT, 1);
    if (!g_ia_nmodels) { tx(L->cx + 20, A.list_y, 24, g_ia.prov == LLM_LOCAL ? "Pulsa Ver modelos para preguntar al servidor cuales tiene." : "Pon la clave y pulsa Ver modelos para ver los de tu cuenta.", 12, KC_FAINT, 0); return; }
    if (g_ia_nmodels > A.rows) {
        char b[40]; b[0] = 0; s_num(b, sizeof b, g_ia_first + 1); s_cat(b, "-", sizeof b); s_num(b, sizeof b, g_ia_first + A.rows < g_ia_nmodels ? g_ia_first + A.rows : g_ia_nmodels); s_cat(b, " de ", sizeof b); s_num(b, sizeof b, g_ia_nmodels);
        tx(A.pg_x - tw(b, 11, 0) - 10, A.c2_y + 8, 22, b, 11, KC_FAINT, 0);
        button(A.pg_x, A.c2_y + 4, 70, "Anterior", 0); button(A.pg_x + 80, A.c2_y + 4, 70, "Siguiente", 0);
    }
    for (int r = 0; r < A.rows && g_ia_first + r < g_ia_nmodels; r++) {
        char one[LLM_MODEL_MAX]; ia_model_at(g_ia_first + r, one, sizeof one);
        int sel = s_eq(one, g_ia_listmode ? g_ia_pick : g_ia_f[1]), ry = A.list_y + r * 24;
        if (sel) fb_fill_rounded_rect(L->cx + 12, ry, L->cw - 24, 22, 6, KC_SELBG);
        fb_set_clip(L->cx + 20, ry, L->cw - 40, 22);
        tx(L->cx + 20, ry, 22, one, 12, sel ? 0x00FFFFFF : KC_TXT, sel);
        fb_reset_clip();
    }
}
static int click_ia(const struct sl *L, int32_t mx, int32_t my)
{
    struct ial A; ia_load(); ia_lay(L, &A);
    int was = g_ia_focus; g_ia_focus = -1;
    if (my >= A.seg_y && my < A.seg_y + 32 && mx >= L->cx + 20 && mx < L->cx + 20 + L->seg_w) {
        if (g_ia_busy) return 1;
        int p = seg_index(L, mx, 4);
        if (p != g_ia.prov) { (void)ia_fields_to(0); g_ia.prov = p; ia_fields_from(); g_ia_nmodels = 0; g_ia_listmode = 0; g_ia_show = 0;
            g_ia_msg[0] = 0; s_cat(g_ia_msg, p == LLM_LOCAL ? "Servidor de tu equipo o de tu red local (llama.cpp, Ollama, LM Studio...). Pulsa Guardar y probar." : "Hace falta la clave de la API de ese proveedor. Lo que preguntes viajara a sus servidores.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
        return 1;
    }
    for (int k = 0; k < A.nf; k++) if (my >= A.f_y[k] && my < A.f_y[k] + 30 && mx >= A.f_x && mx < A.f_x + A.f_w) {
        if (k == 2 && mx >= A.eye_x) { g_ia_show = !g_ia_show; g_ia_focus = was; return 1; }
        g_ia_focus = k; return 1;
    }
    if (my >= A.btn_y && my < A.btn_y + 30) {
        if (mx >= A.b1_x && mx < A.b1_x + A.b1_w) { ia_start(1); return 1; }
        if (mx >= A.b2_x && mx < A.b2_x + A.b2_w) { g_ia_listmode = 0; ia_start(2); return 1; }
        if (A.nf == 4 && mx >= A.b3_x && mx < A.b3_x + A.b3_w) { ia_start(3); return 1; }     /* SOMA_196 */
    }
    if (g_ia_nmodels > A.rows && my >= A.c2_y + 4 && my < A.c2_y + 34) {
        if (mx >= A.pg_x && mx < A.pg_x + 70) { g_ia_first -= A.rows; if (g_ia_first < 0) g_ia_first = 0; return 1; }
        if (mx >= A.pg_x + 80 && mx < A.pg_x + 150) { if (g_ia_first + A.rows < g_ia_nmodels) g_ia_first += A.rows; return 1; }
    }
    if (g_ia_nmodels && A.rows > 0 && my >= A.list_y && mx >= L->cx + 12 && mx < L->cx + L->cw - 12) {
        int r = (my - A.list_y) / 24;
        if (r < A.rows && g_ia_first + r < g_ia_nmodels && g_ia_listmode) {       /* SOMA_196: arrancar ese modelo en el servidor */
            if (g_ia_busy) return 1;
            ia_model_at(g_ia_first + r, g_ia_pick, LLM_MODEL_MAX);
            ia_start(4);
            return 1;
        }
        if (r < A.rows && g_ia_first + r < g_ia_nmodels) {
            ia_model_at(g_ia_first + r, g_ia_f[1], LLM_MODEL_MAX);
            g_ia_msg[0] = 0; s_cat(g_ia_msg, "Modelo elegido: ", sizeof g_ia_msg); s_cat(g_ia_msg, g_ia_f[1], sizeof g_ia_msg); s_cat(g_ia_msg, ". Pulsa Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1;
            return 1;
        }
    }
    return was >= 0;
}
static int key_ia(char c)
{
    if (c == '\t') { g_ia_focus = (g_ia_focus + 1) % ia_nf(); return 1; }
    if (g_ia_focus < 0) return 0;
    char *f = g_ia_f[g_ia_focus]; int n = s_len(f);
    if (c == 8 || c == 127) { if (n) f[n - 1] = 0; return 1; }
    if (c == '\n' || c == '\r') { ia_start(1); return 1; }
    if ((unsigned char)c > 32 && (unsigned char)c < 127 && n < IA_FMAX[g_ia_focus]) { f[n] = c; f[n + 1] = 0; return 1; }
    return 0;
}

/* SOMA_227_IMAGEN: la barra lateral se compacta si la ventana es baja (8 apartados) */
static int sb_step(const struct sl *L) { int st = (L->h - 64) / NITEMS; return st >= 40 ? 40 : (st < 28 ? 28 : st); }
static int sb_y(const struct sl *L, int i) { return L->y + (sb_step(L) < 40 ? 52 : 60) + i * sb_step(L); }
static void render_sidebar(const struct sl *L)
{
    tx(L->x + 18, L->y + 14, 30, "Ajustes", 18, KC_TXT, 1);
    static const char *const items[NITEMS] = { "Apariencia", "Redes", "Fuentes", "IA", "Pantalla", "Iconos", "Voz", "Sonido", "Sistema", "Acerca de" };   /* SOMA_250: Sonido */   /* SOMA_195: IA; SOMA_225: Pantalla e Iconos; SOMA_226: Sistema y Acerca de; SOMA_246: Voz */
    for (int i = 0; i < NITEMS; i++) {
        int st = sb_step(L), ih = st - 6, iy = sb_y(L, i), on = i == g_page;
        if (on) { fb_fill_rounded_rect(L->x + 10, iy, L->side_w - 20, ih, 9, KC_SELBG); fb_fill_rounded_rect(L->x + 10, iy + ih / 2 - 9, 3, 18, 1, KC_ACC); }
        int ix = L->x + 30, icy = iy + ih / 2; uint32_t ic = on ? 0xFFFFFFFFu : 0xFF8592A8u;
        if (i == 0) { fb_stroke_rrect_aa(ix - 7, icy - 7, 14, 14, 7, ic); fb_fill_rrect_aa(ix - 3, icy - 3, 4, 4, 2, ic); }
        else if (i == 1) { for (int k = 0; k < 3; k++) fb_fill_rect((uint32_t)(ix - 6 + k * 5), (uint32_t)(icy + 4 - k * 4), 3, (uint32_t)(4 + k * 4), ic & 0xFFFFFF); }
        else if (i == 2) { tx(ix - 10, iy, 34, "Aa", 13, ic & 0xFFFFFF, 1); }                                              /* SOMA_184: Fuentes */
        else if (i == 3) { fb_stroke_rrect_aa(ix - 8, icy - 7, 16, 12, 4, ic); fb_fill_rrect_aa(ix - 4, icy - 2, 2, 2, 1, ic); fb_fill_rrect_aa(ix + 2, icy - 2, 2, 2, 1, ic); fb_fill_rect((uint32_t)(ix - 5), (uint32_t)(icy + 5), 3, 3, ic & 0xFFFFFF); }   /* IA */
        else if (i == 4) { fb_stroke_rrect_aa(ix - 8, icy - 6, 16, 11, 2, ic); fb_fill_rect((uint32_t)(ix - 3), (uint32_t)(icy + 6), 6, 2, ic & 0xFFFFFF); }
        else if (i == 5) { for (int k = 0; k < 4; k++) fb_stroke_rrect_aa(ix - 8 + (k & 1) * 9, icy - 8 + (k >> 1) * 9, 7, 7, 2, ic); }
        else if (i == PAGE_VOZ) { fb_stroke_rrect_aa(ix - 3, icy - 9, 7, 12, 3, ic); fb_stroke_rrect_aa(ix - 7, icy - 3, 15, 9, 4, ic); fb_fill_rect((uint32_t)ix, (uint32_t)(icy + 6), 1, 4, ic & 0xFFFFFF); }   /* microfono */
        else if (i == PAGE_SONIDO) { fb_fill_rect((uint32_t)(ix - 8), (uint32_t)(icy - 3), 4, 6, ic & 0xFFFFFF); fb_line_aa((ix - 4) * 16, (icy - 3) * 16, (ix + 1) * 16, (icy - 8) * 16, 22, ic); fb_line_aa((ix + 1) * 16, (icy - 8) * 16, (ix + 1) * 16, (icy + 8) * 16, 22, ic); fb_line_aa((ix - 4) * 16, (icy + 3) * 16, (ix + 1) * 16, (icy + 8) * 16, 22, ic); fb_stroke_rrect_aa(ix + 3, icy - 4, 6, 8, 4, ic); }   /* altavoz */
        else if (i == PAGE_SISTEMA) { fb_stroke_rrect_aa(ix - 6, icy - 6, 12, 12, 3, ic); for (int k = 0; k < 3; k++) { fb_fill_rect((uint32_t)(ix - 4 + k * 4), (uint32_t)(icy - 9), 1, 3, ic & 0xFFFFFF); fb_fill_rect((uint32_t)(ix - 4 + k * 4), (uint32_t)(icy + 7), 1, 3, ic & 0xFFFFFF); } }
        else { fb_stroke_rrect_aa(ix - 8, icy - 8, 16, 16, 8, ic); fb_fill_rect((uint32_t)ix - 1, (uint32_t)icy - 1, 2, 6, ic & 0xFFFFFF); fb_fill_rect((uint32_t)ix - 1, (uint32_t)icy - 5, 2, 2, ic & 0xFFFFFF); }
        tx(L->x + 46, iy, ih, items[i], 13, on ? 0x00FFFFFF : KC_DIM, on);
    }
}

/* ================================================================ SOMA_184: pagina Fuentes */
static const int UIFAM[5] = { WF_FAM_SANS, WF_FAM_LIBERATION, WF_FAM_MONO, WF_FAM_ORBITAL, WF_FAM_TECNICA };   /* orden en pantalla -> familia de la interfaz (SOMA_225: + propias) */
static int fam_sel(int fam) { for (int i = 0; i < 5; i++) if (UIFAM[i] == fam) return i; return 0; }
static const int EDF[5] = { FFACE_MONO, FFACE_DEJAVU, FFACE_LIBERATION, FFACE_ORBITAL, FFACE_TECNICA };   /* editor */
static int ed_sel(int f) { for (int i = 0; i < 5; i++) if (EDF[i] == f) return i; return 0; }
static void render_reset(const struct sl *L, int ry)
{
    fb_fill_rounded_rect(L->reset_x, ry, 130, 32, 16, 0x00182133);
    fb_stroke_rrect_aa(L->reset_x, ry, 130, 32, 16, 0xFF263142u);
    tx(L->reset_x + (130 - tw("Restablecer", 13, 1)) / 2, ry, 32, "Restablecer", 13, KC_TXT, 1);
    cbot(ry + 32);
    fb_set_clip(L->cx, ry, L->reset_x - L->cx - 10, 32);
    tx(L->cx, ry, 32, g_msg, 11, KC_FAINT, 0);
    fb_reset_clip();
}
/* SOMA_250_TTS: las tres fuentes se eligen en listas desplegables (el catalogo del escritorio tiene decenas) */
static const char *const FAM_NAME[5] = { "DejaVu Sans", "Liberation Sans", "DejaVu Sans Mono", "SOMA Orbital", "SOMA T\xC3\xA9" "cnica" };
static const char *const ED_NAME[5] = { "DejaVu Sans Mono", "DejaVu Sans", "Liberation Sans", "SOMA Orbital", "SOMA T\xC3\xA9" "cnica" };
static const char *lab_desk(int i, char *b, int max)
{
    int id = font_get_sorted_id(i); const char *n = font_get_name(id), *d = font_get_desc(id);
    b[0] = 0; s_cat(b, n ? n : "?", max);
    if (d && d[0]) { s_cat(b, "  \xC2\xB7  ", max); s_cat(b, d, max); }
    return b;
}
static const char *lab_fam(int i, char *b, int max) { (void)b; (void)max; return FAM_NAME[i]; }
static const char *lab_ed(int i, char *b, int max) { (void)b; (void)max; return ED_NAME[i]; }
static int desk_sel(void) { int cur = font_get_current(); for (int i = 0; i < font_get_count(); i++) if (font_get_sorted_id(i) == cur) return i; return -1; }
static void render_fuentes(const struct sl *L)
{
    char b[120];
    tx(L->cx, L->y + 18, 30, "Fuentes", 20, KC_TXT, 1);
    fb_set_clip(L->cx, L->y + 46, L->cw, 22);
    tx(L->cx, L->y + 46, 22, "Escritorio, aplicaciones, documentos y editor.", 12, KC_DIM, 0);
    fb_reset_clip();
    if (font_sysfont_off()) {
        card(L->cx, L->f1_y, L->cw, 70);
        tx(L->cx + 20, L->f1_y + 14, 22, "Catalogo ampliado desactivado", 14, KC_TXT, 1);
        tx(L->cx + 20, L->f1_y + 36, 18, "soma.sysfont=off (o modo seguro): se usan las fuentes de siempre.", 11, KC_DIM, 0);
        return;
    }
    /* escritorio */
    static const char *const dsk[6] = { "DejaVu Mono", "DejaVu Sans", "Liberation", "Lib. negr.", "Orbital", "Tecnica" };
    card(L->cx, L->f1_y, L->cw, L->f1_h);
    tx(L->cx + 20, L->f1_y + 14, 22, "Fuente del escritorio", 14, KC_TXT, 1);
    b[0] = 0; s_cat(b, "R\xC3\xB3tulos, iconos y ventanas: ", sizeof b); s_num(b, sizeof b, font_get_count()); s_cat(b, " fuentes.", sizeof b);
    fb_set_clip(L->cx + 20, L->f1_y + 34, L->cw - 40, 18);
    tx(L->cx + 20, L->f1_y + 34, 18, b, 11, KC_DIM, 0);
    fb_reset_clip();
    (void)dsk;
    dd(DD_DESK, L->cx + 20, L->f1_seg, L->seg_w, font_get_count(), desk_sel(), lab_desk);
    fb_fill_rounded_rect(L->cx + 20, L->f1_demo, L->seg_w, 26, 8, 0x00090D15);
    fb_set_clip(L->cx + 20, L->f1_demo, L->seg_w, 26);
    font_draw_string((uint32_t)(L->cx + 32), (uint32_t)(L->f1_demo + 8), "El veloz murcielago hindu comia kiwi. 0123", KC_TXT, 1);
    fb_reset_clip();
    /* interfaz y documentos */
    static const char *const fam[5] = { "DejaVu Sans", "Liberation", "DejaVu Mono", "SOMA Orbital", "SOMA Tecnica" };
    card(L->cx, L->f2_y, L->cw, L->f2_h);
    tx(L->cx + 20, L->f2_y + 14, 22, "Interfaz y documentos", 14, KC_TXT, 1);
    fb_set_clip(L->cx + 20, L->f2_y + 34, L->cw - 40, 18);
    tx(L->cx + 20, L->f2_y + 34, 18, "Texto de las aplicaciones y de los documentos (no de las p\xC3\xA1ginas web).", 11, KC_DIM, 0);
    fb_reset_clip();
    (void)fam;
    dd(DD_UIFAM, L->cx + 20, L->fam_y, L->seg_w, 5, fam_sel(wf_ui_family()), lab_fam);
    int si = 1; for (int i = 0; i < 4; i++) if (SCALES[i] == wf_ui_scale()) si = i;
    seg(L->cx + 20, L->sz_y, L->seg_w, 4, si, SCALE_NAME);
    fb_fill_rounded_rect(L->cx + 20, L->demo_y, L->seg_w, 40, 10, 0x00090D15);
    fb_set_clip(L->cx + 20, L->demo_y, L->seg_w, 40);
    tx(L->cx + 34, L->demo_y, 40, "El veloz murcielago hindu comia kiwi. 0123", 14, KC_TXT, 0);
    fb_reset_clip();
    /* editor de texto */
    static const char *const ed[5] = { "DejaVu Mono", "DejaVu Sans", "Liberation", "SOMA Orbital", "SOMA Tecnica" };
    card(L->cx, L->f3_y, L->cw, L->f3_h);
    tx(L->cx + 20, L->f3_y + 14, 22, "Editor de texto (Notas)", 14, KC_TXT, 1);
    fb_set_clip(L->cx + 20, L->f3_y + 34, L->cw - 40, 18);
    tx(L->cx + 20, L->f3_y + 34, 18, "Letra de lo que escribes (el terminal usa DejaVu Sans Mono).", 11, KC_DIM, 0);
    fb_reset_clip();
    (void)ed;
    dd(DD_EDITOR, L->cx + 20, L->ed_y, L->seg_w, 5, ed_sel(g_editor_face), lab_ed);
    render_reset(L, L->reset2_y);
}


/* ================================================================ SOMA_225_AJUSTES: paginas Pantalla e Iconos */
#define PAGE_PANTALLA 4
#define PAGE_ICONOS   5
/* SOMA_232_GEOMETRIA: cada pagina calcula su geometria UNA vez; el dibujo y el clic usan la misma */
struct pant_lay { int y1, h1, seg1_y, y2, h2, seg2_y, reset_y; };
static void pant_lay(const struct sl *L, struct pant_lay *P)
{
    P->y1 = L->y + 84; P->h1 = 150; P->seg1_y = P->y1 + 62;              /* resolucion */
    P->y2 = P->y1 + P->h1 + 12; P->h2 = 130; P->seg2_y = P->y2 + 62;    /* velocidad del puntero */
    P->reset_y = P->y2 + P->h2 + 14;
}
static void render_pantalla(const struct sl *L)
{
    char b[140];
    tx(L->cx, L->y + 18, 30, "Pantalla y raton", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Resolucion de trabajo y velocidad del puntero. Se aplican al momento y se guardan.", 12, KC_DIM, 0);
    struct pant_lay P; pant_lay(L, &P);
    int y1 = P.y1, h1 = P.h1;
    card(L->cx, y1, L->cw, h1);
    tx(L->cx + 20, y1 + 14, 22, "Resolucion", 14, KC_TXT, 1);
    b[0] = 0; s_cat(b, "Pantalla: ", sizeof b); s_num(b, sizeof b, (int)fb_hw_width()); s_cat(b, " x ", sizeof b); s_num(b, sizeof b, (int)fb_hw_height());
    s_cat(b, ". Menos resolucion = todo mas grande y menos trabajo para el procesador.", sizeof b);
    tx(L->cx + 20, y1 + 34, 18, b, 11, KC_DIM, 0);
    int n = res_count(); char lab[4][24]; const char *lp[4];
    for (int k = 0; k < n; k++) { uint32_t w, h; res_of(k, &w, &h); lab[k][0] = 0; s_num(lab[k], 24, (int)w); s_cat(lab[k], "x", 24); s_num(lab[k], 24, (int)h); lp[k] = lab[k]; }
    seg(L->cx + 20, P.seg1_y, L->seg_w, n, g_res < n ? g_res : 0, lp);
    b[0] = 0; s_cat(b, "Ahora: ", sizeof b); s_num(b, sizeof b, (int)fb_get_width()); s_cat(b, " x ", sizeof b); s_num(b, sizeof b, (int)fb_get_height());
    s_cat(b, fb_scaled() ? "  (escalada con suavizado)" : "  (nativa, pixel a pixel)", sizeof b);
    tx(L->cx + 20, y1 + 104, 28, b, 12, KC_TXT, 0);
    int y2 = P.y2, h2 = P.h2;
    card(L->cx, y2, L->cw, h2);
    tx(L->cx + 20, y2 + 14, 22, "Velocidad del puntero", 14, KC_TXT, 1);
    tx(L->cx + 20, y2 + 34, 18, "Raton, trackpad y raton USB. Los movimientos lentos se conservan (no se pierden fracciones).", 11, KC_DIM, 0);
    int si = 2; for (int k = 0; k < 6; k++) if (SPEEDS[k] == g_speed) si = k;
    seg(L->cx + 20, P.seg2_y, L->seg_w, 6, si, SPEED_NAME);
    b[0] = 0; s_num(b, sizeof b, g_speed); s_cat(b, " % de la velocidad normal", sizeof b);
    tx(L->cx + 20, y2 + 98, 24, b, 12, KC_DIM, 0);
    render_reset(L, P.reset_y);
}
static int click_pantalla(const struct sl *L, int32_t mx, int32_t my)
{
    struct pant_lay P; pant_lay(L, &P);
    if (mx >= L->reset_x && mx < L->reset_x + 130 && my >= P.reset_y && my < P.reset_y + 32) {
        g_res = 0; g_speed = mouse_set_speed(100); res_apply(); save();
        g_msg[0] = 0; s_cat(g_msg, "Resolucion nativa y velocidad normal", sizeof g_msg); return 1;
    }
    if (mx < L->cx + 20 || mx >= L->cx + 20 + L->seg_w) return 0;
    if (my >= P.seg1_y && my < P.seg1_y + 32) {
        int k = seg_index(L, mx, res_count());
        if (k != g_res) { g_res = k; res_apply(); save(); g_msg[0] = 0; s_cat(g_msg, "Resolucion cambiada", sizeof g_msg); }
        return 1;
    }
    if (my >= P.seg2_y && my < P.seg2_y + 32) { g_speed = mouse_set_speed(SPEEDS[seg_index(L, mx, 6)]); save(); return 1; }
    return 0;
}
static const char *const SI_NAMES[SI_COUNT] = { "", "Grabar", "Detener", "Reproducir", "Pausa", "Microfono", "Mic. apagado", "Camara",
    "Cam. apagada", "Video", "Guardar", "Nuevo", "Papelera", "Nivel", "Pantalla", "Altavoz", "Pantalla y audio", "Temporizador",
    "Escala", "Indicador", "Carpeta", "Anterior", "Siguiente", "Automatico", "Apagar", "Menos", "Mas", "Imagen", "Onda", "Aceptar",
    "Disparador", "Reiniciar",
    "Musica", "Pelicula", "Retroceder", "Avanzar", "Pista anterior", "Pista siguiente", "Lista", "Codigo", "Deshacer", "Rehacer",
    "Buscar", "Reemplazar", "Ir a la linea", "Comentario", "Documento", "Codigo fuente", "Disco", "Editar", "Cuadricula", "Copiar",
    "Cerrar", "Sin sonido", "Repetir", "Terminal" };   /* SOMA_240_ICONOS */
static const char *const VI_NAMES[VICON_COUNT] = { "Agente", "Agentes", "Terminal", "Navegador", "Archivos", "Imagenes", "Notas", "Paint",
    "Maquetacion", "Memoria" };
static int icon_tabs_y(const struct sl *L) { return L->y + 76; }   /* pestanas "Del sistema" / "De las aplicaciones" */
static void render_iconos(const struct sl *L)
{
    char b[96];
    tx(L->cx, L->y + 18, 30, "Iconos del sistema", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Vectoriales: se dibujan a cualquier tamano. Las aplicaciones los usan con su nombre.", 12, KC_DIM, 0);
    static const char *const tabs[2] = { "Del sistema", "De las aplicaciones" };
    seg(L->cx, icon_tabs_y(L), L->cw, 2, g_icon_tab, tabs);
    int n = g_icon_tab ? VICON_COUNT : SI_COUNT - 1;
    int cols = L->cw / 84; if (cols < 3) cols = 3;
    int cw = L->cw / cols, ch = 66, y0 = L->y + 118;
    for (int i = 0; i < n; i++) {                       /* SOMA_227_IMAGEN: todas las filas (la pagina se desplaza) */
        int r = i / cols, c = i % cols;
        cbot(y0 + (r + 1) * ch + 30);
        int x = L->cx + c * cw, y = y0 + r * ch;
        fb_fill_rrect_aa(x + 4, y + 4, cw - 8, ch - 8, 10, 0xFF000000u | KC_CARD);
        if (g_icon_tab) vicon_draw(i, x + cw / 2 - 12, y + 10, 24, 0x00E5EAF2u);
        else si_icon(i + 1, x + cw / 2, y + 24, 26, i + 1 == SI_POWER || i + 1 == SI_REC ? 0x00FB7185u : 0x00E5EAF2u);
        const char *nm = g_icon_tab ? VI_NAMES[i] : SI_NAMES[i + 1];
        int w = tw(nm, 10, 0); if (w > cw - 12) w = cw - 12;
        fb_set_clip(x + 6, y + 40, cw - 12, 20);
        tx(x + (cw - w) / 2, y + 40, 20, nm, 10, KC_DIM, 0);
        fb_reset_clip();
    }
    b[0] = 0; s_num(b, sizeof b, n); s_cat(b, g_icon_tab ? " iconos de aplicaciones (vicon_draw)" : " iconos del sistema (si_icon, sysicon.h)", sizeof b);
    tx(L->cx, y0 + ((n + cols - 1) / cols) * ch + 4, 24, b, 11, KC_FAINT, 0);
}
static int click_iconos(const struct sl *L, int32_t mx, int32_t my)
{
    if (my >= icon_tabs_y(L) && my < icon_tabs_y(L) + 32 && mx >= L->cx && mx < L->cx + L->cw) {
        int k = (mx - L->cx) * 2 / L->cw; if (k != g_icon_tab) { g_icon_tab = k; return 1; }
    }
    return 0;
}

/* ================================================================ SOMA_226_SISTEMA: paginas Sistema y Acerca de */
static struct hw_cpu g_hcpu; static struct hw_gpu g_hgpu; static struct hw_board g_hbrd; static int g_hw_ok;
static void hw_load(void) { if (!g_hw_ok) { hwmon_cpu(&g_hcpu); hwmon_gpu(&g_hgpu); hwmon_board(&g_hbrd); g_hw_ok = 1; } }
static void kvrow(int x, int y, int w, const char *k, const char *v)
{
    tx(x, y, 26, k, 12, KC_DIM, 0);
    int vw = tw(v, 12, 0), vx = x + w - vw; if (vx < x + 150) vx = x + 150;
    fb_set_clip(x + 150, y, w - 150, 26); tx(vx, y, 26, v, 12, KC_TXT, 0); fb_reset_clip();
    fb_fill_rect((uint32_t)x, (uint32_t)(y + 27), (uint32_t)w, 1, 0x001A2333);
}
/* texto en varias lineas dentro de un ancho; devuelve la y siguiente */
static int tx_wrap(int x, int y, int w, const char *s, int px, uint32_t col, int lh)
{
    char line[160]; int n = 0, last_sp = -1; const char *p = s;
    line[0] = 0;
    while (*p) {
        if (n < (int)sizeof line - 1) { line[n++] = *p; line[n] = 0; }
        if (*p == ' ') last_sp = n - 1;
        if (tw(line, px, 0) > w && last_sp > 0) {
            char rest[160]; int rn = 0; for (int i = last_sp + 1; i < n; i++) rest[rn++] = line[i]; rest[rn] = 0;
            line[last_sp] = 0; tx(x, y, lh, line, px, col, 0); y += lh;
            for (int i = 0; i <= rn; i++) line[i] = rest[i];
            n = rn; last_sp = -1;
        }
        p++;
    }
    if (n) { tx(x, y, lh, line, px, col, 0); y += lh; }
    return y;
}
struct sis_lay { int y1, h1, y2, h2, y3, h3, btn_y, reset_w, rep_x, rep_w, msg_y; };
static void sis_lay(const struct sl *L, struct sis_lay *S)
{
    S->y1 = L->y + 84; S->h1 = 4 * 30 + 50;                            /* SOMA */
    S->y2 = S->y1 + S->h1 + 12; S->h2 = 5 * 30 + 50;                   /* Equipo */
    S->y3 = S->h3 = 0; S->rep_x = S->rep_w = 0; S->reset_w = 170;
    S->btn_y = S->y2 + S->h2 + 14;
#ifdef __x86_64__
    S->y3 = S->y2 + S->h2 + 12; S->h3 = 4 * 30 + 50;                   /* Arranque y diagnostico (SOMA_228) */
    S->btn_y = S->y3 + S->h3 + 14;
    S->rep_w = 170; S->rep_x = L->cx + L->cw - S->reset_w - 10 - S->rep_w;   /* "Guardar informe" */
#endif
    S->msg_y = S->rep_w ? S->btn_y + 40 : S->btn_y;                    /* con dos botones, el aviso va debajo */
}
static void render_sistema(const struct sl *L)
{
    char v[96];
    hw_load();
    tx(L->cx, L->y + 18, 30, "Sistema", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Version de SOMA y equipo en el que funciona. El detalle esta en el Monitor del sistema.", 12, KC_DIM, 0);
    struct sis_lay S; sis_lay(L, &S);
    int y1 = S.y1, h1 = S.h1;
    card(L->cx, y1, L->cw, h1);
    tx(L->cx + 20, y1 + 12, 24, "SOMA", 14, KC_TXT, 1);
    int kx = L->cx + 20, kw = L->cw - 40, ky = y1 + 44;
    kvrow(kx, ky, kw, "Version", "0.3 pre-beta"); ky += 30;
    v[0] = 0; s_cat(v, g_hcpu.arch[0] ? g_hcpu.arch : "-", sizeof v); s_cat(v, "  (compilado el " __DATE__ ")", sizeof v); kvrow(kx, ky, kw, "Arquitectura", v); ky += 30;
    { uint64_t sec = timer_get_uptime_ms() / 1000; v[0] = 0; s_num(v, sizeof v, (int)(sec / 3600)); s_cat(v, " h ", sizeof v); s_num(v, sizeof v, (int)(sec / 60 % 60)); s_cat(v, " min", sizeof v); kvrow(kx, ky, kw, "Encendido", v); ky += 30; }
    { int p = vfs_big_persist(); kvrow(kx, ky, kw, "Almacenamiento", p > 0 ? "En disco (se conserva al apagar)" : p == 0 ? "En disco (archivos grandes en memoria)" : "Solo en memoria"); }
    int y2 = S.y2, h2 = S.h2;
    card(L->cx, y2, L->cw, h2);
    tx(L->cx + 20, y2 + 12, 24, "Equipo", 14, KC_TXT, 1);
    ky = y2 + 44;
    kvrow(kx, ky, kw, "Modelo", g_hbrd.model[0] ? g_hbrd.model : "-"); ky += 30;
    kvrow(kx, ky, kw, "Procesador", g_hcpu.brand[0] ? g_hcpu.brand : "-"); ky += 30;
    kvrow(kx, ky, kw, "Graficos", g_hgpu.name[0] ? g_hgpu.name : "-"); ky += 30;
    v[0] = 0; s_num(v, sizeof v, (int)g_hbrd.ram_mb); s_cat(v, " MiB", sizeof v); kvrow(kx, ky, kw, "Memoria", v); ky += 30;
    v[0] = 0; s_num(v, sizeof v, (int)fb_hw_width()); s_cat(v, " x ", sizeof v); s_num(v, sizeof v, (int)fb_hw_height());
    if (fb_scaled()) { s_cat(v, "  (trabajo ", sizeof v); s_num(v, sizeof v, (int)fb_get_width()); s_cat(v, " x ", sizeof v); s_num(v, sizeof v, (int)fb_get_height()); s_cat(v, ")", sizeof v); }
    kvrow(kx, ky, kw, "Pantalla", v);
    int ry = S.btn_y, bw = S.reset_w, bw2 = S.rep_w;
#ifdef __x86_64__
    {   /* SOMA_228_ARRANQUE: arranque, SOMADATA e informe de hardware */
        int y3 = S.y3, h3 = S.h3;
        card(L->cx, y3, L->cw, h3);
        tx(L->cx + 20, y3 + 12, 24, "Arranque y diagnostico", 14, KC_TXT, 1);
        ky = y3 + 44;
        if (bootdiag_in_boot()) kvrow(kx, ky, kw, "Este arranque", "en curso");
        else { uint32_t ms = bootdiag_boot_ms(); v[0] = 0; s_cat(v, "completo en ", sizeof v); s_num(v, sizeof v, (int)(ms / 1000)); s_cat(v, ",", sizeof v); s_num(v, sizeof v, (int)(ms % 1000 / 100)); s_cat(v, " s", sizeof v); kvrow(kx, ky, kw, "Este arranque", v); }
        ky += 30;
        { const char *pf = bootdiag_prev_failure(); kvrow(kx, ky, kw, "Arranque anterior", pf[0] ? pf : "termino bien"); ky += 30; }
        {
            const struct usbdisk_info *u = usbdisk_info();
            v[0] = 0;
            if (u->active) {
                s_num(v, sizeof v, (int)(u->sectors / 2048)); s_cat(v, u->in_file ? " MiB en SOMADATA.IMG del pendrive" : " MiB en el pendrive", sizeof v);
                if (u->grown_to_mb) { s_cat(v, " (ampliada desde ", sizeof v); s_num(v, sizeof v, (int)u->grown_from_mb); s_cat(v, " MiB)", sizeof v); }
            } else if (sysdisk_capacity()) s_cat(v, "no hace falta (hay otro disco del sistema)", sizeof v);
            else { s_cat(v, "NO SE GUARDA NADA: ", sizeof v); s_cat(v, u->why && u->why[0] ? u->why : "sin disco", sizeof v); }   /* SOMA_238 */
            kvrow(kx, ky, kw, "SOMADATA", v); ky += 30;
        }
        kvrow(kx, ky, kw, "Informe", bootdiag_report_status());
        int bx = S.rep_x;
        fb_fill_rounded_rect(bx, ry, bw2, 32, 16, 0x00182133);
        fb_stroke_rrect_aa(bx, ry, bw2, 32, 16, 0xFF2B6CB0u);
        tx(bx + (bw2 - tw("Guardar informe", 13, 1)) / 2, ry, 32, "Guardar informe", 13, 0x0093C5FD, 1);
    }
#endif
    fb_fill_rounded_rect(L->cx + L->cw - bw, ry, bw, 32, 16, 0x00182133);
    fb_stroke_rrect_aa(L->cx + L->cw - bw, ry, bw, 32, 16, 0xFF7F1D1Du);
    tx(L->cx + L->cw - bw + (bw - tw("Restablecer todo", 13, 1)) / 2, ry, 32, "Restablecer todo", 13, 0x00FCA5A5, 1);
    int my_ = S.msg_y;
    cbot(my_ + 32);
    fb_set_clip(L->cx, my_, bw2 ? L->cw : L->cw - bw - 10, 32);
    tx(bw2 ? L->cx + L->cw - tw(g_msg, 11, 0) : L->cx, my_, 32, g_msg, 11, KC_FAINT, 0);
    fb_reset_clip();
}
void gui_desktop_layout_reset(void);
static int click_sistema(const struct sl *L, int32_t mx, int32_t my)
{
    struct sis_lay S; sis_lay(L, &S);
    int ry = S.btn_y, bw = S.reset_w;
#ifdef __x86_64__
    {   /* SOMA_228_ARRANQUE: boton "Guardar informe" */
        if (mx >= S.rep_x && mx < S.rep_x + S.rep_w && my >= ry && my < ry + 32) {
            bootdiag_report_save_async();
            g_msg[0] = 0; s_cat(g_msg, "Generando el informe... (resultado en la fila Informe)", sizeof g_msg);
            return 1;
        }
    }
#endif
    if (mx < L->cx + L->cw - bw || mx >= L->cx + L->cw || my < ry || my >= ry + 32) return 0;
    g_opacity = 100; g_active_opaque = 0; wf_ui_set_family(WF_FAM_SANS, 100);
    g_editor_face = FFACE_MONO; g_desk[0] = 0; (void)font_set(FONT_VECTOR);
    g_res = 0; res_apply(); g_speed = mouse_set_speed(100);
    gui_desktop_layout_reset();
    save();
    g_msg[0] = 0; s_cat(g_msg, "Todo restablecido: apariencia, fuentes, pantalla, raton y escritorio", sizeof g_msg);
    return 1;
}
static int32_t her_sin1024(int a)                         /* seno * 1024, a en 1/1024 de vuelta (aproximacion de Bhaskara) */
{
    a &= 1023; int neg = a >= 512; int64_t u = a & 511;
    int64_t num = 16 * u * (512 - u) * 1024, den = 5LL * 512 * 512 - 4 * u * (512 - u);
    int32_t v = (int32_t)(num / den);
    return neg ? -v : v;
}
static int32_t her_cos1024(int a) { return her_sin1024(a + 256); }
static void about_logo(int cx, int cy, int r)
{
    fb_fill_rrect_aa(cx - r - 10, cy - r - 10, 2 * r + 20, 2 * r + 20, r + 10, 0x30388BFDu);
    for (int ring = 0; ring < 3; ring++) {                              /* tres orbitas inclinadas, punto a punto */
        static const int TILT[3] = { 0, 60, -50 }; static const uint32_t COL[3] = { 0x0067E8F9u, 0x00A78BFAu, 0x00E5EAF2u };
        int ry = r * (ring == 2 ? 45 : 32) / 100;
        for (int k = 0; k < 220; k++) {
            int a = k * 1024 / 220;
            int32_t ex = her_cos1024(a) * r / 1024, ey = her_sin1024(a) * ry / 1024;
            int t = TILT[ring] * 1024 / 360;
            int32_t x = (ex * her_cos1024(t) - ey * her_sin1024(t)) / 1024, y = (ex * her_sin1024(t) + ey * her_cos1024(t)) / 1024;
            fb_fill_rrect_aa(cx + x - 1, cy + y - 1, 3, 3, 1, 0xC0000000u | COL[ring]);
        }
    }
    fb_fill_rrect_aa(cx - r / 4, cy - r / 4, r / 2, r / 2, r / 4, 0xFF000000u);
    fb_stroke_rrect_aa(cx - r / 4, cy - r / 4, r / 2, r / 2, r / 4, 0xFFE5EAF2u);
}
static void render_acerca(const struct sl *L)
{
    int lx = L->cx + 70;
    about_logo(L->cx + 34, L->y + 52, 32);
    tx(lx + 20, L->y + 22, 34, "SOMA", 26, KC_TXT, 1);
    tx(lx + 20, L->y + 58, 22, "Sistema operativo para agentes de IA  -  version 0.3 pre-beta", 12, KC_DIM, 0);
    int y1 = L->y + 100, h1 = 4 * 30 + 78;
    card(L->cx, y1, L->cw, h1);
    tx(L->cx + 20, y1 + 12, 22, "Dise\xC3\xB1o y desarrollo", 11, KC_ACC, 1);
    tx(L->cx + 20, y1 + 32, 28, "Marcos Luis \xC3\x81lvarez Alejandre", 16, KC_TXT, 1);
    int kx = L->cx + 20, kw = L->cw - 40, ky = y1 + 68;
    kvrow(kx, ky, kw, "Correo", "mlalejandre@gmail.com"); ky += 30;
    kvrow(kx, ky, kw, "YouTube", "youtube.com/@mentedevonneumann"); ky += 30;
    kvrow(kx, ky, kw, "GitHub", "github.com/mlalejandre/x86-soma"); ky += 30;
    kvrow(kx, ky, kw, "Licencia", "MIT");
    int y2 = y1 + h1 + 12, h2 = 112;
    card(L->cx, y2, L->cw, h2);
    tx(L->cx + 20, y2 + 12, 22, "Licencia MIT", 13, KC_TXT, 1);
    tx_wrap(L->cx + 20, y2 + 38, L->cw - 40, "Software libre: puedes usarlo, copiarlo, modificarlo, fusionarlo, publicarlo, distribuirlo y venderlo, siempre que conserves el aviso de copyright y el texto de la licencia (archivo LICENSE). Se ofrece tal cual, sin garantias.", 11, KC_DIM, 18);
    int y3 = y2 + h2 + 12, h3 = 96;
    {
        card(L->cx, y3, L->cw, h3);
        tx(L->cx + 20, y3 + 12, 22, "Componentes de terceros", 13, KC_TXT, 1);
        tx_wrap(L->cx + 20, y3 + 38, L->cw - 40, "uACPI (MIT), whisper.cpp y ggml (MIT, voz a texto), voces de Piper (las instala el usuario; cada voz tiene su licencia), fuentes DejaVu (licencia libre Bitstream Vera) y Liberation Sans (SIL OFL 1.1), firmware WiFi de Intel (licencia de redistribucion de Intel). SOMA Orbital y SOMA Tecnica son propias (MIT).", 11, KC_DIM, 18);
    }
}
static void render_apariencia(const struct sl *L)
{
    char b[96];
    /* cabecera */
    tx(L->cx, L->y + 18, 30, "Apariencia", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Transparencia de las ventanas (las fuentes estan en la pagina Fuentes)", 12, KC_DIM, 0);

    /* tarjeta 1: transparencia */
    card(L->cx, L->c1_y, L->cw, L->c1_h);
    tx(L->cx + 20, L->c1_y + 14, 22, "Transparencia de las ventanas", 14, KC_TXT, 1);
    tx(L->cx + 20, L->c1_y + 34, 18, "100 % = opacas. Se ve el escritorio a traves de las ventanas.", 11, KC_DIM, 0);
    {
        int ty = L->sl_y + 10, kx = L->sl_x + (g_opacity - 40) * (L->sl_w - 1) / 60;
        fb_fill_rounded_rect(L->sl_x, ty - 3, L->sl_w, 6, 3, 0x00263142);
        fb_fill_rounded_rect(L->sl_x, ty - 3, kx - L->sl_x + 1, 6, 3, KC_PRIMARY);
        fb_fill_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0xFFFFFFFFu);
        fb_stroke_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0x40000000u);
        b[0] = 0; s_num(b, sizeof b, g_opacity); s_cat(b, " %", sizeof b);
        tx(L->sl_x + L->sl_w + 18, L->sl_y, 20, b, 15, KC_TXT, 1);
    }
    tx(L->cx + 20, L->tg_y, 24, "Mantener opaca la ventana activa", 13, KC_TXT, 0);
    {
        int on = g_active_opaque;
        fb_fill_rrect_aa(L->tg_x, L->tg_y, 44, 24, 12, on ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
        fb_fill_rrect_aa(L->tg_x + (on ? 22 : 2), L->tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    }
    /* vista previa: "escritorio" con una ventana translucida encima */
    {
        int px = L->cx + 20, pw = L->cw - 40, ph = 38, py = L->prev_y;
        for (int i = 0; i < pw; i += 2) { uint32_t c = 0x001E3A8Au + (uint32_t)(i * 60 / pw) * 0x00010100u; fb_fill_rect((uint32_t)(px + i), (uint32_t)py, 2, (uint32_t)ph, c); }
        for (int k = 0; k < 6; k++) fb_fill_rrect_aa(px + 12 + k * (pw / 6), py + 8, 22, 22, 11, 0xFF000000u | (0x00F59E0Bu + (uint32_t)k * 0x00101030u));
        uint32_t a = (uint32_t)(g_opacity * 255 / 100);
        fb_fill_rect_alpha(px + pw / 4, py + 4, pw / 2, ph - 8, (a << 24) | KC_WIN);
        tx(px + pw / 4 + 12, py + 4, ph - 8, "Ventana de ejemplo", 12, KC_TXT, 0);
    }

    /* pie */
    render_reset(L, L->reset_y);
}
/* ================================================================ SOMA_246_VOZ_MODELOS: pagina Voz
 * Modelo de reconocimiento del dictado (Alt) y de la Grabadora: Rapido (base), Preciso (small) o Maxima precision
 * (large-v3-turbo). El estado (instalado / en el pendrive) se consulta en un hilo: leer el pendrive puede tardar. */
#ifdef __x86_64__
static volatile int g_vz_inst[VOZ_TIERS], g_vz_stick[VOZ_TIERS], g_vz_ver, g_vz_busy, g_vz_job;   /* job: 0 consultar, 1+t instalar t */
static uint32_t g_vz_stick_mb[VOZ_TIERS];
static char g_vz_used[96], g_vz_msg[200]; static volatile int g_vz_msg_ok;
static int g_vz_seen = -1, g_vz_last_prog = -2;
/* SOMA_250_TTS: voces instaladas (las lee el hilo de la pagina) */
static char g_vc_path[VOZ_VOICES_MAX][96], g_vc_label[VOZ_VOICES_MAX][80]; static int g_vc_spk[VOZ_VOICES_MAX]; static volatile int g_vc_n;
static char g_vc_msg[220]; static volatile int g_vc_msg_ok = -1;
static uint32_t g_vc_speak_seen;
static void vc_scan(void)
{
    static char paths[VOZ_VOICES_MAX][96];
    int n = voz_voice_list(paths, 0, VOZ_VOICES_MAX);
    for (int i = 0; i < n; i++) {
        g_vc_path[i][0] = 0; s_cat(g_vc_path[i], paths[i], 96);
        voz_voice_label(paths[i], g_vc_label[i], 80);
        g_vc_spk[i] = voz_voice_speakers(paths[i]);
    }
    g_vc_n = n;
}
static void vz_scan(void)
{
    vc_scan();
    for (int t = 0; t < VOZ_TIERS; t++) {
        char m[96]; uint32_t sz = 0;
        g_vz_inst[t] = voz_installed(t, m, sizeof m, 0) == 0;
        g_vz_stick[t] = voz_on_stick(t, 0, 0, &sz) == 0; g_vz_stick_mb[t] = sz >> 20;
    }
    int tier = -1; char m[96]; uint64_t sz = 0;
    g_vz_used[0] = 0;
    if (voz_model_tier(m, sizeof m, &sz, &tier) == 0) { s_cat(g_vz_used, m, sizeof g_vz_used); s_cat(g_vz_used, "  (", sizeof g_vz_used); s_num(g_vz_used, sizeof g_vz_used, (int)(sz >> 20)); s_cat(g_vz_used, " MiB)", sizeof g_vz_used); }
}
static void vz_worker(void *arg)
{
    (void)arg;
    if (g_vz_job == 100) {                                             /* SOMA_250_TTS: instalar las voces del pendrive */
        static char msg[256];
        int r = voz_voice_install(msg, sizeof msg);
        g_vc_msg[0] = 0; s_cat(g_vc_msg, r == 0 ? "Listo: " : "No se pudo: ", sizeof g_vc_msg); s_cat(g_vc_msg, msg, sizeof g_vc_msg); g_vc_msg_ok = r == 0;
    } else if (g_vz_job > 0) {
        int t = g_vz_job - 1; static char msg[256];
        int r = voz_install_tier(t, msg, sizeof msg);
        g_vz_msg[0] = 0; s_cat(g_vz_msg, r == 0 ? "Listo: " : "No se pudo instalar: ", sizeof g_vz_msg); s_cat(g_vz_msg, msg, sizeof g_vz_msg); g_vz_msg_ok = r == 0;
    }
    vz_scan();
    g_vz_busy = 0; g_vz_ver++;
}
static void vz_start(int job)
{
    if (g_vz_busy) return;
    g_vz_job = job; g_vz_busy = 1;
    if (!thread_create("ajustes_voz", vz_worker, 0)) { g_vz_busy = 0; g_vz_msg[0] = 0; s_cat(g_vz_msg, "No se pudo crear el hilo", sizeof g_vz_msg); g_vz_msg_ok = 0; }
}
static void voz_page_open(void) { if (!g_vz_busy) { if (g_vz_job == 0) g_vz_msg[0] = 0; vz_start(0); } }
static int voz_page_tick(void)
{
    int ch = 0;
    if (g_vz_seen != g_vz_ver) { g_vz_seen = g_vz_ver; ch = 1; }
    int pr = voz_install_progress();
    if (pr != g_vz_last_prog) { g_vz_last_prog = pr; ch = 1; }
    if (voz_speak_ver() != g_vc_speak_seen) { g_vc_speak_seen = voz_speak_ver(); ch = 1; }   /* SOMA_250_TTS: Probar / Parar */
    return ch;
}
struct vz_lay { int y1, row_y, row_h, h1, btn_y, btn_w, y2, h2, t_y, t_tg_x, t_tg_y, t_dd_y, t_sp_y, t_spk_y, t_btn_y, t_msg_y, t_h, lab_w; };
static int tts_cur(void)                                                /* indice de la voz en uso en la lista */
{
    struct voz_tts_conf c; voz_tts_get(&c);
    for (int i = 0; i < g_vc_n; i++) { const char *a = g_vc_path[i], *b = c.voz; int k = 0; while (a[k] && a[k] == b[k]) k++; if (a[k] == b[k]) return i; }
    return g_vc_n > 0 ? 0 : -1;
}
static void vz_lay(const struct sl *L, struct vz_lay *V)
{
    V->y1 = L->y + 84; V->row_y = V->y1 + 66; V->row_h = 80;
    V->btn_y = V->row_y + VOZ_TIERS * (V->row_h + 8) + 6; V->btn_w = 220;
    V->h1 = V->btn_y + 32 + 50 - V->y1;
    /* SOMA_250_TTS: tarjeta de la lectura en voz alta */
    V->t_y = V->y1 + V->h1 + 12; V->lab_w = 130;
    V->t_tg_x = L->cx + L->cw - 20 - 44; V->t_tg_y = V->t_y + 66;
    V->t_dd_y = V->t_y + 112; V->t_sp_y = V->t_dd_y + 44;
    int cur = tts_cur(), spk = cur >= 0 ? g_vc_spk[cur] : 1;
    V->t_spk_y = spk > 1 ? V->t_sp_y + 44 : -1000;
    V->t_btn_y = (spk > 1 ? V->t_spk_y : V->t_sp_y) + 48;
    V->t_msg_y = V->t_btn_y + 38; V->t_h = V->t_msg_y + 40 - V->t_y;
    V->y2 = V->t_y + V->t_h + 12; V->h2 = 150;
}
static const char *lab_voice(int i, char *b, int max) { (void)b; (void)max; return i >= 0 && i < g_vc_n ? g_vc_label[i] : ""; }
static const char *const SPD_NAME[4] = { "Lenta", "Normal", "R\xC3\xA1pida", "Muy r\xC3\xA1pida" };
static const int SPD[4] = { 85, 100, 115, 130 };
static void tts_pick_voice(int i)
{
    if (i < 0 || i >= g_vc_n) return;
    struct voz_tts_conf c; voz_tts_get(&c);
    c.voz[0] = 0; s_cat(c.voz, g_vc_path[i], sizeof c.voz); c.hablante = 0;
    voz_tts_set(&c);
    g_vc_msg[0] = 0; s_cat(g_vc_msg, "Voz elegida: ", sizeof g_vc_msg); s_cat(g_vc_msg, g_vc_label[i], sizeof g_vc_msg); g_vc_msg_ok = -1;
}
static void render_tts(const struct sl *L, const struct vz_lay *V)
{
    struct voz_tts_conf c; voz_tts_get(&c);
    card(L->cx, V->t_y, L->cw, V->t_h);
    tx(L->cx + 20, V->t_y + 12, 24, "Lectura en voz alta", 14, KC_TXT, 1);
    fb_set_clip(L->cx + 20, V->t_y + 30, L->cw - 40, V->t_tg_y + 40 - V->t_y - 30);
    tx(L->cx + 20, V->t_y + 34, 18, "Voces de Piper, sin conexi\xC3\xB3n. Notas usa la misma voz.", 11, KC_DIM, 0);
    fb_set_clip(L->cx + 20, V->t_tg_y - 8, V->t_tg_x - 12 - L->cx - 20, 50);
    tx(L->cx + 20, V->t_tg_y - 4, 20, "Leer las respuestas del orbe", 13, KC_TXT, 0);
    tx(L->cx + 20, V->t_tg_y + 16, 18, "Alt o una pregunta nueva la cortan.", 11, KC_FAINT, 0);
    fb_reset_clip();
    fb_fill_rrect_aa(V->t_tg_x, V->t_tg_y, 44, 24, 12, c.leer_orbe ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
    fb_fill_rrect_aa(V->t_tg_x + (c.leer_orbe ? 22 : 2), V->t_tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    int x1 = L->cx + 20 + V->lab_w, w1 = L->cw - 40 - V->lab_w;
    tx(L->cx + 20, V->t_dd_y, 32, "Voz", 13, KC_TXT, 0);
    dd(DD_VOICE, x1, V->t_dd_y, w1, g_vc_n, tts_cur(), lab_voice);
    tx(L->cx + 20, V->t_sp_y, 32, "Velocidad", 13, KC_TXT, 0);
    int si = 1; for (int i = 0; i < 4; i++) if (SPD[i] == c.velocidad) si = i;
    seg(x1, V->t_sp_y, w1, 4, si, SPD_NAME);
    int cur = tts_cur();
    if (cur >= 0 && g_vc_spk[cur] > 1) {
        static const char *const SPK[4] = { "1", "2", "3", "4" };
        int n = g_vc_spk[cur] < 4 ? g_vc_spk[cur] : 4;
        tx(L->cx + 20, V->t_spk_y, 32, "Hablante", 13, KC_TXT, 0);
        seg(x1, V->t_spk_y, w1, n, c.hablante < n ? c.hablante : 0, SPK);
    }
    int speaking = voz_speak_state();
    button(L->cx + 20, V->t_btn_y, 150, speaking ? "Parar" : "Probar voz", 1);
    button(L->cx + 182, V->t_btn_y, 230, g_vz_busy && g_vz_job == 100 ? "Instalando..." : "Instalar del pendrive", 0);
    const char *m = g_vc_msg; char hint[200];
    if (!m[0]) {
        hint[0] = 0;
        if (!usermode_avx2_ok()) s_cat(hint, "Este procesador no tiene AVX2: la lectura en voz alta no funciona en este equipo.", sizeof hint);
        else if (voz_speak_msg()[0]) s_cat(hint, voz_speak_msg(), sizeof hint);
        else if (!g_vc_n) s_cat(hint, "No hay voces instaladas: copia una voz de Piper (.onnx y .onnx.json) en la carpeta MODELOS del pendrive y pulsa Instalar.", sizeof hint);
        else { s_num(hint, sizeof hint, g_vc_n); s_cat(hint, g_vc_n == 1 ? " voz instalada en /voces." : " voces instaladas en /voces.", sizeof hint); }
        m = hint;
    }
    fb_set_clip(L->cx + 20, V->t_msg_y - 2, L->cw - 40, 40);
    tx_wrap(L->cx + 20, V->t_msg_y, L->cw - 40, m, 11, g_vc_msg[0] ? (g_vc_msg_ok == 0 ? 0x00FCA5A5 : g_vc_msg_ok == 1 ? 0x0034D399 : KC_DIM) : KC_FAINT, 17);
    fb_reset_clip();
}
static int click_tts(const struct sl *L, const struct vz_lay *V, int32_t mx, int32_t my)
{
    struct voz_tts_conf c; voz_tts_get(&c);
    int x1 = L->cx + 20 + V->lab_w, w1 = L->cw - 40 - V->lab_w;
    if (mx >= V->t_tg_x && mx < V->t_tg_x + 44 && my >= V->t_tg_y && my < V->t_tg_y + 24) {
        c.leer_orbe = !c.leer_orbe; voz_tts_set(&c);
        if (!c.leer_orbe && voz_speak_state()) voz_speak_stop();
        g_vc_msg[0] = 0; s_cat(g_vc_msg, c.leer_orbe ? "El orbe leer\xC3\xA1 sus respuestas en voz alta." : "El orbe ya no lee sus respuestas.", sizeof g_vc_msg); g_vc_msg_ok = -1;
        return 1;
    }
    if (mx >= x1 && mx < x1 + w1 && my >= V->t_sp_y && my < V->t_sp_y + 32) {
        int i = (mx - x1 - 2) * 4 / (w1 - 4); if (i < 0) i = 0; if (i > 3) i = 3;
        c.velocidad = SPD[i]; voz_tts_set(&c); return 1;
    }
    int cur = tts_cur();
    if (cur >= 0 && g_vc_spk[cur] > 1 && mx >= x1 && mx < x1 + w1 && my >= V->t_spk_y && my < V->t_spk_y + 32) {
        int n = g_vc_spk[cur] < 4 ? g_vc_spk[cur] : 4;
        int i = (mx - x1 - 2) * n / (w1 - 4); if (i < 0) i = 0; if (i >= n) i = n - 1;
        c.hablante = i; voz_tts_set(&c); return 1;
    }
    if (my >= V->t_btn_y && my < V->t_btn_y + 30) {
        if (mx >= L->cx + 20 && mx < L->cx + 170) {
            if (voz_speak_state()) voz_speak_stop();
            else {
                static const char demo[] = "Hola, soy SOMA. As\xC3\xAD sonar\xC3\xA1n mis respuestas. Puedes cambiar la voz y la velocidad en esta p\xC3\xA1gina.";
                int r = voz_speak_start(demo, (uint32_t)(sizeof demo - 1));
                g_vc_msg[0] = 0; if (r != 0) { s_cat(g_vc_msg, "No se pudo empezar a leer", sizeof g_vc_msg); g_vc_msg_ok = 0; }
            }
            return 1;
        }
        if (mx >= L->cx + 182 && mx < L->cx + 412 && !g_vz_busy) { g_vc_msg[0] = 0; s_cat(g_vc_msg, "Buscando voces en la carpeta MODELOS del pendrive...", sizeof g_vc_msg); g_vc_msg_ok = -1; vz_start(100); return 1; }
    }
    return 0;
}
static void render_voz(const struct sl *L)
{
    static const char *const DESC[VOZ_TIERS] = {
        "El m\xC3\xA1s r\xC3\xA1pido; falla m\xC3\xA1s con nombres y con ruido.",
        "M\xC3\xA1s preciso, ~3 veces m\xC3\xA1s lento. Recomendado.",
        "El m\xC3\xA1s preciso, ~8-10 veces m\xC3\xA1s lento." };
    tx(L->cx, L->y + 18, 30, "Voz", 20, KC_TXT, 1);
    fb_set_clip(L->cx, L->y + 46, L->cw, 22);
    tx(L->cx, L->y + 46, 22, "Dictado (mant\xC3\xA9n Alt) y lectura en voz alta, sin conexi\xC3\xB3n.", 12, KC_DIM, 0);
    fb_reset_clip();
    struct vz_lay V; vz_lay(L, &V);
    card(L->cx, V.y1, L->cw, V.h1);
    tx(L->cx + 20, V.y1 + 12, 24, "Modelo de transcripci\xC3\xB3n", 14, KC_TXT, 1);
    { char u[140] = "En uso: "; s_cat(u, g_vz_used[0] ? g_vz_used : (g_vz_busy ? "..." : "ninguno"), sizeof u); fb_set_clip(L->cx + 20, V.y1 + 36, L->cw - 40, 22); tx(L->cx + 20, V.y1 + 36, 22, u, 11, KC_FAINT, 0); fb_reset_clip(); }
    int pref = voz_pref(), prog = voz_install_progress();
    for (int t = 0; t < VOZ_TIERS; t++) {
        int ry = V.row_y + t * (V.row_h + 8), rx = L->cx + 16, rw = L->cw - 32, on = t == pref;
        fb_fill_rounded_rect(rx, ry, rw, V.row_h, 10, on ? 0x00142640 : 0x000C1119);
        if (on) fb_stroke_rrect_aa(rx, ry, rw, V.row_h, 10, 0xFF38BDF8u);
        fb_stroke_rrect_aa(rx + 14, ry + V.row_h / 2 - 9, 18, 18, 9, on ? 0xFF38BDF8u : 0xFF566379u);      /* boton de opcion */
        if (on) fb_fill_rrect_aa(rx + 19, ry + V.row_h / 2 - 4, 8, 8, 4, 0xFF38BDF8u);
        char nm[80]; nm[0] = 0; s_cat(nm, voz_tier_name(t), sizeof nm);
        if (t == 2) { nm[0] = 0; s_cat(nm, "M\xC3\xA1xima precisi\xC3\xB3n", sizeof nm); }
        else if (t == 0) { nm[0] = 0; s_cat(nm, "R\xC3\xA1pido", sizeof nm); }
        tx(rx + 44, ry + 8, 22, nm, 14, on ? 0x00FFFFFF : KC_TXT, 1);
        char fl[80]; fl[0] = 0; s_cat(fl, voz_tier_file(t), sizeof fl); s_cat(fl, "  ~", sizeof fl); s_num(fl, sizeof fl, (int)voz_tier_mb(t)); s_cat(fl, " MB", sizeof fl);
        fb_set_clip(rx + 44, ry + 4, rw - 58, V.row_h - 8);
        tx(rx + 44, ry + 31, 20, fl, 11, KC_FAINT, 0);
        tx(rx + 44, ry + 52, 20, DESC[t], 11, KC_DIM, 0);
        fb_reset_clip();
        const char *st; uint32_t sc;
        if (g_vz_busy && g_vz_job == t + 1) { st = "Instalando..."; sc = 0x0093C5FD; }
        else if (g_vz_inst[t]) { st = "Instalado"; sc = 0x0034D399; }
        else if (g_vz_stick[t]) { st = "En el pendrive"; sc = 0x00FBBF24; }
        else { st = "No disponible"; sc = 0x00566379; }
        int sw_ = tw(st, 11, 1) + 20;
        fb_fill_rounded_rect(rx + rw - sw_ - 14, ry + 10, sw_, 22, 11, 0x00111827);
        tx(rx + rw - sw_ - 4, ry + 10, 22, st, 11, sc, 1);
    }
    /* boton: instalar el elegido (si esta en el pendrive y no instalado) o volver a mirar el pendrive */
    int can_inst = !g_vz_busy && !g_vz_inst[pref] && g_vz_stick[pref];
    const char *bl = g_vz_busy ? (g_vz_job ? "Instalando..." : "Buscando...") : can_inst ? "Instalar desde el pendrive" : "Buscar en el pendrive";
    int bx = L->cx + 20;
    fb_fill_rounded_rect(bx, V.btn_y, V.btn_w, 32, 16, can_inst ? KC_PRIMARY : 0x00182133);
    fb_stroke_rrect_aa(bx, V.btn_y, V.btn_w, 32, 16, can_inst ? 0xFF38BDF8u : 0xFF2B6CB0u);
    tx(bx + (V.btn_w - tw(bl, 13, 1)) / 2, V.btn_y, 32, bl, 13, can_inst ? 0x00FFFFFF : 0x0093C5FD, 1);
    int mx0 = bx + V.btn_w + 16, mw = L->cx + L->cw - 20 - mx0;
    if (prog >= 0 && mw > 40) {                                       /* progreso de la copia */
        fb_fill_rounded_rect(mx0, V.btn_y + 12, mw, 8, 4, 0x00182133);
        fb_fill_rounded_rect(mx0, V.btn_y + 12, mw * prog / 100 > 8 ? mw * prog / 100 : 8, 8, 4, 0x0038BDF8);
    }
    const char *msg = g_vz_msg;
    char hint[200];
    if (!msg[0]) {
        hint[0] = 0;
        if (!usermode_avx2_ok()) s_cat(hint, "Este procesador no tiene AVX2: el reconocimiento de voz no funciona en este equipo.", sizeof hint);
        else if (g_vz_inst[pref]) s_cat(hint, "El dictado y la Grabadora usan este modelo desde ya.", sizeof hint);
        else if (g_vz_stick[pref]) s_cat(hint, "Se instalar\xC3\xA1 solo al dictar, o ahora con el bot\xC3\xB3n (copia al almac\xC3\xA9n).", sizeof hint);
        else { s_cat(hint, "No est\xC3\xA1 en la carpeta MODELOS del pendrive: mientras tanto se usa el instalado m\xC3\xA1s parecido.", sizeof hint); }
        msg = hint;
    }
    fb_set_clip(L->cx + 20, V.btn_y + 38, L->cw - 40, 40);
    tx_wrap(L->cx + 20, V.btn_y + 42, L->cw - 40, msg, 11, g_vz_msg[0] ? (g_vz_msg_ok ? 0x0034D399 : 0x00FCA5A5) : KC_FAINT, 17);
    fb_reset_clip();
    render_tts(L, &V);                                               /* SOMA_250_TTS */
    card(L->cx, V.y2, L->cw, V.h2);
    tx(L->cx + 20, V.y2 + 12, 22, "C\xC3\xB3mo a\xC3\xB1" "adir un modelo", 13, KC_TXT, 1);
    tx_wrap(L->cx + 20, V.y2 + 38, L->cw - 40, "Descarga el archivo de huggingface.co/ggerganov/whisper.cpp (carpeta \"Files\") y c\xC3\xB3pialo en la carpeta MODELOS de la unidad SOMA del pendrive, junto a ggml-base-q5_1.bin. Despu\xC3\xA9s elige aqu\xC3\xAD el modelo y pulsa Instalar (o dicta: se instala solo). Sirven tambi\xC3\xA9n otras variantes del mismo tama\xC3\xB1o (q5_0, q8_0); el nivel se deduce del nombre: base, small, large/turbo.", 11, KC_DIM, 18);
}
static int click_voz(const struct sl *L, int32_t mx, int32_t my)
{
    struct vz_lay V; vz_lay(L, &V);
    if (my >= V.t_y && my < V.t_y + V.t_h) return click_tts(L, &V, mx, my);   /* SOMA_250_TTS */
    for (int t = 0; t < VOZ_TIERS; t++) {
        int ry = V.row_y + t * (V.row_h + 8);
        if (mx >= L->cx + 16 && mx < L->cx + L->cw - 16 && my >= ry && my < ry + V.row_h) {
            if (t != voz_pref()) { voz_set_pref(t); g_vz_msg[0] = 0; vz_start(0); }
            return 1;
        }
    }
    if (mx >= L->cx + 20 && mx < L->cx + 20 + V.btn_w && my >= V.btn_y && my < V.btn_y + 32 && !g_vz_busy) {
        int p = voz_pref();
        g_vz_msg[0] = 0;
        vz_start(!g_vz_inst[p] && g_vz_stick[p] ? p + 1 : 0);
        return 1;
    }
    return 0;
}
#else
static void voz_page_open(void) { }
static int voz_page_tick(void) { return 0; }
static void render_voz(const struct sl *L)
{
    tx(L->cx, L->y + 18, 30, "Voz y dictado", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "El reconocimiento de voz (whisper.cpp) de momento solo funciona en x86_64 (necesita AVX2).", 12, KC_DIM, 0);
}
static int click_voz(const struct sl *L, int32_t mx, int32_t my) { (void)L; (void)mx; (void)my; return 0; }
static void tts_pick_voice(int i) { (void)i; }
#endif

/* ================================================================ SOMA_250_TTS: pagina Sonido
 * Salida: volumen (el mismo que las teclas del portatil), silencio y prueba. Microfono: entrada y ganancia (dictado y Grabadora).
 * Se guarda en /etc/sonido.conf y se aplica al arrancar el escritorio. */
static int g_snd_vol = -1, g_snd_mute, g_snd_vol_before = 80, g_snd_mic = -1, g_snd_gain = -1;
static char g_snd_msg[160];
static void snd_save(void)
{
    char b[120] = "volumen="; s_num(b, sizeof b, g_snd_vol < 0 ? 100 : g_snd_vol);
    s_cat(b, "\nsilencio=", sizeof b); s_num(b, sizeof b, g_snd_mute);
    s_cat(b, "\nmicrofono=", sizeof b); s_num(b, sizeof b, g_snd_mic < 0 ? 0 : g_snd_mic);
    s_cat(b, "\nganancia=", sizeof b); s_num(b, sizeof b, g_snd_gain < 0 ? 66 : g_snd_gain);
    s_cat(b, "\n", sizeof b);
    (void)vfs_write("/etc/sonido.conf", b, (uint32_t)s_len(b));
}
static void snd_load(void)
{
    char b[160]; int n = vfs_read("/etc/sonido.conf", b, sizeof b - 1);
    if (n > 0) {
        b[n] = 0;
        g_snd_vol = val_of(b, "volumen", 100); if (g_snd_vol < 0 || g_snd_vol > 100) g_snd_vol = 100;
        g_snd_mute = val_of(b, "silencio", 0) ? 1 : 0;
        g_snd_mic = val_of(b, "microfono", 0);
        g_snd_gain = val_of(b, "ganancia", 66); if (g_snd_gain < 0 || g_snd_gain > 100) g_snd_gain = 66;
#ifdef __x86_64__
        if (hda_ready()) {
            hda_set_volume(g_snd_mute ? 0 : g_snd_vol);
            if (hda_mic_ready()) { if (g_snd_mic >= 0 && g_snd_mic < hda_mic_count() && g_snd_mic != hda_mic_selected()) (void)hda_mic_select(g_snd_mic); (void)hda_mic_set_gain(g_snd_gain); }
        }
#endif
    }
}
static void snd_sync(void)                                              /* lo que diga el hardware (las teclas cambian el volumen) */
{
#ifdef __x86_64__
    if (hda_ready()) { int v = hda_get_volume(); if (v > 0) { g_snd_vol = v; g_snd_mute = 0; } else if (!g_snd_mute) { g_snd_mute = 1; } if (g_snd_vol < 0) g_snd_vol = 80; }
    if (hda_mic_ready()) { g_snd_mic = hda_mic_selected(); g_snd_gain = hda_mic_get_gain(); }
#endif
    if (g_snd_vol < 0) g_snd_vol = 100;
    if (g_snd_gain < 0) g_snd_gain = 66;
}
struct snd_lay { int y1, h1, vol_y, mute_y, sl_x, sl_w, y2, h2, mic_y, gain_y, y3, h3, lab_w; };
static void snd_lay(const struct sl *L, struct snd_lay *S)
{
    S->lab_w = 130; S->sl_x = L->cx + 20 + S->lab_w; S->sl_w = L->cw - 40 - S->lab_w - 64;
    S->y1 = L->y + 84; S->vol_y = S->y1 + 72; S->mute_y = S->y1 + 118; S->h1 = 168;
    S->y2 = S->y1 + S->h1 + 12; S->mic_y = S->y2 + 52; S->gain_y = S->y2 + 104; S->h2 = 190;
    S->y3 = S->y2 + S->h2 + 12; S->h3 = 96;
}
static void slider(int x, int y, int w, int v, int on)
{
    fb_fill_rrect_aa(x, y + 10, w, 6, 3, 0xFF1E293Bu);
    int f = w * v / 100;
    if (f > 0) fb_fill_rrect_aa(x, y + 10, f, 6, 3, on ? 0xFF38BDF8u : 0xFF475569u);
    fb_fill_rrect_aa(x + f - 9, y + 4, 18, 18, 9, 0xFFFFFFFFu);
    fb_stroke_rrect_aa(x + f - 9, y + 4, 18, 18, 9, 0xFF0B6BCBu);
}
static const char *lab_mic(int i, char *b, int max)
{
    b[0] = 0;
#ifdef __x86_64__
    hda_mic_describe(i, b, (uint32_t)max);
#else
    (void)i; (void)max;
#endif
    if (!b[0]) { s_cat(b, "Entrada ", max); s_num(b, max, i + 1); }
    return b;
}
static void snd_pick_mic(int i)
{
#ifdef __x86_64__
    if (hda_mic_select(i) == 0) { g_snd_mic = i; snd_save(); g_snd_msg[0] = 0; s_cat(g_snd_msg, "Micr\xC3\xB3" "fono elegido.", sizeof g_snd_msg); }
    else { g_snd_msg[0] = 0; s_cat(g_snd_msg, "No se pudo usar esa entrada.", sizeof g_snd_msg); }
#else
    (void)i;
#endif
}
static void render_sonido(const struct sl *L)
{
    struct snd_lay S; snd_lay(L, &S);
    tx(L->cx, L->y + 18, 30, "Sonido", 20, KC_TXT, 1);
    fb_set_clip(L->cx, L->y + 46, L->cw, 22);
    tx(L->cx, L->y + 46, 22, "Altavoces, auriculares y micr\xC3\xB3" "fono.", 12, KC_DIM, 0);
    fb_reset_clip();
    int out_ok = 0, mic_ok = 0;
#ifdef __x86_64__
    out_ok = hda_ready(); mic_ok = hda_mic_ready();
#endif
    char b[160];
    /* salida */
    card(L->cx, S.y1, L->cw, S.h1);
    tx(L->cx + 20, S.y1 + 12, 24, "Salida", 14, KC_TXT, 1);
    dot(L->cx + 22, S.y1 + 42, out_ok ? 0x0022C55E : 0x00F87171);
    fb_set_clip(L->cx + 40, S.y1 + 36, L->cw - 60, 22);
    tx(L->cx + 40, S.y1 + 36, 22, out_ok ? "Tarjeta de sonido HD Audio lista" : "No hay salida de audio en este equipo", 12, out_ok ? KC_TXT : KC_DIM, 0);
    fb_reset_clip();
    tx(L->cx + 20, S.vol_y, 26, "Volumen", 13, KC_TXT, 0);
    slider(S.sl_x, S.vol_y, S.sl_w, g_snd_mute ? 0 : g_snd_vol, out_ok && !g_snd_mute);
    b[0] = 0; if (g_snd_mute) s_cat(b, "silencio", sizeof b); else { s_num(b, sizeof b, g_snd_vol); s_cat(b, " %", sizeof b); }
    tx(S.sl_x + S.sl_w + 18, S.vol_y, 26, b, 12, KC_DIM, 0);
    int tgx = L->cx + 20 + S.lab_w;
    tx(L->cx + 20, S.mute_y, 26, "Silenciar", 13, KC_TXT, 0);
    fb_fill_rrect_aa(tgx, S.mute_y + 1, 44, 24, 12, g_snd_mute ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
    fb_fill_rrect_aa(tgx + (g_snd_mute ? 22 : 2), S.mute_y + 3, 20, 20, 10, 0xFFFFFFFFu);
    button(L->cx + L->cw - 20 - 160, S.mute_y - 2, 160, "Probar sonido", 0);
    /* microfono */
    card(L->cx, S.y2, L->cw, S.h2);
    tx(L->cx + 20, S.y2 + 12, 24, "Micr\xC3\xB3" "fono", 14, KC_TXT, 1);
    fb_set_clip(L->cx + 20, S.y2 + 32, L->cw - 40, 18);
    tx(L->cx + 20, S.y2 + 32, 18, "Para el dictado (mant\xC3\xA9n Alt) y la Grabadora.", 11, KC_DIM, 0);
    fb_reset_clip();
    int nmic = 0;
#ifdef __x86_64__
    nmic = mic_ok ? hda_mic_count() : 0;
#endif
    tx(L->cx + 20, S.mic_y, 32, "Entrada", 13, KC_TXT, 0);
    dd(DD_MIC, L->cx + 20 + S.lab_w, S.mic_y, L->cw - 40 - S.lab_w, nmic, mic_ok ? g_snd_mic : -1, lab_mic);
    tx(L->cx + 20, S.gain_y, 26, "Ganancia", 13, KC_TXT, 0);
    slider(S.sl_x, S.gain_y, S.sl_w, g_snd_gain, mic_ok);
    b[0] = 0; s_num(b, sizeof b, g_snd_gain); s_cat(b, " %", sizeof b);
    tx(S.sl_x + S.sl_w + 18, S.gain_y, 26, b, 12, KC_DIM, 0);
    fb_set_clip(L->cx + 20, S.gain_y + 32, L->cw - 40, 38);
    tx_wrap(L->cx + 20, S.gain_y + 36, L->cw - 40, g_snd_msg[0] ? g_snd_msg : (mic_ok ? "Si el dictado no te entiende, sube la ganancia; si satura o capta ruido, b\xC3\xA1jala." : "No se encontr\xC3\xB3 ninguna entrada de micr\xC3\xB3" "fono."), 11, KC_FAINT, 16);
    fb_reset_clip();
    /* lectura en voz alta */
    card(L->cx, S.y3, L->cw, S.h3);
    tx(L->cx + 20, S.y3 + 12, 24, "Voz del sistema", 14, KC_TXT, 1);
    fb_set_clip(L->cx + 20, S.y3 + 36, L->cw - 40, 20);
    tx(L->cx + 20, S.y3 + 36, 20, "Voz, velocidad y lectura del orbe: p\xC3\xA1gina Voz.", 11, KC_DIM, 0);
    fb_reset_clip();
    button(L->cx + L->cw - 20 - 120, S.y3 + 54, 120, "Ir a Voz", 0);
    cbot(S.y3 + S.h3);
}
static int snd_set_from(const struct snd_lay *S, int mx, int which)    /* 1 volumen, 2 ganancia */
{
    int v = (mx - S->sl_x) * 100 / (S->sl_w > 1 ? S->sl_w : 1); if (v < 0) v = 0; if (v > 100) v = 100;
    v = (v + 2) / 5 * 5;
    if (which == 1) {
        if (v == g_snd_vol && !g_snd_mute) return 0;
        g_snd_vol = v; g_snd_mute = v == 0;
#ifdef __x86_64__
        hda_set_volume(v);
#endif
    } else {
        if (v == g_snd_gain) return 0;
        g_snd_gain = v;
#ifdef __x86_64__
        if (hda_mic_ready()) (void)hda_mic_set_gain(v);
#endif
    }
    return 1;
}
static int g_snd_drag;
static int click_sonido(const struct sl *L, int32_t mx, int32_t my)
{
    struct snd_lay S; snd_lay(L, &S);
    if (mx >= S.sl_x - 12 && mx < S.sl_x + S.sl_w + 12) {
        if (my >= S.vol_y && my < S.vol_y + 26) { g_snd_drag = 1; return snd_set_from(&S, mx, 1) | 1; }
        if (my >= S.gain_y && my < S.gain_y + 26) { g_snd_drag = 2; return snd_set_from(&S, mx, 2) | 1; }
    }
    int tgx = L->cx + 20 + S.lab_w;
    if (mx >= tgx && mx < tgx + 44 && my >= S.mute_y && my < S.mute_y + 26) {
        if (!g_snd_mute) { g_snd_vol_before = g_snd_vol > 0 ? g_snd_vol : 80; g_snd_mute = 1; }
        else { g_snd_mute = 0; if (g_snd_vol <= 0) g_snd_vol = g_snd_vol_before; }
#ifdef __x86_64__
        hda_set_volume(g_snd_mute ? 0 : g_snd_vol);
#endif
        snd_save(); return 1;
    }
    if (mx >= L->cx + L->cw - 180 && mx < L->cx + L->cw - 20 && my >= S.mute_y - 2 && my < S.mute_y + 28) {
        static char o[160]; (void)audio_sys_command("test", o, sizeof o);
        g_snd_msg[0] = 0; s_cat(g_snd_msg, o, sizeof g_snd_msg);
        return 1;
    }
    if (mx >= L->cx + L->cw - 140 && mx < L->cx + L->cw - 20 && my >= S.y3 + 54 && my < S.y3 + 84) { g_page = PAGE_VOZ; voz_page_open(); return 1; }
    return 0;
}

#define NPAGES NITEMS
static int g_sc[NPAGES], g_sc_max[NPAGES], g_sdrag;           /* SOMA_227_IMAGEN: desplazamiento por pagina */
static int sc_page(void) { return g_page >= 0 && g_page < NPAGES ? g_page : 0; }
void settings_goto_voz(void) { g_page = PAGE_VOZ; voz_page_open(); }
void settings_goto_sonido(void) { g_page = PAGE_SONIDO; snd_sync(); }   /* SOMA_250_TTS: "ajustes sonido" */
void settings_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focus)
{
    (void)focus;
    settings_load();
    struct sl L; lay(&L, x, y, w, h);
    int pg = sc_page();
    fb_fill_rect(x, y, w, h, KC_WIN);
    fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
    fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
    render_sidebar(&L);
    struct sl C; lay(&C, x, (uint32_t)((int)y - g_sc[pg]), w, h);   /* la pagina, desplazada */
    g_clon = 1; g_clx = L.x + L.side_w; g_cly = L.y; g_clw = L.w - L.side_w; g_clh = L.h;
    sclip_reset();
    g_cbot = C.y;
    g_ndd = 0;                                                     /* SOMA_250_TTS: las listas se registran al dibujarse */
    switch (g_page) {
    case 1:             render_redes(&C); break;
    case 2:             render_fuentes(&C); break;
    case PAGE_IA:       render_ia(&C); break;
    case PAGE_PANTALLA: render_pantalla(&C); break;
    case PAGE_ICONOS:   render_iconos(&C); break;
    case PAGE_VOZ:      render_voz(&C); break;          /* SOMA_246_VOZ_MODELOS */
    case PAGE_SONIDO:   render_sonido(&C); break;       /* SOMA_250_TTS */
    case PAGE_SISTEMA:  render_sistema(&C); break;
    case PAGE_ACERCA:   render_acerca(&C); break;
    default:            render_apariencia(&C); break;
    }
    g_clon = 0; (fb_reset_clip)();
    int total = g_cbot - C.y + 20, mx = total - L.h; if (mx < 0) mx = 0;
    g_sc_max[pg] = mx;
    if (g_sc[pg] > mx) g_sc[pg] = mx;
    if (mx > 0) {                                                  /* barra de desplazamiento */
        int tx0 = L.x + L.w - 10, ty0 = L.y + 6, th = L.h - 12;
        int kh = th * L.h / total; if (kh < 30) kh = 30;
        int ky = ty0 + (th - kh) * g_sc[pg] / mx;
        fb_fill_rrect_aa(tx0, ty0, 5, th, 2, 0x22FFFFFFu);
        fb_fill_rrect_aa(tx0 - 1, ky, 7, kh, 3, g_sdrag ? 0xE0FFFFFFu : 0x90A5B4CBu);
    }
    if (g_dd_open != DD_NONE && !dd_find(g_dd_open)) g_dd_open = DD_NONE;   /* su pagina ya no se ve */
    dd_draw_open((int)x, (int)y, (int)w, (int)h);                  /* encima de todo */
}

static int set_opacity_from(const struct sl *L, int mx)
{
    int x = mx - L->sl_x; if (x < 0) x = 0;
    if (x > L->sl_w - 1) x = L->sl_w - 1;
    int v = 40 + x * 60 / (L->sl_w - 1);
    v = (v + 2) / 5 * 5;                                   /* de 5 en 5 */
    if (v == g_opacity) return 0;
    g_opacity = v; return 1;
}
/* SOMA_184: Restablecer (Apariencia y Fuentes) */
static int click_reset(const struct sl *L, int32_t mx, int32_t my, int ry)
{
    if (mx < L->reset_x || mx >= L->reset_x + 130 || my < ry || my >= ry + 32) return 0;
    g_opacity = 100; g_active_opaque = 0; wf_ui_set_family(WF_FAM_SANS, 100);
    g_editor_face = FFACE_MONO; g_desk[0] = 0; (void)font_set(FONT_VECTOR);
    save();
    g_msg[0] = 0; s_cat(g_msg, "Valores por defecto restablecidos", sizeof g_msg);
    return 1;
}
static int seg_index(const struct sl *L, int32_t mx, int n) { int i = (mx - L->cx - 22) * n / (L->seg_w - 4); return i < 0 ? 0 : (i >= n ? n - 1 : i); }
static int click_fuentes(const struct sl *L, int32_t mx, int32_t my)
{
    if (click_reset(L, mx, my, L->reset2_y)) return 1;
    if (font_sysfont_off() || mx < L->cx + 20 || mx >= L->cx + 20 + L->seg_w) return 0;
    if (my >= L->sz_y && my < L->sz_y + 32) { wf_ui_set_family(wf_ui_family(), SCALES[seg_index(L, mx, 4)]); save(); return 1; }
    return 0;
}
static void tts_pick_voice(int i);
static void snd_pick_mic(int i);
static int dd_pick(int id, int i)                                       /* SOMA_250_TTS: se eligio i en una lista */
{
    switch (id) {
    case DD_DESK: { int fid = font_get_sorted_id(i); if (!font_set(fid)) return 0; g_desk[0] = 0; s_cat(g_desk, font_get_name(fid), sizeof g_desk); save(); return 1; }
    case DD_UIFAM: wf_ui_set_family(UIFAM[i], wf_ui_scale()); save(); return 1;
    case DD_EDITOR: g_editor_face = EDF[i]; save(); return 1;
    case DD_VOICE: tts_pick_voice(i); return 1;
    case DD_MIC: snd_pick_mic(i); return 1;
    }
    return 0;
}
static int dd_click(int32_t mx, int32_t my)                             /* 1 = el clic era de una lista */
{
    if (g_dd_open != DD_NONE) {
        struct ddbox *b = dd_find(g_dd_open); int id = g_dd_open;
        g_dd_open = DD_NONE;
        if (b && g_ddl_h > 0 && mx >= g_ddl_x && mx < g_ddl_x + g_ddl_w && my >= g_ddl_y && my < g_ddl_y + g_ddl_h) {
            int r = (my - g_ddl_y - 4) / DD_ROW, i = g_dd_first + r;
            if (r >= 0 && r < g_ddl_rows && i < b->n) dd_pick(id, i);
        }
        return 1;                                                       /* fuera: solo se cierra */
    }
    for (int k = 0; k < g_ndd; k++) {
        struct ddbox *b = &g_dd[k];
        if (mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + DD_H) {
            if (b->n <= 0) return 1;
            g_dd_open = b->id; g_dd_first = b->sel - DD_ROWS / 2; if (g_dd_first < 0) g_dd_first = 0;
            return 1;
        }
    }
    return 0;
}
int settings_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct sl L; lay(&L, x, y, w, h);
    g_drag = 0;
    if (dd_click(mx, my)) return 1;                                     /* SOMA_250_TTS: listas desplegables */
    if (mx >= L.x + 10 && mx < L.x + L.side_w - 10) {                   /* SOMA_175C: barra lateral */
        for (int i = 0; i < NITEMS; i++) { int iy = sb_y(&L, i); if (my >= iy && my < iy + sb_step(&L) - 6) { g_page = i; g_pw_focus = 0; g_ia_focus = -1; if (i == PAGE_IA && !g_ia_busy) g_ia_loaded = 0; if (i == PAGE_VOZ) voz_page_open(); if (i == PAGE_SONIDO) snd_sync(); return i == PAGE_IA || i == PAGE_VOZ || i == PAGE_SONIDO; } }
    }
    int pg = sc_page();
    if (g_sc_max[pg] > 0 && mx >= L.x + L.w - 16 && mx < L.x + L.w && my >= L.y && my < L.y + L.h) {   /* SOMA_227_IMAGEN: barra */
        g_sdrag = 1; return settings_mouse(mx, my, x, y, w, h, 1) | 1;
    }
    if (my < L.y || my >= L.y + L.h) return 0;
    lay(&L, x, (uint32_t)((int)y - g_sc[pg]), w, h);                    /* el contenido esta desplazado */
    if (g_page == PAGE_IA) return click_ia(&L, mx, my);                 /* SOMA_195 */
    if (g_page == 1) return click_redes(&L, mx, my);
    if (g_page == 2) return click_fuentes(&L, mx, my);
    if (g_page == PAGE_PANTALLA) return click_pantalla(&L, mx, my);     /* SOMA_225_AJUSTES */
    if (g_page == PAGE_ICONOS) return click_iconos(&L, mx, my);
    if (g_page == PAGE_SISTEMA) return click_sistema(&L, mx, my);      /* SOMA_226_SISTEMA */
    if (g_page == PAGE_VOZ) return click_voz(&L, mx, my);              /* SOMA_246_VOZ_MODELOS */
    if (g_page == PAGE_SONIDO) return click_sonido(&L, mx, my);        /* SOMA_250_TTS */
    if (my >= L.sl_y - 6 && my < L.sl_y + 26 && mx >= L.sl_x - 12 && mx < L.sl_x + L.sl_w + 12) { g_drag = 1; int c = set_opacity_from(&L, mx); if (c) save(); return c; }
    if (mx >= L.tg_x && mx < L.tg_x + 44 && my >= L.tg_y && my < L.tg_y + 24) { g_active_opaque = !g_active_opaque; save(); return 1; }
    if (click_reset(&L, mx, my, L.reset_y)) return 1;
    return 0;
}
int settings_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int held)
{
    int pg = sc_page();
    if (g_sdrag) {                                                      /* SOMA_227_IMAGEN: arrastre de la barra */
        if (!held) { g_sdrag = 0; return 1; }
        int th = (int)h - 12, v = (my - (int)y - 6) * (g_sc_max[pg] + 1) / (th > 1 ? th : 1);
        if (v < 0) v = 0;
        if (v > g_sc_max[pg]) v = g_sc_max[pg];
        if (v == g_sc[pg]) return 0;
        g_sc[pg] = v; return 1;
    }
    if (g_snd_drag) {                                                   /* SOMA_250_TTS: deslizadores de Sonido */
        struct sl L; lay(&L, x, (uint32_t)((int)y - g_sc[PAGE_SONIDO]), w, h); struct snd_lay S; snd_lay(&L, &S);
        if (!held) { g_snd_drag = 0; snd_save(); return 1; }
        return snd_set_from(&S, mx, g_snd_drag);
    }
    if (!g_drag || g_page != 0) return 0;
    if (!held) { g_drag = 0; save(); return 1; }
    struct sl L; lay(&L, x, (uint32_t)((int)y - g_sc[0]), w, h);
    return set_opacity_from(&L, mx);
}
int settings_wheel(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel)   /* SOMA_227_IMAGEN */
{
    if (g_dd_open != DD_NONE) {                                         /* SOMA_250_TTS: la rueda mueve la lista abierta */
        struct ddbox *b = dd_find(g_dd_open);
        if (b && wheel) { int v = g_dd_first + (wheel > 0 ? 3 : -3), mxf = b->n - g_ddl_rows; if (v > mxf) v = mxf; if (v < 0) v = 0; if (v != g_dd_first) { g_dd_first = v; return 1; } }
        return 0;
    }
    int pg = sc_page();
    if (!wheel || g_sc_max[pg] <= 0) return 0;
    if (mx < (int32_t)x || mx >= (int32_t)(x + w) || my < (int32_t)y || my >= (int32_t)(y + h)) return 0;
    int v = g_sc[pg] + (wheel > 0 ? 48 : -48);
    if (v < 0) v = 0;
    if (v > g_sc_max[pg]) v = g_sc_max[pg];
    if (v == g_sc[pg]) return 0;
    g_sc[pg] = v; return 1;
}
int settings_handle_key(char c)
{
    if (g_dd_open != DD_NONE) { if (c == 27) { g_dd_open = DD_NONE; return 1; } return 0; }   /* SOMA_250_TTS */
    if (g_page == PAGE_IA) return key_ia(c);                            /* SOMA_195 */
    if (g_page == 1) {                                                  /* SOMA_175C: contrasena de la WiFi */
        if (!g_pw_focus) return 0;
        if (c == 8 || c == 127) { if (g_pw_len) g_pw[--g_pw_len] = 0; return 0; }
        if (c == '\n' || c == '\r') {
#if defined(__x86_64__)
            g_pw[g_pw_len] = 0;
            if (g_sel[0] && !iwl_wifi_request_connect(g_sel, g_pw_len ? g_pw : 0, 1)) { for (int i = 0; i < 65; i++) g_pw[i] = 0; g_pw_len = 0; g_pw_focus = 0; }
#endif
            return 0;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127 && g_pw_len < 63) { g_pw[g_pw_len++] = c; g_pw[g_pw_len] = 0; }
        return 0;
    }
    if (c == '+' || c == '=') { if (g_opacity < 100) { g_opacity += 5; save(); return 1; } }
    if (c == '-') { if (g_opacity > 40) { g_opacity -= 5; save(); return 1; } }
    return 0;
}
