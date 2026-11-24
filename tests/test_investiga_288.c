/* SOMA_288_INVESTIGA: pruebas en el anfitrion de la investigacion en internet:
 *   - web_lee.c: texto legible de una pagina (sin menus, scripts ni pies), titulo, entidades, <article>, filtro de lineas;
 *     URLs utiles de unos resultados (sin redes sociales, sin repetir, como mucho dos por sitio, parentesis de Wikipedia);
 *   - agent_ruta.c: que peticiones son de investigar (y cuales no) y la consulta limpia para el buscador;
 *   - habla_texto.h: la voz no lee las citas [n] ni la lista de fuentes. */
#include <stdio.h>
#include <string.h>
#include "web_lee.h"
#include "agent_ruta.h"
#include "habla_texto.h"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }
static int tiene(const char *s, const char *p) { return strstr(s, p) != 0; }

static const char PAG1[] =
    "<!DOCTYPE html><html><head><title>Los agujeros negros &mdash; Ciencia Hoy</title>"
    "<style>body{color:red} .nav{}</style><script>var x = 'no debe salir este script en el texto';</script></head><body>"
    "<header><a href=/>Inicio</a> <a href=/c>Contacto</a> Cabecera con un texto bastante largo que no deberia aparecer nunca en el texto final</header>"
    "<nav><ul><li>Portada de la seccion de ciencia con enlaces que no interesan a nadie</li><li>Otra cosa</li></ul></nav>"
    "<div class=\"cookies\">Aceptar</div>"
    "<article><h1>Qu&eacute; es un agujero negro</h1>"
    "<p>Un agujero negro es una regi&oacute;n finita del espacio en cuyo interior existe una concentraci&oacute;n de masa lo suficientemente elevada.</p>"
    "<p>Se forman   cuando una estrella muy masiva colapsa al final de su vida y su n&uacute;cleo se comprime hasta un punto de densidad enorme.</p>"
    "<!-- comentario que tampoco debe salir en ningun caso porque es un comentario -->"
    "<p>Se forman   cuando una estrella muy masiva colapsa al final de su vida y su n&uacute;cleo se comprime hasta un punto de densidad enorme.</p>"
    "<ul><li>El horizonte de sucesos es el l&iacute;mite del que nada puede escapar, ni siquiera la luz.</li><li>Corto</li></ul>"
    "<p>Ver m&aacute;s</p>"
    "<p>La primera imagen de uno se obtuvo en 2019 con el telescopio Event Horizon &#8212; un hito de la astronom&#xED;a moderna y de la colaboraci&oacute;n.</p>"
    "<p>Relleno para que el art&iacute;culo tenga texto de verdad y se prefiera a la pagina entera: los agujeros negros supermasivos ocupan el centro de casi todas las galaxias grandes.</p>"
    "<p>Relleno dos: Sagitario A* es el agujero negro del centro de la V&iacute;a L&aacute;ctea y tiene unos cuatro millones de masas solares, segun las medidas.</p>"
    "<p>Relleno tres: la radiaci&oacute;n de Hawking es un efecto cu&aacute;ntico por el que los agujeros negros pierden masa muy lentamente con el tiempo.</p>"
    "<p>Relleno cuatro: cuando dos agujeros negros se fusionan emiten ondas gravitacionales que detectaron por primera vez los observatorios LIGO en 2015.</p>"
    "</article>"
    "<aside>Publicidad: compra ahora el mejor telescopio del mercado con un descuento enorme solo hoy</aside>"
    "<footer>Copyright 2026 Ciencia Hoy. Todos los derechos reservados. Aviso legal y politica de privacidad del sitio.</footer>"
    "</body></html>";

