/* SOMA_248_DOC: documentos paginados a partir de HTML+CSS o LaTeX. Ver docgen.h. */
#include "docgen.h"
#include "web_dom.h"
#include "web_css.h"
#include "web_layout.h"
#include "web_ua.h"
#include "web_util.h"
#include "web_font.h"
#include "web_img.h"
#include "tex.h"
#include "fb.h"
#ifdef WEB_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef WEB_HOST_TEST
#include "thread.h"
#include "mutex.h"
#include "gui.h"
/* fb_target_begin cambia el destino de TODO el dibujo: desde el hilo del agente hay que pedir turno de pantalla (el bucle de la
 * GUI se para en un punto seguro). En el hilo de la GUI o sin GUI, gui_turn_begin() no hace nada. */
#define SCR_BEGIN() int scr_tok_ = gui_turn_begin(); font_on()
#define SCR_END() font_off(); gui_turn_end(scr_tok_)
#else
#define SCR_BEGIN() font_on()
#define SCR_END() font_off()
#endif
/* los documentos siempre con Liberation Sans (anchos de Arial, como en el PDF), aunque el navegador use DejaVu (soma.webfont=off).
 * Todo el trabajo con el motor va en el turno de pantalla o en el hilo de la GUI: el cambio no se cruza con otro dibujo. */
static int g_font_depth, g_font_saved;
static void font_on(void) { if (g_font_depth++ == 0) { g_font_saved = g_wf182_on; g_wf182_on = 1; } }
static void font_off(void) { if (g_font_depth > 0 && --g_font_depth == 0) g_wf182_on = g_font_saved; }

/* ================================================================ hoja de estilo de la casa */
const char dg_house_css[] =
/* tema: el documento puede cambiar estas variables en :root */
":root{--acento:#1d4ed8;--acento-oscuro:#1e3a8a;--acento-suave:#eff6ff;--acento-borde:#bfdbfe;--tinta:#1f2937;--titulos:#0f172a;--tenue:#64748b;--linea:#e2e8f0;--fondo-suave:#f8fafc}"
"body{margin:0;font-family:'Liberation Sans',Arial,sans-serif;font-size:11pt;line-height:1.5;color:var(--tinta)}"
"h1,h2,h3,h4{color:var(--titulos);line-height:1.25;font-weight:bold}"
"h1{font-size:22pt;margin:0 0 10pt}"
"h2{font-size:15pt;margin:16pt 0 6pt;padding-bottom:3pt;border-bottom:1px solid var(--linea)}"
"h3{font-size:12.5pt;margin:12pt 0 4pt;color:var(--acento-oscuro)}"
"h4{font-size:11pt;margin:10pt 0 3pt}"
"p{margin:0 0 7pt;text-align:justify}"
"ul,ol{margin:0 0 8pt;padding-left:18pt}li{margin:0 0 3pt}"
"a{color:var(--acento);text-decoration:none}"
"strong,b{color:var(--titulos)}"
"small{font-size:9pt}"
"code{font-family:'DejaVu Sans Mono',monospace;font-size:9.5pt;background:#f1f5f9;padding:0 3px;border-radius:3px}"
"pre{font-family:'DejaVu Sans Mono',monospace;font-size:9pt;line-height:1.4;background:var(--fondo-suave);border:1px solid var(--linea);border-radius:5pt;padding:8pt 10pt;margin:6pt 0 10pt;white-space:pre-wrap}"
"hr{border:0;border-top:1px solid var(--linea);margin:12pt 0}"
"table{border-collapse:collapse;width:100%;margin:6pt 0 12pt;font-size:9.5pt}"
"th{background:var(--acento-oscuro);color:#fff;text-align:left;padding:5pt 7pt;font-weight:bold}"
"td{padding:4.5pt 7pt;border-bottom:1px solid var(--linea);vertical-align:top}"
"tbody tr:nth-child(even) td{background:var(--fondo-suave)}"
"td.num,th.num{text-align:right}"
"caption{caption-side:top;text-align:left;font-size:9pt;color:var(--tenue);padding-bottom:4pt}"
"blockquote,.cita{margin:10pt 0;padding:6pt 14pt;border-left:3pt solid var(--acento);color:#334155;font-style:italic;background:var(--fondo-suave)}"
"figure,.figura{margin:10pt 0;text-align:center}figure img,.figura img,figure svg{max-width:100%}"
"figcaption,.pie-figura{font-size:9pt;color:var(--tenue);margin-top:4pt;text-align:center}"
/* portada */
".portada{padding:34pt 0 22pt;border-bottom:3pt solid var(--acento);margin-bottom:16pt}"
".portada.pagina-completa{height:100vh;box-sizing:border-box;display:flex;flex-direction:column;justify-content:center;border:0;padding:0 10pt;margin:0}"
".sobretitulo,.etiqueta-seccion{font-size:9pt;letter-spacing:2px;text-transform:uppercase;color:var(--acento);font-weight:bold;margin-bottom:6pt}"
".titulo{font-size:28pt;line-height:1.15;font-weight:bold;color:var(--titulos);margin:0 0 8pt;border:0}"
".subtitulo{font-size:14pt;color:#475569;margin:0 0 12pt;line-height:1.35;text-align:left}"
".autores,.meta{color:#475569;font-size:10pt;margin:0 0 4pt;text-align:left}"
".afiliacion{color:var(--tenue);font-size:9pt;font-style:italic}"
".resumen{background:var(--fondo-suave);border-left:3pt solid #94a3b8;padding:8pt 12pt;font-size:10pt;margin:10pt 0 14pt}"
".resumen p{margin:0 0 4pt}"
".palabras-clave{font-size:9pt;color:#475569;margin-top:4pt}"
/* rejillas (el texto estrecho no se justifica: sin guiones quedarian huecos) */
".columnas{display:grid;grid-template-columns:1fr 1fr;gap:18pt}"
".columnas-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:14pt}"
".lateral{display:grid;grid-template-columns:2fr 1fr;gap:18pt}"
".columnas p,.columnas-3 p,.lateral p{text-align:left}"
/* destacados */
".destacado{background:var(--acento-suave);border:1px solid var(--acento-borde);border-radius:6pt;padding:8pt 12pt;margin:10pt 0}"
".aviso{background:#fffbeb;border:1px solid #fde68a;border-radius:6pt;padding:8pt 12pt;margin:10pt 0}"
".exito{background:#ecfdf5;border:1px solid #a7f3d0;border-radius:6pt;padding:8pt 12pt;margin:10pt 0}"
".destacado p,.aviso p,.exito p{margin:0 0 3pt;text-align:left}"
".kpis{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8pt;margin:8pt 0 12pt}"
".kpi{background:var(--fondo-suave);border:1px solid var(--linea);border-radius:6pt;padding:8pt 6pt;text-align:center}"
".kpi .valor{display:block;font-size:19pt;font-weight:bold;color:var(--acento);line-height:1.2}"
".kpi .etiqueta{display:block;font-size:8.5pt;color:var(--tenue)}"
".nota-pie{font-size:8.5pt;color:var(--tenue);border-top:1px solid var(--linea);padding-top:4pt;margin-top:12pt}"
".centro{text-align:center}.derecha{text-align:right}.tenue{color:var(--tenue)}"
".salto-pagina{height:0;margin:0;padding:0}"
/* barras de datos sin imagenes: <div class=barra><span style='width:62%'>62 %</span></div> */
".barra{background:var(--linea);border-radius:3pt;height:12pt;margin:2pt 0 6pt}"
".barra span{display:block;height:12pt;background:var(--acento);border-radius:3pt;color:#fff;font-size:8pt;line-height:12pt;padding-left:4pt}"
/* revista */
".entradilla{font-size:13pt;line-height:1.45;color:#334155;margin:0 0 12pt;text-align:left}"
".firma-articulo{font-size:9pt;color:var(--tenue);text-transform:uppercase;letter-spacing:1px;margin-bottom:10pt}"
".cita-grande{font-size:16pt;line-height:1.3;color:var(--acento);font-weight:bold;border-top:2pt solid var(--acento);border-bottom:2pt solid var(--acento);padding:8pt 0;margin:12pt 0;text-align:left}"
/* carta */
".remitente{text-align:right;font-size:10pt;color:#334155;margin-bottom:24pt}"
".destinatario{font-size:10.5pt;margin-bottom:18pt}"
".fecha{text-align:right;margin-bottom:18pt}"
".firma{margin-top:30pt}"
/* libro */
".capitulo{font-size:11pt;letter-spacing:3px;text-transform:uppercase;color:var(--tenue);text-align:center;margin:40pt 0 6pt}"
".titulo-capitulo{font-size:24pt;text-align:center;margin:0 0 28pt;border:0}"
".libro p{text-indent:14pt;margin:0 0 3pt}"
/* presentaciones: cada .diapositiva ocupa una pagina 16:9 */
".diapositiva{height:100vh;box-sizing:border-box;padding:46pt 60pt;display:flex;flex-direction:column;justify-content:center;background:#fff;overflow:hidden}"
".diapositiva h1{font-size:44pt;line-height:1.1;margin:0 0 14pt;border:0}"
".diapositiva h2{font-size:32pt;margin:0 0 18pt;border:0;color:var(--acento-oscuro)}"
".diapositiva h3{font-size:19pt;margin:0 0 8pt}"
".diapositiva p,.diapositiva li{font-size:20pt;line-height:1.4;text-align:left}"
".diapositiva .destacado p,.diapositiva .kpi,.diapositiva td,.diapositiva th{font-size:16pt}"
".diapositiva .kpi .valor{font-size:34pt}.diapositiva .kpi .etiqueta{font-size:13pt}"
".diapositiva .sobretitulo{font-size:13pt}.diapositiva .subtitulo{font-size:22pt}"
".diapositiva .columnas,.diapositiva .columnas-3{gap:22pt}"
".diapositiva li{margin-bottom:10pt}"
".diapositiva.oscura{background:var(--titulos);color:#e2e8f0}.diapositiva.oscura h1,.diapositiva.oscura h2{color:#fff}"
".diapositiva.acento{background:var(--acento);color:#fff}.diapositiva.acento h1,.diapositiva.acento h2,.diapositiva.acento strong{color:#fff}"
".diapositiva .pie-diapositiva{font-size:12pt;color:#94a3b8;margin-top:18pt}"
/* email (600 px, estilos sencillos para que se vea igual en cualquier cliente) */
".email{width:600px;margin:20px auto;background:#fff;border:1px solid var(--linea);border-radius:8px}"
".email-cabecera{background:var(--acento-oscuro);color:#fff;padding:20px 28px;border-radius:8px 8px 0 0;font-size:18pt;font-weight:bold}"
".email-cuerpo{padding:22px 28px}"
".email-cuerpo p{text-align:left}"
".email-pie{padding:14px 28px;font-size:8.5pt;color:var(--tenue);border-top:1px solid var(--linea)}"
".boton{display:inline-block;background:var(--acento);color:#fff;padding:9px 18px;border-radius:6px;font-weight:bold;text-decoration:none}";

