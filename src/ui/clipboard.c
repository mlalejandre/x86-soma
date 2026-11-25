#include "clipboard.h"
#include "fb.h"
#include "gui_font.h"
#include "mem.h"
#include "idt.h"

/* SOMA_PARCHE_PORTAPAPELES */

struct clip_ent {
    int       kind;          /* CLIP_NONE = hueco libre */
    char     *text;          /* texto o ruta (terminado en 0) */
    uint32_t  len;
    uint32_t *px;
    int       w, h;
    int       cut;
};

static struct clip_ent hist[CLIP_HIST_MAX];     /* hist[0] = el mas reciente */
static uint32_t g_seq = 1;
static char     g_toast[128];
static uint64_t g_toast_until;
static int      g_ov_open, g_ov_sel;
static int      g_ov_dirty;                      /* hay que borrar pantalla al cerrar */

static uint32_t cstrlen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }

static void ent_free(struct clip_ent *e)
{
    if (e->text) kfree(e->text);
    if (e->px) kfree(e->px);
    e->text = 0; e->px = 0; e->len = 0; e->w = e->h = 0; e->kind = CLIP_NONE; e->cut = 0;
}

/* Memoria total que ocupa el historial: si se pasa de ~12 MiB se descartan los mas antiguos */
static uint32_t hist_bytes(void)
{
    uint32_t t = 0;
    for (int i = 0; i < CLIP_HIST_MAX; i++) { t += hist[i].len; if (hist[i].px) t += (uint32_t)hist[i].w * (uint32_t)hist[i].h * 4u; }
    return t;
}

static void hist_remove(int idx)
{
    if (idx < 0 || idx >= CLIP_HIST_MAX) return;
    ent_free(&hist[idx]);
    for (int i = idx; i + 1 < CLIP_HIST_MAX; i++) hist[i] = hist[i + 1];
    hist[CLIP_HIST_MAX - 1].kind = CLIP_NONE; hist[CLIP_HIST_MAX - 1].text = 0; hist[CLIP_HIST_MAX - 1].px = 0;
    hist[CLIP_HIST_MAX - 1].len = 0; hist[CLIP_HIST_MAX - 1].w = hist[CLIP_HIST_MAX - 1].h = 0; hist[CLIP_HIST_MAX - 1].cut = 0;
}

/* Hace hueco en la posicion 0 (el ultimo hueco se pierde y se libera) */
static void hist_push_front(struct clip_ent *e)
{
    ent_free(&hist[CLIP_HIST_MAX - 1]);
    for (int i = CLIP_HIST_MAX - 1; i > 0; i--) hist[i] = hist[i - 1];
    hist[0] = *e;
    while (hist_bytes() > (12u << 20)) {
        int last = -1;
        for (int i = CLIP_HIST_MAX - 1; i > 0; i--) if (hist[i].kind != CLIP_NONE) { last = i; break; }
        if (last < 0) break;
        hist_remove(last);
    }
    g_seq++;
}

int clip_kind(void) { return hist[0].kind; }
uint32_t clip_seq(void) { return g_seq; }

int clip_set_text(const char *s, uint32_t n)
{
    if (!s) return -1;
    if (n == 0) n = cstrlen(s);
    if (n == 0) return -1;
    if (n > CLIP_TEXT_MAX) n = CLIP_TEXT_MAX;
    /* no repetir el mismo texto dos veces seguidas */
    if (hist[0].kind == CLIP_TEXT && hist[0].len == n) {
        uint32_t i = 0; while (i < n && hist[0].text[i] == s[i]) i++;
        if (i == n) return 0;
    }
    struct clip_ent e; memset(&e, 0, sizeof e);
    e.text = (char *)kmalloc(n + 1); if (!e.text) return -1;
    memcpy(e.text, s, n); e.text[n] = 0; e.len = n; e.kind = CLIP_TEXT;
    hist_push_front(&e);
    return 0;
}