static void prueba_texto(void)
{
    static char out[8192]; char tit[160];
    uint32_t n = wl_texto(PAG1, (uint32_t)strlen(PAG1), out, sizeof out, tit, sizeof tit);
    chk(n > 300 && n == strlen(out), "wl_texto devuelve el texto y su longitud");
    chk(!strcmp(tit, "Los agujeros negros \342\200\224 Ciencia Hoy"), "titulo con su entidad &mdash;");
    chk(tiene(out, "## Qu\303\251 es un agujero negro"), "h1 como ## con la tilde decodificada");
    chk(tiene(out, "regi\303\263n finita del espacio"), "entidades de vocales con tilde");
    chk(tiene(out, "Se forman cuando una estrella"), "espacios seguidos se juntan");
    chk(tiene(out, "- El horizonte de sucesos"), "li como vineta");
    chk(tiene(out, "Event Horizon \342\200\224 un hito de la astronom\303\255a"), "entidades numericas decimal y hexadecimal");
    chk(!tiene(out, "script"), "sin el contenido de <script>");
    chk(!tiene(out, "color:red"), "sin el contenido de <style>");
    chk(!tiene(out, "Cabecera"), "sin <header>");
    chk(!tiene(out, "Portada de la seccion"), "sin <nav>");
    chk(!tiene(out, "Publicidad"), "sin <aside>");
    chk(!tiene(out, "Copyright"), "sin <footer>");
    chk(!tiene(out, "comentario"), "sin comentarios HTML");
    chk(!tiene(out, "Ver m"), "sin lineas cortas sueltas (Ver mas)");
    chk(!tiene(out, "Corto"), "sin vinetas cortas");
    chk(!tiene(out, "Aceptar"), "sin el aviso de cookies");
    const char *a = strstr(out, "Se forman"); chk(a && !strstr(a + 1, "Se forman"), "la linea repetida seguida sale una vez");
    chk(tiene(out, ".\n\n"), "parrafos separados por una linea en blanco");
    chk(out[n - 1] != '\n' && out[0] != '\n', "sin lineas en blanco al principio ni al final");
    /* sin <article>: la pagina entera, igual de limpia */
    static const char PAG2[] = "<html><body><div id=menu><a>uno</a><a>dos</a></div><div><p>Madrid es la capital de Espa&ntilde;a y su ciudad m&aacute;s poblada, con mas de tres millones de habitantes.</p></div>"
                               "<script>alert(1)</script><div><p>Tiene un clima mediterr&aacute;neo continentalizado con inviernos frios y veranos calurosos y secos.</p></div></body></html>";
    n = wl_texto(PAG2, (uint32_t)strlen(PAG2), out, sizeof out, tit, sizeof tit);
    chk(tit[0] == 0, "sin <title>, titulo vacio");
    chk(tiene(out, "capital de Espa\303\261a") && tiene(out, "mediterr\303\241neo") && !tiene(out, "alert"), "pagina sin article: texto limpio");
    /* tope de salida: no se pasa nunca */
    static char peq[100];
    n = wl_texto(PAG1, (uint32_t)strlen(PAG1), peq, sizeof peq, 0, 0);
    chk(n < sizeof peq && strlen(peq) == n, "respeta el tope de la salida");
    /* HTML roto o vacio */
    chk(wl_texto("", 0, out, sizeof out, tit, sizeof tit) == 0 && out[0] == 0, "vacio");
    static const char ROTO[] = "<p>Un parrafo sin cerrar con texto suficiente para que se considere util en el filtro de lineas <b>y una etiqueta <i";
    n = wl_texto(ROTO, (uint32_t)strlen(ROTO), out, sizeof out, tit, sizeof tit);
    chk(tiene(out, "Un parrafo sin cerrar"), "HTML cortado a medias");
    static const char SCR[] = "<p>Texto antes de un script sin cerrar y que ademas es suficientemente largo para salir.</p><script>var a = 1; var b = 2;";
    n = wl_texto(SCR, (uint32_t)strlen(SCR), out, sizeof out, tit, sizeof tit);
    chk(tiene(out, "Texto antes") && !tiene(out, "var a"), "script sin cerrar: se descarta hasta el final");
}

