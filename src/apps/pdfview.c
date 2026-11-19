/* PARCHE 088: lector de PDF con la estetica del explorador.
 * Barra (abrir, paginas, zoom, ajustar, miniaturas, copiar texto), miniaturas a la izquierda, hoja blanca con sombra, rueda y arrastre,
 * selector de archivos PDF. La pagina se dibuja una vez en un bufer propio (texto con DejaVu en su sitio exacto, trayectos con
 * antialias, imagenes) y luego solo se copia: desplazarse es inmediato. */
#include "pdfview.h"
#include "pdf.h"
#include "fb.h"
#include "fs.h"
#include "mem.h"
#include "mouse.h"
#include "web_font.h"
#include "gui_font.h"
#include "clipboard.h"
#include "idt.h"

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_CARD    0x00111827
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB
#define KC_DESK    0x0020262F
#define BAR_H 48
#define ST_H 26
#define SIDE_W 150
#define MAX_PAGE_PX (14u * 1024u * 1024u)

/* ---------------------------------------------------------------- estado */
#define MAXTH 400
/* SOMA_213_PDF: varias ventanas del lector PDF. Documento, pagina, zoom, desplazamiento, miniaturas y escalas de letra viven en
 * una instancia por ventana; la lista de archivos PDF se comparte. La GUI elige la instancia con pdfview_select(). */
