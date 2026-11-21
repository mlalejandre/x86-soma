/* SOMA_251_RUTAS: lectura de las respuestas de busqueda (Wikipedia y DuckDuckGo ligero) */
#include <stdio.h>
#include <string.h>
#include "busca_web.h"
static int f, n;
#define CK(c, m) do { n++; if (!(c)) { f++; printf("FALLO: %s\n", m); } } while (0)
int main(void)
{
    char out[4096], e[256];
    bw_urlenc("Primera Guerra Mundial ñ", e, sizeof e);
    CK(!strcmp(e, "Primera+Guerra+Mundial+%C3%B1"), "codificar consulta");
    const char *wj = "{\"batchcomplete\":\"\",\"query\":{\"searchinfo\":{\"totalhits\":5},\"search\":[{\"ns\":0,\"title\":\"Primera Guerra Mundial\",\"pageid\":1,\"size\":1,"
                     "\"snippet\":\"La <span class=\\\"searchmatch\\\">Primera</span> Guerra Mundial fue un conflicto b\\u00e9lico &quot;global&quot;\",\"timestamp\":\"2026\"},"
                     "{\"ns\":0,\"title\":\"Batalla del Marne\",\"pageid\":2,\"snippet\":\"Batalla de 1914\"}]}}";
    int k = bw_parse_wikipedia(wj, "es", out, sizeof out, 5);
    CK(k == 2, "dos resultados de Wikipedia");
    CK(strstr(out, "- Primera Guerra Mundial: La Primera Guerra Mundial fue un conflicto bélico \"global\". (https://es.wikipedia.org/wiki/Primera_Guerra_Mundial)") != 0, "titulo, resumen sin etiquetas y enlace");
    CK(strstr(out, "- Batalla del Marne: Batalla de 1914.") != 0, "segundo resultado");
    const char *dh = "<table><tr><td>1.&nbsp;</td><td><a rel=\"nofollow\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fes.wikipedia.org%2Fwiki%2FGuerra&amp;rut=x\" class='result-link'>Primera <b>Guerra</b> Mundial - Wikipedia</a></td></tr>"
                     "<tr><td>&nbsp;</td><td class='result-snippet'>Conflicto militar de 1914 a 1918 &amp; más.</td></tr>"
                     "<tr><td>2.</td><td><a rel=\"nofollow\" href=\"https://historia.example/ww1\" class='result-link'>Historia</a></td></tr></table>";
    k = bw_parse_ddg_lite(dh, out, sizeof out, 5);
    CK(k == 2, "dos resultados de DuckDuckGo");
    CK(strstr(out, "- Primera Guerra Mundial - Wikipedia: Conflicto militar de 1914 a 1918 & más. (https://es.wikipedia.org/wiki/Guerra)") != 0, "enlace real y resumen");
    CK(strstr(out, "- Historia (https://historia.example/ww1)") != 0, "enlace directo sin resumen");
    CK(bw_parse_wikipedia("{\"error\":1}", "es", out, sizeof out, 5) == 0 && bw_parse_ddg_lite("<html>sin nada</html>", out, sizeof out, 5) == 0, "sin resultados");
    printf("busqueda 251: %d/%d correctas\n", n - f, n);
    return f ? 1 : 0;
}