static void prueba_urls(void)
{
    static const char RES[] =
        "- Agujero negro: Un agujero negro es... (https://es.wikipedia.org/wiki/Agujero_negro)\n"
        "- Mercurio (planeta): el planeta mas cercano al Sol (https://es.wikipedia.org/wiki/Mercurio_(planeta))\n"
        "- Agujero negro: repetido (https://es.wikipedia.org/wiki/Agujero_negro)\n"
        "- Tercero de wikipedia (https://es.wikipedia.org/wiki/Horizonte_de_sucesos)\n"
        "- Video: (https://www.youtube.com/watch?v=abc)\n"
        "- Red social (https://twitter.com/nasa/status/1)\n"
        "- Un pdf (https://www.esa.int/informe.pdf)\n"
        "- NASA: Black holes (https://science.nasa.gov/universe/black-holes/)\n"
        "- National Geographic (https://www.nationalgeographic.com.es/ciencia/agujeros-negros_1234)\n"
        "- Sin cierre (https://ejemplo.com/a\n"
        "- texto suelto https://suelto.com/x sin parentesis\n";
    char u[8][300];
    int n = wl_urls(RES, u, 8);
    chk(n == 4, "4 URLs utiles");
    chk(n > 0 && !strcmp(u[0], "https://es.wikipedia.org/wiki/Agujero_negro"), "primera: Wikipedia");
    chk(n > 1 && !strcmp(u[1], "https://es.wikipedia.org/wiki/Mercurio_(planeta)"), "URL con parentesis dentro");
    chk(n > 2 && !strcmp(u[2], "https://science.nasa.gov/universe/black-holes/"), "la tercera de Wikipedia no entra (como mucho 2 por sitio)");
    chk(n > 3 && !strcmp(u[3], "https://www.nationalgeographic.com.es/ciencia/agujeros-negros_1234"), "national geographic");
    chk(wl_urls(RES, u, 2) == 2, "respeta el maximo");
    chk(!wl_url_util("https://m.youtube.com/watch?v=1") && !wl_url_util("https://www.facebook.com/x") && !wl_url_util("https://duckduckgo.com/l/?u=1"), "redes, videos y el propio buscador fuera");
    chk(wl_url_util("https://notyoutube.com/a") && wl_url_util("http://ejemplo.es/"), "un dominio que solo acaba igual no es el malo");
    chk(!wl_url_util("ftp://x.com/a") && !wl_url_util("javascript:alert(1)") && !wl_url_util(0), "solo http y https");
    chk(!wl_url_util("https://x.com/cosa.ZIP"), "archivos fuera (sin distinguir mayusculas)");
    char d[64];
    wl_dominio("https://WWW.Ejemplo.COM:8080/a/b?c", d, sizeof d); chk(!strcmp(d, "ejemplo.com"), "dominio sin www, puerto ni ruta");
    wl_dominio("es.wikipedia.org/wiki/X", d, sizeof d); chk(!strcmp(d, "es.wikipedia.org"), "dominio sin esquema");
}