/* ================================================================ estado */
struct dg_img { char *key; int w, h; uint32_t *px; int pxw, pxh; int state; };   /* w,h natural; pxw,pxh pixeles guardados */
#define DG_MAX_IMG 48
#define DG_MAX_SVG 32

struct dg_doc {
    int kind, zoom;
    int pw, ph, ml, mr, mt, mb, cw, ch;          /* pagina y margenes en px (con zoom) */
    int pw_pt, ph_pt;
    int numbers, slides, single;
    char header[96], footer[96], fmt[16], title[128];
    uint32_t page_bg;
    int npages; int cut[DG_MAX_PAGES + 1];
    int doc_h;
    /* HTML */
    char *src;
    struct wdoc *wd; struct wcss *css; struct wlay *L;
    struct dg_img img[DG_MAX_IMG]; int nimg;
    char *svg[DG_MAX_SVG]; uint32_t svglen[DG_MAX_SVG]; int nsvg;
    /* LaTeX */
    struct tex_out to; struct tex_op *ops; char *pool;
};

static dg_load_fn g_loader;
void dg_set_loader(dg_load_fn f) { g_loader = f; }

static int dl(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void dcpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } if (max) d[i] = 0; }
static int dlower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int dstarts_ci(const char *s, const char *p) { while (*p) { if (dlower(*s) != dlower(*p)) return 0; s++; p++; } return 1; }
static const char *dfind_ci(const char *h, const char *n) { if (!h) return 0; for (; *h; h++) if (dstarts_ci(h, n)) return h; return 0; }
static void seterr(char *err, uint32_t max, const char *m) { if (err && max) dcpy(err, m, (int)max); }

/* ---- la escala del CSS solo vale en el hilo que maqueta un documento (el navegador sigue a 100 %) */
#ifndef WEB_HOST_TEST
static struct kmutex g_dg_lock; static int g_dg_lock_init; static volatile uint32_t g_dg_tid;
static int dg_zoom_mine(void) { struct tcb *t = thread_current(); return t && t->tid == g_dg_tid; }
static void zoom_begin(int z) { if (!g_dg_lock_init) { kmutex_init(&g_dg_lock, "docgen"); g_dg_lock_init = 1; } kmutex_lock(&g_dg_lock); struct tcb *t = thread_current(); g_dg_tid = t ? t->tid : 0; g_wc_zoom_owner = dg_zoom_mine; g_wc_zoom = z; font_on(); }
static void zoom_end(void) { g_wc_zoom = 100; font_off(); kmutex_unlock(&g_dg_lock); }
#else
static void zoom_begin(int z) { g_wc_zoom = z; font_on(); }
static void zoom_end(void) { g_wc_zoom = 100; font_off(); }
#endif
static int Z(const struct dg_doc *d, int v) { return v * d->zoom / 100; }

