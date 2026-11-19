/* SOMA_179_T480S: aplicacion Captura. Ver capapp.h */
#include <stdint.h>
#include <stddef.h>
#include "capapp.h"
#include "scrcap.h"
#include "fb.h"
#include "gui_font.h"
#include "idt.h"
#include "gui.h"
#include "sysicon.h"     /* SOMA_210_SYSICON */

#define ACCENT 0x00A78BFA        /* SOMA_210: color de la aplicacion (el del icono del escritorio) */

enum { B_PHOTO = 1, B_VIDEO, B_AUDIO, B_BOTH, B_DELAY, B_SCALE, B_FPS, B_OPEN };
struct btn { int id, x, y, w, h; };
struct lay { int mx, mw, hdr_y, ty, th, px, py, pw, ph, oy, sy; struct btn b[10]; int nb; };

static const uint32_t DELAYS[3] = { 0, 3, 5 }, SCALES[4] = { 1, 2, 3, 4 }, FPSV[4] = { 5, 10, 15, 24 };
static int delay_i = 1, scale_i = 1, fps_i = 1;
static uint64_t last_tick; static uint32_t last_sig = 0xFFFFFFFFu, last_seq_seen;
static char shown[112]; static uint64_t shown_at;

static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void put_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void put_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } put_s(o, max, p, b + i); }
static void fmt_time(char *o, uint32_t ms) { uint32_t s = ms / 1000, m = s / 60; s %= 60; if (m > 99) m = 99; o[0] = (char)('0' + m / 10); o[1] = (char)('0' + m % 10); o[2] = ':'; o[3] = (char)('0' + s / 10); o[4] = (char)('0' + s % 10); o[5] = 0; }
static int has_ext(const char *s, const char *e) { uint32_t a = slen(s), b = slen(e); if (a < b) return 0; for (uint32_t i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return 0; } return 1; }

void capapp_init(void) { shown[0] = 0; scrcap_set_options(FPSV[fps_i], SCALES[scale_i]); }

static void addb(struct lay *L, int id, int x, int y, int w, int h) { if (L->nb < 10) { L->b[L->nb].id = id; L->b[L->nb].x = x; L->b[L->nb].y = y; L->b[L->nb].w = w; L->b[L->nb].h = h; L->nb++; } }
/* SOMA_210: cabecera, cuatro tarjetas de modo, panel de informacion y fila de opciones */
static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct lay *L)
{
    L->nb = 0;
    L->mx = (int)wx + 20; L->mw = (int)ww - 40; if (L->mw < 300) L->mw = 300;
    L->hdr_y = (int)wy + 18;
    L->sy = (int)wy + (int)wh - 32;
    L->oy = L->sy - 14 - 34;
    L->ty = (int)wy + 78; L->th = 104;
    L->px = L->mx; L->py = L->ty + L->th + 14; L->pw = L->mw;
    L->ph = L->oy - 14 - L->py; if (L->ph < 70) L->ph = 70;
    int tw = (L->mw - 3 * 12) / 4, x = L->mx;
    addb(L, B_PHOTO, x, L->ty, tw, L->th); x += tw + 12;
    addb(L, B_VIDEO, x, L->ty, tw, L->th); x += tw + 12;
    addb(L, B_AUDIO, x, L->ty, tw, L->th); x += tw + 12;
    addb(L, B_BOTH, x, L->ty, tw, L->th);
    int bw = (L->mw - 3 * 10) / 4; x = L->mx;
    addb(L, B_DELAY, x, L->oy, bw, 34); x += bw + 10;
    addb(L, B_SCALE, x, L->oy, bw, 34); x += bw + 10;
    addb(L, B_FPS, x, L->oy, bw, 34); x += bw + 10;
    addb(L, B_OPEN, x, L->oy, bw, 34);
}

static const char *mode_name(uint32_t m) { return (m == (SC_VIDEO | SC_AUDIO)) ? "Video con audio" : (m & SC_VIDEO) ? "Video de pantalla" : "Audio interno"; }

