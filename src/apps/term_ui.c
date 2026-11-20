/* SOMA_PARCHE_024_TERM: ver term_ui.h */
#include "term_ui.h"
#include "term_core.h"
#include "mem.h" /* SOMA_203_INSTANCIAS */
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "idt.h"

#define HDR_H     34
#define SB_W      14
#define BTN_H     22
#define PAD       8
#define PROMPT    "\033[1;36msoma\033[1;32m>\033[0m "
#define PROMPT_VIS 6

/* SOMA_203_INSTANCIAS: nucleo separado por terminal; sin copiar un buffer de
 * 224 KiB en cada fotograma. Solo cambia el puntero del contexto activo. */
#define TERM_SLOT_MAX 16
static struct term_core TC_default;
static struct term_core *TC = &TC_default;
static struct term_core *term_slots[TERM_SLOT_MAX];
static int term_slot_inited[TERM_SLOT_MAX];
static int term_active_slot;
static int g_inited;
static int live_on;                       /* la ultima linea del nucleo es la del prompt */
static int vec_mode = 1;                  /* 1 = fuente vectorial suave (zoom continuo); 0 = la fuente del sistema a escala entera (si no hay vectorial) */
static int zoom_bmp = 0;                  /* escala entera 1..4 -> indices 0..3 */
static int zoom_vec = 2;                  /* indice en VEC_EM */
static const int VEC_EM[] = { 10, 12, 14, 16, 18, 21, 24, 28, 34 };
#define VEC_N ((int)(sizeof VEC_EM / sizeof VEC_EM[0]))
static int g_debug;

/* geometria de la ultima pintada */
static struct {
    int x, y, w, h;
    int tx, ty, tw, th;                   /* area de texto */
    int cw, ch, em, base, cols, rows;
    int sbx, sby, sbh, sth;               /* barra: x; y y alto de la pista (entre flechas); alto total de la barra */
    int btn[5][4];                        /* copiar, pegar, A-, A+, menu: x,y,w,h */
    int valid;
} G;

static int hover_x = -1, hover_y = -1;
static int popup;                         /* 0 nada, 1 menu, 2 simbolos */
static int pop_x, pop_y, pop_w, pop_h;
static int drag;                          /* 0 nada, 1 seleccionando, 2 arrastrando el pulgar */
static int drag_anchor_idx = -1, drag_anchor_col, drag_started;
static int thumb_grab;                    /* distancia del clic al inicio del pulgar */
static uint64_t last_click_ms; static int last_click_x, last_click_y, click_count;
static uint32_t last_cursor_ms;

int term_ui_select(int slot)
{
    if (slot < 0 || slot >= TERM_SLOT_MAX) return 0;
    if (slot == 0) { TC = &TC_default; term_active_slot = 0; g_inited = term_slot_inited[0]; }
    else {
        if (!term_slots[slot]) {
            term_slots[slot] = (struct term_core *)kmalloc(sizeof(struct term_core));
            if (!term_slots[slot]) return 0;
            tc_init(term_slots[slot], 80);
            term_slot_inited[slot] = 1;
        }
        TC = term_slots[slot]; term_active_slot = slot; g_inited = term_slot_inited[slot];
    }
    popup = 0; drag = 0; G.valid = 0;
    return 1;
}

void term_ui_drop(int slot)
{
    if (slot <= 0 || slot >= TERM_SLOT_MAX) return;
    if (term_active_slot == slot) (void)term_ui_select(0);
    if (term_slots[slot]) { kfree(term_slots[slot]); term_slots[slot] = 0; }
    term_slot_inited[slot] = 0;
}

static void dbg_u(const char *k, int v) { kprint(k); kprint_dec((uint32_t)(v < 0 ? 0 : v)); }

/* ---------- utilidades ---------- */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

void term_set_debug(int on) { g_debug = on; G.valid = 0; }

static void ensure_init(void) { if (!g_inited) { tc_init(TC, 80); g_inited = 1; term_slot_inited[term_active_slot] = 1; } }

/* SOMA_237_PYTHON: mientras una orden pide lineas (python), el prompt es lo que ella ha escrito sin salto (">>> ", "nombre? ")
 * y solo en la sesion de terminal donde corre. NULL = el de siempre. */
