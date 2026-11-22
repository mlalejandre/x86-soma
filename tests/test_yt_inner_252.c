/* SOMA_252_VIDEO_NATIVO: pruebas en el anfitrion de lo que SOMA lee de YouTube cuando le habla directamente
 * (src/apps/yt_inner.h) y del cliente HTTP propio (parte pura de src/apps/yt_https.c).
 * Los datos de tests/datos_252/ son respuestas REALES de YouTube (noviembre de 2026) recortadas y sin datos personales:
 *   busqueda.json     cuatro resultados de una busqueda con el cliente WEB;
 *   player.json       la respuesta del cliente ANDROID para un video (con el formato 18);
 *   player_bot.json   la respuesta "inicia sesion para confirmar que no eres un bot".
 * Si YouTube cambia de formato y esta prueba deja de valer, hay que capturar muestras nuevas (docs/PARCHE_252_...).
 * Ademas, con datos truncados y al azar: nada lee ni escribe fuera de sus buferes (ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/apps/yt_inner.h"
#include "soma_test.h"

static char *slurp(const char *name, uint32_t *n)
{
    char p[300]; snprintf(p, sizeof p, "tests/datos_252/%s", name);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("no se puede abrir %s (la prueba se ejecuta desde la raiz del proyecto)\n", p); exit(2); }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)l + 1); *n = (uint32_t)fread(b, 1, (size_t)l, f); fclose(f);
    char *exact = (char *)malloc(*n ? *n : 1); memcpy(exact, b, *n); free(b);      /* sin byte de mas: ASan ve cualquier lectura fuera */
    return exact;
}
static uint64_t seed = 252;
static uint32_t rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(seed >> 33); }

static void test_busqueda(void)
{
    uint32_t n; char *b = slurp("busqueda.json", &n);
    struct yt_item it[YT_MAX_ITEMS];
    int k = yti_parse_search(b, n, it, YT_MAX_ITEMS);
    CHECK_EQ_INT(k, 4);
    CHECK_STR(it[0].id, "aqz-KE-bpKQ");
    CHECK_STR(it[0].title, "Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film");
    CHECK_STR(it[0].channel, "Blender"); CHECK_EQ_INT(it[0].dur, 635); CHECK_EQ_INT(it[0].views, 23574905);
    CHECK(!strncmp(it[0].desc, "UHD High Frame rate version of the iconic short film by Blender, Big Buck Bunny. More info:", 80));
    CHECK_STR(it[1].id, "YE7VzlLtp-4"); CHECK_EQ_INT(it[1].dur, 597);
    CHECK(strstr(it[3].title, "animaci\xc3\xb3n") != 0);                       /* los acentos llegan en UTF-8 */
    for (int i = 0; i < k; ++i) { CHECK(it[i].title[0] && it[i].channel[0] && it[i].dur > 0 && it[i].views > 0 && it[i].desc[0]); CHECK(strlen(it[i].id) == 11); }
    CHECK_EQ_INT(yti_parse_search(b, n, it, 2), 2);                             /* tope */
    for (uint32_t cut = 0; cut < n; cut += 97) {                                /* respuesta cortada en cualquier punto */
        char *c = (char *)malloc(cut ? cut : 1); memcpy(c, b, cut);
        int r = yti_parse_search(c, cut, it, YT_MAX_ITEMS);
        CHECK(r >= 0 && r <= 4);
        free(c);
    }
    for (int t = 0; t < 300; ++t) {                                             /* bytes cambiados al azar */
        char *c = (char *)malloc(n); memcpy(c, b, n);
        for (int j = 0; j < 40; ++j) c[rnd() % n] = (char)("\"\\{}[]:,u0d\n\xc3\xff"[rnd() % 14]);
        int r = yti_parse_search(c, n, it, YT_MAX_ITEMS);
        CHECK(r >= 0 && r <= YT_MAX_ITEMS);
        for (int i = 0; i < r; ++i) CHECK(strlen(it[i].title) < sizeof it[i].title && strlen(it[i].desc) < sizeof it[i].desc && strlen(it[i].channel) < sizeof it[i].channel);
        free(c);
    }
    free(b);
}

