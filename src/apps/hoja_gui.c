/* SOMA_359_HOJA_VENTANA: ver hoja_gui.h. Las zonas pulsables se anotan al pintar (g_hit) y el clic las lee (la ultima gana).
 * La cuadricula se pinta con lo visible: columnas y filas inmovilizadas primero y luego desde la primera desplazada. */
#include <stdint.h>
#include <stddef.h>
#include "hoja_gui.h"
#include "hoja.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "textfield.h"
#include "clipboard.h"
#include "mem.h"
#include "idt.h"
#include "fs.h"
#include "hoja_ia.h"                                             /* SOMA_360_HOJA_GRAFICOS */
#include "hoja_integra.h"

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);

/* colores: marco oscuro como el resto de SOMA; la hoja, en papel blanco (los colores de los archivos cuentan con eso) */
#define K_WIN   0x000C111Bu
#define K_BAR   0x00131A27u
#define K_LINE  0x00212B3Bu
#define K_CARD  0x00151E2Eu
#define K_TXT   0x00E5EAF2u
#define K_DIM   0x008592A8u
#define K_FAINT 0x00566379u
#define K_ACC   0x0022C55Eu          /* el color del icono */
#define K_ACC2  0x0038BDF8u
#define K_SELB  0x001A3558u
#define K_RED   0x00EF4444u
#define P_BG    0x00FFFFFFu
#define P_GRID  0x00E2E5EAu
#define P_HEAD  0x00F1F3F6u
#define P_HSEL  0x00D3F0DEu
#define P_HTXT  0x00596372u
#define P_TXT   0x00141414u
#define P_FROZ  0x00B9C0CAu

#define TB_H   46
#define FB_H   36
#define CH_H   22
#define TAB_H  30
#define MAXVC  256
#define MAXVR  256
#define NPASOS 80
#define PASOS_BYTES (48u << 20)

enum { H_NADA, H_BOTON, H_MENU_IT, H_SWATCH, H_NOMBRE, H_FX, H_FORM, H_CELDA, H_COLH, H_FILH, H_ESQ, H_ANCHO, H_ALTO, H_RELLENO, H_FILTRO,
       H_PEST, H_PEST_MAS, H_PEST_IZQ, H_PEST_DER, H_BARRA, H_BARRA2, H_BARRA_OK, H_BARRA_NO, H_BARRA_ALT, H_SUG, H_SUGF, H_VBAR, H_HBAR, H_GRID, H_GRAF, H_GRAF_TAM };
struct hit { int x, y, w, h, tipo, val; };
#define MAXH 400
static struct hit g_hit[MAXH]; static int g_nh;
static void hit(int x, int y, int w, int h, int tipo, int val) { if (g_nh < MAXH) { struct hit *t = &g_hit[g_nh++]; t->x = x; t->y = y; t->w = w; t->h = h; t->tipo = tipo; t->val = val; } }

/* botones de la barra de herramientas (val de H_BOTON) */
enum { B_ARCHIVO = 1, B_DESHACER, B_REHACER, B_NEGRITA, B_CURSIVA, B_SUBRAYADO, B_TACHADO, B_IZQ, B_CENTRO, B_DER, B_FORMATO, B_MENOSDEC, B_MASDEC,
       B_MONEDA, B_PORCIENTO, B_COLOR, B_FONDO, B_BORDES, B_AJUSTAR, B_FILAS, B_DATOS, B_SUMA, B_BUSCAR, B_GUARDAR, B_MAS, B_GRAFICO, B_IA };
static int g_tb_oculto[32], g_ntb_oculto;   /* botones que no caben: van al menu >> */
static const char *nombre_boton(int b);
/* menus */
enum { M_NADA, M_ARCHIVO, M_FORMATO, M_COLOR, M_FONDO, M_BORDES, M_FILAS, M_DATOS, M_FX, M_CELDA, M_PEST, M_MAS, M_GRAF, M_GRAF_NUEVO };
/* acciones de los menus */
enum { A_NUEVO = 1, A_ABRIR, A_GUARDAR, A_GUARDAR_COMO, A_EXP_XLSX, A_EXP_ODS, A_EXP_CSV, A_CERRAR, A_LIBRO0 = 20,
       A_NF0 = 40, A_NF_CODIGO = 59,
       A_BORDE_TODOS = 60, A_BORDE_FUERA, A_BORDE_ABAJO, A_BORDE_NINGUNO,
       A_INS_FILA = 70, A_INS_COL, A_BOR_FILA, A_BOR_COL, A_ANCHO_AUTO, A_ALTO_NORMAL, A_BORRAR_TODO, A_BORRAR_FORMATO,
       A_ORD_AZ = 90, A_ORD_ZA, A_FILTRO, A_QUITA_FILTRO, A_RELL_ABAJO, A_RELL_DER, A_INMOV, A_BUSCAR, A_IR,
       A_CORTAR = 110, A_COPIAR, A_PEGAR, A_PEGAR_VAL,
       A_PEST_REN = 120, A_PEST_IZQ, A_PEST_DER, A_PEST_BORRAR, A_PEST_NUEVA,
       A_FX0 = 140, A_BOTON0 = 200, A_GNUEVO0 = 280, A_GTIPO0 = 290, A_GTITULO = 300, A_GLEYENDA, A_GDATOS, A_GBORRA, A_CORREO = 310, A_DESDE_CT, A_DESDE_AG, A_IA_FORMULA, A_IA_TABLA, A_IA_COPIA, A_IA_CIERRA };
/* barra de abajo */
enum { BA_NADA, BA_ABRIR, BA_GUARDAR, BA_BUSCAR, BA_RENOMBRAR, BA_IR, BA_FILTRO, BA_CODIGO, BA_TITULO, BA_IA };
static int g_pide_app;                  /* SOMA_360: 5 = abrir el Correo (con la hoja adjunta) */
static uint32_t g_ia_ver_vista;
static void contexto_ia(char *o, uint32_t cap);
static void pinta_ia(int x, int y, int w, int h);
enum { F_NADA, F_FORM, F_BARRA, F_BARRA2 };

struct vista { uint32_t af, ac, sf, sc, top, left; };
static struct vista g_vis[HOJA_LIBROS][HJ_HOJAS];

static int g_init, g_doc = -1;
static uint32_t g_ver = 1, g_ver_hoja;
static char g_msg[240]; static uint64_t g_msg_t; static int g_msg_err;
static int g_foco;
static int g_edit;                   /* 0 no; 1 entrada (se escribio encima: las flechas confirman); 2 editar (las flechas mueven el cursor) */
static int g_edit_hoja;              /* hoja de la celda que se edita (al elegir referencias de otra hoja) */
static uint32_t g_edit_f, g_edit_c;
static struct tfield g_tf, g_tbarra, g_tbarra2;
static char g_bform[HJ_FORM_MAX + 64], g_bbarra[400], g_bbarra2[400];
static int g_barra; static uint32_t g_barra_col;
static char g_sug[12][160]; static int g_nsug;
static char g_fsug[8][40]; static int g_nfsug;     /* funciones que empiezan por lo escrito */
static int g_menu, g_menu_x, g_menu_y;
struct mitem { char t[64]; int id; int on; };
static struct mitem g_mi[40]; static int g_nmi;
static int g_zoom = 100;
static int g_cerrar_armado;
static int g_tab_off;
static uint32_t g_ultimo_color = 0x01DC2626u, g_ultimo_fondo = 0x01FEF08Au;
/* arrastres */
enum { AR_NADA, AR_SEL, AR_RELLENO, AR_ANCHO, AR_ALTO, AR_VBAR, AR_HBAR, AR_REF, AR_TF, AR_GMUEVE, AR_GTAM };
static int g_ar_v1, g_ar_movido;
static int g_gsel = -1;                 /* SOMA_360_HOJA_GRAFICOS: grafico elegido en la hoja activa */
static int g_ar; static uint32_t g_ar_i; static int g_ar_x0, g_ar_y0, g_ar_v0; static uint32_t g_ar_f, g_ar_c;
static int g_held_ant; static uint32_t g_ar_ref_f, g_ar_ref_c;
static uint64_t g_ult_clic; static int g_ult_clic_tipo; static uint32_t g_ult_clic_val;
/* referencia que se esta eligiendo con el raton dentro de una formula */
static uint32_t g_ref_pos, g_ref_len;
/* geometria del ultimo pintado */
static int g_gx, g_gy, g_gw, g_gh, g_rhw, g_wx, g_wy, g_ww, g_wh;
struct vcol { uint32_t i; int x, w; };
static struct vcol g_vc[MAXVC], g_vr[MAXVR]; static int g_nvc, g_nvr;
static int g_vis_f, g_vis_c;                         /* filas y columnas desplazables que caben enteras */
/* copiar dentro de la hoja (con formulas): vale mientras el portapapeles no cambie */
static struct { int doc, hoja, cortar; uint32_t f1, c1, f2, c2, seq; } g_copia = { -1, 0, 0, 0, 0, 0, 0, 0 };
/* deshacer */
struct paso { int tipo; int doc; struct hj_trozo *t; char *libro; uint32_t n; uint32_t bytes; };
static struct paso g_un[NPASOS], g_re[NPASOS]; static int g_nun, g_nre;
static uint32_t g_pasos_bytes;
/* estado de la suma de la seleccion */
static uint32_t g_st_ver = 0xFFFFFFFFu, g_st_key; static char g_st[160];

uint32_t hoja_gui_version(void) { return g_ver + hoja_version() * 7u; }

/* ---------------------------------------------------------------- texto */
static int sl(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void cat(char *d, const char *s, int max) { int i = sl(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void cpy(char *d, const char *s, int max) { if (max <= 0) return; d[0] = 0; cat(d, s, max); }
static void num(char *d, int max, int64_t v) { char t[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) cat(d, "-", max); do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); char r[24]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; cat(d, r, max); }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int termina(const char *s, const char *suf) { int a = sl(s), b = sl(suf); if (b > a) return 0; for (int i = 0; i < b; ++i) if (baja(s[a - b + i]) != suf[i]) return 0; return 1; }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int fl) { int n = sl(s); return has_wf() ? wf_width(s, (uint32_t)n, px, fl) : n * 8; }
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int fl)
{
    int n = sl(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, fl, 255); return; }
    font_draw_string((uint32_t)(x < 0 ? 0 : x), (uint32_t)(top + (h - 10) / 2 < 0 ? 0 : top + (h - 10) / 2), s, col, 1);
}
static void txc(int x, int y, int w, int h, const char *s, int px, uint32_t col, int fl) { if (w <= 0 || h <= 0) return; fb_set_clip(x, y, w, h); tx(x, y, h, s, px, col, fl); fb_reset_clip(); }
static void rect(int x, int y, int w, int h, uint32_t c) { if (w > 0 && h > 0 && x >= 0 && y >= 0) fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c); }
static void avisa_t(const char *m, int err) { cpy(g_msg, m, sizeof g_msg); for (char *z = g_msg; *z; ++z) if (*z == '\n') *z = ' '; g_msg_t = timer_get_uptime_ms(); g_msg_err = err; ++g_ver; }
static void avisa(const char *m) { avisa_t(m, 0); }
static void error(const char *m) { avisa_t(m, 1); }

/* ---------------------------------------------------------------- libro y vista */
static int nuevo_nombre(char *out, int cap)
{
    for (int k = 1; k < 100; ++k) {
        char n[24]; n[0] = 0; cat(n, "Libro", sizeof n); num(n, sizeof n, k);
        char r[64]; cpy(r, "/usuario/", sizeof r); cat(r, n, sizeof r); cat(r, ".xlsx", sizeof r);
        int usado = vfs_size(r) >= 0;
        for (int i = 0; i < HOJA_LIBROS && !usado; ++i) if (hoja_libro(i)) { const char *a = hoja_ruta(i), *b = r; while (*a && *a == *b) { ++a; ++b; } if (!*a && !*b) usado = 1; }
        if (!usado) { cpy(out, n, cap); return 0; }
    }
    cpy(out, "Libro", cap); return 0;
}
static void limpia_pasos(void);
static int asegura_libro(void)
{
    int i = hoja_actual();
    if (i < 0) { char n[24]; nuevo_nombre(n, sizeof n); i = hoja_nuevo(n); }
    if (i != g_doc) { g_doc = i; limpia_pasos(); g_edit = 0; if (g_foco == F_FORM) g_foco = F_NADA; }
    return i;
}
static struct hj_libro *L(void) { return hoja_libro(g_doc); }
static int H(void) { struct hj_libro *l = L(); return l ? l->activa : 0; }
static struct vista *V(void) { int d = g_doc >= 0 && g_doc < HOJA_LIBROS ? g_doc : 0, h = H(); return &g_vis[d][h >= 0 && h < HJ_HOJAS ? h : 0]; }
static struct hj_hoja *S(void) { struct hj_libro *l = L(); return l ? &l->h[H()] : 0; }
static void cambio(void) { hoja_toca(g_doc); ++g_ver; }
static void sel_rango(uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2)
{
    struct vista *v = V();
    *f1 = v->af < v->sf ? v->af : v->sf; *f2 = v->af < v->sf ? v->sf : v->af;
    *c1 = v->ac < v->sc ? v->ac : v->sc; *c2 = v->ac < v->sc ? v->sc : v->ac;
}
/* la seleccion recortada a lo usado (una columna entera son 1.048.576 filas) */
static void sel_util(uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2)
{
    sel_rango(f1, c1, f2, c2); struct hj_hoja *s = S(); if (!s) return;
    if (*f2 - *f1 > 60000) { uint32_t m = s->max_fila ? s->max_fila - 1 : 0; if (m < *f1) m = *f1; if (*f2 > m) *f2 = m; }
    if (*c2 - *c1 > 1000) { uint32_t m = s->max_col ? s->max_col - 1 : 0; if (m < *c1) m = *c1; if (*c2 > m) *c2 = m; }
}
static int sel_es_una(void) { struct vista *v = V(); return v->af == v->sf && v->ac == v->sc; }

/* ---------------------------------------------------------------- deshacer */
static void paso_libera(struct paso *p) { if (p->t) hj_suelta(p->t); if (p->libro) kfree(p->libro); g_pasos_bytes -= p->bytes < g_pasos_bytes ? p->bytes : g_pasos_bytes; p->t = 0; p->libro = 0; p->bytes = 0; }
static void limpia_pasos(void) { for (int i = 0; i < g_nun; ++i) paso_libera(&g_un[i]); for (int i = 0; i < g_nre; ++i) paso_libera(&g_re[i]); g_nun = g_nre = 0; }
static void pila_push(struct paso *pila, int *n, struct paso *p)
{
    if (*n >= NPASOS) { paso_libera(&pila[0]); for (int i = 1; i < *n; ++i) pila[i - 1] = pila[i]; --*n; }
    pila[(*n)++] = *p;
    while (g_pasos_bytes > PASOS_BYTES && g_nun > 1) { paso_libera(&g_un[0]); for (int i = 1; i < g_nun; ++i) g_un[i - 1] = g_un[i]; --g_nun; }
}
static void redo_limpia(void) { for (int i = 0; i < g_nre; ++i) paso_libera(&g_re[i]); g_nre = 0; }
/* antes de cambiar un rango: guarda como estaba */
static int paso_rango(uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    struct hj_libro *l = L(); if (!l) return -1;
    struct paso p = { 1, g_doc, 0, 0, 0, 0 };
    p.t = hj_toma(l, l->activa, f1, c1, f2, c2);
    if (!p.t) { error("Sin memoria para poder deshacer este cambio."); return -1; }
    p.bytes = hj_trozo_bytes(p.t); g_pasos_bytes += p.bytes;
    redo_limpia(); pila_push(g_un, &g_nun, &p); return 0;
}
static char *libro_texto(uint32_t *n)
{
    struct hj_libro *l = L(); *n = 0; if (!l) return 0;
    uint32_t cap = hj_serializa_tam(l); if (cap > (24u << 20)) return 0;
    char *b = (char *)kmalloc(cap); if (!b) return 0;
    *n = hj_serializa(l, b, cap); if (!*n) { kfree(b); return 0; }
    return b;
}
/* antes de un cambio grande (filas, columnas, hojas, ordenar, filtrar): el libro entero */
static int paso_libro(void)
{
    struct paso p = { 2, g_doc, 0, 0, 0, 0 };
    p.libro = libro_texto(&p.n);
    if (!p.libro) { error("El libro es muy grande: este cambio no se podr\303\241 deshacer."); redo_limpia(); return -1; }
    p.bytes = p.n; g_pasos_bytes += p.bytes;
    redo_limpia(); pila_push(g_un, &g_nun, &p); return 0;
}
static void paso_quita_ultimo(void) { if (g_nun) paso_libera(&g_un[--g_nun]); }
static int libro_repone(const char *txt, uint32_t n)
{
    struct hj_libro *nl = (struct hj_libro *)kmalloc(sizeof *nl); if (!nl) return -1;
    if (hj_carga(nl, txt, n)) { kfree(nl); return -1; }
    struct hj_libro *l = L(); uint32_t ver = l->version;
    hj_libro_libera(l); memcpy(l, nl, sizeof *l); kfree(nl);
    l->version = ver + 1;
    return 0;
}
static void deshace(int rehacer)
{
    struct paso *de = rehacer ? g_re : g_un, *a = rehacer ? g_un : g_re; int *nde = rehacer ? &g_nre : &g_nun, *na = rehacer ? &g_nun : &g_nre;
    if (!*nde) { avisa(rehacer ? "No hay nada que rehacer." : "No hay nada que deshacer."); return; }
    struct paso p = de[--*nde]; struct hj_libro *l = L();
    if (!l || p.doc != g_doc) { paso_libera(&p); return; }
    struct paso q = { p.tipo, g_doc, 0, 0, 0, 0 };
    if (p.tipo == 1) {
        int h = l->activa; (void)h;
        /* el estado de ahora, para el paso contrario */
        uint32_t f1, c1, f2, c2; hj_trozo_rect(p.t, &h, &f1, &c1, &f2, &c2);
        if (h >= l->nh) { paso_libera(&p); return; }
        q.t = hj_toma(l, h, f1, c1, f2, c2);
        hj_deja(l, p.t);
        l->activa = h; struct vista *v = V(); v->af = f1; v->ac = c1; v->sf = f2; v->sc = c2;
        if (q.t) q.bytes = hj_trozo_bytes(q.t);
    } else {
        q.libro = libro_texto(&q.n); q.bytes = q.n;
        if (libro_repone(p.libro, p.n)) { if (q.libro) kfree(q.libro); paso_libera(&p); error("No se pudo deshacer (sin memoria)."); return; }
    }
    paso_libera(&p);
    g_pasos_bytes += q.bytes;
    if (q.t || q.libro) pila_push(a, na, &q);
    cambio();
}

/* ---------------------------------------------------------------- geometria */
static int Z(int px) { int v = px * g_zoom / 100; return v < 1 ? 1 : v; }
static int ancho_col(uint32_t c) { struct hj_hoja *s = S(); return s ? Z(hj_ancho(s, c)) : Z(HJ_ANCHO_DEF); }
static int alto_fila(uint32_t f) { struct hj_hoja *s = S(); return s ? Z(hj_alto(s, f)) : Z(HJ_ALTO_DEF); }
static int oculta(uint32_t f) { struct hj_hoja *s = S(); return s && hj_fila_oculta(s, f); }
/* la siguiente fila visible en esa direccion (o la misma si no hay) */
static uint32_t fila_mueve(uint32_t f, int d)
{
    uint32_t r = f;
    for (int k = 0; k < (d < 0 ? -d : d); ++k) {
        uint32_t g = r;
        do { if (d < 0) { if (!g) break; --g; } else { if (g + 1 >= HJ_FILAS) break; ++g; } } while (oculta(g));
        if (oculta(g)) break;
        r = g;
    }
    return r;
}
static void a_la_vista_de(uint32_t af, uint32_t ac)
{
    struct vista *v = V(); struct hj_hoja *s = S(); if (!s) return;
    uint32_t inf = s->inmov_f, inc = s->inmov_c;
    if (v->top < inf) v->top = inf;
    if (v->left < inc) v->left = inc;
    int hfz = 0; for (uint32_t f = 0; f < inf; ++f) if (!oculta(f)) hfz += alto_fila(f);
    int wfz = 0; for (uint32_t c = 0; c < inc; ++c) wfz += ancho_col(c);
    int ah = g_gh - hfz, aw = g_gw - wfz; if (ah < 40) ah = 40; if (aw < 60) aw = 60;
    if (af >= inf) {
        if (af < v->top) v->top = af;
        else { int acc = 0; uint32_t f = af; acc = alto_fila(f); uint32_t t = f;
            while (t > v->top) { uint32_t g = t - 1; if (!oculta(g)) { if (acc + alto_fila(g) > ah) break; acc += alto_fila(g); } t = g; }
            if (t > v->top) v->top = t; }
    }
    if (ac >= inc) {
        if (ac < v->left) v->left = ac;
        else { int acc = ancho_col(ac); uint32_t t = ac;
            while (t > v->left) { if (acc + ancho_col(t - 1) > aw) break; acc += ancho_col(t - 1); --t; }
            if (t > v->left) v->left = t; }
    }
    while (v->top < HJ_FILAS - 1 && oculta(v->top)) ++v->top;
}
static void a_la_vista(void) { struct vista *v = V(); a_la_vista_de(v->af, v->ac); }
static int busca_vc(uint32_t c) { for (int i = 0; i < g_nvc; ++i) if (g_vc[i].i == c) return i; return -1; }
static int busca_vr(uint32_t f) { for (int i = 0; i < g_nvr; ++i) if (g_vr[i].i == f) return i; return -1; }
/* celda bajo el raton (con el raton fuera de la cuadricula, la del borde: para arrastrar) */
static void celda_en(int mx, int my, uint32_t *f, uint32_t *c)
{
    *f = g_nvr ? g_vr[g_nvr - 1].i : 0; *c = g_nvc ? g_vc[g_nvc - 1].i : 0;
    if (g_nvr && my < g_vr[0].x) *f = g_vr[0].i;
    for (int i = 0; i < g_nvr; ++i) if (my >= g_vr[i].x && my < g_vr[i].x + g_vr[i].w) { *f = g_vr[i].i; break; }
    if (g_nvc && mx < g_vc[0].x) *c = g_vc[0].i;
    for (int i = 0; i < g_nvc; ++i) if (mx >= g_vc[i].x && mx < g_vc[i].x + g_vc[i].w) { *c = g_vc[i].i; break; }
}

