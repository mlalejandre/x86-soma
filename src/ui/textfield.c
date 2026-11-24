/* PARCHE 099: campo de texto reutilizable. Ver textfield.h. */
#include "textfield.h"
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"
#include "clipboard.h"

extern uint64_t timer_get_uptime_ms(void);

#define TF_PX  13
#define TF_FL  (WF_MONO | WF_RAW)
#define TF_LH  19
#define TF_MAXL 256

static int g_wf = -1, g_cw64 = 8 * 64, g_base = 13;
static void tf_font(void)
{
    if (g_wf >= 0) return;
    g_wf = wf_init() ? 1 : 0;
    if (g_wf) { g_cw64 = wf_width64("M", 1, TF_PX, TF_FL); if (g_cw64 < 4 * 64) g_cw64 = 8 * 64; g_base = (TF_LH + wf_ascent(TF_PX) - wf_descent(TF_PX)) / 2; }
}
static int colx(int c) { return (int)(((int64_t)c * g_cw64) >> 6); }
static void run(int x, int y, const char *s, int n, uint32_t col)
{
    if (n <= 0) return;
    if (g_wf) { wf_draw(x, y + g_base, s, (uint32_t)n, TF_PX, col, TF_FL, 255); return; }
    char t[256]; if (n > 255) n = 255; for (int i = 0; i < n; i++) t[i] = s[i]; t[n] = 0;
    font_draw_string((uint32_t)x, (uint32_t)(y + 5), t, col, 1);
}
static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int is_cont(char c) { return ((uint8_t)c & 0xC0) == 0x80; }
static int is_word(char c) { uint8_t u = (uint8_t)c; return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u >= 0x80; }
static uint32_t prev_cp(const struct tfield *t, uint32_t p) { if (p) { p--; while (p && is_cont(t->buf[p])) p--; } return p; }
static uint32_t next_cp(const struct tfield *t, uint32_t p) { uint32_t n = slen(t->buf); if (p < n) { p++; while (p < n && is_cont(t->buf[p])) p++; } return p; }
static int ncols(const char *s, uint32_t a, uint32_t b) { int c = 0; for (uint32_t i = a; i < b; i++) if (!is_cont(s[i])) c++; return c; }

void tf_init(struct tfield *t, char *buf, uint32_t cap, int multiline)
{
    uint8_t *p = (uint8_t *)t; for (uint32_t i = 0; i < sizeof *t; i++) p[i] = 0;
    t->buf = buf; t->cap = cap; t->multiline = multiline;
    t->cur = t->anc = slen(buf);
}
int tf_len(const struct tfield *t) { return (int)slen(t->buf); }
void tf_set(struct tfield *t, const char *s)
{
    uint32_t i = 0; while (s && s[i] && i + 1 < t->cap) { t->buf[i] = s[i]; i++; }
    t->buf[i] = 0; t->cur = t->anc = i; t->scroll = 0;
}
void tf_select_all(struct tfield *t) { t->anc = 0; t->cur = slen(t->buf); }
static void sel(const struct tfield *t, uint32_t *a, uint32_t *b) { if (t->cur < t->anc) { *a = t->cur; *b = t->anc; } else { *a = t->anc; *b = t->cur; } }

/* ---------------------------------------------------------------- ajuste de lineas (multilinea) */
static uint32_t g_ls[TF_MAXL]; static int g_nl;
static void layout(const struct tfield *t, int cols)
{
    const char *s = t->buf; uint32_t n = slen(s), p = 0, ls = 0; int c = 0, lastsp = -1;
    g_nl = 0; g_ls[g_nl++] = 0;
    if (!t->multiline) return;
    if (cols < 4) cols = 4;
    while (p < n && g_nl < TF_MAXL) {
        if (s[p] == '\n') { p++; g_ls[g_nl++] = p; ls = p; c = 0; lastsp = -1; continue; }
        if (s[p] == ' ') lastsp = (int)p;
        if (c >= cols) {
            uint32_t br = (lastsp > (int)ls) ? (uint32_t)lastsp + 1 : p;
            g_ls[g_nl++] = br; ls = br; c = ncols(s, br, p); lastsp = -1;
            continue;
        }
        p = next_cp(t, p); c++;
    }
}
static int line_of(uint32_t pos) { int l = 0; while (l + 1 < g_nl && g_ls[l + 1] <= pos) l++; return l; }
static uint32_t line_end(const struct tfield *t, int l) { uint32_t e = (l + 1 < g_nl) ? g_ls[l + 1] : slen(t->buf); if (e > g_ls[l] && (t->buf[e - 1] == '\n' || (l + 1 < g_nl && t->buf[e - 1] == ' '))) e--; return e; }
static uint32_t pos_at(const struct tfield *t, int l, int col)
{
    uint32_t p = g_ls[l], e = line_end(t, l); int c = 0;
    while (p < e && c < col) { p = next_cp(t, p); c++; }
    return p;
}
static int cols_of(const struct tfield *t) { return (int)(((int64_t)(t->w - 16) * 64) / g_cw64); }