/* ================================================================ imagenes (archivos y <svg> en linea) */
static void svg_text_overlay(struct dg_doc *d, const char *svg, uint32_t len, uint32_t *px, int w, int h);
static void fill_info(struct dg_doc *d, struct dg_img *m, int idx, struct wimginfo *out)
{
    out->state = m->state;
    if (m->state != 2) return;
    out->w = m->w * d->zoom / 100; out->h = m->h * d->zoom / 100; out->px = m->px; out->serial = (uint32_t)idx + 1;
    out->pw = m->pxw; out->ph = m->pxh;
}
static int img_cb(void *ud, const char *url, struct wimginfo *out)
{
    struct dg_doc *d = (struct dg_doc *)ud;
    for (int i = 0; i < d->nimg; i++) if (wu_streq(d->img[i].key, url)) { fill_info(d, &d->img[i], i, out); return 1; }
    if (d->nimg >= DG_MAX_IMG) { out->state = 3; return 1; }
    struct dg_img *m = &d->img[d->nimg++];
    int kl = dl(url); m->key = (char *)kmalloc((size_t)kl + 1); if (!m->key) { d->nimg--; out->state = 3; return 1; }
    for (int i = 0; i <= kl; i++) m->key[i] = url[i];
    m->state = 3;
    struct wimg im; memset(&im, 0, sizeof im);
    if (url[0] == 's' && url[1] == 'v' && url[2] == 'g' && url[3] == ':') {             /* <svg> en linea */
        int k = wu_atoi(url + 4);
        if (k >= 0 && k < d->nsvg && d->svg[k] && wi_decode_svg((const uint8_t *)d->svg[k], d->svglen[k], &im, 0, 0, 0x1f2937) == 0 && im.px) {
            int nw = im.nw ? im.nw : im.w, nh = im.nh ? im.nh : im.h;
            if (d->zoom != 100) {                                     /* se rasteriza a la escala del documento (nitido) */
                struct wimg im2; memset(&im2, 0, sizeof im2);
                if (wi_decode_svg((const uint8_t *)d->svg[k], d->svglen[k], &im2, nw * d->zoom / 100, nh * d->zoom / 100, 0x1f2937) == 0 && im2.px) { wi_free(&im); im = im2; }
            }
            m->px = im.px; m->pxw = im.w; m->pxh = im.h; m->w = nw; m->h = nh; m->state = 2;
            /* sobre el fondo de la pagina: el texto suavizado no se puede mezclar con pixeles transparentes */
            uint32_t bg = d->page_bg;
            for (int i = 0; i < m->pxw * m->pxh; i++) {
                uint32_t c = m->px[i]; uint32_t a = c >> 24;
                if (a == 255) continue;
                uint32_t r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
                m->px[i] = 0xFF000000u | r << 16 | g << 8 | b;
            }
            svg_text_overlay(d, d->svg[k], d->svglen[k], m->px, m->pxw, m->pxh);   /* el rasterizador no dibuja <text> */
        }
    } else if (g_loader) {
        uint8_t *data = 0; uint32_t n = 0;
        if (g_loader(url, &data, &n) == 0 && data) {
            if (wi_decode(data, n, &im) == 0 && im.px) { m->px = im.px; m->pxw = im.w; m->pxh = im.h; m->w = im.nw ? im.nw : im.w; m->h = im.nh ? im.nh : im.h; m->state = 2; }
            kfree(data);
        }
    }
    fill_info(d, m, d->nimg - 1, out);
    return 1;
}

/* ---- <text> de los SVG (graficos: etiquetas de ejes, leyendas, valores). x, y, font-size, font-weight, fill y text-anchor;
 * se respeta el viewBox. Se dibuja con la fuente del documento encima de la imagen ya rasterizada. */
static int attr_num(const char *tag, const char *tag_end, const char *name, int *out)
{
    int nl = dl(name);
    for (const char *p = tag; p + nl + 2 < tag_end; p++) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t') && dstarts_ci(p, name) && p[nl] == '=' && (p[nl + 1] == '"' || p[nl + 1] == '\'')) {
            const char *v = p + nl + 2; int neg = 0, val = 0, frac = 0, fd = 0;
            if (*v == '-') { neg = 1; v++; }
            while (*v >= '0' && *v <= '9') val = val * 10 + (*v++ - '0');
            if (*v == '.') { v++; while (*v >= '0' && *v <= '9') { if (fd < 2) { frac = frac * 10 + (*v - '0'); fd++; } v++; } }
            while (fd < 2) { frac *= 10; fd++; }
            *out = (neg ? -1 : 1) * (val * 100 + frac); return 1;               /* en centesimas */
        }
    }
    return 0;
}
static int attr_str(const char *tag, const char *tag_end, const char *name, char *out, int max)
{
    int nl = dl(name);
    for (const char *p = tag; p + nl + 2 < tag_end; p++) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t') && dstarts_ci(p, name) && p[nl] == '=' && (p[nl + 1] == '"' || p[nl + 1] == '\'')) {
            char q = p[nl + 1]; const char *v = p + nl + 2; int i = 0;
            while (v < tag_end && *v != q && i < max - 1) out[i++] = *v++;
            out[i] = 0; return 1;
        }
    }
    return 0;
}
static uint32_t svg_color(const char *v, uint32_t def)
{
    if (!v || !v[0]) return def;
    int ok = 0; uint32_t c = wc_parse_color(v, &ok);
    return ok ? (c & 0xFFFFFF) : def;
}
static void svg_text_overlay(struct dg_doc *d, const char *svg, uint32_t len, uint32_t *px, int w, int h)
{
    (void)d;
    const char *e = svg + len;
    /* viewBox (o width/height) */
    const char *st = svg; const char *ste = st; while (ste < e && *ste != '>') ste++;
    int vbx = 0, vby = 0, vbw = 0, vbh = 0;
    char vb[96];
    if (attr_str(st, ste, "viewBox", vb, sizeof vb)) {
        int v[4] = {0, 0, 0, 0}, k = 0; const char *p = vb;
        while (*p && k < 4) { while (*p == ' ' || *p == ',') p++; int neg = 0; if (*p == '-') { neg = 1; p++; } int a = 0; while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0'); if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; } v[k++] = neg ? -a : a; }
        vbx = v[0]; vby = v[1]; vbw = v[2]; vbh = v[3];
    }
    if (vbw <= 0 || vbh <= 0) { int a = 0; if (attr_num(st, ste, "width", &a)) vbw = a / 100; if (attr_num(st, ste, "height", &a)) vbh = a / 100; }
    if (vbw <= 0 || vbh <= 0) return;
    int ntext = 0;
    SCR_BEGIN();
    fb_target_begin(px, (uint32_t)w, (uint32_t)h);
    fb_set_clip(0, 0, w, h);
    for (const char *p = svg; p + 6 < e && ntext < 400; p++) {
        if (!(p[0] == '<' && dstarts_ci(p + 1, "text") && (p[5] == ' ' || p[5] == '>'))) continue;
        const char *te = p; while (te < e && *te != '>') te++;
        if (te >= e) break;
        const char *c0 = te + 1, *c1 = c0; while (c1 < e && !(c1[0] == '<' && c1[1] == '/' && dstarts_ci(c1 + 2, "text"))) c1++;
        int x = 0, y = 0, fs = 1600; attr_num(p, te, "x", &x); attr_num(p, te, "y", &y); attr_num(p, te, "font-size", &fs);
        char fill[40] = "", anchor[16] = "", weight[16] = "";
        attr_str(p, te, "fill", fill, sizeof fill); attr_str(p, te, "text-anchor", anchor, sizeof anchor); attr_str(p, te, "font-weight", weight, sizeof weight);
        /* contenido sin etiquetas internas (<tspan>) y con las entidades basicas */
        char txt[256]; int n = 0;
        for (const char *q = c0; q < c1 && n < 250; q++) {
            if (*q == '<') { while (q < c1 && *q != '>') q++; continue; }
            if (*q == '&') { if (dstarts_ci(q, "&amp;")) { txt[n++] = '&'; q += 4; continue; } if (dstarts_ci(q, "&lt;")) { txt[n++] = '<'; q += 3; continue; } if (dstarts_ci(q, "&gt;")) { txt[n++] = '>'; q += 3; continue; } }
            if (*q == '\n' || *q == '\t') { if (n && txt[n - 1] != ' ') txt[n++] = ' '; continue; }
            txt[n++] = *q;
        }
        while (n && txt[n - 1] == ' ') n--;
        txt[n] = 0;
        int a = 0; while (txt[a] == ' ') a++;
        if (txt[a]) {
            int64_t sx = (int64_t)w * 1000 / vbw, sy = (int64_t)h * 1000 / vbh;
            int fpx = (int)((int64_t)fs * sy / 100000); if (fpx < 6) fpx = 6;
            int X = (int)(((int64_t)x / 100 - vbx) * sx / 1000), Y = (int)(((int64_t)y / 100 - vby) * sy / 1000);
            int fl = WF_RAW | WF_WEB | ((weight[0] == 'b' || (weight[0] >= '6' && weight[0] <= '9')) ? WF_BOLD : 0);
            int tw = wf_width(txt + a, (uint32_t)dl(txt + a), fpx, fl);
            if (dstarts_ci(anchor, "middle")) X -= tw / 2; else if (dstarts_ci(anchor, "end")) X -= tw;
            wf_draw(X, Y, txt + a, (uint32_t)dl(txt + a), fpx, svg_color(fill, 0x1f2937), fl, 255);
            ntext++;
        }
        p = c1;
    }
    fb_reset_clip();
    fb_target_end();
    SCR_END();
    /* el dibujo del nucleo escribe los pixeles con alfa 0; la imagen se compone luego con alfa: todo opaco (ya va sobre el fondo) */
    for (int i = 0; i < w * h; i++) px[i] |= 0xFF000000u;
}

/* los <svg> en linea se guardan como texto y se dibujan como imagen (como hace el navegador con su pasarela) */
static void collect_svg(struct dg_doc *d, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_SVG && d->nsvg < DG_MAX_SVG) {
            struct wb b = {0}; wd_outer_html(n, &b, 0);
            if (b.p && !b.err) {
                char key[16] = "svg:"; int k = d->nsvg, p = 4; char t[8]; int tn = 0; do { t[tn++] = (char)('0' + k % 10); k /= 10; } while (k); while (tn) key[p++] = t[--tn]; key[p] = 0;
                d->svg[d->nsvg] = b.p; d->svglen[d->nsvg] = b.len; d->nsvg++;
                wd_set_attr(d->wd, n, "data-soma-svg", wa_strdup(&d->wd->ar, key));
                b.p = 0;
            }
            wb_free(&b);
            continue;
        }
        if (n->first) collect_svg(d, n->first);
    }
}
static void collect_styles(struct wcss *c, struct wnode *n)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_STYLE) { struct wb b = {0}; wd_text_content(n, &b); if (b.p) wc_add_sheet(c, b.p, b.len, 1); wb_free(&b); }
        if (n->first) collect_styles(c, n->first);
    }
}

