/* SOMA_PARCHE_NAVEGADOR: navegador web profesional de SOMA.
 * Hilo GUI: dibuja y encola eventos. Hilo trabajador ("web_worker", pila propia de 1 MiB): red, HTML, CSS, JavaScript, maquetacion y temporizadores. */
#include "browser.h"
#include "web_fault.h"
#include "mutex.h"
#include "tcp.h"
#include "savedlg.h"
#include "acct.h"                                 /* PARCHE 085 */
#include "../ui/clipboard.h"
#include "web_page.h"
#include "web_font.h"
#include "fb.h"
#include "mouse.h"
#include "thread.h"
#include "idt.h"
#include "mem.h"
#include "fs.h"
#include "image.h"
#include "gui_font.h"
#include "web_dom.h"
#include "web_util.h"
#include "web_scroll_149.h" /* SOMA_149_SCROLL_FASTPATH */
#include "web_fluido_152.h" /* SOMA_152_WEB_FLUIDO */

#ifndef SOMA_WEB_FAST_SCROLL
#define SOMA_WEB_FAST_SCROLL 1
#endif
#ifndef SOMA_WEB_PERF_LOG
#define SOMA_WEB_PERF_LOG 0
#endif
/* SOMA_152_WEB_FLUIDO: desplazamiento suave (animado) como Chrome/Firefox; paso por muesca de la rueda */
#ifndef SOMA_WEB_SMOOTH_SCROLL
#define SOMA_WEB_SMOOTH_SCROLL 1
#endif
#ifndef SOMA_WEB_WHEEL_PX
#define SOMA_WEB_WHEEL_PX 80
#endif
#include "console.h"

#define MAX_TABS 6
#define HOME_URL "file:///workspace/demo_web.html"
#define WK_STACK (1024u * 1024u)
#define JQ 48

/* ------------------------------------------------------------ servicios que exige el motor web (tiempo, cesion, pila) */
uint64_t web_now_ms(void) { return timer_get_uptime_ms(); }
static struct tcb *s_wk_tcb;          /* solo el hilo trabajador cede desde los bucles largos del motor: la GUI nunca debe ceder a mitad de pintar */
void web_yield(void) { if (s_wk_tcb && thread_current() == s_wk_tcb) thread_yield(); }
void jpeg_yield_hook(void) { wu_tick(); }      /* SOMA_PARCHE_028_UI: el JPEG del sistema tambien cede desde el hilo del navegador */
static char *s_wk_lo, *s_wk_hi;       /* pila del hilo trabajador */
static char *s_gu_lo, *s_gu_hi;       /* pila grande del hilo de la GUI (dibujo y pruebas de impacto recursivos) */
extern char *g_co_lo, *g_co_hi;   /* web_js_co.c */
uint32_t web_stack_left(void)
{
    char c; char *sp = &c;
    if (g_co_lo && sp >= g_co_lo && sp < g_co_hi) return (uint32_t)(sp - g_co_lo);   /* SOMA_183: pila de una corrutina JS */
    if (s_wk_lo && sp >= s_wk_lo && sp < s_wk_hi) return (uint32_t)(sp - s_wk_lo);
    if (s_gu_lo && sp >= s_gu_lo && sp < s_gu_hi) return (uint32_t)(sp - s_gu_lo);
    return 8000;                       /* pila desconocida (p. ej. la del nucleo, 64 KiB): ser conservador */
}
/* SOMA 189: tope de la pila conocida que contiene sp (la del trabajador o la grande de la GUI); 0 si es otra */
char *web_stack_hi(const char *sp)
{
    if (s_wk_lo && sp >= s_wk_lo && sp < s_wk_hi) return s_wk_hi;
    if (s_gu_lo && sp >= s_gu_lo && sp < s_gu_hi) return s_gu_hi;
    return 0;
}
void web_call_on_stack(char *top, void (*fn)(void *), void *arg);
/* El arbol de cajas se recorre de forma recursiva: en el hilo de la GUI (pila corta) se hace sobre una pila propia de 512 KiB. */
#define GU_STACK (512u * 1024u)
static void on_gui_stack(void (*fn)(void *), void *arg)
{
    char c; char *sp = &c;
    if (s_gu_lo && sp >= s_gu_lo && sp < s_gu_hi) { fn(arg); return; }
    if (!s_gu_lo) {
        char *stk = (char *)kmalloc(GU_STACK);
        if (!stk) { fn(arg); return; }
        s_gu_hi = stk + GU_STACK; s_gu_lo = stk + 8192;
    }
    web_call_on_stack((char *)(((uintptr_t)s_gu_hi) & ~(uintptr_t)15), fn, arg);
}

/* ------------------------------------------------------------ estado */
struct tab { char url[2048]; char title[64]; int scroll; struct wp_hist hist[WP_HIST]; int hn, hpos; };
enum { J_LOAD = 1, J_BACK, J_FWD, J_RELOAD, J_CLICK, J_KEY, J_SELECT, J_SWITCH, J_NEWTAB, J_CLOSE, J_EVAL, J_FIND, J_HOME };
struct job { int type, a, b, c; char *s; };

static struct wpage *s_pg;
static struct tab *s_tabs[MAX_TABS];
static int s_ntabs, s_cur;
static struct job s_q[JQ];
static volatile uint32_t s_qh, s_qt;
static volatile int s_job_busy, s_inited, s_worker_up;
static volatile uint64_t s_last_render;
static volatile int s_move_pending, s_move_x, s_move_y;
/* SOMA_152_WEB_FLUIDO: estado del desplazamiento suave (ver smooth_wheel152) */
static int s152_tgt = -1, s152_anim_sy;
static uint64_t s152_anim_ms;
static volatile uint64_t s152_scroll_ms;
static volatile int s152_hover_deferred;

static uint32_t s_win_x, s_win_y, s_win_w, s_win_h;

static char s_url[2048]; static int s_url_len, s_url_cur, s_url_focus, s_url_all;
static char s_find[80]; static int s_find_len, s_find_open;
static char s_hover_href[300]; static char s_jserr[140]; static uint64_t s_jserr_t;
static int s_dd_hl = -1; static int s_sb_drag, s_sb_grab;
static uint8_t s_u8[4]; static int s_u8n, s_u8need;
static uint64_t s_last_click_t; static int s_last_cx, s_last_cy;
static int s_cursor_cur = -1;

/* ------------------------------------------------------------ servicios para web_page.c */
int wp_fs_read(const char *path, char **out, uint32_t *len)
{
    {   /* PARCHE 087: el archivo entero, mida lo que mida */
        uint32_t l = 0; char *a = vfs_read_alloc(path, &l);
        if (a && l > 0) { *out = a; *len = l; return 0; }
        if (a) kfree(a);
    }
    const uint32_t MAXF = 1u << 18;
    char *tmp = (char *)kmalloc(MAXF + 1); if (!tmp) return -1;
    int r = -1;
    if (r <= 0) {
        uint8_t *bm = (uint8_t *)tmp; int n = 0;
        if (!wu_strieq(path, "/workspace/logo_soma.bmp") && !wu_strieq(path, "/workspace/chip_soma.bmp")) { kfree(tmp); return -1; }
        if (wu_strieq(path, "/workspace/logo_soma.bmp")) n = image_create_demo_bmp(bm, 200000, 80, 40); else n = image_create_chip_bmp(bm, 200000, 64, 32);
        if (n <= 0) { kfree(tmp); return -1; }
        vfs_write(path, tmp, (uint32_t)n); r = n;
    }
    char *o = (char *)kmalloc((uint32_t)r + 1); if (!o) { kfree(tmp); return -1; }
    memcpy(o, tmp, (uint32_t)r); o[r] = 0; kfree(tmp); *out = o; *len = (uint32_t)r; return 0;
}
void wp_log(int level, const char *msg)
{
    if (level >= 2) { wu_copy(s_jserr, msg, sizeof s_jserr); s_jserr_t = web_now_ms(); }
    if (msg && (msg[0] == 'i' && msg[1] == 'm' && msg[2] == 'g')) { kprint("[web] "); kprint(msg); kprint("\n"); }   /* diagnostico de imagenes por el puerto serie */
}

/* ------------------------------------------------------------ SOMA_202: NAVEGADOR AISLADO (x86_64)
 * Con el modo aislado activo el motor no corre aqui sino en un proceso de ring 3 (src/user/websvc.c). `s_pg` pasa a ser una
 * SOMBRA: una struct wpage del nucleo que solo guarda lo que la interfaz necesita ver (URL, titulo, fase, desplazamiento,
 * historial...), rellenada con el estado que publica el proceso. Las funciones del motor que usa este archivo se sustituyen mas
 * abajo por envoltorios bx_*: en modo normal llaman a la de siempre; en modo aislado mandan una orden al proceso o responden con
 * el ultimo estado recibido. Asi el resto del archivo no cambia. Los punteros a nodos y a la maquetacion son centinelas: nunca se
 * siguen (el arbol de la pagina esta en otro espacio de direcciones). */
#if defined(__x86_64__)
#include "web_r3.h"
#define R3_CONF "/etc/navegador.conf"
#ifndef SOMA_WEB_R3_DEFAULT
#define SOMA_WEB_R3_DEFAULT 1                     /* sin /etc/navegador.conf: aislado */
#endif
static int s_r3;                                  /* 1 = esta sesion usa el motor aislado */
static struct wlay r3_lay; static struct wdoc r3_doc;
static struct wnode r3_n_dd, r3_n_link, r3_n_hit, r3_n_focus, r3_n_id, r3_n_opt[W3_DD_MAX];
static char r3_id[128];
static int r3_sy_sent = -1, r3_sx_sent = -1, r3_sy_seen = -1, r3_alert_shown, r3_dd_shown, r3_hist_dirty, r3_force_hist; static uint32_t r3_hist_key;
static void r3_log(int level, const char *msg) { wp_log(level, msg); }
int browser_r3_wanted(void)
{
    char t[64]; int n = vfs_read(R3_CONF, t, sizeof t - 1); t[n > 0 ? n : 0] = 0;
    for (const char *q = t; *q; q++) if (wu_starts(q, "aislado=")) return q[8] == 's' || q[8] == '1';
    return SOMA_WEB_R3_DEFAULT;
}
int browser_r3_active(void) { return s_r3; }
static int r3_ensure(struct wpage *p)
{
    if (web_r3_alive()) return 1;
    web_r3_set_log(r3_log);
    if (web_r3_start(p->vw > 0 ? p->vw : 1024, p->vh > 0 ? p->vh : 700) != 0) return 0;
    r3_sy_sent = r3_sx_sent = r3_sy_seen = -1; r3_alert_shown = r3_dd_shown = 0;
    return 1;
}
static void r3_push_hist(struct wpage *p) { int32_t a[2] = { p->hn, p->hpos }; uint32_t s2 = web_r3_send(W3C_SET_HIST, a, 2, p->hist, sizeof p->hist); if (s2) (void)web_r3_wait(s2, 3000); }
static void r3_pull_hist(struct wpage *p)
{
    static struct wp_hist tmp[WP_HIST];
    if (!web_r3_alive()) return;
    if (web_r3_call(W3C_GET_HIST, 0, 0, 0, 0, tmp, sizeof tmp, 3000) == (int)sizeof tmp) {
        for (int i = 0; i < WP_HIST; i++) { tmp[i].url[sizeof tmp[i].url - 1] = 0; tmp[i].title[sizeof tmp[i].title - 1] = 0; }
        memcpy(p->hist, tmp, sizeof tmp);
    }
    r3_hist_dirty = 0;
}
/* Trae a la sombra el ultimo estado del proceso y le manda lo que la interfaz haya cambiado (desplazamiento, cierres). */
static void r3_sync(struct wpage *p)
{
    const struct w3_state *st = web_r3_state();
    if (!s_r3 || !p || !st || !web_r3_alive()) return;
    if (p->stop) { p->stop = 0; web_r3_request_stop(); }
    wu_copy(p->url, st->url, sizeof p->url); wu_copy(p->title, st->title, sizeof p->title); wu_copy(p->status, st->status, sizeof p->status);
    p->status_kind = st->status_kind; p->phase = st->phase; p->progress = st->progress;
    p->img_ok = st->img_ok; p->img_fail = st->img_fail; p->nimg = st->nimg; p->gen = web_r3_frame_seq();
    uint32_t hk = (uint32_t)st->hn * 131u + (uint32_t)st->hpos + wu_hash(st->url, wu_strlen(st->url));
    if (hk != r3_hist_key) { r3_hist_key = hk; r3_hist_dirty = 1; }
    p->hn = st->hn; p->hpos = st->hpos;
    p->lay = &r3_lay; p->doc = &r3_doc; p->lay_busy = 0;
    p->focus = st->focus_text ? &r3_n_focus : 0;
    /* aviso (alert): la interfaz lo cierra poniendo alert_pending a 0 */
    if (r3_alert_shown && !p->alert_pending) { (void)web_r3_send(W3C_ALERT_ACK, 0, 0, 0, 0); r3_alert_shown = 2; }
    if (st->alert_pending) { if (r3_alert_shown == 0) { p->alert_pending = 1; wu_copy(p->alert_msg, st->alert_msg, sizeof p->alert_msg); r3_alert_shown = 1; } } else { r3_alert_shown = 0; p->alert_pending = 0; }
    /* <select> desplegado: la interfaz lo cierra poniendo dd_sel a 0 */
    if (r3_dd_shown == 1 && !p->dd_sel) { int32_t a[1] = { -1 }; (void)web_r3_send(W3C_SELECT, a, 1, 0, 0); r3_dd_shown = 2; }
    if (st->dd_open) { if (r3_dd_shown == 0) { p->dd_sel = &r3_n_dd; r3_dd_shown = 1; } } else { r3_dd_shown = 0; p->dd_sel = 0; }
    if (st->scroll_req) { p->scroll_req = st->scroll_req; web_r3_scroll_req_done(); }
    /* desplazamiento: manda la interfaz; si lo movio el motor (ancla, historial, script), se adopta */
    if (st->sy != r3_sy_seen) { r3_sy_seen = st->sy; if (st->sy != r3_sy_sent && !web_r3_pending()) { p->sy = st->sy; p->sx = st->sx; r3_sy_sent = st->sy; r3_sx_sent = st->sx; } }
    if (p->sy != r3_sy_sent || p->sx != r3_sx_sent) { int32_t a[2] = { p->sx, p->sy }; if (web_r3_send(W3C_SCROLL, a, 2, 0, 0)) { r3_sy_sent = p->sy; r3_sx_sent = p->sx; } }
}
static void r3_recover(struct wpage *p)
{
    kprint("[WEB] El proceso del navegador ha terminado. Se cierra la pagina y se lanza un proceso nuevo; el sistema no se ha visto afectado.\n");
    if (!r3_ensure(p)) return;
    r3_push_hist(p);
    int32_t a[1] = { 0 }; (void)web_r3_send(W3C_NAVIGATE, a, 1, "about:fallo", 11);
}
static int r3_run(struct wpage *p, int op, const int32_t *a, int na, const char *data, uint32_t timeout_ms)
{
    if (!r3_ensure(p)) { kprint("[WEB] no se pudo lanzar el proceso del navegador aislado\n"); return -1; }
    uint32_t s2 = web_r3_send(op, a, na, data, data ? wu_strlen(data) : 0);
    if (!s2) return -1;
    uint64_t t0 = web_now_ms(); int r;
    while ((r = web_r3_wait(s2, 40)) == 0 && web_now_ms() - t0 < timeout_ms) if (p->stop) { p->stop = 0; web_r3_request_stop(); }
    if (r < 0) { r3_recover(p); return -1; }
    return 0;
}

