/* SOMA_285_CHAT: la aplicacion de chat (src/apps/chat.c) dibujada en el anfitrion con la fuente de verdad (web_font.c), el maquetador
 * (chat_md.c + tex.c), el historial (chat_core.c) y el framebuffer de pruebas (tests/layhost/fbhost.c). El modelo, el agente, la memoria,
 * el disco y la red son de mentira y apuntan lo que reciben. Recorre la aplicacion como un usuario (teclas y clics sobre las zonas de
 * verdad), comprueba lo que llega al modelo y al agente, y guarda capturas PNG en la carpeta que se le pase.
 * Uso: test_chatapp_285 [CARPETA_PNG] */
#define SOMA_CHAT_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include "png_enc.h"

static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)

/* ---------------------------------------------------------------- sistema de mentira */
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_ms = 100000;
uint64_t timer_get_uptime_ms(void) { return g_ms; }
static int64_t g_min = 29000000 + 10 * 60 + 30;          /* minutos locales */
int64_t agenda_ahora(void) { return g_min; }

struct vf { char name[200]; char *d; uint32_t n; };
static struct vf V[64]; static int nV;
static struct vf *vbusca(const char *n) { for (int i = 0; i < nV; i++) if (!strcmp(V[i].name, n)) return &V[i]; return 0; }
int vfs_write(const char *name, const char *data, uint32_t len)
{
    struct vf *f = vbusca(name); if (!f) { if (nV >= 64) return -1; f = &V[nV++]; snprintf(f->name, sizeof f->name, "%s", name); f->d = 0; }
    free(f->d); f->d = malloc(len + 1); memcpy(f->d, data, len); f->d[len] = 0; f->n = len; return (int)len;
}
int vfs_read(const char *name, char *buf, uint32_t max) { struct vf *f = vbusca(name); if (!f) return -1; uint32_t n = f->n < max ? f->n : max; memcpy(buf, f->d, n); return (int)n; }
char *vfs_read_alloc(const char *name, uint32_t *len) { struct vf *f = vbusca(name); if (!f) return 0; char *d = malloc(f->n + 1); memcpy(d, f->d, f->n); d[f->n] = 0; *len = f->n; return d; }
int vfs_format_list_ext(int all, const char *dir, char *out, uint32_t max)
{
    (void)all; int k = snprintf(out, max, "Archivos: "); size_t dl = strlen(dir); char vistos[32][120]; int nv = 0;
    for (int i = 0; i < nV; i++) {
        const char *n = V[i].name; if (strncmp(n, dir, dl) || n[dl] != '/') continue;
        const char *rest = n + dl + 1, *sl = strchr(rest, '/');
        char e[200];
        if (sl) { snprintf(e, sizeof e, "%.*s/%.*s/", (int)dl, dir, (int)(sl - rest), rest); int dup = 0; for (int q = 0; q < nv; q++) if (!strcmp(vistos[q], e)) dup = 1; if (dup) continue; if (nv < 32) snprintf(vistos[nv++], 120, "%s", e); }
        else snprintf(e, sizeof e, "%s", n);
        k += snprintf(out + k, max - (uint32_t)k, "%s%s", k > 10 ? ", " : "", e);
    }
    return k;
}
#include "thread.h"
static struct tcb g_tcb; static int g_hilos;
struct tcb *thread_create(const char *name, thread_func_t entry, void *arg) { (void)name; (void)entry; (void)arg; g_hilos++; return &g_tcb; }
void thread_sleep(uint32_t ms) { (void)ms; }

/* memoria */
static int g_mem_activa = 1; static char g_reg_q[400], g_reg_r[400]; static int g_nreg;
int memoria_activa(void) { return g_mem_activa; }
int memoria_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos)
{
    (void)mision; const char *t = "- El usuario se llama Marcos y programa SOMA en C.\n";
    for (int i = 0; t[i] && *pos + 1 < max; i++) dst[(*pos)++] = t[i];
    dst[*pos] = 0; return 1;
}
void memoria_registra(const char *u, const char *r) { snprintf(g_reg_q, sizeof g_reg_q, "%s", u); snprintf(g_reg_r, sizeof g_reg_r, "%s", r); g_nreg++; }

