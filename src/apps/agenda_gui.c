/* SOMA_272_AGENDA: ver agenda_gui.h. Minimalista: cabecera (navegacion + vistas), cuerpo y una linea de texto para apuntar en lenguaje
 * natural ("el miercoles dia 6 a las 13 horas partido de futbol"). Las zonas pulsables se anotan al pintar (g_hit) y el clic las lee. */
#include "agenda_gui.h"
#include "agenda.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "clipboard.h"
#include "idt.h"
#if defined(__x86_64__)
#include "voz_sys.h"
#endif

#define KC_WIN  0x000C111B
#define KC_BAR  0x00131A27
#define KC_LINE 0x00212B3B
#define KC_CARD 0x00111827
#define KC_TXT  0x00E5EAF2
#define KC_DIM  0x008592A8
#define KC_FAINT 0x00566379
#define KC_ACC  0x0038BDF8
#define KC_SEL  0x001A3558
static const uint32_t CAT_COL[AG_NCAT] = { 0x0038BDF8, 0x00A78BFA, 0x0034D399, 0x00F472B6, 0x00F59E0B, 0x0022D3EE, 0x00FB923C, 0x0094A3B8 };

enum { V_MES, V_SEM, V_DIA };
enum { H_NADA, H_ANT, H_SIG, H_HOY, H_VMES, H_VSEM, H_VDIA, H_DIA, H_EV, H_BORRAR, H_OK };
struct hit { int x, y, w, h, tipo, val; };
#define MAXH 120
static struct hit g_hit[MAXH]; static int g_nh;
static int g_vista = V_MES, g_init = 0;
static int64_t g_dia;                 /* dia seleccionado (dias desde 1970) */
static int g_sel_ev = -1;             /* id de evento seleccionado */
static char g_in[160]; static int g_nin;
static char g_msg[160];
static uint32_t g_ver = 1;
uint32_t agenda_gui_version(void) { return g_ver + agenda_version(); }

