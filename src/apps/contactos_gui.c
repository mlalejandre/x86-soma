/* SOMA_357_CONTACTOS: ver contactos_gui.h. Las zonas pulsables se anotan al pintar (g_hit) y el clic las lee.
 * Las fotos se decodifican aisladas (imgdec, ring 3) y se guardan ya reducidas en una cache pequena (miniaturas redondas). */
#include <stdint.h>
#include <stddef.h>
#include "contactos_gui.h"
#include "contactos.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "textfield.h"
#include "clipboard.h"
#include "mem.h"
#include "idt.h"
#include "web_img.h"
#include "fs.h"
#include "hoja_integra.h"   /* SOMA_360_HOJA_GRAFICOS */

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);

#define K_WIN   0x000C111Bu
#define K_BAR   0x00131A27u
#define K_LINE  0x00212B3Bu
#define K_CARD  0x00111827u
#define K_CARD2 0x00151E2Eu
#define K_TXT   0x00E5EAF2u
#define K_DIM   0x008592A8u
#define K_FAINT 0x00566379u
#define K_ACC   0x00F472B6u          /* el color del icono */
#define K_ACC2  0x0038BDF8u
#define K_SEL   0x001A3558u
#define K_RED   0x00EF4444u
#define K_GOLD  0x00FBBF24u
#define LISTA_W 320
#define FILA_H  52
#define MINI    40
#define GRANDE  112

enum { H_NADA, H_BUSCA, H_NUEVO, H_IMPORTAR, H_EXPORTAR, H_A_HOJA, H_CHIP, H_FILA, H_FAV, H_BORRAR, H_CORREO, H_PESTANA, H_CAMPO, H_TIPO, H_BARRA, H_BARRA_OK,
       H_BARRA_NO, H_SUGIERE, H_FOTO, H_FOTO_ANADIR, H_FOTO_CAMARA, H_FOTO_PRINC, H_FOTO_QUITAR, H_VOZ_GRABAR, H_VOZ_ESCUCHAR, H_VOZ_BORRAR, H_CAMPO_OK };
struct hit { int x, y, w, h, tipo, val; };
#define MAXH 200
static struct hit g_hit[MAXH]; static int g_nh;
static void hit(int x, int y, int w, int h, int tipo, int val) { if (g_nh < MAXH) { struct hit *t = &g_hit[g_nh++]; t->x = x; t->y = y; t->w = w; t->h = h; t->tipo = tipo; t->val = val; } }

/* estado */
static int g_init;
static uint32_t g_ver = 1, g_sel;                     /* contacto elegido (id) */
static int g_pest;                                    /* 0 ficha, 1 fotos, 2 voz */
static int g_filtro = 0; static char g_grupo[CT_GRUPO];   /* 0 todos, 1 favoritos, 2 cumpleanos, 3 grupo */
static int g_lscroll, g_dscroll, g_lmax, g_dmax;
static char g_msg[200]; static uint64_t g_msg_t;
static int g_borrar_armado; static int g_foto_sel = 1;
static char g_email[CT_EMAIL];
enum { F_NADA, F_BUSCA, F_CAMPO, F_BARRA };
static int g_foco;
static struct tfield g_tbusca, g_tcampo, g_tbarra;
static char g_bbusca[96], g_bcampo[CT_NOTA], g_bbarra[400];
static int g_campo = -1;                              /* campo en edicion */
enum { B_NADA, B_NUEVO, B_IMPORTAR, B_FOTOS };
static int g_barra;                                   /* barra de abajo: nada, alta rapida, importar, elegir foto */
static char g_sug[12][96]; static int g_nsug;
static uint16_t *g_idx; static int g_n;               /* la lista visible */
static int g_lista_ok;
static int g_lay_lx, g_lay_ly, g_lay_lw, g_lay_lh, g_lay_dx, g_lay_dy, g_lay_dw, g_lay_dh;

uint32_t contactos_gui_version(void) { return g_ver + contactos_version() * 7u; }
const char *contactos_gui_email(void) { return g_email; }
static void avisa(const char *m) { uint32_t i = 0; while (m[i] && i + 1 < sizeof g_msg) { g_msg[i] = m[i]; ++i; } g_msg[i] = 0; g_msg_t = timer_get_uptime_ms(); ++g_ver; }

