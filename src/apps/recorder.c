/* SOMA_177_T480S: aplicacion Grabadora. Ver recorder.h */
#include <stdint.h>
#include <stddef.h>
#include "recorder.h"
#include "micrec.h"
#include "fb.h"
#include "gui_font.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "idt.h"
#include "console.h"
#include "sysicon.h"     /* SOMA_210_SYSICON */

#define MAXREC   48
#define LIST_W   232
#define ACCENT   0x00FB7185      /* SOMA_210: color de la aplicacion (el del icono del escritorio) */
#define PLAYC    0x0038BDF8

enum { B_REC = 1, B_PLAY, B_NEW, B_SAVE, B_MON, B_SRC_PREV, B_SRC_NEXT, B_AUTO, B_G_DN, B_G_UP, B_S_DN, B_S_UP, B_RATE, B_DEL, B_LIST };

struct btn { int id, x, y, w, h; };
struct lay {
    int lx, ly, lw, lh;                 /* barra lateral con la lista (lw = 0 si no cabe) */
    int row_y, row_h, rows;             /* filas de la lista */
    int mx, mw;                         /* columna principal */
    int hdr_y, card_y, card_h, meter_y, wave_y, wave_h, tr_cy, status_y;
    int rnd[16];                        /* radio de los botones redondos (0 = boton normal) */
    struct btn b[16]; int nb;
};

static const uint32_t RATES[] = { 8000, 16000, 22050, 44100, 48000 };
static const char *const RATE_NAME[] = { "8 kHz (llamada)", "16 kHz (voz)", "22 kHz", "44,1 kHz (musica)", "48 kHz (maxima)" };
static int rate_i = 1;

static char names[MAXREC][40]; static uint32_t sizes[MAXREC];
static int nrec, sel = -1, scroll, scanned, del_armed, discard_armed;
static char umsg[96];
static uint64_t last_tick, umsg_at;
static int last_mode = -1, last_level = -1; static uint32_t last_ms = 0xFFFFFFFFu;

static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void put_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void put_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } put_s(o, max, p, b + i); }
static int has_wav(const char *n) { uint32_t l = slen(n); return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'w' && (n[l - 2] | 32) == 'a' && (n[l - 1] | 32) == 'v'; }
static void setu(const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < sizeof umsg) { umsg[i] = s[i]; i++; } umsg[i] = 0; umsg_at = timer_get_uptime_ms(); }
static void fmt_time(char *o, uint32_t ms)
{
    uint32_t s = ms / 1000, m = s / 60, t = (ms % 1000) / 100; s %= 60; if (m > 99) m = 99;
    o[0] = (char)('0' + m / 10); o[1] = (char)('0' + m % 10); o[2] = ':'; o[3] = (char)('0' + s / 10); o[4] = (char)('0' + s % 10); o[5] = '.'; o[6] = (char)('0' + t); o[7] = 0;
}
static uint32_t isqrt(uint32_t v) { uint32_t r = 0, bit = 1u << 30; while (bit > v) bit >>= 2; while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; } return r; }

static void rescan(void)
{
    scanned = 1; nrec = 0; del_armed = 0;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return;
    uint32_t dir;
    if (bigfs_lookup(fs, "/grabaciones", &dir) != (int)BIGFS_T_DIR) return;
    uint32_t cur = 0; struct bigfs_stat st;
    while (nrec < MAXREC && bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || !has_wav(st.name)) continue;
        uint32_t i = 0; while (st.name[i] && i + 1 < sizeof names[0]) { names[nrec][i] = st.name[i]; i++; } names[nrec][i] = 0;
        sizes[nrec] = (uint32_t)st.size; nrec++;
    }
    for (int a = 1; a < nrec; ++a) {                              /* orden alfabetico (grab_001, grab_002...) */
        char t[40]; uint32_t sz = sizes[a]; int b = a - 1;
        for (uint32_t i = 0; i < 40; ++i) t[i] = names[a][i];
        while (b >= 0) {
            int gt = 0;
            for (uint32_t i = 0; i < 40; ++i) {
                if (names[b][i] != t[i]) { gt = names[b][i] > t[i]; break; }
                if (!t[i]) break;
            }
            if (!gt) break;
            for (uint32_t i = 0; i < 40; ++i) names[b + 1][i] = names[b][i];
            sizes[b + 1] = sizes[b];
            b--;
        }
        for (uint32_t i = 0; i < 40; ++i) names[b + 1][i] = t[i];
        sizes[b + 1] = sz;
    }
    if (sel >= nrec) sel = nrec - 1;
}
static void full_path(int i, char *out, uint32_t max) { uint32_t p = 0; put_s(out, max, &p, "/grabaciones/"); put_s(out, max, &p, names[i]); }

