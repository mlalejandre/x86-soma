/* SOMA_283_MEMORIA_APP: la aplicacion de Memoria, rehecha para la memoria nueva (277-280) con el aspecto de la consola de memorIA.
 *   Monitor     estado en vivo: hechos, ramas, cola de extraccion, pipeline de las 9 tareas del sueno, registro de mantenimiento, servidor.
 *   Explorador  arbol de temas + recuerdos de cada rama: ver, editar, cambiar de tema, archivar, restaurar, borrar, copiar/mover a otro cartucho;
 *               crear, renombrar, describir y borrar ramas.
 *   Buscar      por significado (embeddings) en el primario y en los cartuchos conectados; si el servidor no contesta, por palabras.
 *   Cartuchos   crear, conectar (solo lectura), hacer primario, clonar, exportar, importar .smem, vaciar, borrar.
 *   Ajustes     memoria, extraccion, sueno, cuantos recuerdos entran en el prompt, servidor de embeddings.
 *   Archivos    los archivos de los agentes de siempre (perfil, notas...), que se abren en Notas.
 * Leer y editar es inmediato (cerrojo de la memoria, poco rato); lo que tarda lo hace el hilo de la memoria (memoria_gui_pide). */
#include "memview.h"
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"
#include "textfield.h"
#include "clipboard.h"
#include "idt.h"
#include "memoria.h"
#include "memoria_core.h"
#include "mem.h"

/* ---------------------------------------------------------------- paleta (la de memorIA) */
#define C_BG     0x00070913u
#define C_BG2    0x00151B3Du
#define C_CARD   0x00101426u
#define C_CARD2  0x00151A33u
#define C_INP    0x000A0D1Au
#define C_BD     0x00212845u
#define C_BDL    0x002F3A63u
#define C_BLUE   0x004F46E5u
#define C_CYAN   0x0006B6D4u
#define C_GREEN  0x0010B981u
#define C_AMBER  0x00F59E0Bu
#define C_RED    0x00EF4444u
#define C_PURPLE 0x00A855F7u
#define C_TX     0x00F9FAFBu
#define C_MU     0x008B949Eu
#define C_FAINT  0x00525A6Eu

#define HDR_H  64
#define TAB_H  46
#define FOOT_H 28

enum { T_MON, T_EXP, T_BUS, T_CAR, T_AJU, T_ARC, T_N };
static const char *const TAB_NOM[T_N] = { "Monitor", "Explorador", "Buscar", "Cartuchos", "Ajustes", "Archivos" };
static const char *const TAREA_NOM[9] = { "Repair", "Embed", "Decay", "Ordena", "Describe", "Dedupe", "Reeval", "Split", "Merge" };
static const char *const TAREA_SUB[9] = { "Estructura", "Vectores", "Olvido", "Clasificar", "S\303\255ntesis", "Fusi\303\263n", "Importancia", "Divisi\303\263n", "Ramas" };
static const char *const TAREA_DESC[9] = {
    "Revisa contadores, poda ramas vacias y deshace sustituciones rotas.",
    "Calcula los vectores que faltan con el servidor de embeddings.",
    "Archiva lo que la curva del olvido deja por debajo de 0,5.",
    "Coloca en el \303\241rbol hasta 8 hechos sin tema (pregunta al LLM).",
    "Escribe la descripci\303\263n de 2 ramas pendientes (LLM).",
    "Busca dos hechos redundantes del mismo tema y decide si fusionarlos (LLM).",
    "Rescata hechos a punto de olvidarse si merecen la pena (LLM).",
    "Parte en dos una rama con mas de 12 hechos (LLM).",
    "Une ramas hoja casi vacias y muy parecidas (LLM)." };
static const uint32_t TAREA_COL[9] = { C_MU, C_CYAN, C_AMBER, C_BLUE, C_PURPLE, C_GREEN, C_AMBER, C_RED, C_CYAN };

/* ---------------------------------------------------------------- estado */
static int g_tab, g_inited;
static uint32_t g_vver = 1, g_mem_ver, g_rs_vista;
static uint64_t g_t_poll, g_t_pulso, g_t_seg;
static struct mem_vista g_v;
static char g_msg[200]; static int g_msg_err;
static int g_rx = -1, g_ry = -1, g_hover = -1;
static int g_wx, g_wy, g_ww, g_wh;
static char g_sel_path[96];

/* explorador */
enum { V_NODO, V_SIN, V_ARCH, V_SUST, V_EPI };
static int g_vista = V_SIN; static uint32_t g_nodo;
static uint8_t g_cerrado[MC_NODOS_MAX + 1];
static int g_tscroll, g_cscroll, g_rscroll, g_kscroll, g_ascroll;
static uint32_t *g_lista; static uint32_t g_nlista;               /* se reserva al abrir el Explorador: no engorda la imagen del nucleo */
static uint32_t g_clave_lista = 0xFFFFFFFFu;
struct fila { uint8_t nodo, nivel, hijos, sucio; uint16_t cuenta; uint32_t rama; char nombre[MC_NODO_NOMBRE]; };
static struct fila g_filas[MC_NODOS_MAX + 1]; static int g_nfilas;
static uint32_t g_cnt_sin, g_cnt_arch, g_cnt_sust, g_cnt_epi;

/* campos de texto */
static char b_busca[200], b_desc[MC_NODO_DESC], b_edit[MC_TEXTO_MAX + 1], b_ruta[150], b_nom[100], b_nom2[100], b_url[80], b_mod[40];
static struct tfield f_busca, f_desc, f_edit, f_ruta, f_nom, f_nom2, f_url, f_mod;
static struct tfield *g_foco;
static uint32_t g_desc_nodo;                       /* nodo cuya descripcion esta en f_desc */

/* ventanas encima */
enum { M_NO, M_EDIT, M_CONF, M_IN, M_PICK };
static int g_modal;
static uint32_t g_ed_id, g_ed_gen; static int g_ed_imp, g_ed_kind, g_ed_estado, g_ed_rol; static uint32_t g_ed_t, g_ed_sust, g_ed_dim, g_ed_ref; static char g_ed_fuente[MC_FUENTE_MAX + 1];
enum { CF_BORRA_CEL, CF_BORRA_RAMA_SOLA, CF_BORRA_RAMA, CF_C_VACIA, CF_C_BORRA };
static int g_conf, g_conf_ed; static uint32_t g_conf_a; static char g_conf_txt[200];
enum { IN_C_NUEVO, IN_C_CLONA, IN_C_EXPORTA, IN_IMPORTA, IN_NODO_NUEVO, IN_NODO_REN };
static int g_in; static uint32_t g_in_a; static int g_in_vec = 1; static char g_in_tit[80];
enum { PK_COPIA_CEL, PK_MUEVE_CEL, PK_COPIA_RAMA };
static int g_pick; static uint32_t g_pick_a;

/* resultados y cartuchos (copias locales) */
static struct mem_res g_rs[24]; static int g_nrs;
static struct mem_cart g_cs[8]; static int g_ncs;

/* ---------------------------------------------------------------- utilidades */
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy(char *d, const char *s, int cap) { int i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void scat(char *d, const char *s, int cap) { int i = slen(d), j = 0; while (s && s[j] && i + 1 < cap) d[i++] = s[j++]; d[i] = 0; }
static void snum(char *d, int cap, uint32_t v)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; scat(d, r, cap);
}
static void smil(char *d, int cap, uint32_t v)        /* 12.345 */
{
    char t[16] = ""; snum(t, sizeof t, v); int n = slen(t), k = 0; char r[20];
    for (int i = 0; i < n; i++) { r[k++] = t[i]; if ((n - 1 - i) % 3 == 0 && i != n - 1) r[k++] = '.'; }
    r[k] = 0; scat(d, r, cap);
}
static void sbytes(char *d, int cap, uint32_t b)
{
    if (b >= (1u << 20)) { snum(d, cap, b >> 20); scat(d, ",", cap); snum(d, cap, ((b >> 10) & 1023) * 10 / 1024); scat(d, " MB", cap); }
    else { snum(d, cap, (b + 1023) >> 10); scat(d, " KB", cap); }
}
static int eqs(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint64_t ms(void) { return timer_get_uptime_ms(); }
static void cambia(void) { g_vver++; }
static void aviso(const char *t, int err) { scpy(g_msg, t, sizeof g_msg); g_msg_err = err; cambia(); }

/* ---------------------------------------------------------------- texto */
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int twn(const char *s, int n, int px, int fl) { return has_wf() ? wf_width(s, (uint32_t)n, px, fl) : n * 8; }
static int tw(const char *s, int px, int fl) { return twn(s, slen(s), px, fl); }
static void txn(int x, int top, int h, const char *s, int n, int px, uint32_t col, int fl)
{
    if (n <= 0) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, fl, 255); return; }
    for (int i = 0; i < n && s[i]; i++) { char c[2] = { s[i], 0 }; font_draw_string((uint32_t)(x + i * 8), (uint32_t)(top + (h - 10) / 2), c, col, 1); }
}
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int fl) { txn(x, top, h, s, slen(s), px, col, fl); }
static void txr(int right, int top, int h, const char *s, int px, uint32_t col, int fl) { tx(right - tw(s, px, fl), top, h, s, px, col, fl); }
static void txc(int x, int w, int top, int h, const char *s, int px, uint32_t col, int fl) { tx(x + (w - tw(s, px, fl)) / 2, top, h, s, px, col, fl); }
static int utf8_atras(const char *s, int n) { while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; return n; }
/* una linea recortada con "..." si no cabe en maxw */
static void txe(int x, int top, int h, const char *s, int px, uint32_t col, int fl, int maxw)
{
    int n = slen(s);
    if (maxw <= 0) return;
    if (tw(s, px, fl) <= maxw) { txn(x, top, h, s, n, px, col, fl); return; }
    int pw = tw("...", px, fl), lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (twn(s, mid, px, fl) + pw <= maxw) lo = mid; else hi = mid - 1; }
    lo = utf8_atras(s, lo);
    txn(x, top, h, s, lo, px, col, fl); tx(x + twn(s, lo, px, fl), top, h, "...", px, col, fl);
}
/* parrafo con ajuste de linea; maxl lineas (la ultima con "..." si sobra). Devuelve las lineas usadas */
static int parrafo(int x, int y, int w, const char *s, int px, uint32_t col, int fl, int lh, int maxl)
{
    int n = slen(s), i = 0, l = 0;
    while (i < n && l < maxl) {
        while (i < n && s[i] == ' ') i++;
        int fin = i, ult = -1;
        while (fin < n && s[fin] != '\n') {
            int k = fin + 1; while (k < n && ((uint8_t)s[k] & 0xC0) == 0x80) k++;
            if (twn(s + i, k - i, px, fl) > w) break;
            if (s[fin] == ' ') ult = fin;
            fin = k;
        }
        int corte = fin;
        if (fin < n && s[fin] != '\n' && ult > i) corte = ult;
        if (corte == i && fin < n && s[fin] != '\n') { corte = fin > i ? fin : i + 1; }
        int ultima = l == maxl - 1 && corte < n;
        if (ultima) {
            char t[400]; int m = n - i < 399 ? n - i : 399; for (int k = 0; k < m; k++) t[k] = s[i + k] == '\n' ? ' ' : s[i + k]; t[m] = 0;
            txe(x, y + l * lh, lh, t, px, col, fl, w);
        } else txn(x, y + l * lh, lh, s + i, corte - i, px, col, fl);
        l++; i = corte; if (i < n && s[i] == '\n') i++;
    }
    return l;
}

/* ---------------------------------------------------------------- formas */
static void rr(int x, int y, int w, int h, int r, uint32_t rgb) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | rgb); }
static void rra(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_fill_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void rs(int x, int y, int w, int h, int r, uint32_t rgb, uint32_t a) { if (w > 0 && h > 0) fb_stroke_rrect_aa(x, y, w, h, r, (a << 24) | rgb); }
static void linea(int x0, int y0, int x1, int y1, uint32_t col, int grosor16) { fb_line_aa(x0 * 16, y0 * 16, x1 * 16, y1 * 16, grosor16, 0xFF000000u | col); }
static void panel(int x, int y, int w, int h) { rr(x, y, w, h, 12, C_CARD); rs(x, y, w, h, 12, C_BD, 255); }
static uint32_t mezcla(uint32_t a, uint32_t b, int k)      /* k/255 de b sobre a */
{
    uint32_t r = (((a >> 16) & 255) * (255 - (uint32_t)k) + ((b >> 16) & 255) * (uint32_t)k) / 255, g = (((a >> 8) & 255) * (255 - (uint32_t)k) + ((b >> 8) & 255) * (uint32_t)k) / 255,
             bl = ((a & 255) * (255 - (uint32_t)k) + (b & 255) * (uint32_t)k) / 255;
    return r << 16 | g << 8 | bl;
}
static int pulso(void) { uint32_t t = (uint32_t)(ms() / 100) % 16; return (int)(t < 8 ? 110 + t * 18 : 110 + (16 - t) * 18); }
static void punto(int cx, int cy, int r, uint32_t col, uint32_t a) { fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, (a << 24) | col); }
/* insignia redondeada: devuelve el ancho */
static int badge_w(const char *s, int dot) { return tw(s, 11, WF_BOLD) + 18 + (dot ? 12 : 0); }
static int badge(int x, int y, const char *s, uint32_t col, int dot)
{
    int w = badge_w(s, dot);
    rra(x, y, w, 20, 10, col, 0x26); rs(x, y, w, 20, 10, col, 0x70);
    int tx0 = x + 9;
    if (dot) { punto(x + 12, y + 10, 3, col, dot > 1 ? (uint32_t)pulso() : 255); tx0 += 12; }
    tx(tx0, y, 20, s, 11, col, WF_BOLD);
    return w;
}
static int chip(int x, int y, const char *s, uint32_t col)   /* etiqueta gris con numero */
{
    int w = tw(s, 11, 0) + 16;
    rr(x, y, w, 20, 6, C_INP); rs(x, y, w, 20, 6, C_BD, 255);
    tx(x + 8, y, 20, s, 11, col, 0);
    return w;
}

/* ---------------------------------------------------------------- zonas de clic */
enum { A_NADA, A_TAB, A_PIDE, A_FOCO, A_VISTA, A_NODO, A_PLIEGA, A_CEL, A_CEL_BORRA, A_NUEVO_REC, A_NODO_SUB, A_NODO_REN, A_NODO_BORRA, A_NODO_COPIA, A_NODO_DESC,
       A_RAIZ, A_ED_GUARDA, A_ED_CIERRA, A_ED_IMP, A_ED_ARCH, A_ED_BORRA, A_ED_COPIA, A_ED_MUEVE, A_CONF_SI, A_CONF_NO, A_IN_OK, A_IN_NO, A_IN_VEC,
       A_PICK, A_PICK_NO, A_C_NUEVO, A_C_IMPORTA, A_C_PRIM, A_C_MONTA, A_C_DESMONTA, A_C_CLONA, A_C_EXPORTA, A_C_VACIA, A_C_BORRA, A_CFG, A_CFG_PASO, A_CFG_SRV,
       A_BUSCA, A_RES, A_ARCH, A_BORRA_RAMA_TODO };
struct hit { int x, y, w, h, act; int32_t a, b; struct tfield *f; };
#define NHIT 320
static struct hit H[NHIT]; static int nH;
static void hit(int x, int y, int w, int h, int act, int32_t a, int32_t b) { if (nH < NHIT && w > 0 && h > 0) { struct hit *z = &H[nH++]; z->x = x; z->y = y; z->w = w; z->h = h; z->act = act; z->a = a; z->b = b; z->f = 0; } }
static void hit_f(int x, int y, int w, int h, struct tfield *f) { if (nH < NHIT) { hit(x, y, w, h, A_FOCO, 0, 0); H[nH - 1].f = f; } }
static int hit_en(int mx, int my) { for (int i = nH - 1; i >= 0; i--) if (mx >= H[i].x && my >= H[i].y && mx < H[i].x + H[i].w && my < H[i].y + H[i].h) return i; return -1; }
static int sobre(int x, int y, int w, int h) { return g_rx >= x && g_ry >= y && g_rx < x + w && g_ry < y + h; }

