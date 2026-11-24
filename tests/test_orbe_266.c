/* SOMA_266_AVISO: el aviso en el orbe (gui_prompter.c con las imitaciones del banco del 259): se ve al momento y la respuesta lo sustituye entera. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { SI_SEARCH = 1, SI_FILE, SI_CODE, SI_TERMINAL, SI_SPARK, SI_EDIT, SI_SPEAKER, SI_CHECK };
static void *kmalloc(uint64_t n) { return malloc(n); }
static void kfree(void *p) { free(p); }
static uint64_t reloj;
static uint64_t timer_get_uptime_ms(void) { return reloj; }
static volatile int her_agent_busy;
static uint32_t her_ans_len = 1, her_thk_len;
static uint32_t her_col_cur = 0x007DD3FCu;
static int her_sn(uint32_t a) { (void)a; return 0; }
static int her_cs(uint32_t a) { (void)a; return 1024; }
static uint32_t her_mix256(uint32_t a, uint32_t b, uint32_t k) { (void)b; (void)k; return a; }
static uint16_t her_cp_cell(uint32_t cp) { return cp < 127 ? (uint16_t)cp : '?'; }
static int dibujados;
static void her_draw_cell(int32_t x, int32_t y, uint16_t c, uint32_t col, int sc) { (void)x; (void)y; (void)c; (void)col; (void)sc; dibujados++; }
static int vec = 1;
#define WF_BOLD 1
#define WF_ITALIC 2
#define WF_MONO 4
#define WF_RAW 0x40
#define WF_LIB 0x100
static int wf_init(void) { return vec; }
static int voz_speak_state(void) { return 0; }
static int wf_cp_adv64(uint32_t cp, int px, int fl) { (void)cp; return ((fl & WF_BOLD) ? px * 6 / 10 : px / 2) * 64; }
static void wf_draw(int32_t x, int32_t y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int a) { (void)x; (void)y; (void)s; (void)n; (void)px; (void)rgb; (void)fl; (void)a; dibujados++; }
static void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
static void fb_reset_clip(void) {}
static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
static void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
static void fb_line_aa(int32_t a, int32_t b, int32_t c, int32_t d, int32_t w, uint32_t col) { (void)a; (void)b; (void)c; (void)d; (void)w; (void)col; }
static int iconos[16]; static int niconos;
static void si_icon(int i, int32_t x, int32_t y, int32_t s, uint32_t c) { (void)x; (void)y; (void)s; (void)c; if (niconos < 16) iconos[niconos++] = i; }
static void si_text_c(int32_t x, int32_t t, int32_t w, int32_t h, const char *s, int px, uint32_t c, int b) { (void)x; (void)t; (void)w; (void)h; (void)s; (void)px; (void)c; (void)b; if (niconos < 16) iconos[niconos++] = 99; }
static int letra = 2;
int lcm_cfg_get(int q) { return q == 6 ? letra : 0; }
static void chat_fase(const char *q) { (void)q; }                 /* SOMA_285_CHAT: el chat no esta en este banco */
static void chat_fase_orden(const char *l) { (void)l; }
#include "gui_prompter.c"


static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    const char *av = "Voy a programarlo en C con el modelo de IA: primero planifico el programa. Suele tardar unos 2 minutos, dame un momento.";
    her_pr_turno();
    gui_fase("lcm"); her_pr_aviso(av);
    CK(pr_aviso == 1 && pr_src_n == strlen(av) && !strcmp(pr_src, av) && pr_nuevo == 1, "el aviso se escribe en el orbe");
    CK(pr_nf == 1 && pr_f[0] == FA_LCM, "la fase es la del LCM");
    pr_nuevo = 0;                                                     /* el escritorio ya lo empezo a revelar */
    gui_fase("programador"); her_pr_pensamiento("Fase 2/6: escribiendo el codigo");
    CK(!strcmp(pr_src, av) && !strcmp(pr_thk, "Fase 2/6: escribiendo el codigo"), "el pensamiento no borra el aviso");
    her_pr_fin("[Razonamiento]:\nplan\n\n[Dictamen]:\nHecho: serpiente.c compila.");
    CK(!strcmp(pr_src, "Hecho: serpiente.c compila.") && pr_nuevo == 1 && pr_aviso == 0, "la respuesta sustituye al aviso y se revela desde el principio");
    CK(pr_nf == 4 && pr_f[0] == FA_LCM && pr_f[1] == FA_PROGRAMADOR && pr_f[2] == FA_PENSAR && pr_f[3] == FA_HECHO, "fases: LCM, programador, pensando, hecho (%d)", pr_nf);
    /* con el texto en directo (stream): el aviso se borra al empezar a escribir */
    her_pr_turno(); her_pr_aviso(av); pr_nuevo = 0;
    her_pr_stream_ini(); her_pr_stream("Un agujero negro es");
    CK(pr_aviso == 0 && !strcmp(pr_src, "Un agujero negro es"), "el texto en directo sustituye al aviso");
    pr_nuevo = 0; her_pr_fin("Un agujero negro es una region del espacio.");
    CK(pr_nuevo == 0 && !strcmp(pr_src, "Un agujero negro es una region del espacio."), "como antes: lo que ya se escribia no se vuelve a revelar");
    /* sin aviso: lo de siempre */
    her_pr_turno(); her_pr_stream_ini(); her_pr_stream("hola"); pr_nuevo = 0; her_pr_fin("hola mundo");
    CK(pr_nuevo == 0 && pr_aviso == 0, "sin aviso nada cambia");
    her_pr_turno(); CK(pr_aviso == 0 && pr_src_n == 0 && pr_nf == 0, "pregunta nueva: limpio");
    printf("%s aviso en el orbe: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