static void test_player(void)
{
    uint32_t n; char *b = slurp("player.json", &n);
    struct yti_player p; char d[600];
    CHECK_EQ_INT(yti_parse_player(b, n, &p, d, sizeof d), 1);
    CHECK_EQ_INT(p.playable, 1); CHECK_STR(p.status, "OK");
    CHECK(!strncmp(p.url, "https://rr2---sn-vg5obxn25po-8aae.googlevideo.com/videoplayback?expire=", 70));
    CHECK(strstr(p.url, "&itag=18&") != 0 && strstr(p.url, "\\u0026") == 0 && strstr(p.url, "ip=203.0.113.7") != 0);     /* \u0026 -> & */
    CHECK_STR(p.title, "Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film"); CHECK_STR(p.author, "Blender");
    CHECK_EQ_INT(p.dur, 635); CHECK_EQ_INT(p.views, 23574905);
    CHECK(!strncmp(d, "UHD High Frame rate version", 27) && strchr(d, '\n') != 0);                                        /* la descripcion conserva sus lineas */
    char small[20]; CHECK_EQ_INT(yti_parse_player(b, n, &p, small, sizeof small), 1); CHECK(strlen(small) < sizeof small);
    for (uint32_t cut = 0; cut < n; cut += 7) {
        char *c = (char *)malloc(cut ? cut : 1); memcpy(c, b, cut);
        int r = yti_parse_player(c, cut, &p, d, sizeof d);
        CHECK(r == 0 || r == 1); CHECK(!p.url[0] || yti_media_url_ok(p.url));
        free(c);
    }
    free(b);
    b = slurp("player_bot.json", &n);
    CHECK_EQ_INT(yti_parse_player(b, n, &p, d, sizeof d), 1);
    CHECK_EQ_INT(p.playable, 0); CHECK_STR(p.status, "LOGIN_REQUIRED"); CHECK(strstr(p.reason, "no eres un bot") != 0); CHECK_STR(p.url, "");
    free(b);
    CHECK_EQ_INT(yti_parse_player("<html>error</html>", 18, &p, d, sizeof d), 0);
    /* la direccion del video solo puede ser de googlevideo.com por https */
    static const char mal[] = "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[{\"itag\":18,\"url\":\"https://evil.example/x.googlevideo.com/v\"}]}}";
    CHECK_EQ_INT(yti_parse_player(mal, sizeof mal - 1, &p, d, sizeof d), 1); CHECK_STR(p.url, "");
    CHECK(yti_media_url_ok("https://rr1---sn-a.googlevideo.com/videoplayback?x=1"));
    CHECK(!yti_media_url_ok("http://rr1---sn-a.googlevideo.com/v") && !yti_media_url_ok("https://googlevideo.com.evil.org/v") && !yti_media_url_ok("https://a@b.googlevideo.com/v")
          && !yti_media_url_ok("https://.googlevideo.com") && !yti_media_url_ok("https://xgooglevideo.com/v") && !yti_media_url_ok(""));
    CHECK_EQ_INT(yti_parse_player_ex(mal, sizeof mal - 1, &p, d, sizeof d, 1), 1); CHECK(p.url[0] != 0);                   /* con una base propia (pruebas) si */
}

static void test_cadenas(void)
{
    char o[64]; uint32_t k = 0; const char *e;
    static const char a[] = "Caf\\u00e9 \\\"x\\\" \\ud83d\\ude00 fin\\n2\" resto";
    e = yti_str(a, a + sizeof a - 1, o, sizeof o, &k, 0);
    CHECK(e && !strcmp(e, " resto")); CHECK_STR(o, "Caf\xc3\xa9 \"x\" fin 2");
    k = 0; e = yti_str(a, a + sizeof a - 1, o, sizeof o, &k, 1); CHECK_STR(o, "Caf\xc3\xa9 \"x\" fin\n2");
    k = 0; CHECK(yti_str("sin cerrar", (const char *)"sin cerrar" + 10, o, sizeof o, &k, 0) == 0);
    k = 0; CHECK(yti_str("\\u12", (const char *)"\\u12" + 4, o, sizeof o, &k, 0) == 0);
    char t[8]; k = 0; e = yti_str("abcdefghijklmnop\"", (const char *)"abcdefghijklmnop\"" + 17, t, sizeof t, &k, 0); CHECK(e && strlen(t) == 7);
    CHECK_EQ_INT(yti_clock("10:35\"", (const char *)"10:35\"" + 6), 635); CHECK_EQ_INT(yti_clock("1:02:03\"", (const char *)"1:02:03\"" + 8), 3723);
    CHECK_EQ_INT(yti_clock("EN DIRECTO\"", (const char *)"EN DIRECTO\"" + 11), 0);
    CHECK_EQ_INT(yti_digits("23.574.905 visualizaciones\"", (const char *)"23.574.905 visualizaciones\"" + 27), 23574905);
    CHECK_EQ_INT(yti_digits("Sin visualizaciones\"", (const char *)"Sin visualizaciones\"" + 20), 0);
    CHECK_EQ_INT(yti_digits("99999999999999\"", (const char *)"99999999999999\"" + 15), 0xFFFFFFFFu);
    char body[900]; uint32_t n = yti_body_search(body, sizeof body, "pan \"casero\"\\ f\xc3\xa1" "cil\n");
    CHECK(n == strlen(body) && strstr(body, "\"query\":\"pan \\\"casero\\\"\\\\ f\xc3\xa1" "cil\\u000a\"}") != 0);
    n = yti_body_player(body, sizeof body, "aqz-KE-bpKQ"); CHECK(n == strlen(body) && strstr(body, "\"videoId\":\"aqz-KE-bpKQ\"") != 0 && strstr(body, "\"clientName\":\"ANDROID\"") != 0);
    char tiny[40]; n = yti_body_search(tiny, sizeof tiny, "x"); CHECK(strlen(tiny) < sizeof tiny);
    char u[80]; yti_thumb_url(u, sizeof u, "aqz-KE-bpKQ"); CHECK_STR(u, "https://i.ytimg.com/vi/aqz-KE-bpKQ/mqdefault.jpg");
}