static struct wpage *bx_new(void)
{
    if (!browser_r3_wanted()) return wp_new();
    struct wpage *p = (struct wpage *)kmalloc(sizeof *p);
    if (!p) return 0;
    memset(p, 0, sizeof *p); p->hpos = -1; s_r3 = 1;
    return p;
}
static int bx_navigate(struct wpage *p, const char *url, int flags)
{
    if (!s_r3) return wp_navigate(p, url, flags);
    const struct w3_state *st = web_r3_state();
    if (!r3_ensure(p)) {
        uint32_t starts = 0; web_r3_stats(&starts, 0, 0, 0, 0);
        if (starts) return -1;
        /* el proceso no ha llegado a arrancar ni una vez (sin memoria, sin ranuras): esta sesion sigue con el motor dentro del nucleo */
        struct wpage *np = wp_new();
        if (!np) return -1;
        kprint("[WEB] AVISO: no se pudo lanzar el navegador aislado; esta sesion usa el motor dentro del nucleo.\n");
        memcpy(np->hist, p->hist, sizeof np->hist); np->hn = p->hn; np->hpos = p->hpos;
        if (p->vw > 0 && p->vh > 0) wp_set_viewport(np, p->vw, p->vh);
        s_r3 = 0; s_pg = np;                              /* la sombra no se libera: puede quedar algun puntero a ella en la pila de quien llama */
        return wp_navigate(np, url, flags);
    }
    if (r3_force_hist || (st && (p->hn != st->hn || p->hpos != st->hpos))) { r3_force_hist = 0; r3_push_hist(p); }   /* la interfaz cambio el historial (pestana nueva o restaurada) */
    int32_t a[1] = { flags };
    int r = r3_run(p, W3C_NAVIGATE, a, 1, url, 180000);
    r3_hist_dirty = 1;
    return r;
}
static void bx_history_go(struct wpage *p, int delta)
{
    if (!s_r3) { wp_history_go(p, delta); return; }
    int t = p->hpos + delta;                              /* la entrada de destino, segun la copia del historial que guarda la interfaz */
    int32_t a[2] = { delta, (t >= 0 && t < p->hn && t < WP_HIST && (wu_starts(p->hist[t].url, "file:") || wu_starts(p->hist[t].url, "about:"))) ? 1 : 0 };
    (void)r3_run(p, W3C_HISTORY, a, 2, 0, 180000); r3_hist_dirty = 1;
}
static int bx_can_back(const struct wpage *p) { if (!s_r3) return wp_can_back(p); const struct w3_state *st = web_r3_state(); return st ? st->can_back : 0; }
static int bx_can_fwd(const struct wpage *p) { if (!s_r3) return wp_can_fwd(p); const struct w3_state *st = web_r3_state(); return st ? st->can_fwd : 0; }
static int bx_doc_height(const struct wpage *p) { if (!s_r3) return wp_doc_height(p); const struct w3_state *st = web_r3_state(); return st ? st->doc_h : 0; }
static void bx_scroll_by(struct wpage *p, int dx, int dy)
{
    if (!s_r3) { wp_scroll_by(p, dx, dy); return; }
    int m = bx_doc_height(p) - p->vh; if (m < 0) m = 0;
    p->sy += dy; if (p->sy > m) p->sy = m; if (p->sy < 0) p->sy = 0;
    p->sx += dx; if (p->sx < 0) p->sx = 0;
}
static int bx_tick(struct wpage *p, uint64_t now)
{
    if (!s_r3) return wp_tick(p, now);
    if (!web_r3_alive()) { if (web_r3_state() && web_r3_state()->magic) r3_recover(p); return 40; }
    if (r3_hist_dirty && p->phase == WP_IDLE) r3_pull_hist(p);
    return 30;
}
static void bx_set_viewport(struct wpage *p, int vw, int vh)
{
    if (!s_r3) { wp_set_viewport(p, vw, vh); return; }
    /* Si dos vistas de distinto tamano se dibujan a la vez (la ventana y la vista heredada a pantalla completa de gui.c), manda la
     * mayor: alternar haria remaquetar la pagina en cada fotograma. SOMA_203: una vista menor solo se acepta cuando la mayor lleva
     * 400 ms sin pedirse (antes se aceptaba cada 1,5 s aunque la mayor siguiera viva: dos maquetaciones completas cada 1,5 s). */
    static uint64_t big_ms; uint64_t now = web_now_ms();
    if (vw == p->vw && vh == p->vh) { big_ms = now; return; }
    if (p->vw > 0 && (int64_t)vw * vh < (int64_t)p->vw * p->vh && now - big_ms < 400) return;
    big_ms = now;
    p->vw = vw; p->vh = vh;
    if (web_r3_alive()) { int32_t a[2] = { vw, vh }; (void)web_r3_send(W3C_VIEWPORT, a, 2, 0, 0); }
}
static void bx_select_choose(struct wpage *p, struct wnode *sel, int index) { if (!s_r3) { wp_select_choose(p, sel, index); return; } int32_t a[1] = { index }; (void)web_r3_send(W3C_SELECT, a, 1, 0, 0); r3_dd_shown = 2; p->dd_sel = 0; }
static void bx_paint(struct wpage *p, int sx, int sy, int sw, int sh) { if (!s_r3) { wp_paint(p, sx, sy, sw, sh); return; } web_r3_blit(sx, sy, sw, sh, p->sx, p->sy); }
static void bx_mouse_move(struct wpage *p, int px, int py) { if (!s_r3) { wp_mouse_move(p, px, py); return; } int32_t a[2] = { px, py }; (void)web_r3_send(W3C_MOVE, a, 2, 0, 0); }
static void bx_mouse_click(struct wpage *p, int px, int py, int button, int mods, int dbl) { if (!s_r3) { wp_mouse_click(p, px, py, button, mods, dbl); return; } int32_t a[5] = { px, py, button, mods, dbl }; (void)web_r3_send(W3C_CLICK, a, 5, 0, 0); }
static void bx_key(struct wpage *p, int key, const char *utf8, int mods) { if (!s_r3) { wp_key(p, key, utf8, mods); return; } int32_t a[2] = { key, mods }; (void)web_r3_send(W3C_KEY, a, 2, utf8, utf8 ? wu_strlen(utf8) : 0); }
static int bx_hit_cursor(struct wpage *p, int px, int py) { if (!s_r3) return wp_hit_cursor(p, px, py); const struct w3_state *st = web_r3_state(); return st ? st->cursor : WPK_NONE; }
static void bx_find_scroll(struct wpage *p, const char *text) { if (!s_r3) { wp_find_scroll(p, text); return; } (void)web_r3_send(W3C_FIND, 0, 0, text, wu_strlen(text)); }
static void bx_eval_js(struct wpage *p, const char *code, char *out, uint32_t max) { if (!s_r3) { wp_eval_js(p, code, out, max); return; } if (max) out[0] = 0; (void)web_r3_send(W3C_EVAL, 0, 0, code, wu_strlen(code)); }
/* SOMA_203: Guardar/Copiar imagen en modo aislado. Al abrir el menu (se pide `abs`) solo se pregunta si la imagen esta; al
 * elegir la opcion se traen los pixeles a un buffer del nucleo, que cm_click suelta al terminar. */
static int bx_img_get(struct wpage *p, const char *src, const uint32_t **px, int *w, int *h, char *abs, uint32_t absmax) { if (!s_r3) return wp_img_get(p, src, px, w, h, abs, absmax); return web_r3_get_image(src, abs == 0, px, w, h, abs, absmax); }
static struct wnode r3_n_rhit, r3_n_rlink; static char r3_rhit[2 * 2048 + 8]; static const char *r3_rhit_link = "", *r3_rhit_img = "";
/* clic derecho: que hay bajo el puntero (enlace e imagen) se pregunta al proceso; durante una carga no contesta y no se espera */
static struct wnode *r3_right_hit(struct wpage *p, int x, int y, struct wnode **link)
{
    int32_t a[2] = { x, y }; r3_rhit_link = r3_rhit_img = ""; *link = 0;
    if (p->phase != WP_IDLE || !web_r3_alive()) return 0;
    int n = web_r3_call(W3C_HIT, a, 2, 0, 0, r3_rhit, sizeof r3_rhit - 2, 800);
    if (n < 2) return 0;
    r3_rhit[n] = 0; r3_rhit[n + 1] = 0;
    r3_rhit_link = r3_rhit; r3_rhit_img = r3_rhit + wu_strlen(r3_rhit) + 1;
    if (r3_rhit_link[0]) *link = &r3_n_rlink;
    return &r3_n_rhit;
}
static struct wnode *bx_hit(struct wlay *L, int x, int y, struct wnode **link, struct wbox **box)
{
    if (!s_r3) return wl_hit(L, x, y, link, box);
    const struct w3_state *st = web_r3_state();
    if (link) *link = (st && st->hover_href[0]) ? &r3_n_link : 0;
    if (box) *box = 0;
    return &r3_n_hit;
}
static const char *bx_attr(const struct wnode *n, const char *name)
{
    if (!s_r3) return wd_attr(n, name);
    const struct w3_state *st = web_r3_state();
    if (n == &r3_n_rlink && name && name[0] == 'h') return r3_rhit_link;                   /* SOMA_203 */
    return (n == &r3_n_link && st && name && name[0] == 'h') ? st->hover_href : 0;
}
static const char *bx_img_src(struct wnode *n) { if (!s_r3) return wl_img_src(n); return (n == &r3_n_rhit && r3_rhit_img[0]) ? r3_rhit_img : 0; }
static uint32_t bx_inner_gen(struct wlay *L) { if (!s_r3) return wl_inner_gen(L); return web_r3_frame_seq(); }
/* SOMA_203: rueda sobre cajas con desplazamiento propio. Si la pagina tiene alguna, la rueda se manda al motor, que mueve la
 * caja bajo el puntero o, si no hay o ya llego al tope, devuelve el desplazamiento a la pagina (scroll_req). Sin cajas asi, la
 * rueda sigue moviendo la pagina aqui mismo, sin esperar a nadie. */