int clip_set_file(const char *path, int is_cut)
{
    uint32_t n = cstrlen(path);
    if (n == 0 || n > 200) return -1;
    struct clip_ent e; memset(&e, 0, sizeof e);
    e.text = (char *)kmalloc(n + 1); if (!e.text) return -1;
    memcpy(e.text, path, n); e.text[n] = 0; e.len = n; e.kind = CLIP_FILE; e.cut = is_cut ? 1 : 0;
    hist_push_front(&e);
    return 0;
}

int clip_set_image(const uint32_t *px, int w, int h)
{
    if (!px || w <= 0 || h <= 0) return -1;
    if ((uint32_t)w * (uint32_t)h > CLIP_IMG_MAX_PIX) return -2;
    struct clip_ent e; memset(&e, 0, sizeof e);
    uint32_t bytes = (uint32_t)w * (uint32_t)h * 4u;
    e.px = (uint32_t *)kmalloc(bytes); if (!e.px) return -1;
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; i++) e.px[i] = px[i] & 0x00FFFFFFu;   /* sin canal alfa */
    e.w = w; e.h = h; e.kind = CLIP_IMAGE;
    hist_push_front(&e);
    return 0;
}

const char *clip_text(uint32_t *n)
{
    if (hist[0].kind == CLIP_TEXT || hist[0].kind == CLIP_FILE) { if (n) *n = hist[0].len; return hist[0].text; }
    /* si lo ultimo es una imagen, el texto mas reciente sigue estando disponible */
    for (int i = 1; i < CLIP_HIST_MAX; i++) if (hist[i].kind == CLIP_TEXT) { if (n) *n = hist[i].len; return hist[i].text; }
    if (n) *n = 0;
    return 0;
}

const uint32_t *clip_image(int *w, int *h)
{
    if (hist[0].kind != CLIP_IMAGE) {
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    if (w) *w = hist[0].w;
    if (h) *h = hist[0].h;
    return hist[0].px;
}

int clip_file_is_cut(void) { return hist[0].kind == CLIP_FILE && hist[0].cut; }
void clip_file_done(void) { if (hist[0].kind == CLIP_FILE) hist[0].cut = 0; }

void clip_clear(void)
{
    for (int i = 0; i < CLIP_HIST_MAX; i++) ent_free(&hist[i]);
    g_seq++;
}

int clip_image_to_bmp(uint8_t *out, uint32_t max)
{
    if (hist[0].kind != CLIP_IMAGE) return -1;
    uint32_t w = (uint32_t)hist[0].w, h = (uint32_t)hist[0].h, stride = (w * 3u + 3u) & ~3u, total = 54u + stride * h;
    if (total > max) return -2;
    memset(out, 0, 54);
    out[0] = 'B'; out[1] = 'M';
    out[2] = (uint8_t)total; out[3] = (uint8_t)(total >> 8); out[4] = (uint8_t)(total >> 16); out[5] = (uint8_t)(total >> 24);
    out[10] = 54; out[14] = 40;
    out[18] = (uint8_t)w; out[19] = (uint8_t)(w >> 8); out[20] = (uint8_t)(w >> 16); out[21] = (uint8_t)(w >> 24);
    out[22] = (uint8_t)h; out[23] = (uint8_t)(h >> 8); out[24] = (uint8_t)(h >> 16); out[25] = (uint8_t)(h >> 24);
    out[26] = 1; out[28] = 24;
    uint32_t sz = stride * h; out[34] = (uint8_t)sz; out[35] = (uint8_t)(sz >> 8); out[36] = (uint8_t)(sz >> 16); out[37] = (uint8_t)(sz >> 24);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = out + 54 + (h - 1 - y) * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p = hist[0].px[y * w + x];
            row[x * 3] = (uint8_t)p; row[x * 3 + 1] = (uint8_t)(p >> 8); row[x * 3 + 2] = (uint8_t)(p >> 16);
        }
        for (uint32_t x = w * 3u; x < stride; x++) row[x] = 0;
    }
    return (int)total;
}

