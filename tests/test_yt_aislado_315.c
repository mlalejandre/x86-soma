/* SOMA_315_YOUTUBE_AISLADO: pruebas en el anfitrion de src/apps/yt_aislado.h/.c (en el anfitrion se llama directo, como en aarch64).
 *  1. yti_str deja la salida SIEMPRE terminada: las respuestas reales de tests/datos_252 cortadas en cualquier punto, con los
 *     buferes llenos de basura antes, dan campos terminados (antes, un canal o un estado sin cerrar quedaba sin terminador).
 *  2. El saneado del nucleo: estructuras al azar salen con las cadenas terminadas y sin caracteres de control, ids validos,
 *     direcciones de YouTube de solo ASCII visible y como mucho max resultados.
 *  3. Con respuestas legitimas el saneado no cambia nada. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "yt_aislado.h"

static char *slurp(const char *name, uint32_t *n)
{
    char p[300]; snprintf(p, sizeof p, "tests/datos_252/%s", name);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("no se puede abrir %s (la prueba se ejecuta desde la raiz del proyecto)\n", p); exit(2); }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)l + 1); *n = (uint32_t)fread(b, 1, (size_t)l, f); fclose(f);
    char *exact = (char *)malloc(*n ? *n : 1); memcpy(exact, b, *n); free(b);
    return exact;
}
static uint64_t seed = 315;
static uint32_t rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(seed >> 33); }

static int termina(const char *s, size_t cap) { return memchr(s, 0, cap) != 0; }
static int limpia(const char *s, size_t cap, int nl)
{
    if (!termina(s, cap)) return 0;
    for (; *s; ++s) { unsigned char c = (unsigned char)*s; if ((c < 32 && !(nl && c == '\n')) || c == 127) return 0; }
    return 1;
}
static int item_ok(const struct yt_item *o)
{
    return yta_id_ok(o->id) && o->title[0] && limpia(o->title, sizeof o->title, 0) && limpia(o->channel, sizeof o->channel, 0) && limpia(o->desc, sizeof o->desc, 0);
}
static int ficha_ok(const struct yti_player *o, const char *d, uint32_t dmax)
{
    return limpia(o->status, sizeof o->status, 0) && limpia(o->reason, sizeof o->reason, 0) && limpia(o->title, sizeof o->title, 0)
        && limpia(o->author, sizeof o->author, 0) && termina(o->url, sizeof o->url) && (!o->url[0] || yti_media_url_ok(o->url))
        && (!dmax || limpia(d, dmax, 1));
}

static void test_terminadores(void)
{
    uint32_t nb, np;
    char *b = slurp("busqueda.json", &nb), *p = slurp("player.json", &np);
    static struct yt_item it[YT_MAX_ITEMS]; static struct yti_player pl; static char d[700];
    int malos = 0;
    for (uint32_t k = 0; k < 1500; ++k) {                                   /* la busqueda cortada en 1500 puntos al azar */
        uint32_t n = rnd() % (nb + 1);
        memset(it, 'Z', sizeof it);
        int r = yti_parse_search(b, n, it, YT_MAX_ITEMS);
        for (int i = 0; i < r; ++i) if (!termina(it[i].id, sizeof it[i].id) || !termina(it[i].title, sizeof it[i].title)
                                        || !termina(it[i].channel, sizeof it[i].channel) || !termina(it[i].desc, sizeof it[i].desc)) malos++;
    }
    for (uint32_t n = 0; n <= np; ++n) {                                    /* la ficha cortada en todos los puntos */
        memset(&pl, 'Z', sizeof pl); memset(d, 'Z', sizeof d);
        if (yti_parse_player_ex(p, n, &pl, d, sizeof d, 0) != 1) continue;
        if (!termina(pl.status, sizeof pl.status) || !termina(pl.reason, sizeof pl.reason) || !termina(pl.title, sizeof pl.title)
            || !termina(pl.author, sizeof pl.author) || !termina(pl.url, sizeof pl.url) || !termina(d, sizeof d)) malos++;
    }
    CHECK_EQ_INT(malos, 0);
    /* el caso concreto: un canal sin cerrar */
    static const char s[] = "{\"videoRenderer\":{\"videoId\":\"aqz-KE-bpKQ\",\"title\":{\"runs\":[{\"text\":\"Hola\"}]},\"ownerText\":{\"runs\":[{\"text\":\"Blend";
    memset(it, 'Z', sizeof it);
    CHECK_EQ_INT(yti_parse_search(s, sizeof s - 1, it, 2), 1);
    CHECK_STR(it[0].channel, "Blend");
    free(b); free(p);
}

