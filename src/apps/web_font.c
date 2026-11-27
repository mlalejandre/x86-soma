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
#define WF_NFIX 7                                    /* SOMA_225: + 3 SOMA Orbital, 4 SOMA Tecnica; SOMA_347: 5 y 6 Liberation Serif */
#define WF_MAXWEB 40                                 /* SOMA_348_FUENTES_WEB: caras de @font-face (7..46) */
#define WF_NFACE (WF_NFIX + WF_MAXWEB)
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
/* SOMA_225: caras propias (se cargan la primera vez que se piden) */
extern const unsigned char soma_ttf_orbital[], soma_ttf_tecnica[];
extern const unsigned int soma_ttf_orbital_len, soma_ttf_tecnica_len;
static int wf_own_state[2];                                     /* 0 sin intentar, 1 lista, -1 fallo */
static int wf_own_ready(int k)
{
    if (wf_own_state[k] == 0) {
        if (k == 0) wf_face_load(3, soma_ttf_orbital, soma_ttf_orbital_len);
        else        wf_face_load(4, soma_ttf_tecnica, soma_ttf_tecnica_len);
        wf_own_state[k] = wf_faces[3 + k].ok ? 1 : -1;
    }
    return wf_own_state[k] == 1;
}
/* SOMA_347_NAVEGADOR_SERIF: caras con serifa (se cargan la primera vez que una pagina las pide) */
static int wf_serif_state;                                      /* 0 sin intentar, 1 listas, -1 fallo */
static int wf_serif_ready(void)
{
    if (wf_serif_state == 0) {
        if (!wf_init()) { wf_serif_state = -1; return 0; }
        wf_face_load(5, WEB_TTF_SERIF_PTR, WEB_TTF_SERIF_LEN);
        wf_face_load(6, WEB_TTF_SERIFB_PTR, WEB_TTF_SERIFB_LEN);
        wf_serif_state = (wf_faces[5].ok && wf_faces[6].ok) ? 1 : -1;
    }
    return wf_serif_state == 1;
}
static inline int wf_web_serif(int fl) { return (fl & WF_SERIF) && (fl & WF_WEB) && g_wf182_on && !(fl & WF_MONO) && wf_serif_ready(); }
/* SOMA_348_FUENTES_WEB: caras de @font-face. Se guardan para toda la sesion del navegador (la misma URL en otra pagina no se vuelve a
 * descargar); como mucho WF_MAXWEB caras y WF_WEB_BYTES en total. Los datos (sfnt) son de la cara: no se liberan mientras exista. */
#define WF_WEB_BYTES (48u << 20)
static uint32_t wf_web_key[WF_NFACE]; static uint8_t wf_web_bold[WF_NFACE]; static int wf_nweb; static uint32_t wf_web_bytes;
int wf_web_find(uint32_t key) { for (int i = WF_NFIX; i < WF_NFIX + wf_nweb; i++) if (wf_web_key[i] == key && wf_faces[i].ok) return i; return 0; }
int wf_web_add(uint8_t *sfnt, uint32_t n, uint32_t key, int bold)
{
    int f = wf_web_find(key); if (f) return f;
    if (!sfnt || wf_nweb >= WF_MAXWEB || wf_web_bytes + n > WF_WEB_BYTES || !wf_init()) return 0;
    f = WF_NFIX + wf_nweb;
    wf_face_load(f, sfnt, n);
    if (!wf_faces[f].ok) { wf_cur = -1; return 0; }
    wf_cur = -1;                                   /* la cara activa la vuelve a elegir quien dibuje */
    wf_web_key[f] = key; wf_web_bold[f] = (uint8_t)(bold != 0); wf_nweb++; wf_web_bytes += n;
    return f;
}
static inline int wf_webface(int fl) { int f = (fl >> 16) & 0xFF; return (f >= WF_NFIX && f < WF_NFIX + wf_nweb && wf_faces[f].ok) ? f : 0; }
static inline int wf_real_bold(int face) { return face == 2 || face == 6 || (face >= WF_NFIX && wf_web_bold[face]); }   /* caras que ya son negrita (no se engordan) */
static inline int wf_web_arial(int fl) { return (((fl & WF_WEB) && g_wf182_on) || (fl & WF_LIB)) && !(fl & WF_MONO) && wf_arial_ready(); }   /* SOMA_184: WF_LIB = familia Liberation de la interfaz */

