/* SOMA_248_DOC: el Maquetador ve documentos de verdad: HTML (con la hoja de estilo de la casa) y LaTeX se paginan como en papel
 * (A4, A5, carta, 16:9, email) con el motor docgen, se exportan a PDF/HTML y se pueden revisar con IA (con vision si el modelo la tiene).
 * "Generar" manda la peticion al agente de documentos (docagent) en el hilo del agente: el orbe muestra el progreso y la GUI sigue viva.
 *
 * PARCHE 084: Maquetador renovado: entiende y dibuja LaTeX (motor propio en tex.c) con la estetica del explorador de archivos.
 *
 *  - Vista "Documento": hoja blanca con sombra sobre el fondo oscuro, texto suavizado, formulas, tablas, listas, notas, imagenes.
 *  - Vista "Fuente": el .tex con colores (comandos, formulas, comentarios, llaves) y numeros de linea.
 *  - Barra: abrir y recargar el .tex, editarlo en el Bloc de notas, tamano de letra, ruta; rueda y barra de desplazamiento.
 *  - Abajo: el subagente "maquetador" escribe documentos LaTeX a partir de una peticion (y se guardan en /workspace/documento.tex).
 *  - Compatibilidad: los documentos #SOMA-LAYOUT v1 de antes se convierten a LaTeX al cargarlos. */
#include "layout.h"
#include "fb.h"
#include "gui_font.h"
#include "console.h"
#include "mem.h"
#include "agent.h"
#include "fs.h"
#include "gui.h"
#include "tex.h"
#include "web_font.h"
#include "web_img.h"
#include "docgen.h"
#include "docagent.h"
#include "thread.h"

uint64_t timer_get_uptime_ms(void);

/* ---------------------------------------------------------------- tema */
#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_LINE    0x00212B3B
#define KC_FIELD   0x00090D15
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB
#define KC_AREA    0x00080B12
#define KC_OK      0x0034D399

#define LY_SRC_MAX    98304        /* SOMA_248_DOC: documentos HTML de hasta 96 KiB */
#define LY_PROMPT_MAX 256
#define OPS_CAP       14000
#define POOL_CAP      (192 * 1024)

static char ly_src[LY_SRC_MAX]; static uint32_t ly_src_len;
static char ly_path[128] = "/workspace/documento.tex";
static char ly_prompt[LY_PROMPT_MAX]; static uint32_t ly_prompt_len; static int ly_prompt_focus;
static char ly_status[112]; static int ly_status_ok = 1;
static int ly_scroll, ly_view, ly_px = 15, ly_drag_sb, ly_drag_y0, ly_drag_s0;
static struct tex_op ly_ops[OPS_CAP]; static char ly_pool[POOL_CAP];
static struct tex_out ly_out;
static int ly_dirty = 1, ly_w_cached, ly_px_cached;
static int ly_doc_h = 100;
static uint32_t ly_font_gen;                    /* PARCHE 085: se vuelve a maquetar si cambia la fuente de la interfaz */
/* SOMA_248_DOC: vista de paginas */
#define PG_GAP 22
static struct dg_doc *pg_doc; static int pg_zoom, pg_w, pg_h, pg_n, pg_avail, pg_scale = 100, pg_dirty = 1, pg_failed;
static uint32_t *pg_px[DG_MAX_PAGES]; static uint32_t pg_font_gen;
static volatile int ly_job;                     /* exportacion en marcha (hilo aparte) */
static char ly_job_path[128], ly_job_fmt[8];

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static void status(const char *m, int ok) { s_cpy(ly_status, m, sizeof ly_status); ly_status_ok = ok; }
static int find(const char *h, const char *n) { if (!h || !n || !*n) return -1; for (int i = 0; h[i]; i++) { int j = 0; while (n[j] && h[i + j] == n[j]) j++; if (!n[j]) return i; } return -1; }

