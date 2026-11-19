#include "web_font.h"
#include "web_util.h"
#include "fb.h"
#include "gui_font.h"
#include "web_fontdata.h"
#include "fonts/vraster.h"
#include "fonts/vfont.h"  /* copia propia (static) del rasterizador TrueType: no toca la fuente del escritorio */

static int wf_state = 0; /* 0 sin iniciar, 1 lista, -1 sin fuente */

/* SOMA_182: caras de la web. 0 = DejaVu Sans (la de siempre, tambien respaldo de glifos), 1 = Liberation Sans (anchos de Arial),
 * 2 = Liberation Sans negrita. Cada cara guarda su copia del estado del lector (vf, vf_ascii); wf_use() la activa. */
#define WF_NFACE 3
static struct { struct vf_font f; uint16_t ascii[256]; int ok; } wf_faces[WF_NFACE];
static int wf_cur = -1;
static int wf_face_state = 0;   /* 0 sin iniciar, 1 listas las caras Liberation, -1 no hay */

static void wf_use(int face)
{
    if (wf_cur == face) return;
    vf = wf_faces[face].f;
    for (int i = 0; i < 256; i++) vf_ascii[i] = wf_faces[face].ascii[i];
    vf_face = (uint32_t)face; wf_cur = face;
}
static void wf_face_load(int face, const uint8_t *d, uint32_t n)
{
    wf_faces[face].ok = vfont_init(d, n) ? 1 : 0;
    wf_faces[face].f = vf;
    for (int i = 0; i < 256; i++) wf_faces[face].ascii[i] = vf_ascii[i];
    wf_cur = face; vf_face = (uint32_t)face;
}

int wf_init(void)
{
    if (wf_state == 0) { wf_face_load(0, WEB_TTF_PTR, WEB_TTF_LEN); wf_state = wf_faces[0].ok ? 1 : -1; }
    return wf_state == 1;
}
/* SOMA_182: 1 si las caras Liberation estan cargadas (se intentan una sola vez) */
static int wf_arial_ready(void)
{
    if (wf_face_state == 0) {
        if (!wf_init()) { wf_face_state = -1; return 0; }
        wf_face_load(1, WEB_TTF_SANS_PTR, WEB_TTF_SANS_LEN);
        wf_face_load(2, WEB_TTF_SANSB_PTR, WEB_TTF_SANSB_LEN);
        wf_face_state = (wf_faces[1].ok && wf_faces[2].ok) ? 1 : -1;
    }
    return wf_face_state == 1;
}
static inline int wf_web_arial(int fl) { return (((fl & WF_WEB) && g_wf182_on) || (fl & WF_LIB)) && !(fl & WF_MONO) && wf_arial_ready(); }   /* SOMA_184: WF_LIB = familia Liberation de la interfaz */

/* PARCHE 085: ajustes de fuente de la interfaz */
static int g_ui_mono, g_ui_fam, g_ui_scale = 100; static uint32_t g_ui_gen = 1;
/* SOMA_184: la familia de la interfaz es una de tres (0 DejaVu Sans, 1 DejaVu Sans Mono, 2 Liberation Sans); wf_ui_set conserva la firma antigua */
void wf_ui_set_family(int fam, int scale)
{
    if (scale < 80) scale = 80;
    if (scale > 130) scale = 130;
    if (fam < 0 || fam > 2) fam = 0;
    if (fam != g_ui_fam || scale != g_ui_scale) { g_ui_fam = fam; g_ui_mono = fam == WF_FAM_MONO; g_ui_scale = scale; g_ui_gen++; }
}
void wf_ui_set(int mono, int scale) { wf_ui_set_family(mono ? WF_FAM_MONO : (g_ui_fam == WF_FAM_LIBERATION ? WF_FAM_LIBERATION : WF_FAM_SANS), scale); }
int wf_ui_family(void) { return g_ui_fam; }
int wf_ui_mono(void) { return g_ui_mono; }
int wf_ui_scale(void) { return g_ui_scale; }
uint32_t wf_ui_gen(void) { return g_ui_gen; }
int wf_ui_px(int px) { return g_ui_scale == 100 ? px : (px * g_ui_scale + 50) / 100; }
static void ui_adj(int *px, int *fl) { if (*fl & WF_RAW) { *fl &= ~WF_RAW; return; } if (g_ui_scale != 100) *px = (*px * g_ui_scale + 50) / 100; if (g_ui_mono) *fl |= WF_MONO; else if (g_ui_fam == WF_FAM_LIBERATION) *fl |= WF_LIB; }

static int clamp_px(int px) { return px < 5 ? 5 : (px > VF_MAX_EM ? VF_MAX_EM : px); }

static uint32_t glyph_of(uint32_t cp)
{
    uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
    if (g == 0 && cp != '?') g = vf_ascii[(uint8_t)'?'];
    return g;
}
/* SOMA_182: elige la cara y el glifo de un codepoint. Con las caras Liberation, los glifos que no tienen (simbolos raros, escrituras
 * que Liberation no cubre) salen de DejaVu. Deja activa la cara elegida y devuelve el glifo (0 = ninguno). */
