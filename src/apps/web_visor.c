/* SOMA_289_VISOR: ver web_visor.h */
#include "web_visor.h"
#include "web_tipo.h"
#include "web_net.h"
#include "pdfview.h"
#include "tex.h"
#include "fb.h"
#include "fs.h"
#include "mem.h"
#include "web_font.h"
#include "../ui/clipboard.h"

uint64_t timer_get_uptime_ms(void);
void gui_open_layout_file(const char *path);

#define WV_MAX_DOC   (48u << 20)         /* lo mas grande que se descarga */
#define WV_TEX_OPS   40000
#define WV_TEX_POOL  (384u * 1024u)

#define KC_BAR   0x00131A27
#define KC_LINE  0x00212B3B
#define KC_DESK  0x0020262F
#define KC_CARD  0x00111827
#define KC_TXT   0x00E5EAF2
#define KC_DIM   0x008592A8
#define KC_FAINT 0x00566379
#define KC_ACC   0x0038BDF8
#define KC_RED   0x00F87171
#define KC_BTN   0x00182133
#define KC_SEL   0x001A3558
#define BAR_H 44
#define SB_W 12

enum { E_NADA = 0, E_BAJANDO, E_LISTO, E_ABIERTO, E_ERROR };

/* lo que pide la GUI y lo que deja el trabajador (gen distingue peticiones: una respuesta vieja se descarta) */
static volatile int s_estado, s_tipo;
static volatile uint32_t s_gen, s_gen_bajada;
static char s_url[WN_URL_MAX], s_url_bajar[WN_URL_MAX];
static char s_nombre[160], s_ruta[200], s_err[160];
static uint8_t *volatile s_dato; static volatile uint32_t s_len;
static volatile int s_cancelar;
static uint64_t s_huella_gen;

/* PDF */
static int s_slot;
/* LaTeX */
static char *s_src; static uint32_t s_src_len;
static struct tex_op *s_ops; static char *s_pool; static struct tex_out s_out;
static int s_px = 16, s_hecho_w = -1, s_hecho_px = -1, s_codigo, s_scroll, s_alto = 1, s_vista_h = 1, s_drag, s_drag_y, s_drag_s, s_hover = -1;
static int s_lineas = 1;