void recorder_init(void) { rescan(); umsg[0] = 0; discard_armed = 0; }

static void addb(struct lay *L, int id, int x, int y, int w, int h) { if (L->nb < 16) { L->b[L->nb].id = id; L->b[L->nb].x = x; L->b[L->nb].y = y; L->b[L->nb].w = w; L->b[L->nb].h = h; L->rnd[L->nb] = 0; L->nb++; } }
static void addr(struct lay *L, int id, int cx, int cy, int r) { addb(L, id, cx - r, cy - r, 2 * r, 2 * r + 22); L->rnd[L->nb - 1] = r; }

/* SOMA_210: barra lateral con las grabaciones, cabecera, tarjeta de entrada, medidor, onda y botones de transporte redondos */
static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct lay *L)
{
    L->nb = 0;
    L->lw = ww >= 700 ? LIST_W : 0;
    L->lx = (int)wx; L->ly = (int)wy; L->lh = (int)wh;
    L->row_y = (int)wy + 56; L->row_h = 34;
    L->rows = ((int)wh - 56 - 66) / L->row_h; if (L->rows < 1) L->rows = 1;
    L->mx = (int)wx + L->lw + 20;
    L->mw = (int)wx + (int)ww - 20 - L->mx; if (L->mw < 300) L->mw = 300;
    int mx = L->mx, mw = L->mw;
    L->hdr_y = (int)wy + 18;
    L->status_y = (int)wy + (int)wh - 32;
    L->tr_cy = L->status_y - 22 - 32 - 10;
    L->card_y = (int)wy + 78; L->card_h = 92;
    L->meter_y = L->card_y + L->card_h + 14;
    L->wave_y = L->meter_y + 46;
    L->wave_h = L->tr_cy - 32 - 16 - L->wave_y; if (L->wave_h < 70) L->wave_h = 70;
    addb(L, B_SRC_NEXT, mx + mw - 32, L->hdr_y + 7, 32, 30);
    addb(L, B_SRC_PREV, mx + mw - 70, L->hdr_y + 7, 32, 30);
    addb(L, B_AUTO, mx + mw - 70 - 8 - 84, L->hdr_y + 7, 84, 30);
    int r1 = L->card_y + 14;
    addb(L, B_G_DN, mx + 104, r1, 30, 30);
    addb(L, B_G_UP, mx + 194, r1, 30, 30);
    addb(L, B_S_DN, mx + 318, r1, 30, 30);
    addb(L, B_S_UP, mx + 408, r1, 30, 30);
    addb(L, B_RATE, mx + 16, L->card_y + 50, mw - 32 < 320 ? mw - 32 : 320, 30);
    int cx = mx + mw / 2, sp = mw / 5; if (sp > 96) sp = 96;
    addr(L, B_NEW,  cx - 2 * sp, L->tr_cy, 20);
    addr(L, B_PLAY, cx - sp,     L->tr_cy, 24);
    addr(L, B_REC,  cx,          L->tr_cy, 32);
    addr(L, B_MON,  cx + sp,     L->tr_cy, 24);
    addr(L, B_SAVE, cx + 2 * sp, L->tr_cy, 20);
    if (L->lw) addb(L, B_DEL, L->lx + 14, L->ly + L->lh - 50, L->lw - 28, 34);
}

