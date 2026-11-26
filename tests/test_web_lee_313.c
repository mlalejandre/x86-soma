/* SOMA_313_LECTOR_WEB: wl_texto (src/agent/web_lee.c) perdia parrafos enteros. Al compactar las lineas utiles en el mismo bufer,
 * cada una se separa con "\n\n" pero en el original solo habia "\n": con dos lineas utiles seguidas, la escritura adelantaba a la
 * lectura y la segunda linea se convertia en una ristra de saltos de linea. Aqui: paginas con muchas lineas utiles seguidas, y
 * el resultado se compara con una version de referencia escrita aparte (lineas -> filtro -> union con "\n\n"). */
#include <stdio.h>
#include <string.h>
#include "soma_test.h"
#include "web_lee.h"

static int contiene(const char *s, const char *p) { return strstr(s, p) != 0; }

int main(void)
{
    static const char PAG[] =
        "<html><head><title>La imprenta</title></head><body><nav><a href=/>Inicio</a></nav><article><h1>La imprenta</h1>"
        "<p>Johannes Gutenberg desarroll&oacute; hacia 1450 la imprenta de tipos m&oacute;viles en Maguncia, lo que abarat&oacute; los libros.</p>"
        "<ul><li>Primer libro impreso: la Biblia de 42 l&iacute;neas, en dos vol&uacute;menes.</li><li>Tipos de una aleaci&oacute;n de plomo, esta\303\261o y antimonio.</li></ul>"
        "<p>Su invento se extendi&oacute; en pocas d&eacute;cadas a m&aacute;s de doscientas ciudades &amp; cambi&oacute; la ciencia europea.</p></article>"
        "<footer>Aviso legal</footer></body></html>";
    static char out[8192]; char tit[100];
    uint32_t n = wl_texto(PAG, (uint32_t)strlen(PAG), out, sizeof out, tit, sizeof tit);
    CHECK(n == strlen(out));
    CHECK(contiene(out, "## La imprenta"));
    CHECK(contiene(out, "Maguncia"));                              /* el primer parrafo: antes se perdia */
    CHECK(contiene(out, "- Primer libro impreso: la Biblia de 42 l\303\255neas"));
    CHECK(contiene(out, "- Tipos de una aleaci\303\263n de plomo"));
    CHECK(contiene(out, "doscientas ciudades & cambi\303\263 la ciencia"));
    CHECK(!contiene(out, "\n\n\n"));                               /* nunca tres saltos seguidos (antes, cientos) */
    CHECK(!contiene(out, "Aviso legal") && !contiene(out, "Inicio"));
    CHECK_STR(tit, "La imprenta");
    /* forma exacta: cinco lineas utiles separadas por una linea en blanco */
    int lineas = 1; for (const char *p = out; *p; p++) if (p[0] == '\n' && p[1] == '\n') lineas++;
    CHECK_EQ_INT(lineas, 5);

    /* muchas lineas utiles seguidas, con un bufer justo: nunca se pasa ni se rompe */
    static char big[60000]; uint32_t q = 0;
    for (int i = 0; i < 200; ++i) q += (uint32_t)snprintf(big + q, sizeof big - q, "<p>Parrafo numero %03d con texto suficiente para ser util en el filtro de lineas del lector.</p>", i);
    for (uint32_t cap = 50; cap < 9000; cap += 337) {
        static char o2[9000];
        memset(o2, 'Z', sizeof o2);
        uint32_t k = wl_texto(big, q, o2, cap, 0, 0);
        int ok = k < cap && o2[k] == 0 && strlen(o2) == k && o2[cap] == 'Z' && !contiene(o2, "\n\n\n");
        for (uint32_t i = 0; ok && i < k; ++i) if (o2[i] == '\n' && i > 0 && o2[i - 1] != '\n' && o2[i + 1] != '\n') ok = 0;   /* separador doble */
        int num = 0; for (const char *p = o2; (p = strstr(p, "Parrafo numero ")) != 0; p++) { int v = -1; sscanf(p, "Parrafo numero %d", &v); if (v != num) ok = 0; num++; }
        if (!ok) { printf("cap=%u k=%u\n", cap, k); CHECK(0 && "parrafos seguidos con bufer justo"); break; }
        CHECK(ok);
    }
    TEST_MAIN_END("web_lee_313");
}
