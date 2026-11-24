/* SOMA_285_CHAT: la aplicacion SOMA (chat con el agente). Ver chat.h.
 * Hilos: el de la GUI pinta y atiende; "chat-llm" hace las peticiones del modo Solo LLM (lee adjuntos, consulta la memoria, llama al
 * modelo y registra el intercambio); "chat-img" baja las imagenes de la web. El modo Agente usa el hilo del agente de siempre
 * (gui_agent_submit_chat) y recibe la respuesta en chat_agente_fin. Las fases del agente llegan por chat_fase desde su hilo. */
#include "chat.h"
#include "chat_core.h"
#include "chat_md.h"
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"
#include "textfield.h"
#include "clipboard.h"
#include "idt.h"
#include "mem.h"
#include "fs.h"
#include "thread.h"
#include "memoria.h"
#include "llm.h"
#include "llm_proto.h"
#include "web_net.h"
#include "web_img.h"
#include "agenda.h"
#if defined(__x86_64__) && !defined(SOMA_CHAT_HOST)
#include "voz_sys.h"
#define CHAT_VOZ 1
#endif

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);
int  gui_agent_submit_chat(const char *mision);
int  gui_agent_busy(void);
int  gui_agent_cancel(void);
void agent_chat_contexto(const char *historial, int sin_memoria);

void *cc_alloc(uint32_t n) { return kmalloc(n ? n : 1); }
void  cc_free(void *p) { if (p) kfree(p); }

/* ---------------------------------------------------------------- paleta */
#define C_BG     0x000B0F1Au
#define C_SIDE   0x00070A12u
#define C_SIDEH  0x00121827u
#define C_SEL    0x001A2238u
#define C_CARD   0x00111727u
#define C_CARD2  0x00171E33u
#define C_INP    0x00141B2Eu
#define C_BD     0x00222A44u
#define C_BDL    0x00313B60u
#define C_TX     0x00F3F4F6u
#define C_TX2    0x00CBD2DDu
#define C_MU     0x008B949Eu
#define C_FAINT  0x00535B70u
#define C_ACC    0x006366F1u
#define C_ACC2   0x008B5CF6u
#define C_CYAN   0x0022D3EEu
#define C_GREEN  0x0010B981u
#define C_AMBER  0x00F59E0Bu
#define C_RED    0x00EF4444u
#define C_UB     0x001C2440u

static const struct md_theme TEMA = {
    0x00E5E7EBu, 0x009CA3AFu, 0x00FFFFFFu, 0x007DD3FCu, 0x00283150u,
    0x00090D16u, 0x00131929u, 0x00222A44u, 0x00E6EDF3u, 0x001E2640u, 0x00F9A8D4u, 0x006366F1u, 0x00574A12u,
    0x00182036u, 0x000E1322u, 0x006366F1u,
    0x00FF7B72u, 0x00A5D6FFu, 0x0079C0FFu, 0x006E7681u, 0x00D2A8FFu, 0x00FFA657u, 0x00F87171u };

#define SIDE_W   272
#define TOP_H    56
#define COL_MAX  800
#define HIST_FILE "/chat/historial.txt"
#define AJ_FILE   "/chat/ajustes.txt"
#define COMP_CAP  32768
#define ADJ_MAX   6

/* ---------------------------------------------------------------- estado */
static struct cc_hist g_h; static int g_ini;
static uint32_t g_cur;                                   /* conversacion abierta (0 = nueva) */
static int g_modo = CC_MODO_LLM, g_mem = 1, g_side = 1;  /* para una conversacion nueva / panel lateral */
static uint32_t g_ver = 1;
static int g_sucio; static uint64_t g_t_sucio;
static char g_toast[120]; static uint64_t g_toast_t; static int g_toast_err;
static int g_wx, g_wy, g_ww, g_wh;
static int g_rx = -1, g_ry = -1;
static char g_url[1024], g_path[200];

/* composer */
static char *b_comp; static struct tfield f_comp;
static char b_busca[100]; static struct tfield f_busca;
static char b_in[200]; static struct tfield f_in;
static struct tfield *g_foco;
static char g_adj[ADJ_MAX][160]; static int g_nadj;
static int g_edit_idx = -1;                              /* editando el mensaje n (se reenvia desde ahi) */

/* desplazamiento de los mensajes y de la lista */
static int g_scroll, g_max_scroll, g_pegado = 1, g_sscroll, g_smax;
static int g_view_y0, g_view_y1, g_mx0, g_mw0;

/* ventanas encima */
enum { M_NO, M_REN, M_BORRA, M_ARCH, M_VACIAR };
static int g_modal; static uint32_t g_modal_id;
static char g_dir[160] = "/usuario"; static int g_ascroll;
static char *g_lista; static int g_nlista;              /* entradas del selector de archivos separadas por '\0' */
static char g_menu_conv; static uint32_t g_menu_id; static int g_menu_x, g_menu_y;

/* ---------------------------------------------------------------- utilidades */
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy(char *d, const char *s, int cap) { int i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void scat(char *d, const char *s, int cap) { int i = slen(d), j = 0; while (s && s[j] && i + 1 < cap) d[i++] = s[j++]; d[i] = 0; }
static void snum(char *d, int cap, uint32_t v)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; scat(d, r, cap);
}
static int eqn(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; } return 1; }
static int pre(const char *s, const char *p) { while (*p) { if (*s != *p) return 0; s++; p++; } return 1; }
static uint64_t ms(void) { return timer_get_uptime_ms(); }
static void cambia(void) { g_ver++; }
static void toast(const char *t, int err) { scpy(g_toast, t, sizeof g_toast); g_toast_t = ms(); g_toast_err = err; cambia(); }
static uint32_t ahora(void) { int64_t m = agenda_ahora(); return m > 0 ? (uint32_t)m : 0; }
static void mcpy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static const char *base_name(const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' && q[1]) b = q + 1; return b; }
static void seg_txt(char *d, int cap, uint32_t msx)                  /* "4,2 s" / "1 min 5 s" */
{
    d[0] = 0;
    if (msx < 60000) { snum(d, cap, msx / 1000); scat(d, ",", cap); snum(d, cap, (msx / 100) % 10); scat(d, " s", cap); return; }
    snum(d, cap, msx / 60000); scat(d, " min ", cap); snum(d, cap, (msx / 1000) % 60); scat(d, " s", cap);
}

/* ---------------------------------------------------------------- texto */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int twn(const char *s, int n, int px, int fl) { return has_wf() ? wf_width(s, (uint32_t)n, px, fl) : n * 8; }
static int tw(const char *s, int px, int fl) { return twn(s, slen(s), px, fl); }
static void txn(int x, int top, int h, const char *s, int n, int px, uint32_t col, int fl)
{
    if (n <= 0) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, fl, 255); return; }
    for (int i = 0; i < n && s[i]; i++) { char c[2] = { s[i], 0 }; font_draw_string((uint32_t)(x + i * 8), (uint32_t)(top + (h - 10) / 2), c, col, 1); }
}
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int fl) { txn(x, top, h, s, slen(s), px, col, fl); }
static void txc(int x, int w, int top, int h, const char *s, int px, uint32_t col, int fl) { tx(x + (w - tw(s, px, fl)) / 2, top, h, s, px, col, fl); }
static int u8back(const char *s, int n) { while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; return n; }
static void txe(int x, int top, int h, const char *s, int px, uint32_t col, int fl, int maxw)
{
    int n = slen(s);
    if (maxw <= 0) return;
    if (tw(s, px, fl) <= maxw) { txn(x, top, h, s, n, px, col, fl); return; }
    int pw = tw("\342\200\246", px, fl), lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (twn(s, mid, px, fl) + pw <= maxw) lo = mid; else hi = mid - 1; }
    lo = u8back(s, lo);
    txn(x, top, h, s, lo, px, col, fl); tx(x + twn(s, lo, px, fl), top, h, "\342\200\246", px, col, fl);
}