/* ---------------------------------------------------------------- texto de la interfaz */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { wf_draw(x, top + (h + (px * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}

/* ---------------------------------------------------------------- medidas para el motor */
static int m_width(const char *s, int n, int px, int st, void *u)
{
    (void)u;
    if (!has_wf()) return n * 8;
    int fl = (st & TS_BOLD ? WF_BOLD : 0) | (st & TS_ITALIC ? WF_ITALIC : 0) | (st & TS_MONO ? WF_MONO : 0);
    return wf_width(s, (uint32_t)n, px, fl);
}
static int m_asc(int px, void *u) { (void)u; return has_wf() ? wf_ascent(wf_ui_px(px)) : px; }
static int m_desc(int px, void *u) { (void)u; return has_wf() ? wf_descent(wf_ui_px(px)) : px / 4; }
/* imagenes: cache pequena de decodificadas */
struct icache { char path[96]; struct wimg im; int ok; };
static struct icache g_img[4]; static int g_img_next;
static struct icache *img_get(const char *path)
{
    for (int i = 0; i < 4; i++) if (g_img[i].path[0] && find(g_img[i].path, path) == 0 && s_len(g_img[i].path) == s_len(path)) return &g_img[i];
    struct icache *c = &g_img[g_img_next]; g_img_next = (g_img_next + 1) % 4;
    if (c->ok) wi_free(&c->im);
    c->ok = 0; s_cpy(c->path, path, sizeof c->path);
    char full[128] = ""; if (path[0] != '/') s_cpy(full, "/workspace/", sizeof full);
    s_cat(full, path, sizeof full);
    uint32_t un = 0; uint8_t *raw = (uint8_t *)vfs_read_alloc(full, &un);   /* PARCHE 087: imagenes de cualquier tamano */
    if (!raw && path[0] != '/') raw = (uint8_t *)vfs_read_alloc(path, &un);
    if (!raw) return c;
    if (un > 8 && wi_decode(raw, un, &c->im) == 0) c->ok = 1;
    kfree(raw);
    return c;
}
static int m_image(const char *path, int *w, int *h, void *u) { (void)u; struct icache *c = img_get(path); if (!c->ok) return -1; *w = c->im.w; *h = c->im.h; return 0; }
static const struct tex_metrics g_metrics = { m_width, m_asc, m_desc, m_image, 0 };

/* ---------------------------------------------------------------- documento de ejemplo */
static const char SAMPLE_TEX[] =
"\\documentclass{article}\n"
"\\title{El motor \\LaTeX{} de SOMA}\n"
"\\author{SOMA OS \\and Maquetador}\n"
"\\date{Noviembre de 2026}\n"
"\\begin{document}\n"
"\\maketitle\n"
"\\begin{abstract}\n"
"El maquetador entiende \\LaTeX: secciones numeradas, listas, tablas, notas al pie y f\\'ormulas como $E = mc^2$ "
"o $\\sqrt{x^2+y^2}$ dentro del texto. Abre cualquier \\texttt{.tex} desde el explorador o p\\'idele uno a la IA.\n"
"\\end{abstract}\n"
"\\section{C\\'alculo}\\label{sec:calc}\n"
"El \\textbf{teorema fundamental del c\\'alculo}\\footnote{Newton y Leibniz, siglo XVII.} une derivada e integral. "
"Para $f$ continua en $[a,b]$:\n"
"\\begin{equation}\\label{eq:tfc}\n\\int_a^b f(x)\\,dx = F(b) - F(a)\n\\end{equation}\n"
"La ecuaci\\'on~\\eqref{eq:tfc} abre la secci\\'on~\\ref{sec:calc}. Las ra\\'ices de $ax^2+bx+c=0$ son\n"
"\\[ x = \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a} \\]\n"
"\\subsection{Series}\n"
"\\begin{itemize}\n"
"\\item Basilea: $\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}$.\n"
"\\item El n\\'umero $e = \\lim_{n\\to\\infty}\\left(1+\\frac{1}{n}\\right)^n$.\n"
"\\item Texto en \\emph{cursiva}, \\texttt{monoespaciado} y \\textcolor{red}{color}.\n"
"\\end{itemize}\n"
"\\[ \\sum_{k=0}^{n} \\binom{n}{k} x^k = (1+x)^n \\qquad \\oint_C \\vec{F}\\cdot d\\vec{r} = 0 \\]\n"
"\\section{F\\'isica}\n"
"Ecuaciones de Maxwell en forma diferencial:\n"
"\\begin{align}\n\\nabla \\cdot \\mathbf{E} &= \\frac{\\rho}{\\varepsilon_0} \\\\\n"
"\\nabla \\times \\mathbf{B} &= \\mu_0 \\mathbf{J} + \\mu_0\\varepsilon_0 \\frac{\\partial \\mathbf{E}}{\\partial t}\n\\end{align}\n"
"\\section{\\'Algebra}\n"
"\\[ A = \\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}, \\qquad \\det A = ad - bc, \\qquad "
"|x| = \\begin{cases} x & \\text{si } x \\geq 0 \\\\ -x & \\text{si } x < 0 \\end{cases} \\]\n"
"\\begin{table}\n\\centering\n\\begin{tabular}{|l|c|r|}\n\\hline\nS\\'imbolo & Nombre & Valor \\\\\n\\hline\n"
"$\\pi$ & pi & 3,14159 \\\\\n$e$ & Euler & 2,71828 \\\\\n$\\phi$ & raz\\'on \\'aurea & 1,61803 \\\\\n\\hline\n\\end{tabular}\n"
"\\caption{Constantes matem\\'aticas.}\n\\end{table}\n"
"\\begin{enumerate}\n\\item Abre un \\texttt{.tex} con doble clic en el explorador.\n\\item Pulsa \\emph{Editar} para cambiarlo en el Bloc de notas y luego \\emph{Recargar}.\n"
"\\item Escribe abajo un tema y pulsa \\textbf{Generar}: la IA escribe el \\LaTeX.\n\\end{enumerate}\n"
"\\begin{quote}\nLa matem\\'atica es el alfabeto con el cual Dios ha escrito el universo. --- Galileo\n\\end{quote}\n"
"\\end{document}\n";

/* ---------------------------------------------------------------- formato antiguo -> LaTeX */
static void legacy_to_tex(const char *src, uint32_t len)
{
    static char out[LY_SRC_MAX]; uint32_t o = 0;
#define PUT(s) do { const char *_p = (s); while (*_p && o + 1 < sizeof out) out[o++] = *_p++; } while (0)
    PUT("\\documentclass{article}\n");
    int in_list = 0, body = 0; char foot[200] = "";
    uint32_t i = 0;
    while (i < len) {
        uint32_t a = i; while (i < len && src[i] != '\n') i++;
        char line[400]; uint32_t n = i - a; if (n > 399) n = 399; for (uint32_t k = 0; k < n; k++) line[k] = src[a + k]; line[n] = 0; if (n && line[n - 1] == '\r') line[n - 1] = 0;
        if (i < len) i++;
        const char *v = 0;
        if (!body) {
            if (find(line, "TITLE:") == 0) { PUT("\\title{"); PUT(line + 6); PUT("}\n"); }
            else if (find(line, "SUBTITLE:") == 0) { PUT("\\author{"); PUT(line + 9); PUT("}\n"); }
            else if (find(line, "---") == 0) { body = 1; PUT("\\date{}\n\\begin{document}\n\\maketitle\n"); }
            continue;
        }
        int is_list = find(line, "LIST:") == 0;
        if (in_list && !is_list) { PUT("\\end{itemize}\n"); in_list = 0; }
        if (find(line, "H1:") == 0) { PUT("\\section*{"); PUT(line + 3); PUT("}\n"); }
        else if (find(line, "H2:") == 0) { PUT("\\subsection*{"); PUT(line + 3); PUT("}\n"); }
        else if (find(line, "H3:") == 0) { PUT("\\subsubsection*{"); PUT(line + 3); PUT("}\n"); }
        else if (find(line, "P:") == 0) { PUT(line + 2); PUT("\n\n"); }
        else if (find(line, "QUOTE:") == 0) { PUT("\\begin{quote}"); PUT(line + 6); PUT("\\end{quote}\n"); }
        else if (find(line, "HR") == 0) PUT("\\hrule\n");
        else if (is_list) { if (!in_list) { PUT("\\begin{itemize}\n"); in_list = 1; } PUT("\\item "); PUT(line + 5); PUT("\n"); }
        else if (find(line, "FOOTER:") == 0) s_cpy(foot, line + 7, sizeof foot);
        else if (line[0]) { PUT(line); PUT("\n"); }
        (void)v;
    }
    if (in_list) PUT("\\end{itemize}\n");
    if (foot[0]) { PUT("\\hrule\n\\begin{center}\\small "); PUT(foot); PUT("\\end{center}\n"); }
    if (!body) PUT("\\begin{document}\n");
    PUT("\\end{document}\n");
    out[o] = 0;
#undef PUT
    for (uint32_t k = 0; k <= o && k < LY_SRC_MAX; k++) ly_src[k] = out[k];
    ly_src_len = o;
}

void layout_set_source(const char *src, uint32_t len)
{
    if (!src) src = "";
    if (len == 0) while (src[len]) len++;
    if (len >= LY_SRC_MAX) len = LY_SRC_MAX - 1;
    int legacy = 0; for (uint32_t i = 0; i + 12 <= len && i < 200; i++) if (src[i] == '#' && find(src + i, "#SOMA-LAYOUT") == 0) { legacy = 1; break; }
    if (legacy && !tex_is_latex(src, len)) legacy_to_tex(src, len);
    else { for (uint32_t i = 0; i < len; i++) ly_src[i] = src[i]; ly_src[len] = 0; ly_src_len = len; }
    ly_scroll = 0; ly_dirty = 1; pg_dirty = 1;
    status(legacy ? "Documento #SOMA-LAYOUT convertido a LaTeX" : "Documento cargado", 1);
}
static void load_path(void)
{
    static char buf[LY_SRC_MAX];                            /* PARCHE 087: documentos de hasta 32 KiB */
    int n = vfs_read(ly_path, buf, sizeof buf - 1);
    if (n <= 0) { char m[112] = "No se pudo leer "; s_cat(m, ly_path, sizeof m); status(m, 0); return; }
    buf[n] = 0;
    layout_set_source(buf, (uint32_t)n);
    char m[112] = "Abierto: "; s_cat(m, ly_path, sizeof m); status(m, 1);
}
void layout_open_file(const char *path)
{
    if (!path || !*path) return;
    s_cpy(ly_path, path, sizeof ly_path);
    load_path();
    ly_view = 0;
}

void layout_init(void)
{
    ly_prompt[0] = 0; ly_prompt_len = 0; ly_prompt_focus = 0; ly_scroll = 0; ly_view = 0;
    if (!ly_src_len) { layout_set_source(SAMPLE_TEX, 0); status("Documento de ejemplo. Abre un .tex o pide uno a la IA abajo.", 1); }
}
const char *layout_get_prompt(void) { return ly_prompt; }

/* ---------------------------------------------------------------- IA */
static void prog_cb(const char *m) { status(m, 1); }
static void do_generate(void)
{
    if (ly_prompt_len == 0) { status("Escribe primero que documento quieres (tema, tipo, publico, extension...)", 0); return; }
    char mission[LY_PROMPT_MAX + 16] = "maquetador: "; s_cat(mission, ly_prompt, sizeof mission);
    docagent_set_progress(prog_cb);
    int r = gui_agent_submit(mission);
    if (r == -1) { status("SOMA esta ocupado con otra mision: espera a que termine", 0); return; }
    status("El maquetador redacta, maqueta y revisa el documento (progreso en el orbe)...", 1);
}
static void do_review(void)
{
    if (!ly_src_len) return;
    (void)vfs_write(ly_path, ly_src, ly_src_len);
    char mission[200] = "maquetador revisa "; s_cat(mission, ly_path, sizeof mission);
    if (ly_prompt_len) { s_cat(mission, " ", sizeof mission); s_cat(mission, ly_prompt, sizeof mission); }
    docagent_set_progress(prog_cb);
    int r = gui_agent_submit(mission);
    if (r == -1) { status("SOMA esta ocupado con otra mision: espera a que termine", 0); return; }
    status("Revisando el documento con IA (progreso en el orbe)...", 1);
}
/* exportar en un hilo aparte: el motor pide turno de pantalla para cada pagina */
static void export_main(void *a)
{
    (void)a;
    static char out[300];
    int ok = docagent_export(ly_job_path, ly_job_fmt, 0, out, sizeof out);
    status(out, ok);
    ly_job = 0;
}
static void do_export(const char *fmt)
{
    if (ly_job) { status("Ya hay una exportacion en marcha", 0); return; }
    if (!ly_src_len) return;
    if (vfs_write(ly_path, ly_src, ly_src_len) < 0) { status("No se pudo guardar el documento antes de exportarlo", 0); return; }
    s_cpy(ly_job_path, ly_path, sizeof ly_job_path); s_cpy(ly_job_fmt, fmt, sizeof ly_job_fmt);
    docagent_set_progress(prog_cb);
    ly_job = 1;
    status(fmt[0] == 'p' && fmt[1] == 'd' ? "Exportando a PDF..." : "Exportando...", 1);
    if (!thread_create("maq_export", export_main, 0)) { ly_job = 0; status("No se pudo crear el hilo de exportacion", 0); }
}

/* ---------------------------------------------------------------- disposicion */
enum { B_OPEN = 0, B_RELOAD, B_EDIT, B_DOC, B_SRC, B_SMALL, B_BIG, B_PDF, B_HTML, B_IA, B_N };
#define TB_H 48
#define PR_H 52
#define ST_H 26
#define BTN 32
struct ll { int x, y, w, h, bx[B_N], by, path_x, path_w, area_x, area_y, area_w, area_h, pr_y, st_y, paper_x, paper_w, col_x, col_w, sb_x, gen_x; };
static void lay(struct ll *L, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    L->x = (int)wx; L->y = (int)wy; L->w = (int)ww; L->h = (int)wh;
    L->by = L->y + (TB_H - BTN) / 2;
    int x = L->x + 12;
    for (int i = 0; i < B_N; i++) {
        L->bx[i] = x;
        x += (i == B_DOC || i == B_SRC) ? 92 : i == B_PDF ? 58 : i == B_HTML ? 66 : i == B_IA ? 108 : BTN + 4;
        if (i == B_EDIT || i == B_SRC || i == B_BIG) x += 14;
    }
    L->path_x = x + 10; L->path_w = L->x + L->w - 12 - L->path_x; if (L->path_w < 0) L->path_w = 0;
    L->st_y = L->y + L->h - ST_H; L->pr_y = L->st_y - PR_H;
    L->area_x = L->x; L->area_y = L->y + TB_H; L->area_w = L->w - 12; L->area_h = L->pr_y - L->area_y;
    L->sb_x = L->x + L->w - 12;
    L->paper_w = L->area_w - 40; if (L->paper_w > 860) L->paper_w = 860;
    if (L->paper_w < 200) L->paper_w = L->area_w - 8;
    L->paper_x = L->area_x + (L->area_w - L->paper_w) / 2;
    int pad = L->paper_w > 600 ? 56 : L->paper_w > 400 ? 32 : 14;
    L->col_x = L->paper_x + pad; L->col_w = L->paper_w - 2 * pad;
    L->gen_x = L->x + L->w - 16 - 110;
}
static void typeset_if_needed(const struct ll *L)
{
    if (ly_font_gen != wf_ui_gen()) { ly_font_gen = wf_ui_gen(); ly_dirty = 1; }
    if (!ly_dirty && ly_w_cached == L->col_w && ly_px_cached == ly_px) return;
    ly_out.ops = ly_ops; ly_out.cap = OPS_CAP; ly_out.pool = ly_pool; ly_out.poolcap = POOL_CAP;
    int r = tex_typeset(ly_src, ly_src_len, L->col_w, ly_px, &g_metrics, &ly_out);
    ly_dirty = 0; ly_w_cached = L->col_w; ly_px_cached = ly_px;
    ly_doc_h = ly_out.height + 112;
    if (r < 0) status("Documento demasiado largo: se muestra lo que cabe", 0);
}
/* ---- SOMA_248_DOC: paginas con docgen */
static void pg_free(void)
{
    for (int i = 0; i < DG_MAX_PAGES; i++) if (pg_px[i]) { kfree(pg_px[i]); pg_px[i] = 0; }
    if (pg_doc) { dg_close(pg_doc); pg_doc = 0; }
    pg_n = 0;
}
static int pg_loader(const char *url, uint8_t **data, uint32_t *len)
{
    char full[160] = "";
    if (url[0] != '/') {                                   /* relativa a la carpeta del documento */
        s_cpy(full, ly_path, sizeof full);
        int k = s_len(full); while (k > 0 && full[k - 1] != '/') k--;
        full[k] = 0;
    }
    s_cat(full, url, sizeof full);
    char *b = vfs_read_alloc(full, len);
    if (!b && url[0] != '/') { char t[160] = "/workspace/"; s_cat(t, url, sizeof t); b = vfs_read_alloc(t, len); }
    if (!b) return -1;
    *data = (uint8_t *)b; return 0;
}
static void pg_ensure(const struct ll *L)
{
    if (ly_font_gen != wf_ui_gen()) { ly_font_gen = wf_ui_gen(); ly_dirty = 1; }
    if (pg_font_gen != wf_ui_gen()) { pg_font_gen = wf_ui_gen(); pg_dirty = 1; }
    int avail = L->area_w - 2 * PG_GAP;
    if (!pg_dirty && avail == pg_avail && (pg_doc || pg_failed)) return;
    pg_free(); pg_failed = 0; pg_dirty = 0; pg_avail = avail;
    if (!ly_src_len) { pg_failed = 1; return; }
    dg_set_loader(pg_loader);
    char err[120];
    struct dg_doc *d = dg_open(ly_src, ly_src_len, 100, err, sizeof err);
    if (!d) { pg_failed = 1; char m[112] = "No se pudo paginar: "; s_cat(m, err, sizeof m); status(m, 0); return; }
    int w100, h100; dg_page_px(d, &w100, &h100);
    int z = w100 > 0 ? avail * 100 / w100 : 100;
    if (z > 100) z = 100;                                  /* a tamano real como mucho (96 ppp) salvo que se amplie */
    z = z * pg_scale / 100;
    if (z < 25) z = 25;
    if (z > 300) z = 300;
    if (z != 100) { dg_close(d); d = dg_open(ly_src, ly_src_len, z, err, sizeof err); if (!d) { pg_failed = 1; status(err, 0); return; } }
    pg_doc = d; pg_zoom = z; pg_n = dg_npages(d); dg_page_px(d, &pg_w, &pg_h);
}
static int pg_content_h(void) { return pg_doc ? pg_n * (pg_h + PG_GAP) + PG_GAP : 0; }
static void pg_draw(const struct ll *L, int top)
{
    int x = L->area_x + (L->area_w - pg_w) / 2; if (x < L->area_x + 8) x = L->area_x + 8;
    for (int i = 0; i < pg_n; i++) {
        int y = L->area_y + PG_GAP + i * (pg_h + PG_GAP) - top;
        int vis = y + pg_h > L->area_y && y < L->area_y + L->area_h;
        int near = y + pg_h > L->area_y - 2 * pg_h && y < L->area_y + L->area_h + 2 * pg_h;
        if (!near && pg_px[i]) { kfree(pg_px[i]); pg_px[i] = 0; }   /* solo se guardan las paginas cercanas */
        if (!vis) continue;
        if (!pg_px[i]) {
            pg_px[i] = (uint32_t *)kmalloc((size_t)pg_w * (size_t)pg_h * 4u);
            if (!pg_px[i] || dg_render_page(pg_doc, i, pg_px[i]) != 0) { if (pg_px[i]) kfree(pg_px[i]); pg_px[i] = 0; continue; }
            fb_set_clip(L->area_x, L->area_y, L->area_w, L->area_h);   /* dg_render_page deja el recorte como estaba; por si acaso */
        }
        fb_draw_shadow(x, y, pg_w, pg_h, 2, 18, 6, 0x90000000u);
        fb_blit(x, y, pg_px[i], pg_w, pg_h, pg_w);
    }
}
static int max_scroll(const struct ll *L, int content_h) { int m = content_h - L->area_h + 24; return m > 0 ? m : 0; }
static int src_lines(void) { int n = 1; for (uint32_t i = 0; i < ly_src_len; i++) if (ly_src[i] == '\n') n++; return n; }
static int content_height(const struct ll *L) { (void)L; return ly_view ? src_lines() * 18 + 24 : pg_doc ? pg_content_h() : ly_doc_h + 24; }

/* ---------------------------------------------------------------- iconos */
#define LN(a, b, c, d, w, col) fb_line_aa((a) * 16, (b) * 16, (c) * 16, (d) * 16, (w), (col))
static void icon(int b, int cx, int cy, uint32_t col)
{
    switch (b) {
    case B_OPEN: fb_stroke_rrect_aa(cx - 9, cy - 6, 18, 13, 2, col); LN(cx - 9, cy - 3, cx - 2, cy - 3, 20, col); LN(cx - 9, cy - 6, cx - 4, cy - 6, 20, col); break;
    case B_RELOAD: fb_stroke_rrect_aa(cx - 8, cy - 8, 16, 16, 8, col); fb_fill_rect((uint32_t)(cx + 2), (uint32_t)(cy - 10), 8, 7, KC_BAR); LN(cx + 3, cy - 8, cx + 8, cy - 8, 22, col); LN(cx + 8, cy - 8, cx + 8, cy - 3, 22, col); break;
    case B_EDIT: LN(cx - 8, cy + 8, cx + 6, cy - 6, 36, col); LN(cx - 8, cy + 8, cx - 9, cy + 9, 24, col); LN(cx + 4, cy - 8, cx + 8, cy - 4, 24, col); break;
    case B_SMALL: case B_BIG: {
        int big = b == B_BIG; const char *t = "A";
        if (has_wf()) wf_draw(cx - (big ? 8 : 6), cy + (big ? 6 : 5), t, 1, big ? 17 : 12, col & 0x00FFFFFFu, WF_BOLD, 255);
        LN(cx + 4, cy - 6, cx + 10, cy - 6, 20, col); if (big) LN(cx + 7, cy - 9, cx + 7, cy - 3, 20, col); break; }
    }
}

/* ---------------------------------------------------------------- dibujo */
static void draw_ops(int ox, int oy, int y_min, int y_max)
{
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ly_out.nops; i++) {
            const struct tex_op *p = &ly_ops[i];
            int bg = p->type == TOP_RECT && p->style == 1;
            if ((pass == 0) != bg) continue;
            if (p->type == TOP_TEXT) {
                int y = oy + p->y;
                if (y + p->px < y_min || y - p->px > y_max) continue;
                int fl = (p->style & TS_BOLD ? WF_BOLD : 0) | (p->style & TS_ITALIC ? WF_ITALIC : 0) | (p->style & TS_MONO ? WF_MONO : 0);
                if (has_wf()) wf_draw(ox + p->x, y, ly_pool + p->s, p->n, p->px, p->rgb, fl, 255);
                else font_draw_string((uint32_t)(ox + p->x), (uint32_t)(y - 10), ly_pool + p->s, p->rgb, 1);
            } else if (p->type == TOP_RECT) {
                int y = oy + p->y;
                if (y + p->h < y_min || y > y_max) continue;
                fb_fill_rect((uint32_t)(ox + p->x), (uint32_t)y, (uint32_t)(p->w > 0 ? p->w : 1), (uint32_t)(p->h > 0 ? p->h : 1), p->rgb);
            } else if (p->type == TOP_LINE) {
                int y = oy + p->y / 16;
                if (y < y_min - 200 || y > y_max + 200) continue;
                fb_line_aa(ox * 16 + p->x, oy * 16 + p->y, ox * 16 + p->x + p->w, oy * 16 + p->y + p->h, p->px, 0xFF000000u | p->rgb);
            } else if (p->type == TOP_IMAGE) {
                int y = oy + p->y;
                if (y + p->h < y_min || y > y_max) continue;
                struct icache *c = img_get(ly_pool + p->s);
                if (c->ok) fb_blit_scale(ox + p->x, y, p->w, p->h, c->im.px, c->im.w, c->im.h, c->im.w);
            }
        }
}
/* fuente con colores: comandos, formulas, comentarios, llaves */
static void draw_source(const struct ll *L, int top)
{
    int px = 13, lh = 18, gut = 44, x0 = L->paper_x, y = L->area_y + 12 - top;
    fb_fill_rect((uint32_t)x0, (uint32_t)L->area_y, (uint32_t)L->paper_w, (uint32_t)L->area_h, 0x000E1420);
    fb_fill_rect((uint32_t)x0, (uint32_t)L->area_y, (uint32_t)gut, (uint32_t)L->area_h, 0x000B1019);
    uint32_t i = 0; int line = 1, in_math = 0;
    while (i <= ly_src_len) {
        uint32_t a = i; while (i < ly_src_len && ly_src[i] != '\n') i++;
        if (y + lh > L->area_y && y < L->area_y + L->area_h) {
            char nb[8] = ""; s_num(nb, sizeof nb, line);
            if (has_wf()) wf_draw(x0 + gut - 8 - wf_width(nb, (uint32_t)s_len(nb), 11, WF_MONO), y + 13, nb, (uint32_t)s_len(nb), 11, KC_FAINT, WF_MONO, 255);
            int x = x0 + gut + 10;
            uint32_t k = a;
            while (k < i) {
                uint32_t s = k; uint32_t col = KC_TXT;
                char c = ly_src[k];
                if (c == '%') { k = i; col = 0x006B7280; }
                else if (c == '\\') { k++; if (k < i && ((ly_src[k] >= 'a' && ly_src[k] <= 'z') || (ly_src[k] >= 'A' && ly_src[k] <= 'Z'))) { while (k < i && ((ly_src[k] >= 'a' && ly_src[k] <= 'z') || (ly_src[k] >= 'A' && ly_src[k] <= 'Z'))) k++; } else if (k < i) k++; col = 0x0060A5FA; }
                else if (c == '$') { k++; if (k < i && ly_src[k] == '$') k++; in_math = !in_math; col = 0x0034D399; }
                else if (c == '{' || c == '}' || c == '[' || c == ']') { k++; col = 0x00FBBF24; }
                else { while (k < i && ly_src[k] != '\\' && ly_src[k] != '$' && ly_src[k] != '%' && ly_src[k] != '{' && ly_src[k] != '}' && ly_src[k] != '[' && ly_src[k] != ']') k++; col = in_math ? 0x0086EFAC : KC_TXT; }
                if (k > s && has_wf()) { wf_draw(x, y + 13, ly_src + s, k - s, px, col, WF_MONO, 255); x += wf_width(ly_src + s, k - s, px, WF_MONO); }
                if (x > x0 + L->paper_w) break;
            }
        }
        y += lh; line++;
        if (i >= ly_src_len) break;
        i++;
    }
}
void layout_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;
    if (win_w < 240 || win_h < 180) return;
    if (!ly_src_len) layout_init();
    struct ll L; lay(&L, win_x, win_y, win_w, win_h);
    if (!ly_view) { pg_ensure(&L); if (!pg_doc) typeset_if_needed(&L); }
    int ms = max_scroll(&L, content_height(&L));
    if (ly_scroll > ms) ly_scroll = ms;
    if (ly_scroll < 0) ly_scroll = 0;
    char b[128];

    /* barra superior */
    fb_fill_rect(win_x, win_y, win_w, TB_H, KC_BAR);
    fb_fill_rect(win_x, win_y + TB_H - 1, win_w, 1, KC_LINE);
    for (int i = 0; i < B_N; i++) {
        if (i == B_DOC || i == B_SRC) continue;
        icon(i, L.bx[i] + BTN / 2, L.by + BTN / 2, 0xFFC4CDDAu);
        if (i == B_EDIT) fb_fill_rect((uint32_t)(L.bx[i] + BTN + 10), (uint32_t)(L.by + 6), 1, BTN - 12, KC_LINE);
    }
    /* selector Documento / Fuente */
    fb_fill_rounded_rect(L.bx[B_DOC] - 2, L.by - 2, 184 + 4, BTN + 4, 10, 0x000C1119);
    fb_fill_rounded_rect(L.bx[ly_view ? B_SRC : B_DOC], L.by, 92 - 2, BTN, 8, 0x00283449);
    tx(L.bx[B_DOC] + (90 - tw("Documento", 12, 1)) / 2, L.by, BTN, "Documento", 12, ly_view ? KC_DIM : 0x00FFFFFF, 1);
    tx(L.bx[B_SRC] + (90 - tw("Fuente", 12, 1)) / 2, L.by, BTN, "Fuente", 12, ly_view ? 0x00FFFFFF : KC_DIM, 1);
    fb_fill_rect((uint32_t)(L.bx[B_SRC] + 92 + 8), (uint32_t)(L.by + 6), 1, BTN - 12, KC_LINE);
    fb_fill_rect((uint32_t)(L.bx[B_BIG] + BTN + 10), (uint32_t)(L.by + 6), 1, BTN - 12, KC_LINE);
    {   /* SOMA_248_DOC: exportar y revisar */
        static const char *const lab[3] = { "PDF", "HTML", "Revisar IA" };
        static const int bw[3] = { 52, 60, 102 };
        for (int k = 0; k < 3; k++) {
            int bx = L.bx[B_PDF + k], busy = ly_job && k < 2;
            fb_fill_rounded_rect(bx, L.by, bw[k], BTN, BTN / 2, k == 2 ? 0x002A2350 : 0x001C2636);
            fb_stroke_rrect_aa(bx, L.by, bw[k], BTN, BTN / 2, k == 2 ? 0xFF6D5BD0u : 0xFF2C3A50u);
            tx(bx + (bw[k] - tw(lab[k], 12, 1)) / 2, L.by, BTN, lab[k], 12, busy ? KC_FAINT : (k == 2 ? 0x00C4B5FD : 0x00E5EAF2), 1);
        }
    }
    if (L.path_w > 80) {
        fb_fill_rounded_rect(L.path_x, L.by, L.path_w, BTN, BTN / 2, KC_FIELD);
        fb_stroke_rrect_aa(L.path_x, L.by, L.path_w, BTN, BTN / 2, 0xFF263142u);
        fb_fill_rrect_aa(L.path_x + 12, L.by + 9, 12, 15, 2, 0xFF38BDF8u);
        if (has_wf()) wf_draw(L.path_x + 14, L.by + 21, "T", 1, 9, 0x000C111B, WF_BOLD, 255);
        fb_set_clip(L.path_x + 30, L.by, L.path_w - 40, BTN);
        tx(L.path_x + 32, L.by, BTN, ly_path, 13, KC_TXT, 0);
        fb_reset_clip();
    }

    /* area */
    fb_fill_rect((uint32_t)L.area_x, (uint32_t)L.area_y, (uint32_t)L.w, (uint32_t)L.area_h, KC_AREA);
    fb_set_clip(L.area_x, L.area_y, L.area_w, L.area_h);
    if (ly_view) draw_source(&L, ly_scroll);
    else if (pg_doc) pg_draw(&L, ly_scroll);
    else {
        int py = L.area_y + 20 - ly_scroll, ph = ly_doc_h;
        fb_draw_shadow(L.paper_x, py, L.paper_w, ph, 4, 26, 10, 0xA0000000u);
        fb_fill_rect((uint32_t)L.paper_x, (uint32_t)(py > L.area_y ? py : L.area_y), (uint32_t)L.paper_w,
                     (uint32_t)((py + ph) - (py > L.area_y ? py : L.area_y) > 0 ? (py + ph) - (py > L.area_y ? py : L.area_y) : 0), 0x00FFFFFF);
        draw_ops(L.col_x, py + 56, L.area_y, L.area_y + L.area_h);
    }
    fb_reset_clip();
    /* barra de desplazamiento */
    {
        int ch = content_height(&L), track = L.area_h - 8;
        fb_fill_rect((uint32_t)L.sb_x, (uint32_t)L.area_y, 12, (uint32_t)L.area_h, KC_AREA);
        if (ch > L.area_h) {
            int th = track * L.area_h / ch; if (th < 30) th = 30;
            int ty = L.area_y + 4 + (track - th) * ly_scroll / (ms ? ms : 1);
            fb_fill_rounded_rect(L.sb_x + 3, ty, 6, th, 3, ly_drag_sb ? 0x005B6B85 : 0x00334155);
        }
    }

    /* peticion a la IA */
    fb_fill_rect(win_x, (uint32_t)L.pr_y, win_w, PR_H, KC_BAR);
    fb_fill_rect(win_x, (uint32_t)L.pr_y, win_w, 1, KC_LINE);
    {
        int fx = L.x + 16, fw = L.gen_x - 10 - fx, fy = L.pr_y + 10, fh = 32;
        fb_fill_rounded_rect(fx, fy, fw, fh, fh / 2, KC_FIELD);
        fb_stroke_rrect_aa(fx, fy, fw, fh, fh / 2, ly_prompt_focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
        /* chispa */
        int sx = fx + 18, sy = fy + fh / 2;
        LN(sx, sy - 7, sx, sy + 7, 22, 0xFFA5B4FCu); LN(sx - 7, sy, sx + 7, sy, 22, 0xFFA5B4FCu); LN(sx - 4, sy - 4, sx + 4, sy + 4, 14, 0xFFA5B4FCu); LN(sx - 4, sy + 4, sx + 4, sy - 4, 14, 0xFFA5B4FCu);
        fb_set_clip(fx + 34, fy, fw - 46, fh);
        if (ly_prompt_len || ly_prompt_focus) {
            tx(fx + 36, fy, fh, ly_prompt, 13, 0x00FFFFFF, 0);
            if (ly_prompt_focus && ((timer_get_uptime_ms() / 530) & 1) == 0) fb_fill_rect((uint32_t)(fx + 37 + tw(ly_prompt, 13, 0)), (uint32_t)(fy + 8), 2, 16, KC_ACC);
        } else tx(fx + 36, fy, fh, "Pide un documento: informe, paper, revista, presentacion, carta, CV, email... (tema, publico, extension)", 13, KC_FAINT, 0);
        fb_reset_clip();
        fb_fill_rounded_rect(L.gen_x, fy, 110, fh, fh / 2, KC_PRIMARY);
        tx(L.gen_x + (110 - tw("Generar", 13, 1)) / 2, fy, fh, "Generar", 13, 0x00FFFFFF, 1);
    }

    /* estado */
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, ST_H, KC_BAR);
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, 1, KC_LINE);
    fb_fill_rrect_aa(L.x + 12, L.st_y + 9, 8, 8, 4, ly_status_ok ? 0xFF34D399u : 0xFFF87171u);
    fb_set_clip(L.x + 26, L.st_y, L.w / 2, ST_H);
    tx(L.x + 26, L.st_y, ST_H, ly_status, 12, KC_TXT, 0);
    fb_reset_clip();
    b[0] = 0;
    if (!ly_view && pg_doc) {
        s_num(b, sizeof b, pg_n); s_cat(b, pg_n == 1 ? " pagina  " : " paginas  ", sizeof b);
        s_cat(b, dg_format_name(pg_doc), sizeof b); s_cat(b, dg_kind(pg_doc) == DG_TEX ? "  LaTeX  " : "  HTML  ", sizeof b);
        s_num(b, sizeof b, pg_zoom); s_cat(b, " %", sizeof b);
    } else if (!ly_view) {
        s_num(b, sizeof b, ly_out.sections); s_cat(b, " secciones  ", sizeof b); s_num(b, sizeof b, ly_out.equations); s_cat(b, " ecuaciones  ", sizeof b);
        if (ly_out.errors) { s_num(b, sizeof b, ly_out.errors); s_cat(b, " sin reconocer  ", sizeof b); }
    } else { s_num(b, sizeof b, src_lines()); s_cat(b, " lineas  ", sizeof b); s_num(b, sizeof b, (int)ly_src_len); s_cat(b, " bytes  ", sizeof b); }
    if (ly_view || !pg_doc) { s_num(b, sizeof b, ly_px); s_cat(b, " px", sizeof b); }
    tx(L.x + L.w - 14 - tw(b, 12, 0), L.st_y, ST_H, b, 12, ly_out.errors && !ly_view ? 0x00FBBF24 : KC_DIM, 0);
}