struct pv_inst {
    char g_path[128];
    uint8_t *g_data; uint32_t g_len;
    struct pdf *g_pdf; int g_np, g_page;
    char g_err[96], g_title[96], g_msg[120]; int g_msg_ok;
    int g_zoom;
    int g_scroll, g_side, g_picker, g_pick_sel, g_pick_scroll;
    uint32_t *g_buf; int g_bw, g_bh, g_bpage, g_bscale, g_unknown, g_ms;
    int g_drag, g_drag_y, g_drag_s, g_side_scroll;
    uint32_t *g_th[MAXTH]; int16_t g_thw[MAXTH], g_thh[MAXTH];
    int g_fscale[65];
};
#define PV_INST_MAX 8
static struct pv_inst pv_insts[PV_INST_MAX + 1];
static int pv_used[PV_INST_MAX + 1], pv_ready;
static struct pv_inst *PV = &pv_insts[1];
static void pv_defaults(struct pv_inst *p)
{
    char *b = (char *)p; for (size_t i = 0; i < sizeof *p; ++i) b[i] = 0;
    p->g_msg_ok = 1; p->g_side = 1; p->g_bpage = -1; p->g_bscale = -1;
}
static void pv_boot(void) { if (!pv_ready) { pv_ready = 1; pv_defaults(&pv_insts[1]); } }
#define g_path (PV->g_path)
#define g_data (PV->g_data)
#define g_len (PV->g_len)
#define g_pdf (PV->g_pdf)
#define g_np (PV->g_np)
#define g_page (PV->g_page)
#define g_err (PV->g_err)
#define g_title (PV->g_title)
#define g_msg (PV->g_msg)
#define g_msg_ok (PV->g_msg_ok)
#define g_zoom (PV->g_zoom)
#define g_scroll (PV->g_scroll)
#define g_side (PV->g_side)
#define g_picker (PV->g_picker)
#define g_pick_sel (PV->g_pick_sel)
#define g_pick_scroll (PV->g_pick_scroll)
#define g_buf (PV->g_buf)
#define g_bw (PV->g_bw)
#define g_bh (PV->g_bh)
#define g_bpage (PV->g_bpage)
#define g_bscale (PV->g_bscale)
#define g_unknown (PV->g_unknown)
#define g_ms (PV->g_ms)
#define g_drag (PV->g_drag)
#define g_drag_y (PV->g_drag_y)
#define g_drag_s (PV->g_drag_s)
#define g_side_scroll (PV->g_side_scroll)
#define g_th (PV->g_th)
#define g_thw (PV->g_thw)
#define g_thh (PV->g_thh)
#define g_fscale (PV->g_fscale)
static char g_files[48][96]; static int g_nfiles;

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; if (v < 0) { s_cat(d, "-", max); v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int in_r(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void say(const char *m, int ok) { s_cpy(g_msg, m, sizeof g_msg); g_msg_ok = ok; }

/* ---------------------------------------------------------------- rasterizado */
struct edge { int32_t x0, y0, x1, y1; int dir; };
static uint8_t *g_cov; static int g_covw;
static int cmp_i32(const void *a, const void *b) { int32_t x = *(const int32_t *)a, y = *(const int32_t *)b; return x < y ? -1 : x > y; }
static void isort(int32_t *a, int n, int32_t *dirs) { for (int i = 1; i < n; i++) { int32_t v = a[i], d = dirs[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; dirs[j + 1] = dirs[j]; j--; } a[j + 1] = v; dirs[j + 1] = d; } }
static inline void blend(uint32_t *p, uint32_t rgb, uint32_t a)
{
    if (a >= 255) { *p = rgb | 0xFF000000u; return; }
    if (!a) return;
    uint32_t d = *p, ia = 255 - a;
    uint32_t rb = ((rgb & 0xFF00FFu) * a + (d & 0xFF00FFu) * ia + 0x800080u) >> 8, g = ((rgb & 0xFF00u) * a + (d & 0xFF00u) * ia + 0x8000u) >> 8;
    *p = 0xFF000000u | (rb & 0xFF00FFu) | (g & 0xFF00u);
}
/* relleno de un trayecto con antialias (4 subfilas, 1/16 px en horizontal); regla par-impar o de giro distinto de cero */
static void fill_path(uint32_t *buf, int bw, int bh, const struct pdf_op *o)
{
    int ne = 0; for (int i = 0; i < o->npts; i++) ne++;
    struct edge *E = (struct edge *)kmalloc((size_t)(ne + 4) * sizeof *E); if (!E) return;
    int n = 0, start = -1;
    for (int i = 0; i < o->npts; i++) {
        int32_t x = o->pts[2 * i], y = o->pts[2 * i + 1];
        int brk = x == -1 && y == -1;
        if (brk || i == o->npts - 1) {
            int end = brk ? i - 1 : i;
            if (start >= 0 && end > start) {   /* cierra el subtrayecto */
                for (int k = start; k <= end; k++) {
                    int k2 = k == end ? start : k + 1;
                    int32_t ax = o->pts[2 * k], ay = o->pts[2 * k + 1], bx = o->pts[2 * k2], by = o->pts[2 * k2 + 1];
                    if (ay == by) continue;
                    struct edge *e = &E[n++]; if (ay < by) { e->x0 = ax; e->y0 = ay; e->x1 = bx; e->y1 = by; e->dir = 1; } else { e->x0 = bx; e->y0 = by; e->x1 = ax; e->y1 = ay; e->dir = -1; }
                    if (n >= ne + 2) break;
                }
            }
            start = -1;
            continue;
        }
        if (start < 0) start = i;
    }
    if (n == 0) { kfree(E); return; }
    int32_t ymin = 0x7FFFFFFF, ymax = -0x7FFFFFFF, xmin = 0x7FFFFFFF, xmax = -0x7FFFFFFF;
    for (int i = 0; i < n; i++) { if (E[i].y0 < ymin) ymin = E[i].y0; if (E[i].y1 > ymax) ymax = E[i].y1; int32_t a = E[i].x0 < E[i].x1 ? E[i].x0 : E[i].x1, b = E[i].x0 < E[i].x1 ? E[i].x1 : E[i].x0; if (a < xmin) xmin = a; if (b > xmax) xmax = b; }
    int py0 = ymin >> 4, py1 = (ymax + 15) >> 4; if (py0 < 0) py0 = 0;
    if (py1 > bh) py1 = bh;
    int px0 = xmin >> 4, px1 = (xmax >> 4) + 2; if (px0 < 0) px0 = 0;
    if (px1 > bw) px1 = bw;
    if (py0 >= py1 || px0 >= px1) { kfree(E); return; }
    if (g_covw < bw + 2) { if (g_cov) kfree(g_cov); g_cov = (uint8_t *)kmalloc((size_t)bw + 2); g_covw = g_cov ? bw + 2 : 0; if (!g_cov) { kfree(E); return; } }
    int32_t *xs = (int32_t *)kmalloc((size_t)(n + 1) * 4), *ds = (int32_t *)kmalloc((size_t)(n + 1) * 4);
    if (!xs || !ds) { if (xs) kfree(xs); if (ds) kfree(ds); kfree(E); return; }
    int eo = (o->flags & 4) != 0;
    uint32_t rgb = o->fill & 0xFFFFFF;
    for (int py = py0; py < py1; py++) {
        memset(g_cov + px0, 0, (size_t)(px1 - px0 + 1));
        int any = 0;
        for (int sub = 0; sub < 4; sub++) {
            int32_t sy = py * 16 + sub * 4 + 2;
            int k = 0;
            for (int i = 0; i < n; i++) {
                struct edge *e = &E[i];
                if (sy < e->y0 || sy >= e->y1) continue;
                xs[k] = e->x0 + (int32_t)((int64_t)(e->x1 - e->x0) * (sy - e->y0) / (e->y1 - e->y0)); ds[k] = e->dir; k++;
            }
            if (k < 2) continue;
            isort(xs, k, ds);
            int wind = 0;
            for (int i = 0; i + 1 < k; i++) {
                wind += ds[i];
                int inside = eo ? (i % 2 == 0) : wind != 0;
                if (!inside) continue;
                int32_t a = xs[i], b = xs[i + 1];
                if (a < px0 * 16) a = px0 * 16;
                if (b > px1 * 16) b = px1 * 16;
                if (b <= a) continue;
                any = 1;
                int ca = a >> 4, cb = b >> 4;
                if (ca == cb) { g_cov[ca] = (uint8_t)(g_cov[ca] + (b - a)); continue; }
                g_cov[ca] = (uint8_t)(g_cov[ca] + (16 - (a & 15)));
                for (int c = ca + 1; c < cb; c++) g_cov[c] = (uint8_t)(g_cov[c] + 16);
                if (cb < px1) g_cov[cb] = (uint8_t)(g_cov[cb] + (b & 15));
            }
        }
        if (!any) continue;
        uint32_t *row = buf + (size_t)py * bw;
        for (int x = px0; x < px1; x++) { int c = g_cov[x]; if (c) blend(row + x, rgb, c >= 64 ? 255 : (uint32_t)(c * 255 / 64)); }
    }
    kfree(xs); kfree(ds); kfree(E);
    (void)cmp_i32;
}
static void stroke_path(const struct pdf_op *o)
{
    uint32_t argb = 0xFF000000u | (o->color & 0xFFFFFF);
    int w16 = o->size < 12 ? 12 : o->size;
    for (int i = 0; i + 1 < o->npts; i++) {
        int32_t ax = o->pts[2 * i], ay = o->pts[2 * i + 1], bx = o->pts[2 * i + 2], by = o->pts[2 * i + 3];
        if ((ax == -1 && ay == -1) || (bx == -1 && by == -1)) continue;
        if (ax == bx && ay == by) continue;
        fb_line_aa(ax, ay, bx, by, w16, argb);
    }
}
static void draw_image(uint32_t *buf, int bw, int bh, const struct pdf_op *o)
{
    if (o->w <= 0 || o->h <= 0 || !o->img) return;
    int x0 = o->x < 0 ? 0 : o->x, y0 = o->y < 0 ? 0 : o->y, x1 = o->x + o->w, y1 = o->y + o->h;
    if (x1 > bw) x1 = bw;
    if (y1 > bh) y1 = bh;
    for (int y = y0; y < y1; y++) {
        int sy = (int)((int64_t)(y - o->y) * o->ih / o->h); if (o->flip_y) sy = o->ih - 1 - sy;
        if (sy < 0) sy = 0;
        if (sy >= o->ih) sy = o->ih - 1;
        const uint32_t *srow = o->img + (size_t)sy * o->iw; uint32_t *drow = buf + (size_t)y * bw;
        for (int x = x0; x < x1; x++) {
            int sx = (int)((int64_t)(x - o->x) * o->iw / o->w); if (o->flip_x) sx = o->iw - 1 - sx;
            if (sx < 0) sx = 0;
            if (sx >= o->iw) sx = o->iw - 1;
            uint32_t c = srow[sx]; blend(drow + x, c & 0xFFFFFF, c >> 24);
        }
    }
}
/* g_fscale: por fuente, escala de la letra (1/1000) para que sus palabras midan como en el PDF (por ventana: SOMA_213) */
static void draw_word(const struct pdf_op *o, int thumb)
{
    if (!o->text || o->len <= 0 || o->size <= 0) return;
    int fl = WF_RAW | ((o->flags & PDF_TF_BOLD) ? WF_BOLD : 0) | ((o->flags & PDF_TF_ITALIC) ? WF_ITALIC : 0) | ((o->flags & PDF_TF_MONO) ? WF_MONO : 0);
    int px = o->size;
    if (thumb || px < 5 || !has_wf()) {   /* letra diminuta: una raya gris donde va el texto */
        int h = px / 2; if (h < 1) h = 1;
        fb_fill_rect_alpha(o->x, o->y - px * 2 / 3, o->w, h, 0x70000000u | (o->color & 0xFFFFFF));
        return;
    }
    if (px > 400) px = 400;
    if (o->font && g_fscale[o->font] && g_fscale[o->font] < 1000) { px = px * g_fscale[o->font] / 1000; if (px < 4) px = 4; }
    int w = wf_width(o->text, (uint32_t)o->len, px, fl);
    if (w <= o->w + 1 || o->len < 2) { wf_draw(o->x, o->y, o->text, (uint32_t)o->len, px, o->color & 0xFFFFFF, fl, 255); return; }
    /* nuestra letra es mas ancha que la del PDF: se juntan las letras (y, si hace mucha falta, se reduce un poco el tamano) */
    /* lo que aun sobre se compensa juntando un poco las letras */
    int64_t pos = 0; int i = 0;
    while (i < o->len) {
        int l = 1; uint8_t c = (uint8_t)o->text[i];
        if (c >= 0xF0) l = 4; else if (c >= 0xE0) l = 3; else if (c >= 0xC0) l = 2;
        if (i + l > o->len) l = o->len - i;
        int cw = wf_width(o->text + i, (uint32_t)l, px, fl);
        int x = o->x + (int)(pos * o->w / (w > 0 ? w : 1));
        wf_draw(x, o->y, o->text + i, (uint32_t)l, px, o->color & 0xFFFFFF, fl, 255);
        pos += cw; i += l;
    }
}
static void raster(const struct pdf_page *pg, uint32_t *buf, int bw, int bh, int thumb)
{
    /* escala comun por fuente: DejaVu es mas ancha que Helvetica, Times o Computer Modern; se reduce igual toda la fuente
     * (minimo 78 %) para que las palabras ocupen su sitio sin cambiar de tamano de una a otra */
    static int64_t sw[65], sp[65];
    for (int f = 0; f < 65; f++) { sw[f] = sp[f] = 0; g_fscale[f] = 1000; }
    if (!thumb && has_wf()) for (int i = 0; i < pg->nops; i++) {
        const struct pdf_op *o = &pg->ops[i];
        if (o->kind != PDF_OP_TEXT || !o->font || !o->text || o->len < 3 || o->size < 5) continue;
        int fl = WF_RAW | ((o->flags & PDF_TF_BOLD) ? WF_BOLD : 0) | ((o->flags & PDF_TF_MONO) ? WF_MONO : 0);
        sw[o->font] += o->w; sp[o->font] += wf_width(o->text, (uint32_t)o->len, o->size > 400 ? 400 : o->size, fl);
    }
    for (int f = 1; f < 65; f++) if (sp[f] > 0) { int r = (int)(sw[f] * 1000 / sp[f]); g_fscale[f] = r > 1000 ? 1000 : r < 780 ? 780 : r; }
    for (int i = 0; i < bw * bh; i++) buf[i] = 0xFFFFFFFFu;
    fb_target_begin(buf, (uint32_t)bw, (uint32_t)bh);
    for (int i = 0; i < pg->nops; i++) {
        const struct pdf_op *o = &pg->ops[i];
        if (o->kind == PDF_OP_PATH) {
            if (o->flags & 1) fill_path(buf, bw, bh, o);
            if (o->flags & 2) stroke_path(o);
        } else if (o->kind == PDF_OP_IMAGE) draw_image(buf, bw, bh, o);
        else if (o->kind == PDF_OP_TEXT) draw_word(o, thumb);
    }
    fb_target_end();
}

/* ---------------------------------------------------------------- documento */
static void free_thumbs(void) { for (int i = 0; i < MAXTH; i++) { if (g_th[i]) kfree(g_th[i]); g_th[i] = 0; } }
static void close_doc(void)
{
    if (g_pdf) pdf_close(g_pdf);
    g_pdf = 0;
    if (g_data) kfree(g_data);
    g_data = 0; g_len = 0; g_np = 0; g_page = 0; g_scroll = 0;
    if (g_buf) kfree(g_buf);
    g_buf = 0; g_bpage = -1; g_bscale = -1;
    free_thumbs(); g_title[0] = 0; g_err[0] = 0;
}
int pdfview_open_file(const char *path)
{
    pv_boot();
    close_doc();
    s_cpy(g_path, path, sizeof g_path);
    uint32_t n = 0; uint8_t *d = (uint8_t *)vfs_read_alloc(path, &n);
    if (!d) { s_cpy(g_err, "No se pudo leer el archivo", sizeof g_err); say(g_err, 0); return -1; }
    g_data = d; g_len = n;
    g_pdf = pdf_open(d, n, g_err, sizeof g_err);
    if (!g_pdf) { say(g_err, 0); kfree(g_data); g_data = 0; return -1; }
    g_np = pdf_page_count(g_pdf); g_page = 0; g_scroll = 0; g_picker = 0; g_side_scroll = 0;
    pdf_title(g_pdf, g_title, sizeof g_title);
    char m[120] = "Abierto: "; s_cat(m, path, sizeof m); say(m, 1);
    return 0;
}
void pdfview_init(void) { pv_boot(); (void)has_wf(); }

/* escala (px por punto * 256) para el modo de zoom y el area dada */
static int scale_for(int vw, int vh)
{
    int pw = 612, ph = 792;
    if (g_pdf) pdf_page_size(g_pdf, g_page, &pw, &ph);
    if (pw < 50) pw = 50;
    if (ph < 50) ph = 50;
    int s;
    if (g_zoom > 0) s = g_zoom * 256 * 96 / 72 / 100;              /* 100 % = 96 ppp */
    else if (g_zoom < 0) { int a = (vw - 40) * 256 / pw, b = (vh - 40) * 256 / ph; s = a < b ? a : b; }
    else s = (vw - 48) * 256 / pw;
    if (s < 40) s = 40;
    if (s > 1600) s = 1600;
    while ((int64_t)pw * ph * s / 256 * s / 256 > MAX_PAGE_PX && s > 40) s -= 8;
    return s;
}
static void ensure_page(int scale)
{
    if (!g_pdf || (g_bpage == g_page && g_bscale == scale && g_buf)) return;
    uint64_t t0 = timer_get_uptime_ms();
    struct pdf_page pg;
    if (pdf_render_page(g_pdf, g_page, scale, &pg) != 0) { say("No se pudo dibujar la pagina", 0); return; }
    if (pg.w < 1) pg.w = 1;
    if (pg.h < 1) pg.h = 1;
    if (!g_buf || g_bw * g_bh < pg.w * pg.h) { if (g_buf) kfree(g_buf); g_buf = (uint32_t *)kmalloc((size_t)pg.w * pg.h * 4); }
    if (!g_buf) { pdf_page_free(&pg); say("Sin memoria para la pagina", 0); g_bpage = -1; return; }
    g_bw = pg.w; g_bh = pg.h;
    raster(&pg, g_buf, g_bw, g_bh, 0);
    g_unknown = pg.unknown;
    pdf_page_free(&pg);
    g_bpage = g_page; g_bscale = scale; g_ms = (int)(timer_get_uptime_ms() - t0);
}
static void make_thumb(int i)
{
    if (i < 0 || i >= g_np || i >= MAXTH || g_th[i]) return;
    int pw, ph; if (pdf_page_size(g_pdf, i, &pw, &ph) || pw < 1) return;
    int s = 100 * 256 / pw; struct pdf_page pg;
    if (pdf_render_page(g_pdf, i, s, &pg) != 0) return;
    int w = pg.w < 1 ? 1 : pg.w, h = pg.h < 1 ? 1 : pg.h; if (h > 180) h = 180;
    uint32_t *b = (uint32_t *)kmalloc((size_t)w * h * 4);
    if (b) { raster(&pg, b, w, h, 1); g_th[i] = b; g_thw[i] = (int16_t)w; g_thh[i] = (int16_t)h; }
    pdf_page_free(&pg);
}

/* ---------------------------------------------------------------- selector de archivos */
static int ends_pdf(const char *s) { int n = s_len(s); return n > 4 && s[n - 4] == '.' && (s[n - 3] | 32) == 'p' && (s[n - 2] | 32) == 'd' && (s[n - 1] | 32) == 'f'; }
static void scan_files(void)
{
    static char list[8192]; g_nfiles = 0;
    int n = vfs_format_list_ext(1, 0, list, sizeof list - 1); if (n <= 0) return;
    list[n] = 0; const char *p = list;
    if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p) p++; }
    while (*p && g_nfiles < 48) {
        while (*p == ' ' || *p == ',') p++;
        char f[96]; int k = 0; while (*p && *p != ',' && k < 95) f[k++] = *p++;
        f[k] = 0; while (*p && *p != ',') p++;
        if (k && ends_pdf(f)) s_cpy(g_files[g_nfiles++], f, 96);
    }
}

