/* SOMA_PARCHE_NAVEGADOR: motor CSS (analizador, selectores, cascada, propiedades calculadas). Solo enteros. */
#include "web_css.h"

/* ============================ colores ============================ */
static const char color_names[] =
 "aliceblue f0f8ff antiquewhite faebd7 aqua 00ffff aquamarine 7fffd4 azure f0ffff beige f5f5dc bisque ffe4c4 black 000000 blanchedalmond ffebcd blue 0000ff blueviolet 8a2be2 brown a52a2a burlywood deb887 cadetblue 5f9ea0 chartreuse 7fff00 chocolate d2691e coral ff7f50 cornflowerblue 6495ed cornsilk fff8dc crimson dc143c cyan 00ffff "
 "darkblue 00008b darkcyan 008b8b darkgoldenrod b8860b darkgray a9a9a9 darkgrey a9a9a9 darkgreen 006400 darkkhaki bdb76b darkmagenta 8b008b darkolivegreen 556b2f darkorange ff8c00 darkorchid 9932cc darkred 8b0000 darksalmon e9967a darkseagreen 8fbc8f darkslateblue 483d8b darkslategray 2f4f4f darkslategrey 2f4f4f darkturquoise 00ced1 darkviolet 9400d3 deeppink ff1493 deepskyblue 00bfff dimgray 696969 dimgrey 696969 dodgerblue 1e90ff "
 "firebrick b22222 floralwhite fffaf0 forestgreen 228b22 fuchsia ff00ff gainsboro dcdcdc ghostwhite f8f8ff gold ffd700 goldenrod daa520 gray 808080 grey 808080 green 008000 greenyellow adff2f honeydew f0fff0 hotpink ff69b4 indianred cd5c5c indigo 4b0082 ivory fffff0 khaki f0e68c "
 "lavender e6e6fa lavenderblush fff0f5 lawngreen 7cfc00 lemonchiffon fffacd lightblue add8e6 lightcoral f08080 lightcyan e0ffff lightgoldenrodyellow fafad2 lightgray d3d3d3 lightgrey d3d3d3 lightgreen 90ee90 lightpink ffb6c1 lightsalmon ffa07a lightseagreen 20b2aa lightskyblue 87cefa lightslategray 778899 lightslategrey 778899 lightsteelblue b0c4de lightyellow ffffe0 lime 00ff00 limegreen 32cd32 linen faf0e6 "
 "magenta ff00ff maroon 800000 mediumaquamarine 66cdaa mediumblue 0000cd mediumorchid ba55d3 mediumpurple 9370db mediumseagreen 3cb371 mediumslateblue 7b68ee mediumspringgreen 00fa9a mediumturquoise 48d1cc mediumvioletred c71585 midnightblue 191970 mintcream f5fffa mistyrose ffe4e1 moccasin ffe4b5 navajowhite ffdead navy 000080 "
 "oldlace fdf5e6 olive 808000 olivedrab 6b8e23 orange ffa500 orangered ff4500 orchid da70d6 palegoldenrod eee8aa palegreen 98fb98 paleturquoise afeeee palevioletred db7093 papayawhip ffefd5 peachpuff ffdab9 peru cd853f pink ffc0cb plum dda0dd powderblue b0e0e6 purple 800080 rebeccapurple 663399 red ff0000 rosybrown bc8f8f royalblue 4169e1 "
 "saddlebrown 8b4513 salmon fa8072 sandybrown f4a460 seagreen 2e8b57 seashell fff5ee sienna a0522d silver c0c0c0 skyblue 87ceeb slateblue 6a5acd slategray 708090 slategrey 708090 snow fffafa springgreen 00ff7f steelblue 4682b4 tan d2b48c teal 008080 thistle d8bfd8 tomato ff6347 turquoise 40e0d0 violet ee82ee wheat f5deb3 white ffffff whitesmoke f5f5f5 yellow ffff00 yellowgreen 9acd32";

static int parse_num(const char **pp, int32_t *out)   /* -> x1000 */
{
    const char *p = *pp; int neg = 0; int64_t v = 0; int any = 0, frac = 0, fd = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    while (wu_isdigit(*p) || *p == '.') {
        if (*p == '.') { if (frac) break; frac = 1; p++; continue; }
        any = 1;
        if (!frac) { if (v < 100000000) v = v * 10 + (*p - '0'); }
        else if (fd < 3) { v = v * 10 + (*p - '0'); fd++; }
        p++;
    }
    if (!any) return 0;
    while (frac && fd < 3) { v *= 10; fd++; }
    if (!frac) v *= 1000;
    if ((*p == 'e' || *p == 'E') && (wu_isdigit(p[1]) || ((p[1] == '-' || p[1] == '+') && wu_isdigit(p[2])))) {
        p++; int en = 0, es = 1; if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        while (wu_isdigit(*p)) { en = en * 10 + (*p - '0'); p++; }
        if (en > 6) en = 6;
        while (en--) { if (es > 0) v *= 10; else v /= 10; }
    }
    if (v > 2000000000LL) v = 2000000000LL;
    *out = neg ? -(int32_t)v : (int32_t)v; *pp = p; return 1;
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static uint32_t hsl_to_rgb(int h, int s, int l)   /* h 0..359, s,l 0..1000 */
{
    h %= 360; if (h < 0) h += 360;
    int c = (1000 - (2 * l > 1000 ? 2 * l - 1000 : 1000 - 2 * l)) * s / 1000;
    int hh = h * 1000 / 60; int m2 = hh % 2000; int x = c * (1000 - (m2 > 1000 ? m2 - 1000 : 1000 - m2)) / 1000;
    int m = l - c / 2, r = 0, g = 0, b = 0;
    switch (h / 60) { case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
                      case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x; }
    return ((uint32_t)clamp255((r + m) * 255 / 1000) << 16) | ((uint32_t)clamp255((g + m) * 255 / 1000) << 8) | (uint32_t)clamp255((b + m) * 255 / 1000);
}

#include "web_color.inc"
static uint32_t wc_parse_color_ex(const char *s, int *ok)
{
    int dummy; if (!ok) ok = &dummy;
    *ok = 0;
    while (wu_isspace(*s)) s++;
    if (!g_wl181_off && (wu_startsi(s, "hwb(") || wu_startsi(s, "oklch(") || wu_startsi(s, "oklab(") || wu_startsi(s, "lab(") || wu_startsi(s, "lch(") || wu_startsi(s, "color(") || wu_startsi(s, "color-mix(") || wu_startsi(s, "light-dark("))) {
        uint32_t c = 0; if (c181_func(s, &c)) { *ok = 1; return c; }
        return 0;
    }
    if (c181_cur_set && wu_startsi(s, "currentcolor")) { *ok = 1; return c181_cur; }
    if (*s == '#') {
        const char *h = s + 1; uint32_t n = 0; while (wu_ishex(h[n])) n++;
        uint32_t v = 0;
        if (n == 3 || n == 4) { for (uint32_t i = 0; i < n; i++) { uint32_t d = (uint32_t)wu_hexval(h[i]); v = (v << 8) | (d << 4) | d; }
            if (n == 3) v = (0xFFu << 24) | v; else v = ((v & 0xFF) << 24) | (v >> 8); *ok = 1; return v; }
        if (n == 6 || n == 8) { for (uint32_t i = 0; i < n; i++) v = (v << 4) | (uint32_t)wu_hexval(h[i]);
            if (n == 6) v |= 0xFF000000u; else v = ((v & 0xFF) << 24) | (v >> 8); *ok = 1; return v; }
        return 0;
    }
    if (wu_startsi(s, "rgb") || wu_startsi(s, "hsl")) {
        int hsl = wu_lower(s[0]) == 'h';
        const char *p = s; while (*p && *p != '(') p++;
        if (*p != '(') return 0;
        p++;
        int32_t v[4] = {0, 0, 0, 1000}; int pct[4] = {0, 0, 0, 0}, k = 0;
        while (*p && *p != ')' && k < 4) {
            while (wu_isspace(*p) || *p == ',' || *p == '/') p++;
            if (*p == ')') break;
            if (!parse_num(&p, &v[k])) return 0;
            if (*p == '%') { pct[k] = 1; p++; }
            else if (hsl && k == 0) { if (wu_startsi(p, "deg")) p += 3; else if (wu_startsi(p, "turn")) { v[0] *= 360; p += 4; } }
            k++;
        }
        if (k < 3) return 0;
        int a = 255;
        if (k == 4) a = clamp255(pct[3] ? v[3] * 255 / 100000 : v[3] * 255 / 1000);
        uint32_t rgb;
        if (hsl) rgb = hsl_to_rgb(v[0] / 1000, v[1] / 100, v[2] / 100);
        else {
            int c[3]; for (int i = 0; i < 3; i++) c[i] = clamp255(pct[i] ? v[i] * 255 / 100000 : (v[i] + 500) / 1000);
            rgb = ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
        }
        *ok = 1; return ((uint32_t)a << 24) | rgb;
    }
    if (wu_startsi(s, "transparent")) { *ok = 1; return 0; }
    {
        char nm[24]; uint32_t n = 0; while (wu_isalpha(s[n]) && n < 23) { nm[n] = (char)wu_lower(s[n]); n++; } nm[n] = 0;
        if (!n || wu_isalnum(s[n])) return 0;
        const char *p = color_names;
        while (*p) {
            const char *q = p; while (*q != ' ') q++;
            uint32_t l = (uint32_t)(q - p);
            const char *hx = q + 1; const char *e = hx; while (*e && *e != ' ') e++;
            if (l == n && wu_strneq(p, nm, n)) { uint32_t v = 0; for (int i = 0; i < 6; i++) v = (v << 4) | (uint32_t)wu_hexval(hx[i]); *ok = 1; return 0xFF000000u | v; }
            p = *e ? e + 1 : e;
        }
    }
    return 0;
}

uint32_t wc_parse_color(const char *s, int *ok) { return wc_parse_color_ex(s, ok); }

/* ============================ tokens y longitudes ============================ */
struct lctx { int font_px, root_px, vw, vh; };

/* extrae un token (palabra con parentesis anidados, cadena, o separador ,/) */
static const char *next_tok(const char *p, const char *end, const char **ts, uint32_t *tl)
{
    while (p < end && wu_isspace(*p)) p++;
    if (p >= end) return 0;
    *ts = p;
    if (*p == ',' || *p == '/') { p++; *tl = 1; return p; }
    if (*p == '"' || *p == '\'') { char q = *p++; while (p < end && *p != q) { if (*p == '\\' && p + 1 < end) p++; p++; } if (p < end) p++; *tl = (uint32_t)(p - *ts); return p; }
    int depth = 0;
    while (p < end) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth > 0) depth--; }
        else if (depth == 0 && (wu_isspace(*p) || *p == ',' || *p == '/')) { if (*p == '/' && p > *ts && p[-1] != ')' && 0) {} break; }
        else if ((*p == '"' || *p == '\'') && depth > 0) { char q = *p++; while (p < end && *p != q) p++; }
        p++;
    }
    *tl = (uint32_t)(p - *ts); return p;
}

struct lin { int64_t px, pct; int scalar; int64_t sc; };   /* px/pct en milesimas */

static int unit_len(const char *p, uint32_t n)
{
    uint32_t i = 0; while (i < n && (wu_isalpha(p[i]) || p[i] == '%')) i++; return (int)i;
}

static int calc_val(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth);

static int calc_factor(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
    if (p >= end || depth > 6) return 0;
    if (*p == '(') { p++; if (!calc_val(&p, end, cx, o, depth + 1)) return 0; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ')') p++; *pp = p; return 1; }
    if (wu_startsi(p, "calc(")) { p += 5; if (!calc_val(&p, end, cx, o, depth + 1)) return 0; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ')') p++; *pp = p; return 1; }
    if (wu_startsi(p, "min(") || wu_startsi(p, "max(") || wu_startsi(p, "clamp(")) {
        int isclamp = wu_lower(p[1]) == 'l', ismin = wu_lower(p[1]) == 'i'; p += isclamp ? 6 : 4;
        struct lin a[3]; int n = 0;
        while (n < 3) { if (!calc_val(&p, end, cx, &a[n], depth + 1)) return 0; n++; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ',') { p++; continue; } break; }
        if (p < end && *p == ')') p++;
        /* sin contenedor conocido: se elige por el valor en px (aprox. 800 px para %) */
        int64_t k[3]; for (int i = 0; i < n; i++) k[i] = a[i].px + a[i].pct * 8;
        int pick = 0;
        if (isclamp && n == 3) { pick = k[1] < k[0] ? 0 : (k[1] > k[2] ? 2 : 1); }
        else for (int i = 1; i < n; i++) if (ismin ? k[i] < k[pick] : k[i] > k[pick]) pick = i;
        *o = a[pick]; *pp = p; return 1;
    }
    int32_t v;
    if (!parse_num(&p, &v)) return 0;
    uint32_t ul = (uint32_t)unit_len(p, (uint32_t)(end - p));
    o->px = 0; o->pct = 0; o->scalar = 0; o->sc = v;
    if (ul == 0) { o->scalar = 1; }
    else if (ul == 1 && *p == '%') o->pct = v;
    else if (ul == 2 && wu_strnieq(p, "px", 2)) o->px = v;
    else if (ul == 2 && wu_strnieq(p, "em", 2)) o->px = (int64_t)v * cx->font_px;
    else if (ul == 3 && wu_strnieq(p, "rem", 3)) o->px = (int64_t)v * cx->root_px;
    else if (ul == 2 && wu_strnieq(p, "pt", 2)) o->px = (int64_t)v * 4 / 3;
    else if (ul == 3 && (wu_lower(p[0]) == 'd' || wu_lower(p[0]) == 's' || wu_lower(p[0]) == 'l') && wu_strnieq(p + 1, "vh", 2) && !g_wl181_off) o->px = (int64_t)v * cx->vh / 100;   /* SOMA_181: dvh/svh/lvh */
    else if (ul == 3 && (wu_lower(p[0]) == 'd' || wu_lower(p[0]) == 's' || wu_lower(p[0]) == 'l') && wu_strnieq(p + 1, "vw", 2) && !g_wl181_off) o->px = (int64_t)v * cx->vw / 100;
    else if (ul == 2 && wu_strnieq(p, "lh", 2) && !g_wl181_off) o->px = (int64_t)v * cx->font_px * 118 / 100;
    else if (ul == 3 && wu_strnieq(p, "rlh", 3) && !g_wl181_off) o->px = (int64_t)v * cx->root_px * 118 / 100;
    else if (ul == 2 && wu_strnieq(p, "vw", 2)) o->px = (int64_t)v * cx->vw / 100;
    else if (ul == 2 && wu_strnieq(p, "vh", 2)) o->px = (int64_t)v * cx->vh / 100;
    else if (ul == 4 && wu_strnieq(p, "vmin", 4)) o->px = (int64_t)v * (cx->vw < cx->vh ? cx->vw : cx->vh) / 100;
    else if (ul == 4 && wu_strnieq(p, "vmax", 4)) o->px = (int64_t)v * (cx->vw > cx->vh ? cx->vw : cx->vh) / 100;
    else if (ul == 2 && wu_strnieq(p, "ch", 2)) o->px = (int64_t)v * cx->font_px * (g_wl181_off ? 500 : 636) / 1000;   /* SOMA_181: ancho del 0 de la fuente (antes media em) */
    else if (ul == 2 && wu_strnieq(p, "ex", 2)) o->px = (int64_t)v * cx->font_px / 2;
    else if (ul == 2 && wu_strnieq(p, "cm", 2)) o->px = (int64_t)v * 3780 / 100;
    else if (ul == 2 && wu_strnieq(p, "mm", 2)) o->px = (int64_t)v * 378 / 100;
    else if (ul == 2 && wu_strnieq(p, "in", 2)) o->px = (int64_t)v * 96;
    else if (ul == 2 && wu_strnieq(p, "pc", 2)) o->px = (int64_t)v * 16;
    else if (ul == 1 && wu_lower(*p) == 'q') o->px = (int64_t)v * 945 / 1000;
    else if (ul == 2 && wu_strnieq(p, "fr", 2)) { o->scalar = 1; *pp = p; return 1; }
    else return 0;
    p += ul; *pp = p; return 1;
}

static int calc_term(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    if (!calc_factor(pp, end, cx, o, depth)) return 0;
    for (;;) {
        const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
        if (p >= end || (*p != '*' && *p != '/')) return 1;
        char op = *p++; *pp = p;
        struct lin b; if (!calc_factor(pp, end, cx, &b, depth)) return 0;
        if (op == '*') {
            if (b.scalar) { o->px = o->px * b.sc / 1000; o->pct = o->pct * b.sc / 1000; if (o->scalar) o->sc = o->sc * b.sc / 1000; }
            else if (o->scalar) { int64_t s = o->sc; *o = b; o->px = o->px * s / 1000; o->pct = o->pct * s / 1000; }
            else return 0;
        } else {
            if (!b.scalar || b.sc == 0) return 0;
            o->px = o->px * 1000 / b.sc; o->pct = o->pct * 1000 / b.sc; if (o->scalar) o->sc = o->sc * 1000 / b.sc;
        }
    }
}

static int calc_val(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    if (!calc_term(pp, end, cx, o, depth)) return 0;
    for (;;) {
        const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
        if (p >= end || (*p != '+' && *p != '-')) return 1;
        char op = *p++; *pp = p;
        struct lin b; if (!calc_term(pp, end, cx, &b, depth)) return 0;
        if (op == '+') { o->px += b.px; o->pct += b.pct; } else { o->px -= b.px; o->pct -= b.pct; }
        o->scalar = o->scalar && b.scalar; if (o->scalar) o->sc += (op == '+' ? b.sc : -b.sc);
    }
}

static int parse_len(const char *t, uint32_t n, const struct lctx *cx, struct wlen *o, int allow_num_as_px)
{
    char b[96]; if (n >= sizeof(b)) return 0;
    memcpy(b, t, n); b[n] = 0;
    memset(o, 0, sizeof(*o));
    if (wu_strieq(b, "auto")) { o->kind = LK_AUTO; return 1; }
    if (wu_strieq(b, "none") || wu_strieq(b, "normal")) { o->kind = LK_NONE; return 1; }
    if (wu_strieq(b, "max-content") || wu_strieq(b, "fit-content") || wu_strieq(b, "min-content") || wu_strieq(b, "-webkit-fill-available") || wu_strieq(b, "stretch")) { o->kind = LK_AUTO; return 1; }
    {
        const char *p = b; struct lin l;
        if (!calc_val(&p, b + n, cx, &l, 0)) return 0;
        while (wu_isspace(*p)) p++;
        if (*p && *p != ')') {
            /* fr */
            if (l.scalar == 0 && l.px == 0 && l.pct == 0) return 0;
        }
        if (*p && wu_strnieq(p, "fr", 2)) { o->kind = LK_FR; o->px = (int32_t)(l.sc / 10); return 1; }
        if (*p && *p != ')') return 0;
        if (l.scalar && !allow_num_as_px) { if (l.sc != 0) return 0; }
        o->kind = LK_VAL;
        o->px = (int32_t)((l.scalar ? l.sc : l.px + (l.px >= 0 ? 500 : -500)) / 1000);
        if (l.scalar) o->px = (int32_t)(l.sc / 1000);
        o->pct = (int32_t)(l.pct / 10);
    }
    return 1;
}

int wc_parse_length_px(const char *s, int base)
{
    struct lctx cx = { 16, 16, 1024, 768 }; struct wlen l;
    if (!parse_len(s, wu_strlen(s), &cx, &l, 1) || l.kind != LK_VAL) return 0;
    return l.px + (int)((int64_t)base * l.pct / 10000);
}

int wc_line_px(const struct wstyle *s)
{
    if (s->lh_mode == 2) return s->lh_val;
    if (s->lh_mode == 1) return (s->font_px * s->lh_val + 50) / 100;
    return (s->font_px * (g_wf182_on ? 115 : 118) + 50) / 100;   /* SOMA_182: 'normal' de Liberation Sans/Arial = 1,149 */
}

/* ============================ propiedades ============================ */
int g_wf182_on;                          /* SOMA_182: contenido web con Liberation Sans (lo fija web_page.c; 0 = DejaVu como antes) */
int g_wl181_off;                         /* SOMA_181: soma.weblayout=off (o modo seguro) deja la maquetacion como antes del parche 181 */
#define PROPLIST(X) \
 X(DISPLAY,"display") X(POSITION,"position") X(FLOAT,"float") X(CLEAR,"clear") X(VISIBILITY,"visibility") X(OVERFLOW,"overflow") \
 X(OVERFLOW_X,"overflow-x") X(OVERFLOW_Y,"overflow-y") X(Z_INDEX,"z-index") X(TOP,"top") X(RIGHT,"right") X(BOTTOM,"bottom") X(LEFT,"left") \
 X(INSET,"inset") X(WIDTH,"width") X(HEIGHT,"height") X(MIN_WIDTH,"min-width") X(MAX_WIDTH,"max-width") X(MIN_HEIGHT,"min-height") \
 X(MAX_HEIGHT,"max-height") X(MARGIN,"margin") X(MARGIN_TOP,"margin-top") X(MARGIN_RIGHT,"margin-right") X(MARGIN_BOTTOM,"margin-bottom") \
 X(MARGIN_LEFT,"margin-left") X(PADDING,"padding") X(PADDING_TOP,"padding-top") X(PADDING_RIGHT,"padding-right") X(PADDING_BOTTOM,"padding-bottom") \
 X(PADDING_LEFT,"padding-left") X(BOX_SIZING,"box-sizing") X(BORDER,"border") X(BORDER_TOP,"border-top") X(BORDER_RIGHT,"border-right") \
 X(BORDER_BOTTOM,"border-bottom") X(BORDER_LEFT,"border-left") X(BORDER_WIDTH,"border-width") X(BORDER_STYLE,"border-style") X(BORDER_COLOR,"border-color") \
 X(BORDER_TOP_WIDTH,"border-top-width") X(BORDER_RIGHT_WIDTH,"border-right-width") X(BORDER_BOTTOM_WIDTH,"border-bottom-width") X(BORDER_LEFT_WIDTH,"border-left-width") \
 X(BORDER_TOP_STYLE,"border-top-style") X(BORDER_RIGHT_STYLE,"border-right-style") X(BORDER_BOTTOM_STYLE,"border-bottom-style") X(BORDER_LEFT_STYLE,"border-left-style") \
 X(BORDER_TOP_COLOR,"border-top-color") X(BORDER_RIGHT_COLOR,"border-right-color") X(BORDER_BOTTOM_COLOR,"border-bottom-color") X(BORDER_LEFT_COLOR,"border-left-color") \
 X(BORDER_RADIUS,"border-radius") X(OPACITY,"opacity") X(FLEX,"flex") X(FLEX_GROW,"flex-grow") X(FLEX_SHRINK,"flex-shrink") X(FLEX_BASIS,"flex-basis") \
 X(FLEX_DIRECTION,"flex-direction") X(FLEX_WRAP,"flex-wrap") X(FLEX_FLOW,"flex-flow") X(JUSTIFY_CONTENT,"justify-content") X(ALIGN_ITEMS,"align-items") \
 X(ALIGN_SELF,"align-self") X(ALIGN_CONTENT,"align-content") X(ORDER,"order") X(GAP,"gap") X(GRID_GAP,"grid-gap") X(ROW_GAP,"row-gap") X(COLUMN_GAP,"column-gap") \
 X(GRID_TEMPLATE_COLUMNS,"grid-template-columns") X(GRID_COLUMN,"grid-column") X(COLOR,"color") X(FONT_SIZE,"font-size") X(FONT_WEIGHT,"font-weight") \
 X(FONT_STYLE,"font-style") X(FONT_FAMILY,"font-family") X(FONT,"font") X(LINE_HEIGHT,"line-height") X(TEXT_ALIGN,"text-align") X(TEXT_DECORATION,"text-decoration") \
 X(TEXT_DECORATION_LINE,"text-decoration-line") X(TEXT_TRANSFORM,"text-transform") X(TEXT_INDENT,"text-indent") X(LETTER_SPACING,"letter-spacing") \
 X(WHITE_SPACE,"white-space") X(VERTICAL_ALIGN,"vertical-align") X(TEXT_OVERFLOW,"text-overflow") X(WORD_BREAK,"word-break") X(OVERFLOW_WRAP,"overflow-wrap") \
 X(WORD_WRAP,"word-wrap") X(LIST_STYLE,"list-style") X(LIST_STYLE_TYPE,"list-style-type") X(LIST_STYLE_POSITION,"list-style-position") \
 X(BACKGROUND,"background") X(BACKGROUND_COLOR,"background-color") X(BACKGROUND_IMAGE,"background-image") X(BACKGROUND_REPEAT,"background-repeat") \
 X(BACKGROUND_SIZE,"background-size") X(BACKGROUND_POSITION,"background-position") X(BORDER_COLLAPSE,"border-collapse") X(BORDER_SPACING,"border-spacing") \
 X(CURSOR,"cursor") X(TOP_LEFT_RADIUS,"border-top-left-radius") X(TOP_RIGHT_RADIUS,"border-top-right-radius") X(BOTTOM_LEFT_RADIUS,"border-bottom-left-radius") \
 X(BOTTOM_RIGHT_RADIUS,"border-bottom-right-radius") X(PLACE_ITEMS,"place-items") X(INSET_BLOCK_START,"inset-block-start") X(MARGIN_INLINE,"margin-inline") \
 X(MARGIN_BLOCK,"margin-block") X(PADDING_INLINE,"padding-inline") X(PADDING_BLOCK,"padding-block") X(BACKGROUND_POSITION_X,"background-position-x") \
 X(BOX_SHADOW,"box-shadow") \
 X(TRANSFORM,"transform") X(TRANSLATE,"translate") X(ASPECT_RATIO,"aspect-ratio") X(OBJECT_FIT,"object-fit") X(OBJECT_POSITION,"object-position") X(LINE_CLAMP,"line-clamp") \
 X(GRID_TEMPLATE_ROWS,"grid-template-rows") X(GRID_AUTO_ROWS,"grid-auto-rows") X(GRID_AUTO_COLUMNS,"grid-auto-columns") X(GRID_TEMPLATE_AREAS,"grid-template-areas") X(GRID_AREA,"grid-area") X(GRID_ROW,"grid-row") \
 X(GRID_COLUMN_START,"grid-column-start") X(GRID_COLUMN_END,"grid-column-end") X(GRID_ROW_START,"grid-row-start") X(GRID_ROW_END,"grid-row-end") \
 X(JUSTIFY_ITEMS,"justify-items") X(JUSTIFY_SELF,"justify-self") X(PLACE_SELF,"place-self") X(CLIP,"clip") X(CLIP_PATH,"clip-path") X(POINTER_EVENTS,"pointer-events") X(CONTENT,"content") X(COUNTER_RESET,"counter-reset") X(COUNTER_INCREMENT,"counter-increment") X(COUNTER_SET,"counter-set") X(FILL,"fill") X(STROKE,"stroke") X(STROKE_WIDTH,"stroke-width") X(GRID_AUTO_FLOW,"grid-auto-flow") X(TRANSITION,"transition") X(TRANSITION_PROPERTY,"transition-property") X(TRANSITION_DURATION,"transition-duration") X(TRANSITION_DELAY,"transition-delay") X(ANIMATION,"animation") X(ANIMATION_NAME,"animation-name") X(ANIMATION_DURATION,"animation-duration") X(ANIMATION_DELAY,"animation-delay") X(ANIMATION_ITER,"animation-iteration-count") X(PLACE_CONTENT,"place-content") X(INSET_INLINE,"inset-inline") X(INSET_BLOCK,"inset-block")