static void draw_list(const struct lay *L)
{
    fb_fill_rect((uint32_t)L->lx, (uint32_t)L->ly, (uint32_t)L->lw, (uint32_t)L->lh, SI_SIDE);
    fb_fill_rect((uint32_t)(L->lx + L->lw - 1), (uint32_t)L->ly, 1, (uint32_t)L->lh, SI_LINE);
    si_text(L->lx + 18, L->ly + 16, 24, "Grabaciones", 14, SI_TXT, 1);
    if (nrec) { char n[12]; uint32_t p = 0; put_d(n, sizeof n, &p, (uint64_t)nrec); si_chip(L->lx + L->lw - 18 - si_chip_w(20, n, 0), L->ly + 18, 20, n, SI_DIM, 0); }
    if (nrec == 0) {
        int cy = L->row_y + 60;
        si_icon(SI_WAVE, L->lx + L->lw / 2, cy, 40, SI_FAINT);
        si_text_c(L->lx, cy + 30, L->lw, 20, "Aun no hay grabaciones", 13, SI_DIM, 1);
        si_text_c(L->lx, cy + 50, L->lw, 18, "Graba y pulsa Guardar", 12, SI_FAINT, 0);
        return;
    }
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + L->rows) scroll = sel - L->rows + 1;
    if (scroll < 0) scroll = 0;
    for (int r = 0; r < L->rows && scroll + r < nrec; ++r) {
        int i = scroll + r, y = L->row_y + r * L->row_h;
        int on = i == sel;
        if (on) {
            fb_fill_rrect_aa(L->lx + 8, y, L->lw - 16, L->row_h - 4, 8, si_mix(SI_SIDE, ACCENT, 46));
            fb_fill_rrect_aa(L->lx + 8, y + 8, 3, L->row_h - 20, 1, ACCENT);
        }
        si_icon(SI_WAVE, L->lx + 30, y + (L->row_h - 4) / 2, 16, on ? ACCENT : SI_FAINT);
        char sz[16]; uint32_t q = 0; put_d(sz, sizeof sz, &q, sizes[i] / 1024u); put_s(sz, sizeof sz, &q, " KB");
        int szw = si_text_w(sz, 11, 0);
        si_text_fit(L->lx + 46, y, L->row_h - 4, names[i], L->lw - 46 - 22 - szw, 13, on ? 0x00FFFFFFu : 0x00CBD5E1u, on);
        si_text_r(L->lx + L->lw - 18, y, L->row_h - 4, sz, 11, SI_FAINT, 0);
    }
}