static int bx_inner_scroll(struct wlay *L, int x, int y, int dy)
{
    if (!s_r3) return wl_inner_scroll(L, x, y, dy);
    const struct w3_state *st = web_r3_state(); (void)L;
    if (!st || !st->inner_scroll || !dy) return 0;
    int32_t a[3] = { x, y, dy };
    return web_r3_send(W3C_INNER_SCROLL, a, 3, 0, 0) != 0;
}
static int bx_node_rect(struct wlay *L, struct wnode *n, int *x, int *y, int *w, int *h)
{
    if (!s_r3) return wl_node_rect(L, n, x, y, w, h);
    const struct w3_state *st = web_r3_state();
    if (n != &r3_n_dd || !st || !st->dd_open) return 0;
    *x = st->dd_x; *y = st->dd_y; *w = st->dd_w; *h = st->dd_h; return 1;
}
static int bx_select_count(struct wnode *n) { if (!s_r3) return wl_select_count(n); const struct w3_state *st = web_r3_state(); return st ? st->dd_count : 0; }
static int bx_select_index(struct wnode *n) { if (!s_r3) return wl_select_index(n); const struct w3_state *st = web_r3_state(); return st ? st->dd_cur : 0; }
static struct wnode *bx_select_option(struct wnode *n, int idx) { if (!s_r3) return wl_select_option(n, idx); return (idx >= 0 && idx < W3_DD_MAX) ? &r3_n_opt[idx] : 0; }
static struct wnode *bx_by_id(struct wnode *root, const char *id) { if (!s_r3) return wd_by_id(root, id); wu_copy(r3_id, id, sizeof r3_id); return &r3_n_id; }
static void bx_text_content(const struct wnode *n, struct wb *out)
{
    if (!s_r3) { wd_text_content(n, out); return; }
    const struct w3_state *st = web_r3_state();
    if (n >= r3_n_opt && n < r3_n_opt + W3_DD_MAX) { if (st) wb_add(out, st->dd_opt[n - r3_n_opt]); return; }
    if (n == &r3_n_id) {
        static char tmp[8192];
        int k = web_r3_call(W3C_GET_TEXT, 0, 0, r3_id, wu_strlen(r3_id), tmp, sizeof tmp - 1, 3000);
        if (k > 0) wb_addn(out, tmp, (uint32_t)k);
    }
}
static void bx_set_text(struct wdoc *d, struct wnode *n, const char *text)
{
    if (!s_r3) { wd_set_text(d, n, text); return; }
    if (n != &r3_n_id) return;
    static char b[4096]; uint32_t il = wu_strlen(r3_id), tl = wu_strlen(text);
    if (il + 1 + tl >= sizeof b) return;
    memcpy(b, r3_id, il + 1); memcpy(b + il + 1, text, tl);
    (void)web_r3_call(W3C_SET_TEXT, 0, 0, b, il + 1 + tl, 0, 0, 3000);
}
#define wp_new          bx_new
#define wp_navigate     bx_navigate
#define wp_history_go   bx_history_go
#define wp_can_back     bx_can_back
#define wp_can_fwd      bx_can_fwd
#define wp_doc_height   bx_doc_height
#define wp_scroll_by    bx_scroll_by
#define wp_tick         bx_tick
#define wp_set_viewport bx_set_viewport
#define wp_select_choose bx_select_choose
#define wp_paint        bx_paint
#define wp_mouse_move   bx_mouse_move
#define wp_mouse_click  bx_mouse_click
#define wp_key          bx_key
#define wp_hit_cursor   bx_hit_cursor
#define wp_find_scroll  bx_find_scroll
#define wp_eval_js      bx_eval_js
#define wp_img_get      bx_img_get
#define wl_hit          bx_hit
#define wd_attr         bx_attr
#define wl_img_src      bx_img_src
#define wl_inner_gen    bx_inner_gen
#define wl_inner_scroll bx_inner_scroll
#define wl_node_rect    bx_node_rect
#define wl_select_count bx_select_count
#define wl_select_index bx_select_index
#define wl_select_option bx_select_option
#define wd_by_id        bx_by_id
#define wd_text_content bx_text_content
#define wd_set_text     bx_set_text
#else
#define s_r3 0
static struct wnode *r3_right_hit(struct wpage *p, int x, int y, struct wnode **link) { (void)p; (void)x; (void)y; *link = 0; return 0; }
static void web_r3_image_free(void) { }
static void r3_sync(struct wpage *p) { (void)p; }
static void r3_pull_hist(struct wpage *p) { (void)p; }
#endif

/* ------------------------------------------------------------ cola de trabajos */
static void start_worker(void);
static int q_push(int type, int a, int b, int c, const char *s)
{
    start_worker();
    uint32_t nt = (s_qt + 1) % JQ; if (nt == s_qh) return 0;
    struct job *j = &s_q[s_qt]; j->type = type; j->a = a; j->b = b; j->c = c; j->s = 0;
    if (s) { uint32_t n = wu_strlen(s); j->s = (char *)kmalloc(n + 1); if (!j->s) return 0; memcpy(j->s, s, n + 1); }
    s_qt = nt; return 1;
}
static int q_pop(struct job *out)
{
    if (s_qh == s_qt) return 0;
    *out = s_q[s_qh]; s_qh = (s_qh + 1) % JQ; return 1;
}

/* ------------------------------------------------------------ pestanas (la pagina viva es una sola; las demas se descartan y se recargan al volver) */
static void tab_save(void)
{
    struct tab *t = s_tabs[s_cur]; if (!t || !s_pg) return;
    if (s_r3) r3_pull_hist(s_pg);                                           /* SOMA_202: el historial vivo esta en el proceso */
    memcpy(t->hist, s_pg->hist, sizeof t->hist); t->hn = s_pg->hn; t->hpos = s_pg->hpos;
    wu_copy(t->url, s_pg->url, sizeof t->url); wu_copy(t->title, s_pg->title, sizeof t->title); t->scroll = s_pg->sy;
    if (t->hpos >= 0 && t->hpos < t->hn) t->hist[t->hpos].scroll_y = s_pg->sy;
}
static struct tab *tab_new(void)
{
    struct tab *t = (struct tab *)kmalloc(sizeof *t); if (!t) return 0; memset(t, 0, sizeof *t); t->hpos = -1; return t;
}
static void tab_load(int i)
{
    struct tab *t = s_tabs[i]; if (!t || !s_pg) return;
    s_cur = i;
    memcpy(s_pg->hist, t->hist, sizeof t->hist); s_pg->hn = t->hn; s_pg->hpos = t->hpos;
#if defined(__x86_64__)
    r3_force_hist = 1;                                                      /* SOMA_202 */
#endif
    if (t->url[0] && t->hpos >= 0) { s_pg->hist[t->hpos].scroll_y = t->scroll; wp_navigate(s_pg, t->url, WPN_NOHIST); }
    else wp_navigate(s_pg, HOME_URL, 0);
}

static void run_job(struct job *j)
{
    struct wpage *p = s_pg; if (!p) return;
    switch (j->type) {
    case J_LOAD: s_job_busy = 1; wp_navigate(p, j->s ? j->s : "", j->a ? WPN_REPLACE : 0); s_job_busy = 0; break;
    case J_HOME: s_job_busy = 1; wp_navigate(p, HOME_URL, 0); s_job_busy = 0; break;
    case J_BACK: s_job_busy = 1; wp_history_go(p, -1); s_job_busy = 0; break;
    case J_FWD: s_job_busy = 1; wp_history_go(p, 1); s_job_busy = 0; break;
    case J_RELOAD: s_job_busy = 1; wp_navigate(p, p->url, WPN_RELOAD); s_job_busy = 0; break;
    case J_CLICK: wp_mouse_click(p, j->a, j->b, j->c & 15, (j->c >> 4) & 15, (j->c >> 8) & 1); break;
    case J_KEY: wp_key(p, j->a, j->s ? j->s : "", j->c); break;
    case J_SELECT: if (p->dd_sel) wp_select_choose(p, p->dd_sel, j->a); break;
    case J_EVAL: { char o[64]; wp_eval_js(p, j->s ? j->s : "", o, sizeof o); break; }
    case J_FIND: wp_find_scroll(p, j->s ? j->s : ""); break;
    case J_SWITCH:
        if (j->a >= 0 && j->a < s_ntabs && j->a != s_cur) { s_job_busy = 1; tab_save(); tab_load(j->a); s_job_busy = 0; }
        break;
    case J_NEWTAB:
        if (s_ntabs < MAX_TABS) {
            struct tab *t = tab_new(); if (!t) break;
            s_job_busy = 1; tab_save(); s_tabs[s_ntabs] = t; s_cur = s_ntabs++;
            s_pg->hn = 0; s_pg->hpos = -1; wp_navigate(s_pg, j->s && j->s[0] ? j->s : HOME_URL, 0); s_job_busy = 0;
        }
        break;
    case J_CLOSE: {
        int i = j->a; if (i < 0 || i >= s_ntabs) break;
        if (s_ntabs == 1) { s_job_busy = 1; s_pg->hn = 0; s_pg->hpos = -1; wp_navigate(s_pg, "about:blank", 0); s_job_busy = 0; break; }
        if (i != s_cur) { kfree(s_tabs[i]); for (int k = i; k < s_ntabs - 1; k++) s_tabs[k] = s_tabs[k + 1]; s_ntabs--; if (s_cur > i) s_cur--; break; }
        kfree(s_tabs[i]); for (int k = i; k < s_ntabs - 1; k++) s_tabs[k] = s_tabs[k + 1]; s_ntabs--;
        int n = i < s_ntabs ? i : s_ntabs - 1; s_job_busy = 1; tab_load(n); s_job_busy = 0; break; }
    default: break;
    }
}
/* ------------------------------------------------------------ SOMA_198: contencion de fallos del motor (web_fault.h) */
#if defined(__x86_64__)
static void *s_wk_jb[5];                               /* punto seguro del hilo de trabajo (__builtin_setjmp) */
static volatile int s_wk_armed, s_wk_recovering;
static uint32_t s_wk_faults;
static int s_wkf_vec; static uint64_t s_wkf_rip, s_wkf_addr;
static char s_wkf_text[160];
volatile int web_fault_inject;
uint32_t web_fault_count(void) { return s_wk_faults; }
const char *web_fault_text(void) { return s_wkf_text; }
__attribute__((noinline)) void web_fault_fire(int phase)
{
    web_fault_inject = 0;
    if (phase == 4) { __asm__ volatile ("ud2"); return; }
    if (phase == 5) { char b[16]; volatile char *volatile q = b; for (int i = 0; i < 64; i++) q[i] = (char)0x41; __asm__ volatile ("" : : "r"(b)); return; }   /* pisa el canario: lo detecta el protector de pila */
    { volatile uintptr_t nul = 8; *(volatile int *)nul = 1; }                                       /* puntero nulo */
}
static __attribute__((noreturn, noinline)) void web_fault_resume(void)
{
    struct tcb *me = thread_current();
    kmutex_release_all_for_thread(me);                 /* como al matar un hilo: cerrojos y conexiones que tenia a medias */
    tcp_cleanup_for_thread(me->tid);
    __builtin_longjmp(s_wk_jb, 1);
}
static int web_fault_ok(void)
{
    return s_wk_armed && !s_wk_recovering && s_wk_lo && thread_current() == s_wk_tcb && s_wk_faults < WEB_FAULT_MAX && !kheap_busy();
}
int web_fault_recover(int vector, uint64_t fault_rip, uint64_t fault_addr, uint64_t *rip, uint64_t *rsp)
{
    if (vector != 0 && vector != 6 && vector != 13 && vector != 14) return 0;
    if (!web_fault_ok()) return 0;
    s_wk_recovering = 1; s_wk_armed = 0;
    s_wkf_vec = vector; s_wkf_rip = fault_rip; s_wkf_addr = fault_addr;
    *rip = (uint64_t)(uintptr_t)web_fault_resume;
    *rsp = (((uint64_t)(uintptr_t)s_wk_lo + 65536) & ~15ull) - 8;      /* parte baja de la pila del hilo: lo que hubiera ahi ya se abandona */
    return 1;
}
void web_fault_from_ssp(void)
{
    if (!web_fault_ok()) return;
    s_wk_recovering = 1; s_wk_armed = 0;
    s_wkf_vec = -1; s_wkf_rip = (uint64_t)(uintptr_t)__builtin_return_address(0); s_wkf_addr = 0;
    web_fault_resume();
}
static void wkf_hex(char *d, uint32_t max, uint64_t v)
{
    uint32_t n = wu_strlen(d); if (n + 19 >= max) return;
    d[n++] = '0'; d[n++] = 'x';
    for (int i = 60; i >= 0; i -= 4) d[n++] = "0123456789abcdef"[(v >> i) & 15];
    d[n] = 0;
}
static void wkf_cat(char *d, uint32_t max, const char *s) { uint32_t n = wu_strlen(d); while (*s && n + 1 < max) d[n++] = *s++; d[n] = 0; }
/* En el punto seguro, ya en contexto de hilo normal: la pagina averiada se abandona y se abre una nueva con el aviso. */
static void web_fault_after(void)
{
    s_wk_faults++;
    s_wkf_text[0] = 0;
    wkf_cat(s_wkf_text, sizeof s_wkf_text, s_wkf_vec == 14 ? "acceso a memoria no valido" : s_wkf_vec == 13 ? "fallo de proteccion general" : s_wkf_vec == 6 ? "instruccion no valida" : s_wkf_vec == 0 ? "division por cero" : "desbordamiento de pila detectado");
    wkf_cat(s_wkf_text, sizeof s_wkf_text, " en "); wkf_hex(s_wkf_text, sizeof s_wkf_text, s_wkf_rip);
    if (s_wkf_vec == 14) { wkf_cat(s_wkf_text, sizeof s_wkf_text, ", direccion "); wkf_hex(s_wkf_text, sizeof s_wkf_text, s_wkf_addr); }
    kprint("\n[WEB] El motor del navegador ha fallado ("); kprint(s_wkf_text); kprint("). Pagina: "); kprint(s_pg ? s_pg->url : "?");
    kprint("\n[WEB] Se cierra la pagina y el sistema sigue (recuperacion "); kprint_dec(s_wk_faults); kprint(" de "); kprint_dec(WEB_FAULT_MAX); kprint(" en este arranque).\n");
    s_job_busy = 0; s_move_pending = 0; s152_tgt = -1; s_dd_hl = -1; s_hover_href[0] = 0;
    struct wpage *old = s_pg, *np = wp_new();
    if (np) {
        if (old) { memcpy(np->hist, old->hist, sizeof np->hist); np->hn = old->hn; np->hpos = old->hpos; }   /* el historial se conserva; la pagina vieja no se libera */
        s_pg = np;
    }
    s_wk_recovering = 0;
    if (s_pg) wp_navigate(s_pg, "about:fallo", 0);
    s_last_render = web_now_ms();
}
#endif