/* ---------------------------------------------------------------- pintado */
void tf_render(struct tfield *t, int x, int y, int w, int h, int focused, const char *ph, uint32_t tc)
{
    tf_font();
    t->x = x; t->y = y; t->w = w; t->h = h;
    if (!t->bare) {                                            /* SOMA_285_CHAT: el chat pinta su propia caja */
        fb_fill_rounded_rect(x, y, w, h, 8, 0x00070C17u);
        fb_stroke_rrect_aa(x, y, w, h, 8, focused ? 0xFF38BDF8u : 0xFF243044u);
        if (focused) fb_stroke_rrect_aa(x - 1, y - 1, w + 2, h + 2, 9, 0x5038BDF8u);
    }
    const char *s = t->buf; uint32_t n = slen(s), a, b; sel(t, &a, &b);
    if (t->cur > n) t->cur = n;
    if (t->anc > n) t->anc = n;
    fb_set_clip(x + 4, y + 2, w - 8, h - 4);
    if (!t->multiline) {
        int ty = y + (h - TF_LH) / 2;
        int cx = colx(ncols(s, 0, t->cur));
        if (cx - t->scroll > w - 24) t->scroll = cx - (w - 24);
        if (cx - t->scroll < 0) t->scroll = cx > 20 ? cx - 20 : 0;
        int ox = x + 10 - t->scroll;
        if (!n && ph && !focused) run(x + 10, ty, ph, (int)slen(ph), 0x00566479u);
        if (focused && b > a) { int ax = colx(ncols(s, 0, a)), bx = colx(ncols(s, 0, b)); fb_fill_rect((uint32_t)(ox + ax), (uint32_t)(ty + 1), (uint32_t)(bx - ax), TF_LH - 2, 0x001E4A80u); }
        run(ox, ty, s, (int)n, tc);
        if (focused) fb_fill_rect((uint32_t)(ox + cx), (uint32_t)(ty + 2), 2, TF_LH - 4, 0x0038BDF8u);
        if (!n && ph && focused) run(x + 14, ty, ph, (int)slen(ph), 0x00384458u);
    } else {
        layout(t, cols_of(t));
        int rows = (h - 10) / TF_LH; if (rows < 1) rows = 1;
        int cl = line_of(t->cur);
        if (cl < t->scroll) t->scroll = cl;
        if (cl >= t->scroll + rows) t->scroll = cl - rows + 1;
        if (t->scroll > g_nl - 1) t->scroll = g_nl - 1;
        if (t->scroll < 0) t->scroll = 0;
        if (!n && ph) run(x + 10, y + 5, ph, (int)slen(ph), focused ? 0x00384458u : 0x00566479u);
        for (int l = t->scroll; l < g_nl && l < t->scroll + rows; l++) {
            int ly = y + 5 + (l - t->scroll) * TF_LH;
            uint32_t ls = g_ls[l], le = line_end(t, l);
            if (focused && b > a && a < le + 1 && b > ls) {
                uint32_t sa = a > ls ? a : ls, sb = b < le ? b : le;
                int ax = colx(ncols(s, ls, sa)), bx = colx(ncols(s, ls, sb)) + ((b > le && l + 1 < g_nl) ? colx(1) / 2 : 0);
                if (bx > ax) fb_fill_rect((uint32_t)(x + 10 + ax), (uint32_t)(ly + 1), (uint32_t)(bx - ax), TF_LH - 2, 0x001E4A80u);
            }
            run(x + 10, ly, s + ls, (int)(le - ls), tc);
            if (focused && l == cl) fb_fill_rect((uint32_t)(x + 10 + colx(ncols(s, ls, t->cur))), (uint32_t)(ly + 2), 2, TF_LH - 4, 0x0038BDF8u);
        }
        if (g_nl > rows) {                                     /* indicador de desplazamiento */
            int th = (h - 8) * rows / g_nl, ty = y + 4 + (h - 8 - th) * t->scroll / (g_nl - rows);
            fb_fill_rounded_rect(x + w - 7, ty, 3, th, 1, 0x00334155u);
        }
    }
    fb_reset_clip();
}