/* ---------------------------------------------------------------- dibujo */
struct lay { int x, y, w, h, bar_y, side_x, side_w, vx, vy, vw, vh, st_y; };
static void lay_of(struct lay *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->bar_y = L->y; L->st_y = L->y + L->h - ST_H;
    L->side_w = (g_side && g_np > 1 && L->w > 600) ? SIDE_W : 0; L->side_x = L->x;
    L->vx = L->x + L->side_w; L->vy = L->y + BAR_H; L->vw = L->w - L->side_w; L->vh = L->h - BAR_H - ST_H;
}
/* botones de la barra: id, x, ancho */
enum { B_OPEN = 1, B_PREV, B_NEXT, B_ZOUT, B_ZIN, B_FITW, B_FITP, B_SIDE, B_COPY };
struct btn { int id, x, w; const char *label; };
static int bar_buttons(const struct lay *L, struct btn *b)
{
    int n = 0, x = L->x + 12;
    b[n++] = (struct btn){ B_OPEN, x, 86, "Abrir" }; x += 86 + 14;
    b[n++] = (struct btn){ B_SIDE, x, 34, 0 }; x += 34 + 14;
    b[n++] = (struct btn){ B_PREV, x, 34, 0 }; x += 34 + 116;
    b[n++] = (struct btn){ B_NEXT, x, 34, 0 }; x += 34 + 14;
    int r = L->x + L->w - 12;
    r -= 92; b[n++] = (struct btn){ B_COPY, r, 92, "Copiar texto" }; r -= 10;
    r -= 72; b[n++] = (struct btn){ B_FITP, r, 72, "Pagina" }; r -= 6;
    r -= 72; b[n++] = (struct btn){ B_FITW, r, 72, "Ancho" }; r -= 10;
    r -= 34; b[n++] = (struct btn){ B_ZIN, r, 34, 0 }; r -= 64;
    r -= 34; b[n++] = (struct btn){ B_ZOUT, r, 34, 0 };
    (void)x;
    return n;
}
static void icon(int id, int cx, int cy, uint32_t col)
{
    uint32_t a = 0xFF000000u | col; int s = 16;
    switch (id) {
    case B_PREV: fb_line_aa((cx + 3) * s, (cy - 6) * s, (cx - 3) * s, cy * s, 30, a); fb_line_aa((cx - 3) * s, cy * s, (cx + 3) * s, (cy + 6) * s, 30, a); break;
    case B_NEXT: fb_line_aa((cx - 3) * s, (cy - 6) * s, (cx + 3) * s, cy * s, 30, a); fb_line_aa((cx + 3) * s, cy * s, (cx - 3) * s, (cy + 6) * s, 30, a); break;
    case B_ZOUT: fb_line_aa((cx - 6) * s, cy * s, (cx + 6) * s, cy * s, 30, a); break;
    case B_ZIN: fb_line_aa((cx - 6) * s, cy * s, (cx + 6) * s, cy * s, 30, a); fb_line_aa(cx * s, (cy - 6) * s, cx * s, (cy + 6) * s, 30, a); break;
    case B_SIDE: fb_stroke_rrect_aa(cx - 8, cy - 7, 16, 14, 2, a); fb_line_aa((cx - 3) * s, (cy - 7) * s, (cx - 3) * s, (cy + 7) * s, 20, a); break;
    default: break;
    }
}
static void pdf_glyph(int x, int y, int sz)   /* icono de documento PDF */
{
    fb_fill_rrect_aa(x, y, sz, sz * 13 / 10, sz / 8, 0xFFF1F5F9u);
    fb_fill_rect_alpha(x + sz - sz / 3, y, sz / 3, sz / 3, 0xFF94A3B8u);
    fb_fill_rrect_aa(x - sz / 6, y + sz * 6 / 10, sz * 9 / 10, sz * 4 / 10, sz / 12, 0xFFDC2626u);
    if (has_wf()) wf_draw(x - sz / 6 + sz / 10, y + sz * 6 / 10 + sz * 3 / 10, "PDF", 3, sz / 4 > 6 ? sz / 4 : 6, 0xFFFFFF, WF_BOLD | WF_RAW, 255);
    for (int i = 0; i < 3; i++) fb_fill_rect((uint32_t)(x + sz / 6), (uint32_t)(y + sz / 5 + i * sz / 8), (uint32_t)(sz / 2), 2, 0x00CBD5E1);
}
void pdfview_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused)
{
    (void)focused;
    struct lay L; lay_of(&L, x, y, w, h);
    fb_fill_rect(x, y, w, h, KC_WIN);
    /* barra */
    fb_fill_rect(x, y, w, BAR_H, KC_BAR); fb_fill_rect(x, y + BAR_H - 1, w, 1, KC_LINE);
    struct btn b[12]; int nb = bar_buttons(&L, b);
    for (int i = 0; i < nb; i++) {
        int by = L.y + 8, bh = 32;
        int on = (b[i].id == B_FITW && g_zoom == 0) || (b[i].id == B_FITP && g_zoom < 0) || (b[i].id == B_SIDE && g_side);
        int dis = !g_pdf && b[i].id != B_OPEN;
        uint32_t bg = b[i].id == B_OPEN ? KC_PRIMARY : on ? KC_SELBG : 0x00182133;
        fb_fill_rounded_rect(b[i].x, by, b[i].w, bh, 9, bg);
        if (b[i].id != B_OPEN) fb_stroke_rrect_aa(b[i].x, by, b[i].w, bh, 9, on ? 0xFF2B5C8Au : 0xFF263142u);
        uint32_t c = dis ? KC_FAINT : (b[i].id == B_OPEN ? 0x00FFFFFF : KC_TXT);
        if (b[i].label) { if (b[i].id == B_OPEN) { pdf_glyph(b[i].x + 12, by + 7, 13); tx(b[i].x + 34, by, bh, b[i].label, 13, c, 1); } else tx(b[i].x + (b[i].w - tw(b[i].label, 12, 0)) / 2, by, bh, b[i].label, 12, c, 0); }
        else icon(b[i].id, b[i].x + b[i].w / 2, by + bh / 2, c);
        if (b[i].id == B_PREV) {
            char t[40] = ""; if (g_pdf) { s_num(t, sizeof t, g_page + 1); s_cat(t, " / ", sizeof t); s_num(t, sizeof t, g_np); } else s_cpy(t, "- / -", sizeof t);
            int cx = b[i].x + b[i].w + 58; tx(cx - tw(t, 13, 1) / 2, by, bh, t, 13, g_pdf ? KC_TXT : KC_FAINT, 1);
        }
        if (b[i].id == B_ZOUT) {
            char t[16] = ""; int sc = scale_for(L.vw, L.vh); s_num(t, sizeof t, sc * 72 * 100 / 96 / 256); s_cat(t, " %", sizeof t);
            int cx = b[i].x + b[i].w + 32; tx(cx - tw(t, 12, 0) / 2, by, bh, t, 12, g_pdf ? KC_DIM : KC_FAINT, 0);
        }
    }
    /* vista */
    fb_set_clip(L.vx, L.vy, L.vw, L.vh);
    fb_fill_rect((uint32_t)L.vx, (uint32_t)L.vy, (uint32_t)L.vw, (uint32_t)L.vh, KC_DESK);
    if (!g_pdf) {
        int cx = L.vx + L.vw / 2, cy = L.vy + L.vh / 2 - 40;
        pdf_glyph(cx - 30, cy - 70, 60);
        const char *t1 = g_err[0] ? g_err : "Ningun documento abierto";
        tx(cx - tw(t1, 18, 1) / 2, cy + 20, 30, t1, 18, g_err[0] ? 0x00F87171 : KC_TXT, 1);
        const char *t2 = "Pulsa Abrir, haz doble clic en un PDF en el explorador o usa: pdf <archivo>";
        tx(cx - tw(t2, 13, 0) / 2, cy + 52, 24, t2, 13, KC_DIM, 0);
        const char *t3 = "Para traer uno de internet: descargar <url>";
        tx(cx - tw(t3, 12, 0) / 2, cy + 76, 22, t3, 12, KC_FAINT, 0);
    } else {
        int sc = scale_for(L.vw, L.vh);
        ensure_page(sc);
        if (g_buf) {
            int px = L.vx + (L.vw - g_bw) / 2; if (px < L.vx + 20 && g_bw > L.vw - 40) px = L.vx + 20 - 0;
            int maxs = g_bh + 40 - L.vh; if (maxs < 0) maxs = 0;
            if (g_scroll > maxs) g_scroll = maxs;
            if (g_scroll < 0) g_scroll = 0;
            int py = L.vy + 20 - g_scroll; if (g_bh + 40 < L.vh) py = L.vy + (L.vh - g_bh) / 2;
            fb_draw_shadow(px, py, g_bw, g_bh, 2, 18, 6, 0x90000000u);
            /* solo la parte visible */
            int sy0 = L.vy - py; if (sy0 < 0) sy0 = 0;
            int sy1 = L.vy + L.vh - py; if (sy1 > g_bh) sy1 = g_bh;
            if (sy1 > sy0) fb_blit(px, py + sy0, g_buf + (size_t)sy0 * g_bw, g_bw, sy1 - sy0, g_bw);
            /* barra de desplazamiento */
            if (maxs > 0) {
                int th = L.vh * L.vh / (g_bh + 40); if (th < 30) th = 30;
                int ty = L.vy + (L.vh - th) * g_scroll / maxs;
                fb_fill_rrect_aa(L.vx + L.vw - 10, ty + 2, 6, th - 4, 3, 0x9094A3B8u);
            }
        }
    }
    fb_reset_clip();
    /* miniaturas */
    if (L.side_w) {
        fb_fill_rect((uint32_t)L.side_x, (uint32_t)L.vy, (uint32_t)L.side_w, (uint32_t)L.vh, KC_SIDE);
        fb_fill_rect((uint32_t)(L.side_x + L.side_w - 1), (uint32_t)L.vy, 1, (uint32_t)L.vh, KC_LINE);
        fb_set_clip(L.side_x, L.vy, L.side_w - 1, L.vh);
        int made = 0, yy = L.vy + 12 - g_side_scroll;
        for (int i = 0; i < g_np && i < MAXTH; i++) {
            int th = g_th[i] ? g_thh[i] : 130;
            if (yy + th + 30 >= L.vy && yy < L.vy + L.vh) {
                if (!g_th[i] && made < 2) { make_thumb(i); made++; th = g_th[i] ? g_thh[i] : 130; }
                int tw_ = g_th[i] ? g_thw[i] : 100, tx_ = L.side_x + (L.side_w - tw_) / 2;
                if (i == g_page) fb_fill_rrect_aa(tx_ - 6, yy - 6, tw_ + 12, th + 30, 8, 0xFF1A3558u);
                if (g_th[i]) fb_blit(tx_, yy, g_th[i], g_thw[i], g_thh[i], g_thw[i]); else fb_fill_rect((uint32_t)tx_, (uint32_t)yy, (uint32_t)tw_, (uint32_t)th, 0x00E2E8F0);
                fb_stroke_rrect_aa(tx_ - 1, yy - 1, tw_ + 2, th + 2, 2, i == g_page ? 0xFF38BDF8u : 0xFF334155u);
                char t[8] = ""; s_num(t, sizeof t, i + 1); tx(L.side_x + (L.side_w - tw(t, 11, 0)) / 2, yy + th + 2, 18, t, 11, i == g_page ? KC_ACC : KC_DIM, 0);
            }
            yy += th + 34;
        }
        if (g_np > MAXTH) { char t[48] = "+ "; s_num(t, sizeof t, g_np - MAXTH); s_cat(t, " paginas", sizeof t); tx(L.side_x + 10, yy, 20, t, 11, KC_FAINT, 0); }
        fb_reset_clip();
    }
    /* estado */
    fb_fill_rect(x, (uint32_t)L.st_y, w, ST_H, KC_BAR); fb_fill_rect(x, (uint32_t)L.st_y, w, 1, KC_LINE);
    char st[200] = "";
    if (g_pdf) {
        s_cat(st, g_title[0] ? g_title : g_path, sizeof st); s_cat(st, "   |   ", sizeof st);
        s_num(st, sizeof st, (int)(g_len / 1024)); s_cat(st, " KiB   |   pagina ", sizeof st);
        int pw = 0, ph = 0; pdf_page_size(g_pdf, g_page, &pw, &ph);
        s_num(st, sizeof st, pw * 254 / 720); s_cat(st, " x ", sizeof st); s_num(st, sizeof st, ph * 254 / 720); s_cat(st, " mm   |   ", sizeof st);
        s_num(st, sizeof st, g_ms); s_cat(st, " ms", sizeof st);
        if (g_unknown) { s_cat(st, "   |   ", sizeof st); s_num(st, sizeof st, g_unknown); s_cat(st, " elementos sin soporte", sizeof st); }
    } else s_cpy(st, "Lector de PDF de SOMA", sizeof st);
    fb_set_clip(L.x + 10, L.st_y, L.w / 2 + 120, ST_H);
    tx(L.x + 12, L.st_y, ST_H, st, 11, KC_DIM, 0);
    fb_reset_clip();
    if (g_msg[0]) { int mw = tw(g_msg, 11, 0); fb_set_clip(L.x + L.w / 2 + 140, L.st_y, L.w / 2 - 150, ST_H); tx(L.x + L.w - 14 - mw, L.st_y, ST_H, g_msg, 11, g_msg_ok ? 0x0034D399 : 0x00F87171, 0); fb_reset_clip(); }
    /* selector */
    if (g_picker) {
        int pw = 520 < L.w - 40 ? 520 : L.w - 40, ph = 420 < L.h - 80 ? 420 : L.h - 80;
        int px = L.x + (L.w - pw) / 2, py = L.y + BAR_H + 16;
        fb_fill_rect_alpha(L.x, L.y + BAR_H, L.w, L.h - BAR_H, 0x80000000u);
        fb_draw_shadow(px, py, pw, ph, 14, 24, 8, 0xA0000000u);
        fb_fill_rrect_aa(px, py, pw, ph, 14, 0xFF161D2Bu); fb_stroke_rrect_aa(px, py, pw, ph, 14, 0xFF2A3446u);
        tx(px + 22, py + 12, 34, "Abrir documento PDF", 17, KC_TXT, 1);
        char t[48] = ""; s_num(t, sizeof t, g_nfiles); s_cat(t, g_nfiles == 1 ? " archivo en RamFS" : " archivos en RamFS", sizeof t);
        tx(px + 22, py + 44, 20, t, 12, KC_DIM, 0);
        int ly = py + 74, lh = 40, vis = (ph - 74 - 16) / lh;
        if (!g_nfiles) {
            tx(px + 22, ly + 10, 24, "No hay ningun .pdf en RamFS.", 13, KC_TXT, 0);
            tx(px + 22, ly + 38, 22, "Descarga uno con:  descargar <url> /workspace/doc.pdf", 12, KC_DIM, 0);
            tx(px + 22, ly + 62, 22, "o abrelo desde BIGFS con doble clic en el explorador.", 12, KC_DIM, 0);
        }
        for (int i = 0; i < vis && i + g_pick_scroll < g_nfiles; i++) {
            int k = i + g_pick_scroll, yy = ly + i * lh;
            if (k == g_pick_sel) fb_fill_rrect_aa(px + 12, yy, pw - 24, lh - 4, 8, 0xFF1A3558u);
            pdf_glyph(px + 26, yy + 7, 16);
            tx(px + 56, yy, lh - 4, g_files[k], 13, KC_TXT, 0);
            int sz = vfs_size(g_files[k]); if (sz >= 0) { char z[24] = ""; s_num(z, sizeof z, (sz + 1023) / 1024); s_cat(z, " KiB", sizeof z); tx(px + pw - 30 - tw(z, 11, 0), yy, lh - 4, z, 11, KC_DIM, 0); }
        }
    }
}