static void test_saneado_azar(void)
{
    static struct yt_item it[YT_MAX_ITEMS]; static struct yti_player pl; static char d[300];
    int malos = 0;
    for (int k = 0; k < 20000; ++k) {
        uint8_t *q = (uint8_t *)it;
        for (size_t i = 0; i < sizeof it; ++i) q[i] = (uint8_t)rnd();
        if (k & 1) for (int i = 0; i < YT_MAX_ITEMS; ++i) {                 /* la mitad de las veces, ids y titulos plausibles */
            for (int j = 0; j < YT_ID_LEN; ++j) it[i].id[j] = "abcXYZ019-_"[rnd() % 11];
            it[i].id[YT_ID_LEN] = (rnd() & 3) ? 0 : 'x';
            it[i].title[rnd() % 20] = 0;
        }
        int max = 1 + (int)(rnd() % YT_MAX_ITEMS), n = (int)(rnd() % 60) - 10;
        int r = yta_sanea_lista(it, n, max);
        if (r < 0 || r > max || r > (n < 0 ? 0 : n)) malos++;
        for (int i = 0; i < r; ++i) if (!item_ok(&it[i])) malos++;

        q = (uint8_t *)&pl;
        for (size_t i = 0; i < sizeof pl; ++i) q[i] = (uint8_t)rnd();
        for (size_t i = 0; i < sizeof d; ++i) d[i] = (char)rnd();
        if (k % 3 == 0) { strcpy(pl.url, "https://rr1---sn-x.googlevideo.com/videoplayback?a=1"); pl.url[30 + rnd() % 20] = (char)(rnd() % 40); }
        if (k % 5 == 0) strcpy(pl.status, "OK");
        uint32_t dmax = rnd() % sizeof d;
        yta_sanea_ficha(&pl, dmax ? d : 0, dmax, 0);
        if (!ficha_ok(&pl, d, dmax) || pl.playable != (strcmp(pl.status, "OK") == 0)) malos++;
        if (pl.url[0] && strchr(pl.url, '\r')) malos++;
    }
    CHECK_EQ_INT(malos, 0);
    /* any_host (base propia para pruebas): http(s) de cualquier sitio, pero nunca con espacios ni controles */
    strcpy(pl.url, "http://10.0.2.2:8097/v.mp4"); strcpy(pl.status, "OK");
    yta_sanea_ficha(&pl, 0, 0, 1); CHECK_STR(pl.url, "http://10.0.2.2:8097/v.mp4");
    yta_sanea_ficha(&pl, 0, 0, 0); CHECK_STR(pl.url, "");
    strcpy(pl.url, "http://10.0.2.2/v.mp4 HTTP/1.1"); yta_sanea_ficha(&pl, 0, 0, 1); CHECK_STR(pl.url, "");
    strcpy(pl.url, "ftp://x/y"); yta_sanea_ficha(&pl, 0, 0, 1); CHECK_STR(pl.url, "");
}

static void test_legitimo(void)
{
    uint32_t nb, np, nbot;
    char *b = slurp("busqueda.json", &nb), *p = slurp("player.json", &np), *bot = slurp("player_bot.json", &nbot);
    static struct yt_item a[YT_MAX_ITEMS], c[YT_MAX_ITEMS];
    int est = 0, n = yti_busca_aislado(b, nb, a, YT_MAX_ITEMS, &est);
    CHECK_EQ_INT(n, 4);
    CHECK(est == 1);
    memcpy(c, a, sizeof a);
    CHECK_EQ_INT(yta_sanea_lista(c, n, YT_MAX_ITEMS), 4);
    CHECK(memcmp(a, c, sizeof a) == 0);                                     /* el saneado no cambia nada legitimo */
    CHECK(yti_busca_aislado("{}", 2, a, YT_MAX_ITEMS, &est) == 0 && est == 0);

    static struct yti_player p1, p2; static char d1[6200], d2[6200];
    CHECK_EQ_INT(yti_ficha_aislada(p, np, &p1, d1, sizeof d1, 0), 1);
    CHECK(p1.playable && yti_media_url_ok(p1.url));
    memcpy(&p2, &p1, sizeof p1); memcpy(d2, d1, sizeof d1);
    yta_sanea_ficha(&p2, d2, sizeof d2, 0);
    CHECK(memcmp(&p1, &p2, sizeof p1) == 0 && memcmp(d1, d2, sizeof d1) == 0);
    CHECK_EQ_INT(yti_ficha_aislada(bot, nbot, &p1, d1, sizeof d1, 0), 1);
    CHECK(!p1.playable && !p1.url[0]);
    free(b); free(p); free(bot);
}

int main(void)
{
    test_terminadores();
    test_saneado_azar();
    test_legitimo();
    TEST_MAIN_END("yt_aislado_315");
}
