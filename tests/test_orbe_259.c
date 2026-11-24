/* SOMA_259_ORBE: prueba en el anfitrion del texto del orbe (formato, maquetacion, desplazamiento y fases).
 * cc -O1 -Wall -Isrc/ui -o /tmp/t tests/test_orbe_259.c && /tmp/t */
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

/* celdas -> texto con marcas: {negrita} /cursiva/ `codigo` [mate] ^sup ,sub _sub_ #titulo */
static char *marcas(const char *src)
{
    static char o[4096]; uint32_t p = 0; uint16_t ant = 0;
    pr_reset(); pr_respuesta(src, (uint32_t)strlen(src), 0); pr_prepara(600);
    for (uint32_t i = 0; i < pr_n && p + 16 < sizeof o; ++i) {
        uint16_t a = pr_c[i].at;
        if ((a & AT_B) && !(ant & AT_B)) o[p++] = '{';
        if (!(a & AT_B) && (ant & AT_B)) o[p++] = '}';
        if ((a & AT_I) && !(ant & AT_I)) o[p++] = '/';
        if (!(a & AT_I) && (ant & AT_I)) o[p++] = '/';
        if ((a & AT_CODE) && !(ant & AT_CODE)) o[p++] = '`';
        if (!(a & AT_CODE) && (ant & AT_CODE)) o[p++] = '`';
        if ((a & AT_MATH) && !(ant & AT_MATH)) o[p++] = '[';
        if (!(a & AT_MATH) && (ant & AT_MATH)) o[p++] = ']';
        if ((a & AT_U) && !(ant & AT_U)) o[p++] = '_';
        if (!(a & AT_U) && (ant & AT_U)) o[p++] = '_';
        if (a & AT_SUP) o[p++] = '^';
        if (a & AT_SUB) o[p++] = ',';
        if ((a & AT_HR)) { memcpy(o + p, "<HR>", 4); p += 4; ant = a; continue; }
        uint32_t cp = pr_c[i].cp;
        if (cp < 128) o[p++] = (char)cp;
        else if (cp < 0x800) { o[p++] = (char)(0xC0 | (cp >> 6)); o[p++] = (char)(0x80 | (cp & 63)); }
        else { o[p++] = (char)(0xE0 | (cp >> 12)); o[p++] = (char)(0x80 | ((cp >> 6) & 63)); o[p++] = (char)(0x80 | (cp & 63)); }
        ant = a;
    }
    if (ant & AT_B) o[p++] = '}';
    if (ant & AT_I) o[p++] = '/';
    if (ant & AT_MATH) o[p++] = ']';
    if (ant & AT_CODE) o[p++] = '`';
    o[p] = 0;
    return o;
}
#define FMT(src, esp) do { const char *_r = marcas(src); CK(!strcmp(_r, esp), "formato [%s]\n   da    [%s]\n   quiero[%s]", src, _r, esp); } while (0)