/* ---------------------------------------------------------------- formas e iconos */
static void rr(int x, int y, int w, int h, int r, uint32_t rgb) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | rgb); }
static void rra(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void rs(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_stroke_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void ln(int x0, int y0, int x1, int y1, uint32_t col, int g16) { fb_line_aa(x0 * 16, y0 * 16, x1 * 16, y1 * 16, g16, 0xFF000000u | col); }
static void ln16(int x0, int y0, int x1, int y1, uint32_t col, int g16) { fb_line_aa(x0, y0, x1, y1, g16, 0xFF000000u | col); }
static void punto(int cx, int cy, int r, uint32_t col, uint32_t a) { fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, (a << 24) | col); }
static uint32_t mezcla(uint32_t a, uint32_t b, int k)
{
    uint32_t r = (((a >> 16) & 255) * (255 - (uint32_t)k) + ((b >> 16) & 255) * (uint32_t)k) / 255, g = (((a >> 8) & 255) * (255 - (uint32_t)k) + ((b >> 8) & 255) * (uint32_t)k) / 255,
             bl = ((a & 255) * (255 - (uint32_t)k) + (b & 255) * (uint32_t)k) / 255;
    return r << 16 | g << 8 | bl;
}
/* seno y coseno en 1/1024 de vuelta, resultado en 1/256 (sin coma flotante) */
static const int16_t SN[17] = { 0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256 };
static int sn(int a)
{
    a &= 1023; int q = a >> 8, r = a & 255;
    if (q == 1 || q == 3) r = 256 - r;
    int k = r >> 4, f = r & 15, v = k >= 16 ? 256 : SN[k] + (SN[k + 1] - SN[k]) * f / 16;
    return q >= 2 ? -v : v;
}
static int cs(int a) { return sn(a + 256); }
enum { I_PLUS, I_CLIP, I_SEND, I_STOP, I_COPY, I_REGEN, I_VOZ, I_EDIT, I_TRASH, I_PIN, I_LUPA, I_SIDE, I_EXPORT, I_MEM, I_AGENT, I_CHAT, I_CHECK, I_X, I_FOLDER, I_FILE, I_IMG, I_DOWN, I_DOTS, I_NEW, I_UP };
static void icono(int k, int cx, int cy, uint32_t c, int s)          /* s: lado aproximado (16 normal) */
{
    int g = s >= 18 ? 30 : 26, h = s / 2;
    switch (k) {
    case I_PLUS: ln(cx - h + 2, cy, cx + h - 2, cy, c, g); ln(cx, cy - h + 2, cx, cy + h - 2, c, g); break;
    case I_CLIP:
        ln16(cx * 16 + 40, cy * 16 - 100, cx * 16 - 60, cy * 16 + 4, c, g); ln16(cx * 16 - 60, cy * 16 + 4, cx * 16 - 20, cy * 16 + 48, c, g);
        ln16(cx * 16 - 20, cy * 16 + 48, cx * 16 + 84, cy * 16 - 56, c, g); ln16(cx * 16 + 84, cy * 16 - 56, cx * 16 + 56, cy * 16 - 86, c, g);
        ln16(cx * 16 + 56, cy * 16 - 86, cx * 16 - 24, cy * 16 - 6, c, g); break;
    case I_SEND: ln(cx, cy + h - 1, cx, cy - h + 2, c, 34); ln(cx - h + 2, cy - 1, cx, cy - h + 2, c, 34); ln(cx + h - 2, cy - 1, cx, cy - h + 2, c, 34); break;
    case I_UP: ln(cx - h + 2, cy + 2, cx, cy - 3, c, g); ln(cx + h - 2, cy + 2, cx, cy - 3, c, g); break;
    case I_DOWN: ln(cx, cy - h + 2, cx, cy + h - 3, c, g); ln(cx - h + 3, cy + 1, cx, cy + h - 3, c, g); ln(cx + h - 3, cy + 1, cx, cy + h - 3, c, g); break;
    case I_STOP: rr(cx - h + 3, cy - h + 3, s - 6, s - 6, 2, c); break;
    case I_COPY: rs(cx - h + 4, cy - h + 1, s - 7, s - 6, 3, c, 255); rr(cx - h + 1, cy - h + 4, s - 7, s - 6, 3, C_BG); rs(cx - h + 1, cy - h + 4, s - 7, s - 6, 3, c, 255); break;
    case I_REGEN: {
        int r = h - 2; for (int a = 120; a < 900; a += 60) { int a2 = a + 60; ln16(cx * 16 + cs(a) * r / 16, cy * 16 + sn(a) * r / 16, cx * 16 + cs(a2) * r / 16, cy * 16 + sn(a2) * r / 16, c, g); }
        int ex = cx + cs(900) * r / 256, ey = cy + sn(900) * r / 256; ln(ex, ey, ex + 4, ey - 1, c, g); ln(ex, ey, ex + 1, ey - 4, c, g); break; }
    case I_VOZ: rr(cx - h + 2, cy - 2, 4, 5, 1, c); ln(cx - h + 5, cy - 2, cx, cy - h + 2, c, g); ln(cx - h + 5, cy + 3, cx, cy + h - 2, c, g); ln(cx, cy - h + 2, cx, cy + h - 2, c, g);
        { int r = 4; for (int a = -160; a < 160; a += 64) ln16(cx * 16 + 48 + cs(a) * r / 16, cy * 16 + sn(a) * r / 16, cx * 16 + 48 + cs(a + 64) * r / 16, cy * 16 + sn(a + 64) * r / 16, c, 22); } break;
    case I_EDIT: ln(cx - h + 3, cy + h - 3, cx + h - 4, cy - h + 4, c, g); ln(cx + h - 6, cy - h + 2, cx + h - 2, cy - h + 6, c, g); ln(cx - h + 3, cy + h - 3, cx - h + 2, cy + h - 2, c, g); break;
    case I_TRASH: ln(cx - h + 2, cy - h + 4, cx + h - 2, cy - h + 4, c, g); ln(cx - 2, cy - h + 2, cx + 2, cy - h + 2, c, g); rs(cx - h + 4, cy - h + 5, s - 8, s - 7, 2, c, 255);
        ln(cx - 1, cy - 2, cx - 1, cy + h - 4, c, 18); ln(cx + 2, cy - 2, cx + 2, cy + h - 4, c, 18); break;
    case I_PIN: punto(cx + 1, cy - 3, 4, c, 255); ln(cx + 1, cy, cx - h + 3, cy + h - 2, c, g); ln(cx - 4, cy - 1, cx + 4, cy + 3, c, g); break;
    case I_LUPA: rs(cx - h + 2, cy - h + 2, s - 7, s - 7, (s - 7) / 2, c, 255); rs(cx - h + 3, cy - h + 3, s - 9, s - 9, (s - 9) / 2, c, 160); ln(cx + 2, cy + 2, cx + h - 1, cy + h - 1, c, 30); break;
    case I_SIDE: rs(cx - h + 1, cy - h + 2, s - 2, s - 4, 3, c, 255); ln(cx - h + 6, cy - h + 3, cx - h + 6, cy + h - 3, c, 22); break;
    case I_EXPORT: ln(cx, cy - h + 2, cx, cy + 2, c, g); ln(cx - 4, cy - 2, cx, cy + 2, c, g); ln(cx + 4, cy - 2, cx, cy + 2, c, g); ln(cx - h + 2, cy + h - 3, cx + h - 2, cy + h - 3, c, g);
        ln(cx - h + 2, cy + 3, cx - h + 2, cy + h - 3, c, g); ln(cx + h - 2, cy + 3, cx + h - 2, cy + h - 3, c, g); break;
    case I_MEM:      /* dos lobulos con circuitos */
        rs(cx - h + 1, cy - h + 2, h + 1, s - 4, h / 2 + 1, c, 255); rs(cx - 1, cy - h + 2, h + 1, s - 4, h / 2 + 1, c, 255);
        ln(cx, cy - h + 3, cx, cy + h - 3, c, 18); punto(cx - 3, cy - 1, 1, c, 255); punto(cx + 3, cy + 2, 1, c, 255); break;
    case I_AGENT:    /* destello */
        ln(cx, cy - h + 1, cx, cy + h - 1, c, g); ln(cx - h + 1, cy, cx + h - 1, cy, c, g); ln(cx - 3, cy - 3, cx + 3, cy + 3, c, 18); ln(cx - 3, cy + 3, cx + 3, cy - 3, c, 18); punto(cx, cy, 2, c, 255); break;
    case I_CHAT: rs(cx - h + 1, cy - h + 2, s - 2, s - 6, 5, c, 255); ln(cx - 3, cy + h - 4, cx - 5, cy + h - 1, c, g); break;
    case I_CHECK: ln(cx - h + 3, cy, cx - 1, cy + h - 4, c, 30); ln(cx - 1, cy + h - 4, cx + h - 2, cy - h + 4, c, 30); break;
    case I_X: ln(cx - h + 4, cy - h + 4, cx + h - 4, cy + h - 4, c, g); ln(cx - h + 4, cy + h - 4, cx + h - 4, cy - h + 4, c, g); break;
    case I_FOLDER: rr(cx - h + 1, cy - h + 3, h, 4, 2, c); rr(cx - h + 1, cy - h + 5, s - 2, s - 8, 2, c); break;
    case I_FILE: rs(cx - h + 3, cy - h + 1, s - 6, s - 2, 2, c, 255); ln(cx - 2, cy - 1, cx + 3, cy - 1, c, 18); ln(cx - 2, cy + 2, cx + 3, cy + 2, c, 18); break;
    case I_IMG: rs(cx - h + 1, cy - h + 2, s - 2, s - 4, 2, c, 255); ln(cx - h + 3, cy + h - 4, cx - 1, cy, c, 22); ln(cx - 1, cy, cx + 2, cy + 3, c, 22); ln(cx + 1, cy + 2, cx + 3, cy, c, 22); ln(cx + 3, cy, cx + h - 2, cy + h - 4, c, 22); punto(cx + 3, cy - 3, 1, c, 255); break;
    case I_DOTS: punto(cx - 5, cy, 2, c, 255); punto(cx, cy, 2, c, 255); punto(cx + 5, cy, 2, c, 255); break;
    case I_NEW: rs(cx - h + 1, cy - h + 3, s - 4, s - 4, 3, c, 255); rr(cx - 1, cy - h, h + 2, h + 1, 0, C_SIDE); ln(cx - 2, cy + 2, cx + h - 1, cy - h + 3, c, 30); ln(cx + h - 3, cy - h + 1, cx + h, cy - h + 4, c, 30); break;
    }
}
/* el orbe de SOMA: degradado radial cian - violeta con brillo */
static void orbe(int cx, int cy, int r, int fase)
{
    for (int k = r; k > 0; k -= (r > 20 ? 2 : 1)) {
        int t = (r - k) * 255 / r;
        uint32_t c = mezcla(mezcla(0x004338CAu, 0x007C3AEDu, (fase & 255) / 2 + 60), 0x0022D3EEu, t * 3 / 4);
        punto(cx, cy, k, c, 255);
    }
    punto(cx - r / 3, cy - r / 3, r / 3 > 1 ? r / 3 : 1, 0x00FFFFFFu, 60);
    punto(cx - r / 3, cy - r / 3, r / 6 > 1 ? r / 6 : 1, 0x00FFFFFFu, 110);
}

/* ---------------------------------------------------------------- zonas de clic */
enum { A_NADA, A_FOCO, A_NUEVA, A_CONV, A_CONV_MENU, A_SIDE, A_MODO, A_MEMT, A_ENVIAR, A_PARAR, A_ADJ, A_ADJ_QUITA, A_COPIA_MSG, A_REGEN, A_VOZ, A_EDITA,
       A_ZONA, A_SUGIERE, A_ABAJO, A_TITULO, A_EXPORTA, A_M_REN, A_M_BORRA, A_M_FIJA, A_M_EXPORTA, A_OK, A_CANCELA, A_AR_ITEM, A_AR_SUBE, A_AR_DIR, A_REINTENTA,
       A_CERRAR_MENU, A_EDIT_NO, A_VACIAR, A_SCROLLBAR, A_SSCROLL };
struct hit { int x, y, w, h, act; int32_t a, b; struct tfield *f; };
#define NHIT 400
static struct hit H[NHIT]; static int nH;
static void hit(int x, int y, int w, int h, int act, int32_t a, int32_t b) { if (nH < NHIT && w > 0 && h > 0) { struct hit *z = &H[nH++]; z->x = x; z->y = y; z->w = w; z->h = h; z->act = act; z->a = a; z->b = b; z->f = 0; } }
static int hit_en(int mx, int my) { for (int i = nH - 1; i >= 0; i--) if (mx >= H[i].x && my >= H[i].y && mx < H[i].x + H[i].w && my < H[i].y + H[i].h) return i; return -1; }
static int sobre(int x, int y, int w, int h) { return g_rx >= x && g_ry >= y && g_rx < x + w && g_ry < y + h; }
static void boton_ic(int x, int y, int s, int ic, uint32_t col, int act, int32_t a, int32_t b, int fondo)
{
    int ov = sobre(x, y, s, s);
    if (ov || fondo) rra(x, y, s, s, 8, ov ? 0x00FFFFFFu : col, ov ? 0x14 : 0x10);
    icono(ic, x + s / 2, y + s / 2, ov ? C_TX : col, 16);
    hit(x, y, s, s, act, a, b);
}
static int boton(int x, int y, int h, const char *s, int estilo, uint32_t col, int act, int32_t a, int32_t b)   /* 0 contorno, 1 solido; devuelve el ancho */
{
    int w = tw(s, 13, WF_BOLD) + 28, ov = sobre(x, y, w, h);
    if (estilo == 1) { rr(x, y, w, h, h / 2, ov ? mezcla(col, 0xFFFFFF, 30) : col); txc(x, w, y, h, s, 13, 0xFFFFFF, WF_BOLD); }
    else { rra(x, y, w, h, h / 2, 0xFFFFFF, ov ? 0x14 : 0x08); rs(x, y, w, h, h / 2, ov ? C_BDL : C_BD, 255); txc(x, w, y, h, s, 13, C_TX, WF_BOLD); }
    hit(x, y, w, h, act, a, b);
    return w;
}

/* ---------------------------------------------------------------- guardar y cargar */
static void guarda_ajustes(void)
{
    char t[64] = "modo="; snum(t, sizeof t, (uint32_t)g_modo); scat(t, "\nmem=", sizeof t); snum(t, sizeof t, (uint32_t)g_mem);
    scat(t, "\nlateral=", sizeof t); snum(t, sizeof t, (uint32_t)g_side); scat(t, "\n", sizeof t);
    (void)vfs_write(AJ_FILE, t, (uint32_t)slen(t));
}
static void carga(void)
{
    uint32_t n = 0; char *b = vfs_read_alloc(HIST_FILE, &n);
    if (b) { if (cc_carga(&g_h, b, n) < 0) toast("El historial del chat no se pudo leer", 1); kfree(b); }
    char t[80]; int r = vfs_read(AJ_FILE, t, sizeof t - 1);
    if (r > 0) {
        t[r] = 0;
        for (char *p = t; *p; p++) {
            if (pre(p, "modo=")) g_modo = p[5] == '1' ? CC_MODO_AGENTE : CC_MODO_LLM;
            if (pre(p, "mem=")) g_mem = p[4] == '1';
            if (pre(p, "lateral=")) g_side = p[8] == '1';
        }
    }
}
static void ensucia(void) { if (!g_sucio) g_t_sucio = ms(); g_sucio = 1; }
static void guarda(void)
{
    uint32_t need = cc_serializa(&g_h, 0, 0);
    char *b = (char *)kmalloc(need + 1);
    if (!b) { toast("Sin memoria para guardar el historial", 1); return; }
    cc_serializa(&g_h, b, need);
    if (vfs_write(HIST_FILE, b, need) < 0) toast("No se pudo guardar el historial del chat", 1);
    kfree(b); g_sucio = 0;
}

/* ---------------------------------------------------------------- imagenes (cache) */
struct img { char src[200]; int st; uint8_t *data; uint32_t len; uint32_t *px; int w, h; uint64_t uso; };
enum { IM_LIBRE = 0, IM_PEDIR, IM_BAJANDO, IM_BYTES, IM_OK, IM_ERR = -1 };
#define NIMG 16
static struct img g_img[NIMG]; static uint32_t g_img_gen;
static volatile int g_img_hilo;
static struct img *img_busca(const char *s, int n)
{
    for (int i = 0; i < NIMG; i++) if (g_img[i].st != IM_LIBRE && slen(g_img[i].src) == n && eqn(g_img[i].src, s, n)) return &g_img[i];
    return 0;
}
static void img_decodifica(struct img *m)                /* en el hilo de la GUI */
{
    struct wimg w; w.px = 0; w.delay = 0;
    if (!m->data || wi_decode_fit(m->data, m->len, &w, 900, 700) != 0 || !w.px || w.w <= 0 || w.h <= 0) { m->st = IM_ERR; if (w.px) wi_free(&w); }
    else {
        int dw = w.w, dh = w.h;
        if (dw > 900) { dh = dh * 900 / dw; dw = 900; }
        if (dh > 700) { dw = dw * 700 / dh; dh = 700; }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        uint32_t *p = (uint32_t *)kmalloc((uint32_t)(dw * dh) * 4u);
        if (!p) m->st = IM_ERR;
        else {
            for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
                uint32_t c = w.px[(y * w.h / dh) * w.w + (x * w.w / dw)], a = c >> 24;
                if (a != 255) { uint32_t bg = 0x0011151Fu; c = mezcla(bg, c & 0xFFFFFF, (int)a); }
                p[y * dw + x] = c & 0x00FFFFFFu;
            }
            m->px = p; m->w = dw; m->h = dh; m->st = IM_OK;
        }
        wi_free(&w);
    }
    if (m->data) { kfree(m->data); m->data = 0; }
    g_img_gen++; cambia();
}
static void img_una(struct img *m)                      /* baja una imagen (en el hilo chat-img) */
{
    {
        m->st = IM_BAJANDO;
        struct wn_req rq; struct wn_resp rs_; uint8_t *z = (uint8_t *)&rq; for (uint32_t i = 0; i < sizeof rq; i++) z[i] = 0;
        z = (uint8_t *)&rs_; for (uint32_t i = 0; i < sizeof rs_; i++) z[i] = 0;
        char url[200]; scpy(url, m->src, sizeof url);
        rq.url = url; rq.timeout_ms = 20000; rq.max_body = 8u << 20; rq.want_binary = 1; rq.accept = "image/*";
        int rc = wn_fetch(&rq, &rs_);
        if (rc == 0 && rs_.status == 200 && rs_.body && rs_.len > 0) {
            uint8_t *d = (uint8_t *)kmalloc(rs_.len);
            if (d) { mcpy(d, rs_.body, rs_.len); m->data = d; m->len = rs_.len; m->st = IM_BYTES; }
            else m->st = IM_ERR;
        } else m->st = IM_ERR;
        wn_free(&rs_);
        cambia();
    }
}
static void img_hilo(void *arg)
{
    (void)arg;
    for (;;) {
        struct img *m = 0;
        for (int i = 0; i < NIMG; i++) if (g_img[i].st == IM_PEDIR) { m = &g_img[i]; break; }
        if (!m) { thread_sleep(60); continue; }
        img_una(m);
    }
}
static struct img *img_nueva(const char *s, int n)
{
    struct img *m = 0; uint64_t viejo = ~0ull;
    for (int i = 0; i < NIMG; i++) { if (g_img[i].st == IM_LIBRE) { m = &g_img[i]; break; } if (g_img[i].st != IM_BAJANDO && g_img[i].st != IM_PEDIR && g_img[i].uso < viejo) { viejo = g_img[i].uso; m = &g_img[i]; } }
    if (!m) return 0;
    if (m->px) kfree(m->px);
    if (m->data) kfree(m->data);
    m->px = 0; m->data = 0; m->w = m->h = 0; m->st = IM_LIBRE;
    int k = n < (int)sizeof m->src - 1 ? n : (int)sizeof m->src - 1; mcpy(m->src, s, (uint32_t)k); m->src[k] = 0;
    m->uso = ms();
    return m;
}
static int md_img(const char *s, int n, int *w, int *h, void *u)
{
    (void)u;
    if (n <= 0 || n >= 200) return -1;
    struct img *m = img_busca(s, n);
    if (!m) {
        m = img_nueva(s, n); if (!m) return -1;
        if (s[0] == '/') {
            uint32_t len = 0; char *d = vfs_read_alloc(m->src, &len);
            if (!d) m->st = IM_ERR;
            else { m->data = (uint8_t *)d; m->len = len; img_decodifica(m); }
        } else if (pre(m->src, "http://") || pre(m->src, "https://")) {
            m->st = IM_PEDIR;
            if (!g_img_hilo) { g_img_hilo = 1; thread_create("chat-img", img_hilo, 0); }
        } else m->st = IM_ERR;
    }
    m->uso = ms();
    if (m->st == IM_OK) { *w = m->w; *h = m->h; return 0; }
    return m->st == IM_ERR ? -1 : 1;
}