/* ---------------------------------------------------------------- edicion */
static void ref_texto(uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, char *o, int cap)
{
    o[0] = 0; struct hj_libro *l = L();
    if (l && l->activa != g_edit_hoja) {
        const char *n = l->h[l->activa].nombre; int raro = 0; for (const char *z = n; *z; ++z) if (!((*z >= 'A' && *z <= 'Z') || (*z >= 'a' && *z <= 'z') || (*z >= '0' && *z <= '9') || *z == '_' || (uint8_t)*z >= 0x80)) raro = 1;
        if (raro) cat(o, "'", cap);
        cat(o, n, cap);
        if (raro) cat(o, "'", cap);
        cat(o, "!", cap);
    }
    char d[16]; hj_dir(f1, c1, d); cat(o, d, cap);
    if (f2 != f1 || c2 != c1) { hj_dir(f2, c2, d); cat(o, ":", cap); cat(o, d, cap); }
}
static void sugerencias_fx(void);
static void empieza_edicion(int modo, const char *con)
{
    struct vista *v = V(); struct hj_libro *l = L(); if (!l) return;
    g_edit = modo; g_edit_hoja = l->activa; g_edit_f = v->af; g_edit_c = v->ac; g_foco = F_FORM; g_ref_len = 0; g_nfsug = 0;
    tf_init(&g_tf, g_bform, sizeof g_bform, 0);
    if (con) tf_set(&g_tf, con);
    else { static char b[HJ_FORM_MAX + 64]; hj_texto_edicion(l, l->activa, v->af, v->ac, b, sizeof b); tf_set(&g_tf, b); }
    ++g_ver;
}
static void cancela_edicion(void)
{
    struct hj_libro *l = L();
    if (l && g_edit && l->activa != g_edit_hoja && g_edit_hoja < l->nh) l->activa = g_edit_hoja;
    g_edit = 0; g_foco = F_NADA; g_nfsug = 0; g_ref_len = 0; ++g_ver;
}
/* 0 = escrito; -1 = la formula no se entiende (se sigue editando) */
static int confirma(void)
{
    struct hj_libro *l = L(); if (!l || !g_edit) return 0;
    if (g_edit_hoja < l->nh) l->activa = g_edit_hoja;
    struct vista *v = V(); v->af = g_edit_f; v->ac = g_edit_c;
    const char *t = g_bform; while (*t == ' ' && t[1] == '=') ++t;
    if (paso_rango(g_edit_f, g_edit_c, g_edit_f, g_edit_c)) return 0;
    int e = 0, r;
    if (!t[0]) { hj_borra_celda(l, l->activa, g_edit_f, g_edit_c); r = 0; }
    else r = hj_escribe(l, l->activa, g_edit_f, g_edit_c, t, &e);
    if (r == -1) {
        paso_quita_ultimo();
        char m[200]; cpy(m, "No entiendo la f\303\263rmula", sizeof m);
        if (e > 0) { cat(m, " (cerca de la posici\303\263n ", sizeof m); num(m, sizeof m, e); cat(m, ")", sizeof m); }
        cat(m, ": revisa los par\303\251ntesis y que los argumentos vayan separados por ';'.", sizeof m);
        error(m); return -1;
    }
    if (r) { paso_quita_ultimo(); error("Sin memoria (o la hoja ya tiene 400.000 celdas)."); g_edit = 0; g_foco = F_NADA; return 0; }
    g_edit = 0; g_foco = F_NADA; g_nfsug = 0; g_ref_len = 0;
    v->sf = v->af; v->sc = v->ac;
    cambio();
    if (l->sin_memoria) error("Sin memoria: parte del libro no se pudo calcular.");
    return 0;
}
static int es_formula(void) { return g_edit && g_bform[0] == '='; }
/* se puede poner una referencia en el cursor (tras '=', '(', ';', un operador...) */
static int pide_ref(void)
{
    if (!es_formula()) return 0;
    if (g_ref_len && g_tf.cur == g_ref_pos + g_ref_len) return 1;
    uint32_t p = g_tf.cur; while (p > 0 && g_bform[p - 1] == ' ') --p;
    if (!p) return 0;
    char c = g_bform[p - 1];
    return c == '=' || c == '(' || c == ';' || c == ',' || c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '&' || c == '<' || c == '>' || c == ':';
}
static void pon_ref(uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    char r[96]; ref_texto(f1 < f2 ? f1 : f2, c1 < c2 ? c1 : c2, f1 < f2 ? f2 : f1, c1 < c2 ? c2 : c1, r, sizeof r);
    if (g_ref_len && g_tf.cur == g_ref_pos + g_ref_len) { g_tf.anc = g_ref_pos; }
    else { g_ref_pos = g_tf.cur; g_tf.anc = g_tf.cur; }
    uint32_t a = g_tf.anc < g_tf.cur ? g_tf.anc : g_tf.cur, b = g_tf.anc < g_tf.cur ? g_tf.cur : g_tf.anc;
    uint32_t n = (uint32_t)sl(g_bform), k = (uint32_t)sl(r);
    if (n - (b - a) + k + 1 >= sizeof g_bform) return;
    for (uint32_t i = b; i <= n; ++i) g_bform[a + k + (i - b)] = g_bform[i];
    for (uint32_t i = 0; i < k; ++i) g_bform[a + i] = r[i];
    g_ref_pos = a; g_ref_len = k; g_tf.cur = g_tf.anc = a + k; ++g_ver;
}
/* funciones que empiezan por la palabra antes del cursor */
static void sugerencias_fx(void)
{
    g_nfsug = 0; if (!es_formula()) return;
    uint32_t p = g_tf.cur, i = p;
    while (i > 0 && ((g_bform[i - 1] >= 'A' && g_bform[i - 1] <= 'Z') || (g_bform[i - 1] >= 'a' && g_bform[i - 1] <= 'z') || g_bform[i - 1] == '.' || (uint8_t)g_bform[i - 1] >= 0x80 || (g_bform[i - 1] >= '0' && g_bform[i - 1] <= '9'))) --i;
    if (i == p || !((g_bform[i] >= 'A' && g_bform[i] <= 'Z') || (g_bform[i] >= 'a' && g_bform[i] <= 'z'))) return;
    if (i > 0 && (g_bform[i - 1] == '"' || g_bform[i - 1] == '!' || g_bform[i - 1] == '$')) return;
    { int q = 0; for (uint32_t k = 0; k < i; ++k) if (g_bform[k] == '"') q = !q; if (q) return; }
    char pre[40]; uint32_t np = p - i; if (np >= sizeof pre) return;
    for (uint32_t k = 0; k < np; ++k) pre[k] = g_bform[i + k];
    pre[np] = 0;
    static char lista[6000]; hj_lista_funciones(lista, sizeof lista, 0);
    for (const char *s = lista; *s && g_nfsug < 8; ) {
        const char *e = s; while (*e && *e != ',') ++e;
        uint32_t L2 = (uint32_t)(e - s);
        if (L2 >= np && L2 < 40) { int ok = 1; for (uint32_t k = 0; k < np; ++k) if (baja(s[k]) != baja(pre[k])) { ok = 0; break; } if (ok) { for (uint32_t k = 0; k < L2; ++k) g_fsug[g_nfsug][k] = s[k]; g_fsug[g_nfsug][L2] = 0; ++g_nfsug; } }
        s = *e ? e + 1 : e; while (*s == ' ') ++s;
    }
    if (g_nfsug == 1 && sl(g_fsug[0]) == (int)np) g_nfsug = 0;
}
static void acepta_fx(int k)
{
    if (k < 0 || k >= g_nfsug) return;
    uint32_t p = g_tf.cur, i = p;
    while (i > 0 && ((g_bform[i - 1] >= 'A' && g_bform[i - 1] <= 'Z') || (g_bform[i - 1] >= 'a' && g_bform[i - 1] <= 'z') || g_bform[i - 1] == '.' || (uint8_t)g_bform[i - 1] >= 0x80 || (g_bform[i - 1] >= '0' && g_bform[i - 1] <= '9'))) --i;
    g_tf.anc = i; g_tf.cur = p;
    char t[48]; cpy(t, g_fsug[k], sizeof t); cat(t, "(", sizeof t);
    tf_insert(&g_tf, t, (uint32_t)sl(t)); g_nfsug = 0; ++g_ver;
}

/* ---------------------------------------------------------------- estilos de la seleccion */
static const struct hj_estilo *estilo_activo(void)
{
    struct hj_libro *l = L(); struct vista *v = V(); const struct hj_celda *x = l ? hj_celda_c(l, l->activa, v->af, v->ac) : 0;
    return hj_estilo_de(l, x ? x->estilo : 0);
}
typedef void (*cambia_fn)(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2);
static void aplica(cambia_fn fn, uint32_t a)
{
    struct hj_libro *l = L(); if (!l) return;
    uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2);
    if ((uint64_t)(f2 - f1 + 1) * (uint64_t)(c2 - c1 + 1) > 200000u) { error("Selecci\303\263n demasiado grande para darle formato (200.000 celdas como mucho)."); return; }
    if (paso_rango(f1, c1, f2, c2)) return;
    int h = l->activa;
    for (uint32_t f = f1; f <= f2; ++f) for (uint32_t c = c1; c <= c2; ++c) {
        const struct hj_celda *x = hj_celda_c(l, h, f, c);
        struct hj_estilo e = *hj_estilo_de(l, x ? x->estilo : 0);
        fn(&e, a, f, c, f1, c1, f2, c2);
        uint32_t id = hj_estilo_id(l, &e);
        if (!id && (x ? x->estilo : 0) == 0) { struct hj_estilo z = *hj_estilo_de(l, 0); int igual = 1; const uint8_t *p = (const uint8_t *)&z, *q = (const uint8_t *)&e; for (uint32_t k = 0; k < sizeof z; ++k) if (p[k] != q[k]) { igual = 0; break; } if (!igual) { error("Hay demasiados estilos distintos en el libro (4.096)."); goto fin; } }
        if ((x ? x->estilo : 0) != id) (void)hj_pon_estilo(l, h, f, c, (uint16_t)id);
    }
fin:
    cambio();
}
static void fn_negrita(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->negrita = (uint8_t)a; }
static void fn_cursiva(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->cursiva = (uint8_t)a; }
static void fn_subr(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->subrayado = (uint8_t)a; }
static void fn_tach(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->tachado = (uint8_t)a; }
static void fn_alin(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->alinear = (uint8_t)a; }
static void fn_ajus(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->ajustar = (uint8_t)a; }
static void fn_color(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->color = a; }
static void fn_fondo(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2) { (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2; e->fondo = a; }
static void fn_nf(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2;
    e->nf = (uint8_t)a; e->codigo[0] = 0;
    if (a == NF_NUMERO || a == NF_MONEDA) { e->decimales = 2; e->miles = 1; }
    else if (a == NF_PORCENTAJE) { e->decimales = 0; e->miles = 0; }
    else if (a == NF_CIENTIFICA) { e->decimales = 2; e->miles = 0; }
    else { e->decimales = 0; e->miles = 0; }
}
static void fn_dec(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2;
    uint8_t nf, dec, mi; hj_tipo_numero(e, &nf, &dec, &mi);
    if (nf == NF_GENERAL || nf == NF_CODIGO) { nf = NF_NUMERO; dec = 0; mi = 0; }
    if (nf == NF_FECHA || nf == NF_HORA || nf == NF_FECHAHORA || nf == NF_TEXTO) return;
    if (a) { if (dec < 10) ++dec; } else if (dec) --dec;
    e->nf = nf; e->decimales = dec; e->miles = mi; e->codigo[0] = 0;
}
static void fn_codigo(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    (void)a; (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2;
    e->nf = NF_CODIGO; cpy(e->codigo, g_bbarra, HJ_NFCOD);
}
static void fn_bordes(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    if (a == 0) e->bordes = 15;
    else if (a == 1) { uint8_t b = e->bordes; if (f == f1) b |= 1; if (f == f2) b |= 2; if (c == c1) b |= 4; if (c == c2) b |= 8; e->bordes = b; }
    else if (a == 2) { if (f == f2) e->bordes |= 2; }
    else e->bordes = 0;
}
static void fn_limpia(struct hj_estilo *e, uint32_t a, uint32_t f, uint32_t c, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    (void)a; (void)f; (void)c; (void)f1; (void)c1; (void)f2; (void)c2;
    struct hj_libro *l = L(); *e = *hj_estilo_de(l, 0);
}

/* ---------------------------------------------------------------- portapapeles */
static void sel_a_texto(char *o, uint32_t cap, uint32_t *n, int *cortado)
{
    struct hj_libro *l = L(); uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); *n = 0; *cortado = 0; if (!cap) return; o[0] = 0;
    static char b[1024];
    for (uint32_t f = f1; f <= f2; ++f) {
        if (oculta(f)) continue;
        for (uint32_t c = c1; c <= c2; ++c) {
            hj_texto_ver(l, l->activa, f, c, b, sizeof b);
            uint32_t k = (uint32_t)sl(b);
            if (*n + k + 3 >= cap) { *cortado = 1; o[*n] = 0; return; }
            for (uint32_t i = 0; i < k; ++i) o[(*n)++] = (b[i] == '\t' || b[i] == '\n' || b[i] == '\r') ? ' ' : b[i];
            if (c < c2) o[(*n)++] = '\t';
        }
        o[(*n)++] = '\n';
    }
    o[*n] = 0;
}
static void copia(int cortar)
{
    struct hj_libro *l = L(); if (!l) return;
    uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2);
    char *b = (char *)kmalloc(CLIP_TEXT_MAX); if (!b) { error("Sin memoria."); return; }
    uint32_t n; int cortado; sel_a_texto(b, CLIP_TEXT_MAX, &n, &cortado);
    clip_set_text(b, n ? n : 1); kfree(b);
    g_copia.doc = g_doc; g_copia.hoja = l->activa; g_copia.cortar = cortar; g_copia.f1 = f1; g_copia.c1 = c1; g_copia.f2 = f2; g_copia.c2 = c2; g_copia.seq = clip_seq();
    avisa(cortado ? "Copiado (para otras apps, solo los primeros 64 KB; aqu\303\255 se pega entero)." : cortar ? "Cortado: p\303\251galo donde quieras." : "Copiado.");
}
/* una tabla que viene de fuera (web, correo, otra app): tabuladores, tabla markdown, ';' o columnas separadas por espacios */
static int separador(const char *t, uint32_t n, int *md)
{
    *md = 0; int lineas = 0, pc_fija = -1, pc_ok = 1, tabs = 0, pipes = 0, esp_ok = 1;
    for (uint32_t i = 0; i < n; ) {
        uint32_t j = i; int k = 0, e2 = 0;
        while (j < n && t[j] != '\n') { if (t[j] == '\t') ++tabs; if (t[j] == ';') ++k; if (t[j] == ' ' && j + 1 < n && t[j + 1] == ' ') ++e2; ++j; }
        uint32_t a = i; while (a < j && (t[a] == ' ' || t[a] == '\r')) ++a;
        if (a < j) { ++lineas; if (t[a] == '|') ++pipes; if (pc_fija < 0) pc_fija = k; else if (k != pc_fija) pc_ok = 0; if (!e2) esp_ok = 0; }
        i = j + 1;
    }
    if (tabs) return '\t';
    if (lineas && pipes == lineas) { *md = 1; return '|'; }
    if (pc_ok && pc_fija > 0) return ';';
    if (lineas > 1 && esp_ok) return ' ';
    return 0;
}
static void pega_texto(const char *t, uint32_t n, int solo_valores)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l) return;
    int md; int sep = separador(t, n, &md);
    /* tamano */
    uint32_t nf = 0, ncmax = 0;
    for (uint32_t i = 0; i < n; ) {
        uint32_t j = i; uint32_t nc = 1; while (j < n && t[j] != '\n') { if (sep && sep != ' ' && t[j] == sep) ++nc; ++j; }
        if (sep == ' ') { nc = 1; for (uint32_t k = i; k + 1 < j; ++k) if (t[k] == ' ' && t[k + 1] == ' ') { ++nc; while (k + 1 < j && t[k + 1] == ' ') ++k; } }
        if (md) nc = nc > 2 ? nc - 2 : 1;
        if (j > i || j < n) { ++nf; if (nc > ncmax) ncmax = nc; }
        i = j + 1;
    }
    while (n && (t[n - 1] == '\n' || t[n - 1] == '\r')) { --n; }
    if (!nf || !n) return;
    if ((uint64_t)nf * ncmax > 200000u) { error("La tabla es demasiado grande para pegarla de una vez (200.000 celdas)."); return; }
    uint32_t f2 = v->af + nf - 1, c2 = v->ac + ncmax - 1; if (f2 >= HJ_FILAS) f2 = HJ_FILAS - 1; if (c2 >= HJ_COLS) c2 = HJ_COLS - 1;
    if (paso_rango(v->af, v->ac, f2, c2)) return;
    static char cel[2048]; uint32_t f = v->af, escritas = 0; int e;
    for (uint32_t i = 0; i < n && f <= f2; ) {
        uint32_t j = i; while (j < n && t[j] != '\n') ++j;
        uint32_t a = i, b = j; while (b > a && (t[b - 1] == '\r')) --b;
        int salta = 0;
        if (md) { uint32_t z = a; while (z < b && (t[z] == ' ' || t[z] == '|' || t[z] == '-' || t[z] == ':')) ++z; if (z == b) salta = 1; while (a < b && t[a] == ' ') ++a; if (a < b && t[a] == '|') ++a; while (b > a && t[b - 1] == ' ') --b; if (b > a && t[b - 1] == '|') --b; }
        if (!salta) {
            uint32_t c = v->ac, p = a;
            while (p <= b && c <= c2) {
                uint32_t q = p;
                if (sep == ' ') { while (q < b && !(t[q] == ' ' && q + 1 < b && t[q + 1] == ' ')) ++q; }
                else if (sep) { while (q < b && t[q] != sep) ++q; }
                else q = b;
                uint32_t x = p, y = q; while (x < y && (t[x] == ' ' || t[x] == '\r')) ++x; while (y > x && (t[y - 1] == ' ' || t[y - 1] == '\r')) --y;
                if (y > x && y - x >= 2 && t[x] == '"' && t[y - 1] == '"') { ++x; --y; }
                uint32_t k = 0; for (uint32_t z = x; z < y && k + 1 < sizeof cel; ++z) cel[k++] = t[z];
                cel[k] = 0;
                if (k) {
                    if (solo_valores && cel[0] == '=') (void)hj_pon_txt(l, l->activa, f, c, cel);
                    else if (hj_escribe(l, l->activa, f, c, cel, &e) == -1) (void)hj_pon_txt(l, l->activa, f, c, cel);
                    ++escritas;
                }
                ++c;
                if (sep == ' ') { while (q < b && t[q] == ' ') ++q; p = q; if (q >= b) break; }
                else { if (q >= b) break; p = q + 1; }
            }
            ++f;
        }
        i = j + 1;
    }
    v->sf = f > v->af ? f - 1 : v->af; v->sc = c2;
    cambio();
    char m[96]; m[0] = 0; cat(m, "Pegadas ", sizeof m); num(m, sizeof m, escritas); cat(m, escritas == 1 ? " celda." : " celdas.", sizeof m); avisa(m);
}
static void pega(int solo_valores)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l) return;
    if (g_copia.doc == g_doc && g_copia.seq == clip_seq() && g_copia.hoja < l->nh) {
        uint32_t af = g_copia.f2 - g_copia.f1, ac = g_copia.c2 - g_copia.c1;
        uint32_t f2 = v->af + af < HJ_FILAS ? v->af + af : HJ_FILAS - 1, c2 = v->ac + ac < HJ_COLS ? v->ac + ac : HJ_COLS - 1;
        if (g_copia.cortar) { if (paso_libro()) return; }
        else if (paso_rango(v->af, v->ac, f2, c2)) return;
        if (hj_copia(l, l->activa, v->af, v->ac, g_copia.hoja, g_copia.f1, g_copia.c1, g_copia.f2, g_copia.c2, solo_valores)) { error("No se pudo pegar (sin memoria)."); return; }
        if (g_copia.cortar) {
            /* lo cortado se vacia salvo donde se ha pegado encima */
            int h = g_copia.hoja;
            for (uint32_t f = g_copia.f1; f <= g_copia.f2; ++f) for (uint32_t c = g_copia.c1; c <= g_copia.c2; ++c)
                if (!(h == l->activa && f >= v->af && f <= f2 && c >= v->ac && c <= c2)) { hj_borra_celda(l, h, f, c); (void)hj_pon_estilo(l, h, f, c, 0); }
            g_copia.doc = -1;
        }
        v->sf = f2; v->sc = c2;
        cambio(); return;
    }
    uint32_t n = 0; const char *t = clip_text(&n);
    if (!t || !n) { avisa("El portapapeles est\303\241 vac\303\255o."); return; }
    pega_texto(t, n, solo_valores);
}
int hoja_gui_clip_op(int op)
{
    hoja_gui_init(); if (asegura_libro() < 0) return 1;
    if (g_foco == F_BARRA) { tf_clip(&g_tbarra, op); ++g_ver; return 1; }
    if (g_foco == F_BARRA2) { tf_clip(&g_tbarra2, op); ++g_ver; return 1; }
    if (g_edit) { tf_clip(&g_tf, op); sugerencias_fx(); ++g_ver; return 1; }
    if (op == CLIP_OP_COPY) copia(0);
    else if (op == CLIP_OP_CUT) copia(1);
    else pega(0);
    ++g_ver; return 1;
}