static const char *findn(const char *h, uint32_t hl, const char *n);   /* SOMA_PARCHE_028_UI (definida mas abajo) */
enum { PR_NONE = 0,
#define X(a, b) PR_##a,
PROPLIST(X)
#undef X
PR__COUNT, PR_CUSTOM };
static const char *const prop_names[] = { "",
#define X(a, b) b,
PROPLIST(X)
#undef X
};

static int prop_id(const char *n, uint32_t l)
{
    if (l >= 2 && n[0] == '-' && n[1] == '-') return PR_CUSTOM;
    if (l > 8 && n[0] == '-' && n[1] == 'w' && wu_strneq(n, "-webkit-", 8)) { n += 8; l -= 8; }
    char b[40]; if (l >= sizeof(b)) return 0;
    for (uint32_t i = 0; i < l; i++) b[i] = (char)wu_lower(n[i]);
    b[l] = 0;
    for (int i = 1; i < PR__COUNT; i++) if (wu_streq(prop_names[i], b)) return i;
    /* SOMA_181: propiedades logicas (escritura de izquierda a derecha) */
    static const struct { const char *a, *b; } alias[] = {
        {"margin-inline-start", "margin-left"}, {"margin-inline-end", "margin-right"}, {"margin-block-start", "margin-top"}, {"margin-block-end", "margin-bottom"},
        {"padding-inline-start", "padding-left"}, {"padding-inline-end", "padding-right"}, {"padding-block-start", "padding-top"}, {"padding-block-end", "padding-bottom"},
        {"inset-inline-start", "left"}, {"inset-inline-end", "right"}, {"inset-block-end", "bottom"}, {"inline-size", "width"}, {"block-size", "height"},
        {"min-inline-size", "min-width"}, {"max-inline-size", "max-width"}, {"min-block-size", "min-height"}, {"max-block-size", "max-height"},
        {"border-inline-start-width", "border-left-width"}, {"border-inline-end-width", "border-right-width"}, {"border-block-start-width", "border-top-width"}, {"border-block-end-width", "border-bottom-width"},
        /* SOMA_188: mas alias logicos y nombres antiguos */
        {"inset-block-end", "bottom"}, {"grid-row-gap", "row-gap"}, {"grid-column-gap", "column-gap"},
        {"border-start-start-radius", "border-top-left-radius"}, {"border-start-end-radius", "border-top-right-radius"}, {"border-end-start-radius", "border-bottom-left-radius"}, {"border-end-end-radius", "border-bottom-right-radius"},
        {"border-inline-start", "border-left"}, {"border-inline-end", "border-right"}, {"border-block-start", "border-top"}, {"border-block-end", "border-bottom"},
        {"border-inline-start-color", "border-left-color"}, {"border-inline-end-color", "border-right-color"}, {"border-block-start-color", "border-top-color"}, {"border-block-end-color", "border-bottom-color"},
        {"border-inline-start-style", "border-left-style"}, {"border-inline-end-style", "border-right-style"}, {"border-block-start-style", "border-top-style"}, {"border-block-end-style", "border-bottom-style"},
    };
    for (unsigned k = 0; k < sizeof alias / sizeof alias[0]; k++) if (wu_streq(alias[k].a, b)) { for (int i = 1; i < PR__COUNT; i++) if (wu_streq(prop_names[i], alias[k].b)) return i; }
    return 0;
}

struct wdecl { uint16_t prop, vlen; uint8_t imp; const char *val; const char *name; uint16_t nlen; };

struct wsel;
struct wrule { struct wsel *sel; struct wdecl *decls; uint16_t nd; uint8_t origin; uint16_t minw, maxw; uint32_t order; struct wrule *bnext; };

struct wcss {
    struct wdoc *d;
    struct wstyle *phst;   /* SOMA_188: estilo temporal de ::placeholder */
    struct wrule **rules; int nrules, cap;
    struct wrule *univ; struct wrule *tagt[WT_COUNT]; struct wrule *idt[128]; struct wrule *clst[128];
    char **imports; int nimp, cimp, imp_next;
    uint32_t order;
    int vw, vh;
    struct wrule **match; int match_cap;
    char *scratch;                 /* expansion de var() */
    struct wlen cur_tmp;
    int pe_seen;
    /* SOMA_188: generado por ::before/::after y contadores CSS */
    char pc[512]; int pc_len;      /* ultimo valor de content aplicado (-1 = no hay) */
    char creset[160], cinc[160], cset[160];
    struct { char name[24]; int val; } cnt[24]; int ncnt;
    struct wvar **vtab;            /* SOMA_152: variables CSS internadas (sin fuga de arena por cada recalculo) */
    struct wsel **stab; const char **stab_key; /* SOMA_152: cache de selectores de querySelector/matches */
};

/* ---------------- aplicar propiedades ---------------- */
struct actx { struct lctx lc; const struct wstyle *par; struct wcss *c; };

static int is_kw(const char *t, uint32_t n, const char *k) { return wu_strlen(k) == n && wu_strnieq(t, k, n); }
static int tok_is_color(const char *t, uint32_t n) { char b[64]; if (n >= sizeof(b)) return 0; memcpy(b, t, n); b[n] = 0; int ok; wc_parse_color(b, &ok); return ok; }
static uint32_t tok_color(const char *t, uint32_t n, uint32_t cur, int *ok)
{
    char b[160]; *ok = 0; if (n >= sizeof(b)) return 0; memcpy(b, t, n); b[n] = 0;
    if (wu_strieq(b, "currentcolor")) { *ok = 1; return cur; }
    c181_cur_set = 1; c181_cur = cur;                   /* SOMA_181: currentcolor dentro de color-mix() */
    uint32_t r = wc_parse_color(b, ok);
    c181_cur_set = 0;
    return r;
}

static void set_len4(struct wlen *arr, int side_mask, const struct wlen *l) { for (int i = 0; i < 4; i++) if (side_mask & (1 << i)) arr[i] = *l; }

/* analiza hasta 4 longitudes (margin/padding) */
static void apply_box4(struct actx *a, struct wlen *arr, const char *v, uint32_t vl, int neg_ok)
{
    struct wlen l[4]; int n = 0; const char *p = v, *e = v + vl, *ts; uint32_t tl;
    while (n < 4 && (p = next_tok(p, e, &ts, &tl))) {
        if (!parse_len(ts, tl, &a->lc, &l[n], 1)) return;
        if (!neg_ok && l[n].kind == LK_VAL && (l[n].px < 0 || l[n].pct < 0)) return;
        n++;
    }
    if (!n) return;
    struct wlen t = l[0], r = n > 1 ? l[1] : l[0], b = n > 2 ? l[2] : l[0], le = n > 3 ? l[3] : r;
    arr[0] = t; arr[1] = r; arr[2] = b; arr[3] = le;
}

static int border_style_of(const char *t, uint32_t n)
{
    if (is_kw(t, n, "none") || is_kw(t, n, "hidden")) return 0;
    if (is_kw(t, n, "dashed")) return 2;
    if (is_kw(t, n, "dotted")) return 3;
    if (is_kw(t, n, "-soma-field")) return 4;    /* SOMA_188: borde de control (UA): 2px de caja, se pinta como el tema */
    if (is_kw(t, n, "-soma-button")) return 5;
    if (is_kw(t, n, "solid") || is_kw(t, n, "double") || is_kw(t, n, "groove") || is_kw(t, n, "ridge") || is_kw(t, n, "inset") || is_kw(t, n, "outset")) return 1;
    return -1;
}

static void apply_border_one(struct actx *a, struct wstyle *st, int mask, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int w = 3, sty = 0, ok; uint32_t col = st->color; int seen_style = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        int bs = border_style_of(ts, tl);
        if (bs >= 0) { sty = bs; seen_style = 1; continue; }
        if (is_kw(ts, tl, "thin")) { w = 1; continue; } if (is_kw(ts, tl, "medium")) { w = 3; continue; } if (is_kw(ts, tl, "thick")) { w = 5; continue; }
        uint32_t c = tok_color(ts, tl, st->color, &ok);
        if (ok) { col = c; continue; }
        struct wlen l; if (parse_len(ts, tl, &a->lc, &l, 0) && l.kind == LK_VAL) w = l.px;
    }
    if (!seen_style) sty = 0;
    if (w < 0) w = 0;
    if (w > 200) w = 200;
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) { st->border_w[i] = (int16_t)(sty ? w : 0); st->border_style[i] = (uint8_t)sty; st->border_color[i] = col; }
}

/* SOMA_188: escapes CSS en url()/cadenas: "\00003c" -> '<' (hasta 6 cifras hex + un espacio opcional), "\X" -> 'X'.
   Google y YouTube ponen asi sus iconos SVG: data:image/svg+xml,\00003csvg ... (antes: "formato desconocido") */
static char *css_unescape188(struct wa_arena *ar, const char *s, uint32_t n)
{
    int has = 0; for (uint32_t i = 0; i < n; i++) if (s[i] == '\\') { has = 1; break; }
    if (!has || g_wl181_off) return wa_strndup(ar, s, n);
    char *o = (char *)wa_alloc(ar, n + 1); if (!o) return 0; uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] != '\\' || i + 1 >= n) { o[k++] = s[i]; continue; }
        i++;
        if (wu_ishex((unsigned char)s[i])) {
            uint32_t cp = 0; int d = 0; while (i < n && d < 6 && wu_ishex((unsigned char)s[i])) { cp = cp * 16 + (uint32_t)wu_hexval(s[i]); i++; d++; }
            if (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
            i--;
            if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
            if (cp < 0x80) o[k++] = (char)cp;
            else if (cp < 0x800) { o[k++] = (char)(0xC0 | (cp >> 6)); o[k++] = (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { o[k++] = (char)(0xE0 | (cp >> 12)); o[k++] = (char)(0x80 | ((cp >> 6) & 63)); o[k++] = (char)(0x80 | (cp & 63)); }
            else { o[k++] = (char)(0xF0 | (cp >> 18)); o[k++] = (char)(0x80 | ((cp >> 12) & 63)); o[k++] = (char)(0x80 | ((cp >> 6) & 63)); o[k++] = (char)(0x80 | (cp & 63)); }
        } else if (s[i] == '\n') { /* continuacion de linea */ }
        else o[k++] = s[i];
    }
    o[k] = 0; return o;
}
static void apply_bg_token(struct actx *a, struct wstyle *st, const char *ts, uint32_t tl, struct wcss *c)
{
    (void)c;
    int ok; uint32_t col = tok_color(ts, tl, st->color, &ok);
    if (ok && !(tl > 3 && wu_strnieq(ts, "url", 3))) { st->bg_color = col; return; }
    if (tl > 4 && wu_strnieq(ts, "url(", 4)) {
        const char *p = ts + 4, *e = ts + tl - 1; while (p < e && wu_isspace(*p)) p++;
        char q = 0; if (p < e && (*p == '"' || *p == '\'')) q = *p++;
        const char *s = p; while (p < e && (q ? *p != q : !wu_isspace(*p))) { if (*p == '\\' && p + 1 < e) p++; p++; }
        st->bg_image = css_unescape188(&a->c->d->ar, s, (uint32_t)(p - s)); return;
    }
    if (tl > 16 && (wu_strnieq(ts, "linear-gradient(", 16) || wu_strnieq(ts, "repeating-linear-gradient(", 26) || 0)) {
        const char *p = ts + 16, *e = ts + tl - 1; const char *s2; uint32_t l2; int n = 0; uint32_t first = 0, last = 0; int dir = 0, rev = 0;
        if (wu_strnieq(ts, "repeating", 9)) p = ts + 26;
        while ((p = next_tok(p, e, &s2, &l2))) {
            if (l2 == 1 && *s2 == ',') continue;
            if (n == 0 && (is_kw(s2, l2, "to") )) {
                const char *s3; uint32_t l3; p = next_tok(p, e, &s3, &l3); if (p && (is_kw(s3, l3, "right") || is_kw(s3, l3, "left"))) { dir = 1; rev = is_kw(s3, l3, "left"); } else if (p && is_kw(s3, l3, "top")) rev = 1;
                const char *s4; uint32_t l4; const char *p2 = next_tok(p, e, &s4, &l4); (void)p2; (void)s4; (void)l4;
                continue;
            }
            if (l2 && (wu_isdigit(*s2) || *s2 == '-') && l2 > 3 && wu_strnieq(s2 + l2 - 3, "deg", 3)) { int d = wu_atoi(s2); d = ((d % 360) + 360) % 360; if (d == 90 || d == 270) dir = 1; if (d == 270 || d == 0) rev = 1; continue; }
            int ok2; uint32_t cc = tok_color(s2, l2, st->color, &ok2);
            if (ok2) { if (n == 0) first = cc; last = cc; n++; }
        }
        if (n >= 1) { st->has_grad = 1; st->grad_dir = (uint8_t)dir; st->grad_a = rev ? last : first; st->grad_b = rev ? first : last; if (n == 1) st->grad_b = st->grad_a; }
        return;
    }
    if ((tl > 16 && wu_strnieq(ts, "radial-gradient(", 16)) || (tl > 15 && wu_strnieq(ts, "conic-gradient(", 15)) || (tl > 26 && wu_strnieq(ts, "repeating-radial-gradient(", 26))) {
        /* SOMA_PARCHE_028_UI: los degradados radiales y conicos se aproximan con uno lineal vertical entre su primer y su ultimo color */
        const char *p = ts; while (p < ts + tl && *p != '(') p++; p++;
        const char *e = ts + tl - 1, *s2; uint32_t l2; int n = 0; uint32_t first = 0, last = 0;
        while ((p = next_tok(p, e, &s2, &l2))) { int ok2; uint32_t cc = tok_color(s2, l2, st->color, &ok2); if (ok2) { if (n == 0) first = cc; last = cc; n++; } }
        if (n >= 1) { st->has_grad = 1; st->grad_dir = 0; st->grad_a = first; st->grad_b = n > 1 ? last : first; }
        return;
    }
    if (is_kw(ts, tl, "no-repeat")) st->bg_repeat = 3; else if (is_kw(ts, tl, "repeat-x")) st->bg_repeat = 1; else if (is_kw(ts, tl, "repeat-y")) st->bg_repeat = 2; else if (is_kw(ts, tl, "repeat")) st->bg_repeat = 0;
    else if (is_kw(ts, tl, "none")) { st->bg_image = 0; st->has_grad = 0; }
    else if (is_kw(ts, tl, "cover")) st->bg_size_mode = 1; else if (is_kw(ts, tl, "contain")) st->bg_size_mode = 2;
    else if (is_kw(ts, tl, "center")) { st->bg_pos_x = 50; st->bg_pos_y = 50; }
    else if (is_kw(ts, tl, "left")) st->bg_pos_x = 0; else if (is_kw(ts, tl, "right")) st->bg_pos_x = 100;
    else if (is_kw(ts, tl, "top")) st->bg_pos_y = 0; else if (is_kw(ts, tl, "bottom")) st->bg_pos_y = 100;
}


/* SOMA_181: posicion y tamano de fondo completos (keywords, porcentajes y desplazamientos en px, "x / tamano") */
enum { BK_NONE = 0, BK_L, BK_R, BK_T, BK_B, BK_C };
struct bgpt { uint8_t kw; uint8_t has_len; struct wlen len; };
static int bg_kw_of(const char *t, uint32_t n)
{
    if (is_kw(t, n, "left")) return BK_L;
    if (is_kw(t, n, "right")) return BK_R;
    if (is_kw(t, n, "top")) return BK_T;
    if (is_kw(t, n, "bottom")) return BK_B;
    if (is_kw(t, n, "center")) return BK_C;
    return BK_NONE;
}
static int bg_is_lenlike(const char *t, uint32_t n) { return n && (wu_isdigit(t[0]) || t[0] == '.' || t[0] == '-' || t[0] == '+' || (n > 5 && (wu_strnieq(t, "calc(", 5) || wu_strnieq(t, "var(", 4)))); }
static void bg_axis(const struct bgpt *p, int *pct, int *off)
{
    if (p->kw) { *pct = p->kw == BK_L || p->kw == BK_T ? 0 : p->kw == BK_C ? 50 : 100; *off = 0; return; }
    if (p->has_len && p->len.kind == LK_VAL) { *pct = p->len.pct / 100; *off = p->len.px; return; }
    *pct = 50; *off = 0;
}
static void bg_set_pos(struct wstyle *st, const struct bgpt *pt, int n)
{
    int xp = 50, yp = 50, xo = 0, yo = 0;
    if (n == 1) {
        if (pt[0].kw == BK_T || pt[0].kw == BK_B) bg_axis(&pt[0], &yp, &yo); else bg_axis(&pt[0], &xp, &xo);
    } else if (n == 2) {
        const struct bgpt *a = &pt[0], *b = &pt[1];
        if (a->kw == BK_T || a->kw == BK_B || b->kw == BK_L || b->kw == BK_R) { const struct bgpt *t = a; a = b; b = t; }
        bg_axis(a, &xp, &xo); bg_axis(b, &yp, &yo);
    } else {
        int sx = 0, sy = 0;
        for (int i = 0; i < n; i++) {
            if (!pt[i].kw) continue;
            int off = 0, has = 0; if (i + 1 < n && !pt[i + 1].kw && pt[i + 1].has_len && pt[i + 1].len.kind == LK_VAL) { off = pt[i + 1].len.px; has = 1; }
            int isy = pt[i].kw == BK_T || pt[i].kw == BK_B || (pt[i].kw == BK_C && sx);
            int pc = pt[i].kw == BK_L || pt[i].kw == BK_T ? 0 : pt[i].kw == BK_C ? 50 : 100;
            if (pt[i].kw == BK_R || pt[i].kw == BK_B) off = -off;
            if (isy && !sy) { yp = pc; yo = off; sy = 1; } else if (!isy && !sx) { xp = pc; xo = off; sx = 1; }
            if (has) i++;
        }
    }
    st->bg_pos_x = (int16_t)xp; st->bg_pos_y = (int16_t)yp;
    st->bg_off_x = (int16_t)(xo < -30000 ? -30000 : xo > 30000 ? 30000 : xo); st->bg_off_y = (int16_t)(yo < -30000 ? -30000 : yo > 30000 ? 30000 : yo);
}
/* una componente de background-size; devuelve 1 si el token lo era */
static int bg_size_tok(struct actx *a, struct wstyle *st, const char *ts, uint32_t tl, int *idx)
{
    struct wlen l;
    if (is_kw(ts, tl, "cover")) { st->bg_size_mode = 1; *idx = 2; return 1; }
    if (is_kw(ts, tl, "contain")) { st->bg_size_mode = 2; *idx = 2; return 1; }
    int isauto = is_kw(ts, tl, "auto");
    if (!isauto && !(bg_is_lenlike(ts, tl) && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL)) return 0;
    if (isauto) { l.kind = LK_AUTO; l.px = 0; l.pct = 0; }
    if (*idx == 0) { st->bg_w = l; st->bg_h.kind = LK_AUTO; st->bg_size_mode = 3; } else if (*idx == 1) st->bg_h = l;
    (*idx)++; return 1;
}
/* "background" (todo = 1) o background-position/-size: reparte los tokens */
static void apply_bg_geometry(struct actx *a, struct wstyle *st, const char *v, uint32_t vl, int todo, int only_size)
{
    struct bgpt pt[4]; int np = 0, after_slash = 0, sidx = 0; const char *p = v, *e = v + vl, *ts; uint32_t tl; struct wlen l;
    int have_pos = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        if (tl == 1 && ts[0] == '/') { after_slash = 1; sidx = 0; continue; }
        if (tl == 1 && ts[0] == ',') break;                         /* varias capas: solo la primera */
        if (only_size) { bg_size_tok(a, st, ts, tl, &sidx); continue; }
        if (after_slash && bg_size_tok(a, st, ts, tl, &sidx)) continue;
        after_slash = 0;
        int kw = bg_kw_of(ts, tl);
        if (kw && np < 4) { pt[np].kw = (uint8_t)kw; pt[np].has_len = 0; np++; have_pos = 1; continue; }
        if (!kw && bg_is_lenlike(ts, tl) && np < 4 && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL) { pt[np].kw = 0; pt[np].has_len = 1; pt[np].len = l; np++; have_pos = 1; continue; }
        if (todo) apply_bg_token(a, st, ts, tl, a->c);
        else if (!todo && (is_kw(ts, tl, "no-repeat") || is_kw(ts, tl, "repeat"))) apply_bg_token(a, st, ts, tl, a->c);
    }
    if (have_pos && !only_size) bg_set_pos(st, pt, np);
}

static int family_is_mono(const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl;
    while (p < e) {
        while (p < e && (wu_isspace(*p) || *p == ',' || *p == '"' || *p == '\'')) p++;
        const char *s = p; while (p < e && *p != ',') p++;
        uint32_t l = (uint32_t)(p - s); char b[48]; if (l >= sizeof(b)) l = sizeof(b) - 1;
        for (uint32_t i = 0; i < l; i++) b[i] = (char)wu_lower(s[i]);
        b[l] = 0;
        if (wu_find(b, "mono") || wu_find(b, "courier") || wu_find(b, "consolas") || wu_find(b, "menlo") || wu_find(b, "typewriter") || wu_find(b, "terminal")) return 1;
        if (wu_find(b, "sans") || wu_find(b, "serif") || wu_find(b, "arial") || wu_find(b, "helvetica") || wu_find(b, "verdana") || wu_find(b, "segoe") ||
            wu_find(b, "roboto") || wu_find(b, "system-ui") || wu_find(b, "georgia") || wu_find(b, "times") || wu_find(b, "tahoma") || wu_find(b, "inter") || wu_find(b, "cursive")) return 0;
    }
    return 0;
}

