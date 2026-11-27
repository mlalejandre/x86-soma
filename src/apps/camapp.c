/* SOMA_178_T480S: aplicacion Camara. Ver camapp.h */
#include <stdint.h>
#include <stddef.h>
#include "camapp.h"
#include "camera.h"
#include "fb.h"
#include "gui_font.h"
#include "bigfs.h"
#include "idt.h"
#include "gui.h"
#include "sysicon.h"     /* SOMA_210_SYSICON */

#define ACCENT 0x0038BDF8        /* SOMA_210: color de la aplicacion */

enum { B_POWER = 1, B_PHOTO, B_VIDEO, B_SIZE, B_AUDIO, B_VIEW };
struct btn { int id, x, y, w, h; };
struct lay { int mx, mw, hdr_y, px, py, pw, ph; int bar_cy, sy; int rnd[8]; struct btn b[8]; int nb; };

static struct camera_mode modes[8]; static int nmodes, mode_i = -1;
static int want_audio = 1;
static char umsg[120], last_video[96], last_photo[96];
static uint64_t umsg_at, last_tick; static uint32_t last_seq = 0xFFFFFFFFu, last_ms = 0xFFFFFFFFu; static int last_state = -1;

static void put_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void put_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } put_s(o, max, p, b + i); }
static void setu(const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < sizeof umsg) { umsg[i] = s[i]; i++; } umsg[i] = 0; umsg_at = timer_get_uptime_ms(); }
static void cpy(char *d, uint32_t max, const char *s) { uint32_t i = 0; while (s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void fmt_time(char *o, uint32_t ms) { uint32_t s = ms / 1000, m = s / 60; s %= 60; if (m > 99) m = 99; o[0] = (char)('0' + m / 10); o[1] = (char)('0' + m % 10); o[2] = ':'; o[3] = (char)('0' + s / 10); o[4] = (char)('0' + s % 10); o[5] = 0; }

void camapp_init(void) { umsg[0] = 0; nmodes = 0; mode_i = -1; }

static void load_modes(void)
{
    if (nmodes) return;
    nmodes = camera_modes(modes, 8);
    if (nmodes && mode_i < 0) {                       /* por defecto: 640x480 si existe */
        mode_i = 0; for (int i = 0; i < nmodes; i++) if (modes[i].w == 640 && modes[i].h == 480) mode_i = i;
    }
}

static void addb(struct lay *L, int id, int x, int y, int w, int h) { if (L->nb < 8) { L->b[L->nb].id = id; L->b[L->nb].x = x; L->b[L->nb].y = y; L->b[L->nb].w = w; L->b[L->nb].h = h; L->rnd[L->nb] = 0; L->nb++; } }
static void addr(struct lay *L, int id, int cx, int cy, int r) { addb(L, id, cx - r, cy - r, 2 * r, 2 * r + 22); L->rnd[L->nb - 1] = r; }
/* SOMA_210: cabecera, vista previa en tarjeta, obturador redondo en el centro y controles a los lados */
static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct lay *L)
{
    L->nb = 0;
    L->mx = (int)wx + 20; L->mw = (int)ww - 40; if (L->mw < 300) L->mw = 300;
    L->hdr_y = (int)wy + 18;
    L->sy = (int)wy + (int)wh - 32;
    L->bar_cy = L->sy - 22 - 30 - 8;
    L->px = L->mx; L->py = (int)wy + 78; L->pw = L->mw;
    L->ph = L->bar_cy - 30 - 14 - L->py; if (L->ph < 60) L->ph = 60;
    int cx = L->mx + L->mw / 2;
    int bw = (L->mw / 2 - 110 - 8) / 2; if (bw > 140) bw = 140;
    if (bw < 40) bw = 40;
    int by = L->bar_cy - 17;
    addb(L, B_POWER, L->mx, by, bw, 34);
    addb(L, B_SIZE, L->mx + bw + 8, by, bw, 34);
    addr(L, B_VIDEO, cx - 78, L->bar_cy, 24);
    addr(L, B_PHOTO, cx, L->bar_cy, 30);
    addb(L, B_AUDIO, L->mx + L->mw - 2 * bw - 8, by, bw, 34);
    addb(L, B_VIEW, L->mx + L->mw - bw, by, bw, 34);
}

void camapp_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    struct camera_info in; camera_info(&in);
    load_modes();
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    fb_fill_rect(wx, wy, ww, wh, SI_WIN);

    /* cabecera */
    char t[80]; uint32_t p = 0;
    if (in.hw_present || in.running) {
        static const char H[] = "0123456789ABCDEF";
        put_s(t, sizeof t, &p, "Camara USB  ");
        for (int i = 3; i >= 0; i--) { char c[2] = { H[(in.vid >> (4 * i)) & 15], 0 }; put_s(t, sizeof t, &p, c); }
        put_s(t, sizeof t, &p, ":");
        for (int i = 3; i >= 0; i--) { char c[2] = { H[(in.pid >> (4 * i)) & 15], 0 }; put_s(t, sizeof t, &p, c); }
    } else put_s(t, sizeof t, &p, "Sin camara detectada aun");
    {
        const char *st = in.running ? "En directo" : "Apagada"; uint32_t sc = in.running ? SI_GREEN : SI_DIM;
        char rt[24]; if (in.recording) { char r[10]; fmt_time(r, in.rec_ms); uint32_t q = 0; put_s(rt, sizeof rt, &q, "Grabando "); put_s(rt, sizeof rt, &q, r); st = rt; sc = SI_RED; }
        else if (in.saving) { st = "Guardando"; sc = SI_AMBER; }
        int cw = si_chip_w(26, st, 1);
        si_chip(L.mx + L.mw - cw, L.hdr_y + 9, 26, st, sc, 1);
        si_header(L.mx, L.hdr_y, L.mw - cw - 16, in.running ? SI_CAMERA : SI_CAMERA_OFF, ACCENT, "Camara", t);
    }

    /* vista previa (se conserva la proporcion) */
    si_card(L.px, L.py, L.pw, L.ph, 0x00070A10u);
    uint32_t pw = 0, ph = 0, sq = 0; const uint32_t *pv = camera_preview(&pw, &ph, &sq);
    if (pv && pw && ph) {
        int dw = L.pw - 4, dh = (int)((uint64_t)dw * ph / pw);
        if (dh > L.ph - 4) { dh = L.ph - 4; dw = (int)((uint64_t)dh * pw / ph); }
        fb_blit_scale(L.px + (L.pw - dw) / 2, L.py + (L.ph - dh) / 2, dw, dh, pv, (int32_t)pw, (int32_t)ph, (int32_t)pw);
        if (in.recording) { char r[16], l[24]; uint32_t q = 0; fmt_time(r, in.rec_ms); put_s(l, sizeof l, &q, "REC  "); put_s(l, sizeof l, &q, r); si_chip(L.px + 14, L.py + 14, 26, l, SI_RED, 1); }
        if (in.running) {
            char f[48]; uint32_t q = 0; put_d(f, sizeof f, &q, in.w); put_s(f, sizeof f, &q, "x"); put_d(f, sizeof f, &q, in.h); put_s(f, sizeof f, &q, "  ");
            put_s(f, sizeof f, &q, in.fourcc); put_s(f, sizeof f, &q, "  "); put_d(f, sizeof f, &q, in.fps_x10 / 10); put_s(f, sizeof f, &q, " fps");
            si_chip(L.px + L.pw - 14 - si_chip_w(24, f, 0), L.py + 15, 24, f, SI_DIM, 0);
        }
    } else {
        int cy = L.py + L.ph / 2;
        const char *m = in.running ? "Esperando imagen de la camara..." : "Pulsa Encender para ver la camara";
        si_icon(in.running ? SI_CAMERA : SI_CAMERA_OFF, L.px + L.pw / 2, cy - 18, 52, 0x00334155u);
        si_text_c(L.px, cy + 16, L.pw, 20, m, 13, SI_DIM, 1);
        if (!in.running) si_text_c(L.px, cy + 36, L.pw, 18, "Las fotos van a Im\303\241genes/Fotos y los v\303\255deos a V\303\255deos (Mi unidad)", 12, SI_FAINT, 0);
    }

    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        int r = L.rnd[i], cx = b->x + r, cy = b->y + r;
        switch (b->id) {
        case B_POWER: si_button(b->x, b->y, b->w, b->h, SI_POWER, in.running ? "Apagar" : "Encender", in.running ? SIB_NEUTRAL : SIB_ACCENT, ACCENT, 0); break;
        case B_PHOTO:
            si_round(cx, cy, r, 0, SIB_NEUTRAL, ACCENT, !in.running);
            si_icon(SI_SHUTTER, cx, cy, r * 2 - 10, in.running ? 0x00F8FAFCu : SI_FAINT);
            si_round_label(cx, cy, r, "Foto", in.running ? SI_TXT : SI_FAINT);
            break;
        case B_VIDEO:
            if (in.recording) si_round(cx, cy, r, SI_STOP, SIB_DANGER, ACCENT, 0);
            else si_round(cx, cy, r, in.running && !in.saving ? 0 : SI_REC, SIB_NEUTRAL, ACCENT, !in.running || in.saving);
            if (!in.recording && in.running && !in.saving) si_icon(SI_REC, cx, cy, r * 11 / 10, SI_RED);
            si_round_label(cx, cy, r, in.recording ? "Parar" : in.saving ? "Guardando" : "Video", in.recording ? 0x00FDA4AFu : in.running ? SI_DIM : SI_FAINT);
            break;
        case B_SIZE: { char l[24]; uint32_t q = 0; if (mode_i >= 0 && mode_i < nmodes) { put_d(l, sizeof l, &q, modes[mode_i].w); put_s(l, sizeof l, &q, "x"); put_d(l, sizeof l, &q, modes[mode_i].h); } else put_s(l, sizeof l, &q, "Tamano"); si_button(b->x, b->y, b->w, b->h, SI_SCALE, l, SIB_NEUTRAL, ACCENT, in.recording); break; }
        case B_AUDIO: si_button(b->x, b->y, b->w, b->h, want_audio ? SI_MIC : SI_MIC_OFF, want_audio ? "Audio" : "Sin audio", want_audio ? SIB_ON : SIB_NEUTRAL, SI_GREEN, in.recording); break;
        case B_VIEW: si_button(b->x, b->y, b->w, b->h, SI_PLAY, "Ver video", SIB_NEUTRAL, ACCENT, !last_video[0]); break;
        default: break;
        }
    }
    /* estado */
    char s[150]; p = 0;
    if (umsg[0]) put_s(s, sizeof s, &p, umsg);
    else if (in.msg[0] && !in.running) put_s(s, sizeof s, &p, in.msg);
    else if (in.running) {
        put_s(s, sizeof s, &p, "Tramas "); put_d(s, sizeof s, &p, in.fr_ok);
        if (in.fr_bad) { put_s(s, sizeof s, &p, " (malas "); put_d(s, sizeof s, &p, in.fr_bad); put_s(s, sizeof s, &p, ")"); }
        if (in.dec_err) { put_s(s, sizeof s, &p, "  -  errores de imagen "); put_d(s, sizeof s, &p, in.dec_err); }
        if (in.recording) { put_s(s, sizeof s, &p, "  -  grabado "); put_d(s, sizeof s, &p, in.rec_bytes / 1048576u); put_s(s, sizeof s, &p, " MB"); }
        else put_s(s, sizeof s, &p, "  -  Espacio foto  -  V video  -  C apagar");
    } else put_s(s, sizeof s, &p, "Espacio foto  -  V video  -  C encender  -  Esc cerrar");
    fb_fill_rect((uint32_t)L.mx, (uint32_t)L.sy, (uint32_t)L.mw, 1, SI_LINE);
    si_text_fit(L.mx, L.sy + 1, 30, s, L.mw, 12, umsg[0] ? SI_AMBER : SI_FAINT, 0);
}