/* ---------------------------------------------------------------- maquetacion de mensajes (cache) */
#define LOPS  20000
#define LPOOL (768u * 1024u)
#define LZON  1500
static struct md_op *g_ops; static char *g_pool; static struct md_zone *g_zs; static void *g_scr;
/* contenido: Liberation Sans (cubre los simbolos matematicos, flechas y griego; lo que no tenga sale de DejaVu); codigo: DejaVu Mono */
static int cfl(int fl) { return fl | WF_RAW | ((fl & WF_MONO) ? 0 : WF_LIB); }
static int m_w(const char *s, int n, int px, int fl, void *u) { (void)u; return twn(s, n, px, cfl(fl)); }
static int m_a(int px, void *u) { (void)u; return has_wf() ? wf_ascent(px) : px * 8 / 10; }
static int m_d(int px, void *u) { (void)u; return has_wf() ? wf_descent(px) : px / 4; }
static const struct md_metrics MET = { m_w, m_a, m_d, md_img, 0 };
struct lay { const char *txt; uint32_t n; int w, h, cw; uint32_t gen; struct md_op *ops; int nops; char *pool; struct md_zone *z; int nz; };
static struct lay *g_lay; static uint32_t g_nlay, g_lay_conv;
static void lay_libera(struct lay *l) { if (l->ops) kfree(l->ops); if (l->pool) kfree(l->pool); if (l->z) kfree(l->z); l->ops = 0; l->pool = 0; l->z = 0; l->nops = l->nz = 0; l->txt = 0; }
static void lay_reset(void) { for (uint32_t i = 0; i < g_nlay; i++) lay_libera(&g_lay[i]); }
static int base_px(void) { return wf_ui_px(16); }
static int lay_mem(void)                                 /* la memoria de trabajo del maquetador (~2 MB) se pide al primer uso */
{
    if (g_ops) return 1;
    g_ops = (struct md_op *)kmalloc(LOPS * sizeof(struct md_op)); g_pool = (char *)kmalloc(LPOOL);
    g_zs = (struct md_zone *)kmalloc(LZON * sizeof(struct md_zone)); g_scr = kmalloc(MD_SCRATCH);
    if (g_ops && g_pool && g_zs && g_scr) return 1;
    if (g_ops) kfree(g_ops);
    if (g_pool) kfree(g_pool);
    if (g_zs) kfree(g_zs);
    if (g_scr) kfree(g_scr);
    g_ops = 0; g_pool = 0; g_zs = 0; g_scr = 0; return 0;
}
static struct lay *lay_msg(const struct cc_conv *c, uint32_t i, int w)
{
    if (!lay_mem()) return 0;
    if (g_lay_conv != c->id) { lay_reset(); g_lay_conv = c->id; }
    if (c->nm > g_nlay) {
        uint32_t nn = c->nm + 32; struct lay *nl = (struct lay *)kmalloc(nn * sizeof(struct lay)); if (!nl) return 0;
        uint8_t *z = (uint8_t *)nl; for (uint32_t k = 0; k < nn * sizeof(struct lay); k++) z[k] = 0;
        if (g_lay) { mcpy(nl, g_lay, g_nlay * sizeof(struct lay)); kfree(g_lay); }
        g_lay = nl; g_nlay = nn;
    }
    struct lay *l = &g_lay[i]; const struct cc_msg *m = &c->m[i];
    if (l->txt == m->txt && l->n == m->n && l->w == w && (l->gen == g_img_gen || !l->nz)) return l;
    lay_libera(l);
    struct md_out o; o.ops = g_ops; o.cap = LOPS; o.pool = g_pool; o.poolcap = LPOOL; o.z = g_zs; o.zcap = LZON;
    md_layout(m->txt, m->n, w, base_px(), &MET, &TEMA, &o, g_scr);
    l->txt = m->txt; l->n = m->n; l->w = w; l->h = o.height; l->cw = o.width; l->gen = g_img_gen;
    if (o.nops) { l->ops = (struct md_op *)kmalloc((uint32_t)o.nops * sizeof(struct md_op)); if (l->ops) { mcpy(l->ops, g_ops, (uint32_t)o.nops * sizeof(struct md_op)); l->nops = o.nops; } }
    if (o.npool) { l->pool = (char *)kmalloc(o.npool); if (l->pool) mcpy(l->pool, g_pool, o.npool); else l->nops = 0; }
    if (o.nz) { l->z = (struct md_zone *)kmalloc((uint32_t)o.nz * sizeof(struct md_zone)); if (l->z) { mcpy(l->z, g_zs, (uint32_t)o.nz * sizeof(struct md_zone)); l->nz = o.nz; } }
    if (!l->ops) l->nops = 0;
    return l;
}
static void trunca(struct cc_conv *c, uint32_t desde) { cc_trunca(c, desde); if (c->id == g_lay_conv) lay_reset(); }
static int g_hz_msg = -1, g_hz_zone = -1;
/* SOMA_286_YOUTUBE: barras de desplazamiento que se arrastran (antes solo se pintaban, y pegadas al borde de la ventana, donde el
 * clic lo coge el redimensionado). Se agarra el tirador donde se pulsa; pulsar fuera de el lo centra ahi. */
struct barra { int y, h, th, ty, max; int *val; };
static struct barra g_b_main, g_b_side; static struct barra *g_arrastra; static int g_agarre;
static void barra_pon(struct barra *b, int y, int h, int max, int *val)
{
    b->y = y; b->h = h; b->max = max; b->val = val;
    b->th = max > 0 ? h * h / (h + max) : h; if (b->th < 30) b->th = 30; if (b->th > h) b->th = h;
    b->ty = y + (max > 0 ? (h - b->th) * *val / max : 0);
}
static void barra_pulsa(struct barra *b, int my)
{
    g_arrastra = b;
    g_agarre = (my >= b->ty && my < b->ty + b->th) ? my - b->ty : b->th / 2;
}
static void barra_mueve(struct barra *b, int my)
{
    if (b->max <= 0 || b->h <= b->th) return;
    int t = my - g_agarre - b->y; if (t < 0) t = 0; if (t > b->h - b->th) t = b->h - b->th;
    *b->val = (int)((int64_t)t * b->max / (b->h - b->th));
}                 /* zona bajo el raton (para resaltar enlaces) */
static void pinta_md(const struct lay *l, int ox, int oy, int msg, int y0, int y1)
{
    for (int i = 0; i < l->nops; i++) {
        const struct md_op *p = &l->ops[i];
        int hov = p->zone >= 0 && msg == g_hz_msg && p->zone == g_hz_zone;
        switch (p->type) {
        case MD_TEXT: {
            int y = oy + p->y; if (y + p->px < y0 || y - p->px > y1) break;
            uint32_t col = hov ? 0x00FFFFFFu : p->rgb;
            if (has_wf()) wf_draw(ox + p->x, y, l->pool + p->s, p->n, p->px, col, cfl(p->fl), 255);
            break; }
        case MD_RECT: { if (oy + p->y > y1 || oy + p->y + p->h < y0) break; uint32_t a = p->rgb >> 24; if (!a) a = 255; fb_fill_rrect_aa(ox + p->x, oy + p->y, p->w, p->h, p->px, (a << 24) | (p->rgb & 0xFFFFFF)); break; }
        case MD_STROKE: if (oy + p->y > y1 || oy + p->y + p->h < y0) break; fb_stroke_rrect_aa(ox + p->x, oy + p->y, p->w, p->h, p->px, 0xFF000000u | p->rgb); break;
        case MD_LINE: {
            int ya = oy + p->y / 16, yb = oy + (p->y + p->h) / 16; if ((ya < y0 && yb < y0) || (ya > y1 && yb > y1)) break;
            uint32_t a = p->rgb >> 24; if (!a) a = 255; if (hov) a = 255;
            fb_line_aa(ox * 16 + p->x, oy * 16 + p->y, ox * 16 + p->x + p->w, oy * 16 + p->y + p->h, p->px, (a << 24) | (hov ? 0xFFFFFFu : (p->rgb & 0xFFFFFF)));
            break; }
        case MD_IMG: {
            int x = ox + p->x, y = oy + p->y; if (y > y1 || y + p->h < y0) break;
            struct img *m = img_busca(l->pool + p->s, (int)p->n);
            if (m && m->st == IM_OK && m->px) {
                fb_blit_scale(x, y, p->w, p->h, m->px, m->w, m->h, m->w);
                rs(x, y, p->w, p->h, 10, C_BD, 255);
                if (hov) rs(x - 1, y - 1, p->w + 2, p->h + 2, 11, C_ACC, 255);
            } else {
                rr(x, y, p->w, p->h, 12, C_CARD); rs(x, y, p->w, p->h, 12, C_BD, 255);
                int err = !m || m->st == IM_ERR;
                icono(I_IMG, x + p->w / 2, y + p->h / 2 - 14, err ? C_RED : C_MU, 22);
                txc(x, p->w, y + p->h / 2, 22, err ? "No se pudo cargar la imagen" : "Cargando imagen\342\200\246", 12, err ? C_RED : C_MU, 0);
                char s[80]; int k = (int)p->n < 79 ? (int)p->n : 79; mcpy(s, l->pool + p->s, (uint32_t)k); s[k] = 0;
                txc(x + 8, p->w - 16, y + p->h / 2 + 20, 18, s, 11, C_FAINT, 0);
            }
            break; }
        }
    }
}

/* ---------------------------------------------------------------- turnos (Solo LLM: hilo chat-llm; Agente: hilo del agente) */
enum { T_NO, T_LLM, T_AGENTE };
static int g_turno; static uint32_t g_turno_conv; static uint64_t g_turno_t0;
static volatile int g_w_estado, g_w_cancel;              /* 0 libre, 1 pedido, 2 trabajando, 3 hecho */
static volatile int g_llm_hilo;
static char *g_w_blob; static struct llm_msg *g_w_msgs; static int g_w_nm; static int g_w_mem; static char g_w_adj[ADJ_MAX][160]; static int g_w_nadj;
static char *g_w_resp; static int g_w_ok; static char g_w_err[220];
#define RESP_CAP (256u * 1024u)
static char g_fase_txt[8][72]; static volatile int g_nfase; static volatile uint32_t g_fase_ver;
static char g_ag_hist[6144];
static uint32_t g_rev_conv; static int g_rev_idx = -1; static uint64_t g_rev_t0;   /* respuesta que se esta desvelando */

static const char SYS_CHAT[] =
    "Eres SOMA, la inteligencia artificial del sistema operativo SOMA. Conversas con el usuario en una aplicaci\303\263n de chat que muestra "
    "Markdown con estilo. Responde en el idioma del usuario (normalmente espa\303\261ol), con claridad y buena organizaci\303\263n: t\303\255tulos (##) "
    "y listas cuando ayuden, tablas para comparar, **negrita** para lo importante, bloques de c\303\263digo con su lenguaje (```python), f\303\263rmulas "
    "LaTeX con $...$ en l\303\255nea y $$...$$ aparte, y enlaces con [texto](url). Para respuestas cortas, escribe corto. No inventes datos: si no lo "
    "sabes, dilo. Si te pasan archivos, est\303\241n al final de la pregunta.";
