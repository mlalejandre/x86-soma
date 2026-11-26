/* SOMA_331_DISCOS: la aplicacion Discos. A la izquierda los discos; a la derecha el disco elegido con sus particiones en una
 * barra a escala (colores por sistema de archivos, el espacio libre rayado) y lo que se puede hacer con el. Todo lo que escribe
 * pide confirmacion: para desbloquear el disco hay que escribir su nombre, y cada operacion destructiva se confirma otra vez.
 * El trabajo va en un hilo (discos.c): la ventana muestra el progreso y el resultado. */
#include "discos_gui.h"
#include "discos.h"
#include "fb.h"
#include "web_font.h"
#include "gui_font.h"
#include "unidades.h"
#include "exfat.h"

#define C_WIN   0x000C111B
#define C_SIDE  0x000F1520
#define C_LINE  0x00212B3B
#define C_CARD  0x00111827
#define C_TXT   0x00E5EAF2
#define C_DIM   0x008592A8
#define C_FAINT 0x00566379
#define C_ACC   0x0038BDF8
#define C_SEL   0x001A3558
#define C_ROJO  0x00F87171
#define C_VERDE 0x004ADE80
#define C_AMBAR 0x00FBBF24
#define SIDE 250

static int g_sel = -1, g_part = -1, g_ini;
static uint32_t g_ver = 1, g_visto;
static char g_aviso[200]; static int g_aviso_ok;

/* dialogo */
enum { D_NADA = 0, D_DESBLOQUEA, D_DISCO, D_NUEVA, D_FORMATEA, D_BORRA, D_GRABA };
static int g_dlg, g_foco, g_fs = DC_FS_EXFAT, g_esq = DC_GPT;
static char g_f_etq[24], g_f_tam[12], g_f_conf[16], g_f_ruta[160];
#define NIMG 12
static char g_img[NIMG][160]; static uint64_t g_img_tam[NIMG]; static int g_nimg;

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, uint64_t v) { char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23); char r[24]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int s_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void boton(int x, int y, int w, const char *s, int estilo)   /* 1 = principal, 2 = peligro, 0 = normal, -1 = apagado */
{
    uint32_t bg = estilo == 1 ? 0xFF0E7490u : estilo == 2 ? 0xFF7F1D1Du : estilo < 0 ? 0xFF161D2Au : 0xFF1E293Bu;
    fb_fill_rrect_aa(x, y, w, 30, 8, bg);
    if (estilo == 0) fb_stroke_rrect_aa(x, y, w, 30, 8, 0xFF334155u);
    tx(x + (w - tw(s, 12, 1)) / 2, y, 30, s, 12, estilo < 0 ? C_FAINT : 0x00FFFFFF, 1);
}
static void campo(int x, int y, int w, const char *s, int foco, const char *pista)
{
    fb_fill_rrect_aa(x, y, w, 30, 8, 0xFF0C1119u);
    fb_stroke_rrect_aa(x, y, w, 30, 8, foco ? (0xFF000000u | C_ACC) : 0xFF263142u);
    fb_set_clip(x + 8, y, w - 16, 30);
    int n = s_len(s), st = 0; while (st < n && tw(s + st, 12, 0) > w - 24) st++;
    if (n) tx(x + 10, y, 30, s + st, 12, C_TXT, 0); else if (!foco) tx(x + 10, y, 30, pista, 12, C_FAINT, 0);
    if (foco) fb_fill_rect((uint32_t)(x + 11 + (n ? tw(s + st, 12, 0) : 0)), (uint32_t)(y + 7), 1, 16, C_ACC);
    fb_reset_clip();
}
static uint32_t color_fs(int fs)
{
    switch (fs) {
    case DC_FS_EXFAT: return 0x0038BDF8; case DC_FS_FAT32: case DC_FS_FAT16: case DC_FS_FAT12: return 0x0034D399;
    case DC_FS_NTFS: return 0x00A78BFA; case DC_FS_EXT: return 0x00F59E0B; case DC_FS_BIGFS: return 0x00F472B6;
    case DC_FS_ISO9660: return 0x00FB923C; case DC_FS_VACIO: return 0x0064748B; default: return 0x0094A3B8;
    }
}
static void icono_disco(int x, int y, uint32_t col)
{
    fb_fill_rrect_aa(x, y + 4, 26, 20, 5, 0xFF000000u | col);
    fb_fill_rect((uint32_t)(x + 4), (uint32_t)(y + 17), 18, 2, 0x000C111B);
    fb_fill_circle(x + 20, y + 10, 2, 0x000C111B);
}

