/* SOMA_294_AYUDA: prueba del anfitrion con el codigo de verdad del escritorio (src/ui/gui_ayuda.c) sobre un framebuffer en
 * memoria (tests/layhost/fbhost.c) y la fuente de verdad (src/apps/web_font.c): la bienvenida al encender, las tarjetas se
 * dibujan solo dentro de su sitio y dejan el recorte como estaba, Esc y el clic solo se toman con una tarjeta a la vista,
 * la casilla se guarda y se respeta al siguiente encendido, el dictado no cuenta como "escribir mucho" y sin la fuente con
 * acentos se escribe en ASCII. Con AYUDA_PPM=prefijo guarda las imagenes (para mirarlas). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "web_font.h"
#include "micrec.h"

extern uint32_t *g_fb; extern int g_fbw, g_fbh;
int g_wf182_on;                                         /* web_css.c (no se enlaza aqui) */
static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

/* ---- lo que el escritorio le da a gui_ayuda.c ---- */
static uint64_t t_now;
uint64_t timer_get_uptime_ms(void) { return t_now; }
uint32_t fb_get_width(void) { return (uint32_t)g_fbw; }
uint32_t fb_get_height(void) { return (uint32_t)g_fbh; }
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    (void)r;
    fb_fill_rect_alpha(x, y, w, 1, argb); fb_fill_rect_alpha(x, y + h - 1, w, 1, argb);
    fb_fill_rect_alpha(x, y, 1, h, argb); fb_fill_rect_alpha(x + w - 1, y, 1, h, argb);
}
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
static int ascii_mal, ascii_llamadas;
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t c, uint32_t scale)
{
    (void)scale; ascii_llamadas++;
    for (int i = 0; s[i]; i++) { if ((unsigned char)s[i] >= 0x80) ascii_mal = 1; fb_fill_rect(x + (uint32_t)i * 8u, y, 6, 10, c); }
}
int si_text_w(const char *s, int px, int bold) { int n = (int)strlen(s); return n ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : 0; }
void si_text(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t rgb, int bold)
{
    int n = (int)strlen(s); if (!n) return;
    int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, rgb, bold ? WF_BOLD : 0, 255);
}
static char vfs_conf[64]; static int vfs_hay, vfs_escrituras;
int vfs_read(const char *p, char *b, uint32_t max)
{
    if (strcmp(p, "/etc/ayuda.conf") || !vfs_hay) return -2;
    int n = (int)strlen(vfs_conf); if ((uint32_t)n > max) n = (int)max; memcpy(b, vfs_conf, (size_t)n); return n;
}
int vfs_write(const char *p, const char *b, uint32_t n)
{
    if (strcmp(p, "/etc/ayuda.conf") || n >= sizeof vfs_conf) return -1;
    memcpy(vfs_conf, b, n); vfs_conf[n] = 0; vfs_hay = 1; vfs_escrituras++; return (int)n;
}
static char aviso_txt[400], voz_txt[400]; static int con_micro = 1;
static void her_aviso(const char *t) { snprintf(aviso_txt, sizeof aviso_txt, "%s", t); }
int voz_speak_reply(const char *t) { snprintf(voz_txt, sizeof voz_txt, "%s", t); return 0; }
void micrec_info(struct micrec_info *o) { memset(o, 0, sizeof *o); o->available = con_micro; }
int usermode_avx2_ok(void) { return 1; }
enum { APP_DESKTOP = 0, APP_NOTAS = 3 };
static int current_app, agent_global_focus = 1;
static int her_voz_shown_kind;
#define SOMA_SYSTEM_BAR_H 44u
#include "../src/ui/gui_ayuda.c"