static int es_imagen(const uint8_t *d, uint32_t n, const char **mime)
{
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') { *mime = "image/png"; return 1; }
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) { *mime = "image/jpeg"; return 1; }
    if (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') { *mime = "image/gif"; return 1; }
    if (n >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' && d[9] == 'E') { *mime = "image/webp"; return 1; }
    return 0;
}
static int es_texto(const uint8_t *d, uint32_t n) { uint32_t m = n < 4096 ? n : 4096; for (uint32_t i = 0; i < m; i++) if (d[i] == 0) return 0; return 1; }
static void fecha_larga(char *d, int cap)
{
    static const char *const MES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    uint32_t t = ahora(); int32_t z = (int32_t)(t / 1440) + 719468, era = z / 146097; uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int32_t y = (int32_t)yoe + era * 400; uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9; if (mo <= 2) y++;
    d[0] = 0; snum(d, cap, dd); scat(d, " de ", cap); scat(d, MES[mo - 1], cap); scat(d, " de ", cap); snum(d, cap, (uint32_t)y);
    char h[8]; cc_hora(t, h); scat(d, ", ", cap); scat(d, h, cap);
}
static void llm_una(void)                               /* una peticion del modo Solo LLM (en el hilo chat-llm) */
{
    {
        g_w_estado = 2; g_w_ok = 0; g_w_err[0] = 0;
        uint32_t scap = 16384; char *sys = (char *)kmalloc(scap);
        struct llm_img imgs[3]; int nimg = 0; char *imgbuf[3] = { 0, 0, 0 };
        char *ultimo = 0;
        if (!sys || !g_w_resp) { scpy(g_w_err, "Sin memoria para la petici\303\263n", sizeof g_w_err); goto fin; }
        {
            uint32_t pos = 0; char f[64]; fecha_larga(f, sizeof f);
            scpy(sys, SYS_CHAT, (int)scap); scat(sys, "\nFecha y hora actuales: ", (int)scap); scat(sys, f, (int)scap); scat(sys, ".", (int)scap);
            pos = (uint32_t)slen(sys);
            const char *pregunta = g_w_nm > 0 ? g_w_msgs[g_w_nm - 1].text : "";
            if (g_w_mem && memoria_activa()) {
                const char *cab = "\n\nLo que recuerdas del usuario y de otras conversaciones (\303\272salo solo si viene al caso; no lo recites):\n";
                uint32_t p0 = pos; scat(sys, cab, (int)scap); pos = (uint32_t)slen(sys);
                uint32_t antes = pos;
                (void)memoria_contexto(pregunta, sys, scap - 64, &pos);
                if (pos <= antes) { pos = p0; sys[pos] = 0; }
                else sys[pos] = 0;
            }
        }
        /* adjuntos: textos dentro de la ultima pregunta, imagenes aparte */
        if (g_w_nadj && g_w_nm > 0) {
            const char *q = g_w_msgs[g_w_nm - 1].text; uint32_t ql = (uint32_t)slen(q);
            uint32_t cap = ql + 4096; char *txts[ADJ_MAX]; uint32_t tl[ADJ_MAX];
            for (int i = 0; i < g_w_nadj; i++) {
                txts[i] = 0; tl[i] = 0;
                uint32_t n = 0; char *d = vfs_read_alloc(g_w_adj[i], &n);
                if (!d) continue;
                const char *mime;
                if (es_imagen((const uint8_t *)d, n, &mime) && nimg < 3 && n <= (6u << 20)) { imgbuf[nimg] = d; imgs[nimg].data = (const uint8_t *)d; imgs[nimg].len = n; imgs[nimg].mime = mime; nimg++; continue; }
                if (es_texto((const uint8_t *)d, n)) { if (n > 60000) n = 60000; txts[i] = d; tl[i] = n; cap += n + 256; continue; }
                kfree(d);
            }
            ultimo = (char *)kmalloc(cap);
            if (ultimo) {
                scpy(ultimo, q, (int)cap);
                for (int i = 0; i < g_w_nadj; i++) {
                    if (!txts[i]) continue;
                    scat(ultimo, "\n\n--- Archivo adjunto: ", (int)cap); scat(ultimo, g_w_adj[i], (int)cap); scat(ultimo, " ---\n", (int)cap);
                    uint32_t k = (uint32_t)slen(ultimo); mcpy(ultimo + k, txts[i], tl[i]); ultimo[k + tl[i]] = 0;
                    if (tl[i] == 60000) scat(ultimo, "\n[... recortado ...]", (int)cap);
                    scat(ultimo, "\n--- fin del archivo ---", (int)cap);
                }
                g_w_msgs[g_w_nm - 1].text = ultimo;
            }
            for (int i = 0; i < g_w_nadj; i++) if (txts[i]) kfree(txts[i]);
        }
        llm_trace_tag("chat");
        {
            int r = llm_chat_hist(sys, g_w_msgs, g_w_nm, imgs, nimg, 0, g_w_resp, RESP_CAP, 600000, &g_w_cancel);
            if (r == -2 && nimg) {
                r = llm_chat_hist(sys, g_w_msgs, g_w_nm, 0, 0, 0, g_w_resp, RESP_CAP - 200, 600000, &g_w_cancel);
                if (r == 1) scat(g_w_resp, "\n\n> [!NOTE]\n> El modelo actual no admite im\303\241genes: he respondido sin verlas.", (int)RESP_CAP);
            }
            if (r == 1) g_w_ok = 1;
            else if (g_w_cancel) scpy(g_w_err, "Respuesta detenida.", sizeof g_w_err);
            else { scpy(g_w_err, llm_last_error[0] ? llm_last_error : "El modelo de lenguaje no ha contestado.", sizeof g_w_err); }
        }
        if (g_w_ok && !g_w_cancel && g_w_mem && memoria_activa() && g_w_nm > 0)
            memoria_registra(g_w_blob, g_w_resp);               /* la pregunta del usuario (sin los adjuntos; va primero en el blob) y la respuesta */
    fin:
        if (sys) kfree(sys);
        if (ultimo) kfree(ultimo);
        for (int i = 0; i < 3; i++) if (imgbuf[i]) kfree(imgbuf[i]);
        g_w_estado = 3;
        cambia();
    }
}
static void llm_hilo(void *arg)
{
    (void)arg;
    for (;;) { if (g_w_estado != 1) { thread_sleep(30); continue; } llm_una(); }
}

static struct cc_conv *conv_actual(void) { return g_cur ? cc_por_id(&g_h, g_cur) : 0; }
static void falla(struct cc_conv *c, const char *t)                 /* el turno no llega a empezar: queda un aviso con Reintentar */
{
    struct cc_msg *m = cc_anade(c, CC_ASIS, t, (uint32_t)slen(t), 0, ahora());
    if (m) { m->estado = CC_ERROR; m->modo = c->modo; }
    ensucia(); cambia();
}
static int ocupado(void) { return g_turno != T_NO; }
/* el texto con el que se registra la pregunta en la memoria: va primero en el blob */
static void turno_empieza(struct cc_conv *c)
{
    uint32_t hasta = c->nm;                              /* la ultima es la pregunta */
    const struct cc_msg *q = &c->m[hasta - 1];
    g_turno_conv = c->id; g_turno_t0 = ms(); g_nfase = 0; g_fase_ver++;
    g_pegado = 1;
    if (c->modo == CC_MODO_AGENTE) {
        if (gui_agent_busy()) { falla(c, "El agente est\303\241 ocupado con otra misi\303\263n (desde el orbe o una aplicaci\303\263n). Espera a que termine y pulsa Reintentar."); return; }
        (void)cc_contexto(c, hasta - 1, g_ag_hist, sizeof g_ag_hist);
        uint32_t cap = q->n + 1200; char *mis = (char *)kmalloc(cap);
        if (!mis) { falla(c, "Sin memoria para preparar la misi\303\263n."); return; }
        mcpy(mis, q->txt, q->n); mis[q->n] = 0;
        if (q->adj && q->adj[0]) {
            scat(mis, "\n\n[Archivos adjuntos por el usuario: ", (int)cap);
            const char *a = q->adj; char r[160]; int first = 1;
            while (*a) { int k = 0; while (a[k] && a[k] != '\n') k++; int m = k < 159 ? k : 159; mcpy(r, a, (uint32_t)m); r[m] = 0; if (!first) scat(mis, ", ", (int)cap); scat(mis, r, (int)cap); first = 0; a += k; if (*a) a++; }
            scat(mis, "]", (int)cap);
        }
        agent_chat_contexto(g_ag_hist[0] ? g_ag_hist : 0, !c->mem);
        int rc = gui_agent_submit_chat(mis);
        kfree(mis);
        if (rc != 0) { agent_chat_contexto(0, 0); falla(c, "El agente est\303\241 ocupado. Pulsa Reintentar en un momento."); return; }
        g_turno = T_AGENTE; cambia(); return;
    }
    /* Solo LLM */
    if (!g_llm_hilo) { g_llm_hilo = 1; thread_create("chat-llm", llm_hilo, 0); }
    if (g_w_estado != 0) { falla(c, "La petici\303\263n anterior (detenida) a\303\272n no ha terminado en el servidor. Pulsa Reintentar en un momento."); return; }
    if (g_w_blob) { kfree(g_w_blob); g_w_blob = 0; }
    if (g_w_msgs) { kfree(g_w_msgs); g_w_msgs = 0; }
    const struct llm_tam *tm = llm_tam();
    uint32_t presu = tm && tm->contexto ? tm->contexto * 3 : 24000u; if (presu > 400000u) presu = 400000u;
    if (presu > 8000) presu -= 6000;
    uint32_t v = cc_ventana(c, hasta, presu), tot = q->n + 1;
    for (uint32_t i = v; i < hasta; i++) tot += c->m[i].n + 1;
    int nm = (int)(hasta - v);
    g_w_blob = (char *)kmalloc(tot + 16); g_w_msgs = (struct llm_msg *)kmalloc((uint32_t)(nm + 1) * sizeof(struct llm_msg));
    if (!g_w_resp) g_w_resp = (char *)kmalloc(RESP_CAP);
    if (!g_w_blob || !g_w_msgs || !g_w_resp) { falla(c, "Sin memoria para enviar la conversaci\303\263n."); return; }
    uint32_t p = 0;
    mcpy(g_w_blob, q->txt, q->n); g_w_blob[q->n] = 0; p = q->n + 1;          /* copia de la pregunta para la memoria */
    int k = 0;
    for (uint32_t i = v; i < hasta; i++) {
        const struct cc_msg *m = &c->m[i];
        if (m->rol == CC_ASIS && m->estado != CC_OK) continue;
        mcpy(g_w_blob + p, m->txt, m->n); g_w_blob[p + m->n] = 0;
        g_w_msgs[k].rol = m->rol == CC_USER ? LLM_ROL_USER : LLM_ROL_ASSISTANT; g_w_msgs[k].text = g_w_blob + p; k++;
        p += m->n + 1;
    }
    g_w_nm = k; g_w_mem = c->mem; g_w_nadj = 0;
    if (q->adj) { const char *a = q->adj; while (*a && g_w_nadj < ADJ_MAX) { int n = 0; while (a[n] && a[n] != '\n') n++; int m = n < 159 ? n : 159; mcpy(g_w_adj[g_w_nadj], a, (uint32_t)m); g_w_adj[g_w_nadj][m] = 0; g_w_nadj++; a += n; if (*a) a++; } }
    g_w_cancel = 0; g_w_resp[0] = 0;
    g_w_estado = 1; g_turno = T_LLM; cambia();
}
static void turno_acaba(const char *txt, int ok, const char *err)
{
    struct cc_conv *c = cc_por_id(&g_h, g_turno_conv);
    uint32_t dur = (uint32_t)(ms() - g_turno_t0);
    int modo = g_turno == T_AGENTE ? CC_MODO_AGENTE : CC_MODO_LLM;
    g_turno = T_NO;
    if (!c) return;
    const char *t = ok ? txt : err;
    struct cc_msg *m = cc_anade(c, CC_ASIS, t, (uint32_t)slen(t), 0, ahora());
    if (m) {
        m->dur = dur; m->estado = ok ? CC_OK : (err && pre(err, "Respuesta detenida") ? CC_CORTADA : CC_ERROR); m->modo = (uint8_t)modo;
        if (c->id == g_cur && ok) { g_rev_conv = c->id; g_rev_idx = (int)c->nm - 1; g_rev_t0 = ms(); }
    }
    ensucia(); cambia();
}
static void envia(const char *texto)
{
    while (*texto == ' ' || *texto == '\n') texto++;
    int n = slen(texto); while (n > 0 && (texto[n - 1] == ' ' || texto[n - 1] == '\n')) n--;
    if (n == 0 && !g_nadj) return;
    if (ocupado()) { toast("Espera a que termine la respuesta (o p\303\241rala)", 1); return; }
    struct cc_conv *c = conv_actual();
    if (c && g_edit_idx >= 0 && (uint32_t)g_edit_idx < c->nm) trunca(c, (uint32_t)g_edit_idx);
    g_edit_idx = -1;
    if (!c) {
        c = cc_nueva(&g_h, ahora(), g_modo, g_mem);
        if (!c) { toast("Hay demasiadas conversaciones: borra alguna", 1); return; }
        g_cur = c->id;
    }
    char adj[ADJ_MAX * 162]; adj[0] = 0;
    for (int i = 0; i < g_nadj; i++) { if (i) scat(adj, "\n", sizeof adj); scat(adj, g_adj[i], sizeof adj); }
    if (!cc_anade(c, CC_USER, texto, (uint32_t)n, adj[0] ? adj : 0, ahora())) { toast("No cabe el mensaje", 1); return; }
    c->modo = (uint8_t)g_modo; c->mem = (uint8_t)g_mem;
    c->m[c->nm - 1].modo = c->modo; c->m[c->nm - 1].mem = c->mem;
    cc_titulo_auto(c);
    g_nadj = 0; tf_set(&f_comp, "");
    ensucia();
    turno_empieza(c);
}
static void regenera(uint32_t idx)
{
    struct cc_conv *c = conv_actual(); if (!c || ocupado() || idx >= c->nm || idx == 0) return;
    trunca(c, idx);
    if (!c->nm || c->m[c->nm - 1].rol != CC_USER) return;
    c->modo = (uint8_t)g_modo; c->mem = (uint8_t)g_mem;
    ensucia(); turno_empieza(c);
}
static void parar(void)
{
    /* SOMA_286_YOUTUBE: con el servidor local la peticion no se puede cortar a medias: el turno se cierra ya y lo que llegue se tira */
    if (g_turno == T_LLM) { g_w_cancel = 1; turno_acaba(0, 0, "Respuesta detenida."); }
    else if (g_turno == T_AGENTE) (void)gui_agent_cancel();
}
void chat_fase(const char *que)
{
    static const struct { const char *k, *t; } F[] = {
        { "lcm", "Entendiendo la petici\303\263n" }, { "mayordomo", "El mayordomo prepara las \303\263rdenes" }, { "buscar", "Buscando en la web" },
        { "documento", "Maquetando un documento" }, { "programador", "Programando" }, { "orden", "Ejecutando \303\263rdenes" }, { "pensar", "Pensando" },
        { "escribir", "Escribiendo la respuesta" }, { 0, 0 } };
    if (g_turno != T_AGENTE || !que) return;
    const char *t = 0; for (int i = 0; F[i].k; i++) { int j = 0; while (F[i].k[j] && F[i].k[j] == que[j]) j++; if (!F[i].k[j] && !que[j]) { t = F[i].t; break; } }
    if (!t) return;
    int n = g_nfase;
    if (n && eqn(g_fase_txt[n - 1], t, 72)) return;
    if (n >= 8) { for (int i = 1; i < 8; i++) scpy(g_fase_txt[i - 1], g_fase_txt[i], 72); n = 7; }
    scpy(g_fase_txt[n], t, 72); g_nfase = n + 1; g_fase_ver++;
}
void chat_fase_orden(const char *linea)
{
    if (g_turno != T_AGENTE || !linea) return;
    char t[72] = "$ "; scat(t, linea, 66);
    int n = g_nfase;
    if (n >= 8) { for (int i = 1; i < 8; i++) scpy(g_fase_txt[i - 1], g_fase_txt[i], 72); n = 7; }
    scpy(g_fase_txt[n], t, 72); g_nfase = n + 1; g_fase_ver++;
}
int chat_agente_fin(const char *resp)
{
    if (g_turno != T_AGENTE) return 0;
    agent_chat_contexto(0, 0);
    int ok = resp && resp[0] && !pre(resp, "Mision detenida");
    turno_acaba(ok ? resp : 0, ok, resp && pre(resp, "Mision detenida") ? "Respuesta detenida." : "El agente no ha devuelto respuesta.");
    return 1;
}

/* ---------------------------------------------------------------- selector de archivos */
static void lista_carga(void)
{
    if (!g_lista) g_lista = (char *)kmalloc(16384);
    if (!g_lista) return;
    g_nlista = 0; g_ascroll = 0;
    char *raw = (char *)kmalloc(16384); if (!raw) return;
    int n = vfs_format_list_ext(1, g_dir, raw, 16383); if (n < 0) n = 0; raw[n] = 0;
    const char *p = raw; if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p) p++; }
    int q = 0, dl = slen(g_dir);
    while (*p && g_nlista < 400) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char e[200]; int k = 0; while (*p && *p != ',') { if (k < 199) e[k++] = *p; p++; }
        e[k] = 0; while (k && e[k - 1] == ' ') e[--k] = 0;
        if (!k) continue;
        /* solo lo que cuelga directamente de g_dir */
        const char *rel = e;
        if (dl > 1) { if (!eqn(e, g_dir, dl) || e[dl] != '/') continue; rel = e + dl + 1; }
        else if (e[0] == '/') rel = e + 1;
        int sl = 0; for (int i = 0; rel[i]; i++) if (rel[i] == '/' && rel[i + 1]) sl = 1;
        if (sl || !rel[0]) continue;
        int rl = slen(rel); if (q + rl + 2 > 16000) break;
        mcpy(g_lista + q, rel, (uint32_t)rl + 1); q += rl + 1; g_nlista++;
    }
    kfree(raw);
}
static void abre_archivos(void) { g_modal = M_ARCH; lista_carga(); cambia(); }
static void anade_adjunto(const char *ruta)
{
    if (g_nadj >= ADJ_MAX) { toast("Como mucho 6 adjuntos por mensaje", 1); return; }
    for (int i = 0; i < g_nadj; i++) if (eqn(g_adj[i], ruta, 160)) return;
    scpy(g_adj[g_nadj++], ruta, 160);
}

/* ---------------------------------------------------------------- init */
void chat_init(void)
{
    if (g_ini) return;
    g_ini = 1;
    cc_init(&g_h);
    b_comp = (char *)kmalloc(COMP_CAP); if (b_comp) b_comp[0] = 0;
    tf_init(&f_comp, b_comp ? b_comp : b_in, b_comp ? COMP_CAP : sizeof b_in, 1); f_comp.bare = 1;
    tf_init(&f_busca, b_busca, sizeof b_busca, 0); f_busca.bare = 1;
    tf_init(&f_in, b_in, sizeof b_in, 0);
    carga();
    g_foco = &f_comp;
    /* abre la ultima conversacion de hoy, si la hay */
    uint32_t ids[1];
    if (cc_orden(&g_h, 0, ids, 1) == 1) { struct cc_conv *c = cc_por_id(&g_h, ids[0]); if (c && !c->fija && c->t1 / 1440 == ahora() / 1440) { g_cur = c->id; g_modo = c->modo; g_mem = c->mem; } }
}

/* ---------------------------------------------------------------- pintado: lateral */
static void pinta_lateral(int x, int y, int w, int h)
{
    rr(x, y, w, h, 0, C_SIDE);
    ln(x + w - 1, y, x + w - 1, y + h, C_BD, 16);
    /* cabecera */
    orbe(x + 26, y + 28, 11, 120);
    tx(x + 44, y + 14, 28, "SOMA", 16, C_TX, WF_BOLD);
    boton_ic(x + w - 40, y + 14, 28, I_SIDE, C_MU, A_SIDE, 0, 0, 0);
    /* nueva conversacion */
    int by = y + 54; int ov = sobre(x + 12, by, w - 24, 38);
    rra(x + 12, by, w - 24, 38, 10, ov ? 0x00FFFFFFu : C_ACC, ov ? 0x18 : 0x22); rs(x + 12, by, w - 24, 38, 10, C_ACC, ov ? 255 : 0x90);
    icono(I_NEW, x + 32, by + 19, C_TX, 16); tx(x + 50, by, 38, "Nueva conversaci\303\263n", 13, C_TX, WF_BOLD);
    hit(x + 12, by, w - 24, 38, A_NUEVA, 0, 0);
    /* busqueda */
    int sy = by + 50;
    rr(x + 12, sy, w - 24, 34, 9, C_INP); rs(x + 12, sy, w - 24, 34, 9, g_foco == &f_busca ? C_ACC : C_BD, 255);
    icono(I_LUPA, x + 28, sy + 17, C_MU, 14);
    tf_render(&f_busca, x + 34, sy + 3, w - 50, 28, g_foco == &f_busca, "Buscar conversaciones", C_TX);
    { int i = nH; hit(x + 12, sy, w - 24, 34, A_FOCO, 0, 0); if (i < nH) H[i].f = &f_busca; }
    /* lista */
    int ly = sy + 46, lh = y + h - 52 - ly;
    static uint32_t ids[CC_CONV_MAX];
    int n = cc_orden(&g_h, b_busca, ids, CC_CONV_MAX);
    int tot = 0, prev = -1; uint32_t hoy = ahora();
    for (int i = 0; i < n; i++) { struct cc_conv *c = cc_por_id(&g_h, ids[i]); int gr = cc_grupo(c, hoy); if (gr != prev) { tot += 30; prev = gr; } tot += 36; }
    g_smax = tot - lh; if (g_smax < 0) g_smax = 0;
    if (g_sscroll > g_smax) g_sscroll = g_smax;
    if (g_sscroll < 0) g_sscroll = 0;
    fb_set_clip(x, ly, w - 1, lh);
    int yy = ly - g_sscroll; prev = -1;
    if (!n) txc(x, w, ly + 20, 24, b_busca[0] ? "Nada coincide" : "A\303\272n no hay conversaciones", 12, C_FAINT, 0);
    for (int i = 0; i < n; i++) {
        struct cc_conv *c = cc_por_id(&g_h, ids[i]); int gr = cc_grupo(c, hoy);
        if (gr != prev) { prev = gr; if (yy + 30 > ly && yy < ly + lh) tx(x + 20, yy + 8, 22, cc_grupo_nombre(gr), 11, C_FAINT, WF_BOLD); yy += 30; }
        if (yy + 36 > ly && yy < ly + lh) {
            int sel = c->id == g_cur, ov = sobre(x + 8, yy, w - 16, 34) && g_ry >= ly && g_ry < ly + lh;
            if (sel || ov) rr(x + 8, yy, w - 16, 34, 9, sel ? C_SEL : C_SIDEH);
            int icx = x + 24; icono(c->modo == CC_MODO_AGENTE ? I_AGENT : I_CHAT, icx, yy + 17, sel ? C_CYAN : C_FAINT, 14);
            const char *t = c->titulo[0] ? c->titulo : "Conversaci\303\263n nueva";
            int tw_ = w - 16 - 34 - ((ov || sel) ? 34 : 12);
            txe(x + 38, yy, 34, t, 13, sel ? C_TX : C_TX2, sel ? WF_BOLD : 0, tw_);
            if (c->fija && !(ov || sel)) icono(I_PIN, x + w - 26, yy + 17, C_FAINT, 12);
            int hy = yy < ly ? ly : yy, hh = (yy + 34 > ly + lh ? ly + lh : yy + 34) - hy;
            hit(x + 8, hy, w - 16, hh, A_CONV, (int32_t)c->id, 0);
            if (ov || sel) { int bx = x + w - 42; if (yy >= ly && yy + 34 <= ly + lh) boton_ic(bx, yy + 4, 26, I_DOTS, C_MU, A_CONV_MENU, (int32_t)c->id, yy + 30, 0); }
        }
        yy += 36;
    }
    fb_reset_clip(); fb_set_clip(g_wx, g_wy, g_ww, g_wh);
    if (g_smax > 0) {
        barra_pon(&g_b_side, ly, lh, g_smax, &g_sscroll);
        int ov = sobre(x + w - 16, ly, 14, lh) || g_arrastra == &g_b_side;
        rra(x + w - (ov ? 11 : 9), g_b_side.ty, ov ? 7 : 4, g_b_side.th, 3, C_BDL, ov ? 255 : 200);
        hit(x + w - 16, ly, 14, lh, A_SSCROLL, 0, 0);
    }
    /* pie: modelo */
    int fy = y + h - 52;
    ln(x + 12, fy, x + w - 12, fy, C_BD, 16);
    const struct llm_conf *lc = llm_conf_get();
    char mod[96] = ""; if (lc) { scpy(mod, lc->model[lc->prov], sizeof mod); }
    rr(x + 14, fy + 12, 28, 28, 14, C_CARD2); icono(I_AGENT, x + 28, fy + 26, C_CYAN, 14);
    txe(x + 50, fy + 8, 20, mod[0] ? mod : "sin modelo", 12, C_TX2, WF_BOLD, w - 64);
    txe(x + 50, fy + 26, 18, lc ? llm_prov_label(lc->prov) : "", 11, C_FAINT, 0, w - 64);
}