static char g_prompt[96]; static int g_prompt_vis = -1, g_prompt_slot = -1;
void term_set_prompt(int slot, const char *p)
{
    if (!p) { g_prompt_vis = -1; g_prompt_slot = -1; return; }
    int n = 0, v = 0;
    for (; p[n] && n < (int)sizeof g_prompt - 1; ++n) { g_prompt[n] = p[n]; if (((uint8_t)p[n] & 0xC0) != 0x80) v++; }
    g_prompt[n] = 0; g_prompt_vis = v; g_prompt_slot = slot;
}
static const char *cur_prompt(void) { return g_prompt_vis >= 0 && g_prompt_slot == term_active_slot ? g_prompt : PROMPT; }
static int cur_prompt_vis(void) { return g_prompt_vis >= 0 && g_prompt_slot == term_active_slot ? g_prompt_vis : PROMPT_VIS; }

void term_ui_init(void) { ensure_init(); tc_clear(TC); live_on = 0; popup = 0; drag = 0; }

void term_clear(void) { ensure_init(); tc_clear(TC); live_on = 0; }

void term_print(const char *raw)
{
    ensure_init();
    if (live_on) { tc_pop(TC); live_on = 0; }
    int n = slen(raw);
    if (n <= 200) { tc_add(TC, raw); return; }
    for (int i = 0; i < n; i += 200) {            /* las lineas muy largas se parten en trozos (el nucleo admite 223 bytes) */
        char tmp[204]; int k = 0;
        while (k < 200 && raw[i + k]) { tmp[k] = raw[i + k]; k++; }
        tmp[k] = 0; tc_add(TC, tmp);
    }
}

void term_note_input(void) { ensure_init(); TC->scroll = 0; tc_sel_clear(TC); }

static void metrics(void)
{
    if (vec_mode && font_vec_available()) {
        G.em = VEC_EM[zoom_vec];
        G.cw = (G.em * 6 + 5) / 10; if (G.cw < 5) G.cw = 5;
        G.ch = (G.em * 13 + 5) / 10;
        G.base = (G.em * 95 + 50) / 100;
    } else {
        int s = zoom_bmp + 1;
        G.em = 0; G.cw = 8 * s; G.ch = 14 * s; G.base = 0;
    }
}

static uint32_t color_of(const struct tc_cell *c)
{
    static const uint32_t pal[8] = { 0x00475569, 0x00F87171, 0x0034D399, 0x00FBBF24, 0x0060A5FA, 0x00E879F9, 0x0038BDF8, 0x00F8FAFC };
    if (c->fg >= 30 && c->fg <= 37) return pal[c->fg - 30];
    return c->bold ? 0x00FFFFFF : 0x00E2E8F0;
}

static void draw_cell(int x, int y, const struct tc_cell *c, uint32_t col)
{
    char ch = c->b[0];
    if (c->nb == 1 && ch == ' ') return;
    if (vec_mode && font_vec_available()) {
        uint32_t cp = (uint8_t)ch;
        if (c->nb == 2) cp = (((uint32_t)(uint8_t)c->b[0] & 0x1Fu) << 6) | ((uint32_t)(uint8_t)c->b[1] & 0x3Fu);
        font_vec_draw(x, y, cp, col, G.em, G.cw, G.base);
    } else {
        int s = zoom_bmp + 1;
        if (c->nb == 2) { char seq[3] = { c->b[0], c->b[1], 0 }; font_bitmap_cell(x, y + 3 * s, seq, col, s); }
        else font_draw_char((uint32_t)x, (uint32_t)(y + 3 * s), ch, col, s);
    }
}

/* ---------- disposicion ---------- */
static void layout(int x, int y, int w, int h)
{
    metrics();
    G.x = x; G.y = y; G.w = w; G.h = h;
    G.tx = x + PAD; G.ty = y + HDR_H + 6;
    G.sbx = x + w - SB_W - 8;                            /* separada del borde y de la esquina de redimensionar */
    G.tw = G.sbx - 6 - G.tx; if (G.tw < G.cw) G.tw = G.cw;
    G.th = h - HDR_H - 6 - PAD; if (G.th < G.ch) G.th = G.ch;
    G.cols = G.tw / G.cw; if (G.cols < 1) G.cols = 1;
    G.rows = G.th / G.ch; if (G.rows < 1) G.rows = 1;
    G.sth = G.th - 20; if (G.sth < 48) G.sth = 48;       /* deja libre el tirador de la esquina inferior derecha de la ventana */
    G.sby = G.ty + 16; G.sbh = G.sth - 32; if (G.sbh < 8) G.sbh = 8;
    static const int bw[5] = { 58, 50, 30, 30, 46 };
    int bx = x + w - 8;
    for (int i = 4; i >= 0; --i) { bx -= bw[i]; G.btn[i][0] = bx; G.btn[i][1] = y + 6; G.btn[i][2] = bw[i]; G.btn[i][3] = BTN_H; bx -= 6; }
    G.valid = 1;
}