/* ---------------------------------------------------------------- raton y teclado */
static void set_px(int px) { if (px < 11) px = 11; if (px > 24) px = 24; if (px != ly_px) { ly_px = px; ly_dirty = 1; } }
static void set_scale(int d) { int s = pg_scale + d; if (s < 40) s = 40; if (s > 250) s = 250; if (s != pg_scale) { pg_scale = s; pg_dirty = 1; ly_scroll = 0; } }
int layout_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    struct ll L; lay(&L, win_x, win_y, win_w, win_h);
    ly_drag_sb = 0;
    if (my >= L.by && my < L.by + BTN) {
        for (int i = 0; i < B_N; i++) {
            int w = (i == B_DOC || i == B_SRC) ? 90 : i == B_PDF ? 52 : i == B_HTML ? 60 : i == B_IA ? 102 : BTN;
            if (mx < L.bx[i] || mx >= L.bx[i] + w) continue;
            switch (i) {
            case B_OPEN: case B_RELOAD: load_path(); break;
            case B_EDIT:   /* lo que se ve se guarda en la ruta y se abre en el Bloc de notas */
                (void)vfs_write(ly_path, ly_src, ly_src_len);
                status("Editando en el Bloc de notas: guarda alli y pulsa Recargar", 1); gui_open_notepad(ly_path); break;
            case B_DOC: ly_view = 0; ly_scroll = 0; break;
            case B_SRC: ly_view = 1; ly_scroll = 0; break;
            case B_SMALL: if (pg_doc && !ly_view) set_scale(-10); else set_px(ly_px - 1); break;
            case B_BIG: if (pg_doc && !ly_view) set_scale(10); else set_px(ly_px + 1); break;
            case B_PDF: do_export("pdf"); break;
            case B_HTML: do_export("html"); break;
            case B_IA: do_review(); break;
            }
            ly_prompt_focus = 0;
            return 0;
        }
        return 0;
    }
    if (my >= L.pr_y && my < L.st_y) {
        if (mx >= L.gen_x) { ly_prompt_focus = 0; do_generate(); }
        else ly_prompt_focus = 1;
        return 0;
    }
    ly_prompt_focus = 0;
    if (mx >= L.sb_x && my >= L.area_y && my < L.pr_y) { ly_drag_sb = 1; ly_drag_y0 = my; ly_drag_s0 = ly_scroll; return 0; }
    return 0;
}
int layout_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held)
{
    struct ll L; lay(&L, win_x, win_y, win_w, win_h);
    int old = ly_scroll, ms = max_scroll(&L, content_height(&L));
    if (wheel && mx >= L.area_x && mx < L.x + L.w && my >= L.area_y && my < L.pr_y) ly_scroll += wheel * 48;
    if (ly_drag_sb) {
        if (!held) ly_drag_sb = 0;
        else { int track = L.area_h - 8, ch = content_height(&L); if (ch > 0 && track > 0) ly_scroll = ly_drag_s0 + (my - ly_drag_y0) * ch / track; }
    }
    if (ly_scroll > ms) ly_scroll = ms;
    if (ly_scroll < 0) ly_scroll = 0;
    return ly_scroll != old || ly_drag_sb;
}
void layout_handle_key(char c)
{
    if (c == 27) { ly_prompt_focus = 0; return; }
    if (!ly_prompt_focus) {
        if (c == 'k' || c == 16) ly_scroll -= 40;
        else if (c == 'j' || c == 14) ly_scroll += 40;
        else if (c == ' ') ly_scroll += 300;
        else if (c == 'b') ly_scroll -= 300;
        else if (c == '+' || c == '=') { if (pg_doc && !ly_view) set_scale(10); else set_px(ly_px + 1); }
        else if (c == '-') { if (pg_doc && !ly_view) set_scale(-10); else set_px(ly_px - 1); }
        else if (c == 'f' || c == '\t') { ly_view = !ly_view; ly_scroll = 0; }
        else if (c == 'r' || c == 18) load_path();
        else if (c == 'g' || c == '\r' || c == '\n') ly_prompt_focus = 1;
        if (ly_scroll < 0) ly_scroll = 0;
        return;
    }
    if (c == '\b' || c == 127) { while (ly_prompt_len > 0 && ((uint8_t)ly_prompt[ly_prompt_len - 1] & 0xC0) == 0x80) ly_prompt_len--; if (ly_prompt_len) ly_prompt_len--; ly_prompt[ly_prompt_len] = 0; return; }
    if (c == '\r' || c == '\n') { ly_prompt_focus = 0; do_generate(); return; }
    uint8_t u = (uint8_t)c;
    if (u >= 32 && u < 127) { if (ly_prompt_len + 1 < LY_PROMPT_MAX) { ly_prompt[ly_prompt_len++] = c; ly_prompt[ly_prompt_len] = 0; } }
    else if (u >= 0xA0 && ly_prompt_len + 2 < LY_PROMPT_MAX) { ly_prompt[ly_prompt_len++] = (char)(0xC0 | (u >> 6)); ly_prompt[ly_prompt_len++] = (char)(0x80 | (u & 0x3F)); ly_prompt[ly_prompt_len] = 0; }   /* latin-1 -> UTF-8 */
}
