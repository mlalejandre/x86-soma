/* SOMA_283_MEMORIA_APP: la aplicacion de Memoria (src/apps/memview.c) dibujada en el anfitrion con la memoria de verdad (src/agent/memoria.c
 * con el disco, el LLM y el servidor de embeddings falsos de test_memoria_glue_279.c), la fuente de verdad (web_font.c) y el framebuffer de
 * pruebas (tests/layhost/fbhost.c). Recorre las pestanas y ventanas, pulsa botones y teclas, y guarda capturas PNG en la carpeta que se le pase.
 * Uso: test_memview_283 [CARPETA_PNG] */
#define main main_glue_279
#include "test_memoria_glue_279.c"
#undef main
#include <math.h>
#include "memview.h"
#include "../src/apps/memview.c"          /* incluida: la prueba pulsa los botones de verdad (zonas H[]) */
#include "png_enc.h"

/* ---- lo que falta del framebuffer de pruebas y del sistema */
extern uint32_t *g_fb; extern int g_fbw, g_fbh;
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb);
static void put2(int x, int y, uint32_t c, int a)
{
    int32_t x0, y0, x1, y1; fb_get_clip(&x0, &y0, &x1, &y1);
    if (x < x0 || y < y0 || x >= x1 || y >= y1 || x < 0 || y < 0 || x >= g_fbw || y >= g_fbh || a <= 0) return;
    if (a > 255) a = 255;
    uint32_t d = g_fb[y * g_fbw + x];
    uint32_t r = (((c >> 16) & 255) * (uint32_t)a + ((d >> 16) & 255) * (uint32_t)(255 - a)) / 255, g = (((c >> 8) & 255) * (uint32_t)a + ((d >> 8) & 255) * (uint32_t)(255 - a)) / 255, b = ((c & 255) * (uint32_t)a + (d & 255) * (uint32_t)(255 - a)) / 255;
    g_fb[y * g_fbw + x] = 0xFF000000u | r << 16 | g << 8 | b;
}
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | c); }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    int a0 = (int)(argb >> 24); if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
    double cx = x + w / 2.0, cy = y + h / 2.0, hx = w / 2.0 - 0.5, hy = h / 2.0 - 0.5;
    for (int j = y - 1; j <= y + h; j++) for (int i = x - 1; i <= x + w; i++) {
        double px = fabs(i + 0.5 - cx) - (hx - r), py = fabs(j + 0.5 - cy) - (hy - r);
        double ox = px > 0 ? px : 0, oy = py > 0 ? py : 0;
        double d = sqrt(ox * ox + oy * oy) + (px > py ? (px < 0 ? px : 0) : (py < 0 ? py : 0)) - r;
        double cov = 1.0 - fabs(d); if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t argb)
{
    double ax = x0 / 16.0, ay = y0 / 16.0, bx = x1 / 16.0, by = y1 / 16.0, hw = width16 / 32.0; int a0 = (int)(argb >> 24);
    int mnx = (int)floor((ax < bx ? ax : bx) - hw - 1), mxx = (int)ceil((ax > bx ? ax : bx) + hw + 1), mny = (int)floor((ay < by ? ay : by) - hw - 1), mxy = (int)ceil((ay > by ? ay : by) + hw + 1);
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int j = mny; j <= mxy; j++) for (int i = mnx; i <= mxx; i++) {
        double px = i + 0.5 - ax, py = j + 0.5 - ay, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        double ex = px - t * dx, ey = py - t * dy, d = sqrt(ex * ex + ey * ey), cov = hw + 0.5 - d;
        if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; }
void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; }
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t c, int sc) { (void)x; (void)y; (void)s; (void)c; (void)sc; }
static int g_ctrl, g_shift;
int keyboard_ctrl_held(void) { return g_ctrl; }
int keyboard_shift_held(void) { return g_shift; }
static char g_clip[256];
const char *clip_text(uint32_t *n) { *n = (uint32_t)strlen(g_clip); return g_clip; }
int clip_set_text(const char *t, uint32_t n) { if (!n) n = (uint32_t)strlen(t); if (n > 255) n = 255; memcpy(g_clip, t, n); g_clip[n] = 0; return 0; }
int g_wf182_on = 1;