void clip_status_text(char *out, uint32_t max, const char *prefix, uint32_t n)
{
    uint32_t k = 0;
    while (prefix && *prefix && k + 1 < max) out[k++] = *prefix++;
    char tb[12]; int tn = 0; if (n == 0) tb[tn++] = '0'; while (n) { tb[tn++] = (char)('0' + n % 10); n /= 10; }
    while (tn && k + 1 < max) out[k++] = tb[--tn];
    if (k + 3 < max) { out[k++] = ' '; out[k++] = 'B'; }
    out[k] = 0;
}

/* ------------------------------ avisos ------------------------------ */
void clip_toast(const char *msg)
{
    uint32_t k = 0; while (msg && msg[k] && k + 1 < sizeof g_toast) { g_toast[k] = msg[k]; k++; }
    g_toast[k] = 0;
    g_toast_until = timer_get_uptime_ms() + 1800;
}
void clip_toast_ms(const char *msg, uint32_t ms) { clip_toast(msg); g_toast_until = timer_get_uptime_ms() + ms; }   /* SOMA_238 */

/* ------------------------------ panel de historial ------------------------------ */
int clip_overlay_active(void) { return g_ov_open; }
void clip_overlay_open(void)
{
    g_ov_open = 1; g_ov_sel = 0; g_ov_dirty = 1;
}

static int ov_count(void) { int n = 0; for (int i = 0; i < CLIP_HIST_MAX; i++) if (hist[i].kind != CLIP_NONE) n++; return n; }

/* Sube el elemento idx a la cabecera (para que "Pegar" use ese) */
static void hist_promote(int idx)
{
    if (idx <= 0 || idx >= CLIP_HIST_MAX) return;
    struct clip_ent e = hist[idx];
    for (int i = idx; i > 0; i--) hist[i] = hist[i - 1];
    hist[0] = e; g_seq++;
}

int clip_overlay_key(char c)
{
    int n = ov_count();
    if (c == 27 || c == 25 || c == 'q') { g_ov_open = 0; return 0; }
    if (n == 0) { return 0; }
    if (c == 16 && g_ov_sel > 0) g_ov_sel--;
    else if (c == 14 && g_ov_sel + 1 < n) g_ov_sel++;
    else if (c >= '1' && c <= '8' && (c - '1') < n) { hist_promote(c - '1'); g_ov_open = 0; return 1; }
    else if (c == '\r' || c == '\n') { hist_promote(g_ov_sel); g_ov_open = 0; return 1; }
    else if (c == 4 || c == 127 || c == '\b') { hist_remove(g_ov_sel); g_seq++; if (g_ov_sel >= ov_count() && g_ov_sel > 0) g_ov_sel--; }
    else if (c == 'C') { clip_clear(); g_ov_sel = 0; }
    return 0;
}

static void ov_line(const struct clip_ent *e, char *out, uint32_t max)
{
    uint32_t k = 0;
    if (e->kind == CLIP_IMAGE) {
        const char *p = "Imagen "; while (*p && k + 1 < max) out[k++] = *p++;
        for (int pass = 0; pass < 2; pass++) {
            int v = pass ? e->h : e->w; char tb[12]; int tn = 0; if (!v) tb[tn++] = '0'; while (v) { tb[tn++] = (char)('0' + v % 10); v /= 10; }
            while (tn && k + 1 < max) out[k++] = tb[--tn];
            if (!pass && k + 1 < max) out[k++] = 'x';
        }
    } else {
        if (e->kind == CLIP_FILE) { const char *p = e->cut ? "[cortado] " : "[archivo] "; while (*p && k + 1 < max) out[k++] = *p++; }
        for (uint32_t i = 0; i < e->len && k + 1 < max && k < 60; i++) {
            char ch = e->text[i];
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            if ((unsigned char)ch < 32) continue;
            out[k++] = ch;
        }
        if (e->len > 60 && k + 4 < max) { out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; }
    }
    out[k] = 0;
}

