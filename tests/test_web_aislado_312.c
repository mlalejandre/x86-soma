/* SOMA_312_WEB_AISLADA: el nucleo no se cree el texto que devuelve el lector web aislado (src/agent/web_aislado.h): nunca pasa de
 * cap - 1, siempre queda terminado y se corta en el primer NUL; el numero de resultados queda entre 0 y max. Y en el anfitrion
 * (WEB_HOST_TEST) los envoltorios llaman directo y dan lo mismo. La comparacion con el servicio de verdad: sbx_selftest (QEMU). */
#include <stdio.h>
#include <string.h>
#include "soma_test.h"
#include "web_aislado.h"
#include "web_lee.h"
#include "busca_web.h"

static unsigned semilla = 312u;
static unsigned azar(void) { semilla = semilla * 1103515245u + 12345u; return semilla >> 8; }

int main(void)
{
    static char b[5000];
    for (int it = 0; it < 20000; ++it) {
        uint32_t cap = 1 + azar() % 4000, n = azar() % 6000;
        for (uint32_t i = 0; i < sizeof b; ++i) b[i] = (char)(azar() % 7 == 0 ? 0 : 'a' + azar() % 26);
        uint32_t k = web_aislado_sanea(b, n, cap);
        if (!(k <= cap - 1 && b[k] == 0 && strlen(b) == k)) { CHECK(0 && "texto saneado fuera de rango"); break; }
    }
    CHECK_EQ_INT(web_aislado_sanea(b, 10, 0), 0);
    CHECK_EQ_INT(web_aislado_cuenta(-5, 3), 0);
    CHECK_EQ_INT(web_aislado_cuenta(99, 3), 3);
    CHECK_EQ_INT(web_aislado_cuenta(2, 3), 2);

    static const char pag[] = "<html><head><title>T&iacute;tulo</title></head><body><nav>menu</nav><p>Un parrafo con texto suficiente "
                              "para que el filtro de lineas lo considere util y lo deje pasar sin problemas.</p></body></html>";
    static char o1[4096], o2[4096]; char t1[80], t2[80];
    uint32_t a = wl_texto(pag, (uint32_t)strlen(pag), o1, sizeof o1, t1, sizeof t1);
    uint32_t c = wl_texto_aislado(pag, (uint32_t)strlen(pag), o2, sizeof o2, t2, sizeof t2);
    CHECK(a == c && !strcmp(o1, o2) && !strcmp(t1, t2));
    static const char ddg[] = "<table><tr><td><a rel=\"nofollow\" href=\"https://x.example/a\" class='result-link'>Uno</a></td></tr></table>";
    CHECK_EQ_INT(bw_ddg_aislado(ddg, o2, sizeof o2, 5), bw_parse_ddg_lite(ddg, o1, sizeof o1, 5));
    CHECK(!strcmp(o1, o2));
    TEST_MAIN_END("web_aislado_312");
}