/* PARCHE 085: ajustes de fuente de la interfaz */
static int g_ui_mono, g_ui_fam, g_ui_scale = 100; static uint32_t g_ui_gen = 1;
/* SOMA_184: la familia de la interfaz es una de tres (0 DejaVu Sans, 1 DejaVu Sans Mono, 2 Liberation Sans); wf_ui_set conserva la firma antigua */
void wf_ui_set_family(int fam, int scale)
{
    if (scale < 80) scale = 80;
    if (scale > 130) scale = 130;
    if (fam < 0 || fam > WF_FAM_TECNICA) fam = 0;
    if (fam != g_ui_fam || scale != g_ui_scale) { g_ui_fam = fam; g_ui_mono = fam == WF_FAM_MONO; g_ui_scale = scale; g_ui_gen++; }
}
void wf_ui_set(int mono, int scale) { wf_ui_set_family(mono ? WF_FAM_MONO : (g_ui_fam == WF_FAM_MONO ? WF_FAM_SANS : g_ui_fam), scale); }
int wf_ui_family(void) { return g_ui_fam; }
int wf_ui_mono(void) { return g_ui_mono; }
int wf_ui_scale(void) { return g_ui_scale; }
uint32_t wf_ui_gen(void) { return g_ui_gen; }
int wf_ui_px(int px) { return g_ui_scale == 100 ? px : (px * g_ui_scale + 50) / 100; }
static void ui_adj(int *px, int *fl) { if (*fl & WF_RAW) { *fl &= ~WF_RAW; return; } if (g_ui_scale != 100) *px = (*px * g_ui_scale + 50) / 100; if (g_ui_mono) *fl |= WF_MONO; else if (g_ui_fam == WF_FAM_LIBERATION) *fl |= WF_LIB; else if (g_ui_fam == WF_FAM_ORBITAL) *fl |= WF_ORB; else if (g_ui_fam == WF_FAM_TECNICA) *fl |= WF_TEC; }

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
    { int wf = wf_webface(fl);                                   /* SOMA_348_FUENTES_WEB: la fuente de @font-face primero */
      if (wf) { wf_use(wf); uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp); if (g) { *face = wf; return g; } } }
    if ((fl & (WF_ORB | WF_TEC)) && !(fl & WF_MONO)) {          /* SOMA_225: fuente propia; lo que no tenga sale de DejaVu */
        int k = (fl & WF_ORB) ? 0 : 1;
        if (wf_own_ready(k)) {
            wf_use(3 + k);
            uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
            if (g) { *face = 3 + k; return g; }
        }
    }
    if (wf_web_serif(fl)) {
        int want = (fl & WF_BOLD) ? 6 : 5;
        wf_use(want);
        uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
        if (g) { *face = want; return g; }
    }
    if (wf_web_arial(fl)) {
        int want = (fl & WF_BOLD) ? 2 : 1;
        wf_use(want);
        uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
        if (g) { *face = want; return g; }
    }
    if (!wf_init()) { *face = 0; return 0; }                   /* SOMA_225: sin DejaVu no hay respaldo */
    wf_use(0); *face = 0;
    return glyph_of(cp);
}

int wf_cp_adv64(uint32_t cp, int px, int fl)
{
    px = clamp_px(px);
    /* SOMA_345_NAVEGADOR_REAL: zona de uso privado de Unicode. Ahi ponen sus dibujos las fuentes de iconos (Font Awesome, Material
     * Icons...), que SOMA no carga: salia un "?" en cada pestana, boton y enlace externo (PyPI). Sin la fuente, no ocupa ni se dibuja. */
    int wf = wf_webface(fl), wg = 0;                        /* SOMA_348_FUENTES_WEB: con fuente web, sus glifos (iconos incluidos) */
    if (wf) { wf_use(wf); wg = (cp < 256) ? vf_ascii[cp] : (int)vf_lookup(cp); }
    /* negrita simulada en una cara web: se engorda el trazo pero no el avance, como Chrome (Skia); con fuentes variables el ancho
     * de verdad de un 599/700 es solo un 1-2 % mayor que el del 400 (Source Serif: 1058 frente a 1046 px en un titular) */
    if (wg) return (int)(((uint64_t)vf_adv((uint32_t)wg) * (uint64_t)px * 64u + vf.upem / 2) / vf.upem);
    if ((cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000) return 0;
    if (fl & WF_MONO) return ((px * 1233 + 1024) / 2048) * 64;
    if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF) return 0;
    if (cp == 0xA0) cp = ' ';
    if (!wf_init() && !(fl & (WF_ORB | WF_TEC))) return px * 32;
    int face; uint32_t g = wf_pick(cp, fl, &face);
    if (!g) return px * 32;
    int w = (int)(((uint64_t)vf_adv(g) * (uint64_t)px * 64u + vf.upem / 2) / vf.upem);
    if ((fl & WF_BOLD) && !wf_real_bold(face)) w += (px >= 20) ? 64 : 32;
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
            int webg = 0; { int wf = wf_webface(fl); if (wf) { wf_use(wf); webg = ((cp < 256) ? vf_ascii[cp] : vf_lookup(cp)) != 0; } }   /* SOMA_348 */
            if (mono_ok && !webg) {
                font_vec_draw(gx, baseline_y, cp, rgb | (uint32_t)(alpha & 255) << 24, px, a64 >> 6, 0);
            } else if (wf_init() || (fl & (WF_ORB | WF_TEC))) {
                int face; uint32_t g = wf_pick(cp, fl, &face);
                if (g) {
                    struct vf_ce *ce = vf_get(g, px);
                    if ((fl & WF_MONO) && face < WF_NFIX) { int nat = (int)(((uint64_t)vf_adv(g) * (uint64_t)px + vf.upem / 2) / vf.upem); int off = ((a64 >> 6) - nat) / 2; gx += off; }
                    draw_glyph(gx, baseline_y, ce, rgb, alpha, fl, px);
                    if ((fl & WF_BOLD) && !wf_real_bold(face)) draw_glyph(gx + (px >= 20 ? 2 : 1), baseline_y, ce, rgb, alpha, fl, px);
                }
            }
        }
        pen += a64;
    }
}
