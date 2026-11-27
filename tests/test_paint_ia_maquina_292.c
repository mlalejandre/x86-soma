/* SOMA_292_PAINT_IA: Paint entero (src/apps/paint.c + paint_ia.inc) en el anfitrion con el rasterizador SVG de verdad y el modelo,
 * la pantalla, el disco y los hilos simulados (el hilo del encargo corre en el acto). Comprueba el ciclo completo: el boton IA abre el
 * campo, se escribe y Enter lanza el encargo; el SVG que devuelve el modelo se pinta por pasos (cada paso cubre al menos lo del
 * anterior, el ultimo es el dibujo entero sobre el lienzo), un lienzo sin tocar pasa a 640x480, todo es un solo Ctrl+Z, no se puede
 * pintar encima mientras dibuja; el segundo intento cuando el primero no vale; los fallos del modelo; cerrar la ventana a medias;
 * dos ventanas de Paint (el dibujo va a la suya); el encargo desde el orbe. Con ASan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/apps/paint.c"
#include "web_img.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }
static uint64_t g_ms = 10000; uint64_t timer_get_uptime_ms(void) { return g_ms; }
/* pantalla */
void fb_blit(int32_t x, int32_t y, const uint32_t *s, int32_t w, int32_t h, int32_t st) { (void)x; (void)y; (void)s; (void)w; (void)h; (void)st; }
void fb_blit_scale(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *s, int32_t sw, int32_t sh, int32_t st) { (void)x; (void)y; (void)w; (void)h; (void)s; (void)sw; (void)sh; (void)st; }
void fb_draw_shadow(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t b, int32_t o, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)b; (void)o; (void)c; }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
void fb_line_aa(int32_t a, int32_t b, int32_t c, int32_t d, int32_t w, uint32_t col) { (void)a; (void)b; (void)c; (void)d; (void)w; (void)col; }
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void fb_reset_clip(void) { }
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t c, int sc) { (void)x; (void)y; (void)s; (void)c; (void)sc; }
static char g_txt[4096]; static uint32_t g_tn;
int wf_init(void) { return 1; }
int wf_width(const char *s, uint32_t n, int px, int fl) { (void)s; (void)fl; return (int)n * px / 2; }
void wf_draw(int32_t x, int32_t y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int a) { (void)x; (void)y; (void)px; (void)rgb; (void)fl; (void)a; for (uint32_t i = 0; i < n && g_tn + 2 < sizeof g_txt; i++) g_txt[g_tn++] = s[i]; g_txt[g_tn++] = '|'; g_txt[g_tn] = 0; }
/* portapapeles, disco, PNG */
const uint32_t *clip_image(int *w, int *h) { (void)w; (void)h; return 0; }
int clip_set_image(const uint32_t *p, int w, int h) { (void)p; (void)w; (void)h; return 0; }
void clip_toast(const char *m) { (void)m; }
char *vfs_read_alloc(const char *n, uint32_t *l) { (void)n; (void)l; return 0; }
int vfs_write(const char *n, const char *d, uint32_t l) { (void)n; (void)d; (void)l; return 0; }
int acceso_carpeta(int t, char *o, uint32_t c) { (void)t; snprintf(o, c, "/usuario/Im\303\241genes"); return 0; }   /* SOMA_363_ACCESO */
size_t png_work_size(int w, int h) { (void)w; (void)h; return 0; }
size_t png_encode_rgb(const uint32_t *px, int w, int h, uint8_t *out, size_t cap, void *work) { (void)px; (void)w; (void)h; (void)out; (void)cap; (void)work; return 0; }
/* el modelo */
char llm_last_error[200];
void llm_trace_tag(const char *t) { (void)t; }
static const char *g_resp[4]; static int g_nresp, g_llamadas; static int g_llm_falla;
static char g_ultimo_prompt[1024];
int llm_chat_ex(const char *sys, const char *prompt, const struct llm_img *img, int nimg, int maxt, char *out, uint32_t cap, uint32_t tmo)
{
    (void)sys; (void)img; (void)nimg; (void)maxt; (void)tmo;
    snprintf(g_ultimo_prompt, sizeof g_ultimo_prompt, "%s", prompt);
    g_llamadas++;
    if (g_llm_falla) { snprintf(llm_last_error, sizeof llm_last_error, "no hay conexion con el modelo"); return 0; }
    const char *r = g_resp[g_nresp > 0 ? (g_llamadas - 1 < g_nresp ? g_llamadas - 1 : g_nresp - 1) : 0];
    snprintf(out, cap, "%s", r ? r : ""); return 1;
}
/* hilos: el encargo corre en el acto */
static int g_hilos;
struct tcb *thread_create(const char *name, thread_func_t f, void *a) { (void)name; g_hilos++; f(a); return (struct tcb *)1; }