/* ---------------------------------------------------------------- raton */
int tf_hit(const struct tfield *t, int mx, int my) { return t->w > 0 && mx >= t->x && mx < t->x + t->w && my >= t->y && my < t->y + t->h; }
static uint32_t pos_xy(struct tfield *t, int mx, int my)
{
    tf_font();
    if (!t->multiline) {
        int rel = mx - (t->x + 10) + t->scroll; if (rel < 0) rel = 0;
        int col = (int)(((int64_t)rel * 64 + g_cw64 / 2) / g_cw64);
        uint32_t p = 0, n = slen(t->buf); int c = 0;
        while (p < n && c < col) { p = next_cp(t, p); c++; }
        return p;
    }
    layout(t, cols_of(t));
    int l = t->scroll + (my - (t->y + 5)) / TF_LH; if (my < t->y + 5) l = t->scroll - (my < t->y ? 1 : 0);
    if (l < 0) return 0;
    if (l >= g_nl) return slen(t->buf);
    int rel = mx - (t->x + 10); if (rel < 0) rel = 0;
    return pos_at(t, l, (int)(((int64_t)rel * 64 + g_cw64 / 2) / g_cw64));
}
void tf_press(struct tfield *t, int mx, int my, int shift)
{
    uint32_t p = pos_xy(t, mx, my);
    uint64_t now = timer_get_uptime_ms();
    if (now - t->last_click < 450 && (p == t->last_pos || t->clicks >= 2)) t->clicks++; else t->clicks = 1;
    t->last_click = now; t->last_pos = p;
    if (t->clicks == 2) {                                         /* palabra (o tramo de ruta entre barras) */
        uint32_t a = p, b = p, n = slen(t->buf);
        while (a > 0 && is_word(t->buf[a - 1])) a--;
        while (b < n && is_word(t->buf[b])) b++;
        t->anc = a; t->cur = b; t->dragging = 0; return;
    }
    if (t->clicks >= 3) { tf_select_all(t); t->dragging = 0; return; }
    t->cur = p; if (!shift) t->anc = p;
    t->dragging = 1;
}
int tf_drag(struct tfield *t, int mx, int my, int held)
{
    if (!t->dragging) return 0;
    if (!held) { t->dragging = 0; return 1; }
    uint32_t p = pos_xy(t, mx, my);
    if (p != t->cur) { t->cur = p; return 1; }
    return 0;
}

