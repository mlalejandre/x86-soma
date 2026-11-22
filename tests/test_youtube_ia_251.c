/* SOMA_251_YOUTUBE_IA: pruebas en el anfitrion de la parte pura del buscador con IA, la ficha, la transcripcion y las
 * preguntas (src/apps/yt_proto.h): la sexta columna de los resultados, la configuracion nueva, las rutas del servicio, la
 * respuesta de /yt/info, el saneado de lo que escriben el LLM y whisper, las busquedas que propone el LLM en todos los
 * formatos en que suele responder, la mezcla de resultados, el recorte del contexto y la salida de whisper.
 * Con datos al azar: nada de lo que llega del LLM, de whisper o de la red escribe fuera de un bufer (ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/apps/yt_proto.h"
#include "soma_test.h"

static uint64_t seed = 251;
static uint32_t rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(seed >> 33); }

static void test_resultados(void)
{
    static const char r[] = "SOMAYT 1 3\n"
        "dQw4w9WgXcQ\t213\tNever Gonna Give You Up\tRick Astley\t1500000000\tEl video oficial de la cancion.\n"
        "aqz-KE-bpKQ\t634\tBig Buck Bunny\tBlender\t27000\n"                    /* servicio antiguo: sin descripcion */
        "___________\t0\tDirecto\tCanal\t0\t\n";
    struct yt_item it[YT_MAX_ITEMS];
    CHECK_EQ_INT(yt_parse_results(r, sizeof r - 1, it, YT_MAX_ITEMS), 3);
    CHECK_STR(it[0].desc, "El video oficial de la cancion."); CHECK_EQ_INT(it[0].views, 1500000000u);
    CHECK_STR(it[1].desc, ""); CHECK_STR(it[1].channel, "Blender"); CHECK_EQ_INT(it[1].views, 27000);
    CHECK_STR(it[2].desc, "");
    /* descripcion mas larga que el campo: se corta sin dejar un caracter UTF-8 a medias */
    char big[600]; int n = snprintf(big, sizeof big, "SOMAYT 1 1\ndQw4w9WgXcQ\t1\tt\tc\t1\t");
    for (int i = 0; i < 150; ++i) { big[n++] = (char)0xC3; big[n++] = (char)0xB1; }        /* 150 enes: 300 bytes */
    big[n++] = '\n';
    CHECK_EQ_INT(yt_parse_results(big, (uint32_t)n, it, YT_MAX_ITEMS), 1);
    uint32_t L = yt_slen(it[0].desc);
    CHECK(L < sizeof it[0].desc && L >= sizeof it[0].desc - 3 && L % 2 == 0);
    CHECK((unsigned char)it[0].desc[L - 1] == 0xB1);
}

static void test_conf(void)
{
    struct yt_conf c, d; char t[400];
    static const char vieja[] = "servidor=192.168.1.20:8097\ncalidad=480\nfps=20\n";                 /* archivo del parche 250 */
    yt_conf_parse(vieja, sizeof vieja - 1, &c);
    CHECK_EQ_INT(c.height, 480); CHECK_EQ_INT(c.ai, 1); CHECK_STR(c.lang, "auto"); CHECK_EQ_INT(c.max_min, 20); CHECK_EQ_INT(c.ctx, 16000);
    c.ai = 0; yt_scopy(c.lang, sizeof c.lang, "es"); c.max_min = 45; c.ctx = 60000;
    uint32_t n = yt_conf_format(&c, t, sizeof t);
    yt_conf_parse(t, n, &d);
    CHECK_EQ_INT(d.ai, 0); CHECK_STR(d.lang, "es"); CHECK_EQ_INT(d.max_min, 45); CHECK_EQ_INT(d.ctx, 60000); CHECK_STR(d.host, "192.168.1.20"); CHECK_EQ_INT(d.fps, 20);
    static const char mala[] = "ia=0\nidioma=../../x\nminutos=0\ncontexto=5\nidioma=ES\nminutos=999\n";
    yt_conf_parse(mala, sizeof mala - 1, &d);
    CHECK_EQ_INT(d.ai, 0); CHECK_STR(d.lang, "auto"); CHECK_EQ_INT(d.max_min, 20); CHECK_EQ_INT(d.ctx, 16000);
    CHECK(yt_lang_ok("auto") && yt_lang_ok("es") && yt_lang_ok("fil") && !yt_lang_ok("e") && !yt_lang_ok("espa") && !yt_lang_ok("e s") && !yt_lang_ok(""));
}