static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

static const char DIBUJO[] =
    "Aqui tienes tu dibujo:\n```svg\n<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\">"
    "<rect width=\"640\" height=\"480\" fill=\"#87ceeb\"/>"
    "<g><rect x=\"0\" y=\"300\" width=\"640\" height=\"180\" fill=\"#16a34a\"/></g>"
    "<g><circle cx=\"520\" cy=\"90\" r=\"50\" fill=\"#facc15\" stroke=\"#78350f\" stroke-width=\"3\"/></g>"
    "<g stroke=\"#1f2937\" stroke-width=\"3\"><rect x=\"200\" y=\"200\" width=\"200\" height=\"140\" fill=\"#f8fafc\"/><polygon points=\"180,200 300,110 420,200\" fill=\"#dc2626\"/></g>"
    "<g><rect x=\"280\" y=\"270\" width=\"40\" height=\"70\" fill=\"#7c2d12\"/></g>"
    "</svg>\n```";
#define W 900
#define H 640
static void pinta(void) { g_tn = 0; g_txt[0] = 0; paint_render_window(0, 0, W, H, 1); }
static int txt(const char *s) { return strstr(g_txt, s) != 0; }
static void teclea(const char *s) { for (const char *c = s; *c; c++) paint_handle_key(*c); }
static int en_ia(void) { struct pl L; layout(&L, 0, 0, W, H); return L.bx[B_IA] + 4; }
static void pulsa_ia(void) { struct pl L; layout(&L, 0, 0, W, H); paint_handle_click(L.bx[B_IA] + 4, L.by + 4, 0, 0, W, H); }
static void pinta_hasta_fin(int max) { for (int i = 0; i < max && (ia_estado == IA_LISTO || ia_estado == IA_PINTANDO || ia_estado == IA_ERROR); i++) { g_ms += 200; pinta(); } }