void clip_overlay_draw(void)
{
    uint32_t sw = fb_get_width(), sh = fb_get_height();
    if (sw == 0 || sh == 0) return;
    uint64_t now = timer_get_uptime_ms();

    if (g_ov_open) {
        int n = ov_count();
        uint32_t pw = 620, row = 36, ph = 76 + (n ? (uint32_t)n * row : 40) + 8;
        if (pw > sw - 20) pw = sw - 20;
        uint32_t px = (sw - pw) / 2, py = sh > ph + 60 ? (sh - ph) / 3 : 10;
        fb_fill_rect(px + 6, py + 6, pw, ph, 0x00050810);
        fb_fill_rect(px, py, pw, ph, 0x000F172A);
        fb_draw_rect(px, py, pw, ph, 0x0038BDF8);
        fb_fill_rect(px, py, pw, 34, 0x001E293B);
        font_draw_string(px + 14, py + 12, "Portapapeles  (historial)", 0x00F8FAFC, 1);
        font_draw_string(px + pw - 270, py + 12, "Ctrl+C copiar   Ctrl+V pegar", 0x0094A3B8, 1);
        if (n == 0) {
            font_draw_string(px + 20, py + 56, "Vacio. Copia texto, una imagen o un archivo con Ctrl+C.", 0x0094A3B8, 1);
        }
        int shown = 0;
        for (int i = 0; i < CLIP_HIST_MAX; i++) {
            if (hist[i].kind == CLIP_NONE) continue;
            uint32_t ry = py + 40 + (uint32_t)shown * row;
            int sel = (shown == g_ov_sel);
            fb_fill_rect(px + 8, ry, pw - 16, row - 4, sel ? 0x001D4ED8 : 0x00162033);
            char num[4] = { (char)('1' + shown), 0, 0, 0 };
            font_draw_string(px + 18, ry + 10, num, 0x0038BDF8, 1);
            uint32_t tx = px + 44;
            if (hist[i].kind == CLIP_IMAGE && hist[i].px) {
                int tw = 40, th = 28;
                fb_blit_scale((int32_t)tx, (int32_t)ry + 1, tw, th, hist[i].px, hist[i].w, hist[i].h, hist[i].w);
                tx += 52;
            }
            char line[80]; ov_line(&hist[i], line, sizeof line);
            font_draw_string(tx, ry + 10, line, 0x00E2E8F0, 1);
            shown++;
        }
        font_draw_string(px + 14, py + ph - 22, "Enter / 1-8: pegar    Supr: borrar    C: vaciar    Esc: cerrar", 0x0094A3B8, 1);
        g_ov_dirty = 1;
    } else if (g_ov_dirty) {
        /* el panel acaba de cerrarse: refrescar toda la pantalla para borrarlo */
        g_ov_dirty = 0;
        fb_damage(0, 0, (int32_t)sw, (int32_t)sh);
    }

    if (g_toast[0] && now < g_toast_until) {
        uint32_t tl = cstrlen(g_toast), tw = tl * 8 + 36, th = 34;
        uint32_t tx = (sw - tw) / 2, ty = sh > 120 ? sh - 110 : 10;
        fb_fill_rounded_rect((int32_t)tx, (int32_t)ty, (int32_t)tw, (int32_t)th, 8, 0x00111827);
        fb_draw_rounded_rect((int32_t)tx, (int32_t)ty, (int32_t)tw, (int32_t)th, 8, 0x0010B981);
        font_draw_string(tx + 18, ty + 13, g_toast, 0x00F8FAFC, 1);
    } else if (g_toast[0]) {
        g_toast[0] = 0;
        g_ov_dirty = 0;
        fb_damage(0, 0, (int32_t)sw, (int32_t)sh);
    }
}
