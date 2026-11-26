/* SOMA_338_AGENTES: la aplicacion Agentes, ahora un panel vivo (antes, tres tarjetas fijas).
 *   - Ahora: la mision en curso, cuanto lleva, los especialistas por los que ha pasado (el de ahora late) y "Detener".
 *     En reposo: la ultima mision, como acabo y el principio de la respuesta.
 *   - Especialistas: que hace cada uno, si esta trabajando, cuantas veces ha trabajado y hace cuanto; "Probar" le manda una
 *     peticion de ejemplo (la respuesta sale en el orbe).
 *   - Misiones recientes (hora, como acabaron, cuanto duraron, quien trabajo) y la actividad (ordenes, busquedas, lecturas).
 * Los datos los lleva src/agent/agentes_reg.c. */
#include "agentes_gui.h"
#include "agentes_reg.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "idt.h"
#include "hora.h"

#define C_WIN   0x000B1220
#define C_CARD  0x00111827
#define C_CARD2 0x000F1622
#define C_LINE  0x00212B3B
#define C_TXT   0x00E5EAF2
#define C_DIM   0x008592A8
#define C_FAINT 0x00566379
#define C_ACC   0x0038BDF8
#define C_ROJO  0x00F87171
#define C_VERDE 0x004ADE80
#define C_AMBAR 0x00FBBF24

int gui_agent_cancel(void);
int gui_agent_busy(void);
int gui_agent_submit_chat(const char *mision);

static const uint32_t COL[AGE_N] = { 0x0038BDF8, 0x0034D399, 0x00F59E0B, 0x0022C55E, 0x000EA5E9, 0x00A78BFA, 0x00F472B6, 0x00FB7185 };
static uint32_t g_ver = 1, g_visto; static uint64_t g_t;
static int g_scroll, g_hover_prob = -1;
static char g_msg[160]; static uint32_t g_msg_col = C_DIM;

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, uint64_t v) { char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23); char r[24]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int twn(const char *s, int n, int px, int bold) { return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static int tw(const char *s, int px, int bold) { return twn(s, s_len(s), px, bold); }
static void txn(int32_t x, int32_t top, int32_t h, const char *s, int n, int px, uint32_t col, int bold)
{
    if (n <= 0) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    for (int i = 0; i < n; i++) { char c[2] = { s[i], 0 }; font_draw_string((uint32_t)(x + i * 8), (uint32_t)(top + (h - 10) / 2), c, col, 1); }
}
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold) { txn(x, top, h, s, s_len(s), px, col, bold); }
/* texto en un ancho, por palabras, hasta max lineas (la ultima con "...") */
static void lineas(int x, int y, int lh, const char *s, int px, int bold, uint32_t col, int w, int max)
{
    int n = s_len(s), a = 0, k = 0;
    while (a < n && k < max) {
        while (a < n && s[a] == ' ') a++;
        if (a >= n) break;
        int e = a, ult = -1;
        while (e < n) { int e2 = e + 1; while (e2 < n && ((unsigned char)s[e2] & 0xC0) == 0x80) e2++; if (twn(s + a, e2 - a, px, bold) > w) break; if (s[e] == ' ') ult = e; e = e2; }
        if (e < n && ult > a) e = ult;
        if (e == a) { e = a + 1; while (e < n && ((unsigned char)s[e] & 0xC0) == 0x80) e++; }
        if (k == max - 1 && e < n) {
            char t[400]; int m = e - a; if (m > 380) m = 380; for (int j = 0; j < m; j++) t[j] = s[a + j];
            while (m > 0 && twn(t, m, px, bold) + tw("\342\200\246", px, bold) > w) { m--; while (m > 0 && ((unsigned char)t[m] & 0xC0) == 0x80) m--; }
            t[m] = 0; s_cat(t, "\342\200\246", sizeof t); tx(x, y + k * lh, lh, t, px, col, bold);
        } else txn(x, y + k * lh, lh, s + a, e - a, px, col, bold);
        k++; a = e;
    }
}
static void boton(int x, int y, int w, const char *s, int estilo)
{
    uint32_t bg = estilo == 1 ? 0xFF0E7490u : estilo == 2 ? 0xFF7F1D1Du : estilo < 0 ? 0xFF161D2Au : 0xFF1E293Bu;
    fb_fill_rrect_aa(x, y, w, 28, 8, bg);
    if (estilo == 0) fb_stroke_rrect_aa(x, y, w, 28, 8, 0xFF334155u);
    tx(x + (w - tw(s, 12, 1)) / 2, y, 28, s, 12, estilo < 0 ? C_FAINT : 0x00FFFFFF, 1);
}
static void hace(uint64_t ms, char *o, int cap)
{
    o[0] = 0;
    if (!ms) { s_cpy(o, "sin usar", cap); return; }
    uint64_t d = timer_get_uptime_ms() - ms;
    if (d < 60000) { s_cpy(o, "hace un momento", cap); return; }
    s_cpy(o, "hace ", cap);
    if (d < 3600000) { s_num(o, cap, d / 60000); s_cat(o, " min", cap); return; }
    s_num(o, cap, d / 3600000); s_cat(o, " h", cap);
}
static void hhmm(int64_t min, char *o, int cap)
{
    o[0] = 0; if (min <= 0) return;
    int64_t l = min + hora_desfase_en(min), d = l % 1440; if (d < 0) d += 1440;
    if (d / 60 < 10) s_cat(o, "0", cap);
    s_num(o, cap, (uint64_t)(d / 60)); s_cat(o, ":", cap); if (d % 60 < 10) s_cat(o, "0", cap); s_num(o, cap, (uint64_t)(d % 60));
}