/* ---------------------------------------------------------------- pintado: mensajes */
struct geo { int x, w, cx, cw; };                       /* columna de mensajes */
static int alto_msg(const struct cc_conv *c, uint32_t i, const struct geo *g, struct lay **lo)
{
    const struct cc_msg *m = &c->m[i];
    if (m->rol == CC_USER) {
        int maxw = g->cw * 78 / 100 - 36; if (maxw < 120) maxw = 120;
        struct lay *l = lay_msg(c, i, maxw); *lo = l;
        int h = (l ? l->h : 20) + 24;
        if (m->adj && m->adj[0]) h += 44;
        return h + 34;
    }
    struct lay *l = lay_msg(c, i, g->cw - 46); *lo = l;
    int h = 28 + (l ? l->h : 20) + 40;
    if (m->estado != CC_OK) h = 28 + 70 + 40;
    return h + 14;
}
static void pinta_msg(const struct cc_conv *c, uint32_t i, const struct geo *g, int y, struct lay *l, int y0, int y1)
{
    const struct cc_msg *m = &c->m[i];
    int ultimo = i + 1 == c->nm;
    if (m->rol == CC_USER) {
        int maxw = g->cw * 78 / 100 - 36; if (maxw < 120) maxw = 120;
        int bw = (l ? l->cw : 40) + 36; if (bw < 60) bw = 60; if (bw > maxw + 36) bw = maxw + 36;
        int bx = g->cx + g->cw - bw, yy = y;
        if (m->adj && m->adj[0]) {                       /* adjuntos, alineados a la derecha */
            const char *a = m->adj; int ax = g->cx + g->cw;
            char r[160]; int cnt = 0;
            const char *ps[ADJ_MAX]; int pl[ADJ_MAX];
            while (*a && cnt < ADJ_MAX) { int k = 0; while (a[k] && a[k] != '\n') k++; ps[cnt] = a; pl[cnt] = k; cnt++; a += k; if (*a) a++; }
            for (int k = cnt - 1; k >= 0; k--) {
                int ml = pl[k] < 159 ? pl[k] : 159; mcpy(r, ps[k], (uint32_t)ml); r[ml] = 0;
                const char *bn = base_name(r); int cw = tw(bn, 12, WF_BOLD); if (cw > 180) cw = 180;
                int chw = cw + 44; ax -= chw;
                rr(ax, yy, chw, 36, 10, C_CARD2); rs(ax, yy, chw, 36, 10, C_BD, 255);
                int img = 0; { int L = slen(r); if (L > 4 && (eqn(r + L - 4, ".png", 4) || eqn(r + L - 4, ".jpg", 4) || eqn(r + L - 5, ".jpeg", 5) || eqn(r + L - 4, ".gif", 4) || eqn(r + L - 5, ".webp", 5) || eqn(r + L - 4, ".bmp", 4))) img = 1; }
                rr(ax + 6, yy + 6, 24, 24, 6, img ? 0x002A1E4Au : 0x00122A3Au); icono(img ? I_IMG : I_FILE, ax + 18, yy + 18, img ? C_ACC2 : C_CYAN, 14);
                txe(ax + 36, yy, 36, bn, 12, C_TX2, WF_BOLD, cw);
                ax -= 6;
            }
            yy += 44;
        }
        int bh = (l ? l->h : 20) + 24;
        rr(bx, yy, bw, bh, 18, C_UB);
        if (l) { fb_set_clip(bx, yy > y0 ? yy : y0, bw, (yy + bh < y1 ? yy + bh : y1) - (yy > y0 ? yy : y0)); pinta_md(l, bx + 18, yy + 12, (int)i, y0, y1); fb_set_clip(g_mx0, y0, g_mw0, y1 - y0); }
        /* acciones al pasar el raton */
        int ay = yy + bh + 4, show = sobre(g->cx, y, g->cw, bh + 44) || ultimo;
        if (show) {
            int ax = g->cx + g->cw - 28;
            boton_ic(ax, ay, 26, I_COPY, C_FAINT, A_COPIA_MSG, (int32_t)i, 0, 0); ax -= 30;
            if (!ocupado()) boton_ic(ax, ay, 26, I_EDIT, C_FAINT, A_EDITA, (int32_t)i, 0, 0);
            char h[8]; cc_hora(m->t, h); txc(ax - 50, 46, ay, 26, h, 11, C_FAINT, 0);
        }
        return;
    }
    /* SOMA */
    orbe(g->cx + 15, y + 14, 14, (int)(i * 40));
    int hx = g->cx + 46;
    tx(hx, y, 28, "SOMA", 14, C_TX, WF_BOLD);
    int bx = hx + tw("SOMA", 14, WF_BOLD) + 10;
    {   const char *mo = m->modo == CC_MODO_AGENTE ? "Agente" : "LLM"; uint32_t col = m->modo == CC_MODO_AGENTE ? C_CYAN : C_ACC2;
        int w = tw(mo, 11, WF_BOLD) + 16; rra(bx, y + 5, w, 18, 9, col, 0x22); rs(bx, y + 5, w, 18, 9, col, 0x80); tx(bx + 8, y + 5, 18, mo, 11, col, WF_BOLD); bx += w + 6;
        if (m->mem) { int w2 = tw("Memoria", 11, WF_BOLD) + 16; rra(bx, y + 5, w2, 18, 9, C_GREEN, 0x22); rs(bx, y + 5, w2, 18, 9, C_GREEN, 0x80); tx(bx + 8, y + 5, 18, "Memoria", 11, C_GREEN, WF_BOLD); bx += w2 + 6; }
        char h[8]; cc_hora(m->t, h); tx(bx + 2, y, 28, h, 11, C_FAINT, 0); }
    int cy = y + 30;
    if (m->estado != CC_OK) {
        int ew = g->cw - 46; uint32_t col = m->estado == CC_CORTADA ? C_AMBER : C_RED;
        rra(hx, cy, ew, 62, 12, col, 0x18); rs(hx, cy, ew, 62, 12, col, 0x90);
        tx(hx + 16, cy + 8, 22, m->estado == CC_CORTADA ? "Respuesta detenida" : "No se pudo responder", 13, col, WF_BOLD);
        char e[200]; int k = m->n < 199 ? (int)m->n : 199; mcpy(e, m->txt, (uint32_t)k); e[k] = 0;
        txe(hx + 16, cy + 30, 22, e, 12, C_TX2, 0, ew - 140);
        if (ultimo && !ocupado()) { int bw = tw("Reintentar", 12, WF_BOLD) + 26; int rx = hx + ew - bw - 12; rr(rx, cy + 16, bw, 30, 15, col); txc(rx, bw, cy + 16, 30, "Reintentar", 12, 0xFFFFFF, WF_BOLD); hit(rx, cy + 16, bw, 30, A_REINTENTA, (int32_t)i, 0); }
        cy += 70;
    } else if (l) {
        int rev = (c->id == g_rev_conv && (int)i == g_rev_idx);
        if (rev) {
            uint64_t dt = ms() - g_rev_t0; int dur = l->h * 2 + 300; if (dur > 1600) dur = 1600;
            int vis = (int)((uint64_t)l->h * dt / (uint64_t)dur); if (vis >= l->h) { g_rev_idx = -1; rev = 0; }
            else {
                int cy1 = cy + vis; int a = y0 > cy ? y0 : cy, b = y1 < cy1 ? y1 : cy1;
                if (b > a) { fb_set_clip(hx, a, g->cw - 46 + 4, b - a); pinta_md(l, hx, cy, (int)i, a, b); }
                fb_set_clip(g_mx0, y0, g_mw0, y1 - y0);
                for (int k = 0; k < 24; k++) { int yy = cy1 - 24 + k; if (yy >= cy && yy >= y0 && yy < y1) fb_fill_rect_alpha(hx, yy, g->cw - 46, 1, ((uint32_t)(k * 10) << 24) | C_BG); }
            }
        }
        if (!rev) pinta_md(l, hx, cy, (int)i, y0, y1);
        cy += l->h;
    }
    /* acciones */
    int ay = cy + 8, ax = hx - 4, show = ultimo || sobre(g->cx, y, g->cw, cy + 40 - y);
    if (show && m->estado == CC_OK) {
        boton_ic(ax, ay, 28, I_COPY, C_FAINT, A_COPIA_MSG, (int32_t)i, 0, 0); ax += 32;
        if (!ocupado()) { boton_ic(ax, ay, 28, I_REGEN, C_FAINT, A_REGEN, (int32_t)i, 0, 0); ax += 32; }
#ifdef CHAT_VOZ
        boton_ic(ax, ay, 28, voz_speaking() ? I_STOP : I_VOZ, voz_speaking() ? C_CYAN : C_FAINT, A_VOZ, (int32_t)i, 0, 0); ax += 32;
#endif
        if (m->dur) { char d[32]; seg_txt(d, sizeof d, m->dur); tx(ax + 6, ay, 28, d, 11, C_FAINT, 0); }
    }
}
static void pinta_pensando(const struct geo *g, int y)
{
    uint64_t t = ms() - g_turno_t0;
    orbe(g->cx + 15, y + 14, 14, (int)(t / 8));
    int hx = g->cx + 46;
    tx(hx, y, 28, "SOMA", 14, C_TX, WF_BOLD);
    int ph = (int)((t / 120) % 12);
    for (int k = 0; k < 3; k++) { int a = ph - k * 2; int up = (a >= 0 && a < 4) ? (a < 2 ? a : 4 - a) : 0; punto(hx + tw("SOMA", 14, WF_BOLD) + 18 + k * 11, y + 15 - up * 2, 3, mezcla(C_FAINT, C_CYAN, 80 + up * 80), 255); }
    char s[48]; seg_txt(s, sizeof s, (uint32_t)t);
    const char *que = g_turno == T_AGENTE ? "El agente est\303\241 trabajando" : "Pensando";
    int ly = y + 34;
    int sh = 44 + (g_turno == T_AGENTE ? g_nfase * 24 : 0);
    rra(hx, ly, g->cw - 46, sh, 12, 0x00FFFFFFu, 0x08); rs(hx, ly, g->cw - 46, sh, 12, C_BD, 255);
    /* barra de luz que recorre */
    { int bw = g->cw - 46 - 24, pos = (int)((t / 6) % (uint64_t)(bw + 120)) - 60; for (int k = 0; k < 60; k++) { int xx = hx + 12 + pos + k; if (xx >= hx + 12 && xx < hx + 12 + bw) fb_fill_rect_alpha(xx, ly + sh - 3, 1, 2, ((uint32_t)(k < 30 ? k * 6 : (60 - k) * 6) << 24) | C_CYAN); } }
    tx(hx + 16, ly + 8, 28, que, 13, C_TX2, WF_BOLD);
    tx(hx + 16 + tw(que, 13, WF_BOLD) + 10, ly + 8, 28, s, 12, C_FAINT, 0);
    if (g_turno == T_AGENTE) {
        for (int k = 0; k < g_nfase; k++) {
            int yy = ly + 40 + k * 24, last = k + 1 == g_nfase;
            if (last) { int a = (int)((t / 40) % 1024); for (int q = 0; q < 6; q++) punto(hx + 24 + cs(a + q * 170) * 5 / 256, yy + 10 + sn(a + q * 170) * 5 / 256, 1, C_CYAN, (uint32_t)(80 + q * 30)); }
            else icono(I_CHECK, hx + 24, yy + 10, C_GREEN, 12);
            txe(hx + 40, yy, 20, g_fase_txt[k], 12, last ? C_TX2 : C_MU, g_fase_txt[k][0] == '$' ? WF_MONO : 0, g->cw - 46 - 56);
        }
    }
}
static const char *const SUG_T[4] = { "Expl\303\255came la relatividad", "Escribe una funci\303\263n en Python", "\302\277Qu\303\251 recuerdas de m\303\255?", "Noticias de tecnolog\303\255a" };
static const char *const SUG_S[4] = { "con las f\303\263rmulas importantes", "que ordene una lista sin sort()", "lo que sabe la memoria de SOMA", "busca en la web lo m\303\241s reciente" };
static const char *const SUG_P[4] = {
    "Expl\303\255came la relatividad especial con las f\303\263rmulas importantes (dilataci\303\263n del tiempo, contracci\303\263n de la longitud y E = mc\302\262) y una tabla resumen.",
    "Escribe una funci\303\263n en Python que ordene una lista con mergesort, sin usar sort(), con comentarios y un ejemplo de uso.",
    "\302\277Qu\303\251 recuerdas de m\303\255? Res\303\272melo por temas.",
    "Busca en la web las noticias de tecnolog\303\255a m\303\241s recientes y res\303\272melas en una lista con enlaces." };
