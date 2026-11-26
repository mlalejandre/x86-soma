/* SOMA_337_NOTICIAS: la ventana de Noticias. Arriba las categorias elegidas (pestanas); a la izquierda los titulares (medio y
 * hace cuanto; un clic abre la noticia en el Navegador); a la derecha el resumen con IA, con sus citas [n] y sus fuentes, y
 * "Guardar en memoria". En una ventana estrecha, el resumen va en su propia pestana. "Ajustes" elige categorias, el lugar de las
 * noticias locales y canales RSS propios. Todo lo lento (descargar, leer, resumir, guardar) va en el hilo de noticias.c. */
#include "noticias_gui.h"
#include "noticias.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "chat_md.h"
#include "mem.h"
#include "idt.h"
#include "hora.h"
#include "zona.h"
#include "agenda_core.h"

#define C_WIN   0x000C111B
#define C_CARD  0x00111827
#define C_CARD2 0x000F1622
#define C_LINE  0x00212B3B
#define C_TXT   0x00E5EAF2
#define C_DIM   0x008592A8
#define C_FAINT 0x00566379
#define C_ACC   0x0038BDF8
#define C_SEL   0x001A3558
#define C_ROJO  0x00F87171
#define C_VERDE 0x004ADE80
#define C_AMBAR 0x00FBBF24

static int g_cat = -1, g_ini, g_ajustes, g_foco, g_panel;       /* g_panel (ventana estrecha): 0 titulares, 1 resumen */
static int g_scroll, g_rscroll, g_hover = -1;
static uint32_t g_ver = 1, g_visto; static uint64_t g_t_reloj;
static char g_msg[200]; static uint32_t g_msg_col = C_DIM;
static char g_url[NT_URL];
static char g_f_lugar[64], g_f_rss[NT_URL];
static struct nt_conf g_c;

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int64_t v) { char t[24]; int n = 0; if (v < 0) v = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23); char r[24]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
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
static void boton(int x, int y, int w, const char *s, int estilo)   /* 1 = principal, 0 = normal, -1 = apagado */
{
    uint32_t bg = estilo == 1 ? 0xFF0E7490u : estilo < 0 ? 0xFF161D2Au : 0xFF1E293Bu;
    fb_fill_rrect_aa(x, y, w, 30, 8, bg);
    if (estilo == 0) fb_stroke_rrect_aa(x, y, w, 30, 8, 0xFF334155u);
    tx(x + (w - tw(s, 12, 1)) / 2, y, 30, s, 12, estilo < 0 ? C_FAINT : 0x00FFFFFF, 1);
}
static void campo(int x, int y, int w, const char *s, int foco, const char *pista)
{
    fb_fill_rrect_aa(x, y, w, 30, 8, 0xFF0C1119u);
    fb_stroke_rrect_aa(x, y, w, 30, 8, foco ? (0xFF000000u | C_ACC) : 0xFF263142u);
    fb_set_clip(x + 8, y, w - 16, 30);
    int n = s_len(s), st = 0; while (st < n && tw(s + st, 12, 0) > w - 24) st++;
    if (n) tx(x + 10, y, 30, s + st, 12, C_TXT, 0); else if (!foco) tx(x + 10, y, 30, pista, 12, C_FAINT, 0);
    if (foco) fb_fill_rect((uint32_t)(x + 11 + (n ? tw(s + st, 12, 0) : 0)), (uint32_t)(y + 7), 1, 16, C_ACC);
    fb_reset_clip();
}
/* corta s en lineas de ancho w (por palabras); devuelve cuantas caben en max (las posiciones en ini/fin) */
static int parte(const char *s, int px, int bold, int w, int max, int *ini, int *fin)
{
    int n = s_len(s), k = 0, a = 0;
    while (a < n && k < max) {
        while (a < n && s[a] == ' ') a++;
        if (a >= n) break;
        int e = a, ult = -1;
        while (e < n) {
            int e2 = e + 1; while (e2 < n && ((unsigned char)s[e2] & 0xC0) == 0x80) e2++;
            if (twn(s + a, e2 - a, px, bold) > w) break;
            if (s[e] == ' ') ult = e;
            e = e2;
        }
        if (e < n && ult > a) e = ult;
        if (e == a) { e = a + 1; while (e < n && ((unsigned char)s[e] & 0xC0) == 0x80) e++; }
        ini[k] = a; fin[k] = e; k++; a = e;
    }
    if (k == max && a < n && k > 0) fin[k - 1] = -fin[k - 1] - 1;   /* marca: no cabe todo (puntos suspensivos) */
    return k;
}
static void lineas(int x, int y, int lh, const char *s, int px, int bold, uint32_t col, int w, int max)
{
    int ini[6], fin[6]; if (max > 6) max = 6;
    int k = parte(s, px, bold, w, max, ini, fin);
    for (int i = 0; i < k; i++) {
        int f = fin[i] < 0 ? -fin[i] - 1 : fin[i];
        if (fin[i] < 0) {                                         /* la ultima con "..." */
            char t[400]; int m = f - ini[i]; if (m > 390) m = 390;
            for (int j = 0; j < m; j++) t[j] = s[ini[i] + j];
            while (m > 0 && twn(t, m, px, bold) + tw("\342\200\246", px, bold) > w) { m--; while (m > 0 && ((unsigned char)t[m] & 0xC0) == 0x80) m--; }
            t[m] = 0; s_cat(t, "\342\200\246", sizeof t);
            tx(x, y + i * lh, lh, t, px, col, bold);
        } else txn(x, y + i * lh, lh, s + ini[i], f - ini[i], px, col, bold);
    }
}

