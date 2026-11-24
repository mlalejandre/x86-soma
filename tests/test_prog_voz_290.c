/* SOMA_290_VOZ_CODIGO: pruebas en el anfitrion de lo que dice SOMA mientras programa y al terminar (src/agent/prog_voz.c). */
#include <stdio.h>
#include <string.h>
#include "prog_voz.h"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }
static int tiene(const char *s, const char *p) { return strstr(s, p) != 0; }

int main(void)
{
    char o[600];
    /* fases */
    chk(!pv_narra(1, "Fase 1/6: Analizando la tarea...", "", 0, o, sizeof o) && !o[0], "analizar: no se dice (ya lo dijo el aviso)");
    chk(!pv_narra(2, "Fase 2/6: Generando el programa...", "", 0, o, sizeof o), "generar: no se dice");
    chk(pv_narra(4, "Fase 4/6: Reparando (llamada 2/7)...", "Linea 12: se esperaba ')'", 0, o, sizeof o) && tiene(o, "l\303\255nea 12") && tiene(o, "corrigiendo"), "primer fallo con su linea");
    chk(pv_narra(4, "Fase 4/6: Reparando (llamada 2/7)...", "La tarea usa las FLECHAS: ...", 0, o, sizeof o) && tiene(o, "le falta algo") && !tiene(o, "FLECHAS"), "fallo sin linea: frase general, sin el texto tecnico");
    chk(pv_narra(4, "Fase 4/6: Reparando (llamada 3/7)...", "Linea 40: x", 1, o, sizeof o) && tiene(o, "Sigo corrigiendo") && tiene(o, "40"), "segunda reparacion");
    chk(!pv_narra(4, "Fase 4/6: Reparando (llamada 4/7)...", "Linea 41: x", 2, o, sizeof o), "tercera: callado (no repetir)");
    chk(pv_narra(4, "Fase 4/6: Reparando (llamada 5/7)...", "", 3, o, sizeof o) && tiene(o, "Todav\303\255a"), "cuarta: sigue");
    chk(!pv_narra(4, "Fase 4/6: Reparando (llamada 6/7)...", "", 4, o, sizeof o), "quinta: callado");
    chk(pv_narra(4, "Fase 4/6: Regenerando con un enfoque mas simple (llamada 4/7)...", "Linea 3: x", 5, o, sizeof o) && tiene(o, "m\303\241s sencilla"), "regenerar");
    chk(pv_narra(5, "Fase 5/6: Revision critica del resultado...", "", 0, o, sizeof o) && tiene(o, "Ya funciona"), "revision");
    { char p[20]; chk(pv_narra(4, "x", "Linea 123456: y", 0, p, sizeof p) && strlen(p) < sizeof p, "respeta el tope"); }
    /* nombres y lenguajes */
    char n[64];
    pv_nombre("/workspace/juegos/snake_clasico.py", n, sizeof n); chk(!strcmp(n, "snake clasico"), "nombre sin ruta, extension ni guiones bajos");
    pv_nombre("/workspace/mi-web.v2.html", n, sizeof n); chk(!strcmp(n, "mi web.v2"), "solo la ultima extension");
    pv_nombre("/workspace/.oculto", n, sizeof n); chk(!strcmp(n, ".oculto"), "archivo que empieza por punto");
    pv_nombre("reloj", n, sizeof n); chk(!strcmp(n, "reloj"), "sin ruta ni extension");
    chk(!strcmp(pv_lenguaje("/a/b.py"), "Python") && !strcmp(pv_lenguaje("/a/b.c"), "C") && !strcmp(pv_lenguaje("/a/b.js"), "JavaScript")
        && !strcmp(pv_lenguaje("/a/b.HTML"), "HTML") && !strcmp(pv_lenguaje("/a/b.soma"), "SOMA-LANG") && !strcmp(pv_lenguaje("/a/b.json"), "SOMA-LANG"), "lenguajes");
    /* limpieza de la explicacion */
    char e[700];
    strcpy(e, "<think>pienso\nmucho</think>\n**El juego** dibuja una _serpiente_ que mueves con las flechas.\n\n- Cada manzana suma un punto.\n```python\nprint(1)\n```\nSi chocas, se acaba. Esta frase sobra.");
    pv_limpia(e, 420);
    chk(!tiene(e, "think") && !tiene(e, "pienso") && !tiene(e, "**") && !tiene(e, "print") && !tiene(e, "`"), "sin razonamiento, Markdown ni codigo");
    chk(tiene(e, "El juego dibuja una serpiente que mueves con las flechas.") && tiene(e, "Cada manzana suma un punto."), "el texto queda limpio y seguido");
    chk(tiene(e, "Si chocas, se acaba.") && !tiene(e, "sobra"), "como mucho tres frases");
    chk(!tiene(e, "  ") && e[0] != ' ' && e[strlen(e) - 1] != ' ', "sin espacios dobles ni en los bordes");
    strcpy(e, "Una frase larguisima sin puntos que no se acaba nunca y que sigue y sigue con palabras y mas palabras para pasarse del tope");
    pv_limpia(e, 50); chk(strlen(e) <= 51 && e[strlen(e) - 1] == '.', "recortada en una palabra, con punto final");
    strcpy(e, "Mueve con a\303\261adidos y m\303\241s cosas aqu\303\255 para ver que no se parte una letra"); pv_limpia(e, 20);
    { int bien = 1; size_t L = strlen(e); for (size_t i = 0; i < L; i++) { unsigned char c = (unsigned char)e[i]; if (c >= 0xC0 && (i + 1 >= L || ((unsigned char)e[i + 1] & 0xC0) != 0x80)) bien = 0; } chk(bien, "sin letras UTF-8 partidas"); }
    strcpy(e, ""); pv_limpia(e, 100); chk(e[0] == 0, "vacio");
    /* resumen final */
    pv_final(1, 0, "/workspace/snake.py", "El juego dibuja una serpiente.", "", o, sizeof o);
    chk(tiene(o, "he creado \302\253snake\302\273 en Python y funciona.") && tiene(o, "El juego dibuja una serpiente.") && tiene(o, "SOMA Studio"), "programa nuevo");
    pv_final(1, 0, "/workspace/tienda.html", "", "", o, sizeof o);
    chk(tiene(o, "p\303\241gina web \302\253tienda\302\273") && tiene(o, "JavaScript en un solo archivo") && !tiene(o, "  "), "pagina web, sin explicacion");
    pv_final(1, 1, "/workspace/reloj.c", "", "", o, sizeof o);
    chk(tiene(o, "he hecho los cambios en \302\253reloj\302\273"), "cambios");
    pv_final(0, 0, "/workspace/x.py", "", "Linea 7: nombre no definido", o, sizeof o);
    chk(tiene(o, "No he conseguido") && tiene(o, "l\303\255nea 7") && tiene(o, "queda como estaba"), "fallo con su linea");
    pv_final(0, 1, "/workspace/x.py", "", "", o, sizeof o);
    chk(tiene(o, "sin romper el programa") && !tiene(o, "l\303\255nea"), "fallo de un cambio, sin linea");
    { char p[30]; pv_final(1, 0, "/workspace/snake.py", "Explicacion larga.", "", p, sizeof p); chk(strlen(p) < sizeof p, "final: respeta el tope"); }
    printf("prog_voz 290: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