static void print_geom(void)
{
    if (!g_debug) return;
    kprint("[term] geom ventana "); dbg_u("", G.x); dbg_u(",", G.y); dbg_u(",", G.w); dbg_u(",", G.h);
    dbg_u(" texto ", G.tx); dbg_u(",", G.ty); dbg_u(",", G.tw); dbg_u(",", G.th);
    dbg_u(" celda ", G.cw); dbg_u("x", G.ch); dbg_u(" cols ", G.cols); dbg_u(" filas ", G.rows);
    dbg_u(" barra ", G.sbx); dbg_u(",", G.sby); dbg_u(",", G.sbh); dbg_u(",", G.sth);
    const char *nm[5] = { " copiar ", " pegar ", " menos ", " mas ", " menu " };
    for (int i = 0; i < 5; ++i) { dbg_u(nm[i], G.btn[i][0]); dbg_u(",", G.btn[i][1]); dbg_u(",", G.btn[i][2]); dbg_u(",", G.btn[i][3]); }
    kprint("\n");
}

static int in_rect(int mx, int my, int rx, int ry, int rw, int rh) { return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh; }

/* ---------- pintado ---------- */
static void draw_button(const int *b, const char *label, int enabled, int hot)
{
    uint32_t bg = !enabled ? 0x00172033 : hot ? 0x00334155 : 0x00253349;
    fb_fill_rounded_rect(b[0], b[1], b[2], b[3], 4, bg);
    fb_draw_rounded_rect(b[0], b[1], b[2], b[3], 4, enabled ? (hot ? 0x0038BDF8 : 0x00475569) : 0x00263248);
    int tw = 0; for (const char *q = label; *q; ++q) if (((unsigned char)*q & 0xC0) != 0x80) tw += 8;
    font_draw_string((uint32_t)(b[0] + (b[2] - tw) / 2), (uint32_t)(b[1] + (b[3] - 8) / 2), label, enabled ? 0x00F8FAFC : 0x00566074, 1);
}

static const char *const MENU_TXT[] = {
    "Copiar", "Pegar", "Seleccionar todo", "Limpiar pantalla", "-",
    "Aumentar texto", "Reducir texto", "Tamaño normal", "-",
    "Símbolos...", "Ir al final"
};
static const char *const MENU_KEY[] = { "Ctrl+C", "Ctrl+V", "", "Ctrl+L", "", "Ctrl +", "Ctrl -", "", "", "", "Fin" };
#define MENU_N 11
#define MENU_ROW 24
#define MENU_SEP 9

static int menu_item_y(int i) { int y = pop_y + 6; for (int k = 0; k < i; ++k) y += (MENU_TXT[k][0] == '-') ? MENU_SEP : MENU_ROW; return y; }
static int menu_item_h(int i) { return MENU_TXT[i][0] == '-' ? MENU_SEP : MENU_ROW; }

static int menu_height(void) { int h = 12; for (int k = 0; k < MENU_N; ++k) h += menu_item_h(k); return h; }

static int has_sel(void)
{
    char t[8];
    return TC->sel_active && tc_sel_text(TC, t, 2) > 0;
}

static const char SYM[] = "<>|&~`\\/{}[]()$#^_-+=*\"';:%@!?,.";
#define SYM_COLS 8
#define SYM_CELL 30