int main(void)
{
    struct wimg ref; memset(&ref, 0, sizeof ref);
    /* referencia: el dibujo entero sobre blanco */
    static char svg[8192]; uint32_t sn = ps_extrae(DIBUJO, svg, sizeof svg);
    chk(sn && wi_decode_svg((const uint8_t *)svg, sn, &ref, 640, 480, 0) == 0, "referencia");
    static uint32_t blanco[640 * 480], esperado[640 * 480];
    for (int i = 0; i < 640 * 480; i++) blanco[i] = 0x00FFFFFF;
    ps_mezcla(esperado, blanco, ref.px, 640 * 480);

    int a = paint_inst_new(); paint_select(a); paint_ensure(); pinta();
    chk(cw == 320 && ch == 240, "lienzo nuevo de 320x240");
    (void)en_ia();
    /* 1. el boton abre el campo; Esc lo cierra; vacio no lanza */
    pulsa_ia(); pinta(); chk(ia_abierto && txt("Que dibujo"), "el boton IA abre el campo");
    paint_handle_key(27); chk(!ia_abierto, "Esc lo cierra");
    pulsa_ia(); paint_handle_key('\r'); pinta(); chk(ia_estado == IA_LIBRE && g_llamadas == 0 && txt("Escribe que quieres"), "Enter vacio no lanza");
    /* 2. dibujar */
    g_resp[0] = DIBUJO; g_nresp = 1;
    teclea("una casa con un sol"); paint_handle_key('x'); paint_handle_key(8);
    chk(!strcmp(ia_txt, "una casa con un sol"), "escribir y borrar en el campo");
    paint_handle_key('\r');
    chk(g_llamadas == 1 && strstr(g_ultimo_prompt, "una casa con un sol") && ia_estado == IA_LISTO && !ia_abierto, "Enter lanza el encargo; el modelo responde");
    pinta();
    chk(cw == 640 && ch == 480 && ia_estado == IA_PINTANDO, "lienzo sin tocar: pasa a 640x480 y empieza a pintar");
    chk(txt("Dibujando"), "la barra de estado lo cuenta");
    chk(ia_pasos == 5, "5 pasos (fondo, prado, sol, casa, puerta)");
    {   /* durante el dibujo no se pinta encima */
        struct pl L; layout(&L, 0, 0, W, H); int u0 = u_count;
        paint_handle_click(L.dx + 10, L.dy + 10, 0, 0, W, H); chk(u_count == u0 && txt("") , "clic en el lienzo durante el dibujo: no pinta"); }
    uint32_t prev = 0; int crece = 1, pasos_vistos = 1;
    while (ia_estado == IA_PINTANDO) {
        uint32_t no_blanco = 0; for (int i = 0; i < 640 * 480; i++) if (cv[i] != 0x00FFFFFF) no_blanco++;
        if (no_blanco < prev) crece = 0; prev = no_blanco;
        g_ms += 200; pinta(); pasos_vistos++;
        if (pasos_vistos > 50) break;
    }
    chk(crece, "cada paso tapa al menos lo del anterior");
    chk(ia_estado == IA_LIBRE && ia_resultado == 1, "termina");
    chk(!memcmp(cv, esperado, sizeof esperado), "el lienzo final es el dibujo entero");
    chk(p_dirty == 1, "hay cambios sin guardar");
    pinta(); chk(txt("Dibujo de la IA listo"), "aviso de listo");
    /* 3. un solo Ctrl+Z */
    paint_handle_key(26); { int todo_blanco = 1; for (int i = 0; i < cw * ch; i++) if (cv[i] != 0x00FFFFFF) { todo_blanco = 0; break; } chk(todo_blanco, "Ctrl+Z quita todo el dibujo de una vez"); }
    paint_handle_key(25); chk(!memcmp(cv, esperado, sizeof esperado), "Ctrl+Y lo devuelve");
    /* 4. encima de un dibujo: no cambia el tamano, se pinta encima */
    {   cv[0] = 0x00123456; int u0 = u_count; g_llamadas = 0;
        chk(paint_ia_encargar("un sol") == 0, "encargo desde el orbe"); pinta_hasta_fin(40);
        chk(cw == 640 && ch == 480 && u_count == u0 + 1 && ia_resultado == 1, "encima: mismo tamano, un paso de deshacer"); }
    /* 5. primer intento malo: se pide otra vez */
    g_resp[0] = "Lo siento, no puedo dibujar."; g_resp[1] = DIBUJO; g_nresp = 2; g_llamadas = 0;
    chk(paint_ia_encargar("un perro") == 0 && g_llamadas == 2 && strstr(g_ultimo_prompt, "no valia"), "sin SVG la primera vez: se pide otra vez diciendo por que");
    pinta_hasta_fin(40); chk(ia_resultado == 1, "y la segunda vale");
    g_resp[0] = "<svg viewBox=\"0 0 640 480\"><rect width=\"1\" height=\"1\" fill=\"red\"/></svg>"; g_nresp = 1; g_llamadas = 0;
    { char m[200]; chk(paint_ia_encargar("nada") == 0 && g_llamadas == 2 && paint_ia_estado(m, sizeof m) == -1 && strstr(m, "vacio"), "un dibujo vacio no vale (dos intentos y fallo)"); }
    pinta(); chk(txt("no ha podido dibujarlo") && ia_estado == IA_LIBRE, "el fallo se dice en la barra");
    g_llm_falla = 1;
    { char m[200]; chk(paint_ia_encargar("x") == 0 && paint_ia_estado(m, sizeof m) == -1 && strstr(m, "conexion"), "sin modelo: el motivo"); pinta(); }
    g_llm_falla = 0;
    /* 6. un encargo a la vez */
    g_resp[0] = DIBUJO; g_nresp = 1;
    chk(paint_ia_encargar("uno") == 0 && paint_ia_encargar("dos") == -1, "un encargo a la vez");
    pinta_hasta_fin(40);
    /* 7. dos ventanas: el dibujo va a la suya */
    int b = paint_inst_new(); paint_select(b); paint_ensure(); pinta();
    chk(paint_ia_encargar("un barco") == 0 && paint_ia_slot() == b, "encargo en la ventana B");
    paint_select(a); pinta(); chk(ia_estado == IA_LISTO, "la ventana A no se lo queda");
    paint_select(b); pinta_hasta_fin(40); chk(ia_resultado == 1 && cw == 640 && !memcmp(cv, esperado, sizeof esperado), "se pinta en B");
    /* 8. cerrar la ventana a medias */
    chk(paint_ia_encargar("otro") == 0, "otro encargo en B"); pinta();   /* empieza a pintar */
    paint_inst_drop(b); chk(ia_estado == IA_LIBRE && paint_ia_slot() == -1 && !ia_base && !ia_dib, "cerrar la ventana a medias lo descarta y libera");
    paint_select(a);
    /* 9. animacion: pide repintar mientras pinta */
    chk(paint_ia_encargar("animado") == 0 && paint_animando() == 1, "listo: hay que repintar");
    pinta(); g_ms += 1; int r1 = paint_animando(); g_ms += 200; int r2 = paint_animando();
    chk(r1 == 0 && r2 == 1, "entre pasos espera ~110 ms");
    pinta_hasta_fin(40); chk(paint_animando() == 0, "terminado: nada que repintar");
    paint_inst_drop(a);
    wi_free(&ref);
    printf("paint ia maquina 292: %d/%d correctas (hilos %d)\n", ok_n, total, g_hilos);
    return ok_n == total ? 0 : 1;
}