static int s_len_(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, uint32_t max) { uint32_t i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } if (max) d[i] = 0; }
static void s_cat(char *d, const char *s, uint32_t max) { uint32_t i = (uint32_t)s_len_(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; if (max) d[i] = 0; }
static void s_num(char *d, uint32_t max, uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int s_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void toca(void) { s_huella_gen++; }

/* ---------------------------------------------------------------- estado */
static void libera_tex(void)
{
    if (s_src) kfree(s_src);
    s_src = 0; s_src_len = 0; s_hecho_w = -1; s_scroll = 0; s_codigo = 0; s_lineas = 1;
}
void wv_cierra(void)
{
    s_gen++; s_cancelar = 1;                                   /* una descarga en curso ya no vale */
    if (s_slot) { int ant = pdfview_selected(); pdfview_inst_drop(s_slot); if (ant != s_slot) pdfview_select(ant); s_slot = 0; }
    libera_tex();
    uint8_t *d = s_dato; s_dato = 0; s_len = 0; if (d) kfree(d);
    s_estado = E_NADA; s_tipo = WT_NADA; s_url[0] = 0; s_err[0] = 0; toca();
}
int wv_activo(void) { return s_estado != E_NADA; }
const char *wv_titulo(void)
{
    if (!wv_activo()) return "";
    if (s_tipo == WT_PDF && s_slot) { int ant = pdfview_selected(); pdfview_select(s_slot); const char *t = pdfview_doc_title(); static char b[96]; s_cpy(b, t && t[0] ? t : s_nombre, sizeof b); pdfview_select(ant); return b; }
    if (s_tipo == WT_TEX && s_out.title[0] && s_estado == E_ABIERTO) return s_out.title;
    return s_nombre;
}
int wv_sync(const char *titulo, const char *url)
{
    int t = wt_marca_tipo(titulo);
    if (t) {   /* el titulo es de la pagina marcador de ESTA direccion (al navegar, la URL cambia un momento antes que el titulo) */
        char nu[160]; wt_nombre(url, nu, sizeof nu); const char *nt = wt_marca_nombre(titulo); int k = 0;
        while (nt[k] && nt[k] == nu[k]) k++;
        if (nt[k]) t = WT_NADA;
    }
    if (!t) { if (s_estado != E_NADA) wv_cierra(); return 0; }
    if (s_estado != E_NADA && s_tipo == t && s_eq(s_url, url)) return 0;
    wv_cierra();
    s_tipo = t; s_cpy(s_url, url, sizeof s_url); wt_nombre(url, s_nombre, sizeof s_nombre);
    s_cpy(s_url_bajar, url, sizeof s_url_bajar); s_gen_bajada = s_gen;
    s_cancelar = 0; s_estado = E_BAJANDO; toca();
    return 1;
}

/* ---------------------------------------------------------------- descarga (hilo trabajador) */
static void falla(uint32_t gen, const char *m) { if (gen != s_gen) return; s_cpy(s_err, m, sizeof s_err); s_estado = E_ERROR; toca(); }
void wv_descarga(void)
{
    uint32_t gen = s_gen_bajada; if (gen != s_gen || s_estado != E_BAJANDO) return;
    char url[WN_URL_MAX]; s_cpy(url, s_url_bajar, sizeof url);
    int tipo = s_tipo;
    uint8_t *d = 0; uint32_t n = 0;
    char ruta[200];
    if (wt_ruta_local(url, ruta, sizeof ruta)) {
        int sz = vfs_size(ruta);
        if (sz <= 0) { falla(gen, "No se encuentra el archivo en el disco"); return; }
        if ((uint32_t)sz > WV_MAX_DOC) { falla(gen, "El archivo es demasiado grande para el visor (mas de 48 MB)"); return; }
        d = (uint8_t *)vfs_read_alloc(ruta, &n);
        if (!d) { falla(gen, "No se pudo leer el archivo"); return; }
        s_cpy(s_ruta, ruta, sizeof s_ruta);
    } else {
        static struct wn_resp rs; struct wn_req rq;
        for (uint32_t i = 0; i < sizeof rq; i++) ((char *)&rq)[i] = 0;
        rq.url = url; rq.timeout_ms = 90000; rq.max_body = WV_MAX_DOC; rq.want_binary = 1;
        rq.accept = tipo == WT_PDF ? "application/pdf,*/*;q=0.5" : "application/x-tex,text/plain,*/*;q=0.5";
        for (uint32_t i = 0; i < sizeof rs; i++) ((char *)&rs)[i] = 0;
        rq.cancel = &s_cancelar;                               /* al cerrar el visor (otra pagina, otra pestana) se corta */
        /* SOMA_317_DESCARGAS_AISLADAS: con netsvc (ring 3, un proceso por descarga): el HTTP y el TLS del servidor, fuera del nucleo */
        if (wn_fetch_isolated(&rq, &rs) != 0 || !rs.body) {
            char m[160] = "No se pudo descargar: "; s_cat(m, rs.err[0] ? rs.err : "error de red", sizeof m); wn_free_isolated(&rs); falla(gen, m); return;
        }
        if (rs.status >= 400) { char m[64] = "El servidor respondio HTTP "; s_num(m, sizeof m, (uint32_t)rs.status); wn_free_isolated(&rs); falla(gen, m); return; }
        d = (uint8_t *)rs.body; n = rs.len; rs.body = 0; wn_free_isolated(&rs);       /* el cuerpo pasa a ser nuestro */
        s_ruta[0] = 0;
    }
    if (tipo == WT_PDF && !wt_tipo_resp("", d, n)) {
        int html = 0; for (uint32_t i = 0; i + 5 < n && i < 512; i++) if (d[i] == '<' && ((d[i + 1] | 32) == 'h' || d[i + 1] == '!')) { html = 1; break; }
        kfree(d); falla(gen, html ? "El servidor no ha devuelto un PDF sino una pagina web (quiza pide iniciar sesion)." : "El archivo no es un PDF valido."); return;
    }
    if (tipo == WT_TEX) {
        if (wt_parece_html(d, n)) { kfree(d); falla(gen, "El servidor ha devuelto una pagina web, no el archivo .tex."); return; }
        for (uint32_t i = 0; i < n && i < 4096; i++) if (d[i] == 0) { kfree(d); falla(gen, "El archivo no es texto LaTeX."); return; }
    }
    if (gen != s_gen) { kfree(d); return; }
    s_dato = d; s_len = n; s_estado = E_LISTO; toca();
}

/* ---------------------------------------------------------------- LaTeX: medidas y maquetacion */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int m_width(const char *s, int n, int px, int st, void *u)
{
    (void)u; if (!has_wf()) return n * 8;
    return wf_width(s, (uint32_t)n, px, (st & TS_BOLD ? WF_BOLD : 0) | (st & TS_ITALIC ? WF_ITALIC : 0) | (st & TS_MONO ? WF_MONO : 0));
}
static int m_asc(int px, void *u) { (void)u; return has_wf() ? wf_ascent(wf_ui_px(px)) : px; }
static int m_desc(int px, void *u) { (void)u; return has_wf() ? wf_descent(wf_ui_px(px)) : px / 4; }
static const struct tex_metrics g_m = { m_width, m_asc, m_desc, 0, 0 };

static void tex_abre(void)
{
    uint8_t *d = s_dato; uint32_t n = s_len; s_dato = 0; s_len = 0;
    libera_tex();
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) { for (uint32_t i = 3; i < n; i++) d[i - 3] = d[i]; n -= 3; }   /* BOM */
    s_src = (char *)kmalloc(n + 1);
    if (!s_src) { kfree(d); s_cpy(s_err, "Sin memoria para el documento", sizeof s_err); s_estado = E_ERROR; return; }
    for (uint32_t i = 0; i < n; i++) s_src[i] = (char)d[i];
    s_src[n] = 0; s_src_len = n; kfree(d);
    s_lineas = 1; for (uint32_t i = 0; i < n; i++) if (s_src[i] == '\n') s_lineas++;
    if (!s_ops) s_ops = (struct tex_op *)kmalloc(sizeof(struct tex_op) * WV_TEX_OPS);
    if (!s_pool) s_pool = (char *)kmalloc(WV_TEX_POOL);
    if (!s_ops || !s_pool) { s_cpy(s_err, "Sin memoria para maquetar", sizeof s_err); s_estado = E_ERROR; return; }
    s_estado = E_ABIERTO;
}
static void tex_maqueta(int col_w)
{
    if (!s_src || !s_ops || !s_pool) return;
    if (col_w == s_hecho_w && s_px == s_hecho_px) return;
    s_out.ops = s_ops; s_out.nops = 0; s_out.cap = WV_TEX_OPS; s_out.pool = s_pool; s_out.npool = 0; s_out.poolcap = WV_TEX_POOL;
    s_out.height = 0; s_out.title[0] = 0; s_out.errors = 0;
    (void)tex_typeset(s_src, s_src_len, col_w, s_px, &g_m, &s_out);
    s_hecho_w = col_w; s_hecho_px = s_px; toca();
}

/* ---------------------------------------------------------------- dibujo */
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int fl)
{
    int n = s_len_(s); if (!n) return;
    if (has_wf()) wf_draw(x, top + (h + (px * 72) / 100) / 2, s, (uint32_t)n, px, col, fl, 255);
}
static int tw(const char *s, int px, int fl) { return has_wf() ? wf_width(s, (uint32_t)s_len_(s), px, fl) : s_len_(s) * 8; }

