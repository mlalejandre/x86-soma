/* SOMA_289_VISOR: el visor de documentos del navegador (src/apps/web_visor.c) entero en el anfitrion, con el motor LaTeX de
 * verdad (tex.c) y la pantalla, el disco, la red y el lector de PDF simulados. Comprueba el ciclo completo: la pagina marcador
 * abre el visor, se lee el archivo, se maqueta y se pinta la hoja, el codigo fuente, la rueda, los botones y el teclado; una
 * pagina normal lo cierra; un PDF va al lector en una instancia propia y se deja seleccionada la que estaba; los errores (no
 * existe, no es PDF, es una pagina web) salen como tarjeta y no como pagina rota. Con ASan: sin fugas ni accesos fuera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- simulaciones ---- */
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_ms = 1000; uint64_t timer_get_uptime_ms(void) { return g_ms; }
/* pantalla: se cuentan las llamadas y el texto dibujado */
static int g_rects, g_texts, g_blits; static char g_dibujado[8192]; static uint32_t g_dn;
static void apunta(const char *s, uint32_t n) { for (uint32_t i = 0; i < n && g_dn + 2 < sizeof g_dibujado; i++) g_dibujado[g_dn++] = s[i]; g_dibujado[g_dn++] = '|'; g_dibujado[g_dn] = 0; }
static void limpia_pantalla(void) { g_rects = g_texts = g_blits = 0; g_dn = 0; g_dibujado[0] = 0; }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; g_rects++; }
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; g_rects++; }
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
void fb_draw_shadow(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t b, int32_t o, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)b; (void)o; (void)c; }
void fb_line_aa(int32_t a, int32_t b, int32_t c, int32_t d, int32_t w, uint32_t col) { (void)a; (void)b; (void)c; (void)d; (void)w; (void)col; }
static int g_clip_ok = 1; static int32_t g_cx, g_cy, g_cw, g_ch;
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h) { g_cx = x; g_cy = y; g_cw = w; g_ch = h; if (w < 0 || h < 0) g_clip_ok = 0; }
void fb_reset_clip(void) { g_cw = g_ch = 0; }
int wf_init(void) { return 1; }
int wf_width(const char *s, uint32_t n, int px, int fl) { (void)s; (void)fl; return (int)n * px / 2; }
int wf_ascent(int px) { return px * 3 / 4; }
int wf_descent(int px) { return px / 4; }
int wf_ui_px(int px) { return px; }
void wf_draw(int32_t x, int32_t y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int a) { (void)x; (void)y; (void)px; (void)rgb; (void)fl; (void)a; g_texts++; apunta(s, n); }
/* disco */
static const char *g_fs_nombre, *g_fs_dato; static char g_escrito[128]; static uint32_t g_escrito_n;
int vfs_size(const char *n) { return g_fs_nombre && !strcmp(n, g_fs_nombre) ? (int)strlen(g_fs_dato) : -1; }
char *vfs_read_alloc(const char *n, uint32_t *len) { if (vfs_size(n) < 0) return 0; uint32_t L = (uint32_t)strlen(g_fs_dato); char *d = malloc(L + 1); memcpy(d, g_fs_dato, L + 1); *len = L; return d; }
int vfs_write(const char *n, const char *d, uint32_t len) { (void)d; snprintf(g_escrito, sizeof g_escrito, "%s", n); g_escrito_n = len; return 0; }
/* portapapeles y GUI */
static char g_toast[160]; static uint32_t g_clip_n;
int clip_set_text(const char *s, uint32_t n) { (void)s; g_clip_n = n; return 0; }
void clip_toast(const char *m) { snprintf(g_toast, sizeof g_toast, "%s", m); }
static char g_maq[160]; void gui_open_layout_file(const char *p) { snprintf(g_maq, sizeof g_maq, "%s", p); }
/* red */
#include "web_net.h"
static int g_red_status = 200; static const char *g_red_ctype = "application/pdf", *g_red_body; static int g_red_falla;
int wn_fetch(const struct wn_req *rq, struct wn_resp *rs)
{
    (void)rq; memset(rs, 0, sizeof *rs);
    if (g_red_falla) { snprintf(rs->err, sizeof rs->err, "sin conexion"); return -1; }
    rs->status = g_red_status; snprintf(rs->ctype, sizeof rs->ctype, "%s", g_red_ctype);
    uint32_t L = (uint32_t)strlen(g_red_body); rs->body = malloc(L + 1); memcpy(rs->body, g_red_body, L + 1); rs->len = L; return 0;
}
void wn_free(struct wn_resp *rs) { free(rs->body); rs->body = 0; }
int wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs) { return wn_fetch(rq, rs); }   /* SOMA_317: el visor descarga con netsvc */
void wn_free_isolated(struct wn_resp *rs) { wn_free(rs); }
/* lector de PDF: instancias de mentira */
static int pv_sel = 1, pv_used[9], pv_open[9], pv_pintado, pv_teclas, pv_clics; static uint8_t *pv_data[9]; static char pv_err[64];
int pdfview_inst_new(void) { for (int k = 2; k <= 8; k++) if (!pv_used[k]) { pv_used[k] = 1; pv_sel = k; return k; } return 0; }
void pdfview_select(int s) { pv_sel = s; }
int pdfview_selected(void) { return pv_sel; }
void pdfview_inst_drop(int s) { if (s >= 1 && s <= 8) { pv_used[s] = 0; pv_open[s] = 0; free(pv_data[s]); pv_data[s] = 0; } }
int pdfview_open_data(const char *n, uint8_t *d, uint32_t len) { (void)n; if (len < 8 || memcmp(d, "%PDF-", 5) || !strstr((const char *)d, "%%EOF")) { free(d); snprintf(pv_err, sizeof pv_err, "PDF roto"); return -1; } pv_data[pv_sel] = d; pv_open[pv_sel] = 1; return 0; }
const char *pdfview_doc_title(void) { return pv_open[pv_sel] ? "Titulo del PDF" : ""; }
const char *pdfview_error(void) { return pv_err; }
void pdfview_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int f) { (void)x; (void)y; (void)w; (void)h; (void)f; pv_pintado = pv_sel; }
int pdfview_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)mx; (void)my; (void)x; (void)y; (void)w; (void)h; pv_clics++; return 0; }
void pdfview_handle_key(char c) { (void)c; pv_teclas++; }
int pdfview_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wh, int held) { (void)mx; (void)my; (void)x; (void)y; (void)w; (void)h; (void)held; return wh != 0; }