/* modelo */
#include "llm_proto.h"
#include "llm.h"
char llm_last_error[200];
void llm_trace_tag(const char *t) { (void)t; }
static struct llm_tam g_tam;
const struct llm_tam *llm_tam(void) { return &g_tam; }
static struct llm_conf g_conf; static int g_conf_ok;
const struct llm_conf *llm_conf_get(void) { if (!g_conf_ok) { llm_conf_defaults(&g_conf); g_conf_ok = 1; } return &g_conf; }
int llm_vision_unsupported(void) { return 0; }
static char g_sys[20000], g_last_user[4000]; static int g_nmsg, g_nimg, g_falla, g_nllamadas, g_roles[64];
static const char RESP_REL[] =
    "## Relatividad especial\n\n"
    "La **relatividad especial** (Einstein, 1905) parte de dos postulados: las leyes de la f\303\255sica son iguales en todos los sistemas inerciales y la "
    "velocidad de la luz $c$ es la misma para todos los observadores. De ah\303\255 salen efectos que *no* se ven en la vida diaria pero que el GPS tiene que corregir.\n\n"
    "### Las f\303\263rmulas clave\n\n"
    "El factor de Lorentz mide cu\303\241nto se nota el efecto:\n\n"
    "$$\\gamma = \\frac{1}{\\sqrt{1 - \\frac{v^2}{c^2}}}$$\n\n"
    "- **Dilataci\303\263n del tiempo:** un reloj en movimiento va m\303\241s despacio, $\\Delta t = \\gamma \\, \\Delta t_0$.\n"
    "- **Contracci\303\263n de la longitud:** $L = L_0 / \\gamma$ en la direcci\303\263n del movimiento.\n"
    "- **Equivalencia masa-energ\303\255a:** $E = mc^2$.\n\n"
    "| Velocidad | $\\gamma$ | Un a\303\261o a bordo equivale a |\n|:--|:-:|--:|\n| 0,1 c | 1,005 | 1,005 a\303\261os |\n| 0,5 c | 1,155 | 1,155 a\303\261os |\n| 0,9 c | 2,294 | 2,294 a\303\261os |\n| 0,99 c | 7,089 | 7,089 a\303\261os |\n\n"
    "> [!TIP]\n> A velocidades normales $\\gamma \\approx 1 + \\frac{v^2}{2c^2}$, por eso la mec\303\241nica de Newton funciona tan bien.\n\n"
    "Para calcularlo t\303\272 mismo:\n\n"
    "```python\nfrom math import sqrt\n\nC = 299_792_458  # m/s\n\ndef gamma(v):\n    \"\"\"Factor de Lorentz para una velocidad v en m/s.\"\"\"\n    return 1 / sqrt(1 - (v / C) ** 2)\n\nprint(round(gamma(0.9 * C), 3))  # 2.294\n```\n\n"
    "M\303\241s en la [Wikipedia](https://es.wikipedia.org/wiki/Relatividad_especial) o en <https://einsteinpapers.press.princeton.edu>.\n";