static int font_size_of(struct actx *a, const char *t, uint32_t n, int *out)
{
    static const struct { const char *k; int px; } kw[] = { {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18}, {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48} };
    for (unsigned i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) if (is_kw(t, n, kw[i].k)) { *out = kw[i].px; return 1; }
    if (is_kw(t, n, "smaller")) { *out = a->lc.font_px * 83 / 100; return 1; }
    if (is_kw(t, n, "larger")) { *out = a->lc.font_px * 120 / 100; return 1; }
    struct wlen l; if (!parse_len(t, n, &a->lc, &l, 0) || l.kind != LK_VAL) return 0;
    int px = l.px + (int)((int64_t)a->lc.font_px * l.pct / 10000);
    if (px < 0) return 0;
    *out = px; return 1;
}

static void apply_lh(struct actx *a, struct wstyle *st, const char *t, uint32_t n)
{
    if (is_kw(t, n, "normal")) { st->lh_mode = 0; return; }
    char b[32]; if (n >= sizeof(b)) return; memcpy(b, t, n); b[n] = 0;
    const char *p = b; int32_t v;
    if (parse_num(&p, &v) && *p == 0) { st->lh_mode = 1; st->lh_val = (int16_t)(v / 10); return; }
    struct wlen l; if (!parse_len(t, n, &a->lc, &l, 0) || l.kind != LK_VAL) return;
    st->lh_mode = 2; st->lh_val = (int16_t)(l.px + (int)((int64_t)st->font_px * l.pct / 10000));
}

static void apply_font_shorthand(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int got_size = 0;
    st->italic = 0; st->bold = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        if (is_kw(ts, tl, "italic") || is_kw(ts, tl, "oblique")) { st->italic = 1; continue; }
        if (is_kw(ts, tl, "bold") || is_kw(ts, tl, "bolder")) { st->bold = 1; continue; }
        if (is_kw(ts, tl, "normal") || is_kw(ts, tl, "small-caps")) continue;
        if (tl == 3 && wu_isdigit(ts[0]) && ts[1] == '0' && ts[2] == '0') { st->bold = ts[0] >= '6'; continue; }
        if (!got_size) {
            const char *sl = ts; while (sl < ts + tl && *sl != '/') sl++;
            int px; if (font_size_of(a, ts, (uint32_t)(sl - ts), &px)) {
                st->font_px = (int16_t)(px < 1 ? 1 : (px > 400 ? 400 : px)); a->lc.font_px = st->font_px; got_size = 1;
                if (sl < ts + tl) apply_lh(a, st, sl + 1, (uint32_t)(ts + tl - sl - 1));
                continue;
            }
        } else {
            if (tl == 1 && *ts == '/') { const char *q = next_tok(p, e, &ts, &tl); if (q) { apply_lh(a, st, ts, tl); p = q; } continue; }
            st->mono = (uint8_t)family_is_mono(ts, (uint32_t)(e - ts));
            break;
        }
    }
}

static void apply_flex(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int idx = 0;
    if (is_kw(v, vl, "none")) { st->flex_grow = 0; st->flex_shrink = 0; st->flex_basis.kind = LK_AUTO; return; }
    if (is_kw(v, vl, "auto")) { st->flex_grow = 100; st->flex_shrink = 100; st->flex_basis.kind = LK_AUTO; return; }
    st->flex_grow = 0; st->flex_shrink = 100; st->flex_basis.kind = LK_VAL; st->flex_basis.px = 0; st->flex_basis.pct = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        char b[24]; if (tl >= sizeof(b)) continue; memcpy(b, ts, tl); b[tl] = 0;
        const char *q = b; int32_t nv;
        if (parse_num(&q, &nv) && *q == 0 && idx < 2) { if (idx == 0) st->flex_grow = (int16_t)(nv / 10); else st->flex_shrink = (int16_t)(nv / 10); idx++; if (idx == 1 && 0) {} continue; }
        struct wlen l; if (parse_len(ts, tl, &a->lc, &l, 0)) st->flex_basis = l;
    }
}

static int align_of(const char *t, uint32_t n)
{
    if (is_kw(t, n, "stretch") || is_kw(t, n, "normal")) return AI_STRETCH;
    if (is_kw(t, n, "flex-start") || is_kw(t, n, "start") || is_kw(t, n, "self-start") || is_kw(t, n, "left")) return AI_START;
    if (is_kw(t, n, "flex-end") || is_kw(t, n, "end") || is_kw(t, n, "self-end") || is_kw(t, n, "right")) return AI_END;
    if (is_kw(t, n, "center")) return AI_CENTER;
    if (is_kw(t, n, "baseline")) return AI_BASELINE;
    if (is_kw(t, n, "auto")) return AI_AUTO;
    if (is_kw(t, n, "space-between")) return 10 + JC_BETWEEN;
    if (is_kw(t, n, "space-around")) return 10 + JC_AROUND;
    if (is_kw(t, n, "space-evenly")) return 10 + JC_EVENLY;
    return -1;
}

/* lista de pistas ("200px 1fr repeat(3, 1fr) minmax(0, 1fr) auto"); SOMA_181: hasta max pistas y minmax con su minimo (repeat(auto-fill, minmax(N, 1fr))) */
static int parse_tracks(struct actx *a, const char *v, uint32_t vl, struct wlen *out, int max, int *auto_min)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int n = 0;
    if (auto_min) *auto_min = 0;
    while (n < max && (p = next_tok(p, e, &ts, &tl))) {
        if (tl > 7 && wu_strnieq(ts, "repeat(", 7)) {
            const char *q = ts + 7, *qe = ts + tl - 1, *s2; uint32_t l2; int cnt = 0; int autoc = 0; int minpx = 0;
            q = next_tok(q, qe, &s2, &l2); if (!q) continue;
            if (is_kw(s2, l2, "auto-fill") || is_kw(s2, l2, "auto-fit")) autoc = 1; else cnt = wu_atoi(s2);
            q = next_tok(q, qe, &s2, &l2); /* coma */
            struct wlen tr[4]; int nt = 0;
            while (nt < 4 && (q = next_tok(q, qe, &s2, &l2))) {
                if (l2 == 1 && s2[0] == ',') continue;
                if (l2 > 7 && wu_strnieq(s2, "minmax(", 7)) {
                    const char *m = s2 + 7, *me = s2 + l2 - 1, *s3, *s4; uint32_t l3, l4 = 0; struct wlen mn;
                    m = next_tok(m, me, &s3, &l3);                 /* minimo */
                    s4 = 0; if (m) { m = next_tok(m, me, &s4, &l4); if (m) m = next_tok(m, me, &s4, &l4); }   /* coma y maximo */
                    if (autoc && nt == 0 && parse_len(s3, l3, &a->lc, &mn, 0) && mn.kind == LK_VAL && mn.px > 0) minpx = mn.px;
                    if (s4 && parse_len(s4, l4, &a->lc, &tr[nt], 0) && tr[nt].kind != LK_NONE) nt++; else { tr[nt].kind = LK_FR; tr[nt].px = 100; tr[nt].pct = 0; nt++; }
                } else if (parse_len(s2, l2, &a->lc, &tr[nt], 0)) { if (autoc && nt == 0 && tr[0].kind == LK_VAL && tr[0].px > 0) minpx = tr[0].px; nt++; }
            }
            if (nt == 0) continue;
            if (autoc) { int w = minpx > 0 ? minpx : 200; if (auto_min) *auto_min = (int)(w > 4000 ? 4000 : w); cnt = a->c->vw / (w + 16); if (cnt < 1) cnt = 1; if (cnt > max) cnt = max; if (tr[0].kind == LK_VAL) { tr[0].kind = LK_FR; tr[0].px = 100; } }
            for (int c2 = 0; c2 < cnt && n < max; c2++) for (int k = 0; k < nt && n < max; k++) out[n++] = tr[k];
            continue;
        }
        if (tl > 7 && wu_strnieq(ts, "minmax(", 7)) {
            const char *m = ts + 7, *me = ts + tl - 1, *s3; uint32_t l3; m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3);
            if (!parse_len(s3, l3, &a->lc, &out[n], 0) || out[n].kind == LK_NONE) { out[n].kind = LK_FR; out[n].px = 100; out[n].pct = 0; }
            n++; continue;
        }
        if (is_kw(ts, tl, "none")) continue;
        if (ts[0] == '[') continue;
        if (is_kw(ts, tl, "min-content") || is_kw(ts, tl, "max-content") || is_kw(ts, tl, "auto")) { memset(&out[n], 0, sizeof out[n]); out[n].kind = LK_AUTO; n++; continue; }
        if (tl > 4 && wu_strnieq(ts, "fit-content(", 12)) { memset(&out[n], 0, sizeof out[n]); out[n].kind = LK_AUTO; n++; continue; }
        if (parse_len(ts, tl, &a->lc, &out[n], 0)) n++;
    }
    return n;
}
static void apply_grid_cols(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    int am = 0;
    memset(st->grid_tracks, 0, sizeof(st->grid_tracks)); st->grid_cols = 0; st->grid_auto_min = 0;
    st->grid_cols = (int16_t)parse_tracks(a, v, vl, st->grid_tracks, GRID_MAX_TRACKS, &am); st->grid_auto_min = (int16_t)am;
}
static struct wgx *gx_of(struct actx *a, struct wstyle *st)
{
    if (!st->gx) { st->gx = (struct wgx *)wa_alloc(&a->c->d->ar, sizeof(struct wgx)); if (st->gx) { memset(st->gx, 0, sizeof *st->gx); st->gx->auto_rows.kind = LK_AUTO; } }
    return st->gx;
}
/* "a / b" de grid-column / grid-row: 2, span 2, 1 / 3, 1 / span 2, 1 / -1 */
static void grid_place(const char *v, uint32_t vl, int16_t *gs, int16_t *ge, int16_t *gsp)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int part = 0, span_next = 0;
    *gs = *ge = *gsp = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        if (tl == 1 && ts[0] == '/') { part = 1; span_next = 0; continue; }
        if (is_kw(ts, tl, "span")) { span_next = 1; continue; }
        if (is_kw(ts, tl, "auto")) continue;
        char b[16]; if (tl >= sizeof b) continue; memcpy(b, ts, tl); b[tl] = 0;
        if (!(wu_isdigit(b[0]) || b[0] == '-' || b[0] == '+')) continue;      /* lineas con nombre: sin soporte */
        int nv = wu_atoi(b); if (nv > 1000) nv = 1000; if (nv < -1000) nv = -1000;
        if (span_next) { *gsp = (int16_t)(nv < 1 ? 1 : nv); span_next = 0; }
        else if (part == 0) *gs = (int16_t)nv; else *ge = (int16_t)nv;
    }
}