void discos_gui_init(void) { if (g_sel < 0 || !g_ini) { discos_lista(); g_ini = 1; if (g_sel < 0 && discos_info(0)) g_sel = 0; } }
uint32_t discos_gui_version(void) { return g_ver + discos_version() * 7u; }
int discos_gui_tick(void)
{
    uint32_t v = discos_version();
    if (v != g_visto) { g_visto = v; g_ver++; return 1; }
    return 0;
}

static const struct disco_info *sel(void) { return discos_info(g_sel); }

/* ---------------------------------------------------------------- pintar */
static void pinta_lista(int x, int y, int h)
{
    fb_fill_rect((uint32_t)x, (uint32_t)y, SIDE, (uint32_t)h, C_SIDE);
    fb_fill_rect((uint32_t)(x + SIDE - 1), (uint32_t)y, 1, (uint32_t)h, C_LINE);
    tx(x + 20, y + 14, 30, "Discos", 18, C_TXT, 1);
    tx(x + 20, y + 42, 20, "unidades y particiones", 12, C_DIM, 0);
    for (int i = 0; discos_info(i); i++) {
        const struct disco_info *d = discos_info(i);
        int cy = y + 76 + i * 64, on = i == g_sel;
        if (on) { fb_fill_rrect_aa(x + 10, cy, SIDE - 20, 56, 10, 0xFF1A3558u); fb_fill_rect((uint32_t)(x + 10), (uint32_t)(cy + 14), 3, 28, C_ACC); }
        icono_disco(x + 22, cy + 12, d->sistema ? C_AMBAR : d->escribible ? C_VERDE : C_ACC);
        char t[48]; t[0] = 0; s_cat(t, d->nombre, sizeof t);
        tx(x + 58, cy + 4, 24, t, 13, C_TXT, 1);
        char sz[24]; discos_tam(sz, sizeof sz, d->sectores);
        char l2[64]; l2[0] = 0; s_cat(l2, sz, sizeof l2); s_cat(l2, "  ", sizeof l2); s_cat(l2, d->driver, sizeof l2);
        tx(x + 58, cy + 26, 22, l2, 11, C_DIM, 0);
        const char *b = d->sistema ? "SISTEMA" : d->escribible ? "ABIERTO" : "PROTEGIDO";
        uint32_t bc = d->sistema ? C_AMBAR : d->escribible ? C_VERDE : C_FAINT;
        int bw = tw(b, 9, 1) + 12; fb_stroke_rrect_aa(x + SIDE - 22 - bw, cy + 8, bw, 16, 8, 0xFF000000u | bc); tx(x + SIDE - 16 - bw, cy + 8, 16, b, 9, bc, 1);
    }
    if (!discos_info(0)) tx(x + 20, y + 80, 24, "No hay discos", 12, C_DIM, 0);
    boton(x + 20, y + h - 46, SIDE - 40, "Volver a leer", 0);
}