/* ---------------------------------------------------------------- acciones */
static void go_page(int p) { if (!g_pdf) return; if (p < 0) p = 0; if (p >= g_np) p = g_np - 1; if (p != g_page) { g_page = p; g_scroll = 0; } }
static void set_zoom(int z) { g_zoom = z; g_scroll = 0; }
static void zoom_step(int dir, int vw, int vh)
{
    int cur = g_zoom > 0 ? g_zoom : scale_for(vw, vh) * 72 * 100 / 96 / 256;
    static const int steps[] = { 25, 33, 50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400 };
    int n = sizeof steps / sizeof steps[0], z = cur;
    if (dir > 0) { z = steps[n - 1]; for (int i = 0; i < n; i++) if (steps[i] > cur) { z = steps[i]; break; } }
    else { z = steps[0]; for (int i = n - 1; i >= 0; i--) if (steps[i] < cur) { z = steps[i]; break; } }
    set_zoom(z);
}
static void copy_text(void)
{
    if (!g_pdf) return;
    struct pdf_page pg; if (pdf_render_page(g_pdf, g_page, 256, &pg) != 0) return;
    int cap = 256 * 1024; char *t = (char *)kmalloc((size_t)cap);
    if (t) { int n = pdf_page_text(&pg, t, cap); if (n > 0) { clip_set_text(t, (uint32_t)n); clip_toast("Texto de la pagina copiado"); say("Texto de la pagina copiado al portapapeles", 1); } else say("Esta pagina no tiene texto (puede ser una imagen escaneada)", 0); kfree(t); }
    pdf_page_free(&pg);
}
static void open_picker(void) { scan_files(); g_picker = 1; g_pick_sel = 0; g_pick_scroll = 0; }
int pdfview_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct lay L; lay_of(&L, x, y, w, h);
    if (g_picker) {
        int pw = 520 < L.w - 40 ? 520 : L.w - 40, ph = 420 < L.h - 80 ? 420 : L.h - 80;
        int px = L.x + (L.w - pw) / 2, py = L.y + BAR_H + 16, ly = py + 74, lh = 40;
        if (!in_r(mx, my, px, py, pw, ph)) { g_picker = 0; return 0; }
        int i = (my - ly) / lh;
        if (my >= ly && i >= 0 && i + g_pick_scroll < g_nfiles) { pdfview_open_file(g_files[i + g_pick_scroll]); g_picker = 0; }
        return 0;
    }
    if (my < L.y + BAR_H) {
        struct btn b[12]; int nb = bar_buttons(&L, b);
        for (int i = 0; i < nb; i++) if (in_r(mx, my, b[i].x, L.y + 8, b[i].w, 32)) {
            switch (b[i].id) {
            case B_OPEN: open_picker(); break;
            case B_PREV: go_page(g_page - 1); break;
            case B_NEXT: go_page(g_page + 1); break;
            case B_ZOUT: zoom_step(-1, L.vw, L.vh); break;
            case B_ZIN: zoom_step(1, L.vw, L.vh); break;
            case B_FITW: set_zoom(0); break;
            case B_FITP: set_zoom(-1); break;
            case B_SIDE: g_side = !g_side; break;
            case B_COPY: copy_text(); break;
            }
            return 0;
        }
        return 0;
    }
    if (L.side_w && in_r(mx, my, L.side_x, L.vy, L.side_w, L.vh)) {
        int yy = L.vy + 12 - g_side_scroll;
        for (int i = 0; i < g_np && i < MAXTH; i++) { int th = g_th[i] ? g_thh[i] : 130; if (my >= yy - 6 && my < yy + th + 24) { go_page(i); break; } yy += th + 34; }
        return 0;
    }
    if (in_r(mx, my, L.vx, L.vy, L.vw, L.vh) && g_pdf) { g_drag = 1; g_drag_y = my; g_drag_s = g_scroll; }
    return 0;
}
int pdfview_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held)
{
    struct lay L; lay_of(&L, x, y, w, h);
    int r = 0;
    if (g_drag) {
        if (!held) g_drag = 0;
        else { int ns = g_drag_s - (my - g_drag_y); if (ns != g_scroll) { g_scroll = ns; r = 1; } }
        mouse_set_cursor(held ? CURSOR_MOVE : CURSOR_ARROW);
    }
    if (wheel) {
        if (g_picker) { g_pick_scroll += wheel > 0 ? 1 : -1; if (g_pick_scroll < 0) g_pick_scroll = 0; if (g_pick_scroll > g_nfiles - 1) g_pick_scroll = g_nfiles > 0 ? g_nfiles - 1 : 0; return 1; }
        if (L.side_w && in_r(mx, my, L.side_x, L.vy, L.side_w, L.vh)) { g_side_scroll += wheel * 60; if (g_side_scroll < 0) g_side_scroll = 0; return 1; }
        if (!g_pdf) return 0;
        int maxs = g_bh + 40 - L.vh; if (maxs < 0) maxs = 0;
        int ns = g_scroll + wheel * 80;
        if (ns > maxs && g_scroll >= maxs && wheel > 0 && g_page + 1 < g_np) { go_page(g_page + 1); return 1; }
        if (ns < 0 && g_scroll <= 0 && wheel < 0 && g_page > 0) { go_page(g_page - 1); g_scroll = 1 << 20; return 1; }
        g_scroll = ns < 0 ? 0 : ns > maxs ? maxs : ns; r = 1;
    }
    (void)mx;
    return r;
}
void pdfview_handle_key(char ch)
{
    int c = (uint8_t)ch;
    if (g_picker) {
        if (c == 27) g_picker = 0;
        else if (c == 16 && g_pick_sel > 0) { g_pick_sel--; if (g_pick_sel < g_pick_scroll) g_pick_scroll = g_pick_sel; }
        else if (c == 14 && g_pick_sel + 1 < g_nfiles) { g_pick_sel++; if (g_pick_sel >= g_pick_scroll + 8) g_pick_scroll = g_pick_sel - 7; }
        else if ((c == '\r' || c == '\n') && g_pick_sel < g_nfiles) { pdfview_open_file(g_files[g_pick_sel]); g_picker = 0; }
        return;
    }
    switch (c) {
    case 'o': case 'O': case 15: open_picker(); break;
    case 29: case 6: case 'n': go_page(g_page + 1); break;            /* AvPag, flecha derecha */
    case 28: case 2: case 'p': go_page(g_page - 1); break;            /* RePag, flecha izquierda */
    case 14: case 'j': g_scroll += 60; break;
    case 16: case 'k': g_scroll -= 60; break;
    case ' ': g_scroll += 400; break;
    case 'b': g_scroll -= 400; break;
    case 1: case 'g': go_page(0); break;
    case 5: case 'G': go_page(g_np - 1); break;
    case '+': case '=': zoom_step(1, 1000, 800); break;
    case '-': zoom_step(-1, 1000, 800); break;
    case 'w': case 'W': set_zoom(0); break;
    case 'f': case 'F': set_zoom(-1); break;
    case 't': case 'T': g_side = !g_side; break;
    case 'c': case 'C': case 3: copy_text(); break;
    default: break;
    }
    if (g_scroll < 0) g_scroll = 0;
}
int pdfview_clip_op(int op) { if (op == CLIP_OP_COPY) { copy_text(); return 1; } return 0; }