/* estilos: 0 normal, 1 tenido del color, 2 solido, 3 desactivado */
static void boton(int x, int y, int w, int h, const char *s, int estilo, uint32_t col, int act, int32_t a, int32_t b)
{
    int ov = estilo != 3 && sobre(x, y, w, h);
    if (estilo == 2) { rr(x, y, w, h, 7, ov ? mezcla(col, 0xFFFFFF, 40) : col); txc(x, w, y, h, s, 12, 0xFFFFFF, WF_BOLD); }
    else if (estilo == 1) { rra(x, y, w, h, 7, col, ov ? 0x40 : 0x24); rs(x, y, w, h, 7, col, ov ? 255 : 0xA0); txc(x, w, y, h, s, 12, col, WF_BOLD); }
    else if (estilo == 3) { rr(x, y, w, h, 7, C_CARD2); rs(x, y, w, h, 7, C_BD, 255); txc(x, w, y, h, s, 12, C_FAINT, WF_BOLD); return; }
    else { rr(x, y, w, h, 7, C_CARD2); rs(x, y, w, h, 7, ov ? C_CYAN : C_BD, 255); txc(x, w, y, h, s, 12, ov ? C_CYAN : C_TX, WF_BOLD); }
    hit(x, y, w, h, act, a, b);
}
static void boton_ic(int x, int y, int s, int ic, uint32_t col, int act, int32_t a, int32_t b)   /* boton cuadrado con un icono dibujado */
{
    int ov = sobre(x, y, s, s);
    rra(x, y, s, s, 7, col, ov ? 0x40 : 0x1C); rs(x, y, s, s, 7, col, ov ? 255 : 0x90);
    int cx = x + s / 2, cy = y + s / 2;
    if (ic == 0) { linea(cx - 4, cy + 4, cx + 3, cy - 3, col, 30); linea(cx + 2, cy - 4, cx + 4, cy - 2, col, 30); punto(cx - 4, cy + 4, 1, col, 255); }   /* lapiz */
    else { linea(cx - 4, cy - 4, cx + 4, cy + 4, col, 30); linea(cx - 4, cy + 4, cx + 4, cy - 4, col, 30); }                                          /* aspa */
    hit(x, y, s, s, act, a, b);
}
static int boton_w(const char *s) { return tw(s, 12, WF_BOLD) + 24; }
/* boton que crece hacia la izquierda desde right; devuelve su x */
static int boton_d(int right, int y, int h, const char *s, int estilo, uint32_t col, int act, int32_t a, int32_t b)
{
    int w = boton_w(s); boton(right - w, y, w, h, s, estilo, col, act, a, b); return right - w;
}
static void campo_tf(struct tfield *f, int x, int y, int w, int h, const char *ph)
{
    tf_render(f, x, y, w, h, g_foco == f, ph, C_TX);
    hit_f(x, y, w, h, f);
}

/* ---------------------------------------------------------------- iconos dibujados (sin emojis: la fuente no los tiene) */
static void icono(int k, int cx, int cy, uint32_t c)
{
    switch (k) {
    case 0: punto(cx - 3, cy + 3, 3, c, 255); linea(cx - 1, cy + 1, cx + 5, cy - 5, c, 32); break;                                      /* repair: llave */
    case 1: rs(cx - 6, cy - 6, 12, 12, 6, c, 255); punto(cx, cy, 2, c, 255); break;                                                    /* embed: diana */
    case 2: linea(cx - 5, cy + 5, cx + 5, cy - 5, c, 24); rs(cx - 5, cy - 6, 11, 11, 7, c, 200); break;                                /* decay: hoja */
    case 3: for (int i = -1; i <= 1; i++) { punto(cx - 5, cy + i * 4, 1, c, 255); linea(cx - 2, cy + i * 4, cx + 6, cy + i * 4, c, 24); } break;   /* ordena: lista */
    case 4: linea(cx - 5, cy + 5, cx + 4, cy - 4, c, 36); punto(cx - 5, cy + 5, 1, c, 255); break;                                     /* describe: lapiz */
    case 5: rs(cx - 7, cy - 4, 9, 9, 5, c, 255); rs(cx - 2, cy - 4, 9, 9, 5, c, 255); break;                                           /* dedupe */
    case 6: linea(cx - 6, cy - 3, cx + 6, cy - 3, c, 24); linea(cx, cy - 6, cx, cy + 5, c, 24); linea(cx - 4, cy + 5, cx + 4, cy + 5, c, 24); break;   /* balanza */
    case 7: linea(cx - 5, cy - 5, cx + 5, cy + 5, c, 24); linea(cx - 5, cy + 5, cx + 5, cy - 5, c, 24); punto(cx - 5, cy + 5, 2, c, 255); punto(cx - 5, cy - 5, 2, c, 255); break;   /* tijera */
    case 8: linea(cx - 5, cy - 5, cx, cy, c, 24); linea(cx + 5, cy - 5, cx, cy, c, 24); linea(cx, cy, cx, cy + 6, c, 24); break;      /* merge */
    case 10: linea(cx - 7, cy, cx - 3, cy, c, 24); linea(cx - 3, cy, cx - 1, cy - 5, c, 24); linea(cx - 1, cy - 5, cx + 2, cy + 5, c, 24); linea(cx + 2, cy + 5, cx + 4, cy, c, 24); linea(cx + 4, cy, cx + 7, cy, c, 24); break;  /* monitor */
    case 11: punto(cx - 5, cy - 5, 2, c, 255); linea(cx - 5, cy - 3, cx - 5, cy + 5, c, 20); linea(cx - 5, cy, cx + 2, cy, c, 20); linea(cx - 5, cy + 5, cx + 2, cy + 5, c, 20); punto(cx + 4, cy, 2, c, 255); punto(cx + 4, cy + 5, 2, c, 255); break;  /* arbol */
    case 12: rs(cx - 6, cy - 6, 10, 10, 5, c, 255); linea(cx + 2, cy + 2, cx + 6, cy + 6, c, 32); break;                               /* lupa */
    case 13: rs(cx - 6, cy - 6, 12, 13, 2, c, 255); fb_fill_rect((uint32_t)(cx - 3), (uint32_t)(cy - 6), 6, 4, 0x00000000u | c); linea(cx - 3, cy + 3, cx + 3, cy + 3, c, 20); break;   /* cartucho */
    case 14: rs(cx - 5, cy - 5, 10, 10, 5, c, 255); for (int i = 0; i < 4; i++) { int dx = i == 0 ? 7 : i == 1 ? -7 : 0, dy = i == 2 ? 7 : i == 3 ? -7 : 0; punto(cx + dx, cy + dy, 1, c, 255); } break;   /* ajustes */
    case 15: rs(cx - 5, cy - 6, 10, 13, 2, c, 255); linea(cx - 2, cy - 2, cx + 2, cy - 2, c, 16); linea(cx - 2, cy + 1, cx + 2, cy + 1, c, 16); break;   /* archivo */
    default: punto(cx, cy, 3, c, 255); break;
    }
}
static void logo(int x, int y, int s)
{
    int r = 9;
    for (int i = 0; i < s; i++) {                                   /* degradado azul -> cian con esquinas redondas */
        int ins = 0;
        int d = i < r ? r - i : i >= s - r ? i - (s - r - 1) : 0;
        if (d) { int q = r * r - d * d; int sq = 0; while ((sq + 1) * (sq + 1) <= q) sq++; ins = r - sq; }
        fb_fill_rect((uint32_t)(x + i), (uint32_t)(y + ins), 1, (uint32_t)(s - 2 * ins), mezcla(C_BLUE, C_CYAN, i * 255 / s));
    }
    int cx = x + s / 2, cy = y + s / 2;                             /* una pequena red: tres neuronas unidas */
    linea(cx - 7, cy + 5, cx, cy - 7, 0xFFFFFF, 20); linea(cx, cy - 7, cx + 7, cy + 5, 0xFFFFFF, 20); linea(cx - 7, cy + 5, cx + 7, cy + 5, 0xFFFFFF, 20);
    punto(cx - 7, cy + 5, 3, 0xFFFFFF, 255); punto(cx, cy - 7, 3, 0xFFFFFF, 255); punto(cx + 7, cy + 5, 3, 0xFFFFFF, 255);
}

/* ---------------------------------------------------------------- datos */
static void campos_ini(void)
{
    static int hecho;
    if (hecho) return;
    hecho = 1;
    tf_init(&f_busca, b_busca, sizeof b_busca, 0); tf_init(&f_desc, b_desc, sizeof b_desc, 0); tf_init(&f_edit, b_edit, sizeof b_edit, 1);
    tf_init(&f_ruta, b_ruta, sizeof b_ruta, 0); tf_init(&f_nom, b_nom, sizeof b_nom, 0); tf_init(&f_nom2, b_nom2, sizeof b_nom2, 0);
    tf_init(&f_url, b_url, sizeof b_url, 0); tf_init(&f_mod, b_mod, sizeof b_mod, 0);
}
void memview_init(void)
{
    campos_ini();
    if (!g_inited) { g_inited = 1; g_tab = T_MON; g_t_poll = 0; }
    memoria_vista(&g_v);
    if (!b_url[0]) tf_set(&f_url, g_v.url);
    if (!b_mod[0]) tf_set(&f_mod, g_v.modelo);
    g_mem_ver = memoria_version();
    cambia();
}
uint32_t memview_version(void) { return g_vver; }
int memview_tick(void)
{
    uint64_t t = ms();
    if (t - g_t_poll < 250) return 0;
    g_t_poll = t;
    int r = 0;
    uint32_t v = memoria_version();
    if (v != g_mem_ver) { g_mem_ver = v; r = 1; }
    if ((g_v.dormido || g_v.extrayendo || g_v.ops_ocupado || g_v.agentes) && t - g_t_pulso >= 300) { g_t_pulso = t; r = 1; }   /* los puntos que laten */
    if (g_tab == T_MON && t - g_t_seg >= 1000) { g_t_seg = t; r = 1; }                                                       /* cuentas atras */
    if (r) cambia();
    return r;
}
static void pide(int op, uint32_t a, uint32_t b, const char *s1, const char *s2)
{
    struct mem_pet p; uint8_t *z = (uint8_t *)&p; for (uint32_t i = 0; i < sizeof p; i++) z[i] = 0;
    p.op = op; p.a = a; p.b = b; p.gen = memoria_gui_gen();
    if (s1) scpy(p.s1, s1, sizeof p.s1);
    if (s2) scpy(p.s2, s2, sizeof p.s2);
    if (!memoria_gui_pide(&p)) aviso("Espera: la memoria est\303\241 terminando otra operacion.", 1);
    else { g_msg[0] = 0; cambia(); }
}
static void pide_ids(int op, uint32_t a, const uint32_t *ids, uint32_t n)
{
    struct mem_pet p; uint8_t *z = (uint8_t *)&p; for (uint32_t i = 0; i < sizeof p; i++) z[i] = 0;
    p.op = op; p.a = a; p.gen = g_ed_gen ? g_ed_gen : memoria_gui_gen();
    for (uint32_t i = 0; i < n && i < 64; i++) p.ids[p.nids++] = ids[i];
    if (!memoria_gui_pide(&p)) aviso("Espera: la memoria est\303\241 terminando otra operacion.", 1);
    else { g_msg[0] = 0; cambia(); }
}

/* arbol del explorador (con el cerrojo tomado) */
static void arbol_filas(const struct mc *m)
{
    g_nfilas = 0;
    uint32_t st[MC_NODOS_MAX + 1], sp = 0;
    for (uint32_t r = MC_NODOS_MAX; r >= 1; r--) if (m->nodo[r].usado && !m->nodo[r].padre) st[sp++] = r;
    /* orden alfabetico aproximado de raices: el de creacion basta; la pila invierte, por eso se recorre al reves */
    while (sp && g_nfilas <= (int)MC_NODOS_MAX) {
        uint32_t id = st[--sp]; const struct mc_nodo *x = &m->nodo[id];
        struct fila *f = &g_filas[g_nfilas++];
        f->nodo = (uint8_t)id; f->nivel = x->nivel; f->cuenta = x->cuenta; f->sucio = x->sucio; f->hijos = (uint8_t)(mc_nodo_hijos(m, id) > 255 ? 255 : mc_nodo_hijos(m, id));
        scpy(f->nombre, x->nombre, sizeof f->nombre);
        f->rama = 0; for (uint32_t k = 1; k <= MC_NODOS_MAX; k++) if (m->nodo[k].usado && mc_nodo_dentro(m, k, id)) f->rama += m->nodo[k].cuenta;
        if (g_cerrado[id]) continue;
        for (uint32_t c = MC_NODOS_MAX; c >= 1; c--) if (m->nodo[c].usado && m->nodo[c].padre == id && sp < MC_NODOS_MAX) st[sp++] = c;
    }
}
static uint32_t rama_cuenta(const struct mc *m, uint32_t nodo)
{
    uint32_t n = 0;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado && mc_nodo_dentro(m, i, nodo)) n += m->nodo[i].cuenta;
    return n;
}
/* la lista de recuerdos de la vista actual (con el cerrojo tomado); se rehace solo si algo cambio */
static void lista_rehace(const struct mc *m)
{
    uint32_t clave = (uint32_t)g_vista * 7919u ^ g_nodo * 104729u ^ g_mem_ver * 31u ^ memoria_gui_gen() * 131u;
    if (clave == g_clave_lista) return;
    g_nlista = 0; g_cnt_sin = g_cnt_arch = g_cnt_sust = g_cnt_epi = 0;
    if (!g_lista) g_lista = (uint32_t *)kmalloc(MC_MAX_ITEMS * sizeof(uint32_t));
    if (!g_lista) return;
    g_clave_lista = clave;
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *e = &m->it[i]; int sirve = 0;
        if (e->kind == MC_EPISODIO) { g_cnt_epi++; sirve = g_vista == V_EPI; }
        else if (e->estado == MC_ARCHIVADO) { g_cnt_arch++; sirve = g_vista == V_ARCH; }
        else if (e->estado == MC_SUSTITUIDO) { g_cnt_sust++; sirve = g_vista == V_SUST; }
        else if (!e->nodo || !m->nodo[e->nodo].usado) { g_cnt_sin++; sirve = g_vista == V_SIN; }
        else sirve = g_vista == V_NODO && g_nodo && mc_nodo_dentro(m, e->nodo, g_nodo);
        if (sirve && g_nlista < MC_MAX_ITEMS) g_lista[g_nlista++] = e->id;
    }
    /* orden: los mensajes, los mas recientes primero; los hechos, por importancia y luego recientes (ordenacion de Shell) */
    uint32_t n = g_nlista;
    for (uint32_t gap = n / 2; gap > 0; gap /= 2)
        for (uint32_t i = gap; i < n; i++) {
            uint32_t v = g_lista[i]; const struct mc_item *a = &m->it[v - 1]; uint32_t j = i;
            while (j >= gap) {
                const struct mc_item *b = &m->it[g_lista[j - gap] - 1];
                int antes = g_vista == V_EPI ? a->t > b->t : (a->imp > b->imp || (a->imp == b->imp && a->t > b->t));
                if (!antes) break;
                g_lista[j] = g_lista[j - gap]; j -= gap;
            }
            g_lista[j] = v;
        }
}