static void apply_prop(struct actx *a, struct wstyle *st, int prop, const char *v, uint32_t vl, const char *cname, uint32_t cnl)
{
    while (vl && wu_isspace(v[0])) { v++; vl--; }
    while (vl && wu_isspace(v[vl - 1])) vl--;
    if (!vl && prop != PR_CUSTOM) return;
    if (prop == PR_CONTENT) { struct wcss *cc = a->c; uint32_t n = vl < sizeof cc->pc - 1 ? vl : (uint32_t)sizeof cc->pc - 1; memcpy(cc->pc, v, n); cc->pc[n] = 0; cc->pc_len = (int)n; return; }   /* SOMA_188 */
    if (prop == PR_COUNTER_RESET || prop == PR_COUNTER_INCREMENT || prop == PR_COUNTER_SET) {
        struct wcss *cc = a->c; char *dst = prop == PR_COUNTER_RESET ? cc->creset : prop == PR_COUNTER_SET ? cc->cset : cc->cinc;
        uint32_t n = vl < 159 ? vl : 159; memcpy(dst, v, n); dst[n] = 0; return;
    }
    const struct wstyle *par = a->par;
    struct wlen l; const char *ts; uint32_t tl; int ok;
    int inherit = is_kw(v, vl, "inherit");
    if (is_kw(v, vl, "initial") || is_kw(v, vl, "unset") || inherit) {
        /* soporte limitado: inherit/unset de las propiedades mas comunes */
        if (inherit || is_kw(v, vl, "unset")) {
            switch (prop) {
            case PR_COLOR: st->color = par->color; return;
            case PR_FONT_SIZE: st->font_px = par->font_px; st->fs_zero = par->fs_zero; return;
            case PR_FONT_WEIGHT: st->bold = par->bold; return;
            case PR_TEXT_ALIGN: st->text_align = par->text_align; return;
            case PR_BACKGROUND_COLOR: st->bg_color = par->bg_color; return;
            case PR_VISIBILITY: st->visibility = par->visibility; return;
            case PR_POINTER_EVENTS: st->no_ptr = par->no_ptr; return;
            case PR_LINE_HEIGHT: st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; return;
            case PR_DISPLAY: st->display = par->display; return;
            case PR_WIDTH: st->width = par->width; return;
            case PR_HEIGHT: st->height = par->height; return;
            case PR_BORDER_COLOR: for (int i = 0; i < 4; i++) st->border_color[i] = par->border_color[i]; return;
            default: break;
            }
        }
        if (prop == PR_BACKGROUND_COLOR || prop == PR_BACKGROUND) { st->bg_color = 0; if (prop == PR_BACKGROUND) { st->bg_image = 0; st->has_grad = 0; } return; }
        if (prop == PR_WIDTH) { st->width.kind = LK_AUTO; return; } if (prop == PR_HEIGHT) { st->height.kind = LK_AUTO; return; }
        if (prop == PR_DISPLAY) { st->display = D_INLINE; return; }
        if (prop == PR_COLOR) { st->color = 0xFF000000u; return; }
        if (prop == PR_OPACITY) { st->opacity = 255; return; }
        if (prop == PR_MARGIN) { for (int i = 0; i < 4; i++) { st->margin[i].kind = LK_VAL; st->margin[i].px = st->margin[i].pct = 0; } return; }
        if (prop == PR_PADDING) { for (int i = 0; i < 4; i++) { st->padding[i].kind = LK_VAL; st->padding[i].px = st->padding[i].pct = 0; } return; }
        if (prop == PR_BORDER) { for (int i = 0; i < 4; i++) { st->border_w[i] = 0; st->border_style[i] = 0; } return; }
        return;
    }
    switch (prop) {
    case PR_DISPLAY: {
        static const struct { const char *k; uint8_t d; } dk[] = { {"none", D_NONE}, {"block", D_BLOCK}, {"inline", D_INLINE}, {"inline-block", D_INLINE_BLOCK}, {"list-item", D_LIST_ITEM},
            {"flex", D_FLEX}, {"inline-flex", D_INLINE_FLEX}, {"-webkit-box", D_FLEX}, {"-webkit-inline-box", D_INLINE_FLEX}, {"grid", D_GRID}, {"inline-grid", D_GRID}, {"table", D_TABLE}, {"inline-table", D_INLINE_TABLE},
            {"table-row", D_TABLE_ROW}, {"table-cell", D_TABLE_CELL}, {"table-row-group", D_TABLE_GROUP}, {"table-header-group", D_TABLE_GROUP}, {"table-footer-group", D_TABLE_GROUP},
            {"table-caption", D_TABLE_CAPTION}, {"table-column", D_NONE}, {"table-column-group", D_NONE}, {"contents", D_CONTENTS}, {"flow-root", D_BLOCK}, {"ruby", D_INLINE} };
        for (unsigned i = 0; i < sizeof(dk) / sizeof(dk[0]); i++) if (is_kw(v, vl, dk[i].k)) { st->display = dk[i].d; return; }
        return; }
    case PR_POSITION: st->position = is_kw(v, vl, "relative") ? P_RELATIVE : is_kw(v, vl, "absolute") ? P_ABSOLUTE : is_kw(v, vl, "fixed") ? P_FIXED : is_kw(v, vl, "sticky") ? P_STICKY : P_STATIC; return;
    case PR_FLOAT: st->floating = is_kw(v, vl, "left") || is_kw(v, vl, "inline-start") ? FL_LEFT : is_kw(v, vl, "right") || is_kw(v, vl, "inline-end") ? FL_RIGHT : FL_NONE; return;
    case PR_CLEAR: st->clear = is_kw(v, vl, "left") ? 1 : is_kw(v, vl, "right") ? 2 : is_kw(v, vl, "both") ? 3 : 0; return;
    case PR_VISIBILITY: st->visibility = !is_kw(v, vl, "visible"); return;
    case PR_CLIP: case PR_CLIP_PATH: {   /* SOMA_187A: solo el idioma "visualmente oculto" (rect(0,0,0,0), inset(50%)); el recorte general no se dibuja */
        int hide = 0;
        if (prop == PR_CLIP && vl > 5 && !memcmp(v, "rect(", 5)) {
            hide = 1; int num = 0, any = 0;
            for (uint32_t i = 5; i < vl && v[i] != ')'; i++) {
                char c = v[i];
                if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); any = 1; if (num > 1) hide = 0; }
                else if (c == ' ' || c == ',' || c == 'p' || c == 'x') { if (c == ' ' || c == ',') num = 0; }
                else hide = 0;
            }
            if (!any) hide = 0;
        } else if (prop == PR_CLIP_PATH) {
            static const char *const CP[] = { "inset(50%)", "inset(100%)", "circle(0", "inset(50% 50%", 0 };
            for (int k = 0; CP[k]; k++) { uint32_t cl = (uint32_t)wu_strlen(CP[k]); if (vl >= cl && !memcmp(v, CP[k], cl)) hide = 1; }
        }
        if (hide) st->visibility = 1;
        return; }
    case PR_OVERFLOW: case PR_OVERFLOW_X: case PR_OVERFLOW_Y: {
        const char *p = next_tok(v, v + vl, &ts, &tl); if (!p) return;
        uint8_t o = (is_kw(ts, tl, "hidden") || is_kw(ts, tl, "clip")) ? 1 : (is_kw(ts, tl, "scroll") || is_kw(ts, tl, "auto")) ? 2 : 0;
        if (prop != PR_OVERFLOW_Y) st->overflow_x = o;
        if (prop != PR_OVERFLOW_X) st->overflow_y = o;
        if (prop == PR_OVERFLOW && next_tok(p, v + vl, &ts, &tl)) st->overflow_y = (is_kw(ts, tl, "hidden") || is_kw(ts, tl, "clip")) ? 1 : (is_kw(ts, tl, "scroll") || is_kw(ts, tl, "auto")) ? 2 : 0;
        return; }
    case PR_Z_INDEX: if (is_kw(v, vl, "auto")) st->z_index = 0; else st->z_index = (int16_t)wu_atoi(v); return;
    case PR_TOP: if (parse_len(v, vl, &a->lc, &l, 1)) st->top = l; return;
    case PR_RIGHT: if (parse_len(v, vl, &a->lc, &l, 1)) st->right = l; return;
    case PR_BOTTOM: if (parse_len(v, vl, &a->lc, &l, 1)) st->bottom = l; return;
    case PR_LEFT: if (parse_len(v, vl, &a->lc, &l, 1)) st->left = l; return;
    case PR_INSET: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->top = b4[0]; st->right = b4[1]; st->bottom = b4[2]; st->left = b4[3]; return; }
    case PR_INSET_BLOCK_START: if (parse_len(v, vl, &a->lc, &l, 1)) st->top = l; return;
    case PR_INSET_INLINE: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->left = b4[0]; st->right = b4[1]; return; }   /* SOMA_188 */
    case PR_INSET_BLOCK: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->top = b4[0]; st->bottom = b4[1]; return; }
    case PR_WIDTH:
        if (!g_wl181_off && (is_kw(v, vl, "fit-content") || is_kw(v, vl, "max-content") || is_kw(v, vl, "-moz-fit-content") || (vl > 12 && wu_strnieq(v, "fit-content(", 12)))) { memset(&st->width, 0, sizeof st->width); st->width.kind = LK_FIT; return; }
        if (!g_wl181_off && is_kw(v, vl, "min-content")) { memset(&st->width, 0, sizeof st->width); st->width.kind = LK_MINC; return; }
        if (parse_len(v, vl, &a->lc, &l, 0) && !(l.kind == LK_VAL && l.px < 0 && l.pct <= 0)) st->width = l;
        return;
    case PR_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0) && !(l.kind == LK_VAL && l.px < 0 && l.pct <= 0)) st->height = l; return;
    case PR_MIN_WIDTH: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind != LK_VAL) { l.kind = LK_VAL; l.px = 0; l.pct = 0; } st->min_w = l; } return;
    case PR_MIN_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind != LK_VAL) { l.kind = LK_VAL; l.px = 0; l.pct = 0; } st->min_h = l; } return;
    case PR_MAX_WIDTH: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind == LK_AUTO) l.kind = LK_NONE; st->max_w = l; } return;
    case PR_MAX_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind == LK_AUTO) l.kind = LK_NONE; st->max_h = l; } return;
    case PR_MARGIN: apply_box4(a, st->margin, v, vl, 1); return;
    case PR_MARGIN_INLINE: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->margin[3] = b4[0]; st->margin[1] = b4[1]; return; }   /* SOMA_188: inicio fin */
    case PR_MARGIN_BLOCK: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->margin[0] = b4[0]; st->margin[2] = b4[1]; return; }
    case PR_MARGIN_TOP: case PR_MARGIN_RIGHT: case PR_MARGIN_BOTTOM: case PR_MARGIN_LEFT:
        if (parse_len(v, vl, &a->lc, &l, 1)) st->margin[prop - PR_MARGIN_TOP] = l;
        return;
    case PR_PADDING: apply_box4(a, st->padding, v, vl, 0); return;
    case PR_PADDING_INLINE: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 0); st->padding[3] = b4[0]; st->padding[1] = b4[1]; return; }
    case PR_PADDING_BLOCK: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 0); st->padding[0] = b4[0]; st->padding[2] = b4[1]; return; }
    case PR_PADDING_TOP: case PR_PADDING_RIGHT: case PR_PADDING_BOTTOM: case PR_PADDING_LEFT:
        if (parse_len(v, vl, &a->lc, &l, 1) && l.kind == LK_VAL && l.px >= 0) st->padding[prop - PR_PADDING_TOP] = l;
        return;
    case PR_BOX_SIZING: st->box_sizing = is_kw(v, vl, "border-box"); return;
    case PR_BORDER: apply_border_one(a, st, 15, v, vl); return;
    case PR_BORDER_TOP: apply_border_one(a, st, 1, v, vl); return;
    case PR_BORDER_RIGHT: apply_border_one(a, st, 2, v, vl); return;
    case PR_BORDER_BOTTOM: apply_border_one(a, st, 4, v, vl); return;
    case PR_BORDER_LEFT: apply_border_one(a, st, 8, v, vl); return;
    case PR_BORDER_WIDTH: case PR_BORDER_TOP_WIDTH: case PR_BORDER_RIGHT_WIDTH: case PR_BORDER_BOTTOM_WIDTH: case PR_BORDER_LEFT_WIDTH: {
        struct wlen b4[4]; int w[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { int px = 3; if (is_kw(ts, tl, "thin")) px = 1; else if (is_kw(ts, tl, "medium")) px = 3; else if (is_kw(ts, tl, "thick")) px = 5; else if (parse_len(ts, tl, &a->lc, &b4[0], 0) && b4[0].kind == LK_VAL) px = b4[0].px; w[n++] = px; }
        if (!n) return;
        int bw[4] = { w[0], n > 1 ? w[1] : w[0], n > 2 ? w[2] : w[0], n > 3 ? w[3] : (n > 1 ? w[1] : w[0]) };
        if (prop == PR_BORDER_WIDTH) { for (int i = 0; i < 4; i++) if (st->border_style[i]) st->border_w[i] = (int16_t)bw[i]; }
        else { int i = prop - PR_BORDER_TOP_WIDTH; if (st->border_style[i]) st->border_w[i] = (int16_t)bw[0]; }
        return; }
    case PR_BORDER_STYLE: case PR_BORDER_TOP_STYLE: case PR_BORDER_RIGHT_STYLE: case PR_BORDER_BOTTOM_STYLE: case PR_BORDER_LEFT_STYLE: {
        int s[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { int b = border_style_of(ts, tl); if (b < 0) return; s[n++] = b; }
        if (!n) return;
        int bs[4] = { s[0], n > 1 ? s[1] : s[0], n > 2 ? s[2] : s[0], n > 3 ? s[3] : (n > 1 ? s[1] : s[0]) };
        for (int i = 0; i < 4; i++) {
            if (prop != PR_BORDER_STYLE && i != prop - PR_BORDER_TOP_STYLE) continue;
            int sv = prop == PR_BORDER_STYLE ? bs[i] : bs[0];
            st->border_style[i] = (uint8_t)sv; if (sv && st->border_w[i] == 0) st->border_w[i] = 3; if (!sv) st->border_w[i] = 0;
        }
        return; }
    case PR_BORDER_COLOR: case PR_BORDER_TOP_COLOR: case PR_BORDER_RIGHT_COLOR: case PR_BORDER_BOTTOM_COLOR: case PR_BORDER_LEFT_COLOR: {
        uint32_t c[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { c[n] = tok_color(ts, tl, st->color, &ok); if (!ok) return; n++; }
        if (!n) return;
        uint32_t bc[4] = { c[0], n > 1 ? c[1] : c[0], n > 2 ? c[2] : c[0], n > 3 ? c[3] : (n > 1 ? c[1] : c[0]) };
        for (int i = 0; i < 4; i++) { if (prop == PR_BORDER_COLOR) st->border_color[i] = bc[i]; else if (i == prop - PR_BORDER_TOP_COLOR) st->border_color[i] = bc[0]; }
        return; }
    case PR_BORDER_RADIUS: case PR_TOP_LEFT_RADIUS: case PR_TOP_RIGHT_RADIUS: case PR_BOTTOM_LEFT_RADIUS: case PR_BOTTOM_RIGHT_RADIUS: {
        const char *p = next_tok(v, v + vl, &ts, &tl); (void)p;
        if (parse_len(ts, tl, &a->lc, &l, 0) && l.kind == LK_VAL) { int r = l.px; if (l.pct >= 4900) r = 999; if (r > 999) r = 999; if (r < 0) r = 0; if (prop == PR_BORDER_RADIUS || r > st->radius) st->radius = (int16_t)r; }
        return; }
    case PR_OPACITY: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (!parse_num(&q, &nv)) return;
        if (*q == '%') nv *= 10;
        nv = nv < 0 ? 0 : (nv > 1000 ? 1000 : nv); st->opacity = (uint8_t)(nv * 255 / 1000); return; }
    case PR_FLEX: apply_flex(a, st, v, vl); return;
    case PR_FLEX_GROW: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (parse_num(&q, &nv)) st->flex_grow = (int16_t)(nv / 10); return; }
    case PR_FLEX_SHRINK: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (parse_num(&q, &nv)) st->flex_shrink = (int16_t)(nv / 10); return; }
    case PR_FLEX_BASIS: if (parse_len(v, vl, &a->lc, &l, 0)) st->flex_basis = l; return;
    case PR_FLEX_DIRECTION: st->flex_dir = is_kw(v, vl, "row-reverse") ? 1 : is_kw(v, vl, "column") ? 2 : is_kw(v, vl, "column-reverse") ? 3 : 0; return;
    case PR_FLEX_WRAP: st->flex_wrap = !is_kw(v, vl, "nowrap"); return;
    case PR_FLEX_FLOW: { const char *p = v, *e = v + vl; while ((p = next_tok(p, e, &ts, &tl))) {
            if (is_kw(ts, tl, "row")) st->flex_dir = 0; else if (is_kw(ts, tl, "row-reverse")) st->flex_dir = 1; else if (is_kw(ts, tl, "column")) st->flex_dir = 2; else if (is_kw(ts, tl, "column-reverse")) st->flex_dir = 3;
            else if (is_kw(ts, tl, "nowrap")) st->flex_wrap = 0; else if (is_kw(ts, tl, "wrap") || is_kw(ts, tl, "wrap-reverse")) st->flex_wrap = 1; } return; }
    case PR_JUSTIFY_CONTENT: { int al = align_of(v, vl); if (al < 0) return;
        st->justify = (uint8_t)(al >= 10 ? al - 10 : (al == AI_START ? JC_START : al == AI_END ? JC_END : al == AI_CENTER ? JC_CENTER : JC_START)); return; }
    case PR_ALIGN_ITEMS: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_items = (uint8_t)al; return; }
    /* SOMA_188: transiciones y animaciones: no se animan, pero se emiten transitionend/animationend a su tiempo */
    case PR_TRANSITION: case PR_TRANSITION_DURATION: case PR_TRANSITION_DELAY: case PR_ANIMATION: case PR_ANIMATION_DURATION: case PR_ANIMATION_DELAY: case PR_ANIMATION_ITER: case PR_TRANSITION_PROPERTY: case PR_ANIMATION_NAME: {
        if (g_wl181_off) return;
        int isan = prop == PR_ANIMATION || prop == PR_ANIMATION_DURATION || prop == PR_ANIMATION_DELAY || prop == PR_ANIMATION_ITER || prop == PR_ANIMATION_NAME;
        if (prop == PR_TRANSITION_PROPERTY) { st->tr_prop = is_kw(v, vl, "all") ? 0 : wa_strndup(&a->c->d->ar, v, vl); if (is_kw(v, vl, "none")) st->tr_ms = 0; return; }
        if (prop == PR_ANIMATION_NAME) { st->an_name = is_kw(v, vl, "none") ? 0 : wa_strndup(&a->c->d->ar, v, vl); return; }
        /* lista separada por comas: cada elemento aporta [propiedad|nombre] duracion [funcion] [retardo] [repeticiones] */
        int best = 0, cur_d = -1, cur_l = 0, iters = 1, inf = 0; const char *p = v, *e = v + vl; char names[96]; uint32_t nn = 0; names[0] = 0;
        int single_d = prop == PR_TRANSITION_DURATION || prop == PR_ANIMATION_DURATION, single_l = prop == PR_TRANSITION_DELAY || prop == PR_ANIMATION_DELAY;
        for (;;) {
            const char *np = next_tok(p, e, &ts, &tl); int end = !np || (tl == 1 && ts[0] == ',');
            if (end) {
                int tot = (cur_d < 0 ? 0 : cur_d) * (inf ? 1 : iters) + (cur_l > 0 ? cur_l : 0);
                if (single_d) tot = cur_d < 0 ? 0 : cur_d;
                if (single_l) tot = cur_l;
                if (tot > best) best = tot;
                if (!np) break;
                p = np; cur_d = -1; cur_l = 0; iters = 1; if (nn && nn + 1 < sizeof names) names[nn++] = ','; continue;
            }
            p = np;
            if (tl && (wu_isdigit((unsigned char)ts[0]) || ts[0] == '.') ) {
                uint32_t k = 0; int ip = 0, fr = 0, fd = 0; while (k < tl && wu_isdigit((unsigned char)ts[k])) ip = ip * 10 + (ts[k++] - '0');
                if (k < tl && ts[k] == '.') { k++; while (k < tl && wu_isdigit((unsigned char)ts[k])) { if (fd < 3) { fr = fr * 10 + (ts[k] - '0'); fd++; } k++; } }
                while (fd < 3) { fr *= 10; fd++; }
                int ms = -1;
                if (k + 2 == tl && ts[k] == 'm' && ts[k + 1] == 's') ms = ip + fr / 1000;
                else if (k + 1 == tl && (ts[k] == 's' || ts[k] == 'S')) ms = ip * 1000 + fr;
                else if (k == tl && isan) { iters = ip > 0 ? ip : 1; continue; }
                if (ms >= 0) { if (single_l || (cur_d >= 0 && !single_d)) cur_l = ms; else cur_d = ms; }
                continue;
            }
            if (is_kw(ts, tl, "infinite")) { inf = 1; continue; }
            static const char *const kw[] = { "ease", "linear", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end", "normal", "reverse", "alternate", "alternate-reverse", "none", "forwards", "backwards", "both", "running", "paused", "initial", "inherit", 0 };
            int isk = 0; for (int q = 0; kw[q]; q++) if (is_kw(ts, tl, kw[q])) isk = 1;
            if (isk || (tl > 6 && (wu_strnieq(ts, "cubic-", 6) || wu_strnieq(ts, "steps(", 6)))) continue;
            if (nn + tl + 2 < sizeof names) { for (uint32_t q = 0; q < tl; q++) names[nn++] = ts[q]; names[nn] = 0; }
        }
        if (best > 60000) best = 60000;
        if (isan) {
            if (prop == PR_ANIMATION_ITER) { st->an_inf = (uint8_t)inf; if (!inf && st->an_ms) st->an_ms = (uint16_t)(st->an_ms * (iters > 0 ? iters : 1) > 60000 ? 60000 : st->an_ms * (iters > 0 ? iters : 1)); return; }
            if (prop == PR_ANIMATION) { st->an_inf = (uint8_t)inf; st->an_name = nn && !is_kw(names, nn, "none") ? wa_strndup(&a->c->d->ar, names, nn) : 0; st->an_ms = (uint16_t)best; return; }
            if (prop == PR_ANIMATION_DELAY) { st->an_ms = (uint16_t)(st->an_ms + best > 60000 ? 60000 : st->an_ms + best); return; }
            st->an_ms = (uint16_t)best; return;
        }
        if (prop == PR_TRANSITION) { st->tr_prop = nn && !is_kw(names, nn, "all") ? wa_strndup(&a->c->d->ar, names, nn) : 0; if (nn && is_kw(names, nn, "none")) { st->tr_ms = 0; return; } st->tr_ms = (uint16_t)best; return; }
        if (prop == PR_TRANSITION_DELAY) { st->tr_ms = (uint16_t)(st->tr_ms + best > 60000 ? 60000 : st->tr_ms + best); return; }
        st->tr_ms = (uint16_t)best; return; }
    case PR_GRID_AUTO_FLOW: st->grid_flow = (uint8_t)((findn(v, vl, "column") ? 1 : 0) | (findn(v, vl, "dense") ? 2 : 0)); return;   /* SOMA_188 */
    case PR_PLACE_CONTENT: {   /* SOMA_188: <align-content> [<justify-content>] */
        const char *sp = v; while (sp < v + vl && !wu_isspace(*sp)) sp++;
        uint32_t l1 = (uint32_t)(sp - v); const char *v2 = sp; while (v2 < v + vl && wu_isspace(*v2)) v2++; uint32_t l2 = (uint32_t)(v + vl - v2); if (!l2) { v2 = v; l2 = l1; }
        int al = align_of(v, l1); if (al >= 0) st->align_content = (uint8_t)(al >= 10 ? al - 10 : al);
        int aj = align_of(v2, l2); if (aj >= 0) st->justify = (uint8_t)(aj >= 10 ? aj - 10 : (aj == AI_START ? JC_START : aj == AI_END ? JC_END : aj == AI_CENTER ? JC_CENTER : JC_START));
        return; }
    case PR_PLACE_ITEMS: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_items = (uint8_t)al; st->justify_items = al == AI_START ? 1 : al == AI_END ? 2 : al == AI_CENTER ? 3 : 0; return; }
    case PR_ALIGN_SELF: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_self = (uint8_t)al; return; }
    case PR_ALIGN_CONTENT: { int al = align_of(v, vl); if (al >= 0) st->align_content = (uint8_t)(al >= 10 ? al - 10 : al); return; }
    case PR_ORDER: st->order = (int16_t)wu_atoi(v); return;
    case PR_GAP: case PR_GRID_GAP: { const char *p = next_tok(v, v + vl, &ts, &tl); if (!p) return;
        if (!parse_len(ts, tl, &a->lc, &l, 0)) return;
        st->gap_r = l; st->gap_c = l;
        if (next_tok(p, v + vl, &ts, &tl) && parse_len(ts, tl, &a->lc, &l, 0)) st->gap_c = l;
        return; }
    case PR_ROW_GAP: if (parse_len(v, vl, &a->lc, &l, 0)) st->gap_r = l; return;
    case PR_COLUMN_GAP: if (parse_len(v, vl, &a->lc, &l, 0)) st->gap_c = l; return;
    case PR_GRID_TEMPLATE_COLUMNS: apply_grid_cols(a, st, v, vl); return;
    case PR_GRID_COLUMN: { const char *q = findn(v, vl, "span"); if (q) { st->col_span = (int16_t)wu_atoi(q + 4); } else st->col_span = 1; grid_place(v, vl, &st->gs[0], &st->ge[0], &st->gsp[0]); if (st->gsp[0] > 0) st->col_span = st->gsp[0]; return; }
    case PR_GRID_ROW: grid_place(v, vl, &st->gs[1], &st->ge[1], &st->gsp[1]); return;
    case PR_GRID_COLUMN_START: { int16_t e2, sp; int16_t a1 = st->gs[0]; grid_place(v, vl, &st->gs[0], &e2, &sp); if (sp) st->gsp[0] = sp; (void)a1; return; }
    case PR_GRID_COLUMN_END: { int16_t s2, sp; grid_place(v, vl, &s2, &st->ge[0], &sp); if (!st->ge[0] && s2) st->ge[0] = s2; if (sp) st->gsp[0] = sp; return; }
    case PR_GRID_ROW_START: { int16_t e2, sp; grid_place(v, vl, &st->gs[1], &e2, &sp); if (sp) st->gsp[1] = sp; return; }
    case PR_GRID_ROW_END: { int16_t s2, sp; grid_place(v, vl, &s2, &st->ge[1], &sp); if (!st->ge[1] && s2) st->ge[1] = s2; if (sp) st->gsp[1] = sp; return; }
    case PR_GRID_AREA: {
        const char *p = v, *e = v + vl; int hasslash = 0; for (const char *q = v; q < e; q++) if (*q == '/') hasslash = 1;
        if (!hasslash) { const char *s1; uint32_t l1; if (next_tok(p, e, &s1, &l1) && !wu_isdigit(s1[0]) && !is_kw(s1, l1, "auto") && !is_kw(s1, l1, "span")) { st->garea = wa_strndup(&a->c->d->ar, s1, l1); } return; }
        int16_t vals[4] = { 0, 0, 0, 0 }; int k = 0; const char *ts; uint32_t tl;
        while (k < 4 && (p = next_tok(p, e, &ts, &tl))) { if (tl == 1 && ts[0] == '/') { k++; continue; } if (is_kw(ts, tl, "auto") || is_kw(ts, tl, "span")) continue; char b[12]; if (tl < sizeof b) { memcpy(b, ts, tl); b[tl] = 0; if (wu_isdigit(b[0]) || b[0] == '-') vals[k] = (int16_t)wu_atoi(b); } }
        st->gs[1] = vals[0]; st->gs[0] = vals[1]; st->ge[1] = vals[2]; st->ge[0] = vals[3]; return; }
    case PR_GRID_TEMPLATE_ROWS: { struct wgx *g = gx_of(a, st); if (g) { memset(g->rows, 0, sizeof g->rows); int am; g->nrows = (int8_t)parse_tracks(a, v, vl, g->rows, GRID_MAX_ROWS, &am); } return; }
    case PR_GRID_AUTO_ROWS: { struct wgx *g = gx_of(a, st); if (g) { struct wlen t[2]; int am; int n = parse_tracks(a, v, vl, t, 1, &am); g->auto_rows = n ? t[0] : g->auto_rows; if (n && t[0].kind == LK_NONE) g->auto_rows.kind = LK_AUTO; } return; }
    case PR_GRID_AUTO_COLUMNS: return;
    case PR_GRID_TEMPLATE_AREAS: { struct wgx *g = gx_of(a, st); if (g) g->areas = is_kw(v, vl, "none") ? 0 : wa_strndup(&a->c->d->ar, v, vl); return; }
    case PR_JUSTIFY_ITEMS: case PR_JUSTIFY_SELF: case PR_PLACE_SELF: {
        int al = align_of(v, vl); uint8_t m = al == AI_START ? 1 : al == AI_END ? 2 : al == AI_CENTER ? 3 : 0;
        if (prop == PR_JUSTIFY_ITEMS) st->justify_items = m; else { st->justify_self = m; if (prop == PR_PLACE_SELF && al >= 0 && al < 10) st->align_self = (uint8_t)al; }
        return; }
    case PR_COLOR: { uint32_t c = tok_color(v, vl, par->color, &ok); if (ok) st->color = c; return; }
    case PR_FONT_SIZE: { int px; if (font_size_of(a, v, vl, &px)) { st->font_px = (int16_t)(px < 1 ? 1 : (px > 400 ? 400 : px)); st->fs_zero = (uint8_t)(px < 1 && !g_wl181_off); a->lc.font_px = st->font_px; } return; }
    case PR_FONT_WEIGHT: if (is_kw(v, vl, "bold") || is_kw(v, vl, "bolder")) st->bold = 1; else if (is_kw(v, vl, "normal") || is_kw(v, vl, "lighter")) st->bold = 0; else st->bold = wu_atoi(v) >= 600; return;
    case PR_FONT_STYLE: st->italic = is_kw(v, vl, "italic") || is_kw(v, vl, "oblique"); return;
    case PR_FONT_FAMILY: st->mono = (uint8_t)family_is_mono(v, vl); return;
    case PR_FONT: apply_font_shorthand(a, st, v, vl); return;
    case PR_LINE_HEIGHT: apply_lh(a, st, v, vl); return;
    case PR_TEXT_ALIGN: st->text_align = is_kw(v, vl, "center") || is_kw(v, vl, "-webkit-center") ? TA_CENTER : is_kw(v, vl, "right") || is_kw(v, vl, "end") ? TA_RIGHT : is_kw(v, vl, "justify") ? TA_JUSTIFY : TA_LEFT; return;
    case PR_TEXT_DECORATION: case PR_TEXT_DECORATION_LINE: {
        uint8_t d = 0; const char *p = v, *e = v + vl;
        while ((p = next_tok(p, e, &ts, &tl))) { if (is_kw(ts, tl, "underline")) d |= 1; else if (is_kw(ts, tl, "line-through")) d |= 2; else if (is_kw(ts, tl, "overline")) d |= 4; }
        st->text_decor = d; return; }
    case PR_TEXT_TRANSFORM: st->text_transform = is_kw(v, vl, "uppercase") ? 1 : is_kw(v, vl, "lowercase") ? 2 : is_kw(v, vl, "capitalize") ? 3 : 0; return;
    case PR_TEXT_INDENT: if (parse_len(v, vl, &a->lc, &l, 0)) st->text_indent = l; return;
    case PR_LETTER_SPACING: if (parse_len(v, vl, &a->lc, &l, 0)) st->letter_sp = (int16_t)(l.kind == LK_VAL ? l.px : 0); return;
    case PR_WHITE_SPACE: st->white_space = is_kw(v, vl, "nowrap") ? WS_NOWRAP : is_kw(v, vl, "pre") ? WS_PRE : is_kw(v, vl, "pre-wrap") || is_kw(v, vl, "break-spaces") ? WS_PRE_WRAP : is_kw(v, vl, "pre-line") ? WS_PRE_LINE : WS_NORMAL; return;
    case PR_VERTICAL_ALIGN: st->vertical_align = is_kw(v, vl, "top") ? VA_TOP : is_kw(v, vl, "middle") ? VA_MIDDLE : is_kw(v, vl, "bottom") ? VA_BOTTOM : is_kw(v, vl, "sub") ? VA_SUB :
        is_kw(v, vl, "super") ? VA_SUPER : is_kw(v, vl, "text-top") ? VA_TEXT_TOP : is_kw(v, vl, "text-bottom") ? VA_TEXT_BOTTOM : VA_BASELINE; return;
    case PR_TEXT_OVERFLOW: st->text_overflow = is_kw(v, vl, "ellipsis"); return;
    case PR_WORD_BREAK: st->word_break = is_kw(v, vl, "break-all") || is_kw(v, vl, "break-word") ? 1 : 0; return;
    case PR_OVERFLOW_WRAP: case PR_WORD_WRAP: st->word_break = (is_kw(v, vl, "break-word") || is_kw(v, vl, "anywhere")) ? 2 : (st->word_break == 1 ? 1 : 0); return;
    case PR_LIST_STYLE: case PR_LIST_STYLE_TYPE: case PR_LIST_STYLE_POSITION: {
        const char *p = v, *e = v + vl;
        while ((p = next_tok(p, e, &ts, &tl))) {
            if (is_kw(ts, tl, "none")) st->list_style = LS_NONE; else if (is_kw(ts, tl, "disc")) st->list_style = LS_DISC; else if (is_kw(ts, tl, "circle")) st->list_style = LS_CIRCLE;
            else if (is_kw(ts, tl, "square")) st->list_style = LS_SQUARE; else if (is_kw(ts, tl, "decimal") || is_kw(ts, tl, "decimal-leading-zero")) st->list_style = LS_DECIMAL;
            else if (is_kw(ts, tl, "lower-alpha") || is_kw(ts, tl, "lower-latin")) st->list_style = LS_ALPHA; else if (is_kw(ts, tl, "upper-alpha") || is_kw(ts, tl, "upper-latin")) st->list_style = LS_UALPHA;
            else if (is_kw(ts, tl, "lower-roman") || is_kw(ts, tl, "upper-roman")) st->list_style = LS_ROMAN;
            else if (is_kw(ts, tl, "inside")) st->list_inside = 1; else if (is_kw(ts, tl, "outside")) st->list_inside = 0;
        }
        return; }
    case PR_BACKGROUND: {
        st->bg_color = 0; st->bg_image = 0; st->has_grad = 0; st->bg_repeat = 0; st->bg_size_mode = 0; st->bg_pos_x = 0; st->bg_pos_y = 0; st->bg_off_x = st->bg_off_y = 0;
        apply_bg_geometry(a, st, v, vl, 1, 0);
        return; }
    case PR_BACKGROUND_COLOR: { uint32_t c = tok_color(v, vl, st->color, &ok); if (ok) st->bg_color = c; return; }
    case PR_BACKGROUND_IMAGE: { st->bg_image = 0; st->has_grad = 0; const char *p = v, *e = v + vl; while ((p = next_tok(p, e, &ts, &tl))) { if (ts[0] != ',') apply_bg_token(a, st, ts, tl, a->c); } return; }
    case PR_BACKGROUND_REPEAT: { const char *p = v, *e = v + vl; int rx = -1, ry = -1;
        while ((p = next_tok(p, e, &ts, &tl))) {
            if (is_kw(ts, tl, "repeat-x")) { rx = 1; ry = 0; } else if (is_kw(ts, tl, "repeat-y")) { rx = 0; ry = 1; } else if (is_kw(ts, tl, "no-repeat")) { if (rx < 0) rx = 0; else ry = 0; if (ry < 0) ry = 0; }
            else if (is_kw(ts, tl, "repeat") || is_kw(ts, tl, "round") || is_kw(ts, tl, "space")) { if (rx < 0) rx = 1; else ry = 1; if (ry < 0) ry = 1; }
        }
        if (rx >= 0) st->bg_repeat = (uint8_t)(rx && ry ? 0 : rx ? 1 : ry ? 2 : 3);
        return; }
    case PR_BACKGROUND_SIZE: apply_bg_geometry(a, st, v, vl, 0, 1); return;
    case PR_BACKGROUND_POSITION: st->bg_off_x = st->bg_off_y = 0; apply_bg_geometry(a, st, v, vl, 0, 0); return;
    case PR_BACKGROUND_POSITION_X: { struct bgpt pt[1]; const char *p = next_tok(v, v + vl, &ts, &tl); if (p) { int kw = bg_kw_of(ts, tl); pt[0].kw = (uint8_t)kw; pt[0].has_len = 0;
            if (!kw && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL) { pt[0].has_len = 1; pt[0].len = l; }
            int xp, xo; bg_axis(&pt[0], &xp, &xo); st->bg_pos_x = (int16_t)xp; st->bg_off_x = (int16_t)xo; } return; }
    case PR_TRANSFORM: case PR_TRANSLATE: {
        /* SOMA_181: solo desplazamientos (translate/translateX/translateY/translate3d); rotate/scale no se dibujan */
        struct wlen zx, zy; memset(&zx, 0, sizeof zx); memset(&zy, 0, sizeof zy); zx.kind = zy.kind = LK_VAL; int any = 0;
        const char *p = v, *e = v + vl;
        if (prop == PR_TRANSLATE) {
            int idx = 0; const char *q = p; while ((q = next_tok(q, e, &ts, &tl)) && idx < 2) { if (is_kw(ts, tl, "none")) break; if (!parse_len(ts, tl, &a->lc, &l, 1) || l.kind != LK_VAL) break; if (idx == 0) zx = l; else zy = l; idx++; any = 1; }
        } else { st->rot10 = 0; st->sc_x = st->sc_y = 0; } if (prop != PR_TRANSLATE) while (p < e) {
            while (p < e && !wu_isalpha(*p)) p++;
            const char *fs = p; while (p < e && (wu_isalpha(*p) || wu_isdigit(*p))) p++;
            uint32_t fl = (uint32_t)(p - fs); if (p >= e || *p != '(') continue;
            const char *as = ++p; int dep = 1; while (p < e && dep) { if (*p == '(') dep++; else if (*p == ')') dep--; p++; }
            const char *ae = p - 1; if (ae < as) ae = as;
            int tx = fl == 10 && wu_strnieq(fs, "translateX", 10) ? 1 : fl == 10 && wu_strnieq(fs, "translateY", 10) ? 2 : (fl == 9 && wu_strnieq(fs, "translate", 9)) || (fl == 11 && wu_strnieq(fs, "translate3d", 11)) ? 3 : 0;
            if (!tx) {   /* SOMA_188: rotate()/rotateZ() y scale()/scaleX()/scaleY() */
                char ab[40]; uint32_t al = (uint32_t)(ae - as); if (al > 39) al = 39; memcpy(ab, as, al); ab[al] = 0;
                if ((fl == 6 && wu_strnieq(fs, "rotate", 6)) || (fl == 7 && wu_strnieq(fs, "rotateZ", 7))) {
                    const char *q = ab; while (*q == ' ') q++; int neg = *q == '-'; if (*q == '-' || *q == '+') q++;
                    int64_t ip = 0, fr = 0, fd = 1; while (*q >= '0' && *q <= '9') ip = ip * 10 + (*q++ - '0'); if (*q == '.') { q++; while (*q >= '0' && *q <= '9' && fd < 100000) { fr = fr * 10 + (*q++ - '0'); fd *= 10; } }
                    int64_t milli = ip * 1000 + fr * 1000 / fd;   /* milesimas de la unidad */
                    int64_t d10 = wu_strnieq(q, "turn", 4) ? milli * 3600 / 1000 : wu_strnieq(q, "rad", 3) ? milli * 18000 / 31416 / 10 : wu_strnieq(q, "grad", 4) ? milli * 9 / 1000 : milli / 100;
                    if (neg) d10 = -d10;
                    st->rot10 = (int16_t)(((st->rot10 + d10) % 3600 + 3600) % 3600); any = 1;
                } else if ((fl == 5 && wu_strnieq(fs, "scale", 5)) || (fl == 6 && (wu_strnieq(fs, "scaleX", 6) || wu_strnieq(fs, "scaleY", 6))) || (fl == 7 && wu_strnieq(fs, "scale3d", 7))) {
                    int vals[2] = { 100, 100 }, nv = 0; const char *q = ab;
                    while (*q && nv < 2) { while (*q == ' ' || *q == ',') q++; if (!*q) break; int neg = *q == '-'; if (neg) q++; int ip = 0, fr = 0, fd = 1; while (*q >= '0' && *q <= '9') ip = ip * 10 + (*q++ - '0'); if (*q == '.') { q++; while (*q >= '0' && *q <= '9' && fd < 1000) { fr = fr * 10 + (*q++ - '0'); fd *= 10; } } int pc = ip * 100 + fr * 100 / fd; if (*q == '%') { pc = ip; q++; } vals[nv++] = neg ? -pc : pc; while (*q && *q != ' ' && *q != ',') q++; }
                    if (!nv) continue;
                    int sx2 = vals[0], sy2 = nv > 1 ? vals[1] : vals[0];
                    if (fl == 6 && (fs[5] == 'X' || fs[5] == 'x')) sy2 = 100; else if (fl == 6) { sy2 = vals[0]; sx2 = 100; }
                    int cx = st->sc_x ? st->sc_x : 100, cy = st->sc_y ? st->sc_y : 100; cx = cx * sx2 / 100; cy = cy * sy2 / 100;
                    st->sc_x = (int16_t)(cx == 0 ? -32768 : cx); st->sc_y = (int16_t)(cy == 0 ? -32768 : cy); any = 1;
                }
                continue;
            }
            struct wlen a2[2]; int n2 = 0; const char *q = as, *ts2; uint32_t tl2;
            while (n2 < 2 && (q = next_tok(q, ae, &ts2, &tl2))) { if (tl2 == 1 && ts2[0] == ',') continue; if (!parse_len(ts2, tl2, &a->lc, &a2[n2], 1) || a2[n2].kind != LK_VAL) { n2 = -1; break; } n2++; }
            if (n2 <= 0) continue;
            if (tx == 1) { zx.px += a2[0].px; zx.pct += a2[0].pct; } else if (tx == 2) { zy.px += a2[0].px; zy.pct += a2[0].pct; }
            else { zx.px += a2[0].px; zx.pct += a2[0].pct; if (n2 > 1) { zy.px += a2[1].px; zy.pct += a2[1].pct; } }
            any = 1;
        }
        if (is_kw(v, vl, "none")) { st->rot10 = 0; st->sc_x = st->sc_y = 0; }
        if (any || is_kw(v, vl, "none")) { st->tr_x = zx; st->tr_y = zy; if (!any) st->tr_x.kind = st->tr_y.kind = LK_AUTO; }
        return; }
    case PR_ASPECT_RATIO: {
        const char *p = v, *e = v + vl; int32_t n1 = 0, n2 = 0, k = 0; st->ar_w = st->ar_h = 0;
        while ((p = next_tok(p, e, &ts, &tl))) {
            if (tl == 1 && ts[0] == '/') continue;
            char b[24]; if (tl >= sizeof b) return; memcpy(b, ts, tl); b[tl] = 0; const char *q = b; int32_t nv;
            if (!(wu_isdigit(b[0]) || b[0] == '.')) continue;                       /* "auto" */
            if (!parse_num(&q, &nv) || nv <= 0) return;
            if (k == 0) n1 = nv; else if (k == 1) n2 = nv; k++;
        }
        if (k == 0) return;
        if (k == 1) n2 = 1000;
        while (n1 > 60000 || n2 > 60000) { n1 /= 2; n2 /= 2; }
        if (n1 > 0 && n2 > 0) { st->ar_w = (uint16_t)n1; st->ar_h = (uint16_t)n2; }
        return; }
    case PR_OBJECT_FIT: st->obj_fit = is_kw(v, vl, "contain") ? 1 : is_kw(v, vl, "cover") ? 2 : is_kw(v, vl, "none") ? 3 : is_kw(v, vl, "scale-down") ? 4 : 0; return;
    case PR_OBJECT_POSITION: {
        struct bgpt pt[2]; int np = 0; const char *p = v, *e = v + vl;
        while (np < 2 && (p = next_tok(p, e, &ts, &tl))) { int kw = bg_kw_of(ts, tl); pt[np].kw = (uint8_t)kw; pt[np].has_len = 0; if (!kw && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL) { pt[np].has_len = 1; pt[np].len = l; } np++; }
        if (np) { struct wstyle tmp; memset(&tmp, 0, sizeof tmp); bg_set_pos(&tmp, pt, np); st->obj_pos_x = (int8_t)(tmp.bg_pos_x > 100 ? 100 : tmp.bg_pos_x < 0 ? 0 : tmp.bg_pos_x); st->obj_pos_y = (int8_t)(tmp.bg_pos_y > 100 ? 100 : tmp.bg_pos_y < 0 ? 0 : tmp.bg_pos_y); }
        return; }
    case PR_LINE_CLAMP: { int n = is_kw(v, vl, "none") ? 0 : wu_atoi(v); st->line_clamp = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n); return; }
    case PR_BORDER_COLLAPSE: st->border_collapse = is_kw(v, vl, "collapse"); return;
    case PR_BORDER_SPACING: if (parse_len(v, vl, &a->lc, &l, 0) && l.kind == LK_VAL) { st->spacing_h = (int16_t)l.px; st->spacing_v = (int16_t)l.px;
            const char *p = next_tok(v, v + vl, &ts, &tl); if (p && next_tok(p, v + vl, &ts, &tl) && parse_len(ts, tl, &a->lc, &l, 0)) st->spacing_v = (int16_t)l.px; } return;
    case PR_CURSOR: st->pointer = is_kw(v, vl, "pointer"); return;
    case PR_POINTER_EVENTS: st->no_ptr = is_kw(v, vl, "none"); return;   /* SOMA_188 */
    case PR_FILL: case PR_STROKE: {   /* SOMA_188: color de iconos SVG desde CSS */
        uint32_t *dst = prop == PR_FILL ? &st->svg_fill : &st->svg_stroke;
        if (is_kw(v, vl, "none") || is_kw(v, vl, "transparent")) { *dst = 1; return; }
        if (is_kw(v, vl, "currentcolor")) { *dst = 2; return; }   /* se resuelve con el color de cada elemento al dibujar */
        if (vl > 4 && wu_strnieq(v, "url(", 4)) return;
        int ok2; uint32_t c2 = tok_color(v, vl, st->color, &ok2); if (ok2) *dst = (c2 >> 24) ? (0xFF000000u | (c2 & 0xFFFFFF)) : 1;
        return; }
    case PR_STROKE_WIDTH: { char b[24]; uint32_t n = vl < 23 ? vl : 23; memcpy(b, v, n); b[n] = 0; int w = 0, frac = 0, d = 0; const char *q = b; while (*q >= '0' && *q <= '9') w = w * 10 + (*q++ - '0'); if (*q == '.') { q++; while (*q >= '0' && *q <= '9' && d < 2) { frac = frac * 10 + (*q++ - '0'); d++; } } while (d < 2) { frac *= 10; d++; } st->svg_sw = (int16_t)(w * 100 + frac); if (!st->svg_sw) st->svg_sw = 1; return; }
    case PR_BOX_SHADOW: {
        /* SOMA_PARCHE_028_UI: primera sombra exterior de la lista. SOMA_188: y la primera interior ("inset") */
        const char *p = v, *e = v + vl; int nl = 0, vals[4] = { 0, 0, 0, 0 }, inset = 0, got = 0, have_out = 0, have_in = 0; uint32_t col = 0x59000000u;
        st->shadow_color = 0; st->ish_color = 0; st->xsh_c[0] = st->xsh_c[1] = st->xsh_c[2] = st->xsh_c[3] = 0; int nxsh = 0;
        for (;;) {
            const char *np = next_tok(p, e, &ts, &tl);
            int end = !np || (tl == 1 && ts[0] == ',');
            if (end) {
                if (got && nl >= 2 && (col >> 24)) {
                    for (int i = 0; i < 4; i++) { if (vals[i] > 160) vals[i] = 160; if (vals[i] < -160) vals[i] = -160; }
                    if (vals[2] < 0) vals[2] = 0;
                    if (!inset && !have_out) { have_out = 1; st->shadow_x = (int16_t)vals[0]; st->shadow_y = (int16_t)vals[1]; st->shadow_blur = (int16_t)vals[2]; st->shadow_spread = (int16_t)vals[3]; st->shadow_color = col; }
                    else if (!inset && nxsh < 4) { int k = nxsh++; int cv[4]; for (int q = 0; q < 4; q++) cv[q] = vals[q] > 120 ? 120 : vals[q] < -120 ? -120 : vals[q];
                        st->xsh_x[k] = (int8_t)cv[0]; st->xsh_y[k] = (int8_t)cv[1]; st->xsh_b[k] = (int8_t)cv[2]; st->xsh_s[k] = (int8_t)cv[3]; st->xsh_c[k] = col; }
                    else if (inset && !have_in) { have_in = 1; for (int i = 0; i < 4; i++) { if (vals[i] > 60) vals[i] = 60; if (vals[i] < -60) vals[i] = -60; } st->ish_x = (int8_t)vals[0]; st->ish_y = (int8_t)vals[1]; st->ish_blur = (int8_t)vals[2]; st->ish_spread = (int8_t)vals[3]; st->ish_color = col; }
                }
                if (!np || (have_out && have_in && nxsh >= 4)) break;
                p = np; nl = 0; inset = 0; got = 0; vals[0] = vals[1] = vals[2] = vals[3] = 0; col = 0x59000000u; continue;
            }
            p = np;
            if (is_kw(ts, tl, "none")) return;
            if (is_kw(ts, tl, "inset")) { inset = 1; continue; }
            int ok; uint32_t c2 = tok_color(ts, tl, st->color, &ok);
            if (ok) { col = c2; got = 1; continue; }
            if (nl < 4 && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL) { vals[nl++] = l.px; got = 1; }
        }
        return; }
    default: break;
    }
    (void)cname; (void)cnl;
}