/* ---------------------------------------------------------------- acciones */
static void borra_contenido(void)
{
    struct hj_libro *l = L(); if (!l) return;
    uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2);
    if (paso_rango(f1, c1, f2, c2)) return;
    hj_borra_rango(l, l->activa, f1, c1, f2, c2, 0); cambio();
}
static uint32_t ultimo_usado(int fila, uint32_t desde, int d)
{
    /* Ctrl+flecha: hasta el borde del bloque de datos */
    struct hj_libro *l = L(); struct vista *v = V(); uint32_t lim = fila ? HJ_FILAS - 1 : HJ_COLS - 1, p = desde;
    #define LLENA(q) (fila ? hj_celda_c(l, l->activa, (q), v->ac) != 0 && hj_celda_c(l, l->activa, (q), v->ac)->t != HJ_VACIA : hj_celda_c(l, l->activa, v->af, (q)) != 0 && hj_celda_c(l, l->activa, v->af, (q))->t != HJ_VACIA)
    struct hj_hoja *s = S(); uint32_t usado = fila ? s->max_fila : s->max_col;
    if (d > 0 && p >= usado) return p;
    int sig = (d > 0 ? p < lim : p > 0) ? LLENA(d > 0 ? p + 1 : p - 1) : 0;
    if (LLENA(p) && sig) { while ((d > 0 ? p < lim : p > 0) && LLENA(d > 0 ? p + 1 : p - 1)) p = d > 0 ? p + 1 : p - 1; return p; }
    while (d > 0 ? p < lim && p < usado : p > 0) { p = d > 0 ? p + 1 : p - 1; if (LLENA(p)) return p; }
    #undef LLENA
    return d > 0 ? (usado ? usado - 1 : 0) : 0;
}
static void mueve(int df, int dc, int extiende)
{
    struct vista *v = V();
    uint32_t *f = extiende ? &v->sf : &v->af, *c = extiende ? &v->sc : &v->ac;   /* con Mayus se mueve el otro extremo: la celda activa no cambia */
    if (df) *f = fila_mueve(*f, df);
    if (dc < 0) *c = (uint32_t)-dc > *c ? 0 : *c - (uint32_t)(-dc);
    else if (dc > 0) *c = *c + (uint32_t)dc >= HJ_COLS ? HJ_COLS - 1 : *c + (uint32_t)dc;
    if (!extiende) { v->sf = v->af; v->sc = v->ac; }
    a_la_vista_de(*f, *c); ++g_ver;
}
static void autosuma(void)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l) return;
    int h = l->activa; uint32_t f = v->af, c = v->ac, a, b; char r[64]; char fo[96];
    #define ESNUM(ff, cc) ({ const struct hj_celda *_x = hj_celda_c(l, h, (ff), (cc)); _x && _x->t == HJ_NUM; })
    if (f > 0 && ESNUM(f - 1, c)) { b = f - 1; a = b; while (a > 0 && ESNUM(a - 1, c)) --a; ref_texto(a, c, b, c, r, sizeof r); }
    else if (c > 0 && ESNUM(f, c - 1)) { b = c - 1; a = b; while (a > 0 && ESNUM(f, a - 1)) --a; ref_texto(f, a, f, b, r, sizeof r); }
    else { r[0] = 0; }
    #undef ESNUM
    cpy(fo, "=SUMA(", sizeof fo); cat(fo, r, sizeof fo); cat(fo, ")", sizeof fo);
    empieza_edicion(2, fo);
    if (!r[0]) { g_tf.cur = g_tf.anc = (uint32_t)sl(fo) - 1; }
}
static void selecciona_region(uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2)
{
    /* la seleccion, o el bloque de datos alrededor de la celda activa */
    struct hj_libro *l = L(); struct vista *v = V();
    if (!sel_es_una()) { sel_util(f1, c1, f2, c2); return; }
    int h = l->activa; uint32_t a = v->af, b = v->af, x = v->ac, y = v->ac;
    #define LL(ff, cc) (hj_celda_c(l, h, (ff), (cc)) && hj_celda_c(l, h, (ff), (cc))->t != HJ_VACIA)
    for (int k = 0; k < 4; ++k) {
        while (a > 0 && ({ int z = 0; for (uint32_t c = x; c <= y; ++c) if (LL(a - 1, c)) { z = 1; break; } z; })) --a;
        while (b + 1 < HJ_FILAS && ({ int z = 0; for (uint32_t c = x; c <= y; ++c) if (LL(b + 1, c)) { z = 1; break; } z; })) ++b;
        while (x > 0 && ({ int z = 0; for (uint32_t f = a; f <= b; ++f) if (LL(f, x - 1)) { z = 1; break; } z; })) --x;
        while (y + 1 < HJ_COLS && ({ int z = 0; for (uint32_t f = a; f <= b; ++f) if (LL(f, y + 1)) { z = 1; break; } z; })) ++y;
    }
    #undef LL
    *f1 = a; *f2 = b; *c1 = x; *c2 = y;
}
/* la primera fila son titulos: todo texto (o negrita) y debajo hay numeros en alguna de esas columnas */
static int tiene_titulos(uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    struct hj_libro *l = L(); int h = l->activa; if (f2 <= f1) return 0;
    int texto = 0, bajo_num = 0, negrita = 1, alguna = 0;
    for (uint32_t c = c1; c <= c2 && c < c1 + 200; ++c) {
        const struct hj_celda *a = hj_celda_c(l, h, f1, c);
        if (!a || a->t == HJ_VACIA) continue;
        alguna = 1;
        if (a->t != HJ_TXT) return 0;
        ++texto; if (!hj_estilo_de(l, a->estilo)->negrita) negrita = 0;
        for (uint32_t f = f1 + 1; f <= f2 && f < f1 + 6; ++f) { const struct hj_celda *b = hj_celda_c(l, h, f, c); if (b && (b->t == HJ_NUM || b->t == HJ_BOOL)) { bajo_num = 1; break; } }
    }
    if (!alguna) return 0;
    if (negrita) return 1;
    return texto && bajo_num;
}
static void ordena(int desc)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l) return;
    uint32_t f1, c1, f2, c2; selecciona_region(&f1, &c1, &f2, &c2);
    /* si la primera fila son titulos, no se ordena */
    if (sel_es_una() && tiene_titulos(f1, c1, f2, c2)) ++f1;
    if (f2 <= f1) { avisa("No hay nada que ordenar."); return; }
    uint32_t col = v->ac < c1 || v->ac > c2 ? c1 : v->ac;
    if (paso_rango(f1, c1, f2, c2)) return;
    if (hj_ordena(l, l->activa, f1, c1, f2, c2, col, desc, -1, 0)) { paso_quita_ultimo(); error("No se pudo ordenar (sin memoria)."); return; }
    cambio(); avisa(desc ? "Ordenado de mayor a menor (Z \342\206\222 A)." : "Ordenado de menor a mayor (A \342\206\222 Z).");
}
static void filtro_onoff(void)
{
    struct hj_libro *l = L(); struct hj_hoja *s = S(); if (!l) return;
    if (paso_libro()) return;
    if (s->hay_filtro) { for (uint32_t f = s->filtro_r1; f <= s->filtro_r2 && f < HJ_FILAS && f < s->max_fila + 1; ++f) hj_oculta_fila(s, f, 0); s->hay_filtro = 0; s->nfil = 0; cambio(); avisa("Filtro quitado."); return; }
    uint32_t f1, c1, f2, c2; selecciona_region(&f1, &c1, &f2, &c2);
    if (f2 <= f1) { paso_quita_ultimo(); avisa("Pon el cursor en una tabla con una fila de t\303\255tulos."); return; }
    s->hay_filtro = 1; s->filtro_r1 = f1; s->filtro_c1 = c1; s->filtro_r2 = f2; s->filtro_c2 = c2; s->nfil = 0; l->version++;
    cambio(); avisa("Filtro puesto: pulsa el tri\303\241ngulo de un t\303\255tulo para elegir qu\303\251 filas ver.");
}
static void rellena(int der)
{
    struct hj_libro *l = L(); if (!l) return;
    uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2);
    if (sel_es_una()) { avisa("Selecciona primero el rango a rellenar (la primera fila o columna es el modelo)."); return; }
    if (paso_rango(f1, c1, f2, c2)) return;
    /* el modelo es la primera fila (o columna): el resto se vacia antes, como en Excel */
    if (der) { if (c2 > c1) hj_borra_rango(l, l->activa, f1, c1 + 1, f2, c2, 0); }
    else if (f2 > f1) hj_borra_rango(l, l->activa, f1 + 1, c1, f2, c2, 0);
    (void)hj_rellena(l, l->activa, f1, c1, f2, c2, der); cambio();
}
static void inmoviliza(void)
{
    struct hj_hoja *s = S(); struct vista *v = V(); if (!s) return;
    if (s->inmov_f || s->inmov_c) { s->inmov_f = s->inmov_c = 0; avisa("Paneles sin inmovilizar."); }
    else {
        s->inmov_f = v->af; s->inmov_c = v->ac;
        if (!v->af && !v->ac) s->inmov_f = 1;
        avisa(v->af || v->ac ? "Inmovilizado lo que queda arriba y a la izquierda de la celda." : "Inmovilizada la primera fila.");
    }
    v->top = s->inmov_f > v->top ? s->inmov_f : v->top; v->left = s->inmov_c > v->left ? s->inmov_c : v->left;
    L()->version++; cambio();
}
static void filas_cols(int que)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l) return;
    uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2); int r = 0;
    if (paso_libro()) return;
    uint32_t nf = f2 - f1 + 1, nc = c2 - c1 + 1;
    if (nf > 100000) nf = 100000;
    if (nc > 1000) nc = 1000;
    if (que == A_INS_FILA) r = hj_inserta_filas(l, l->activa, f1, nf);
    else if (que == A_INS_COL) r = hj_inserta_cols(l, l->activa, c1, nc);
    else if (que == A_BOR_FILA) r = hj_borra_filas(l, l->activa, f1, nf);
    else if (que == A_BOR_COL) r = hj_borra_cols(l, l->activa, c1, nc);
    if (r) { paso_quita_ultimo(); error("No se pudo (sin memoria)."); return; }
    v->sf = v->af; v->sc = v->ac;
    cambio();
}
static void ancho_auto(uint32_t c)
{
    struct hj_libro *l = L(); struct hj_hoja *s = S(); if (!l) return;
    int mx = 0; static char b[512];
    for (int32_t i = 0; i < s->nc; ++i) {
        const struct hj_celda *x = &s->c[i]; if (x->col != c || x->t == HJ_VACIA) continue;
        hj_texto_ver(l, l->activa, x->fila, c, b, sizeof b);
        const struct hj_estilo *e = hj_estilo_de(l, x->estilo);
        int px = e->tam ? e->tam * 13 / 11 : 13; int w = tw(b, px, e->negrita ? WF_BOLD : 0);
        if (w > mx) mx = w;
    }
    int px = mx + 14; if (px < 30) px = 30; if (px > 640) px = 640;
    hj_pon_ancho(s, c, (uint16_t)px); l->version++;
}

/* ---------------------------------------------------------------- barra de abajo */
static void lista_archivos(void)
{
    g_nsug = 0;
    static char lista[8192];
    static const char *const D[] = { "/usuario", "/usuario/Documentos", "/usuario/Descargas", "/tmp", "/workspace", "/workspace/Descargas", 0 };
    for (int d = 0; D[d] && g_nsug < 12; ++d) {
        lista[0] = 0; (void)vfs_format_list_ext(0, D[d], lista, sizeof lista);
        const char *s = lista; if (s[0] == 'A') { while (*s && *s != ' ') ++s; if (*s) ++s; }
        while (*s && g_nsug < 12) {
            const char *e = s; while (*e && !(e[0] == ',' && e[1] == ' ')) ++e;
            int L2 = (int)(e - s); if (L2 > 159) L2 = 159;
            char r[160]; for (int i = 0; i < L2; ++i) r[i] = s[i]; r[L2] = 0;
            if (r[0] == '/' && hoja_formato_ruta(r) && !termina(r, "fm_vista.csv")) { int rep = 0; for (int k = 0; k < g_nsug; ++k) { const char *a = g_sug[k], *b = r; while (*a && *a == *b) { ++a; ++b; } if (!*a && !*b) rep = 1; } if (!rep) cpy(g_sug[g_nsug++], r, 160); }
            s = *e ? e + 2 : e;
        }
    }
}
static void abre_barra(int tipo)
{
    if (g_edit) { if (confirma()) return; }
    g_barra = tipo; g_foco = F_BARRA; g_nsug = 0; g_menu = M_NADA;
    tf_init(&g_tbarra, g_bbarra, sizeof g_bbarra, 0); tf_init(&g_tbarra2, g_bbarra2, sizeof g_bbarra2, 0);
    struct hj_libro *l = L(); struct vista *v = V();
    if (tipo == BA_ABRIR) { tf_set(&g_tbarra, "/usuario/"); lista_archivos(); }
    else if (tipo == BA_GUARDAR) { tf_set(&g_tbarra, hoja_ruta(g_doc)); }
    else if (tipo == BA_RENOMBRAR) { tf_set(&g_tbarra, l->h[l->activa].nombre); tf_select_all(&g_tbarra); }
    else if (tipo == BA_IR) { char d[16]; hj_dir(v->af, v->ac, d); tf_set(&g_tbarra, d); tf_select_all(&g_tbarra); }
    else if (tipo == BA_IA) { tf_set(&g_tbarra, ""); }
    else if (tipo == BA_TITULO) { struct hj_hoja *sh = S(); if (g_gsel >= 0 && g_gsel < sh->ngraf) tf_set(&g_tbarra, sh->graf[g_gsel].titulo); tf_select_all(&g_tbarra); }
    else if (tipo == BA_CODIGO) { const struct hj_estilo *e = estilo_activo(); tf_set(&g_tbarra, e->nf == NF_CODIGO ? e->codigo : "#.##0,00 \"kg\""); tf_select_all(&g_tbarra); }
    else if (tipo == BA_FILTRO) {
        struct hj_hoja *s = S(); const char *cr = "";
        for (uint32_t i = 0; i < s->nfil; ++i) if (s->fil[i].col == g_barra_col) cr = s->fil[i].crit;
        tf_set(&g_tbarra, cr); tf_select_all(&g_tbarra);
        /* valores distintos de la columna (hasta 12) */
        static char b[160];
        for (uint32_t f = s->filtro_r1 + 1; f <= s->filtro_r2 && f < s->max_fila && g_nsug < 12; ++f) {
            hj_texto_ver(l, l->activa, f, g_barra_col, b, sizeof b); if (!b[0]) continue;
            int rep = 0; for (int k = 0; k < g_nsug && !rep; ++k) if (hj_ieq_pl(g_sug[k], b)) rep = 1;
            if (!rep) cpy(g_sug[g_nsug++], b, 160);
        }
    }
    ++g_ver;
}
static void cierra_barra(void) { g_barra = BA_NADA; if (g_foco == F_BARRA || g_foco == F_BARRA2) g_foco = F_NADA; g_nsug = 0; ++g_ver; }
static void guarda(const char *ruta)
{
    char m[300];
    int r = hoja_guarda(g_doc, ruta, m, sizeof m);
    if (r) { error(m); if (!ruta || !ruta[0]) abre_barra(BA_GUARDAR); }
    else { avisa(m); if (g_barra == BA_GUARDAR) cierra_barra(); }
}
static void busca_sig(void)
{
    struct hj_libro *l = L(); struct vista *v = V(); if (!l || !g_bbarra[0]) return;
    uint32_t f = v->af, c = v->ac;
    if (hj_busca(l, l->activa, &f, &c, g_bbarra, 0, 0)) {
        if (f == v->af && c == v->ac) { uint32_t f2 = f, c2 = c; if (hj_busca(l, l->activa, &f2, &c2, g_bbarra, 0, 0)) { f = f2; c = c2; } }
        v->af = v->sf = f; v->ac = v->sc = c; a_la_vista(); ++g_ver;
    } else avisa("No est\303\241 en esta hoja.");
}
static void barra_ok(int alt)
{
    struct hj_libro *l = L(); struct vista *v = V(); char m[300];
    switch (g_barra) {
    case BA_ABRIR: {
        if (!g_bbarra[0]) return;
        int k = hoja_abre(g_bbarra, m, sizeof m);
        if (k < 0) { error(m); return; }
        hoja_pon_actual(k); asegura_libro(); cierra_barra(); avisa("Abierto."); break;
    }
    case BA_GUARDAR: {
        if (alt) { /* cambiar la extension */
            static const char *const X[] = { ".xlsx", ".ods", ".csv" };
            char r[400]; cpy(r, g_bbarra, sizeof r); int L2 = sl(r); int p = L2; while (p > 0 && r[p - 1] != '.' && r[p - 1] != '/') --p;
            if (p > 0 && r[p - 1] == '.') r[p - 1] = 0;
            cat(r, X[alt - 1], sizeof r); tf_set(&g_tbarra, r); ++g_ver; return;
        }
        guarda(g_bbarra); break;
    }
    case BA_BUSCAR:
        if (alt == 1) { if (!g_bbarra[0]) return; if (paso_libro()) return; int n = hj_reemplaza(l, l->activa, g_bbarra, g_bbarra2, 0); if (!n) paso_quita_ultimo(); else cambio(); m[0] = 0; cat(m, "Reemplazadas ", sizeof m); num(m, sizeof m, n); cat(m, n == 1 ? " celda." : " celdas.", sizeof m); avisa(m); }
        else busca_sig();
        break;
    case BA_RENOMBRAR: {
        if (paso_libro()) return;
        int r = hj_hoja_renombra(l, l->activa, g_bbarra);
        if (r) { paso_quita_ultimo(); error(r == -2 ? "Ya hay una hoja con ese nombre." : "Nombre no v\303\241lido: sin : / \\ ? * [ ] y 31 letras como mucho."); return; }
        cambio(); cierra_barra(); break;
    }
    case BA_IR: {
        uint32_t f1, c1, f2, c2; const char *s = g_bbarra; int h = -1;
        for (const char *z = s; *z; ++z) if (*z == '!') { char hn[40]; int k = 0; for (const char *q = s; q < z && k < 39; ++q) if (*q != '\'') hn[k++] = *q; hn[k] = 0; h = hj_hoja_busca(l, hn); s = z + 1; break; }
        if (hj_lee_rango(s, &f1, &c1, &f2, &c2) <= 0) { error("Escribe una celda o un rango: B12, A1:D20, Hoja2!C3."); return; }
        if (h >= 0) l->activa = h;
        v = V(); v->af = f1; v->ac = c1; v->sf = f2; v->sc = c2; a_la_vista(); cierra_barra(); break;
    }
    case BA_FILTRO: {
        struct hj_hoja *s = S(); if (!s->hay_filtro) { cierra_barra(); return; }
        if (paso_libro()) return;
        int vis = hj_filtra(l, l->activa, s->filtro_r1, s->filtro_c1, s->filtro_r2, s->filtro_c2, g_barra_col, alt ? "" : g_bbarra);
        if (vis < 0) { paso_quita_ultimo(); error("Como mucho 8 columnas con filtro a la vez."); return; }
        cambio(); m[0] = 0; cat(m, "Filas que se ven: ", sizeof m); num(m, sizeof m, vis); avisa(m); cierra_barra();
        while (oculta(v->af) && v->af + 1 < HJ_FILAS) ++v->af;
        v->sf = v->af; a_la_vista(); break;
    }
    case BA_CODIGO: if (g_bbarra[0]) { aplica(fn_codigo, 0); cierra_barra(); } break;
    case BA_IA: {
        if (!g_bbarra[0]) return;
        if (hoja_ia_estado() == HIA_PENSANDO) { avisa("La IA a\303\272n est\303\241 pensando la respuesta anterior."); return; }
        char *ctx = (char *)kmalloc(24u * 1024u); if (!ctx) { error("Sin memoria."); return; }
        contexto_ia(ctx, 24u * 1024u);
        if (hoja_ia_pide(g_bbarra, ctx)) error("No se pudo preguntar a la IA ahora."); else tf_set(&g_tbarra, "");
        kfree(ctx); break;
    }
    case BA_TITULO: { struct hj_hoja *sh = S(); if (g_gsel < 0 || g_gsel >= sh->ngraf) { cierra_barra(); break; } if (paso_libro()) return; cpy(sh->graf[g_gsel].titulo, g_bbarra, sizeof sh->graf[0].titulo); l->version++; cambio(); cierra_barra(); break; }
    default: break;
    }
}