static void draw_popup(void)
{
    if (popup == 1) {
        fb_fill_rect((uint32_t)pop_x + 3, (uint32_t)pop_y + 3, (uint32_t)pop_w, (uint32_t)pop_h, 0x00020308);
        fb_fill_rounded_rect(pop_x, pop_y, pop_w, pop_h, 6, 0x00182235);
        fb_draw_rounded_rect(pop_x, pop_y, pop_w, pop_h, 6, 0x00475569);
        for (int i = 0; i < MENU_N; ++i) {
            int y = menu_item_y(i), h = menu_item_h(i);
            if (MENU_TXT[i][0] == '-') { fb_fill_rect((uint32_t)pop_x + 8, (uint32_t)(y + h / 2), (uint32_t)pop_w - 16, 1, 0x00334155); continue; }
            int enabled = !(i == 0 && !has_sel()) && !(i == 1 && !tc_clip_get(0)[0]);
            int hot = enabled && in_rect(hover_x, hover_y, pop_x + 4, y, pop_w - 8, h);
            if (hot) fb_fill_rounded_rect(pop_x + 4, y, pop_w - 8, h, 4, 0x001D4ED8);
            const char *t = MENU_TXT[i];
            font_draw_string((uint32_t)pop_x + 14, (uint32_t)(y + (h - 8) / 2), t, enabled ? 0x00F1F5F9 : 0x00566074, 1);
            int kl = slen(MENU_KEY[i]) * 8;
            if (kl) font_draw_string((uint32_t)(pop_x + pop_w - 14 - kl), (uint32_t)(y + (h - 8) / 2), MENU_KEY[i], enabled ? 0x0094A3B8 : 0x00475569, 1);
        }
    } else if (popup == 2) {
        fb_fill_rect((uint32_t)pop_x + 3, (uint32_t)pop_y + 3, (uint32_t)pop_w, (uint32_t)pop_h, 0x00020308);
        fb_fill_rounded_rect(pop_x, pop_y, pop_w, pop_h, 6, 0x00182235);
        fb_draw_rounded_rect(pop_x, pop_y, pop_w, pop_h, 6, 0x00475569);
        font_draw_string((uint32_t)pop_x + 10, (uint32_t)pop_y + 8, "Símbolos (clic = insertar)", 0x0038BDF8, 1);
        font_draw_string((uint32_t)(pop_x + pop_w - 20), (uint32_t)pop_y + 8, "x", 0x00F87171, 1);
        int n = slen(SYM);
        for (int i = 0; i < n; ++i) {
            int cx = pop_x + 8 + (i % SYM_COLS) * SYM_CELL, cy = pop_y + 28 + (i / SYM_COLS) * SYM_CELL;
            int hot = in_rect(hover_x, hover_y, cx, cy, SYM_CELL - 2, SYM_CELL - 2);
            fb_fill_rounded_rect(cx, cy, SYM_CELL - 2, SYM_CELL - 2, 4, hot ? 0x001D4ED8 : 0x00253349);
            char s[2] = { SYM[i], 0 };
            font_draw_string((uint32_t)(cx + (SYM_CELL - 2 - 8) / 2), (uint32_t)(cy + (SYM_CELL - 2 - 8) / 2), s, 0x00F8FAFC, 1);
        }
    }
}