/* ============================ selectores ============================ */
#define PF_FIRST 1u
#define PF_LAST 2u
#define PF_ONLY 4u
#define PF_FIRST_T 8u
#define PF_LAST_T 16u
#define PF_ONLY_T 32u
#define PF_ROOT 64u
#define PF_EMPTY 128u
#define PF_LINK 256u
#define PF_HOVER 512u
#define PF_FOCUS 1024u
#define PF_CHECKED 2048u
#define PF_DISABLED 4096u
#define PF_ENABLED 8192u
#define PF_NEVER 16384u
#define PF_NTH 32768u

struct wattrsel { const char *n; const char *v; uint8_t op; uint8_t ci; };
struct wsimple {
    uint8_t comb, tag, ncls, nattr, nth_kind;
    const char *name, *id, *cls[3];
    struct wattrsel at[2];
    uint32_t pseudo;
    int16_t nth_a, nth_b;
    struct wsimple *not_;
    struct wlist *lists;                 /* SOMA 144: grupos funcionales :is/:where/:not */
    uint8_t list_calls;                 /* max 4 por compuesto */
    uint8_t cmask_ok;                   /* SOMA_152: cmask calculada */
    uint64_t cmask;                     /* SOMA_152: bits de Bloom de sus clases */
};
struct wlist { struct wlist *next; struct wsimple *items; uint8_t count, mode; struct wsel **hsel; };   /* SOMA_188: mode 3..6 = :has() (descendiente, hijo, +, ~) con hsel[count] */
struct wsel { struct wsimple *parts; uint8_t n; uint32_t spec; uint8_t pe; };

static int is_ident_ch(int c) { return wu_isalnum(c) || c == '-' || c == '_' || (unsigned char)c >= 0x80 || c == '\\'; }

static const char *read_ident(const char *p, const char *e, char *out, uint32_t max)
{
    uint32_t n = 0;
    while (p < e && is_ident_ch(*p)) {
        if (*p == '\\' && p + 1 < e) p++;
        if (n + 1 < max) out[n++] = *p;
        p++;
    }
    out[n] = 0; return p;
}

static int parse_nth(const char *s, int16_t *a, int16_t *b)
{
    char t[24]; uint32_t n = 0; for (; *s && n < 23; s++) if (!wu_isspace(*s)) t[n++] = (char)wu_lower(*s); t[n] = 0;
    if (wu_streq(t, "odd")) { *a = 2; *b = 1; return 1; }
    if (wu_streq(t, "even")) { *a = 2; *b = 0; return 1; }
    const char *p = t; int sign = 1; int av = 0, bv = 0, hasn = 0;
    const char *q = p; if (*q == '-') { sign = -1; q++; } else if (*q == '+') q++;
    int num = 0, anynum = 0; while (wu_isdigit(*q)) { num = num * 10 + (*q - '0'); q++; anynum = 1; }
    if (*q == 'n') { hasn = 1; av = sign * (anynum ? num : 1); q++;
        if (*q == '+' || *q == '-') { int s2 = *q == '-' ? -1 : 1; q++; int nb = 0; while (wu_isdigit(*q)) { nb = nb * 10 + (*q - '0'); q++; } bv = s2 * nb; }
    } else if (anynum) { bv = sign * num; } else return 0;
    if (*q) return 0;
    *a = (int16_t)(hasn ? av : 0); *b = (int16_t)bv; return 1;
}

static struct wsel *compile_selector(struct wcss *c, const char *s, uint32_t n);   /* SOMA_188: :has() */
static const char *parse_compound(struct wcss *c, const char *p, const char *e, struct wsimple *s, uint32_t *spec, int in_not)
{
    char id[64]; int any = 0;
    memset(s, 0, sizeof(*s));
    for (;;) {
        if (p >= e) break;
        char ch = *p;
        if (ch == '*') { p++; any = 1; continue; }
        if (is_ident_ch(ch) && !any && !s->tag && !s->name && !s->id && !s->ncls && !s->nattr && !s->pseudo) {
            p = read_ident(p, e, id, sizeof(id)); any = 1;
            for (char *q = id; *q; q++) *q = (char)wu_lower(*q);
            int t = wd_tag_of(id, wu_strlen(id));
            if (t) s->tag = (uint8_t)t; else s->name = wa_strdup(&c->d->ar, id);
            *spec += 1; continue;
        }
        if (ch == '#') { p = read_ident(p + 1, e, id, sizeof(id)); s->id = wa_strdup(&c->d->ar, id); *spec += 0x10000; any = 1; continue; }
        if (ch == '.') { p = read_ident(p + 1, e, id, sizeof(id)); if (s->ncls < 3) s->cls[s->ncls++] = wa_strdup(&c->d->ar, id); *spec += 0x100; any = 1; continue; }
        if (ch == '[') {
            p++; while (p < e && wu_isspace(*p)) p++;
            char nm[40]; p = read_ident(p, e, nm, sizeof(nm)); while (p < e && wu_isspace(*p)) p++;
            struct wattrsel at; memset(&at, 0, sizeof(at));
            for (char *q = nm; *q; q++) *q = (char)wu_lower(*q);
            at.n = wa_strdup(&c->d->ar, nm);
            if (p < e && *p != ']') {
                uint8_t op = '=';
                if (*p != '=') { op = (uint8_t)*p; p++; }
                if (p >= e || *p != '=') return 0;
                p++; while (p < e && wu_isspace(*p)) p++;
                char val[96]; uint32_t n = 0;
                if (p < e && (*p == '"' || *p == '\'')) { char q = *p++; while (p < e && *p != q) { if (n < 95) val[n++] = *p; p++; } if (p < e) p++; }
                else { while (p < e && *p != ']' && !wu_isspace(*p)) { if (n < 95) val[n++] = *p; p++; } }
                val[n] = 0; at.v = wa_strdup(&c->d->ar, val); at.op = op;
                while (p < e && wu_isspace(*p)) p++;
                if (p < e && (*p == 'i' || *p == 'I')) { at.ci = 1; p++; }
            }
            while (p < e && *p != ']') p++;
            if (p >= e) return 0;
            p++;
            if (s->nattr < 2) s->at[s->nattr++] = at;
            *spec += 0x100; any = 1; continue;
        }
        if (ch == ':') {
            int dbl = (p + 1 < e && p[1] == ':');
            char nm[32]; p = read_ident(p + 1 + dbl, e, nm, sizeof(nm));
            for (char *q = nm; *q; q++) *q = (char)wu_lower(*q);
            if (wu_streq(nm, "before") || wu_streq(nm, "after")) { c->pe_seen = nm[0] == 'b' ? 1 : 2; any = 1; continue; }
            if (wu_streq(nm, "placeholder") || wu_streq(nm, "-webkit-input-placeholder") || wu_streq(nm, "-moz-placeholder") || wu_streq(nm, "-ms-input-placeholder")) { c->pe_seen = 3; any = 1; continue; }   /* SOMA_188: ::placeholder */
            if (dbl) return 0;
            const char *arg = 0; uint32_t argl = 0;
            if (p < e && *p == '(') {
                int d = 1; const char *a0 = ++p;
                while (p < e && d) { if (*p == '(') d++; else if (*p == ')') d--; if (d) p++; }
                arg = a0; argl = (uint32_t)(p - a0); if (p < e) p++;
            }
            *spec += 0x100; any = 1;
            if (wu_streq(nm, "first-child")) s->pseudo |= PF_FIRST; else if (wu_streq(nm, "last-child")) s->pseudo |= PF_LAST;
            else if (wu_streq(nm, "only-child")) s->pseudo |= PF_ONLY; else if (wu_streq(nm, "first-of-type")) s->pseudo |= PF_FIRST_T;
            else if (wu_streq(nm, "last-of-type")) s->pseudo |= PF_LAST_T; else if (wu_streq(nm, "only-of-type")) s->pseudo |= PF_ONLY_T;
            else if (wu_streq(nm, "root")) s->pseudo |= PF_ROOT; else if (wu_streq(nm, "empty")) s->pseudo |= PF_EMPTY;
            else if (wu_streq(nm, "link") || wu_streq(nm, "any-link")) s->pseudo |= PF_LINK;
            else if (wu_streq(nm, "hover")) s->pseudo |= PF_HOVER; else if (wu_streq(nm, "focus") || wu_streq(nm, "focus-visible") || wu_streq(nm, "focus-within")) s->pseudo |= PF_FOCUS;
            else if (wu_streq(nm, "checked")) s->pseudo |= PF_CHECKED; else if (wu_streq(nm, "disabled")) s->pseudo |= PF_DISABLED; else if (wu_streq(nm, "enabled")) s->pseudo |= PF_ENABLED;
            else if (wu_streq(nm, "visited") || wu_streq(nm, "active") || wu_streq(nm, "target") || wu_streq(nm, "invalid") || wu_streq(nm, "placeholder-shown") || wu_streq(nm, "read-only") || wu_streq(nm, "out-of-range") || wu_streq(nm, "indeterminate")) s->pseudo |= PF_NEVER;
            else if (wu_streq(nm, "required") || wu_streq(nm, "optional") || wu_streq(nm, "valid") || wu_streq(nm, "in-range") || wu_streq(nm, "read-write") || wu_streq(nm, "defined") || wu_streq(nm, "scope")) { /* aceptadas sin efecto */ }
            else if (wu_streq(nm, "nth-child") || wu_streq(nm, "nth-last-child") || wu_streq(nm, "nth-of-type") || wu_streq(nm, "nth-last-of-type")) {
                char tmp[32]; if (!arg || argl >= sizeof(tmp)) return 0; memcpy(tmp, arg, argl); tmp[argl] = 0;
                char *of = (char *)wu_find(tmp, " of "); if (of) *of = 0;
                if (!parse_nth(tmp, &s->nth_a, &s->nth_b)) return 0;
                s->pseudo |= PF_NTH; s->nth_kind = (uint8_t)(wu_streq(nm, "nth-child") ? 1 : wu_streq(nm, "nth-last-child") ? 2 : wu_streq(nm, "nth-of-type") ? 3 : 4);
            } else if (wu_streq(nm, "has") && !g_wl181_off) {
                /* SOMA_188: :has() con selectores relativos completos: "x" (descendiente), "> x", "+ x", "~ x" */
                if (!arg || !argl || in_not || s->list_calls >= 4) return 0;
                struct wsel **hs = (struct wsel **)wa_zalloc(&c->d->ar, sizeof(*hs) * 8u); if (!hs) return 0;
                const char *q = arg, *qe = arg + argl; uint8_t count = 0, mode = 0; int spe = c->pe_seen;
                while (q < qe && count < 8) {
                    while (q < qe && wu_isspace(*q)) q++;
                    const char *end = q; int depth = 0; char quote = 0;
                    for (; end < qe; end++) {
                        if (quote) { if (*end == '\\' && end + 1 < qe) { end++; continue; } if (*end == quote) quote = 0; continue; }
                        if (*end == '\'' || *end == '"') { quote = *end; continue; }
                        if (*end == '[' || *end == '(') depth++; else if (*end == ']' || *end == ')') depth--; else if (*end == ',' && !depth) break;
                    }
                    uint8_t m = 3; if (q < end && (*q == '>' || *q == '+' || *q == '~')) { m = *q == '>' ? 4 : *q == '+' ? 5 : 6; q++; }
                    if (count && m != mode) return 0;   /* modos mezclados: no soportado */
                    mode = m;
                    struct wsel *sl = compile_selector(c, q, (uint32_t)(end - q)); if (!sl || sl->pe) return 0;
                    hs[count++] = sl;
                    if (end >= qe) break;
                    q = end + 1;
                }
                c->pe_seen = spe;
                if (!count) return 0;
                struct wlist *l = (struct wlist *)wa_zalloc(&c->d->ar, sizeof(*l)); if (!l) return 0;
                l->hsel = hs; l->count = count; l->mode = mode;
                l->next = s->lists; s->lists = l; s->list_calls++;
            } else if (wu_streq(nm, "not") || wu_streq(nm, "is") || wu_streq(nm, "where") || wu_streq(nm, "matches")) {
                /* SOMA 144: listas de compuestos de CSS Selectors Level 4.
                 * Hasta cuatro llamadas por compuesto mantienen acotadas
                 * memoria y recursion. No admitimos selectores relativos. */
                if (!arg || !argl || in_not || s->list_calls >= 4) return 0;
                struct wsimple *lst = (struct wsimple *)wa_zalloc(&c->d->ar, sizeof(*lst) * 8u);
                if (!lst) return 0;
                const char *q = arg, *qe = arg + argl;
                uint32_t max_sp = 0; uint8_t count = 0;
                while (q < qe) {
                    while (q < qe && wu_isspace(*q)) q++;
                    if (q >= qe || count >= 8) return 0;
                    const char *end = q; int depth = 0; char quote = 0;
                    for (; end < qe; end++) {
                        if (quote) { if (*end == '\\' && end + 1 < qe) { end++; continue; } if (*end == quote) quote = 0; continue; }
                        if (*end == '\'' || *end == '"') { quote = *end; continue; }
                        if (*end == '[' || *end == '(') depth++;
                        else if (*end == ']' || *end == ')') { if (depth == 0) return 0; depth--; }
                        else if (*end == ',' && !depth) break;
                    }
                    if (quote || depth) return 0;
                    uint32_t sp2 = 0;
                    const char *r = parse_compound(c, q, end, &lst[count], &sp2, 1);
                    if (!r) return 0;
                    while (r < end && wu_isspace(*r)) r++;
                    if (r != end) return 0;
                    if (sp2 > max_sp) max_sp = sp2;
                    count++;
                    if (end == qe) break;
                    q = end + 1;
                    if (q == qe) return 0; /* coma terminal */
                }
                if (!count) return 0;
                struct wlist *l = (struct wlist *)wa_zalloc(&c->d->ar, sizeof(*l));
                if (!l) return 0;
                l->items = lst; l->count = count;
                l->mode = (uint8_t)(wu_streq(nm, "not") ? 2 : 1);
                l->next = s->lists; s->lists = l; s->list_calls++;
                /* Quitar especificidad de la pseudoclase funcional. */
                *spec -= 0x100;
                if (!wu_streq(nm, "where")) *spec += max_sp;
            } else return 0;
            continue;
        }
        break;
    }
    return any ? p : 0;
}