/* ---------------------------------------------------------------- teclado */
static void del_range(struct tfield *t, uint32_t a, uint32_t b)
{
    uint32_t n = slen(t->buf); if (b > n) b = n; if (b <= a) return;
    for (uint32_t i = a; i <= n - (b - a); i++) t->buf[i] = t->buf[i + (b - a)];
}
static void insert(struct tfield *t, const char *s, uint32_t k)
{
    if (t->readonly) return;
    uint32_t a, b; sel(t, &a, &b); del_range(t, a, b); t->cur = t->anc = a;
    uint32_t n = slen(t->buf);
    if (n + k + 1 > t->cap) k = t->cap - 1 - n;
    if (!k) return;
    for (uint32_t i = n + 1; i-- > a;) t->buf[i + k] = t->buf[i];
    for (uint32_t i = 0; i < k; i++) t->buf[a + i] = s[i];
    t->cur = t->anc = a + k;
}
static void vmove(struct tfield *t, int d, int shift)
{
    layout(t, cols_of(t));
    int l = line_of(t->cur), c = ncols(t->buf, g_ls[l], t->cur), nl = l + d;
    if (nl < 0) t->cur = 0; else if (nl >= g_nl) t->cur = slen(t->buf); else t->cur = pos_at(t, nl, c);
    if (!shift) t->anc = t->cur;
}
int tf_key(struct tfield *t, char ch, int ctrl, int shift)
{
    int k = (uint8_t)ch; uint32_t a, b, n = slen(t->buf); sel(t, &a, &b);
    tf_font();
    if (ctrl && k == 1) { tf_select_all(t); return TF_EDIT; }
    switch (k) {
    case 27: return TF_ESC;
    case '\t': return TF_TAB;
    case '\r': case '\n':
        if (t->multiline && !ctrl) { insert(t, "\n", 1); return TF_EDIT; }
        return TF_ENTER;
    case 2: if (!shift && b > a) t->cur = a; else t->cur = prev_cp(t, t->cur); if (!shift) t->anc = t->cur; return TF_EDIT;
    case 6: if (!shift && b > a) t->cur = b; else t->cur = next_cp(t, t->cur); if (!shift) t->anc = t->cur; return TF_EDIT;
    case 1: if (t->multiline) { layout(t, cols_of(t)); t->cur = g_ls[line_of(t->cur)]; } else t->cur = 0; if (!shift) t->anc = t->cur; return TF_EDIT;
    case 5: if (t->multiline) { layout(t, cols_of(t)); t->cur = line_end(t, line_of(t->cur)); } else t->cur = n; if (!shift) t->anc = t->cur; return TF_EDIT;
    case 16: if (t->multiline) { vmove(t, -1, shift); return TF_EDIT; } t->cur = 0; if (!shift) t->anc = 0; return TF_EDIT;
    case 14: if (t->multiline) { vmove(t, 1, shift); return TF_EDIT; } t->cur = n; if (!shift) t->anc = n; return TF_EDIT;
    case 8: case 127:
        if (t->readonly) return TF_EDIT;
        if (b > a) { del_range(t, a, b); t->cur = t->anc = a; }
        else if (t->cur > 0) { uint32_t p = prev_cp(t, t->cur); if (ctrl) { while (p > 0 && !is_word(t->buf[p])) p--; while (p > 0 && is_word(t->buf[p - 1])) p--; } del_range(t, p, t->cur); t->cur = t->anc = p; }
        return TF_EDIT;
    case 4:
        if (t->readonly) return TF_EDIT;
        if (b > a) { del_range(t, a, b); t->cur = t->anc = a; } else del_range(t, t->cur, next_cp(t, t->cur));
        return TF_EDIT;
    default: break;
    }
    if (k >= 0x80) {
        if (t->u8n == 0) { t->u8need = k >= 0xF0 ? 4 : k >= 0xE0 ? 3 : k >= 0xC0 ? 2 : 0; if (!t->u8need) return TF_EDIT; }
        else if ((k & 0xC0) != 0x80) { t->u8n = 0; return TF_EDIT; }
        t->u8[t->u8n++] = (uint8_t)k; if (t->u8n < t->u8need) return TF_EDIT;
        char s[4]; for (int i = 0; i < t->u8n; i++) s[i] = (char)t->u8[i];
        insert(t, s, (uint32_t)t->u8n); t->u8n = 0; return TF_EDIT;
    }
    if (k >= 32) { insert(t, &ch, 1); return TF_EDIT; }
    return TF_NONE;
}
int tf_clip(struct tfield *t, int op)
{
    uint32_t a, b; sel(t, &a, &b);
    if (op == CLIP_OP_PASTE) {
        uint32_t n = 0; const char *c = clip_text(&n);
        char tmp[1024]; uint32_t k = 0;
        for (uint32_t i = 0; c && i < n && k < sizeof tmp; i++) { char x = c[i]; if (x == '\r') continue; if (x == '\t') x = ' '; if (x == '\n' && !t->multiline) x = ' '; tmp[k++] = x; }
        insert(t, tmp, k);
        return 1;
    }
    if (b > a) clip_set_text(t->buf + a, b - a);
    if (op == CLIP_OP_CUT && b > a && !t->readonly) { del_range(t, a, b); t->cur = t->anc = a; }
    return 1;
}
/* SOMA_285_CHAT: lo que necesita el cuadro de texto del chat (crece con el texto, pega textos largos) */
int tf_lines(struct tfield *t, int w)
{
    tf_font();
    int ow = t->w; t->w = w; layout(t, cols_of(t)); t->w = ow;
    return g_nl;
}
void tf_insert(struct tfield *t, const char *s, uint32_t n) { tf_font(); insert(t, s, n); }
int tf_line_h(void) { return TF_LH; }