static int bar_x, bar_y, bar_w, bar_h, seg_x[DC_PART_MAX + 1], seg_w[DC_PART_MAX + 1];
static void pinta_barra(const struct disco_info *d, int x, int y, int w)
{
    bar_x = x; bar_y = y; bar_w = w; bar_h = 54;
    fb_fill_rrect_aa(x, y, w, bar_h, 10, 0xFF111827u);
    /* espacio libre: rayas */
    for (int k = 0; k < w; k += 8) fb_fill_rect((uint32_t)(x + k), (uint32_t)(y + 4), 3, (uint32_t)(bar_h - 8), 0x00182234);
    uint64_t tot = d->sectores ? d->sectores : 1;
    for (int i = 0; i < d->tabla.n && i < DC_PART_MAX; i++) {
        const struct dc_part *p = &d->tabla.p[i];
        int a = x + (int)((p->lba * (uint64_t)w) / tot), b = x + (int)(((p->lba + p->n) * (uint64_t)w) / tot);
        if (b - a < 14) b = a + 14;
        if (b > x + w) b = x + w;
        seg_x[i] = a; seg_w[i] = b - a;
        uint32_t col = color_fs(d->fs[i]);
        fb_fill_rrect_aa(a + 1, y + 3, b - a - 2, bar_h - 6, 8, 0xFF000000u | col);
        if (i == g_part) fb_stroke_rrect_aa(a, y + 1, b - a, bar_h - 2, 9, 0xFFFFFFFFu);
        if (b - a > 60) {
            char t[40]; t[0] = 0; s_num(t, sizeof t, (uint64_t)i + 1); s_cat(t, ". ", sizeof t); s_cat(t, d->etiqueta[i][0] ? d->etiqueta[i] : dc_fs_nombre(d->fs[i]), sizeof t);
            fb_set_clip(a + 6, y, b - a - 12, bar_h);
            tx(a + 8, y + 6, 22, t, 12, 0x000B1220, 1);
            char sz[24]; discos_tam(sz, sizeof sz, p->n); tx(a + 8, y + 26, 20, sz, 11, 0x000B1220, 0);
            fb_reset_clip();
        }
    }
    if (d->tabla.esquema == DC_NADA) {
        const char *t = d->fs_disco > DC_FS_VACIO ? dc_fs_nombre(d->fs_disco) : "Sin tabla de particiones";
        tx(x + (w - tw(t, 13, 1)) / 2, y, bar_h, t, 13, C_DIM, 1);
    }
}

/* botones de accion: si no caben en una fila (ventana estrecha) siguen en la de abajo */
static int bx[8], bw_[8], by_[8], bn, bact[8], g_ax0, g_alim;
static void boton_accion(int *x, int *y, const char *s, int act, int estilo)
{
    int w = tw(s, 12, 1) + 28;
    if (*x > g_ax0 && *x + w > g_alim) { *x = g_ax0; *y += 40; }
    boton(*x, *y, w, s, estilo);
    if (bn < 8) { bx[bn] = *x; bw_[bn] = w; by_[bn] = *y; bact[bn] = act; bn++; }
    *x += w + 10;
}