void agentes_gui_init(void) { g_scroll = 0; }
uint32_t agentes_gui_version(void) { return g_ver + agentes_reg_version() * 7u; }
int agentes_gui_tick(void)
{
    uint32_t v = agentes_reg_version(); uint64_t t = timer_get_uptime_ms();
    if (v != g_visto) { g_visto = v; g_ver++; return 1; }
    if (gui_agent_busy() && t - g_t > 500) { g_t = t; g_ver++; return 1; }          /* el reloj de la mision en curso */
    if (t - g_t > 30000) { g_t = t; g_ver++; return 1; }                              /* "hace 5 min" */
    return 0;
}

/* ---------------------------------------------------------------- geometria */
struct lay { int x, y, w, h, ahora_y, ahora_h, esp_y, cols, cw, ch, filas, bajo_y, mis_x, mis_w, act_x, act_w, det_x; };
static void lay(struct lay *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->ahora_y = L->y + 64; L->ahora_h = 112;
    L->esp_y = L->ahora_y + L->ahora_h + 40;
    L->cols = L->w >= 1100 ? 4 : L->w >= 760 ? 3 : 2;
    L->cw = (L->w - 32 - (L->cols - 1) * 10) / L->cols; L->ch = 104;
    L->filas = (AGE_N + L->cols - 1) / L->cols;
    L->bajo_y = L->esp_y + L->filas * (L->ch + 10) + 30;
    if (L->w >= 900) { L->mis_x = L->x + 16; L->mis_w = (L->w - 42) * 58 / 100; L->act_x = L->mis_x + L->mis_w + 10; L->act_w = L->x + L->w - 16 - L->act_x; }
    else { L->mis_x = L->x + 16; L->mis_w = L->w - 32; L->act_x = 0; L->act_w = 0; }
    L->det_x = L->x + L->w - 16 - 16 - 110;
}