static int inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void do_power(void)
{
    struct camera_info in; camera_info(&in);
    if (in.running) { if (in.recording) camera_rec_discard(); camera_stop(); setu("Camara apagada"); return; }
    load_modes();
    uint32_t w = 640, h = 480;
    if (mode_i >= 0 && mode_i < nmodes) { w = modes[mode_i].w; h = modes[mode_i].h; }
    setu("Encendiendo la camara...");
    int r = camera_start(w, h, 30);
    if (r != 0) { struct camera_info e; camera_info(&e); setu(e.msg[0] ? e.msg : "No se pudo encender la camara"); } else umsg[0] = 0;
}
static void do_photo(void)
{
    struct camera_info in; camera_info(&in);
    if (!in.running) { setu("Enciende la camara primero"); return; }
    char used[96]; used[0] = 0;
    int r = camera_photo(0, used, sizeof used);
    if (r == 0) { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "Foto guardada en "); put_s(m, sizeof m, &p, used); setu(m); cpy(last_photo, sizeof last_photo, used); }
    else if (r == -61) setu("Aun no ha llegado ninguna imagen");
    else if (r == -19) setu("Almacen BIGFS no disponible: usa 'big format yes'");
    else { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "No se pudo guardar: "); put_s(m, sizeof m, &p, bigfs_strerror(r)); setu(m); }
}
static void do_video(void)
{
    struct camera_info in; camera_info(&in);
    if (in.saving) return;
    if (in.recording) {
        setu("Guardando el video...");
        char used[96]; used[0] = 0;
        int r = camera_rec_stop(0, used, sizeof used);
        if (r == 0) { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "Video guardado en "); put_s(m, sizeof m, &p, used); setu(m); cpy(last_video, sizeof last_video, used); }
        else if (r == -61) setu("Video demasiado corto");
        else if (r == -19) setu("Almacen BIGFS no disponible: usa 'big format yes'");
        else { char m[130]; uint32_t p = 0; put_s(m, sizeof m, &p, "No se pudo guardar el video: "); put_s(m, sizeof m, &p, bigfs_strerror(r)); setu(m); }
        return;
    }
    if (!in.running) { setu("Enciende la camara primero"); return; }
    int r = camera_rec_start(want_audio);
    if (r != 0) setu("No se pudo empezar a grabar"); else umsg[0] = 0;
}