static void pinta_vacio(const struct geo *g, int y, int h)
{
    int cy = y + h * 30 / 100; if (cy < y + 40) cy = y + 40;
    uint64_t t = ms();
    for (int k = 3; k > 0; k--) punto(g->cx + g->cw / 2, cy, 34 + k * 7, C_ACC, (uint32_t)(10 + 4 * (3 - k)));
    orbe(g->cx + g->cw / 2, cy, 32, (int)(t / 12));
    txc(g->cx, g->cw, cy + 48, 40, "\302\277En qu\303\251 puedo ayudarte?", 26, C_TX, WF_BOLD);
    const char *sub = g_modo == CC_MODO_AGENTE ? "Modo Agente: SOMA puede usar sus herramientas (\303\263rdenes, web, archivos, documentos)."
                                               : "Modo Solo LLM: conversaci\303\263n directa con el modelo de lenguaje.";
    txc(g->cx, g->cw, cy + 88, 24, sub, 13, C_MU, 0);
    int gw = g->cw > 640 ? 640 : g->cw, gx = g->cx + (g->cw - gw) / 2, cw = (gw - 12) / 2, gy = cy + 130;
    for (int k = 0; k < 4; k++) {
        int x = gx + (k & 1) * (cw + 12), yy = gy + (k >> 1) * 76, ov = sobre(x, yy, cw, 64);
        rr(x, yy, cw, 64, 14, ov ? C_CARD2 : C_CARD); rs(x, yy, cw, 64, 14, ov ? C_BDL : C_BD, 255);
        static const int IC[4] = { I_CHAT, I_FILE, I_MEM, I_AGENT }; static const uint32_t CO[4] = { C_ACC2, C_CYAN, C_GREEN, C_AMBER };
        rra(x + 12, yy + 16, 32, 32, 9, CO[k], 0x22); icono(IC[k], x + 28, yy + 32, CO[k], 16);
        txe(x + 54, yy + 12, 22, SUG_T[k], 13, C_TX, WF_BOLD, cw - 66);
        txe(x + 54, yy + 32, 20, SUG_S[k], 12, C_MU, 0, cw - 66);
        hit(x, yy, cw, 64, A_SUGIERE, k, 0);
    }
}

/* ---------------------------------------------------------------- pintado: cuadro de texto */
static int alto_comp(int w)
{
    int lines = tf_lines(&f_comp, w - 40); if (lines < 1) lines = 1; if (lines > 9) lines = 9;
    int h = lines * tf_line_h() + 14 + 48;
    if (g_nadj) h += 52;
    if (g_edit_idx >= 0) h += 30;
    return h;
}
static void pinta_comp(int x, int y, int w, int h)
{
    int foc = g_foco == &f_comp;
    fb_draw_box_shadow(x, y, w, h, 22, 0, 6, 0, 22, 0x60000000u);
    rr(x, y, w, h, 22, C_CARD2); rs(x, y, w, h, 22, foc ? C_BDL : C_BD, 255);
    int yy = y + 8;
    if (g_edit_idx >= 0) {
        rra(x + 12, yy, w - 24, 24, 8, C_AMBER, 0x1C); icono(I_EDIT, x + 26, yy + 12, C_AMBER, 12);
        tx(x + 38, yy, 24, "Editando un mensaje: al enviar, la conversaci\303\263n sigue desde aqu\303\255", 12, C_AMBER, 0);
        boton_ic(x + w - 40, yy, 24, I_X, C_AMBER, A_EDIT_NO, 0, 0, 0); yy += 30;
    }
    if (g_nadj) {
        int ax = x + 12;
        for (int i = 0; i < g_nadj; i++) {
            const char *bn = base_name(g_adj[i]); int cw = tw(bn, 12, WF_BOLD); if (cw > 150) cw = 150;
            int chw = cw + 64; if (ax + chw > x + w - 12) break;
            rr(ax, yy, chw, 42, 10, C_CARD); rs(ax, yy, chw, 42, 10, C_BD, 255);
            int L = slen(g_adj[i]); int img = L > 4 && (eqn(g_adj[i] + L - 4, ".png", 4) || eqn(g_adj[i] + L - 4, ".jpg", 4) || eqn(g_adj[i] + L - 5, ".jpeg", 5) || eqn(g_adj[i] + L - 4, ".gif", 4) || eqn(g_adj[i] + L - 5, ".webp", 5));
            rr(ax + 7, yy + 7, 28, 28, 7, img ? 0x002A1E4Au : 0x00122A3Au); icono(img ? I_IMG : I_FILE, ax + 21, yy + 21, img ? C_ACC2 : C_CYAN, 14);
            txe(ax + 42, yy + 3, 20, bn, 12, C_TX2, WF_BOLD, cw);
            tx(ax + 42, yy + 20, 18, img ? "Imagen" : "Archivo", 11, C_FAINT, 0);
            boton_ic(ax + chw - 22, yy + 2, 18, I_X, C_MU, A_ADJ_QUITA, i, 0, 0);
            ax += chw + 8;
        }
        yy += 52;
    }
    int th = h - (yy - y) - 48;
    const char *ph = g_modo == CC_MODO_AGENTE ? "P\303\255dele algo al agente SOMA\342\200\246" : "Escribe un mensaje\342\200\246";
    tf_render(&f_comp, x + 12, yy, w - 24, th, foc, ph, C_TX);
    { int i = nH; hit(x + 12, yy, w - 24, th, A_FOCO, 0, 0); if (i < nH) H[i].f = &f_comp; }
    /* barra inferior */
    int by = y + h - 44;
    boton_ic(x + 10, by + 4, 32, I_PLUS, C_TX2, A_ADJ, 0, 0, 0);
    /* modo: segmentado */
    int sx = x + 50, sw1 = tw("Solo LLM", 12, WF_BOLD) + 40, sw2 = tw("Agente", 12, WF_BOLD) + 40;
    rr(sx, by + 6, sw1 + sw2 + 6, 30, 15, C_INP); rs(sx, by + 6, sw1 + sw2 + 6, 30, 15, C_BD, 255);
    for (int k = 0; k < 2; k++) {
        int bx = k ? sx + 3 + sw1 : sx + 3, bw = k ? sw2 : sw1, on = (k ? CC_MODO_AGENTE : CC_MODO_LLM) == g_modo;
        uint32_t col = k ? C_CYAN : C_ACC2;
        if (on) { rra(bx, by + 9, bw, 24, 12, col, 0x30); rs(bx, by + 9, bw, 24, 12, col, 0xA0); }
        else if (sobre(bx, by + 9, bw, 24)) rra(bx, by + 9, bw, 24, 12, 0xFFFFFF, 0x10);
        icono(k ? I_AGENT : I_CHAT, bx + 15, by + 21, on ? col : C_MU, 13);
        tx(bx + 27, by + 9, 24, k ? "Agente" : "Solo LLM", 12, on ? C_TX : C_MU, WF_BOLD);
        hit(bx, by + 9, bw, 24, A_MODO, k ? CC_MODO_AGENTE : CC_MODO_LLM, 0);
    }
    /* memoria: interruptor */
    int mx = sx + sw1 + sw2 + 16, mw = tw("Memoria", 12, WF_BOLD) + 74;
    { int ov = sobre(mx, by + 6, mw, 30); rra(mx, by + 6, mw, 30, 15, g_mem ? C_GREEN : 0xFFFFFF, g_mem ? 0x1E : (ov ? 0x12 : 0x08)); rs(mx, by + 6, mw, 30, 15, g_mem ? C_GREEN : C_BD, g_mem ? 0x90 : 255);
      icono(I_MEM, mx + 16, by + 21, g_mem ? C_GREEN : C_MU, 14);
      tx(mx + 30, by + 6, 30, "Memoria", 12, g_mem ? C_TX : C_MU, WF_BOLD);
      int kx = mx + mw - 34; rr(kx, by + 14, 24, 14, 7, g_mem ? C_GREEN : C_FAINT); punto(g_mem ? kx + 17 : kx + 7, by + 21, 5, 0xFFFFFF, 255);
      hit(mx, by + 6, mw, 30, A_MEMT, 0, 0); }
    /* enviar / parar */
    int ex = x + w - 46;
    if (ocupado()) { int ov = sobre(ex, by + 4, 36, 36); punto(ex + 18, by + 22, 18, ov ? 0xFFFFFF : C_TX, 255); icono(I_STOP, ex + 18, by + 22, C_BG, 16); hit(ex, by + 4, 36, 36, A_PARAR, 0, 0); }
    else {
        int vacio = !tf_len(&f_comp) && !g_nadj, ov = sobre(ex, by + 4, 36, 36);
        if (vacio) punto(ex + 18, by + 22, 18, C_BD, 255);
        else { punto(ex + 18, by + 22, 18, ov ? mezcla(C_ACC, 0xFFFFFF, 50) : C_ACC, 255); }
        icono(I_SEND, ex + 18, by + 22, vacio ? C_FAINT : 0xFFFFFF, 18);
        hit(ex, by + 4, 36, 36, A_ENVIAR, 0, 0);
    }
}

/* ---------------------------------------------------------------- pintado: ventanas encima */
static void pinta_modal(int x, int y, int w, int h)
{
    fb_fill_rect_alpha(x, y, w, h, 0x99000000u);
    hit(x, y, w, h, A_CANCELA, 0, 0);
    if (g_modal == M_REN || g_modal == M_BORRA || g_modal == M_VACIAR) {
        int mw = 440, mh = g_modal == M_REN ? 178 : 170, mx = x + (w - mw) / 2, my = y + (h - mh) / 2 - 40;
        fb_draw_box_shadow(mx, my, mw, mh, 16, 0, 10, 0, 30, 0x80000000u);
        rr(mx, my, mw, mh, 16, C_CARD2); rs(mx, my, mw, mh, 16, C_BDL, 255);
        hit(mx, my, mw, mh, A_NADA, 0, 0);
        struct cc_conv *c = cc_por_id(&g_h, g_modal_id);
        if (g_modal == M_REN) {
            tx(mx + 24, my + 18, 28, "Renombrar conversaci\303\263n", 16, C_TX, WF_BOLD);
            rr(mx + 24, my + 62, mw - 48, 40, 10, C_INP); rs(mx + 24, my + 62, mw - 48, 40, 10, C_ACC, 255);
            tf_render(&f_in, mx + 30, my + 66, mw - 60, 32, g_foco == &f_in, "T\303\255tulo", C_TX);
            { int i = nH; hit(mx + 24, my + 62, mw - 48, 40, A_FOCO, 0, 0); if (i < nH) H[i].f = &f_in; }
        } else {
            tx(mx + 24, my + 18, 28, g_modal == M_BORRA ? "\302\277Borrar la conversaci\303\263n?" : "\302\277Borrar todas las conversaciones?", 16, C_TX, WF_BOLD);
            txe(mx + 24, my + 52, 22, g_modal == M_BORRA ? (c && c->titulo[0] ? c->titulo : "Conversaci\303\263n nueva") : "No se puede deshacer.", 13, C_TX2, 0, mw - 48);
            tx(mx + 24, my + 76, 22, "Lo que la memoria ya aprendi\303\263 de ella se queda en la memoria.", 12, C_MU, 0);
        }
        int by = my + mh - 56;
        int w2 = tw(g_modal == M_REN ? "Guardar" : "Borrar", 13, WF_BOLD) + 28;
        boton(mx + mw - 24 - w2, by, 36, g_modal == M_REN ? "Guardar" : "Borrar", 1, g_modal == M_REN ? C_ACC : C_RED, A_OK, 0, 0);
        int w1 = tw("Cancelar", 13, WF_BOLD) + 28;
        boton(mx + mw - 24 - w2 - 10 - w1, by, 36, "Cancelar", 0, 0, A_CANCELA, 0, 0);
        return;
    }
    if (g_modal == M_ARCH) {
        int mw = w - 80 > 620 ? 620 : w - 80, mh = h - 80 > 520 ? 520 : h - 80, mx = x + (w - mw) / 2, my = y + (h - mh) / 2;
        fb_draw_box_shadow(mx, my, mw, mh, 16, 0, 10, 0, 30, 0x80000000u);
        rr(mx, my, mw, mh, 16, C_CARD2); rs(mx, my, mw, mh, 16, C_BDL, 255);
        hit(mx, my, mw, mh, A_NADA, 0, 0);
        tx(mx + 24, my + 16, 28, "Adjuntar archivos", 16, C_TX, WF_BOLD);
        boton_ic(mx + mw - 44, my + 14, 30, I_X, C_MU, A_CANCELA, 0, 0, 0);
        /* ruta + subir */
        int ry = my + 56;
        rr(mx + 24, ry, mw - 48, 34, 9, C_INP); rs(mx + 24, ry, mw - 48, 34, 9, C_BD, 255);
        boton_ic(mx + 28, ry + 3, 28, I_UP, C_TX2, A_AR_SUBE, 0, 0, 0);
        icono(I_FOLDER, mx + 70, ry + 17, C_AMBER, 14);
        txe(mx + 84, ry, 34, g_dir, 13, C_TX, WF_BOLD, mw - 130);
        /* atajos */
        int qx = mx + 24, qy = ry + 44;
        static const char *const Q[4] = { "/usuario", "/", "/chat", "/tmp" }; static const char *const QN[4] = { "Mi unidad", "Sistema", "Chat", "Temporal" };
        for (int k = 0; k < 4; k++) { int bw = tw(QN[k], 12, WF_BOLD) + 24, on = eqn(g_dir, Q[k], 160); if (on) rra(qx, qy, bw, 26, 13, C_ACC, 0x30); else rra(qx, qy, bw, 26, 13, 0xFFFFFF, sobre(qx, qy, bw, 26) ? 0x14 : 0x08); txc(qx, bw, qy, 26, QN[k], 12, on ? C_TX : C_TX2, WF_BOLD); hit(qx, qy, bw, 26, A_AR_DIR, k, 0); qx += bw + 8; }
        int ly = qy + 38, lh = my + mh - 66 - ly;
        rr(mx + 24, ly, mw - 48, lh, 10, C_CARD); rs(mx + 24, ly, mw - 48, lh, 10, C_BD, 255);
        fb_set_clip(mx + 25, ly + 1, mw - 50, lh - 2);
        int rows = g_nlista, rh = 34, maxs = rows * rh - lh + 8; if (maxs < 0) maxs = 0;
        if (g_ascroll > maxs) g_ascroll = maxs;
        if (g_ascroll < 0) g_ascroll = 0;
        const char *p = g_lista; int yy = ly + 4 - g_ascroll;
        if (!rows) txc(mx + 24, mw - 48, ly + lh / 2 - 12, 24, "Carpeta vac\303\255a", 13, C_FAINT, 0);
        for (int k = 0; k < rows && p; k++) {
            int L = slen(p), dir = L > 0 && p[L - 1] == '/';
            if (yy + rh > ly && yy < ly + lh) {
                int ov = sobre(mx + 28, yy, mw - 56, rh - 2) && g_ry > ly && g_ry < ly + lh;
                char full[200]; scpy(full, g_dir, sizeof full); if (slen(full) > 1) scat(full, "/", sizeof full); scat(full, p, sizeof full);
                int L2 = slen(full); if (L2 && full[L2 - 1] == '/') full[L2 - 1] = 0;
                int ya = 0; for (int q = 0; q < g_nadj; q++) if (eqn(g_adj[q], full, 160)) ya = 1;
                if (ov || ya) rr(mx + 28, yy, mw - 56, rh - 2, 8, ya ? C_SEL : C_SIDEH);
                icono(dir ? I_FOLDER : I_FILE, mx + 46, yy + rh / 2 - 1, dir ? C_AMBER : C_CYAN, 14);
                char nm[160]; scpy(nm, p, sizeof nm); if (dir) nm[slen(nm) - 1] = 0;
                txe(mx + 62, yy, rh - 2, nm, 13, C_TX2, dir ? WF_BOLD : 0, mw - 140);
                if (ya) icono(I_CHECK, mx + mw - 48, yy + rh / 2 - 1, C_GREEN, 14);
                int hy = yy < ly ? ly : yy, hh = (yy + rh - 2 > ly + lh ? ly + lh : yy + rh - 2) - hy;
                hit(mx + 28, hy, mw - 56, hh, A_AR_ITEM, k, dir);
            }
            yy += rh; p += L + 1;
        }
        fb_reset_clip(); fb_set_clip(g_wx, g_wy, g_ww, g_wh);
        char s[64] = ""; snum(s, sizeof s, (uint32_t)g_nadj); scat(s, g_nadj == 1 ? " archivo elegido" : " archivos elegidos", sizeof s);
        tx(mx + 24, my + mh - 50, 36, s, 12, C_MU, 0);
        int w2 = tw("Listo", 13, WF_BOLD) + 28; boton(mx + mw - 24 - w2, my + mh - 52, 36, "Listo", 1, C_ACC, A_CANCELA, 0, 0);
    }
}
static void pinta_menu(void)
{
    if (!g_menu_conv) return;
    struct cc_conv *c = cc_por_id(&g_h, g_menu_id); if (!c) { g_menu_conv = 0; return; }
    int mw = 200, mh = 4 * 36 + 12, x = g_menu_x, y = g_menu_y;
    if (y + mh > g_wy + g_wh - 8) y = g_wy + g_wh - 8 - mh;
    hit(g_wx, g_wy, g_ww, g_wh, A_CERRAR_MENU, 0, 0);
    fb_draw_box_shadow(x, y, mw, mh, 12, 0, 8, 0, 24, 0x80000000u);
    rr(x, y, mw, mh, 12, C_CARD2); rs(x, y, mw, mh, 12, C_BDL, 255);
    static const char *const T[4] = { "Renombrar", "Fijar", "Exportar a Markdown", "Borrar" };
    static const int IC[4] = { I_EDIT, I_PIN, I_EXPORT, I_TRASH }, AC[4] = { A_M_REN, A_M_FIJA, A_M_EXPORTA, A_M_BORRA };
    for (int k = 0; k < 4; k++) {
        int yy = y + 6 + k * 36, ov = sobre(x + 6, yy, mw - 12, 34);
        if (ov) rr(x + 6, yy, mw - 12, 34, 8, k == 3 ? 0x003A1620u : C_SIDEH);
        uint32_t col = k == 3 ? C_RED : C_TX2;
        icono(IC[k], x + 24, yy + 17, col, 14);
        tx(x + 42, yy, 34, k == 1 && c->fija ? "Desfijar" : T[k], 13, col, 0);
        hit(x + 6, yy, mw - 12, 34, AC[k], (int32_t)c->id, 0);
    }
}