/* ================================================================ formato de pagina */
static int mm_px(const struct dg_doc *d, int mm) { return Z(d, mm * 378 / 100); }
static void parse_fmt(struct dg_doc *d, const char *v)
{
    /* tamanos en px CSS (96 ppp) */
    int w = 794, h = 1123, m = 20, ptw = 595, pth = 842;
    dcpy(d->fmt, "A4", sizeof d->fmt);
    d->numbers = 1;
    if (v) {
        if (dfind_ci(v, "16:9") || dfind_ci(v, "presentaci") || dfind_ci(v, "diapositiva")) { w = 1280; h = 720; m = 0; ptw = 960; pth = 540; d->slides = 1; d->numbers = 0; dcpy(d->fmt, "16:9", sizeof d->fmt); }
        else if (dfind_ci(v, "email") || dfind_ci(v, "correo")) { w = 720; h = 0; m = 0; ptw = 540; pth = 0; d->single = 1; d->numbers = 0; dcpy(d->fmt, "email", sizeof d->fmt); }
        else if (dfind_ci(v, "apaisad") || dfind_ci(v, "landscape")) { w = 1123; h = 794; ptw = 842; pth = 595; dcpy(d->fmt, "A4 apaisado", sizeof d->fmt); }
        else if (dfind_ci(v, "A5") || dfind_ci(v, "libro")) { w = 559; h = 794; m = 16; ptw = 420; pth = 595; dcpy(d->fmt, "A5", sizeof d->fmt); }
        else if (dfind_ci(v, "carta") || dfind_ci(v, "letter")) { w = 816; h = 1056; ptw = 612; pth = 792; dcpy(d->fmt, "carta", sizeof d->fmt); }
        const char *q;
        if ((q = dfind_ci(v, "margen="))) { int mm = wu_atoi(q + 7); if (mm >= 0 && mm <= 60) m = mm; }
        if ((q = dfind_ci(v, "numeros="))) d->numbers = !(dlower(q[8]) == 'n');
        if ((q = dfind_ci(v, "pie="))) { int i = 0; q += 4; while (q[i] && q[i] != ';' && i < (int)sizeof d->footer - 1) { d->footer[i] = q[i]; i++; } d->footer[i] = 0; }
        if ((q = dfind_ci(v, "cabecera="))) { int i = 0; q += 9; while (q[i] && q[i] != ';' && i < (int)sizeof d->header - 1) { d->header[i] = q[i]; i++; } d->header[i] = 0; }
    }
    d->pw = Z(d, w); d->ph = Z(d, h); d->pw_pt = ptw; d->ph_pt = pth;
    d->ml = d->mr = mm_px(d, m); d->mt = mm_px(d, m); d->mb = mm_px(d, m);
    if (m > 0 && d->numbers) d->mb = mm_px(d, m + 4);                     /* sitio para el numero de pagina */
    if (m > 0 && d->header[0]) d->mt = mm_px(d, m + 4);
    d->cw = d->pw - d->ml - d->mr; d->ch = d->ph ? d->ph - d->mt - d->mb : 0;
}
static const char *meta_content(struct wnode *n, const char *name)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && n->tag == WT_META) { const char *nm = wd_attr(n, "name"); if (nm && wu_strieq(nm, name)) return wd_attr(n, "content"); }
        if (n->first) { const char *r = meta_content(n->first, name); if (r) return r; }
    }
    return 0;
}

/* ================================================================ dibujo de una franja del documento en un bufer */
static void tex_paint(struct dg_doc *d, int ox, int oy, int y0, int y1);
static void paint_band(struct dg_doc *d, uint32_t *buf, int w, int h, int doc_y, uint32_t bg)
{
    for (int i = 0; i < w * h; i++) buf[i] = 0xFF000000u | bg;
    SCR_BEGIN();
    fb_target_begin(buf, (uint32_t)w, (uint32_t)h);
    fb_set_clip(0, 0, w, h);
    if (d->kind == DG_HTML) wl_paint(d->L, 0, 0, w, h, 0, doc_y);
    else tex_paint(d, 0, -doc_y, 0, h);
    fb_reset_clip();
    fb_target_end();
    SCR_END();
}

