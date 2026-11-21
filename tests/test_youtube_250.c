/* SOMA_250_YOUTUBE: pruebas en el anfitrion de la parte pura de la aplicacion YouTube (src/apps/yt_proto.h):
 * resultados de busqueda, cabeceras HTTP, direcciones, configuracion y, sobre todo, yt_ring (la ventana de memoria del video
 * que va llegando), con un "reproductor" simulado que lee con dos cursores (video y audio) mientras la descarga escribe.
 * Tambien datos al azar: nada de lo que llega de la red puede hacer leer o escribir fuera de un bufer (ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define YT_BLK 4096u                 /* bloques pequenos: las vueltas del anillo se ejercitan con pocos datos */
#define YT_HDR_KEEP 1024u
#include "../src/apps/yt_proto.h"
#include "soma_test.h"

static uint64_t seed = 250;
static uint32_t rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(seed >> 33); }
static uint8_t byte_at(uint64_t off) { return (uint8_t)((off * 2654435761u) >> 11 ^ off); }

static void test_resultados(void)
{
    static const char ok[] = "SOMAYT 1 3\n"
        "dQw4w9WgXcQ\t213\tNever Gonna Give You Up\tRick Astley\t1500000000\n"
        "aqz-KE-bpKQ\t634\tBig Buck Bunny 60fps 4K\tBlender\t27000\n"
        "___________\t0\tDirecto sin duracion\n";
    struct yt_item it[YT_MAX_ITEMS];
    int n = yt_parse_results(ok, sizeof ok - 1, it, YT_MAX_ITEMS);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(it[0].id, "dQw4w9WgXcQ"); CHECK_EQ_INT(it[0].dur, 213); CHECK_STR(it[0].title, "Never Gonna Give You Up");
    CHECK_STR(it[0].channel, "Rick Astley"); CHECK_EQ_INT(it[0].views, 1500000000u);
    CHECK_STR(it[1].channel, "Blender"); CHECK_EQ_INT(it[1].views, 27000);
    CHECK_STR(it[2].channel, ""); CHECK_EQ_INT(it[2].dur, 0);
    CHECK_EQ_INT(yt_parse_results(ok, sizeof ok - 1, it, 2), 2);                       /* no pasa del maximo */
    CHECK_EQ_INT(yt_parse_results("<html>error</html>", 18, it, YT_MAX_ITEMS), -1);    /* no es el servicio */
    CHECK_EQ_INT(yt_parse_results("SOMAYT 1 0\n", 11, it, YT_MAX_ITEMS), 0);
    static const char malas[] = "SOMAYT 1 4\n"
        "corto\t1\tx\n"                                   /* identificador corto */
        "dQw4w9WgXc!\t1\tcaracter no valido\n"
        "dQw4w9WgXcQ\n"                                   /* sin campos */
        "dQw4w9WgXcQ\t99999999999999\t\t\t\n";            /* numero enorme y titulo vacio */
    n = yt_parse_results(malas, sizeof malas - 1, it, YT_MAX_ITEMS);
    CHECK_EQ_INT(n, 1); CHECK_EQ_INT(it[0].dur, 0xFFFFFFFFu); CHECK_STR(it[0].title, "(sin titulo)");
    char largo[600]; int k = snprintf(largo, sizeof largo, "SOMAYT 1 1\naaaaaaaaaaa\t5\t");
    for (int i = 0; i < 400; ++i) largo[k++] = 'T';
    largo[k++] = '\t'; for (int i = 0; i < 100; ++i) largo[k++] = 'C';
    n = yt_parse_results(largo, (uint32_t)k, it, YT_MAX_ITEMS);
    CHECK_EQ_INT(n, 1); CHECK_EQ_INT(strlen(it[0].title), sizeof it[0].title - 1); CHECK_EQ_INT(strlen(it[0].channel), sizeof it[0].channel - 1);
    CHECK(yt_id_ok("dQw4w9WgXcQ")); CHECK(!yt_id_ok("dQw4w9WgXc")); CHECK(!yt_id_ok("dQw4w9WgXcQQ")); CHECK(!yt_id_ok("dQw4w9 gXcQ")); CHECK(!yt_id_ok("../../etc/x"));
}