void term_render(uint32_t ux, uint32_t uy, uint32_t uw, uint32_t uh, const char *cmd, uint32_t cmd_len, uint32_t cursor_pos, int focused)
{
    ensure_init();
    int x = (int)ux, y = (int)uy, w = (int)uw, h = (int)uh;
    int old_cols = G.cols, old_cw = G.cw, old_valid = G.valid, ow = G.w, oh = G.h;
    layout(x, y, w, h);
    if (g_debug && (!old_valid || old_cols != G.cols || old_cw != G.cw || ow != G.w || oh != G.h)) print_geom();
    tc_set_cols(TC, G.cols);

    /* la linea del prompt es la ultima linea del nucleo */
    {
        char live[TC_RAW]; int p = 0;
        for (const char *s = cur_prompt(); *s && p < TC_RAW - 1; ++s) live[p++] = *s;
        for (uint32_t i = 0; i < cmd_len && p < TC_RAW - 1; ++i) live[p++] = cmd[i];
        live[p] = 0;
        if (live_on) tc_replace_last(TC, live); else { tc_add(TC, live); live_on = 1; }
    }

    /* marco y cabecera */
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x00050810);
    fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x0010B981);
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, HDR_H, 0x001E293B);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + HDR_H - 1), (uint32_t)w, 1, 0x00334155);
    {
        int room = G.btn[0][0] - (x + 16) - 8;
        const char *title = room >= 41 * 8 ? "soma@soma-node1: ~ (tty1) - Terminal SOMA" : room >= 22 * 8 ? "soma@soma-node1: ~ (tty1)" : room >= 8 * 8 ? "Terminal" : "";
        font_draw_string((uint32_t)x + 16, (uint32_t)y + 13, title, 0x00F8FAFC, 1);
    }
    int pops = popup;
    draw_button(G.btn[0], "Copiar", has_sel(), !pops && in_rect(hover_x, hover_y, G.btn[0][0], G.btn[0][1], G.btn[0][2], G.btn[0][3]));
    draw_button(G.btn[1], "Pegar", tc_clip_get(0)[0] != 0, !pops && in_rect(hover_x, hover_y, G.btn[1][0], G.btn[1][1], G.btn[1][2], G.btn[1][3]));
    draw_button(G.btn[2], "A-", 1, !pops && in_rect(hover_x, hover_y, G.btn[2][0], G.btn[2][1], G.btn[2][2], G.btn[2][3]));
    draw_button(G.btn[3], "A+", 1, !pops && in_rect(hover_x, hover_y, G.btn[3][0], G.btn[3][1], G.btn[3][2], G.btn[3][3]));
    draw_button(G.btn[4], "Menú", 1, !pops && in_rect(hover_x, hover_y, G.btn[4][0], G.btn[4][1], G.btn[4][2], G.btn[4][3]));

    /* texto */
    fb_set_clip(G.tx, G.ty, G.tw, G.th);
    int cur_row = -1, cur_col = 0;
    {
        int live_idx = TC->count - 1;
        /* columna visual del cursor: caracteres (no bytes) desde el inicio de la orden */
        int vc = cur_prompt_vis();
        for (uint32_t i = 0; i < cursor_pos && i < cmd_len; ++i) { uint8_t b = (uint8_t)cmd[i]; if ((b & 0xC0) != 0x80) vc++; }
        for (int r = 0; r < G.rows; ++r) {
            int idx, sub;
            if (!tc_screen_row(TC, G.rows, r, &idx, &sub)) continue;
            struct tc_cell cells[256];
            int n = tc_row_cells(TC, idx, sub, cells, G.cols < 256 ? G.cols : 256);
            int ry = G.ty + r * G.ch;
            for (int i = 0; i < n; ++i) {
                int rx = G.tx + i * G.cw;
                uint32_t col = color_of(&cells[i]);
                if (tc_is_selected(TC, idx, sub * G.cols + i)) { fb_fill_rect((uint32_t)rx, (uint32_t)ry, (uint32_t)G.cw, (uint32_t)G.ch, 0x001E40AF); col = 0x00FFFFFF; }
                draw_cell(rx, ry, &cells[i], col);
            }
            if (idx == live_idx && vc / G.cols == sub) { cur_row = r; cur_col = vc % G.cols; }
        }
    }
    if (focused && cur_row >= 0 && TC->scroll == 0 && !popup) {
        uint32_t now = timer_get_uptime_ms();
        if (now - last_cursor_ms < 1200 || ((now / 500) & 1) == 0) {
            int cx = G.tx + cur_col * G.cw, cy = G.ty + cur_row * G.ch;
            fb_fill_rect((uint32_t)cx, (uint32_t)cy + 1, (uint32_t)G.cw, (uint32_t)G.ch - 2, 0x0038BDF8);
            /* el caracter bajo el cursor, en negro */
            struct tc_cell cc; int idx, sub;
            if (tc_screen_row(TC, G.rows, cur_row, &idx, &sub)) {
                struct tc_cell cells[256]; int n = tc_row_cells(TC, idx, sub, cells, G.cols < 256 ? G.cols : 256);
                if (cur_col < n) { cc = cells[cur_col]; draw_cell(cx, cy, &cc, 0x00000000); }
            }
        }
    }
    fb_reset_clip();

    /* barra de desplazamiento */
    {
        int tx = G.sbx, track_y = G.ty, track_h = G.sth;
        fb_fill_rounded_rect(tx, track_y, SB_W, track_h, 4, 0x000C1220);
        int up_hot = in_rect(hover_x, hover_y, tx, track_y, SB_W, 16), dn_hot = in_rect(hover_x, hover_y, tx, track_y + track_h - 16, SB_W, 16);
        fb_fill_rounded_rect(tx, track_y, SB_W, 16, 4, up_hot ? 0x00334155 : 0x00172033);
        fb_fill_rounded_rect(tx, track_y + track_h - 16, SB_W, 16, 4, dn_hot ? 0x00334155 : 0x00172033);
        /* triangulos de las flechas */
        for (int i = 0; i < 4; ++i) {
            fb_fill_rect((uint32_t)(tx + 7 - i), (uint32_t)(track_y + 5 + i), (uint32_t)(2 * i + 1), 1, 0x0094A3B8);
            fb_fill_rect((uint32_t)(tx + 7 - i), (uint32_t)(track_y + track_h - 5 - i), (uint32_t)(2 * i + 1), 1, 0x0094A3B8);
        }
        int ty, th;
        if (tc_thumb(TC, G.rows, G.sbh, 24, &ty, &th)) {
            int hot = drag == 2 || in_rect(hover_x, hover_y, tx, G.sby + ty, SB_W, th);
            fb_fill_rounded_rect(tx + 2, G.sby + ty, SB_W - 4, th, 4, hot ? 0x0064748B : 0x00475569);
        }
    }

    if (popup) draw_popup();
    if (g_debug) {
        static int ls = -1, lt = -1, lz = -1, lm = -1, lsel = -1, lpop = -1;
        int total = tc_total_rows(TC), z = (vec_mode && font_vec_available()) ? zoom_vec : zoom_bmp, m = vec_mode && font_vec_available();
        char tmp[4]; int sel = TC->sel_active ? tc_sel_text(TC, tmp, 2) > 0 : 0;
        if (ls != TC->scroll || lt != total || lz != z || lm != m || lsel != sel || lpop != popup) {
            ls = TC->scroll; lt = total; lz = z; lm = m; lsel = sel; lpop = popup;
            kprint("[term] estado scroll "); kprint_dec((uint32_t)TC->scroll); kprint(" total "); kprint_dec((uint32_t)total);
            kprint(" vista "); kprint_dec((uint32_t)G.rows); kprint(" zoom "); kprint_dec((uint32_t)z); kprint(m ? " vector" : " bitmap");
            kprint(sel ? " con-seleccion" : " sin-seleccion"); kprint(popup == 1 ? " menu" : popup == 2 ? " simbolos" : ""); kprint("\n");
        }
    }
}