/* ---------------------------------------------------------------- cabecera, pestanas y pie */
static void fondo(int x, int y, int w, int h)
{
    int gh = h < 320 ? h : 320;
    for (int j = 0; j < gh; j += 2) fb_fill_rect((uint32_t)x, (uint32_t)(y + j), (uint32_t)w, 2, mezcla(C_BG2, C_BG, j * 255 / gh));
    if (h > gh) fb_fill_rect((uint32_t)x, (uint32_t)(y + gh), (uint32_t)w, (uint32_t)(h - gh), C_BG);
}
static void cabecera(int x, int y, int w)
{
    fb_fill_rect_alpha(x, y, w, HDR_H, 0xD8000000u | C_CARD);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + HDR_H - 1), (uint32_t)w, 1, C_BD);
    logo(x + 16, y + 14, 36);
    tx(x + 62, y + 12, 22, "MEMORIA DE SOMA", 15, C_TX, WF_BOLD);
    tx(x + 62 + tw("MEMORIA DE SOMA", 15, WF_BOLD) + 8, y + 12, 22, "// CONSOLA COGNITIVA", 12, C_MU, WF_BOLD);
    char sub[160] = "";
    scat(sub, g_v.modelo, sizeof sub); scat(sub, " \302\267 ", sizeof sub); scat(sub, g_v.srv ? g_v.url : "sin servidor de embeddings", sizeof sub);
    scat(sub, " \302\267 tick #", sizeof sub); snum(sub, sizeof sub, g_v.tick);
    txe(x + 62, y + 34, 18, sub, 11, C_MU, WF_MONO, w / 2 - 70);
    /* a la derecha: cartucho, estado y servidor */
    int bx = x + w - 16;
    char ct[80] = "Cartuchos: "; scat(ct, g_v.cartucho, sizeof ct); if (g_v.nmont) { scat(ct, " +", sizeof ct); snum(ct, sizeof ct, (uint32_t)g_v.nmont); }
    int cw = boton_w(ct); if (cw > 260) cw = 260;
    boton(bx - cw, y + 18, cw, 28, ct, 1, C_AMBER, A_TAB, T_CAR, 0);
    bx -= cw + 10;
    const char *st; uint32_t sc; int dot = 2;
    if (!g_v.activa) { st = "MEMORIA DESACTIVADA"; sc = C_RED; dot = 1; }
    else if (g_v.agentes) { st = "ATENDIENDO AL AGENTE"; sc = C_AMBER; }
    else if (g_v.dormido) { st = "SO\303\221ANDO"; sc = C_PURPLE; }
    else if (g_v.extrayendo) { st = "EXTRAYENDO HECHOS"; sc = C_CYAN; }
    else if (g_v.sueno) { st = "HOMEOSTASIS ACTIVA"; sc = C_CYAN; dot = 1; }
    else { st = "SUE\303\221O APAGADO"; sc = C_MU; dot = 1; }
    int w1 = badge_w(st, 1); if (bx - w1 > x + w / 2) { badge(bx - w1, y + 22, st, sc, dot); bx -= w1 + 8; }
    const char *sv; uint32_t svc;
    if (!g_v.srv) { sv = "Sin embeddings"; svc = C_MU; }
    else if (g_v.emb_caido) { sv = "Embeddings caidos"; svc = C_RED; }
    else if (g_v.srv_dim) { sv = "Embeddings listos"; svc = C_GREEN; }
    else { sv = "Embeddings sin verificar"; svc = C_AMBER; }
    int w2 = badge_w(sv, 1); if (bx - w2 > x + w / 2 - 40) badge(bx - w2, y + 22, sv, svc, 1);
}
static void pestanas(int x, int y, int w)
{
    int px = x + 16;
    for (int t = 0; t < T_N; t++) {
        int on = t == g_tab, bw = tw(TAB_NOM[t], 12, WF_BOLD) + 44;
        if (px + bw > x + w - 10) break;
        int ov = sobre(px, y + 8, bw, 30);
        if (on) { rr(px, y + 8, bw, 30, 15, C_BLUE); }
        else { rra(px, y + 8, bw, 30, 15, 0xFFFFFF, ov ? 0x12 : 0x06); rs(px, y + 8, bw, 30, 15, ov ? C_BDL : C_BD, 255); }
        icono(10 + t, px + 17, y + 23, on ? 0xFFFFFF : ov ? C_CYAN : C_MU);
        tx(px + 30, y + 8, 30, TAB_NOM[t], 12, on ? 0xFFFFFF : ov ? C_TX : C_MU, WF_BOLD);
        hit(px, y + 8, bw, 30, A_TAB, t, 0);
        px += bw + 8;
    }
}
static void pie(int x, int y, int w)
{
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, FOOT_H, C_CARD);
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 1, C_BD);
    const char *m = g_msg[0] ? g_msg : g_v.ops_msg;
    int mx = x + 14;
    if (g_v.ops_ocupado) {                                           /* rueda girando */
        int f = (int)(ms() / 120 % 8);
        for (int i = 0; i < 8; i++) { int dx[8] = { 0, 4, 6, 4, 0, -4, -6, -4 }, dy[8] = { -6, -4, 0, 4, 6, 4, 0, -4 }; punto(mx + 6 + dx[i], y + 14 + dy[i], 1, C_CYAN, (uint32_t)(i == f ? 255 : 70)); }
        mx += 20;
    }
    char r[160] = ""; smil(r, sizeof r, g_v.total); scat(r, " elementos \302\267 ", sizeof r); sbytes(r, sizeof r, g_v.bytes); scat(r, " en RAM \302\267 ", sizeof r); snum(r, sizeof r, g_v.nfrag); scat(r, " archivo(s)", sizeof r);
    int rw = tw(r, 11, 0);
    txe(mx, y, FOOT_H, m, 11, g_msg_err && g_msg[0] ? C_AMBER : C_MU, 0, w - (mx - x) - rw - 40);
    txr(x + w - 14, y, FOOT_H, r, 11, C_FAINT, 0);
}

/* ---------------------------------------------------------------- MONITOR */
static void tarjeta_stat(int x, int y, int w, int h, const char *tit, const char *bad, uint32_t bc, const char *valor)
{
    panel(x, y, w, h);
    int bw = bad ? badge_w(bad, 0) : 0;
    txe(x + 14, y + 10, 20, tit, 11, C_MU, WF_BOLD, w - 36 - bw);
    if (bad) badge(x + w - 14 - bw, y + 10, bad, bc, 0);
    tx(x + 14, y + 34, 28, valor, 20, C_TX, WF_BOLD | WF_MONO);
}
static void gauge(int x, int y, int w, uint32_t v, uint32_t max)
{
    rr(x, y, w, 8, 4, C_INP); rs(x, y, w, 8, 4, C_BD, 255);
    int f = max ? (int)((uint64_t)w * (v > max ? max : v) / max) : 0;
    if (f > 6) for (int i = 0; i < f; i++) fb_fill_rect((uint32_t)(x + i), (uint32_t)(y + 1), 1, 6, mezcla(C_CYAN, C_BLUE, i * 255 / (w ? w : 1)));
    fb_fill_rect((uint32_t)(x + w * 8 / 10), (uint32_t)y, 2, 8, C_AMBER);
}
static void sec_monitor(int x, int y, int w, int h)
{
    struct mem_vista *v = &g_v;
    int cols = w >= 820 ? 3 : 1, gap = 12;
    int cw = cols == 3 ? (w - 2 * gap) / 3 : w, ch = 118;
    char t[120];
    /* 1. largo plazo */
    t[0] = 0; smil(t, sizeof t, v->celdas); scat(t, " hechos vivos", sizeof t);
    char tk[32] = "Tick #"; snum(tk, sizeof tk, v->tick);
    tarjeta_stat(x, y, cw, ch, "MEMORIA NEOCORTICAL (LARGO PLAZO)", tk, C_CYAN, t);
    gauge(x + 14, y + 68, cw - 28, v->total, v->cap);
    { int cx = x + 14, cy = y + 86; char c[48];
      c[0] = 0; snum(c, sizeof c, v->nodos); scat(c, " ramas", sizeof c); cx += chip(cx, cy, c, C_TX) + 6;
      c[0] = 0; smil(c, sizeof c, v->episodios); scat(c, " mensajes", sizeof c); cx += chip(cx, cy, c, C_TX) + 6;
      if (cx < x + cw - 90) { c[0] = 0; smil(c, sizeof c, v->archivadas); scat(c, " archivados", sizeof c); cx += chip(cx, cy, c, C_MU) + 6; }
      if (cx < x + cw - 90 && v->sin_vec) { c[0] = 0; smil(c, sizeof c, v->sin_vec); scat(c, " sin vector", sizeof c); cx += chip(cx, cy, c, C_AMBER) + 6; } }
    int sy = cols == 3 ? y : y + ch + gap, sx = cols == 3 ? x + cw + gap : x;
    /* 2. extraccion */
    t[0] = 0; snum(t, sizeof t, v->qn); scat(t, v->qn == 1 ? " mensaje en cola" : " mensajes en cola", sizeof t);
    tarjeta_stat(sx, sy, cw, ch, "EXTRACCI\303\223N DE HECHOS", v->ingesta ? (v->extrayendo ? "extrayendo" : "activa") : "pausada", v->ingesta ? C_GREEN : C_MU, t);
    { int cx = sx + 14, cy = sy + 72; char c[48];
      c[0] = 0; smil(c, sizeof c, v->n_nuevos); scat(c, " nuevos", sizeof c); cx += chip(cx, cy, c, C_GREEN) + 6;
      c[0] = 0; smil(c, sizeof c, v->n_dup); scat(c, " repetidos", sizeof c); cx += chip(cx, cy, c, C_TX) + 6;
      c[0] = 0; smil(c, sizeof c, v->n_sust); scat(c, " sustituciones", sizeof c); if (cx < sx + cw - 110) cx += chip(cx, cy, c, C_PURPLE) + 6;
      if (v->n_fallo) { c[0] = 0; snum(c, sizeof c, v->n_fallo); scat(c, " fallos", sizeof c); if (cx < sx + cw - 70) chip(cx, cy, c, C_RED); } }
    { char c[80] = ""; if (v->agentes) scpy(c, "Esperando: un agente est\303\241 usando el LLM", sizeof c); else if (v->qn) { scpy(c, "En calma desde hace ", sizeof c); snum(c, sizeof c, v->seg_calma); scat(c, " s", sizeof c); } else scpy(c, "Nada pendiente", sizeof c);
      tx(sx + 14, sy + 96, 16, c, 11, C_MU, 0); }
    int ty = cols == 3 ? y : sy + ch + gap, tx0 = cols == 3 ? sx + cw + gap : x;
    /* 3. ciclo de sueno */
    t[0] = 0; scat(t, "paso ", sizeof t); smil(t, sizeof t, v->pasos); scat(t, " \302\267 ciclo ", sizeof t); snum(t, sizeof t, v->pasos / 9);
    char sb[32] = ""; if (v->sueno) { scpy(sb, "cada ", sizeof sb); snum(sb, sizeof sb, (uint32_t)v->sueno_s); scat(sb, " s", sizeof sb); } else scpy(sb, "apagado", sizeof sb);
    tarjeta_stat(tx0, ty, cw, ch, "CICLO DE SUE\303\221O (HOMEOSTASIS)", sb, v->sueno ? C_PURPLE : C_MU, t);
    { int cx = tx0 + 14, cy = ty + 72; char c[48];
      c[0] = 0; smil(c, sizeof c, v->n_olv); scat(c, " olvidadas", sizeof c); cx += chip(cx, cy, c, C_AMBER) + 6;
      c[0] = 0; smil(c, sizeof c, v->n_rest); scat(c, " restauradas", sizeof c); cx += chip(cx, cy, c, C_GREEN) + 6;
      c[0] = 0; smil(c, sizeof c, v->n_fus); scat(c, " fusionadas", sizeof c); if (cx < tx0 + cw - 100) chip(cx, cy, c, C_CYAN); }
    { char c[80] = "";
      if (!v->sueno) scpy(c, "Enci\303\251ndelo en Ajustes o duerme a mano", sizeof c);
      else if (v->dormido) { scpy(c, "Ahora: ", sizeof c); scat(c, TAREA_NOM[v->tarea], sizeof c); }
      else if (v->agentes) scpy(c, "Espera a que el agente termine", sizeof c);
      else { scpy(c, "Pr\303\263ximo paso (", sizeof c); scat(c, TAREA_NOM[v->tarea], sizeof c); scat(c, ") en ~", sizeof c); snum(c, sizeof c, v->seg_proximo); scat(c, " s", sizeof c); }
      tx(tx0 + 14, ty + 96, 16, c, 11, C_MU, 0); }

    int by = (cols == 3 ? y + ch : ty + ch) + gap, bh = y + h - by;
    if (bh < 120) return;
    int lw = w >= 820 ? (w - gap) * 46 / 100 : w, rx = w >= 820 ? x + lw + gap : x, rw = w >= 820 ? w - lw - gap : w;
    /* pipeline */
    int ph = w >= 820 ? bh : 330;
    panel(x, by, lw, ph);
    tx(x + 14, by + 10, 20, "PIPELINE COGNITIVO DEL SUE\303\221O", 12, C_TX, WF_BOLD);
    txe(x + 14, by + 28, 18, "Las 9 tareas de memorIA, una por paso y por turnos, con todo en calma.", 11, C_MU, 0, lw - 28);
    int gx = x + 14, gy = by + 54, gw = (lw - 28 - 2 * 6) / 3, gh = 50;
    for (int k = 0; k < 9; k++) {
        int cx = gx + (k % 3) * (gw + 6), cy = gy + (k / 3) * (gh + 6);
        int activa = v->dormido && v->tarea == k, sig = !v->dormido && v->tarea == k;
        uint32_t c = TAREA_COL[k];
        if (activa) { rra(cx, cy, gw, gh, 8, C_CYAN, 0x30); rs(cx, cy, gw, gh, 8, C_CYAN, 255); rs(cx - 1, cy - 1, gw + 2, gh + 2, 9, C_CYAN, (uint32_t)pulso() / 2); }
        else { rr(cx, cy, gw, gh, 8, C_INP); rs(cx, cy, gw, gh, 8, sig ? C_BDL : C_BD, 255); }
        icono(k, cx + 18, cy + 25, activa ? C_CYAN : c);
        tx(cx + 34, cy + 8, 18, TAREA_NOM[k], 12, activa ? C_CYAN : C_TX, WF_BOLD);
        tx(cx + 34, cy + 26, 16, sig ? "siguiente" : TAREA_SUB[k], 10, activa ? C_CYAN : sig ? C_CYAN : C_MU, 0);
    }
    int ay = gy + 3 * (gh + 6) + 4;
    if (v->agentes) {
        rra(x + 14, ay, lw - 28, 44, 8, C_AMBER, 0x22); rs(x + 14, ay, lw - 28, 44, 8, C_AMBER, 255);
        tx(x + 26, ay + 4, 18, "ATENCI\303\223N AL AGENTE (ALTA PRIORIDAD)", 11, C_AMBER, WF_BOLD);
        tx(x + 26, ay + 22, 18, "El sue\303\261o y la extracci\303\263n esperan a que termine la respuesta.", 11, C_AMBER, 0);
    } else {
        rr(x + 14, ay, lw - 28, 44, 8, C_INP); rs(x + 14, ay, lw - 28, 44, 8, C_BD, 255);
        char f[60] = "FASE ACTUAL: "; scat(f, v->dormido ? TAREA_NOM[v->tarea] : v->extrayendo ? "EXTRACCI\303\223N" : "REPOSO", sizeof f);
        for (int i = 13; f[i]; i++) if (f[i] >= 'a' && f[i] <= 'z') f[i] = (char)(f[i] - 32);
        tx(x + 26, ay + 4, 18, f, 11, C_CYAN, WF_BOLD);
        txe(x + 26, ay + 22, 18, v->dormido ? TAREA_DESC[v->tarea] : v->extrayendo ? "Leyendo lo \303\272ltimo que dijiste para guardar los hechos." : "Esperando el siguiente tick cognitivo...", 11, C_TX, WF_MONO, lw - 52);
    }
    int ky = ay + 54, ocu = v->ops_ocupado;
    { int uy = ky + 46, quedan = (by + ph - 14 - (uy + 26)) / 30;           /* lo ultimo que ha aprendido, en el hueco que queda */
      if (quedan >= 2) {
          fb_fill_rect((uint32_t)(x + 14), (uint32_t)(uy - 6), (uint32_t)(lw - 28), 1, C_BD);
          tx(x + 14, uy, 20, "\303\232LTIMOS HECHOS APRENDIDOS", 11, C_MU, WF_BOLD);
          struct mc *m = memoria_gui_entra(); int k = 0;
          if (m) for (uint32_t i = m->n; i > 0 && k < quedan && k < 12; i--) {
              const struct mc_item *e = &m->it[i - 1]; if (e->kind != MC_CELDA || e->estado != MC_ACTIVO) continue;
              int ly2 = uy + 26 + k * 30; char n[8] = ""; snum(n, sizeof n, e->imp);
              rra(x + 14, ly2 + 3, 22, 20, 6, C_CYAN, 0x30); txc(x + 14, 22, ly2 + 3, 20, n, 10, C_CYAN, WF_BOLD);
              txe(x + 44, ly2, 26, e->texto, 12, C_TX, 0, lw - 160);
              char f[24]; mc_fecha(e->t, f, sizeof f); f[10] = 0; txr(x + lw - 14, ly2, 26, f, 10, C_FAINT, WF_MONO);
              hit(x + 14, ly2, lw - 28, 26, A_CEL, (int32_t)e->id, 0);
              k++;
          }
          if (m) memoria_gui_sale(0);
          if (!k) tx(x + 14, uy + 26, 24, "A\303\272n ninguno: habla con el agente y aparecer\303\241n aqu\303\255.", 11, C_FAINT, 0);
      } }
    if (ky + 30 <= by + ph) {
        int bx = x + 14;
        boton(bx, ky, 112, 30, "Dormir 1 paso", ocu ? 3 : 1, C_PURPLE, A_PIDE, MEMP_SUENO, 1); bx += 118;
        boton(bx, ky, 124, 30, "Vuelta completa", ocu ? 3 : 0, 0, A_PIDE, MEMP_SUENO, 9); bx += 130;
        if (bx + 100 <= x + lw - 14) boton(bx, ky, 100, 30, "Guardar ya", ocu ? 3 : 0, 0, A_PIDE, MEMP_GUARDAR, 0);
    }
    /* registro y servidor */
    int ly = w >= 820 ? by : by + ph + gap, lh = w >= 820 ? bh : 300;
    if (ly + 80 > y + h) return;
    if (ly + lh > y + h) lh = y + h - ly;
    int sh = 118, lgh = lh - sh - gap;
    panel(rx, ly, rw, lgh);
    tx(rx + 14, ly + 10, 20, "LOG DE MANTENIMIENTO HOMEOST\303\201TICO", 12, C_TX, WF_BOLD);
    int bxy = ly + 36, bxh = lgh - 50;
    rr(rx + 14, bxy, rw - 28, bxh, 8, C_INP); rs(rx + 14, bxy, rw - 28, bxh, 8, C_BD, 255);
    if (!v->ninf) tx(rx + 26, bxy + 8, 18, "A\303\272n no ha dormido. Los pasos aparecer\303\241n aqu\303\255.", 11, C_MU, WF_MONO);
    fb_set_clip(rx + 15, bxy + 1, rw - 30, bxh - 2);
    int yy = bxy + 8;
    for (int i = (int)v->ninf - 1; i >= 0 && yy + 34 < bxy + bxh; i--) {
        const char *l = v->inf[i]; int k = 0;
        const char *a = l; while (*a && *a != '[') a++;
        if (*a == '[') { a++; for (int q = 0; q < 9; q++) { const char *nm[9] = { "repair", "embed", "decay", "ordena", "describe", "dedupe", "reevaluate", "split", "merge" }; int L = slen(nm[q]), ok = 1; for (int z = 0; z < L; z++) if (a[z] != nm[q][z]) ok = 0; if (ok && a[L] == ']') k = q; } }
        fb_fill_rect((uint32_t)(rx + 24), (uint32_t)(yy + 2), 3, 30, TAREA_COL[k]);
        fb_fill_rect_alpha(rx + 27, yy + 2, rw - 64, 30, 0x08FFFFFFu);
        int used = parrafo(rx + 34, yy + 1, rw - 72, l, 11, i == (int)v->ninf - 1 ? C_TX : C_MU, WF_MONO, 16, 2);
        yy += (used < 2 ? 18 : 34) + 6;
    }
    fb_reset_clip();
    int sy2 = ly + lgh + gap;
    panel(rx, sy2, rw, sh);
    tx(rx + 14, sy2 + 10, 20, "SERVIDOR DE EMBEDDINGS", 12, C_TX, WF_BOLD);
    { char c[160] = ""; scat(c, v->modelo, sizeof c); scat(c, " en ", sizeof c); scat(c, v->srv ? v->url : "(sin direccion: Ajustes)", sizeof c);
      if (v->srv_dim) { scat(c, " \302\267 ", sizeof c); snum(c, sizeof c, (uint32_t)v->srv_dim); scat(c, " dim", sizeof c); }
      txe(rx + 14, sy2 + 32, 18, c, 11, C_MU, WF_MONO, rw - 28);
      c[0] = 0; snum(c, sizeof c, v->emb_ok); scat(c, " vectores calculados \302\267 ", sizeof c); snum(c, sizeof c, v->emb_mal); scat(c, " fallos", sizeof c);
      tx(rx + 14, sy2 + 50, 18, c, 11, v->emb_caido ? C_AMBER : C_MU, 0); }
    int bx = rx + 14, b2 = sy2 + sh - 42;
    boton(bx, b2, 92, 30, "Arrancar", ocu ? 3 : 1, C_GREEN, A_PIDE, MEMP_SRV_ON, 0); bx += 98;
    boton(bx, b2, 72, 30, "Parar", ocu ? 3 : 1, C_AMBER, A_PIDE, MEMP_SRV_OFF, 0); bx += 78;
    if (bx + 76 <= rx + rw - 14) boton(bx, b2, 76, 30, "Probar", ocu ? 3 : 0, 0, A_PIDE, MEMP_PROBAR, 0);
}