static void worker_run(void);
static void worker_loop(void *arg)
{
    (void)arg;
#if defined(__x86_64__)
    for (;;) {
        if (__builtin_setjmp(s_wk_jb)) web_fault_after();
        s_wk_armed = 1;
        worker_run();
    }
#else
    worker_run();
#endif
}
static void worker_run(void)
{
    for (;;) {
        struct job j; int worked = 0;
        while (q_pop(&j)) { run_job(&j); if (j.s) kfree(j.s); worked = 1; }
        uint64_t now = web_now_ms(); int next = 250;      /* ventana no visible: casi dormido (el SO en reposo no gasta CPU) */
        if (s_pg && now - s_last_render < 4000) {
            if (s152_hover_deferred && s152_tgt < 0 && now - s152_scroll_ms >= 150) { s152_hover_deferred = 0; s_move_pending = 1; }   /* SOMA_152 */
            if (s_move_pending) { s_move_pending = 0; if (s_pg->lay) wp_mouse_move(s_pg, s_move_x + s_pg->sx, s_move_y + s_pg->sy); }
            next = wp_tick(s_pg, now);
            if (next < 2) next = 2;
            if (next > 40) next = 40;
        }
        if (!worked) thread_sleep((uint32_t)next); else thread_yield();
    }
}
static void worker_entry(void *arg)
{
    s_wk_tcb = thread_current();
    char *stk = (char *)kmalloc(WK_STACK);
    if (!stk) { worker_loop(arg); return; }
    s_wk_lo = stk + 4096; s_wk_hi = stk + WK_STACK;
    web_call_on_stack((char *)(((uintptr_t)stk + WK_STACK) & ~(uintptr_t)15), worker_loop, arg);
}
static int s_need_home;
static void start_worker(void)
{
    if (s_worker_up || !s_inited) return;
    s_last_render = web_now_ms();
    { struct tcb *wt = thread_create("web_worker", worker_entry, 0); if (wt) { thread_set_owner(wt, ACCT_WEB); s_worker_up = 1; } }   /* PARCHE 089 */
}
/* Al arrancar la GUI solo se reserva el estado minimo: ni hilo, ni fuente, ni pagina hasta que se abre la ventana. */
static void ensure_init(void)
{
    if (s_inited) return;
    s_pg = wp_new();
    s_tabs[0] = tab_new(); s_ntabs = 1; s_cur = 0;
    if (!s_pg || !s_tabs[0]) return;
    s_inited = 1; s_need_home = 1;
    wu_copy(s_url, HOME_URL, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len;
}

/* ------------------------------------------------------------ dibujo */
/* SOMA_PARCHE_028_UI: interfaz renovada. La ventana ya tiene barra de titulo y marco propios del gestor de ventanas,
 * asi que aqui solo quedan la tira de pestanas, la barra de navegacion, la pagina y una linea de estado. */
#define TB_H 0
#define TAB_H 34
#define NAV_H 42
#define STAT_H 22
#define SB_W 12
#define NAVB 32            /* lado de los botones de navegacion */
#define NAVSTEP 36
#define C_BG 0x000F131C
#define C_PANEL 0x001A2030
#define C_LINE 0x002A3446
#define C_TXT 0x00F1F5F9
#define C_DIM 0x008A97AD
#define C_AMB 0x00F59E0B
#define C_GRN 0x0010B981
#define C_RED 0x00EF4444
#define C_CYA 0x0038BDF8
#define C_FIELD 0x000B0E14
#define C_HOVER 0x002A3349

static int s_font;
static int s_hover_ui = -1;        /* elemento de la interfaz bajo el puntero: 0..3 botones, 4 ir, 10+i pestana, 30+i cierre de pestana, 50 nueva pestana */
struct geo { int vx, vy, vw, vh, tab_y, nav_y, stat_y, ub_x, ub_w; };

static void geo_of(struct geo *g, int wx, int wy, int ww, int wh)
{
    g->tab_y = wy + TB_H; g->nav_y = g->tab_y + TAB_H; g->vx = wx; g->vy = g->nav_y + NAV_H + 2;
    g->vw = ww; g->stat_y = wy + wh - STAT_H; g->vh = g->stat_y - g->vy; if (g->vh < 32) g->vh = 32;
    g->ub_x = wx + 10 + 4 * NAVSTEP + 2; g->ub_w = (wx + ww - 10) - g->ub_x; if (g->ub_w < 60) g->ub_w = 60;
}
static int tab_width(int ww) { int t = (ww - 20 - 34) / (s_ntabs ? s_ntabs : 1); return t > 210 ? 210 : (t < 40 ? 40 : t); }
static int in_r(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }
static int tx_w(const char *s, int n, int px, int fl)
{
    if (s_font) return wf_width(s, (uint32_t)n, px, fl);
    return n * 8;
}
static void tx(int x, int y, const char *s, int n, int px, uint32_t col, int fl)
{
    if (n <= 0) return;
    if (s_font) { wf_draw(x, y + wf_ascent(px), s, (uint32_t)n, px, col, fl, 255); return; }
    char b[160]; int k = n < 159 ? n : 159; memcpy(b, s, (uint32_t)k); b[k] = 0; font_draw_string((uint32_t)x, (uint32_t)y, b, col, 1);
}
/* recorta texto a un ancho maximo (por bytes UTF-8 completos) y devuelve los bytes a dibujar */
static int fit(const char *s, int maxw, int px, int fl)
{
    int n = (int)wu_strlen(s);
    if (tx_w(s, n, px, fl) <= maxw) return n;
    while (n > 0) { n--; while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; if (tx_w(s, n, px, fl) <= maxw) break; }
    return n;
}
static void button(int x, int y, int w, int h, const char *lab, uint32_t edge, int on)
{
    fb_fill_rounded_rect(x, y, w, h, 7, on ? 0x000B6BCB : C_PANEL);
    int n = (int)wu_strlen(lab), lw = tx_w(lab, n, 13, 0);
    tx(x + (w - lw) / 2, y + (h - 16) / 2, lab, n, 13, on ? 0x00FFFFFF : 0x00475569, 0);
    (void)edge;
}
/* ---- iconos de la barra (trazos suavizados; coordenadas en 1/16 px) ---- */
#define LN(x0, y0, x1, y1) fb_line_aa((x0), (y0), (x1), (y1), 28, col)
static void ico_back(int cx, int cy, uint32_t col, int fwd)
{
    int d = fwd ? -1 : 1, X = cx * 16, Y = cy * 16;
    LN(X - d * 72, Y, X + d * 88, Y); LN(X - d * 72, Y, X + d * 8, Y - 80); LN(X - d * 72, Y, X + d * 8, Y + 80);
}
static void ico_reload(int cx, int cy, uint32_t col)
{
    /* arco de 300 grados (tabla de seno/coseno x100) y punta de flecha */
    static const signed char cs[13][2] = { {50,-87}, {87,-50}, {100,0}, {87,50}, {50,87}, {0,100}, {-50,87}, {-87,50}, {-100,0}, {-87,-50}, {-50,-87}, {0,-100}, {26,-97} };
    int X = cx * 16, Y = cy * 16, r = 104;
    for (int i = 1; i + 1 < 12; i++) LN(X + cs[i][0] * r / 100, Y + cs[i][1] * r / 100, X + cs[i + 1][0] * r / 100, Y + cs[i + 1][1] * r / 100);
    LN(X + 56, Y - r, X - 8, Y - r - 58); LN(X + 56, Y - r, X - 8, Y - r + 58);
}
static void ico_stop(int cx, int cy, uint32_t col) { int X = cx * 16, Y = cy * 16; LN(X - 80, Y - 80, X + 80, Y + 80); LN(X - 80, Y + 80, X + 80, Y - 80); }
static void ico_home(int cx, int cy, uint32_t col)
{
    int X = cx * 16, Y = cy * 16;
    LN(X - 128, Y - 8, X, Y - 128); LN(X, Y - 128, X + 128, Y - 8);
    LN(X - 92, Y - 30, X - 92, Y + 112); LN(X - 92, Y + 112, X + 92, Y + 112); LN(X + 92, Y + 112, X + 92, Y - 30);
}
static void ico_plus(int cx, int cy, uint32_t col) { int X = cx * 16, Y = cy * 16; LN(X - 80, Y, X + 80, Y); LN(X, Y - 80, X, Y + 80); }
static void ico_lock(int x, int cy, uint32_t col)
{
    fb_fill_rrect_aa(x, cy - 2, 12, 9, 2, col);
    int X = (x + 6) * 16, Y = (cy - 2) * 16;
    fb_line_aa(X - 56, Y, X - 56, Y - 56, 24, col); fb_line_aa(X + 56, Y, X + 56, Y - 56, 24, col); fb_line_aa(X - 56, Y - 56, X - 24, Y - 96, 24, col);
    fb_line_aa(X - 24, Y - 96, X + 24, Y - 96, 24, col); fb_line_aa(X + 24, Y - 96, X + 56, Y - 56, 24, col);
}
static void ico_globe(int x, int cy, uint32_t col)
{
    static const signed char cs[12][2] = { {100,0}, {87,50}, {50,87}, {0,100}, {-50,87}, {-87,50}, {-100,0}, {-87,-50}, {-50,-87}, {0,-100}, {50,-87}, {87,-50} };
    int X = (x + 6) * 16, Y = cy * 16, r = 96;
    for (int i = 0; i < 12; i++) { int j = (i + 1) % 12; fb_line_aa(X + cs[i][0] * r / 100, Y + cs[i][1] * r / 100, X + cs[j][0] * r / 100, Y + cs[j][1] * r / 100, 20, col); }
    fb_line_aa(X - r, Y, X + r, Y, 18, col); fb_line_aa(X, Y - r, X, Y + r, 18, col);
}
#undef LN
static void nav_button(int i, int x, int y, int enabled)
{
    if (enabled && s_hover_ui == i) fb_fill_rounded_rect(x, y, NAVB, NAVB, NAVB / 2, C_HOVER);
}
static void set_cursor(int c) { if (c != s_cursor_cur) { s_cursor_cur = c; mouse_set_cursor(c); } }

static void page_title(char *o, int max)
{
    struct wpage *p = s_pg;
    if (p->title[0]) wu_copy(o, p->title, (uint32_t)max); else if (p->url[0]) wu_copy(o, p->url, (uint32_t)max); else wu_copy(o, "Nueva pesta\xc3\xb1" "a", (uint32_t)max);
}

static int sb_geom(struct geo *g, int *th, int *tr);
static void draw_scrollbar(struct geo *g)
{
    struct wpage *p = s_pg; int x = g->vx + g->vw - SB_W, y = g->vy, h = g->vh;
    fb_fill_rect((uint32_t)x, (uint32_t)y, SB_W, (uint32_t)h, 0x00F1F3F5);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, 0x00E2E5EA);
    int th, tr, range = sb_geom(g, &th, &tr);
    if (range > 0) {
        int off = (int)((int64_t)(tr - th) * p->sy / range);
        fb_fill_rrect_aa(x + 3, y + 2 + off, SB_W - 5, th, 3, s_sb_drag ? 0xFF64748Bu : 0xFFB4BCC8u);
    }
}

static void draw_dropdown(struct geo *g)
{
    struct wpage *p = s_pg; struct wnode *sel = p->dd_sel; if (!sel || !p->lay) return;
    int x, y, w, h; if (!wl_node_rect(p->lay, sel, &x, &y, &w, &h)) return;
    int cnt = wl_select_count(sel), cur = wl_select_index(sel); if (cnt <= 0) return;
    int vis = cnt < 14 ? cnt : 14, first = cur - 6; if (first > cnt - vis) first = cnt - vis; if (first < 0) first = 0;
    int sx0 = g->vx + x - p->sx, sy0 = g->vy + y - p->sy + h, ww = w < 120 ? 120 : w;
    if (sx0 + ww > g->vx + g->vw - SB_W) { sx0 = g->vx + g->vw - SB_W - ww; }
    if (sx0 < g->vx) { sx0 = g->vx; }
    if (sy0 + vis * 20 > g->vy + g->vh) { sy0 = g->vy + g->vh - vis * 20; }
    if (sy0 < g->vy) { sy0 = g->vy; }
    fb_fill_rect((uint32_t)sx0, (uint32_t)sy0, (uint32_t)ww, (uint32_t)(vis * 20), 0x00FFFFFF);
    fb_draw_rect((uint32_t)sx0, (uint32_t)sy0, (uint32_t)ww, (uint32_t)(vis * 20), 0x00475569);
    for (int i = 0; i < vis; i++) {
        int idx = first + i, hl = (s_dd_hl >= 0 ? s_dd_hl : cur) == idx;
        if (hl) fb_fill_rect((uint32_t)(sx0 + 1), (uint32_t)(sy0 + i * 20 + 1), (uint32_t)(ww - 2), 18, 0x003B82F6);
        struct wnode *o = wl_select_option(sel, idx); struct wb b = {0, 0, 0, 0};
        if (o) wd_text_content(o, &b);
        if (b.p) { int n = fit(b.p, ww - 12, 13, 0); tx(sx0 + 6, sy0 + i * 20 + 2, b.p, n, 13, hl ? 0x00FFFFFF : 0x00111827, 0); }
        wb_free(&b);
    }
}

struct paint_args { struct wpage *p; struct geo *g; };
static void paint_thunk(void *v)
{
    struct paint_args *a = (struct paint_args *)v;
    wp_paint(a->p, a->g->vx, a->g->vy, a->g->vw - SB_W, a->g->vh);
    draw_dropdown(a->g);
}

/* Instantanea de la zona de pagina: mientras el hilo trabajador esta maquetando (p->lay_busy) no se toca el arbol de cajas;
 * se repinta la ultima imagen completa y la GUI sigue fluida. */