/* ---------------------------------------------------------------- texto */
static int sl(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void cat(char *d, const char *s, int max) { int i = sl(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void cpy(char *d, const char *s, int max) { d[0] = 0; cat(d, s, max); }
static int igual(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void num(char *d, int max, int v) { char t[16]; int n = 0; if (v < 0) { cat(d, "-", max); v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15); char r[16]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = sl(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int x, int top, int h, const char *s, int px, uint32_t col, int bold)
{
    int n = sl(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void txc(int x, int y, int w, int h, const char *s, int px, uint32_t col, int bold) { fb_set_clip(x, y, w, h); tx(x, y, h, s, px, col, bold); fb_reset_clip(); }
static void boton(int x, int y, int w, int h, const char *t, int on, int tipo, int val)
{
    fb_fill_rrect_aa(x, y, w, h, 8, on ? 0xFF1A3558u : 0xFF151E2Eu);
    tx(x + (w - tw(t, 12, on)) / 2, y, h, t, 12, on ? K_ACC2 : K_DIM, on);
    hit(x, y, w, h, tipo, val);
}
static int boton_auto(int x, int y, const char *t, int on, int tipo, int val) { int w = tw(t, 12, on) + 26; boton(x, y, w, 28, t, on, tipo, val); return w; }

/* ---------------------------------------------------------------- fotos: decodificar y cachear */
struct mini { uint32_t id, ver; int lado; uint32_t px[GRANDE * GRANDE]; };
#define NMINI 48
static struct mini *g_mini;                           /* NMINI miniaturas de 40 px (en las primeras 40*40) + la grande del elegido */
static int g_mini_rr;
static struct mini *g_grande;                         /* foto de la ficha (112 px) */
#define NFT 6
static struct mini *g_tira;                           /* las 6 fotos del elegido (112 px) */
static int g_pend;                                    /* hay miniaturas por hacer: repintar */
static int g_decod_frame;                             /* decodificaciones en este fotograma (para no bloquear) */
/* recorta al centro, reduce a lado x lado y, si redondo, deja transparente fuera del circulo (con borde suave) */
static int decodifica(const uint8_t *d, uint32_t n, int lado, uint32_t *out, int redondo)
{
    struct wimg im; memset(&im, 0, sizeof im);
    if (wi_decode_fit(d, n, &im, lado, lado) != 0 || !im.px || im.w <= 0 || im.h <= 0) { wi_free(&im); return -1; }
    int s = im.w < im.h ? im.w : im.h, ox = (im.w - s) / 2, oy = (im.h - s) / 2;
    for (int y = 0; y < lado; ++y) for (int x = 0; x < lado; ++x) {
        int sx0 = ox + x * s / lado, sx1 = ox + (x + 1) * s / lado, sy0 = oy + y * s / lado, sy1 = oy + (y + 1) * s / lado;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint32_t r = 0, g = 0, b = 0, k = 0;
        for (int yy = sy0; yy < sy1; yy += (sy1 - sy0 > 3 ? (sy1 - sy0) / 3 : 1)) for (int xx = sx0; xx < sx1; xx += (sx1 - sx0 > 3 ? (sx1 - sx0) / 3 : 1)) {
            uint32_t c = im.px[(size_t)yy * (size_t)im.w + (size_t)xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; ++k;
        }
        uint32_t a = 255;
        if (redondo) {                                    /* distancia al centro en 1/16 de pixel */
            int dx = 2 * x + 1 - lado, dy = 2 * y + 1 - lado; int d2 = dx * dx + dy * dy, R = lado * lado;
            if (d2 >= R) a = 0; else if (d2 > R - 4 * lado) a = (uint32_t)((R - d2) * 255 / (4 * lado));
        }
        out[y * lado + x] = (a << 24) | ((r / k) << 16) | ((g / k) << 8) | (b / k);
    }
    wi_free(&im); return 0;
}
static struct mini *mini_de(const struct ct_contacto *c, int lado, struct mini *zona, int nzona, int k_foto)
{
    if (!zona || !c->nfotos || k_foto > c->nfotos) return 0;
    uint32_t ver = contactos_version() * 8u + (uint32_t)k_foto;
    for (int i = 0; i < nzona; ++i) if (zona[i].id == c->id && zona[i].lado == lado && zona[i].ver == ver) return &zona[i];
    if (g_decod_frame >= 2) { g_pend = 1; return 0; }
    ++g_decod_frame;
    struct mini *m = nzona == 1 ? zona : &zona[g_mini_rr++ % nzona];
    uint8_t *b; uint32_t bn;
    if (contactos_foto_lee(c->id, k_foto, &b, &bn)) { m->id = c->id; m->ver = ver; m->lado = -1; return 0; }
    int r = decodifica(b, bn, lado, m->px, 1); kfree(b);
    m->id = c->id; m->ver = ver; m->lado = r ? -1 : lado;
    return r ? 0 : m;
}
static uint32_t color_de(uint32_t id) { static const uint32_t C[8] = { 0x00F472B6, 0x0038BDF8, 0x0034D399, 0x00A78BFA, 0x00F59E0B, 0x0022D3EE, 0x00FB923C, 0x0094A3B8 }; return C[(id * 2654435761u >> 29) & 7]; }
static void avatar(const struct ct_contacto *c, int x, int y, int lado, int k_foto)
{
    struct mini *m = 0;
    if (c->nfotos) {
        if (lado == MINI) m = mini_de(c, MINI, g_mini, NMINI, 1);
        else if (lado == GRANDE && k_foto == 1) m = mini_de(c, GRANDE, g_grande, 1, 1);
    }
    if (m) { fb_blit_alpha(x, y, m->px, lado, lado, lado); return; }
    fb_fill_rrect_aa(x, y, lado, lado, lado / 2, 0xFF000000u | color_de(c->id));
    char in[8]; ct_iniciales(c, in);
    int px = lado >= 100 ? 34 : 15;
    tx(x + (lado - tw(in, px, 1)) / 2, y, lado, in, px, 0x000C111B, 1);
}

/* ---------------------------------------------------------------- lista */
static void rehace_lista(void)
{
    struct ct_libreta *l = contactos_libreta(); g_n = 0;
    if (!l) return;
    if (!g_idx) { g_idx = (uint16_t *)kmalloc(CT_MAX * sizeof *g_idx); if (!g_idx) return; }
    if (g_filtro == 2) {
        static int16_t fa[CT_MAX]; int y, m, d; contactos_hoy(&y, &m, &d);
        g_n = ct_cumples(l, y, m, d, 60, g_idx, fa, CT_MAX);
        if (g_bbusca[0]) { int w = 0; for (int i = 0; i < g_n; ++i) if (ct_coincide(&l->c[g_idx[i]], g_bbusca)) g_idx[w++] = g_idx[i]; g_n = w; }
    } else g_n = ct_lista(l, g_bbusca, g_filtro == 3 ? g_grupo : 0, g_filtro == 1, g_idx, CT_MAX);
    g_lista_ok = 1;
}
static struct ct_contacto *elegido(void) { struct ct_libreta *l = contactos_libreta(); return l && g_sel ? ct_busca_id(l, g_sel) : 0; }
void contactos_gui_init(void)
{
    if (g_init) return;
    g_init = 1;
    tf_init(&g_tbusca, g_bbusca, sizeof g_bbusca, 0); tf_init(&g_tcampo, g_bcampo, sizeof g_bcampo, 0); tf_init(&g_tbarra, g_bbarra, sizeof g_bbarra, 0);
    if (!g_mini) { g_mini = (struct mini *)kmalloc(NMINI * sizeof *g_mini); if (g_mini) memset(g_mini, 0, NMINI * sizeof *g_mini); }
    if (!g_grande) { g_grande = (struct mini *)kmalloc(sizeof *g_grande); if (g_grande) memset(g_grande, 0, sizeof *g_grande); }
    if (!g_tira) { g_tira = (struct mini *)kmalloc(NFT * sizeof *g_tira); if (g_tira) memset(g_tira, 0, NFT * sizeof *g_tira); }
    rehace_lista();
    if (!g_sel && g_n) g_sel = contactos_libreta()->c[g_idx[0]].id;
}
void contactos_gui_ver(uint32_t id) { contactos_gui_init(); g_sel = id; g_pest = 0; g_filtro = 0; g_bbusca[0] = 0; tf_set(&g_tbusca, ""); g_lista_ok = 0; g_dscroll = 0; ++g_ver; }

/* ---------------------------------------------------------------- ficha: campos */
struct fcampo { const char *et, *campo; };
static const struct fcampo FC[] = {
    { "Nombre", "nombre" }, { "Apellidos", "apellidos" }, { "Apodo", "apodo" },
    { "Email", "email" }, { "Email 2", "email2" }, { "Email 3", "email3" },
    { "Tel\303\251fono", "tel" }, { "Tel\303\251fono 2", "tel2" }, { "Tel\303\251fono 3", "tel3" },
    { "Direcci\303\263n", "calle" }, { "C\303\263digo postal", "cp" }, { "Ciudad", "ciudad" }, { "Provincia", "provincia" }, { "Pa\303\255s", "pais" },
    { "Nacimiento", "nacimiento" }, { "Grupos", "grupo" }, { "Empresa", "empresa" }, { "Cargo", "cargo" }, { "Web", "web" }, { "Nota", "nota" },
};
#define NFC ((int)(sizeof FC / sizeof FC[0]))
static void valor_campo(const struct ct_contacto *c, int i, char *v, int cap, int para_editar)
{
    v[0] = 0;
    switch (i) {
    case 0: cpy(v, c->nombre, cap); break; case 1: cpy(v, c->apellidos, cap); break; case 2: cpy(v, c->apodo, cap); break;
    case 3: case 4: case 5: cpy(v, c->email[i - 3], cap); break;
    case 6: case 7: case 8: cpy(v, c->tel[i - 6], cap); break;
    case 9: cpy(v, c->calle, cap); break; case 10: cpy(v, c->cp, cap); break; case 11: cpy(v, c->ciudad, cap); break; case 12: cpy(v, c->prov, cap); break; case 13: cpy(v, c->pais, cap); break;
    case 14:
        if (!c->nac_m) break;
        if (para_editar) { num(v, cap, c->nac_d); cat(v, "/", cap); num(v, cap, c->nac_m); if (c->nac_y) { cat(v, "/", cap); num(v, cap, c->nac_y); } break; }
        num(v, cap, c->nac_d); cat(v, " de ", cap); cat(v, ct_nombre_mes(c->nac_m), cap); if (c->nac_y) { cat(v, " de ", cap); num(v, cap, c->nac_y); }
        { int y, m, d; contactos_hoy(&y, &m, &d); int e = ct_edad(c, y, m, d), f = ct_dias_para_cumple(c, y, m, d);
          if (e >= 0) { cat(v, "  \302\267  ", cap); num(v, cap, e); cat(v, " a\303\261os", cap); }
          if (f == 0) cat(v, "  \302\267  \302\241hoy es su cumplea\303\261os!", cap); else if (f > 0 && f <= 30) { cat(v, "  \302\267  cumple en ", cap); num(v, cap, f); cat(v, f == 1 ? " d\303\255a" : " d\303\255as", cap); } }
        break;
    case 15: cpy(v, c->grupo, cap); break; case 16: cpy(v, c->empresa, cap); break; case 17: cpy(v, c->cargo, cap); break; case 18: cpy(v, c->web, cap); break;
    case 19: cpy(v, c->nota, cap); if (!para_editar) for (char *z = v; *z; ++z) if (*z == '\n') *z = ' '; break;
    }
}
static void edita_campo(int i)
{
    struct ct_contacto *c = elegido(); if (!c || i < 0 || i >= NFC) return;
    char v[CT_NOTA]; valor_campo(c, i, v, sizeof v, 1);
    tf_init(&g_tcampo, g_bcampo, sizeof g_bcampo, i == 19); tf_set(&g_tcampo, v);
    g_campo = i; g_foco = F_CAMPO; ++g_ver;
}
static void guarda_campo(int y_sigue)
{
    struct ct_contacto *c = elegido(); int i = g_campo;
    if (!c || i < 0) { g_campo = -1; g_foco = F_NADA; return; }
    struct ct_contacto copia = *c;
    int r = ct_pon_campo(c, FC[i].campo, g_bcampo);
    if (r) { *c = copia; avisa(r == -2 ? (i == 14 ? "Esa fecha no la entiendo: escribe 17/5/1990 o 17 de mayo." : "Ese email no es v\303\241lido.") : "No se pudo cambiar."); return; }
    int igual = 1; { const uint8_t *a = (const uint8_t *)&copia, *b = (const uint8_t *)c; for (size_t k = 0; k < sizeof copia; ++k) if (a[k] != b[k]) { igual = 0; break; } }
    if (!igual && contactos_toca(c->id)) { *c = copia; avisa("No se pudo guardar."); return; }
    g_lista_ok = 0;
    if (y_sigue && i + 1 < NFC) edita_campo(i + 1);
    else { g_campo = -1; g_foco = F_NADA; }
    ++g_ver;
}

/* ---------------------------------------------------------------- pintar */
static void pinta_lista(int x, int y, int w, int h)
{
    struct ct_libreta *l = contactos_libreta();
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, K_WIN);
    if (!g_lista_ok) rehace_lista();
    g_lmax = g_n * FILA_H - h; if (g_lmax < 0) g_lmax = 0;
    if (g_lscroll > g_lmax) g_lscroll = g_lmax;
    if (!g_n) {
        tx(x + 16, y + 12, 24, g_bbusca[0] || g_filtro ? "Nadie coincide." : "La libreta est\303\241 vac\303\255a.", 13, K_DIM, 0);
        if (!g_bbusca[0] && !g_filtro) { tx(x + 16, y + 40, 20, "Pulsa \302\253+ Nuevo\302\273 o \302\253Importar\302\273 (un .vcf", 12, K_FAINT, 0); tx(x + 16, y + 60, 20, "de Google Contactos, iCloud o el m\303\263vil).", 12, K_FAINT, 0); }
        return;
    }
    int hy, hm, hd; contactos_hoy(&hy, &hm, &hd);
    fb_set_clip(x, y, w, h);
    int i0 = g_lscroll / FILA_H;
    for (int i = i0; i < g_n; ++i) {
        int ry = y + i * FILA_H - g_lscroll; if (ry >= y + h) break;
        const struct ct_contacto *c = &l->c[g_idx[i]]; int sel = c->id == g_sel;
        if (sel) fb_fill_rrect_aa(x + 6, ry + 2, w - 12, FILA_H - 4, 10, 0xFF1A3558u);
        avatar(c, x + 14, ry + (FILA_H - MINI) / 2, MINI, 1);
        char nc[CT_NOM + CT_APE + 8]; ct_nombre_completo(c, nc, sizeof nc);
        int nx = x + 14 + MINI + 12, nw = w - (nx - x) - 14;
        txc(nx, ry + 6, nw - (c->fav ? 18 : 0), 22, nc, 14, K_TXT, 1);
        if (c->fav) tx(x + w - 26, ry + 6, 22, "\342\230\205", 13, K_GOLD, 0);
        char sub[160]; sub[0] = 0;
        if (g_filtro == 2) { int f = ct_dias_para_cumple(c, hy, hm, hd); if (f == 0) cpy(sub, "\302\241Hoy es su cumplea\303\261os!", sizeof sub); else if (f == 1) cpy(sub, "Cumple ma\303\261ana", sizeof sub); else { cpy(sub, "Cumple en ", sizeof sub); num(sub, sizeof sub, f); cat(sub, " d\303\255as", sizeof sub); } }
        else if (c->email[0][0]) cpy(sub, c->email[0], sizeof sub); else if (c->tel[0][0]) cpy(sub, c->tel[0], sizeof sub); else if (c->empresa[0]) cpy(sub, c->empresa, sizeof sub);
        txc(nx, ry + 26, nw, 20, sub, 12, g_filtro == 2 ? K_ACC : K_DIM, 0);
        hit(x, ry, w, FILA_H, H_FILA, (int)c->id);
    }
    fb_reset_clip();
    if (g_lmax > 0) { int bh = h * h / (g_n * FILA_H); if (bh < 24) bh = 24; int by = y + (h - bh) * g_lscroll / g_lmax; fb_fill_rrect_aa(x + w - 5, by, 3, bh, 1, 0xFF2A3550u); }
}
static void pinta_ficha(const struct ct_contacto *c, int x, int y, int w, int h)
{
    int ry = y - g_dscroll, tot = 0, lab = 130;
    fb_set_clip(x, y, w, h);
    for (int i = 0; i < NFC; ++i) {
        char v[CT_NOTA + 64]; valor_campo(c, i, v, sizeof v, 0);
        int vacio = !v[0], editando = g_campo == i;
        /* los segundos y terceros emails y telefonos vacios no ocupan sitio salvo el primero libre */
        if (vacio && !editando && (i == 4 || i == 5 || i == 7 || i == 8)) { int prev = i - 1; char pv[CT_NOTA]; valor_campo(c, prev, pv, sizeof pv, 0); if (!pv[0]) continue; }
        int fh = editando && i == 19 ? 96 : 36;
        if (ry + fh > y && ry < y + h) {
            if (editando) fb_fill_rrect_aa(x, ry, w, fh, 8, 0xFF131C2Cu);
            tx(x + 10, ry, 36, FC[i].et, 12, K_FAINT, 0);
            if (editando) {
                tf_render(&g_tcampo, x + lab, ry + 4, w - lab - 92, fh - 8, 1, i == 14 ? "17/5/1990 o 17 de mayo" : "", K_TXT);
                boton(x + w - 84, ry + 4, 76, 28, "Guardar", 1, H_CAMPO_OK, i);
            } else {
                int vw = w - lab - 10, tipo = -1;
                if (i >= 3 && i <= 5 && !vacio) tipo = c->t_email[i - 3];
                if (i >= 6 && i <= 8 && !vacio) tipo = c->t_tel[i - 6];
                if (tipo >= 0) {                                   /* el tipo (movil, casa, trabajo): pulsar lo cambia */
                    const char *tn = tipo ? ct_nombre_tipo(tipo) : "otro"; int tw_ = tw(tn, 11, 0) + 16;
                    fb_fill_rrect_aa(x + w - tw_ - 8, ry + 8, tw_, 20, 10, 0xFF1C2638u); tx(x + w - tw_ - 8 + 8, ry + 8, 20, tn, 11, K_DIM, 0);
                    hit(x + w - tw_ - 8, ry + 8, tw_, 20, H_TIPO, i);
                    vw -= tw_ + 10;
                }
                txc(x + lab, ry, vw, 36, vacio ? "a\303\261adir" : v, 13, vacio ? 0x003A4558u : K_TXT, 0);
                hit(x, ry, w - (tipo >= 0 ? 90 : 0), 36, H_CAMPO, i);
            }
            fb_fill_rect((uint32_t)(x + 10), (uint32_t)(ry + fh - 1), (uint32_t)(w - 20), 1, 0x00182030u);
        }
        ry += fh; tot += fh;
    }
    fb_reset_clip();
    g_dmax = tot - h; if (g_dmax < 0) g_dmax = 0;
}
static void pinta_fotos(const struct ct_contacto *c, int x, int y, int w, int h)
{
    int t = 150, gap = 14, cols = (w + gap) / (t + gap); if (cols < 1) cols = 1;
    if (g_foto_sel > c->nfotos) g_foto_sel = c->nfotos ? 1 : 0;
    for (int k = 1; k <= c->nfotos; ++k) {
        int cx = x + ((k - 1) % cols) * (t + gap), cy = y + ((k - 1) / cols) * (t + gap + 6);
        if (cy + t > y + h) break;
        struct mini *m = 0;
        if (g_tira) { m = &g_tira[k - 1]; uint32_t ver = contactos_version() * 8u + (uint32_t)k;
            if (!(m->id == c->id && m->ver == ver && m->lado == GRANDE)) {
                if (g_decod_frame < 2) { ++g_decod_frame; uint8_t *b; uint32_t bn; m->id = c->id; m->ver = ver; m->lado = -1;
                    if (!contactos_foto_lee(c->id, k, &b, &bn)) { if (!decodifica(b, bn, GRANDE, m->px, 0)) m->lado = GRANDE; kfree(b); } }
                else g_pend = 1;
            }
            if (m->lado != GRANDE) m = 0; }
        fb_fill_rrect_aa(cx, cy, t, t, 12, k == g_foto_sel ? 0xFF1A3558u : 0xFF111827u);
        if (m) fb_blit_scale(cx + 8, cy + 8, t - 16, t - 16, m->px, GRANDE, GRANDE, GRANDE);
        else tx(cx + 40, cy, t, "\342\200\246", 18, K_DIM, 0);
        if (k == 1) { fb_fill_rrect_aa(cx + 10, cy + t - 30, 64, 20, 10, 0xD0000000u | 0x001A3558u); tx(cx + 18, cy + t - 30, 20, "En la ficha", 10, K_ACC2, 1); }
        hit(cx, cy, t, t, H_FOTO, k);
    }
    int by = y + ((c->nfotos + cols - 1) / cols) * (t + gap + 6) + (c->nfotos ? 4 : 0);
    if (!c->nfotos) { tx(x, y, 30, "Sin fotos todav\303\255a.", 14, K_DIM, 0); by = y + 40; }
    if (by + 30 > y + h) by = y + h - 30;
    int bx = x;
    if (c->nfotos < CT_FOTOS) { bx += boton_auto(bx, by, "A\303\261adir foto\342\200\246", 1, H_FOTO_ANADIR, 0) + 8; bx += boton_auto(bx, by, "Hacer una con la c\303\241mara", 0, H_FOTO_CAMARA, 0) + 8; }
    if (c->nfotos && g_foto_sel > 1) bx += boton_auto(bx, by, "Usar en la ficha", 0, H_FOTO_PRINC, g_foto_sel) + 8;
    if (c->nfotos && g_foto_sel >= 1) bx += boton_auto(bx, by, "Quitar", 0, H_FOTO_QUITAR, g_foto_sel) + 8;
    (void)bx;
    if (c->foto_url[0] && !c->nfotos) tx(x, by + 36, 22, "Tiene una foto en la nube: se descarga sola cuando haya red.", 12, K_FAINT, 0);
}
static void pinta_voz(const struct ct_contacto *c, int x, int y, int w, int h)
{
    uint32_t vid, ms; int nivel; int st = contactos_voz_estado(&vid, &ms, &nivel); if (vid != c->id) st = CTV_NADA;
    char nc[CT_NOM + CT_APE + 8]; ct_nombre_completo(c, nc, sizeof nc);
    char t[200]; cpy(t, "Muestra de voz de ", sizeof t); cat(t, nc, sizeof t);
    txc(x, y, w, 28, t, 15, K_TXT, 1);
    tx(x, y + 28, 22, "Una grabaci\303\263n corta (hasta 30 s) para reconocer su voz. Se guarda solo en este equipo.", 12, K_FAINT, 0);
    int cx = x + 70, cy = y + 120, r = 46;
    int grabando = st == CTV_GRABANDO, oyendo = st == CTV_ESCUCHANDO;
    if (grabando) { int halo = r + 6 + nivel / 6; fb_fill_rrect_aa(cx - halo, cy - halo, 2 * halo, 2 * halo, halo, 0x40000000u | K_RED); }
    fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, grabando ? 0xFF000000u | K_RED : 0xFF1C2638u);
    if (grabando) fb_fill_rrect_aa(cx - 14, cy - 14, 28, 28, 6, 0xFFFFFFFFu);
    else fb_fill_rrect_aa(cx - 16, cy - 16, 32, 32, 16, 0xFF000000u | K_RED);
    hit(cx - r, cy - r, 2 * r, 2 * r, H_VOZ_GRABAR, 0);
    tx(cx - r, cy + r + 6, 22, grabando ? "Parar" : c->voz ? "Grabar otra" : "Grabar", 12, K_DIM, 1);
    int ix = cx + r + 40;
    char e[120]; e[0] = 0;
    if (grabando) { cpy(e, "Grabando\342\200\246 ", sizeof e); num(e, sizeof e, (int)(ms / 1000)); cat(e, " s de 30", sizeof e); }
    else if (st == CTV_GUARDANDO) cpy(e, "Guardando\342\200\246", sizeof e);
    else if (oyendo) { cpy(e, "Escuchando\342\200\246 ", sizeof e); num(e, sizeof e, (int)(ms / 1000)); cat(e, " s", sizeof e); }
    else cpy(e, c->voz ? "Hay una muestra guardada." : "No hay ninguna muestra todav\303\255a.", sizeof e);
    tx(ix, cy - 40, 26, e, 14, grabando ? K_RED : K_TXT, 1);
    if (grabando || oyendo) {                                     /* nivel */
        int mw = w - (ix - x) - 20; if (mw > 360) mw = 360;
        fb_fill_rrect_aa(ix, cy - 6, mw, 12, 6, 0xFF1C2638u);
        int lv = nivel * mw / 100; if (lv > 0) fb_fill_rrect_aa(ix, cy - 6, lv, 12, 6, 0xFF000000u | (grabando ? K_RED : K_ACC2));
    }
    if (c->voz && !grabando) { int bx = ix; bx += boton_auto(bx, cy + 18, oyendo ? "Parar" : "\342\226\266  Escuchar", 1, H_VOZ_ESCUCHAR, 0) + 8; boton_auto(bx, cy + 18, "Borrar la voz", 0, H_VOZ_BORRAR, 0); }
    (void)h;
}
static void pinta_detalle(int x, int y, int w, int h)
{
    struct ct_contacto *c = elegido();
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x000E1420u);
    if (!c) { tx(x + 30, y + 30, 30, "Elige un contacto de la lista.", 14, K_DIM, 0); return; }
    int px = x + 28, py = y + 20;
    avatar(c, px, py, GRANDE, 1);
    char nc[CT_NOM + CT_APE + 8]; ct_nombre_completo(c, nc, sizeof nc);
    int nx = px + GRANDE + 22, nw = w - (nx - x) - 20;
    txc(nx, py + 4, nw, 34, nc, 22, K_TXT, 1);
    char sub[200]; sub[0] = 0; if (c->cargo[0]) cpy(sub, c->cargo, sizeof sub); if (c->empresa[0]) { if (sub[0]) cat(sub, " \302\267 ", sizeof sub); cat(sub, c->empresa, sizeof sub); }
    if (!sub[0] && c->apodo[0]) { cpy(sub, "\342\200\234", sizeof sub); cat(sub, c->apodo, sizeof sub); cat(sub, "\342\200\235", sizeof sub); }
    txc(nx, py + 38, nw, 22, sub, 13, K_DIM, 0);
    int bx = nx, by = py + 70;
    bx += boton_auto(bx, by, c->fav ? "\342\230\205 Favorito" : "\342\230\206 Favorito", c->fav, H_FAV, 0) + 8;
    if (c->email[0][0]) bx += boton_auto(bx, by, "Escribir correo", 0, H_CORREO, 0) + 8;
    boton_auto(bx, by, g_borrar_armado ? "\302\277Seguro? Borrar" : "Borrar", g_borrar_armado, H_BORRAR, 0);
    if (g_borrar_armado) { /* el boton se pinta en rojo */ int bw = tw("\302\277Seguro? Borrar", 12, 1) + 26; fb_stroke_rrect_aa(bx, by, bw, 28, 8, 0xFF000000u | K_RED); }
    if (c->cambiado) tx(nx, by + 32, 20, "Cambiado en SOMA desde la \303\272ltima exportaci\303\263n", 11, K_FAINT, 0);
    /* pestanas */
    int ty = py + GRANDE + 22; static const char *const P[3] = { "Ficha", "Fotos", "Voz" };
    int tx0 = x + 20;
    for (int i = 0; i < 3; ++i) {
        char et[24]; cpy(et, P[i], sizeof et);
        if (i == 1 && c->nfotos) { cat(et, " (", sizeof et); num(et, sizeof et, c->nfotos); cat(et, ")", sizeof et); }
        if (i == 2 && c->voz) cat(et, " \342\227\217", sizeof et);
        int ww = tw(et, 13, 1) + 28;
        tx(tx0 + 14, ty, 32, et, 13, g_pest == i ? K_TXT : K_DIM, g_pest == i);
        if (g_pest == i) fb_fill_rrect_aa(tx0 + 10, ty + 30, ww - 20, 3, 1, 0xFF000000u | K_ACC);
        hit(tx0, ty, ww, 34, H_PESTANA, i); tx0 += ww;
    }
    fb_fill_rect((uint32_t)(x + 20), (uint32_t)(ty + 34), (uint32_t)(w - 40), 1, K_LINE);
    int cy = ty + 44, ch = y + h - cy - 8;
    if (g_pest == 0) pinta_ficha(c, x + 20, cy, w - 40, ch);
    else if (g_pest == 1) pinta_fotos(c, x + 28, cy + 6, w - 56, ch - 6);
    else pinta_voz(c, x + 28, cy + 6, w - 56, ch - 6);
}
static void pinta_barra(int x, int y, int w)
{
    /* barra de abajo: alta rapida en lenguaje natural, importar (ruta) o elegir foto */
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 1, K_LINE);
    const char *et = g_barra == B_NUEVO ? "Nuevo contacto: escribe como lo dir\303\255as (\302\253Ana L\303\263pez, m\303\263vil 600 11 22 33, email ana@x.es, naci\303\263 el 3 de mayo\302\273)"
                   : g_barra == B_IMPORTAR ? "Importar un .vcf (en Google Contactos: Exportar > vCard). Ruta del archivo:" : "Elige una imagen o escribe su ruta:";
    txc(x + 20, y + 6, w - 40, 22, et, 12, K_DIM, 0);
    int fy = y + 30;
    tf_render(&g_tbarra, x + 20, fy, w - 220, 34, g_foco == F_BARRA, g_barra == B_NUEVO ? "Nombre, email, tel\303\251fono\342\200\246" : "/usuario/\342\200\246", K_TXT);
    hit(x + 20, fy, w - 220, 34, H_BARRA, 0);
    boton(x + w - 190, fy + 3, 90, 28, g_barra == B_NUEVO ? "Crear" : g_barra == B_IMPORTAR ? "Importar" : "A\303\261adir", 1, H_BARRA_OK, 0);
    boton(x + w - 94, fy + 3, 74, 28, "Cancelar", 0, H_BARRA_NO, 0);
    if (g_nsug) {
        int sx = x + 20, sy = fy + 40;
        for (int i = 0; i < g_nsug; ++i) {
            const char *n = g_sug[i]; const char *b = n; for (const char *z = n; *z; ++z) if (*z == '/') b = z + 1;
            int ww = tw(b, 11, 0) + 20; if (sx + ww > x + w - 20) { sx = x + 20; sy += 26; }
            if (sy > fy + 40 + 26) break;
            fb_fill_rrect_aa(sx, sy, ww, 22, 11, 0xFF1C2638u); tx(sx + 10, sy, 22, b, 11, K_TXT, 0); hit(sx, sy, ww, 22, H_SUGIERE, i);
            sx += ww + 6;
        }
    }
}
void contactos_gui_render(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused; contactos_gui_init(); g_nh = 0; g_decod_frame = 0; g_pend = 0;
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    struct ct_libreta *l = contactos_libreta();
    fb_fill_rect(wx, wy, ww, wh, K_WIN);
    /* cabecera */
    fb_fill_rect(wx, wy, ww, 56, K_BAR); fb_fill_rect(wx, wy + 56, ww, 1, K_LINE);
    tx(x + 20, y + 4, 48, "Contactos", 20, K_TXT, 1);
    char nb[16]; nb[0] = 0; num(nb, sizeof nb, l ? (int)ct_numero(l) : 0);
    int tx1 = x + 20 + tw("Contactos", 20, 1) + 10; tx(tx1, y + 6, 48, nb, 13, K_FAINT, 0);
    int bw = boton_auto(0, -100, "Exportar", 0, H_NADA, 0) + boton_auto(0, -100, "Importar", 0, H_NADA, 0) + boton_auto(0, -100, "+ Nuevo", 1, H_NADA, 0) + boton_auto(0, -100, "A hoja", 0, H_NADA, 0) + 24;
    g_nh -= 4;
    int sx = tx1 + tw(nb, 13, 0) + 24, sw = w - (sx - x) - bw - 34; if (sw > 340) sw = 340;
    if (sw > 120) { tf_render(&g_tbusca, sx, y + 12, sw, 32, g_foco == F_BUSCA, "Buscar nombre, email, tel\303\251fono, ciudad\342\200\246", K_TXT); hit(sx, y + 12, sw, 32, H_BUSCA, 0); }
    int bx = x + w - bw - 14;
    bx += boton_auto(bx, y + 14, "+ Nuevo", 1, H_NUEVO, 0) + 8; bx += boton_auto(bx, y + 14, "Importar", 0, H_IMPORTAR, 0) + 8; bx += boton_auto(bx, y + 14, "Exportar", 0, H_EXPORTAR, 0) + 8; boton_auto(bx, y + 14, "A hoja", 0, H_A_HOJA, 0);   /* SOMA_360_HOJA_GRAFICOS */
    /* chips: todos, favoritos, cumpleanos, grupos */
    int cy = y + 64; int cx = x + 14;
    fb_set_clip(x, cy, LISTA_W, 30);
    cx += boton_auto(cx, cy, "Todos", g_filtro == 0, H_CHIP, -1) + 6;
    cx += boton_auto(cx, cy, "\342\230\205", g_filtro == 1, H_CHIP, -2) + 6;
    cx += boton_auto(cx, cy, "Cumplea\303\261os", g_filtro == 2, H_CHIP, -3) + 6;
    static char gr[16][CT_GRUPO]; int ng = l ? ct_grupos(l, gr, 16) : 0;
    for (int i = 0; i < ng && cx < x + LISTA_W; ++i) cx += boton_auto(cx, cy, gr[i], g_filtro == 3 && igual(gr[i], g_grupo), H_CHIP, i) + 6;
    fb_reset_clip();
    int barra_h = g_barra ? (g_nsug ? 128 : 78) : 0, pie = g_msg[0] ? 28 : 0;
    g_lay_lx = x; g_lay_ly = y + 100; g_lay_lw = LISTA_W; g_lay_lh = h - 100 - barra_h - pie;
    g_lay_dx = x + LISTA_W; g_lay_dy = y + 57; g_lay_dw = w - LISTA_W; g_lay_dh = h - 57 - barra_h - pie;
    if (g_lay_lh > 0) pinta_lista(g_lay_lx, g_lay_ly, g_lay_lw, g_lay_lh);
    fb_fill_rect((uint32_t)(x + LISTA_W), wy + 57, 1, (uint32_t)(g_lay_dh > 0 ? g_lay_dh : 0), K_LINE);
    if (g_lay_dh > 0 && g_lay_dw > 200) pinta_detalle(g_lay_dx + 1, g_lay_dy, g_lay_dw - 1, g_lay_dh);
    if (g_barra) pinta_barra(x, y + h - barra_h - pie, w);
    if (pie) { fb_fill_rect(wx, (uint32_t)(y + h - pie), ww, (uint32_t)pie, K_BAR); txc(x + 20, y + h - pie, w - 40, pie, g_msg, 12, K_ACC2, 0); }
    const char *ec = contactos_error_carga(); if (ec[0]) { fb_fill_rect(wx, wy + 57, ww, 26, 0x003B1111u); txc(x + 20, y + 57, w - 40, 26, ec, 12, K_RED, 1); }
}

/* ---------------------------------------------------------------- acciones */
static void abre_barra(int tipo)
{
    g_barra = tipo; g_foco = F_BARRA; g_nsug = 0; g_campo = -1;
    tf_init(&g_tbarra, g_bbarra, sizeof g_bbarra, 0);
    if (tipo == B_IMPORTAR) {
        tf_set(&g_tbarra, "/usuario/");
        static char lista[8192]; lista[0] = 0;
        static const char *const D[] = { "/usuario", "/usuario/Descargas", "/workspace/Descargas", 0 };
        for (int d = 0; D[d] && g_nsug < 12; ++d) {
            lista[0] = 0; (void)vfs_format_list_ext(0, D[d], lista, sizeof lista);
            const char *s = lista; if (s[0] == 'A') { while (*s && *s != ' ') ++s; ++s; }
            while (*s && g_nsug < 12) {
                const char *e = s; while (*e && !(e[0] == ',' && e[1] == ' ')) ++e;
                int L = (int)(e - s); if (L > 95) L = 95;
                char r[96]; for (int i = 0; i < L; ++i) r[i] = s[i]; r[L] = 0;
                if (L > 4 && (r[L - 4] == '.') && (r[L - 3] | 32) == 'v' && (r[L - 2] | 32) == 'c' && (r[L - 1] | 32) == 'f' && r[0] == '/') { cpy(g_sug[g_nsug++], r, 96); }
                s = *e ? e + 2 : e;
            }
        }
        if (g_nsug) tf_set(&g_tbarra, g_sug[0]);
    } else if (tipo == B_FOTOS) {
        tf_set(&g_tbarra, "/fotos/");
        static char r[12][96]; int n = contactos_imagenes(r, 12);
        for (int i = 0; i < n; ++i) cpy(g_sug[i], r[i], 96);
        g_nsug = n;
    } else tf_set(&g_tbarra, "");
    ++g_ver;
}
static void barra_ok(void)
{
    char res[600];
    if (g_barra == B_NUEVO) {
        if (!g_bbarra[0]) return;
        int id = contactos_anade_texto(g_bbarra, 0, 0);
        if (id > 0) { g_sel = (uint32_t)id; g_pest = 0; g_lista_ok = 0; g_filtro = 0; tf_set(&g_tbusca, ""); g_dscroll = 0; avisa("Contacto creado: completa la ficha pulsando en cada dato."); g_barra = B_NADA; g_foco = F_NADA; }
        else avisa(id == -2 ? "La libreta est\303\241 llena." : id == -3 ? "No se pudo guardar." : "Escribe al menos un nombre, un email o un tel\303\251fono.");
    } else if (g_barra == B_IMPORTAR) {
        int r = contactos_importa(g_bbarra, res, sizeof res);
        for (char *z = res; *z; ++z) if (*z == '\n') *z = ' ';
        avisa(res);
        if (r >= 0) { g_barra = B_NADA; g_foco = F_NADA; g_lista_ok = 0; if (!g_sel) { rehace_lista(); if (g_n) g_sel = contactos_libreta()->c[g_idx[0]].id; } }
    } else if (g_barra == B_FOTOS) {
        struct ct_contacto *c = elegido(); if (!c) return;
        int r = contactos_foto_anade(c->id, g_bbarra, res, sizeof res); avisa(res);
        if (r == 0) { g_barra = B_NADA; g_foco = F_NADA; g_foto_sel = c->nfotos; }
    }
    ++g_ver;
}
int contactos_gui_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    (void)wx; (void)wy; (void)ww; (void)wh; ++g_ver;
    for (int i = g_nh - 1; i >= 0; i--) {
        struct hit *t = &g_hit[i]; if (mx < t->x || mx >= t->x + t->w || my < t->y || my >= t->y + t->h) continue;
        if (t->tipo != H_BORRAR) g_borrar_armado = 0;
        if (g_foco == F_CAMPO && t->tipo != H_CAMPO_OK && !(t->tipo == H_CAMPO && t->val == g_campo)) { guarda_campo(0); if (g_foco == F_CAMPO) return 0; }
        struct ct_contacto *c = elegido();
        switch (t->tipo) {
        case H_BUSCA: g_foco = F_BUSCA; tf_press(&g_tbusca, mx, my, 0); break;
        case H_NUEVO: abre_barra(B_NUEVO); break;
        case H_IMPORTAR: abre_barra(B_IMPORTAR); break;
        case H_A_HOJA: { char r[300]; int k = hoja_desde_contactos(r, sizeof r); avisa(r); if (k >= 0) return 4; break; }   /* SOMA_360_HOJA_GRAFICOS */
        case H_EXPORTAR: { char r[400]; (void)contactos_exporta(CT_VCF_DEF, 1, r, sizeof r); for (char *z = r; *z; ++z) if (*z == '\n') *z = ' '; avisa(r); break; }
        case H_CHIP:
            if (t->val == -1) g_filtro = 0; else if (t->val == -2) g_filtro = g_filtro == 1 ? 0 : 1; else if (t->val == -3) g_filtro = g_filtro == 2 ? 0 : 2;
            else { static char gr[16][CT_GRUPO]; int ng = ct_grupos(contactos_libreta(), gr, 16); if (t->val < ng) { if (g_filtro == 3 && igual(g_grupo, gr[t->val])) g_filtro = 0; else { cpy(g_grupo, gr[t->val], sizeof g_grupo); g_filtro = 3; } } }
            g_lista_ok = 0; g_lscroll = 0; break;
        case H_FILA: g_sel = (uint32_t)t->val; g_dscroll = 0; g_foto_sel = 1; g_foco = F_NADA; break;
        case H_FAV: if (c) { c->fav = !c->fav; (void)contactos_toca(c->id); g_lista_ok = 0; } break;
        case H_CORREO: if (c && c->email[0][0]) { cpy(g_email, c->email[0], sizeof g_email); return 3; } break;
        case H_BORRAR:
            if (!c) break;
            if (!g_borrar_armado) { g_borrar_armado = 1; break; }
            g_borrar_armado = 0; { uint32_t id = c->id; if (contactos_borra(id) == 0) { avisa("Contacto borrado."); g_sel = 0; g_lista_ok = 0; rehace_lista(); if (g_n) g_sel = contactos_libreta()->c[g_idx[0]].id; } }
            break;
        case H_PESTANA: g_pest = t->val; g_dscroll = 0; g_campo = -1; if (g_foco == F_CAMPO) g_foco = F_NADA; break;
        case H_CAMPO: if (g_campo == t->val && g_foco == F_CAMPO) tf_press(&g_tcampo, mx, my, 0); else edita_campo(t->val); break;
        case H_CAMPO_OK: guarda_campo(0); break;
        case H_TIPO: if (c) {
            int k = t->val; uint8_t *tp = k <= 5 ? &c->t_email[k - 3] : &c->t_tel[k - 6];
            *tp = (uint8_t)(k <= 5 ? (*tp == CT_T_CASA ? CT_T_TRABAJO : *tp == CT_T_TRABAJO ? CT_T_OTRO : CT_T_CASA) : (*tp + 1) % 4);
            (void)contactos_toca(c->id); } break;
        case H_BARRA: g_foco = F_BARRA; tf_press(&g_tbarra, mx, my, 0); break;
        case H_BARRA_OK: barra_ok(); break;
        case H_BARRA_NO: g_barra = B_NADA; g_foco = F_NADA; break;
        case H_SUGIERE: if (t->val < g_nsug) { tf_set(&g_tbarra, g_sug[t->val]); if (g_barra != B_NUEVO) barra_ok(); } break;
        case H_FOTO: g_foto_sel = t->val; break;
        case H_FOTO_ANADIR: abre_barra(B_FOTOS); break;
        case H_FOTO_CAMARA: avisa("Haz la foto en la C\303\241mara y vuelve: saldr\303\241 la primera en \302\253A\303\261adir foto\302\273."); return 2;
        case H_FOTO_PRINC: if (c && contactos_foto_principal(c->id, t->val) == 0) { g_foto_sel = 1; avisa("Esa foto sale ahora en la ficha."); } break;
        case H_FOTO_QUITAR: if (c && contactos_foto_borra(c->id, t->val) == 0) { if (g_foto_sel > 1) g_foto_sel--; avisa("Foto quitada."); } break;
        case H_VOZ_GRABAR: if (c) { uint32_t vid; int st = contactos_voz_estado(&vid, 0, 0); char m[160]; if (st == CTV_GRABANDO) contactos_voz_para(); else if (contactos_voz_graba(c->id, m, sizeof m)) avisa(m); } break;
        case H_VOZ_ESCUCHAR: if (c) { uint32_t vid; int st = contactos_voz_estado(&vid, 0, 0); char m[160]; if (st == CTV_ESCUCHANDO) contactos_voz_para(); else if (contactos_voz_escucha(c->id, m, sizeof m)) avisa(m); } break;
        case H_VOZ_BORRAR: if (c) { contactos_voz_para(); if (contactos_voz_borra(c->id) == 0) avisa("Voz borrada."); } break;
        default: break;
        }
        return 0;
    }
    if (g_foco == F_CAMPO) guarda_campo(0);
    else if (g_foco == F_BUSCA) g_foco = F_NADA;
    g_borrar_armado = 0;
    return 0;
}
static void mueve_sel(int d)
{
    if (!g_lista_ok) rehace_lista();
    if (!g_n) return;
    struct ct_libreta *l = contactos_libreta(); int i = 0;
    for (int k = 0; k < g_n; ++k) if (l->c[g_idx[k]].id == g_sel) { i = k; break; }
    i += d; if (i < 0) i = 0; if (i >= g_n) i = g_n - 1;
    g_sel = l->c[g_idx[i]].id; g_dscroll = 0; g_foto_sel = 1;
    if (i * FILA_H < g_lscroll) g_lscroll = i * FILA_H;
    if ((i + 1) * FILA_H > g_lscroll + g_lay_lh) g_lscroll = (i + 1) * FILA_H - g_lay_lh;
}
int contactos_gui_key(char ch)
{
    int c = (uint8_t)ch, ctrl = keyboard_ctrl_held(), shift = keyboard_shift_held(); ++g_ver;
    contactos_gui_init();
    if (g_foco == F_CAMPO) {
        if (c == 27) { g_campo = -1; g_foco = F_NADA; return 1; }
        int r = tf_key(&g_tcampo, ch, ctrl, shift);
        if (r == TF_ENTER) guarda_campo(0); else if (r == TF_TAB) guarda_campo(1);
        return 1;
    }
    if (g_foco == F_BARRA) {
        if (c == 27) { g_barra = B_NADA; g_foco = F_NADA; return 1; }
        int r = tf_key(&g_tbarra, ch, ctrl, shift);
        if (r == TF_ENTER) barra_ok();
        return 1;
    }
    if (g_foco == F_BUSCA) {
        if (c == 27) { if (g_bbusca[0]) tf_set(&g_tbusca, ""); else g_foco = F_NADA; g_lista_ok = 0; return 1; }
        if (c == 16 || c == 14) { mueve_sel(c == 16 ? -1 : 1); return 1; }
        int r = tf_key(&g_tbusca, ch, ctrl, shift);
        if (r == TF_EDIT) { g_lista_ok = 0; g_lscroll = 0; rehace_lista(); if (g_n) g_sel = contactos_libreta()->c[g_idx[0]].id; }
        if (r == TF_ENTER) { g_foco = F_NADA; g_pest = 0; }
        return 1;
    }
    if (c == 27) { if (g_barra) { g_barra = B_NADA; return 1; } if (g_borrar_armado) { g_borrar_armado = 0; return 1; } return 0; }
    if (c == 16) { mueve_sel(-1); return 1; }
    if (c == 14) { mueve_sel(1); return 1; }
    if (c == 9) { g_pest = (g_pest + 1) % 3; return 1; }
    if (c == 10 || c == 13) { if (elegido()) edita_campo(0); return 1; }
    if (ctrl && (c == 'n' || c == 14)) { abre_barra(B_NUEVO); return 1; }
    if (c >= 32 && c != 127) { g_foco = F_BUSCA; tf_key(&g_tbusca, ch, ctrl, shift); g_lista_ok = 0; g_lscroll = 0; rehace_lista(); if (g_n) g_sel = contactos_libreta()->c[g_idx[0]].id; return 1; }
    return 1;
}
int contactos_gui_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held)
{
    int r = 0;
    int dentro = mx >= (int32_t)x && my >= (int32_t)y && mx < (int32_t)(x + w) && my < (int32_t)(y + h);
    if (held) {
        if (g_foco == F_CAMPO && g_tcampo.dragging && tf_drag(&g_tcampo, mx, my, held)) r = 1;
        if (g_foco == F_BUSCA && g_tbusca.dragging && tf_drag(&g_tbusca, mx, my, held)) r = 1;
        if (g_foco == F_BARRA && g_tbarra.dragging && tf_drag(&g_tbarra, mx, my, held)) r = 1;
    }
    if (wheel && dentro) {
        int d = -wheel; if (d > 5) d = 5; if (d < -5) d = -5;
        if (mx < g_lay_dx) { g_lscroll += d * FILA_H; if (g_lscroll < 0) g_lscroll = 0; if (g_lscroll > g_lmax) g_lscroll = g_lmax; }
        else if (g_pest == 0) { g_dscroll += d * 36; if (g_dscroll < 0) g_dscroll = 0; if (g_dscroll > g_dmax) g_dscroll = g_dmax; }
        r = 1;
    }
    if (r) ++g_ver;
    return r;
}
int contactos_gui_tick(void)
{
    int r = contactos_tick();
    if (g_msg[0] && timer_get_uptime_ms() - g_msg_t > 9000) { g_msg[0] = 0; r = 1; }
    if (g_pend) r = 1;
    if (r) ++g_ver;
    return r;
}