int camapp_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    struct camera_info in; camera_info(&in);
    for (int i = 0; i < L.nb; ++i) {
        const struct btn *b = &L.b[i];
        if (!inside(mx, my, b->x, b->y, b->w, b->h)) continue;
        switch (b->id) {
        case B_POWER: do_power(); break;
        case B_PHOTO: do_photo(); break;
        case B_VIDEO: do_video(); break;
        case B_SIZE:
            load_modes();
            if (in.recording) { setu("Para el video antes de cambiar el tamano"); break; }
            if (nmodes) { mode_i = (mode_i + 1) % nmodes; if (in.running) { do_power(); do_power(); } }
            break;
        case B_AUDIO: if (!in.recording) want_audio = !want_audio; break;
        case B_VIEW: if (last_video[0]) gui_open_media(last_video); else setu("Aun no has grabado ningun video"); break;
        default: break;
        }
        return 0;
    }
    return 0;
}

void camapp_handle_key(char c)
{
    if (c == ' ') do_photo();
    else if (c == 'v' || c == 'V') do_video();
    else if (c == 'c' || c == 'C') do_power();
}

int camapp_tick(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (umsg[0] && now - umsg_at > 7000) { umsg[0] = 0; last_tick = 0; }
    if (now - last_tick < 40) return 0;
    last_tick = now;
    struct camera_info in; camera_info(&in);
    int st = (in.running ? 1 : 0) | (in.recording ? 2 : 0) | (in.saving ? 4 : 0);
    int ch = in.pv_seq != last_seq || st != last_state || (in.recording && in.rec_ms / 1000u != last_ms / 1000u);
    last_seq = in.pv_seq; last_state = st; last_ms = in.rec_ms;
    return ch;
}

void camapp_close(void)
{
    struct camera_info in; camera_info(&in);
    if (in.recording) camera_rec_discard();
    if (in.running) camera_stop();
}