static struct wsel *compile_selector(struct wcss *c, const char *s, uint32_t n)
{
    struct wsimple tmp[16]; uint8_t combs[16]; int cnt = 0; uint32_t spec = 0;
    const char *p = s, *e = s + n; char pend = 0;
    c->pe_seen = 0;
    while (p < e && wu_isspace(*p)) p++;
    while (p < e) {
        if (cnt >= 16) return 0;
        const char *r = parse_compound(c, p, e, &tmp[cnt], &spec, 0);
        if (!r) return 0;
        combs[cnt] = (uint8_t)(cnt == 0 ? 0 : pend);
        cnt++; p = r;
        pend = 0; int ws = 0;
        while (p < e && wu_isspace(*p)) { p++; ws = 1; }
        if (p >= e) break;
        if (*p == '>' || *p == '+' || *p == '~') { pend = *p++; while (p < e && wu_isspace(*p)) p++; }
        else if (ws) pend = ' ';
        else return 0;
        if (p >= e) return 0;
    }
    if (!cnt) return 0;
    struct wsel *sel = (struct wsel *)wa_zalloc(&c->d->ar, sizeof(*sel));
    struct wsimple *parts = (struct wsimple *)wa_zalloc(&c->d->ar, sizeof(struct wsimple) * (uint32_t)cnt);
    if (!sel || !parts) return 0;
    for (int i = 0; i < cnt; i++) { parts[i] = tmp[cnt - 1 - i]; parts[i].comb = combs[cnt - 1 - i]; }
    sel->parts = parts; sel->n = (uint8_t)cnt; sel->spec = spec; sel->pe = (uint8_t)c->pe_seen;
    return sel;
}

static struct wnode *prev_elem(struct wnode *n) { struct wnode *p = n->prev; while (p && p->type != WN_ELEM) p = p->prev; return p; }
static struct wnode *next_elem(struct wnode *n) { struct wnode *p = n->next; while (p && p->type != WN_ELEM) p = p->next; return p; }

static int attr_match(const struct wattrsel *a, const struct wnode *n)
{
    const char *v = wd_attr(n, a->n);
    if (!v) return 0;
    if (!a->op) return 1;
    uint32_t vl = wu_strlen(v), al = wu_strlen(a->v);
    int ci = a->ci;
#define EQN(x, y, k) (ci ? wu_strnieq(x, y, k) : wu_strneq(x, y, k))
    switch (a->op) {
    case '=': return vl == al && EQN(v, a->v, al);
    case '^': return al && vl >= al && EQN(v, a->v, al);
    case '$': return al && vl >= al && EQN(v + vl - al, a->v, al);
    case '*': { if (!al) return 0; for (uint32_t i = 0; i + al <= vl; i++) if (EQN(v + i, a->v, al)) return 1; return 0; }
    case '~': { const char *p = v; while (*p) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if ((uint32_t)(p - s) == al && al && EQN(s, a->v, al)) return 1; } return 0; }
    case '|': return vl >= al && EQN(v, a->v, al) && (vl == al || v[al] == '-');
    }
#undef EQN
    return 0;
}

static int nth_ok(int idx, int a, int b) { if (a == 0) return idx == b; int d = idx - b; return d % a == 0 && d / a >= 0; }

/* SOMA_152_WEB_FLUIDO: filtro de Bloom de clases. Cada elemento resume sus clases en 64 bits (2 bits por clase) y cada
 * compuesto del selector guarda los bits de las suyas: si faltan bits, el compuesto no puede encajar y se descarta sin
 * buscar el atributo class ni comparar cadenas. Es la comprobacion mas repetida del calculo de estilos (cada regla
 * candidata y cada ancestro en los combinadores descendientes). La generacion avanza en cada entrada publica (calculo,
 * querySelector, matches): dentro de ellas el DOM no cambia; entre ellas JS puede cambiar class y el filtro se rehace. */
static uint32_t s_cb_gen152 = 1;
static uint64_t cls_bits152(const char *s, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return (1ull << (h & 63)) | (1ull << ((h >> 6) & 63));
}
static uint64_t node_bloom152(struct wnode *n)
{
    if (n->cbloom_gen == s_cb_gen152) return n->cbloom;
    uint64_t b = 0; const char *c = wd_attr(n, "class");
    if (c) while (*c) {
        while (wu_isspace(*c)) c++;
        const char *t = c; while (*c && !wu_isspace(*c)) c++;
        if (c > t) b |= cls_bits152(t, (uint32_t)(c - t));
    }
    n->cbloom = b; n->cbloom_gen = s_cb_gen152;
    return b;
}
static uint64_t simple_mask152(const struct wsimple *s)
{
    if (!s->cmask_ok) {
        uint64_t m = 0;
        for (int i = 0; i < s->ncls; i++) m |= cls_bits152(s->cls[i], wu_strlen(s->cls[i]));
        ((struct wsimple *)s)->cmask = m; ((struct wsimple *)s)->cmask_ok = 1;
    }
    return s->cmask;
}
static void cbloom_new_gen152(void) { if (++s_cb_gen152 == 0) s_cb_gen152 = 1; }

static int match_from(const struct wsel *sel, int i, struct wnode *n);
static int match_simple(const struct wsimple *s, struct wnode *n)
{
    if (s->tag) { if (n->tag != s->tag) return 0; }
    else if (s->name && !wu_streq(n->name, s->name)) return 0;
    if (s->ncls) { uint64_t m = simple_mask152(s); if ((node_bloom152(n) & m) != m) return 0; }   /* SOMA_152 */
    if (s->id) { const char *v = wd_attr(n, "id"); if (!v || !wu_streq(v, s->id)) return 0; }
    for (int i = 0; i < s->ncls; i++) if (!wd_has_class(n, s->cls[i])) return 0;
    for (int i = 0; i < s->nattr; i++) if (!attr_match(&s->at[i], n)) return 0;
    uint32_t pf = s->pseudo;
    if (pf) {
        if (pf & PF_NEVER) return 0;
        if ((pf & PF_FIRST) && prev_elem(n)) return 0;
        if ((pf & PF_LAST) && next_elem(n)) return 0;
        if ((pf & PF_ONLY) && (prev_elem(n) || next_elem(n))) return 0;
        if (pf & (PF_FIRST_T | PF_LAST_T | PF_ONLY_T)) {
            int before = 0, after = 0;
            for (struct wnode *q = n->prev; q; q = q->prev) if (q->type == WN_ELEM && q->tag == n->tag && wu_streq(q->name, n->name)) before = 1;
            for (struct wnode *q = n->next; q; q = q->next) if (q->type == WN_ELEM && q->tag == n->tag && wu_streq(q->name, n->name)) after = 1;
            if ((pf & PF_FIRST_T) && before) return 0;
            if ((pf & PF_LAST_T) && after) return 0;
            if ((pf & PF_ONLY_T) && (before || after)) return 0;
        }
        if ((pf & PF_ROOT) && n->tag != WT_HTML) return 0;
        if ((pf & PF_EMPTY)) { for (struct wnode *q = n->first; q; q = q->next) if (q->type == WN_ELEM || (q->type == WN_TEXT && q->tlen)) return 0; }
        if ((pf & PF_LINK) && !((n->tag == WT_A || n->tag == WT_AREA) && wd_attr(n, "href"))) return 0;
        if ((pf & PF_HOVER) && !(n->flags & WNF_HOVER)) return 0;
        if ((pf & PF_FOCUS) && !(n->flags & WNF_FOCUS)) return 0;
        if ((pf & PF_CHECKED) && !((n->flags & WNF_CHECKED) || (n->tag == WT_OPTION && wd_attr(n, "selected")))) return 0;
        if ((pf & PF_DISABLED) && !(wd_attr(n, "disabled") || (n->flags & WNF_DISABLED))) return 0;
        if ((pf & PF_ENABLED) && (wd_attr(n, "disabled") || (n->flags & WNF_DISABLED))) return 0;
        if (pf & PF_NTH) {
            int idx = 1, k = s->nth_kind;
            if (k == 1 || k == 3) { for (struct wnode *q = n->prev; q; q = q->prev) if (q->type == WN_ELEM && (k == 1 || (q->tag == n->tag && wu_streq(q->name, n->name)))) idx++; }
            else { for (struct wnode *q = n->next; q; q = q->next) if (q->type == WN_ELEM && (k == 2 || (q->tag == n->tag && wu_streq(q->name, n->name)))) idx++; }
            if (!nth_ok(idx, s->nth_a, s->nth_b)) return 0;
        }
    }
    if (s->not_ && match_simple(s->not_, n)) return 0;
    for (const struct wlist *l = s->lists; l; l = l->next) {
        int any = 0;
        if (l->mode >= 3) {   /* SOMA_188: :has() */
            for (uint8_t i = 0; i < l->count && !any; i++) {
                const struct wsel *hs = l->hsel[i];
                if (l->mode == 4) { for (struct wnode *k = n->first; k && !any; k = k->next) if (k->type == WN_ELEM && match_from(hs, 0, k)) any = 1; }
                else if (l->mode == 5) { struct wnode *k = n->next; while (k && k->type != WN_ELEM) k = k->next; if (k && match_from(hs, 0, k)) any = 1; }
                else if (l->mode == 6) { for (struct wnode *k = n->next; k && !any; k = k->next) if (k->type == WN_ELEM && match_from(hs, 0, k)) any = 1; }
                else { int budget = 4000; for (struct wnode *k = n->first; k && budget-- > 0 && !any; ) {
                        if (k->type == WN_ELEM && match_from(hs, 0, k)) any = 1;
                        if (k->first) { k = k->first; continue; }
                        while (k && k != n && !k->next) k = k->parent;
                        if (!k || k == n) break;
                        k = k->next; } }
            }
            if (!any) return 0;
            continue;
        }
        for (uint8_t i = 0; i < l->count; i++) {
            if (l->items && match_simple(&l->items[i], n)) { any = 1; break; }
        }
        if (l->mode == 2 ? any : !any) return 0;
    }
    return 1;
}

static int match_from(const struct wsel *sel, int i, struct wnode *n)
{
    if (!match_simple(&sel->parts[i], n)) return 0;
    if (i == sel->n - 1) return 1;
    uint8_t cb = sel->parts[i].comb;
    struct wnode *q;
    switch (cb) {
    case '>': q = n->parent; if (wd_is_shadow_root(q)) q = q->parent;   /* SOMA_188: CSS acotado: el hijo directo de la raiz sombra cuelga del anfitrion */
              return q && q->type == WN_ELEM && match_from(sel, i + 1, q);
    case '+': q = prev_elem(n); return q && match_from(sel, i + 1, q);
    case '~': for (q = prev_elem(n); q; q = prev_elem(q)) if (match_from(sel, i + 1, q)) return 1; return 0;
    default: for (q = n->parent; q && q->type == WN_ELEM; q = q->parent) if (!wd_is_shadow_root(q) && match_from(sel, i + 1, q)) return 1; return 0;
    }
}

/* SOMA_152_WEB_FLUIDO: invalidacion selectiva por :hover.
 * Antes, con cualquier regla :hover en la pagina, cada cambio del elemento bajo el puntero recalculaba los estilos y
 * maquetaba el documento entero (decenas de ms en paginas con Bootstrap). Un compuesto con :hover solo puede cambiar su
 * resultado en un nodo cuyo estado hover cambio; si ninguno de esos nodos encaja con el compuesto (sin contar el propio
 * :hover), ninguna regla cambia y no hace falta reestilar. Ante :hover dentro de :not()/:is() se responde "si". */
static int hover_nested152(const struct wsimple *s, int depth)
{
    if (!s || depth > 6) return 1;
    if (s->not_ && ((s->not_->pseudo & PF_HOVER) || hover_nested152(s->not_, depth + 1))) return 1;
    for (const struct wlist *l = s->lists; l; l = l->next) {
        /* SOMA_204: las listas de :has() (modo >= 3, parche 188) no tienen `items` sino `hsel`: selectores completos. Aqui se leia
         * items[0] siendo NULL: bastaba una hoja con :has() y mover el raton para tumbar el motor (fallo de pagina en 0x61). */
        if (l->mode >= 3) {
            for (uint8_t i = 0; l->hsel && i < l->count; i++) {
                const struct wsel *hs = l->hsel[i]; if (!hs || !hs->parts) continue;
                for (int k = 0; k < hs->n; k++) if ((hs->parts[k].pseudo & PF_HOVER) || hover_nested152(&hs->parts[k], depth + 1)) return 1;
            }
            continue;
        }
        for (uint8_t i = 0; l->items && i < l->count; i++)
            if ((l->items[i].pseudo & PF_HOVER) || hover_nested152(&l->items[i], depth + 1)) return 1;
    }
    return 0;
}
static int on_chain152(struct wnode *chain, struct wnode *n) { for (; chain; chain = chain->parent) if (chain == n) return 1; return 0; }
static int compound_hits152(const struct wsimple *s, struct wnode *a, struct wnode *b)
{
    for (struct wnode *n = a; n; n = n->parent) {
        if (n->type != WN_ELEM || on_chain152(b, n)) continue;    /* solo los nodos cuyo estado hover cambio */
        uint16_t f = n->flags; n->flags = (uint16_t)(n->flags | WNF_HOVER);
        int m = match_simple(s, n);
        n->flags = f;
        if (m) return 1;
    }
    return 0;
}
int wc_hover_affects(struct wcss *c, struct wnode *old_hover, struct wnode *new_hover)
{
    if (!c) return 1;
    if (old_hover == new_hover) return 0;
    cbloom_new_gen152();
    for (int i = 0; i < c->nrules; i++) {
        const struct wsel *sel = c->rules[i]->sel; if (!sel) continue;
        for (int k = 0; k < sel->n; k++) {
            const struct wsimple *s = &sel->parts[k];
            if (hover_nested152(s, 0)) return 1;
            if (!(s->pseudo & PF_HOVER)) continue;
            if (compound_hits152(s, old_hover, new_hover) || compound_hits152(s, new_hover, old_hover)) return 1;
        }
    }
    return 0;
}

/* ============================ hojas ============================ */
static int add_rule_ptr(struct wcss *c, struct wrule *r)
{
    if (c->nrules >= c->cap) {
        int nc = c->cap ? c->cap * 2 : 256;
        struct wrule **nr = (struct wrule **)kmalloc(sizeof(*nr) * (uint32_t)nc); if (!nr) return 0;
        if (c->rules) { memcpy(nr, c->rules, sizeof(*nr) * (uint32_t)c->nrules); kfree(c->rules); }
        c->rules = nr; c->cap = nc;
    }
    c->rules[c->nrules++] = r; return 1;
}

struct wcss *wc_new(struct wdoc *d)
{
    struct wcss *c = (struct wcss *)kmalloc(sizeof(*c)); if (!c) return 0;
    memset(c, 0, sizeof(*c)); c->d = d;
    c->match_cap = 1024; c->match = (struct wrule **)kmalloc(sizeof(struct wrule *) * 1024);
    c->scratch = (char *)kmalloc(2048);
    if (!c->match || !c->scratch) { wc_free(c); return 0; }
    return c;
}
static void vcache_clear152(void);
void wc_free(struct wcss *c)
{
    if (!c) return;
    vcache_clear152();
    if (c->vtab) kfree(c->vtab);
    if (c->stab) kfree(c->stab);
    if (c->stab_key) kfree(c->stab_key);
    if (c->rules) kfree(c->rules);
    if (c->match) kfree(c->match);
    if (c->scratch) kfree(c->scratch);
    if (c->imports) kfree(c->imports);
    kfree(c);
}

static const char *skip_to_block_end(const char *p, const char *e)   /* p apunta tras '{'; devuelve el '}' que cierra */
{
    int d = 1;
    while (p < e) {
        if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) { if (*p == '\\') p++; p++; } if (p < e) p++; continue; }
        if (*p == '{') d++;
        else if (*p == '}') { d--; if (!d) return p; }
        p++;
    }
    return e;
}

static int parse_decls(struct wcss *c, const char *s, const char *e, struct wdecl *out, int max)
{
    int n = 0; const char *p = s;
    while (p < e && n < max) {
        while (p < e && (wu_isspace(*p) || *p == ';')) p++;
        if (p >= e) break;
        const char *ns = p; while (p < e && *p != ':' && *p != ';') p++;
        if (p >= e || *p != ':') { while (p < e && *p != ';') p++; continue; }
        const char *ne = p; while (ne > ns && wu_isspace(ne[-1])) ne--;
        p++;
        const char *vs = p; int depth = 0;
        while (p < e) {
            if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) { if (*p == '\\') p++; p++; } if (p < e) p++; continue; }
            if (*p == '(') depth++; else if (*p == ')') { if (depth) depth--; }
            else if (*p == ';' && !depth) break;
            p++;
        }
        const char *ve = p; while (ve > vs && wu_isspace(ve[-1])) ve--;
        int imp = 0;
        if (ve - vs >= 10) { const char *b = ve - 1; while (b > vs && wu_isspace(*b)) b--; if (b - vs >= 8 && wu_strnieq(b - 8, "important", 9)) { const char *q = b - 9; while (q > vs && wu_isspace(*q)) q--; if (*q == '!') { imp = 1; ve = q; while (ve > vs && wu_isspace(ve[-1])) ve--; } } }
        int pid = prop_id(ns, (uint32_t)(ne - ns));
        if (!pid) continue;
        out[n].prop = (uint16_t)pid; out[n].imp = (uint8_t)imp; out[n].val = vs; out[n].vlen = (uint16_t)(ve - vs > 65000 ? 65000 : ve - vs);
        out[n].name = pid == PR_CUSTOM ? ns : 0; out[n].nlen = (uint16_t)(ne - ns);
        c = c; n++;
    }
    return n;
}

static void media_eval(struct wcss *c, const char *s, uint32_t n, int *minw, int *maxw, int *ok)
{
    /* devuelve el rango de ancho en que la consulta es verdadera */
    int lo_any = 65535, hi_any = 0, any_ok = 0;
    const char *p = s, *e = s + n;
    while (p <= e) {
        const char *q = p; int d = 0; while (q < e && !(*q == ',' && !d)) { if (*q == '(') d++; else if (*q == ')') d--; q++; }
        int lo = 0, hi = 65535, good = 1, neg = 0;
        const char *r = p;
        while (r < q) {
            while (r < q && wu_isspace(*r)) r++;
            if (r >= q) break;
            if (*r == '(') {
                const char *f = r + 1, *fe = f; int dd = 1; while (fe < q && dd) { if (*fe == '(') dd++; else if (*fe == ')') dd--; if (dd) fe++; }
                char b[64]; uint32_t fl = (uint32_t)(fe - f); if (fl >= sizeof(b)) fl = sizeof(b) - 1; memcpy(b, f, fl); b[fl] = 0;
                for (char *t = b; *t; t++) *t = (char)wu_lower(*t);
                const char *colon = b; while (*colon && *colon != ':') colon++;
                if (wu_startsi(b, "min-width") && *colon) { int v = wc_parse_length_px(colon + 1, 0); if (v > lo) lo = v; }
                else if (wu_startsi(b, "max-width") && *colon) { int v = wc_parse_length_px(colon + 1, 0); if (v < hi) hi = v; }
                else if (wu_startsi(b, "width") && !*colon) { const char *o = b + 5; while (wu_isspace(*o)) o++; int ge = (o[0] == '>'), le = (o[0] == '<'); if (ge || le) { o++; int eq = (*o == '='); if (eq) o++; int v = wc_parse_length_px(o, 0); if (ge) lo = v + (eq ? 0 : 1); else hi = v - (eq ? 0 : 1); } }
                else if (wu_find(b, "prefers-color-scheme") && wu_find(b, "dark")) good = 0;
                else if (wu_find(b, "prefers-reduced-motion") && wu_find(b, "reduce")) good = 0;
                else if (wu_find(b, "resolution") || wu_find(b, "pixel-ratio")) { if (wu_find(b, "min-") && !wu_find(b, "1")) good = 0; if (wu_find(b, "min-") && (wu_find(b, "1.5") || wu_find(b, "2") || wu_find(b, "192"))) good = 0; }
                else if (wu_find(b, "hover") && wu_find(b, "none")) good = 0;
                else if (wu_find(b, "pointer") && wu_find(b, "coarse")) good = 0;
                else if (wu_find(b, "orientation") && wu_find(b, "portrait") && c->vw > c->vh) good = 0;
                else if (wu_find(b, "min-aspect") || wu_find(b, "forced-colors") || wu_find(b, "prefers-contrast")) good = 0;
                r = fe < q ? fe + 1 : q; continue;
            }
            const char *w = r; while (r < q && !wu_isspace(*r) && *r != '(') r++;
            uint32_t wl = (uint32_t)(r - w);
            if (wl == 3 && wu_strnieq(w, "not", 3)) neg = 1;
            else if ((wl == 5 && wu_strnieq(w, "print", 5)) || (wl == 6 && wu_strnieq(w, "speech", 6))) good = 0;
        }
        if (neg) { if (lo == 0 && hi == 65535) good = !good; else good = 0; lo = 0; hi = 65535; }
        if (good) { any_ok = 1; if (lo < lo_any) lo_any = lo; if (hi > hi_any) hi_any = hi; }
        p = q + 1;
    }
    *ok = any_ok; *minw = any_ok ? lo_any : 0; *maxw = any_ok ? hi_any : 0;
}

static void add_import(struct wcss *c, const char *u, uint32_t n)
{
    if (c->nimp >= c->cimp) {
        int nc = c->cimp ? c->cimp * 2 : 8; char **ni = (char **)kmalloc(sizeof(char *) * (uint32_t)nc); if (!ni) return;
        if (c->imports) { memcpy(ni, c->imports, sizeof(char *) * (uint32_t)c->nimp); kfree(c->imports); }
        c->imports = ni; c->cimp = nc;
    }
    c->imports[c->nimp++] = wa_strndup(&c->d->ar, u, n);
}
const char *wc_next_import(struct wcss *c) { return c->imp_next < c->nimp ? c->imports[c->imp_next++] : 0; }