static void pinta_detalle(const struct disco_info *d, int x, int y, int w, int h)
{
    char t[160];
    tx(x, y, 30, d->nombre, 20, C_TXT, 1);
    char sz[24]; discos_tam(sz, sizeof sz, d->sectores);
    t[0] = 0; s_cat(t, sz, sizeof t); s_cat(t, "  -  ", sizeof t); s_cat(t, d->driver, sizeof t); s_cat(t, "  -  ", sizeof t);
    s_cat(t, d->tabla.esquema == DC_GPT ? "tabla GPT" : d->tabla.esquema == DC_MBR ? "tabla MBR" : "sin tabla", sizeof t);
    tx(x, y + 30, 22, t, 12, C_DIM, 0);
    if (d->sistema) tx(x, y + 52, 22, "Es el disco del sistema: se puede mirar, pero SOMA no lo modifica desde aqui.", 12, C_AMBAR, 0);
    else if (!d->escribible) tx(x, y + 52, 22, "Protegido: para cambiar algo hay que desbloquearlo antes.", 12, C_DIM, 0);
    else tx(x, y + 52, 22, "Desbloqueado: las operaciones pueden BORRAR datos de este disco.", 12, C_ROJO, 0);
    if (d->tabla.aviso[0]) tx(x, y + 74, 22, d->tabla.aviso, 12, C_AMBAR, 0);
    pinta_barra(d, x, y + 100, w);
    /* lista de particiones */
    int ly = y + 170;
    tx(x, ly, 22, "Particion", 11, C_FAINT, 1); tx(x + 120, ly, 22, "Tamano", 11, C_FAINT, 1); tx(x + 220, ly, 22, "Tipo", 11, C_FAINT, 1);
    tx(x + 390, ly, 22, "Formato", 11, C_FAINT, 1); tx(x + 500, ly, 22, "Etiqueta", 11, C_FAINT, 1);
    ly += 24;
    for (int i = 0; i < d->tabla.n && ly < y + h - 150; i++) {
        const struct dc_part *p = &d->tabla.p[i];
        if (i == g_part) fb_fill_rrect_aa(x - 8, ly, w + 16, 28, 7, 0xFF1A3558u);
        fb_fill_circle(x + 6, ly + 14, 5, color_fs(d->fs[i]));
        t[0] = 0; s_num(t, sizeof t, (uint64_t)i + 1); if (p->logica) s_cat(t, " (logica)", sizeof t);
        tx(x + 18, ly, 28, t, 12, C_TXT, i == g_part);
        discos_tam(sz, sizeof sz, p->n); tx(x + 120, ly, 28, sz, 12, C_TXT, 0);
        tx(x + 220, ly, 28, dc_tipo_nombre(&d->tabla, p), 12, C_DIM, 0);
        tx(x + 390, ly, 28, dc_fs_nombre(d->fs[i]), 12, C_DIM, 0);
        t[0] = 0; s_cat(t, d->etiqueta[i], sizeof t); if (p->nombre[0]) { if (t[0]) s_cat(t, "  ", sizeof t); s_cat(t, "[", sizeof t); s_cat(t, p->nombre, sizeof t); s_cat(t, "]", sizeof t); }
        fb_set_clip(x + 500, ly, w - 500, 28); tx(x + 500, ly, 28, t, 12, C_DIM, 0); fb_reset_clip();
        ly += 30;
    }
    if (!d->tabla.n && d->tabla.esquema != DC_NADA) tx(x, ly, 28, "Sin particiones: todo el disco esta libre.", 12, C_DIM, 0);
    /* acciones */
    static const char *const nom[] = { "Desbloquear...", "Formatear todo...", "Nueva particion...", "Formatear particion...", "Borrar particion", "Grabar imagen..." };
    int desb = !d->sistema && !d->escribible, total = 0;
    for (int k = desb ? 0 : 1; k < 6; k++) total += tw(nom[k], 12, 1) + 38;
    int ax = x, ay = y + h - (total - 10 > w ? 136 : 96); bn = 0;
    g_ax0 = x; g_alim = x + w;
    if (desb) boton_accion(&ax, &ay, nom[0], D_DESBLOQUEA, 1);
    boton_accion(&ax, &ay, nom[1], D_DISCO, d->escribible ? 2 : -1);
    boton_accion(&ax, &ay, nom[2], D_NUEVA, d->escribible ? 0 : -1);
    boton_accion(&ax, &ay, nom[3], D_FORMATEA, d->escribible && g_part >= 0 ? 0 : -1);
    boton_accion(&ax, &ay, nom[4], D_BORRA, d->escribible && g_part >= 0 ? 2 : -1);
    boton_accion(&ax, &ay, nom[5], D_GRABA, d->escribible ? 0 : -1);
}

static void pinta_progreso(int x, int y, int w)
{
    char m[200]; int pct = 0;
    int st = discos_estado(m, sizeof m, &pct);
    if (st == DISCOS_TRABAJANDO) {
        tx(x, y, 22, "Trabajando... no quites el disco", 12, C_TXT, 1);
        fb_fill_rrect_aa(x, y + 26, w, 10, 5, 0xFF1E293Bu);
        fb_fill_rrect_aa(x, y + 26, w * pct / 100 < 10 ? 10 : w * pct / 100, 10, 5, 0xFF000000u | C_ACC);
    } else if (st == DISCOS_HECHO || st == DISCOS_ERROR) {
        tx(x, y, 22, m, 12, st == DISCOS_HECHO ? C_VERDE : C_ROJO, 1);
    } else if (g_aviso[0]) tx(x, y, 22, g_aviso, 12, g_aviso_ok ? C_VERDE : C_ROJO, 0);
}