#include "../src/apps/web_visor.c"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }
static int dibujo_tiene(const char *s) { return strstr(g_dibujado, s) != 0; }

static const char TEX[] =
    "\\documentclass{article}\n\\title{Ondas gravitacionales}\n\\author{SOMA}\n\\begin{document}\n\\maketitle\n"
    "\\section{Introduccion}\nLas ondas se describen con $h_{\\mu\\nu}$ y la energia es $E = mc^2$.\n"
    "\\begin{equation} \\frac{a}{b} = \\sqrt{x^2 + y^2} \\end{equation}\n"
    "\\begin{itemize}\\item Primera \\item Segunda\\end{itemize}\n\\end{document}\n";

int main(void)
{
    char tit[200];
    const int X = 0, Y = 100, W = 1000, H = 700;
    /* 1. pagina normal: nada */
    chk(wv_sync("Ejemplo", "https://ejemplo.com/") == 0 && !wv_activo(), "una pagina normal no abre el visor");
    /* 2. LaTeX local */
    g_fs_nombre = "/workspace/onda.tex"; g_fs_dato = TEX;
    wt_marca(tit, sizeof tit, WT_TEX, "onda.tex");
    chk(wv_sync(tit, "file:///workspace/onda.tex") == 1 && wv_activo(), "el marcador abre el visor y pide la descarga");
    chk(wv_sync(tit, "file:///workspace/onda.tex") == 0, "el mismo documento no se pide dos veces");
    limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(dibujo_tiene("Abriendo el documento LaTeX") && dibujo_tiene("onda.tex"), "mientras se lee: tarjeta con el nombre");
    uint64_t h0 = wv_huella(); g_ms += 200; chk(wv_huella() != h0, "la tarjeta de espera se anima");
    wv_descarga();
    limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ABIERTO && s_out.nops > 10, "documento maquetado");
    chk(dibujo_tiene("gravitacionales"), "se pinta el titulo del documento");
    chk(dibujo_tiene("Introduccion"), "se pinta la seccion");
    chk(dibujo_tiene("Documento") && dibujo_tiene("Abrir en Maquetaci"), "la barra del visor");
    chk(!strcmp(wv_titulo(), "Ondas gravitacionales"), "la pestana lleva el titulo del documento");
    chk(s_out.errors == 0, "LaTeX sin construcciones desconocidas");
    int h_doc = s_alto;
    /* zoom */
    int px0 = s_px; (void)wv_tecla('+'); limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_px == px0 + 2 && s_alto > h_doc, "A+ agranda la letra y la hoja crece");
    (void)wv_tecla('-'); chk(s_px == px0, "A- la devuelve");
    /* desplazamiento con la rueda: dentro de los limites */
    (void)wv_raton(500, 400, X, Y, W, H, -3, 0); chk(s_scroll >= 0 && s_scroll <= max_scroll(), "la rueda respeta los limites");
    for (int i = 0; i < 200; i++) (void)wv_raton(500, 400, X, Y, W, H, -5, 0);
    chk(s_scroll == max_scroll(), "rueda hasta el final");
    (void)wv_tecla(1); chk(s_scroll == 0, "Inicio vuelve arriba");
    /* boton Codigo */
    struct btn b[B_N]; (void)botones(X, W, b);
    (void)wv_click(b[B_COD].x + 3, Y + 20, X, Y, W, H); limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_codigo == 1 && dibujo_tiene("\\documentclass"), "Codigo muestra el fuente");
    chk(dibujo_tiene("|1|") || dibujo_tiene("1|"), "con numeros de linea");
    (void)wv_click(b[B_DOC].x + 3, Y + 20, X, Y, W, H); chk(s_codigo == 0, "Documento vuelve a la hoja");
    /* copiar y maquetacion (archivo local: se abre el mismo, no se guarda copia) */
    (void)wv_click(b[B_COPIA].x + 3, Y + 20, X, Y, W, H); chk(g_clip_n == strlen(TEX) && strstr(g_toast, "copiado"), "Copiar codigo");
    g_escrito[0] = 0; (void)wv_click(b[B_MAQ].x + 3, Y + 20, X, Y, W, H);
    chk(!strcmp(g_maq, "/workspace/onda.tex") && !g_escrito[0], "Maquetacion abre el archivo local");
    /* ventana estrecha: los botones que no caben no se pintan ni se pulsan */
    { struct btn bb[B_N]; int nb = botones(0, 420, bb); chk(nb < B_N && bb[nb - 1].x + bb[nb - 1].w <= 420, "barra estrecha: solo los botones que caben"); }
    limpia_pantalla(); wv_pinta(0, 0, 200, 60); chk(g_clip_ok, "ventana diminuta sin recortes negativos");
    /* 3. al navegar a otra pagina se cierra (y libera) */
    chk(wv_sync("Otra pagina", "https://otra.com/") == 0 && !wv_activo() && !s_src, "otra pagina cierra el visor y libera el documento");
    /* transitorio: la URL nueva con el titulo viejo no abre nada */
    chk(wv_sync(tit, "https://otra.com/x") == 0 && !wv_activo(), "URL nueva con el titulo de la anterior: no se abre el visor");
    /* 4. LaTeX remoto: Maquetacion guarda una copia en Descargas */
    g_red_ctype = "text/plain"; g_red_body = TEX;
    wt_marca(tit, sizeof tit, WT_TEX, "paper.tex");
    chk(wv_sync(tit, "https://raw.ejemplo.org/paper.tex") == 1, "tex remoto");
    wv_descarga(); wv_pinta(X, Y, W, H); (void)botones(X, W, b);
    (void)wv_click(b[B_MAQ].x + 3, Y + 20, X, Y, W, H);
    chk(!strcmp(g_escrito, "/workspace/Descargas/paper.tex") && g_escrito_n == strlen(TEX) && !strcmp(g_maq, g_escrito), "remoto: se guarda en Descargas y se abre");
    /* 5. tex remoto que resulta ser una pagina web */
    g_red_body = "<!DOCTYPE html><html><body>Hola</body></html>";
    wt_marca(tit, sizeof tit, WT_TEX, "otro.tex");
    (void)wv_sync(tit, "https://github.com/u/r/blob/main/otro.tex"); wv_descarga(); limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && dibujo_tiene("pagina web"), "un .tex que es HTML: tarjeta de error clara");
    /* 6. PDF remoto: instancia propia, la seleccion anterior intacta */
    g_red_ctype = "application/pdf"; g_red_body = "%PDF-1.4\n1 0 obj << >> endobj\n%%EOF";
    wt_marca(tit, sizeof tit, WT_PDF, "informe.pdf");
    pv_sel = 1;
    chk(wv_sync(tit, "https://a.com/informe.pdf") == 1, "pdf remoto");
    wv_descarga(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ABIERTO && s_slot >= 2 && pv_open[s_slot] && pv_pintado == s_slot, "el PDF se abre y se pinta en su instancia");
    chk(pv_sel == 1, "la instancia que estaba seleccionada sigue seleccionada");
    chk(!strcmp(wv_titulo(), "Titulo del PDF"), "pestana con el titulo del PDF");
    (void)wv_click(300, 300, X, Y, W, H); (void)wv_tecla('n'); chk(pv_clics == 1 && pv_teclas == 1 && pv_sel == 1, "clics y teclas van al lector");
    chk(wv_tecla(12) == 0 && wv_tecla(20) == 0, "los atajos del navegador no se los queda el lector");
    int slot = s_slot; wv_cierra(); chk(!pv_used[slot] && !wv_activo(), "cerrar libera la instancia del lector");
    /* 7. errores del PDF */
    g_red_body = "<html><body>Inicia sesion</body></html>"; g_red_ctype = "text/html";
    (void)wv_sync(tit, "https://a.com/informe.pdf"); wv_descarga(); limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && dibujo_tiene("pagina web"), "un .pdf que es una pagina: lo dice");
    wv_cierra(); g_red_falla = 1;
    (void)wv_sync(tit, "https://a.com/informe.pdf"); wv_descarga(); limpia_pantalla(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && dibujo_tiene("sin conexion"), "sin red: lo dice");
    wv_cierra(); g_red_falla = 0; g_red_status = 404; g_red_body = "no";
    (void)wv_sync(tit, "https://a.com/informe.pdf"); wv_descarga(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && strstr(s_err, "404"), "HTTP 404");
    wv_cierra(); g_red_status = 200; g_red_ctype = "application/pdf"; g_red_body = "%PDF-roto";
    (void)wv_sync(tit, "https://a.com/informe.pdf"); wv_descarga(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && strstr(s_err, "PDF roto") && !s_slot, "PDF que el lector no entiende: error y sin instancia colgada");
    wv_cierra();
    /* 8. local que no existe */
    wt_marca(tit, sizeof tit, WT_PDF, "nada.pdf");
    (void)wv_sync(tit, "file:///workspace/nada.pdf"); wv_descarga(); wv_pinta(X, Y, W, H);
    chk(s_estado == E_ERROR && strstr(s_err, "No se encuentra"), "archivo local que no existe");
    wv_cierra();
    /* 9. respuesta tardia: se descarta */
    g_red_body = "%PDF-1.4 bien";
    (void)wv_sync(tit, "https://a.com/nada.pdf");
    wt_marca(tit, sizeof tit, WT_PDF, "otro.pdf"); (void)wv_sync(tit, "https://a.com/otro.pdf");   /* cambia antes de que llegue */
    uint32_t gen_viejo = s_gen_bajada; s_gen_bajada = gen_viejo - 1; wv_descarga(); s_gen_bajada = gen_viejo;
    chk(s_estado == E_BAJANDO && !s_dato, "la descarga de la peticion anterior se descarta");
    wv_descarga(); chk(s_estado == E_LISTO && s_dato, "la buena llega");
    wv_cierra();
    chk(!s_dato && !s_src && !s_slot, "todo liberado");
    free(s_ops); free(s_pool);
    printf("visor maquina 289: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