static void parse_rules(struct wcss *c, const char *s, const char *e, int origin, int minw, int maxw, int depth)
{
    const char *p = s;
    while (p < e) {
        /* SOMA_188: solo los tokens CDO "<!--" y CDC "-->" se ignoran (antes se saltaba hasta el siguiente '>': con
           "<![CDATA[" se retomaba a media regla y salian selectores universales, p. ej. "* {display:table-cell}") */
        for (;;) {
            while (p < e && wu_isspace(*p)) p++;
            if (p + 3 < e && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') { p += 4; continue; }
            if (p + 2 < e && p[0] == '-' && p[1] == '-' && p[2] == '>') { p += 3; continue; }
            break;
        }
        if (p >= e) break;
        if (*p == '@') {
            const char *ns = ++p; while (p < e && (wu_isalpha(*p) || *p == '-')) p++;
            uint32_t nl = (uint32_t)(p - ns);
            const char *pre = p; int dd = 0;
            while (p < e && (dd || (*p != '{' && *p != ';'))) {
                if (*p == '(') dd++; else if (*p == ')') { if (dd) dd--; }
                else if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) p++; }
                p++;
            }
            const char *pre_e = p;
            if (p >= e) break;
            if (*p == ';') {
                p++;
                if (nl == 6 && wu_strnieq(ns, "import", 6) && depth == 0) {
                    const char *u = pre; while (u < pre_e && wu_isspace(*u)) u++;
                    const char *us; uint32_t ul = 0;
                    if (wu_startsi(u, "url(")) { u += 4; while (wu_isspace(*u)) u++; char q = (*u == '"' || *u == '\'') ? *u++ : 0; us = u; while (u < pre_e && (q ? *u != q : (*u != ')' && !wu_isspace(*u)))) u++; ul = (uint32_t)(u - us); while (u < pre_e && *u != ')') u++; if (u < pre_e) u++; }
                    else if (*u == '"' || *u == '\'') { char q = *u++; us = u; while (u < pre_e && *u != q) u++; ul = (uint32_t)(u - us); if (u < pre_e) u++; }
                    else continue;
                    int ok = 1, mn = 0, mx = 0; while (u < pre_e && wu_isspace(*u)) u++;
                    if (u < pre_e) media_eval(c, u, (uint32_t)(pre_e - u), &mn, &mx, &ok);
                    if (ok && ul) add_import(c, us, ul);
                }
                continue;
            }
            const char *bs = p + 1, *be = skip_to_block_end(bs, e);
            p = be < e ? be + 1 : e;
            if (depth > 4) continue;
            if (nl == 5 && wu_strnieq(ns, "media", 5)) {
                int mn, mx, ok; media_eval(c, pre, (uint32_t)(pre_e - pre), &mn, &mx, &ok);
                if (ok) parse_rules(c, bs, be, origin, mn > minw ? mn : minw, (mx && (!maxw || mx < maxw)) ? mx : maxw, depth + 1);
            } else if ((nl == 8 && wu_strnieq(ns, "supports", 8)) || (nl == 5 && wu_strnieq(ns, "layer", 5)) || (nl == 8 && wu_strnieq(ns, "document", 8))) {
                parse_rules(c, bs, be, origin, minw, maxw, depth + 1);
            }
            continue;
        }
        const char *ss = p; int qd = 0;
        while (p < e && (*p != '{' || qd)) { if (*p == '(' || *p == '[') qd++; else if (*p == ')' || *p == ']') { if (qd) qd--; } else if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) p++; } p++; }
        if (p >= e) break;
        const char *se = p; const char *bs = p + 1, *be = skip_to_block_end(bs, e);
        p = be < e ? be + 1 : e;
        /* declaraciones */
        int maxd = 1; for (const char *t = bs; t < be; t++) if (*t == ';') maxd++;
        struct wdecl *dl = (struct wdecl *)wa_alloc(&c->d->ar, (uint32_t)maxd * (uint32_t)sizeof(struct wdecl));
        if (!dl) return;
        int nd = parse_decls(c, bs, be, dl, maxd);
        if (!nd) continue;
        /* lista de selectores */
        const char *q = ss;
        while (q < se) {
            const char *r = q; int d2 = 0;
            while (r < se && !(*r == ',' && !d2)) { if (*r == '(' || *r == '[') d2++; else if (*r == ')' || *r == ']') d2--; r++; }
            struct wsel *sel = compile_selector(c, q, (uint32_t)(r - q));
            if (sel && sel->pe) { int hasclear = 0; for (int di = 0; di < nd; di++) if (dl[di].prop == PR_CLEAR && !is_kw(dl[di].val, dl[di].vlen, "none")) hasclear = 1; if (hasclear) sel->pe |= 4; }   /* SOMA_188: se conservan (contenido generado) */
            if (sel) {
                struct wrule *ru = (struct wrule *)wa_zalloc(&c->d->ar, sizeof(*ru));
                if (!ru) return;
                ru->sel = sel; ru->decls = dl; ru->nd = (uint16_t)nd; ru->origin = (uint8_t)origin;
                ru->minw = (uint16_t)minw; ru->maxw = (uint16_t)(maxw ? maxw : 65535); ru->order = ++c->order;
                add_rule_ptr(c, ru);
            }
            q = r + 1;
        }
    }
}

void wc_add_sheet(struct wcss *c, const char *css, uint32_t len, int origin)
{
    if (!c || !css || !len) return;
    char *cp = (char *)wa_alloc(&c->d->ar, len + 1); if (!cp) return;
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i++) {            /* quita comentarios */
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') { i += 2; while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++; i++; continue; }
        if (css[i] == '"' || css[i] == '\'') { char q = css[i]; cp[o++] = css[i++]; while (i < len && css[i] != q) { if (css[i] == '\\' && i + 1 < len) cp[o++] = css[i++]; cp[o++] = css[i++]; } if (i < len) cp[o++] = css[i]; continue; }
        cp[o++] = css[i];
    }
    cp[o] = 0;
    parse_rules(c, cp, cp + o, origin, 0, 0, 0);
}

/* ============================ calculo de estilos ============================ */
static void build_index(struct wcss *c)
{
    c->univ = 0; memset(c->tagt, 0, sizeof(c->tagt)); memset(c->idt, 0, sizeof(c->idt)); memset(c->clst, 0, sizeof(c->clst));
    for (int i = 0; i < c->nrules; i++) {
        struct wrule *r = c->rules[i]; const struct wsimple *s = &r->sel->parts[0];
        r->bnext = 0;
        struct wrule **slot;
        if (s->id) slot = &c->idt[wu_hash(s->id, wu_strlen(s->id)) & 127];
        else if (s->ncls) slot = &c->clst[wu_hash(s->cls[0], wu_strlen(s->cls[0])) & 127];
        else if (s->tag) slot = &c->tagt[s->tag];
        else slot = &c->univ;
        /* conserva el orden de aparicion */
        struct wrule **pp = slot; while (*pp) pp = &(*pp)->bnext; *pp = r;
    }
}

static const struct wvar *find_var_slow(const struct wvar *v, const char *name, uint32_t nl)
{
    for (; v; v = v->next) if (wu_strneq(v->name, name, nl) && v->name[nl] == 0) return v;
    return 0;
}
/* SOMA_152_WEB_FLUIDO: cache de var(). Las listas de variables son persistentes (cada elemento antepone las suyas a la
 * lista del padre y los nodos existentes nunca cambian), asi que (cabeza de lista, nombre) -> resultado es estable durante
 * un wc_compute. Con Bootstrap (~300 propiedades en :root) cada var() recorria la lista entera comparando cadenas: era la
 * mayor parte del calculo de estilos. Se vacia al empezar cada wc_compute (las direcciones del arena pueden reutilizarse). */
#define VC152 2048
struct vc152 { const struct wvar *head, *res; uint64_t h; uint32_t nl; };
static struct vc152 s_vc152[VC152];
static void vcache_clear152(void) { memset(s_vc152, 0, sizeof s_vc152); }
static const struct wvar *find_var(const struct wvar *v, const char *name, uint32_t nl)
{
    if (!v) return 0;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < nl; i++) { h ^= (uint8_t)name[i]; h *= 1099511628211ull; }
    struct vc152 *e = &s_vc152[(h ^ ((uint64_t)(uintptr_t)v >> 4) ^ ((uint64_t)(uintptr_t)v >> 15)) & (VC152 - 1)];
    if (e->head == v && e->h == h && e->nl == nl && (!e->res || (wu_strneq(e->res->name, name, nl) && e->res->name[nl] == 0))) return e->res;
    const struct wvar *r = find_var_slow(v, name, nl);
    e->head = v; e->res = r; e->h = h; e->nl = nl;
    return r;
}

static int expand_vars(struct wcss *c, const struct wvar *vars, const char *in, uint32_t n, char *out, uint32_t max, int depth)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; ) {
        if (n - i > 4 && wu_strnieq(in + i, "var(", 4)) {
            uint32_t j = i + 4, d = 1; while (j < n && wu_isspace(in[j])) j++;
            uint32_t ns = j; while (j < n && in[j] != ',' && in[j] != ')' && !wu_isspace(in[j])) j++;
            uint32_t nl = j - ns; while (j < n && wu_isspace(in[j])) j++;
            const char *fb = 0; uint32_t fl = 0;
            if (j < n && in[j] == ',') { j++; fb = in + j; uint32_t k = j; while (k < n) { if (in[k] == '(') d++; else if (in[k] == ')') { d--; if (!d) break; } k++; } fl = k - j; j = k; }
            while (j < n && d && in[j] != ')') j++;
            const struct wvar *v = find_var(vars, in + ns, nl);
            if (depth > 20) return 0;   /* SOMA_188: antes 6 (los sistemas de diseno encadenan muchas variables) */
            if (v) { int r = expand_vars(c, vars, v->val, wu_strlen(v->val), out + o, max - o, depth + 1); if (!r) return 0; o += (uint32_t)r - 1; }
            else if (fb) { int r = expand_vars(c, vars, fb, fl, out + o, max - o, depth + 1); if (!r) return 0; o += (uint32_t)r - 1; }
            else return 0;
            i = j + 1; continue;
        }
        if (o + 2 >= max) return 0;
        out[o++] = in[i++];
    }
    out[o] = 0; return (int)o + 1;
}

/* SOMA_188: valor calculado de una propiedad personalizada (--x) para getComputedStyle().getPropertyValue */
int wc_custom_prop(const struct wstyle *st, const char *name, char *out, uint32_t max)
{
    out[0] = 0; if (!st || max < 2) return -1;
    uint32_t nl = wu_strlen(name); const struct wvar *v = find_var_slow(st->vars, name, nl);
    if (!v || !v->val) return -1;
    int r = expand_vars(0, st->vars, v->val, wu_strlen(v->val), out, max, 0);
    if (!r) { wu_copy(out, v->val, max); }
    /* recorta espacios como Chrome */
    uint32_t a = 0, z = wu_strlen(out); while (a < z && wu_isspace(out[a])) a++; while (z > a && wu_isspace(out[z - 1])) z--;
    if (a) for (uint32_t i = a; i < z; i++) out[i - a] = out[i];
    out[z - a] = 0; return 0;
}
static void init_style(struct wstyle *st, const struct wstyle *par)
{
    memset(st, 0, sizeof(*st));
    st->display = D_INLINE; st->opacity = 255; st->flex_shrink = 100; st->width.kind = LK_AUTO; st->height.kind = LK_AUTO;
    st->min_w.kind = LK_VAL; st->min_h.kind = LK_VAL; st->max_w.kind = LK_NONE; st->max_h.kind = LK_NONE; st->flex_basis.kind = LK_AUTO;
    for (int i = 0; i < 4; i++) { st->margin[i].kind = LK_VAL; st->padding[i].kind = LK_VAL; st->border_color[i] = 1; }
    st->top.kind = st->right.kind = st->bottom.kind = st->left.kind = LK_AUTO;
    st->gap_r.kind = st->gap_c.kind = LK_VAL; st->text_indent.kind = LK_VAL; st->bg_w.kind = st->bg_h.kind = LK_AUTO;
    st->z_index = 0; st->spacing_h = st->spacing_v = 2; st->align_self = AI_AUTO;
    st->color = par->color; st->font_px = par->font_px; st->fs_zero = par->fs_zero; st->bold = par->bold; st->italic = par->italic; st->mono = par->mono;
    st->text_align = par->text_align; st->text_decor = par->text_decor; st->text_transform = par->text_transform; st->white_space = par->white_space;
    st->list_style = par->list_style; st->list_inside = par->list_inside; st->visibility = par->visibility; st->letter_sp = par->letter_sp;
    st->word_break = par->word_break; st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; st->border_collapse = par->border_collapse;
    st->spacing_h = par->spacing_h; st->spacing_v = par->spacing_v; st->pointer = par->pointer; st->no_ptr = par->no_ptr; st->vars = par->vars; st->svg_fill = par->svg_fill; st->svg_stroke = par->svg_stroke; st->svg_sw = par->svg_sw;
    st->text_indent = par->text_indent; st->vertical_align = 0;
    st->obj_pos_x = st->obj_pos_y = 50;   /* SOMA_181 */
}

/* SOMA_PARCHE_028_UI: busqueda acotada a [h, h+hl). Los valores de las declaraciones apuntan DENTRO del texto de la hoja
 * (no terminan en nulo): buscar con wu_find recorria la hoja entera por cada declaracion y cada elemento (coste
 * cuadratico: en un periodico con 300 KB de CSS era la mayor parte del tiempo de carga). */
static const char *findn(const char *h, uint32_t hl, const char *n)
{
    uint32_t nl = wu_strlen(n); if (!nl || hl < nl) return 0;
    for (uint32_t i = 0; i + nl <= hl; i++) if (h[i] == n[0] && !memcmp(h + i, n, nl)) return h + i;
    return 0;
}
static void apply_decl(struct wcss *c, struct wstyle *st, struct actx *a, const struct wdecl *d)
{
    const char *v = d->val; uint32_t vl = d->vlen;
    if (d->prop == PR_CUSTOM) {
        uint32_t s = 0; while (s < vl && wu_isspace(v[s])) s++;
        /* SOMA_152_WEB_FLUIDO: antes cada recalculo de estilos creaba de nuevo TODAS las variables (--bs-*) de cada
         * elemento en el arena del documento, que nunca se libera hasta cambiar de pagina: con Bootstrap eran cientos de
         * KB por recalculo; con un reloj en JS o al pasar el raton (:hover) el arena (16 MB) se agotaba en ~1 minuto y la
         * pagina se quedaba sin estilos ni DOM nuevo. Ahora un nodo identico (nombre, valor y lista de la que cuelga) se
         * reutiliza: las listas son persistentes, compartirlo es seguro y el arena deja de crecer. */
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < d->nlen; i++) { h ^= (uint8_t)d->name[i]; h *= 16777619u; }
        for (uint32_t i = s; i < vl; i++) { h ^= (uint8_t)v[i]; h *= 16777619u; }
        h ^= (uint32_t)((uintptr_t)st->vars >> 3); h *= 16777619u; h ^= h >> 15;
        if (!c->vtab) { c->vtab = (struct wvar **)kmalloc(sizeof(struct wvar *) * 16384u); if (c->vtab) memset(c->vtab, 0, sizeof(struct wvar *) * 16384u); }
        struct wvar **slot = c->vtab ? &c->vtab[h & 16383u] : 0;
        if (slot && *slot) {
            struct wvar *e = *slot;
            if (e->next == st->vars && wu_strneq(e->name, d->name, d->nlen) && e->name[d->nlen] == 0 &&
                wu_strneq(e->val, v + s, vl - s) && e->val[vl - s] == 0) { st->vars = e; return; }
        }
        struct wvar *nv = (struct wvar *)wa_alloc(&c->d->ar, sizeof(*nv)); if (!nv) return;
        nv->name = wa_strndup(&c->d->ar, d->name, d->nlen);
        nv->val = wa_strndup(&c->d->ar, v + s, vl - s);
        if (!nv->name || !nv->val) return;
        nv->next = st->vars; st->vars = nv;
        if (slot) *slot = nv;
        return;
    }
    if (vl > 4 && findn(v, vl, "var(")) {
        int r = expand_vars(c, st->vars, v, vl, c->scratch, 2048, 0);
        if (!r) return;
        v = c->scratch; vl = (uint32_t)r - 1;
    }
    apply_prop(a, st, d->prop, v, vl, 0, 0);
}

static void cascade_pass(struct wcss *c, struct wstyle *st, struct actx *a, struct wrule **m, int nm, const struct wdecl *inl, int ninl, int important)
{
    /* barrido 0: propiedades personalizadas, 1: fuente, 2: el resto */
    for (int sweep = 0; sweep < 3; sweep++) {
        for (int i = 0; i < nm + 1; i++) {
            const struct wdecl *dl; int nd;
            if (i < nm) { dl = m[i]->decls; nd = m[i]->nd; if (m[i]->origin == 0 && important) continue; } else { dl = inl; nd = ninl; }
            for (int k = 0; k < nd; k++) {
                const struct wdecl *d = &dl[k];
                if ((d->imp != 0) != (important != 0)) continue;
                int is_custom = d->prop == PR_CUSTOM;
                int is_font = d->prop == PR_FONT_SIZE || d->prop == PR_FONT;
                if ((sweep == 0) != is_custom) { if (sweep == 0 || is_custom) continue; }
                if (sweep == 1 && !is_font) continue;
                if (sweep == 2 && (is_font || is_custom)) continue;
                if (sweep == 0 && !is_custom) continue;
                a->lc.font_px = sweep == 1 ? a->par->font_px : st->font_px;
                if (sweep == 1) { apply_decl(c, st, a, d); }
                else { int save = a->lc.font_px; a->lc.font_px = st->font_px; apply_decl(c, st, a, d); a->lc.font_px = save; }
            }
        }
    }
}


/* atributos de presentacion HTML heredados (bgcolor, width, align, border, cellpadding, font...) */
static int attr_len(const char *v, struct wlen *o)
{
    if (!v) return 0;
    while (wu_isspace(*v)) v++;
    if (!wu_isdigit(*v)) return 0;
    int n = 0; while (wu_isdigit(*v)) { if (n < 100000) n = n * 10 + (*v - '0'); v++; }
    memset(o, 0, sizeof(*o)); o->kind = LK_VAL;
    if (*v == '%') o->pct = n * 100; else o->px = n;
    return 1;
}
static uint32_t attr_color(const char *v)
{
    int ok; uint32_t c = wc_parse_color(v, &ok);
    if (ok) return c;
    char b[16]; uint32_t l = wu_strlen(v); if (l > 6 || l == 0) return 0;
    b[0] = '#'; memcpy(b + 1, v, l); b[l + 1] = 0;
    c = wc_parse_color(b, &ok); return ok ? c : 0;
}
static void apply_presentational(struct wnode *n, struct wstyle *st)
{
    if (!n->attrs) return;
    const char *v; struct wlen l; int t = n->tag;
    if (wd_attr(n, "hidden")) { st->display = D_NONE; return; }   /* SOMA_187A: tambien en <input hidden> (Google oculta asi el input de archivos del menu "+") */
    if ((v = wd_attr(n, "bgcolor")) && !st->bg_color) { uint32_t c = attr_color(v); if (c) st->bg_color = c; }
    if (t == WT_BODY) { if ((v = wd_attr(n, "text"))) { uint32_t c = attr_color(v); if (c) st->color = c; } }
    if (t == WT_TABLE || t == WT_TD || t == WT_TH || t == WT_IMG || t == WT_IFRAME || t == WT_CANVAS || t == WT_VIDEO || t == WT_SVG || t == WT_EMBED || t == WT_OBJECT || t == WT_HR || t == WT_COL || t == WT_COLGROUP || t == WT_INPUT) {
        /* SOMA_PARCHE_028_UI: en imagenes y videos, si el autor fija solo una dimension por CSS (width:100%; height:auto),
         * el atributo de la otra no se aplica: queda como proporcion (lo usa la maquetacion), igual que en los navegadores actuales */
        int media = (t == WT_IMG || t == WT_VIDEO || t == WT_CANVAS || t == WT_SVG);
        int css_w = st->width.kind != LK_AUTO, css_h = st->height.kind != LK_AUTO;
        if (st->width.kind == LK_AUTO && !(media && css_h) && (v = wd_attr(n, "width")) && attr_len(v, &l)) st->width = l;
        if (st->height.kind == LK_AUTO && !(media && css_w) && (v = wd_attr(n, "height")) && attr_len(v, &l)) st->height = l;
    }
    if ((v = wd_attr(n, "align"))) {
        int a = wu_strieq(v, "center") ? 1 : (wu_strieq(v, "right") ? 2 : (wu_strieq(v, "left") ? 3 : 0));
        if (t == WT_TABLE) { if (a == 1) { st->margin[1].kind = st->margin[3].kind = LK_AUTO; } else if (a == 2) st->floating = FL_RIGHT; else if (a == 3) st->floating = FL_LEFT; }
        else if (t == WT_IMG) { if (a == 2) st->floating = FL_RIGHT; else if (a == 3) st->floating = FL_LEFT; }
        else if (t == WT_DIV || t == WT_P || (t >= WT_H1 && t <= WT_H6) || t == WT_TD || t == WT_TH || t == WT_TR || t == WT_TBODY || t == WT_CAPTION || t == WT_CENTER || t == WT_THEAD || t == WT_TFOOT || t == WT_LEGEND) {
            if (a == 1) st->text_align = TA_CENTER; else if (a == 2) st->text_align = TA_RIGHT; else if (a == 3) st->text_align = TA_LEFT; else if (wu_strieq(v, "justify")) st->text_align = TA_JUSTIFY;
        }
    }
    if ((t == WT_TD || t == WT_TH || t == WT_TR || t == WT_TBODY) && (v = wd_attr(n, "valign"))) {
        st->vertical_align = wu_strieq(v, "top") ? VA_TOP : (wu_strieq(v, "bottom") ? VA_BOTTOM : (wu_strieq(v, "middle") ? VA_MIDDLE : st->vertical_align));
    }
    if (t == WT_TD || t == WT_TH) {
        if (wd_attr(n, "nowrap")) st->white_space = WS_NOWRAP;
        struct wnode *tb = n->parent; int guard = 0;
        while (tb && tb->tag != WT_TABLE && guard++ < 4) tb = tb->parent;
        if (tb && tb->tag == WT_TABLE) {
            int b = 0; if ((v = wd_attr(tb, "border"))) b = *v ? wu_atoi(v) : 1;
            if (b > 0) for (int i = 0; i < 4; i++) if (!st->border_style[i] && !st->border_w[i]) { st->border_style[i] = 1; st->border_w[i] = 1; st->border_color[i] = 0xFF808080u; }
            if ((v = wd_attr(tb, "cellpadding")) && attr_len(v, &l)) {
                int all0 = 1; for (int i = 0; i < 4; i++) if (st->padding[i].px || st->padding[i].pct) all0 = 0;
                if (all0) for (int i = 0; i < 4; i++) st->padding[i] = l;
            }
        }
    }
    if (t == WT_TABLE) {
        if ((v = wd_attr(n, "border")) ) { int b = *v ? wu_atoi(v) : 1; if (b > 0) for (int i = 0; i < 4; i++) if (!st->border_style[i] && !st->border_w[i]) { st->border_style[i] = 1; st->border_w[i] = (int16_t)b; st->border_color[i] = 0xFF808080u; } }
        if ((v = wd_attr(n, "cellspacing")) && attr_len(v, &l)) { st->spacing_h = st->spacing_v = (int16_t)l.px; }
    }
    if (t == WT_FONT) {
        if ((v = wd_attr(n, "color"))) { uint32_t c = attr_color(v); if (c) st->color = c; }
        if ((v = wd_attr(n, "size"))) {
            static const int fs[8] = { 13, 10, 13, 16, 18, 24, 32, 48 };
            int sz = wu_atoi(v); int rel = (v[0] == '+' || v[0] == '-');
            if (rel) sz = 3 + sz;
            if (sz < 1) sz = 1;
            if (sz > 7) sz = 7;
            st->font_px = (int16_t)fs[sz];
        }
        if ((v = wd_attr(n, "face"))) st->mono = (uint8_t)family_is_mono(v, wu_strlen(v));
    }
    if (t == WT_UL || t == WT_OL || t == WT_LI) {
        if ((v = wd_attr(n, "type"))) {
            if (v[0] == '1') st->list_style = LS_DECIMAL; else if (v[0] == 'a') st->list_style = LS_ALPHA; else if (v[0] == 'A') st->list_style = LS_UALPHA;
            else if (v[0] == 'i' || v[0] == 'I') st->list_style = LS_ROMAN; else if (wu_strieq(v, "disc")) st->list_style = LS_DISC; else if (wu_strieq(v, "circle")) st->list_style = LS_CIRCLE; else if (wu_strieq(v, "square")) st->list_style = LS_SQUARE;
        }
    }
    if (t == WT_A && wd_attr(n, "href") && n->parent && n->parent->tag == WT_BODY) { /* link=... del body */ const char *lc = wd_attr(n->parent, "link"); if (lc) { uint32_t c = attr_color(lc); if (c && st->color == 0xFF0000EEu) st->color = c; } }
}