/* ---------------------------------------------------------------- pintar */
void agentes_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused)
{
    (void)focused;
    struct lay L; lay(&L, x, y, w, h);
    fb_fill_rect(x, y, w, h, C_WIN);
    tx(L.x + 18, L.y + 12, 28, "Agentes", 20, C_TXT, 1);
    tx(L.x + 118, L.y + 16, 24, "Qui\303\251n trabaja en lo que pides a SOMA, y c\303\263mo le ha ido", 12, C_DIM, 0);
    int busy = gui_agent_busy();
    struct agr *r = agentes_reg_entra();
    uint64_t ya = timer_get_uptime_ms();
    int activo = busy ? agr_activo(r) : -1;
    int sy = g_scroll;                                            /* todo lo de debajo del encabezado se desplaza */
    fb_set_clip(L.x, L.y + 52, L.w, L.h - 52 - 30);
    /* ---- ahora ---- */
    {
        int ay = L.ahora_y - sy;
        fb_fill_rrect_aa(L.x + 16, ay, L.w - 32, L.ahora_h, 12, 0xFF000000u | C_CARD);
        const struct agr_mision *m = agr_mision_i(r, 0);
        if (busy && m && m->estado == AGF_EN_CURSO) {
            uint32_t lat = (uint32_t)((ya / 300) % 2);
            fb_fill_circle(L.x + 34, ay + 26, 6, lat ? C_VERDE : 0x00166534);
            tx(L.x + 48, ay + 12, 28, "Trabajando", 14, C_VERDE, 1);
            char d[24]; agr_dura(ya - m->ini_ms, d, sizeof d); tx(L.x + 48 + tw("Trabajando", 14, 1) + 12, ay + 12, 28, d, 12, C_DIM, 0);
            lineas(L.x + 30, ay + 42, 20, m->texto, 13, 0, C_TXT, L.w - 60 - 130, 1);
            int px = L.x + 30;                                    /* la cadena de especialistas */
            for (int i = 0; i < m->npasos; i++) {
                int e = m->paso[i]; const char *nm = agr_nombre(e); int ww = tw(nm, 11, 1) + 22;
                if (px + ww > L.x + L.w - 40) break;
                int act = i == m->npasos - 1;
                fb_fill_rrect_aa(px, ay + 72, ww, 24, 12, act ? (0xFF000000u | COL[e]) : 0xFF1E293Bu);
                tx(px + 11, ay + 72, 24, nm, 11, act ? 0x00081018 : COL[e], 1);
                px += ww; if (i + 1 < m->npasos) { tx(px + 4, ay + 72, 24, "\342\200\272", 13, C_FAINT, 1); px += 16; }
            }
            boton(L.det_x, ay + 14, 110, "Detener", 2);
        } else {
            fb_fill_circle(L.x + 34, ay + 26, 6, 0x00475569);
            tx(L.x + 48, ay + 12, 28, "En reposo", 14, C_DIM, 1);
            if (m) {
                const char *st = m->estado == AGF_BIEN ? "termin\303\263 bien" : m->estado == AGF_ERROR ? "acab\303\263 con un error" : m->estado == AGF_DETENIDA ? "se detuvo" : "en curso";
                uint32_t sc = m->estado == AGF_BIEN ? C_VERDE : m->estado == AGF_ERROR ? C_ROJO : C_AMBAR;
                char s[200]; s_cpy(s, "\303\232ltima misi\303\263n: ", sizeof s); s_cat(s, m->texto, sizeof s);
                lineas(L.x + 30, ay + 42, 20, s, 13, 0, C_TXT, L.w - 60, 1);
                char d[24]; agr_dura((m->fin_ms ? m->fin_ms : ya) - m->ini_ms, d, sizeof d);
                char s2[220]; s_cpy(s2, st, sizeof s2); s_cat(s2, " en ", sizeof s2); s_cat(s2, d, sizeof s2);
                if (m->respuesta[0]) { s_cat(s2, "  \302\267  ", sizeof s2); s_cat(s2, m->respuesta, sizeof s2); }
                lineas(L.x + 30, ay + 66, 20, s2, 12, 0, sc, L.w - 60, 2);
            } else lineas(L.x + 30, ay + 42, 20, "Todav\303\255a no has pedido nada. Escr\303\255bele al orbe o pulsa Probar en un especialista.", 13, 0, C_DIM, L.w - 60, 2);
        }
    }
    /* ---- especialistas ---- */
    tx(L.x + 18, L.esp_y - 30 - sy, 24, "Especialistas", 14, C_TXT, 1);
    for (int e = 0; e < AGE_N; e++) {
        int cx = L.x + 16 + (e % L.cols) * (L.cw + 10), cy = L.esp_y + (e / L.cols) * (L.ch + 10) - sy;
        int on = e == activo;
        fb_fill_rrect_aa(cx, cy, L.cw, L.ch, 10, 0xFF000000u | C_CARD2);
        fb_stroke_rrect_aa(cx, cy, L.cw, L.ch, 10, on ? (0xFF000000u | COL[e]) : 0xFF1E293Bu);
        if (on) { uint32_t lat = (uint32_t)((ya / 300) % 2); fb_fill_circle(cx + 16, cy + 18, 5, lat ? COL[e] : 0x00334155); }
        else fb_fill_circle(cx + 16, cy + 18, 4, COL[e]);
        fb_set_clip(cx + 28, cy + 4, L.cw - 36, 28); tx(cx + 28, cy + 4, 28, agr_nombre(e), 13, C_TXT, 1); fb_set_clip(L.x, L.y + 52, L.w, L.h - 52 - 30);
        lineas(cx + 12, cy + 32, 16, agr_desc(e), 11, 0, C_DIM, L.cw - 24, 2);
        char u[64]; u[0] = 0;
        if (on) s_cpy(u, "trabajando ahora", sizeof u);
        else { s_num(u, sizeof u, r->usos[e]); s_cat(u, r->usos[e] == 1 ? " vez  \302\267  " : " veces  \302\267  ", sizeof u); char hh[32]; hace(r->ult_ms[e], hh, sizeof hh); s_cat(u, hh, sizeof u); }
        tx(cx + 12, cy + L.ch - 30, 24, u, 11, on ? COL[e] : C_FAINT, on);
        if (e != AGE_VOZ) boton(cx + L.cw - 12 - 70, cy + L.ch - 32, 70, "Probar", busy ? -1 : (g_hover_prob == e ? 1 : 0));
    }
    /* ---- misiones recientes y actividad ---- */
    int by = L.bajo_y - sy;
    tx(L.mis_x + 2, by - 30, 24, "Misiones recientes", 14, C_TXT, 1);
    for (int i = 0; i < r->nmis && i < 12; i++) {
        const struct agr_mision *m = agr_mision_i(r, i); int yy = by + i * 46;
        fb_fill_rrect_aa(L.mis_x, yy, L.mis_w, 40, 8, 0xFF000000u | C_CARD2);
        uint32_t sc = m->estado == AGF_BIEN ? C_VERDE : m->estado == AGF_ERROR ? C_ROJO : m->estado == AGF_DETENIDA ? C_AMBAR : C_ACC;
        fb_fill_rrect_aa(L.mis_x + 8, yy + 8, 4, 24, 2, 0xFF000000u | sc);
        char hh[8]; hhmm(m->ini_min, hh, sizeof hh); tx(L.mis_x + 20, yy, 40, hh, 11, C_DIM, 1);
        fb_set_clip(L.mis_x + 64, yy, L.mis_w - 64 - 160, 40); tx(L.mis_x + 64, yy + 2, 20, m->texto, 12, C_TXT, 0); fb_set_clip(L.x, L.y + 52, L.w, L.h - 52 - 30);
        char d[24]; agr_dura((m->fin_ms ? m->fin_ms : ya) - m->ini_ms, d, sizeof d); tx(L.mis_x + L.mis_w - 12 - tw(d, 11, 0), yy + 2, 20, d, 11, C_DIM, 0);
        int px = L.mis_x + 64;                                    /* quien trabajo */
        for (int e = 0; e < AGE_N; e++) if (m->esp & (1u << e)) { fb_fill_circle(px + 4, yy + 29, 4, COL[e]); px += 12; }
        if (m->respuesta[0]) { fb_set_clip(px + 4, yy + 20, L.mis_x + L.mis_w - 20 - px, 20); tx(px + 6, yy + 19, 20, m->respuesta, 11, C_FAINT, 0); fb_set_clip(L.x, L.y + 52, L.w, L.h - 52 - 30); }
    }
    if (!r->nmis) tx(L.mis_x + 4, by, 24, "Ninguna todav\303\255a en esta sesi\303\263n.", 12, C_FAINT, 0);
    if (L.act_w) {
        tx(L.act_x + 2, by - 30, 24, "Actividad", 14, C_TXT, 1);
        fb_fill_rrect_aa(L.act_x, by, L.act_w, 12 * 46 - 6, 8, 0xFF000000u | C_CARD2);
        for (int i = 0; i < r->nact && i < 24; i++) {
            const struct agr_act *a = agr_act_i(r, i); int yy = by + 8 + i * 22;
            if (yy > by + 12 * 46 - 30) break;
            char hh[8]; hhmm(a->min, hh, sizeof hh);
            fb_fill_circle(L.act_x + 14, yy + 11, 3, COL[a->esp < AGE_N ? a->esp : AGE_HERRAMIENTAS]);
            tx(L.act_x + 24, yy, 22, hh, 11, C_FAINT, 0);
            fb_set_clip(L.act_x + 66, yy, L.act_w - 76, 22); tx(L.act_x + 66, yy, 22, a->txt, 11, C_DIM, 0); fb_set_clip(L.x, L.y + 52, L.w, L.h - 52 - 30);
        }
        if (!r->nact) tx(L.act_x + 14, by + 8, 22, "Las \303\263rdenes y b\303\272squedas de los agentes salen aqu\303\255.", 11, C_FAINT, 0);
    }
    agentes_reg_sale();
    fb_reset_clip();
    /* pie */
    {   const char *s = g_msg[0] ? g_msg : "Rueda del rat\303\263n: m\303\241s abajo  \302\267  la orden agentes da lo mismo en texto";
        tx(L.x + 18, L.y + L.h - 28, 24, s, 11, g_msg[0] ? g_msg_col : C_FAINT, 0); }
}