static const char RESP_CORTA[] = "\302\241Claro! El **mergesort** divide la lista en dos, ordena cada mitad y las mezcla. Es $O(n \\log n)$ siempre.";
int llm_chat_hist(const char *system, const struct llm_msg *m, int n, const struct llm_img *img, int nimg, int max_tokens, char *out, uint32_t max, uint32_t to, const volatile int *cancel)
{
    (void)img; (void)max_tokens; (void)to; (void)cancel;
    g_nllamadas++;
    snprintf(g_sys, sizeof g_sys, "%s", system); g_nmsg = n; g_nimg = nimg;
    for (int i = 0; i < n && i < 64; i++) g_roles[i] = m[i].rol;
    snprintf(g_last_user, sizeof g_last_user, "%s", n ? m[n - 1].text : "");
    if (g_falla) { g_falla--; snprintf(llm_last_error, sizeof llm_last_error, "LLM: no responde el servidor (192.168.1.200:8087)"); return 0; }
    const char *r = strstr(g_last_user, "relatividad") ? RESP_REL : RESP_CORTA;
    snprintf(out, max, "%s", r);
    return 1;
}
/* red e imagenes */
#include "web_net.h"
#include "web_img.h"
static int g_fetch_n;
int wn_fetch(const struct wn_req *rq, struct wn_resp *rs) { (void)rq; g_fetch_n++; rs->status = 200; rs->len = 64; rs->body = malloc(64); memset(rs->body, 7, 64); return 0; }
void wn_free(struct wn_resp *rs) { free(rs->body); rs->body = 0; }
int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh)
{
    (void)d; (void)n; (void)maxw; (void)maxh;
    o->w = 640; o->h = 360; o->px = malloc((size_t)o->w * o->h * 4); o->frames = 1; o->delay = 0; o->nw = o->nh = 0;
    for (int y = 0; y < o->h; y++) for (int x = 0; x < o->w; x++) {
        int r = 40 + x * 180 / o->w, g = 60 + y * 120 / o->h, b = 160 + (x + y) % 90;
        int dx = x - 420, dy = y - 140; if (dx * dx + dy * dy < 50 * 50) { r = 250; g = 210; b = 90; }
        if (y > 250 + (x % 160 < 80 ? x % 80 : 80 - x % 80)) { r /= 3; g = g / 2 + 30; b /= 3; }
        o->px[y * o->w + x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    return 0;
}
void wi_free(struct wimg *o) { free(o->px); o->px = 0; }
/* agente */
static char g_mision[4000], g_ctx[8000]; static int g_ctx_sin, g_ag_busy, g_ag_env, g_ag_cancel;
int gui_agent_submit_chat(const char *m) { if (g_ag_busy) return -1; snprintf(g_mision, sizeof g_mision, "%s", m); g_ag_env++; g_ag_busy = 1; return 0; }
int gui_agent_busy(void) { return g_ag_busy; }
int gui_agent_cancel(void) { g_ag_cancel++; return 1; }
void agent_chat_contexto(const char *h, int sin) { snprintf(g_ctx, sizeof g_ctx, "%s", h ? h : ""); g_ctx_sin = sin; }
/* teclado, portapapeles, framebuffer */
static int g_ctrl, g_shift;
int keyboard_ctrl_held(void) { return g_ctrl; }
int keyboard_shift_held(void) { return g_shift; }
static char g_clip[8192];
const char *clip_text(uint32_t *n) { *n = (uint32_t)strlen(g_clip); return g_clip; }
int clip_set_text(const char *t, uint32_t n) { if (!n) n = (uint32_t)strlen(t); if (n > sizeof g_clip - 1) n = sizeof g_clip - 1; memcpy(g_clip, t, n); g_clip[n] = 0; return 0; }
int g_wf182_on = 1;
extern uint32_t *g_fb; extern int g_fbw, g_fbh;
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb);
static void put2(int x, int y, uint32_t c, int a)
{
    int32_t x0, y0, x1, y1; fb_get_clip(&x0, &y0, &x1, &y1);
    if (x < x0 || y < y0 || x >= x1 || y >= y1 || x < 0 || y < 0 || x >= g_fbw || y >= g_fbh || a <= 0) return;
    if (a > 255) a = 255;
    uint32_t d = g_fb[y * g_fbw + x];
    uint32_t r = (((c >> 16) & 255) * (uint32_t)a + ((d >> 16) & 255) * (uint32_t)(255 - a)) / 255, g = (((c >> 8) & 255) * (uint32_t)a + ((d >> 8) & 255) * (uint32_t)(255 - a)) / 255, b = ((c & 255) * (uint32_t)a + (d & 255) * (uint32_t)(255 - a)) / 255;
    g_fb[y * g_fbw + x] = 0xFF000000u | r << 16 | g << 8 | b;
}
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | c); }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    int a0 = (int)(argb >> 24); if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
    double cx = x + w / 2.0, cy = y + h / 2.0, hx = w / 2.0 - 0.5, hy = h / 2.0 - 0.5;
    for (int j = y - 1; j <= y + h; j++) for (int i = x - 1; i <= x + w; i++) {
        double px = fabs(i + 0.5 - cx) - (hx - r), py = fabs(j + 0.5 - cy) - (hy - r);
        double ox = px > 0 ? px : 0, oy = py > 0 ? py : 0;
        double d = sqrt(ox * ox + oy * oy) + (px > py ? (px < 0 ? px : 0) : (py < 0 ? py : 0)) - r;
        double cov = 1.0 - fabs(d); if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t argb)
{
    double ax = x0 / 16.0, ay = y0 / 16.0, bx = x1 / 16.0, by = y1 / 16.0, hw = width16 / 32.0; int a0 = (int)(argb >> 24);
    int mnx = (int)floor((ax < bx ? ax : bx) - hw - 1), mxx = (int)ceil((ax > bx ? ax : bx) + hw + 1), mny = (int)floor((ay < by ? ay : by) - hw - 1), mxy = (int)ceil((ay > by ? ay : by) + hw + 1);
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int j = mny; j <= mxy; j++) for (int i = mnx; i <= mxx; i++) {
        double px = i + 0.5 - ax, py = j + 0.5 - ay, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        double ex = px - t * dx, ey = py - t * dy, d = sqrt(ex * ex + ey * ey), cov = hw + 0.5 - d;
        if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) put2(dx + i, dy + j, src[(j * sh / dh) * stride + i * sw / dw], 255);
}
void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; }
void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; }
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t c, int sc) { (void)x; (void)y; (void)s; (void)c; (void)sc; }

#include "../src/apps/chat.c"            /* incluida: la prueba pulsa las zonas de verdad (H[]) y mira el estado */