static const char *g_dir;
static int g_shots;
static void guarda_png(const char *nombre)
{
    if (!g_dir) return;
    size_t cap = (size_t)g_fbw * g_fbh * 4 + 4096; uint8_t *out = malloc(cap); void *wk = malloc(png_work_size(g_fbw, g_fbh));
    size_t n = png_encode_rgb(g_fb, g_fbw, g_fbh, out, cap, wk);
    char r[300]; snprintf(r, sizeof r, "%s/%s.png", g_dir, nombre); FILE *f = fopen(r, "wb"); if (f) { fwrite(out, 1, n, f); fclose(f); g_shots++; }
    free(out); free(wk);
}
static int W = 1280, Hh = 860;
static void pinta(const char *nombre) { memset(g_fb, 0, (size_t)g_fbw * g_fbh * 4); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); guarda_png(nombre); }
static void clic(int x, int y) { memview_mouse(x, y, 0, 0, (uint32_t)W, (uint32_t)Hh, 0, 0); memview_handle_click(x, y, 0, 0, (uint32_t)W, (uint32_t)Hh); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); }
static void tecla(char c) { memview_handle_key(c); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); }
static void escribe(const char *s) { while (*s) tecla(*s++); }
static uint32_t pix(int x, int y) { return g_fb[y * g_fbw + x] & 0xFFFFFF; }
/* el hilo de peticiones no corre en la prueba: se atiende a mano */
static void atiende(void) { if (g_pet_hay) { struct mem_pet pt = g_pet; g_pet_hay = 0; ops_ejecuta(&pt); g_ops_n++; g_ops_ocupado = 0; g_ver++; } memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); }