static uint32_t *s_snap; static int s_snap_w, s_snap_h, s_snap_cap;
/* SOMA_152_WEB_FLUIDO: la instantanea es un ANILLO de filas. La fila i de la vista vive en la fila (s_snap_r0 + i) % h del
 * buffer: desplazar la pagina es mover s_snap_r0 y copiar solo la franja nueva, en vez de volver a copiar la vista entera
 * (antes: rellenar de blanco + copiar la instantanea + releer TODA la vista del backbuffer en cada paso de rueda). */
static int s_snap_r0;
static inline void copy_px152(uint32_t *d, const volatile uint32_t *s, int n)
{
    if (n <= 0) return;
#if defined(__x86_64__)
    if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0 && n >= 2) {
        size_t q = (size_t)n / 2; uint32_t *dd = d; const volatile uint32_t *ss = s;
        __asm__ volatile ("cld\n\trep movsq" : "+D"(dd), "+S"(ss), "+c"(q) : : "memory");
        if (n & 1) d[n - 1] = s[n - 1];
        return;
    }
#endif
    int i = 0;
    for (; i + 4 <= n; i += 4) { uint32_t a = s[i], b = s[i + 1], c = s[i + 2], e = s[i + 3]; d[i] = a; d[i + 1] = b; d[i + 2] = c; d[i + 3] = e; }
    for (; i < n; i++) d[i] = s[i];
}
static int snap_alloc(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (w * h > s_snap_cap) {
        if (s_snap) kfree(s_snap);
        s_snap = (uint32_t *)kmalloc((uint32_t)w * (uint32_t)h * 4u); s_snap_cap = s_snap ? w * h : 0; s_snap_w = s_snap_h = 0;
        if (!s_snap) return 0;
    }
    return 1;
}
/* copia filas de la pantalla (vista) [row, row+n) a sus filas del anillo */
static void snap_store_rows(int x, int y, int row, int n)
{
    int fw = (int)fb_get_width(); volatile uint32_t *fbp = fb_get_buffer(); int w = s_snap_w, h = s_snap_h;
    for (int j = 0; j < n; j++) {
        int r = soma152_ring_row(s_snap_r0, row + j, h);
        copy_px152(s_snap + (size_t)r * (size_t)w, fbp + (size_t)(y + row + j) * (size_t)fw + (size_t)x, w);
    }
}
static void snap_take(int x, int y, int w, int h)
{
    int fw = (int)fb_get_width(), fh = (int)fb_get_height();
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > fw || y + h > fh) return;
    if (!snap_alloc(w, h)) return;
    s_snap_w = w; s_snap_h = h; s_snap_r0 = 0;
    snap_store_rows(x, y, 0, h);
}
/* vuelca filas de la vista [row, row+n) desde el anillo (en a lo sumo dos bloques contiguos) */
static void snap_show_rows(int x, int y, int row, int n)
{
    int w = s_snap_w, h = s_snap_h;
    while (n > 0) {
        int r = soma152_ring_row(s_snap_r0, row, h), run = h - r; if (run > n) run = n;
        fb_blit(x, y + row, s_snap + (size_t)r * (size_t)w, w, run, w);
        row += run; n -= run;
    }
}
static void snap_show(int x, int y, int w, int h)
{
    if (s_snap && s_snap_w == w && s_snap_h == h) snap_show_rows(x, y, 0, h);
    else fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x00FFFFFF);
}
/* SOMA_PARCHE_028_UI: huella de todo lo que cambia el aspecto de la pagina. Si no cambia, el fotograma reutiliza la
 * imagen anterior (una copia de memoria) en vez de volver a pintar el arbol de cajas y reescalar cada foto. */