/* ---------------------------------------------------------------- utilidades de la prueba */
static const char *g_cap; static int g_shots;
static void guarda_png(const char *nombre)
{
    if (!g_cap) return;
    size_t cap = (size_t)g_fbw * g_fbh * 4 + 4096; uint8_t *out = malloc(cap); void *wk = malloc(png_work_size(g_fbw, g_fbh));
    size_t n = png_encode_rgb(g_fb, g_fbw, g_fbh, out, cap, wk);
    char r[300]; snprintf(r, sizeof r, "%s/%s.png", g_cap, nombre); FILE *f = fopen(r, "wb"); if (f) { fwrite(out, 1, n, f); fclose(f); g_shots++; }
    free(out); free(wk);
}
static int WW = 1280, HH = 820;
static void pinta(void) { memset(g_fb, 0, (size_t)g_fbw * g_fbh * 4); chat_render_window(0, 0, (uint32_t)WW, (uint32_t)HH, 1); }
static void foto(const char *nombre) { pinta(); guarda_png(nombre); }
static int clic(int x, int y) { chat_mouse(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); pinta(); int r = chat_handle_click(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH); pinta(); return r; }
static void tecla(char c) { chat_handle_key(c); }
static void escribe(const char *s) { while (*s) tecla(*s++); pinta(); }
static int pulsa(int act, int a)
{
    pinta();
    for (int i = nH - 1; i >= 0; i--) if (H[i].act == act && (a == -999 || H[i].a == a)) return clic(H[i].x + H[i].w / 2, H[i].y + H[i].h / 2) + 100;
    printf("  (no encuentro el boton %d/%d)\n", act, a); return 0;
}
static int hay(int act) { for (int i = 0; i < nH; i++) if (H[i].act == act) return 1; return 0; }
static void avanza(int ms_) { for (int t = 0; t < ms_; t += 40) { g_ms += 40; chat_tick(); } pinta(); }
static void atiende_llm(void) { if (g_w_estado == 1) llm_una(); avanza(2000); }
static struct cc_conv *cur(void) { return conv_actual(); }
static void sobre_msg(uint32_t idx)                                     /* pone el raton encima de un mensaje (sus botones aparecen) */
{
    struct cc_conv *c = cur(); pinta();
    struct geo g; g.x = g_mx0; g.w = g_mw0; g.cw = g_mw0 - 48 > COL_MAX ? COL_MAX : g_mw0 - 48; if (g.cw < 200) g.cw = 200; g.cx = g_mx0 + (g_mw0 - g.cw) / 2;
    int yy = g_view_y0 + 24 - g_scroll;
    for (uint32_t i = 0; i < idx; i++) { struct lay *l; yy += alto_msg(c, i, &g, &l); }
    struct lay *l; int hh = alto_msg(c, idx, &g, &l);
    int y = yy + hh / 2; if (y < g_view_y0 + 4) { g_pegado = 0; g_scroll -= g_view_y0 + 40 - y; pinta(); y = g_view_y0 + 40; }
    chat_mouse(g.cx + g.cw - 30, y, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); pinta();
}
static int zona_de(int kind, const char *txt, int *px, int *py)          /* busca una zona del Markdown en pantalla */
{
    struct cc_conv *c = cur(); if (!c) return 0;
    for (int y = g_view_y0 + 2; y < g_view_y1 - 2; y += 3) for (int x = g_mx0 + 10; x < g_mx0 + g_mw0 - 10; x += 5) {
        int msg, z; if (zona_en(x, y, &msg, &z)) { struct lay *l = &g_lay[msg]; const struct md_zone *q = &l->z[z]; if (q->kind == kind && (!txt || (q->n == strlen(txt) && !memcmp(l->pool + q->s, txt, q->n)))) { *px = x; *py = y; return 1; } }
    }
    return 0;
}