void recorder_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    if (!scanned) rescan();
    struct micrec_info in; micrec_info(&in);
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    fb_fill_rect(wx, wy, ww, wh, SI_WIN);
    if (L.lw) draw_list(&L);
    int mx = L.mx, mw = L.mw;

    /* cabecera */
    {
        char sub[120]; uint32_t p = 0;
        if (!in.available) put_s(sub, sizeof sub, &p, "Sin entrada de microfono");
        else {
            char d[96]; micrec_source_name(in.src < 0 ? 0 : in.src, d, sizeof d);
            put_s(sub, sizeof sub, &p, "Fuente "); put_d(sub, sizeof sub, &p, (uint64_t)(in.src + 1)); put_s(sub, sizeof sub, &p, " de ");
            put_d(sub, sizeof sub, &p, (uint64_t)in.nsrc); put_s(sub, sizeof sub, &p, "  -  "); put_s(sub, sizeof sub, &p, d);
        }
        si_header(mx, L.hdr_y, mw - 180, in.available ? SI_MIC : SI_MIC_OFF, ACCENT, "Grabadora", sub);
    }

    /* tarjeta de entrada */
    si_card(mx, L.card_y, mw, L.card_h, SI_CARD);
    if (!in.available) {
        si_icon(SI_MIC_OFF, mx + 34, L.card_y + L.card_h / 2, 26, 0x00FB7185u);
        si_text(mx + 60, L.card_y + 18, 22, "No hay entrada de microfono", 14, SI_TXT, 1);
        si_text_fit(mx + 60, L.card_y + 42, 20, "Comprueba soma.audio, soma.mic o que el equipo tenga codec de audio.", mw - 76, 12, SI_DIM, 0);
    } else {
        int r1 = L.card_y + 14;
        char g[12]; uint32_t p = 0; put_d(g, sizeof g, &p, (uint64_t)in.hw_gain); put_s(g, sizeof g, &p, " %");
        char s[12]; p = 0; put_s(s, sizeof s, &p, "x"); put_d(s, sizeof s, &p, (uint64_t)(in.sw_x10 / 10)); put_s(s, sizeof s, &p, ","); put_d(s, sizeof s, &p, (uint64_t)(in.sw_x10 % 10));
        si_text(mx + 16, r1, 30, "Ganancia", 13, SI_DIM, 0);
        si_text_c(mx + 134, r1, 60, 30, g, 13, SI_TXT, 1);
        si_text(mx + 250, r1, 30, "Digital", 13, SI_DIM, 0);
        si_text_c(mx + 348, r1, 60, 30, s, 13, SI_TXT, 1);
    }

    /* medidor */
    {
        int active = in.mode == MR_REC || in.mode == MR_MON || in.mode == MR_PROBE;
        si_text(mx, L.meter_y, 18, "Nivel de entrada", 12, SI_DIM, 1);
        si_text_r(mx + mw, L.meter_y, 18, active ? "en directo" : "pulsa Nivel o Grabar para medir", 12, active ? SI_GREEN : SI_FAINT, 0);
        si_meter(mx, L.meter_y + 24, mw, 10, active ? in.level : 0, in.hold, active);
    }

    /* onda, estado y tiempo */
    {
        int y0 = L.wave_y, h0 = L.wave_h;
        si_card(mx, y0, mw, h0, 0x000A0F18u);
        const char *st = "Listo"; uint32_t sc = SI_DIM; int dot = 0;
        if (in.mode == MR_REC) { st = "Grabando"; sc = SI_RED; dot = 1; }
        else if (in.mode == MR_PLAY) { st = "Reproduciendo"; sc = PLAYC; dot = 1; }
        else if (in.mode == MR_MON) { st = "Midiendo el nivel"; sc = SI_GREEN; dot = 1; }
        else if (in.mode == MR_PROBE) { st = "Probando entradas"; sc = SI_AMBER; dot = 1; }
        else if (in.unsaved) { st = "Sin guardar"; sc = SI_AMBER; }
        si_chip(mx + 14, y0 + 14, 24, st, sc, dot);
        char t1[10], t2[10], ln[40]; uint32_t p = 0;
        fmt_time(t1, in.mode == MR_PLAY ? in.play_ms : in.ms); fmt_time(t2, in.ms);
        put_s(ln, sizeof ln, &p, t1); if (in.mode == MR_PLAY) { put_s(ln, sizeof ln, &p, " / "); put_s(ln, sizeof ln, &p, t2); }
        si_text_r(mx + mw - 16, y0 + 8, 34, ln, 24, in.mode == MR_REC ? 0x00FDA4AFu : SI_TXT, 1);
        char r[48]; p = 0; put_d(r, sizeof r, &p, in.rate); put_s(r, sizeof r, &p, " Hz  -  mono  -  16 bits");
        si_text_r(mx + mw - 16, y0 + 42, 16, r, 11, SI_FAINT, 0);
        int ax = mx + 16, ay = y0 + 66, aw = mw - 32, ah = h0 - 80;
        if (ah >= 20) {
            int mid = ay + ah / 2;
            fb_fill_rect((uint32_t)ax, (uint32_t)mid, (uint32_t)aw, 1, 0x001C2638u);
            uint8_t cols[256]; uint32_t ncol = (uint32_t)aw / 3; if (ncol > 256) ncol = 256;
            uint32_t used = micrec_waveform(cols, ncol);
            uint32_t wc = in.mode == MR_REC ? ACCENT : PLAYC;
            for (uint32_t c = 0; c < used; ++c) {
                int hh = (int)(isqrt((uint32_t)cols[c] * 255u) * (uint32_t)(ah / 2 - 2) / 255u); if (hh < 1) hh = 1;
                uint32_t col = si_mix(si_mix(SI_CARD, wc, 120), wc, (uint32_t)(hh * 256 / (ah / 2 > 1 ? ah / 2 : 1)));
                fb_fill_rect((uint32_t)(ax + (int)c * 3), (uint32_t)(mid - hh), 2, (uint32_t)(2 * hh), col);
            }
            if (in.mode == MR_PLAY && in.ms) {
                int px = ax + (int)((uint64_t)used * 3u * in.play_ms / in.ms); if (px > ax + aw - 2) px = ax + aw - 2;
                fb_fill_rect((uint32_t)px, (uint32_t)ay, 2, (uint32_t)ah, SI_AMBER);
            }
            if (!used) {
                si_icon(SI_WAVE, mx + mw / 2, mid - 10, 30, 0x00334155u);
                si_text_c(mx, mid + 14, mw, 18, "Pulsa el boton rojo para empezar a grabar", 12, SI_FAINT, 0);
            }
        }
    }

    /* botones */
    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        int r = L.rnd[i], cx = b->x + r, cy = b->y + r;
        int na = !in.available;
        switch (b->id) {
        case B_REC:
            si_round(cx, cy, r, in.mode == MR_REC ? SI_STOP : na ? SI_MIC_OFF : SI_NONE, SIB_DANGER, ACCENT, na);
            si_round_label(cx, cy, r, in.mode == MR_REC ? "Parar" : "Grabar", in.mode == MR_REC ? 0x00FDA4AFu : SI_TXT);
            break;
        case B_PLAY:
            si_round(cx, cy, r, in.mode == MR_PLAY ? SI_STOP : SI_PLAY, in.mode == MR_PLAY ? SIB_ON : SIB_ACCENT, PLAYC, 0);
            si_round_label(cx, cy, r, in.mode == MR_PLAY ? "Parar" : "Escuchar", SI_DIM);
            break;
        case B_NEW:  si_round(cx, cy, r, SI_NEW, SIB_NEUTRAL, ACCENT, 0); si_round_label(cx, cy, r, "Nueva", SI_DIM); break;
        case B_SAVE: si_round(cx, cy, r, SI_SAVE, in.unsaved ? SIB_SUCCESS : SIB_NEUTRAL, ACCENT, 0); si_round_label(cx, cy, r, "Guardar", in.unsaved ? SI_GREEN : SI_DIM); break;
        case B_MON:
            si_round(cx, cy, r, SI_LEVEL, in.mode == MR_MON ? SIB_ON : SIB_NEUTRAL, SI_GREEN, na);
            si_round_label(cx, cy, r, in.mode == MR_MON ? "Fin nivel" : "Nivel", SI_DIM);
            break;
        case B_SRC_PREV: si_button(b->x, b->y, b->w, b->h, SI_PREV, 0, SIB_NEUTRAL, ACCENT, na); break;
        case B_SRC_NEXT: si_button(b->x, b->y, b->w, b->h, SI_NEXT, 0, SIB_NEUTRAL, ACCENT, na); break;
        case B_AUTO: si_button(b->x, b->y, b->w, b->h, SI_AUTO, "Auto", SIB_ACCENT, PLAYC, na); break;
        case B_G_DN: case B_S_DN: if (!na) si_button(b->x, b->y, b->w, b->h, SI_MINUS, 0, SIB_NEUTRAL, ACCENT, 0); break;
        case B_G_UP: case B_S_UP: if (!na) si_button(b->x, b->y, b->w, b->h, SI_PLUS, 0, SIB_NEUTRAL, ACCENT, 0); break;
        case B_RATE:
            if (!na) { char l[48]; uint32_t p = 0; put_s(l, sizeof l, &p, "Calidad: "); put_s(l, sizeof l, &p, RATE_NAME[rate_i]); si_button(b->x, b->y, b->w, b->h, SI_GAUGE, l, SIB_NEUTRAL, ACCENT, in.mode == MR_REC); }
            break;
        case B_DEL:
            si_button(b->x, b->y, b->w, b->h, SI_TRASH, del_armed ? "Confirmar borrado" : "Borrar", del_armed ? SIB_DANGER : SIB_NEUTRAL, ACCENT, sel < 0 || sel >= nrec);
            break;
        default: break;
        }
    }

    /* estado */
    {
        const char *m = umsg[0] ? umsg : in.msg;
        char fb[130]; uint32_t p = 0;
        if (m[0]) put_s(fb, sizeof fb, &p, m);
        else if (in.mode == MR_REC) { put_s(fb, sizeof fb, &p, "Grabando  -  saturadas "); put_d(fb, sizeof fb, &p, in.clipped); put_s(fb, sizeof fb, &p, ", perdidas "); put_d(fb, sizeof fb, &p, in.overruns); }
        else if (in.mode == MR_MON) put_s(fb, sizeof fb, &p, "Habla y mira la barra. Cambia de fuente con las flechas si no se mueve.");
        else if (in.mode == MR_PROBE) put_s(fb, sizeof fb, &p, "Probando las entradas... habla un poco.");
        else put_s(fb, sizeof fb, &p, "Espacio grabar  -  P escuchar  -  S guardar  -  N nueva");
        fb_fill_rect((uint32_t)mx, (uint32_t)L.status_y, (uint32_t)mw, 1, SI_LINE);
        si_text_fit(mx, L.status_y + 1, 30, fb, mw, 12, (in.msg[0] || umsg[0]) ? SI_AMBER : SI_FAINT, 0);
    }
}