static int dlg_x, dlg_y, dlg_w, dlg_h;
static int f_y[4], f_n; static int opt_y, ok_x, cancel_x, btn_y, img_y;
static void pinta_dialogo(int wx, int wy, int ww, int wh)
{
    const struct disco_info *d = sel();
    if (!d) { g_dlg = D_NADA; return; }
    fb_fill_rect_alpha(wx, wy, ww, wh, 0xA0000000u);
    dlg_w = ww - 160 < 560 ? ww - 160 : 560; dlg_h = g_dlg == D_GRABA ? 420 : 300; dlg_x = wx + (ww - dlg_w) / 2; dlg_y = wy + (wh - dlg_h) / 2;
    fb_fill_rrect_aa(dlg_x, dlg_y, dlg_w, dlg_h, 14, 0xFF111827u); fb_stroke_rrect_aa(dlg_x, dlg_y, dlg_w, dlg_h, 14, 0xFF334155u);
    int x = dlg_x + 24, y = dlg_y + 18, w = dlg_w - 48;
    static const char *const tit[] = { "", "Desbloquear el disco", "Formatear todo el disco", "Nueva particion", "Formatear la particion", "Borrar la particion", "Grabar una imagen en el disco" };
    tx(x, y, 28, tit[g_dlg], 16, C_TXT, 1); y += 36;
    char t[160]; t[0] = 0;
    switch (g_dlg) {
    case D_DESBLOQUEA: s_cat(t, "Despues podras cambiar este disco hasta reiniciar SOMA.", sizeof t); break;
    case D_DISCO: s_cat(t, "Se BORRA TODO lo que hay en el disco y queda una sola particion.", sizeof t); break;
    case D_NUEVA: s_cat(t, "En el espacio libre mas grande del disco.", sizeof t); break;
    case D_FORMATEA: s_cat(t, "Se BORRA todo lo que hay en la particion ", sizeof t); s_num(t, sizeof t, (uint64_t)g_part + 1); s_cat(t, ".", sizeof t); break;
    case D_BORRA: s_cat(t, "La particion ", sizeof t); s_num(t, sizeof t, (uint64_t)g_part + 1); s_cat(t, " desaparece con todo lo que tiene.", sizeof t); break;
    case D_GRABA: s_cat(t, "La imagen se copia TAL CUAL desde el principio del disco (se borra todo) y se comprueba.", sizeof t); break;
    }
    tx(x, y, 22, t, 12, g_dlg == D_DESBLOQUEA || g_dlg == D_NUEVA ? C_DIM : C_ROJO, 0); y += 32;
    f_n = 0; opt_y = -1; img_y = -1;
    if (g_dlg == D_DISCO || g_dlg == D_NUEVA || g_dlg == D_FORMATEA) {
        opt_y = y;
        static const char *const fs[2] = { "exFAT", "FAT32" };
        for (int i = 0; i < 2; i++) { int on = (i == 0) == (g_fs == DC_FS_EXFAT); fb_fill_rrect_aa(x + i * 100, y, 92, 30, 8, on ? 0xFF0E7490u : 0xFF1E293Bu); tx(x + i * 100 + (92 - tw(fs[i], 12, 1)) / 2, y, 30, fs[i], 12, 0x00FFFFFF, 1); }
        if (g_dlg == D_DISCO) {
            static const char *const es[2] = { "GPT", "MBR" };
            for (int i = 0; i < 2; i++) { int on = (i == 0) == (g_esq == DC_GPT); fb_fill_rrect_aa(x + 230 + i * 80, y, 72, 30, 8, on ? 0xFF0E7490u : 0xFF1E293Bu); tx(x + 230 + i * 80 + (72 - tw(es[i], 12, 1)) / 2, y, 30, es[i], 12, 0x00FFFFFF, 1); }
        }
        y += 42;
        tx(x, y, 30, "Etiqueta", 12, C_DIM, 0); f_y[f_n++] = y; campo(x + 110, y, w - 110, g_f_etq, g_foco == 0, "SOMA"); y += 40;
        if (g_dlg == D_NUEVA) { tx(x, y, 30, "Tamano (MiB)", 12, C_DIM, 0); f_y[f_n++] = y; campo(x + 110, y, w - 110, g_f_tam, g_foco == 1, "vacio = todo el espacio libre"); y += 40; }
    }
    if (g_dlg == D_GRABA) {
        tx(x, y, 30, "Imagen", 12, C_DIM, 0); f_y[f_n++] = y; campo(x + 110, y, w - 110, g_f_ruta, g_foco == 0, "/usuario/... .img o .iso"); y += 40;
        img_y = y;
        if (!g_nimg) tx(x, y, 22, "No hay imagenes .img/.iso en /usuario (ni en sus carpetas).", 12, C_FAINT, 0);
        for (int i = 0; i < g_nimg && i < 5; i++) {
            int on = s_eq(g_img[i], g_f_ruta);
            if (on) fb_fill_rrect_aa(x - 6, y + i * 28, w + 12, 26, 6, 0xFF1A3558u);
            fb_set_clip(x, y + i * 28, w - 90, 26); tx(x, y + i * 28, 26, g_img[i], 12, C_TXT, on); fb_reset_clip();
            char sz[24]; discos_tam(sz, sizeof sz, (g_img_tam[i] + 511) / 512); tx(x + w - tw(sz, 11, 0), y + i * 28, 26, sz, 11, C_DIM, 0);
        }
        y += 5 * 28 + 8;
    }
    if (g_dlg != D_NUEVA) {
        t[0] = 0; s_cat(t, "Para confirmar escribe: ", sizeof t); s_cat(t, d->nombre, sizeof t);
        tx(x, y, 30, t, 12, C_AMBAR, 1); f_y[f_n++] = y; campo(x + 230, y, w - 230, g_f_conf, g_foco == f_n - 1, ""); y += 40;
    }
    btn_y = dlg_y + dlg_h - 50; cancel_x = dlg_x + dlg_w - 24 - 110; ok_x = cancel_x - 10 - 150;
    int listo = g_dlg == D_NUEVA || s_eq(g_f_conf, d->nombre);
    boton(ok_x, btn_y, 150, g_dlg == D_DESBLOQUEA ? "Desbloquear" : g_dlg == D_GRABA ? "Grabar" : g_dlg == D_BORRA ? "Borrar" : "Aceptar", listo ? (g_dlg == D_DESBLOQUEA || g_dlg == D_NUEVA ? 1 : 2) : -1);
    boton(cancel_x, btn_y, 110, "Cancelar", 0);
}