/* ---------------------------------------------------------------- EXPLORADOR */
#define CARD_H 84
#define CARD_G 8
static const char *estado_nom(int k, int e) { return k == MC_EPISODIO ? "mensaje" : e == MC_ARCHIVADO ? "archivado" : e == MC_SUSTITUIDO ? "sustituido" : "vigente"; }
static uint32_t estado_col(int k, int e) { return k == MC_EPISODIO ? C_BLUE : e == MC_ARCHIVADO ? C_AMBER : e == MC_SUSTITUIDO ? C_PURPLE : C_GREEN; }
struct vcel { uint32_t id, t, sust; uint8_t kind, estado, imp, rol, tiene_vec, nodo; char tx[360], fu[96], sub[MC_NODO_NOMBRE]; };
static struct vcel g_vc[24];
static void tarjeta_rec(int x, int y, int w, const struct vcel *c)
{
    int ov = sobre(x, y, w, CARD_H);
    rr(x, y, w, CARD_H, 9, ov ? C_CARD2 : C_INP); rs(x, y, w, CARD_H, 9, ov ? C_BDL : C_BD, 255);
    fb_fill_rect((uint32_t)x + 1, (uint32_t)(y + 10), 3, CARD_H - 20, estado_col(c->kind, c->estado));
    hit(x, y, w - 74, CARD_H, A_CEL, (int32_t)c->id, 0);
    int bx = x + 14; char b[40];
    if (c->kind == MC_CELDA) { scpy(b, "imp ", sizeof b); snum(b, sizeof b, c->imp); bx += badge(bx, y + 9, b, C_CYAN, 0) + 6; }
    else bx += badge(bx, y + 9, c->rol == MC_ROL_ASISTENTE ? "asistente" : c->rol == MC_ROL_DOC ? "documento" : "usuario", C_BLUE, 0) + 6;
    if (c->kind == MC_CELDA && c->estado != MC_ACTIVO) bx += badge(bx, y + 9, estado_nom(c->kind, c->estado), estado_col(c->kind, c->estado), 0) + 6;
    if (!c->tiene_vec) bx += badge(bx, y + 9, "sin vector", C_AMBER, 0) + 6;
    if (c->sub[0]) bx += badge(bx, y + 9, c->sub, C_PURPLE, 0) + 6;
    char f[40] = "#"; snum(f, sizeof f, c->id); scat(f, " \302\267 ", sizeof f); { char d[24]; mc_fecha(c->t, d, sizeof d); d[10] = 0; scat(f, d, sizeof f); }
    if (bx < x + w - 90 - tw(f, 11, 0)) txr(x + w - 84, y + 9, 20, f, 11, C_FAINT, WF_MONO);
    parrafo(x + 14, y + 34, w - 100, c->tx, 13, C_TX, 0, 18, 2);
    if (c->fu[0]) { /* la fuente va al final de la linea de insignias si no cabe abajo */ }
    /* acciones */
    boton_ic(x + w - 66, y + 12, 26, 0, C_CYAN, A_CEL, (int32_t)c->id, 0);
    boton_ic(x + w - 36, y + 12, 26, 1, C_RED, A_CEL_BORRA, (int32_t)c->id, 0);
}
static void sec_explorador(int x, int y, int w, int h)
{
    struct mc *m = memoria_gui_entra();
    if (!m) { tx(x, y, 30, "La memoria no esta lista.", 13, C_MU, 0); return; }
    arbol_filas(m);
    lista_rehace(m);
    int lw = w >= 1000 ? 320 : w * 34 / 100; if (lw < 220) lw = 220;
    /* panel del arbol */
    panel(x, y, lw, h);
    tx(x + 14, y + 10, 22, "ESTRUCTURA DE TEMAS", 11, C_MU, WF_BOLD);
    boton_d(x + lw - 10, y + 8, 26, "+ Tema ra\303\255z", 0, 0, A_RAIZ, 0, 0);
    int ry = y + 44;
    struct { int v; const char *n; uint32_t c; uint32_t k; } esp[4] = { { V_SIN, "Sin ordenar", C_CYAN, g_cnt_sin }, { V_ARCH, "Archivados", C_AMBER, g_cnt_arch },
                                                                        { V_SUST, "Historia (sustituidos)", C_PURPLE, g_cnt_sust }, { V_EPI, "Mensajes literales", C_BLUE, g_cnt_epi } };
    for (int i = 0; i < 4; i++) {
        int on = g_vista == esp[i].v, ov = sobre(x + 8, ry, lw - 16, 26);
        if (on) { rra(x + 8, ry, lw - 16, 26, 6, C_BLUE, 0x40); rs(x + 8, ry, lw - 16, 26, 6, C_BLUE, 255); }
        else if (ov) rra(x + 8, ry, lw - 16, 26, 6, 0xFFFFFF, 0x0C);
        punto(x + 20, ry + 13, 4, esp[i].c, 255);
        tx(x + 32, ry, 26, esp[i].n, 12, on ? C_TX : C_MU, on ? WF_BOLD : 0);
        char k[16] = ""; smil(k, sizeof k, esp[i].k); txr(x + lw - 18, ry, 26, k, 11, C_MU, WF_MONO);
        hit(x + 8, ry, lw - 16, 26, A_VISTA, esp[i].v, 0);
        ry += 28;
    }
    fb_fill_rect((uint32_t)(x + 14), (uint32_t)(ry + 6), (uint32_t)(lw - 28), 1, C_BD);
    ry += 14;
    int rows_y = ry, rh = 26, vis = (y + h - 10 - rows_y) / rh;
    if (g_tscroll > g_nfilas - vis) g_tscroll = g_nfilas - vis;
    if (g_tscroll < 0) g_tscroll = 0;
    if (!g_nfilas) parrafo(x + 16, rows_y + 4, lw - 32, "A\303\272n no hay temas: el ciclo de sue\303\261o los crea al ordenar (o crea uno con + Tema ra\303\255z).", 11, C_MU, 0, 16, 4);
    fb_set_clip(x + 1, rows_y, lw - 2, y + h - 6 - rows_y);
    for (int i = g_tscroll; i < g_nfilas && i < g_tscroll + vis + 1; i++) {
        const struct fila *f = &g_filas[i]; int fy = rows_y + (i - g_tscroll) * rh, ind = 10 + f->nivel * 16;
        int on = g_vista == V_NODO && g_nodo == f->nodo, ov = sobre(x + 8, fy, lw - 16, rh - 2);
        if (on) { rra(x + 8, fy, lw - 16, rh - 2, 6, C_PURPLE, 0x40); rs(x + 8, fy, lw - 16, rh - 2, 6, C_PURPLE, 255); }
        else if (ov) rra(x + 8, fy, lw - 16, rh - 2, 6, 0xFFFFFF, 0x0C);
        uint32_t col = f->nivel == 0 ? C_CYAN : f->nivel == 1 ? C_GREEN : C_PURPLE;
        if (f->hijos) {                                          /* triangulo para plegar */
            int cx = x + ind + 6, cy = fy + rh / 2 - 1;
            if (g_cerrado[f->nodo]) { linea(cx - 2, cy - 4, cx + 3, cy, C_MU, 20); linea(cx + 3, cy, cx - 2, cy + 4, C_MU, 20); }
            else { linea(cx - 4, cy - 2, cx, cy + 3, C_MU, 20); linea(cx, cy + 3, cx + 4, cy - 2, C_MU, 20); }
            hit(x + ind - 2, fy, 18, rh, A_PLIEGA, f->nodo, 0);
        } else punto(x + ind + 6, fy + rh / 2 - 1, 2, col, 200);
        char k[12] = ""; snum(k, sizeof k, f->rama);
        int kw = tw(k, 10, WF_BOLD) + 12;
        txe(x + ind + 18, fy, rh - 2, f->nombre, 12, on ? C_TX : f->nivel ? C_MU : C_TX, f->nivel ? 0 : WF_BOLD, lw - ind - 40 - kw);
        if (f->sucio) punto(x + lw - 26 - kw, fy + rh / 2 - 1, 2, C_AMBER, 255);
        rra(x + lw - 16 - kw, fy + 5, kw, 15, 7, col, 0x30); txc(x + lw - 16 - kw, kw, fy + 5, 15, k, 10, col, WF_BOLD);
        hit(x + ind + 16, fy, lw - ind - 24, rh - 2, A_NODO, f->nodo, 0);
    }
    fb_reset_clip();
    /* panel derecho: cabecera de la vista y tarjetas */
    int dx = x + lw + 12, dw = w - lw - 12;
    int bh = g_vista == V_NODO ? 128 : 82, baja = 0;
    if (g_vista == V_NODO && g_nodo && m->nodo[g_nodo].usado) {        /* si los botones no caben junto al titulo, van en su propia fila */
        const struct mc_nodo *n = &m->nodo[g_nodo];
        int bt = boton_w("Borrar rama") + boton_w("Copiar a...") + boton_w("Renombrar") + boton_w("+ Recuerdo") + 30 + (n->nivel + 1 < (int)MC_NIVELES ? boton_w("+ Subtema") + 6 : 0);
        if (14 + 70 + tw(n->nombre, 16, WF_BOLD) + 20 + bt > dw - 12) { baja = 46; bh += baja; }
    }
    panel(dx, y, dw, bh);
    char tit[160] = ""; const char *expl = "";
    int nivel = 0;
    if (g_vista == V_NODO && g_nodo && m->nodo[g_nodo].usado) {
        const struct mc_nodo *n = &m->nodo[g_nodo]; nivel = n->nivel;
        char lv[16] = "NIVEL "; snum(lv, sizeof lv, (uint32_t)n->nivel + 1);
        int bw = badge(dx + 14, y + 12, lv, C_CYAN, 0);
        txe(dx + 22 + bw, y + 10, 24, n->nombre, 16, C_TX, WF_BOLD, dw - 40 - bw);
        mc_nodo_ruta_texto(m, g_nodo, tit, sizeof tit);
        txe(dx + 14, y + 38, 18, tit, 11, C_MU, WF_MONO, dw / 2);
        if (g_desc_nodo != g_nodo) { g_desc_nodo = g_nodo; tf_set(&f_desc, n->desc); }
        int bx = dx + dw - 12, byb = y + 10 + baja;
        bx = boton_d(bx, byb, 28, "Borrar rama", 1, C_RED, A_NODO_BORRA, g_nodo, 0) - 6;
        bx = boton_d(bx, byb, 28, "Copiar a...", 1, C_AMBER, A_NODO_COPIA, g_nodo, 0) - 6;
        bx = boton_d(bx, byb, 28, "Renombrar", 0, 0, A_NODO_REN, g_nodo, 0) - 6;
        if (nivel + 1 < (int)MC_NIVELES) bx = boton_d(bx, byb, 28, "+ Subtema", 0, 0, A_NODO_SUB, g_nodo, 0) - 6;
        boton_d(bx, byb, 28, "+ Recuerdo", 1, C_CYAN, A_NUEVO_REC, g_nodo, 0);
        int fw = dw - 28 - 150;
        campo_tf(&f_desc, dx + 14, y + 62 + baja, fw, 30, "Descripci\303\263n del \303\241mbito de esta rama...");
        boton(dx + 14 + fw + 8, y + 62 + baja, 142, 30, "Guardar descripci\303\263n", 0, 0, A_NODO_DESC, g_nodo, 0);
        char c[60] = ""; smil(c, sizeof c, rama_cuenta(m, g_nodo)); scat(c, " hechos en la rama", sizeof c);
        int cx = dx + 14 + chip(dx + 14, y + 100 + baja, c, C_TX) + 6;
        if (n->sucio) cx += badge(cx, y + 100 + baja, "descripci\303\263n pendiente", C_AMBER, 0) + 6;
        if (n->fijo) badge(cx, y + 100 + baja, "fijada a mano", C_GREEN, 0);
    } else {
        const char *t2 = "Sin ordenar";
        if (g_vista == V_SIN) expl = "Hechos vigentes que a\303\272n no tienen tema. El paso Ordena del sue\303\261o los coloca; tambi\303\251n puedes darles tema a mano.";
        else if (g_vista == V_ARCH) { t2 = "Archivados"; expl = "Lo que el olvido archiv\303\263 (vuelve solo si una consulta lo trae) y lo que archivaste t\303\272. Abre uno para restaurarlo."; }
        else if (g_vista == V_SUST) { t2 = "Historia (sustituidos)"; expl = "Hechos que dejaron de valer porque otro los sustituy\303\263 (viv\303\255a en Madrid -> vive en Bilbao)."; }
        else if (g_vista == V_EPI) { t2 = "Mensajes literales"; expl = "Lo que se dijo, tal cual. No se resumen ni se olvidan; se pueden borrar."; }
        if (g_vista == V_NODO) { t2 = "Elige un tema"; expl = "Selecciona una rama del \303\241rbol para ver sus hechos."; }
        tx(dx + 14, y + 10, 24, t2, 16, C_TX, WF_BOLD);
        parrafo(dx + 14, y + 38, dw - 180, expl, 11, C_MU, 0, 16, 2);
        if (g_vista != V_EPI) boton_d(dx + dw - 12, y + 10, 28, "+ Recuerdo", 1, C_CYAN, A_NUEVO_REC, 0, 0);
    }
    /* tarjetas */
    int ly = y + bh + 12, lh = h - bh - 12;
    char st[60] = ""; scpy(st, g_vista == V_EPI ? "MENSAJES (" : "RECUERDOS (", sizeof st); smil(st, sizeof st, g_nlista); scat(st, ")", sizeof st);
    tx(dx + 2, ly, 20, st, 11, C_MU, WF_BOLD);
    txr(dx + dw - 2, ly, 20, g_vista == V_EPI ? "los m\303\241s recientes primero" : "por importancia y fecha", 11, C_FAINT, 0);
    ly += 26; lh -= 26;
    int paso = CARD_H + CARD_G, total = (int)g_nlista * paso;
    if (g_cscroll > total - lh) g_cscroll = total - lh;
    if (g_cscroll < 0) g_cscroll = 0;
    int i0 = g_cscroll / paso, nv = 0;
    for (int i = i0; i < (int)g_nlista && nv < 24; i++) {
        int cy = ly + i * paso - g_cscroll; if (cy > ly + lh) break;
        const struct mc_item *e = mc_get(m, g_lista[i]); if (!e) continue;
        struct vcel *c = &g_vc[nv++];
        c->id = e->id; c->t = e->t; c->sust = e->sust_por; c->kind = e->kind; c->estado = e->estado; c->imp = e->imp; c->rol = e->rol; c->tiene_vec = e->vec != 0; c->nodo = e->nodo;
        scpy(c->tx, e->texto, sizeof c->tx); scpy(c->fu, e->fuente, sizeof c->fu); c->sub[0] = 0;
        if (g_vista == V_NODO && e->nodo && e->nodo != g_nodo && m->nodo[e->nodo].usado) scpy(c->sub, m->nodo[e->nodo].nombre, sizeof c->sub);
    }
    memoria_gui_sale(0);
    if (!g_nlista) {
        rr(dx, ly, dw, 70, 9, C_INP); rs(dx, ly, dw, 70, 9, C_BD, 255);
        txc(dx, dw, ly, 70, g_vista == V_NODO && !g_nodo ? "Elige una rama a la izquierda." : "No hay recuerdos aqu\303\255.", 12, C_MU, 0);
        return;
    }
    fb_set_clip(dx, ly, dw, lh);
    for (int k = 0; k < nv; k++) tarjeta_rec(dx, ly + (i0 + k) * paso - g_cscroll, dw - (total > lh ? 10 : 0), &g_vc[k]);
    fb_reset_clip();
    if (total > lh) {                                            /* barra de desplazamiento */
        int th = lh * lh / total; if (th < 24) th = 24;
        int tyy = ly + (lh - th) * g_cscroll / (total - lh);
        rr(dx + dw - 5, ly, 4, lh, 2, C_INP); rr(dx + dw - 5, tyy, 4, th, 2, C_BDL);
    }
}