/* ---------------------------------------------------------------- el resumen, con el maquetador del chat */
#define RM_OPS  12000
#define RM_POOL (320u * 1024u)
#define RM_ZON  400
static struct md_op *rm_ops; static char *rm_pool; static struct md_zone *rm_z; static void *rm_scr; static int rm_ok = -1;
static struct md_out rm_o; static uint32_t rm_ver = 0xFFFFFFFFu; static int rm_w, rm_cat = -1;
static char *rm_txt;
static int rm_fl(int fl) { return fl | WF_RAW | ((fl & WF_MONO) ? 0 : WF_LIB); }
static int rm_mw(const char *s, int n, int px, int fl, void *u) { (void)u; return wf_width(s, (uint32_t)n, px, rm_fl(fl)); }
static int rm_ma(int px, void *u) { (void)u; return wf_ascent(px); }
static int rm_md(int px, void *u) { (void)u; return wf_descent(px); }
static int rm_mi(const char *s, int n, int *w, int *h, void *u) { (void)s; (void)n; (void)w; (void)h; (void)u; return -1; }
static const struct md_metrics RM_MET = { rm_mw, rm_ma, rm_md, rm_mi, 0 };
static const struct md_theme RM_TEMA = {
    0x00E5E7EBu, 0x009CA3AFu, 0x00FFFFFFu, 0x007DD3FCu, 0x00283150u,
    0x00090D16u, 0x00131929u, 0x00222A44u, 0x00E6EDF3u, 0x001E2640u, 0x00F9A8D4u, 0x006366F1u, 0x00574A12u,
    0x00182036u, 0x000E1322u, 0x006366F1u,
    0x00FF7B72u, 0x00A5D6FFu, 0x0079C0FFu, 0x006E7681u, 0x00D2A8FFu, 0x00FFA657u, 0x00F87171u };