/* ---------------------------------------------------------------- raton */
static int prob_de(const struct lay *L, int mx, int my)
{
    for (int e = 0; e < AGE_N; e++) {
        if (e == AGE_VOZ) continue;
        int cx = L->x + 16 + (e % L->cols) * (L->cw + 10), cy = L->esp_y + (e / L->cols) * (L->ch + 10) - g_scroll;
        int bx = cx + L->cw - 12 - 70, by = cy + L->ch - 32;
        if (mx >= bx && mx < bx + 70 && my >= by && my < by + 28) return e;
    }
    return -1;
}
int agentes_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct lay L; lay(&L, x, y, w, h);
    g_ver++; g_msg[0] = 0;
    if (my < L.y + 52) return 1;
    int busy = gui_agent_busy();
    int ay = L.ahora_y - g_scroll;
    if (busy && mx >= L.det_x && mx < L.det_x + 110 && my >= ay + 14 && my < ay + 42) {
        if (gui_agent_cancel()) { s_cpy(g_msg, "Misi\303\263n detenida.", sizeof g_msg); g_msg_col = C_AMBAR; }
        return 1;
    }
    int e = prob_de(&L, mx, my);
    if (e >= 0) {
        if (busy) { s_cpy(g_msg, "Ya hay una misi\303\263n en marcha: espera a que acabe o det\303\251nla.", sizeof g_msg); g_msg_col = C_AMBAR; return 1; }
        int rr = gui_agent_submit_chat(agr_ejemplo(e));          /* va al orbe como cualquier peticion */
        if (rr == 0) { s_cpy(g_msg, "Enviado: \"", sizeof g_msg); s_cat(g_msg, agr_ejemplo(e), sizeof g_msg); s_cat(g_msg, "\" (la respuesta sale en el orbe)", sizeof g_msg); g_msg_col = C_VERDE; }
        else { s_cpy(g_msg, "No se pudo enviar ahora.", sizeof g_msg); g_msg_col = C_ROJO; }
        return 1;
    }
    return 1;
}
int agentes_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel)
{
    struct lay L; lay(&L, x, y, w, h);
    int r = 0;
    if (wheel) {
        int total = L.bajo_y + 12 * 46 - L.y, max = total - L.h + 60; if (max < 0) max = 0;
        g_scroll += wheel * 40; if (g_scroll < 0) g_scroll = 0; if (g_scroll > max) g_scroll = max;
        r = 1;
    }
    int hv = prob_de(&L, mx, my);
    if (hv != g_hover_prob) { g_hover_prob = hv; r = 1; }
    if (r) g_ver++;
    return r;
}
int agentes_gui_key(char c)
{
    if (c == 27) return 0;
    g_ver++;
    return 1;
}