static int inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static int guard_unsaved(void)
{
    struct micrec_info in; micrec_info(&in);
    if (!in.unsaved) { discard_armed = 0; return 1; }
    if (discard_armed) { discard_armed = 0; return 1; }
    discard_armed = 1; setu("Hay una grabacion SIN GUARDAR: pulsa GUARDAR, o repite la accion para descartarla");
    return 0;
}
static void do_record(void)
{
    struct micrec_info in; micrec_info(&in);
    if (in.mode == MR_REC) { micrec_stop(); setu(""); return; }
    if (!guard_unsaved()) return;
    umsg[0] = 0;
    int r = micrec_record(RATES[rate_i]);
    if (r == -19) setu("No hay microfono utilizable"); else if (r == -16) setu("Ocupado: espera a que termine la prueba"); else if (r != 0) setu("No se pudo empezar a grabar");
}
static void do_save(void)
{
    char used[96]; used[0] = 0;
    int r = micrec_save(0, used, sizeof used);
    if (r == 0) { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "Guardado en "); put_s(m, sizeof m, &p, used); setu(m); rescan(); sel = nrec - 1; discard_armed = 0; }
    else if (r == -61) setu("No hay nada que guardar");
    else if (r == -19) setu("Almacen BIGFS no disponible: formatealo con la orden 'big format yes'");
    else { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "No se pudo guardar: "); put_s(m, sizeof m, &p, bigfs_strerror(r)); setu(m); }
}