static void test_http(void)
{
    struct yh_url u;
    CHECK(yh_parse_url("https://www.youtube.com/youtubei/v1/search?prettyPrint=false", &u) && u.tls && u.port == 443 && !strcmp(u.host, "www.youtube.com") && !strcmp(u.path, "/youtubei/v1/search?prettyPrint=false"));
    CHECK(yh_parse_url("http://10.0.2.2:8090/v.mp4", &u) && !u.tls && u.port == 8090 && !strcmp(u.host, "10.0.2.2"));
    CHECK(yh_parse_url("HTTPS://Rr2---SN-x.GoogleVideo.com", &u) && !strcmp(u.host, "rr2---sn-x.googlevideo.com") && !strcmp(u.path, "/"));
    CHECK(!yh_parse_url("ftp://x/y", &u) && !yh_parse_url("https://", &u) && !yh_parse_url("https://user@host/x", &u) && !yh_parse_url("http://h:0/", &u) && !yh_parse_url("http://h:99999/", &u)
          && !yh_parse_url("http://h:12x/", &u) && !yh_parse_url("http://a b/", &u) && !yh_parse_url("", &u));
    char longh[400]; strcpy(longh, "https://"); memset(longh + 8, 'a', 300); strcpy(longh + 308, "/x"); CHECK(!yh_parse_url(longh, &u));
    struct yh_head h;
    static const char r1[] = "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\ncontent-length: 1048576\r\nContent-Range: bytes 0-1048575/28523658\r\n\r\n";
    CHECK(yh_parse_head(r1, sizeof r1 - 1, &h) && h.status == 206 && h.has_clen && h.clen == 1048576 && h.range_total == 28523658u && !h.chunked);
    static const char r2[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\nLocation: nada\r\n\r\n";
    CHECK(yh_parse_head(r2, sizeof r2 - 1, &h) && h.status == 200 && h.chunked && !h.has_clen);
    static const char r3[] = "HTTP/1.1 302 Found\r\nLocation:   https://rr5---sn-y.googlevideo.com/videoplayback?a=1\r\nContent-Length: 0\r\n\r\n";
    CHECK(yh_parse_head(r3, sizeof r3 - 1, &h) && h.status == 302 && !strcmp(h.location, "https://rr5---sn-y.googlevideo.com/videoplayback?a=1") && h.has_clen && h.clen == 0);
    static const char r4[] = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */28523658\r\n\r\n";
    CHECK(yh_parse_head(r4, sizeof r4 - 1, &h) && h.status == 416 && h.range_total == 28523658u);
    CHECK(!yh_parse_head("SSH-2.0-OpenSSH\r\n\r\n", 19, &h) && !yh_parse_head("HTTP/1.1 2x0 OK\r\n\r\n", 19, &h) && !yh_parse_head("", 0, &h));
    char big[3000]; int p = snprintf(big, sizeof big, "HTTP/1.1 302 Found\r\nLocation: https://x/"); memset(big + p, 'a', 2000); strcpy(big + p + 2000, "\r\n\r\n");
    CHECK(yh_parse_head(big, (uint32_t)strlen(big), &h) && h.location[0] == 0);                         /* una direccion que no cabe no se sigue a medias */
    for (int t = 0; t < 3000; ++t) {
        char z[200]; uint32_t L = 12 + rnd() % 180;
        memcpy(z, "HTTP/1.1 200", 12);
        for (uint32_t i = 12; i < L; ++i) { uint32_t r = rnd(); z[i] = (char)(r % 3 == 0 ? (uint32_t)(unsigned char)"\r\n: /0123456789-*cCLlrRtT"[r % 25] : 'a' + r % 26); }
        char *c = (char *)malloc(L); memcpy(c, z, L);
        (void)yh_parse_head(c, L, &h);
        CHECK(strlen(h.location) < sizeof h.location);
        free(c);
    }
}

int main(void)
{
    test_busqueda();
    test_player();
    test_cadenas();
    test_http();
    TEST_MAIN_END("yt_inner_252");
}