/* ---------------------------------------------------------------- pintado principal */
void chat_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    if (!g_ini) chat_init();
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    g_wx = x; g_wy = y; g_ww = w; g_wh = h;
    nH = 0;
    fb_set_clip(x, y, w, h);
    rr(x, y, w, h, 0, C_BG);
    int side = g_side && w >= 640;
    if (side) pinta_lateral(x, y, SIDE_W, h);
    int mx = x + (side ? SIDE_W : 0), mw = w - (side ? SIDE_W : 0);
    g_mx0 = mx; g_mw0 = mw;
    struct cc_conv *c = conv_actual();
    /* barra superior */
    {
        int bx = mx + 14;
        if (!side) { boton_ic(bx, y + 14, 30, I_SIDE, C_MU, A_SIDE, 0, 0, 0); bx += 36; boton_ic(bx, y + 14, 30, I_NEW, C_MU, A_NUEVA, 0, 0, 0); bx += 40; }
        const char *t = c && c->titulo[0] ? c->titulo : "Conversaci\303\263n nueva";
        int tmax = mw - (bx - mx) - 220;
        int ov = c && sobre(bx, y + 12, tw(t, 15, WF_BOLD) + 16, 32);
        if (ov) rra(bx - 6, y + 12, (tw(t, 15, WF_BOLD) + 16 < tmax ? tw(t, 15, WF_BOLD) + 16 : tmax), 32, 8, 0xFFFFFF, 0x10);
        txe(bx + 2, y + 12, 32, t, 15, C_TX, WF_BOLD, tmax);
        if (c) hit(bx - 6, y + 12, tmax, 32, A_TITULO, (int32_t)c->id, 0);
        /* derecha: modelo y exportar */
        int rx = mx + mw - 14;
        if (c) { rx -= 32; boton_ic(rx, y + 13, 30, I_EXPORT, C_MU, A_EXPORTA, (int32_t)c->id, 0, 0); rx -= 6; }
        const struct llm_conf *lc = llm_conf_get();
        if (lc && mw > 560) {
            char mod[64]; scpy(mod, lc->model[lc->prov], sizeof mod);
            int bw = tw(mod, 12, WF_BOLD) + 34; if (bw > 230) bw = 230;
            rx -= bw; rra(rx, y + 15, bw, 26, 13, 0xFFFFFF, 0x08); rs(rx, y + 15, bw, 26, 13, C_BD, 255);
            punto(rx + 14, y + 28, 4, llm_vision_unsupported() ? C_AMBER : C_GREEN, 255);
            txe(rx + 24, y + 15, 26, mod, 12, C_TX2, WF_BOLD, bw - 32);
        }
        ln(mx, y + TOP_H - 1, mx + mw, y + TOP_H - 1, C_BD, 16);
    }
    /* columna */
    struct geo g; g.x = mx; g.w = mw; g.cw = mw - 48 > COL_MAX ? COL_MAX : mw - 48; if (g.cw < 200) g.cw = 200; g.cx = mx + (mw - g.cw) / 2;
    int cwid = g.cw + 16 > mw - 24 ? mw - 24 : g.cw + 16;
    int ch = alto_comp(cwid);
    int comp_y = y + h - ch - 30;
    int vy0 = y + TOP_H, vy1 = comp_y - 8;
    g_view_y0 = vy0; g_view_y1 = vy1;
    int vacio = !c || !c->nm;
    if (vacio) { g_scroll = 0; g_max_scroll = 0; fb_set_clip(mx, vy0, mw, vy1 - vy0); pinta_vacio(&g, vy0, vy1 - vy0); }
    else {
        /* alturas */
        int tot = 24;
        for (uint32_t i = 0; i < c->nm; i++) { struct lay *l; tot += alto_msg(c, i, &g, &l); }
        if (ocupado() && g_turno_conv == c->id) tot += 80 + (g_turno == T_AGENTE ? g_nfase * 24 : 0) + 20;
        tot += 16;
        int vis = vy1 - vy0;
        g_max_scroll = tot - vis; if (g_max_scroll < 0) g_max_scroll = 0;
        if (g_pegado) g_scroll = g_max_scroll;
        if (g_scroll > g_max_scroll) g_scroll = g_max_scroll;
        if (g_scroll < 0) g_scroll = 0;
        fb_set_clip(mx, vy0, mw, vis);
        int yy = vy0 + 24 - g_scroll;
        for (uint32_t i = 0; i < c->nm; i++) {
            struct lay *l; int hh = alto_msg(c, i, &g, &l);
            if (yy + hh > vy0 && yy < vy1) pinta_msg(c, i, &g, yy, l, vy0, vy1);
            yy += hh;
        }
        if (ocupado() && g_turno_conv == c->id) pinta_pensando(&g, yy);
        /* barra de desplazamiento */
        if (g_max_scroll > 0) {
            barra_pon(&g_b_main, vy0 + 4, vis - 8, g_max_scroll, &g_scroll);
            int ov = sobre(mx + mw - 30, vy0, 22, vis) || g_arrastra == &g_b_main;
            rra(mx + mw - (ov ? 21 : 19), g_b_main.ty, ov ? 8 : 5, g_b_main.th, 4, C_BDL, ov ? 255 : 170);
            hit(mx + mw - 30, vy0, 22, vis, A_SCROLLBAR, 0, 0);
        }
        /* degradado bajo la barra superior */
        for (int k = 0; k < 14; k++) fb_fill_rect_alpha(mx, vy0 + k, mw - 14, 1, ((uint32_t)(140 - k * 10) << 24) | C_BG);
        if (g_scroll < g_max_scroll - 40) {
            int bx = mx + mw / 2 - 18, by = vy1 - 46;
            punto(bx + 18, by + 18, 18, C_CARD2, 255); rs(bx, by, 36, 36, 18, C_BDL, 255); icono(I_DOWN, bx + 18, by + 18, C_TX2, 16);
            hit(bx, by, 36, 36, A_ABAJO, 0, 0);
        }
    }
    fb_set_clip(x, y, w, h);
    /* cuadro de texto */
    int cxx = mx + (mw - cwid) / 2;
    for (int k = 0; k < 24; k++) fb_fill_rect_alpha(mx, comp_y - 24 + k, mw, 1, ((uint32_t)(k * 9) << 24) | C_BG);
    rr(mx, comp_y, mw, y + h - comp_y, 0, C_BG);
    pinta_comp(cxx, comp_y, cwid, ch);
    txc(mx, mw, comp_y + ch + 4, 22, "Intro env\303\255a \302\267 May\303\272s+Intro: nueva l\303\255nea \302\267 SOMA puede equivocarse: comprueba lo importante.", 11, C_FAINT, 0);
    /* aviso */
    if (g_toast[0] && ms() - g_toast_t < 3500) {
        int tw_ = tw(g_toast, 13, WF_BOLD) + 48, tx_ = mx + (mw - tw_) / 2, ty = comp_y - 50;
        uint32_t col = g_toast_err ? C_RED : C_GREEN;
        fb_draw_box_shadow(tx_, ty, tw_, 38, 19, 0, 6, 0, 18, 0x70000000u);
        rr(tx_, ty, tw_, 38, 19, C_CARD2); rs(tx_, ty, tw_, 38, 19, col, 0xC0);
        punto(tx_ + 20, ty + 19, 4, col, 255); tx(tx_ + 32, ty, 38, g_toast, 13, C_TX, WF_BOLD);
    } else g_toast[0] = 0;
    pinta_menu();
    if (g_modal) pinta_modal(x, y, w, h);
    fb_reset_clip();
}