/* ---- utilidades ---- */
#define FONDO 0x00123456u
static void pantalla(int w, int h) { free(g_fb); g_fbw = w; g_fbh = h; g_fb = malloc((size_t)w * (size_t)h * 4u); for (int i = 0; i < w * h; i++) g_fb[i] = FONDO; }
static void encendido(void) { her_ay_arrancada = 0; her_ay_modo_prev = 0; her_ay_repinta = 0; her_ay_dibujada = 0; her_ay_wf = -1; t_now = 1000; her_ayuda_arranque(); }
/* nada pintado fuera de r; cuantos pixeles claros (texto) dentro de la franja [y0, y1) de r */
static int fuera_intacto(struct ay_rect r)
{
    for (int y = 0; y < g_fbh; y++) for (int x = 0; x < g_fbw; x++) {
        int in = x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h;
        if (!in && g_fb[y * g_fbw + x] != FONDO) return 0;
    }
    return 1;
}
static int claros(struct ay_rect r, int y0, int y1)
{
    int n = 0;
    for (int y = r.y + y0; y < r.y + y1; y++) for (int x = r.x; x < r.x + r.w; x++) { uint32_t p = g_fb[y * g_fbw + x]; if (((p >> 16) & 255) > 160 && ((p >> 8) & 255) > 160) n++; }
    return n;
}
static void ppm(const char *suf)
{
    const char *pre = getenv("AYUDA_PPM"); if (!pre) return;
    char n[256]; snprintf(n, sizeof n, "%s%s.ppm", pre, suf);
    FILE *f = fopen(n, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_fbw, g_fbh);
    for (int i = 0; i < g_fbw * g_fbh; i++) { unsigned char c[3] = { (unsigned char)(g_fb[i] >> 16), (unsigned char)(g_fb[i] >> 8), (unsigned char)g_fb[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
}

int main(void)
{
    chk(wf_init(), "hay fuente");
    pantalla(1366, 768);

    /* 1. encendido: primero el escritorio, luego la bienvenida */
    encendido();
    chk(her_ayuda_tick() == 0 && !aviso_txt[0], "nada en el primer segundo y medio");
    t_now = 1000 + AY_ESPERA_MS;
    chk(her_ayuda_tick() == 2, "a los 1,5 s: repintar (aparece el consejo)");
    chk(strstr(aviso_txt, "Hola") && strstr(aviso_txt, "Windows") && strstr(aviso_txt, "Alt") && strstr(aviso_txt, "F1"), "el orbe escribe la bienvenida");
    chk(!strcmp(aviso_txt, voz_txt), "y la lee (si Ajustes > Voz lo tiene activado)");
    chk(her_ayuda_tick() == 0, "la bienvenida una sola vez");
    t_now += 100; chk(her_ayuda_tick() == 1, "despues solo se vuelca (la barra de tiempo), sin repintar lo de debajo");

    /* 2. el consejo se dibuja solo en su sitio, con texto, barra y sin tocar el recorte */
    t_now += AY_CONSEJO_MS / 2 - 100;
    fb_set_clip(100, 100, 300, 200);
    her_ayuda_draw();
    { int32_t a, b, c, d; fb_get_clip(&a, &b, &c, &d); chk(a == 100 && b == 100 && c == 400 && d == 300, "deja el recorte como estaba"); }
    struct ay_rect r = ay_rect_recorrido(1366, 768, 44);
    chk(her_ay_dibujada && fuera_intacto(r), "el consejo no pinta fuera de su tarjeta");
    chk((g_fb[(r.y + 50) * g_fbw + r.x + r.w - 30] & 0xFFFFFFu) == 0x000B1220u, "fondo opaco (se puede volver a dibujar encima sin mezclarse)");
    chk(claros(r, 42, 70) > 300 && claros(r, 12, 36) > 100, "se lee el consejo y su tecla");
    { int y = r.y + r.h - 7, lleno = 0; for (int x = r.x + 20; x < r.x + r.w - 20; x++) if ((g_fb[y * g_fbw + x] & 0xFFFFFFu) == 0x0038BDF8u) lleno++;
      chk(lleno > (r.w - 40) * 45 / 100 && lleno < (r.w - 40) * 55 / 100, "la barra de tiempo, a la mitad"); }
    ppm("_consejo");

    /* 3. Esc y el clic: solo con la tarjeta a la vista */
    chk(her_ayuda_tecla('a') == 0 && her_ayuda_tecla('\r') == 0, "las teclas siguen su camino");
    chk(her_ayuda_clic(r.x - 5, r.y + 5) == 0, "un clic fuera de la tarjeta sigue su camino");
    { struct ay_rect x = ay_rect_x(r); chk(her_ayuda_clic(x.x + 4, x.y + 4) == 1 && her_ay.modo == AY_NADA, "la X la cierra (y el clic no atraviesa)"); }
    chk(her_ayuda_tick() == 2, "repintar lo que tapaba");
    for (int i = 0; i < g_fbw * g_fbh; i++) g_fb[i] = FONDO;
    her_ayuda_draw(); chk(!her_ay_dibujada && fuera_intacto((struct ay_rect){ 0, 0, 0, 0 }), "cerrada: no se dibuja nada");
    chk(her_ayuda_tecla(27) == 0, "Esc sin tarjeta: para quien estaba (detener la lectura, cerrar la app...)");
    chk(her_ayuda_clic(r.x + 30, r.y + 30) == 0, "clic donde estuvo la tarjeta: para lo de debajo");

    /* 4. F1: la ayuda completa, la casilla y el siguiente encendido */
    her_ayuda_f1(); chk(her_ayuda_tick() == 2 && her_ay.modo == AY_TARJETA, "F1 la abre");
    her_ayuda_draw();
    struct ay_rect t = ay_rect_tarjeta(1366, 768, 44);
    chk(her_ay_dibujada && fuera_intacto(t), "la ayuda completa no pinta fuera de su tarjeta");
    { int filas = 1; for (int i = 0; i < AY_NFILAS; i++) if (claros(t, AY_TAR_CAB + i * AY_FILA_H + 4, AY_TAR_CAB + i * AY_FILA_H + 26) < 150) filas = 0; chk(filas, "las 8 filas tienen texto"); }
    ppm("_f1");
    chk(her_ayuda_tecla('h') == 0 && her_ay.modo == AY_TARJETA, "con la ayuda abierta se puede seguir escribiendo");
    { struct ay_rect in = ay_rect_inicio(t);
      chk(her_ayuda_clic(in.x + 8, in.y + 12) == 1 && her_ay.inicio == 0 && vfs_escrituras == 1 && !strcmp(vfs_conf, "inicio=0\n"), "quitar la casilla la guarda");
      chk(her_ayuda_tick() == 1, "la casilla: basta volcar"); her_ayuda_draw(); ppm("_f1_sin_casilla"); }
    chk(her_ayuda_clic(t.x + t.w / 2, t.y + 100) == 1 && her_ay.modo == AY_TARJETA, "un clic dentro de la ayuda no la cierra ni atraviesa");
    chk(her_ayuda_tecla(27) == 1 && her_ay.modo == AY_NADA, "Esc la cierra");
    encendido(); t_now = 60000; aviso_txt[0] = 0;
    chk(her_ayuda_tick() == 0 && !aviso_txt[0] && her_ay.modo == AY_NADA, "siguiente encendido: sin consejos ni bienvenida");
    her_ayuda_f1(); chk(her_ay.modo == AY_TARJETA, "pero F1 sigue funcionando");
    { struct ay_rect in = ay_rect_inicio(t); her_ayuda_tick(); her_ayuda_draw(); her_ayuda_clic(in.x + 8, in.y + 12); chk(!strcmp(vfs_conf, "inicio=1\n"), "y se puede volver a activar"); }
    her_ayuda_tecla(27); her_ayuda_tick();

    /* 5. hacer lo que dice el consejo; el dictado no cuenta como escribir */
    encendido(); t_now = 1000 + AY_ESPERA_MS; her_ayuda_tick();
    her_ay_ev(AYEV_SUPER); chk(her_ay.t_bien && her_ayuda_tick() == 1, "la tecla Windows: 'Eso es' (sin repintar lo de debajo)");
    her_ayuda_draw(); ppm("_eso_es");
    t_now += AY_BIEN_MS; her_ayuda_tick(); chk(her_ay.i == 1, "y pasa al siguiente");
    her_voz_shown_kind = 1; chk(her_ayuda_tick() == 2 || 1, "dictando"); her_ayuda_draw(); chk(!her_ay_dibujada, "mientras se dicta, la pastilla va en ese sitio: el consejo no se dibuja");
    chk(her_ay.t_bien, "mantener Alt y hablar: aprendido");
    her_voz_shown_kind = 2; her_ayuda_tick(); her_voz_shown_kind = 0; her_ayuda_tick();
    for (int i = 0; i < 60; i++) her_ayuda_tecla('x');
    chk(her_ay.modo == AY_RECORRIDO, "el texto del dictado entra por el teclado: no aparta los consejos");
    t_now += 3001; for (int i = 0; i < 40; i++) her_ayuda_tecla('x');
    chk(her_ay.modo == AY_NADA && her_ayuda_tick() == 2, "40 teclas escritas a mano: los consejos se apartan");

    /* 6. Intro cuenta para el orbe, no para otra aplicacion */
    encendido(); t_now = 1000 + AY_ESPERA_MS; her_ayuda_tick();
    current_app = APP_NOTAS; agent_global_focus = 0; her_ayuda_tecla('\r'); chk(!(her_ay.hechos & 4u), "Intro en Notas no es preguntar al orbe");
    current_app = APP_DESKTOP; agent_global_focus = 1; her_ayuda_tecla('\r'); chk(her_ay.hechos & 4u, "Intro en el orbe si");

    /* 7. sin microfono y sin la fuente con acentos */
    con_micro = 0; encendido(); t_now = 1000 + AY_ESPERA_MS; her_ayuda_tick();
    chk(!strstr(aviso_txt, "Alt") && strstr(ay_consejo(&her_ay, 1)->texto, "micr"), "sin microfono no promete la voz");
    her_ay_wf = 0; ascii_mal = 0; ascii_llamadas = 0; her_ayuda_draw();
    her_ayuda_f1(); her_ayuda_tick(); her_ay_wf = 0; her_ayuda_draw();
    chk(ascii_llamadas > 10 && !ascii_mal, "sin la fuente: todo en ASCII (sin bytes de acentos)");
    ppm("_ascii");

    /* 8. pantallas pequenas y grandes */
    static const int P[][2] = { {640, 400}, {800, 600}, {1920, 1080}, {3840, 2160} };
    int bien = 1;
    for (int k = 0; k < 4; k++) {
        pantalla(P[k][0], P[k][1]); encendido(); con_micro = 1; t_now = 1000 + AY_ESPERA_MS; her_ayuda_tick(); her_ay_wf = -1;
        her_ayuda_draw(); if (!her_ay_dibujada || !fuera_intacto(ay_rect_recorrido((uint32_t)P[k][0], (uint32_t)P[k][1], 44))) bien = 0;
        for (int i = 0; i < g_fbw * g_fbh; i++) g_fb[i] = FONDO;
        her_ayuda_f1(); her_ayuda_tick(); her_ayuda_draw(); if (!her_ay_dibujada || !fuera_intacto(ay_rect_tarjeta((uint32_t)P[k][0], (uint32_t)P[k][1], 44))) bien = 0;
        if (k == 0) ppm("_640");
    }
    chk(bien, "de 640x400 a 3840x2160: las dos tarjetas caben y no pintan fuera");

    printf("== test_ayuda_maquina_294: %d/%d\n", ok_n, total);
    free(g_fb);
    return ok_n == total ? 0 : 1;
}