/* ---------------------------------------------------------------- menus */
static void menu_add(const char *t, int id, int on) { if (g_nmi < 40) { cpy(g_mi[g_nmi].t, t, sizeof g_mi[0].t); g_mi[g_nmi].id = id; g_mi[g_nmi].on = on; ++g_nmi; } }
static const uint32_t PALETA[20] = {
    0x01000000u, 0x01404040u, 0x01808080u, 0x01DC2626u, 0x01EA580Cu, 0x01CA8A04u, 0x0116A34Au, 0x010891B2u, 0x012563EBu, 0x017C3AEDu,
    0x01FFFFFFu, 0x01E5E7EBu, 0x01FECACAu, 0x01FED7AAu, 0x01FEF08Au, 0x01BBF7D0u, 0x01A5F3FCu, 0x01BFDBFEu, 0x01DDD6FEu, 0x01FBCFE8u };
static void abre_menu(int m, int x, int y)
{
    if (g_menu == m) { g_menu = M_NADA; ++g_ver; return; }
    g_menu = m; g_menu_x = x; g_menu_y = y; g_nmi = 0;
    const struct hj_estilo *e = estilo_activo(); struct hj_hoja *s = S();
    switch (m) {
    case M_ARCHIVO:
        menu_add("Nuevo libro", A_NUEVO, 0); menu_add("Abrir\342\200\246   (Ctrl+O)", A_ABRIR, 0); menu_add("Guardar   (Ctrl+S)", A_GUARDAR, 0); menu_add("Guardar como\342\200\246", A_GUARDAR_COMO, 0);
        menu_add("Enviar por correo\342\200\246", A_CORREO, 0); menu_add("Contactos a una hoja", A_DESDE_CT, 0); menu_add("Calendario a una hoja", A_DESDE_AG, 0);
        menu_add("Exportar a Excel (.xlsx)", A_EXP_XLSX, 0); menu_add("Exportar a LibreOffice (.ods)", A_EXP_ODS, 0); menu_add("Exportar la hoja a CSV", A_EXP_CSV, 0);
        menu_add(g_cerrar_armado ? "Cerrar SIN GUARDAR (pulsa otra vez)" : "Cerrar el libro", A_CERRAR, g_cerrar_armado);
        { int otros = 0; for (int i = 0; i < HOJA_LIBROS; ++i) if (hoja_libro(i)) ++otros;
          if (otros > 1) for (int i = 0; i < HOJA_LIBROS; ++i) if (hoja_libro(i)) { const char *r = hoja_ruta(i), *b = r; for (const char *z = r; *z; ++z) if (*z == '/') b = z + 1; char t[64]; cpy(t, i == g_doc ? "\342\200\242 " : "   ", sizeof t); cat(t, b, sizeof t); if (hoja_cambiado(i)) cat(t, " *", sizeof t); menu_add(t, A_LIBRO0 + i, i == g_doc); } }
        break;
    case M_FORMATO: {
        uint8_t nf, d, mi; hj_tipo_numero(e, &nf, &d, &mi);
        static const char *const N[] = { "General", "N\303\272mero   1.234,50", "Moneda   1.234,50 \342\202\254", "Porcentaje   12%", "Fecha   27/11/2026", "Hora   14:30", "Fecha y hora", "Texto", "Cient\303\255fica   1,23E+05" };
        for (int i = 0; i < 9; ++i) menu_add(N[i], A_NF0 + i, nf == i && e->nf != NF_CODIGO);
        menu_add("Personalizado\342\200\246", A_NF_CODIGO, e->nf == NF_CODIGO);
        break; }
    case M_BORDES: menu_add("Todos los bordes", A_BORDE_TODOS, 0); menu_add("Borde exterior", A_BORDE_FUERA, 0); menu_add("Borde de abajo", A_BORDE_ABAJO, 0); menu_add("Sin bordes", A_BORDE_NINGUNO, 0); break;
    case M_FILAS:
        menu_add("Insertar filas encima", A_INS_FILA, 0); menu_add("Insertar columnas a la izquierda", A_INS_COL, 0);
        menu_add("Borrar filas", A_BOR_FILA, 0); menu_add("Borrar columnas", A_BOR_COL, 0);
        menu_add("Ancho autom\303\241tico de columna", A_ANCHO_AUTO, 0); menu_add("Alto normal de fila", A_ALTO_NORMAL, 0);
        menu_add("Borrar contenido y formato", A_BORRAR_TODO, 0); menu_add("Quitar el formato", A_BORRAR_FORMATO, 0);
        break;
    case M_DATOS:
        menu_add("Ordenar de A a Z (menor a mayor)", A_ORD_AZ, 0); menu_add("Ordenar de Z a A (mayor a menor)", A_ORD_ZA, 0);
        menu_add(s && s->hay_filtro ? "Quitar el filtro" : "Poner filtro", A_FILTRO, s && s->hay_filtro);
        if (s && s->nfil) menu_add("Ver todas las filas", A_QUITA_FILTRO, 0);
        menu_add("Rellenar hacia abajo   (Ctrl+D)", A_RELL_ABAJO, 0); menu_add("Rellenar a la derecha   (Ctrl+R)", A_RELL_DER, 0);
        menu_add(s && (s->inmov_f || s->inmov_c) ? "Movilizar paneles" : "Inmovilizar paneles", A_INMOV, s && (s->inmov_f || s->inmov_c));
        menu_add("Buscar y reemplazar   (Ctrl+F)", A_BUSCAR, 0); menu_add("Ir a una celda\342\200\246", A_IR, 0);
        break;
    case M_FX: {
        static const char *const F[] = { "SUMA", "PROMEDIO", "CONTAR", "CONTARA", "MAX", "MIN", "SI", "SI.ERROR", "BUSCARV", "BUSCARX", "SUMAR.SI", "CONTAR.SI", "CONCAT", "TEXTO", "REDONDEAR", "HOY", "AHORA", "DIAS", "IZQUIERDA", "ESPACIOS", 0 };
        for (int i = 0; F[i]; ++i) menu_add(F[i], A_FX0 + i, 0);
        break; }
    case M_CELDA:
        menu_add("Cortar   (Ctrl+X)", A_CORTAR, 0); menu_add("Copiar   (Ctrl+C)", A_COPIAR, 0); menu_add("Pegar   (Ctrl+V)", A_PEGAR, 0); menu_add("Pegar solo valores", A_PEGAR_VAL, 0);
        menu_add("Insertar filas encima", A_INS_FILA, 0); menu_add("Insertar columnas a la izquierda", A_INS_COL, 0); menu_add("Borrar filas", A_BOR_FILA, 0); menu_add("Borrar columnas", A_BOR_COL, 0);
        menu_add("Borrar contenido y formato", A_BORRAR_TODO, 0); menu_add("Ordenar de A a Z", A_ORD_AZ, 0); menu_add("Ordenar de Z a A", A_ORD_ZA, 0);
        break;
    case M_GRAF_NUEVO: {
        static const int ORD[6] = { 1, 0, 2, 3, 4, 5 };
        for (int i = 0; i < 6; ++i) { char t[48]; cpy(t, "Gr\303\241fico de ", sizeof t); cat(t, hj_grafico_tipo_nombre(ORD[i]), sizeof t); menu_add(t, A_GNUEVO0 + ORD[i], 0); }
        break; }
    case M_GRAF: {
        struct hj_hoja *sh = S(); const struct hj_grafico *gg = g_gsel >= 0 && g_gsel < sh->ngraf ? &sh->graf[g_gsel] : 0;
        static const int ORD[6] = { 1, 0, 2, 3, 4, 5 };
        for (int i = 0; i < 6; ++i) menu_add(hj_grafico_tipo_nombre(ORD[i]), A_GTIPO0 + ORD[i], gg && gg->tipo == ORD[i]);
        menu_add("T\303\255tulo\342\200\246", A_GTITULO, 0); menu_add(gg && gg->leyenda ? "Quitar la leyenda" : "Poner leyenda", A_GLEYENDA, 0);
        menu_add("Usar la selecci\303\263n como datos", A_GDATOS, 0); menu_add("Borrar el gr\303\241fico   (Supr)", A_GBORRA, 0);
        break; }
    case M_MAS:
        for (int i = 0; i < g_ntb_oculto; ++i) menu_add(nombre_boton(g_tb_oculto[i]), A_BOTON0 + g_tb_oculto[i], 0);
        break;
    case M_PEST:
        menu_add("Cambiar el nombre\342\200\246", A_PEST_REN, 0); menu_add("Mover a la izquierda", A_PEST_IZQ, 0); menu_add("Mover a la derecha", A_PEST_DER, 0);
        menu_add("Hoja nueva", A_PEST_NUEVA, 0); menu_add("Borrar la hoja", A_PEST_BORRAR, 0);
        break;
    default: break;
    }
    ++g_ver;
}
static void boton_accion(int b, int bx, int by);
static void accion(int a)
{
    struct hj_libro *l = L();
    if (a != A_CERRAR) g_cerrar_armado = 0;
    if (a >= A_LIBRO0 && a < A_LIBRO0 + HOJA_LIBROS) { hoja_pon_actual(a - A_LIBRO0); asegura_libro(); return; }
    if (a >= A_GNUEVO0 && a < A_GNUEVO0 + 6) {                       /* SOMA_360_HOJA_GRAFICOS: grafico nuevo con la seleccion (o la tabla de alrededor) */
        uint32_t f1, c1, f2, c2; selecciona_region(&f1, &c1, &f2, &c2);
        struct hj_grafico prueba; memset(&prueba, 0, sizeof prueba); prueba.tipo = (uint8_t)(a - A_GNUEVO0); prueba.hoja_datos = (uint8_t)l->activa; prueba.r1 = f1; prueba.c1 = c1; prueba.r2 = f2; prueba.c2 = c2;
        struct hj_gforma fm; if (hj_grafico_forma(l, l->activa, &prueba, &fm)) { avisa("Selecciona primero los datos (con sus t\303\255tulos) o pon el cursor en una tabla."); return; }
        if (paso_libro()) return;
        int k = hj_grafico_nuevo(l, l->activa, f1, c1, f2, c2, a - A_GNUEVO0);
        if (k < 0) { paso_quita_ultimo(); error("16 gr\303\241ficos por hoja como mucho."); return; }
        g_gsel = k; cambio(); avisa("Gr\303\241fico creado: arr\303\241stralo para moverlo; la esquina, para cambiar su tama\303\261o."); return;
    }
    if (a >= A_GTIPO0 && a < A_GTIPO0 + 6) { struct hj_hoja *sh = S(); if (g_gsel < 0 || g_gsel >= sh->ngraf) return; if (paso_libro()) return; sh->graf[g_gsel].tipo = (uint8_t)(a - A_GTIPO0); l->version++; cambio(); return; }
    if (a == A_GTITULO) { abre_barra(BA_TITULO); return; }
    if (a == A_GLEYENDA) { struct hj_hoja *sh = S(); if (g_gsel < 0 || g_gsel >= sh->ngraf) return; if (paso_libro()) return; sh->graf[g_gsel].leyenda = !sh->graf[g_gsel].leyenda; l->version++; cambio(); return; }
    if (a == A_GDATOS) { struct hj_hoja *sh = S(); if (g_gsel < 0 || g_gsel >= sh->ngraf) return; uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); if (sel_es_una()) { avisa("Selecciona antes el rango de datos."); return; } if (paso_libro()) return; struct hj_grafico *gg = &sh->graf[g_gsel]; gg->r1 = f1; gg->c1 = c1; gg->r2 = f2; gg->c2 = c2; gg->hoja_datos = (uint8_t)l->activa; l->version++; cambio(); return; }
    if (a == A_GBORRA) { if (g_gsel < 0 || g_gsel >= S()->ngraf) return; if (paso_libro()) return; hj_grafico_borra(l, l->activa, g_gsel); g_gsel = -1; cambio(); return; }
    if (a >= A_BOTON0 && a < A_BOTON0 + 64) { boton_accion(a - A_BOTON0, g_menu_x, g_menu_y); return; }
    if (a >= A_NF0 && a < A_NF0 + 9) { aplica(fn_nf, (uint32_t)(a - A_NF0)); return; }
    if (a >= A_FX0 && a < A_FX0 + 30) {
        static const char *const F[] = { "SUMA", "PROMEDIO", "CONTAR", "CONTARA", "MAX", "MIN", "SI", "SI.ERROR", "BUSCARV", "BUSCARX", "SUMAR.SI", "CONTAR.SI", "CONCAT", "TEXTO", "REDONDEAR", "HOY", "AHORA", "DIAS", "IZQUIERDA", "ESPACIOS" };
        int k = a - A_FX0; if (k >= 20) return;
        char t[48]; t[0] = 0;
        if (!g_edit) { cpy(t, "=", sizeof t); empieza_edicion(2, ""); }
        cat(t, F[k], sizeof t); cat(t, "(", sizeof t); if (k == 15 || k == 16) cat(t, ")", sizeof t);
        tf_insert(&g_tf, t, (uint32_t)sl(t)); ++g_ver; return;
    }
    switch (a) {
    case A_CORREO: { if (g_edit && confirma()) break; char m[300]; if (hoja_envia_correo(g_doc, "", m, sizeof m)) error(m); else { avisa(m); g_pide_app = 5; } break; }
    case A_DESDE_CT: case A_DESDE_AG: { char m[300]; int k = a == A_DESDE_CT ? hoja_desde_contactos(m, sizeof m) : hoja_desde_agenda("", m, sizeof m); if (k < 0) error(m); else { hoja_pon_actual(k); asegura_libro(); avisa(m); } break; }
    case A_IA_FORMULA: {
        const char *f = hoja_ia_formula(); if (!f[0]) break;
        struct vista *v = V(); if (paso_rango(v->af, v->ac, v->af, v->ac)) break;
        int e = 0; if (hj_escribe(l, l->activa, v->af, v->ac, f, &e)) { paso_quita_ultimo(); error("La f\303\263rmula que propone la IA no es v\303\241lida: rev\303\255sala en la barra de f\303\263rmulas."); empieza_edicion(2, f); break; }
        cambio(); avisa("F\303\263rmula puesta."); break; }
    case A_IA_TABLA: { const char *t = hoja_ia_tabla(); if (t[0]) pega_texto(t, (uint32_t)sl(t), 0); break; }
    case A_IA_COPIA: { char *b = (char *)kmalloc(8192); if (!b) break; cpy(b, hoja_ia_texto(), 8192); if (hoja_ia_formula()[0]) { cat(b, "\n", 8192); cat(b, hoja_ia_formula(), 8192); } clip_set_text(b, 0); kfree(b); avisa("Respuesta copiada."); break; }
    case A_IA_CIERRA: hoja_ia_olvida(); break;
    case A_NUEVO: { char n[24]; nuevo_nombre(n, sizeof n); int k = hoja_nuevo(n); if (k < 0) error("Hay 8 libros abiertos: cierra alguno."); else { hoja_pon_actual(k); asegura_libro(); } break; }
    case A_ABRIR: abre_barra(BA_ABRIR); break;
    case A_GUARDAR: guarda(0); break;
    case A_GUARDAR_COMO: abre_barra(BA_GUARDAR); break;
    case A_EXP_XLSX: case A_EXP_ODS: case A_EXP_CSV: {
        abre_barra(BA_GUARDAR); barra_ok(a - A_EXP_XLSX + 1); break; }
    case A_CERRAR:
        if (hoja_cambiado(g_doc) && !g_cerrar_armado) { g_cerrar_armado = 1; avisa("El libro tiene cambios sin guardar: vuelve a pulsar \302\253Cerrar\302\273 para cerrarlo igualmente."); break; }
        g_cerrar_armado = 0; hoja_cierra(g_doc); g_doc = -1; asegura_libro(); avisa("Libro cerrado."); break;
    case A_BORDE_TODOS: case A_BORDE_FUERA: case A_BORDE_ABAJO: case A_BORDE_NINGUNO: aplica(fn_bordes, (uint32_t)(a - A_BORDE_TODOS)); break;
    case A_NF_CODIGO: abre_barra(BA_CODIGO); break;
    case A_INS_FILA: case A_INS_COL: case A_BOR_FILA: case A_BOR_COL: filas_cols(a); break;
    case A_ANCHO_AUTO: { uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); if (paso_libro()) break; for (uint32_t c = c1; c <= c2 && c < c1 + 200; ++c) ancho_auto(c); cambio(); break; }
    case A_ALTO_NORMAL: { uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); if (paso_libro()) break; struct hj_hoja *s = S(); for (uint32_t f = f1; f <= f2 && f < f1 + 100000; ++f) if (hj_alto(s, f) != HJ_ALTO_DEF) hj_pon_alto(s, f, HJ_ALTO_DEF); l->version++; cambio(); break; }
    case A_BORRAR_TODO: { uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); if (paso_rango(f1, c1, f2, c2)) break; hj_borra_rango(l, l->activa, f1, c1, f2, c2, 1); cambio(); break; }
    case A_BORRAR_FORMATO: aplica(fn_limpia, 0); break;
    case A_ORD_AZ: ordena(0); break;
    case A_ORD_ZA: ordena(1); break;
    case A_FILTRO: filtro_onoff(); break;
    case A_QUITA_FILTRO: { struct hj_hoja *s = S(); if (!s->hay_filtro) break; if (paso_libro()) break; s->nfil = 0; (void)hj_filtra(l, l->activa, s->filtro_r1, s->filtro_c1, s->filtro_r2, s->filtro_c2, s->filtro_c1, ""); cambio(); break; }
    case A_RELL_ABAJO: rellena(0); break;
    case A_RELL_DER: rellena(1); break;
    case A_INMOV: inmoviliza(); break;
    case A_BUSCAR: abre_barra(BA_BUSCAR); break;
    case A_IR: abre_barra(BA_IR); break;
    case A_CORTAR: copia(1); break;
    case A_COPIAR: copia(0); break;
    case A_PEGAR: pega(0); break;
    case A_PEGAR_VAL: pega(1); break;
    case A_PEST_REN: abre_barra(BA_RENOMBRAR); break;
    case A_PEST_IZQ: case A_PEST_DER: {
        int h = l->activa, d = a == A_PEST_IZQ ? h - 1 : h + 1; if (d < 0 || d >= l->nh) break;
        if (paso_libro()) break;
        if (hj_hoja_mueve(l, h, d)) { paso_quita_ultimo(); break; }
        { struct vista t = g_vis[g_doc][h]; g_vis[g_doc][h] = g_vis[g_doc][d]; g_vis[g_doc][d] = t; }
        l->activa = d; cambio(); break; }
    case A_PEST_NUEVA: { if (paso_libro()) break; int k = hj_hoja_nueva(l, 0); if (k < 0) { paso_quita_ultimo(); error("32 hojas como mucho."); break; } l->activa = k; memset(&g_vis[g_doc][k], 0, sizeof g_vis[0][0]); cambio(); break; }
    case A_PEST_BORRAR: {
        if (l->nh <= 1) { avisa("Es la \303\272nica hoja: no se puede borrar."); break; }
        if (paso_libro()) break;
        int h = l->activa; if (hj_hoja_borra(l, h)) { paso_quita_ultimo(); break; }
        for (int k = h; k + 1 < HJ_HOJAS; ++k) g_vis[g_doc][k] = g_vis[g_doc][k + 1];
        if (l->activa >= l->nh) l->activa = l->nh - 1;
        cambio(); avisa("Hoja borrada (Ctrl+Z la recupera)."); break; }
    default: break;
    }
}
static void boton_accion(int b, int bx, int by)
{
    const struct hj_estilo *e = estilo_activo();
    switch (b) {
    case B_ARCHIVO: abre_menu(M_ARCHIVO, bx, by); break;
    case B_DESHACER: deshace(0); break;
    case B_REHACER: deshace(1); break;
    case B_NEGRITA: aplica(fn_negrita, !e->negrita); break;
    case B_CURSIVA: aplica(fn_cursiva, !e->cursiva); break;
    case B_SUBRAYADO: aplica(fn_subr, !e->subrayado); break;
    case B_TACHADO: aplica(fn_tach, !e->tachado); break;
    case B_IZQ: aplica(fn_alin, e->alinear == AL_IZQ ? AL_AUTO : AL_IZQ); break;
    case B_CENTRO: aplica(fn_alin, e->alinear == AL_CENTRO ? AL_AUTO : AL_CENTRO); break;
    case B_DER: aplica(fn_alin, e->alinear == AL_DER ? AL_AUTO : AL_DER); break;
    case B_FORMATO: abre_menu(M_FORMATO, bx, by); break;
    case B_MENOSDEC: aplica(fn_dec, 0); break;
    case B_MASDEC: aplica(fn_dec, 1); break;
    case B_MONEDA: aplica(fn_nf, NF_MONEDA); break;
    case B_PORCIENTO: aplica(fn_nf, NF_PORCENTAJE); break;
    case B_COLOR: abre_menu(M_COLOR, bx, by); break;
    case B_FONDO: abre_menu(M_FONDO, bx, by); break;
    case B_BORDES: abre_menu(M_BORDES, bx, by); break;
    case B_AJUSTAR: aplica(fn_ajus, !e->ajustar); break;
    case B_FILAS: abre_menu(M_FILAS, bx, by); break;
    case B_DATOS: abre_menu(M_DATOS, bx, by); break;
    case B_SUMA: autosuma(); break;
    case B_BUSCAR: abre_barra(BA_BUSCAR); break;
    case B_GUARDAR: guarda(0); break;
    case B_MAS: abre_menu(M_MAS, bx, by); break;
    case B_GRAFICO: abre_menu(g_gsel >= 0 ? M_GRAF : M_GRAF_NUEVO, bx, by); break;
    case B_IA: if (g_barra == BA_IA) cierra_barra(); else abre_barra(BA_IA); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- pintar */
static void ico_flecha(int x, int y, int der, uint32_t c)
{   /* deshacer/rehacer: flecha curva sencilla */
    int s = der ? -1 : 1, cx = x + 11;
    fb_line_aa((cx - 6 * s) * 16, (y + 9) * 16, (cx + 4 * s) * 16, (y + 9) * 16, 26, 0xFF000000u | c);
    fb_line_aa((cx + 4 * s) * 16, (y + 9) * 16, (cx + 7 * s) * 16, (y + 13) * 16, 26, 0xFF000000u | c);
    fb_line_aa((cx + 7 * s) * 16, (y + 13) * 16, (cx + 4 * s) * 16, (y + 17) * 16, 26, 0xFF000000u | c);
    fb_line_aa((cx - 6 * s) * 16, (y + 9) * 16, (cx - 2 * s) * 16, (y + 5) * 16, 26, 0xFF000000u | c);
    fb_line_aa((cx - 6 * s) * 16, (y + 9) * 16, (cx - 2 * s) * 16, (y + 13) * 16, 26, 0xFF000000u | c);
}
static void ico_alin(int x, int y, int tipo, uint32_t c)
{
    static const int L1[3][2] = { { 0, 14 }, { 3, 11 }, { 6, 14 } }, L2[3][2] = { { 0, 9 }, { 1, 13 }, { 5, 14 } };
    for (int k = 0; k < 4; ++k) { const int *q = (k & 1) ? L2[tipo] : L1[tipo]; rect(x + 4 + q[0], y + 5 + k * 4, q[1] - q[0], 2, c); }
}
static void tri(int x, int y, uint32_t c) { for (int k = 0; k < 4; ++k) rect(x + k, y + k, 7 - 2 * k, 1, c); }
static int boton(int x, int y, int w, const char *t, int on, int val, int fl, uint32_t col)
{
    fb_fill_rrect_aa(x, y, w, 30, 7, on ? 0xFF1A3558u : 0xFF151E2Eu);
    if (t) tx(x + (w - tw(t, 13, fl)) / 2, y, 30, t, 13, col ? col : on ? K_ACC2 : K_TXT, fl);
    hit(x, y, w, 30, H_BOTON, val);
    return w;
}
/* boton que abre un menu: texto y un triangulo */
static int boton_menu(int x, int y, const char *t, int on, int val)
{
    int w = tw(t, 13, 0) + 30;
    fb_fill_rrect_aa(x, y, w, 30, 7, on ? 0xFF1A3558u : 0xFF151E2Eu);
    tx(x + 10, y, 30, t, 13, on ? K_ACC2 : K_TXT, 0); tri(x + w - 15, y + 13, on ? K_ACC2 : K_DIM);
    hit(x, y, w, 30, H_BOTON, val);
    return w;
}
static void sep(int x, int y) { rect(x, y + 6, 1, 18, K_LINE); }
/* los botones que no caben van al menu >> (en este orden) */
static const char *nombre_boton(int b)
{
    switch (b) {
    case B_NEGRITA: return "Negrita (Ctrl+B)"; case B_CURSIVA: return "Cursiva (Ctrl+I)"; case B_SUBRAYADO: return "Subrayado (Ctrl+U)"; case B_TACHADO: return "Tachado";
    case B_IZQ: return "Alinear a la izquierda"; case B_CENTRO: return "Centrar"; case B_DER: return "Alinear a la derecha"; case B_AJUSTAR: return "Ajustar texto";
    case B_FORMATO: return "Formato de n\303\272mero\342\200\246"; case B_MONEDA: return "Moneda (\342\202\254)"; case B_PORCIENTO: return "Porcentaje (%)";
    case B_MENOSDEC: return "Menos decimales"; case B_MASDEC: return "M\303\241s decimales"; case B_COLOR: return "Color del texto\342\200\246"; case B_FONDO: return "Color de relleno\342\200\246";
    case B_BORDES: return "Bordes\342\200\246"; case B_SUMA: return "Autosuma (\316\243)"; case B_FILAS: return "Filas y columnas\342\200\246"; case B_DATOS: return "Datos\342\200\246"; case B_BUSCAR: return "Buscar y reemplazar"; case B_GRAFICO: return "Gr\303\241fico\342\200\246"; case B_IA: return "Preguntar a la IA\342\200\246";
    default: return "";
    }
}
static void pinta_barra_herr(int x, int y, int w)
{
    rect(x, y, w, TB_H, K_BAR); rect(x, y + TB_H - 1, w, 1, K_LINE);
    const struct hj_estilo *e = estilo_activo(); int bx = x + 10, by = y + 8;
    /* nombre del libro a la derecha (si cabe) */
    const char *r = hoja_ruta(g_doc), *b = r; for (const char *z = r; *z; ++z) if (*z == '/') b = z + 1;
    char t[120]; cpy(t, b, sizeof t); if (hoja_cambiado(g_doc)) cat(t, "  \342\200\242", sizeof t);
    int nomw = tw(t, 13, WF_BOLD); if (nomw > 220) nomw = 220;
    int lim = x + w - 14 - 40;                               /* hueco para >> */
    int lim_nom = lim - nomw - 16;
    g_ntb_oculto = 0; int lleno = 0;
    #define CABE(ww) (!lleno && (bx + (ww) <= lim ? 1 : (lleno = 1, 0)))
    #define OCULTA(id) do { if (g_ntb_oculto < 32) g_tb_oculto[g_ntb_oculto++] = (id); } while (0)
    bx += boton_menu(bx, by, "Archivo", g_menu == M_ARCHIVO, B_ARCHIVO) + 4;
    bx += boton(bx, by, 32, "", 0, B_GUARDAR, 0, 0) - 32; { int gx = bx + 9, gy = by + 7; fb_stroke_rrect_aa(gx, gy, 14, 16, 2, 0xFFE5EAF2u); rect(gx + 3, gy + 2, 8, 4, K_TXT); rect(gx + 3, gy + 10, 8, 5, K_TXT); } bx += 32 + 6;
    sep(bx, by); bx += 7;
    bx += boton(bx, by, 32, "", 0, B_DESHACER, 0, 0); ico_flecha(bx - 32 + 5, by + 3, 0, g_nun ? K_TXT : K_FAINT); bx += 2;
    bx += boton(bx, by, 32, "", 0, B_REHACER, 0, 0); ico_flecha(bx - 32 + 5, by + 3, 1, g_nre ? K_TXT : K_FAINT); bx += 6;
    sep(bx, by); bx += 7;
    if (CABE(30)) bx += boton(bx, by, 30, "N", e->negrita, B_NEGRITA, WF_BOLD, 0) + 2; else OCULTA(B_NEGRITA);
    if (CABE(30)) bx += boton(bx, by, 30, "K", e->cursiva, B_CURSIVA, WF_ITALIC, 0) + 2; else OCULTA(B_CURSIVA);
    if (CABE(30)) { bx += boton(bx, by, 30, "S", e->subrayado, B_SUBRAYADO, 0, 0); rect(bx - 20, by + 22, 10, 1, e->subrayado ? K_ACC2 : K_TXT); bx += 2; } else OCULTA(B_SUBRAYADO);
    if (CABE(34)) { bx += boton(bx, by, 34, "abc", e->tachado, B_TACHADO, 0, 0); rect(bx - 28, by + 15, 22, 1, e->tachado ? K_ACC2 : K_TXT); bx += 6; } else OCULTA(B_TACHADO);
    if (!lleno) { sep(bx, by); bx += 7; }
    for (int k = 0; k < 3; ++k) { if (CABE(30)) { int on = e->alinear == (k == 0 ? AL_IZQ : k == 1 ? AL_CENTRO : AL_DER); bx += boton(bx, by, 30, "", on, B_IZQ + k, 0, 0); ico_alin(bx - 30 + 3, by + 3, k, on ? K_ACC2 : K_TXT); bx += 2; } else OCULTA(B_IZQ + k); }
    if (CABE(tw("Ajustar", 13, 0) + 16)) bx += boton(bx, by, tw("Ajustar", 13, 0) + 16, "Ajustar", e->ajustar, B_AJUSTAR, 0, 0) + 6; else OCULTA(B_AJUSTAR);
    if (!lleno) { sep(bx, by); bx += 7; }
    { uint8_t nf, d, mi; hj_tipo_numero(e, &nf, &d, &mi);
      static const char *const N[] = { "General", "N\303\272mero", "Moneda", "Porcentaje", "Fecha", "Hora", "Fecha y hora", "Texto", "Cient\303\255fica", "Personal." };
      const char *nn = N[e->nf == NF_CODIGO ? 9 : nf < 9 ? nf : 0];
      if (CABE(tw(nn, 13, 0) + 30)) bx += boton_menu(bx, by, nn, g_menu == M_FORMATO, B_FORMATO) + 2; else OCULTA(B_FORMATO); }
    if (CABE(28)) bx += boton(bx, by, 28, "\342\202\254", 0, B_MONEDA, 0, 0) + 2; else OCULTA(B_MONEDA);
    if (CABE(28)) bx += boton(bx, by, 28, "%", 0, B_PORCIENTO, 0, 0) + 2; else OCULTA(B_PORCIENTO);
    if (CABE(36)) bx += boton(bx, by, 36, ",0\342\206\220", 0, B_MENOSDEC, 0, 0) + 2; else OCULTA(B_MENOSDEC);
    if (CABE(40)) bx += boton(bx, by, 40, ",00\342\206\222", 0, B_MASDEC, 0, 0) + 6; else OCULTA(B_MASDEC);
    if (!lleno) { sep(bx, by); bx += 7; }
    if (CABE(34)) { bx += boton(bx, by, 34, "A", g_menu == M_COLOR, B_COLOR, WF_BOLD, 0); rect(bx - 26, by + 23, 18, 3, (e->color & 0x01000000u) ? (e->color & 0xFFFFFFu) : K_TXT); bx += 2; } else OCULTA(B_COLOR);
    if (CABE(34)) { bx += boton(bx, by, 34, "", g_menu == M_FONDO, B_FONDO, 0, 0); { int gx = bx - 26; fb_stroke_rrect_aa(gx + 2, by + 7, 14, 12, 2, 0xFFE5EAF2u); rect(gx, by + 23, 18, 3, (e->fondo & 0x01000000u) ? (e->fondo & 0xFFFFFFu) : K_FAINT); } bx += 2; } else OCULTA(B_FONDO);
    if (CABE(34)) { bx += boton(bx, by, 34, "", g_menu == M_BORDES, B_BORDES, 0, 0); { int gx = bx - 26, gy = by + 6; rect(gx, gy, 18, 1, K_TXT); rect(gx, gy + 17, 18, 1, K_TXT); rect(gx, gy, 1, 18, K_TXT); rect(gx + 17, gy, 1, 18, K_TXT); rect(gx + 9, gy, 1, 18, K_FAINT); rect(gx, gy + 9, 18, 1, K_FAINT); } bx += 6; } else OCULTA(B_BORDES);
    if (!lleno) { sep(bx, by); bx += 7; }
    if (CABE(32)) bx += boton(bx, by, 32, "\316\243", 0, B_SUMA, WF_BOLD, 0) + 2; else OCULTA(B_SUMA);
    if (CABE(34)) { bx += boton(bx, by, 34, "", g_menu == M_GRAF || g_menu == M_GRAF_NUEVO, B_GRAFICO, 0, 0); { int gx = bx - 26, gy = by + 7; rect(gx + 1, gy + 10, 4, 8, K_ACC); rect(gx + 7, gy + 4, 4, 14, K_ACC2); rect(gx + 13, gy + 7, 4, 11, 0x00FBBF24u); rect(gx, gy + 18, 18, 1, K_TXT); } bx += 2; } else OCULTA(B_GRAFICO);
    if (CABE(36)) bx += boton(bx, by, 36, "IA", g_barra == BA_IA, B_IA, WF_BOLD, 0x00A78BFAu) + 2; else OCULTA(B_IA);
    if (CABE(tw("Filas", 13, 0) + 30)) bx += boton_menu(bx, by, "Filas", g_menu == M_FILAS, B_FILAS) + 2; else OCULTA(B_FILAS);
    if (CABE(tw("Datos", 13, 0) + 30)) bx += boton_menu(bx, by, "Datos", g_menu == M_DATOS, B_DATOS) + 2; else OCULTA(B_DATOS);
    if (CABE(tw("Buscar", 13, 0) + 18)) bx += boton(bx, by, tw("Buscar", 13, 0) + 18, "Buscar", g_barra == BA_BUSCAR, B_BUSCAR, 0, 0) + 8; else OCULTA(B_BUSCAR);
    #undef CABE
    #undef OCULTA
    if (g_ntb_oculto) bx += boton(bx, by, 32, "\302\273", g_menu == M_MAS, B_MAS, WF_BOLD, 0) + 8;
    if (!g_ntb_oculto && bx < lim_nom + 40) {
        int ancho = x + w - 14 - bx; if (ancho > nomw) ancho = nomw;
        if (ancho > 40) txc(x + w - 14 - ancho, y, ancho, TB_H, t, 13, hoja_cambiado(g_doc) ? 0x00FBBF24u : K_DIM, WF_BOLD);
    }
}
static void pinta_barra_form(int x, int y, int w)
{
    rect(x, y, w, FB_H, K_WIN); rect(x, y + FB_H - 1, w, 1, K_LINE);
    struct vista *v = V(); char d[64], e2[16]; uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2);
    hj_dir(g_edit ? g_edit_f : v->af, g_edit ? g_edit_c : v->ac, d);
    if (!g_edit && !sel_es_una()) {
        if (f1 == 0 && f2 == HJ_FILAS - 1) { char a[8], b[8]; hj_nombre_col(c1, a); hj_nombre_col(c2, b); cpy(d, a, sizeof d); cat(d, ":", sizeof d); cat(d, b, sizeof d); }
        else { hj_dir(f1, c1, d); hj_dir(f2, c2, e2); cat(d, ":", sizeof d); cat(d, e2, sizeof d); }
    }
    fb_fill_rrect_aa(x + 10, y + 4, 110, 27, 6, 0xFF151E2Eu); txc(x + 18, y + 4, 100, 27, d, 13, K_TXT, WF_BOLD); hit(x + 10, y + 4, 110, 27, H_NOMBRE, 0);
    fb_fill_rrect_aa(x + 126, y + 4, 34, 27, 6, g_menu == M_FX ? 0xFF1A3558u : 0xFF151E2Eu); tx(x + 133, y + 4, 27, "fx", 13, K_ACC, WF_ITALIC | WF_BOLD); hit(x + 126, y + 4, 34, 27, H_FX, 0);
    int fx = x + 166, fw = w - 176;
    if (g_edit) { tf_render(&g_tf, fx, y + 4, fw, 27, g_foco == F_FORM, "", K_TXT); hit(fx, y + 4, fw, 27, H_FORM, 0); }
    else {
        static char b[HJ_FORM_MAX + 64]; struct hj_libro *l = L(); hj_texto_edicion(l, l->activa, v->af, v->ac, b, sizeof b);
        fb_fill_rrect_aa(fx, y + 4, fw, 27, 6, 0xFF111827u); txc(fx + 8, y + 4, fw - 16, 27, b, 13, K_TXT, b[0] == '=' ? WF_MONO : 0); hit(fx, y + 4, fw, 27, H_FORM, 0);
    }
}
/* una celda: fondo, texto con su formato y bordes */
static void pinta_celda(struct hj_libro *l, int h, uint32_t f, uint32_t c, int x, int y, int w, int hh, int sobra_der, int sobra_izq)
{
    const struct hj_celda *k = hj_celda_c(l, h, f, c);
    const struct hj_estilo *e = hj_estilo_de(l, k ? k->estilo : 0);
    if (e->fondo & 0x01000000u) rect(x, y, w - 1, hh - 1, e->fondo & 0xFFFFFFu);
    static char b[1024];
    int editando = g_edit && f == g_edit_f && c == g_edit_c && h == g_edit_hoja;
    if (editando) cpy(b, g_bform, sizeof b);
    else if (!k || k->t == HJ_VACIA) b[0] = 0;
    uint32_t col = 0; struct hj_valor val; val.t = HJ_VACIA;
    if (!editando && k && k->t != HJ_VACIA) { hj_valor_de(l, h, f, c, &val); hj_formatea(l, k->estilo, &val, b, sizeof b, &col); }
    if (b[0]) {
        int px = Z(e->tam ? e->tam * 13 / 11 : 13); if (px < 6) px = 6;
        int fl = (e->negrita ? WF_BOLD : 0) | (e->cursiva ? WF_ITALIC : 0);
        uint32_t tc = col ? (col & 0xFFFFFFu) : (e->color & 0x01000000u) ? (e->color & 0xFFFFFFu) : P_TXT;
        if (val.t == HJ_ERR) tc = col ? tc : 0x00B91C1Cu;
        int al = e->alinear;
        if (al == AL_AUTO) al = editando ? AL_IZQ : (val.t == HJ_NUM ? AL_DER : val.t == HJ_BOOL || val.t == HJ_ERR ? AL_CENTRO : AL_IZQ);
        int ancho = tw(b, px, fl), pad = 4;
        int asc = has_wf() ? wf_ascent(px) : 10, desc = has_wf() ? wf_descent(px) : 2, lh = asc + desc + 2;
        if (e->ajustar && !editando) {
            /* varias lineas, cortando por palabras */
            fb_set_clip(x, y, w - 1, hh - 1);
            int ly = e->valinear == 2 ? y + 2 : e->valinear == 1 ? y + 2 : y + 2; const char *s = b;
            while (*s && ly < y + hh) {
                const char *fin = s, *ult_esp = 0; while (*fin && *fin != '\n') { char tmp[512]; int n = (int)(fin - s) + 1; if (n > 511) break; for (int i = 0; i < n; ++i) tmp[i] = s[i]; tmp[n] = 0; if (tw(tmp, px, fl) > w - 2 * pad) break; if (*fin == ' ') ult_esp = fin; ++fin; }
                if (*fin && *fin != '\n' && ult_esp) fin = ult_esp;
                if (fin == s) fin = s + 1;
                char ln[512]; int n = (int)(fin - s); if (n > 511) n = 511; for (int i = 0; i < n; ++i) ln[i] = s[i]; ln[n] = 0;
                int lx = al == AL_DER ? x + w - pad - tw(ln, px, fl) : al == AL_CENTRO ? x + (w - tw(ln, px, fl)) / 2 : x + pad;
                if (has_wf()) wf_draw(lx, ly + asc, ln, (uint32_t)n, px, tc, fl, 255); else font_draw_string((uint32_t)lx, (uint32_t)ly, ln, tc, 1);
                ly += lh; s = fin; while (*s == ' ' || *s == '\n') ++s;
            }
            fb_reset_clip();
        } else {
            if (val.t == HJ_NUM && !editando && ancho > w - 2 * pad && e->nf == NF_GENERAL) {
                /* General: como Excel, menos decimales hasta que quepa */
                for (int d = 9; d >= 0 && ancho > w - 2 * pad; --d) {
                    hj_num_txt(val.n, d, 0, b, sizeof b); int n = sl(b), coma = -1; for (int i = 0; i < n; ++i) if (b[i] == ',') coma = i;
                    if (coma >= 0) { while (n > coma + 1 && b[n - 1] == '0') b[--n] = 0; if (n == coma + 1) b[--n] = 0; }
                    ancho = tw(b, px, fl);
                }
            }
            if (val.t == HJ_NUM && !editando && ancho > w - 2 * pad) {
                /* un numero que no cabe: ### como Excel */
                int nh = (w - 2 * pad) / (tw("#", px, fl) > 0 ? tw("#", px, fl) : 7); if (nh < 1) nh = 1; if (nh > 60) nh = 60;
                for (int i = 0; i < nh; ++i) b[i] = '#';
                b[nh] = 0; ancho = tw(b, px, fl);
            }
            int lx = al == AL_DER ? x + w - pad - ancho : al == AL_CENTRO ? x + (w - ancho) / 2 : x + pad;
            int by = e->valinear == 1 ? y + 2 + asc : e->valinear == 2 ? y + (hh + asc - desc) / 2 : y + hh - desc - 4;
            if (by - asc < y) by = y + asc;
            int cx0 = x - (al == AL_IZQ ? 0 : sobra_izq), cx1 = x + w - 1 + (al == AL_DER ? 0 : sobra_der);
            if (al == AL_CENTRO) { cx0 = x - sobra_izq; cx1 = x + w - 1 + sobra_der; }
            fb_set_clip(cx0, y, cx1 - cx0, hh - 1);
            if (has_wf()) wf_draw(lx, by, b, (uint32_t)sl(b), px, tc, fl, 255); else font_draw_string((uint32_t)(lx < 0 ? 0 : lx), (uint32_t)(by - 10), b, tc, 1);
            if (e->subrayado) rect(lx, by + 2, ancho, 1, tc);
            if (e->tachado) rect(lx, by - asc / 2 + 1, ancho, 1, tc);
            fb_reset_clip();
        }
    }
    if (e->bordes) {
        uint32_t bc = 0x00303030u;
        if (e->bordes & 1) rect(x - 1, y - 1, w + 1, 1, bc);
        if (e->bordes & 2) rect(x - 1, y + hh - 1, w + 1, 1, bc);
        if (e->bordes & 4) rect(x - 1, y - 1, 1, hh + 1, bc);
        if (e->bordes & 8) rect(x + w - 1, y - 1, 1, hh + 1, bc);
    }
}
static int celda_vacia(struct hj_libro *l, int h, uint32_t f, uint32_t c)
{
    if (g_edit && f == g_edit_f && c == g_edit_c && h == g_edit_hoja) return 0;
    const struct hj_celda *k = hj_celda_c(l, h, f, c); return !k || k->t == HJ_VACIA;
}
static void calcula_vista(void)
{
    struct hj_hoja *s = S(); struct vista *v = V(); g_nvc = g_nvr = 0;
    int x = g_gx;
    for (uint32_t c = 0; c < s->inmov_c && g_nvc < MAXVC && x < g_gx + g_gw; ++c) { int w = ancho_col(c); g_vc[g_nvc].i = c; g_vc[g_nvc].x = x; g_vc[g_nvc].w = w; ++g_nvc; x += w; }
    int x0 = x; g_vis_c = 0;
    for (uint32_t c = v->left < s->inmov_c ? s->inmov_c : v->left; c < HJ_COLS && g_nvc < MAXVC && x < g_gx + g_gw; ++c) { int w = ancho_col(c); g_vc[g_nvc].i = c; g_vc[g_nvc].x = x; g_vc[g_nvc].w = w; ++g_nvc; x += w; if (x <= g_gx + g_gw) ++g_vis_c; }
    (void)x0;
    int y = g_gy;
    for (uint32_t f = 0; f < s->inmov_f && g_nvr < MAXVR && y < g_gy + g_gh; ++f) { if (hj_fila_oculta(s, f)) continue; int hh = alto_fila(f); g_vr[g_nvr].i = f; g_vr[g_nvr].x = y; g_vr[g_nvr].w = hh; ++g_nvr; y += hh; }
    g_vis_f = 0;
    for (uint32_t f = v->top < s->inmov_f ? s->inmov_f : v->top; f < HJ_FILAS && g_nvr < MAXVR && y < g_gy + g_gh; ++f) { if (hj_fila_oculta(s, f)) continue; int hh = alto_fila(f); g_vr[g_nvr].i = f; g_vr[g_nvr].x = y; g_vr[g_nvr].w = hh; ++g_nvr; y += hh; if (y <= g_gy + g_gh) ++g_vis_f; }
}
static void pinta_graficos(void);
static void pinta_cuadricula(int x, int y, int w, int h)
{
    struct hj_libro *l = L(); int hj = l->activa; struct hj_hoja *s = &l->h[hj]; struct vista *v = V();
    /* ancho de la cabecera de filas: lo que ocupa el numero mas largo visible */
    char nb[16]; nb[0] = 0; num(nb, sizeof nb, (int64_t)(v->top + 60 > s->max_fila ? v->top + 60 : s->max_fila));
    g_rhw = tw(nb, Z(12), 0) + 14; if (g_rhw < 36) g_rhw = 36;
    g_gx = x + g_rhw; g_gy = y + Z(CH_H); g_gw = w - g_rhw - 12; g_gh = h - Z(CH_H) - 12;
    if (g_gw < 20 || g_gh < 20) return;
    if (v->top < s->inmov_f) v->top = s->inmov_f;
    if (v->left < s->inmov_c) v->left = s->inmov_c;
    while (v->top + 1 < HJ_FILAS && hj_fila_oculta(s, v->top)) ++v->top;
    calcula_vista();
    rect(x, y, w, h, P_BG);
    uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2);
    /* cabeceras */
    rect(x, y, w, Z(CH_H), P_HEAD); rect(x, y, g_rhw, h, P_HEAD);
    for (int i = 0; i < g_nvc; ++i) {
        struct vcol *q = &g_vc[i]; int on = q->i >= c1 && q->i <= c2; char cn[8]; hj_nombre_col(q->i, cn);
        if (on) rect(q->x, y, q->w, Z(CH_H), P_HSEL);
        int cw = q->x + q->w > g_gx + g_gw ? g_gx + g_gw - q->x : q->w;
        txc(q->x + (q->w - tw(cn, Z(12), on ? WF_BOLD : 0)) / 2, y, cw, Z(CH_H), cn, Z(12), on ? 0x00166534u : P_HTXT, on ? WF_BOLD : 0);
        rect(q->x + q->w - 1, y, 1, Z(CH_H), P_GRID);
        hit(q->x, y, q->w - 4, Z(CH_H), H_COLH, (int)q->i); hit(q->x + q->w - 4, y, 8, Z(CH_H), H_ANCHO, (int)q->i);
    }
    for (int i = 0; i < g_nvr; ++i) {
        struct vcol *q = &g_vr[i]; int on = q->i >= f1 && q->i <= f2; char rn[16]; rn[0] = 0; num(rn, sizeof rn, (int64_t)q->i + 1);
        if (on) rect(x, q->x, g_rhw, q->w, P_HSEL);
        int ch = q->x + q->w > g_gy + g_gh ? g_gy + g_gh - q->x : q->w;
        txc(x + g_rhw - 6 - tw(rn, Z(12), on ? WF_BOLD : 0), q->x, g_rhw - 6, ch, rn, Z(12), on ? 0x00166534u : P_HTXT, on ? WF_BOLD : 0);
        rect(x, q->x + q->w - 1, g_rhw, 1, P_GRID);
        hit(x, q->x, g_rhw, q->w - 3, H_FILH, (int)q->i); hit(x, q->x + q->w - 3, g_rhw, 6, H_ALTO, (int)q->i);
    }
    rect(x + g_rhw - 1, y, 1, h, P_FROZ); rect(x, y + Z(CH_H) - 1, w, 1, P_FROZ);
    hit(x, y, g_rhw, Z(CH_H), H_ESQ, 0);
    hit(g_gx, g_gy, g_gw, g_gh, H_GRID, 0);
    fb_set_clip(g_gx, g_gy, g_gw, g_gh);
    /* rejilla */
    for (int i = 0; i < g_nvc; ++i) rect(g_vc[i].x + g_vc[i].w - 1, g_gy, 1, g_gh, P_GRID);
    for (int i = 0; i < g_nvr; ++i) rect(g_gx, g_vr[i].x + g_vr[i].w - 1, g_gw, 1, P_GRID);
    /* celdas */
    for (int r = 0; r < g_nvr; ++r) {
        struct vcol *R = &g_vr[r];
        for (int i = 0; i < g_nvc; ++i) {
            struct vcol *C = &g_vc[i];
            int vacia = celda_vacia(l, hj, R->i, C->i);
            const struct hj_celda *k = vacia ? 0 : hj_celda_c(l, hj, R->i, C->i);
            if (vacia) { const struct hj_celda *z = hj_celda_c(l, hj, R->i, C->i); if (!z || (!z->estilo)) continue; }
            /* el texto puede invadir celdas vacias de los lados */
            int sd = 0, si = 0;
            if (!vacia && (!k || k->t == HJ_TXT || g_edit)) {
                for (int j = i + 1; j < g_nvc && celda_vacia(l, hj, R->i, g_vc[j].i) && (g_vc[j].i >= s->inmov_c || C->i < s->inmov_c); ++j) { sd += g_vc[j].w; if (sd > 2000) break; }
                for (int j = i - 1; j >= 0 && celda_vacia(l, hj, R->i, g_vc[j].i); --j) { si += g_vc[j].w; if (si > 2000) break; }
            }
            pinta_celda(l, hj, R->i, C->i, C->x, R->x, C->w, R->w, sd, si);
        }
    }
    /* filtro: botones en la fila de titulos */
    if (s->hay_filtro) {
        int r = busca_vr(s->filtro_r1);
        if (r >= 0) for (int i = 0; i < g_nvc; ++i) {
            if (g_vc[i].i < s->filtro_c1 || g_vc[i].i > s->filtro_c2) continue;
            int act = 0; for (uint32_t k = 0; k < s->nfil; ++k) if (s->fil[k].col == g_vc[i].i) act = 1;
            int bx = g_vc[i].x + g_vc[i].w - 19, by = g_vr[r].x + (g_vr[r].w - 16) / 2;
            fb_fill_rrect_aa(bx, by, 16, 16, 3, act ? 0xFF16A34Au : 0xFFE5E7EBu); tri(bx + 5, by + 6, act ? 0x00FFFFFFu : 0x00374151u);
            hit(bx, by, 16, 16, H_FILTRO, (int)g_vc[i].i);
        }
    }
    /* separadores de lo inmovilizado */
    if (s->inmov_c) { int i = busca_vc(s->inmov_c - 1); if (i >= 0) rect(g_vc[i].x + g_vc[i].w - 1, g_gy, 2, g_gh, P_FROZ); }
    if (s->inmov_f) { int i = -1; for (int k = 0; k < g_nvr; ++k) if (g_vr[k].i < s->inmov_f) i = k; if (i >= 0) rect(g_gx, g_vr[i].x + g_vr[i].w - 1, g_gw, 2, P_FROZ); }
    /* seleccion */
    {
        int sx0 = -1, sy0 = -1, sx1 = -1, sy1 = -1;
        for (int i = 0; i < g_nvc; ++i) if (g_vc[i].i >= c1 && g_vc[i].i <= c2) { if (sx0 < 0) sx0 = g_vc[i].x; sx1 = g_vc[i].x + g_vc[i].w; }
        for (int i = 0; i < g_nvr; ++i) if (g_vr[i].i >= f1 && g_vr[i].i <= f2) { if (sy0 < 0) sy0 = g_vr[i].x; sy1 = g_vr[i].x + g_vr[i].w; }
        if (sx0 >= 0 && sy0 >= 0) {
            if (!sel_es_una()) {
                fb_fill_rect_alpha(sx0, sy0, sx1 - sx0 - 1, sy1 - sy0 - 1, 0x2A22C55Eu);
                int ai = busca_vc(v->ac), ar = busca_vr(v->af); if (ai >= 0 && ar >= 0) rect(g_vc[ai].x, g_vr[ar].x, g_vc[ai].w - 1, g_vr[ar].w - 1, P_BG), pinta_celda(l, hj, v->af, v->ac, g_vc[ai].x, g_vr[ar].x, g_vc[ai].w, g_vr[ar].w, 0, 0);
            }
            uint32_t bc = 0x0016A34Au;
            rect(sx0 - 1, sy0 - 1, sx1 - sx0 + 1, 2, bc); rect(sx0 - 1, sy1 - 2, sx1 - sx0 + 1, 2, bc);
            rect(sx0 - 1, sy0 - 1, 2, sy1 - sy0 + 1, bc); rect(sx1 - 2, sy0 - 1, 2, sy1 - sy0 + 1, bc);
            if (!g_edit) { rect(sx1 - 4, sy1 - 4, 7, 7, P_BG); rect(sx1 - 3, sy1 - 3, 5, 5, bc); hit(sx1 - 5, sy1 - 5, 10, 10, H_RELLENO, 0); }
        }
    }
    /* destino de rellenar arrastrando */
    if (g_ar == AR_RELLENO) {
        uint32_t tf = g_ar_f > f2 ? g_ar_f : f2, tc = g_ar_c > c2 ? g_ar_c : c2;
        if ((tf - f2) >= (tc - c2)) tc = c2; else tf = f2;
        int sx0 = -1, sy0 = -1, sx1 = -1, sy1 = -1;
        for (int i = 0; i < g_nvc; ++i) if (g_vc[i].i >= c1 && g_vc[i].i <= tc) { if (sx0 < 0) sx0 = g_vc[i].x; sx1 = g_vc[i].x + g_vc[i].w; }
        for (int i = 0; i < g_nvr; ++i) if (g_vr[i].i >= f1 && g_vr[i].i <= tf) { if (sy0 < 0) sy0 = g_vr[i].x; sy1 = g_vr[i].x + g_vr[i].w; }
        if (sx0 >= 0 && sy0 >= 0) { fb_stroke_rrect_aa(sx0 - 1, sy0 - 1, sx1 - sx0 + 1, sy1 - sy0 + 1, 0, 0xFF6B7280u); }
    }
    /* lo copiado: borde a rayas */
    if (g_copia.doc == g_doc && g_copia.hoja == hj && g_copia.seq == clip_seq()) {
        int sx0 = -1, sy0 = -1, sx1 = -1, sy1 = -1;
        for (int i = 0; i < g_nvc; ++i) if (g_vc[i].i >= g_copia.c1 && g_vc[i].i <= g_copia.c2) { if (sx0 < 0) sx0 = g_vc[i].x; sx1 = g_vc[i].x + g_vc[i].w; }
        for (int i = 0; i < g_nvr; ++i) if (g_vr[i].i >= g_copia.f1 && g_vr[i].i <= g_copia.f2) { if (sy0 < 0) sy0 = g_vr[i].x; sy1 = g_vr[i].x + g_vr[i].w; }
        if (sx0 >= 0 && sy0 >= 0) for (int k = sx0; k < sx1; k += 8) { rect(k, sy0, 4, 1, 0x002563EBu); rect(k, sy1 - 1, 4, 1, 0x002563EBu); }
        if (sx0 >= 0 && sy0 >= 0) for (int k = sy0; k < sy1; k += 8) { rect(sx0, k, 1, 4, 0x002563EBu); rect(sx1 - 1, k, 1, 4, 0x002563EBu); }
    }
    fb_reset_clip();
    pinta_graficos();                                                  /* SOMA_360_HOJA_GRAFICOS */
    /* barras de desplazamiento */
    {
        uint32_t tot_f = s->max_fila + 40 > v->top + 60 ? s->max_fila + 40 : v->top + 60, tot_c = s->max_col + 6 > v->left + 20 ? s->max_col + 6 : v->left + 20;
        int vx = x + w - 10, vy = g_gy, vh = g_gh; rect(vx, vy, 10, vh, 0x00F3F4F6u);
        int th = vh * (g_vis_f ? (int)g_vis_f : 1) / (int)tot_f; if (th < 24) th = 24; if (th > vh) th = vh;
        int ty = vy + (int)((uint64_t)(vh - th) * v->top / (tot_f > (uint32_t)g_vis_f ? tot_f - (uint32_t)g_vis_f : 1)); if (ty + th > vy + vh) ty = vy + vh - th;
        fb_fill_rrect_aa(vx + 2, ty, 6, th, 3, 0xFFB0B7C3u); hit(vx, vy, 10, vh, H_VBAR, 0);
        int hx = g_gx, hy = y + h - 10, hw = g_gw; rect(hx, hy, hw, 10, 0x00F3F4F6u);
        int tw2 = hw * (g_vis_c ? (int)g_vis_c : 1) / (int)tot_c; if (tw2 < 24) tw2 = 24; if (tw2 > hw) tw2 = hw;
        int txx = hx + (int)((uint64_t)(hw - tw2) * v->left / (tot_c > (uint32_t)g_vis_c ? tot_c - (uint32_t)g_vis_c : 1)); if (txx + tw2 > hx + hw) txx = hx + hw - tw2;
        fb_fill_rrect_aa(txx, hy + 2, tw2, 6, 3, 0xFFB0B7C3u); hit(hx, hy, hw, 10, H_HBAR, 0);
    }
}
static void num_corto(f64 v, char *b, uint32_t cap)
{
    hj_num_txt(v, 2, 1, b, cap); int n = sl(b), coma = -1; for (int i = 0; i < n; ++i) if (b[i] == ',') coma = i;
    if (coma >= 0) { while (n > coma + 1 && b[n - 1] == '0') b[--n] = 0; if (n == coma + 1) b[--n] = 0; }
}
static void estado_sel(char *o, int cap)
{
    struct hj_libro *l = L(); struct vista *v = V(); uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2);
    uint32_t key = f1 * 2654435761u ^ c1 * 40503u ^ f2 * 97u ^ c2 ^ (uint32_t)l->activa * 7919u ^ (uint32_t)g_doc;
    if (g_st_ver == l->version && g_st_key == key) { cpy(o, g_st, cap); return; }
    g_st_ver = l->version; g_st_key = key; g_st[0] = 0; (void)v;
    if (f1 == f2 && c1 == c2) { cpy(o, "", cap); return; }
    struct hj_hoja *s = &l->h[l->activa]; f64 suma = 0; uint32_t nn = 0, na = 0;
    for (int32_t i = 0; i < s->nc; ++i) {
        const struct hj_celda *x = &s->c[i];
        if (x->fila < f1 || x->fila > f2 || x->col < c1 || x->col > c2 || x->t == HJ_VACIA) continue;
        if (hj_fila_oculta(s, x->fila)) continue;
        ++na; if (x->t == HJ_NUM) { suma = f64_add(suma, x->n); ++nn; }
    }
    char b[64];
    if (nn) {
        cat(g_st, "Suma: ", sizeof g_st); num_corto(suma, b, sizeof b); cat(g_st, b, sizeof g_st);
        cat(g_st, "   Promedio: ", sizeof g_st); num_corto(f64_div(suma, f64_from_i64((int64_t)nn)), b, sizeof b); cat(g_st, b, sizeof g_st);
        cat(g_st, "   ", sizeof g_st);
    }
    cat(g_st, "Recuento: ", sizeof g_st); b[0] = 0; num(b, sizeof b, na); cat(g_st, b, sizeof g_st);
    cpy(o, g_st, cap);
}
#include "hoja_gui_graf.inc"                                       /* SOMA_360_HOJA_GRAFICOS */
static void pinta_pestanas(int x, int y, int w)
{
    struct hj_libro *l = L(); rect(x, y, w, TAB_H, K_BAR); rect(x, y, w, 1, K_LINE);
    int bx = x + 8;
    fb_fill_rrect_aa(bx, y + 4, 24, 22, 6, 0xFF151E2Eu); tx(bx + 7, y + 4, 22, "+", 15, K_ACC, WF_BOLD); hit(bx, y + 4, 24, 22, H_PEST_MAS, 0); bx += 30;
    if (g_tab_off > 0) { fb_fill_rrect_aa(bx, y + 4, 20, 22, 6, 0xFF151E2Eu); tx(bx + 6, y + 4, 22, "<", 13, K_TXT, WF_BOLD); hit(bx, y + 4, 20, 22, H_PEST_IZQ, 0); bx += 24; }
    char st[160]; estado_sel(st, sizeof st);
    int stw = st[0] ? tw(st, 12, 0) + 20 : 0; if (stw > w / 2) stw = w / 2;
    int lim = x + w - stw - 40;
    if (g_tab_off >= l->nh) g_tab_off = 0;
    int k;
    for (k = g_tab_off; k < l->nh; ++k) {
        const char *n = l->h[k].nombre; int tw2 = tw(n, 12, k == l->activa ? WF_BOLD : 0) + 24;
        if (bx + tw2 > lim) break;
        int on = k == l->activa;
        fb_fill_rrect_aa(bx, y + 3, tw2, 24, 6, on ? 0xFFFFFFFFu : 0xFF151E2Eu);
        if (on) rect(bx + 6, y + 24, tw2 - 12, 2, K_ACC);
        tx(bx + 12, y + 3, 24, n, 12, on ? 0x00166534u : K_DIM, on ? WF_BOLD : 0);
        hit(bx, y + 3, tw2, 24, H_PEST, k); bx += tw2 + 4;
    }
    if (k < l->nh) { fb_fill_rrect_aa(bx, y + 4, 20, 22, 6, 0xFF151E2Eu); tx(bx + 6, y + 4, 22, ">", 13, K_TXT, WF_BOLD); hit(bx, y + 4, 20, 22, H_PEST_DER, 0); }
    if (st[0]) txc(x + w - stw, y, stw - 10, TAB_H, st, 12, K_DIM, 0);
}
static int ia_con_respuesta(void) { int e = hoja_ia_estado(); return e == HIA_LISTA || e == HIA_ERROR || e == HIA_PENSANDO; }
static int alto_barra(void) { if (!g_barra) return 0; if (g_barra == BA_IA) return 74 + (ia_con_respuesta() ? 150 : 0); return g_nsug ? 118 : 74; }
/* lo que la IA sabe de la hoja: nombre, celda activa, seleccion y las celdas usadas (direccion=valor, formulas entre parentesis) */
static void contexto_ia(char *o, uint32_t cap)
{
    struct hj_libro *l = L(); int h = l->activa; struct hj_hoja *s = &l->h[h]; struct vista *v = V(); o[0] = 0;
    char d[16], e2[16]; static char b[512];
    cat(o, "Libro: ", (int)cap); { const char *r = hoja_ruta(g_doc), *q = r; for (const char *z = r; *z; ++z) if (*z == '/') q = z + 1; cat(o, q, (int)cap); }
    cat(o, ". Hojas:", (int)cap); for (int k = 0; k < l->nh; ++k) { cat(o, k ? ", " : " ", (int)cap); cat(o, l->h[k].nombre, (int)cap); }
    cat(o, ". Hoja activa: ", (int)cap); cat(o, s->nombre, (int)cap);
    hj_dir(v->af, v->ac, d); cat(o, ". Celda activa: ", (int)cap); cat(o, d, (int)cap);
    if (!sel_es_una()) { uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2); hj_dir(f1, c1, d); hj_dir(f2, c2, e2); cat(o, ". Selecci\303\263n: ", (int)cap); cat(o, d, (int)cap); cat(o, ":", (int)cap); cat(o, e2, (int)cap); }
    cat(o, ".\nContenido:\n", (int)cap);
    uint32_t mf = s->max_fila < 300 ? s->max_fila : 300, mc = s->max_col < 30 ? s->max_col : 30;
    for (uint32_t f = 0; f < mf; ++f) {
        if ((uint32_t)sl(o) + 600 >= cap) { cat(o, "(\342\200\246 la hoja sigue)\n", (int)cap); break; }
        int algo = 0;
        for (uint32_t c = 0; c < mc; ++c) {
            const struct hj_celda *x = hj_celda_c(l, h, f, c); if (!x || x->t == HJ_VACIA) continue;
            hj_dir(f, c, d); hj_texto_ver(l, h, f, c, b, 120); for (char *z = b; *z; ++z) if (*z == '\n' || *z == '|') *z = ' ';
            cat(o, algo ? " | " : "", (int)cap); cat(o, d, (int)cap); cat(o, "=", (int)cap); cat(o, b, (int)cap);
            if (x->es_form) { hj_texto_edicion(l, h, f, c, b, 200); cat(o, " (", (int)cap); cat(o, b, (int)cap); cat(o, ")", (int)cap); }
            algo = 1;
        }
        if (algo) cat(o, "\n", (int)cap);
    }
}
static void pinta_ia(int x, int y, int w, int h)
{
    fb_fill_rrect_aa(x, y, w, h, 10, 0xFF101826u); fb_stroke_rrect_aa(x, y, w, h, 10, 0x66A78BFAu);
    int e = hoja_ia_estado();
    if (e == HIA_PENSANDO) { tx(x + 16, y, 40, "La IA est\303\241 leyendo la hoja y pensando\342\200\246", 13, 0x00C4B5FDu, 0); return; }
    const char *t = hoja_ia_texto(), *f = hoja_ia_formula(), *tb = hoja_ia_tabla();
    /* texto en lineas que caben (como mucho las que quepan; el resto se corta) */
    int ly = y + 8, lh = 18, maxy = y + h - 40;
    fb_set_clip(x + 12, y + 4, w - 24, h - 44);
    const char *p = t;
    while (*p && ly + lh <= maxy) {
        char ln[400]; int k = 0, ult = -1; const char *q = p;
        while (*q && *q != '\n' && k < 398) { ln[k] = *q; ln[k + 1] = 0; if (*q == ' ') ult = k; if (tw(ln, 13, 0) > w - 32) break; ++k; ++q; }
        if (*q && *q != '\n' && ult > 0) { k = ult; q = p + ult + 1; }
        ln[k] = 0;
        tx(x + 16, ly, lh, ln, 13, e == HIA_ERROR ? 0x00FCA5A5u : K_TXT, 0); ly += lh;
        p = *q == '\n' ? q + 1 : q;
    }
    if (f[0] && ly + lh <= maxy) { tx(x + 16, ly, lh, f, 13, K_ACC, WF_MONO); ly += lh; }
    fb_reset_clip();
    int bx = x + 12, by = y + h - 36;
    if (f[0]) { char t2[64]; char dd[16]; struct vista *v = V(); hj_dir(v->af, v->ac, dd); cpy(t2, "Poner la f\303\263rmula en ", sizeof t2); cat(t2, dd, sizeof t2);
        int bw = tw(t2, 13, WF_BOLD) + 24; fb_fill_rrect_aa(bx, by, bw, 28, 7, 0xFF16A34Au); tx(bx + 12, by, 28, t2, 13, 0x00FFFFFFu, WF_BOLD); hit(bx, by, bw, 28, H_MENU_IT, A_IA_FORMULA); bx += bw + 8; }
    if (tb[0]) { const char *t2 = "Pegar la tabla aqu\303\255"; int bw = tw(t2, 13, WF_BOLD) + 24; fb_fill_rrect_aa(bx, by, bw, 28, 7, 0xFF16A34Au); tx(bx + 12, by, 28, t2, 13, 0x00FFFFFFu, WF_BOLD); hit(bx, by, bw, 28, H_MENU_IT, A_IA_TABLA); bx += bw + 8; }
    if (e == HIA_LISTA) { int bw = tw("Copiar", 13, 0) + 24; fb_fill_rrect_aa(bx, by, bw, 28, 7, 0xFF1F2A3Cu); tx(bx + 12, by, 28, "Copiar", 13, K_TXT, 0); hit(bx, by, bw, 28, H_MENU_IT, A_IA_COPIA); bx += bw + 8; }
    { int bw = tw("Olvidar", 13, 0) + 24; fb_fill_rrect_aa(bx, by, bw, 28, 7, 0xFF1F2A3Cu); tx(bx + 12, by, 28, "Olvidar", 13, K_DIM, 0); hit(bx, by, bw, 28, H_MENU_IT, A_IA_CIERRA); }
}
static void pinta_barra(int x, int y, int w)
{
    int hb = alto_barra(); rect(x, y, w, hb, K_CARD); rect(x, y, w, 1, K_LINE);
    static const char *const T[] = { "", "Abrir una hoja de c\303\241lculo (.xlsx, .ods, .csv)", "Guardar como (la extensi\303\263n elige el formato)", "Buscar", "Nombre de la hoja",
        "Ir a la celda o rango", "Mostrar las filas de esta columna que cumplan", "C\303\263digo de formato (como en Excel: #.##0,00 \"kg\"; 0,0%; d/m/aaaa)", "T\303\255tulo del gr\303\241fico", "Pregunta a la IA sobre esta hoja: \302\253suma los gastos por concepto\302\273, \302\253f\303\263rmula del IVA para B2\302\273, \302\253haz una tabla de amortizaci\303\263n\302\273" };
    char t[160]; cpy(t, T[g_barra], sizeof t);
    if (g_barra == BA_FILTRO) { char cn[8]; hj_nombre_col(g_barra_col, cn); cat(t, " (columna ", sizeof t); cat(t, cn, sizeof t); cat(t, "): =Madrid  >100  <>0  *ana*  vac\303\255o = todas", sizeof t); }
    txc(x + 16, y + 4, w - 32, 22, t, 12, K_DIM, 0);
    int fy = y + 28, bw = 0;
    if (g_barra == BA_IA && ia_con_respuesta()) { pinta_ia(x + 16, y + 74, w - 32, 140); }
    const char *ok = g_barra == BA_IA ? "Preguntar" : g_barra == BA_ABRIR ? "Abrir" : g_barra == BA_GUARDAR ? "Guardar" : g_barra == BA_BUSCAR ? "Siguiente" : g_barra == BA_FILTRO ? "Filtrar" : "Aceptar";
    int okw = tw(ok, 13, WF_BOLD) + 28;
    int extra = g_barra == BA_GUARDAR ? 3 * 64 + 12 : g_barra == BA_BUSCAR ? tw("Reemplazar todo", 13, 0) + 32 : g_barra == BA_FILTRO ? tw("Todas", 13, 0) + 30 : 0;
    bw = okw + 40 + extra;
    int fw = w - 32 - bw;
    if (g_barra == BA_BUSCAR) {
        int f1w = fw / 2 - 6;
        tf_render(&g_tbarra, x + 16, fy, f1w, 30, g_foco == F_BARRA, "Texto a buscar", K_TXT); hit(x + 16, fy, f1w, 30, H_BARRA, 0);
        tf_render(&g_tbarra2, x + 16 + f1w + 12, fy, f1w, 30, g_foco == F_BARRA2, "Reemplazar por\342\200\246", K_TXT); hit(x + 16 + f1w + 12, fy, f1w, 30, H_BARRA2, 0);
    } else { tf_render(&g_tbarra, x + 16, fy, fw, 30, g_foco == F_BARRA, "", K_TXT); hit(x + 16, fy, fw, 30, H_BARRA, 0); }
    int bx = x + 16 + fw + 10;
    fb_fill_rrect_aa(bx, fy, okw, 30, 8, 0xFF16A34Au); tx(bx + 14, fy, 30, ok, 13, 0x00FFFFFFu, WF_BOLD); hit(bx, fy, okw, 30, H_BARRA_OK, 0); bx += okw + 6;
    if (g_barra == BA_GUARDAR) { static const char *const X[] = { ".xlsx", ".ods", ".csv" }; for (int i = 0; i < 3; ++i) { int on = termina(g_bbarra, X[i]); fb_fill_rrect_aa(bx, fy, 60, 30, 8, on ? 0xFF1A3558u : 0xFF151E2Eu); tx(bx + (60 - tw(X[i], 13, 0)) / 2, fy, 30, X[i], 13, on ? K_ACC2 : K_TXT, 0); hit(bx, fy, 60, 30, H_BARRA_ALT, i + 1); bx += 64; } }
    if (g_barra == BA_BUSCAR) { int ww = tw("Reemplazar todo", 13, 0) + 24; fb_fill_rrect_aa(bx, fy, ww, 30, 8, 0xFF151E2Eu); tx(bx + 12, fy, 30, "Reemplazar todo", 13, K_TXT, 0); hit(bx, fy, ww, 30, H_BARRA_ALT, 1); bx += ww + 6; }
    if (g_barra == BA_FILTRO) { int ww = tw("Todas", 13, 0) + 24; fb_fill_rrect_aa(bx, fy, ww, 30, 8, 0xFF151E2Eu); tx(bx + 12, fy, 30, "Todas", 13, K_TXT, 0); hit(bx, fy, ww, 30, H_BARRA_ALT, 1); bx += ww + 6; }
    fb_fill_rrect_aa(bx, fy, 30, 30, 8, 0xFF151E2Eu); tx(bx + 10, fy, 30, "\303\227", 14, K_DIM, 0); hit(bx, fy, 30, 30, H_BARRA_NO, 0);
    if (g_nsug) {
        int sx = x + 16, sy = fy + 38;
        for (int i = 0; i < g_nsug; ++i) {
            const char *n = g_sug[i]; const char *b = n; if (g_barra == BA_ABRIR) for (const char *z = n; *z; ++z) if (*z == '/') b = z + 1;
            int ww = tw(b, 12, 0) + 22; if (ww > 260) ww = 260;
            if (sx + ww > x + w - 16) break;
            fb_fill_rrect_aa(sx, sy, ww, 28, 8, 0xFF1F2A3Cu); txc(sx + 11, sy, ww - 18, 28, b, 12, K_TXT, 0); hit(sx, sy, ww, 28, H_SUG, i); sx += ww + 6;
        }
    }
}
static void pinta_menu(void)
{
    if (!g_menu) return;
    int x = g_menu_x, y = g_menu_y;
    if (g_menu == M_COLOR || g_menu == M_FONDO) {
        int w = 10 * 26 + 16, h = 2 * 26 + 50;
        if (x + w > g_wx + g_ww) x = g_wx + g_ww - w - 4;
        fb_draw_shadow(x, y, w, h, 10, 12, 4, 0x66000000u); fb_fill_rrect_aa(x, y, w, h, 10, 0xFF1A2333u);
        for (int i = 0; i < 20; ++i) { int sx = x + 8 + (i % 10) * 26, sy = y + 8 + (i / 10) * 26; rect(sx, sy, 22, 22, PALETA[i] & 0xFFFFFFu); fb_stroke_rrect_aa(sx, sy, 22, 22, 2, 0x55FFFFFFu); hit(sx, sy, 22, 22, H_SWATCH, i); }
        int ay = y + 2 * 26 + 12; fb_fill_rrect_aa(x + 8, ay, w - 16, 30, 7, 0xFF151E2Eu); tx(x + 18, ay, 30, g_menu == M_COLOR ? "Autom\303\241tico (negro)" : "Sin relleno", 13, K_TXT, 0); hit(x + 8, ay, w - 16, 30, H_SWATCH, -1);
        return;
    }
    int w = 0; for (int i = 0; i < g_nmi; ++i) { int t = tw(g_mi[i].t, 13, 0) + 40; if (t > w) w = t; }
    if (w < 180) w = 180;
    int h = g_nmi * 30 + 12;
    if (x + w > g_wx + g_ww) x = g_wx + g_ww - w - 4;
    if (y + h > g_wy + g_wh) y = g_wy + g_wh - h - 4;
    if (y < g_wy) y = g_wy;
    fb_draw_shadow(x, y, w, h, 10, 12, 4, 0x66000000u); fb_fill_rrect_aa(x, y, w, h, 10, 0xFF1A2333u);
    for (int i = 0; i < g_nmi; ++i) {
        int iy = y + 6 + i * 30;
        if (g_mi[i].on) fb_fill_rrect_aa(x + 6, iy, w - 12, 30, 6, 0xFF1A3558u);
        tx(x + 18, iy, 30, g_mi[i].t, 13, g_mi[i].on ? K_ACC2 : K_TXT, 0);
        hit(x + 6, iy, w - 12, 30, H_MENU_IT, g_mi[i].id);
    }
}
static void pinta_sug_fx(void)
{
    if (!g_nfsug || !g_edit) return;
    int x = g_tf.x, y = g_tf.y + g_tf.h + 4, w = 0;
    for (int i = 0; i < g_nfsug; ++i) { int t = tw(g_fsug[i], 13, 0) + 30; if (t > w) w = t; }
    if (w < 160) w = 160;
    int h = g_nfsug * 26 + 30;
    fb_draw_shadow(x, y, w, h, 8, 10, 3, 0x66000000u); fb_fill_rrect_aa(x, y, w, h, 8, 0xFF1A2333u);
    for (int i = 0; i < g_nfsug; ++i) { int iy = y + 4 + i * 26; if (!i) fb_fill_rrect_aa(x + 4, iy, w - 8, 26, 5, 0xFF1A3558u); tx(x + 14, iy, 26, g_fsug[i], 13, i ? K_TXT : K_ACC2, i ? 0 : WF_BOLD); hit(x + 4, iy, w - 8, 26, H_SUGF, i); }
    tx(x + 14, y + h - 26, 24, "Tab: elegir la primera", 11, K_FAINT, 0);
}
void hoja_gui_init(void)
{
    if (g_init) return;
    g_init = 1;
    tf_init(&g_tf, g_bform, sizeof g_bform, 0); tf_init(&g_tbarra, g_bbarra, sizeof g_bbarra, 0); tf_init(&g_tbarra2, g_bbarra2, sizeof g_bbarra2, 0);
}
void hoja_gui_render(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused; hoja_gui_init(); g_nh = 0;
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    g_wx = x; g_wy = y; g_ww = w; g_wh = h;
    fb_fill_rect(wx, wy, ww, wh, K_WIN);
    if (asegura_libro() < 0 || !L()) { tx(x + 20, y + 20, 30, "No se pudo crear el libro (sin memoria).", 14, K_RED, 0); return; }
    struct hj_libro *l = L();
    if (g_edit && g_edit_hoja >= l->nh) cancela_edicion();
    g_ver_hoja = hoja_version();
    pinta_barra_herr(x, y, w);
    pinta_barra_form(x, y + TB_H, w);
    int hb = alto_barra(), pie = g_msg[0] ? 26 : 0;
    int gy = y + TB_H + FB_H, gh = h - TB_H - FB_H - TAB_H - hb - pie;
    if (gh > 40) pinta_cuadricula(x, gy, w, gh);
    pinta_pestanas(x, y + h - TAB_H - hb - pie, w);
    if (g_barra) pinta_barra(x, y + h - hb - pie, w);
    if (pie) { rect(x, y + h - pie, w, pie, g_msg_err ? 0x003B1111u : K_BAR); txc(x + 16, y + h - pie, w - 32, pie, g_msg, 12, g_msg_err ? 0x00FCA5A5u : K_ACC2, 0); }
    if (l->sin_memoria && !g_msg[0]) { rect(x, gy, w, 22, 0x003B1111u); txc(x + 16, gy, w - 32, 22, "Sin memoria: algunas f\303\263rmulas no se han podido calcular.", 12, 0x00FCA5A5u, 0); }
    pinta_sug_fx();
    pinta_menu();
}