void discos_gui_render(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    discos_gui_init();
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    fb_fill_rect(wx, wy, ww, wh, C_WIN);
    pinta_lista(x, y, h);
    const struct disco_info *d = sel();
    int cx = x + SIDE + 24, cw = w - SIDE - 48;
    fb_set_clip(x + SIDE, y, w - SIDE, h);
    if (d) pinta_detalle(d, cx, y + 16, cw, h - 32);
    pinta_progreso(cx, y + h - 50, cw);
    fb_reset_clip();
    if (g_dlg) pinta_dialogo(x, y, w, h);
}

/* ---------------------------------------------------------------- acciones */
static void busca_imagenes(void)
{
    g_nimg = 0;
    static struct exfat_dirent e[48]; static char carpetas[6][96]; int nc = 0;
    int tot, n = unid_listar("/usuario", e, 48, &tot);
    for (int i = 0; i < n; i++) if (e[i].is_dir && nc < 6) { carpetas[nc][0] = 0; s_cat(carpetas[nc], "/usuario/", 96); s_cat(carpetas[nc], e[i].name, 96); nc++; }
    for (int c = -1; c < nc && g_nimg < NIMG; c++) {
        const char *dir = c < 0 ? "/usuario" : carpetas[c];
        n = unid_listar(dir, e, 48, &tot);
        for (int i = 0; i < n && g_nimg < NIMG; i++) {
            int l = s_len(e[i].name);
            if (e[i].is_dir || l < 5) continue;
            const char *ext = e[i].name + l - 4;
            if (!(s_eq(ext, ".img") || s_eq(ext, ".iso") || s_eq(ext, ".IMG") || s_eq(ext, ".ISO"))) continue;
            g_img[g_nimg][0] = 0; s_cat(g_img[g_nimg], dir, 160); s_cat(g_img[g_nimg], "/", 160); s_cat(g_img[g_nimg], e[i].name, 160);
            g_img_tam[g_nimg] = e[i].size; g_nimg++;
        }
    }
}
static void abre_dlg(int k)
{
    g_dlg = k; g_foco = 0; g_f_conf[0] = 0; g_f_tam[0] = 0; g_f_etq[0] = 0; g_fs = DC_FS_EXFAT; g_esq = DC_GPT;
    if (k == D_DESBLOQUEA || k == D_BORRA) g_foco = 0;
    if (k == D_GRABA) { busca_imagenes(); g_f_ruta[0] = 0; }
    g_aviso[0] = 0;
}
static char *campo_foco(void)
{
    if (g_dlg == D_DESBLOQUEA || g_dlg == D_BORRA) return g_f_conf;
    if (g_dlg == D_GRABA) return g_foco == 0 ? g_f_ruta : g_f_conf;
    if (g_dlg == D_NUEVA) return g_foco == 0 ? g_f_etq : g_f_tam;
    return g_foco == 0 ? g_f_etq : g_f_conf;
}
static int cap_foco(void) { char *c = campo_foco(); return c == g_f_ruta ? (int)sizeof g_f_ruta : c == g_f_etq ? 12 : c == g_f_tam ? 9 : (int)sizeof g_f_conf; }
static void acepta(void)
{
    const struct disco_info *d = sel(); if (!d) return;
    const char *why = ""; int r = 0;
    if (g_dlg != D_NUEVA && !s_eq(g_f_conf, d->nombre)) return;
    uint32_t mib = 0; for (const char *q = g_f_tam; *q >= '0' && *q <= '9'; q++) mib = mib * 10 + (uint32_t)(*q - '0');
    switch (g_dlg) {
    case D_DESBLOQUEA: r = discos_desbloquea(d->dev, g_f_conf, &why); if (!r) { s_cpy(g_aviso, "Disco desbloqueado hasta reiniciar.", sizeof g_aviso); g_aviso_ok = 1; } break;
    case D_DISCO: r = discos_op(DISCOS_OP_DISCO, d->dev, -1, g_esq, g_fs, 0, g_f_etq, 0, &why); break;
    case D_NUEVA: r = discos_op(DISCOS_OP_NUEVA, d->dev, -1, 0, g_fs, mib, g_f_etq, 0, &why); break;
    case D_FORMATEA: r = discos_op(DISCOS_OP_FORMATEA, d->dev, g_part, 0, g_fs, 0, g_f_etq, 0, &why); break;
    case D_BORRA: r = discos_op(DISCOS_OP_BORRA, d->dev, g_part, 0, 0, 0, 0, 0, &why); if (!r) g_part = -1; break;
    case D_GRABA: if (!g_f_ruta[0]) { why = "elige una imagen"; r = -1; break; } r = discos_op(DISCOS_OP_GRABA, d->dev, -1, 0, 0, 0, 0, g_f_ruta, &why); break;
    }
    if (r) { s_cpy(g_aviso, why, sizeof g_aviso); g_aviso_ok = 0; }
    g_dlg = D_NADA; g_ver++;
}