static int wu_isxdigit(int ch) { return (ch >= '0' && ch <= '9') || ((ch | 32) >= 'a' && (ch | 32) <= 'f'); }
/* ---------------- SOMA_188: contadores CSS y contenido generado (::before / ::after) ---------------- */
static int *cnt_slot(struct wcss *c, const char *nm, uint32_t nl, int create)
{
    for (int i = 0; i < c->ncnt; i++) if (wu_strlen(c->cnt[i].name) == nl && wu_strneq(c->cnt[i].name, nm, nl)) return &c->cnt[i].val;
    if (!create || c->ncnt >= 24 || nl >= 24) return 0;
    memcpy(c->cnt[c->ncnt].name, nm, nl); c->cnt[c->ncnt].name[nl] = 0; c->cnt[c->ncnt].val = 0; return &c->cnt[c->ncnt++].val;
}
/* "a 2 b" -> op(a, 2), op(b, def). op 0 reset, 1 increment, 2 set */
static void cnt_apply(struct wcss *c, const char *s, int op)
{
    if (!s[0] || wu_strnieq(s, "none", 4)) return;
    const char *p = s;
    while (*p) {
        while (*p && wu_isspace(*p)) p++;
        const char *a = p; while (*p && !wu_isspace(*p)) p++;
        uint32_t nl = (uint32_t)(p - a); if (!nl) break;
        if ((a[0] >= '0' && a[0] <= '9') || a[0] == '-') continue;
        while (*p && wu_isspace(*p)) p++;
        int val = op == 1 ? 1 : 0;
        if ((*p >= '0' && *p <= '9') || (*p == '-' && p[1] >= '0' && p[1] <= '9')) { val = wu_atoi(p); if (*p == '-') p++; while (*p >= '0' && *p <= '9') p++; }
        int *v = cnt_slot(c, a, nl, 1); if (!v) continue;
        if (op == 1) *v += val; else *v = val;
    }
}
static void put_cp(char *o, int *k, int max, uint32_t cp)
{
    if (cp >= 0xE000 && cp <= 0xF8FF) return;            /* zona privada: iconos de fuentes web (no hay @font-face): nada */
    if (cp == 0) return;
    if (cp < 0x80) { if (*k < max) o[(*k)++] = (char)cp; }
    else if (cp < 0x800) { if (*k + 1 < max) { o[(*k)++] = (char)(0xC0 | (cp >> 6)); o[(*k)++] = (char)(0x80 | (cp & 63)); } }
    else if (cp < 0x10000) { if (*k + 2 < max) { o[(*k)++] = (char)(0xE0 | (cp >> 12)); o[(*k)++] = (char)(0x80 | ((cp >> 6) & 63)); o[(*k)++] = (char)(0x80 | (cp & 63)); } }
    else if (cp < 0x110000 && *k + 3 < max) { o[(*k)++] = (char)(0xF0 | (cp >> 18)); o[(*k)++] = (char)(0x80 | ((cp >> 12) & 63)); o[(*k)++] = (char)(0x80 | ((cp >> 6) & 63)); o[(*k)++] = (char)(0x80 | (cp & 63)); }
}
static void put_str(char *o, int *k, int max, const char *s) { while (*s && *k < max) o[(*k)++] = *s++; }
static void fmt_counter(char *o, int *k, int max, int v, const char *style, uint32_t sl)
{
    char b[40]; int n = 0;
    if (sl && (is_kw(style, sl, "lower-alpha") || is_kw(style, sl, "lower-latin") || is_kw(style, sl, "upper-alpha") || is_kw(style, sl, "upper-latin")) && v > 0) {
        int up = style[0] == 'u' || style[0] == 'U'; char t[16]; int m = 0; int x = v;
        while (x > 0 && m < 15) { x--; t[m++] = (char)((up ? 'A' : 'a') + x % 26); x /= 26; }
        while (m) b[n++] = t[--m];
    } else if (sl && (is_kw(style, sl, "lower-roman") || is_kw(style, sl, "upper-roman")) && v > 0 && v < 4000) {
        static const int V[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 }; static const char *const R[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
        int up = style[0] == 'u' || style[0] == 'U'; int x = v;
        for (int i = 0; i < 13; i++) while (x >= V[i]) { for (const char *r = R[i]; *r; r++) b[n++] = up ? (char)(*r - 32) : *r; x -= V[i]; }
    } else if (sl && is_kw(style, sl, "disc")) { put_cp(o, k, max, 0x2022); return; }
    else if (sl && is_kw(style, sl, "circle")) { put_cp(o, k, max, 0x25E6); return; }
    else if (sl && is_kw(style, sl, "square")) { put_cp(o, k, max, 0x25AA); return; }
    else if (sl && is_kw(style, sl, "none")) return;
    else { if (sl && is_kw(style, sl, "decimal-leading-zero") && v >= 0 && v < 10) b[n++] = '0'; n += wu_itoa(v, b + n); }
    b[n] = 0; put_str(o, k, max, b);
}
/* evalua content: cadenas (con escapes CSS), attr(), comillas, counter()/counters(). Devuelve -1 si es none/normal */
static int eval_content(struct wcss *c, struct wnode *n, const char *v, int vl, char *o, int max)
{
    int k = 0, i = 0, any = 0;
    while (i < vl) {
        while (i < vl && wu_isspace(v[i])) i++;
        if (i >= vl) break;
        char ch = v[i];
        if (ch == '/') break;                             /* texto alternativo: content: "x" / "alt" */
        if (ch == '"' || ch == '\'') {
            i++; any = 1;
            while (i < vl && v[i] != ch) {
                if (v[i] == '\\' && i + 1 < vl) {
                    i++; uint32_t cp = 0; int h = 0;
                    while (i < vl && h < 6 && wu_isxdigit(v[i])) { char d = v[i]; cp = cp * 16 + (uint32_t)(d <= '9' ? d - '0' : (d | 32) - 'a' + 10); i++; h++; }
                    if (h) { if (i < vl && v[i] == ' ') i++; put_cp(o, &k, max, cp); }
                    else if (v[i] == '\n') i++;
                    else { if (k < max) o[k++] = v[i]; i++; }
                } else { if (k < max) o[k++] = v[i]; i++; }
            }
            i++; continue;
        }
        int s0 = i; while (i < vl && (wu_isalnum(v[i]) || v[i] == '-' || v[i] == '_')) i++;
        uint32_t wl = (uint32_t)(i - s0); const char *w = v + s0;
        if (!wl) { i++; continue; }
        if (i < vl && v[i] == '(') {
            int d = 0, a0 = i + 1; while (i < vl) { if (v[i] == '(') d++; else if (v[i] == ')') { if (--d == 0) break; } i++; }
            int a1 = i; i++;
            while (a0 < a1 && wu_isspace(v[a0])) a0++;
            int e1 = a1; while (e1 > a0 && wu_isspace(v[e1 - 1])) e1--;
            any = 1;
            if (is_kw(w, wl, "attr")) { char an[64]; int an_l = e1 - a0; if (an_l > 63) an_l = 63; memcpy(an, v + a0, (size_t)an_l); an[an_l] = 0; for (int q = 0; q < an_l; q++) if (an[q] == ' ' || an[q] == ',') { an[q] = 0; break; } const char *av = wd_attr(n, an); if (av) put_str(o, &k, max, av); }
            else if (is_kw(w, wl, "counter") || is_kw(w, wl, "counters")) {
                int cm = a0; while (cm < e1 && v[cm] != ',') cm++;
                int ne = cm; while (ne > a0 && wu_isspace(v[ne - 1])) ne--;
                const char *sty = 0; uint32_t syl = 0;
                if (cm < e1) { int ss = cm + 1; if (is_kw(w, wl, "counters")) { /* separador entre comillas: se salta */ while (ss < e1 && v[ss] != ',') ss++; ss++; } while (ss < e1 && wu_isspace(v[ss])) ss++; if (ss < e1) { sty = v + ss; syl = (uint32_t)(e1 - ss); } }
                int *cv = cnt_slot(c, v + a0, (uint32_t)(ne - a0), 0);
                fmt_counter(o, &k, max, cv ? *cv : 0, sty, syl);
            }
            /* url(), image-set(), etc.: imagen generada no soportada: nada */
            continue;
        }
        if (is_kw(w, wl, "none") || is_kw(w, wl, "normal")) return -1;
        if (is_kw(w, wl, "open-quote")) { put_cp(o, &k, max, 0x201C); any = 1; }
        else if (is_kw(w, wl, "close-quote")) { put_cp(o, &k, max, 0x201D); any = 1; }
        else if (is_kw(w, wl, "no-open-quote") || is_kw(w, wl, "no-close-quote")) any = 1;
    }
    if (!any) return -1;
    o[k < max ? k : max - 1] = 0; return k;
}
static void sort_rules(struct wrule **m, int nm)
{
    for (int i = 1; i < nm; i++) {
        struct wrule *x = m[i]; int j = i - 1;
        uint64_t kx = ((uint64_t)x->origin << 56) | ((uint64_t)x->sel->spec << 24) | (x->order & 0xFFFFFF);
        while (j >= 0) { struct wrule *y = m[j]; uint64_t ky = ((uint64_t)y->origin << 56) | ((uint64_t)y->sel->spec << 24) | (y->order & 0xFFFFFF); if (ky <= kx) break; m[j + 1] = y; j--; }
        m[j + 1] = x;
    }
}
static void counters_after_cascade(struct wcss *c)
{
    if (c->creset[0]) cnt_apply(c, c->creset, 0);
    if (c->cset[0]) cnt_apply(c, c->cset, 2);
    if (c->cinc[0]) cnt_apply(c, c->cinc, 1);
    c->creset[0] = c->cinc[0] = c->cset[0] = 0;
}
static void compute_pseudo(struct wcss *c, struct wnode *n, struct wstyle *st, struct wrule **m, int nm, int k)
{
    struct wnode *ps = st->pse[k];
    if (!ps) {
        ps = (struct wnode *)wa_zalloc(&c->d->ar, sizeof *ps); if (!ps) return;
        ps->type = WN_ELEM; ps->tag = WT_SPAN; ps->name = k ? "::after" : "::before"; ps->tlen = 7; ps->flags = WNF_PSEUDO | WNF_INDOC;
        st->pse[k] = ps;
    }
    ps->parent = n; ps->next = ps->prev = 0;
    struct wstyle *pst = ps->st;
    if (!pst) { pst = (struct wstyle *)wa_alloc(&c->d->ar, sizeof *pst); if (!pst) return; ps->st = pst; }
    init_style(pst, st);
    sort_rules(m, nm);
    c->pc_len = -1; c->creset[0] = c->cinc[0] = c->cset[0] = 0;
    struct actx a; a.par = st; a.c = c; a.lc.root_px = 16; a.lc.vw = c->vw; a.lc.vh = c->vh; a.lc.font_px = st->font_px;
    cascade_pass(c, pst, &a, m, nm, 0, 0, 0);
    cascade_pass(c, pst, &a, m, nm, 0, 0, 1);
    for (int i = 0; i < 4; i++) if (pst->border_color[i] == 1) pst->border_color[i] = pst->color;
    if ((pst->floating || pst->position == P_ABSOLUTE || pst->position == P_FIXED) && pst->display != D_NONE) {
        if (pst->display == D_INLINE || pst->display == D_INLINE_BLOCK) pst->display = D_BLOCK;
        else if (pst->display == D_INLINE_FLEX) pst->display = D_FLEX;
    }
    pst->pse[0] = pst->pse[1] = 0;
    counters_after_cascade(c);
    char txt[512]; int tl = c->pc_len >= 0 ? eval_content(c, n, c->pc, c->pc_len, txt, (int)sizeof txt - 1) : -1;
    if (tl < 0) { pst->display = D_NONE; return; }
    struct wnode *t = ps->first;
    if (t && t->tlen == (uint32_t)tl && wu_strneq(t->name, txt, (uint32_t)tl)) return;
    if (!tl) { ps->first = ps->last = 0; return; }
    if (!t) { t = (struct wnode *)wa_zalloc(&c->d->ar, sizeof *t); if (!t) return; t->type = WN_TEXT; t->flags = WNF_PSEUDO | WNF_INDOC; t->parent = ps; ps->first = ps->last = t; }
    t->name = wa_strndup(&c->d->ar, txt, (uint32_t)tl); t->tlen = t->name ? (uint32_t)tl : 0;
}

static void compute_node(struct wcss *c, struct wnode *n, const struct wstyle *par, struct wdecl *tmpd)
{
    wu_tick();
    struct wstyle *st = n->st; int fresh = 0;
    if (!st) { st = (struct wstyle *)wa_alloc(&c->d->ar, sizeof(*st)); if (!st) return; n->st = st; fresh = 1; }
    /* SOMA_188: un estilo recien reservado no esta a cero (en el kernel kmalloc no limpia): sus pse[] son basura */
    struct wnode *keep_pse[2] = { fresh ? 0 : st->pse[0], fresh ? 0 : st->pse[1] };
    init_style(st, par);
    st->pse[0] = keep_pse[0]; st->pse[1] = keep_pse[1];
    struct wrule *pm[3][24]; int npm[3] = { 0, 0, 0 };
    /* reglas coincidentes */
    int nm = 0; struct wrule **m = c->match;
    struct wrule *lists[8]; int nl = 0;
    lists[nl++] = c->univ;
    if (n->tag) lists[nl++] = c->tagt[n->tag];
    const char *id = wd_attr(n, "id");
    if (id && *id) lists[nl++] = c->idt[wu_hash(id, wu_strlen(id)) & 127];
    const char *cl = wd_attr(n, "class");
    struct wrule *clists[16]; int ncl = 0;
    if (cl) { const char *p = cl; while (*p && ncl < 16) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if (p > s) clists[ncl++] = c->clst[wu_hash(s, (uint32_t)(p - s)) & 127]; } }
    for (int li = 0; li < nl + ncl; li++) {
        struct wrule *r = li < nl ? lists[li] : clists[li - nl];
        for (; r; r = r->bnext) {
            if (c->vw < r->minw || c->vw > r->maxw) continue;
            if (!match_from(r->sel, 0, n)) continue;
            if (r->sel->pe) { if (r->sel->pe & 4) st->clearfix = 1; int pk = (r->sel->pe & 3) - 1; if (pk >= 0 && pk < 3 && npm[pk] < 24) pm[pk][npm[pk]++] = r; continue; }   /* SOMA_188 */
            if (nm < c->match_cap) m[nm++] = r;
        }
    }
    /* orden: origen, especificidad, aparicion (insercion; listas casi ordenadas) */
    for (int i = 1; i < nm; i++) {
        struct wrule *x = m[i]; int j = i - 1;
        uint64_t kx = ((uint64_t)x->origin << 56) | ((uint64_t)x->sel->spec << 24) | (x->order & 0xFFFFFF);
        while (j >= 0) { struct wrule *y = m[j]; uint64_t ky = ((uint64_t)y->origin << 56) | ((uint64_t)y->sel->spec << 24) | (y->order & 0xFFFFFF); if (ky <= kx) break; m[j + 1] = y; j--; }
        m[j + 1] = x;
    }
    int ninl = 0; const char *sty = wd_attr(n, "style");
    if (sty && *sty) { uint32_t sl = wu_strlen(sty); ninl = parse_decls(c, sty, sty + sl, tmpd, 96); }
    struct actx a; a.par = par; a.c = c; a.lc.root_px = 16; a.lc.vw = c->vw; a.lc.vh = c->vh; a.lc.font_px = par->font_px;
    c->creset[0] = c->cinc[0] = c->cset[0] = 0;
    cascade_pass(c, st, &a, m, nm, tmpd, ninl, 0);
    cascade_pass(c, st, &a, m, nm, tmpd, ninl, 1);
    for (int i = 0; i < 4; i++) if (st->border_color[i] == 1) st->border_color[i] = st->color;
    apply_presentational(n, st);
    counters_after_cascade(c);   /* SOMA_188 */
    if (n->tag == WT_OL || n->tag == WT_UL) { int *lv = cnt_slot(c, "list-item", 9, 1); if (lv) *lv = 0; }
    if (st->display == D_LIST_ITEM) { int *lv = cnt_slot(c, "list-item", 9, 1); if (lv) (*lv)++; }
    st->ph_color = 0; st->ph_italic = st->ph_bold = -1;
    if (npm[2] && (n->tag == WT_INPUT || n->tag == WT_TEXTAREA)) {   /* SOMA_188: ::placeholder -> color/estilo del texto indicativo */
        if (!c->phst) c->phst = (struct wstyle *)wa_alloc(&c->d->ar, sizeof *c->phst);
        struct wstyle *ph = c->phst;
        if (ph) {
            init_style(ph, st); ph->color = 0x01000000u; ph->italic = 2; ph->bold = 2;
            sort_rules(pm[2], npm[2]); c->pc_len = -1;
            struct actx pa; pa.par = st; pa.c = c; pa.lc.root_px = 16; pa.lc.vw = c->vw; pa.lc.vh = c->vh; pa.lc.font_px = st->font_px;
            cascade_pass(c, ph, &pa, pm[2], npm[2], 0, 0, 0); cascade_pass(c, ph, &pa, pm[2], npm[2], 0, 0, 1);
            if (ph->color != 0x01000000u) st->ph_color = ph->color ? ph->color : 0x00000001u;
            if (ph->italic != 2) st->ph_italic = (int8_t)(ph->italic != 0);
            if (ph->bold != 2) st->ph_bold = (int8_t)(ph->bold != 0);
        }
    }
    for (int pk = 0; pk < 2; pk++) {
        if (npm[pk] && st->display != D_NONE && !(n->flags & WNF_VOID) && n->tag != WT_IMG && n->tag != WT_INPUT && n->tag != WT_TEXTAREA && n->tag != WT_SELECT && n->tag != WT_IFRAME && n->tag != WT_VIDEO && n->tag != WT_CANVAS && n->tag != WT_SVG)
            compute_pseudo(c, n, st, pm[pk], npm[pk], pk);
        else if (st->pse[pk] && st->pse[pk]->st) st->pse[pk]->st->display = D_NONE;
    }
    /* flotantes y posicion absoluta bloquean el display */
    if ((st->floating || st->position == P_ABSOLUTE || st->position == P_FIXED) && st->display != D_NONE) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    n->flags &= (uint16_t)~WNF_DIRTY;
}

/* SOMA_188: <slot> del arbol sombra de h al que se asigna el hijo n (o NULL) */
static const struct wnode *slot_for(const struct wnode *h, const struct wnode *n)
{
    const char *want = n->type == WN_ELEM ? wd_attr(n, "slot") : 0; if (want && !*want) want = 0;
    const struct wnode *sr = h->shadow;
    for (const struct wnode *q = sr->first; q; ) {
        if (q->type == WN_ELEM && q->name && wu_streq(q->name, "slot")) { const char *nm = wd_attr(q, "name"); if (nm && !*nm) nm = 0; if ((!want && !nm) || (want && nm && wu_streq(want, nm))) return q; }
        if (q->first) { q = q->first; continue; }
        while (q && q != sr && !q->next) q = q->parent;
        if (!q || q == sr) break;
        q = q->next;
    }
    return 0;
}
void wc_compute(struct wcss *c, int vw, int vh)
{
    if (!c) return;
    c->vw = vw; c->vh = vh;
    vcache_clear152(); cbloom_new_gen152();
    build_index(c);
    c->ncnt = 0;   /* SOMA_188 */
    struct wstyle *root = (struct wstyle *)kmalloc(sizeof(struct wstyle));
    struct wdecl *tmpd = (struct wdecl *)kmalloc(sizeof(struct wdecl) * 96);
    if (!root || !tmpd) { if (root) kfree(root); if (tmpd) kfree(tmpd); return; }
    memset(root, 0, sizeof(*root)); root->font_px = 16; root->color = 0xFF000000u; root->spacing_h = root->spacing_v = g_wl181_off ? 2 : 0;   /* SOMA_181: border-spacing inicial 0 (la hoja base lo pone a 2px en <table>) */
    struct wnode *docn = c->d->root;
    for (struct wnode *n = docn->first; n; ) {
        if (n->type == WN_ELEM) {
            const struct wnode *pn = n->parent; if (wd_is_shadow_root(pn)) pn = pn->parent;   /* SOMA_188: el arbol sombra hereda del anfitrion */
            else if (pn && pn->shadow) { const struct wnode *sl = slot_for(pn, n); if (sl && sl->st) pn = sl; }   /* un hijo asignado a un <slot> hereda del slot */
            const struct wstyle *par = (pn && pn->st) ? pn->st : root;
            compute_node(c, n, par, tmpd);
        }
        if (n->type == WN_ELEM && (n->first || (n->shadow && n->shadow->first))) { n = wd_walk_next(n, docn); continue; }
        n = wd_walk_next(n, docn);
    }
    kfree(root); kfree(tmpd);
}

/* ============================ consultas ============================ */
/* SOMA_152_WEB_FLUIDO: cache real (antes cada querySelector/matches/closest compilaba el selector de nuevo en el arena del
 * documento: memoria que solo se libera al salir de la pagina; con scripts que consultan el DOM en bucle se agotaba). */
static struct wsel *cached_sel(struct wcss *c, const char *sel, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) { h ^= (uint8_t)sel[i]; h *= 16777619u; }
    h = (h ^ (h >> 13)) & 511u;
    if (!c->stab) {
        c->stab = (struct wsel **)kmalloc(sizeof(struct wsel *) * 512u);
        c->stab_key = (const char **)kmalloc(sizeof(const char *) * 512u);
        if (!c->stab || !c->stab_key) { if (c->stab) kfree(c->stab); if (c->stab_key) kfree(c->stab_key); c->stab = 0; c->stab_key = 0; }
        else { memset(c->stab, 0, sizeof(struct wsel *) * 512u); memset(c->stab_key, 0, sizeof(const char *) * 512u); }
    }
    if (c->stab && c->stab[h] && c->stab_key[h] && wu_strneq(c->stab_key[h], sel, n) && c->stab_key[h][n] == 0) return c->stab[h];
    struct wsel *r = compile_selector(c, sel, n);
    if (r && c->stab) { const char *k = wa_strndup(&c->d->ar, sel, n); if (k) { c->stab[h] = r; c->stab_key[h] = k; } }
    return r;
}

static int query_one(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only, int *count)
{
    const char *p = sel, *e = sel + wu_strlen(sel);
    struct wsel *list[8]; int nl = 0;
    while (p < e && nl < 8) {
        const char *r = p; int d = 0; while (r < e && !(*r == ',' && !d)) { if (*r == '(' || *r == '[') d++; else if (*r == ')' || *r == ']') d--; r++; }
        struct wsel *s = cached_sel(c, p, (uint32_t)(r - p));
        if (!s) return 0;
        list[nl++] = s; p = r + 1;
    }
    for (struct wnode *n = root->first; n; ) {
        if (n->type == WN_ELEM) {
            for (int i = 0; i < nl; i++) if (match_from(list[i], 0, n)) { if (*count < max && out) out[*count] = n; (*count)++; if (first_only) return 1; break; }
        }
        if (n->type == WN_ELEM && n->first) { n = n->first; continue; }
        while (n && n != root && !n->next) n = n->parent;
        if (!n || n == root) break;
        n = n->next;
    }
    return 1;
}

int wc_query(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only)
{
    int count = 0;
    if (!c || !root || !sel) return 0;
    cbloom_new_gen152();
    size_t mark = 0; (void)mark;
    query_one(c, root, sel, out, max, first_only, &count);
    return count > max ? max : count;
}

int wc_matches(struct wcss *c, struct wnode *n, const char *sel)
{
    cbloom_new_gen152();
    const char *p = sel, *e = sel + wu_strlen(sel);
    while (p < e) {
        const char *r = p; int d = 0; while (r < e && !(*r == ',' && !d)) { if (*r == '(' || *r == '[') d++; else if (*r == ')' || *r == ']') d--; r++; }
        struct wsel *s = cached_sel(c, p, (uint32_t)(r - p));
        if (s && match_from(s, 0, n)) return 1;
        p = r + 1;
    }
    return 0;
}

/* ============================ estilo en linea ============================ */
void wc_set_inline(struct wdoc *d, struct wnode *n, const char *prop, const char *val)
{
    struct wb out = {0, 0, 0, 0}; const char *cur = wd_attr(n, "style"); int done = 0;
    uint32_t pl = wu_strlen(prop);
    if (cur) {
        const char *p = cur;
        while (*p) {
            while (*p == ';' || wu_isspace(*p)) p++;
            if (!*p) break;
            const char *ns = p; int dp = 0; while (*p && (*p != ';' || dp)) { if (*p == '(') dp++; else if (*p == ')' && dp) dp--; p++; }
            const char *ne = ns; while (ne < p && *ne != ':') ne++;
            const char *nt = ne; while (nt > ns && wu_isspace(nt[-1])) nt--;
            if ((uint32_t)(nt - ns) == pl && wu_strnieq(ns, prop, pl)) {
                if (val && *val && !done) { wb_add(&out, prop); wb_add(&out, ": "); wb_add(&out, val); wb_add(&out, "; "); }
                done = 1;
            } else { wb_addn(&out, ns, (uint32_t)(p - ns)); wb_add(&out, "; "); }
        }
    }
    if (!done && val && *val) { wb_add(&out, prop); wb_add(&out, ": "); wb_add(&out, val); wb_add(&out, ";"); }
    wd_set_attr(d, n, "style", out.p ? out.p : "");
    wb_free(&out);
}

const char *wc_get_inline(struct wnode *n, const char *prop, char *out, uint32_t max)
{
    const char *cur = wd_attr(n, "style"); uint32_t pl = wu_strlen(prop);
    out[0] = 0;
    if (!cur) return out;
    const char *p = cur;
    while (*p) {
        while (*p == ';' || wu_isspace(*p)) p++;
        if (!*p) break;
        const char *ns = p; while (*p && *p != ';') p++;
        const char *ne = ns; while (ne < p && *ne != ':') ne++;
        const char *nt = ne; while (nt > ns && wu_isspace(nt[-1])) nt--;
        if ((uint32_t)(nt - ns) == pl && wu_strnieq(ns, prop, pl) && ne < p) {
            const char *vs = ne + 1; while (vs < p && wu_isspace(*vs)) vs++;
            const char *ve = p; while (ve > vs && wu_isspace(ve[-1])) ve--;
            wu_copyn(out, vs, (uint32_t)(ve - vs), max); return out;
        }
    }
    return out;
}