/* ---------------------------------------------------------------- BUSCAR */
static void sec_buscar(int x, int y, int w, int h)
{
    uint32_t rv = 0;
    g_nrs = memoria_gui_resultados(g_rs, 24, &rv);
    if (rv != g_rs_vista) { g_rs_vista = rv; g_rscroll = 0; }
    panel(x, y, w, 84);
    tx(x + 14, y + 10, 20, "BUSCAR EN LA MEMORIA", 12, C_TX, WF_BOLD);
    tx(x + 14 + tw("BUSCAR EN LA MEMORIA", 12, WF_BOLD) + 10, y + 10, 20, "por significado en el primario y en los cartuchos conectados", 11, C_MU, 0);
    campo_tf(&f_busca, x + 14, y + 38, w - 28 - 110, 34, "\302\277Qu\303\251 quieres recordar?  (Intro busca)");
    boton(x + w - 14 - 100, y + 38, 100, 34, "Buscar", g_v.ops_ocupado ? 3 : 2, C_BLUE, A_BUSCA, 0, 0);
    int ly = y + 96, lh = h - 96, paso = 92;
    if (!g_nrs) {
        rr(x, ly, w, 80, 9, C_INP); rs(x, ly, w, 80, 9, C_BD, 255);
        txc(x, w, ly + 8, 30, "Escribe arriba lo que buscas: \"donde vive\", \"mi perro\", \"que dije del viaje\"...", 12, C_MU, 0);
        txc(x, w, ly + 36, 30, "Los archivados que una b\303\272squeda vuelve a traer se restauran solos, como en memorIA.", 11, C_FAINT, 0);
        return;
    }
    int total = g_nrs * paso;
    if (g_rscroll > total - lh) g_rscroll = total - lh;
    if (g_rscroll < 0) g_rscroll = 0;
    fb_set_clip(x, ly, w, lh);
    for (int i = 0; i < g_nrs; i++) {
        int cy = ly + i * paso - g_rscroll; if (cy + paso < ly || cy > ly + lh) continue;
        const struct mem_res *r = &g_rs[i]; int cw = w - (total > lh ? 10 : 0);
        int ov = sobre(x, cy, cw, paso - 8);
        rr(x, cy, cw, paso - 8, 9, ov && r->primario ? C_CARD2 : C_INP); rs(x, cy, cw, paso - 8, 9, ov && r->primario ? C_BDL : C_BD, 255);
        if (r->primario) hit(x, cy, cw, paso - 8, A_RES, (int32_t)r->id, 0);
        int bx = x + 14;
        if (r->score >= 0) {
            int p = r->score * 100 / MC_COS_UNO; if (p > 100) p = 100;
            char s[16] = ""; snum(s, sizeof s, (uint32_t)p); scat(s, "%", sizeof s);
            rr(bx, cy + 14, 60, 6, 3, C_BD); rr(bx, cy + 14, 60 * p / 100 < 6 ? 6 : 60 * p / 100, 6, 3, C_CYAN);
            tx(bx + 66, cy + 7, 20, s, 11, C_CYAN, WF_BOLD | WF_MONO); bx += 110;
        } else bx += badge(bx, cy + 7, "por palabras", C_MU, 0) + 6;
        bx += badge(bx, cy + 7, r->kind == MC_EPISODIO ? (r->rol == MC_ROL_ASISTENTE ? "asistente" : "usuario") : "hecho", r->kind == MC_EPISODIO ? C_BLUE : C_GREEN, 0) + 6;
        if (r->kind == MC_CELDA && r->estado != MC_ACTIVO) bx += badge(bx, cy + 7, estado_nom(r->kind, r->estado), estado_col(r->kind, r->estado), 0) + 6;
        if (!r->primario) { char c[60] = "cartucho "; scat(c, r->cart_nombre, sizeof c); bx += badge(bx, cy + 7, c, C_AMBER, 0) + 6; }
        char f[40] = "#"; snum(f, sizeof f, r->id); scat(f, " \302\267 ", sizeof f); { char d[24]; mc_fecha(r->t, d, sizeof d); d[10] = 0; scat(f, d, sizeof f); }
        txr(x + cw - 14, cy + 7, 20, f, 11, C_FAINT, WF_MONO);
        parrafo(x + 14, cy + 34, cw - 28, r->texto, 13, C_TX, 0, 18, 2);
    }
    fb_reset_clip();
}

/* ---------------------------------------------------------------- CARTUCHOS */
static void sec_cartuchos(int x, int y, int w, int h)
{
    g_ncs = memoria_gui_cartuchos(g_cs, 8);
    int nm = 0; for (int i = 0; i < g_ncs; i++) if (g_cs[i].montado) nm++;
    panel(x, y, w, 64);
    tx(x + 14, y + 8, 22, "GESTOR MULTI-CARTUCHO DE MEMORIA", 13, C_TX, WF_BOLD);
    char s[160] = "Total: "; snum(s, sizeof s, (uint32_t)g_ncs); scat(s, " cartuchos \302\267 ", sizeof s); snum(s, sizeof s, (uint32_t)nm); scat(s, " conectados \302\267 primario: ", sizeof s); scat(s, g_v.cartucho, sizeof s);
    txe(x + 14, y + 32, 20, s, 11, C_MU, 0, w - 330);
    int bx = boton_d(x + w - 14, y + 16, 32, "Importar .smem", 1, C_GREEN, A_C_IMPORTA, 0, 0) - 8;
    boton_d(bx, y + 16, 32, "+ Nuevo cartucho", 1, C_CYAN, A_C_NUEVO, 0, 0);
    int gy = y + 76, gh = h - 76, cols = w >= 1050 ? 3 : w >= 680 ? 2 : 1, gap = 12;
    int cw = (w - (cols - 1) * gap) / cols, ch = 212;
    int filas = (g_ncs + cols - 1) / cols, total = filas * (ch + gap);
    if (g_kscroll > total - gh) g_kscroll = total - gh;
    if (g_kscroll < 0) g_kscroll = 0;
    fb_set_clip(x, gy, w, gh);
    int ocu = g_v.ops_ocupado;
    for (int i = 0; i < g_ncs; i++) {
        const struct mem_cart *c = &g_cs[i];
        int cx = x + (i % cols) * (cw + gap), cy = gy + (i / cols) * (ch + gap) - g_kscroll;
        uint32_t bc = c->primario ? C_GREEN : c->montado ? C_CYAN : C_BD;
        rr(cx, cy, cw, ch, 10, C_INP); rs(cx, cy, cw, ch, 10, bc, 255);
        if (c->primario || c->montado) { rs(cx - 1, cy - 1, cw + 2, ch + 2, 11, bc, 0x60); rs(cx - 2, cy - 2, cw + 4, ch + 4, 12, bc, 0x28); }
        const char *bs = c->primario ? "PRIMARIO (R/W)" : c->montado ? "CONECTADO (lectura)" : "DESCONECTADO";
        int bw = badge_w(bs, c->primario || c->montado);
        badge(cx + cw - 14 - bw, cy + 14, bs, c->primario ? C_GREEN : c->montado ? C_CYAN : C_MU, c->primario || c->montado ? 2 : 0);
        txe(cx + 14, cy + 10, 24, c->nombre, 15, C_TX, WF_BOLD, cw - bw - 40);
        char id[40] = "ID: "; scat(id, c->id, sizeof id); tx(cx + 14, cy + 34, 16, id, 10, C_MU, WF_MONO);
        parrafo(cx + 14, cy + 56, cw - 28, c->desc[0] ? c->desc : "Sin descripci\303\263n.", 12, c->desc[0] ? C_TX : C_FAINT, 0, 17, 2);
        int kx = cx + 14; char k[40];
        k[0] = 0; smil(k, sizeof k, c->celdas); scat(k, " hechos", sizeof k); kx += badge(kx, cy + 96, k, C_CYAN, 0) + 6;
        k[0] = 0; snum(k, sizeof k, c->nodos); scat(k, " ramas", sizeof k); kx += badge(kx, cy + 96, k, C_MU, 0) + 6;
        k[0] = 0; smil(k, sizeof k, c->kb); scat(k, " KB", sizeof k); badge(kx, cy + 96, k, C_FAINT, 0);
        fb_fill_rect((uint32_t)(cx + 12), (uint32_t)(cy + 128), (uint32_t)(cw - 24), 1, C_BD);
        int ay = cy + 138, ax = cx + 14, fin;
        if (c->primario) { tx(ax, ay, 28, "Aprende aqu\303\255", 11, C_GREEN, WF_BOLD); fin = ax + tw("Aprende aqu\303\255", 11, WF_BOLD); }
        else if (c->montado) { boton(ax, ay, 110, 28, "Hacer primario", ocu ? 3 : 1, C_GREEN, A_C_PRIM, c->idx, 0); ax += 116; boton(ax, ay, 100, 28, "Desconectar", ocu ? 3 : 1, C_AMBER, A_C_DESMONTA, c->idx, 0); fin = ax + 100; }
        else { boton(ax, ay, 84, 28, "Conectar", ocu ? 3 : 2, C_BLUE, A_C_MONTA, c->idx, 0); ax += 90; boton(ax, ay, 110, 28, "Hacer primario", ocu ? 3 : 1, C_GREEN, A_C_PRIM, c->idx, 0); fin = ax + 110; }
        int sx = cx + 14, sy = ay + 36;                                   /* segunda fila: el resto de acciones */
        const char *rb[4] = { "Clonar", "Exportar", "Vaciar", "Borrar" }; int ra[4] = { A_C_CLONA, A_C_EXPORTA, A_C_VACIA, A_C_BORRA }; uint32_t rc[4] = { 0, 0, C_AMBER, C_RED };
        for (int b = 0; b < 4; b++) {
            if (b == 3 && (c->primario || eqs(c->id, "principal"))) continue;
            int bw2 = boton_w(rb[b]) - 6; if (sx + bw2 > cx + cw - 14) break;
            boton(sx, sy, bw2, 26, rb[b], ocu ? 3 : rc[b] ? 1 : 0, rc[b], ra[b], c->idx, 0); sx += bw2 + 6;
        }
        (void)fin;
    }
    fb_reset_clip();
    int ny = gy + filas * (ch + gap) - g_kscroll;
    if (ny + 40 < gy + gh) parrafo(x + 4, ny, w - 8, "Los cartuchos conectados se consultan a la vez que el primario cuando el agente busca recuerdos (hasta 3, solo lectura). "
                                   "Lo nuevo se aprende siempre en el primario. Para meter recuerdos en otro cartucho, abre un recuerdo o una rama en el Explorador y usa Copiar o Mover.", 11, C_MU, 0, 16, 3);
}