static int sl(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void cat(char *d, const char *s, int max) { int i = sl(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void cpy(char *d, const char *s, int max) { d[0] = 0; cat(d, s, max); }
static void num(char *d, int max, int v) { char t[16]; int n = 0; if (v < 0) { cat(d, "-", max); v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15); char r[16]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = sl(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int bold)
{
    int n = sl(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void hit(int x, int y, int w, int h, int tipo, int val) { if (g_nh < MAXH) { g_hit[g_nh].x = x; g_hit[g_nh].y = y; g_hit[g_nh].w = w; g_hit[g_nh].h = h; g_hit[g_nh].tipo = tipo; g_hit[g_nh].val = val; g_nh++; } }
static void boton(int x, int y, int w, int h, const char *t, int on, int tipo, int val)
{
    fb_fill_rrect_aa(x, y, w, h, 8, on ? 0xFF1A3558u : 0xFF151E2Eu);
    tx(x + (w - tw(t, 12, on)) / 2, y, h, t, 12, on ? KC_ACC : KC_DIM, on);
    hit(x, y, w, h, tipo, val);
}

void agenda_gui_init(void)
{
    if (g_init) return;
    g_init = 1; g_dia = ag_dia_de(agenda_ahora()); g_vista = V_MES; g_nin = 0; g_in[0] = 0; g_msg[0] = 0; g_sel_ev = -1;
}
static void mover(int n)             /* n periodos hacia delante o atras segun la vista */
{
    int y, m, d; ag_civil(g_dia, &y, &m, &d);
    if (g_vista == V_MES) { m += n; while (m > 12) { m -= 12; ++y; } while (m < 1) { m += 12; --y; } if (d > ag_dias_mes(y, m)) d = ag_dias_mes(y, m); g_dia = ag_dias(y, m, d); }
    else if (g_vista == V_SEM) g_dia += 7 * n; else g_dia += n;
    g_sel_ev = -1; g_ver++;
}
static void fila_evento(int x, int y, int w, int h, const struct ag_cal *c, const struct ag_oc *o, int64_t ahora)
{
    const struct ag_ev *e = &c->ev[o->idx]; int sel = (int)e->id == g_sel_ev; char hr[16], t[AG_TIT + 24];
    fb_fill_rrect_aa(x, y, w, h - 4, 8, sel ? 0xFF1A3558u : 0xFF111827u); fb_fill_rect((uint32_t)(x + 6), (uint32_t)(y + 6), 4, (uint32_t)(h - 16), CAT_COL[e->cat % AG_NCAT]);
    if (e->todo_dia) cpy(hr, "Todo el d\303\255a", sizeof hr); else ag_hora_txt(o->ini, hr, sizeof hr);
    tx(x + 18, y, h - 4, hr, 12, o->ini + e->dur <= ahora && !e->todo_dia ? KC_FAINT : KC_ACC, 1);
    t[0] = 0; cat(t, e->titulo, sizeof t); if (e->rep) cat(t, "  \342\206\273", sizeof t); if (e->lugar[0]) { cat(t, "  \302\267  ", sizeof t); cat(t, e->lugar, sizeof t); }
    fb_set_clip(x + 100, y, w - 106, h); tx(x + 100, y, h - 4, t, 13, KC_TXT, 0); fb_reset_clip();
    hit(x, y, w, h - 4, H_EV, (int)e->id);
}
static void lista_dias(int x, int y, int w, int h, int64_t d0, int ndias, int64_t ahora, int con_cab)
{
    static struct ag_oc oc[120]; const struct ag_cal *c = agenda_cal();
    int n = ag_agenda_rango(c, d0 * 1440, (d0 + ndias) * 1440, oc, 120), yy = y, vacio = 1;
    for (int d = 0; d < ndias; d++) {
        int64_t dia = d0 + d; int k0 = yy; int tengo = 0;
        for (int i = 0; i < n; i++) if (ag_dia_de(oc[i].ini) == dia) tengo = 1;
        if (!tengo && ndias > 1) continue;
        vacio = 0;
        if (con_cab) { char f[64]; ag_fecha_txt(dia * 1440, ahora, f, sizeof f); if (f[0] >= 'a' && f[0] <= 'z') f[0] = (char)(f[0] - 32); fb_fill_rect((uint32_t)x, (uint32_t)yy, (uint32_t)w, 1, KC_LINE); tx(x, yy + 2, 26, f, 13, dia == ag_dia_de(ahora) ? KC_ACC : KC_DIM, 1); yy += 28; }
        for (int i = 0; i < n && yy + 34 <= y + h; i++) if (ag_dia_de(oc[i].ini) == dia) { fila_evento(x, yy, w, 36, c, &oc[i], ahora); yy += 38; }
        (void)k0;
    }
    if (vacio) tx(x, y + 8, 30, ndias == 1 ? "Nada este d\303\255a. Escribe abajo para apuntar algo." : "No hay eventos en estos d\303\255as.", 13, KC_FAINT, 0);
}
static void vista_mes(int x, int y, int w, int h, int64_t ahora)
{
    int yy, mm, dd; ag_civil(g_dia, &yy, &mm, &dd);
    int64_t p = ag_dias(yy, mm, 1); int off = ag_dia_semana(p); int64_t ini = p - off;
    int filas = (off + ag_dias_mes(yy, mm) + 6) / 7, cw = w / 7, rh = (h - 24) / filas; if (rh < 40) rh = 40;
    static struct ag_oc oc[400]; const struct ag_cal *c = agenda_cal();
    int n = ag_agenda_rango(c, ini * 1440, (ini + filas * 7) * 1440, oc, 400);
    for (int i = 0; i < 7; i++) { char t[4]; t[0] = ag_nombre_dia(i)[0]; t[1] = ag_nombre_dia(i)[1]; t[2] = ag_nombre_dia(i)[2]; t[3] = 0; tx(x + i * cw + 8, y, 22, t, 11, KC_FAINT, 1); }
    for (int f = 0; f < filas; f++) for (int i = 0; i < 7; i++) {
        int64_t dia = ini + f * 7 + i; int cy2, cm, cd; ag_civil(dia, &cy2, &cm, &cd); int cx = x + i * cw, cy = y + 24 + f * rh;
        int fuera = cm != mm, hoy = dia == ag_dia_de(ahora), sel = dia == g_dia;
        fb_fill_rrect_aa(cx + 2, cy + 2, cw - 4, rh - 4, 8, sel ? 0xFF1A3558u : hoy ? 0xFF16283Fu : 0xFF111827u);
        char t[8]; t[0] = 0; num(t, sizeof t, cd); tx(cx + 10, cy + 3, 20, t, 12, hoy ? KC_ACC : fuera ? KC_FAINT : KC_TXT, hoy);
        int k = 0, mas = 0;
        for (int j = 0; j < n; j++) if (ag_dia_de(oc[j].ini) == dia || (c->ev[oc[j].idx].todo_dia && c->ev[oc[j].idx].dur > 1440 && dia >= ag_dia_de(oc[j].ini) && dia * 1440 < oc[j].ini + c->ev[oc[j].idx].dur)) {
            if (cy + 26 + k * 16 + 14 > cy + rh - 2) { mas++; continue; }
            const struct ag_ev *e = &c->ev[oc[j].idx]; fb_fill_rrect_aa(cx + 8, cy + 26 + k * 16, cw - 16, 14, 4, 0xFF000000u | CAT_COL[e->cat % AG_NCAT]);
            fb_set_clip(cx + 10, cy + 26 + k * 16, cw - 20, 14); tx(cx + 12, cy + 26 + k * 16, 14, e->titulo, 10, 0x000C111B, 1); fb_reset_clip(); k++;
        }
        if (mas) { char m[12] = "+"; num(m, sizeof m, mas); tx(cx + cw - 30, cy + 3, 20, m, 10, KC_DIM, 0); }
        hit(cx, cy, cw, rh, H_DIA, (int)dia);
    }
}
void agenda_gui_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused; agenda_gui_init(); g_nh = 0;
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh; int64_t ahora = agenda_ahora();
    fb_fill_rect(wx, wy, ww, wh, KC_WIN); fb_fill_rect(wx, wy, ww, 56, KC_BAR); fb_fill_rect(wx, wy + 56, ww, 1, KC_LINE);
    int yy, mm, dd; ag_civil(g_dia, &yy, &mm, &dd); char tit[64]; tit[0] = 0;
    if (g_vista == V_DIA) { num(tit, sizeof tit, dd); cat(tit, " de ", sizeof tit); } cat(tit, ag_nombre_mes(mm), sizeof tit); cat(tit, " ", sizeof tit); num(tit, sizeof tit, yy);
    tit[0] = (char)(tit[0] >= 'a' && tit[0] <= 'z' ? tit[0] - 32 : tit[0]);
    tx(x + 20, y + 4, 48, tit, 20, KC_TXT, 1);
    int bx = x + 20 + tw(tit, 20, 1) + 24;
    boton(bx, y + 14, 30, 28, "<", 0, H_ANT, 0); boton(bx + 36, y + 14, 30, 28, ">", 0, H_SIG, 0); boton(bx + 72, y + 14, 56, 28, "Hoy", 0, H_HOY, 0);
    int vx = x + w - 3 * 76 - 14;
    boton(vx, y + 14, 70, 28, "Mes", g_vista == V_MES, H_VMES, 0); boton(vx + 76, y + 14, 70, 28, "Semana", g_vista == V_SEM, H_VSEM, 0); boton(vx + 152, y + 14, 70, 28, "D\303\255a", g_vista == V_DIA, H_VDIA, 0);
    int by = y + 68, bh = h - 68 - 96, bw = w - 40;
    fb_set_clip(x, by, w, bh + 4);
    if (g_vista == V_MES) vista_mes(x + 20, by, bw, bh, ahora);
    else if (g_vista == V_SEM) { int64_t lun = g_dia - ag_dia_semana(g_dia); lista_dias(x + 20, by, bw, bh, lun, 7, ahora, 1); }
    else lista_dias(x + 20, by, bw, bh, g_dia, 1, ahora, 0);
    fb_reset_clip();
    /* pie: detalle del evento elegido + linea de texto */
    int fy = y + h - 88; fb_fill_rect(wx, (uint32_t)fy, ww, 1, KC_LINE);
    struct ag_ev *e = g_sel_ev > 0 ? ag_busca_id(agenda_cal(), (uint32_t)g_sel_ev) : 0;
    if (e) {
        char d[240]; ag_describe(e, e->ini, ahora, d, sizeof d); fb_set_clip(x + 20, fy, w - 200, 34); tx(x + 20, fy + 2, 30, d, 12, KC_DIM, 0); fb_reset_clip();
        boton(x + w - 110, fy + 4, 90, 26, "Borrar", 0, H_BORRAR, g_sel_ev);
    } else if (g_msg[0]) tx(x + 20, fy + 2, 30, g_msg, 12, KC_ACC, 0);
    else tx(x + 20, fy + 2, 30, "Escribe en lenguaje natural: \302\253el mi\303\251rcoles d\303\255a 6 a las 13 horas partido con los amigos\302\273", 12, KC_FAINT, 0);
    fb_fill_rrect_aa(x + 20, fy + 38, w - 130, 38, 10, 0xFF111827u); fb_fill_rrect_aa(x + 21, fy + 39, w - 132, 36, 9, 0xFF0F1520u);
    char sh[170]; cpy(sh, g_in, sizeof sh); cat(sh, "_", sizeof sh);
    if (g_nin) tx(x + 34, fy + 38, 38, sh, 14, KC_TXT, 0); else tx(x + 34, fy + 38, 38, "Apuntar\342\200\246", 14, KC_FAINT, 0);
    boton(x + w - 100, fy + 38, 80, 38, "Apuntar", g_nin > 0, H_OK, 0);
}
int agenda_gui_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh; g_ver++;
    for (int i = g_nh - 1; i >= 0; i--) {
        struct hit *t = &g_hit[i]; if (mx < t->x || mx >= t->x + t->w || my < t->y || my >= t->y + t->h) continue;
        g_msg[0] = 0;
        switch (t->tipo) {
        case H_ANT: mover(-1); break; case H_SIG: mover(1); break;
        case H_HOY: g_dia = ag_dia_de(agenda_ahora()); g_sel_ev = -1; break;
        case H_VMES: g_vista = V_MES; g_sel_ev = -1; break; case H_VSEM: g_vista = V_SEM; g_sel_ev = -1; break; case H_VDIA: g_vista = V_DIA; g_sel_ev = -1; break;
        case H_DIA: if (g_dia == t->val) g_vista = V_DIA; g_dia = t->val; g_sel_ev = -1; break;
        case H_EV: g_sel_ev = t->val; break;
        case H_BORRAR: if (agenda_borra((uint32_t)t->val) == 0) cpy(g_msg, "Evento borrado.", sizeof g_msg); g_sel_ev = -1; break;
        case H_OK: agenda_gui_handle_key(10); break;
        }
        return 0;
    }
    return 0;
}
void agenda_gui_handle_key(char ch)
{
    int c = (uint8_t)ch; g_ver++;
    if (c == 10 || c == 13) {
        if (!g_nin) return;
        char r[300]; int id = agenda_anade_texto(g_in, r, sizeof r);
        if (id > 0) { cpy(g_msg, r, sizeof g_msg); g_nin = 0; g_in[0] = 0; g_sel_ev = -1; }
        else cpy(g_msg, id == -2 ? "La agenda est\303\241 llena." : id == -3 ? "No se pudo guardar." : "No entiendo cu\303\241ndo es: di el d\303\255a o la hora.", sizeof g_msg);
        return;
    }
    if (c == 8 || c == 127) { if (g_nin) g_in[--g_nin] = 0; return; }
    if (c == 9) { g_vista = (g_vista + 1) % 3; g_sel_ev = -1; return; }
    if (!g_nin) {                                         /* con la linea vacia, las flechas navegan */
        if (c == 2) { mover(-1); return; } if (c == 6) { mover(1); return; }
        if (c == 16) { g_dia -= g_vista == V_MES ? 7 : 1; return; } if (c == 14) { g_dia += g_vista == V_MES ? 7 : 1; return; }
    }
    if (c >= 32 && g_nin + 1 < (int)sizeof g_in) { g_in[g_nin++] = ch; g_in[g_nin] = 0; }
}
/* ---- avisos activos: se llaman desde el bucle de la GUI aunque la ventana este cerrada */
int agenda_gui_aviso(void)
{
    char t[260]; if (!agenda_tick(t, sizeof t)) return 0;
    struct agenda_conf c; agenda_conf_get(&c);
    if (c.visual) clip_toast_ms(t, 9000);
#if defined(__x86_64__)
    if (c.voz) (void)voz_speak_start(t, (uint32_t)sl(t));
#endif
    g_ver++; return 1;
}
void agenda_gui_probar(void)
{
    struct agenda_conf c; agenda_conf_get(&c);
    if (c.visual) clip_toast_ms("Aviso de prueba: en 10 minutos, reuni\303\263n de equipo.", 6000);
#if defined(__x86_64__)
    if (c.voz) (void)voz_speak_start("En 10 minutos: reuni\303\263n de equipo.", 35);
#endif
}