static void prueba_ruta(void)
{
    static const char *const prof[] = { "investiga la historia de la imprenta", "Investiga sobre los agujeros negros", "busca información sobre la fotosíntesis",
        "búscame información de Marie Curie", "averigua cuánto mide la torre Eiffel", "últimas noticias de la economía española", "infórmate sobre el cambio climático",
        "haz una investigación sobre energías renovables", "qué se sabe de la misión Artemis", "noticias sobre inteligencia artificial", 0 };
    for (int i = 0; prof[i]; i++) chk(ar_investiga(prof[i]) == 2, prof[i]);
    static const char *const rap[] = { "busca en internet recetas de pasta", "búscame en la web el horario del museo del Prado", "mira en internet qué es el bitcoin",
        "¿puedes buscar en internet el sistema solar?", "busca online vuelos baratos a Roma", "busca en google la población de Japón", 0 };
    for (int i = 0; rap[i]; i++) chk(ar_investiga(rap[i]) == 1, rap[i]);
    static const char *const no[] = { "busca el archivo notas.txt", "investiga el archivo /workspace/a.txt", "busca en youtube vídeos de gatos", "busca un vídeo de cocina en internet",
        "averigua la ip de google.com", "abre internet", "qué tiempo hace en Madrid", "hola qué tal", "explícame la relatividad", "busca la carpeta fotos",
        "investiga por qué falla este código", "crea un programa que busque en internet", "busca canciones de rock en internet", "averigua cuánta ram queda",
        "cuéntame un chiste", "lista los archivos de /workspace", "pon música", 0 };
    for (int i = 0; no[i]; i++) { char m[200]; snprintf(m, sizeof m, "no es investigar: %s", no[i]); chk(ar_investiga(no[i]) == 0, m); }
    chk(ar_investiga("") == 0 && ar_investiga(0) == 0, "vacio");
    /* la consulta limpia */
    static const char *const c[][2] = {
        { "búscame en internet recetas de pasta", "recetas de pasta" },
        { "¿puedes buscar en internet el sistema solar?", "sistema solar" },
        { "búscame información sobre agujeros negros en internet", "agujeros negros" },
        { "investiga el año en que nació Cervantes", "año en que nació Cervantes" },
        { "busca información de la fotosíntesis", "fotosíntesis" },
        { "últimas noticias de España", "últimas noticias de España" },
        { "averigua cuánto mide la torre Eiffel", "cuánto mide la torre Eiffel" },
        { "Investiga, por favor, sobre la Guerra Civil española.", "Guerra Civil española" },
        { "busca en internet", "busca en internet" },
        { 0, 0 } };
    for (int i = 0; c[i][0]; i++) {
        char o[200], m[300]; ar_consulta(c[i][0], o, sizeof o);
        snprintf(m, sizeof m, "consulta de '%s': '%s' (se esperaba '%s')", c[i][0], o, c[i][1]);
        chk(!strcmp(o, c[i][1]), m);
    }
    char o4[8]; ar_consulta("investiga la historia de la imprenta", o4, sizeof o4); chk(strlen(o4) < sizeof o4, "consulta: respeta el tope");
    /* ar_direct_ok no cambia: investigar sigue necesitando herramientas */
    chk(!ar_direct_ok("investiga la historia de la imprenta") && ar_direct_ok("cuéntame un chiste"), "ar_direct_ok igual que antes");
}

static void prueba_voz(void)
{
    char o[2048];
    habla_limpia("La imprenta la invento Gutenberg hacia 1450 [1]. Se extendio por Europa [2, 3] en pocas decadas [1-3].\n\n## Fuentes\n1. Wikipedia - https://es.wikipedia.org/wiki/Imprenta\n2. Otra", o, sizeof o, 1500);
    chk(!tiene(o, "[") && !tiene(o, "Fuentes") && !tiene(o, "Wikipedia"), "la voz no lee las citas ni las fuentes");
    chk(tiene(o, "hacia 1450.") && tiene(o, "Europa en pocas decadas."), "el texto se lee entero y sin huecos raros");
    habla_limpia("Mira [este enlace](https://x.com) y la lista [1] y [a].", o, sizeof o, 1500);
    chk(tiene(o, "[a]") && !tiene(o, "[1]"), "solo se quitan citas numericas");
    habla_limpia("Fuentes de energia: la solar y la eolica son renovables.", o, sizeof o, 1500);
    chk(tiene(o, "Fuentes de energia"), "una frase que empieza por 'Fuentes' se lee");
    habla_limpia("Respuesta.\nFuentes:\n- una\n- dos", o, sizeof o, 1500);
    chk(!strcmp(o, "Respuesta."), "'Fuentes:' sola en su linea corta la lectura");
}

int main(void)
{
    prueba_texto();
    prueba_urls();
    prueba_ruta();
    prueba_voz();
    printf("investiga 288: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