/* ---------- acciones ---------- */
static void copy_selection(void)
{
    static char buf[TC_CLIP_MAX + 1];
    int n = tc_sel_text(TC, buf, sizeof buf);
    if (n <= 0) return;
    tc_clip_set(buf, n);
    if (g_debug) {
        char show[210]; int k = 0;
        if (n <= 200) { while (k < n) { show[k] = buf[k] == '\n' ? '|' : buf[k]; k++; } }
        else {                                            /* texto largo: principio y final */
            while (k < 100) { show[k] = buf[k] == '\n' ? '|' : buf[k]; k++; }
            show[k++] = '~'; show[k++] = '~'; show[k++] = '~';
            for (int q = n - 90; q < n && k < 205; ++q) show[k++] = buf[q] == '\n' ? '|' : buf[q];
        }
        show[k] = 0;
        kprint("[term] copiado "); kprint_dec((uint32_t)n); kprint(" caracteres: "); kprint(show); kprint("\n");
    }
}

static int edit_insert(struct term_edit *ed, const char *s, int n)
{
    int ins = 0;
    for (int i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c < 32 || c == 127) continue;
        if (*ed->len + 2 >= ed->cap) break;
        for (uint32_t k = *ed->len; k > *ed->cur; --k) ed->buf[k] = ed->buf[k - 1];
        ed->buf[(*ed->cur)++] = (char)c; (*ed->len)++; ed->buf[*ed->len] = 0;
        ins++;
    }
    return ins;
}

static int do_paste(struct term_edit *ed)
{
    int n; const char *s = tc_clip_get(&n);
    if (n <= 0) return 0;
    int ins = edit_insert(ed, s, n);
    if (g_debug) { kprint("[term] pegado "); kprint_dec((uint32_t)ins); kprint(" caracteres\n"); }
    term_note_input(); last_cursor_ms = timer_get_uptime_ms();
    return 1;
}

static void zoom(int dir)
{
    if (vec_mode && font_vec_available()) { zoom_vec += dir; if (zoom_vec < 0) zoom_vec = 0; if (zoom_vec >= VEC_N) zoom_vec = VEC_N - 1; }
    else { zoom_bmp += dir; if (zoom_bmp < 0) zoom_bmp = 0; if (zoom_bmp > 3) zoom_bmp = 3; }
    G.valid = 0;
}
static void zoom_reset(void) { zoom_vec = 2; zoom_bmp = 0; G.valid = 0; }

static void open_menu(int mx, int my)
{
    popup = 1; pop_w = 250; pop_h = menu_height();
    pop_x = mx; pop_y = my;
    if (pop_x + pop_w > G.x + G.w - 4) pop_x = G.x + G.w - 4 - pop_w;
    if (pop_y + pop_h > G.y + G.h - 4) pop_y = G.y + G.h - 4 - pop_h;
    if (pop_x < G.x + 4) pop_x = G.x + 4;
    if (pop_y < G.y + 4) pop_y = G.y + 4;
}

static void open_symbols(void)
{
    int n = slen(SYM);
    popup = 2; pop_w = 16 + SYM_COLS * SYM_CELL; pop_h = 36 + ((n + SYM_COLS - 1) / SYM_COLS) * SYM_CELL;
    pop_x = G.x + G.w - pop_w - 24; pop_y = G.y + HDR_H + 8;
    if (pop_x < G.x + 4) pop_x = G.x + 4;
}

