/* SOMA_336_INVESTIGA_TEMAS: tema de la pregunta, actualidad, fuentes por tema y razonamiento fuera (src/agent/inv_tema.c). */
#include <stdio.h>
#include <string.h>
#include "inv_tema.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
    /* temas */
    CK(it_tema("\302\277Qui\303\251n gan\303\263 el partido del Real Madrid ayer?") == IT_DEPORTES);
    CK(it_tema("clasificaci\303\263n de LaLiga esta jornada") == IT_DEPORTES);
    CK(it_tema("resultados de Alcaraz en Roland Garros") == IT_DEPORTES);
    CK(it_tema("\302\277Qu\303\251 s\303\255ntomas tiene la gripe y cu\303\241ndo es grave?") == IT_SALUD);
    CK(it_tema("dosis de ibuprofeno para la fiebre en adultos") == IT_SALUD);
    CK(it_tema("\302\277C\303\263mo se forma un agujero negro?") == IT_CIENCIA);
    CK(it_tema("explica la relatividad general y la gravedad") == IT_CIENCIA);
    CK(it_tema("c\303\263mo instalar Python en Linux") == IT_TECNO);
    CK(it_tema("qu\303\251 GPU es mejor para inteligencia artificial") == IT_TECNO);
    CK(it_tema("\303\272ltimas noticias de las elecciones en Portugal") == IT_ACTUALIDAD);
    CK(it_tema("\302\277cu\303\241nto ha subido la inflaci\303\263n (IPC) este a\303\261o?") == IT_ACTUALIDAD);
    CK(it_tema("receta de la tortilla de patatas") == IT_GENERAL);
    CK(it_tema("") == IT_GENERAL && it_tema(0) == IT_GENERAL);
    CK(it_tema("QUI\303\211N GAN\303\223 EL BAL\303\223N DE ORO") == IT_DEPORTES);
    /* actualidad */
    CK(it_reciente("\302\277Qui\303\251n gan\303\263 el partido del Real Madrid ayer?", IT_DEPORTES) == IT_RECIENTE_DIA);
    CK(it_reciente("resultados de la \303\272ltima jornada", IT_DEPORTES) == IT_RECIENTE_SEMANA);
    CK(it_reciente("historia del Real Madrid", IT_DEPORTES) == IT_RECIENTE_NO);
    CK(it_reciente("la guerra de Ucrania", IT_ACTUALIDAD) == IT_RECIENTE_SEMANA);
    CK(it_reciente("\302\277c\303\263mo se forma un agujero negro?", IT_CIENCIA) == IT_RECIENTE_NO);
    /* fuentes */
    CK(it_puntua(IT_SALUD, "https://medlineplus.gov/spanish/flu.html") > it_puntua(IT_SALUD, "https://es.wikipedia.org/wiki/Gripe"));
    CK(it_puntua(IT_SALUD, "https://www.who.int/es/news-room/fact-sheets") >= 9);
    CK(it_puntua(IT_SALUD, "https://www.remedios-caseros.com/gripe") < 0);
    CK(it_puntua(IT_SALUD, "https://www.sanidad.gob.es/areas/x") == 10);
    CK(it_puntua(IT_SALUD, "https://www.comunidad.madrid.gob.es/x") == 6);        /* otro organismo publico */
    CK(it_puntua(IT_DEPORTES, "https://www.marca.com/futbol/real-madrid.html") > it_puntua(IT_DEPORTES, "https://es.wikipedia.org/wiki/Real_Madrid"));
    CK(it_puntua(IT_DEPORTES, "https://www.bbc.com/sport/football") == 7 && it_puntua(IT_ACTUALIDAD, "https://www.bbc.com/news/world") == 7);
    CK(it_puntua(IT_DEPORTES, "https://canvas.com/x") == 0 && it_puntua(IT_DEPORTES, "https://as.com/futbol/") == 8);   /* "as.com" no es "canvas.com" */
    CK(it_puntua(IT_CIENCIA, "https://science.nasa.gov/universe/black-holes/") == 9);
    CK(it_puntua(IT_CIENCIA, "https://es.wikipedia.org/wiki/Agujero_negro") == 5);
    CK(it_puntua(IT_TECNO, "https://docs.python.org/3/using/unix.html") == 9);
    CK(it_puntua(IT_ACTUALIDAD, "https://www.reuters.com/world/") == 9);
    CK(it_puntua(IT_GENERAL, "https://es.quora.com/algo") < 0);
    CK(it_puntua(IT_GENERAL, "https://foro.example.com/hilo") < 0);
    CK(it_puntua(IT_GENERAL, "https://www.uc3m.es/x") == 0 && it_puntua(IT_GENERAL, "https://www.universidad.es/x") == 3);
    CK(it_puntua(IT_GENERAL, "") < 0 && it_puntua(IT_GENERAL, "https://") < 0);
    CK(it_puntua(99, "https://www.marca.com") == 1);                               /* tema raro: el general */
    {   /* ordenar: estable y sin las malas */
        char u[6][300] = { "https://es.wikipedia.org/wiki/Gripe", "https://www.quora.com/x", "https://medlineplus.gov/spanish/flu.html",
                           "https://blog.example.com/gripe", "https://www.cdc.gov/flu/", "https://otra.example.com/gripe" };
        int n = it_ordena(IT_SALUD, u, 6);
        CK(n == 5);
        CK(!strcmp(u[0], "https://medlineplus.gov/spanish/flu.html") && !strcmp(u[1], "https://www.cdc.gov/flu/"));
        CK(!strcmp(u[2], "https://es.wikipedia.org/wiki/Gripe"));
        CK(!strcmp(u[3], "https://blog.example.com/gripe") && !strcmp(u[4], "https://otra.example.com/gripe"));   /* iguales: en su orden */
    }
    /* informe */
    CK(strstr(it_instrucciones(IT_SALUD, 0), "no sustituye") != 0);
    CK(strstr(it_instrucciones(IT_DEPORTES, IT_RECIENTE_DIA), "marcador") != 0 && strstr(it_instrucciones(IT_DEPORTES, IT_RECIENTE_DIA), "reciente") != 0);
    CK(it_instrucciones(IT_GENERAL, 0)[0] == 0);
    CK(it_wiki_en(IT_CIENCIA) && !it_wiki_en(IT_DEPORTES));
    CK(!strcmp(it_nombre(IT_SALUD), "salud") && !strcmp(it_nombre(-3), "general"));
    /* razonamiento fuera */
    char r1[] = "[Razonamiento]:\nPienso en ello largo y tendido.\n\n[Respuesta]:\nagujero negro formacion\nblack hole formation";
    CK(it_sin_razonamiento(r1) == strlen(r1) && !strncmp(r1, "\nagujero negro", 14));
    char r2[] = "<think>bla bla</think>sintomas gripe";
    CK(it_sin_razonamiento(r2) == 14 && !strcmp(r2, "sintomas gripe"));
    char r3[] = "[Razonamiento]:\nsolo pensando";
    CK(it_sin_razonamiento(r3) == 0 && !r3[0]);
    char r4[] = "<think>sin cerrar";
    CK(it_sin_razonamiento(r4) == 0);
    /* basura: nada se sale */
    unsigned x = 336;
    for (int k = 0; k < 20000; k++) {
        char b[300]; int n = (int)(x % 299);
        for (int i = 0; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (char)(x >> 16); if (!b[i]) b[i] = ' '; }
        b[n] = 0;
        int t = it_tema(b); if (t < 0 || t >= IT_N) { CK(0); break; }
        int r = it_reciente(b, t); if (r < 0 || r > 2) { CK(0); break; }
        (void)it_puntua(t, b);
        if (it_sin_razonamiento(b) > (uint32_t)n) { CK(0); break; }
    }
    printf("test_inv_tema_336: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