static void test_rutas(void)
{
    struct yt_conf c; yt_conf_default(&c);
    char p[300];
    yt_path_info(p, sizeof p, &c, "dQw4w9WgXcQ"); CHECK_STR(p, "/yt/info?id=dQw4w9WgXcQ");
    yt_path_audio(p, sizeof p, &c, "dQw4w9WgXcQ", 1200); CHECK_STR(p, "/yt/audio?id=dQw4w9WgXcQ&max=1200");
    yt_scopy(c.key, sizeof c.key, "a b&c"); yt_scopy(c.lang, sizeof c.lang, "en");
    yt_path_subs(p, sizeof p, &c, "dQw4w9WgXcQ"); CHECK_STR(p, "/yt/subs?id=dQw4w9WgXcQ&l=en&k=a%20b%26c");
    yt_txt_path(p, sizeof p, "dQw4w9WgXcQ"); CHECK_STR(p, "/yt_dQw4w9WgXcQ.txt");
    char tiny[10]; yt_path_audio(tiny, sizeof tiny, &c, "dQw4w9WgXcQ", 5); CHECK(yt_slen(tiny) == 9);
    static const char h[] = "HTTP/1.0 200 OK\r\nServer: x\r\nContent-Type: audio/wav\r\ncontent-LENGTH:   19200044\r\nX-Content-Length: 7\r\n\r\nRIFF";
    int st = 0; uint32_t off = 0;
    CHECK_EQ_INT(yt_http_head(h, sizeof h - 1, &st, &off), 1);
    CHECK(yt_http_clen(h, off) == 19200044u);
    static const char h2[] = "HTTP/1.0 200 OK\r\nX-Content-Length: 7\r\n\r\n";
    CHECK(yt_http_clen(h2, sizeof h2 - 1) == 0);
    CHECK(yt_http_clen("", 0) == 0);
}

static void test_info(void)
{
    static const char b[] = "SOMAYT 1 info\ntitulo\tUn titulo\ncanal\tCanal\nduracion\t754\nvistas\t123456\nfecha\t2024-05-01\nsubs\tes,en\ncampo_nuevo\tx\n"
                            "\nPrimera linea.\n\nSegunda: con\ttabulador\nfecha\t1999-01-01\n\n";
    struct yt_info i;
    CHECK_EQ_INT(yt_parse_info(b, sizeof b - 1, &i), 1);
    CHECK_EQ_INT(i.dur, 754); CHECK_EQ_INT(i.views, 123456); CHECK_STR(i.date, "2024-05-01"); CHECK_STR(i.subs, "es,en");
    CHECK(i.desc_len == strlen("Primera linea.\n\nSegunda: con\ttabulador\nfecha\t1999-01-01"));
    CHECK(memcmp(b + i.desc_off, "Primera linea.", 14) == 0);
    static const char sin[] = "SOMAYT 1 info\nduracion\t5\n";                    /* sin descripcion ni linea en blanco */
    CHECK_EQ_INT(yt_parse_info(sin, sizeof sin - 1, &i), 1); CHECK_EQ_INT(i.desc_len, 0); CHECK_EQ_INT(i.dur, 5);
    CHECK_EQ_INT(yt_parse_info("SOMAYT 1 3\nx\n", 13, &i), 0);                    /* es una lista de resultados */
    CHECK_EQ_INT(yt_parse_info("Ruta desconocida", 16, &i), 0);
    static const char larga[] = "SOMAYT 1 info\nfecha\t2024-05-01-y-mucho-mas-de-lo-que-cabe\nsubs\taa,bb,cc,dd,ee,ff,gg,hh,ii,jj,kk,ll,mm,nn,oo,pp,qq,rr,ss,tt\n\nd";
    CHECK_EQ_INT(yt_parse_info(larga, sizeof larga - 1, &i), 1);
    CHECK(yt_slen(i.date) == sizeof i.date - 1 && yt_slen(i.subs) == sizeof i.subs - 1 && i.desc_len == 1);
}