void term_popup_close(void) { popup = 0; }
int term_popup_open(void) { return popup != 0; }

static void go_bottom(void) { TC->scroll = 0; }

static int menu_action(int i, struct term_edit *ed)
{
    switch (i) {
    case 0: copy_selection(); break;
    case 1: do_paste(ed); break;
    case 2: tc_select_all(TC); break;
    case 3: term_clear(); break;
    case 5: zoom(1); break;
    case 6: zoom(-1); break;
    case 7: zoom_reset(); break;
    case 9: open_symbols(); return 1;
    case 10: go_bottom(); break;
    default: return 0;
    }
    popup = 0;
    return 1;
}

/* ---------- raton ---------- */
static int text_hit(int mx, int my, int *idx, int *vcol, int clamp)
{
    int row = (my - G.ty) / G.ch, col = (mx - G.tx) / G.cw;
    if (my < G.ty) row = 0; else if (my >= G.ty + G.th) row = G.rows - 1;
    if (!clamp && (mx < G.tx || mx >= G.tx + G.tw || my < G.ty || my >= G.ty + G.th)) return 0;
    if (col < 0) col = 0;
    if (col >= G.cols) col = G.cols - 1;
    return tc_hit(TC, G.rows, row, col, idx, vcol);
}

int term_mouse_down(int32_t mx, int32_t my, uint32_t ux, uint32_t uy, uint32_t uw, uint32_t uh, struct term_edit *ed)
{
    ensure_init();
    if (!G.valid) layout((int)ux, (int)uy, (int)uw, (int)uh);
    hover_x = mx; hover_y = my;
    if (popup == 1) {
        if (in_rect(mx, my, pop_x, pop_y, pop_w, pop_h)) {
            for (int i = 0; i < MENU_N; ++i) if (MENU_TXT[i][0] != '-' && in_rect(mx, my, pop_x + 4, menu_item_y(i), pop_w - 8, menu_item_h(i))) {
                int enabled = !(i == 0 && !has_sel()) && !(i == 1 && !tc_clip_get(0)[0]);
                if (enabled) menu_action(i, ed);
                return 1;
            }
            return 1;
        }
        popup = 0; return 1;                              /* clic fuera: se cierra */
    }
    if (popup == 2) {
        if (in_rect(mx, my, pop_x, pop_y, pop_w, pop_h)) {
            if (in_rect(mx, my, pop_x + pop_w - 26, pop_y + 2, 24, 22)) { popup = 0; return 1; }
            int n = slen(SYM);
            for (int i = 0; i < n; ++i) {
                int cx = pop_x + 8 + (i % SYM_COLS) * SYM_CELL, cy = pop_y + 28 + (i / SYM_COLS) * SYM_CELL;
                if (in_rect(mx, my, cx, cy, SYM_CELL - 2, SYM_CELL - 2)) { char c = SYM[i]; edit_insert(ed, &c, 1); term_note_input(); last_cursor_ms = timer_get_uptime_ms(); return 1; }
            }
            return 1;
        }
        popup = 0; return 1;
    }
    /* barra de herramientas */
    for (int i = 0; i < 5; ++i) if (in_rect(mx, my, G.btn[i][0], G.btn[i][1], G.btn[i][2], G.btn[i][3])) {
        switch (i) {
        case 0: if (has_sel()) copy_selection(); break;
        case 1: do_paste(ed); break;
        case 2: zoom(-1); break;
        case 3: zoom(1); break;
        case 4: open_menu(G.btn[4][0] + G.btn[4][2] - 250, G.btn[4][1] + G.btn[4][3] + 4); break;
        }
        return 1;
    }
    /* barra de desplazamiento */
    if (in_rect(mx, my, G.sbx, G.ty, SB_W, G.sth)) {
        if (my < G.ty + 16) { tc_scroll_by(TC, 3, G.rows); return 1; }
        if (my >= G.ty + G.sth - 16) { tc_scroll_by(TC, -3, G.rows); return 1; }
        int ty, th;
        if (tc_thumb(TC, G.rows, G.sbh, 24, &ty, &th)) {
            int rel = my - G.sby;
            if (rel >= ty && rel < ty + th) { drag = 2; thumb_grab = rel - ty; }
            else if (rel < ty) tc_scroll_by(TC, G.rows - 1, G.rows);
            else tc_scroll_by(TC, -(G.rows - 1), G.rows);
        }
        return 1;
    }
    /* texto: seleccion */
    int idx, vcol;
    if (text_hit(mx, my, &idx, &vcol, 0)) {
        uint64_t now = timer_get_uptime_ms();
        int near = (mx - last_click_x) * (mx - last_click_x) + (my - last_click_y) * (my - last_click_y) < 25 * 25;
        click_count = (now - last_click_ms < 450 && near) ? click_count + 1 : 1;
        last_click_ms = now; last_click_x = mx; last_click_y = my;
        if (click_count == 2) { tc_select_word(TC, idx, vcol); drag = 0; }
        else if (click_count >= 3) { tc_select_line(TC, idx); drag = 0; click_count = 3; }
        else { tc_sel_clear(TC); drag = 1; drag_anchor_idx = idx; drag_anchor_col = vcol; drag_started = 0; }
        return 1;
    }
    return 1;
}