static uint32_t wf_pick(uint32_t cp, int fl, int *face)
{
    if (wf_web_arial(fl)) {
        int want = (fl & WF_BOLD) ? 2 : 1;
        wf_use(want);
        uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
        if (g) { *face = want; return g; }
    }
    wf_use(0); *face = 0;
    return glyph_of(cp);
}

int wf_cp_adv64(uint32_t cp, int px, int fl)
{
    px = clamp_px(px);
    if (fl & WF_MONO) return ((px * 1233 + 1024) / 2048) * 64;
    if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF) return 0;
    if (cp == 0xA0) cp = ' ';
    if (!wf_init()) return px * 32;
    int face; uint32_t g = wf_pick(cp, fl, &face);
    if (!g) return px * 32;
    int w = (int)(((uint64_t)vf_adv(g) * (uint64_t)px * 64u + vf.upem / 2) / vf.upem);
    if ((fl & WF_BOLD) && face != 2) w += (px >= 20) ? 64 : 32;
    return w;
}

int wf_width64(const char *s, uint32_t n, int px, int fl)
{
    ui_adj(&px, &fl);
    int w = 0; uint32_t i = 0;
    while (i < n && s[i]) { uint32_t adv; uint32_t cp = wu_utf8_next(s + i, &adv); i += adv; w += wf_cp_adv64(cp, px, fl); }
    return w;
}

int wf_width(const char *s, uint32_t n, int px, int fl) { return (wf_width64(s, n, px, fl) + 63) >> 6; }
int wf_ascent(int px) { return (px * 93 + 50) / 100; }
int wf_descent(int px) { return (px * 24 + 50) / 100; }
/* SOMA_182: ascenso/descenso del contenido web (Liberation: 0,905 + media holgura; DejaVu: 0,93 / 0,24) */
int wf_web_ascent(int px) { return g_wf182_on && wf_arial_ready() ? (px * 92 + 50) / 100 : wf_ascent(px); }
int wf_web_descent(int px) { return g_wf182_on && wf_arial_ready() ? (px * 23 + 50) / 100 : wf_descent(px); }

static void draw_glyph(int32_t x, int32_t by, struct vf_ce *ce, uint32_t rgb, int alpha, int fl, int px)
{
    if (!ce || !ce->cov) return;
    int32_t gx = x + ce->bx, gy = by + ce->by;
    if (!(fl & WF_ITALIC)) { fb_draw_mask8(gx, gy, ce->cov, ce->w, ce->h, rgb, (uint32_t)alpha); return; }
    int bands = ce->h >= 12 ? 4 : (ce->h >= 6 ? 2 : 1);
    int bh = (ce->h + bands - 1) / bands;
    (void)px;
    for (int r = 0; r < ce->h; r += bh) {
        int hh = (r + bh > ce->h) ? ce->h - r : bh;
        int cy = ce->by + r + hh / 2;                 /* y relativa a la linea base (negativa = arriba) */
        int dx = (-cy * 3) / 16;
        fb_draw_mask8(gx + dx, gy + r, ce->cov + r * ce->w, ce->w, hh, rgb, (uint32_t)alpha);
    }
}

void wf_draw(int32_t x, int32_t baseline_y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int alpha)
{
    if (!s || alpha <= 0) return;
    ui_adj(&px, &fl);
    px = clamp_px(px);
    int64_t pen = (int64_t)x * 64; uint32_t i = 0;
    int mono_ok = (fl & WF_MONO) && font_vec_available();
    while (i < n && s[i]) {
        uint32_t adv; uint32_t cp = wu_utf8_next(s + i, &adv); i += adv;
        int a64 = wf_cp_adv64(cp, px, fl);
        int32_t gx = (int32_t)((pen + 32) >> 6);
        if (cp > 32 && cp != 0xA0 && a64) {
            if (mono_ok) {
                font_vec_draw(gx, baseline_y, cp, rgb | (uint32_t)(alpha & 255) << 24, px, a64 >> 6, 0);
            } else if (wf_init()) {
                int face; uint32_t g = wf_pick(cp, fl, &face);
                if (g) {
                    struct vf_ce *ce = vf_get(g, px);
                    if (fl & WF_MONO) { int nat = (int)(((uint64_t)vf_adv(g) * (uint64_t)px + vf.upem / 2) / vf.upem); int off = ((a64 >> 6) - nat) / 2; gx += off; }
                    draw_glyph(gx, baseline_y, ce, rgb, alpha, fl, px);
                    if ((fl & WF_BOLD) && face != 2) draw_glyph(gx + (px >= 20 ? 2 : 1), baseline_y, ce, rgb, alpha, fl, px);
                }
            }
        }
        pen += a64;
    }
}
