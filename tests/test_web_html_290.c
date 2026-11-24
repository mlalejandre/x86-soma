/* SOMA_290_VOZ_CODIGO: "hazme una web..." se programa como pagina HTML (con su JavaScript dentro), no en SOMA-LANG. */
#include <stdio.h>
#include "coder_core.h"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

int main(void)
{
    static const char *const si[] = { "hazme una web para mi tienda de bicicletas", "crea una web", "quiero una web, sencilla y bonita", "Hazme Una Web de recetas",
        "genera una pagina html con un reloj", "crea una página html", "una pagina web con un contador", "crea una landing de mi app", "programa index.html", 0 };
    static const char *const no[] = { "crea un juego de snake", "configura una webcam", "haz un programa en python", "crea una calculadora en C",
        "una webapp no", "lee la web", "crea un script.js", 0 };
    for (int i = 0; si[i]; i++) chk(cc_mission_wants_html(si[i]), si[i]);
    for (int i = 0; no[i]; i++) { char m[120]; snprintf(m, sizeof m, "no es web: %s", no[i]); chk(!cc_mission_wants_html(no[i]), m); }
    printf("web html 290: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