/* ---------------------------------------------------------------- acciones */
static void abre_conv(uint32_t id)
{
    struct cc_conv *c = cc_por_id(&g_h, id); if (!c) return;
    g_cur = id; g_modo = c->modo; g_mem = c->mem; g_pegado = 1; g_scroll = 0; g_edit_idx = -1; g_rev_idx = -1;
    g_foco = &f_comp;
}
static void nueva(void) { g_cur = 0; g_pegado = 1; g_scroll = 0; g_edit_idx = -1; g_nadj = 0; tf_set(&f_comp, ""); g_foco = &f_comp; }
static void copia_texto(const char *s, uint32_t n) { if (n > 0) { clip_set_text(s, n); toast("Copiado al portapapeles", 0); } }
static void exporta(uint32_t id)
{
    struct cc_conv *c = cc_por_id(&g_h, id); if (!c) return;
    uint32_t cap = 512; for (uint32_t i = 0; i < c->nm; i++) cap += c->m[i].n + 300;
    char *b = (char *)kmalloc(cap); if (!b) { toast("Sin memoria para exportar", 1); return; }
    b[0] = 0; scat(b, "# ", (int)cap); scat(b, c->titulo[0] ? c->titulo : "Conversaci\303\263n", (int)cap); scat(b, "\n\n", (int)cap);
    for (uint32_t i = 0; i < c->nm; i++) {
        const struct cc_msg *m = &c->m[i]; char f[32]; cc_fecha(m->t, ahora(), f, sizeof f);
        scat(b, m->rol == CC_USER ? "### T\303\272 \302\267 " : "### SOMA \302\267 ", (int)cap); scat(b, f, (int)cap); scat(b, "\n\n", (int)cap);
        uint32_t k = (uint32_t)slen(b); mcpy(b + k, m->txt, m->n); b[k + m->n] = 0;
        scat(b, "\n\n", (int)cap);
    }
    scpy(g_path, "/chat/conversacion-", sizeof g_path); snum(g_path, sizeof g_path, c->id); scat(g_path, ".md", sizeof g_path);
    int r = vfs_write(g_path, b, (uint32_t)slen(b));
    kfree(b);
    if (r < 0) { toast("No se pudo exportar", 1); g_path[0] = 0; }
}
#ifdef CHAT_VOZ
static uint32_t texto_plano(const char *s, uint32_t n, char *d, uint32_t cap)   /* para leer en voz alta: sin marcas ni codigo */
{
    uint32_t k = 0, i = 0; int code = 0;
    while (i < n && k + 16 < cap) {
        if ((i == 0 || s[i - 1] == '\n') && i + 2 < n && s[i] == '`' && s[i + 1] == '`' && s[i + 2] == '`') {
            code = !code; while (i < n && s[i] != '\n') i++;
            if (code) { const char *t = " (c\303\263digo) "; while (*t) d[k++] = *t++; }
            continue;
        }
        if (code) { i++; continue; }
        char c = s[i];
        if (c == '[') { uint32_t j = i + 1; while (j < n && s[j] != ']' && s[j] != '\n') j++; if (j + 1 < n && s[j] == ']' && s[j + 1] == '(') { for (uint32_t q = i + 1; q < j && k + 16 < cap; q++) d[k++] = s[q]; i = j + 2; while (i < n && s[i] != ')') i++; i++; continue; } }
        if (c == '*' || c == '#' || c == '`' || c == '_' || c == '$' || c == '|' || c == '>' || c == '\\' || c == '~') { i++; continue; }
        d[k++] = c; i++;
    }
    d[k] = 0; return k;
}
#endif
static int accion(const struct hit *z)
{
    struct cc_conv *c = conv_actual();
    switch (z->act) {
    case A_FOCO: g_foco = z->f; if (z->f) tf_press(z->f, g_rx, g_ry, keyboard_shift_held()); return 1;
    case A_NUEVA: nueva(); return 1;
    case A_SIDE: g_side = !g_side; guarda_ajustes(); return 1;
    case A_CONV: abre_conv((uint32_t)z->a); return 1;
    case A_CONV_MENU: g_menu_conv = 1; g_menu_id = (uint32_t)z->a; g_menu_x = z->x - 160; g_menu_y = z->b; if (g_menu_x < g_wx + 8) g_menu_x = g_wx + 8; return 1;
    case A_CERRAR_MENU: g_menu_conv = 0; return 1;
    case A_M_REN: case A_TITULO: { g_menu_conv = 0; struct cc_conv *k = cc_por_id(&g_h, (uint32_t)z->a); if (!k) return 1; g_modal = M_REN; g_modal_id = k->id; tf_set(&f_in, k->titulo); tf_select_all(&f_in); g_foco = &f_in; return 1; }
    case A_M_BORRA: g_menu_conv = 0; g_modal = M_BORRA; g_modal_id = (uint32_t)z->a; return 1;
    case A_M_FIJA: { g_menu_conv = 0; struct cc_conv *k = cc_por_id(&g_h, (uint32_t)z->a); if (k) { k->fija = !k->fija; ensucia(); toast(k->fija ? "Conversaci\303\263n fijada" : "Conversaci\303\263n desfijada", 0); } return 1; }
    case A_M_EXPORTA: case A_EXPORTA: g_menu_conv = 0; exporta((uint32_t)z->a); return g_path[0] ? 4 : 1;
    case A_OK:
        if (g_modal == M_REN) { struct cc_conv *k = cc_por_id(&g_h, g_modal_id); if (k) { if (b_in[0]) cc_titulo_pon(k, b_in); else { k->titulo_manual = 0; cc_titulo_auto(k); } ensucia(); } }
        else if (g_modal == M_BORRA) {
            if (ocupado() && g_turno_conv == g_modal_id) { toast("Espera a que termine la respuesta", 1); g_modal = M_NO; return 1; }
            if (g_lay_conv == g_modal_id) { lay_reset(); g_lay_conv = 0; }
            cc_borra(&g_h, g_modal_id); if (g_cur == g_modal_id) nueva(); ensucia(); toast("Conversaci\303\263n borrada", 0);
        }
        g_modal = M_NO; g_foco = &f_comp; return 1;
    case A_CANCELA: g_modal = M_NO; g_foco = &f_comp; return 1;
    case A_MODO:
        if (g_modo != z->a) { g_modo = z->a; if (c && !ocupado()) { c->modo = (uint8_t)g_modo; ensucia(); } guarda_ajustes(); }
        return 1;
    case A_MEMT: g_mem = !g_mem; if (c && !ocupado()) { c->mem = (uint8_t)g_mem; ensucia(); } guarda_ajustes(); toast(g_mem ? "Memoria activada: SOMA recuerda y aprende de esta conversaci\303\263n" : "Memoria apagada: esta conversaci\303\263n no consulta ni deja recuerdos", 0); return 1;
    case A_ENVIAR: envia(b_comp ? b_comp : ""); g_foco = &f_comp; return 1;
    case A_PARAR: parar(); return 1;
    case A_ADJ: abre_archivos(); return 1;
    case A_ADJ_QUITA: if (z->a >= 0 && z->a < g_nadj) { for (int i = z->a; i + 1 < g_nadj; i++) scpy(g_adj[i], g_adj[i + 1], 160); g_nadj--; } return 1;
    case A_COPIA_MSG: if (c && (uint32_t)z->a < c->nm) copia_texto(c->m[z->a].txt, c->m[z->a].n); return 1;
    case A_REGEN: case A_REINTENTA: regenera((uint32_t)z->a); return 1;
    case A_VOZ:
#ifdef CHAT_VOZ
        if (voz_speaking()) { voz_speak_stop(); return 1; }
        if (c && (uint32_t)z->a < c->nm) {
            uint32_t cap = c->m[z->a].n + 64; char *t = (char *)kmalloc(cap);
            if (t) { uint32_t k = texto_plano(c->m[z->a].txt, c->m[z->a].n, t, cap); if (voz_speak_start(t, k) != 0) toast("No se puede leer en voz alta (\302\277hay voz instalada?)", 1); kfree(t); }
        }
#endif
        return 1;
    case A_EDITA: if (c && (uint32_t)z->a < c->nm && !ocupado()) { char *t = c->m[z->a].txt; uint32_t n = c->m[z->a].n; tf_set(&f_comp, ""); tf_insert(&f_comp, t, n); g_edit_idx = z->a; g_foco = &f_comp; } return 1;
    case A_EDIT_NO: g_edit_idx = -1; tf_set(&f_comp, ""); return 1;
    case A_SUGIERE: if (z->a == 3) g_modo = CC_MODO_AGENTE; if (z->a == 2) g_mem = 1; envia(SUG_P[z->a]); return 1;
    case A_ABAJO: g_pegado = 1; return 1;
    case A_SCROLLBAR: barra_pulsa(&g_b_main, g_ry); barra_mueve(&g_b_main, g_ry); g_pegado = g_scroll >= g_max_scroll; return 1;
    case A_SSCROLL: barra_pulsa(&g_b_side, g_ry); barra_mueve(&g_b_side, g_ry); return 1;
    case A_AR_SUBE: { int L = slen(g_dir); while (L > 1 && g_dir[L - 1] != '/') L--; if (L > 1) L--; g_dir[L > 0 ? L : 1] = 0; if (!g_dir[0]) scpy(g_dir, "/", sizeof g_dir); lista_carga(); return 1; }
    case A_AR_DIR: { static const char *const Q[4] = { "/usuario", "/", "/chat", "/tmp" }; scpy(g_dir, Q[z->a & 3], sizeof g_dir); lista_carga(); return 1; }
    case A_AR_ITEM: {
        const char *p = g_lista; for (int k = 0; k < z->a && p; k++) p += slen(p) + 1;
        if (!p) return 1;
        char full[200]; scpy(full, g_dir, sizeof full); if (slen(full) > 1) scat(full, "/", sizeof full); scat(full, p, sizeof full);
        int L = slen(full); if (L && full[L - 1] == '/') full[L - 1] = 0;
        if (z->b) { scpy(g_dir, full, sizeof g_dir); lista_carga(); return 1; }
        for (int q = 0; q < g_nadj; q++) if (eqn(g_adj[q], full, 160)) { for (int i = q; i + 1 < g_nadj; i++) scpy(g_adj[i], g_adj[i + 1], 160); g_nadj--; return 1; }
        anade_adjunto(full); return 1; }
    case A_ZONA: {
        if (!c || (uint32_t)z->a >= c->nm || (uint32_t)z->a >= g_nlay) return 1;
        struct lay *l = &g_lay[z->a]; if (z->b < 0 || z->b >= l->nz) return 1;
        const struct md_zone *q = &l->z[z->b];
        if (q->kind == MDZ_COPY) { copia_texto(l->pool + q->s, q->n); return 1; }
        int n = (int)q->n < (int)sizeof g_url - 1 ? (int)q->n : (int)sizeof g_url - 1;
        if (q->kind == MDZ_LINK) {
            mcpy(g_url, l->pool + q->s, (uint32_t)n); g_url[n] = 0;
            if (pre(g_url, "www.")) { char t[1024] = "https://"; scat(t, g_url, sizeof t); scpy(g_url, t, sizeof g_url); }
            if (g_url[0] == '/') { scpy(g_path, g_url, sizeof g_path); return 4; }
            return 3;
        }
        if (q->kind == MDZ_IMG) {
            mcpy(g_url, l->pool + q->s, (uint32_t)n); g_url[n] = 0;
            if (g_url[0] == '/') { scpy(g_path, g_url, sizeof g_path); return 5; }
            return 3;
        }
        return 1; }
    }
    return 1;
}
static int alto_msg(const struct cc_conv *c, uint32_t i, const struct geo *g, struct lay **lo);
static int msg_en(int my)                                 /* mensaje bajo el raton (-1 = ninguno) */
{
    struct cc_conv *c = conv_actual();
    if (!c || !c->nm || my < g_view_y0 || my >= g_view_y1) return -1;
    struct geo g; g.x = g_mx0; g.w = g_mw0; g.cw = g_mw0 - 48 > COL_MAX ? COL_MAX : g_mw0 - 48; if (g.cw < 200) g.cw = 200; g.cx = g_mx0 + (g_mw0 - g.cw) / 2;
    int yy = g_view_y0 + 24 - g_scroll;
    for (uint32_t i = 0; i < c->nm; i++) { struct lay *l; int hh = alto_msg(c, i, &g, &l); if (my >= yy && my < yy + hh) return (int)i; yy += hh; }
    return -1;
}
/* zonas del Markdown de los mensajes visibles (no estan en H[]: se calculan aqui) */
static int zona_en(int mx, int my, int *msg, int *zone)
{
    struct cc_conv *c = conv_actual();
    if (!c || !c->nm || my < g_view_y0 || my >= g_view_y1 || g_modal) return 0;
    int mxx = g_mx0, mw = g_mw0;
    struct geo g; g.x = mxx; g.w = mw; g.cw = mw - 48 > COL_MAX ? COL_MAX : mw - 48; if (g.cw < 200) g.cw = 200; g.cx = mxx + (mw - g.cw) / 2;
    int yy = g_view_y0 + 24 - g_scroll;
    for (uint32_t i = 0; i < c->nm; i++) {
        struct lay *l; int hh = alto_msg(c, i, &g, &l);
        if (my >= yy && my < yy + hh && l) {
            int ox, oy; const struct cc_msg *m = &c->m[i];
            if (m->rol == CC_USER) {
                int maxw = g.cw * 78 / 100 - 36; if (maxw < 120) maxw = 120;
                int bw = l->cw + 36; if (bw < 60) bw = 60; if (bw > maxw + 36) bw = maxw + 36;
                ox = g.cx + g.cw - bw + 18; oy = yy + 12 + ((m->adj && m->adj[0]) ? 44 : 0);
            } else { ox = g.cx + 46; oy = yy + 30; }
            for (int k = l->nz - 1; k >= 0; k--) {
                const struct md_zone *q = &l->z[k];
                if (mx >= ox + q->x && my >= oy + q->y && mx < ox + q->x + q->w && my < oy + q->y + q->h) { *msg = (int)i; *zone = k; return 1; }
            }
            return 0;
        }
        yy += hh;
    }
    return 0;
}
int chat_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh;
    cambia();
    g_rx = mx; g_ry = my;
    int i = hit_en(mx, my);
    /* las zonas del Markdown van por encima del fondo de los mensajes, pero debajo de botones y ventanas */
    if (!g_modal && !g_menu_conv && (i < 0 || H[i].act == A_NADA)) {
        int msg, zone;
        if (zona_en(mx, my, &msg, &zone)) { struct hit z; z.act = A_ZONA; z.a = msg; z.b = zone; z.f = 0; z.x = mx; z.y = my; z.w = z.h = 1; return accion(&z); }
    }
    if (i < 0) return 1;
    struct hit z = H[i];
    if (z.act != A_FOCO && g_foco == &f_busca && z.act != A_CONV) g_foco = &f_comp;
    return accion(&z);
}
int chat_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held)
{
    int r = 0;
    int dentro = mx >= (int32_t)x && my >= (int32_t)y && mx < (int32_t)(x + w) && my < (int32_t)(y + h);
    if (mx != g_rx || my != g_ry) {
        g_rx = mx; g_ry = my;
        int msg = -1, zone = -1;
        if (dentro && zona_en(mx, my, &msg, &zone)) {} else { msg = -1; zone = -1; }
        if (msg != g_hz_msg || zone != g_hz_zone) { g_hz_msg = msg; g_hz_zone = zone; r = 1; }
        /* lo que se ilumina al pasar: el boton bajo el raton y las acciones del mensaje bajo el raton */
        static int32_t clave_ant = -1;
        int hv = dentro ? hit_en(mx, my) : -1, mm = dentro ? msg_en(my) : -1;
        int32_t clave = hv < 0 ? -1 : (int32_t)((H[hv].act * 7919 + H[hv].a * 31 + H[hv].b) & 0x7FFFFF);
        clave = clave * 31 + mm;
        if (clave != clave_ant) { clave_ant = clave; r = 1; }
        if (g_modal == M_ARCH || g_menu_conv) r = 1;
    }
    if (held && g_foco && g_foco->dragging && tf_drag(g_foco, mx, my, held)) r = 1;
    if (held && g_arrastra && !g_modal) {
        barra_mueve(g_arrastra, my);
        if (g_arrastra == &g_b_main) g_pegado = g_scroll >= g_max_scroll;
        r = 1;
    } else if (!held && g_arrastra) { g_arrastra = 0; r = 1; }
    if (wheel && dentro) {
        int d = -wheel; if (d > 5) d = 5; if (d < -5) d = -5;      /* SOMA_286_YOUTUBE: positivo = rueda hacia arriba (sube; antes al reves), y cuenta cada paso */
        if (g_modal == M_ARCH) g_ascroll += d * 68;
        else if (!g_modal) {
            if (g_side && w >= 640 && mx < (int32_t)x + SIDE_W) g_sscroll += d * 72;
            else { g_scroll += d * 90; if (g_scroll < 0) g_scroll = 0; g_pegado = g_scroll >= g_max_scroll; }
        }
        r = 1;
    }
    if (r) cambia();
    return r;
}
void chat_handle_key(char ch)
{
    int c = (uint8_t)ch, ctrl = keyboard_ctrl_held(), shift = keyboard_shift_held();
    cambia();
    if (g_menu_conv && c == 27) { g_menu_conv = 0; return; }
    if (g_modal == M_ARCH) { if (c == 27 || c == '\n' || c == '\r') { g_modal = M_NO; g_foco = &f_comp; } return; }
    if (g_modal == M_BORRA || g_modal == M_VACIAR) {
        if (c == 27) g_modal = M_NO;
        else if (c == '\n' || c == '\r') { struct hit z; z.act = A_OK; z.a = z.b = 0; z.f = 0; accion(&z); }
        return;
    }
    if (!g_foco) g_foco = &f_comp;
    if (g_foco == &f_comp && (c == '\n' || c == '\r') && !shift) { envia(b_comp ? b_comp : ""); return; }
    if (g_foco == &f_comp && c == 27) { if (g_edit_idx >= 0) { g_edit_idx = -1; tf_set(&f_comp, ""); return; } if (ocupado()) { parar(); return; } return; }
    if (g_foco == &f_comp && (c == 16 || c == 14) && !tf_len(&f_comp)) { g_scroll += c == 14 ? 80 : -80; if (g_scroll < 0) g_scroll = 0; g_pegado = g_scroll >= g_max_scroll; return; }
    int r = tf_key(g_foco, ch, ctrl, shift);
    if (r == TF_ESC) { if (g_modal) g_modal = M_NO; g_foco = &f_comp; return; }
    if (r == TF_ENTER) {
        if (g_modal == M_REN) { struct hit z; z.act = A_OK; z.a = z.b = 0; z.f = 0; accion(&z); }
        else if (g_foco == &f_busca) { uint32_t ids[1]; if (cc_orden(&g_h, b_busca, ids, 1) == 1) abre_conv(ids[0]); }
        return;
    }
    if (r == TF_TAB) g_foco = g_foco == &f_comp ? &f_busca : &f_comp;
}
int chat_clip_op(int op)
{
    if (!g_foco) return 0;
    if (op == CLIP_OP_PASTE && g_foco == &f_comp) {
        uint32_t n = 0; const char *t = clip_text(&n);
        if (t && n) {
            char tmp[512]; uint32_t k = 0;
            for (uint32_t i = 0; i < n; i++) { if (t[i] == '\r') continue; tmp[k++] = t[i] == '\t' ? ' ' : t[i]; if (k == sizeof tmp) { tf_insert(&f_comp, tmp, k); k = 0; } }
            if (k) tf_insert(&f_comp, tmp, k);
        }
        cambia(); return 1;
    }
    tf_clip(g_foco, op);
    cambia();
    return 1;
}
int chat_escape(void)
{
    if (g_menu_conv) { g_menu_conv = 0; cambia(); return 1; }
    if (g_modal) { g_modal = M_NO; g_foco = &f_comp; cambia(); return 1; }
    if (g_edit_idx >= 0) { g_edit_idx = -1; tf_set(&f_comp, ""); cambia(); return 1; }
    if (ocupado()) { parar(); cambia(); return 1; }
    if (g_foco == &f_busca && b_busca[0]) { tf_set(&f_busca, ""); g_foco = &f_comp; cambia(); return 1; }
    return 0;
}
const char *chat_url(void) { return g_url; }
const char *chat_path(void) { return g_path; }
uint32_t chat_version(void) { return g_ver; }
int chat_tick(void)
{
    if (!g_ini) return 0;
    int r = 0; uint64_t t = ms();
    static uint64_t t_anim; static uint32_t fase_vista;
    if (g_w_estado == 3) {
        g_w_estado = 0;
        if (g_turno == T_LLM) turno_acaba(g_w_resp, g_w_ok && !g_w_cancel, g_w_cancel ? "Respuesta detenida." : g_w_err);
        r = 1;
    }
    for (int i = 0; i < NIMG; i++) if (g_img[i].st == IM_BYTES) { img_decodifica(&g_img[i]); r = 1; }
    if (g_fase_ver != fase_vista) { fase_vista = g_fase_ver; r = 1; }
    if ((ocupado() || g_rev_idx >= 0 || (g_toast[0] && t - g_toast_t < 3600)) && t - t_anim >= 40) { t_anim = t; r = 1; }
    if (g_sucio && t - g_t_sucio > 1500 && !ocupado()) guarda();
    if (r) cambia();
    return r;
}