#undef g_path
#undef g_data
#undef g_len
#undef g_pdf
#undef g_np
#undef g_page
#undef g_err
#undef g_title
#undef g_msg
#undef g_msg_ok
#undef g_zoom
#undef g_scroll
#undef g_side
#undef g_picker
#undef g_pick_sel
#undef g_pick_scroll
#undef g_buf
#undef g_bw
#undef g_bh
#undef g_bpage
#undef g_bscale
#undef g_unknown
#undef g_ms
#undef g_drag
#undef g_drag_y
#undef g_drag_s
#undef g_side_scroll
#undef g_th
#undef g_thw
#undef g_thh
#undef g_fscale

/* SOMA_213_PDF: instancias */
int pdfview_inst_new(void)
{
    pv_boot();
    for (int k = 1; k <= PV_INST_MAX; ++k) {
        if (pv_used[k]) continue;
        pv_used[k] = 1;
        PV = &pv_insts[k];
        close_doc();
        pv_defaults(PV);
        return k;
    }
    return 0;
}
void pdfview_select(int slot) { pv_boot(); if (slot >= 1 && slot <= PV_INST_MAX) PV = &pv_insts[slot]; }
void pdfview_inst_drop(int slot)
{
    if (slot < 1 || slot > PV_INST_MAX || !pv_used[slot]) return;
    struct pv_inst *keep = PV;
    PV = &pv_insts[slot]; close_doc(); pv_defaults(PV);
    pv_used[slot] = 0;
    PV = keep == &pv_insts[slot] ? &pv_insts[1] : keep;
}
int pdfview_inst_find(const char *path)
{
    for (int k = 1; k <= PV_INST_MAX; ++k) {
        if (!pv_used[k] || !pv_insts[k].g_pdf || !path) continue;
        const char *a = pv_insts[k].g_path, *b = path;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return k;
    }
    return 0;
}
const char *pdfview_inst_name(int slot)
{
    if (slot < 1 || slot > PV_INST_MAX || !pv_insts[slot].g_pdf) return "";
    const char *p = pv_insts[slot].g_path, *n = p;
    for (; *p; ++p) if (*p == '/') n = p + 1;
    return n;
}