/* ---------------------------------------------------------------- raton */
static void sel_a(uint32_t f, uint32_t c, int extiende)
{
    struct vista *v = V(); if (extiende) { v->sf = f; v->sc = c; } else { v->af = v->sf = f; v->ac = v->sc = c; } ++g_ver;
}
static int doble(int tipo, uint32_t val)
{
    uint64_t t = timer_get_uptime_ms(); int d = g_ult_clic_tipo == tipo && g_ult_clic_val == val && t - g_ult_clic < 420;
    g_ult_clic = d ? 0 : t; g_ult_clic_tipo = tipo; g_ult_clic_val = val; return d;
}
static int hoja_gui_click_(int32_t mx, int32_t my);
int hoja_gui_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh;
    g_pide_app = 0; int r = hoja_gui_click_(mx, my);
    if (g_pide_app) { r = g_pide_app; g_pide_app = 0; }
    return r;
}
static int hoja_gui_click_(int32_t mx, int32_t my)
{
    ++g_ver;
    hoja_gui_init(); if (asegura_libro() < 0) return 0;
    struct hj_libro *l = L(); struct vista *v = V(); int shift = keyboard_shift_held();
    for (int i = g_nh - 1; i >= 0; i--) {
        struct hit *t = &g_hit[i]; if (mx < t->x || mx >= t->x + t->w || my < t->y || my >= t->y + t->h) continue;
        if (g_menu && t->tipo != H_MENU_IT && t->tipo != H_SWATCH) { int m = g_menu; g_menu = M_NADA; if (t->tipo == H_BOTON || t->tipo == H_FX) { (void)m; } else return 0; }
        if (t->tipo != H_BOTON && t->tipo != H_MENU_IT) g_cerrar_armado = 0;
        switch (t->tipo) {
        case H_BOTON: boton_accion(t->val, t->x, t->y + t->h + 4); return 0;
        case H_MENU_IT: { int m = g_menu; g_menu = M_NADA; (void)m; accion(t->val); return 0; }
        case H_SWATCH: { int m = g_menu; g_menu = M_NADA; uint32_t c = t->val < 0 ? 0 : PALETA[t->val];
            if (m == M_COLOR) { g_ultimo_color = c; aplica(fn_color, c); } else { g_ultimo_fondo = c; aplica(fn_fondo, c); } return 0; }
        case H_NOMBRE: abre_barra(BA_IR); return 0;
        case H_FX: abre_menu(M_FX, t->x, t->y + t->h + 4); return 0;
        case H_FORM:
            if (!g_edit) empieza_edicion(2, 0);
            g_foco = F_FORM; if (g_edit == 1) g_edit = 2;
            tf_press(&g_tf, mx, my, shift); g_ar = AR_TF; return 0;
        case H_SUGF: acepta_fx(t->val); g_foco = F_FORM; return 0;
        case H_BARRA: g_foco = F_BARRA; tf_press(&g_tbarra, mx, my, shift); return 0;
        case H_BARRA2: g_foco = F_BARRA2; tf_press(&g_tbarra2, mx, my, shift); return 0;
        case H_BARRA_OK: barra_ok(0); return 0;
        case H_BARRA_ALT: barra_ok(t->val); return 0;
        case H_BARRA_NO: cierra_barra(); return 0;
        case H_SUG: if (t->val < g_nsug) { tf_set(&g_tbarra, g_sug[t->val]); if (g_barra == BA_FILTRO) { char z[170]; cpy(z, "=", sizeof z); cat(z, g_sug[t->val], sizeof z); tf_set(&g_tbarra, z); } barra_ok(0); } return 0;
        case H_PEST:
            if (g_edit && es_formula() && t->val != l->activa) { l->activa = t->val; g_ref_len = 0; g_foco = F_FORM; return 0; }   /* referencia a otra hoja */
            if (g_edit && confirma()) return 0;
            if (t->val == l->activa && doble(H_PEST, (uint32_t)t->val)) { abre_menu(M_PEST, t->x, t->y - 5 * 30 - 16); return 0; }
            (void)doble(H_PEST, (uint32_t)t->val);
            l->activa = t->val; ++g_ver; return 0;
        case H_PEST_MAS: if (g_edit && confirma()) return 0; accion(A_PEST_NUEVA); return 0;
        case H_PEST_IZQ: if (g_tab_off > 0) --g_tab_off; return 0;
        case H_PEST_DER: if (g_tab_off + 1 < l->nh) ++g_tab_off; return 0;
        case H_ESQ: if (g_edit && confirma()) return 0; v->af = 0; v->ac = 0; v->sf = HJ_FILAS - 1; v->sc = HJ_COLS - 1; return 0;
        case H_ANCHO:
            if (doble(H_ANCHO, (uint32_t)t->val)) { if (paso_libro()) return 0; ancho_auto((uint32_t)t->val); cambio(); return 0; }
            g_ar = AR_ANCHO; g_ar_i = (uint32_t)t->val; g_ar_x0 = mx; g_ar_v0 = hj_ancho(S(), g_ar_i); if (paso_libro()) g_ar = AR_NADA; return 0;
        case H_ALTO: g_ar = AR_ALTO; g_ar_i = (uint32_t)t->val; g_ar_y0 = my; g_ar_v0 = hj_alto(S(), g_ar_i); if (paso_libro()) g_ar = AR_NADA; return 0;
        case H_COLH:
            if (g_edit && pide_ref()) { pon_ref(0, (uint32_t)t->val, HJ_FILAS - 1, (uint32_t)t->val); return 0; }
            if (g_edit && confirma()) return 0;
            if (shift) { v->af = 0; v->sf = HJ_FILAS - 1; v->sc = (uint32_t)t->val; }
            else { v->af = 0; v->ac = (uint32_t)t->val; v->sf = HJ_FILAS - 1; v->sc = (uint32_t)t->val; }
            g_ar = AR_SEL; g_ar_i = 1; return 0;
        case H_FILH:
            if (g_edit && confirma()) return 0;
            if (shift) { v->ac = 0; v->sc = HJ_COLS - 1; v->sf = (uint32_t)t->val; }
            else { v->ac = 0; v->af = (uint32_t)t->val; v->sc = HJ_COLS - 1; v->sf = (uint32_t)t->val; }
            g_ar = AR_SEL; g_ar_i = 2; return 0;
        case H_FILTRO: if (g_edit && confirma()) return 0; g_barra_col = (uint32_t)t->val; abre_barra(BA_FILTRO); return 0;
        case H_RELLENO: g_ar = AR_RELLENO; { uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2); g_ar_f = f2; g_ar_c = c2; } return 0;
        case H_VBAR: case H_HBAR: g_ar = t->tipo == H_VBAR ? AR_VBAR : AR_HBAR; g_ar_x0 = mx; g_ar_y0 = my; g_ar_v0 = (int)(t->tipo == H_VBAR ? v->top : v->left); return 0;
        case H_GRAF: case H_GRAF_TAM: {                                /* SOMA_360_HOJA_GRAFICOS */
            if (g_edit && confirma()) return 0;
            struct hj_hoja *sh = S(); if (t->val >= sh->ngraf) return 0;
            g_gsel = t->val;
            if (t->tipo == H_GRAF && doble(H_GRAF, (uint32_t)t->val)) { abre_menu(M_GRAF, mx, my); return 0; }
            g_ar = t->tipo == H_GRAF ? AR_GMUEVE : AR_GTAM; g_ar_i = (uint32_t)t->val; g_ar_movido = 0;
            if (g_ar == AR_GMUEVE) { int gx, gy; graf_pos(&sh->graf[t->val], &gx, &gy); g_ar_x0 = mx - gx; g_ar_y0 = my - gy; }
            else { g_ar_x0 = mx; g_ar_y0 = my; g_ar_v0 = (int)sh->graf[t->val].ancho; g_ar_v1 = (int)sh->graf[t->val].alto; }
            return 0;
        }
        case H_GRID: {
            g_gsel = -1;
            uint32_t f, c; celda_en(mx, my, &f, &c);
            if (g_edit && pide_ref()) { g_ar = AR_REF; g_ar_f = f; g_ar_c = c; g_ar_ref_f = f; g_ar_ref_c = c; pon_ref(f, c, f, c); return 0; }
            if (g_edit) { if (g_edit_f == f && g_edit_c == c && l->activa == g_edit_hoja) { g_edit = 2; return 0; } if (confirma()) return 0; }
            if (g_foco == F_BARRA || g_foco == F_BARRA2) g_foco = F_NADA;
            if (!shift && doble(H_GRID, f * 16384u + c) && f == v->af && c == v->ac) { empieza_edicion(2, 0); return 0; }
            sel_a(f, c, shift); g_ar = AR_SEL; g_ar_i = 0; return 0;
        }
        default: return 0;
        }
    }
    if (g_menu) { g_menu = M_NADA; }
    return 0;
}
int hoja_gui_mouse(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int wheel, int held, int rclick)
{
    int r = 0; hoja_gui_init(); if (asegura_libro() < 0) return 0;
    int dentro = mx >= (int32_t)wx && my >= (int32_t)wy && mx < (int32_t)(wx + ww) && my < (int32_t)(wy + wh);
    struct vista *v = V(); struct hj_hoja *s = S(); struct hj_libro *l = L();
    if (held) {
        switch (g_ar) {
        case AR_TF: if (g_tf.dragging && tf_drag(&g_tf, mx, my, held)) r = 1; break;
        case AR_GMUEVE: case AR_GTAM: {                                 /* SOMA_360_HOJA_GRAFICOS */
            if (g_ar_i >= s->ngraf) { g_ar = AR_NADA; break; }
            struct hj_grafico *gg = &s->graf[g_ar_i];
            if (g_ar == AR_GMUEVE) {
                uint32_t f, c; celda_en(mx - g_ar_x0 + 2, my - g_ar_y0 + 2, &f, &c);
                if (mx - g_ar_x0 < g_gx) c = gg->col;
                if (my - g_ar_y0 < g_gy) f = gg->fila;
                if (f != gg->fila || c != gg->col) { if (!g_ar_movido) { if (paso_libro()) { g_ar = AR_NADA; break; } g_ar_movido = 1; } gg->fila = f; gg->col = c; r = 1; }
            } else {
                int nw = g_ar_v0 + (mx - g_ar_x0) * 100 / g_zoom, nh = g_ar_v1 + (my - g_ar_y0) * 100 / g_zoom;
                if (nw < 160) nw = 160;
                if (nw > 3000) nw = 3000;
                if (nh < 120) nh = 120;
                if (nh > 2000) nh = 2000;
                if ((uint32_t)nw != gg->ancho || (uint32_t)nh != gg->alto) { if (!g_ar_movido) { if (paso_libro()) { g_ar = AR_NADA; break; } g_ar_movido = 1; } gg->ancho = (uint32_t)nw; gg->alto = (uint32_t)nh; r = 1; }
            }
            break;
        }
        case AR_SEL: {
            uint32_t f, c; celda_en(mx, my, &f, &c);
            if (my > g_gy + g_gh && v->top + 1 < HJ_FILAS) { v->top = fila_mueve(v->top, 1); r = 1; }
            if (my < g_gy && v->top > s->inmov_f) { v->top = fila_mueve(v->top, -1); r = 1; }
            if (mx > g_gx + g_gw && v->left + 1 < HJ_COLS) { ++v->left; r = 1; }
            if (mx < g_gx && v->left > s->inmov_c) { --v->left; r = 1; }
            if (g_ar_i == 1) { if (c != v->sc) { v->sc = c; r = 1; } }
            else if (g_ar_i == 2) { if (f != v->sf) { v->sf = f; r = 1; } }
            else if (f != v->sf || c != v->sc) { v->sf = f; v->sc = c; r = 1; }
            break;
        }
        case AR_REF: { uint32_t f, c; celda_en(mx, my, &f, &c); if (f != g_ar_ref_f || c != g_ar_ref_c) { g_ar_ref_f = f; g_ar_ref_c = c; pon_ref(g_ar_f, g_ar_c, f, c); r = 1; } break; }
        case AR_RELLENO: { uint32_t f, c; celda_en(mx, my, &f, &c); if (my > g_gy + g_gh) { v->top = fila_mueve(v->top, 1); } if (f != g_ar_f || c != g_ar_c) { g_ar_f = f; g_ar_c = c; r = 1; } break; }
        case AR_ANCHO: { int px = g_ar_v0 + (mx - g_ar_x0) * 100 / g_zoom; if (px < 8) px = 8; if (px > 2000) px = 2000; if (hj_ancho(s, g_ar_i) != px) { hj_pon_ancho(s, g_ar_i, (uint16_t)px); r = 1; } break; }
        case AR_ALTO: { int px = g_ar_v0 + (my - g_ar_y0) * 100 / g_zoom; if (px < 8) px = 8; if (px > 600) px = 600; if (hj_alto(s, g_ar_i) != px) { hj_pon_alto(s, g_ar_i, (uint16_t)px); r = 1; } break; }
        case AR_VBAR: { uint32_t tot = s->max_fila + 40 > v->top + 60 ? s->max_fila + 40 : v->top + 60; int64_t nt = g_ar_v0 + (int64_t)(my - g_ar_y0) * (int64_t)tot / (g_gh > 1 ? g_gh : 1); if (nt < (int64_t)s->inmov_f) nt = s->inmov_f; if (nt > HJ_FILAS - 1) nt = HJ_FILAS - 1; if ((uint32_t)nt != v->top) { v->top = (uint32_t)nt; while (v->top + 1 < HJ_FILAS && oculta(v->top)) ++v->top; r = 1; } break; }
        case AR_HBAR: { uint32_t tot = s->max_col + 6 > v->left + 20 ? s->max_col + 6 : v->left + 20; int64_t nl = g_ar_v0 + (int64_t)(mx - g_ar_x0) * (int64_t)tot / (g_gw > 1 ? g_gw : 1); if (nl < (int64_t)s->inmov_c) nl = s->inmov_c; if (nl > HJ_COLS - 1) nl = HJ_COLS - 1; if ((uint32_t)nl != v->left) { v->left = (uint32_t)nl; r = 1; } break; }
        default: break;
        }
        if (g_foco == F_BARRA && g_tbarra.dragging && tf_drag(&g_tbarra, mx, my, held)) r = 1;
        if (g_foco == F_BARRA2 && g_tbarra2.dragging && tf_drag(&g_tbarra2, mx, my, held)) r = 1;
    } else if (g_held_ant || g_ar) {
        /* al soltar */
        if (g_ar == AR_RELLENO) {
            uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2);
            uint32_t tf = g_ar_f > f2 ? g_ar_f : f2, tc = g_ar_c > c2 ? g_ar_c : c2;
            if ((tf - f2) >= (tc - c2)) tc = c2; else tf = f2;
            if (tf != f2 || tc != c2) {
                if (paso_rango(f1, c1, tf, tc) == 0) { if (tc != c2) hj_borra_rango(l, l->activa, f1, c2 + 1, tf, tc, 0); else hj_borra_rango(l, l->activa, f2 + 1, c1, tf, tc, 0); (void)hj_rellena(l, l->activa, f1, c1, tf, tc, tc != c2); v->af = f1; v->ac = c1; v->sf = tf; v->sc = tc; cambio(); }
            }
        }
        if (g_ar == AR_ANCHO || g_ar == AR_ALTO) { l->version++; cambio(); }
        if ((g_ar == AR_GMUEVE || g_ar == AR_GTAM) && g_ar_movido) { l->version++; cambio(); }
        if (g_ar == AR_SEL && g_ar_i == 0) a_la_vista_de(v->sf, v->sc);
        g_ar = AR_NADA; r = 1;
    }
    g_held_ant = held;
    if (rclick && dentro && mx >= g_gx && my >= g_gy && mx < g_gx + g_gw && my < g_gy + g_gh) {
        uint32_t f, c; celda_en(mx, my, &f, &c); uint32_t f1, c1, f2, c2; sel_rango(&f1, &c1, &f2, &c2);
        if (g_edit && confirma()) return 1;
        if (!(f >= f1 && f <= f2 && c >= c1 && c <= c2)) sel_a(f, c, 0);
        g_menu = M_NADA; abre_menu(M_CELDA, mx, my); r = 1;
    }
    if (wheel && dentro && !g_menu) {
        int d = -wheel; if (d > 6) d = 6; if (d < -6) d = -6;
        if (keyboard_shift_held()) { int64_t nl = (int64_t)v->left + d * 2; if (nl < (int64_t)s->inmov_c) nl = s->inmov_c; if (nl > HJ_COLS - 1) nl = HJ_COLS - 1; v->left = (uint32_t)nl; }
        else if (keyboard_ctrl_held()) { g_zoom += d < 0 ? 10 : -10; if (g_zoom < 50) g_zoom = 50; if (g_zoom > 200) g_zoom = 200; char m[40]; m[0] = 0; cat(m, "Zoom: ", sizeof m); num(m, sizeof m, g_zoom); cat(m, "%", sizeof m); avisa(m); }
        else { v->top = fila_mueve(v->top, d * 3); if (v->top < s->inmov_f) v->top = s->inmov_f; }
        r = 1;
    }
    if (r) ++g_ver;
    return r;
}
int hoja_gui_tick(void)
{
    int r = 0;
    if (g_msg[0] && timer_get_uptime_ms() - g_msg_t > (g_msg_err ? 12000u : 6000u)) { g_msg[0] = 0; r = 1; }
    if (hoja_version() != g_ver_hoja) r = 1;
    if (hoja_ia_version() != g_ia_ver_vista) { g_ia_ver_vista = hoja_ia_version(); r = 1; }
    if (r) ++g_ver;
    return r;
}