/* ================================================================ LaTeX */
static int t_width(const char *s, int n, int px, int st, void *u) { (void)u; int fl = (st & TS_BOLD ? WF_BOLD : 0) | (st & TS_ITALIC ? WF_ITALIC : 0) | (st & TS_MONO ? WF_MONO : 0); return wf_width(s, (uint32_t)n, px, fl | WF_RAW | WF_WEB); }
static int t_asc(int px, void *u) { (void)u; return wf_web_ascent(px); }
static int t_desc(int px, void *u) { (void)u; return wf_web_descent(px); }
static int t_image(const char *path, int *w, int *h, void *u)
{
    struct dg_doc *d = (struct dg_doc *)u; struct wimginfo ii; memset(&ii, 0, sizeof ii);
    img_cb(d, path, &ii); if (ii.state != 2) return -1;
    *w = ii.w; *h = ii.h; return 0;
}
static void tex_paint(struct dg_doc *d, int ox, int oy, int y_min, int y_max)
{
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < d->to.nops; i++) {
            const struct tex_op *p = &d->ops[i];
            int bg = p->type == TOP_RECT && p->style == 1;
            if ((pass == 0) != bg) continue;
            if (p->type == TOP_TEXT) {
                int y = oy + p->y; if (y + p->px < y_min || y - p->px > y_max) continue;
                int fl = (p->style & TS_BOLD ? WF_BOLD : 0) | (p->style & TS_ITALIC ? WF_ITALIC : 0) | (p->style & TS_MONO ? WF_MONO : 0);
                wf_draw(ox + p->x, y, d->pool + p->s, p->n, p->px, p->rgb, fl | WF_RAW | WF_WEB, 255);
                if (p->style & TS_UNDER) fb_fill_rect((uint32_t)(ox + p->x), (uint32_t)(y + 2), (uint32_t)t_width(d->pool + p->s, (int)p->n, p->px, p->style, 0), 1, p->rgb);
            } else if (p->type == TOP_RECT) {
                int y = oy + p->y; if (y + p->h < y_min || y > y_max) continue;
                fb_fill_rect((uint32_t)(ox + p->x), (uint32_t)(y < 0 ? 0 : y), (uint32_t)(p->w > 0 ? p->w : 1), (uint32_t)(p->h > 0 ? (y < 0 ? p->h + y : p->h) : 1), p->rgb);
            } else if (p->type == TOP_LINE) {
                int y = oy + p->y / 16; if (y < y_min - 400 || y > y_max + 400) continue;
                fb_line_aa(ox * 16 + p->x, oy * 16 + p->y, ox * 16 + p->x + p->w, oy * 16 + p->y + p->h, p->px, 0xFF000000u | p->rgb);
            } else if (p->type == TOP_IMAGE) {
                int y = oy + p->y; if (y + p->h < y_min || y > y_max) continue;
                for (int k = 0; k < d->nimg; k++) if (wu_streq(d->img[k].key, d->pool + p->s) && d->img[k].state == 2) { fb_blit_scale(ox + p->x, y, p->w, p->h, d->img[k].px, d->img[k].pxw, d->img[k].pxh, d->img[k].pxw); break; }
            }
        }
}

/* ================================================================ paginacion */
/* candidatos de corte: el principio de cada bloque (parrafo, titulo, elemento de lista, fila de tabla, figura...) */
#define DG_MAX_CAND 4096
struct cand { int y; uint8_t heading, forced; int bottom; };
static int is_block_tag(int t)
{
    return t == WT_P || (t >= WT_H1 && t <= WT_H6) || t == WT_LI || t == WT_TR || t == WT_FIGURE || t == WT_TABLE || t == WT_BLOCKQUOTE || t == WT_PRE ||
           t == WT_IMG || t == WT_HR || t == WT_SECTION || t == WT_ARTICLE || t == WT_DIV || t == WT_UL || t == WT_OL || t == WT_DL || t == WT_DT || t == WT_DD ||
           t == WT_HEADER || t == WT_FOOTER || t == WT_ASIDE || t == WT_FIGCAPTION;
}
static int forced_break(struct wnode *n)
{
    if (wd_has_class(n, "salto-pagina") || wd_has_class(n, "diapositiva") || wd_has_class(n, "slide") || wd_has_class(n, "page-break") || wd_has_class(n, "nueva-pagina")) return 1;
    const char *st = wd_attr(n, "style");
    if (st && (dfind_ci(st, "page-break-before:always") || dfind_ci(st, "page-break-before: always") || dfind_ci(st, "break-before:page") || dfind_ci(st, "break-before: page"))) return 1;
    return 0;
}
static int forced_after(struct wnode *n)
{
    if (wd_has_class(n, "portada") && wd_has_class(n, "pagina-completa")) return 1;
    const char *st = wd_attr(n, "style");
    return st && (dfind_ci(st, "page-break-after:always") || dfind_ci(st, "page-break-after: always") || dfind_ci(st, "break-after:page") || dfind_ci(st, "break-after: page"));
}
static void collect_cands(struct dg_doc *d, struct wnode *n, struct cand *c, int *nc)
{
    for (; n; n = n->next) {
        if (n->type == WN_ELEM && is_block_tag(n->tag)) {
            int x, y, w, h;
            if (wl_node_rect(d->L, n, &x, &y, &w, &h) && h > 0 && *nc < DG_MAX_CAND - 1) {
                struct cand *k = &c[(*nc)++]; k->y = y; k->bottom = y + h; k->heading = (uint8_t)(n->tag >= WT_H1 && n->tag <= WT_H4); k->forced = (uint8_t)forced_break(n);
                if (forced_after(n) && *nc < DG_MAX_CAND - 1) { struct cand *a = &c[(*nc)++]; a->y = y + h; a->bottom = y + h; a->heading = 0; a->forced = 1; }
            }
        }
        if (n->first && !(n->type == WN_ELEM && n->tag == WT_SVG)) collect_cands(d, n->first, c, nc);
    }
}
static void tex_cands(struct dg_doc *d, struct cand *c, int *nc)
{
    /* LaTeX: la parte de arriba de cada linea de texto (las operaciones de texto agrupadas por linea base) */
    for (int i = 0; i < d->to.nops && *nc < DG_MAX_CAND - 1; i++) {
        const struct tex_op *p = &d->ops[i];
        if (p->type != TOP_TEXT) continue;
        int top = p->y - wf_web_ascent(p->px) - 2;
        int dup = 0; for (int k = *nc - 1; k >= 0 && k >= *nc - 6; k--) if (c[k].y == top) { dup = 1; break; }
        if (!dup) { struct cand *k = &c[(*nc)++]; k->y = top; k->bottom = p->y; k->heading = (uint8_t)(p->px > d->zoom * 15 / 100 * 13 / 10 && (p->style & TS_BOLD)); k->forced = 0; }
    }
}
static int cmp_cand(const void *a, const void *b) { return ((const struct cand *)a)->y - ((const struct cand *)b)->y; }
static void sort_cands(struct cand *c, int n) { for (int i = 1; i < n; i++) { struct cand t = c[i]; int j = i - 1; while (j >= 0 && cmp_cand(&c[j], &t) > 0) { c[j + 1] = c[j]; j--; } c[j + 1] = t; } }

/* fila uniforme (sin tinta): se puede cortar por ella sin partir letras */
static int row_free(const uint32_t *r, int w) { uint32_t v = r[0]; for (int i = 1; i < w; i++) if (r[i] != v) return 0; return 1; }