int discos_gui_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    g_ver++;
    int x = (int)wx, y = (int)wy, h = (int)wh; (void)ww;
    if (g_dlg) {
        if (mx < dlg_x || mx >= dlg_x + dlg_w || my < dlg_y || my >= dlg_y + dlg_h) { g_dlg = D_NADA; return 1; }
        if (my >= btn_y && my < btn_y + 30) {
            if (mx >= cancel_x && mx < cancel_x + 110) { g_dlg = D_NADA; return 1; }
            if (mx >= ok_x && mx < ok_x + 150) { acepta(); return 1; }
        }
        if (opt_y >= 0 && my >= opt_y && my < opt_y + 30) {
            int rx = mx - (dlg_x + 24);
            if (rx >= 0 && rx < 92) g_fs = DC_FS_EXFAT; else if (rx >= 100 && rx < 192) g_fs = DC_FS_FAT32;
            else if (g_dlg == D_DISCO && rx >= 230 && rx < 302) g_esq = DC_GPT; else if (g_dlg == D_DISCO && rx >= 310 && rx < 382) g_esq = DC_MBR;
            return 1;
        }
        for (int k = 0; k < f_n; k++) if (my >= f_y[k] && my < f_y[k] + 30) {
            if (g_dlg == D_NUEVA) g_foco = k;
            else if (g_dlg == D_DESBLOQUEA || g_dlg == D_BORRA) g_foco = 0;
            else g_foco = k == f_n - 1 ? 1 : 0;
            return 1;
        }
        if (img_y >= 0 && my >= img_y && my < img_y + 5 * 28) { int i = (my - img_y) / 28; if (i < g_nimg) s_cpy(g_f_ruta, g_img[i], sizeof g_f_ruta); return 1; }
        return 1;
    }
    if (mx < x + SIDE) {
        if (my >= y + h - 46 && my < y + h - 16) { discos_reescanea(); g_aviso[0] = 0; return 1; }
        int i = (my - (y + 76)) / 64;
        if (my >= y + 76 && discos_info(i)) { g_sel = i; g_part = -1; g_aviso[0] = 0; }
        return 1;
    }
    const struct disco_info *d = sel();
    if (!d) return 1;
    if (discos_estado(0, 0, 0) == DISCOS_HECHO || discos_estado(0, 0, 0) == DISCOS_ERROR) discos_acepta();
    if (my >= bar_y && my < bar_y + bar_h) {
        g_part = -1;
        for (int i = 0; i < d->tabla.n; i++) if (mx >= seg_x[i] && mx < seg_x[i] + seg_w[i]) g_part = i;
        return 1;
    }
    int ly = y + 16 + 170 + 24;
    if (my >= ly && my < ly + d->tabla.n * 30 && my < y + 16 + h - 32 - 150) { int i = (my - ly) / 30; if (i < d->tabla.n) g_part = i; return 1; }
    for (int k = 0; k < bn; k++) if (my >= by_[k] && my < by_[k] + 30 && mx >= bx[k] && mx < bx[k] + bw_[k]) {
        if (discos_estado(0, 0, 0) == DISCOS_TRABAJANDO) return 1;
        int a = bact[k];
        if (d->sistema) return 1;
        if (a != D_DESBLOQUEA && !d->escribible) return 1;
        if ((a == D_FORMATEA || a == D_BORRA) && g_part < 0) return 1;
        abre_dlg(a);
        return 1;
    }
    return 1;
}
int discos_gui_key(char c)
{
    g_ver++;
    if (!g_dlg) {                                   /* sin dialogo: Tab y las flechas cambian de disco */
        int k = (uint8_t)c, n = 0;
        while (discos_info(n)) n++;
        if (n && (k == 9 || k == 14)) { g_sel = (g_sel + 1) % n; g_part = -1; return 1; }
        if (n && k == 16) { g_sel = (g_sel + n - 1) % n; g_part = -1; return 1; }
        return 0;
    }
    if (c == 27) { g_dlg = D_NADA; return 1; }
    if (c == '\t') { int nf = g_dlg == D_DESBLOQUEA || g_dlg == D_BORRA ? 1 : 2; g_foco = (g_foco + 1) % nf; return 1; }
    if (c == '\n' || c == '\r') { acepta(); return 1; }
    char *f = campo_foco(); int n = s_len(f), cap = cap_foco();
    if (c == 8 || c == 127) { if (n) f[n - 1] = 0; return 1; }
    if ((unsigned char)c >= 32 && (unsigned char)c < 127 && n + 1 < cap) {
        if (f == g_f_tam && (c < '0' || c > '9')) return 1;
        f[n] = c; f[n + 1] = 0;
    }
    return 1;
}