int main(void)
{
    FMT("**hola** mundo", "{hola} mundo");
    FMT("esto es *importante* y _esto_ también", "esto es /importante/ y /esto/ también");
    FMT("el archivo_de_prueba.txt y 2*3*4", "el archivo_de_prueba.txt y 2*3*4");
    FMT("usa `ls -l` en el terminal", "usa `ls -l` en el terminal");
    FMT("<u>subrayado</u> y <b>negrita</b>", "_subrayado_ y {negrita}");
    FMT("mira [la web](https://x.com) ya", "mira _la web_ ya");
    FMT("cuesta $5 y $10", "cuesta $5 y $10");
    FMT("la energía $E = mc^2$", "la energía [E = mc^2]");
    FMT("$\\frac{a}{b}$ y $\\frac{a+b}{2}$", "[a/b] y [(a+b)/2]");
    FMT("$\\sqrt{2} \\approx 1.41$", "[√2 ≈ 1.41]");
    FMT("$x_{i} \\le \\alpha$", "[x,i ≤ α]");
    FMT("\\(\\pi r^2\\)", "[πr^2]");
    FMT("$$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$$", "[∑,i,=,1^n i = (n(n+1))/2]");
    FMT("$\\mathbb{R}$ y $90^\\circ$", "[ℝ] y [90°]");
    FMT("# Título\ntexto", "{Título}\ntexto");
    FMT("- uno\n- dos", "• uno\n• dos");
    FMT("> una cita", "una cita");
    FMT("antes\n---\ndespués", "antes\n<HR>\ndespués");
    FMT("```\nint x = 1;\n```\nfin", "`int x = 1;`\nfin");
    FMT("\\*no es cursiva\\*", "*no es cursiva*");
    FMT("* **uno**: el primero", "• {uno}: el primero");
    FMT("**Nota:** guarda el /home/ana/mi_archivo_2.txt", "{Nota:} guarda el /home/ana/mi_archivo_2.txt");
    FMT("1. paso uno\n2. paso dos", "1. paso uno\n2. paso dos");
    FMT("## Resumen", "{Resumen}");
    FMT("Ajustes > LCM y 3 * 4 = 12", "Ajustes > LCM y 3 * 4 = 12");
    FMT("*cursiva con **negrita** dentro*", "/cursiva con {negrita} dentro/");
    FMT("$a \\cdot b \\neq 0$", "[a · b ≠ 0]");
    FMT("$x \\in \\mathbb{R}$, $f'(x) = 2x$", "[x ∈ ℝ], [f'(x) = 2x]");
    FMT("$\\gamma = \\frac{1}{\\sqrt{1 - v^2/c^2}}$", "[γ = 1/√(1 − v^2/c^2)]");
    FMT("$\\Delta t' = \\gamma \\Delta t$ y $\\pi r^2$ y $\\alpha \\le \\beta$", "[Δt' = γΔt] y [πr^2] y [α ≤ β]");
    FMT("$\\frac{\\pi}{2}$", "[π/2]");
    FMT("la velocidad es de 3 m/s^2 y punto", "la velocidad es de 3 m/s^2 y punto");
    /* maquetacion: un texto largo se parte en varias lineas sin pasarse del ancho */
    { const char *t = "Esta es una respuesta bastante larga del modelo de lenguaje, con muchas palabras, para comprobar que el orbe la parte en lineas que caben en la zona de texto y que ninguna se sale del ancho.";
      pr_reset(); pr_respuesta(t, (uint32_t)strlen(t), 0); pr_prepara(400);
      int ok = pr_nl >= 3; for (uint32_t l = 0; l < pr_nl; ++l) if (pr_l[l].ancho > 400) ok = 0;
      CK(ok, "maqueta: %u lineas", pr_nl); }
    /* lista con sangria colgante */
    { const char *t = "- un punto de la lista que es tan largo que tiene que partirse en dos o mas lineas para caber";
      pr_reset(); pr_respuesta(t, (uint32_t)strlen(t), 0); pr_prepara(300);
      CK(pr_nl >= 2 && pr_l[1].sangria > 0, "sangria de la lista: %d lineas, sangria %d", pr_nl, pr_nl > 1 ? pr_l[1].sangria : -1); }
    /* desplazamiento: sigue al texto, el usuario toma el control y vuelve a seguir al final */
    { static char t[6000]; t[0] = 0; for (int k = 0; k < 60; ++k) strcat(t, "Linea de texto numero tal con varias palabras para llenar.\n");
      pr_reset(); pr_respuesta(t, (uint32_t)strlen(t), 0); pr_vista = 300; pr_prepara(500);
      for (int k = 0; k < 2000 && pr_rev < pr_n; ++k) pr_avanza();
      for (int k = 0; k < 60; ++k) pr_avanza();
      int32_t max = (int32_t)pr_alto_total - pr_vista;
      CK(pr_auto && pr_scroll == max, "sigue al texto: scroll %d de %d", pr_scroll, max);
      her_pr_raton(200); for (int k = 0; k < 60; ++k) pr_avanza();
      CK(!pr_auto && pr_scroll < max && pr_scroll == max - 400, "el raton hacia arriba sube: %d", pr_scroll);
      her_pr_rueda(100); for (int k = 0; k < 60; ++k) pr_avanza();
      CK(pr_auto && pr_scroll == max, "al llegar al final vuelve a seguir: auto=%d %d", pr_auto, pr_scroll);
      her_pr_raton(-5000); for (int k = 0; k < 60; ++k) pr_avanza();
      CK(pr_scroll == max, "no se pasa del final: %d", pr_scroll);
      her_pr_raton(100000); for (int k = 0; k < 80; ++k) pr_avanza();
      CK(pr_scroll == 0 && !pr_auto, "ni del principio: %d", pr_scroll); }
    /* el razonamiento va detras, tenue */
    { pr_reset(); pr_respuesta("Respuesta.", 10, 0); pr_pensamiento("ls -l\nnotas.txt", 15); pr_prepara(500);
      int hay = 0; for (uint32_t i = 0; i < pr_n; ++i) if (pr_c[i].at & AT_THK) hay = 1;
      CK(hay && !(pr_c[0].at & AT_THK), "razonamiento detras"); }
    /* sin fuentes vectoriales tambien dibuja */
    { vec = 0; pr_reset(); pr_respuesta("hola **mundo**", 14, 0); pr_vista = 200; pr_prepara(500); pr_rev = pr_n; dibujados = 0; pr_dibuja_texto(0, 0, 500, 200); CK(dibujados >= 9, "mapa de bits: %d", dibujados); vec = 1; }
    /* fases */
    pr_nf = 0; gui_fase("soma"); gui_fase("lcm"); gui_fase("lcm"); gui_fase("mayordomo"); gui_fase("buscador"); gui_fase_orden("search gatos"); gui_fase("pensar"); gui_fase("escribir"); gui_fase("hecho"); gui_fase("orden");
    CK(pr_nf == 6 && pr_f[0] == FA_LCM && pr_f[1] == FA_MAYORDOMO && pr_f[2] == FA_BUSCAR && pr_f[3] == FA_PENSAR && pr_f[4] == FA_ESCRIBIR && pr_f[5] == FA_HECHO, "secuencia de fases (%d)", pr_nf);
    gui_fase("voz"); CK(pr_nf == 7 && pr_f[6] == FA_VOZ, "la voz va tras hecho");
    pr_nf = 0; gui_fase("documento"); gui_fase("investigador"); gui_fase("quien_sea");
    CK(pr_f[0] == FA_DOCUMENTO && pr_f[1] == FA_BUSCAR && pr_f[2] == FA_AGENTE, "agentes");
    niconos = 0; pr_nf = 0; gui_fase("lcm"); gui_fase("orden"); gui_fase("hecho"); pr_dibuja_barra(1280, 500);
    CK(niconos == 3 && iconos[0] == 99 && iconos[1] == SI_TERMINAL && iconos[2] == SI_CHECK, "iconos dibujados: %d", niconos);
    niconos = 0; pr_nf = 0; her_agent_busy = 1; pr_dibuja_barra(1280, 500); her_agent_busy = 0;
    CK(niconos == 1 && iconos[0] == SI_SPARK, "trabajando sin fase: pensando");
    /* turno completo: pregunta, pensamiento, escritura en trozos, final con etiquetas */
    { her_pr_turno(); her_agent_busy = 1;
      gui_fase("mayordomo"); her_pr_pensamiento("busco en la web");
      her_pr_stream_ini(); her_pr_stream("La **capital** de "); her_pr_stream("Francia es París.");
      pr_vista = 300; pr_prepara(600); for (int k = 0; k < 400; ++k) pr_avanza();
      uint32_t visto = pr_rev;
      her_agent_busy = 0; her_pr_fin("[Razonamiento]:\nbusco en la web\n\n[Dictamen]:\nLa **capital** de Francia es París.");
      pr_prepara(600);
      CK(visto > 10 && pr_rev == visto, "el final no repite el revelado (%u -> %u)", visto, pr_rev);
      CK(!strncmp(pr_src, "La **capital**", 14) && !strcmp(pr_thk, "busco en la web"), "respuesta y razonamiento separados");
      CK(pr_nf == 4 && pr_f[0] == FA_MAYORDOMO && pr_f[1] == FA_PENSAR && pr_f[2] == FA_ESCRIBIR && pr_f[3] == FA_HECHO, "fases del turno (%d)", pr_nf);
      her_pr_turno(); pr_prepara(600);
      CK(pr_n == 0 && pr_nf == 0 && pr_rev == 0, "pregunta nueva: limpio"); }
    /* sin etiquetas y vacio */
    { her_pr_turno(); her_pr_fin(""); CK(!strcmp(pr_src, "Mision ejecutada en hardware."), "respuesta vacia"); }
    /* dibujo completo: no se sale y devuelve 1 */
    { her_pr_turno(); static char t[3000]; t[0] = 0; for (int k = 0; k < 40; ++k) strcat(t, "Texto **largo** con $x^2$ y `codigo`. ");
      her_pr_fin(t); dibujados = 0; int r = her_pr_dibuja(1280, 800, 400);
      CK(r == 1 && dibujados > 0, "dibuja (%d, %d glifos)", r, dibujados);
      CK(her_pr_dibuja(200, 200, 100) == 0, "pantalla diminuta: lo de antes");
      uint32_t ha = her_ans_len; her_ans_len = 0; dibujados = 0; her_pr_dibuja(1280, 800, 400); CK(dibujados == 0, "texto deshecho: no se dibuja"); her_ans_len = ha; }
    /* textos enormes y basura: no se pasa de los limites */
    { static char t[40000]; for (int k = 0; k < 39999; ++k) { t[k] = "ab *_`$\\{}^#-\n[]()<>**__$$x"[k % 27]; } t[39999] = 0;
      her_pr_turno(); her_pr_fin(t); pr_vista = 400; pr_prepara(700); for (int k = 0; k < 3000; ++k) pr_avanza(); pr_dibuja_texto(0, 0, 700, 400);
      CK(pr_n <= PR_CEL_MAX && pr_nl <= PR_LIN_MAX && pr_src_n < PR_SRC_MAX, "limites: %u celdas, %u lineas", pr_n, pr_nl);
      for (int k = 0; k < 39999; ++k) { t[k] = (char)(0x80 + (k * 7) % 0x7F); } t[39999] = 0;
      her_pr_turno(); her_pr_fin(t); pr_prepara(700); CK(pr_n <= PR_CEL_MAX, "UTF-8 roto"); }
    /* las tres letras: la grande da mas lineas */
    { const char *t = "Una frase cualquiera que ocupa bastante y que sirve para ver como cambia el numero de lineas con la letra elegida en Ajustes.";
      uint32_t nl[3]; for (letra = 0; letra < 3; ++letra) { her_pr_turno(); her_pr_fin(t); pr_prepara(500); nl[letra] = pr_nl; }
      letra = 2;
      CK(nl[0] < nl[2] && nl[0] <= nl[1], "letra pequena %u, mediana %u, grande %u lineas", nl[0], nl[1], nl[2]); }
    printf("  %s texto del orbe: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
