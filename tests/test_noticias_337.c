/* SOMA_337_NOTICIAS: el lector de RSS/Atom y las piezas puras de la aplicacion de Noticias (src/apps/noticias_rss.c). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "noticias_rss.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)
static struct nt_item it[64];

static const char GNEWS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><rss version=\"2.0\" xmlns:media=\"http://search.yahoo.com/mrss/\">"
    "<channel><generator>NFE/5.0</generator><title>Titulares principales - Google Noticias</title><link>https://news.google.com/?hl=es</link>"
    "<item><title>El Gobierno aprueba la nueva ley de vivienda - EL PA\303\215S</title>"
    "<link>https://news.google.com/rss/articles/CBMiAAA?oc=5</link><guid isPermaLink=\"false\">CBMiAAA</guid>"
    "<pubDate>Tue, 25 Nov 2026 17:30:00 GMT</pubDate>"
    "<description>&lt;ol&gt;&lt;li&gt;&lt;a href=\"https://news.google.com/rss/articles/X\" target=\"_blank\"&gt;El Gobierno aprueba la nueva ley de vivienda&lt;/a&gt;&amp;nbsp;&amp;nbsp;&lt;font color=\"#6f6f6f\"&gt;EL PA\303\215S&lt;/font&gt;&lt;/li&gt;&lt;/ol&gt;</description>"
    "<source url=\"https://elpais.com\">EL PA\303\215S</source></item>"
    "<item><title>Alcaraz gana en Turin y cierra el ano como numero uno - Marca</title>"
    "<link>https://news.google.com/rss/articles/CBMiBBB?oc=5</link>"
    "<pubDate>Tue, 25 Nov 2026 19:05:00 GMT</pubDate><description>Resumen del partido &amp; reacciones</description>"
    "<source url=\"https://www.marca.com\">Marca</source></item>"
    "</channel></rss>";

static const char RSS[] =
    "<rss><channel><title>Agencia de prueba</title>"
    "<item><title><![CDATA[La NASA confirma agua en <b>Marte</b> & m\303\241s]]></title>"
    "<link> https://agencia.example.com/ciencia/marte </link>"
    "<pubDate>Wed, 26 Nov 2026 08:00:00 +0100</pubDate>"
    "<description><![CDATA[<p>El rover <i>Perseverance</i> encontr&oacute; minerales.</p><img src=\"x.jpg\"/>]]></description>"
    "<dc:creator>Ana P&#233;rez</dc:creator></item>"
    "<item><title>Sin fecha ni enlace</title><description>texto &#x41;&#66;</description></item>"
    "<item><title></title><link>https://x.example.com/vacia</link></item>"
    "</channel></rss>";

static const char ATOM[] =
    "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Blog Atom</title>"
    "<entry><title type=\"html\">Version 2.0 &amp;quot;estable&amp;quot;</title>"
    "<link rel=\"replies\" href=\"https://blog.example.org/c/1\"/><link rel=\"alternate\" type=\"text/html\" href=\"https://blog.example.org/v2\"/>"
    "<updated>2026-11-24T10:15:00+02:00</updated><summary>Novedades de la version.</summary><author><name>Equipo</name></author></entry>"
    "<entry><title>Otra</title><link href=\"https://blog.example.org/otra\"/><published>2026-11-20</published></entry>"
    "</feed>";

int main(void)
{
    /* Google Noticias */
    int n = nt_rss(GNEWS, sizeof GNEWS - 1, it, 64, 0);
    CK(n == 2);
    CK(!strcmp(it[0].tit, "El Gobierno aprueba la nueva ley de vivienda") && !strcmp(it[0].fuente, "EL PA\303\215S"));
    CK(!strcmp(it[0].url, "https://news.google.com/rss/articles/CBMiAAA?oc=5"));
    CK(it[0].t == ((int64_t)20782 * 24 + 17) * 60 + 30);             /* 2026-11-25 = dia 20782 */
    CK(it[0].desc[0] == 0);                                          /* la descripcion solo repetia el titular */
    CK(!strcmp(it[1].tit, "Alcaraz gana en Turin y cierra el ano como numero uno") && !strcmp(it[1].fuente, "Marca"));
    CK(!strcmp(it[1].desc, "Resumen del partido & reacciones"));
    /* RSS clasico */
    n = nt_rss(RSS, sizeof RSS - 1, it, 64, 0);
    CK(n == 2);                                                       /* la de titulo vacio no cuenta */
    CK(!strcmp(it[0].tit, "La NASA confirma agua en Marte & m\303\241s"));
    CK(!strcmp(it[0].url, "https://agencia.example.com/ciencia/marte"));
    CK(it[0].t == ((int64_t)20783 * 24 + 7) * 60);                   /* 08:00 +0100 = 07:00 UTC */
    CK(!strcmp(it[0].desc, "El rover Perseverance encontr\303\263 minerales."));
    CK(!strcmp(it[0].fuente, "Ana P\303\251rez"));
    CK(!strcmp(it[1].tit, "Sin fecha ni enlace") && it[1].t == 0 && it[1].url[0] == 0 && !strcmp(it[1].desc, "texto AB"));
    CK(!strcmp(it[1].fuente, "Agencia de prueba"));                   /* el nombre del canal */
    CK(nt_rss(RSS, sizeof RSS - 1, it, 64, "Medio") == 2 && !strcmp(it[1].fuente, "Medio"));
    /* Atom */
    n = nt_rss(ATOM, sizeof ATOM - 1, it, 64, 0);
    CK(n == 2);
    CK(!strcmp(it[0].tit, "Version 2.0 \"estable\"") && !strcmp(it[0].url, "https://blog.example.org/v2"));
    CK(it[0].t == ((int64_t)20781 * 24 + 8) * 60 + 15 && !strcmp(it[0].fuente, "Equipo") && !strcmp(it[0].desc, "Novedades de la version."));
    CK(!strcmp(it[1].url, "https://blog.example.org/otra") && it[1].t == (int64_t)20777 * 1440);
    /* fechas */
    CK(nt_fecha("Tue, 25 Nov 26 17:30:00 GMT") == ((int64_t)20782 * 24 + 17) * 60 + 30);
    CK(nt_fecha("25 Nov 2026 12:30 EST") == ((int64_t)20782 * 24 + 17) * 60 + 30);
    CK(nt_fecha("2026-11-25T18:30:00.123+01:00") == ((int64_t)20782 * 24 + 17) * 60 + 30);
    CK(nt_fecha("2026-11-25T17:30Z") == ((int64_t)20782 * 24 + 17) * 60 + 30);
    CK(nt_fecha("ayer") == 0 && nt_fecha("") == 0 && nt_fecha(0) == 0 && nt_fecha("2026-13-01") == 0 && nt_fecha("Mon, 32 Jan 2026 10:00 GMT") == 0);
    /* limpiar y ordenar */
    struct nt_item v[5]; memset(v, 0, sizeof v);
    strcpy(v[0].tit, "Uno"); v[0].t = 10; strcpy(v[1].tit, "Dos"); v[1].t = 30; strcpy(v[2].tit, "  uno!! "); v[2].t = 50;
    strcpy(v[3].tit, "Tres"); v[3].t = 20; strcpy(v[3].url, "https://a.example/x"); strcpy(v[4].tit, "Cuatro"); strcpy(v[4].url, "https://a.example/x");
    n = nt_limpia(v, 5);
    CK(n == 3 && !strcmp(v[0].tit, "Dos") && !strcmp(v[1].tit, "Tres") && !strcmp(v[2].tit, "Uno"));
    /* hace... */
    char h[32]; int64_t ya = (int64_t)20782 * 1440 + 1200;
    nt_hace(ya, ya - 5, h, sizeof h); CK(!strcmp(h, "hace 5 min"));
    nt_hace(ya, ya - 185, h, sizeof h); CK(!strcmp(h, "hace 3 h"));
    nt_hace(ya, ya - 1500, h, sizeof h); CK(!strcmp(h, "ayer"));
    nt_hace(ya, ya - 4 * 1440, h, sizeof h); CK(!strcmp(h, "hace 4 dias"));
    nt_hace(ya, (int64_t)20765 * 1440, h, sizeof h); CK(!strcmp(h, "08/11"));
    nt_hace(ya, 0, h, sizeof h); CK(!h[0]);
    /* categorias y configuracion */
    char u[400];
    CK(nt_cat_url(NTC_DEPORTES, 0, u, sizeof u) && !strcmp(u, "https://news.google.com/rss/headlines/section/topic/SPORTS?hl=es&gl=ES&ceid=ES:es"));
    CK(nt_cat_url(NTC_LOCAL, "", u, sizeof u) == 0);
    CK(nt_cat_url(NTC_LOCAL, "Alcal\303\241 de Henares", u, sizeof u) && !strcmp(u, "https://news.google.com/rss/search?q=Alcal%C3%A1+de+Henares+when%3A2d&hl=es&gl=ES&ceid=ES:es"));
    CK(nt_cat_busca("Espa\303\261a") == NTC_ESPANA && nt_cat_busca("tecno") == NTC_TECNO && nt_cat_busca("DEPORTES") == NTC_DEPORTES && nt_cat_busca("xx") == -1);
    struct nt_conf c; char t[2048];
    nt_conf_lee(&c, "categorias=portada,local,salud\nlugar=  Valencia \nfuente=https://feeds.example.com/a.xml\nfuente=ftp://mal\nfuente=http://b.example/rss\n");
    CK(c.cats == ((1u << NTC_PORTADA) | (1u << NTC_LOCAL) | (1u << NTC_SALUD)) && !strcmp(c.lugar, "Valencia") && c.nextra == 2);
    nt_conf_escribe(&c, t, sizeof t);
    struct nt_conf c2; nt_conf_lee(&c2, t);
    CK(c2.cats == c.cats && !strcmp(c2.lugar, c.lugar) && c2.nextra == 2 && !strcmp(c2.extra[1], "http://b.example/rss"));
    nt_conf_lee(&c, 0); CK(c.cats & (1u << NTC_PORTADA));
    /* hostil: nada se sale (con ASan) */
    static char b[20000];
    uint32_t x = 337;
    static const char *const P[] = { "<item>", "</item>", "<entry>", "</entry>", "<title>", "</title>", "<link>", "</link>", "<link href=\"", "\"/>",
        "<![CDATA[", "]]>", "&amp;", "&#", "&#x", ";", "<pubDate>", "</pubDate>", "<description>", "</description>", "<source>", "</source>", " - ", "https://", "<", ">", "\"", "'", "&" };
    for (int r = 0; r < 4000; r++) {
        uint32_t L = 0, lim = 1 + (x % (sizeof b - 64));
        while (L < lim) {
            x = x * 1103515245u + 12345u;
            if ((x >> 16) % 3) { const char *p = P[(x >> 8) % (sizeof P / sizeof P[0])]; while (*p && L < lim) b[L++] = *p++; }
            else b[L++] = (char)(x >> 20);
        }
        int k = nt_rss(b, L, it, 64, (x & 1) ? "F" : 0);
        if (k < 0 || k > 64) { CK(0); break; }
        for (int i = 0; i < k; i++) if (strlen(it[i].tit) >= NT_TIT || strlen(it[i].url) >= NT_URL || strlen(it[i].fuente) >= NT_FTE || strlen(it[i].desc) >= NT_DESC) { CK(0); r = 99999; break; }
        nt_limpia(it, k);
        nt_sanea(it, k);
        b[L < sizeof b ? L : sizeof b - 1] = 0; (void)nt_fecha(b);
        struct nt_conf cc; nt_conf_lee(&cc, b); if (cc.nextra > NT_EXTRA) { CK(0); break; }
    }
    printf("test_noticias_337: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
