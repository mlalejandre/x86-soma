/* SOMA 184: catalogo general de fuentes. Las caras TrueType del navegador (DejaVu Sans, Liberation Sans regular/negrita) son entradas del
 * catalogo del sistema; la interfaz puede usar Liberation; el terminal sigue con la mono. Se compila con gui_font.c
 * (-DSOMA_FONT_HOST_TEST) y web_font.c; los bytes de las fuentes los incrusta web_font.c (web_fontdata.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gui_font.h"
#include "web_font.h"

int g_wf182_on = 1;
extern int g_sysfont_off;

/* ---- framebuffer de prueba: registra la ultima mascara dibujada ---- */
static int m_w, m_h, m_calls; static uint32_t m_sum, m_x, m_y;
void fb_draw_mask8(int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h, uint32_t rgb, uint32_t alpha)
{
    (void)rgb; (void)alpha;
    m_calls++; m_w = w; m_h = h; m_x = (uint32_t)x; m_y = (uint32_t)y;
    uint32_t s = 2166136261u; for (int i = 0; i < w * h; i++) { s ^= mask[i]; s *= 16777619u; } m_sum = s;
}
void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; m_calls++; }
void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *g, uint32_t fg, int sc) { (void)x; (void)y; (void)g; (void)fg; (void)sc; m_calls++; }
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct shot { int w, h, calls; uint32_t sum; };
static struct shot draw_desktop(const char *s)
{
    m_calls = 0; m_w = m_h = 0; m_sum = 0;
    font_draw_string(10, 10, s, 0xFFFFFF, 1);
    struct shot r = { m_w, m_h, m_calls, m_sum }; return r;
}
static struct shot draw_vec(int face, uint32_t cp, int em)
{
    m_calls = 0; m_w = m_h = 0; m_sum = 0;
    font_vec_draw_face(face, 10, 10, cp, 0xFFFFFF, em, 8, 13);
    struct shot r = { m_w, m_h, m_calls, m_sum }; return r;
}
static int same(struct shot a, struct shot b) { return a.w == b.w && a.h == b.h && a.sum == b.sum; }