enum { B_DOC = 0, B_COD, B_MENOS, B_MAS, B_COPIA, B_MAQ, B_N };
struct btn { int x, w; const char *t; };
static int botones(int x, int w, struct btn *b)
{
    static const char *const T[B_N] = { "Documento", "C\303\263digo", "A\342\210\222", "A+", "Copiar c\303\263digo", "Abrir en Maquetaci\303\263n" };
    int cx = x + 12;
    for (int i = 0; i < B_N; i++) {
        int bw = tw(T[i], 13, 0) + 26; if (i == B_MENOS || i == B_MAS) bw = 38;
        if (i == B_MENOS || i == B_COPIA) cx += 10;
        b[i].x = cx; b[i].w = bw; b[i].t = T[i]; cx += bw + 6;
    }
    return cx < x + w ? B_N : (cx - b[B_MAQ].w < x + w ? B_MAQ : B_COPIA);   /* los que caben */
}
static void geom(int x, int y, int w, int h, int *ax, int *ay, int *aw, int *ah) { *ax = x; *ay = y + BAR_H; *aw = w - SB_W; *ah = h - BAR_H; if (*ah < 10) *ah = 10; if (*aw < 10) *aw = 10; }
static int max_scroll(void) { int m = s_alto - s_vista_h; return m > 0 ? m : 0; }
static void fija_scroll(int v) { int m = max_scroll(); if (v > m) v = m; if (v < 0) v = 0; if (v != s_scroll) { s_scroll = v; toca(); } }
static int papel_w(int aw) { int pw = aw - 48; if (pw > 900) pw = 900; if (pw < 300) pw = aw - 8 > 300 ? 300 : aw - 8; return pw; }