static void test_plain(void)
{
    char a[] = "## Resumen\r\n**Hola** \xe2\x80\x9cmundo\xe2\x80\x9d \xf0\x9f\x98\x80 caf\xc3\xa9\xe2\x80\xa6\n\n\n\n* punto uno\n- dos *cursiva* 3 * 4 = 12\n   sangrado `codigo`\xc2\xa0x\n\n";
    uint32_t n = yt_plain(a);
    CHECK_STR(a, "Resumen\nHola \"mundo\" caf\xc3\xa9...\n\n- punto uno\n- dos cursiva 3 * 4 = 12\nsangrado codigo x");
    CHECK(n == strlen(a));
    char b[] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e solo \xff\xfe bytes \xc3";            /* CJK fuera, bytes sueltos fuera */
    yt_plain(b); CHECK_STR(b, "solo bytes");
    char c[] = "\n\n  \n"; CHECK_EQ_INT(yt_plain(c), 0); CHECK_STR(c, "");
    char d[] = "\xc2\xbfQu\xc3\xa9 tal? \xe2\x80\x94 bien \xe2\x80\x98s\xc3\xad\xe2\x80\x99"; yt_plain(d); CHECK_STR(d, "\xc2\xbfQu\xc3\xa9 tal? - bien 's\xc3\xad'");
    for (int k = 0; k < 3000; ++k) {                                              /* al azar: nunca crece ni se sale */
        char z[96]; uint32_t L = rnd() % (sizeof z - 1);
        for (uint32_t i = 0; i < L; ++i) { uint32_t r = rnd(); z[i] = (char)(r % 5 == 0 ? (uint32_t)(unsigned char)"*#`\n \xe2\x80\xa6\xc3\xb1"[r % 11] : (r % 255) + 1); }
        z[L] = 0;
        uint32_t o = yt_plain(z);
        CHECK(o <= L && z[o] == 0 && o == strlen(z));
        for (uint32_t i = 0; i < o; ++i) CHECK(z[i] == '\n' || (unsigned char)z[i] >= 32);
    }
}