/* ---------------------------------------------------------------- AJUSTES */
static void interruptor(int x, int y, int on, const char *clave)
{
    rr(x, y, 44, 24, 12, on ? C_GREEN : C_BD);
    punto(on ? x + 32 : x + 12, y + 12, 9, 0xFFFFFF, 255);
    hit(x, y, 44, 24, A_CFG, 0, on ? 0 : 1); H[nH - 1].f = (struct tfield *)(uintptr_t)clave;
}
static void paso_val(int x, int y, int val, const char *suf, const char *clave, int menos, int mas)
{
    boton(x, y, 28, 28, "-", 0, 0, A_CFG_PASO, menos, 0); H[nH - 1].f = (struct tfield *)(uintptr_t)clave;
    char t[32] = ""; snum(t, sizeof t, (uint32_t)(val < 0 ? 0 : val)); scat(t, suf, sizeof t);
    rr(x + 32, y, 80, 28, 7, C_INP); rs(x + 32, y, 80, 28, 7, C_BD, 255); txc(x + 32, 80, y, 28, t, 12, C_TX, WF_BOLD | WF_MONO);
    boton(x + 116, y, 28, 28, "+", 0, 0, A_CFG_PASO, mas, 0); H[nH - 1].f = (struct tfield *)(uintptr_t)clave;
}
static int sig_sueno(int v, int d)
{
    static const int P[] = { 10, 30, 60, 90, 120, 300, 600, 1800, 3600 };
    int i = 0; while (i < 8 && P[i] < v) i++;
    if (d > 0) { if (P[i] == v && i < 8) i++; } else { if (i > 0) i--; }
    return P[i];
}
static void sec_ajustes(int x, int y, int w, int h)
{
    int cw = w > 900 ? 860 : w; (void)h;
    struct { const char *t, *d, *k; int on; } tg[3] = {
        { "Memoria activa", "Si se apaga, el agente vuelve a la memoria antigua y nada se guarda aqu\303\255.", "activa", g_v.activa },
        { "Extraer hechos de la charla", "Despu\303\251s de cada respuesta, con todo en calma, el LLM saca los hechos de lo que dijiste.", "ingesta", g_v.ingesta },
        { "Ciclo de sue\303\261o", "Ordena, describe, fusiona, olvida y rescata recuerdos cuando SOMA est\303\241 en calma.", "sueno", g_v.sueno } };
    int yy = y;
    panel(x, yy, cw, 3 * 58 + 44);
    tx(x + 14, yy + 10, 22, "FUNCIONAMIENTO", 11, C_MU, WF_BOLD);
    for (int i = 0; i < 3; i++) {
        int ry = yy + 38 + i * 58;
        tx(x + 14, ry, 22, tg[i].t, 13, C_TX, WF_BOLD); txe(x + 14, ry + 22, 18, tg[i].d, 11, C_MU, 0, cw - 100);
        interruptor(x + cw - 60, ry + 8, tg[i].on, tg[i].k);
        if (i < 2) fb_fill_rect((uint32_t)(x + 14), (uint32_t)(ry + 50), (uint32_t)(cw - 28), 1, C_BD);
    }
    yy += 3 * 58 + 44 + 12;
    panel(x, yy, cw, 3 * 52 + 44);
    tx(x + 14, yy + 10, 22, "RITMO Y CONTEXTO", 11, C_MU, WF_BOLD);
    struct { const char *t, *d, *k, *suf; int v, mn, mx; } st[3] = {
        { "Un paso de sue\303\261o cada", "Con un paso cada 90 s, un hecho de importancia 5 dura unos 6 d\303\255as sin repetirse.", "sueno_s", " s", g_v.sueno_s, sig_sueno(g_v.sueno_s, -1), sig_sueno(g_v.sueno_s, 1) },
        { "Hechos en cada respuesta", "Cu\303\241ntos hechos de la memoria entran en el prompt del agente (1-40).", "hechos", "", g_v.n_hechos, g_v.n_hechos - 1, g_v.n_hechos + 1 },
        { "Mensajes literales", "Cu\303\241ntos mensajes antiguos relacionados entran adem\303\241s (0-60).", "episodios", "", g_v.n_epis, g_v.n_epis - 2, g_v.n_epis + 2 } };
    for (int i = 0; i < 3; i++) {
        int ry = yy + 38 + i * 52;
        tx(x + 14, ry, 22, st[i].t, 13, C_TX, WF_BOLD); txe(x + 14, ry + 22, 18, st[i].d, 11, C_MU, 0, cw - 200);
        paso_val(x + cw - 160, ry + 6, st[i].v, st[i].suf, st[i].k, st[i].mn, st[i].mx);
    }
    yy += 3 * 52 + 44 + 12;
    panel(x, yy, cw, 136);
    tx(x + 14, yy + 10, 22, "SERVIDOR DE EMBEDDINGS", 11, C_MU, WF_BOLD);
    tx(x + 14, yy + 36, 20, "Direcci\303\263n", 12, C_TX, WF_BOLD); campo_tf(&f_url, x + 110, yy + 32, cw - 124 - 230, 30, "http://192.168.1.200:8089");
    tx(x + cw - 224, yy + 36, 20, "Modelo", 12, C_TX, WF_BOLD); campo_tf(&f_mod, x + cw - 164, yy + 32, 150, 30, "bge-m3");
    parrafo(x + 14, yy + 72, cw - 200, "Cambiar de modelo hace que los vectores viejos no sirvan: la memoria los recalcula poco a poco. Arrancar y parar est\303\241n en Monitor.", 11, C_MU, 0, 16, 2);
    boton(x + cw - 164, yy + 92, 150, 30, "Guardar servidor", 2, C_BLUE, A_CFG_SRV, 0, 0);
}

/* ---------------------------------------------------------------- ARCHIVOS (los de los agentes de siempre) */
static const char *const ARCH[][3] = {
    { "Perfil de SOMA", "/agent/soma.txt", "Directivas, rol y seguridad del agente central" }, { "Memoria reciente (STM) de SOMA", "/agent/soma_stm.txt", "Ventana antigua de conversaciones (sin la memoria nueva)" },
    { "Notas de SOMA", "/workspace/soma.txt", "Informes y planes persistidos" }, { "Estado ReAct", "/agent/state", "Transiciones y auto-modificaciones" },
    { "Perfil del Buscador", "/agent/buscador.txt", "Especialidad en an\303\241lisis y s\303\255ntesis web" }, { "Memoria reciente del Buscador", "/agent/buscador_stm.txt", "Consultas y respuestas recientes" },
    { "Notas del Buscador", "/workspace/buscador.txt", "Res\303\272menes y fuentes recopiladas" }, { "Pizarra compartida", "/agent/blackboard.txt", "Consenso y acuerdos entre agentes" },
    { "Perfil del usuario", "/etc/mem_user.txt", "Preferencias del administrador" }, { "Entorno de hardware", "/etc/mem_hw.txt", "Telemetr\303\255a de CPU, RAM y bus" },
    { "\303\201rbol antiguo: \303\255ndice", "/mem/indice.txt", "Memoria de antes del 277 (memoria importar la trae)" }, { "\303\201rbol antiguo: conversaciones", "/mem/conversaciones.txt", "Conclusiones con el usuario" },
    { "Configuraci\303\263n de la memoria", "/etc/memoria.conf", "Lo que se cambia en Ajustes" }, { "Registro de cartuchos", "/memoria/cartuchos.txt", "Que cartuchos hay y cual es el primario" } };
#define NARCH ((int)(sizeof ARCH / sizeof ARCH[0]))
static void sec_archivos(int x, int y, int w, int h)
{
    tx(x + 2, y, 22, "ARCHIVOS DE LOS AGENTES", 11, C_MU, WF_BOLD);
    txr(x + w - 2, y, 22, "Se abren en Notas", 11, C_FAINT, 0);
    int ly = y + 30, lh = h - 30, paso = 58, total = NARCH * paso;
    if (g_ascroll > total - lh) g_ascroll = total - lh;
    if (g_ascroll < 0) g_ascroll = 0;
    fb_set_clip(x, ly, w, lh);
    for (int i = 0; i < NARCH; i++) {
        int cy = ly + i * paso - g_ascroll; if (cy + paso < ly || cy > ly + lh) continue;
        int ov = sobre(x, cy, w, paso - 8);
        rr(x, cy, w, paso - 8, 9, ov ? C_CARD2 : C_INP); rs(x, cy, w, paso - 8, 9, ov ? C_BDL : C_BD, 255);
        icono(15, x + 22, cy + 25, C_CYAN);
        tx(x + 40, cy + 6, 20, ARCH[i][0], 13, C_TX, WF_BOLD);
        txe(x + 40, cy + 26, 18, ARCH[i][2], 11, C_MU, 0, w / 2 - 40);
        txe(x + w / 2, cy + 6, 38, ARCH[i][1], 11, C_CYAN, WF_MONO, w / 2 - 150);
        boton(x + w - 132, cy + 10, 118, 30, "Abrir en Notas", 0, 0, A_ARCH, i, 0);
    }
    fb_reset_clip();
}

