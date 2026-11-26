/* SOMA_335_YT_BUSQUEDAS: que se busca en YouTube cuando el modelo no ayuda.
 *  - la respuesta sin el razonamiento ([Razonamiento]/[Respuesta] y <think>), como la devuelve llm.c con un modelo que razona;
 *  - una frase larga del razonamiento ya no cuenta como busqueda;
 *  - sin LLM (o sin busquedas utiles), la busqueda sale de la peticion sin "quiero ver videos de", "ponme", "en YouTube"... */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/apps/yt_proto.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)

static char q[YT_AI_QMAX][YT_AI_QLEN];
static int busquedas(const char *resp)                     /* lo que hace job_nl con la respuesta del modelo */
{
    static char b[8192];
    snprintf(b, sizeof b, "%s", resp);
    yt_ai_respuesta(b); yt_plain(b);
    return yt_ai_queries(b, q);
}
static const char *local(const char *t) { static char o[YT_AI_QLEN]; yt_consulta_local(t, o, sizeof o); return o; }

int main(void)
{
    /* el razonamiento fuera */
    CK(busquedas("[Razonamiento]:\nEl usuario quiere aprender a hacer pan sin amasar, asi que buscare recetas sencillas y populares en espanol.\n\n"
                 "[Respuesta]:\npan sin amasar receta\npan casero facil\nno knead bread") == 3);
    CK(!strcmp(q[0], "pan sin amasar receta") && !strcmp(q[2], "no knead bread"));
    CK(busquedas("<think>Vale, el usuario pide videos de gatos graciosos; propondre tres busquedas.</think>\ngatos graciosos\nfunny cats compilation") == 2);
    CK(!strcmp(q[0], "gatos graciosos"));
    CK(busquedas("<think>pienso y pienso sin terminar porque se acabaron las fichas") == 0);   /* sin cerrar: no hay respuesta */
    CK(busquedas("[Razonamiento]:\nEl usuario quiere ver un documental sobre la segunda guerra mundial y yo deberia proponer busquedas") == 0);
    CK(busquedas("Aqui tienes:\n<think>x</think>historia de roma documental") == 1 && !strcmp(q[0], "historia de roma documental"));
    /* frases largas: no son busquedas */
    CK(busquedas("El usuario quiere ver videos sobre como cuidar un bonsai en casa durante el invierno\nbonsai cuidados invierno") == 1);
    CK(!strcmp(q[0], "bonsai cuidados invierno"));
    CK(busquedas("receta paella valenciana\npaella autentica paso a paso\n") == 2);        /* lo de siempre sigue igual */
    CK(busquedas("1. \"como hacer pan en casa\"\n2) pan de masa madre.") == 2);

    /* sin LLM: la busqueda sale de la peticion */
    CK(!strcmp(local("quiero ver videos de como hacer pan casero sin amasar, por favor"), "como hacer pan casero sin amasar"));
    CK(!strcmp(local("Ponme un v\303\255deo de gatos graciosos en YouTube"), "gatos graciosos"));
    CK(!strcmp(local("\302\277Puedes buscarme v\303\255deos sobre la Segunda Guerra Mundial?"), "Segunda Guerra Mundial"));
    CK(!strcmp(local("busca en youtube musica para estudiar"), "musica para estudiar"));
    CK(!strcmp(local("me gustaria ver algun tutorial de python para principiantes"), "python para principiantes tutorial"));
    CK(!strcmp(local("documental sobre agujeros negros"), "documental sobre agujeros negros"));   /* sin relleno: igual */
    CK(!strcmp(local("la casa de papel"), "casa de papel"));
    CK(!strcmp(local("quiero ver"), "quiero ver"));                     /* no queda nada: la peticion tal cual */
    CK(!strcmp(local(""), ""));
    CK(!strcmp(local("  hola soma, pon videos de la luna, gracias "), "luna"));

    /* basura: nada se sale del bufer */
    uint32_t x = 335;
    for (int r = 0; r < 20000; r++) {
        char b[300]; int n = (int)(x % 299);
        for (int i = 0; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (char)((x >> 16) % 3 == 0 ? " ,.?<>/[]:\n"[(x >> 8) % 11] : (char)(x >> 16)); if (!b[i]) b[i] = 'a'; }
        b[n] = 0;
        char o[YT_AI_QLEN]; uint32_t l = yt_consulta_local(b, o, sizeof o);
        if (l >= YT_AI_QLEN || strlen(o) != l) { CK(0); break; }
        char c[300]; memcpy(c, b, sizeof c); c[n] = 0;
        uint32_t m = yt_ai_respuesta(c);
        if (m > (uint32_t)n || strlen(c) != m) { CK(0); break; }
        int k = yt_ai_queries(c, q);
        if (k < 0 || k > YT_AI_QMAX) { CK(0); break; }
    }
    char o2[8]; yt_consulta_local("quiero ver videos de cosas larguisimas", o2, sizeof o2); CK(strlen(o2) < sizeof o2);

    printf("test_yt_busquedas_335: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