static void pinta_ops(int ox, int oy, int y0, int y1)
{
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s_out.nops; i++) {
            const struct tex_op *p = &s_ops[i];
            int bg = p->type == TOP_RECT && p->style == 1;
            if ((pass == 0) != bg) continue;
            if (p->type == TOP_TEXT) {
                int yy = oy + p->y; if (yy + p->px < y0 || yy - p->px > y1) continue;
                int fl = (p->style & TS_BOLD ? WF_BOLD : 0) | (p->style & TS_ITALIC ? WF_ITALIC : 0) | (p->style & TS_MONO ? WF_MONO : 0);
                if (has_wf()) wf_draw(ox + p->x, yy, s_pool + p->s, p->n, p->px, p->rgb, fl, 255);
            } else if (p->type == TOP_RECT) {
                int yy = oy + p->y; if (yy + p->h < y0 || yy > y1) continue;
                fb_fill_rect((uint32_t)(ox + p->x), (uint32_t)yy, (uint32_t)(p->w > 0 ? p->w : 1), (uint32_t)(p->h > 0 ? p->h : 1), p->rgb);
            } else if (p->type == TOP_LINE) {
                int yy = oy + p->y / 16; if (yy < y0 - 200 || yy > y1 + 200) continue;
                fb_line_aa(ox * 16 + p->x, oy * 16 + p->y, ox * 16 + p->x + p->w, oy * 16 + p->y + p->h, p->px, 0xFF000000u | p->rgb);
            } else if (p->type == TOP_IMAGE) {
                int yy = oy + p->y; if (yy + p->h < y0 || yy > y1) continue;
                fb_stroke_rrect_aa(ox + p->x, yy, p->w, p->h, 4, 0xFFD1D5DBu);           /* las imagenes del .tex no vienen con el documento */
                tx(ox + p->x + 8, yy, p->h, "imagen", 12, TEX_MUTED, WF_ITALIC);
            }
        }
}
static void pinta_codigo(int ax, int ay, int aw, int ah)
{
    int px = 13, lh = 19, gut = 52, y = ay + 12 - s_scroll;
    fb_fill_rect((uint32_t)ax, (uint32_t)ay, (uint32_t)aw, (uint32_t)ah, 0x000E1420);
    fb_fill_rect((uint32_t)ax, (uint32_t)ay, (uint32_t)gut, (uint32_t)ah, 0x000B1019);
    uint32_t i = 0; int linea = 1, mat = 0;
    while (i <= s_src_len) {
        uint32_t a = i; while (i < s_src_len && s_src[i] != '\n') i++;
        if (y + lh > ay && y < ay + ah) {
            char nb[12] = ""; s_num(nb, sizeof nb, (uint32_t)linea);
            if (has_wf()) wf_draw(ax + gut - 10 - wf_width(nb, (uint32_t)s_len_(nb), 11, WF_MONO), y + 14, nb, (uint32_t)s_len_(nb), 11, KC_FAINT, WF_MONO, 255);
            int x = ax + gut + 12; uint32_t k = a;
            while (k < i && x < ax + aw) {
                uint32_t s0 = k; uint32_t col = KC_TXT; char c = s_src[k];
                if (c == '%') { k = i; col = 0x006B7280; }
                else if (c == '\\') { k++; if (k < i && ((s_src[k] | 32) >= 'a' && (s_src[k] | 32) <= 'z')) { while (k < i && (s_src[k] | 32) >= 'a' && (s_src[k] | 32) <= 'z') k++; } else if (k < i) k++; col = 0x0060A5FA; }
                else if (c == '$') { k++; if (k < i && s_src[k] == '$') k++; mat = !mat; col = 0x0034D399; }
                else if (c == '{' || c == '}' || c == '[' || c == ']') { k++; col = 0x00FBBF24; }
                else { while (k < i && s_src[k] != '\\' && s_src[k] != '$' && s_src[k] != '%' && s_src[k] != '{' && s_src[k] != '}' && s_src[k] != '[' && s_src[k] != ']') k++; col = mat ? 0x0086EFAC : KC_TXT; }
                if (k > s0 && has_wf()) { uint32_t nn = k - s0; if (s_src[k - 1] == '\r') nn--; wf_draw(x, y + 14, s_src + s0, nn, px, col, WF_MONO, 255); x += wf_width(s_src + s0, nn, px, WF_MONO); }
            }
        }
        y += lh; linea++;
        if (i >= s_src_len) break;
        i++;
    }
}
static void pinta_barra_scroll(int ax, int ay, int aw, int ah)
{
    int x = ax + aw; fb_fill_rect((uint32_t)x, (uint32_t)ay, SB_W, (uint32_t)ah, 0x00161C28);
    if (s_alto <= s_vista_h) return;
    int track = ah - 6, th = track * s_vista_h / s_alto; if (th < 28) th = 28;
    int ty = ay + 3 + (track - th) * s_scroll / (max_scroll() ? max_scroll() : 1);
    fb_fill_rrect_aa(x + 3, ty, SB_W - 6, th, 3, s_drag ? 0xFF94A3B8u : 0xFF475569u);
}
static void tarjeta(int x, int y, int w, int h, const char *t1, const char *t2, uint32_t c2, int puntos)
{
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, KC_DESK);
    int cw = w - 80; if (cw > 560) cw = 560;
    int ch = 150, cx = x + (w - cw) / 2, cy = y + (h - ch) / 3; if (cy < y + 20) cy = y + 20;
    fb_draw_shadow(cx, cy, cw, ch, 14, 22, 8, 0x90000000u);
    fb_fill_rounded_rect(cx, cy, cw, ch, 14, KC_CARD);
    /* icono de documento */
    int ix = cx + 28, iy = cy + 32;
    fb_fill_rounded_rect(ix, iy, 34, 44, 5, s_tipo == WT_PDF ? 0x00B91C1C : 0x000E7490);
    tx(ix, iy, 44, s_tipo == WT_PDF ? " PDF" : " TeX", 11, 0x00FFFFFF, WF_BOLD);
    tx(cx + 80, cy + 24, 28, t1, 17, KC_TXT, WF_BOLD);
    char nm[160]; s_cpy(nm, s_nombre, sizeof nm); tx(cx + 80, cy + 54, 22, nm, 13, KC_DIM, 0);
    tx(cx + 80, cy + 84, 22, t2, 13, c2, 0);
    if (puntos) { int k = (int)((timer_get_uptime_ms() / 120) % 8); for (int i = 0; i < 3; i++) fb_fill_rrect_aa(cx + 80 + i * 12, cy + 118, 7, 7, 3, (0xFF000000u | KC_ACC) - (((k + i * 2) % 8) > 3 ? 0x90000000u : 0u)); }
}
void wv_pinta(int x, int y, int w, int h)
{
    if (s_estado == E_LISTO) {                                     /* lo descargado se abre aqui, en el hilo de la GUI */
        if (s_tipo == WT_PDF) {
            uint8_t *d = s_dato; uint32_t n = s_len; s_dato = 0; s_len = 0;
            int ant = pdfview_selected();
            s_slot = pdfview_inst_new();
            if (!s_slot) { if (d) kfree(d); pdfview_select(ant); s_cpy(s_err, "Hay demasiados PDF abiertos: cierra alguno.", sizeof s_err); s_estado = E_ERROR; }
            else if (pdfview_open_data(s_nombre, d, n) != 0) { s_cpy(s_err, pdfview_error()[0] ? pdfview_error() : "No se pudo abrir el PDF", sizeof s_err); pdfview_inst_drop(s_slot); s_slot = 0; pdfview_select(ant); s_estado = E_ERROR; }
            else { pdfview_select(ant); s_estado = E_ABIERTO; }
        } else tex_abre();
        toca();
    }
    fb_set_clip(x, y, w, h);
    if (s_estado == E_BAJANDO) {
        char m[96] = "";
        s_cpy(m, s_url_bajar[0] == 'f' ? "Leyendo el archivo..." : "Descargando...", sizeof m);
        tarjeta(x, y, w, h, s_tipo == WT_PDF ? "Abriendo el PDF" : "Abriendo el documento LaTeX", m, KC_ACC, 1);
    } else if (s_estado == E_ERROR) {
        tarjeta(x, y, w, h, "No se puede mostrar el documento", s_err, KC_RED, 0);
    } else if (s_estado == E_ABIERTO && s_tipo == WT_PDF && s_slot) {
        int ant = pdfview_selected(); pdfview_select(s_slot);
        pdfview_render_window((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 1);
        pdfview_select(ant);
    } else if (s_estado == E_ABIERTO && s_tipo == WT_TEX) {
        int ax, ay, aw, ah; geom(x, y, w, h, &ax, &ay, &aw, &ah);
        /* barra */
        fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, BAR_H, KC_BAR); fb_fill_rect((uint32_t)x, (uint32_t)(y + BAR_H - 1), (uint32_t)w, 1, KC_LINE);
        struct btn b[B_N]; int nb = botones(x, w, b);
        for (int i = 0; i < nb; i++) {
            int on = (i == B_DOC && !s_codigo) || (i == B_COD && s_codigo);
            int dis = (i == B_MENOS || i == B_MAS) && s_codigo;
            fb_fill_rounded_rect(b[i].x, y + 7, b[i].w, 30, 9, on ? KC_SEL : (s_hover == i ? 0x00222C40 : KC_BTN));
            fb_stroke_rrect_aa(b[i].x, y + 7, b[i].w, 30, 9, on ? 0xFF2B5C8Au : 0xFF263142u);
            tx(b[i].x + (b[i].w - tw(b[i].t, 13, 0)) / 2, y + 7, 30, b[i].t, 13, dis ? KC_FAINT : KC_TXT, 0);
        }
        {   /* titulo y datos a la derecha, si caben */
            int x0 = b[nb - 1].x + b[nb - 1].w + 16; char inf[120] = "";
            if (s_out.title[0]) { s_cpy(inf, s_out.title, sizeof inf); s_cat(inf, "  \302\267  ", sizeof inf); }
            s_num(inf, sizeof inf, (uint32_t)s_lineas); s_cat(inf, " l\303\255neas", sizeof inf);
            if (s_out.errors) { s_cat(inf, "  \302\267  ", sizeof inf); s_num(inf, sizeof inf, (uint32_t)s_out.errors); s_cat(inf, " sin soportar", sizeof inf); }
            if (x0 + tw(inf, 12, 0) < x + w - 12) tx(x + w - 12 - tw(inf, 12, 0), y + 7, 30, inf, 12, KC_DIM, 0);
        }
        fb_set_clip(ax, ay, aw, ah);
        if (s_codigo) {
            s_alto = s_lineas * 19 + 24; s_vista_h = ah; if (s_scroll > max_scroll()) s_scroll = max_scroll();
            pinta_codigo(ax, ay, aw, ah);
        } else {
            int pw = papel_w(aw), margen = pw > 600 ? 64 : 28, col = pw - 2 * margen;
            tex_maqueta(col);
            int ph = s_out.height + 2 * margen; if (ph < 200) ph = 200;
            s_alto = ph + 48; s_vista_h = ah; if (s_scroll > max_scroll()) s_scroll = max_scroll();
            fb_fill_rect((uint32_t)ax, (uint32_t)ay, (uint32_t)aw, (uint32_t)ah, KC_DESK);
            int px0 = ax + (aw - pw) / 2, py0 = ay + 24 - s_scroll;
            fb_draw_shadow(px0, py0, pw, ph, 2, 18, 6, 0x90000000u);
            int vy0 = py0 > ay ? py0 : ay, vy1 = py0 + ph < ay + ah ? py0 + ph : ay + ah;
            if (vy1 > vy0) fb_fill_rect((uint32_t)px0, (uint32_t)vy0, (uint32_t)pw, (uint32_t)(vy1 - vy0), 0x00FFFFFF);
            fb_set_clip(px0 > ax ? px0 : ax, ay, pw < aw ? pw : aw, ah);
            pinta_ops(px0 + margen, py0 + margen, ay, ay + ah);
        }
        fb_set_clip(x, y, w, h);
        pinta_barra_scroll(ax, ay, aw, ah);
    } else fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, KC_DESK);
    fb_reset_clip();
}