int main(int argc, char **argv)
{
    g_cap = argc > 1 ? argv[1] : 0;
    g_fbw = 1600; g_fbh = 1000; g_fb = calloc((size_t)g_fbw * g_fbh, 4);
    vfs_write("/usuario/notas.txt", "Lista de la compra: pan, leche, huevos.\nRecordar: llamar al taller el lunes.", 76);
    vfs_write("/usuario/foto.png", "\x89PNG\r\n\x1a\n0000", 12);
    vfs_write("/usuario/proyectos/soma.md", "# SOMA", 6);
    vfs_write("/usuario/grafica.png", "\x89PNG\r\n\x1a\n1111", 12);

    chat_init();
    CK(g_ops == 0 && b_comp != 0);                                 /* el maquetador pide su memoria al primer mensaje */
    /* ---- 1. pantalla vacia */
    foto("01_vacio");
    CK(hay(A_SUGIERE) && hay(A_NUEVA) && hay(A_MODO) && hay(A_MEMT) && hay(A_ENVIAR));
    CK(g_cur == 0 && g_h.n == 0);
    /* ---- 2. Solo LLM con memoria: escribir y enviar con Intro */
    CK(g_modo == CC_MODO_LLM && g_mem == 1);
    escribe("Expl\303\255came la relatividad especial con f\303\263rmulas");
    g_shift = 1; tecla('\n'); g_shift = 0; escribe("y una tabla");                 /* Mayus+Intro: nueva linea */
    CK(strchr(b_comp, '\n') != 0);
    foto("02_escribiendo");
    tecla('\n');
    CK(g_cur != 0 && cur() && cur()->nm == 1 && g_turno == T_LLM && g_w_estado == 1 && tf_len(&f_comp) == 0);
    CK(!strcmp(cur()->titulo, "Expl\303\255came la relatividad especial con"  "\342\200\246") || strlen(cur()->titulo) > 10);
    avanza(1700);
    foto("03_pensando");
    CK(hay(A_PARAR));
    llm_una(); CK(g_w_estado == 3 && g_w_ok);
    CK(strstr(g_sys, "Eres SOMA") && strstr(g_sys, "Marcos y programa SOMA") && strstr(g_sys, "Fecha y hora"));
    CK(g_nmsg == 1 && strstr(g_last_user, "relatividad especial con f\303\263rmulas\ny una tabla"));
    avanza(80);                                                                   /* la respuesta llega... */
    CK(g_turno == T_NO && cur()->nm == 2 && cur()->m[1].rol == CC_ASIS && cur()->m[1].estado == CC_OK && cur()->m[1].dur >= 1700);
    CK(g_nreg == 1 && strstr(g_reg_q, "relatividad") && !strstr(g_reg_q, "Archivo adjunto") && strstr(g_reg_r, "Lorentz"));
    CK(g_rev_idx == 1);
    avanza(400); foto("04_desvelando");
    avanza(2000); CK(g_rev_idx == -1);
    g_scroll = 0; g_pegado = 0; foto("05_respuesta_arriba");
    g_pegado = 1; foto("06_respuesta_abajo");
    {   struct lay *l = &g_lay[1]; CK(l->nops > 100 && l->nz >= 3);
        int maths = 0, codes = 0; for (int i = 0; i < l->nz; i++) if (l->z[i].kind == MDZ_COPY) codes++;
        (void)maths; CK(codes == 1); }
    /* enlace: clic -> Navegador */
    {   int x, y; g_scroll = g_max_scroll; g_pegado = 1; pinta();
        CK(zona_de(MDZ_LINK, "https://es.wikipedia.org/wiki/Relatividad_especial", &x, &y));
        chat_mouse(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); CK(g_hz_zone >= 0); foto("07_enlace_resaltado");
        int r = chat_handle_click(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH); CK(r == 3 && !strcmp(chat_url(), "https://es.wikipedia.org/wiki/Relatividad_especial"));
        CK(zona_de(MDZ_COPY, 0, &x, &y)); r = chat_handle_click(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH); CK(r == 1 && strstr(g_clip, "def gamma(v):") && strstr(g_toast, "Copiado")); }
    /* copiar el mensaje entero */
    CK(pulsa(A_COPIA_MSG, 1)); CK(!strncmp(g_clip, "## Relatividad", 14));
    /* ---- 3. segunda pregunta: el modelo recibe la conversacion */
    escribe("Ahora escribe mergesort en Python"); tecla('\n');
    llm_una(); avanza(100);
    CK(g_nmsg == 3 && g_roles[0] == LLM_ROL_USER && g_roles[1] == LLM_ROL_ASSISTANT && g_roles[2] == LLM_ROL_USER);
    CK(cur()->nm == 4);
    /* regenerar la ultima */
    int nll = g_nllamadas; CK(pulsa(A_REGEN, 3)); CK(g_turno == T_LLM && cur()->nm == 3); g_ms += 2300; llm_una(); avanza(2000); CK(g_nllamadas == nll + 1 && cur()->nm == 4);
    /* editar la segunda pregunta y reenviar */
    sobre_msg(2); CK(pulsa(A_EDITA, 2)); CK(g_edit_idx == 2 && !strcmp(b_comp, "Ahora escribe mergesort en Python"));
    foto("08_editando");
    tecla(8); tecla(8); tecla(8); tecla(8); tecla(8); tecla(8); escribe("C"); tecla('\n');
    CK(cur()->nm == 3 && !strncmp(cur()->m[2].txt, "Ahora escribe mergesort en C", 28)); llm_una(); avanza(2000); CK(cur()->nm == 4);
    /* ---- 4. un fallo del modelo y Reintentar */
    g_falla = 1; escribe("Otra pregunta"); tecla('\n'); llm_una(); avanza(100);
    CK(cur()->nm == 6 && cur()->m[5].estado == CC_ERROR && strstr(cur()->m[5].txt, "no responde"));
    foto("09_error");
    CK(pulsa(A_REINTENTA, 5)); llm_una(); avanza(100); CK(cur()->nm == 6 && cur()->m[5].estado == CC_OK);
    /* ---- 5. modo Agente, adjuntos y memoria apagada */
    CK(pulsa(A_NUEVA, -999)); CK(g_cur == 0);
    CK(pulsa(A_MODO, CC_MODO_AGENTE)); CK(g_modo == CC_MODO_AGENTE);
    CK(pulsa(A_ADJ, -999)); CK(g_modal == M_ARCH && g_nlista == 4);
    {   int k = -1; const char *p = g_lista; for (int i = 0; i < g_nlista; i++) { if (!strcmp(p, "notas.txt")) k = i; p += strlen(p) + 1; }
        CK(k >= 0); CK(pulsa(A_AR_ITEM, k)); CK(g_nadj == 1 && !strcmp(g_adj[0], "/usuario/notas.txt")); }
    {   int k = -1; const char *p = g_lista; for (int i = 0; i < g_nlista; i++) { if (!strcmp(p, "proyectos/")) k = i; p += strlen(p) + 1; }
        CK(k >= 0); CK(pulsa(A_AR_ITEM, k)); CK(!strcmp(g_dir, "/usuario/proyectos") && g_nlista == 1); CK(pulsa(A_AR_SUBE, -999)); CK(!strcmp(g_dir, "/usuario")); }
    {   int k = -1; const char *p = g_lista; for (int i = 0; i < g_nlista; i++) { if (!strcmp(p, "foto.png")) k = i; p += strlen(p) + 1; } CK(pulsa(A_AR_ITEM, k)); CK(g_nadj == 2); }
    foto("10_adjuntar");
    tecla(27); CK(g_modal == M_NO);
    CK(pulsa(A_MEMT, -999)); CK(g_mem == 0);
    escribe("Resume el archivo y dime qu\303\251 tengo que hacer el lunes");
    foto("11_agente_listo");
    tecla('\n');
    CK(g_turno == T_AGENTE && g_ag_env == 1 && strstr(g_mision, "Resume el archivo") && strstr(g_mision, "/usuario/notas.txt") && strstr(g_mision, "/usuario/foto.png") && g_ctx_sin == 1);
    chat_fase("lcm"); chat_fase("mayordomo"); chat_fase_orden("leer /usuario/notas.txt"); chat_fase("pensar"); avanza(2600);
    CK(g_nfase == 4);
    foto("12_agente_trabajando");
    g_ag_busy = 0; CK(chat_agente_fin("En **notas.txt** tienes:\n\n1. Comprar pan, leche y huevos.\n2. **El lunes:** llamar al taller.\n\n```text\nLista de la compra: pan, leche, huevos.\n```"));
    avanza(2500);
    CK(g_turno == T_NO && cur()->nm == 2 && cur()->m[1].modo == CC_MODO_AGENTE && cur()->m[1].mem == 0 && cur()->m[0].adj && strstr(cur()->m[0].adj, "foto.png"));
    CK(!chat_agente_fin("otra")); CK(g_ctx[0] == 0);
    foto("13_agente_respuesta");
    /* el agente ocupado (mision del orbe): aviso con Reintentar */
    g_ag_busy = 1; escribe("Y el martes?"); tecla('\n'); CK(cur()->nm == 4 && cur()->m[3].estado == CC_ERROR && g_turno == T_NO);
    g_ag_busy = 0; CK(pulsa(A_REINTENTA, 3)); CK(g_turno == T_AGENTE && strstr(g_ctx, "Usuario: Resume el archivo") && strstr(g_ctx, "SOMA: En **notas.txt**"));
    g_ag_busy = 0; chat_agente_fin("Mision detenida."); avanza(100); CK(cur()->m[cur()->nm - 1].estado == CC_CORTADA);
    /* ---- 6. imagenes en la respuesta (local y de la web) */
    CK(pulsa(A_NUEVA, -999)); CK(pulsa(A_MODO, CC_MODO_LLM)); CK(pulsa(A_MEMT, -999)); CK(g_mem == 1);
    { struct cc_conv *c = cc_nueva(&g_h, (uint32_t)g_min, CC_MODO_LLM, 1); g_cur = c->id;
      const char *q = "Ens\303\251\303\261ame la gr\303\241fica"; cc_anade(c, CC_USER, q, (uint32_t)strlen(q), 0, (uint32_t)g_min); cc_titulo_auto(c);
      const char *r = "Aqu\303\255 la tienes:\n\n![grafica](/usuario/grafica.png)\n\nY la de la web:\n\n![remota](https://ejemplo.org/a.png)\n\nLa curva sube como $e^{x}$.";
      struct cc_msg *m = cc_anade(c, CC_ASIS, r, (uint32_t)strlen(r), 0, (uint32_t)g_min); m->dur = 1234; }
    pinta();
    { struct img *a = img_busca("/usuario/grafica.png", 20), *b = img_busca("https://ejemplo.org/a.png", 25);
      CK(a && a->st == IM_OK && b && b->st == IM_PEDIR && g_hilos >= 1);
      foto("14_imagen_cargando");
      img_una(b); CK(b->st == IM_BYTES); avanza(100); CK(b->st == IM_OK);
      foto("15_imagenes"); }
    {   int x, y; CK(zona_de(MDZ_IMG, "/usuario/grafica.png", &x, &y)); int r = chat_handle_click(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH); CK(r == 5 && !strcmp(chat_path(), "/usuario/grafica.png")); }
    /* ---- 7. historial: varios dias, buscar, fijar, renombrar, borrar */
    {   static const char *const T[6] = { "Receta de lentejas", "Configurar el wifi del port\303\241til", "Ideas para el cumplea\303\261os de Luc\303\255a", "Plan de entrenamiento", "Traducir una carta al ingl\303\251s", "\302\277Qu\303\251 es un kernel?" };
        static const int D[6] = { 1, 1, 3, 9, 40, 400 };
        for (int k = 0; k < 6; k++) { uint32_t t = (uint32_t)(g_min - D[k] * 1440); struct cc_conv *c = cc_nueva(&g_h, t, k & 1, 1); cc_anade(c, CC_USER, T[k], (uint32_t)strlen(T[k]), 0, t); cc_anade(c, CC_ASIS, "Vale.", 5, 0, t + 1); cc_titulo_auto(c); } }
    foto("16_historial");
    {   uint32_t ids[20]; int n = cc_orden(&g_h, 0, ids, 20); CK(n == 9); }
    g_foco = &f_busca; escribe("CUMPLEANOS"); { uint32_t ids[4]; CK(cc_orden(&g_h, b_busca, ids, 4) == 1); }
    foto("17_buscar");
    tecla('\n'); CK(cur() && strstr(cur()->titulo, "cumplea"));
    tf_set(&f_busca, ""); g_foco = &f_comp;
    {   uint32_t id = g_cur; pinta();
        CK(pulsa(A_CONV_MENU, (int)id)); CK(g_menu_conv && g_menu_id == id); foto("18_menu");
        CK(pulsa(A_M_FIJA, (int)id)); CK(cur()->fija == 1);
        CK(pulsa(A_CONV_MENU, (int)id)); CK(pulsa(A_M_REN, (int)id)); CK(g_modal == M_REN); g_ctrl = 1; tecla(1); g_ctrl = 0; escribe("Cumple de Luc\303\255a"); foto("19_renombrar"); tecla('\n');
        CK(g_modal == M_NO && !strcmp(cur()->titulo, "Cumple de Luc\303\255a") && cur()->titulo_manual);
        CK(pulsa(A_CONV_MENU, (int)id)); CK(pulsa(A_M_EXPORTA, (int)id) == 104); CK(vbusca(chat_path()) && strstr(vbusca(chat_path())->d, "# Cumple de Luc"));
        CK(pulsa(A_CONV_MENU, (int)id)); CK(pulsa(A_M_BORRA, (int)id)); CK(g_modal == M_BORRA); foto("20_borrar"); tecla('\n');
        CK(cc_por_id(&g_h, id) == 0 && g_cur == 0); }
    /* ---- 8. se guarda y se vuelve a leer igual */
    avanza(2000); CK(!g_sucio);
    {   struct vf *f = vbusca(HIST_FILE); CK(f && f->n > 100);
        struct cc_hist h2; cc_init(&h2); CK(cc_carga(&h2, f->d, f->n) == (int)g_h.n);
        uint32_t a = cc_serializa(&g_h, 0, 0), b = cc_serializa(&h2, 0, 0); CK(a == b); cc_libera(&h2);
        CK(vbusca(AJ_FILE) != 0); }
    /* ---- 9. ventana estrecha: el lateral se esconde */
    { struct cc_conv *c = cc_por_id(&g_h, 1); CK(c != 0); abre_conv(1); }
    WW = 600; HH = 760; g_pegado = 1; foto("21_estrecha"); CK(!hay(A_CONV));
    WW = 1280; HH = 820;
    /* ---- 10. Esc: para la respuesta en marcha (SOMA_286_YOUTUBE: al momento, sin esperar al servidor); lo que llegue despues se tira */
    escribe("una mas"); tecla('\n'); CK(g_turno == T_LLM); { uint32_t nm0 = cur()->nm; CK(chat_escape()); CK(g_w_cancel == 1 && g_turno == T_NO && cur()->nm == nm0 + 1 && cur()->m[nm0].estado == CC_CORTADA);
    llm_una(); avanza(100); CK(g_turno == T_NO && g_w_estado == 0 && cur()->nm == nm0 + 1); }
    CK(!chat_escape());
    /* ---- 11. pegar un texto largo */
    { char *big = malloc(6000); for (int i = 0; i < 5999; i++) big[i] = (char)('a' + i % 26); big[5999] = 0; clip_set_text(big, 0); g_foco = &f_comp; tf_set(&f_comp, ""); chat_clip_op(CLIP_OP_PASTE); CK(tf_len(&f_comp) == (int)strlen(g_clip)); free(big); tf_set(&f_comp, ""); }
    /* ---- 12. muchos mensajes y anchos: nada se rompe */
    {   struct cc_conv *c = cc_nueva(&g_h, (uint32_t)g_min, CC_MODO_LLM, 1); g_cur = c->id;
        for (int i = 0; i < 60; i++) { const char *t = i & 1 ? RESP_REL : "pregunta corta"; cc_anade(c, i & 1, t, (uint32_t)strlen(t), 0, (uint32_t)g_min); }
        for (WW = 420; WW <= 1600; WW += 170) { pinta(); for (g_scroll = 0; g_scroll < g_max_scroll; g_scroll += 1500) { g_pegado = 0; pinta(); } }
        WW = 1280; CK(1); }
    /* ---- 13. SOMA_286_YOUTUBE: rueda en el sentido de mouse.c (positivo = arriba) y barras que se arrastran */
    {   WW = 1280; g_pegado = 0; g_scroll = 0; pinta();
        int cx = g_mx0 + g_mw0 / 2, cy = (g_view_y0 + g_view_y1) / 2;
        chat_mouse(cx, cy, 0, 0, (uint32_t)WW, (uint32_t)HH, -3, 0); CK(g_scroll == 270);
        chat_mouse(cx, cy, 0, 0, (uint32_t)WW, (uint32_t)HH, 3, 0); CK(g_scroll == 0);
        pinta(); int k = -1; for (int i = 0; i < nH; i++) if (H[i].act == A_SCROLLBAR) k = i;
        CK(k >= 0 && H[k].x + H[k].w <= g_mx0 + g_mw0 - 6);                          /* lejos del borde de la ventana (redimensionado) */
        int bx = H[k].x + H[k].w / 2;
        chat_mouse(bx, g_view_y1 - 6, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); chat_handle_click(bx, g_view_y1 - 6, 0, 0, (uint32_t)WW, (uint32_t)HH);
        CK(g_arrastra == &g_b_main && g_scroll > g_max_scroll * 9 / 10);
        chat_mouse(bx, g_view_y0, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 1); CK(g_scroll < g_max_scroll / 20);
        chat_mouse(bx, g_view_y0 + 200, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 1); CK(g_scroll > 0);
        chat_mouse(bx, g_view_y0 + 200, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); CK(g_arrastra == 0);
        /* la lista lateral tambien */
        for (int q = 0; q < 30; q++) { struct cc_conv *c = cc_nueva(&g_h, (uint32_t)g_min - (uint32_t)q * 1440, CC_MODO_LLM, 1); cc_anade(c, CC_USER, "relleno", 7, 0, c->t0); cc_titulo_auto(c); }
        pinta(); k = -1; for (int i = 0; i < nH; i++) if (H[i].act == A_SSCROLL) k = i;
        CK(k >= 0 && g_smax > 0);
        if (k >= 0) { chat_handle_click(H[k].x + 4, H[k].y + H[k].h - 3, 0, 0, (uint32_t)WW, (uint32_t)HH); CK(g_sscroll > g_smax * 9 / 10); chat_mouse(H[k].x + 4, H[k].y + H[k].h - 3, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); }
        chat_mouse(100, 400, 0, 0, (uint32_t)WW, (uint32_t)HH, -2, 0); }
    printf("test_chatapp_285: %d comprobaciones, %d fallos, %d capturas\n", checks, fails, g_shots);
    if (fails) { printf("HAY FALLOS\n"); return 1; }
    printf("TODO BIEN\n");
    return 0;
}