/* ---------------------------------------------------------------- ventanas encima */
static void velo(int x, int y, int w, int h) { fb_fill_rect_alpha(x, y, w, h, 0xB0000000u); hit(x, y, w, h, A_NADA, 0, 0); }
static void caja(int x, int y, int w, int h, const char *tit)
{
    fb_draw_box_shadow(x, y, w, h, 12, 0, 10, 0, 28, 0x90000000u);
    rr(x, y, w, h, 12, C_CARD); rs(x, y, w, h, 12, C_BDL, 255);
    tx(x + 18, y + 12, 26, tit, 15, C_TX, WF_BOLD);
    fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 48), (uint32_t)(w - 2), 1, C_BD);
}
static void modal_editor(int x, int y, int w, int h)
{
    int mw = w - 40 < 760 ? w - 40 : 760, mh = h - 40 < 560 ? h - 40 : 560, mx = x + (w - mw) / 2, my = y + (h - mh) / 2;
    char tit[60];
    if (!g_ed_id) scpy(tit, "Nuevo recuerdo", sizeof tit); else { scpy(tit, g_ed_kind == MC_EPISODIO ? "Mensaje #" : "Recuerdo #", sizeof tit); snum(tit, sizeof tit, g_ed_id); }
    caja(mx, my, mw, mh, tit);
    if (g_ed_id) { int bw = badge_w(estado_nom(g_ed_kind, g_ed_estado), 0); badge(mx + mw - 60 - bw, my + 15, estado_nom(g_ed_kind, g_ed_estado), estado_col(g_ed_kind, g_ed_estado), 0); }
    boton_ic(mx + mw - 44, my + 11, 26, 1, C_MU, A_ED_CIERRA, 0, 0);
    int cy = my + 62;
    tx(mx + 18, cy, 18, g_ed_kind == MC_EPISODIO ? "Mensaje literal:" : "Hecho at\303\263mico (una afirmaci\303\263n completa, con de qui\303\251n es):", 11, C_MU, 0);
    cy += 22;
    int eh = mh - 62 - 22 - 220; if (eh < 90) eh = 90;
    campo_tf(&f_edit, mx + 18, cy, mw - 36, eh, "Ej.: El usuario es alergico a la penicilina");
    cy += eh + 14;
    if (g_ed_kind != MC_EPISODIO) {
        tx(mx + 18, cy, 30, "Importancia", 12, C_TX, WF_BOLD);
        int ix = mx + 110;
        boton(ix, cy + 1, 28, 28, "-", 0, 0, A_ED_IMP, -1, 0);
        for (int i = 1; i <= 10; i++) rr(ix + 34 + (i - 1) * 15, cy + 9, 12, 12, 3, i <= g_ed_imp ? mezcla(C_CYAN, C_BLUE, i * 25) : C_BD);
        char n[8] = ""; snum(n, sizeof n, (uint32_t)g_ed_imp); tx(ix + 34 + 152, cy, 30, n, 14, C_CYAN, WF_BOLD | WF_MONO);
        boton(ix + 34 + 176, cy + 1, 28, 28, "+", 0, 0, A_ED_IMP, 1, 0);
        int rx = ix + 34 + 216;
        tx(rx, cy, 30, "Tema", 12, C_TX, WF_BOLD);
        campo_tf(&f_ruta, rx + 46, cy, mx + mw - 18 - rx - 46, 30, "Tema/Subtema (vac\303\255o = sin ordenar)");
        cy += 42;
    }
    if (g_ed_id) {
        char inf[260] = "Desde "; char d[24]; mc_fecha(g_ed_t, d, sizeof d); scat(inf, d, sizeof inf);
        if (g_ed_kind == MC_CELDA) { scat(inf, " \302\267 refuerzos ", sizeof inf); snum(inf, sizeof inf, g_ed_ref); }
        scat(inf, " \302\267 vector ", sizeof inf); if (g_ed_dim) { snum(inf, sizeof inf, g_ed_dim); scat(inf, " dim", sizeof inf); } else scat(inf, "pendiente", sizeof inf);
        if (g_ed_estado == MC_SUSTITUIDO && g_ed_sust) { scat(inf, " \302\267 sustituido por #", sizeof inf); snum(inf, sizeof inf, g_ed_sust); }
        tx(mx + 18, cy, 18, inf, 11, C_MU, WF_MONO);
        if (g_ed_fuente[0]) { char fu[260] = "Fuente: "; scat(fu, g_ed_fuente, sizeof fu); txe(mx + 18, cy + 20, 18, fu, 11, C_CYAN, 0, mw - 36); }
    }
    int by = my + mh - 50, ocu = g_v.ops_ocupado;
    fb_fill_rect((uint32_t)(mx + 1), (uint32_t)(by - 12), (uint32_t)(mw - 2), 1, C_BD);
    int bx = mx + 18;
    if (g_ed_id) {
        boton(bx, by, 80, 32, "Borrar", ocu ? 3 : 1, C_RED, A_ED_BORRA, 0, 0); bx += 86;
        if (g_ed_kind == MC_CELDA) {
            if (g_ed_estado == MC_ARCHIVADO) { boton(bx, by, 96, 32, "Restaurar", 1, C_GREEN, A_ED_ARCH, 0, 0); bx += 102; }
            else if (g_ed_estado == MC_ACTIVO) { boton(bx, by, 90, 32, "Archivar", 1, C_AMBER, A_ED_ARCH, 1, 0); bx += 96; }
        }
        if (bx + 220 < mx + mw - 240) { boton(bx, by, 104, 32, "Copiar a...", ocu ? 3 : 0, 0, A_ED_COPIA, 0, 0); bx += 110; boton(bx, by, 104, 32, "Mover a...", ocu ? 3 : 0, 0, A_ED_MUEVE, 0, 0); }
    }
    int r = boton_d(mx + mw - 18, by, 32, g_ed_id ? "Guardar cambios" : "Guardar recuerdo", 2, C_BLUE, A_ED_GUARDA, 0, 0) - 8;
    boton_d(r, by, 32, "Cancelar", 0, 0, A_ED_CIERRA, 0, 0);
}
static void modal_conf(int x, int y, int w, int h)
{
    int mw = w - 40 < 520 ? w - 40 : 520, mh = 200, mx = x + (w - mw) / 2, my = y + (h - mh) / 2;
    caja(mx, my, mw, mh, "Confirmar");
    parrafo(mx + 18, my + 62, mw - 36, g_conf_txt, 13, C_TX, 0, 19, 4);
    int r = boton_d(mx + mw - 18, my + mh - 48, 32, g_conf == CF_BORRA_RAMA_SOLA ? "Quitar rama" : g_conf == CF_C_VACIA ? "Vaciar" : "Borrar", 2, g_conf == CF_C_VACIA ? C_AMBER : C_RED, A_CONF_SI, 0, 0) - 8;
    if (g_conf == CF_BORRA_RAMA_SOLA) r = boton_d(r, my + mh - 48, 32, "Rama y sus hechos", 1, C_RED, A_BORRA_RAMA_TODO, 0, 0) - 8;
    boton_d(r, my + mh - 48, 32, "Cancelar", 0, 0, A_CONF_NO, 0, 0);
}
static void modal_in(int x, int y, int w, int h)
{
    int dos = g_in == IN_C_NUEVO, vec = g_in == IN_C_EXPORTA || g_in == IN_IMPORTA;
    int mw = w - 40 < 560 ? w - 40 : 560, mh = 170 + (dos ? 48 : 0) + (vec ? 40 : 0), mx = x + (w - mw) / 2, my = y + (h - mh) / 2;
    caja(mx, my, mw, mh, g_in_tit);
    const char *l1 = g_in == IN_C_EXPORTA || g_in == IN_IMPORTA ? "Archivo (.smem)" : "Nombre", *ph = g_in == IN_NODO_NUEVO ? "Nombre del tema" : g_in == IN_IMPORTA ? "/usuario/memoria-001.smem" : "Nombre";
    tx(mx + 18, my + 62, 18, l1, 11, C_MU, 0);
    campo_tf(&f_nom, mx + 18, my + 82, mw - 36, 32, ph);
    int cy = my + 124;
    if (dos) { tx(mx + 18, cy, 18, "Descripci\303\263n (opcional)", 11, C_MU, 0); campo_tf(&f_nom2, mx + 18, cy + 20, mw - 36, 32, "Para que sirve este cartucho"); cy += 56; }
    if (vec) {
        rr(mx + 18, cy + 4, 20, 20, 5, g_in_vec ? C_BLUE : C_INP); rs(mx + 18, cy + 4, 20, 20, 5, g_in_vec ? C_BLUE : C_BDL, 255);
        if (g_in_vec) { linea(mx + 23, cy + 14, mx + 27, cy + 19, 0xFFFFFF, 28); linea(mx + 27, cy + 19, mx + 34, cy + 9, 0xFFFFFF, 28); }
        tx(mx + 46, cy + 2, 24, g_in == IN_IMPORTA ? "Usar los vectores del archivo (si son del mismo modelo)" : "Incluir vectores (el archivo ocupa unas 8 veces m\303\241s)", 12, C_TX, 0);
        hit(mx + 18, cy, mw - 36, 28, A_IN_VEC, 0, 0);
    }
    int r = boton_d(mx + mw - 18, my + mh - 48, 32, g_in == IN_C_EXPORTA ? "Exportar" : g_in == IN_IMPORTA ? "Importar" : g_in == IN_NODO_REN ? "Renombrar" : "Crear", 2, C_BLUE, A_IN_OK, 0, 0) - 8;
    boton_d(r, my + mh - 48, 32, "Cancelar", 0, 0, A_IN_NO, 0, 0);
}
static void modal_pick(int x, int y, int w, int h)
{
    g_ncs = memoria_gui_cartuchos(g_cs, 8);
    int otros = 0; for (int i = 0; i < g_ncs; i++) if (!g_cs[i].primario) otros++;
    int mw = w - 40 < 520 ? w - 40 : 520, mh = 120 + (otros ? otros : 1) * 50 + 30, mx = x + (w - mw) / 2, my = y + (h - mh) / 2;
    caja(mx, my, mw, mh, g_pick == PK_MUEVE_CEL ? "Mover a otro cartucho" : g_pick == PK_COPIA_RAMA ? "Copiar la rama a otro cartucho" : "Copiar a otro cartucho");
    tx(mx + 18, my + 58, 20, g_pick == PK_MUEVE_CEL ? "Se copia al destino y se borra del primario." : "Lo que ya este en el destino no se duplica; la rama y su tema se conservan.", 11, C_MU, 0);
    int cy = my + 86;
    if (!otros) { tx(mx + 18, cy, 40, "No hay otros cartuchos. Crea uno en la pesta\303\261a Cartuchos.", 12, C_AMBER, 0); cy += 50; }
    for (int i = 0; i < g_ncs; i++) {
        const struct mem_cart *c = &g_cs[i]; if (c->primario) continue;
        int ov = sobre(mx + 18, cy, mw - 36, 42);
        rr(mx + 18, cy, mw - 36, 42, 8, ov ? C_CARD2 : C_INP); rs(mx + 18, cy, mw - 36, 42, 8, ov ? C_CYAN : C_BD, 255);
        icono(13, mx + 36, cy + 21, c->montado ? C_CYAN : C_MU);
        tx(mx + 54, cy, 42, c->nombre, 13, C_TX, WF_BOLD);
        char k[40] = ""; smil(k, sizeof k, c->celdas); scat(k, c->montado ? " hechos \302\267 conectado" : " hechos", sizeof k);
        txr(mx + mw - 32, cy, 42, k, 11, C_MU, 0);
        hit(mx + 18, cy, mw - 36, 42, A_PICK, c->idx, 0);
        cy += 50;
    }
    boton_d(mx + mw - 18, my + mh - 46, 30, "Cancelar", 0, 0, A_PICK_NO, 0, 0);
}

/* ---------------------------------------------------------------- dibujo */
void memview_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int show_cursor)
{
    (void)show_cursor;
    campos_ini();
    if (!g_inited) memview_init();
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    g_wx = x; g_wy = y; g_ww = w; g_wh = h;
    memoria_vista(&g_v);
    nH = 0;
    fb_set_clip(x, y, w, h);
    fondo(x, y, w, h);
    cabecera(x, y, w);
    pestanas(x, y + HDR_H, w);
    int cx = x + 16, cy = y + HDR_H + TAB_H + 4, cw = w - 32, ch = h - HDR_H - TAB_H - FOOT_H - 16;
    fb_set_clip(x, cy - 2, w, ch + 4);
    switch (g_tab) {
    case T_MON: sec_monitor(cx, cy, cw, ch); break;
    case T_EXP: sec_explorador(cx, cy, cw, ch); break;
    case T_BUS: sec_buscar(cx, cy, cw, ch); break;
    case T_CAR: sec_cartuchos(cx, cy, cw, ch); break;
    case T_AJU: sec_ajustes(cx, cy, cw, ch); break;
    default: sec_archivos(cx, cy, cw, ch); break;
    }
    fb_set_clip(x, y, w, h);
    pie(x, y + h - FOOT_H, w);
    if (g_modal) {
        nH = 0;                                                   /* con una ventana encima, solo ella responde */
        velo(x, y, w, h);
        if (g_modal == M_EDIT) modal_editor(x, y, w, h);
        else if (g_modal == M_CONF) modal_conf(x, y, w, h);
        else if (g_modal == M_IN) modal_in(x, y, w, h);
        else modal_pick(x, y, w, h);
    }
    fb_reset_clip();
}