/* ---------------------------------------------------------------- entrada */
static int boton_en(int mx, int my, int x, int y, int w)
{
    struct btn b[B_N]; int nb = botones(x, w, b);
    if (my < y + 7 || my >= y + 37) return -1;
    for (int i = 0; i < nb; i++) if (mx >= b[i].x && mx < b[i].x + b[i].w) return i;
    return -1;
}
static void abrir_maquetacion(void)
{
    char ruta[200];
    if (s_ruta[0]) s_cpy(ruta, s_ruta, sizeof ruta);
    else {
        s_cpy(ruta, "/workspace/Descargas/", sizeof ruta); s_cat(ruta, s_nombre, sizeof ruta);
        if (!wt_ext(ruta, ".tex")) s_cat(ruta, ".tex", sizeof ruta);
        if (vfs_write(ruta, s_src, s_src_len) < 0) { clip_toast("No se pudo guardar el documento para la Maquetacion"); return; }
        s_cpy(s_ruta, ruta, sizeof s_ruta);
        char m[240] = "Guardado en "; s_cat(m, ruta, sizeof m); clip_toast(m);
    }
    gui_open_layout_file(ruta);
}
int wv_click(int mx, int my, int x, int y, int w, int h)
{
    if (!wv_activo()) return 0;
    if (s_estado == E_ABIERTO && s_tipo == WT_PDF && s_slot) {
        int ant = pdfview_selected(); pdfview_select(s_slot); (void)pdfview_handle_click(mx, my, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h); pdfview_select(ant); toca(); return 1;
    }
    if (s_estado != E_ABIERTO || s_tipo != WT_TEX) return 1;
    int ax, ay, aw, ah; geom(x, y, w, h, &ax, &ay, &aw, &ah);
    if (my < ay) {
        switch (boton_en(mx, my, x, y, w)) {
        case B_DOC: s_codigo = 0; s_scroll = 0; break;
        case B_COD: s_codigo = 1; s_scroll = 0; break;
        case B_MENOS: if (!s_codigo && s_px > 11) { s_px -= 2; } break;
        case B_MAS: if (!s_codigo && s_px < 30) { s_px += 2; } break;
        case B_COPIA: if (s_src && clip_set_text(s_src, s_src_len) >= 0) clip_toast("C\303\263digo LaTeX copiado"); break;
        case B_MAQ: abrir_maquetacion(); break;
        default: break;
        }
        toca(); return 1;
    }
    if (mx >= ax + aw && s_alto > s_vista_h) {                    /* barra de desplazamiento */
        int track = ah - 6, th = track * s_vista_h / s_alto; if (th < 28) th = 28;
        int ty = ay + 3 + (track - th) * s_scroll / (max_scroll() ? max_scroll() : 1);
        if (my >= ty && my < ty + th) { s_drag = 1; s_drag_y = my; s_drag_s = s_scroll; }
        else fija_scroll(s_scroll + (my < ty ? -(ah - 40) : ah - 40));
        toca();
    }
    return 1;
}
int wv_raton(int mx, int my, int x, int y, int w, int h, int wheel, int held)
{
    if (!wv_activo()) return 0;
    if (s_estado == E_ABIERTO && s_tipo == WT_PDF && s_slot) {
        int ant = pdfview_selected(); pdfview_select(s_slot); int r = pdfview_mouse(mx, my, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, wheel, held); pdfview_select(ant);
        if (r || wheel || held) toca();
        return r;
    }
    if (s_estado != E_ABIERTO || s_tipo != WT_TEX) return 0;
    int ax, ay, aw, ah; geom(x, y, w, h, &ax, &ay, &aw, &ah);
    int hv = my < ay && my >= y ? boton_en(mx, my, x, y, w) : -1;
    if (hv != s_hover) { s_hover = hv; toca(); }
    if (s_drag) {
        if (!held) { s_drag = 0; toca(); return 1; }
        int track = ah - 6, th = track * s_vista_h / s_alto; if (th < 28) th = 28;
        int span = track - th; if (span < 1) span = 1;
        fija_scroll(s_drag_s + (int)((int64_t)(my - s_drag_y) * max_scroll() / span));
        return 1;
    }
    if (wheel && mx >= x && mx < x + w && my >= ay && my < y + h) { fija_scroll(s_scroll - wheel * 70); return 1; }
    return 0;
}
int wv_tecla(char ch)
{
    if (!wv_activo()) return 0;
    int c = (uint8_t)ch;
    if (s_estado == E_ABIERTO && s_tipo == WT_PDF && s_slot) {
        if (c == 12 || c == 20 || c == 23 || c == 18) return 0;     /* atajos del navegador: barra, pestanas, recargar */
        int ant = pdfview_selected(); pdfview_select(s_slot); pdfview_handle_key(ch); pdfview_select(ant); toca(); return 1;
    }
    if (s_estado != E_ABIERTO || s_tipo != WT_TEX) return 0;
    switch (c) {
    case 14: case 'j': fija_scroll(s_scroll + 60); return 1;
    case 16: case 'k': fija_scroll(s_scroll - 60); return 1;
    case 29: case ' ': fija_scroll(s_scroll + s_vista_h - 40); return 1;
    case 28: case 'b': fija_scroll(s_scroll - (s_vista_h - 40)); return 1;
    case 1: case 'g': fija_scroll(0); return 1;
    case 5: case 'G': fija_scroll(1 << 30); return 1;
    case '+': case '=': if (!s_codigo && s_px < 30) { s_px += 2; toca(); } return 1;
    case '-': if (!s_codigo && s_px > 11) { s_px -= 2; toca(); } return 1;
    case 'c': case 'C': s_codigo = !s_codigo; s_scroll = 0; toca(); return 1;
    case 3: if (s_src && clip_set_text(s_src, s_src_len) >= 0) clip_toast("C\303\263digo LaTeX copiado"); return 1;
    default: return 0;
    }
}
uint64_t wv_huella(void)
{
    uint64_t k = s_huella_gen * 1099511628211ull ^ (uint64_t)s_estado;
    if (s_estado == E_BAJANDO) k ^= (timer_get_uptime_ms() / 120) * 0x9E3779B97F4A7C15ull;   /* puntos animados */
    if (s_estado == E_ABIERTO && s_tipo == WT_PDF) k ^= (timer_get_uptime_ms() / 400) * 0x9E3779B97F4A7C15ull;   /* el lector tiene avisos con tiempo */
    return k;
}