static uint64_t s_snap_key, s_snap_noscroll_key;
static int s_snap_ok, s_snap_sy, s_snap_x, s_snap_y;
/* Cache del criterio conservador: inspeccion de posicion fija/sticky solo al cambiar el layout. */
static struct wlay *s_149_checked_lay;
static uint32_t s_149_checked_gen;
static int s_149_safe;
static int scroll_fast_safe(struct wpage *p)
{
    /* SOMA_152: need_style/styles_pending/img_dirty ya no impiden desplazar la instantanea: la clave sin desplazamiento
     * (gen, maquetacion, imagenes cargadas...) garantiza que la instantanea y la franja nueva salen de la MISMA maquetacion. */
    if (!p->lay || !p->doc || !p->doc->root || p->lay_busy || p->dd_sel || p->alert_pending) return 0;
    if (s_149_checked_lay == p->lay && s_149_checked_gen == p->gen)
        return s_149_safe;
    s_149_checked_lay = p->lay; s_149_checked_gen = p->gen; s_149_safe = 1;
    for (struct wnode *n = p->doc->root; n;) {
        if (n->st && (n->st->position == P_FIXED || n->st->position == P_STICKY)) {
            s_149_safe = 0; break;
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    return s_149_safe;
}
static uint64_t page_key(struct wpage *p, int w, int h, int include_sy)
{
    uint64_t k = 1469598103934665603ull;
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    MIX(p->gen); MIX((uintptr_t)p->lay); MIX(p->sx); MIX(include_sy ? p->sy : 0); MIX(w); MIX(h); MIX(p->caret_on); MIX((uintptr_t)p->focus);
    MIX((uintptr_t)p->dd_sel);   /* SOMA_152: p->hover no cambia el dibujo (solo via :hover -> reestilo -> gen) */ MIX(s_dd_hl); MIX(p->img_ok); MIX(p->img_fail); MIX(p->nimg); MIX(p->anim_tick);   /* SOMA_180: imagenes animadas */ MIX(wl_inner_gen(p->lay));   /* SOMA_181 */
    if (p->focus && !s_r3) { MIX((uintptr_t)p->focus->value); MIX(p->focus->sel); MIX(p->focus->flags); }
    #undef MIX
    return k;
}

/* ------------------------------------------------------------ PARCHE 085: menu del boton derecho */
enum { CM_SAVE_IMG = 0, CM_COPY_IMG, CM_COPY_URL, CM_BACK, CM_FWD, CM_RELOAD, CM_COPY_LINK };
static int s_cm_open, s_cm_x, s_cm_y, s_cm_n, s_cm_hl = -1; static int s_cm_items[8];
static char s_cm_img[512], s_cm_link[512];
static const char *cm_label(int id)
{
    switch (id) {
    case CM_SAVE_IMG: return "Guardar imagen como...";
    case CM_COPY_IMG: return "Copiar imagen";
    case CM_COPY_URL: return "Copiar direccion de la imagen";
    case CM_COPY_LINK: return "Copiar direccion del enlace";
    case CM_BACK: return "Atras";
    case CM_FWD: return "Adelante";
    default: return "Recargar";
    }
}
#define CM_W 250
#define CM_IH 30
static void cm_geom(int *x, int *y, int *h, int wx, int wy, int ww, int wh)
{
    *h = s_cm_n * CM_IH + 12; *x = s_cm_x; *y = s_cm_y;
    if (*x + CM_W > wx + ww - 4) *x = wx + ww - 4 - CM_W;
    if (*y + *h > wy + wh - 4) *y = wy + wh - 4 - *h;
    if (*x < wx) *x = wx;
    if (*y < wy) *y = wy;
}
static void cm_render(int wx, int wy, int ww, int wh)
{
    if (!s_cm_open) return;
    int x, y, h; cm_geom(&x, &y, &h, wx, wy, ww, wh);
    fb_draw_shadow(x, y, CM_W, h, 10, 18, 6, 0xA0000000u);
    fb_fill_rrect_aa(x, y, CM_W, h, 10, 0xFF161D2Bu);
    fb_stroke_rrect_aa(x, y, CM_W, h, 10, 0xFF2A3446u);
    fb_tr_exclude(x - 2, y - 2, CM_W + 4, h + 4);
    for (int i = 0; i < s_cm_n; i++) {
        int iy = y + 6 + i * CM_IH, id = s_cm_items[i];
        int dis = (id == CM_BACK && !wp_can_back(s_pg)) || (id == CM_FWD && !wp_can_fwd(s_pg));
        if (i == s_cm_hl && !dis) fb_fill_rounded_rect(x + 6, iy, CM_W - 12, CM_IH, 7, 0x00243352);
        if (i > 0 && (id == CM_BACK || id == CM_COPY_LINK) && s_cm_items[i - 1] < CM_BACK) fb_fill_rect((uint32_t)(x + 12), (uint32_t)(iy - 1), CM_W - 24, 1, 0x002A3446);
        /* iconitos */
        int ix = x + 22, icy = iy + CM_IH / 2; uint32_t ic = dis ? 0xFF475569u : 0xFFAEB8C8u;
        if (id == CM_SAVE_IMG) { fb_stroke_rrect_aa(ix - 7, icy - 7, 14, 14, 2, ic); fb_fill_rect((uint32_t)(ix - 3), (uint32_t)(icy - 7), 6, 4, ic & 0xFFFFFF); }
        else if (id == CM_COPY_IMG || id == CM_COPY_URL || id == CM_COPY_LINK) { fb_stroke_rrect_aa(ix - 6, icy - 4, 10, 11, 2, ic); fb_stroke_rrect_aa(ix - 3, icy - 7, 10, 11, 2, ic); }
        else if (id == CM_BACK || id == CM_FWD) { int sg = id == CM_BACK ? -1 : 1; fb_line_aa((ix - sg * 5) * 16, icy * 16, (ix + sg * 5) * 16, icy * 16, 22, ic); fb_line_aa((ix + sg * 5) * 16, icy * 16, (ix + sg * 1) * 16, (icy - 4) * 16, 22, ic); fb_line_aa((ix + sg * 5) * 16, icy * 16, (ix + sg * 1) * 16, (icy + 4) * 16, 22, ic); }
        else { fb_stroke_rrect_aa(ix - 6, icy - 6, 12, 12, 6, ic); }
        tx(x + 40, iy + 7, cm_label(id), (int)wu_strlen(cm_label(id)), 13, dis ? 0x00475569 : C_TXT, 0);
    }
}
struct rclick_args { struct wpage *p; int px, py; struct wnode *node, *link; };
static void rclick_thunk(void *v) { struct rclick_args *a = (struct rclick_args *)v; a->link = 0; if (s_r3) { a->node = r3_right_hit(a->p, a->px, a->py, &a->link); return; } /* SOMA_203 */ a->node = wl_hit(a->p->lay, a->px, a->py, &a->link, 0); }
/* lo llama gui.c con cada pulsacion del boton derecho dentro de la ventana */
int browser_right_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    if (!s_inited || !s_pg) return 0;
    if (savedlg_active()) return 0;
    struct wpage *p = s_pg; struct geo g; geo_of(&g, (int)win_x, (int)win_y, (int)win_w, (int)win_h);
    s_cm_n = 0; s_cm_img[0] = 0; s_cm_link[0] = 0; s_cm_hl = -1;
    if (in_r(mx, my, g.vx, g.vy, g.vw - SB_W, g.vh) && p->lay && !p->lay_busy) {
        struct rclick_args ra = { p, mx - g.vx + p->sx, my - g.vy + p->sy, 0, 0 }; on_gui_stack(rclick_thunk, &ra);
        const char *src = ra.node ? wl_img_src(ra.node) : 0;
        if (src && src[0]) {
            const uint32_t *px; int w, h;
            int ok = wp_img_get(p, src, &px, &w, &h, s_cm_img, sizeof s_cm_img);
            if (ok) { s_cm_items[s_cm_n++] = CM_SAVE_IMG; s_cm_items[s_cm_n++] = CM_COPY_IMG; }
            s_cm_items[s_cm_n++] = CM_COPY_URL;
        }
        const char *h = ra.link ? wd_attr(ra.link, "href") : 0;
        if (h && h[0]) { wu_copy(s_cm_link, h, sizeof s_cm_link); s_cm_items[s_cm_n++] = CM_COPY_LINK; }
    }
    s_cm_items[s_cm_n++] = CM_BACK; s_cm_items[s_cm_n++] = CM_FWD; s_cm_items[s_cm_n++] = CM_RELOAD;
    s_cm_open = 1; s_cm_x = mx + 2; s_cm_y = my + 2;
    return 1;
}
static int cm_click(int32_t mx, int32_t my, int wx, int wy, int ww, int wh)
{
    if (!s_cm_open) return 0;
    int x, y, h; cm_geom(&x, &y, &h, wx, wy, ww, wh);
    s_cm_open = 0;
    if (!in_r(mx, my, x, y, CM_W, h)) return 1;                           /* fuera: solo cierra */
    int i = (my - y - 6) / CM_IH; if (i < 0 || i >= s_cm_n) return 1;
    int id = s_cm_items[i];
    const uint32_t *px; int w, hh;
    switch (id) {
    case CM_SAVE_IMG: if (wp_img_get(s_pg, s_cm_img, &px, &w, &hh, 0, 0)) savedlg_open(px, w, hh, s_cm_img); else clip_toast("La imagen ya no esta en memoria"); break;
    case CM_COPY_IMG: if (wp_img_get(s_pg, s_cm_img, &px, &w, &hh, 0, 0) && clip_set_image(px, w, hh) >= 0) clip_toast("Imagen copiada"); break;
    case CM_COPY_URL: clip_set_text(s_cm_img, 0); clip_toast("Direccion de la imagen copiada"); break;
    case CM_COPY_LINK: clip_set_text(s_cm_link, 0); clip_toast("Direccion del enlace copiada"); break;
    case CM_BACK: if (wp_can_back(s_pg)) q_push(J_BACK, 0, 0, 0, 0); break;
    case CM_FWD: if (wp_can_fwd(s_pg)) q_push(J_FWD, 0, 0, 0, 0); break;
    default: q_push(J_RELOAD, 0, 0, 0, 0); break;
    }
    if (s_r3) web_r3_image_free();                                         /* SOMA_203: los pixeles ya estan copiados donde hacian falta */
    return 1;
}

/* SOMA_152_WEB_FLUIDO: desplazamiento suave. La rueda fija un objetivo; cada fotograma avanza una fraccion de la distancia
 * que queda (curva exponencial, ~100 ms por muesca), sin depender de la frecuencia de fotogramas. Si otra cosa mueve la
 * pagina (teclado, ancla, barra), la animacion se cancela. Mientras se desplaza, el hover se aplaza (como Chrome). */
static int max_sy152(struct wpage *p) { int m = wp_doc_height(p) - p->vh; return m > 0 ? m : 0; }
static void smooth_wheel152(struct wpage *p, int dy)
{
    uint64_t now = web_now_ms();
    s152_scroll_ms = now;
#if SOMA_WEB_SMOOTH_SCROLL
    int base = (s152_tgt >= 0 && p->sy == s152_anim_sy) ? s152_tgt : p->sy;
    int t = soma152_clamp_target(base, dy, max_sy152(p));
    if (t == p->sy) { s152_tgt = -1; return; }
    if (s152_tgt < 0 || p->sy != s152_anim_sy) { s152_anim_ms = now; s152_anim_sy = p->sy; }
    s152_tgt = t;
#else
    wp_scroll_by(p, 0, dy);
#endif
}
static void smooth_step152(struct wpage *p)
{
    int rq = __atomic_exchange_n(&p->scroll_req, 0, __ATOMIC_ACQUIRE);   /* teclado (hilo trabajador) */
    if (rq) { if (rq > (1 << 23)) rq = 1 << 23; if (rq < -(1 << 23)) rq = -(1 << 23); smooth_wheel152(p, rq); }
    if (s152_tgt < 0) return;
    if (p->sy != s152_anim_sy) { s152_tgt = -1; return; }           /* alguien mas movio la pagina */
    uint64_t now = web_now_ms(), dt = now - s152_anim_ms;
    int m = max_sy152(p); if (s152_tgt > m) s152_tgt = m;
    int d = s152_tgt - p->sy;
    if (d == 0) { s152_tgt = -1; return; }
    if (dt == 0) return;
    int step = soma152_smooth_step(d, (uint32_t)(dt > 0xFFFFFFFFull ? 0xFFFFFFFFu : dt));
    p->sy += step; s152_anim_sy = p->sy; s152_anim_ms = now; s152_scroll_ms = now;
    if (p->sy == s152_tgt) s152_tgt = -1;
}

/* SOMA_152_WEB_FLUIDO: huella de TODO lo que dibuja browser_render_window (pagina + pestanas, barra, estado, menus).
 * El compositor (gui.c) la consulta en cada vuelta: si no cambio, no se repinta ni se vuelca nada a la pantalla.
 * Si cambio, se repinta solo la ventana del navegador, sin recomponer el escritorio entero. */
static uint64_t s152_drawn_key;
static volatile uint64_t s152_input_ms;
static uint64_t mix_str152(uint64_t k, const char *s)
{
    if (!s) return k ^ 0x9E3779B97F4A7C15ull;
    for (int i = 0; s[i] && i < 600; i++) { k ^= (uint8_t)s[i]; k *= 1099511628211ull; }
    k ^= 0xFF; k *= 1099511628211ull;
    return k;
}
static uint64_t browser152_key(int ww, int wh)
{
    struct wpage *p = s_pg; struct geo g; geo_of(&g, 0, 0, ww, wh);
    uint64_t now = web_now_ms();
    uint64_t k = p->lay_busy ? 0x5A5Aull : page_key(p, g.vw - SB_W, g.vh, 1);
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    int busy = s_job_busy || p->phase != WP_IDLE;
    MIX(ww); MIX(wh); MIX(p->lay_busy); MIX(busy); MIX(p->progress); MIX(p->status_kind); MIX(s_hover_ui);
    MIX(s_ntabs); MIX(s_cur); MIX(wp_can_back(p)); MIX(wp_can_fwd(p)); MIX(s_url_focus); MIX(s_url_all); MIX(s_url_len); MIX(s_url_cur);
    MIX(s_find_open); MIX(s_find_len); MIX(p->alert_pending); MIX(s_cm_open); MIX(s_cm_hl); MIX(s_cm_x); MIX(s_cm_y); MIX(s_sb_drag);
    MIX(wp_doc_height(p)); MIX(p->vh); MIX(s_dd_hl);
    if (busy) MIX(now / 90);                                   /* puntos animados de la linea de estado */
    if (s_url_focus || s_find_open) MIX(now / 500);            /* cursor parpadeante de los campos */
    k = mix_str152(k, s_url); k = mix_str152(k, p->status); k = mix_str152(k, s_hover_href); k = mix_str152(k, p->title); k = mix_str152(k, p->url);
    if (s_find_open) k = mix_str152(k, s_find);
    if (p->alert_pending) k = mix_str152(k, p->alert_msg);
    for (int i = 0; i < s_ntabs; i++) if (s_tabs[i] && i != s_cur) { k = mix_str152(k, s_tabs[i]->title); k = mix_str152(k, s_tabs[i]->url); }
    #undef MIX
    return k;
}
/* gui.c: hay que volver a dibujar la ventana del navegador? (dialogo de guardar: siempre, tiene su propia animacion) */
int browser_frame_dirty(uint32_t win_w, uint32_t win_h)
{
    if (!s_inited || !s_pg) return 0;
    s_last_render = web_now_ms();                           /* la ventana esta delante: el hilo trabajador sigue activo */
    r3_sync(s_pg);                                          /* SOMA_202: estado y fotogramas nuevos del proceso aislado */
    if (savedlg_active() || s152_tgt >= 0 || s_pg->scroll_req) return 1;
    return browser152_key((int)win_w, (int)win_h) != s152_drawn_key;
}
/* SOMA_190: la pagina trabaja en segundo plano (red, scripts, maquetacion) y el usuario no la toca desde hace 1,5 s: el escritorio
 * le deja casi toda la CPU (con un solo nucleo, cada fotograma de la animacion de carga se la quitaba al hilo que carga). */
int browser_loading(void)
{
    if (!s_inited || !s_pg) return 0;
    if (!(s_job_busy || s_pg->phase != WP_IDLE)) return 0;
    return web_now_ms() - s152_input_ms >= 1500;
}
/* gui.c: el usuario esta usando el navegador o la pagina esta cargando -> el escritorio de fondo se congela */
int browser_hot(void)
{
    if (!s_inited || !s_pg) return 0;
    if (s_job_busy || s_pg->phase != WP_IDLE || s_sb_drag || s152_tgt >= 0) return 1;
    return web_now_ms() - s152_input_ms < 1500;
}

void browser_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    ensure_init(); if (!s_pg) return;
    struct wpage *p = s_pg; int wx = (int)win_x, wy = (int)win_y, ww = (int)win_w, wh = (int)win_h;
    r3_sync(p);                                                             /* SOMA_202 */
    s_win_x = win_x; s_win_y = win_y; s_win_w = win_w; s_win_h = win_h;
    s_last_render = web_now_ms();
    start_worker();
    if (s_need_home) { s_need_home = 0; q_push(J_HOME, 0, 0, 0, 0); }
    if (!s_font) s_font = wf_init();
    struct geo g; geo_of(&g, wx, wy, ww, wh);
    wp_set_viewport(p, g.vw - SB_W, g.vh);
    smooth_step152(p);                                      /* SOMA_152_WEB_FLUIDO */
    s152_drawn_key = browser152_key(ww, wh);

    fb_reset_clip();
    fb_fill_rect(win_x, win_y, win_w, (uint32_t)(g.vy - wy), C_BG);

    /* pestanas */
    int tabw = tab_width(ww);
    fb_set_clip(wx, g.tab_y, ww, TAB_H);
    for (int i = 0; i < s_ntabs; i++) {
        int x = wx + 10 + i * tabw, cur = i == s_cur; char ti[96];
        if (cur) page_title(ti, sizeof ti); else wu_copy(ti, s_tabs[i]->title[0] ? s_tabs[i]->title : s_tabs[i]->url, sizeof ti);
        if (cur) { fb_fill_rounded_rect(x, g.tab_y + 6, tabw - 4, TAB_H + 4, 9, C_PANEL); }
        else if (s_hover_ui == 10 + i || s_hover_ui == 30 + i) fb_fill_rounded_rect(x, g.tab_y + 6, tabw - 4, TAB_H - 10, 8, 0x00161B28);
        else if (i + 1 != s_cur && i + 1 < s_ntabs) fb_fill_rect((uint32_t)(x + tabw - 3), (uint32_t)(g.tab_y + 12), 1, TAB_H - 20, C_LINE);
        int busy_tab = cur && (s_job_busy || p->phase != WP_IDLE);
        ico_globe(x + 9, g.tab_y + 6 + (TAB_H - 6) / 2, busy_tab ? (0xFF000000u | C_CYA) : (cur ? 0xFF8A97ADu : 0xFF55627Au));
        int n = fit(ti, tabw - 58, 12, 0); tx(x + 28, g.tab_y + 12, ti, n, 12, cur ? C_TXT : C_DIM, 0);
        int cxm = x + tabw - 18, cym = g.tab_y + 6 + (TAB_H - 6) / 2;
        if (s_hover_ui == 30 + i) fb_fill_rounded_rect(cxm - 9, cym - 9, 18, 18, 9, cur ? C_HOVER : 0x00232B3D);
        { uint32_t col = s_hover_ui == 30 + i ? 0xFFFFFFFFu : 0xFF7C8AA0u; fb_line_aa(cxm * 16 - 52, cym * 16 - 52, cxm * 16 + 52, cym * 16 + 52, 22, col); fb_line_aa(cxm * 16 - 52, cym * 16 + 52, cxm * 16 + 52, cym * 16 - 52, 22, col); }
    }
    if (s_ntabs < MAX_TABS) {
        int x = wx + 10 + s_ntabs * tabw + 2, cym = g.tab_y + 6 + (TAB_H - 6) / 2;
        if (s_hover_ui == 50) fb_fill_rounded_rect(x, cym - 12, 24, 24, 12, C_HOVER);
        ico_plus(x + 12, cym, s_hover_ui == 50 ? 0xFFFFFFFFu : 0xFF8A97ADu);
    }
    fb_reset_clip();

    /* navegacion */
    fb_fill_rect(win_x, (uint32_t)g.nav_y, win_w, NAV_H, C_PANEL);
    int nb = wx + 10, ny = g.nav_y + (NAV_H - NAVB) / 2;
    int busy = s_job_busy || p->phase != WP_IDLE;
    int canb = wp_can_back(p), canf = wp_can_fwd(p);
    nav_button(0, nb, ny, canb); ico_back(nb + NAVB / 2, ny + NAVB / 2, canb ? 0xFFE2E8F0u : 0xFF475569u, 0);
    nav_button(1, nb + NAVSTEP, ny, canf); ico_back(nb + NAVSTEP + NAVB / 2, ny + NAVB / 2, canf ? 0xFFE2E8F0u : 0xFF475569u, 1);
    nav_button(2, nb + 2 * NAVSTEP, ny, 1); if (busy) ico_stop(nb + 2 * NAVSTEP + NAVB / 2, ny + NAVB / 2, 0xFFE2E8F0u); else ico_reload(nb + 2 * NAVSTEP + NAVB / 2, ny + NAVB / 2 + 1, 0xFFE2E8F0u);
    nav_button(3, nb + 3 * NAVSTEP, ny, 1); ico_home(nb + 3 * NAVSTEP + NAVB / 2, ny + NAVB / 2, 0xFFE2E8F0u);
    if (!s_url_focus && !wu_streq(s_url, p->url) && p->url[0]) { wu_copy(s_url, p->url, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len; }
    fb_fill_rounded_rect(g.ub_x, ny, g.ub_w, NAVB, NAVB / 2, C_FIELD);
    fb_stroke_rrect_aa(g.ub_x, ny, g.ub_w, NAVB, NAVB / 2, s_url_focus ? (0xFF000000u | C_CYA) : 0xFF2A3446u);
    {
        int secure = wu_startsi(s_url, "https://");
        if (secure) ico_lock(g.ub_x + 14, ny + NAVB / 2 + 1, wn_https_unverified() ? 0xFFF59E0Bu : 0xFF10B981u);   /* SOMA_176A: ambar = cifrado pero certificado sin verificar */ else ico_globe(g.ub_x + 14, ny + NAVB / 2, 0xFF8A97ADu);
        int tx0 = g.ub_x + 36, inw = g.ub_w - 36 - 40, off = 0;
        fb_set_clip(tx0, ny + 2, inw, NAVB - 4);
        int cw = tx_w(s_url, s_url_cur, 14, 0);
        if (cw > inw - 4) off = cw - (inw - 4);
        if (s_url_focus && s_url_all && s_url_len) fb_fill_rounded_rect(tx0 - 2, ny + 6, (tx_w(s_url, s_url_len, 14, 0) > inw ? inw : tx_w(s_url, s_url_len, 14, 0)) + 4, NAVB - 12, 3, 0x00214E86);
        tx(tx0 - off, ny + 8, s_url, s_url_len, 14, C_TXT, 0);
        if (s_url_focus && ((web_now_ms() / 500) & 1)) fb_fill_rect((uint32_t)(tx0 + cw - off), (uint32_t)(ny + 7), 2, NAVB - 14, C_CYA);
        fb_reset_clip();
        /* boton "ir" dentro del campo */
        int gx = g.ub_x + g.ub_w - 30, gy = ny + 4;
        fb_fill_rounded_rect(gx, gy, 24, 24, 12, s_hover_ui == 4 ? 0x000E7FD6 : (s_url_focus ? 0x000B6BCB : 0x00222B3D));
        ico_back(gx + 12, gy + 12, 0xFFFFFFFFu, 1);
    }
    fb_fill_rect((uint32_t)g.vx, (uint32_t)(g.vy - 2), (uint32_t)g.vw, 2, 0x00141926);
    if (busy) { int pw = (g.vw * (p->progress < 5 ? 5 : p->progress > 100 ? 100 : p->progress)) / 100; fb_fill_rect((uint32_t)g.vx, (uint32_t)(g.vy - 2), (uint32_t)pw, 2, C_CYA); }

    /* contenido */
    fb_set_clip(g.vx, g.vy, g.vw - SB_W, g.vh);
#if SOMA_WEB_PERF_LOG
    uint64_t soma149_render_start = web_now_ms();
    int soma149_painted = 0;
#endif
    if (p->lay_busy) snap_show(g.vx, g.vy, g.vw - SB_W, g.vh);
    else {
        int pw = g.vw - SB_W, ph = g.vh;
        uint64_t key = page_key(p, pw, ph, 1);
        if (s_snap_ok && key == s_snap_key && s_snap && s_snap_w == pw && s_snap_h == ph) snap_show(g.vx, g.vy, pw, ph);
        else {
            int shifted = 0;
#if SOMA_WEB_FAST_SCROLL
            struct soma149_scroll_plan sp;
            if (s_snap_ok && s_snap && s_snap_w == pw && s_snap_h == ph &&
                s_snap_x == g.vx && s_snap_y == g.vy &&
                s_snap_noscroll_key == page_key(p, pw, ph, 0) &&
                soma149_make_scroll_plan(ph, s_snap_sy, p->sy, &sp) &&
                scroll_fast_safe(p)) {
                /* SOMA_152: girar el anillo; las filas que siguen visibles salen de el, solo la franja nueva se pinta */
                int d = sp.src_row - sp.dst_row;                       /* = sy nuevo - sy de la instantanea */
                s_snap_r0 = soma152_ring_rotate(s_snap_r0, d, ph);
                snap_show_rows(g.vx, g.vy, sp.dst_row, sp.copy_rows);
                fb_set_clip(g.vx, g.vy + sp.damage_row, pw, sp.damage_rows);
                struct paint_args pa = { p, &g }; on_gui_stack(paint_thunk, &pa);
                fb_set_clip(g.vx, g.vy, pw, ph);
                snap_store_rows(g.vx, g.vy, sp.damage_row, sp.damage_rows);
                shifted = 1;
            }
#endif
            if (!shifted) {
                /* wl_paint ya rellena toda la vista con el fondo del lienzo: el relleno blanco solo hace falta sin maquetacion */
                if (!p->lay || !p->lay->root) fb_fill_rect((uint32_t)g.vx, (uint32_t)g.vy, (uint32_t)pw, (uint32_t)ph, 0x00FFFFFF);
                struct paint_args pa = { p, &g }; on_gui_stack(paint_thunk, &pa);
                s_snap_w = 0; snap_take(g.vx, g.vy, pw, ph);
            }
#if SOMA_WEB_PERF_LOG
            soma149_painted = 1;
#endif
            s_snap_ok = (s_snap_w == pw && s_snap_h == ph);
            s_snap_key = key; s_snap_noscroll_key = page_key(p, pw, ph, 0);
            s_snap_sy = p->sy; s_snap_x = g.vx; s_snap_y = g.vy;
        }
    }
    fb_reset_clip();
#if SOMA_WEB_PERF_LOG
    if (soma149_painted) {
        uint64_t ms = web_now_ms() - soma149_render_start;
        static uint64_t soma149_last_warning;
        if (ms >= 30 && web_now_ms() - soma149_last_warning >= 5000) {
            char n[32];
            soma149_last_warning = web_now_ms();
            kprint("[web149] paint="); wu_itoa((int)ms, n); kprint(n);
            kprint("ms layout="); wu_itoa((int)p->layout_dur, n); kprint(n);
            kprint("ms pending_images="); wu_itoa(p->img_pending, n); kprint(n);
            kprint("\n");
        }
    }
#endif
    draw_scrollbar(&g);

    if (s_find_open) {
        int fw = 260, fx = g.vx + g.vw - SB_W - fw - 6, fy = g.vy + 6;
        fb_draw_shadow(fx, fy, fw, 28, 8, 12, 4, 0x70000000u);
        fb_fill_rounded_rect(fx, fy, fw, 28, 8, C_PANEL); fb_stroke_rrect_aa(fx, fy, fw, 28, 8, 0xFF000000u | C_CYA);
        tx(fx + 8, fy + 6, "Buscar:", 7, 13, C_DIM, 0); tx(fx + 66, fy + 6, s_find, s_find_len, 13, C_TXT, 0);
        int cx = fx + 66 + tx_w(s_find, s_find_len, 13, 0); if ((web_now_ms() / 500) & 1) fb_fill_rect((uint32_t)cx, (uint32_t)(fy + 5), 2, 17, C_AMB);
    }
    if (p->alert_pending) {
        int aw = ww > 460 ? 420 : ww - 40, ax = wx + (ww - aw) / 2, ay = wy + wh / 2 - 50;
        fb_fill_rect_alpha(g.vx, g.vy, g.vw, g.vh, 0x60000000u);
        fb_draw_shadow(ax, ay, aw, 100, 10, 22, 8, 0xA0000000u);
        fb_fill_rounded_rect(ax, ay, aw, 100, 10, C_PANEL); fb_stroke_rrect_aa(ax, ay, aw, 100, 10, 0xFF3A465Cu);
        tx(ax + 12, ay + 8, "Esta p\xc3\xa1gina dice", 17, 13, C_DIM, WF_BOLD);
        int n = fit(p->alert_msg, aw - 24, 14, 0); tx(ax + 12, ay + 34, p->alert_msg, n, 14, C_TXT, 0);
        button(ax + aw - 92, ay + 62, 80, 28, "Aceptar", C_GRN, 1);
    }

    /* linea de estado: enlace bajo el puntero, progreso de la carga o errores de red (los avisos de script no se muestran) */
    fb_fill_rect(win_x, (uint32_t)g.stat_y, win_w, STAT_H, C_BG);
    fb_fill_rect(win_x, (uint32_t)g.stat_y, win_w, 1, 0x00141926);
    {
        const char *st = s_hover_href[0] ? s_hover_href : p->status; uint32_t col = s_hover_href[0] ? C_DIM : p->status_kind == WPS_ERR ? C_AMB : p->status_kind == WPS_BUSY ? C_CYA : 0x0064748B;
        fb_set_clip(wx, g.stat_y, ww, STAT_H);
        int n = fit(st, ww - 60, 12, 0); tx(wx + 12, g.stat_y + 4, st, n, 12, col, 0);
        if (busy) { int k = (int)((web_now_ms() / 90) % 8); for (int i = 0; i < 3; i++) fb_fill_rrect_aa(wx + ww - 40 + i * 9, g.stat_y + STAT_H / 2 - 2, 5, 5, 2, (0xFF000000u | C_CYA) - (((k + i * 2) % 8) > 3 ? 0x90000000u : 0u)); }
        fb_reset_clip();
    }
    cm_render(wx, wy, ww, wh);                                            /* PARCHE 085 */
    savedlg_render(wx, wy, ww, wh);
    (void)show_cursor;
}

/* ------------------------------------------------------------ entrada */
static void url_set_from_edit(void) { s_url[s_url_len] = 0; }
static void url_go(void)
{
    url_set_from_edit(); s_url_focus = 0; s_url_all = 0;
    if (s_url_len > 0) q_push(J_LOAD, 0, 0, 0, s_url);
}
static void url_insert(const char *b, int n)
{
    if (s_url_all) { s_url_len = 0; s_url_cur = 0; s_url_all = 0; }
    if (s_url_len + n >= (int)sizeof s_url - 1) return;
    for (int i = s_url_len; i >= s_url_cur; i--) s_url[i + n] = s_url[i];
    memcpy(s_url + s_url_cur, b, (uint32_t)n); s_url_len += n; s_url_cur += n; s_url[s_url_len] = 0;
}
static void url_key(int k, const char *u, int un)
{
    if (k == 10 || k == 13) { url_go(); return; }
    if (k == 27) { s_url_focus = 0; s_url_all = 0; return; }
    if (k == 8 || k == 127) {
        if (s_url_all) { s_url_len = s_url_cur = 0; s_url[0] = 0; s_url_all = 0; return; }
        if (s_url_cur > 0) { int a = s_url_cur - 1; while (a > 0 && ((uint8_t)s_url[a] & 0xC0) == 0x80) a--; int n = s_url_cur - a; for (int i = a; i <= s_url_len - n; i++) s_url[i] = s_url[i + n]; s_url_len -= n; s_url_cur = a; }
        return;
    }
    if (k == 4) { if (s_url_all) { s_url_len = s_url_cur = 0; s_url[0] = 0; s_url_all = 0; return; } if (s_url_cur < s_url_len) { int n = 1; while (s_url_cur + n < s_url_len && ((uint8_t)s_url[s_url_cur + n] & 0xC0) == 0x80) n++; for (int i = s_url_cur; i <= s_url_len - n; i++) s_url[i] = s_url[i + n]; s_url_len -= n; } return; }
    if (k == 2) { s_url_all = 0; if (s_url_cur > 0) { s_url_cur--; while (s_url_cur > 0 && ((uint8_t)s_url[s_url_cur] & 0xC0) == 0x80) s_url_cur--; } return; }
    if (k == 6) { s_url_all = 0; if (s_url_cur < s_url_len) { s_url_cur++; while (s_url_cur < s_url_len && ((uint8_t)s_url[s_url_cur] & 0xC0) == 0x80) s_url_cur++; } return; }
    if (k == 1) { s_url_all = 0; s_url_cur = 0; return; }
    if (k == 5) { s_url_all = 0; s_url_cur = s_url_len; return; }
    if (un > 0 && (uint8_t)u[0] >= 32 && k != 127) url_insert(u, un);
}
static void find_key(int k, const char *u, int un)
{
    if (k == 27) { s_find_open = 0; return; }
    if (k == 10 || k == 13) { if (s_find_len) q_push(J_FIND, 0, 0, 0, s_find); return; }
    if (k == 8 || k == 127) { if (s_find_len > 0) { s_find_len--; while (s_find_len > 0 && ((uint8_t)s_find[s_find_len] & 0xC0) == 0x80) s_find_len--; s_find[s_find_len] = 0; } return; }
    if (un > 0 && (uint8_t)u[0] >= 32 && s_find_len + un < (int)sizeof s_find - 1) { memcpy(s_find + s_find_len, u, (uint32_t)un); s_find_len += un; s_find[s_find_len] = 0; }
}

void browser_handle_key(char c)
{
    ensure_init(); if (!s_pg) return;
    s_last_render = web_now_ms(); s152_input_ms = s_last_render;
    int k = (uint8_t)c;
    if (savedlg_key(c)) return;                                           /* PARCHE 085 */
    if (s_cm_open && k == 27) { s_cm_open = 0; return; }
    if (s_pg->alert_pending) { if (k == 10 || k == 13 || k == 27 || k == ' ') s_pg->alert_pending = 0; return; }
    /* ensamblar UTF-8 */
    char u[5]; int un = 0;
    if (k >= 0x80) {
        if (s_u8n == 0) { s_u8need = k >= 0xF0 ? 4 : k >= 0xE0 ? 3 : k >= 0xC0 ? 2 : 0; if (!s_u8need) return; }
        else if ((k & 0xC0) != 0x80) { s_u8n = 0; return; }
        s_u8[s_u8n++] = (uint8_t)k;
        if (s_u8n < s_u8need) return;
        memcpy(u, s_u8, (uint32_t)s_u8n); un = s_u8n; u[un] = 0; s_u8n = 0; k = 0x100;
    } else if (k >= 32 && k != 127) { u[0] = c; u[1] = 0; un = 1; }
    else u[0] = 0;
    /* atajos globales */
    if (k == 12) { s_url_focus = 1; s_url_all = 1; s_url_cur = s_url_len; return; }
    if (k == 20) { q_push(J_NEWTAB, 0, 0, 0, 0); return; }
    if (k == 23) { q_push(J_CLOSE, s_cur, 0, 0, 0); return; }
    if (k == 18) { q_push(J_RELOAD, 0, 0, 0, 0); return; }
    if (s_url_focus) { url_key(k, u, un); return; }
    if (s_find_open) { find_key(k, u, un); return; }
    if (s_pg->dd_sel) {
        int cnt = wl_select_count(s_pg->dd_sel), cur = s_dd_hl >= 0 ? s_dd_hl : wl_select_index(s_pg->dd_sel);
        if (k == 14 && cur + 1 < cnt) { s_dd_hl = cur + 1; return; }
        if (k == 16 && cur > 0) { s_dd_hl = cur - 1; return; }
        if (k == 10 || k == 13) { q_push(J_SELECT, cur, 0, 0, 0); s_dd_hl = -1; return; }
        if (k == 27) { s_pg->dd_sel = 0; s_dd_hl = -1; return; }
    }
    if (k == '/' && !s_pg->focus) { s_find_open = 1; return; }
    if (k == 0x100) { q_push(J_KEY, 0x100, 0, 0, u); return; }
    q_push(J_KEY, k, 0, 0, u);
}

static int sb_geom(struct geo *g, int *th, int *tr)
{
    int doc = wp_doc_height(s_pg), vh = s_pg->vh; *tr = g->vh - 4; if (doc <= vh || *tr < 10) { *th = 0; return 0; }
    *th = (int)((int64_t)*tr * vh / doc); if (*th < 28) *th = 28; if (*th > *tr) *th = *tr; return doc - vh;
}
static void sb_to(struct geo *g, int my)
{
    int th, tr, range = sb_geom(g, &th, &tr); if (!range) return;
    int pos = my - g->vy - 2 - s_sb_grab, span = tr - th; if (span < 1) span = 1;
    if (pos < 0) { pos = 0; }
    if (pos > span) { pos = span; }
    s_pg->sy = (int)((int64_t)range * pos / span);
}

int browser_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    ensure_init(); if (!s_pg) return 0;
    struct wpage *p = s_pg; s_last_render = web_now_ms(); s152_input_ms = s_last_render;
    int wx = (int)win_x, wy = (int)win_y, ww = (int)win_w, wh = (int)win_h; struct geo g; geo_of(&g, wx, wy, ww, wh);
    if (savedlg_click(mx, my, wx, wy, ww, wh)) return 0;                 /* PARCHE 085: dialogo y menu primero */
    if (cm_click(mx, my, wx, wy, ww, wh)) return 0;
    if (p->alert_pending) {
        int aw = ww > 460 ? 420 : ww - 40, ax = wx + (ww - aw) / 2, ay = wy + wh / 2 - 50;
        if (in_r(mx, my, ax + aw - 92, ay + 62, 80, 28)) p->alert_pending = 0;
        return 0;
    }
    /* pestanas */
    if (my >= g.tab_y && my < g.tab_y + TAB_H) {
        int tabw = tab_width(ww);
        for (int i = 0; i < s_ntabs; i++) {
            int x = wx + 10 + i * tabw;
            if (in_r(mx, my, x, g.tab_y, tabw - 2, TAB_H)) { if (mx >= x + tabw - 30) q_push(J_CLOSE, i, 0, 0, 0); else if (i != s_cur) q_push(J_SWITCH, i, 0, 0, 0); return 0; }
        }
        if (s_ntabs < MAX_TABS && in_r(mx, my, wx + 10 + s_ntabs * tabw, g.tab_y, 30, TAB_H)) q_push(J_NEWTAB, 0, 0, 0, 0);
        return 0;
    }
    int ny = g.nav_y + (NAV_H - NAVB) / 2;
    if (my >= g.nav_y && my < g.nav_y + NAV_H) {
        int nb = wx + 10;
        if (in_r(mx, my, nb, ny, NAVB, NAVB)) { if (wp_can_back(p)) q_push(J_BACK, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + NAVSTEP, ny, NAVB, NAVB)) { if (wp_can_fwd(p)) q_push(J_FWD, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + 2 * NAVSTEP, ny, NAVB, NAVB)) { if (s_job_busy || p->phase != WP_IDLE) p->stop = 1; else q_push(J_RELOAD, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + 3 * NAVSTEP, ny, NAVB, NAVB)) { q_push(J_HOME, 0, 0, 0, 0); s_url_focus = 0; }
        else if (in_r(mx, my, g.ub_x + g.ub_w - 34, ny, 34, NAVB)) url_go();
        else if (in_r(mx, my, g.ub_x, ny, g.ub_w, NAVB)) {
            if (!s_url_focus) { s_url_focus = 1; s_url_all = 1; s_url_cur = s_url_len; }
            else { s_url_all = 0; int best = 0, bd = 1 << 30; int inw = g.ub_w - 36 - 40, cwc = tx_w(s_url, s_url_cur, 14, 0), off = cwc > inw - 4 ? cwc - (inw - 4) : 0; int rel = mx - (g.ub_x + 36) + off;
                for (int i = 0; i <= s_url_len; i++) { if (i < s_url_len && ((uint8_t)s_url[i] & 0xC0) == 0x80) continue; int d = tx_w(s_url, i, 14, 0) - rel; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
                s_url_cur = best; }
        }
        return 0;
    }
    s_url_focus = 0; s_url_all = 0;
    if (!in_r(mx, my, g.vx, g.vy, g.vw, g.vh)) return 0;
    /* barra de desplazamiento */
    if (mx >= g.vx + g.vw - SB_W) {
        int th, tr, range = sb_geom(&g, &th, &tr);
        if (range) {
            int off = (int)((int64_t)(tr - th) * p->sy / range), ty = g.vy + 2 + off;
            if (my >= ty && my < ty + th) { s_sb_drag = 1; s_sb_grab = my - ty; }
            else wp_scroll_by(p, 0, my < ty ? -(p->vh - 40) : p->vh - 40);
        }
        return 0;
    }
    if (p->dd_sel && p->lay) {
        int x, y, w, h; struct wnode *sel = p->dd_sel;
        if (wl_node_rect(p->lay, sel, &x, &y, &w, &h)) {
            int cnt = wl_select_count(sel), cur = wl_select_index(sel), vis = cnt < 14 ? cnt : 14, first = cur - 6; if (first > cnt - vis) first = cnt - vis; if (first < 0) first = 0;
            int ww2 = w < 120 ? 120 : w, sx0 = g.vx + x - p->sx, sy0 = g.vy + y - p->sy + h;
            if (sx0 + ww2 > g.vx + g.vw - SB_W) { sx0 = g.vx + g.vw - SB_W - ww2; }
            if (sx0 < g.vx) { sx0 = g.vx; }
            if (sy0 + vis * 20 > g.vy + g.vh) { sy0 = g.vy + g.vh - vis * 20; }
            if (sy0 < g.vy) { sy0 = g.vy; }
            if (in_r(mx, my, sx0, sy0, ww2, vis * 20)) { q_push(J_SELECT, first + (my - sy0) / 20, 0, 0, 0); s_dd_hl = -1; return 0; }
        }
        s_dd_hl = -1;
    }
    uint64_t now = web_now_ms(); int dbl = 0;
    if (now - s_last_click_t < 400 && (mx - s_last_cx) * (mx - s_last_cx) + (my - s_last_cy) * (my - s_last_cy) < 36) dbl = 1;
    s_last_click_t = dbl ? 0 : now; s_last_cx = mx; s_last_cy = my;
    q_push(J_CLICK, mx - g.vx + p->sx, my - g.vy + p->sy, 0 | (dbl << 8), 0);
    return 0;
}

struct hover_args { struct wpage *p; int px, py; int k; struct wnode *link; };
static void hover_thunk(void *v)
{
    struct hover_args *a = (struct hover_args *)v;
    a->k = wp_hit_cursor(a->p, a->px, a->py);
    a->link = 0; wl_hit(a->p->lay, a->px, a->py, &a->link, 0);
}

/* Gancho de raton para hover, rueda y arrastre de la barra (lo llama gui.c si existe; sin el, el navegador sigue funcionando) */
void browser_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held)
{
    if (!s_inited || !s_pg) return;
    static int32_t lx = -1, ly = -1; static uint32_t lsy = 0xFFFFFFFFu;
    if (mx == lx && my == ly && !wheel && !held && !s_sb_drag && (uint32_t)s_pg->sy == lsy) return;
    lx = mx; ly = my; lsy = (uint32_t)s_pg->sy;
    struct wpage *p = s_pg; struct geo g; geo_of(&g, (int)win_x, (int)win_y, (int)win_w, (int)win_h);
    {   /* SOMA_PARCHE_028_UI: resalte de los botones y pestanas bajo el puntero */
        int wx = (int)win_x, ww = (int)win_w, h = -1, ny = g.nav_y + (NAV_H - NAVB) / 2, nb = wx + 10;
        if (my >= g.tab_y && my < g.tab_y + TAB_H) {
            int tabw = tab_width(ww);
            for (int i = 0; i < s_ntabs; i++) { int x = wx + 10 + i * tabw; if (in_r(mx, my, x, g.tab_y, tabw - 2, TAB_H)) h = (mx >= x + tabw - 30 ? 30 : 10) + i; }
            if (h < 0 && s_ntabs < MAX_TABS && in_r(mx, my, wx + 10 + s_ntabs * tabw, g.tab_y, 30, TAB_H)) h = 50;
        } else if (my >= g.nav_y && my < g.nav_y + NAV_H) {
            for (int i = 0; i < 4; i++) if (in_r(mx, my, nb + i * NAVSTEP, ny, NAVB, NAVB)) h = i;
            if (in_r(mx, my, g.ub_x + g.ub_w - 34, ny, 34, NAVB)) h = 4;
        }
        s_hover_ui = held ? -1 : h;
    }
    if (s_cm_open) {                                                      /* PARCHE 085: resalte en el menu */
        int x, y, h; cm_geom(&x, &y, &h, (int)win_x, (int)win_y, (int)win_w, (int)win_h);
        s_cm_hl = in_r(mx, my, x, y, CM_W, h) ? (my - y - 6) / CM_IH : -1;
        set_cursor(CURSOR_ARROW);
        return;
    }
    if (savedlg_active()) { set_cursor(CURSOR_ARROW); return; }
    int inview = in_r(mx, my, g.vx, g.vy, g.vw - SB_W, g.vh) && !p->alert_pending;
    if (s_sb_drag) { if (held) { sb_to(&g, my); s152_input_ms = web_now_ms(); } else s_sb_drag = 0; }
    if (wheel && inview && p->lay && !p->lay_busy && wl_inner_scroll(p->lay, mx - g.vx + p->sx, my - g.vy + p->sy, -wheel * SOMA_WEB_WHEEL_PX)) { s_last_render = web_now_ms(); s152_input_ms = s_last_render; }   /* SOMA_181: cajas con overflow:auto */
    else if (wheel && in_r(mx, my, (int)win_x, g.vy, (int)win_w, g.vh) && !s_sb_drag) { smooth_wheel152(p, -wheel * SOMA_WEB_WHEEL_PX); s_last_render = web_now_ms(); s152_input_ms = s_last_render; }
    if (inview && p->lay && !p->lay_busy) {
        s_move_x = mx - g.vx; s_move_y = my - g.vy;
        /* SOMA_152: durante el desplazamiento no se recalcula el hover (cada cambio puede reestilar la pagina) */
        if (wheel || s152_tgt >= 0 || web_now_ms() - s152_scroll_ms < 150) s152_hover_deferred = 1; else s_move_pending = 1;
        /* No bloquear la GUI con hit-testing O(arbol) mientras se desplaza. */
        static uint64_t last_hit_ms;
        static int last_hit_x = -10000, last_hit_y = -10000;
        uint64_t t = web_now_ms();
        int ddx = mx - last_hit_x, ddy = my - last_hit_y;
        if (wheel || s_sb_drag ||
            (last_hit_ms && t >= last_hit_ms && t - last_hit_ms < 24 &&
             ddx >= -12 && ddx <= 12 && ddy >= -12 && ddy <= 12)) return;
        last_hit_ms = t; last_hit_x = mx; last_hit_y = my;
        int px = mx - g.vx + p->sx, py = my - g.vy + p->sy;
        struct hover_args ha = { p, px, py, 0, 0 }; on_gui_stack(hover_thunk, &ha);
        set_cursor(ha.k == WPK_LINK ? CURSOR_HAND : CURSOR_ARROW);
        const char *h = ha.link ? wd_attr(ha.link, "href") : 0;
        if (h && h[0]) wu_copy(s_hover_href, h, sizeof s_hover_href); else s_hover_href[0] = 0;
    } else { if (s_hover_href[0]) s_hover_href[0] = 0; set_cursor(CURSOR_ARROW); }
}

