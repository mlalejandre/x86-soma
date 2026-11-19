/* SOMA 182: caras de la web (Liberation Sans regular/negrita con respaldo DejaVu). Los anchos esperados salen de las tablas hmtx de
 * Liberation Sans (mismos anchos que Arial): suma de round(avance*px*64/2048) por glifo. Se enlaza con tests/layhost/fbhost.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_font.h"
#include "web_css.h"

extern uint32_t *g_fb; extern int g_fbw, g_fbh;
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const struct { const char *t; int px, bold, w64; } K[] = {
    {"The quick brown fox jumps over the lazy dog", 16, 0, 20279},
    {"The quick brown fox jumps over the lazy dog", 16, 1, 21925},
    {"Iniciar sesi\xc3\xb3n, pol\xc3\xadtica de privacidad", 12, 0, 12122},
    {"ACTUALIDAD DEPORTES ECONOM\xc3\x8d" "A", 14, 1, 16228},
    {"Wireless Headphones & Speakers 2026", 24, 0, 27235},
    {"\xc3\xb1\xc3\x91\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba \xc2\xbf\xc2\xa1\xe2\x82\xac", 20, 0, 7116},
};

int main(void)
{
    g_wf182_on = 1;
    CHECK(wf_init(), "no hay fuente");
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        int w = wf_width64(K[i].t, (uint32_t)strlen(K[i].t), K[i].px, WF_WEB | WF_RAW | (K[i].bold ? WF_BOLD : 0));
        CHECK(w == K[i].w64, "caso %u: ancho %d, esperado %d", i, w, K[i].w64);
    }
    /* la negrita es la cara real (no el trazo doble sintetico) y el escritorio sigue con DejaVu */
    int base = wf_width64("Hello", 5, 16, WF_RAW);
    int web = wf_width64("Hello", 5, 16, WF_RAW | WF_WEB);
    CHECK(web != base, "la web y el escritorio usan la misma fuente (%d)", web);
    CHECK(wf_width64("Hello", 5, 16, WF_RAW) == base, "el escritorio cambio tras usar la web");
    g_wf182_on = 0;
    CHECK(wf_width64("Hello", 5, 16, WF_RAW | WF_WEB) == base, "soma.webfont=off no vuelve a DejaVu");
    g_wf182_on = 1;
    /* glifos que Liberation no trae salen de DejaVu */
    int old = wf_cp_adv64(0x2196, 16, WF_RAW), nw = wf_cp_adv64(0x2196, 16, WF_RAW | WF_WEB);
    CHECK(old > 0 && nw == old, "respaldo DejaVu: %d vs %d", nw, old);
    /* la cache de glifos distingue las caras */
    g_fbw = 64; g_fbh = 24; g_fb = calloc(64 * 24, 4);
    wf_draw(2, 18, "Hl", 2, 16, 0xFFFFFF, WF_RAW, 255);
    uint32_t sum0 = 0; for (int i = 0; i < 64 * 24; i++) sum0 += g_fb[i] & 255;
    memset(g_fb, 0, 64 * 24 * 4);
    wf_draw(2, 18, "Hl", 2, 16, 0xFFFFFF, WF_RAW | WF_WEB, 255);
    uint32_t sum1 = 0; for (int i = 0; i < 64 * 24; i++) sum1 += g_fb[i] & 255;
    memset(g_fb, 0, 64 * 24 * 4);
    wf_draw(2, 18, "Hl", 2, 16, 0xFFFFFF, WF_RAW, 255);
    uint32_t sum2 = 0; for (int i = 0; i < 64 * 24; i++) sum2 += g_fb[i] & 255;
    CHECK(sum0 > 0 && sum1 > 0 && sum0 != sum1 && sum0 == sum2, "cache de caras: %u %u %u", sum0, sum1, sum2);
    CHECK(wf_web_ascent(16) == 15 && wf_web_descent(16) == 4, "metricas %d/%d", wf_web_ascent(16), wf_web_descent(16));
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    if (fails) return 1;
    printf("SOMA 182: fuentes web OK\n");
    return 0;
}