static void test_busquedas(void)
{
    char q[YT_AI_QMAX][YT_AI_QLEN];
    CHECK_EQ_INT(yt_ai_queries("receta paella valenciana\npaella autentica paso a paso\n", q), 2);
    CHECK_STR(q[0], "receta paella valenciana"); CHECK_STR(q[1], "paella autentica paso a paso");
    CHECK_EQ_INT(yt_ai_queries("Aqui tienes las busquedas:\n1. \"como hacer pan en casa\"\n2) pan de masa madre.\n- **pan sin amasar**\n* otra mas\n", q), 3);
    CHECK_STR(q[0], "como hacer pan en casa"); CHECK_STR(q[1], "pan de masa madre"); CHECK_STR(q[2], "pan sin amasar");
    CHECK_EQ_INT(yt_ai_queries("```json\n[\"rust tutorial\", \"rust para principiantes\"]\n```", q), 2);
    CHECK_STR(q[0], "rust tutorial"); CHECK_STR(q[1], "rust para principiantes");
    CHECK_EQ_INT(yt_ai_queries("{\"busquedas\": [\"historia de roma\", \"imperio romano documental\"], \"idioma\": \"es\"}", q), 3);
    CHECK_STR(q[0], "historia de roma"); CHECK_STR(q[2], "es");
    CHECK_EQ_INT(yt_ai_queries("jazz suave; jazz para estudiar ;jazz suave", q), 2);     /* una linea con ';' y una repetida */
    CHECK_STR(q[1], "jazz para estudiar");
    CHECK_EQ_INT(yt_ai_queries("una sola busqueda", q), 1); CHECK_STR(q[0], "una sola busqueda");
    CHECK_EQ_INT(yt_ai_queries("", q), 0);
    CHECK_EQ_INT(yt_ai_queries("\n\n-\n*\n1.\n", q), 0);
    char larga[400]; memset(larga, 'a', sizeof larga - 1); larga[sizeof larga - 1] = 0;
    CHECK_EQ_INT(yt_ai_queries(larga, q), 0);                                     /* un parrafo no es una busqueda */
    CHECK_EQ_INT(yt_ai_queries("[\"sin cerrar", q), 0);
    for (int k = 0; k < 3000; ++k) {
        char z[300]; uint32_t L = rnd() % (sizeof z - 1);
        for (uint32_t i = 0; i < L; ++i) { uint32_t r = rnd(); z[i] = (char)(r % 4 == 0 ? (uint32_t)(unsigned char)"\n\"[]{};:-*1. \\"[r % 14] : 'a' + r % 26); }
        z[L] = 0;
        int n = yt_ai_queries(z, q);
        CHECK(n >= 0 && n <= YT_AI_QMAX);
        for (int i = 0; i < n; ++i) CHECK(yt_slen(q[i]) >= 2 && yt_slen(q[i]) < YT_AI_QLEN);
    }
}

static void test_mezcla(void)
{
    struct yt_item a[6], b[4];
    memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    yt_scopy(a[0].id, 12, "AAAAAAAAAAA"); yt_scopy(a[1].id, 12, "BBBBBBBBBBB");
    yt_scopy(b[0].id, 12, "BBBBBBBBBBB"); yt_scopy(b[1].id, 12, "CCCCCCCCCCC"); yt_scopy(b[2].id, 12, "AAAAAAAAAAA"); yt_scopy(b[3].id, 12, "DDDDDDDDDDD");
    int n = yt_merge(a, 2, 6, b, 4);
    CHECK_EQ_INT(n, 4); CHECK_STR(a[2].id, "CCCCCCCCCCC"); CHECK_STR(a[3].id, "DDDDDDDDDDD");
    CHECK_EQ_INT(yt_merge(a, 4, 4, b, 4), 4);                                     /* lleno */
    CHECK_EQ_INT(yt_merge(a, 0, 3, b, 4), 3);
}

static void test_contexto(void)
{
    char src[5000], dst[5200];
    for (uint32_t i = 0; i < sizeof src - 1; ++i) src[i] = i % 9 == 8 ? ' ' : (char)('a' + i % 26);
    src[sizeof src - 1] = 0;
    uint32_t L = sizeof src - 1;
    CHECK_EQ_INT(yt_clip_ctx(dst, sizeof dst, src, L, 6000), L); CHECK(!strcmp(dst, src));          /* cabe: tal cual */
    uint32_t w = yt_clip_ctx(dst, sizeof dst, src, L, 1000);
    CHECK(w <= 1000 && w > 900 && strlen(dst) == w);
    CHECK(strstr(dst, "[... parte central omitida ...]") != 0);
    CHECK(!strncmp(dst, src, 500));                                               /* empieza igual */
    CHECK(!strcmp(dst + w - 200, src + L - 200));                                 /* acaba igual */
    w = yt_clip_ctx(dst, 300, src, L, 1000); CHECK(w < 300 && strlen(dst) == w);  /* destino menor que el presupuesto */
    w = yt_clip_ctx(dst, sizeof dst, src, L, 40); CHECK_EQ_INT(w, 40);            /* presupuesto minimo: solo el principio */
    CHECK_EQ_INT(yt_clip_ctx(dst, sizeof dst, "", 0, 100), 0);
    /* UTF-8 sin espacios: no se parte un caracter */
    char u[2001]; for (int i = 0; i < 1000; ++i) { u[2 * i] = (char)0xC3; u[2 * i + 1] = (char)0xB1; }
    u[2000] = 0;
    w = yt_clip_ctx(dst, sizeof dst, u, 2000, 501);
    const char *m = strstr(dst, "\n[...");
    CHECK(m && (m - dst) % 2 == 0);
    const char *t = strstr(dst, "...]\n"); CHECK(t && (unsigned char)t[5] == 0xC3 && (strlen(t + 5) % 2) == 0);
    for (int k = 0; k < 2000; ++k) {
        uint32_t len = rnd() % 3000, bud = rnd() % 3500, cap = 1 + rnd() % 3400;
        char *d = (char *)malloc(cap);                                            /* ASan vigila el final exacto */
        uint32_t o = yt_clip_ctx(d, cap, src, len, bud);
        CHECK(o < cap && d[o] == 0);
        free(d);
    }
}