static int rm_mem(void)
{
    if (rm_ok >= 0) return rm_ok;
    rm_ok = 0;
    if (!has_wf()) return 0;
    rm_ops = (struct md_op *)kmalloc(RM_OPS * sizeof(struct md_op)); rm_pool = (char *)kmalloc(RM_POOL);
    rm_z = (struct md_zone *)kmalloc(RM_ZON * sizeof(struct md_zone)); rm_scr = kmalloc(MD_SCRATCH); rm_txt = (char *)kmalloc(24000);
    if (rm_ops && rm_pool && rm_z && rm_scr && rm_txt) { rm_ok = 1; return 1; }
    if (rm_ops) kfree(rm_ops);
    if (rm_pool) kfree(rm_pool);
    if (rm_z) kfree(rm_z);
    if (rm_scr) kfree(rm_scr);
    if (rm_txt) kfree(rm_txt);
    rm_ops = 0; rm_pool = 0; rm_z = 0; rm_scr = 0; rm_txt = 0;
    return 0;
}
static int rm_maqueta(int cat, uint32_t ver, int w)
{
    if (!rm_mem()) return 0;
    if (cat == rm_cat && ver == rm_ver && w == rm_w) return 1;
    uint32_t n = noticias_resumen(cat, rm_txt, 24000);
    rm_o.ops = rm_ops; rm_o.cap = RM_OPS; rm_o.pool = rm_pool; rm_o.poolcap = RM_POOL; rm_o.z = rm_z; rm_o.zcap = RM_ZON;
    md_layout(rm_txt, n, w, 14, &RM_MET, &RM_TEMA, &rm_o, rm_scr);
    rm_cat = cat; rm_ver = ver; rm_w = w;
    return 1;
}
static void rm_pinta(int ox, int oy, int y0, int y1)
{
    for (int i = 0; i < rm_o.nops; i++) {
        const struct md_op *p = &rm_o.ops[i];
        switch (p->type) {
        case MD_TEXT: { int y = oy + p->y; if (y + p->px < y0 || y - p->px > y1) break; wf_draw(ox + p->x, y, rm_o.pool + p->s, p->n, p->px, p->rgb & 0xFFFFFF, rm_fl(p->fl), 255); break; }
        case MD_RECT: { if (oy + p->y > y1 || oy + p->y + p->h < y0) break; uint32_t a = p->rgb >> 24; if (!a) a = 255; fb_fill_rrect_aa(ox + p->x, oy + p->y, p->w, p->h, p->px, (a << 24) | (p->rgb & 0xFFFFFF)); break; }
        case MD_STROKE: if (oy + p->y > y1 || oy + p->y + p->h < y0) break; fb_stroke_rrect_aa(ox + p->x, oy + p->y, p->w, p->h, p->px, 0xFF000000u | p->rgb); break;
        case MD_LINE: { int ya = oy + p->y / 16, yb = oy + (p->y + p->h) / 16; if ((ya < y0 && yb < y0) || (ya > y1 && yb > y1)) break; uint32_t a = p->rgb >> 24; if (!a) a = 255;
            fb_line_aa(ox * 16 + p->x, oy * 16 + p->y, ox * 16 + p->x + p->w, oy * 16 + p->y + p->h, p->px, (a << 24) | (p->rgb & 0xFFFFFF)); break; }
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- estado */
static int primera_cat(void) { for (int i = 0; i < NTC_N; i++) if (g_c.cats & (1u << i)) return i; return NTC_PORTADA; }
static void elige(int c) { g_cat = c; g_scroll = 0; g_rscroll = 0; g_hover = -1; g_msg[0] = 0; noticias_carga(c, 0); g_ver++; }
void noticias_gui_init(void)
{
    noticias_conf(&g_c);
    if (!g_ini) { g_ini = 1; s_cpy(g_f_lugar, g_c.lugar, sizeof g_f_lugar); }
    if (g_cat < 0 || !(g_c.cats & (1u << g_cat))) elige(primera_cat());
    else noticias_carga(g_cat, 0);
}
uint32_t noticias_gui_version(void) { return g_ver + noticias_version() * 7u; }
int noticias_gui_tick(void)
{
    uint32_t v = noticias_version(); uint64_t t = timer_get_uptime_ms();
    if (v != g_visto) { g_visto = v; g_ver++; return 1; }
    if (t - g_t_reloj > 30000u) { g_t_reloj = t; g_ver++; return 1; }   /* "hace 5 min" al dia */
    return 0;
}
const char *noticias_gui_url(void) { return g_url; }

/* ---------------------------------------------------------------- geometria */
struct lay { int x, y, w, h, tab_y, body_y, body_h, lx, lw, rx, rw, ancho, bt_act, bt_aj, item_h; };
static void lay(struct lay *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->tab_y = L->y + 62; L->body_y = L->tab_y + 46; L->body_h = L->y + L->h - 40 - L->body_y;
    L->ancho = L->w >= 980;
    L->item_h = 84;
    if (L->ancho) { L->lx = L->x + 16; L->lw = (L->w - 44) * 54 / 100; L->rx = L->lx + L->lw + 12; L->rw = L->x + L->w - 16 - L->rx; }
    else { L->lx = L->x + 16; L->lw = L->w - 32; L->rx = L->lx; L->rw = L->lw; }
    L->bt_aj = L->x + L->w - 16 - 100; L->bt_act = L->bt_aj - 10 - 110;
}
static int tab_x(const struct lay *L, int i, int *w)        /* pestana de la categoria i (activa) */
{
    int x = L->x + 16;
    for (int c = 0; c < NTC_N; c++) {
        if (!(g_c.cats & (1u << c))) continue;
        int tw_ = tw(nt_cat_nombre(c), 12, 1) + 28;
        if (c == i) { *w = tw_; return x; }
        x += tw_ + 8;
    }
    *w = 0; return -1;
}

/* ---------------------------------------------------------------- pintar */
static void pinta_lista(const struct lay *L)
{
    struct nt_vista v; noticias_vista(g_cat, &v);
    int y0 = L->body_y, y1 = L->body_y + L->body_h;
    fb_fill_rrect_aa(L->lx, y0, L->lw, L->body_h, 12, 0xFF000000u | C_CARD2);
    if (v.estado == NTE_CARGANDO && !v.n) { tx(L->lx + 20, y0 + 20, 24, "Cargando titulares\342\200\246", 14, C_DIM, 0); return; }
    if (!v.n) {
        tx(L->lx + 20, y0 + 20, 24, v.estado == NTE_ERROR ? "No se han podido cargar las noticias" : "Sin noticias todav\303\255a", 14, v.estado == NTE_ERROR ? C_ROJO : C_DIM, 1);
        if (v.err[0]) lineas(L->lx + 20, y0 + 50, 20, v.err, 12, 0, C_DIM, L->lw - 40, 3);
        return;
    }
    int max = v.n * L->item_h - L->body_h + 12; if (max < 0) max = 0;
    if (g_scroll > max) g_scroll = max;
    if (g_scroll < 0) g_scroll = 0;
    fb_set_clip(L->lx, y0 + 2, L->lw, L->body_h - 4);
    int64_t ya = noticias_ahora();
    for (int i = 0; i < v.n; i++) {
        int y = y0 + 8 + i * L->item_h - g_scroll;
        if (y + L->item_h < y0 || y > y1) continue;
        struct nt_item x; if (!noticias_item(g_cat, i, &x)) break;
        if (i == g_hover) fb_fill_rrect_aa(L->lx + 6, y, L->lw - 12, L->item_h - 6, 9, 0xFF16213Au);
        char n[8]; n[0] = 0; s_num(n, sizeof n, i + 1);
        tx(L->lx + 14, y + 6, 22, n, 11, C_FAINT, 1);
        int tx0 = L->lx + 40, tw0 = L->lw - 56;
        lineas(tx0, y + 4, 21, x.tit, 14, 1, C_TXT, tw0, 2);
        char m[120]; m[0] = 0; s_cat(m, x.fuente, sizeof m);
        char h[24]; nt_hace(ya, x.t, h, sizeof h);
        if (h[0]) { s_cat(m, "  \302\267  ", sizeof m); s_cat(m, h, sizeof m); }
        tx(tx0, y + 48, 18, m, 11, C_ACC, 0);
        if (x.desc[0]) lineas(tx0, y + 64, 16, x.desc, 11, 0, C_FAINT, tw0, 1);
        fb_fill_rect((uint32_t)(L->lx + 14), (uint32_t)(y + L->item_h - 4), (uint32_t)(L->lw - 28), 1, C_LINE);
    }
    fb_reset_clip();
    if (max > 0) {                                                /* barra de desplazamiento */
        int bh = L->body_h * L->body_h / (L->body_h + max); if (bh < 30) bh = 30;
        int by = y0 + (L->body_h - bh) * g_scroll / max;
        fb_fill_rrect_aa(L->lx + L->lw - 7, by, 4, bh, 2, 0x90475569u);
    }
}
static void pinta_resumen(const struct lay *L)
{
    struct nt_vista v; noticias_vista(g_cat, &v);
    int x = L->rx, y0 = L->body_y, w = L->rw, h = L->body_h;
    fb_fill_rrect_aa(x, y0, w, h, 12, 0xFF000000u | C_CARD);
    tx(x + 18, y0 + 12, 24, "Resumen con IA", 15, C_TXT, 1);
    int bx = x + w - 16;
    int b1 = tw("Guardar en memoria", 12, 1) + 26; bx -= b1; boton(bx, y0 + 10, b1, "Guardar en memoria", v.res_estado == NTE_BIEN ? 0 : -1);
    int b2 = tw(v.res_estado == NTE_BIEN ? "Rehacer" : "Resumir", 12, 1) + 26; bx -= b2 + 8;
    boton(bx, y0 + 10, b2, v.res_estado == NTE_BIEN ? "Rehacer" : "Resumir", v.res_estado == NTE_CARGANDO || !v.n ? -1 : 1);
    int cy = y0 + 52, ch = h - 60;
    fb_fill_rect((uint32_t)(x + 14), (uint32_t)(cy - 6), (uint32_t)(w - 28), 1, C_LINE);
    if (v.res_estado == NTE_CARGANDO) {
        uint32_t t = (uint32_t)(timer_get_uptime_ms() / 400) % 4; char s[48]; s_cpy(s, "El modelo est\303\241 leyendo los titulares", sizeof s); for (uint32_t k = 0; k < t; k++) s_cat(s, ".", sizeof s);
        tx(x + 18, cy + 4, 24, s, 13, C_DIM, 0);
        return;
    }
    if (v.res_estado == NTE_ERROR) { tx(x + 18, cy + 4, 24, "No se pudo resumir", 13, C_ROJO, 1); lineas(x + 18, cy + 30, 20, v.res_err, 12, 0, C_DIM, w - 36, 4); return; }
    if (v.res_estado != NTE_BIEN) {
        lineas(x + 18, cy + 4, 22, v.n ? "Pulsa Resumir: el modelo de lenguaje lee los titulares de esta categoria y escribe lo m\303\241s importante, citando cada medio."
                                     : "Cuando lleguen los titulares, podras pedir un resumen.", 13, 0, C_DIM, w - 36, 4);
        return;
    }
    if (!rm_maqueta(g_cat, v.res_ver, w - 40)) { tx(x + 18, cy + 4, 24, "(sin fuente vectorial: mira el resumen con la orden noticias resumen)", 12, C_DIM, 0); return; }
    int max = rm_o.height - ch + 16; if (max < 0) max = 0;
    if (g_rscroll > max) g_rscroll = max;
    if (g_rscroll < 0) g_rscroll = 0;
    fb_set_clip(x + 4, cy, w - 8, ch);
    rm_pinta(x + 20, cy + 6 - g_rscroll, cy, cy + ch);
    fb_reset_clip();
    if (max > 0) { int bh = ch * ch / (ch + max); if (bh < 30) bh = 30; int by = cy + (ch - bh) * g_rscroll / max; fb_fill_rrect_aa(x + w - 8, by, 4, bh, 2, 0x90475569u); }
}
static int aj_x, aj_y, aj_w, aj_h;
static void pinta_ajustes(const struct lay *L)
{
    aj_w = L->w - 80 > 760 ? 760 : L->w - 80; aj_h = 440;
    aj_x = L->x + (L->w - aj_w) / 2; aj_y = L->y + 70;
    fb_fill_rect_alpha(L->x, L->y, L->w, L->h, 0xA0000000u);
    fb_fill_rrect_aa(aj_x, aj_y, aj_w, aj_h, 14, 0xFF111827u);
    fb_stroke_rrect_aa(aj_x, aj_y, aj_w, aj_h, 14, 0xFF334155u);
    tx(aj_x + 24, aj_y + 16, 28, "Ajustes de Noticias", 17, C_TXT, 1);
    tx(aj_x + 24, aj_y + 54, 20, "Categor\303\255as que quieres ver", 12, C_DIM, 1);
    int cw = (aj_w - 48 - 4 * 8) / 5;
    for (int c = 0; c < NTC_N; c++) {
        int cx = aj_x + 24 + (c % 5) * (cw + 8), cy = aj_y + 80 + (c / 5) * 40; int on = (g_c.cats >> c) & 1;
        fb_fill_rrect_aa(cx, cy, cw, 32, 8, on ? 0xFF14315Au : 0xFF0C1119u);
        fb_stroke_rrect_aa(cx, cy, cw, 32, 8, on ? (0xFF000000u | C_ACC) : 0xFF263142u);
        fb_fill_rrect_aa(cx + 10, cy + 10, 12, 12, 3, on ? (0xFF000000u | C_ACC) : 0xFF1E293Bu);
        tx(cx + 30, cy, 32, nt_cat_nombre(c), 12, on ? C_TXT : C_DIM, on);
    }
    tx(aj_x + 24, aj_y + 172, 20, "Lugar de las noticias locales (ciudad, provincia o regi\303\263n)", 12, C_DIM, 1);
    campo(aj_x + 24, aj_y + 196, aj_w - 48 - 110, g_f_lugar, g_foco == 1, "Por ejemplo: Valencia");
    boton(aj_x + aj_w - 24 - 100, aj_y + 196, 100, "Guardar", 1);
    tx(aj_x + 24, aj_y + 244, 20, "Canales RSS propios (salen en Portada)", 12, C_DIM, 1);
    for (int i = 0; i < g_c.nextra && i < 4; i++) {
        int ry = aj_y + 268 + i * 26;
        fb_set_clip(aj_x + 24, ry, aj_w - 48 - 70, 24); tx(aj_x + 24, ry, 24, g_c.extra[i], 11, C_TXT, 0); fb_reset_clip();
        tx(aj_x + aj_w - 24 - 56, ry, 24, "quitar", 11, C_ROJO, 1);
    }
    int fy = aj_y + 268 + (g_c.nextra < 4 ? g_c.nextra : 4) * 26 + 4;
    if (g_c.nextra < NT_EXTRA) { campo(aj_x + 24, fy, aj_w - 48 - 110, g_f_rss, g_foco == 2, "https://.../rss.xml"); boton(aj_x + aj_w - 24 - 100, fy, 100, "A\303\261adir", 0); }
    boton(aj_x + aj_w - 24 - 110, aj_y + aj_h - 46, 110, "Cerrar", 0);
}
void noticias_gui_render(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused)
{
    (void)focused;
    if (!g_ini || g_cat < 0) noticias_gui_init();
    struct lay L; lay(&L, x, y, w, h);
    fb_fill_rect(x, y, w, h, C_WIN);
    tx(L.x + 18, L.y + 12, 28, "Noticias", 20, C_TXT, 1);
    {   struct rtc_time t; hora_local(&t); int64_t d = zona_dias(t.year, t.month, t.day);
        char s[80]; s[0] = 0; s_cat(s, ag_nombre_dia(zona_dia_semana(d)), sizeof s); s_cat(s, ", ", sizeof s); s_num(s, sizeof s, t.day); s_cat(s, " de ", sizeof s); s_cat(s, ag_nombre_mes(t.month), sizeof s);
        if (g_c.lugar[0] && (g_c.cats & (1u << NTC_LOCAL))) { s_cat(s, "  \302\267  ", sizeof s); s_cat(s, g_c.lugar, sizeof s); }
        tx(L.x + 128, L.y + 16, 24, s, 12, C_DIM, 0); }
    struct nt_vista v; noticias_vista(g_cat, &v);
    boton(L.bt_act, L.y + 14, 110, v.estado == NTE_CARGANDO ? "Cargando\342\200\246" : "Actualizar", v.estado == NTE_CARGANDO ? -1 : 0);
    boton(L.bt_aj, L.y + 14, 100, "Ajustes", 0);
    /* pestanas */
    for (int c = 0; c < NTC_N; c++) {
        int wt; int xt = tab_x(&L, c, &wt); if (xt < 0) continue;
        if (xt + wt > L.x + L.w - 16 - (L.ancho ? 0 : 130)) break;
        int on = c == g_cat && !(g_panel && !L.ancho);
        fb_fill_rrect_aa(xt, L.tab_y, wt, 32, 16, on ? 0xFF0E7490u : 0xFF151D2Bu);
        tx(xt + 14, L.tab_y, 32, nt_cat_nombre(c), 12, on ? 0x00FFFFFF : C_DIM, 1);
    }
    if (!L.ancho) {                                               /* ventana estrecha: el resumen en su pestana */
        int wt = tw("Resumen IA", 12, 1) + 28, xt = L.x + L.w - 16 - wt;
        fb_fill_rrect_aa(xt, L.tab_y, wt, 32, 16, g_panel ? 0xFF6D28D9u : 0xFF151D2Bu);
        tx(xt + 14, L.tab_y, 32, "Resumen IA", 12, g_panel ? 0x00FFFFFF : C_DIM, 1);
    }
    if (L.ancho) { pinta_lista(&L); pinta_resumen(&L); }
    else if (g_panel) pinta_resumen(&L);
    else pinta_lista(&L);
    /* pie */
    {   char s[200]; s[0] = 0; uint32_t col = g_msg_col;
        if (v.guarda_estado == NTE_CARGANDO) { s_cpy(s, "Guardando en el cartucho de memoria Noticias\342\200\246", sizeof s); col = C_DIM; }
        else if (v.guarda_estado != NTE_NADA && v.guarda_msg[0] && !g_msg[0]) { s_cpy(s, v.guarda_msg, sizeof s); col = v.guarda_estado == NTE_BIEN ? C_VERDE : C_ROJO; }
        else if (g_msg[0]) s_cpy(s, g_msg, sizeof s);
        else if (v.n) { s_num(s, sizeof s, v.n); s_cat(s, " titulares de Google Noticias", sizeof s); if (v.err[0]) { s_cat(s, "  \302\267  ", sizeof s); s_cat(s, v.err, sizeof s); }
                        s_cat(s, "  \302\267  un clic abre la noticia en el Navegador", sizeof s); }
        if (!g_msg[0] && v.guarda_estado == NTE_NADA) col = C_FAINT;
        fb_set_clip(L.x + 16, L.y + L.h - 34, L.w - 32, 28); tx(L.x + 18, L.y + L.h - 34, 28, s, 11, col, 0); fb_reset_clip(); }
    if (g_ajustes) pinta_ajustes(&L);
}

/* ---------------------------------------------------------------- raton y teclado */
static void guarda_lugar(void)
{
    s_cpy(g_c.lugar, g_f_lugar, sizeof g_c.lugar);
    if (g_c.lugar[0]) g_c.cats |= 1u << NTC_LOCAL;
    noticias_pon_conf(&g_c);
    s_cpy(g_msg, g_c.lugar[0] ? "Lugar guardado: la pesta\303\261a Local trae noticias de all\303\255." : "Lugar borrado.", sizeof g_msg); g_msg_col = C_VERDE;
    if (g_cat == NTC_LOCAL) noticias_carga(NTC_LOCAL, 1);
}
static void anade_rss(void)
{
    if (!(g_f_rss[0] == 'h' && g_f_rss[1] == 't' && g_f_rss[2] == 't' && g_f_rss[3] == 'p')) { s_cpy(g_msg, "La direcci\303\263n del canal empieza por http:// o https://", sizeof g_msg); g_msg_col = C_ROJO; return; }
    if (g_c.nextra >= NT_EXTRA) return;
    s_cpy(g_c.extra[g_c.nextra], g_f_rss, NT_URL); g_c.nextra++;
    noticias_pon_conf(&g_c); g_f_rss[0] = 0;
    s_cpy(g_msg, "Canal a\303\261adido: sale en Portada.", sizeof g_msg); g_msg_col = C_VERDE;
    if (g_cat == NTC_PORTADA) noticias_carga(NTC_PORTADA, 1);
}
static int click_ajustes(const struct lay *L, int mx, int my)
{
    (void)L;
    if (mx < aj_x || mx >= aj_x + aj_w || my < aj_y || my >= aj_y + aj_h) { g_ajustes = 0; g_foco = 0; return 1; }
    int cw = (aj_w - 48 - 4 * 8) / 5;
    for (int c = 0; c < NTC_N; c++) {
        int cx = aj_x + 24 + (c % 5) * (cw + 8), cy = aj_y + 80 + (c / 5) * 40;
        if (mx >= cx && mx < cx + cw && my >= cy && my < cy + 32) {
            uint32_t m = g_c.cats ^ (1u << c); if (!m) return 1;     /* al menos una */
            g_c.cats = m; noticias_pon_conf(&g_c);
            if (!(g_c.cats & (1u << g_cat))) elige(primera_cat());
            return 1;
        }
    }
    if (my >= aj_y + 196 && my < aj_y + 226) {
        if (mx >= aj_x + aj_w - 124) { guarda_lugar(); g_foco = 0; } else g_foco = 1;
        return 1;
    }
    for (int i = 0; i < g_c.nextra && i < 4; i++) {
        int ry = aj_y + 268 + i * 26;
        if (my >= ry && my < ry + 24 && mx >= aj_x + aj_w - 24 - 60) {
            for (int k = i; k + 1 < g_c.nextra; k++) s_cpy(g_c.extra[k], g_c.extra[k + 1], NT_URL);
            g_c.nextra--; noticias_pon_conf(&g_c); return 1;
        }
    }
    int fy = aj_y + 268 + (g_c.nextra < 4 ? g_c.nextra : 4) * 26 + 4;
    if (g_c.nextra < NT_EXTRA && my >= fy && my < fy + 30) { if (mx >= aj_x + aj_w - 124) { anade_rss(); g_foco = 0; } else g_foco = 2; return 1; }
    if (my >= aj_y + aj_h - 46 && my < aj_y + aj_h - 16 && mx >= aj_x + aj_w - 134) { g_ajustes = 0; g_foco = 0; return 1; }
    g_foco = 0;
    return 1;
}
int noticias_gui_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct lay L; lay(&L, x, y, w, h);
    g_ver++;
    if (g_ajustes) return click_ajustes(&L, mx, my);
    g_msg[0] = 0;
    if (my >= L.y + 14 && my < L.y + 44) {
        if (mx >= L.bt_act && mx < L.bt_act + 110) { noticias_carga(g_cat, 1); return 1; }
        if (mx >= L.bt_aj && mx < L.bt_aj + 100) { noticias_conf(&g_c); s_cpy(g_f_lugar, g_c.lugar, sizeof g_f_lugar); g_ajustes = 1; g_foco = 0; return 1; }
    }
    if (my >= L.tab_y && my < L.tab_y + 32) {
        if (!L.ancho) { int wt = tw("Resumen IA", 12, 1) + 28; if (mx >= L.x + L.w - 16 - wt) { g_panel = 1; return 1; } }
        for (int c = 0; c < NTC_N; c++) { int wt; int xt = tab_x(&L, c, &wt); if (xt >= 0 && mx >= xt && mx < xt + wt) { g_panel = 0; if (c != g_cat) elige(c); return 1; } }
        return 1;
    }
    int en_res = L.ancho ? (mx >= L.rx) : g_panel;
    if (en_res && my >= L.body_y + 10 && my < L.body_y + 40) {    /* botones del resumen */
        struct nt_vista v; noticias_vista(g_cat, &v);
        int bx = L.rx + L.rw - 16, b1 = tw("Guardar en memoria", 12, 1) + 26; bx -= b1;
        if (mx >= bx && mx < bx + b1) {
            if (v.res_estado != NTE_BIEN || v.guarda_estado == NTE_CARGANDO) return 1;
            noticias_guarda_pide(g_cat);
            return 1;
        }
        int b2 = tw(v.res_estado == NTE_BIEN ? "Rehacer" : "Resumir", 12, 1) + 26; bx -= b2 + 8;
        if (mx >= bx && mx < bx + b2 && v.n && v.res_estado != NTE_CARGANDO) { noticias_resume(g_cat); return 1; }
        return 1;
    }
    if (!en_res && mx >= L.lx && mx < L.lx + L.lw && my >= L.body_y && my < L.body_y + L.body_h) {
        int i = (my - L.body_y - 8 + g_scroll) / L.item_h;
        struct nt_item it; if (i >= 0 && noticias_item(g_cat, i, &it) && it.url[0]) { s_cpy(g_url, it.url, sizeof g_url); return 3; }
    }
    return 1;
}
int noticias_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel)
{
    struct lay L; lay(&L, x, y, w, h);
    if (g_ajustes) return 0;
    int r = 0, en_res = L.ancho ? (mx >= L.rx) : g_panel;
    if (wheel && my >= L.body_y && my < L.body_y + L.body_h) {
        if (en_res) g_rscroll += wheel * 40; else g_scroll += wheel * 42;
        r = 1;
    }
    int hv = -1;
    if (!en_res && mx >= L.lx && mx < L.lx + L.lw && my >= L.body_y && my < L.body_y + L.body_h) {
        int i = (my - L.body_y - 8 + g_scroll) / L.item_h; struct nt_vista v; noticias_vista(g_cat, &v); if (i >= 0 && i < v.n) hv = i;
    }
    if (hv != g_hover) { g_hover = hv; r = 1; }
    if (r) g_ver++;
    return r;
}
int noticias_gui_key(char c)
{
    g_ver++;
    if (!g_ajustes) {
        if (c == 'r' || c == 'R') { noticias_carga(g_cat, 1); return 1; }
        return 0;
    }
    if (c == 27) { if (g_foco) g_foco = 0; else g_ajustes = 0; return 1; }
    char *f = g_foco == 1 ? g_f_lugar : g_foco == 2 ? g_f_rss : 0; int cap = g_foco == 1 ? (int)sizeof g_f_lugar : (int)sizeof g_f_rss;
    if (!f) return 1;
    int n = s_len(f);
    if (c == '\n' || c == '\r') { if (g_foco == 1) guarda_lugar(); else anade_rss(); g_foco = 0; return 1; }
    if (c == '\t') { g_foco = g_foco == 1 ? 2 : 1; return 1; }
    if (c == 8 || c == 127) { while (n > 0 && ((unsigned char)f[n - 1] & 0xC0) == 0x80) n--; if (n > 0) n--; f[n] = 0; return 1; }
    if ((unsigned char)c >= 32 && n + 1 < cap) { f[n] = c; f[n + 1] = 0; }
    return 1;
}