/* pulsa el boton (o zona) con esa accion; a < 0 = cualquiera. 1 si lo encontro */
#define ANY -999
static int pulsa_ab(int act, int a, int b)
{
    for (int i = nH - 1; i >= 0; i--) if (H[i].act == act && H[i].a == a && H[i].b == b) { clic(H[i].x + H[i].w / 2, H[i].y + H[i].h / 2); return 1; }
    printf("  (no encuentro el boton %d/%d/%d)\n", act, a, b); return 0;
}
static int pulsa(int act, int a)
{
    for (int i = nH - 1; i >= 0; i--) if (H[i].act == act && (a == ANY || H[i].a == a)) { clic(H[i].x + H[i].w / 2, H[i].y + H[i].h / 2); return 1; }
    printf("  (no encuentro el boton %d/%d)\n", act, a); return 0;
}
static int hay_boton(int act, int a) { for (int i = 0; i < nH; i++) if (H[i].act == act && (a == ANY || H[i].a == a)) return 1; return 0; }
static void ctrl_a(void) { g_ctrl = 1; tecla(1); g_ctrl = 0; }
static int cart_idx(const char *id) { return cart_busca(id); }

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : 0;
    g_fbw = 1600; g_fbh = 1000; g_fb = calloc((size_t)g_fbw * g_fbh, 4);
    g_c.srv = 0;
    cmd("estado");
    g_c.srv = 1; g_srv_vivo = 1; g_llm_vivo = 1;
    /* una memoria con algo dentro, ordenada por el ciclo de sueno de verdad (con el LLM falso) */
    ingiere("Me llamo Marcos y vivo en Madrid"); ingiere("Ahora vivo en Bilbao"); ingiere("Mi perro se llama Thor"); ingiere("Mi perro Thor es muy travieso");
    ingiere("Me tome un cafe"); ingiere("Trabajo en Iberdrola");
    for (int i = 0; i < 14; i++) { char t[80]; snprintf(t, sizeof t, "Viaje numero %d a un sitio muy lejano", i); ingiere(t); }
    paso(9 * 3);
    cmd("recordar El usuario es alergico a la penicilina"); cmd("cartucho nuevo Trabajo"); cmd("cartucho nuevo Recetas de cocina");
    cmd("cartucho conectar trabajo");
    { struct mc *m = memoria_gui_entra(); uint32_t n = mc_nodo_ruta(m, "Salud/Alergias", 1); mc_asigna(m, busca_celda("penicilina")->id, n); mc_nodo_set_desc(m, n, "Alergias e intolerancias conocidas"); m->nodo[n].sucio = 0; memoria_gui_sale(1); }
    memoria_guarda_todo();
    g_ms += 60000;

    memview_init();
    pinta("01_monitor");
    CK(pix(30, 30) != 0);
    /* ---- monitor: dormir un paso desde el boton */
    uint32_t ps = g_m.pasos;
    CK(pulsa_ab(A_PIDE, MEMP_SUENO, 1)); CK(g_pet_hay && g_pet.op == MEMP_SUENO); atiende(); CKE(g_m.pasos, ps + 1);
    CK(memview_tick() || 1);
    /* ---- pestanas con el raton */
    CK(pulsa(A_TAB, T_EXP)); CKE(g_tab, T_EXP); pinta("02_explorador_sin_ordenar");
    /* ---- crear un tema raiz y un subtema */
    CK(pulsa(A_RAIZ, ANY)); CKE(g_modal, M_IN); escribe("Proyectos"); tecla('\r'); CKE(g_modal, M_NO);
    { uint32_t n = mc_nodo_ruta(&g_m, "Proyectos", 0); CK(n && g_vista == V_NODO && g_nodo == n && g_m.nodo[n].fijo); }
    CK(pulsa(A_NODO_SUB, ANY)); escribe("SOMA/kernel"); tecla('\r');
    uint32_t nsub = mc_nodo_ruta(&g_m, "Proyectos/SOMA-kernel", 0); CK(nsub != 0); CKE(g_nodo, nsub);
    CK(pulsa(A_RAIZ, ANY)); escribe("Proyectos"); tecla('\r'); CK(strstr(g_msg, "Ya existe")); tecla(27); CKE(g_modal, M_NO);
    /* ---- recuerdo nuevo dentro del subtema (la ruta viene puesta) */
    CK(pulsa(A_NUEVO_REC, (int)nsub)); CKE(g_modal, M_EDIT); CK(!strcmp(b_ruta, "Proyectos/SOMA-kernel"));
    escribe("El usuario programa SOMA en C"); pulsa(A_ED_IMP, 1); pulsa(A_ED_IMP, 1); pinta("04b_nuevo_en_tema");
    CK(pulsa(A_ED_GUARDA, ANY)); CKE(g_modal, M_NO);
    const struct mc_item *ns = busca_celda("programa SOMA"); CK(ns && ns->nodo == nsub && ns->imp == 9 && !strcmp(ns->fuente, "Anotado a mano en la aplicacion Memoria"));
    uint32_t idn = ns->id;
    pinta("03_explorador_tema");
    /* ---- abrir, editar texto, importancia y tema */
    CK(pulsa(A_CEL, (int)idn)); CKE(g_modal, M_EDIT); CKE(g_ed_id, idn);
    pinta("04_editor");
    ctrl_a(); escribe("El usuario programa SOMA en C y Python"); pulsa(A_ED_IMP, -1);
    g_foco = &f_ruta; ctrl_a(); escribe("Proyectos"); tecla('\t'); CK(g_foco == &f_edit);
    g_ctrl = 1; tecla('\r'); g_ctrl = 0; CKE(g_modal, M_NO);
    CK(!strcmp(g_m.it[idn - 1].texto, "El usuario programa SOMA en C y Python") && g_m.it[idn - 1].imp == 8 && g_m.it[idn - 1].nodo == mc_nodo_ruta(&g_m, "Proyectos", 0) && !g_m.it[idn - 1].vec);
    /* ---- archivar y restaurar */
    g_vista = V_NODO; g_nodo = mc_nodo_ruta(&g_m, "Proyectos", 0); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(pulsa(A_CEL, (int)idn)); CK(pulsa(A_ED_ARCH, 1)); CKE(g_m.it[idn - 1].estado, MC_ARCHIVADO); CK(g_m.it[idn - 1].t_hasta != 0); tecla(27);
    CK(pulsa(A_VISTA, V_ARCH)); CK(hay_boton(A_CEL, (int)idn)); pinta("03b_archivados");
    CK(pulsa(A_CEL, (int)idn)); CK(pulsa(A_ED_ARCH, 0)); CKE(g_m.it[idn - 1].estado, MC_ACTIVO); tecla(27);
    /* ---- copiar a un cartucho conectado y mover a uno desconectado */
    int kt = cart_idx("trabajo"), kr = cart_idx("recetas_de_cocin");
    g_vista = V_NODO; g_nodo = mc_nodo_ruta(&g_m, "Proyectos", 0); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(kt > 0 && kr > 0);
    CK(pulsa(A_CEL, (int)idn)); CK(pulsa(A_ED_COPIA, ANY)); CKE(g_modal, M_PICK); pinta("04c_elegir_cartucho");
    CK(hay_boton(A_PICK, kt) && hay_boton(A_PICK, kr) && !hay_boton(A_PICK, g_prim));
    uint32_t nt0 = g_mont[kt]->n;
    CK(pulsa(A_PICK, kt)); atiende(); CK(strstr(g_ops_msg, "Copiado a Trabajo: 1 nuevo(s)")); CKE(g_mont[kt]->n, nt0 + 1); CK(busca_celda("Python"));
    uint32_t n0 = g_m.n;
    CK(pulsa(A_VISTA, V_SIN) || 1); g_vista = V_NODO; memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(pulsa(A_CEL, (int)idn)); CK(pulsa(A_ED_MUEVE, ANY)); CK(pulsa(A_PICK, kr)); atiende();
    CK(strstr(g_ops_msg, "Movido a Recetas de cocina: 1 nuevo(s); 1 quitado(s)")); CKE(g_m.n, n0 - 1); CK(!busca_celda("Python"));
    CK(fs_find("/memoria/k_recetas_de_cocin/m000.bin") >= 0);
    /* ---- borrar desde la tarjeta (con confirmacion) */
    CK(pulsa(A_TAB, T_EXP)); pulsa(A_VISTA, V_EPI);
    uint32_t n1 = g_m.n; uint32_t prim_epi = g_lista[0];
    CK(pulsa(A_CEL_BORRA, (int)prim_epi)); CKE(g_modal, M_CONF); pinta("04d_confirmar");
    CK(pulsa(A_CONF_SI, ANY)); atiende(); CKE(g_m.n, n1 - 1); CK(strstr(g_ops_msg, "Borrado(s) para siempre: 1"));
    /* desde el editor: Cancelar vuelve al editor */
    pulsa(A_VISTA, V_EPI); uint32_t e2 = g_lista[0]; pulsa(A_CEL, (int)e2); pulsa(A_ED_BORRA, ANY); CKE(g_modal, M_CONF); pulsa(A_CONF_NO, ANY); CKE(g_modal, M_EDIT); tecla(27);
    /* ---- rama: renombrar, describir, copiar, quitar */
    uint32_t np = mc_nodo_ruta(&g_m, "Proyectos", 0); g_vista = V_NODO; g_nodo = np; g_desc_nodo = 0; memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(pulsa(A_NODO_REN, (int)np)); CK(!strcmp(b_nom, "Proyectos")); ctrl_a(); escribe("Proyectos personales"); tecla('\r');
    CK(!strcmp(g_m.nodo[np].nombre, "Proyectos personales"));
    clic(f_desc.x + 20, f_desc.y + 10); CK(g_foco == &f_desc); ctrl_a(); escribe("Lo que programa el usuario"); tecla('\r');
    CK(!strcmp(g_m.nodo[np].desc, "Lo que programa el usuario") && !g_m.nodo[np].sucio);
    cmd("recordar El usuario quiere terminar el kernel este ano"); { struct mc *m = memoria_gui_entra(); mc_asigna(m, busca_celda("terminar el kernel")->id, nsub); memoria_gui_sale(1); }
    memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(pulsa(A_NODO_COPIA, (int)np)); CK(pulsa(A_PICK, kt)); atiende(); CK(strstr(g_ops_msg, "Copiado a Trabajo: 1 nuevo(s)"));       /* solo el de la rama (no lo archivado ajeno) */
    CK(pulsa(A_NODO_BORRA, (int)np)); CKE(g_modal, M_CONF); CK(hay_boton(A_BORRA_RAMA_TODO, ANY));
    CK(pulsa(A_CONF_SI, ANY)); CK(!g_m.nodo[np].usado && !g_m.nodo[nsub].usado); CK(busca_celda("terminar el kernel") && busca_celda("terminar el kernel")->nodo == 0); CKE(g_vista, V_SIN);
    /* ---- buscar */
    CK(pulsa(A_TAB, T_BUS)); CK(g_foco == &f_busca); escribe("perro"); tecla('\r'); atiende(); pinta("06_buscar");
    { static struct mem_res rs[24]; CK(memoria_gui_resultados(rs, 24, 0) >= 1); }
    memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); CK(g_nrs >= 1);
    { uint32_t idr = g_rs[0].id; CK(pulsa(A_RES, (int)idr)); CKE(g_modal, M_EDIT); CKE(g_ed_id, idr); tecla(27); }
    strcpy(g_clip, " Thor"); g_foco = &f_busca; memview_clip_op(CLIP_OP_PASTE); CK(strstr(b_busca, "perro Thor"));
    tecla(27); CK(!g_foco);
    /* ---- cartuchos */
    CK(pulsa(A_TAB, T_CAR)); pinta("07_cartuchos");
    CK(pulsa(A_C_NUEVO, ANY)); escribe("Libros"); tecla('\t'); escribe("Lo que leo"); tecla('\r'); atiende();
    int kl = cart_idx("libros"); CK(kl > 0 && !strcmp(g_cart[kl].desc, "Lo que leo"));
    memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1);
    CK(pulsa(A_C_MONTA, kl)); atiende(); CK(g_mont[kl] != 0);
    CK(pulsa(A_C_PRIM, kl)); atiende(); CKE(g_prim, kl); CK(strstr(g_ops_msg, "Primario: Libros"));
    pinta("07b_cartuchos_libros_primario");
    CK(pulsa(A_C_PRIM, cart_idx("principal"))); atiende(); CK(!strcmp(g_cart[g_prim].id, "principal"));
    CK(pulsa(A_C_EXPORTA, kt)); CK(!strcmp(b_nom, "/usuario/trabajo.smem")); tecla('\r'); atiende(); CK(fs_find("/usuario/trabajo.smem") >= 0);
    CK(pulsa(A_C_CLONA, kt)); CK(!strcmp(b_nom, "Copia de Trabajo")); tecla('\r'); atiende(); CK(cart_idx("copia_de_trabajo") > 0);
    CK(pulsa(A_C_VACIA, cart_idx("copia_de_trabajo"))); CK(pulsa(A_CONF_SI, ANY)); atiende(); CK(strstr(g_ops_msg, "Vaciado"));
    CK(pulsa(A_C_BORRA, cart_idx("copia_de_trabajo"))); CK(pulsa(A_CONF_SI, ANY)); atiende(); CKE(cart_idx("copia_de_trabajo"), -1);
    CK(!hay_boton(A_C_BORRA, cart_idx("principal")));                                   /* el principal y el primario no tienen Borrar */
    CK(pulsa(A_C_IMPORTA, ANY)); ctrl_a(); escribe("/usuario/trabajo.smem"); tecla('\r'); atiende(); CK(strstr(g_ops_msg, "Importado de /usuario/trabajo.smem"));
    /* una peticion mientras otra esta en curso */
    g_ops_ocupado = 1; pulsa(A_TAB, T_MON); CK(!hay_boton(A_PIDE, MEMP_GUARDAR)); pide(MEMP_GUARDAR, 0, 0, 0, 0); CK(strstr(g_msg, "Espera")); g_ops_ocupado = 0;
    /* ---- ajustes */
    CK(pulsa(A_TAB, T_AJU)); pinta("08_ajustes");
    int s0 = g_c.sueno; for (int i = nH - 1; i >= 0; i--) if (H[i].act == A_CFG && !strcmp((const char *)H[i].f, "sueno")) { clic(H[i].x + 5, H[i].y + 5); break; }
    CKE(g_c.sueno, !s0); CK(fs_find("/etc/memoria.conf") >= 0);
    for (int i = nH - 1; i >= 0; i--) if (H[i].act == A_CFG_PASO && !strcmp((const char *)H[i].f, "sueno_s") && H[i].a > g_c.sueno_s) { clic(H[i].x + 5, H[i].y + 5); break; }
    CKE(g_c.sueno_s, 120);
    clic(f_url.x + 10, f_url.y + 10); ctrl_a(); escribe("http://10.0.0.9:8089"); tecla('\r'); CK(!strcmp(g_c.url, "http://10.0.0.9:8089") && g_c.srv);
    /* ---- archivos: abre en Notas */
    CK(pulsa(A_TAB, T_ARC)); pinta("09_archivos");
    { int r = 0; for (int i = nH - 1; i >= 0; i--) if (H[i].act == A_ARCH && H[i].a == 0) { r = memview_handle_click(H[i].x + 5, H[i].y + 5, 0, 0, (uint32_t)W, (uint32_t)Hh); break; } CKE(r, 4); CK(!strcmp(memview_get_selected_path(), "/agent/soma.txt")); }
    /* ---- teclado: numeros, Tab, flechas, rueda */
    tecla('1'); CKE(g_tab, T_MON); tecla('\t'); CKE(g_tab, T_EXP); g_shift = 1; tecla('\t'); g_shift = 0; CKE(g_tab, T_MON);
    tecla('/'); CK(g_tab == T_BUS && g_foco == &f_busca); tecla(27);
    tecla('2'); pulsa(A_VISTA, V_EPI); { int c0 = g_cscroll; memview_mouse(900, 500, 0, 0, (uint32_t)W, (uint32_t)Hh, -1, 0); memview_render_window(0, 0, (uint32_t)W, (uint32_t)Hh, 1); CK(g_cscroll > c0); }   /* SOMA_286_YOUTUBE: rueda hacia abajo (negativo) baja */
    /* la version cambia con lo que se dibuja */
    { uint32_t v = memview_version(); tecla('3'); CK(memview_version() != v); tecla(27); }
    /* ventana estrecha */
    W = 900; Hh = 640; tecla('1'); pinta("10_monitor_estrecho"); tecla('2'); pulsa(A_NODO, ANY); pinta("11_explorador_estrecho"); tecla('4'); pinta("12_cartuchos_estrecho");
    W = 1280; Hh = 860;
    g_dormido = 1; g_m.pasos = 13; tecla('1'); pinta("13_monitor_sonando"); g_dormido = 0;
    printf("test_memview_283: %d capturas, %d comprobaciones, %d fallos\n", g_shots, checks, fails);
    return fails ? 1 : 0;
}