int recorder_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    struct micrec_info in; micrec_info(&in);
    if (L.lw && inside(mx, my, L.lx, L.row_y, L.lw, L.rows * L.row_h)) {
        int i = scroll + (my - L.row_y) / L.row_h;
        if (i >= 0 && i < nrec) {
            if (!guard_unsaved()) return 0;
            sel = i; del_armed = 0;
            char path[80]; full_path(i, path, sizeof path);
            int r = micrec_load(path);
            setu(r == 0 ? "Cargada: pulsa ESCUCHAR" : "No se pudo cargar (formato no valido)");
        }
        return 0;
    }
    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        if (!inside(mx, my, b->x, b->y, b->w, b->h)) continue;
        if (b->id != B_DEL) del_armed = 0;
        switch (b->id) {
        case B_REC: do_record(); break;
        case B_PLAY:
            if (in.mode == MR_PLAY) micrec_stop();
            else { int r = micrec_play(); if (r == -61) setu("No hay nada grabado"); else if (r == -16) setu("Ya esta sonando"); else if (r != 0) setu("No se pudo reproducir"); else umsg[0] = 0; }
            break;
        case B_NEW: if (guard_unsaved()) { micrec_clear(); umsg[0] = 0; } break;
        case B_SAVE: do_save(); break;
        case B_MON: if (in.mode == MR_MON) micrec_stop(); else { int r = micrec_monitor(); if (r != 0) setu("No se puede medir ahora"); else umsg[0] = 0; } break;
        case B_SRC_NEXT: if (in.nsrc > 0) micrec_set_source((in.src + 1) % in.nsrc); break;
        case B_SRC_PREV: if (in.nsrc > 0) micrec_set_source((in.src + in.nsrc - 1) % in.nsrc); break;
        case B_AUTO: { int r = micrec_probe(); setu(r == 0 ? "Probando todas las entradas: habla un poco..." : "Para la grabacion antes de probar"); break; }
        case B_G_UP: micrec_set_gain(in.hw_gain + 10); break;
        case B_G_DN: micrec_set_gain(in.hw_gain - 10); break;
        case B_S_UP: micrec_set_sw(in.sw_x10 + 10); break;
        case B_S_DN: micrec_set_sw(in.sw_x10 - 10); break;
        case B_RATE: if (in.mode != MR_REC) rate_i = (rate_i + 1) % 5; break;
        case B_DEL:
            if (sel < 0 || sel >= nrec) { setu("Elige una grabacion de la lista"); break; }
            if (!del_armed) { del_armed = 1; break; }
            { char path[80]; full_path(sel, path, sizeof path); int r = micrec_delete(path); setu(r == 0 ? "Borrada" : "No se pudo borrar"); rescan(); }
            break;
        default: break;
        }
        return 0;
    }
    return 0;
}

void recorder_handle_key(char c)
{
    struct micrec_info in; micrec_info(&in);
    if (c == ' ') do_record();
    else if (c == 'p' || c == 'P') {
        if (in.mode == MR_PLAY) micrec_stop();
        else if (micrec_play() != 0) setu("No hay nada que reproducir");
    }
    else if (c == 's' || c == 'S') do_save();
    else if (c == 'n' || c == 'N') { if (guard_unsaved()) { micrec_clear(); umsg[0] = 0; } }
    else if (c == 'l' || c == 'L') { if (in.mode == MR_MON) micrec_stop(); else micrec_monitor(); }
    else if (c == 'r' || c == 'R') rescan();
}

int recorder_tick(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (umsg[0] && now - umsg_at > 6000) { umsg[0] = 0; last_tick = 0; }
    if (now - last_tick < 80) return 0;
    last_tick = now;
    struct micrec_info in; micrec_info(&in);
    int ch = in.mode != last_mode || in.level != last_level || in.ms != last_ms || in.mode == MR_PLAY || in.mode == MR_REC || in.mode == MR_PROBE;
    last_mode = in.mode; last_level = in.level; last_ms = in.ms;
    return ch;
}