static void paginate(struct dg_doc *d)
{
    d->npages = 0;
    if (d->single || d->ch <= 0) {                                          /* una sola pagina con todo (email) */
        d->ph = d->doc_h + d->mt + d->mb; d->ch = d->doc_h; d->ph_pt = d->ph * 72 / (96 * d->zoom / 100);
        d->cut[0] = 0; d->cut[1] = d->doc_h; d->npages = 1; return;
    }
    struct cand *c = (struct cand *)kmalloc(sizeof(struct cand) * DG_MAX_CAND); int nc = 0;
    if (!c) { int y = 0; while (y < d->doc_h && d->npages < DG_MAX_PAGES) { d->cut[d->npages++] = y; y += d->ch; } d->cut[d->npages] = d->doc_h; return; }
    if (d->kind == DG_HTML) collect_cands(d, d->wd->root, c, &nc); else tex_cands(d, c, &nc);
    sort_cands(c, nc);
    int band = d->ch * 45 / 100; if (band < 8) band = 8;
    uint32_t *buf = (uint32_t *)kmalloc((size_t)d->cw * (size_t)band * 4);
    int y = 0;
    while (y < d->doc_h - 2 && d->npages < DG_MAX_PAGES) {
        d->cut[d->npages++] = y;
        int limit = y + d->ch;
        /* salto forzado dentro de esta pagina */
        int forced = -1;
        for (int i = 0; i < nc; i++) if (c[i].forced && c[i].y > y + 4 && c[i].y <= limit) { forced = c[i].y; break; }
        if (forced >= 0) { y = forced; continue; }
        if (limit >= d->doc_h) break;
        int cut = limit, lo = limit - band;
        if (buf) {
            paint_band(d, buf, d->cw, band, lo, 0xFFFFFF);
            /* filas libres en la franja; se prefiere el principio de un bloque y nunca justo despues de un titulo */
            int best = -1, best_sc = -1;
            for (int r = band - 1; r >= 1; r--) {
                if (!row_free(buf + (size_t)r * d->cw, d->cw)) continue;
                int yy = lo + r, sc = 1;
                int run = 0; while (r - run >= 0 && row_free(buf + (size_t)(r - run) * d->cw, d->cw)) run++;
                int top_of_run = yy - run + 1;
                for (int i = 0; i < nc; i++) if (c[i].y >= top_of_run - 1 && c[i].y <= yy + 1) { sc += 4; break; }
                for (int i = 0; i < nc; i++) if (c[i].heading && c[i].bottom >= top_of_run - Z(d, 30) && c[i].bottom <= yy && c[i].y > y) { sc = -100; break; }
                if (run >= Z(d, 6)) sc += 2;                                     /* hueco entre parrafos */
                sc = sc * 100 + r * 100 / band;                                   /* a igualdad, lo mas abajo posible */
                if (sc > best_sc) { best_sc = sc; best = top_of_run + run / 2; }
                r -= run - 1;
            }
            if (best > y + d->ch / 4 && best_sc > 0) cut = best;
            /* un titulo justo antes del corte pasa a la pagina siguiente */
            for (int i = 0; i < nc; i++) if (c[i].heading && c[i].y > y + d->ch / 3 && c[i].y < cut && c[i].bottom + Z(d, 40) > cut) { cut = c[i].y; break; }
        }
        /* la pagina siguiente empieza sin blanco arriba */
        y = cut;
        if (buf && y < d->doc_h) {
            int skip = 0, maxs = Z(d, 24);
            uint32_t *row = (uint32_t *)kmalloc((size_t)d->cw * 4);
            if (row) {
                for (; skip < maxs && y + skip < d->doc_h; skip++) { paint_band(d, row, d->cw, 1, y + skip, 0xFFFFFF); if (!row_free(row, d->cw) || row[0] != 0xFFFFFFFFu) break; }
                kfree(row);
            }
            y += skip;
        }
    }
    if (d->npages == 0) d->cut[d->npages++] = 0;
    d->cut[d->npages] = d->doc_h;
    /* ultima pagina vacia (solo margen): fuera */
    while (d->npages > 1 && d->cut[d->npages] - d->cut[d->npages - 1] < Z(d, 6)) { d->npages--; d->cut[d->npages] = d->doc_h; }
    if (buf) kfree(buf);
    kfree(c);
}

/* ================================================================ abrir */
static int is_tex(const char *s, uint32_t n)
{
    uint32_t i = 0; while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    if (i < n && s[i] == '<') return 0;
    return tex_is_latex(s, n);
}
static uint32_t page_bg_of(struct dg_doc *d)
{
    uint32_t bg = 0xFFFFFF;
    if (d->kind == DG_HTML && d->wd->body && d->wd->body->st && (d->wd->body->st->bg_color >> 24)) bg = d->wd->body->st->bg_color & 0xFFFFFF;
    else if (d->kind == DG_HTML && d->wd->html && d->wd->html->st && (d->wd->html->st->bg_color >> 24)) bg = d->wd->html->st->bg_color & 0xFFFFFF;
    return bg;
}

struct dg_doc *dg_open(const char *src, uint32_t len, int zoom, char *err, uint32_t errmax)
{
    if (!src || !len) { seterr(err, errmax, "documento vacio"); return 0; }
    if (zoom < 50) zoom = 50;
    if (zoom > 300) zoom = 300;
    struct dg_doc *d = (struct dg_doc *)kmalloc(sizeof *d);
    if (!d) { seterr(err, errmax, "sin memoria"); return 0; }
    memset(d, 0, sizeof *d);
    d->zoom = zoom; d->page_bg = 0xFFFFFF;
    wf_init();
    if (is_tex(src, len)) {
        d->kind = DG_TEX;
        const char *pm = dfind_ci(src, "%soma-pagina:");
        char fmt[96] = ""; if (pm) { int i = 0; pm += 13; while (pm[i] && pm[i] != '\n' && i < 95) { fmt[i] = pm[i]; i++; } fmt[i] = 0; }
        parse_fmt(d, fmt[0] ? fmt : "A4");
        d->ops = (struct tex_op *)kmalloc(sizeof(struct tex_op) * 60000); d->pool = (char *)kmalloc(1u << 20);
        if (!d->ops || !d->pool) { seterr(err, errmax, "sin memoria"); dg_close(d); return 0; }
        d->to.ops = d->ops; d->to.cap = 60000; d->to.pool = d->pool; d->to.poolcap = 1u << 20;
        struct tex_metrics m = { t_width, t_asc, t_desc, t_image, d };
        tex_typeset(src, len, d->cw, Z(d, 15), &m, &d->to);
        d->doc_h = d->to.height;
        dcpy(d->title, d->to.title, sizeof d->title);
    } else {
        d->kind = DG_HTML;
        d->src = (char *)kmalloc(len + 1);
        if (!d->src) { seterr(err, errmax, "sin memoria"); dg_close(d); return 0; }
        memcpy(d->src, src, len); d->src[len] = 0;
        d->wd = wd_new();
        if (!d->wd) { seterr(err, errmax, "sin memoria"); dg_close(d); return 0; }
        wd_parse(d->wd, d->src, len);
        parse_fmt(d, meta_content(d->wd->root, "soma-pagina"));
        if (d->wd->title) dcpy(d->title, d->wd->title, sizeof d->title);
        collect_svg(d, d->wd->root);
        zoom_begin(zoom);
        d->css = wc_new(d->wd);
        wc_add_sheet(d->css, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0);
        wc_add_sheet(d->css, dg_house_css, (uint32_t)(sizeof dg_house_css - 1), 1);     /* de autor: gana a la del navegador */
        collect_styles(d->css, d->wd->root);
        int vh = d->ch > 0 ? d->ch : Z(d, 1000);
        wc_compute(d->css, d->cw, vh);
        d->page_bg = page_bg_of(d);
        d->L = wl_new(d->wd, d->css, img_cb, d);
        if (d->L) { d->L->ar.limit = 96u << 20; wl_layout(d->L, d->cw, vh); }
        zoom_end();
        if (!d->L) { seterr(err, errmax, "no se pudo maquetar"); dg_close(d); return 0; }
        d->doc_h = d->L->doc_h;
    }
    if (d->doc_h < 1) d->doc_h = 1;
    if (d->kind == DG_HTML) d->page_bg = page_bg_of(d); else d->page_bg = 0xFFFFFF;
    paginate(d);
    if (!d->title[0]) dcpy(d->title, "Documento", sizeof d->title);
    return d;
}