/* ---------------------------------------------------------------- acciones */
static void abre_editor(uint32_t id, uint32_t nodo_nuevo)
{
    campos_ini();
    g_ed_id = id; g_ed_gen = memoria_gui_gen(); g_ed_imp = 7; g_ed_kind = MC_CELDA; g_ed_estado = MC_ACTIVO; g_ed_rol = 0; g_ed_t = 0; g_ed_sust = 0; g_ed_dim = 0; g_ed_ref = 0; g_ed_fuente[0] = 0;
    tf_set(&f_edit, ""); tf_set(&f_ruta, "");
    struct mc *m = memoria_gui_entra(); if (!m) return;
    if (id) {
        const struct mc_item *e = mc_get(m, id);
        if (!e) { memoria_gui_sale(0); aviso("Ese recuerdo ya no existe.", 1); return; }
        g_ed_imp = e->imp; g_ed_kind = e->kind; g_ed_estado = e->estado; g_ed_rol = e->rol; g_ed_t = e->t; g_ed_sust = e->sust_por; g_ed_dim = e->dim; g_ed_ref = e->refuerzos;
        scpy(g_ed_fuente, e->fuente, sizeof g_ed_fuente);
        tf_set(&f_edit, e->texto);
        if (e->kind == MC_CELDA && e->nodo && m->nodo[e->nodo].usado) { char r[160]; mc_nodo_ruta_texto(m, e->nodo, r, sizeof r); tf_set(&f_ruta, r); }
    } else if (nodo_nuevo && m->nodo[nodo_nuevo].usado) { char r[160]; mc_nodo_ruta_texto(m, nodo_nuevo, r, sizeof r); tf_set(&f_ruta, r); }
    memoria_gui_sale(0);
    f_edit.cur = f_edit.anc = 0; f_edit.scroll = 0;
    g_modal = M_EDIT; g_foco = &f_edit; cambia();
}
static void guarda_editor(void)
{
    char texto[MC_TEXTO_MAX + 1]; scpy(texto, b_edit, sizeof texto);
    int k = slen(texto); while (k && (texto[k - 1] == ' ' || texto[k - 1] == '\n')) texto[--k] = 0;
    const char *t = texto; while (*t == ' ' || *t == '\n') t++;
    if (!*t) { aviso("El recuerdo est\303\241 vac\303\255o.", 1); return; }
    struct mc *m = memoria_gui_entra(); if (!m) return;
    if (g_ed_id && g_ed_gen != memoria_gui_gen()) { memoria_gui_sale(0); aviso("La memoria cambi\303\263 (borrado o cambio de cartucho): vuelve a abrir el recuerdo.", 1); g_modal = M_NO; g_foco = 0; return; }
    uint32_t id = g_ed_id; int nuevo = !id, cambio = 0;
    if (nuevo) {
        id = mc_add(m, MC_CELDA, MC_ROL_USUARIO, t, "Anotado a mano en la aplicacion Memoria", g_ed_imp, memoria_gui_ahora(), m->tick, 0, 0);
        if (!id) { memoria_gui_sale(0); aviso("La memoria est\303\241 llena.", 1); return; }
        cambio = 1;
    } else {
        const struct mc_item *e = mc_get(m, id);
        if (!e) { memoria_gui_sale(0); aviso("Ese recuerdo ya no existe.", 1); g_modal = M_NO; return; }
        if (!eqs(e->texto, t)) { mc_set_texto(m, id, t); cambio = 1; }
        if (e->kind == MC_CELDA && e->imp != g_ed_imp) { mc_set_imp(m, id, g_ed_imp, m->tick); cambio = 1; }
    }
    const struct mc_item *e = mc_get(m, id);
    if (e && e->kind == MC_CELDA) {
        uint32_t nodo = 0; const char *r = b_ruta; while (*r == ' ' || *r == '/') r++;
        if (*r) { nodo = mc_nodo_ruta(m, r, 1); if (nodo) m->nodo[nodo].fijo = 1; }
        if (nodo != e->nodo) { mc_asigna(m, id, nodo); cambio = 1; }
    }
    memoria_gui_sale(cambio);
    g_modal = M_NO; g_foco = 0;
    g_clave_lista = 0xFFFFFFFFu;
    aviso(nuevo ? "Recuerdo guardado (su vector se calcula en segundo plano)." : cambio ? "Cambios guardados." : "Sin cambios.", 0);
}
static void confirma(int que, uint32_t a, const char *txt) { g_conf_ed = 0; g_conf = que; g_conf_a = a; scpy(g_conf_txt, txt, sizeof g_conf_txt); g_modal = M_CONF; g_foco = 0; cambia(); }
static void entrada(int que, uint32_t a, const char *tit, const char *val, const char *val2)
{
    g_in = que; g_in_a = a; scpy(g_in_tit, tit, sizeof g_in_tit); tf_set(&f_nom, val ? val : ""); tf_set(&f_nom2, val2 ? val2 : "");
    tf_select_all(&f_nom); g_modal = M_IN; g_foco = &f_nom; cambia();
}
static const struct mem_cart *cart_de(int idx) { g_ncs = memoria_gui_cartuchos(g_cs, 8); for (int i = 0; i < g_ncs; i++) if (g_cs[i].idx == idx) return &g_cs[i]; return 0; }
static void entrada_ok(void)
{
    const char *v = b_nom; while (*v == ' ') v++;
    if (!*v && g_in != IN_NODO_NUEVO) { aviso("Falta el nombre.", 1); return; }
    switch (g_in) {
    case IN_C_NUEVO: pide(MEMP_C_NUEVO, 0, 0, v, b_nom2); break;
    case IN_C_CLONA: pide(MEMP_C_CLONA, g_in_a, 0, v, 0); break;
    case IN_C_EXPORTA: pide(MEMP_C_EXPORTA, g_in_a, (uint32_t)g_in_vec, v, 0); break;
    case IN_IMPORTA: pide(MEMP_IMPORTA, 0, (uint32_t)g_in_vec, v, 0); break;
    case IN_NODO_NUEVO: case IN_NODO_REN: {
        if (!*v) { aviso("Falta el nombre.", 1); return; }
        struct mc *m = memoria_gui_entra(); if (!m) return;
        int ok = 0;
        if (g_in == IN_NODO_REN) ok = mc_nodo_renombra(m, g_in_a, v) == 0;
        else {
            char ruta[200] = "";
            if (g_in_a && m->nodo[g_in_a].usado) { mc_nodo_ruta_texto(m, g_in_a, ruta, sizeof ruta); scat(ruta, "/", sizeof ruta); }
            char limpio[100]; int k = 0; for (int i = 0; v[i] && k < 99; i++) limpio[k++] = v[i] == '/' ? '-' : v[i]; limpio[k] = 0;
            scat(ruta, limpio, sizeof ruta);
            uint32_t ant = mc_nodo_ruta(m, ruta, 0);
            uint32_t n = ant ? 0 : mc_nodo_ruta(m, ruta, 1);
            if (n && (!g_in_a || m->nodo[n].padre == g_in_a)) { m->nodo[n].fijo = 1; m->arbol_sucio = 1; ok = 1; g_vista = V_NODO; g_nodo = n; if (g_in_a) g_cerrado[g_in_a] = 0; }
            else if (ant) { memoria_gui_sale(0); aviso("Ya existe un tema con ese nombre ahi.", 1); return; }
        }
        memoria_gui_sale(ok);
        if (!ok) { aviso(g_in == IN_NODO_REN ? "No se pudo renombrar (ya hay un tema hermano con ese nombre)." : "No caben m\303\241s temas (255) o el nombre no vale.", 1); return; }
        aviso(g_in == IN_NODO_REN ? "Tema renombrado." : "Tema creado.", 0);
        g_desc_nodo = 0;
        break; }
    }
    g_modal = M_NO; g_foco = 0; cambia();
}
static void accion(const struct hit *z, int mx, int my)
{
    switch (z->act) {
    case A_TAB: g_tab = z->a; g_foco = 0; if (g_tab == T_BUS) g_foco = &f_busca; break;
    case A_PIDE: pide(z->a, (uint32_t)z->b, 0, 0, 0); break;
    case A_FOCO: g_foco = z->f; tf_press(z->f, mx, my, keyboard_shift_held()); break;
    case A_VISTA: g_vista = z->a; g_cscroll = 0; g_foco = 0; break;
    case A_NODO: g_vista = V_NODO; g_nodo = (uint32_t)z->a; g_cscroll = 0; g_foco = 0; break;
    case A_PLIEGA: g_cerrado[z->a] = !g_cerrado[z->a]; break;
    case A_CEL: case A_RES: abre_editor((uint32_t)z->a, 0); break;
    case A_CEL_BORRA: g_ed_id = (uint32_t)z->a; g_ed_gen = memoria_gui_gen(); confirma(CF_BORRA_CEL, (uint32_t)z->a, "Borrar este recuerdo para siempre? No se puede deshacer (Archivar lo guarda sin usarlo)."); break;
    case A_NUEVO_REC: abre_editor(0, (uint32_t)z->a); break;
    case A_RAIZ: entrada(IN_NODO_NUEVO, 0, "Nuevo tema ra\303\255z", "", 0); break;
    case A_NODO_SUB: entrada(IN_NODO_NUEVO, (uint32_t)z->a, "Nuevo subtema", "", 0); break;
    case A_NODO_REN: { struct mc *m = memoria_gui_entra(); char n[MC_NODO_NOMBRE] = ""; if (m) { scpy(n, m->nodo[z->a].nombre, sizeof n); memoria_gui_sale(0); } entrada(IN_NODO_REN, (uint32_t)z->a, "Renombrar tema", n, 0); break; }
    case A_NODO_BORRA: confirma(CF_BORRA_RAMA_SOLA, (uint32_t)z->a, "Quitar esta rama y sus subtemas? Con \"Quitar rama\" sus hechos se quedan (sin tema, el sue\303\261o los reordena). Con \"Rama y sus hechos\" se borran para siempre."); break;
    case A_BORRA_RAMA_TODO: g_ed_gen = memoria_gui_gen(); pide(MEMP_BORRA_RAMA, g_conf_a, 0, 0, 0); g_modal = M_NO; if (g_nodo == g_conf_a) { g_vista = V_SIN; g_nodo = 0; } break;
    case A_NODO_COPIA: g_pick = PK_COPIA_RAMA; g_pick_a = (uint32_t)z->a; g_ed_gen = memoria_gui_gen(); g_modal = M_PICK; break;
    case A_NODO_DESC: { struct mc *m = memoria_gui_entra(); if (m) { if (m->nodo[z->a].usado) { mc_nodo_set_desc(m, (uint32_t)z->a, b_desc); m->nodo[z->a].sucio = 0; m->nodo[z->a].fijo = 1; m->arbol_sucio = 1; } memoria_gui_sale(1); } aviso("Descripci\303\263n guardada.", 0); g_foco = 0; break; }
    case A_ED_GUARDA: guarda_editor(); break;
    case A_ED_CIERRA: g_modal = M_NO; g_foco = 0; break;
    case A_ED_IMP: g_ed_imp += z->a; if (g_ed_imp < 1) g_ed_imp = 1; if (g_ed_imp > 10) g_ed_imp = 10; break;
    case A_ED_ARCH: {
        struct mc *m = memoria_gui_entra(); if (!m) break;
        int ok = g_ed_gen == memoria_gui_gen() && mc_get(m, g_ed_id);
        if (ok) { if (z->a) mc_set_estado(m, g_ed_id, MC_ARCHIVADO, memoria_gui_ahora(), 0); else mc_set_estado(m, g_ed_id, MC_ACTIVO, 0, 0); m->arbol_sucio = 1; }
        memoria_gui_sale(ok);
        if (ok) { g_ed_estado = z->a ? MC_ARCHIVADO : MC_ACTIVO; aviso(z->a ? "Archivado: ya no entra en las respuestas (no se borra)." : "Restaurado: vuelve a estar vigente.", 0); g_clave_lista = 0xFFFFFFFFu; }
        break; }
    case A_ED_BORRA: confirma(CF_BORRA_CEL, g_ed_id, "Borrar este recuerdo para siempre? No se puede deshacer."); g_conf_ed = 1; break;
    case A_ED_COPIA: g_pick = PK_COPIA_CEL; g_pick_a = g_ed_id; g_modal = M_PICK; break;
    case A_ED_MUEVE: g_pick = PK_MUEVE_CEL; g_pick_a = g_ed_id; g_modal = M_PICK; break;
    case A_PICK:
        if (g_pick == PK_COPIA_RAMA) { struct mem_pet p; uint8_t *q = (uint8_t *)&p; for (uint32_t i = 0; i < sizeof p; i++) q[i] = 0; p.op = MEMP_COPIA; p.a = (uint32_t)z->a; p.b = g_pick_a; p.gen = g_ed_gen;
                                       if (!memoria_gui_pide(&p)) aviso("Espera: la memoria est\303\241 terminando otra operacion.", 1); }
        else pide_ids(g_pick == PK_MUEVE_CEL ? MEMP_MUEVE : MEMP_COPIA, (uint32_t)z->a, &g_pick_a, 1);
        g_modal = M_NO; g_foco = 0; break;
    case A_PICK_NO: g_modal = g_pick == PK_COPIA_RAMA ? M_NO : M_EDIT; break;
    case A_CONF_SI:
        if (g_conf == CF_BORRA_CEL) { pide_ids(MEMP_BORRA, 0, &g_conf_a, 1); }
        else if (g_conf == CF_BORRA_RAMA_SOLA) {
            struct mc *m = memoria_gui_entra(); uint32_t n = 0; if (m) { n = mc_nodo_quita(m, g_conf_a, 0); memoria_gui_sale(1); }
            char t[100] = "Rama quitada; "; snum(t, sizeof t, n); scat(t, " hecho(s) quedan sin tema.", sizeof t); aviso(t, 0);
            if (g_nodo == g_conf_a || g_vista == V_NODO) { g_vista = V_SIN; g_nodo = 0; }
        }
        else if (g_conf == CF_C_VACIA) pide(MEMP_C_VACIA, g_conf_a, 0, 0, 0);
        else if (g_conf == CF_C_BORRA) pide(MEMP_C_BORRA, g_conf_a, 0, 0, 0);
        g_modal = M_NO; g_foco = 0; g_ed_gen = 0; break;
    case A_CONF_NO: g_modal = g_conf_ed ? M_EDIT : M_NO; g_foco = 0; break;
    case A_IN_OK: entrada_ok(); break;
    case A_IN_NO: g_modal = M_NO; g_foco = 0; break;
    case A_IN_VEC: g_in_vec = !g_in_vec; break;
    case A_C_NUEVO: entrada(IN_C_NUEVO, 0, "Nuevo cartucho", "", ""); break;
    case A_C_IMPORTA: entrada(IN_IMPORTA, 0, "Importar un cartucho de memorIA en el primario", "/usuario/memoria-001.smem", 0); g_in_vec = 1; break;
    case A_C_PRIM: pide(MEMP_C_PRIM, (uint32_t)z->a, 0, 0, 0); break;
    case A_C_MONTA: pide(MEMP_C_MONTA, (uint32_t)z->a, 0, 0, 0); break;
    case A_C_DESMONTA: pide(MEMP_C_DESMONTA, (uint32_t)z->a, 0, 0, 0); break;
    case A_C_CLONA: { const struct mem_cart *c = cart_de(z->a); char n[60] = "Copia de "; if (c) scat(n, c->nombre, sizeof n); entrada(IN_C_CLONA, (uint32_t)z->a, "Clonar cartucho", n, 0); break; }
    case A_C_EXPORTA: { const struct mem_cart *c = cart_de(z->a); char r[80] = "/usuario/"; if (c) scat(r, c->id, sizeof r); scat(r, ".smem", sizeof r); g_in_vec = 0; entrada(IN_C_EXPORTA, (uint32_t)z->a, "Exportar cartucho", r, 0); break; }
    case A_C_VACIA: { const struct mem_cart *c = cart_de(z->a); char t[200] = "Vaciar el cartucho \""; if (c) scat(t, c->nombre, sizeof t); scat(t, "\"? Se borran todos sus hechos, mensajes y ramas para siempre.", sizeof t); confirma(CF_C_VACIA, (uint32_t)z->a, t); break; }
    case A_C_BORRA: { const struct mem_cart *c = cart_de(z->a); char t[200] = "Borrar el cartucho \""; if (c) scat(t, c->nombre, sizeof t); scat(t, "\" con todo lo que contiene? No se puede deshacer.", sizeof t); confirma(CF_C_BORRA, (uint32_t)z->a, t); break; }
    case A_CFG: memoria_gui_config((const char *)z->f, (int)z->b, 0); aviso("Ajuste guardado en /etc/memoria.conf", 0); break;
    case A_CFG_PASO: memoria_gui_config((const char *)z->f, (int)z->a, 0); break;
    case A_CFG_SRV: memoria_gui_config("emb_url", 0, b_url); memoria_gui_config("emb_modelo", 0, b_mod); aviso("Servidor guardado. Prueba la conexion en Monitor.", 0); g_foco = 0; break;
    case A_BUSCA: { const char *q = b_busca; while (*q == ' ') q++; if (*q) pide(MEMP_BUSCAR, 0, 0, q, 0); else aviso("Escribe algo que buscar.", 1); break; }
    case A_ARCH: scpy(g_sel_path, ARCH[z->a][1], sizeof g_sel_path); break;
    default: break;
    }
}
int memview_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh;
    cambia();
    g_rx = mx; g_ry = my;
    int i = hit_en(mx, my);
    if (i < 0) { if (g_foco && !g_modal) g_foco = 0; return 0; }
    struct hit z = H[i];
    if (z.act != A_FOCO && !g_modal) g_foco = 0;
    if (g_msg[0] && z.act != A_NADA && z.act != A_FOCO) g_msg[0] = 0;
    accion(&z, mx, my);
    if (z.act == A_ARCH) return 4;
    return 0;
}
const char *memview_get_selected_path(void) { return g_sel_path; }
int memview_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held)
{
    int r = 0;
    int dentro = mx >= (int32_t)x && my >= (int32_t)y && mx < (int32_t)(x + w) && my < (int32_t)(y + h);
    if (mx != g_rx || my != g_ry) {
        g_rx = mx; g_ry = my;
        int hv = dentro ? hit_en(mx, my) : -1;
        int clave = hv < 0 ? -1 : (H[hv].act * 7919 + H[hv].a * 31 + H[hv].b) & 0x7FFFFFFF;
        if (clave != g_hover) { g_hover = clave; r = 1; }
    }
    if (held && g_foco && tf_drag(g_foco, mx, my, held)) r = 1;
    if (wheel && dentro && !g_modal) {
        int d = wheel > 0 ? 1 : -1;
        if (g_tab == T_EXP) {
            int lw = g_ww - 32 >= 1000 ? 320 : (g_ww - 32) * 34 / 100;
            if (mx < g_wx + 16 + lw) g_tscroll += d * 3; else g_cscroll += d * 3 * (CARD_H + CARD_G) / 2;
        } else if (g_tab == T_BUS) g_rscroll += d * 92;
        else if (g_tab == T_CAR) g_kscroll += d * 95;
        else if (g_tab == T_ARC) g_ascroll += d * 58;
        r = 1;
    } else if (wheel && g_modal == M_EDIT && g_foco == &f_edit) { f_edit.scroll += wheel > 0 ? 2 : -2; if (f_edit.scroll < 0) f_edit.scroll = 0; r = 1; }
    if (r) cambia();
    return r;
}
void memview_handle_key(char ch)
{
    int c = (uint8_t)ch, ctrl = keyboard_ctrl_held(), shift = keyboard_shift_held();
    cambia();
    if (g_foco) {
        if (g_foco == &f_edit && ctrl && (c == '\r' || c == '\n')) { guarda_editor(); return; }
        int r = tf_key(g_foco, ch, ctrl, shift);
        if (r == TF_ESC) { if (g_modal) g_modal = M_NO; g_foco = 0; return; }
        if (r == TF_ENTER) {
            if (g_foco == &f_busca) { const char *q = b_busca; while (*q == ' ') q++; if (*q) pide(MEMP_BUSCAR, 0, 0, q, 0); }
            else if (g_modal == M_IN) entrada_ok();
            else if (g_modal == M_EDIT) guarda_editor();
            else if (g_foco == &f_desc) { struct hit z; z.act = A_NODO_DESC; z.a = (int32_t)g_nodo; z.b = 0; z.f = 0; accion(&z, 0, 0); }
            else if (g_foco == &f_url || g_foco == &f_mod) { struct hit z; z.act = A_CFG_SRV; z.a = z.b = 0; z.f = 0; accion(&z, 0, 0); }
            return;
        }
        if (r == TF_TAB) {
            if (g_modal == M_EDIT) g_foco = g_foco == &f_edit && g_ed_kind == MC_CELDA ? &f_ruta : &f_edit;
            else if (g_modal == M_IN && g_in == IN_C_NUEVO) g_foco = g_foco == &f_nom ? &f_nom2 : &f_nom;
            else if (g_foco == &f_url) g_foco = &f_mod; else if (g_foco == &f_mod) g_foco = &f_url;
        }
        return;
    }
    if (c == 27) { if (g_modal) { g_modal = M_NO; return; } return; }
    if (g_modal) { if ((c == '\r' || c == '\n') && g_modal == M_CONF) { struct hit z; z.act = A_CONF_SI; z.a = z.b = 0; z.f = 0; accion(&z, 0, 0); } return; }
    if (c >= '1' && c <= '6') { g_tab = c - '1'; if (g_tab == T_BUS) g_foco = &f_busca; return; }
    if (c == '\t') { g_tab = (g_tab + (shift ? T_N - 1 : 1)) % T_N; return; }
    if (c == '/') { g_tab = T_BUS; g_foco = &f_busca; return; }
    if (c == 16 || c == 14) {                                       /* flechas: desplazar */
        int d = c == 14 ? 1 : -1;
        if (g_tab == T_EXP) g_cscroll += d * (CARD_H + CARD_G); else if (g_tab == T_BUS) g_rscroll += d * 92; else if (g_tab == T_CAR) g_kscroll += d * 95; else if (g_tab == T_ARC) g_ascroll += d * 58;
        return;
    }
    if (g_tab == T_EXP && (c == 'n' || c == 'N')) { abre_editor(0, g_vista == V_NODO ? g_nodo : 0); return; }
}
int memview_clip_op(int op)
{
    if (!g_foco) return 0;
    tf_clip(g_foco, op);
    cambia();
    return 1;
}