/* ---------------------------------------------------------------- teclado */
int hoja_gui_key(char ch)
{
    int c = (uint8_t)ch, ctrl = keyboard_ctrl_held(), shift = keyboard_shift_held(); ++g_ver;
    hoja_gui_init(); if (asegura_libro() < 0) return c != 27;
    struct vista *v = V(); struct hj_libro *l = L();
    if (g_menu) { if (c == 27) { g_menu = M_NADA; return 1; } g_menu = M_NADA; }
    if (g_foco == F_BARRA || g_foco == F_BARRA2) {
        if (c == 27) { cierra_barra(); return 1; }
        if (c == 9 && g_barra == BA_BUSCAR) { g_foco = g_foco == F_BARRA ? F_BARRA2 : F_BARRA; return 1; }
        int r = tf_key(g_foco == F_BARRA ? &g_tbarra : &g_tbarra2, ch, ctrl, shift);
        if (r == TF_ENTER) barra_ok(0);
        return 1;
    }
    if (g_edit) {
        if (c == 27) { if (g_nfsug) { g_nfsug = 0; return 1; } cancela_edicion(); return 1; }
        if (c == 9 && g_nfsug && !shift) { acepta_fx(0); return 1; }
        if (c == 13 || c == 10 || c == 9) {
            if (ctrl && (c == 13 || c == 10)) {   /* Ctrl+Intro: lo mismo en toda la seleccion */
                uint32_t f1, c1, f2, c2; sel_util(&f1, &c1, &f2, &c2);
                if (!sel_es_una() && (uint64_t)(f2 - f1 + 1) * (c2 - c1 + 1) <= 100000u) {
                    if (paso_rango(f1, c1, f2, c2)) return 1;
                    int e; int bien = 1;
                    for (uint32_t f = f1; f <= f2 && bien; ++f) for (uint32_t cc = c1; cc <= c2; ++cc) {
                        if (g_bform[0] == '=') { char *ca = (char *)kmalloc(HJ_FORM_MAX + 256), *cd = (char *)kmalloc(HJ_FORM_MAX + 256);
                            if (ca && cd && !hj_form_a_canon(l, l->activa, g_bform + 1, ca, HJ_FORM_MAX + 256, &e) && !hj_form_desplaza(l, l->activa, ca, (int32_t)(f - g_edit_f), (int32_t)(cc - g_edit_c), cd, HJ_FORM_MAX + 256)) (void)hj_pon_form(l, l->activa, f, cc, cd);
                            else bien = 0;
                            if (ca) kfree(ca);
                            if (cd) kfree(cd);
                        } else (void)hj_escribe(l, l->activa, f, cc, g_bform, &e);
                    }
                    g_edit = 0; g_foco = F_NADA; g_nfsug = 0; cambio();
                    if (!bien) error("No entiendo la f\303\263rmula.");
                    return 1;
                }
            }
            if (confirma()) return 1;
            if (c == 9) mueve(0, shift ? -1 : 1, 0); else mueve(shift ? -1 : 1, 0, 0);
            return 1;
        }
        if (g_edit == 1 && !ctrl && (c == 16 || c == 14 || c == 2 || c == 6) && !pide_ref()) {
            if (confirma()) return 1;
            mueve(c == 16 ? -1 : c == 14 ? 1 : 0, c == 2 ? -1 : c == 6 ? 1 : 0, 0); return 1;
        }
        if (g_edit == 1 && !ctrl && (c == 16 || c == 14 || c == 2 || c == 6) && pide_ref()) {
            /* flechas en una formula recien empezada: eligen la referencia */
            uint32_t f = g_ref_len ? g_ar_f : g_edit_f, cc = g_ref_len ? g_ar_c : g_edit_c;
            if (c == 16 && f) --f; else if (c == 14 && f + 1 < HJ_FILAS) ++f; else if (c == 2 && cc) --cc; else if (c == 6 && cc + 1 < HJ_COLS) ++cc;
            g_ar_f = f; g_ar_c = cc; pon_ref(f, cc, f, cc); return 1;
        }
        int r = tf_key(&g_tf, ch, ctrl, shift);
        if (r == TF_EDIT) { g_ref_len = g_ref_len && g_tf.cur == g_ref_pos + g_ref_len ? g_ref_len : 0; sugerencias_fx(); }
        return 1;
    }
    /* SOMA_360_HOJA_GRAFICOS: con un grafico elegido, Supr lo borra y Esc lo suelta */
    if (g_gsel >= 0 && g_gsel < S()->ngraf && !ctrl) {
        if (c == 4 || c == 8 || c == 127) { accion(A_GBORRA); return 1; }
        if (c == 27) { g_gsel = -1; return 1; }
        g_gsel = -1;
    }
    /* sin editar */
    if (c == 27) { if (g_barra) { cierra_barra(); return 1; } if (g_copia.seq == clip_seq() && g_copia.doc == g_doc) { g_copia.doc = -1; return 1; } if (!sel_es_una()) { v->sf = v->af; v->sc = v->ac; return 1; } return 0; }
    if (ctrl) {
        switch (c) {
        case 2: boton_accion(B_NEGRITA, 0, 0); return 1;
        case 9: boton_accion(B_CURSIVA, 0, 0); return 1;
        case 21: boton_accion(B_SUBRAYADO, 0, 0); return 1;
        case 26: deshace(shift); return 1;
        case 4: rellena(0); return 1;
        case 18: rellena(1); return 1;
        case 6: abre_barra(BA_BUSCAR); return 1;
        case 19: guarda(0); return 1;
        case 15: abre_barra(BA_ABRIR); return 1;
        case 7: abre_barra(BA_IR); return 1;                              /* Ctrl+G: ir a */
        case 1: { struct hj_hoja *s = S(); v->af = 0; v->ac = 0; v->sf = s->max_fila ? s->max_fila - 1 : 0; v->sc = s->max_col ? s->max_col - 1 : 0; return 1; }
        case 16: case 14: { uint32_t n = ultimo_usado(1, shift ? v->sf : v->af, c == 16 ? -1 : 1); if (shift) { v->sf = n; a_la_vista_de(v->sf, v->sc); } else { v->af = v->sf = n; v->sc = v->ac; a_la_vista(); } return 1; }
        case 31: g_zoom = g_zoom + 10 > 200 ? 200 : g_zoom + 10; return 1;
        case 30: g_zoom = g_zoom - 10 < 50 ? 50 : g_zoom - 10; return 1;
        case 13: case 10: empieza_edicion(2, 0); return 1;
        default: return 1;
        }
    }
    switch (c) {
    case 16: mueve(-1, 0, shift); return 1;
    case 14: mueve(1, 0, shift); return 1;
    case 2: mueve(0, -1, shift); return 1;
    case 6: mueve(0, 1, shift); return 1;
    case 28: mueve(-(g_vis_f > 1 ? g_vis_f - 1 : 1), 0, shift); { v->top = fila_mueve(v->top, -(g_vis_f > 1 ? g_vis_f - 1 : 1)); struct hj_hoja *s = S(); if (v->top < s->inmov_f) v->top = s->inmov_f; } return 1;
    case 29: mueve(g_vis_f > 1 ? g_vis_f - 1 : 1, 0, shift); { v->top = fila_mueve(v->top, g_vis_f > 1 ? g_vis_f - 1 : 1); } return 1;
    case 1: if (shift) { v->sc = 0; a_la_vista_de(v->sf, 0); } else { v->ac = v->sc = 0; v->sf = v->af; a_la_vista(); } return 1;
    case 5: { struct hj_hoja *s = S(); uint32_t m = 0, fila = shift ? v->sf : v->af; for (int32_t i = 0; i < s->nc; ++i) if (s->c[i].fila == fila && s->c[i].t != HJ_VACIA && s->c[i].col > m) m = s->c[i].col; if (shift) { v->sc = m; a_la_vista_de(v->sf, m); } else { v->ac = v->sc = m; v->sf = v->af; a_la_vista(); } return 1; }
    case 13: case 10: empieza_edicion(2, 0); return 1;
    case 9: mueve(0, shift ? -1 : 1, 0); return 1;
    case 4: borra_contenido(); return 1;
    case 8: case 127: borra_contenido(); empieza_edicion(1, ""); return 1;
    default: break;
    }
    if (c >= 32 || c >= 0x80) {
        empieza_edicion(1, "");
        (void)tf_key(&g_tf, ch, 0, shift);
        sugerencias_fx();
        return 1;
    }
    return 1;
}