void dg_close(struct dg_doc *d)
{
    if (!d) return;
    for (int i = 0; i < d->nimg; i++) { if (d->img[i].key) kfree(d->img[i].key); if (d->img[i].px) kfree(d->img[i].px); }
    for (int i = 0; i < d->nsvg; i++) if (d->svg[i]) kfree(d->svg[i]);
    if (d->L) wl_free(d->L);
    if (d->css) wc_free(d->css);
    if (d->wd) wd_free(d->wd);
    if (d->src) kfree(d->src);
    if (d->ops) kfree(d->ops);
    if (d->pool) kfree(d->pool);
    kfree(d);
}
int dg_npages(const struct dg_doc *d) { return d ? d->npages : 0; }
void dg_page_px(const struct dg_doc *d, int *w, int *h) { *w = d->pw; *h = d->ph; }
void dg_page_pt(const struct dg_doc *d, int *w, int *h) { *w = d->pw_pt; *h = d->ph_pt > 0 ? d->ph_pt : d->ph * 72 * 100 / (96 * d->zoom); }
int dg_kind(const struct dg_doc *d) { return d->kind; }
const char *dg_title(const struct dg_doc *d) { return d->title; }
const char *dg_format_name(const struct dg_doc *d) { return d->fmt; }

/* ================================================================ dibujar una pagina */
static void put_num(char *o, int *p, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); while (n) o[(*p)++] = t[--n]; }
static void expand(const char *tpl, int page, int total, const char *title, char *o, int max)
{
    int p = 0;
    for (int i = 0; tpl[i] && p < max - 12; i++) {
        if (tpl[i] == '{' && tpl[i + 1] == 'n' && tpl[i + 2] == '}') { put_num(o, &p, page); i += 2; }
        else if (tpl[i] == '{' && tpl[i + 1] == 'N' && tpl[i + 2] == '}') { put_num(o, &p, total); i += 2; }
        else if (tpl[i] == '{' && dstarts_ci(tpl + i, "{titulo}")) { for (int k = 0; title[k] && p < max - 12; k++) o[p++] = title[k]; i += 7; }
        else o[p++] = tpl[i];
    }
    o[p] = 0;
}
int dg_render_page(struct dg_doc *d, int page, uint32_t *argb)
{
    if (!d || page < 0 || page >= d->npages || !argb) return -1;
    int w = d->pw, h = d->ph;
    uint32_t bg = d->page_bg;
    for (int i = 0; i < w * h; i++) argb[i] = 0xFF000000u | bg;
    int y0 = d->cut[page], y1 = d->cut[page + 1], sh = y1 - y0;
    if (sh > d->ch && !d->single) sh = d->ch;
    SCR_BEGIN();
    fb_target_begin(argb, (uint32_t)w, (uint32_t)h);
    int bleed = d->mr > Z(d, 6) ? Z(d, 6) : d->mr;                         /* las letras pueden pasar 1-3 px del borde de su caja */
    fb_set_clip(d->ml, d->mt, d->cw + bleed, sh);
    if (d->kind == DG_HTML) wl_paint(d->L, d->ml, d->mt, d->cw + bleed, sh, 0, y0);
    else tex_paint(d, d->ml, d->mt - y0, d->mt, d->mt + sh);
    fb_set_clip(0, 0, w, h);
    /* cabecera y pie */
    uint32_t ink = ((bg >> 16 & 255) + (bg >> 8 & 255) + (bg & 255)) < 384 ? 0x94A3B8 : 0x64748B;
    int fpx = Z(d, 12);
    char t[160];
    if (d->numbers || d->footer[0]) {
        if (d->footer[0]) expand(d->footer, page + 1, d->npages, d->title, t, sizeof t); else expand("{n} / {N}", page + 1, d->npages, d->title, t, sizeof t);
        int tw = wf_width(t, (uint32_t)dl(t), fpx, WF_RAW | WF_WEB);
        int by = h - d->mb / 2 + fpx / 3;
        if (d->footer[0] && d->numbers) {
            wf_draw(d->ml, by, t, (uint32_t)dl(t), fpx, ink, WF_RAW | WF_WEB, 255);
            char n[24]; int p = 0; put_num(n, &p, page + 1); n[p] = 0;
            int nw = wf_width(n, (uint32_t)p, fpx, WF_RAW | WF_WEB);
            wf_draw(w - d->mr - nw, by, n, (uint32_t)p, fpx, ink, WF_RAW | WF_WEB, 255);
        } else wf_draw((w - tw) / 2, by, t, (uint32_t)dl(t), fpx, ink, WF_RAW | WF_WEB, 255);
    }
    if (d->slides && page > 0 && d->npages > 2) {                         /* numero de diapositiva, abajo a la derecha */
        char n[24]; int q = 0; put_num(n, &q, page + 1); n[q] = 0;
        int sp = Z(d, 13), nw = wf_width(n, (uint32_t)q, sp, WF_RAW | WF_WEB);
        uint32_t bgp = argb[(h - Z(d, 16)) * w + (w - Z(d, 20))];          /* contraste con el fondo real de la diapositiva */
        int lum = (int)(((bgp >> 16) & 255) * 3 + ((bgp >> 8) & 255) * 6 + (bgp & 255)) / 10;
        wf_draw(w - Z(d, 28) - nw, h - Z(d, 22), n, (uint32_t)q, sp, lum < 128 ? 0xCBD5E1u : 0x64748Bu, WF_RAW | WF_WEB, 255);
    }
    if (d->header[0] && page > 0) {
        expand(d->header, page + 1, d->npages, d->title, t, sizeof t);
        int by = d->mt / 2 + fpx / 3;
        wf_draw(d->ml, by, t, (uint32_t)dl(t), fpx, ink, WF_RAW | WF_WEB, 255);
        fb_fill_rect((uint32_t)d->ml, (uint32_t)(by + Z(d, 6)), (uint32_t)d->cw, 1, 0xE2E8F0);
    }
    fb_reset_clip();
    fb_target_end();
    SCR_END();
    for (int i = 0; i < w * h; i++) argb[i] |= 0xFF000000u;            /* opaca (el dibujo del nucleo deja alfa 0) */
    return 0;
}