int main(void)
{
    /* ---------------- catalogo ---------------- */
    g_sysfont_off = 0;
    CHECK(font_get_count() == FONT_COUNT && FONT_COUNT == 36, "catalogo: %d fuentes", font_get_count());
    CHECK(!strcmp(font_get_name(FONT_DEJAVU_SANS), "dejavu-sans"), "nombre 33: %s", font_get_name(33));
    CHECK(!strcmp(font_get_name(FONT_LIBERATION), "liberation-sans"), "nombre 34: %s", font_get_name(34));
    CHECK(!strcmp(font_get_name(FONT_LIBERATION_BOLD), "liberation-sans-bold"), "nombre 35: %s", font_get_name(35));
    CHECK(!strcmp(font_get_name(FONT_VECTOR), "vector"), "la vectorial de siempre conserva su nombre");
    { int seen[FONT_COUNT] = {0}; int ok = 1; for (int o = 0; o < FONT_COUNT; o++) { int i = font_get_sorted_id(o); if (i < 0 || i >= FONT_COUNT || seen[i]++) ok = 0; }
      CHECK(ok, "el listado alfabetico no es una permutacion de los ids");
      int prev = -1; for (int o = 0; o < FONT_COUNT; o++) { int i = font_get_sorted_id(o); if (prev >= 0) CHECK(strcmp(font_get_name(prev), font_get_name(i)) <= 0, "orden roto en %d", o); prev = i; } }
    CHECK(font_set_by_name("Liberation-Sans") == 1 && font_get_current() == FONT_LIBERATION, "font_set_by_name (sin distinguir mayusculas)");
    CHECK(font_set(FONT_LIBERATION_BOLD) == 1 && font_face_of(font_get_current()) == FFACE_LIBERATION_BOLD, "font_set 35");
    CHECK(font_set(36) == 0 && font_set(-1) == 0, "ids fuera de rango");
    CHECK(font_set(FONT_CLASSIC) == 1 && font_face_of(font_get_current()) == -1, "las de mapa de bits no tienen cara");
    for (int f = 0; f < FFACE_COUNT; f++) CHECK(font_face_of(font_face_catalog_id(f)) == f && font_face_name(f)[0], "cara %d: ida y vuelta con el catalogo", f);

    /* ---------------- soma.sysfont=off ---------------- */
    g_sysfont_off = 1;
    CHECK(font_get_count() == 33, "off: %d fuentes (esperadas 33)", font_get_count());
    CHECK(font_set(FONT_LIBERATION) == 0 && font_set_by_name("liberation-sans") == 0 && font_set_by_name("dejavu-sans") == 0, "off: las nuevas no se pueden activar");
    CHECK(font_get_name(FONT_LIBERATION)[0] == 0, "off: las nuevas no aparecen");
    { int seen[33] = {0}; int ok = 1; for (int o = 0; o < 33; o++) { int i = font_get_sorted_id(o); if (i < 0 || i >= 33 || seen[i]++) ok = 0; } CHECK(ok, "off: listado alfabetico incompleto"); }
    CHECK(font_set(FONT_VECTOR) == 1, "off: la vectorial de siempre sigue disponible");
    g_sysfont_off = 0;

    /* ---------------- dibujo: las caras son distintas y no se mezclan en la cache ---------------- */
    CHECK(font_vec_available(), "no carga la fuente mono");
    font_set(FONT_VECTOR);   struct shot m1 = draw_desktop("W");
    font_set(FONT_LIBERATION); struct shot lw = draw_desktop("W");
    font_set(FONT_LIBERATION_BOLD); struct shot lb = draw_desktop("W");
    font_set(FONT_DEJAVU_SANS); struct shot dj = draw_desktop("W");
    font_set(FONT_VECTOR);   struct shot m2 = draw_desktop("W");
    CHECK(m1.calls > 0 && lw.calls > 0 && lb.calls > 0 && dj.calls > 0, "alguna cara no dibuja");
    CHECK(!same(m1, lw) && !same(lw, lb) && !same(lw, dj) && !same(m1, dj), "dos caras dibujan la misma 'W'");
    CHECK(same(m1, m2), "la cache mezcla caras: la mono cambia tras dibujar las otras");
    font_set(FONT_LIBERATION); struct shot lw2 = draw_desktop("W");
    CHECK(same(lw, lw2), "Liberation no es estable entre dibujos");
    /* UTF-8 y cadenas largas con cualquier cara */
    for (int f = 0; f < FFACE_COUNT; f++) { font_set(font_face_catalog_id(f)); struct shot r = draw_desktop("Se\xc3\xb1or \xc2\xbfqu\xc3\xa9? caf\xc3\xa9 \xe2\x82\xac 0123"); CHECK(r.calls > 0, "cara %d: no dibuja UTF-8", f); }
    /* el escritorio con Liberation mantiene la rejilla de 8 px: no depende de la anchura del glifo */
    font_set(FONT_LIBERATION);
    { m_calls = 0; font_draw_string(100, 10, "iW", 0xFFFFFF, 1); CHECK(m_calls == 2, "iW: %d glifos", m_calls); }

    /* ---------------- el terminal y el editor ---------------- */
    font_set(FONT_VECTOR);
    struct shot t_mono = draw_vec(FFACE_MONO, 'A', 13);
    font_set(FONT_LIBERATION_BOLD);
    m_calls = 0; font_vec_draw(10, 10, 'A', 0xFFFFFF, 13, 8, 13);
    struct shot t_term = { m_w, m_h, m_calls, m_sum };
    CHECK(same(t_mono, t_term), "el terminal debe seguir con la mono aunque el escritorio use Liberation");
    struct shot e_lib = draw_vec(FFACE_LIBERATION, 'A', font_vec_em(FFACE_LIBERATION, 13));
    CHECK(!same(t_mono, e_lib) && e_lib.calls > 0, "el editor con Liberation dibuja como la mono");
    CHECK(font_vec_em(FFACE_MONO, 13) == 13, "em de la mono sin cambios (%d)", font_vec_em(FFACE_MONO, 13));
    /* respaldo de glifos: algun simbolo que Liberation no tiene sale de DejaVu Sans */
    { int fb = 0; for (uint32_t cp = 0x2190; cp < 0x2400 && !fb; cp++) { struct shot a = draw_vec(FFACE_LIBERATION, cp, 12), b = draw_vec(FFACE_DEJAVU, cp, 12); if (a.calls && b.calls && same(a, b)) fb = 1; }
      CHECK(fb, "ningun simbolo cae al respaldo DejaVu Sans"); }
    { struct shot a = draw_vec(FFACE_LIBERATION, 0x4E2D, 12); CHECK(1, "un glifo que ninguna cara tiene no debe fallar (%d)", a.calls); }
    { struct shot a = draw_vec(99, 'A', 12); CHECK(same(a, t_mono) || a.calls > 0, "cara fuera de rango: usa la mono"); }

    /* ---------------- familia de la interfaz ---------------- */
    const char *txt = "The quick brown fox jumps over the lazy dog";
    int n = (int)strlen(txt);
    wf_ui_set_family(WF_FAM_SANS, 100);
    int w_sans = wf_width64(txt, (uint32_t)n, 16, 0);
    CHECK(w_sans == wf_width64(txt, (uint32_t)n, 16, WF_RAW), "DejaVu Sans: la interfaz = el escritorio de siempre");
    uint32_t gen0 = wf_ui_gen();
    wf_ui_set_family(WF_FAM_LIBERATION, 100);
    CHECK(wf_ui_gen() != gen0 && wf_ui_family() == WF_FAM_LIBERATION && !wf_ui_mono(), "familia Liberation no registrada");
    int w_lib = wf_width64(txt, (uint32_t)n, 16, 0);
    CHECK(w_lib == wf_width64(txt, (uint32_t)n, 16, WF_RAW | WF_WEB), "Liberation en la interfaz = Liberation en la web (%d vs %d)", w_lib, wf_width64(txt, (uint32_t)n, 16, WF_RAW | WF_WEB));
    CHECK(w_lib == 20279, "ancho de Liberation Sans 16 px: %d (esperado 20279, tabla hmtx)", w_lib);
    CHECK(w_lib < w_sans, "Liberation (Arial) debe ser mas estrecha que DejaVu Sans (%d vs %d)", w_lib, w_sans);
    CHECK(wf_width64(txt, (uint32_t)n, 16, WF_BOLD) == 21925, "negrita real de Liberation: %d", wf_width64(txt, (uint32_t)n, 16, WF_BOLD));
    g_wf182_on = 0;                                                     /* soma.webfont=off solo afecta a la web */
    CHECK(wf_width64(txt, (uint32_t)n, 16, 0) == w_lib, "el interruptor de la web no debe cambiar la interfaz");
    CHECK(wf_width64(txt, (uint32_t)n, 16, WF_RAW | WF_WEB) == w_sans, "con la web en DejaVu, WF_WEB cae a DejaVu");
    g_wf182_on = 1;
    wf_ui_set_family(WF_FAM_MONO, 100);
    CHECK(wf_ui_mono() == 1 && wf_ui_family() == WF_FAM_MONO, "familia mono");
    CHECK(wf_width64(txt, (uint32_t)n, 16, 0) == wf_width64(txt, (uint32_t)n, 16, WF_MONO | WF_RAW), "mono: anchos de rejilla");
    wf_ui_set(0, 100); CHECK(wf_ui_family() == WF_FAM_SANS && !wf_ui_mono(), "wf_ui_set(0) vuelve a DejaVu Sans");
    wf_ui_set_family(WF_FAM_LIBERATION, 100); wf_ui_set(0, 110); CHECK(wf_ui_family() == WF_FAM_LIBERATION, "wf_ui_set(0, escala) conserva Liberation");
    wf_ui_set(1, 100); CHECK(wf_ui_family() == WF_FAM_MONO, "wf_ui_set(1) = mono");
    wf_ui_set_family(7, 100); CHECK(wf_ui_family() == WF_FAM_SANS, "familia fuera de rango -> DejaVu Sans");
    wf_ui_set_family(WF_FAM_LIBERATION, 500); CHECK(wf_ui_scale() == 130, "escala acotada");
    wf_ui_set_family(WF_FAM_LIBERATION, 100);
    /* dibujo con la familia Liberation: distinto de DejaVu Sans */
    m_calls = 0; wf_draw(10, 30, "Hello", 5, 16, 0xFFFFFF, 0, 255); struct shot ul = { m_w, m_h, m_calls, m_sum };
    wf_ui_set_family(WF_FAM_SANS, 100);
    m_calls = 0; wf_draw(10, 30, "Hello", 5, 16, 0xFFFFFF, 0, 255); struct shot us = { m_w, m_h, m_calls, m_sum };
    CHECK(ul.calls == 5 && us.calls == 5 && !same(ul, us), "la interfaz dibuja igual con Liberation y DejaVu (%d,%d)", ul.calls, us.calls);
    /* el contenido web (WF_RAW) no depende de la familia de la interfaz */
    wf_ui_set_family(WF_FAM_LIBERATION, 100);
    int web_a = wf_width64(txt, (uint32_t)n, 16, WF_RAW | WF_WEB);
    wf_ui_set_family(WF_FAM_MONO, 100);
    CHECK(web_a == wf_width64(txt, (uint32_t)n, 16, WF_RAW | WF_WEB), "la familia de la interfaz altera el contenido web");

    if (fails) { printf("test_font_184: %d fallos de %d comprobaciones\n", fails, checks); return 1; }
    printf("test_font_184: catalogo (36 fuentes), caras, terminal, respaldo y familia de la interfaz correctos (%d comprobaciones)\n", checks);
    return 0;
}