/* Tarjeta de modo: icono, nombre y formato. Si ese modo esta grabando, se vuelve el boton de parar. */
static void tile(const struct btn *b, int icon, const char *name, const char *sub, uint32_t col, int active, int other_busy, uint32_t rec_ms)
{
    uint32_t fill = active ? si_mix(SI_WIN, SI_RED, 44) : SI_CARD;
    uint32_t edge = active ? SI_RED : other_busy ? SI_LINE : si_mix(SI_LINE, col, 70);
    fb_fill_rrect_aa(b->x, b->y, b->w, b->h, 12, fill);
    fb_stroke_rrect_aa(b->x, b->y, b->w, b->h, 12, edge);
    int cx = b->x + b->w / 2;
    if (active) {
        char tm[8]; uint32_t ms = rec_ms; uint32_t s = ms / 1000, m = s / 60; s %= 60; if (m > 99) m = 99;
        tm[0] = (char)('0' + m / 10); tm[1] = (char)('0' + m % 10); tm[2] = ':'; tm[3] = (char)('0' + s / 10); tm[4] = (char)('0' + s % 10); tm[5] = 0;
        fb_fill_rrect_aa(cx - 19, b->y + 14, 38, 38, 19, SI_RED);
        si_icon(SI_STOP, cx, b->y + 33, 18, 0x00FFFFFFu);
        si_text_c(b->x, b->y + 58, b->w, 20, "Parar", 14, 0x00FFFFFFu, 1);
        si_text_c(b->x, b->y + 78, b->w, 16, tm, 12, 0x00FDA4AFu, 1);
        return;
    }
    uint32_t ic = other_busy ? SI_FAINT : col;
    fb_fill_rrect_aa(cx - 19, b->y + 14, 38, 38, 10, si_mix(SI_CARD, col, other_busy ? 14 : 40));
    si_icon(icon, cx, b->y + 33, 24, ic);
    si_text_c(b->x + 4, b->y + 58, b->w - 8, 20, name, 14, other_busy ? SI_FAINT : SI_TXT, 1);
    si_text_c(b->x + 4, b->y + 78, b->w - 8, 16, sub, 11, SI_FAINT, 0);
}
static void help_row(int x, int y, int w, int icon, uint32_t col, const char *text)
{
    si_icon(icon, x + 10, y + 10, 18, col);
    si_text_fit(x + 30, y, 20, text, w - 30, 13, 0x00CBD5E1u, 0);
}