/* ================================================================ exportar HTML autonomo */
static void xadd(char *o, uint32_t max, uint32_t *p, const char *s, uint32_t n) { for (uint32_t i = 0; i < n && *p + 1 < max; i++) o[(*p)++] = s[i]; }
int dg_export_html(const struct dg_doc *d, const char *src, uint32_t len, char *out, uint32_t max)
{
    if (!d || d->kind != DG_HTML || !src || !out || max < 1024) return -1;
    uint32_t p = 0, at = 0, head_end = 0;
    /* tras <head ...> (o al principio si no hay) */
    for (uint32_t i = 0; i + 5 < len; i++)
        if (src[i] == '<' && dstarts_ci(src + i + 1, "head") && (src[i + 5] == '>' || src[i + 5] == ' ')) { uint32_t j = i; while (j < len && src[j] != '>') j++; head_end = j < len ? j + 1 : 0; break; }
    at = head_end;
    if (!at) for (uint32_t i = 0; i + 5 < len; i++) if (src[i] == '<' && dstarts_ci(src + i + 1, "html")) { uint32_t j = i; while (j < len && src[j] != '>') j++; at = j < len ? j + 1 : 0; break; }
    xadd(out, max, &p, src, at);
    int has_charset = 0; for (uint32_t i = 0; i + 8 < len && i < 2048; i++) if (dstarts_ci(src + i, "charset")) { has_charset = 1; break; }
    const char *a = head_end || !at ? "" : "<head>";
    xadd(out, max, &p, a, (uint32_t)dl(a));
    if (!has_charset) { const char *m = "<meta charset=\"utf-8\">"; xadd(out, max, &p, m, (uint32_t)dl(m)); }
    const char *s1 = "\n<style>/* Hoja de estilo de SOMA (incrustada para que se vea igual en cualquier navegador) */\n";
    xadd(out, max, &p, s1, (uint32_t)dl(s1));
    xadd(out, max, &p, dg_house_css, (uint32_t)(sizeof dg_house_css - 1));
    char pg[200]; uint32_t q = 0;
    int mm = d->ml * 100 / d->zoom * 100 / 378;
#define PS(s) xadd(pg, sizeof pg, &q, (s), (uint32_t)dl(s))
#define PN(v) do { char t_[12]; int k_ = 0; put_num(t_, &k_, (v)); t_[k_] = 0; PS(t_); } while (0)
    PS("\n@page{size:");
    if (d->slides) PS("254mm 142.875mm"); else if (d->single) PS("auto"); else if (wu_strieq(d->fmt, "A4 apaisado")) PS("A4 landscape");
    else if (wu_strieq(d->fmt, "carta")) PS("letter"); else PS(d->fmt);
    PS(";margin:"); PN(d->slides || d->single ? 0 : mm); PS("mm}\n");
    if (!d->slides && !d->single) { PS("@media screen{body{max-width:"); PN(d->cw * 100 / d->zoom); PS("px;margin:40px auto;padding:0 24px}}\n"); }
#undef PS
#undef PN
    xadd(out, max, &p, pg, q);
    const char *s2 = "</style>\n";
    xadd(out, max, &p, s2, (uint32_t)dl(s2));
    if (!head_end && at) { const char *h = "</head>"; xadd(out, max, &p, h, (uint32_t)dl(h)); }
    xadd(out, max, &p, src + at, len - at);
    if (p + 1 >= max) return -1;
    out[p] = 0;
    return (int)p;
}

/* ================================================================ revision automatica */
static void add(char *out, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) out[(*p)++] = *s++; if (max) out[*p < max ? *p : max - 1] = 0; }
static void addn(char *out, uint32_t max, uint32_t *p, int v) { char t[16]; int n = 0; if (v < 0) { add(out, max, p, "-"); v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15); t[n] = 0; for (int i = 0; i < n / 2; i++) { char c = t[i]; t[i] = t[n - 1 - i]; t[n - 1 - i] = c; } add(out, max, p, t); }
static void describe(struct wnode *n, char *o, int max)
{
    int p = 0; const char *id = wd_attr(n, "id"), *cl = wd_attr(n, "class");
    for (int i = 0; n->name && n->name[i] && p < max - 1; i++) o[p++] = n->name[i];
    if (id && p < max - 2) { o[p++] = '#'; for (int i = 0; id[i] && p < max - 1; i++) o[p++] = id[i]; }
    if (cl && p < max - 2) { o[p++] = '.'; for (int i = 0; cl[i] && cl[i] != ' ' && p < max - 1; i++) o[p++] = cl[i]; }
    o[p] = 0;
}
static int g_nchk;
static void check_nodes(struct dg_doc *d, struct wnode *n, char *out, uint32_t max, uint32_t *p, int *count)
{
    for (; n && *count < 24; n = n->next) {
        if (n->type == WN_ELEM && n->tag != WT_HTML && n->tag != WT_BODY && n->tag != WT_HEAD) {
            int x, y, w, h;
            if (wl_node_rect(d->L, n, &x, &y, &w, &h) && w > 0 && h > 0) {
                int tol = Z(d, 3);
                if (x + w > d->cw + tol || x < -tol) {
                    char ds[64]; describe(n, ds, sizeof ds);
                    add(out, max, p, "- El elemento <"); add(out, max, p, ds); add(out, max, p, "> se sale del ancho de la pagina (mide "); addn(out, max, p, w * 100 / d->zoom);
                    add(out, max, p, " px y caben "); addn(out, max, p, d->cw * 100 / d->zoom); add(out, max, p, " px): reduce su ancho, usa max-width:100% o parte el contenido.\n");
                    (*count)++; continue;                                    /* sus hijos tambien se saldrian: no repetir */
                }
                if (d->slides && wd_has_class(n, "diapositiva")) {
                    /* contenido de la diapositiva mas alto que ella */
                    int maxb = y;
                    for (struct wnode *c = n->first; c; c = c->next) { int cx, cy, cw, chh; if (c->type == WN_ELEM && wl_node_rect(d->L, c, &cx, &cy, &cw, &chh) && cy + chh > maxb) maxb = cy + chh; }
                    if (maxb > y + h + tol) {
                        add(out, max, p, "- La diapositiva "); addn(out, max, p, ++g_nchk); add(out, max, p, " tiene mas contenido del que cabe: resume, quita puntos o reparte en dos diapositivas.\n"); (*count)++;
                    }
                }
                if (n->tag == WT_IMG) {
                    const char *src = wd_attr(n, "src");
                    for (int i = 0; src && i < d->nimg; i++) if (wu_streq(d->img[i].key, src) && d->img[i].state == 3) { add(out, max, p, "- No se encuentra la imagen "); add(out, max, p, src); add(out, max, p, ": usa una ruta del almacen que exista o quitala.\n"); (*count)++; break; }
                }
            }
        }
        if (n->first) check_nodes(d, n->first, out, max, p, count);
    }
}
int dg_check(struct dg_doc *d, char *out, uint32_t max)
{
    uint32_t p = 0; int count = 0; if (max) out[0] = 0;
    if (!d) return 0;
    g_nchk = 0;
    if (d->kind == DG_HTML) check_nodes(d, d->wd->root, out, max, &p, &count);
    if (d->kind == DG_TEX && d->to.errors) { add(out, max, &p, "- Hay "); addn(out, max, &p, d->to.errors); add(out, max, &p, " ordenes de LaTeX que SOMA no entiende (salen en rojo): usa las basicas (section, itemize, tabular, equation...).\n"); count++; }
    if (!d->single && d->npages > 1) {
        int last = d->cut[d->npages] - d->cut[d->npages - 1];
        if (last < d->ch / 8 && !d->slides) { add(out, max, &p, "- La ultima pagina tiene solo unas lineas: acorta un poco el texto o reduce espacios para que quepa en "); addn(out, max, &p, d->npages - 1); add(out, max, &p, " paginas.\n"); count++; }
    }
    if (d->slides) {
        int expect = 0; for (struct wnode *n = d->wd->root; n; n = wd_walk_next(n, 0)) if (n->type == WN_ELEM && wd_has_class(n, "diapositiva")) expect++;
        if (expect == 0) { add(out, max, &p, "- Es una presentacion pero no hay ninguna <section class=\"diapositiva\">: cada diapositiva debe ir en su propia section.\n"); count++; }
    }
    if (d->doc_h < Z(d, 40)) { add(out, max, &p, "- El documento esta practicamente vacio.\n"); count++; }
    return count;
}
#ifdef WEB_HOST_TEST
static void dbg(struct dg_doc *d, struct wnode *n, int dep) { for (; n; n = n->next) { if (n->type == WN_ELEM) { int x, y, w, h; if (wl_node_rect(d->L, n, &x, &y, &w, &h)) printf("%*s%s %d %d %d %d\n", dep * 2, "", n->name, x, y, w, h); } if (n->first) dbg(d, n->first, dep + 1); } }
void dg_debug_dump(struct dg_doc *d) { printf("cw=%d ch=%d ml=%d mt=%d pw=%d doc_h=%d\n", d->cw, d->ch, d->ml, d->mt, d->pw, d->doc_h); if (d->kind == DG_HTML) dbg(d, d->wd->root, 0); }
#endif