static void test_whisper(void)
{
    char o[600], inf[80];
    static const char raw[] = "[voz] 25 %\n[00:00.00 -> 00:05.20]  Hola a todos.\n[00:05.20 -> 00:12.00] Hoy vemos SOMA.\r\n"
                              "[00:21.50 -> 00:30.00] Segunda parte.\n[61:02.00 -> 61:09.00] Muy tarde.\n[voz] 70.0 s de audio en 9.1 s\n";
    uint32_t n = yt_voz_text(raw, o, sizeof o, inf, sizeof inf);
    CHECK_STR(o, "[0:00] Hola a todos. Hoy vemos SOMA.\n[0:21] Segunda parte.\n[1:01:02] Muy tarde.");
    CHECK(n == strlen(o)); CHECK_STR(inf, "70.0 s de audio en 9.1 s");
    yt_voz_text("uno\ndos\n\n[musica] tres\n", o, sizeof o, inf, sizeof inf);     /* sin tiempos (voz sin -T): se une tal cual */
    CHECK_STR(o, "uno dos [musica] tres"); CHECK_STR(inf, "");
    yt_voz_text("[voz] no se oye ninguna voz\n", o, sizeof o, inf, sizeof inf);
    CHECK_STR(o, ""); CHECK_STR(inf, "no se oye ninguna voz");
    char small[16]; n = yt_voz_text(raw, small, sizeof small, inf, 6);
    CHECK(n == 15 && strlen(small) == 15 && strlen(inf) == 5);
    for (int k = 0; k < 3000; ++k) {
        char z[200]; uint32_t L = rnd() % (sizeof z - 1);
        for (uint32_t i = 0; i < L; ++i) { uint32_t r = rnd(); z[i] = (char)(r % 3 == 0 ? (uint32_t)(unsigned char)"[]:0123456789.-> \nvoz"[r % 21] : 'a' + r % 26); }
        z[L] = 0;
        uint32_t cap = 1 + rnd() % 120, icap = 1 + rnd() % 40;
        char *d = (char *)malloc(cap), *e = (char *)malloc(icap);
        uint32_t w = yt_voz_text(z, d, cap, e, icap);
        CHECK(w < cap && d[w] == 0 && strlen(e) < icap);
        free(d); free(e);
    }
    uint8_t wav[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x80,0x3E,0,0 };
    uint32_t rate = 0;
    CHECK_EQ_INT(yt_wav_head(wav, 44, &rate), 1); CHECK_EQ_INT(rate, 16000);
    CHECK_EQ_INT(yt_wav_head(wav, 43, &rate), 0);
    wav[9] = 'x'; CHECK_EQ_INT(yt_wav_head(wav, 44, &rate), 0);
}

int main(void)
{
    test_resultados();
    test_conf();
    test_rutas();
    test_info();
    test_plain();
    test_busquedas();
    test_mezcla();
    test_contexto();
    test_whisper();
    TEST_MAIN_END("youtube_ia_251");
}