void capapp_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    struct scrcap_info in; scrcap_info_get(&in);
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    fb_fill_rect(wx, wy, ww, wh, SI_WIN);
    uint32_t act = in.state == SC_REC ? in.mode : 0;

    /* cabecera */
    {
        const char *st = "Lista"; uint32_t sc = SI_DIM; int dot = 0;
        if (in.state == SC_REC) { st = "Grabando"; sc = SI_RED; dot = 1; }
        else if (in.state == SC_WAIT) { st = "Cuenta atras"; sc = SI_AMBER; dot = 1; }
        else if (in.state == SC_SAVING) { st = "Guardando"; sc = SI_AMBER; dot = 1; }
        int cw = si_chip_w(26, st, dot);
        si_chip(L.mx + L.mw - cw, L.hdr_y + 9, 26, st, sc, dot);
        si_header(L.mx, L.hdr_y, L.mw - cw - 16, SI_SCREEN, ACCENT, "Captura", "Pantalla y audio interno");
    }

    /* panel de informacion */
    char t[140]; uint32_t p = 0;
    si_card(L.px, L.py, L.pw, L.ph, SI_CARD);
    int x0 = L.px + 18, y = L.py + 16, w0 = L.pw - 36;
    if (in.state == SC_REC) {
        char tm[8]; fmt_time(tm, in.rec_ms);
        fb_fill_rrect_aa(x0, y + 8, 12, 12, 6, SI_RED);
        si_text(x0 + 22, y, 28, tm, 24, 0x00FFFFFFu, 1);
        si_text(x0 + 22 + si_text_w(tm, 24, 1) + 14, y + 4, 24, mode_name(in.mode), 13, 0x00FDA4AFu, 1);
        y += 38;
        if (in.mode & SC_VIDEO) {
            p = 0; put_d(t, sizeof t, &p, in.w); put_s(t, sizeof t, &p, "x"); put_d(t, sizeof t, &p, in.h); put_s(t, sizeof t, &p, "  -  tramas "); put_d(t, sizeof t, &p, in.frames);
            put_s(t, sizeof t, &p, " (quietas "); put_d(t, sizeof t, &p, in.dup); put_s(t, sizeof t, &p, ")  -  "); put_d(t, sizeof t, &p, in.fps_x10 / 10u); put_s(t, sizeof t, &p, " por segundo  -  "); put_d(t, sizeof t, &p, in.enc_ms); put_s(t, sizeof t, &p, " ms por trama");
            si_text_fit(x0, y, 18, t, w0, 12, SI_DIM, 0); y += 20;
        }
        p = 0; put_s(t, sizeof t, &p, "Memoria usada "); put_d(t, sizeof t, &p, in.bytes / 1048576u); put_s(t, sizeof t, &p, " MB");
        if (in.mode & SC_AUDIO) { put_s(t, sizeof t, &p, "  -  audio "); put_d(t, sizeof t, &p, in.audio_ms / 1000u); put_s(t, sizeof t, &p, " s"); }
        si_text_fit(x0, y, 18, t, w0, 12, SI_DIM, 0); y += 24;
        if ((in.mode & SC_AUDIO) && y + 30 < L.py + L.ph - 24) {
            si_meter(x0, y, w0, 10, (int)((uint64_t)in.level * 100u / 32768u), 0, 1); y += 18;
            if (!in.audio_calls) si_text_fit(x0, y, 18, "Aun no ha sonado nada: reproduce algo (Media, audio tone...)", w0, 12, SI_AMBER, 0);
            else { p = 0; put_s(t, sizeof t, &p, "Sonido detectado a "); put_d(t, sizeof t, &p, in.audio_rate); put_s(t, sizeof t, &p, " Hz"); si_text_fit(x0, y, 18, t, w0, 12, SI_DIM, 0); }
        }
        si_text_fit(x0, L.py + L.ph - 28, 18, "Puedes cerrar esta ventana: sigue grabando. Para parar: Control+ImprPant o vuelve a Captura.", w0, 12, SI_FAINT, 0);
    } else if (in.state == SC_WAIT) {
        p = 0; put_d(t, sizeof t, &p, (in.wait_ms + 999u) / 1000u);
        int hh = L.ph - 56; if (hh > 80) hh = 80;
        si_icon(SI_TIMER, L.px + L.pw / 2 - si_text_w(t, 56, 1) / 2 - 34, L.py + 14 + hh / 2, 30, SI_AMBER);
        si_text_c(L.px, L.py + 14, L.pw, hh, t, 56, SI_AMBER, 1);
        si_text_c(L.px, L.py + L.ph - 34, L.pw, 18, "Cambia a lo que quieras capturar: esta ventana saldra si sigue visible", 12, SI_DIM, 0);
    } else if (in.state == SC_SAVING) {
        si_icon(SI_SAVE, L.px + L.pw / 2 - 70, L.py + L.ph / 2, 26, SI_AMBER);
        si_text(L.px + L.pw / 2 - 48, L.py + L.ph / 2 - 14, 28, "Guardando...", 18, SI_AMBER, 1);
    } else {
        si_text(x0, y, 20, "Atajos en cualquier momento", 13, SI_TXT, 1); y += 28;
        if (y + 20 <= L.py + L.ph - 8) { help_row(x0, y, w0, SI_IMAGE, 0x0034D399u, "ImprPant: captura de imagen de la pantalla"); y += 26; }
        if (y + 20 <= L.py + L.ph - 8) { help_row(x0, y, w0, SI_VIDEO, 0x00FB7185u, "Control+ImprPant: empezar o parar video con audio"); y += 26; }
        if (y + 20 <= L.py + L.ph - 8) { help_row(x0, y, w0, SI_FOLDER, SI_DIM, "Archivos en /capturas (BIGFS): PNG, AVI y WAV"); y += 26; }
        if (y + 20 <= L.py + L.ph - 8) help_row(x0, y, w0, SI_NEXT, SI_FAINT, "Teclas en esta ventana: P imagen, V video, A audio, B video con audio");
    }

    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        int busy = in.state != SC_IDLE;
        switch (b->id) {
        case B_PHOTO: tile(b, SI_IMAGE, "Imagen", "PNG de la pantalla", 0x0034D399u, 0, busy, 0); break;
        case B_VIDEO: tile(b, SI_VIDEO, "Video", "Pantalla en AVI", 0x00FB7185u, act == SC_VIDEO, busy && act != SC_VIDEO, in.rec_ms); break;
        case B_AUDIO: tile(b, SI_SPEAKER, "Audio", "Sonido en WAV", 0x0060A5FAu, act == SC_AUDIO, busy && act != SC_AUDIO, in.rec_ms); break;
        case B_BOTH:  tile(b, SI_SCREEN_AUDIO, "Video y audio", "AVI con sonido", ACCENT, act == (SC_VIDEO | SC_AUDIO), busy && act != (SC_VIDEO | SC_AUDIO), in.rec_ms); break;
        case B_DELAY: { char l[24]; p = 0; put_s(l, sizeof l, &p, "Retardo "); put_d(l, sizeof l, &p, DELAYS[delay_i]); put_s(l, sizeof l, &p, " s"); si_button(b->x, b->y, b->w, b->h, SI_TIMER, l, delay_i ? SIB_ON : SIB_NEUTRAL, ACCENT, 0); break; }
        case B_SCALE: { char l[24]; p = 0; put_s(l, sizeof l, &p, "Escala 1/"); put_d(l, sizeof l, &p, SCALES[scale_i]); si_button(b->x, b->y, b->w, b->h, SI_SCALE, l, SIB_NEUTRAL, ACCENT, act != 0); break; }
        case B_FPS: { char l[24]; p = 0; put_d(l, sizeof l, &p, FPSV[fps_i]); put_s(l, sizeof l, &p, " img/s"); si_button(b->x, b->y, b->w, b->h, SI_GAUGE, l, SIB_NEUTRAL, ACCENT, act != 0); break; }
        case B_OPEN: si_button(b->x, b->y, b->w, b->h, SI_FOLDER, "Abrir ultimo", SIB_NEUTRAL, ACCENT, !in.last[0]); break;
        default: break;
        }
    }
    const char *m = shown[0] ? shown : "";
    char s[130]; p = 0; put_s(s, sizeof s, &p, m[0] ? m : in.last[0] ? "Ultimo archivo: " : "Lista para capturar");
    if (!m[0] && in.last[0]) put_s(s, sizeof s, &p, in.last);
    fb_fill_rect((uint32_t)L.mx, (uint32_t)L.sy, (uint32_t)L.mw, 1, SI_LINE);
    si_text_fit(L.mx, L.sy + 1, 30, s, L.mw, 12, m[0] ? SI_AMBER : SI_FAINT, 0);
}