static void test_http(void)
{
    int st = 0; uint32_t off = 0;
    static const char a[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\ncuerpo";
    CHECK_EQ_INT(yt_http_head(a, sizeof a - 1, &st, &off), 1); CHECK_EQ_INT(st, 200); CHECK(!strcmp(a + off, "cuerpo"));
    for (uint32_t cut = 0; cut + 6 < sizeof a - 1; ++cut) CHECK_EQ_INT(yt_http_head(a, cut, &st, &off), 0);   /* a medias: faltan bytes */
    static const char b[] = "HTTP/1.1 403 Forbidden\r\n\r\nClave incorrecta";
    CHECK_EQ_INT(yt_http_head(b, sizeof b - 1, &st, &off), 1); CHECK_EQ_INT(st, 403);
    CHECK_EQ_INT(yt_http_head("SSH-2.0-OpenSSH\r\n\r\n", 19, &st, &off), -1);
    CHECK_EQ_INT(yt_http_head("HTTP/1.0 2x0 OK\r\n\r\n", 19, &st, &off), -1);
    CHECK_EQ_INT(yt_http_head("HTTP/1.0\r\n\r\n", 12, &st, &off), -1);
}

static void test_direcciones(void)
{
    uint8_t ip[4]; char h[64]; uint16_t po = 0;
    CHECK(yt_parse_ip("192.168.1.20", ip) && ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 20);
    CHECK(!yt_parse_ip("192.168.1", ip)); CHECK(!yt_parse_ip("192.168.1.256", ip)); CHECK(!yt_parse_ip("mac.local", ip));
    CHECK(!yt_parse_ip("1.2.3.4.5", ip)); CHECK(!yt_parse_ip("1..2.3", ip)); CHECK(!yt_parse_ip("", ip)); CHECK(!yt_parse_ip("0001.2.3.4", ip));
    CHECK(yt_host_port("192.168.1.20", h, sizeof h, &po) && !strcmp(h, "192.168.1.20") && po == YT_PORT);
    CHECK(yt_host_port("  mac-de-marcos.local:9000 ", h, sizeof h, &po) && !strcmp(h, "mac-de-marcos.local") && po == 9000);
    CHECK(!yt_host_port("", h, sizeof h, &po)); CHECK(!yt_host_port("a b", h, sizeof h, &po)); CHECK(!yt_host_port("host:0", h, sizeof h, &po));
    CHECK(!yt_host_port("host:70000", h, sizeof h, &po)); CHECK(!yt_host_port("host:", h, sizeof h, &po)); CHECK(!yt_host_port("ho/st", h, sizeof h, &po));
    CHECK(!yt_host_port("h\r\nX-Inyectada: 1", h, sizeof h, &po));
    char e[64];
    yt_urlenc("gatos y perros & ñ?", e, sizeof e); CHECK_STR(e, "gatos%20y%20perros%20%26%20%C3%B1%3F");
    char corto[8]; uint32_t n = yt_urlenc("a b c d", corto, sizeof corto); CHECK(n < sizeof corto && corto[n] == 0); CHECK_STR(corto, "a%20b");
    struct yt_conf c; yt_conf_default(&c);
    char p[400];
    yt_path_search(p, sizeof p, &c, "big buck bunny", 12); CHECK_STR(p, "/yt/search?q=big%20buck%20bunny&n=12");
    yt_scopy(c.key, sizeof c.key, "se cr&to"); c.height = 480; c.fps = 20;
    yt_path_video(p, sizeof p, &c, "aqz-KE-bpKQ", 95); CHECK_STR(p, "/yt/video?id=aqz-KE-bpKQ&t=95&h=480&fps=20&k=se%20cr%26to");
    yt_path_thumb(p, sizeof p, &c, "aqz-KE-bpKQ"); CHECK_STR(p, "/yt/thumb?id=aqz-KE-bpKQ&k=se%20cr%26to");
    char t[16];
    yt_fmt_time(t, sizeof t, 0); CHECK_STR(t, "0:00"); yt_fmt_time(t, sizeof t, 65); CHECK_STR(t, "1:05");
    yt_fmt_time(t, sizeof t, 3600 + 125); CHECK_STR(t, "1:02:05"); yt_fmt_time(t, sizeof t, 599); CHECK_STR(t, "9:59");
    char v[64];
    yt_fmt_views(v, sizeof v, 1); CHECK_STR(v, "1 visualizacion"); yt_fmt_views(v, sizeof v, 999); CHECK_STR(v, "999 visualizaciones");
    yt_fmt_views(v, sizeof v, 34500); CHECK_STR(v, "34 mil visualizaciones"); yt_fmt_views(v, sizeof v, 1250000); CHECK_STR(v, "1,2 M de visualizaciones");
    yt_fmt_views(v, sizeof v, 1500000000u); CHECK_STR(v, "1500 M de visualizaciones");
}

static void test_conf(void)
{
    struct yt_conf c;
    static const char txt[] = "# comentario\nservidor = 192.168.1.20:8100\r\nclave=hola mundo \ncalidad=480\nfps=24\notra=cosa\n";
    yt_conf_parse(txt, sizeof txt - 1, &c);
    CHECK_STR(c.host, "192.168.1.20"); CHECK_EQ_INT(c.port, 8100); CHECK_STR(c.key, "hola mundo"); CHECK_EQ_INT(c.height, 480); CHECK_EQ_INT(c.fps, 24);
    char out[400]; uint32_t n = yt_conf_format(&c, out, sizeof out);
    struct yt_conf d; yt_conf_parse(out, n, &d);                                      /* ida y vuelta */
    CHECK_STR(d.host, c.host); CHECK_EQ_INT(d.port, c.port); CHECK_STR(d.key, c.key); CHECK_EQ_INT(d.height, c.height); CHECK_EQ_INT(d.fps, c.fps);
    static const char malo[] = "servidor=bad host\ncalidad=1080\nfps=500\nclave=\n";
    yt_conf_parse(malo, sizeof malo - 1, &c);                                         /* valores no validos: se quedan los de fabrica */
    CHECK_STR(c.host, ""); CHECK_EQ_INT(c.port, YT_PORT); CHECK_EQ_INT(c.height, 360); CHECK_EQ_INT(c.fps, 15); CHECK_STR(c.key, "");
    yt_conf_parse("", 0, &c); CHECK_EQ_INT(c.height, 360);
    for (int it = 0; it < 3000; ++it) {                                               /* texto al azar */
        char b[300]; uint32_t l = rnd() % sizeof b;
        for (uint32_t i = 0; i < l; ++i) { uint32_t r = rnd(); b[i] = (r % 7 == 0) ? '\n' : (r % 5 == 0) ? '=' : (char)r; }
        yt_conf_parse(b, l, &c);
        CHECK(c.port != 0 && yt_height_ok(c.height) && c.fps >= 10 && c.fps <= 30 && strlen(c.host) < sizeof c.host && strlen(c.key) < sizeof c.key);
    }
}

static void ring_new(struct yt_ring *r, uint32_t nblk, uint32_t margin)
{
    memset(r, 0, sizeof *r);
    for (uint32_t i = 0; i < nblk; ++i) r->blk[i] = malloc(YT_BLK);
    r->nblk = nblk; r->margin = margin; r->hdr = malloc(YT_HDR_KEEP);
    yt_ring_reset(r, 0);
}
static void ring_del(struct yt_ring *r) { for (uint32_t i = 0; i < r->nblk; ++i) free(r->blk[i]); free(r->hdr); }

static void test_ring_basico(void)
{
    struct yt_ring r; ring_new(&r, 4, 2000);
    uint8_t b[20000], o[20000];
    for (uint32_t i = 0; i < sizeof b; ++i) b[i] = byte_at(i);
    CHECK_EQ_INT(yt_ring_cap(&r), 4 * YT_BLK);
    CHECK_EQ_INT(yt_ring_put(&r, b, 10000), 10000);
    CHECK_EQ_INT(yt_ring_put(&r, b + 10000, 10000), 4 * YT_BLK - 10000);              /* solo lo que cabe */
    CHECK_EQ_INT(yt_ring_space(&r), 0);
    CHECK_EQ_INT(yt_ring_read(&r, 0, o, 16384), 16384); CHECK(!memcmp(o, b, 16384));
    CHECK_EQ_INT(yt_ring_read(&r, 16000, o, 385), -1);                                /* aun no ha llegado */
    CHECK_EQ_INT(yt_ring_read(&r, ~(uint64_t)0 - 3, o, 16), -1);                      /* desbordamiento de off + n */
    CHECK_EQ_INT(yt_ring_trim(&r, 100), 0);                                           /* sin dos ventanas de lecturas no se suelta nada */
    /* el reproductor avanza: lee por 9000 durante dos ventanas de tiempo */
    CHECK_EQ_INT(yt_ring_read(&r, 9000, o, 100), 100);
    yt_ring_trim(&r, YT_TRIM_WINDOW_MS + 1);                                          /* rota: la ventana anterior incluye la lectura en 0 */
    CHECK_EQ_INT(r.base, 0);
    CHECK_EQ_INT(yt_ring_read(&r, 9000, o, 100), 100);
    CHECK_EQ_INT(yt_ring_trim(&r, 2 * YT_TRIM_WINDOW_MS + 2), 7000);                  /* lo mas antiguo leido es 9000; menos el margen */
    CHECK_EQ_INT(r.base, 7000);
    CHECK_EQ_INT(yt_ring_read(&r, 6999, o, 10), -1);                                  /* ya soltado */
    CHECK_EQ_INT(yt_ring_read(&r, 7000, o, 9384), 9384); CHECK(!memcmp(o, b + 7000, 9384));
    CHECK_EQ_INT(yt_ring_read(&r, 0, o, YT_HDR_KEEP), YT_HDR_KEEP); CHECK(!memcmp(o, b, YT_HDR_KEEP));   /* la cabecera se conserva */
    CHECK_EQ_INT(yt_ring_read(&r, 100, o, YT_HDR_KEEP), -1);                          /* pero solo ella */
    CHECK_EQ_INT(yt_ring_put(&r, b + 16384, 3000), 3000);                             /* y vuelve a caber: da la vuelta */
    CHECK_EQ_INT(yt_ring_read(&r, 16000, o, 3384), 3384); CHECK(!memcmp(o, b + 16000, 3384));
    /* en pausa (nadie lee) no se suelta nada, pasen las ventanas que pasen */
    yt_ring_trim(&r, 10 * YT_TRIM_WINDOW_MS); uint64_t base = r.base;
    CHECK_EQ_INT(yt_ring_trim(&r, 20 * YT_TRIM_WINDOW_MS), 0); CHECK_EQ_INT(yt_ring_trim(&r, 30 * YT_TRIM_WINDOW_MS), 0); CHECK_EQ_INT(r.base, base);
    ring_del(&r);
    struct yt_ring z; memset(&z, 0, sizeof z);                                        /* sin memoria reservada: no acepta ni entrega nada */
    CHECK_EQ_INT(yt_ring_put(&z, b, 10), 0); CHECK_EQ_INT(yt_ring_read(&z, 0, o, 1), -1);
}

/* Descarga y reproduccion a la vez: quien descarga mete trozos de tamano al azar; el "reproductor" lee con un cursor de audio
 * adelantado y uno de video algo por detras (como media.c), a veces se para (pausa) y al reanudar rebobina un poco el audio.
 * Todo lo leido debe ser exacto y ninguna lectura del reproductor puede fallar por haberse soltado datos que aun necesitaba. */
static void test_ring_reproduccion(uint32_t nblk, uint32_t margin, uint64_t total)
{
    struct yt_ring r; ring_new(&r, nblk, margin);
    uint64_t wr = 0, va = 0, vv = 0, now = 0; int paused = 0; uint64_t perdidas = 0, bytes_leidos = 0;
    uint8_t in[2048], out[2048];
    for (long paso = 0; paso < 4000000 && (vv < total || va < total); ++paso) {
        now += 7;
        if (wr < total) {                                                             /* descarga */
            if (yt_ring_space(&r) < 512) yt_ring_trim(&r, now);
            uint32_t n = 1 + rnd() % sizeof in; if (n > total - wr) n = (uint32_t)(total - wr);
            uint64_t sp = yt_ring_space(&r); if (n > sp) n = (uint32_t)sp;
            for (uint32_t i = 0; i < n; ++i) in[i] = byte_at(wr + i);
            CHECK_EQ_INT(yt_ring_put(&r, in, n), n); wr += n;
        }
        if (rnd() % 400 == 0) { paused = !paused; if (!paused && va > 300) va -= rnd() % 300; }   /* reanudar: el audio rebobina un poco */
        if (paused) { if (rnd() % 50 == 0) now += 3 * YT_TRIM_WINDOW_MS; continue; }
        for (int k = 0; k < 2; ++k) {                                                 /* audio por delante (hasta 1500 bytes), video detras */
            uint64_t *cur = k ? &vv : &va;
            if (k == 1 && vv + 200 > va && va < total) continue;
            if (k == 0 && va > vv + 1500) continue;
            uint32_t n = 1 + rnd() % 700; if (n > total - *cur) n = (uint32_t)(total - *cur);
            if (!n || *cur + n > r.end) continue;                                     /* aun no ha llegado: el reproductor espera */
            int64_t g = yt_ring_read(&r, *cur, out, n);
            if (g != (int64_t)n) { perdidas++; *cur += n; continue; }
            for (uint32_t i = 0; i < n; ++i) if (out[i] != byte_at(*cur + i)) { perdidas++; break; }
            *cur += n; bytes_leidos += n;
        }
    }
    CHECK_EQ_INT(perdidas, 0);
    CHECK_EQ_INT(vv, total); CHECK_EQ_INT(va >= total, 1);
    CHECK(bytes_leidos >= 2 * total - 4096);
    CHECK(r.end - r.base <= yt_ring_cap(&r));
    ring_del(&r);
}

static void test_azar(void)
{
    struct yt_item it[YT_MAX_ITEMS]; char b[700];
    for (int n = 0; n < 20000; ++n) {
        uint32_t l = rnd() % sizeof b;
        for (uint32_t i = 0; i < l; ++i) { uint32_t r = rnd(); b[i] = r % 9 == 0 ? '\t' : r % 11 == 0 ? '\n' : (char)r; }
        if (n & 1 && l > 12) memcpy(b, "SOMAYT 1 9\n", 11);
        if (n % 4 == 3 && l > 30) memcpy(b + 11, "dQw4w9WgXcQ\t", 12);
        int k = yt_parse_results(b, l, it, YT_MAX_ITEMS);
        CHECK(k >= -1 && k <= YT_MAX_ITEMS);
        for (int i = 0; i < k; ++i) CHECK(yt_id_ok(it[i].id) && strlen(it[i].title) < sizeof it[i].title && strlen(it[i].channel) < sizeof it[i].channel);
        int st; uint32_t off = 0;
        if (n % 3 == 0 && l > 9) memcpy(b, "HTTP/1.0 ", 9);
        int h = yt_http_head(b, l, &st, &off);
        CHECK(h >= -1 && h <= 1); if (h == 1) CHECK(off <= l && st >= 0 && st <= 999);
        char e[40]; b[l ? l - 1 : 0] = 0; uint32_t en = yt_urlenc(b, e, sizeof e); CHECK(en < sizeof e && e[en] == 0);
        char hn[20]; uint16_t po; uint8_t ip[4];
        if (yt_host_port(b, hn, sizeof hn, &po)) CHECK(strlen(hn) < sizeof hn && po != 0);
        (void)yt_parse_ip(b, ip);
    }
}

int main(void)
{
    test_resultados();
    test_http();
    test_direcciones();
    test_conf();
    test_ring_basico();
    test_ring_reproduccion(4, 900, 300000);
    test_ring_reproduccion(16, 3000, 2000000);
    test_ring_reproduccion(5, 2500, 500000);
    test_azar();
    TEST_MAIN_END("youtube_250");
}
