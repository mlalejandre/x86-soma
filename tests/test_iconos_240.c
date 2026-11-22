/* SOMA_240_ICONOS: prueba en el anfitrion de los iconos vectoriales del sistema (src/ui/sysicon.c).
 * Dibuja cada icono en un framebuffer de memoria y comprueba que:
 *  - pinta algo (no hay casos vacios en el switch: un icono nuevo sin dibujo saldria en blanco en Media o Studio);
 *  - no se sale de su caja (tamano 48 centrado: todo dentro de 48 +- el grosor del trazo);
 *  - no hay dos iconos iguales (un copiar y pegar sin cambiar coordenadas);
 *  - se ve tambien en pequeno (14 px, el tamano de los botones de SOMA Studio).
 * Las primitivas del framebuffer (linea AA y rectangulo redondeado) se sustituyen por versiones sencillas. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "sysicon.h"

#define W 128
#define H 128
static uint32_t fbm[W * H];
static int clip_on;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FALLO: "); printf(__VA_ARGS__); printf(" (linea %d)\n", __LINE__); fails++; } } while (0)

/* ---- framebuffer de prueba ---- */
volatile uint32_t *fb_get_buffer(void) { return fbm; }
int fb_is_active(void) { return 1; }
uint32_t fb_get_width(void) { return W; }
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1) { *x0 = 0; *y0 = 0; *x1 = W; *y1 = H; (void)clip_on; }
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
static void put(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < W && y < H) fbm[y * W + x] = c & 0x00FFFFFFu; }
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t argb)
{   /* muestreo del segmento con un disco del grosor pedido (unidades de 1/16 px) */
    int32_t dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    n = n / 8 + 1;
    int r = width16 / 32; if (r < 1) r = 1;
    for (int i = 0; i <= n; i++) {
        int cx = (x0 + dx * i / n) / 16, cy = (y0 + dy * i / n) / 16;
        for (int yy = -r; yy <= r; yy++) for (int xx = -r; xx <= r; xx++) if (xx * xx + yy * yy <= r * r) put(cx + xx, cy + yy, argb);
    }
}
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{ (void)r; for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) put(xx, yy, argb); }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{ (void)r; for (int xx = x; xx < x + w; xx++) { put(xx, y, argb); put(xx, y + h - 1, argb); } for (int yy = y; yy < y + h; yy++) { put(x, yy, argb); put(x + w - 1, yy, argb); } }
/* texto: no se usa en la prueba */
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, int scale) { (void)x; (void)y; (void)s; (void)fg; (void)scale; }
int wf_init(void) { return 0; }
int wf_width(const char *s, uint32_t n, int px, int fl) { (void)s; (void)px; (void)fl; return (int)n * 8; }
int wf_ui_px(int px) { return px; }
void wf_draw(int32_t x, int32_t b, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int a) { (void)x; (void)b; (void)s; (void)n; (void)px; (void)rgb; (void)fl; (void)a; }

static uint64_t draw(int icon, int size, int *lit, int *x0, int *y0, int *x1, int *y1)
{
    memset(fbm, 0, sizeof fbm);
    si_icon(icon, W / 2, H / 2, size, 0x00FFFFFFu);
    uint64_t h = 1469598103934665603ull; *lit = 0; *x0 = W; *y0 = H; *x1 = -1; *y1 = -1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint32_t v = fbm[y * W + x];
        if ((v & 0xFF) > 96) {
            (*lit)++; if (x < *x0) *x0 = x; if (y < *y0) *y0 = y; if (x > *x1) *x1 = x; if (y > *y1) *y1 = y;
            h = (h ^ (uint64_t)(y * W + x)) * 1099511628211ull;
        }
    }
    return h;
}

int main(void)
{
    static uint64_t hs[SI_COUNT];
    int lit, a, b, c, d;
    CHECK(SI_COUNT - 1 >= 55, "deberia haber al menos 55 iconos (hay %d)", SI_COUNT - 1);
    for (int i = 1; i < SI_COUNT; i++) {
        hs[i] = draw(i, 48, &lit, &a, &b, &c, &d);
        CHECK(lit >= 30, "icono %d a 48 px casi vacio (%d px)", i, lit);
        CHECK(a >= W / 2 - 28 && b >= H / 2 - 28 && c <= W / 2 + 28 && d <= H / 2 + 28, "icono %d se sale de su caja: [%d,%d]-[%d,%d]", i, a, b, c, d);
        CHECK(c - a >= 10 || d - b >= 10, "icono %d demasiado pequeno (%dx%d)", i, c - a, d - b);
        for (int j = 1; j < i; j++) CHECK(hs[j] != hs[i], "iconos %d y %d son identicos", j, i);
        draw(i, 14, &lit, &a, &b, &c, &d);
        CHECK(lit >= 4, "icono %d a 14 px no se ve (%d px)", i, lit);
        CHECK(a >= W / 2 - 10 && c <= W / 2 + 10 && b >= H / 2 - 10 && d <= H / 2 + 10, "icono %d a 14 px fuera de su caja", i);
    }
    /* los nuevos del parche 240: reproductor y editor */
    static const int nuevos[] = { SI_MUSIC, SI_FILM, SI_REWIND, SI_FFWD, SI_SKIP_PREV, SI_SKIP_NEXT, SI_LIST, SI_CODE, SI_UNDO, SI_REDO,
        SI_SEARCH, SI_REPLACE, SI_GOTO, SI_COMMENT, SI_FILE, SI_FILE_CODE, SI_DISK, SI_EDIT, SI_GRID, SI_COPY, SI_CLOSE, SI_SPEAKER_OFF,
        SI_LOOP, SI_TERMINAL, SI_SPARK, SI_SEND, SI_CUT, SI_PASTE };
    CHECK(sizeof nuevos / sizeof nuevos[0] == 28, "24 iconos nuevos de SOMA_240, 2 de SOMA_254 y 2 de SOMA_255");
    CHECK(nuevos[27] == SI_COUNT - 1 && nuevos[23] == SI_TERMINAL, "SI_PASTE es el ultimo");
    /* simetria esperada: avanzar y retroceder son espejo uno del otro (mismo numero de pixeles) */
    int l1, l2;
    draw(SI_REWIND, 48, &l1, &a, &b, &c, &d); draw(SI_FFWD, 48, &l2, &a, &b, &c, &d);
    CHECK(l1 - l2 < 40 && l2 - l1 < 40, "retroceder (%d) y avanzar (%d) deberian pesar lo mismo", l1, l2);
    draw(SI_UNDO, 48, &l1, &a, &b, &c, &d); draw(SI_REDO, 48, &l2, &a, &b, &c, &d);
    CHECK(l1 - l2 < 40 && l2 - l1 < 40, "deshacer (%d) y rehacer (%d) deberian pesar lo mismo", l1, l2);
    printf("[iconos del sistema 240] %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