int term_mouse_move(int32_t mx, int32_t my, int left_held)
{
    int redraw = (mx != hover_x || my != hover_y);
    hover_x = mx; hover_y = my;
    if (!left_held) { if (drag) { drag = 0; redraw = 1; } return redraw; }
    if (drag == 2) {
        int ty, th;
        if (tc_thumb(TC, G.rows, G.sbh, 24, &ty, &th)) {
            int s = tc_scroll_from_thumb(TC, G.rows, G.sbh, 24, (my - G.sby) - thumb_grab);
            tc_scroll_set(TC, s, G.rows);
        }
        return 1;
    }
    if (drag == 1) {
        if (my < G.ty) tc_scroll_by(TC, 1, G.rows);                  /* arrastrar fuera de la zona: se desplaza solo */
        else if (my >= G.ty + G.th) tc_scroll_by(TC, -1, G.rows);
        int idx, vcol;
        if (text_hit(mx, my, &idx, &vcol, 1)) {
            if (!drag_started && (idx != drag_anchor_idx || vcol != drag_anchor_col)) { drag_started = 1; tc_sel_begin(TC, drag_anchor_idx, drag_anchor_col); }
            if (drag_started) tc_sel_extend(TC, idx, vcol);
        }
        return 1;
    }
    return redraw;
}

int term_mouse_up(int32_t mx, int32_t my)
{
    (void)mx; (void)my;
    int was = drag;
    drag = 0;
    return was != 0;
}

int term_mouse_right(int32_t mx, int32_t my, uint32_t ux, uint32_t uy, uint32_t uw, uint32_t uh)
{
    ensure_init();
    if (!G.valid) layout((int)ux, (int)uy, (int)uw, (int)uh);
    open_menu(mx, my);
    return 1;
}

int term_wheel(int notches)
{
    ensure_init();
    if (popup) return 0;
    tc_scroll_by(TC, notches * 3, G.rows > 0 ? G.rows : 10);
    return 1;
}

/* ---------- teclado ---------- */
int term_key(char c, struct term_edit *ed)
{
    ensure_init();
    int rows = G.rows > 0 ? G.rows : 10;
    switch ((unsigned char)c) {
    case 28: tc_scroll_by(TC, rows - 1, rows); return 1;                      /* RePag */
    case 29: tc_scroll_by(TC, -(rows - 1), rows); return 1;                   /* AvPag */
    case 30: zoom(-1); return 1;                                               /* Ctrl - */
    case 31: zoom(1); return 1;                                                /* Ctrl + */
    case 3:  if (has_sel()) copy_selection(); return 1;                        /* Ctrl+C: copia (si no hay seleccion, no hace nada) */
    case 22: do_paste(ed); return 1;                                           /* Ctrl+V */
    case 12: term_clear(); return 1;                                           /* Ctrl+L */
    case 1:  *ed->cur = 0; term_note_input(); last_cursor_ms = timer_get_uptime_ms(); return 1;          /* Inicio */
    case 5:  *ed->cur = *ed->len; term_note_input(); last_cursor_ms = timer_get_uptime_ms(); go_bottom(); return 1;   /* Fin */
    case 4:                                                                    /* Supr */
        if (*ed->cur < *ed->len) {
            uint32_t n = 1; while (*ed->cur + n < *ed->len && ((unsigned char)ed->buf[*ed->cur + n] & 0xC0) == 0x80) n++;
            for (uint32_t i = *ed->cur; i + n <= *ed->len; ++i) ed->buf[i] = ed->buf[i + n];
            *ed->len -= n;
        }
        term_note_input(); return 1;
    default: break;
    }
    return 0;
}