static int inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void say(const char *s) { uint32_t i = 0; while (s[i] && i + 1 < sizeof shown) { shown[i] = s[i]; i++; } shown[i] = 0; shown_at = timer_get_uptime_ms(); }

static void do_mode(uint32_t mode)
{
    struct scrcap_info in; scrcap_info_get(&in);
    if (in.state == SC_SAVING) return;
    if (in.state == SC_REC) { say("Guardando..."); scrcap_stop_async(); return; }
    if (in.state == SC_WAIT) { scrcap_stop_async(); say("Cuenta atras cancelada"); return; }
    scrcap_set_options(FPSV[fps_i], SCALES[scale_i]);
    int r = scrcap_start(mode, DELAYS[delay_i] * 1000u);
    if (r == -16) say("Ya hay una captura en marcha");
    else if (r == -19) say("Captura no disponible (soma.scrcap=off) o sin almacen BIGFS: big format yes");
    else if (r == -1018) say("No hay salida de audio en este equipo");
    else if (r < 0) say("No se pudo empezar a grabar");
    else shown[0] = 0;
}
static void do_photo(void)
{
    int r = scrcap_shot_async(0, DELAYS[delay_i] * 1000u);
    if (r == -16) say("Espera: ya hay una imagen en curso");
    else if (r < 0) say("Captura no disponible (soma.scrcap=off)");
    else shown[0] = 0;
}

int capapp_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    struct scrcap_info in; scrcap_info_get(&in);
    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        if (!inside(mx, my, b->x, b->y, b->w, b->h)) continue;
        switch (b->id) {
        case B_PHOTO: do_photo(); break;
        case B_VIDEO: do_mode(SC_VIDEO); break;
        case B_AUDIO: do_mode(SC_AUDIO); break;
        case B_BOTH: do_mode(SC_VIDEO | SC_AUDIO); break;
        case B_DELAY: delay_i = (delay_i + 1) % 3; break;
        case B_SCALE: if (in.state != SC_REC) { scale_i = (scale_i + 1) % 4; scrcap_set_options(FPSV[fps_i], SCALES[scale_i]); } break;
        case B_FPS: if (in.state != SC_REC) { fps_i = (fps_i + 1) % 4; scrcap_set_options(FPSV[fps_i], SCALES[scale_i]); } break;
        case B_OPEN:
            if (!in.last[0]) say("Aun no has guardado nada");
            else if (has_ext(in.last, ".png")) gui_open_imgview(in.last);
            else gui_open_media(in.last);
            break;
        default: break;
        }
        return 0;
    }
    return 0;
}

void capapp_handle_key(char c)
{
    if (c == 'p' || c == 'P' || c == ' ') do_photo();
    else if (c == 'v' || c == 'V') do_mode(SC_VIDEO);
    else if (c == 'a' || c == 'A') do_mode(SC_AUDIO);
    else if (c == 'b' || c == 'B') do_mode(SC_VIDEO | SC_AUDIO);
}

int capapp_tick(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (shown[0] && now - shown_at > 7000) { shown[0] = 0; last_sig = 0xFFFFFFFFu; }
    if (now - last_tick < 100) return 0;
    last_tick = now;
    struct scrcap_info in; scrcap_info_get(&in);
    if (in.msg_seq != last_seq_seen) { last_seq_seen = in.msg_seq; if (in.msg[0]) { uint32_t i = 0; while (in.msg[i] && i + 1 < sizeof shown) { shown[i] = in.msg[i]; i++; } shown[i] = 0; shown_at = now; } }
    uint32_t sig = (uint32_t)in.state ^ ((in.rec_ms / 1000u) << 4) ^ ((in.wait_ms / 1000u) << 18) ^ ((in.level >> 10) << 12) ^ (in.frames << 14) ^ (in.msg_seq << 20);
    int ch = sig != last_sig; last_sig = sig;
    return ch;
}