/* ------------------------------------------------------------ API publica (compatible con la anterior) */
void browser_init(void) { ensure_init(); }
void browser_load_url(const char *url)
{
    ensure_init(); if (!s_pg || !url || !*url) return;
    s_need_home = 0;
    wu_copy(s_url, url, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len;
    q_push(J_LOAD, 0, 0, 0, url);
}
int browser_is_loading(void)
{
    if (!s_inited || !s_pg) return 0;
    return s_job_busy || s_qh != s_qt || s_pg->phase == WP_FETCH || s_pg->phase == WP_RES || s_pg->phase == WP_SCRIPT;
}
struct text_args { struct wdoc *d; struct wnode *n; const char *s; struct wb b; };
static void settext_thunk(void *v) { struct text_args *a = (struct text_args *)v; wd_set_text(a->d, a->n, a->s); }
static void gettext_thunk(void *v) { struct text_args *a = (struct text_args *)v; wd_text_content(a->n, &a->b); }
int browser_set_element_text_by_id(const char *id, const char *new_text)
{
    if (!s_inited || !s_pg || !s_pg->doc || !id || s_pg->lay_busy) return 0;
    struct wnode *n = wd_by_id(s_pg->doc->root, id); if (!n) return 0;
    struct text_args ta; memset(&ta, 0, sizeof ta); ta.d = s_pg->doc; ta.n = n; ta.s = new_text ? new_text : ""; on_gui_stack(settext_thunk, &ta);
    s_pg->need_style = 1; return 1;
}
const char *browser_get_element_text_by_id(const char *id)
{
    static char buf[256];
    buf[0] = 0;
    if (!s_inited || !s_pg || !s_pg->doc || !id || s_pg->lay_busy) return buf;
    struct wnode *n = wd_by_id(s_pg->doc->root, id); if (!n) return buf;
    struct text_args ta; memset(&ta, 0, sizeof ta); ta.d = s_pg->doc; ta.n = n; on_gui_stack(gettext_thunk, &ta);
    if (ta.b.p) wu_copy(buf, ta.b.p, sizeof buf);
    wb_free(&ta.b); return buf;
}
void browser_js_eval(const char *code)
{
    ensure_init(); if (!s_pg || !code || !*code) return;
    q_push(J_EVAL, 0, 0, 0, code);
}
