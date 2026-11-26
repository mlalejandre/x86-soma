/* PARCHE 138: pruebas de los servicios aislados (sbx_selftest). Se lanzan con el comando 'sbx test' o en QEMU con
 *   -fw_cfg name=opt/soma/selftest,string=sbx   (tools/sbx_check.py), que ademas sale de QEMU con el resultado. */
#include "sandbox.h"
#include "usermode.h"
#include "pmm.h"
#include "mem.h"
#include "thread.h"
#include "console.h"
#include "web_img.h"
#include "image.h"
#include "sbx_testimg.h"
#include "jpeg.h"
#include "web_inflate.h"
#include "pdf.h"
#include "pdf_hash.h"
#include "correo_core.h"   /* SOMA_311_CORREO_AISLADO */
#include "correo_sbx.h"
#include "web_lee.h"       /* SOMA_312_WEB_AISLADA */
#include "busca_web.h"
#include "web_aislado.h"
#include "progz.h"         /* SOMA_314_PROGZ */
#include "yt_aislado.h"    /* SOMA_315_YOUTUBE_AISLADO */
PROGZ_DECL(websvc); PROGZ_DECL(vplay); PROGZ_DECL(python); PROGZ_DECL(jsrun); PROGZ_DECL(cc); PROGZ_DECL(voz);
int pdf_sbx_unpack_test(const uint8_t *src, uint32_t n);
int browser_prueba_cerrado(void);                           /* SOMA_318_NAVEGADOR_CERRADO: src/apps/browser.c */

uint64_t timer_get_uptime_ms(void);
extern uint32_t sbx_reserve_override;

#define EVIL _binary_sbxevil_elf_start, (size_t)(_binary_sbxevil_elf_end - _binary_sbxevil_elf_start)

static int bad;
static void ck(int ok, const char *what)
{
    kprint(ok ? "  OK    " : "  FALLO "); kprint(what); kprint("\n");
    if (!ok) bad++;
}
static void kdec64(int64_t v) { if (v < 0) { kprint("-"); v = -v; } kprint_dec((uint32_t)v); }

static int evil(uint32_t op, uint32_t heap, uint32_t ms, int64_t *code)
{
    static struct sbx_call c;                       /* 4 KiB+: fuera de la pila del hilo */
    sbx_prepare(&c, op, "x", 1, heap);
    int r = sbx_run(&c, EVIL, ms);
    *code = c.exit_code;
    sbx_release(&c);
    return r;
}

static size_t free_frames(void) { size_t f, u, t; pmm_get_stats(&f, &u, &t); return f; }

/* Marcos libres "netos": se descuenta lo que el monton del kernel haya mapeado (crece al alojar pixeles grandes y no devuelve
 * paginas: no es una fuga del servicio). */
static uint64_t heap_mapped(void) { struct kheap_counters k; kheap_get_counters(&k); return k.mapped_bytes / 4096u; }
static uint64_t g_mapped0;
static void leak_check(size_t f0, const char *what)
{
    thread_sleep(20);
    size_t f1 = free_frames();
    uint64_t grow = heap_mapped() - g_mapped0;
    kprint("  marcos libres antes/despues ("); kprint(what); kprint("): "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1);
    kprint(", crecimiento del monton del kernel "); kprint_dec((uint32_t)grow); kprint("\n");
    ck(f1 + grow + 8 >= f0, "sin fuga de memoria fisica");
    ck(kheap_check() == 0, "monton del kernel integro");
}

static uint32_t fnv(const uint32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) for (int b = 0; b < 4; ++b) { h ^= (uint8_t)(p[i] >> (8 * b)); h *= 16777619u; }
    return h;
}

/* ---- concurrencia: mas hilos que ranuras de proceso (USER_MAX_PROCS) ---- */
static volatile int conc_ok, conc_bad, conc_done;
static void conc_thread(void *arg)
{
    uintptr_t id = (uintptr_t)arg;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    uint8_t *buf = (uint8_t *)kmalloc(3000), *got = (uint8_t *)kmalloc(3000);
    for (int it = 0; it < 4; ++it) {
        if (!c || !buf || !got) { conc_bad++; break; }
        uint32_t n = 1000 + (uint32_t)(id * 97 + (uintptr_t)it * 31) % 2000;
        for (uint32_t i = 0; i < n; ++i) buf[i] = (uint8_t)(i * 7 + id + (uintptr_t)it);
        sbx_prepare(c, SBX_EVIL_ECHO, buf, n, 1u << 20);
        int r = sbx_run(c, EVIL, 5000), good = 0;
        if (r == 0 && c->out.data_len == n && sbx_copy_data(c, got, n) == 0) {
            good = 1; for (uint32_t i = 0; i < n; ++i) if (got[i] != buf[n - 1 - i]) { good = 0; break; }
        }
        sbx_release(c);
        if (good) conc_ok++; else conc_bad++;
    }
    if (c) kfree(c);
    if (buf) kfree(buf);
    if (got) kfree(got);
    conc_done++;
}

/* ---- SOMA_311_CORREO_AISLADO: el lector de correo aislado (mimesvc) da EXACTAMENTE lo mismo que correo_core.c en el nucleo,
 * con mensajes buenos y con cientos de mensajes mutilados; y el nucleo sobrevive a todos. */
static const char *const correo_muestras[] = {
    "From: =?ISO-8859-1?Q?Mar=EDa_L=F3pez?= <maria@ejemplo.es>\r\nTo: Ana <ana@ejemplo.es>, luis@ejemplo.es\r\n"
    "Cc: equipo@ejemplo.es\r\nSubject: =?UTF-8?B?UHJlc3VwdWVzdG8gZGUgbGEgcmVmb3JtYQ==?=\r\nDate: Tue, 24 Nov 2026 10:53:00 +0100\r\n"
    "Message-ID: <abc123@ejemplo.es>\r\nMIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"XYZ\"\r\n\r\n"
    "--XYZ\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n"
    "Hola Ana:\r\n\r\nTe env=EDo el presupuesto de la cocina. El ba=F1o va aparte.=\r\n Un saludo.\r\n\r\n> cita vieja\r\n-- \r\nMar=EDa\r\n"
    "--XYZ\r\nContent-Type: text/plain; name=\"presupuesto.txt\"\r\nContent-Disposition: attachment; filename=\"presupuesto.txt\"\r\n"
    "Content-Transfer-Encoding: base64\r\n\r\nQ29jaW5hOiA0LjIwMCBFVVIKQmHDsW86IDIuOTAwIEVVUgo=\r\n--XYZ--\r\n",
    "From: Tienda <ofertas@tienda.com>\r\nTo: luis@ejemplo.es\r\nSubject: Ofertas de la semana\r\nDate: Mon, 23 Nov 2026 08:00:00 +0000\r\n"
    "Content-Type: text/html; charset=utf-8\r\nContent-Transfer-Encoding: base64\r\n\r\n"
    "PGh0bWw+PGJvZHk+PGgxPlJlYmFqYXM8L2gxPjxwPkhhc3RhIGVsIDxiPjUwJTwvYj4gZW4gPGEg\r\n"
    "aHJlZj0iaHR0cHM6Ly90aWVuZGEuY29tIj50b2RhIGxhIHRpZW5kYTwvYT4uPC9wPjxwPiZuYnNwOyZhbXA7IG3DoXMuPC9wPjwvYm9keT48L2h0bWw+\r\n",
    "From: yo@ejemplo.es\r\nTo: yo@ejemplo.es\r\nSubject: nota\r\n\r\nsolo texto, sin MIME\r\n",
};
static int cad_igual(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int mime_igual(const struct co_mime *a, const char *ta, const struct co_mime *b, const char *tb)
{
    if (!cad_igual(a->asunto, b->asunto) || !cad_igual(a->de, b->de) || !cad_igual(a->para, b->para) || !cad_igual(a->cc, b->cc)) return 0;
    if (!cad_igual(a->msgid, b->msgid) || !cad_igual(a->refs, b->refs) || !cad_igual(a->responder_a, b->responder_a)) return 0;
    if (a->fecha != b->fecha || a->texto_n != b->texto_n || a->html != b->html || a->nadj != b->nadj || a->truncado != b->truncado) return 0;
    if (memcmp(ta, tb, a->texto_n) != 0) return 0;
    for (int i = 0; i < a->nadj; ++i) {
        const struct co_adj *x = &a->adj[i], *y = &b->adj[i];
        if (!cad_igual(x->nombre, y->nombre) || !cad_igual(x->tipo, y->tipo) || x->off != y->off || x->len != y->len || x->cte != y->cte || x->tam != y->tam || x->en_linea != y->en_linea) return 0;
    }
    return 1;
}
static int ficha_igual(const struct co_msg *a, const struct co_msg *b)
{
    return cad_igual(a->de, b->de) && cad_igual(a->de_email, b->de_email) && cad_igual(a->para, b->para) && cad_igual(a->asunto, b->asunto)
        && cad_igual(a->extracto, b->extracto) && cad_igual(a->msgid, b->msgid) && a->fecha == b->fecha && a->adj == b->adj
        && a->uid == b->uid && a->flags == b->flags && a->tam == b->tam;
}
static int correo_compara(const char *raw, uint32_t n, struct co_mime *m1, struct co_mime *m2, char *t1, char *t2, uint32_t cap)
{
    memset(m1, 0, sizeof *m1); memset(m2, 0, sizeof *m2);
    int a = co_mime_lee(raw, n, m1, t1, cap);              /* el de siempre, en el nucleo */
    correo_mime_sanea(m1, n, cap); t1[m1->texto_n] = 0;
    int b = correo_mime_lee(raw, n, m2, t2, cap);          /* el aislado */
    return a == b && mime_igual(m1, t1, m2, t2);
}
static void sbx_correo_test(void)
{
    kprint(" [correo aislado (mimesvc, parche 311) vs correo_core.c en el nucleo]\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    const uint32_t cap = 64u * 1024u;
    struct co_mime *m1 = (struct co_mime *)kmalloc(sizeof *m1), *m2 = (struct co_mime *)kmalloc(sizeof *m2);
    char *t1 = (char *)kmalloc(cap), *t2 = (char *)kmalloc(cap), *buf = (char *)kmalloc(8192);
    uint8_t *a1 = (uint8_t *)kmalloc(4096), *a2 = (uint8_t *)kmalloc(4096);
    if (!m1 || !m2 || !t1 || !t2 || !buf || !a1 || !a2) { ck(0, "memoria para la prueba del correo"); goto fin; }
    for (int i = 0; i < 3; ++i) {
        const char *raw = correo_muestras[i]; uint32_t n = co_len(raw);
        ck(correo_compara(raw, n, m1, m2, t1, t2, cap), i == 0 ? "mensaje con adjunto, latin1 y RFC 2047: igual que en el nucleo" :
                                                       i == 1 ? "mensaje HTML en base64: igual que en el nucleo" : "mensaje sin MIME: igual que en el nucleo");
        if (i == 0) {
            ck(m2->nadj >= 1 && co_ieq(m2->adj[m2->nadj - 1].nombre, "presupuesto.txt") == 1, "  y ve el adjunto");
            const struct co_adj *ad = &m2->adj[m2->nadj - 1];
            uint32_t x = co_adj_saca(raw, n, ad, a1, 4096), y = correo_adj_saca(raw, n, ad, a2, 4096);
            ck(x > 0 && x == y && memcmp(a1, a2, x) == 0, "  adjunto decodificado en el servicio: identico");
            ck(correo_adj_saca(raw, n, ad, a2, 5) == 5, "  adjunto con cap pequeno: nunca devuelve mas de lo pedido");
            struct co_adj malo = *ad; malo.off = n - 2; malo.len = 100;
            ck(correo_adj_saca(raw, n, &malo, a2, 4096) == 0, "  adjunto fuera del mensaje: rechazado");
        }
        /* la ficha de la lista: cabeceras + principio del cuerpo */
        const char *sep = 0; for (uint32_t k = 0; k + 3 < n; ++k) if (raw[k] == '\r' && raw[k + 1] == '\n' && raw[k + 2] == '\r' && raw[k + 3] == '\n') { sep = raw + k + 4; break; }
        if (sep) {
            uint32_t cn = (uint32_t)(sep - raw);
            struct co_msg f1, f2; memset(&f1, 0, sizeof f1); memset(&f2, 0, sizeof f2);
            f1.uid = f2.uid = 77; f1.flags = f2.flags = 3; f1.tam = f2.tam = n;
            co_ficha(&f1, raw, cn, sep, n - cn); correo_msg_sanea(&f1);
            correo_ficha(&f2, raw, cn, sep, n - cn);
            ck(ficha_igual(&f1, &f2), "  ficha de la lista igual (uid, banderas y tamano conservados)");
        }
    }
    /* mensajes mutilados: bytes cambiados, cortes y basura; el aislado y el del nucleo tienen que coincidir siempre */
    {
        uint32_t semilla = 0x311C0DEu, iguales = 0, total = 0;
        uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 300; ++it) {
            const char *base = correo_muestras[it % 3]; uint32_t n = co_len(base);
            memcpy(buf, base, n);
            int cambios = 1 + (int)((semilla >> 7) % 12u);
            for (int c = 0; c < cambios; ++c) {
                semilla = semilla * 1103515245u + 12345u;
                uint32_t pos = (semilla >> 8) % n;
                static const char raros[] = "=?\r\n\";:<>-B0\200\377";
                buf[pos] = (semilla & 1u) ? raros[(semilla >> 3) % (sizeof raros - 1)] : (char)(semilla >> 16);
            }
            if ((semilla & 7u) == 0) n = 1 + (semilla >> 12) % n;           /* corte */
            buf[n] = 0;
            ++total;
            if (correo_compara(buf, n, m1, m2, t1, t2, cap)) ++iguales;
        }
        kprint("  300 mensajes mutilados: "); kprint_dec(iguales); kprint(" iguales en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(iguales == total, "mensajes mutilados: el aislado da lo mismo y el nucleo sobrevive");
    }
    /* coste de una ficha (lo que se paga por cada correo al sincronizar la lista) */
    {
        const char *raw = correo_muestras[0]; uint32_t n = co_len(raw);
        uint64_t t0 = timer_get_uptime_ms();
        for (int i = 0; i < 100; ++i) { struct co_msg f; memset(&f, 0, sizeof f); correo_ficha(&f, raw, n, 0, 0); }
        uint64_t dt = timer_get_uptime_ms() - t0;
        kprint("  100 fichas: "); kprint_dec((uint32_t)dt); kprint(" ms\n");
        ck(dt < 20000, "100 fichas en menos de 20 s (en QEMU sin KVM)");
    }
    /* si el servicio no esta (se ha parado), la siguiente llamada lo vuelve a arrancar */
    sbx_svc_stop_all();
    ck(correo_compara(correo_muestras[2], co_len(correo_muestras[2]), m1, m2, t1, t2, cap), "tras parar los servicios, el correo se sigue leyendo");
fin:
    if (m1) kfree(m1);
    if (m2) kfree(m2);
    if (t1) kfree(t1);
    if (t2) kfree(t2);
    if (buf) kfree(buf);
    if (a1) kfree(a1);
    if (a2) kfree(a2);
    sbx_svc_stop_all();
    leak_check(f0, "correo");
}

/* ---- SOMA_312_WEB_AISLADA: el lector web aislado (leesvc) da lo mismo que web_lee.c / busca_web.c en el nucleo, y el nucleo
 * sobrevive a paginas mutiladas. */
static const char *const web_muestras[] = {
    "<!doctype html><html><head><title>La imprenta de Gutenberg &ndash; Historia</title><style>p{color:red}</style>"
    "<script>var x = '<p>no</p>'; document.write(x);</script></head><body><nav><a href=/>Inicio</a><a href=/b>Blog</a></nav>"
    "<header>Cabecera del sitio</header><article><h1>La imprenta</h1><p>Johannes Gutenberg desarroll&oacute; hacia 1450 la imprenta de "
    "tipos m&oacute;viles en Maguncia, lo que abarat&oacute; los libros y aceler&oacute; la difusi&oacute;n del conocimiento en Europa.</p>"
    "<ul><li>Primer libro impreso: la Biblia de 42 l&iacute;neas.</li><li>Tipos de una aleaci&oacute;n de plomo, esta\303\261o y antimonio.</li></ul>"
    "<p>Su invento se extendi&oacute; en pocas d&eacute;cadas a m&aacute;s de doscientas ciudades &amp; cambi&oacute; la ciencia.</p></article>"
    "<footer>Aviso legal | Cookies</footer><form><input name=q><button>Buscar</button></form></body></html>",
    "{\"batchcomplete\":\"\",\"query\":{\"search\":[{\"ns\":0,\"title\":\"Primera Guerra Mundial\",\"snippet\":\"La <span class=\\\"searchmatch\\\">Primera</span> "
    "Guerra Mundial fue un conflicto b\\u00e9lico &quot;global&quot;\"},{\"ns\":0,\"title\":\"Batalla del Marne\",\"snippet\":\"Batalla de 1914\"}]}}",
    "<table><tr><td>1.&nbsp;</td><td><a rel=\"nofollow\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fes.wikipedia.org%2Fwiki%2FGuerra&amp;rut=x\" class='result-link'>"
    "Primera <b>Guerra</b> Mundial - Wikipedia</a></td></tr><tr><td>&nbsp;</td><td class='result-snippet'>Conflicto militar de 1914 a 1918 &amp; m&aacute;s.</td></tr>"
    "<tr><td>2.</td><td><a rel=\"nofollow\" href=\"https://historia.example/ww1\" class='result-link'>Historia</a></td></tr></table>",
};
static int web_compara(int tipo, const char *s, uint32_t n, char *o1, char *o2, uint32_t cap, char *t1, char *t2)
{
    if (tipo == 0) {
        t1[0] = t2[0] = 0;
        uint32_t a = wl_texto(s, n, o1, cap, t1, 160);
        a = web_aislado_sanea(o1, a, cap);
        uint32_t b = wl_texto_aislado(s, n, o2, cap, t2, 160);
        return a == b && memcmp(o1, o2, a) == 0 && cad_igual(t1, t2);
    }
    int a = tipo == 1 ? bw_parse_wikipedia(s, "es", o1, cap, 5) : bw_parse_ddg_lite(s, o1, cap, 5);
    int b = tipo == 1 ? bw_wikipedia_aislado(s, "es", o2, cap, 5) : bw_ddg_aislado(s, o2, cap, 5);
    if (a <= 0) return b == 0;                              /* sin resultados: el aislado tampoco da ninguno */
    return a == b && cad_igual(o1, o2);
}
static void sbx_lee_test(void)
{
    kprint(" [lector web aislado (leesvc, parche 312) vs web_lee.c / busca_web.c en el nucleo]\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    const uint32_t cap = 32u * 1024u;
    char *o1 = (char *)kmalloc(cap), *o2 = (char *)kmalloc(cap), *buf = (char *)kmalloc(4096), *t1 = (char *)kmalloc(160), *t2 = (char *)kmalloc(160);
    if (!o1 || !o2 || !buf || !t1 || !t2) { ck(0, "memoria para la prueba del lector web"); goto fin; }
    for (int i = 0; i < 3; ++i) {
        const char *s = web_muestras[i]; uint32_t n = co_len(s);
        ck(web_compara(i, s, n, o1, o2, cap, t1, t2), i == 0 ? "pagina web (articulo, scripts, menus, entidades): igual que en el nucleo" :
                                                   i == 1 ? "resultados de Wikipedia (JSON): igual que en el nucleo" : "resultados de DuckDuckGo (HTML): igual que en el nucleo");
        if (i == 0) ck(cad_igual(t2, "La imprenta de Gutenberg \342\200\223 Historia") && co_ibusca(o2, co_len(o2), "document.write") == 0
                      && co_ibusca(o2, co_len(o2), "Aviso legal") == 0, "  titulo de la pagina, sin scripts ni pie");
        if (i == 0) ck(co_ibusca(o2, co_len(o2), "Maguncia") != 0 && co_ibusca(o2, co_len(o2), "Biblia de 42") != 0
                      && co_ibusca(o2, co_len(o2), "\n\n\n") == 0, "  SOMA_313: el texto del articulo entero (antes se perdian parrafos)");
        if (i == 1) ck(bw_wikipedia_aislado(s, "es", o2, cap, 5) == 2, "  dos resultados de Wikipedia");
    }
    ck(wl_texto_aislado(web_muestras[0], co_len(web_muestras[0]), o2, 40, 0, 0) <= 39 && co_len(o2) <= 39, "texto con cap pequeno: nunca pasa de lo pedido");
    {
        uint32_t semilla = 0x312C0DEu, iguales = 0;
        uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 300; ++it) {
            int tipo = it % 3; const char *base = web_muestras[tipo]; uint32_t n = co_len(base);
            if (n > 4000) n = 4000;
            memcpy(buf, base, n);
            int cambios = 1 + (int)((semilla >> 7) % 12u);
            for (int c = 0; c < cambios; ++c) {
                semilla = semilla * 1103515245u + 12345u;
                static const char raros[] = "<>&;\"'/=\\u{}[]\200\377";
                buf[(semilla >> 8) % n] = (semilla & 1u) ? raros[(semilla >> 3) % (sizeof raros - 1)] : (char)(semilla >> 16);
            }
            if ((semilla & 7u) == 0) n = 1 + (semilla >> 12) % n;
            buf[n] = 0;
            uint32_t m = co_len(buf);                       /* las funciones de busqueda leen hasta el primer NUL */
            if (web_compara(tipo, buf, tipo == 0 ? n : m, o1, o2, cap, t1, t2)) ++iguales;
        }
        kprint("  300 paginas mutiladas: "); kprint_dec(iguales); kprint(" iguales en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(iguales == 300, "paginas mutiladas: el aislado da lo mismo y el nucleo sobrevive");
    }
fin:
    if (o1) kfree(o1);
    if (o2) kfree(o2);
    if (buf) kfree(buf);
    if (t1) kfree(t1);
    if (t2) kfree(t2);
    sbx_svc_stop_all();
    leak_check(f0, "lector web");
}

/* SOMA_315_YOUTUBE_AISLADO: la busqueda y la ficha de YouTube nativo, en leesvc (ring 3) vs yt_inner.h en el nucleo. Muestras:
 * una busqueda (escapes, par subrogado, UTF-8 crudo, id repetido y no valido), las fichas reales de tests/datos_252 y una ficha
 * con una direccion que lleva \r\n para colar cabeceras HTTP. */
static const char yt_busca[] =
    "{\"responseContext\":{},\"estimatedResults\":\"812\",\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContent"
    "s\":{\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":[{\"videoRenderer\":{\"videoId\":\"a"
    "qz-KE-bpKQ\",\"thumbnail\":{\"thumbnails\":[{\"url\":\"https://i.ytimg.com/vi/aqz-KE-bpKQ/hq720.jpg\",\"width\":360}]},"
    "\"title\":{\"runs\":[{\"text\":\"Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film\"}]},\"longBylineText"
    "\":{\"runs\":[{\"text\":\"Blender\"}]},\"lengthText\":{\"accessibility\":{\"accessibilityData\":{\"label\":\"10 minutos"
    "\"}},\"simpleText\":\"10:35\"},\"viewCountText\":{\"simpleText\":\"23.574.905 visualizaciones\"},\"detailedMetadataSnip"
    "pets\":[{\"snippetText\":{\"runs\":[{\"text\":\"UHD High Frame rate version of the \"},{\"text\":\"iconic\",\"bold\":tr"
    "ue},{\"text\":\" short film by Blender\"}]},\"snippetHoverText\":{}}]}},{\"videoRenderer\":{\"videoId\":\"YE7VzlLtp-4\""
    ",\"title\":{\"runs\":[{\"text\":\"Caf\\u00e9 \\\"Sintel\\\" \\u2013 corto \\ud83d\\ude00 abierto\\n\\tcompleto\"}]},\"o"
    "wnerText\":{\"runs\":[{\"text\":\"Blender Studio\"}]},\"lengthText\":{\"simpleText\":\"1:02:03\"},\"viewCountText\":{\""
    "simpleText\":\"1.234 visualizaciones\"}}},{\"videoRenderer\":{\"videoId\":\"aqz-KE-bpKQ\",\"title\":{\"runs\":[{\"text\""
    ":\"repetido\"}]}}},{\"videoRenderer\":{\"videoId\":\"mal id!!!!!\",\"title\":{\"runs\":[{\"text\":\"id no valido\"}]}}}"
    ",{\"videoRenderer\":{\"videoId\":\"eRsGyueVLvQ\",\"title\":{\"runs\":[{\"text\":\"Canci\303\263n de prueba en directo\""
    "}]},\"viewCountText\":{\"simpleText\":\"7 visualizaciones\"}}}]}}]}}}}}";
static const char yt_ficha[] =
    "{\"responseContext\":{},\"playabilityStatus\":{\"status\":\"OK\",\"playableInEmbed\":true},\"streamingData\":{\"expires"
    "InSeconds\":\"21540\",\"formats\":[{\"itag\":18,\"url\":\"https://rr2---sn-vg5obxn25po-8aae.googlevideo.com/videoplayba"
    "ck?expire=1795311350\\u0026ei=X\\u0026ip=203.0.113.7\\u0026id=X\\u0026itag=18\\u0026source=youtube\\u0026requiressl=yes"
    "\\u0026xpc=X\\u0026cps=651\\u0026met=1795289750%2C\\u0026mh=aP\\u0026mm=18%2C29\\u0026mn=sn-vg5obxn25po-8aae%2Csn-w511u"
    "xa-h5qd\\u0026ms=aub%2Crdu\\u0026mv=m\\u0026mvi=2\\u0026pl=24\\u0026rms=aub%2Caub\\u0026pcm2=no\\u0026initcwndbps=29830"
    "00\\u0026bui=X\\u0026spc=X\\u0026vprv=1\\u0026svpuc=1\\u0026mime=video%2Fmp4\\u0026rqh=1\\u0026cnr=14\\u0026ratebypass="
    "yes\\u0026dur=634.624\\u0026lmt=1719195733189356\\u0026mt=1795289184\\u0026fvip=203.0.113.7\\u0026epbp=1\\u0026fexp=515"
    "65116\\u0026c=ANDROID\\u0026txp=4538434\\u0026sparams=expire%2Cei%2Cip%2Cid%2Citag%2Csource%2Crequiressl%2Cxpc%2Cpcm2%2"
    "Cbui%2Cspc%2Cvprv%2Csvpuc%2Cmime%2Crqh%2Ccnr%2Cratebypass%2Cdur%2Clmt\\u0026sig=X\\u0026lsparams=cps%2Cmet%2Cmh%2Cmm%2C"
    "mn%2Cms%2Cmv%2Cmvi%2Cpl%2Crms%2Cinitcwndbps\\u0026lsig=X\",\"mimeType\":\"video/mp4; codecs=\\\"avc1.42001E, mp4a.40.2\\\""
    "\",\"bitrate\":359607,\"width\":640,\"height\":360,\"lastModified\":\"1719195733189356\",\"quality\":\"medium\",\"fps\""
    ":30,\"qualityLabel\":\"360p\",\"projectionType\":\"RECTANGULAR\",\"audioQuality\":\"AUDIO_QUALITY_LOW\",\"approxDuratio"
    "nMs\":\"634624\",\"audioSampleRate\":\"44100\",\"audioChannels\":2,\"qualityOrdinal\":\"QUALITY_ORDINAL_360P\"}],\"adap"
    "tiveFormats\":[{\"itag\":134,\"mimeType\":\"video/mp4; codecs=\\\"avc1.4D401E\\\"\",\"bitrate\":643000}],\"serverAbrStr"
    "eamingUrl\":\"https://rr2---sn-x.googlevideo.com/videoplayback?sabr=1\"},\"videoDetails\":{\"videoId\":\"aqz-KE-bpKQ\","
    "\"title\":\"Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film\",\"lengthSeconds\":\"635\",\"channelId\":"
    "\"UCSMOQeBJ2RAnuFungnQOxLg\",\"shortDescription\":\"UHD High Frame rate version of the iconic short film by Blender, Bi"
    "g Buck Bunny. More info:  https://studio.blender.org/projects/big-buck-bunny/\\n\\nLearn more about this UHD high frame"
    "-rate version at: http://bbb3d.renderfarming.net\",\"viewCount\":\"23574905\",\"author\":\"Blender\",\"isLiveContent\":"
    "false}}";
static const char yt_bot[] =
    "{\"responseContext\":{},\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Inicia sesi\303\263n para con"
    "firmar que no eres un bot\",\"errorScreen\":{}},\"videoDetails\":{\"videoId\":\"aqz-KE-bpKQ\"}}";
static const char yt_malo[] =   /* una direccion con \r\n (para colar cabeceras HTTP): no debe llegar */
    "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[{\"itag\":18,\"url\":\"https://rr1---sn-x.go"
    "oglevideo.com/videoplayback?a=1\\u000d\\u000aHost: malo\"}]},\"videoDetails\":{\"title\":\"cabecera inyectada\",\"autho"
    "r\":\"x\",\"lengthSeconds\":\"9\",\"shortDescription\":\"uno\\ndos\\u0007tres\"}}";
static int yt_lista_igual(const struct yt_item *a, int na, const struct yt_item *b, int nb)
{
    if (na != nb) return 0;
    for (int i = 0; i < na; ++i)
        if (!cad_igual(a[i].id, b[i].id) || !cad_igual(a[i].title, b[i].title) || !cad_igual(a[i].channel, b[i].channel)
            || !cad_igual(a[i].desc, b[i].desc) || a[i].dur != b[i].dur || a[i].views != b[i].views) return 0;
    return 1;
}
static int yt_ficha_igual(const struct yti_player *a, const char *da, const struct yti_player *b, const char *db)
{
    return a->playable == b->playable && cad_igual(a->status, b->status) && cad_igual(a->reason, b->reason) && cad_igual(a->url, b->url)
        && cad_igual(a->title, b->title) && cad_igual(a->author, b->author) && a->dur == b->dur && a->views == b->views && cad_igual(da, db);
}
static int yt_compara(int tipo, const char *s, uint32_t n, struct yt_item *l1, struct yt_item *l2, struct yti_player *p1, struct yti_player *p2,
                      char *d1, char *d2, uint32_t dcap)
{
    if (tipo == 0) {
        int n1 = yti_parse_search(s, n, l1, YT_MAX_ITEMS), e1 = yti_find(s, s + n, "\"estimatedResults\"") != 0, e2 = 0;
        int n2 = yti_busca_aislado(s, n, l2, YT_MAX_ITEMS, &e2);
        return e1 == e2 && yt_lista_igual(l1, n1, l2, n2);
    }
    int r1 = yti_parse_player_ex(s, n, p1, d1, dcap, 0);
    int r2 = yti_ficha_aislada(s, n, p2, d2, dcap, 0);
    return r1 == r2 && yt_ficha_igual(p1, d1, p2, d2);
}
static void sbx_yt_test(void)
{
    kprint(" [YouTube nativo aislado (leesvc, parche 315) vs yt_inner.h en el nucleo]\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    const uint32_t dcap = 6200u;
    struct yt_item *l1 = (struct yt_item *)kmalloc(YT_MAX_ITEMS * sizeof *l1), *l2 = (struct yt_item *)kmalloc(YT_MAX_ITEMS * sizeof *l2);
    struct yti_player *p1 = (struct yti_player *)kmalloc(sizeof *p1), *p2 = (struct yti_player *)kmalloc(sizeof *p2);
    char *d1 = (char *)kmalloc(dcap), *d2 = (char *)kmalloc(dcap), *buf = (char *)kmalloc(4096);
    if (!l1 || !l2 || !p1 || !p2 || !d1 || !d2 || !buf) { ck(0, "memoria para la prueba de YouTube"); goto fin; }
    static const char *const m[4] = { yt_busca, yt_ficha, yt_bot, yt_malo };
    static const uint32_t ml[4] = { sizeof yt_busca - 1, sizeof yt_ficha - 1, sizeof yt_bot - 1, sizeof yt_malo - 1 };
    ck(yt_compara(0, m[0], ml[0], l1, l2, p1, p2, d1, d2, dcap), "busqueda de YouTube: igual que en el nucleo");
    {
        int est = 0, n = yti_busca_aislado(m[0], ml[0], l2, YT_MAX_ITEMS, &est);
        ck(n == 3 && est && cad_igual(l2[0].id, "aqz-KE-bpKQ") && l2[0].dur == 635 && l2[0].views == 23574905u && cad_igual(l2[0].channel, "Blender")
           && co_ibusca(l2[0].desc, co_len(l2[0].desc), "iconic short film") != 0, "  tres videos, sin el repetido ni el de id no valido");
        ck(n == 3 && cad_igual(l2[1].title, "Caf\303\251 \"Sintel\" - corto abierto completo") && l2[1].dur == 3723 && cad_igual(l2[2].id, "eRsGyueVLvQ")
           && cad_igual(l2[2].title, "Canci\303\263n de prueba en directo"), "  escapes, emoji y UTF-8 como en el nucleo");
        n = yti_busca_aislado(m[0], ml[0], l2, 1, &est);
        ck(n == 1, "  nunca mas resultados de los pedidos");
    }
    for (int i = 1; i < 4; ++i)
        ck(yt_compara(1, m[i], ml[i], l1, l2, p1, p2, d1, d2, dcap), i == 1 ? "ficha de un video (real): igual que en el nucleo" :
                                                                   i == 2 ? "ficha \"no eres un bot\" (real): igual que en el nucleo" : "ficha con una direccion con \\r\\n: igual que en el nucleo");
    ck(yti_ficha_aislada(m[1], ml[1], p2, d2, dcap, 0) == 1 && p2->playable && yt_starts(p2->url, "https://rr2---") && p2->views == 23574905u
       && p2->dur == 635 && co_ibusca(d2, co_len(d2), "\n\nLearn more") != 0, "  formato 18, visitas, duracion y descripcion con saltos de linea");
    ck(yti_ficha_aislada(m[2], ml[2], p2, d2, dcap, 0) == 1 && !p2->playable && !p2->url[0] && co_ibusca(p2->reason, co_len(p2->reason), "bot") != 0,
       "  la de \"no eres un bot\" no se puede ver y dice por que");
    ck(yti_ficha_aislada(m[3], ml[3], p2, d2, dcap, 0) == 1 && !p2->url[0] && co_ibusca(d2, co_len(d2), "\007") == 0, "  la direccion con \\r\\n no llega");
    ck(yti_ficha_aislada(m[1], ml[1], p2, d2, 8, 0) == 1 && co_len(d2) <= 7, "  descripcion con cap pequeno: nunca pasa de lo pedido");
    {
        uint32_t semilla = 0x315C0DEu, iguales = 0;
        uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 300; ++it) {
            int k = it % 4; uint32_t n = ml[k];
            if (n > 4000) n = 4000;
            memcpy(buf, m[k], n);
            int cambios = 1 + (int)((semilla >> 7) % 12u);
            for (int c = 0; c < cambios; ++c) {
                semilla = semilla * 1103515245u + 12345u;
                static const char raros[] = "\"\\{}[],:u0dD\200\377";
                buf[(semilla >> 8) % n] = (semilla & 1u) ? raros[(semilla >> 3) % (sizeof raros - 1)] : (char)(semilla >> 16);
            }
            if ((semilla & 7u) == 0) n = 1 + (semilla >> 12) % n;
            if (yt_compara(k == 0 ? 0 : 1, buf, n, l1, l2, p1, p2, d1, d2, dcap)) ++iguales;
        }
        kprint("  300 respuestas mutiladas: "); kprint_dec(iguales); kprint(" iguales en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(iguales == 300, "respuestas mutiladas: el aislado da lo mismo y el nucleo sobrevive");
    }
fin:
    if (l1) kfree(l1);
    if (l2) kfree(l2);
    if (p1) kfree(p1);
    if (p2) kfree(p2);
    if (d1) kfree(d1);
    if (d2) kfree(d2);
    if (buf) kfree(buf);
    sbx_svc_stop_all();
    leak_check(f0, "YouTube");
}

/* SOMA_314_PROGZ: los seis programas grandes de ring 3 van comprimidos en la imagen; cada uno se descomprime (en zdec), se mira que
 * sea un ELF del tamano anunciado y se libera. Una copia con un byte cambiado (en el cuerpo o en la cabecera) no se acepta. */
static void sbx_progz_test(void)
{
    kprint(" [programas comprimidos en la imagen (parche 314)]\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    static const struct { const uint8_t *ini, *fin; const char *nombre; } t[] = {
        { PROGZ(websvc), "websvc" }, { PROGZ(vplay), "vplay" }, { PROGZ(python), "python" },
        { PROGZ(jsrun), "jsrun" },   { PROGZ(cc), "cc" },       { PROGZ(voz), "voz" },
    };
    int bien = 0; uint64_t orig = 0, comp = 0, t0 = timer_get_uptime_ms();
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; ++i) {
        struct progz pz;
        if (progz_abre(t[i].ini, t[i].fin, t[i].nombre, &pz) != 0) continue;
        const uint8_t *e = (const uint8_t *)pz.elf;
        if (pz.len > 64 && e[0] == 0x7F && e[1] == 'E' && e[2] == 'L' && e[3] == 'F' && e[4] == 2) ++bien;
        orig += pz.len; comp += (uint64_t)(t[i].fin - t[i].ini);
        progz_cierra(&pz);
        ck(pz.elf == 0 && pz.len == 0, "  progz_cierra deja la estructura vacia");
    }
    kprint("  "); kprint_dec((uint32_t)bien); kprint(" de 6 descomprimidos: "); kprint_dec((uint32_t)(comp >> 10)); kprint(" KiB -> ");
    kprint_dec((uint32_t)(orig >> 10)); kprint(" KiB en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    ck(bien == 6, "los seis programas comprimidos dan un ELF de 64 bits");
    {
        size_t n = (size_t)(_binary_python_elf_z_end - _binary_python_elf_z_start);
        uint8_t *cp = (uint8_t *)kmalloc(n);
        if (!cp) ck(0, "memoria para la copia danada");
        else {
            struct progz pz;
            memcpy(cp, _binary_python_elf_z_start, n);
            cp[n / 2] ^= 0x40u;
            int r1 = progz_abre(cp, cp + n, "copia danada (cuerpo)", &pz);
            memcpy(cp, _binary_python_elf_z_start, n);
            cp[17] ^= 0x01u;
            int r2 = progz_abre(cp, cp + n, "copia danada (huella)", &pz);
            memcpy(cp, _binary_python_elf_z_start, n);
            cp[0] = 'X';
            int r3 = progz_abre(cp, cp + n, "copia danada (firma)", &pz);
            int r4 = progz_abre(cp, cp, "vacio", &pz);
            ck(r1 != 0 && r2 != 0 && r3 != 0 && r4 != 0 && pz.elf == 0, "una copia danada o vacia no se ejecuta");
            kfree(cp);
        }
    }
    sbx_svc_stop_all();
    leak_check(f0, "programas comprimidos");
}

int sbx_selftest(void)
{
    bad = 0;
    int64_t code;
    kprint("SBX: servicios aislados en ring 3 (parche 138)\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();

    kprint(" [ataques al mecanismo]\n");
    ck(evil(SBX_EVIL_SYSCALL, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_DENIED, "syscall prohibida (write/open) -> muere DENIED");
    ck(evil(SBX_EVIL_LOOP, 65536, 300, &code) == SBX_E_DIED && code == USER_EXIT_TIMEOUT, "bucle infinito -> plazo");
    ck(evil(SBX_EVIL_KREAD, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "lee memoria del kernel -> #PF");
    ck(evil(SBX_EVIL_WRITE_IN, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "escribe su entrada (solo lectura) -> #PF");
    ck(evil(SBX_EVIL_HEAP_PAST, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "escribe tras el final de su monton -> #PF");
    ck(evil(SBX_EVIL_EXEC_HEAP, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "ejecuta codigo en su monton (NX) -> #PF");
    ck(evil(SBX_EVIL_BAD_PTR, 65536, 2000, &code) == SBX_E_REPLY, "respuesta con puntero fuera del monton -> rechazada");
    ck(evil(SBX_EVIL_BAD_LEN, 65536, 2000, &code) == SBX_E_REPLY, "respuesta con longitud enorme -> rechazada");
    ck(evil(SBX_EVIL_UNTOUCHED, 4u << 20, 2000, &code) == SBX_E_REPLY, "respuesta en paginas nunca escritas -> rechazada");
    ck(evil(SBX_EVIL_BAD_MAGIC, 65536, 2000, &code) == SBX_E_REPLY, "respuesta sin magic -> rechazada");
    {
        size_t a = free_frames();
        ck(evil(SBX_EVIL_HEAP_ALL, 8u << 20, 3000, &code) == 0, "toca todo su monton (8 MiB) -> OK, bajo demanda");
        size_t b = free_frames();
        ck(b + 8 >= a, "y la memoria vuelve al terminar");
        sbx_reserve_override = (uint32_t)(a > 600 ? a - 600 : 1);    /* solo deja crecer ~600 paginas */
        ck(evil(SBX_EVIL_HEAP_ALL, 8u << 20, 3000, &code) == SBX_E_HEAPOOM && code == USER_EXIT_NOMEM, "reserva del sistema -> muere NOMEM sin agotar la memoria");
        sbx_reserve_override = 0;
    }
    {
        static uint8_t buf[10000]; static uint8_t got[10000];
        static struct sbx_call c;
        for (int i = 0; i < 10000; ++i) buf[i] = (uint8_t)(i * 13 + 5);
        sbx_prepare(&c, SBX_EVIL_ECHO, buf, sizeof buf, 1u << 20);
        int r = sbx_run(&c, EVIL, 2000), good = r == 0 && c.out.data_len == sizeof buf && sbx_copy_data(&c, got, sizeof got) == 0;
        for (int i = 0; good && i < 10000; ++i) if (got[i] != buf[9999 - i]) good = 0;
        sbx_release(&c);
        ck(good, "ida y vuelta de 10000 bytes (entrada -> monton -> kernel) exacta");
    }
    leak_check(f0, "ataques");

    kprint(" [decodificadores de imagen aislados vs referencia de host]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    for (int i = 0; i < SBX_TESTIMG_N; ++i) {
        const struct sbx_testimg *t = &sbx_testimg[i];
        uint64_t t0 = timer_get_uptime_ms();
        int r, w = 0, h = 0; uint32_t hs = 0;
        if (t->op == 4) {                                   /* PARCHE 139: MJPEG del reproductor */
            struct jpeg_info ji; uint32_t *b = (uint32_t *)kmalloc((size_t)t->a0 * (size_t)t->a1 * 4u);
            r = b ? jpeg_decode_isolated(t->data, t->len, b, (uint32_t)t->a0, (uint32_t)t->a1, &ji) : -99;
            if (r == 0) { w = (int)ji.width; h = (int)ji.height; hs = fnv(b, (size_t)t->a0 * h); }
            if (b) kfree(b);
            if (r != 0 && r == t->exp_r) r = -1;            /* mismo codigo de error que la referencia */
        } else if (t->op == 3) {
            struct image im; r = wi_decode_bmp_raw(t->data, t->len, &im);
            if (r == 0) { w = (int)im.width; h = (int)im.height; hs = fnv(im.pixels, (size_t)w * h); kfree(im.pixels); }
        } else {
            struct wimg im; r = t->op == 2 ? wi_decode_fit(t->data, t->len, &im, t->a0, t->a1) : wi_decode(t->data, t->len, &im);
            if (r == 0) { w = im.w; h = im.h; hs = fnv(im.px, (size_t)w * h); wi_free(&im); }
        }
        uint64_t dt = timer_get_uptime_ms() - t0;
        int ok = (r == 0) == (t->exp_r == 0) && w == t->exp_w && h == t->exp_h && hs == t->exp_hash;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(t->name); kprint(": "); kprint_dec((uint32_t)w); kprint("x"); kprint_dec((uint32_t)h);
        kprint(" hash=0x"); kprint_hex32(hs); kprint(" en "); kprint_dec((uint32_t)dt); kprint(" ms\n");
        if (!ok) bad++;
    }
    sbx_svc_stop_all();                                     /* PARCHE 139: los servicios guardan monton entre peticiones */
    leak_check(f0, "imagenes");

    kprint(" [servicios persistentes (parche 139)]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    {
        static struct sbx_call c; static uint8_t buf[4000], got[4000];
        struct sbx_stats a0; sbx_get_stats(&a0);
        int good = 0; uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 200; ++it) {
            uint32_t n = 100 + (uint32_t)it * 17;
            for (uint32_t i = 0; i < n; ++i) buf[i] = (uint8_t)(i ^ (uint32_t)it);
            sbx_prepare(&c, SBX_EVIL_ECHO, buf, n, 0);
            int r = sbx_svc_call(SBX_SVC_TEST, &c, 2000), ok = r == 0 && c.out.data_len == n && sbx_copy_data(&c, got, n) == 0;
            for (uint32_t i = 0; ok && i < n; ++i) if (got[i] != buf[n - 1 - i]) ok = 0;
            sbx_release(&c);
            good += ok;
        }
        uint64_t tsvc = timer_get_uptime_ms() - t0;
        t0 = timer_get_uptime_ms();
        for (int it = 0; it < 50; ++it) { sbx_prepare(&c, SBX_EVIL_ECHO, buf, 100, 1u << 20); (void)sbx_run(&c, EVIL, 2000); sbx_release(&c); }
        uint64_t tone = timer_get_uptime_ms() - t0;
        struct sbx_stats a1; sbx_get_stats(&a1);
        kprint("  200 llamadas al servicio: "); kprint_dec((uint32_t)tsvc); kprint(" ms ("); kprint_dec((uint32_t)(tsvc * 1000 / 200)); kprint(" us/llamada); un proceso por llamada: ");
        kprint_dec((uint32_t)(tone * 1000 / 50)); kprint(" us/llamada; procesos de servicio creados: "); kprint_dec((uint32_t)(a1.svc_starts - a0.svc_starts)); kprint("\n");
        ck(good == 200, "200 peticiones correctas al mismo servicio");
        ck(a1.svc_starts - a0.svc_starts <= 2, "sin crear un proceso por peticion");

        sbx_prepare(&c, SBX_EVIL_PLANT, "x", 1, 0); int r1 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); sbx_release(&c);
        sbx_prepare(&c, SBX_EVIL_SNOOP, "x", 1, 0); int r2 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int32_t nz = c.out.val[0]; sbx_release(&c);
        kprint("  restos de la peticion anterior visibles en el monton: "); kprint_dec((uint32_t)nz); kprint(" bytes\n");
        ck(r1 == 0 && r2 == 0 && nz == 0, "una peticion no ve datos de la anterior (monton a cero entre peticiones)");

        sbx_prepare(&c, SBX_EVIL_KREAD, "x", 1, 0); int r3 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int64_t e3 = c.exit_code; sbx_release(&c);
        ck(r3 == SBX_E_DIED && e3 == USER_EXIT_FAULT_BASE - 14, "el servicio muere por #PF a mitad de peticion -> error");
        sbx_prepare(&c, SBX_EVIL_STATIC, "x", 1, 0); int r4 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int32_t v4 = c.out.val[0]; sbx_release(&c);
        ck(r4 == 0 && v4 == 1, "la siguiente peticion la atiende un proceso NUEVO (estado global limpio)");
        sbx_prepare(&c, SBX_EVIL_LOOP, "x", 1, 0); int r5 = sbx_svc_call(SBX_SVC_TEST, &c, 300); int64_t e5 = c.exit_code; sbx_release(&c);
        ck(r5 == SBX_E_DIED && e5 == USER_EXIT_TIMEOUT, "bucle infinito en el servicio -> plazo de la peticion");
        sbx_prepare(&c, SBX_EVIL_SYSCALL, "x", 1, 0); int r6 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int64_t e6 = c.exit_code; sbx_release(&c);
        ck(r6 == SBX_E_DIED && e6 == USER_EXIT_DENIED, "syscall prohibida en el servicio -> DENIED");
        sbx_prepare(&c, SBX_EVIL_ECHO, buf, 10, 0); int r7 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); sbx_release(&c);
        ck(r7 == 0, "y despues sigue atendiendo");
        int same = 1;
        for (int k = 1; k <= 5 && same; ++k) { sbx_prepare(&c, SBX_EVIL_STATIC, "x", 1, 0); (void)sbx_svc_call(SBX_SVC_TEST, &c, 2000); if (c.out.val[0] < k) same = 0; sbx_release(&c); }
        ck(same, "entre peticiones correctas el proceso se reutiliza");
    }
    kprint(" [inflate aislado (zdec) vs referencia de host]\n");
    for (int i = 0; i < SBX_TESTZ_N; ++i) {
        const struct sbx_testz *z = &sbx_testz[i];
        struct wb o; o.p = 0; o.len = o.cap = 0; o.err = 0;
        uint64_t t0 = timer_get_uptime_ms();
        int r = wz_inflate(z->data, z->len, z->fmt, &o, z->max_out);
        uint32_t hs = 2166136261u; for (uint32_t k = 0; k < o.len; ++k) { hs ^= (uint8_t)o.p[k]; hs *= 16777619u; }
        int ok = (r < 0) == (z->exp_r < 0) && o.len == z->exp_len && hs == z->exp_hash;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(z->name); kprint(": "); kprint_dec(o.len); kprint(" bytes hash=0x"); kprint_hex32(hs);
        kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        if (!ok) bad++;
        if (o.p) kfree(o.p);
    }
    kprint(" [analizador PDF aislado (pdfsvc) vs referencia de host]\n");
    for (int i = 0; i < SBX_TESTPDF_N; ++i) {
        const struct sbx_testpdf *t = &sbx_testpdf[i];
        char err[128]; uint32_t h = 2166136261u; int np = 0, ok = 0;
        uint64_t t0 = timer_get_uptime_ms();
        struct pdf *p = pdf_open(t->data, t->len, err, sizeof err);
        if (p) {
            ok = 1; np = pdf_page_count(p);
            char title[256]; int tl = pdf_title(p, title, sizeof title); h = pdfh_i(h, np); h = pdfh_mix(h, title, tl > 0 ? (uint32_t)tl : 0);
            for (int k = 0; k < np; ++k) {
                int w = 0, hh = 0; pdf_page_size(p, k, &w, &hh); h = pdfh_i(h, w); h = pdfh_i(h, hh);
                struct pdf_page pg; int r = pdf_render_page(p, k, 256, &pg); h = pdfh_i(h, r);
                if (r == 0) { h = pdfh_i(h, pdf_dl_hash(&pg)); pdf_page_free(&pg); }
            }
            pdf_close(p);
        }
        int good = ok == t->exp_open && np == t->exp_pages && h == t->exp_hash;
        kprint(good ? "  OK    " : "  FALLO "); kprint(t->name); kprint(ok ? ": " : ": rechazado ("); if (!ok) { kprint(err); kprint(") "); }
        kprint_dec((uint32_t)np); kprint(" paginas, huella 0x"); kprint_hex32(h); kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        if (!good) bad++;
    }
    {   /* listas de dibujo manipuladas: lo que devolveria un analizador comprometido */
        static uint8_t b[4096];
        struct sbx_pdf_hdr *hd = (struct sbx_pdf_hdr *)b; struct sbx_pdf_op *o = (struct sbx_pdf_op *)(b + sizeof *hd);
        #define RESET() do { for (int _i = 0; _i < 4096; ++_i) b[_i] = 0; hd->magic = SBX_PDFL_MAGIC; hd->w = 100; hd->h = 100; hd->nops = 1; hd->blob_len = 64; \
                             o->kind = PDF_OP_TEXT; o->off = 0; o->n = 8; o->font = 1; } while (0)
        uint32_t N = (uint32_t)(sizeof *hd + sizeof *o + 64);
        int rej = 0, tot = 0;
        #define MUST_REJECT(stmt) do { RESET(); stmt; tot++; if (pdf_sbx_unpack_test(b, N) != 0) rej++; else { kprint("  aceptada: " #stmt "\n"); } } while (0)
        RESET(); ck(pdf_sbx_unpack_test(b, N) == 0, "lista de dibujo valida -> aceptada");
        MUST_REJECT(hd->magic = 0);
        MUST_REJECT(hd->nops = 400001);
        MUST_REJECT(hd->nops = -1);
        MUST_REJECT(hd->blob_len = 65);
        MUST_REJECT(hd->w = 16385);
        MUST_REJECT(hd->h = -5);
        MUST_REJECT(o->kind = 9);
        MUST_REJECT(o->font = 65);
        MUST_REJECT(o->off = 60);
        MUST_REJECT(o->n = 0xFFFFFFF0u);
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 2, o->n = 4));
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 0, o->n = 9));
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 0, o->n = 0x20000000u));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 4, o->ih = 4, o->w = 10, o->h = 0));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 65536, o->ih = 65536, o->w = 10, o->h = 10));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 4, o->ih = 5, o->w = 10, o->h = 10));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 0, o->ih = 4, o->w = 10, o->h = 10));
        { RESET(); o->kind = PDF_OP_IMAGE; o->iw = 4; o->ih = 4; o->w = 10; o->h = 10; ck(pdf_sbx_unpack_test(b, N) == 0, "imagen 4x4 dentro del blob -> aceptada"); }
        kprint("  listas manipuladas rechazadas: "); kprint_dec((uint32_t)rej); kprint(" de "); kprint_dec((uint32_t)tot); kprint("\n");
        ck(rej == tot, "el kernel rechaza toda lista de dibujo incoherente");
        #undef RESET
        #undef MUST_REJECT
    }
    sbx_svc_stop_all();
    ck(sbx_svc_running(SBX_SVC_IMG) + sbx_svc_running(SBX_SVC_ZLIB) + sbx_svc_running(SBX_SVC_TEST) + sbx_svc_running(SBX_SVC_PDF) == 0, "todos los servicios detenidos");
    leak_check(f0, "servicios");

    kprint(" [fuzzing: imagenes mutadas a traves del servicio]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    {
        uint32_t seed = 0x1234567u, nok = 0, nfail = 0;
        uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 240; ++it) {
            const struct sbx_testimg *t = &sbx_testimg[it % SBX_TESTIMG_N];
            if (t->len > 20000) continue;
            uint8_t *d = (uint8_t *)kmalloc(t->len);
            if (!d) continue;
            for (uint32_t k = 0; k < t->len; ++k) d[k] = t->data[k];
            int nm = 1 + (int)(seed % 8);
            for (int m = 0; m < nm; ++m) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; d[seed % t->len] ^= (uint8_t)(seed >> 8); }
            struct wimg im;
            if (wi_decode(d, t->len, &im) == 0) { nok++; wi_free(&im); } else nfail++;
            kfree(d);
        }
        kprint("  240 entradas mutadas: "); kprint_dec(nok); kprint(" decodificadas, "); kprint_dec(nfail); kprint(" rechazadas, en ");
        kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(nok + nfail > 200, "el kernel sobrevive a todas");
    }
    {   /* PARCHE 141: PDF mutados: abrir y dibujar todas sus paginas a traves de pdfsvc */
        const struct sbx_testpdf *t = &sbx_testpdf[2];
        uint32_t seed = 0xBADC0DEu, opened = 0, pages = 0, rejected = 0;
        uint64_t t0 = timer_get_uptime_ms();
        uint8_t *d = (uint8_t *)kmalloc(t->len);
        for (int it = 0; d && it < 120; ++it) {
            for (uint32_t k = 0; k < t->len; ++k) d[k] = t->data[k];
            int nm = 1 + (int)(seed % 6);
            for (int m = 0; m < nm; ++m) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; d[seed % t->len] ^= (uint8_t)(seed >> 8); }
            char err[64]; struct pdf *p = pdf_open(d, t->len, err, sizeof err);
            if (!p) { rejected++; continue; }
            opened++;
            for (int k = 0; k < pdf_page_count(p) && k < 8; ++k) { struct pdf_page pg; if (pdf_render_page(p, k, 256, &pg) == 0) { pages++; pdf_page_free(&pg); } }
            pdf_close(p);
        }
        if (d) kfree(d);
        kprint("  120 PDF mutados: "); kprint_dec(opened); kprint(" abiertos ("); kprint_dec(pages); kprint(" paginas dibujadas), "); kprint_dec(rejected);
        kprint(" rechazados, en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(opened + rejected == 120, "el kernel sobrevive a todos los PDF mutados");
    }
    sbx_svc_stop_all();
    leak_check(f0, "fuzzing");

    sbx_correo_test();                                      /* SOMA_311_CORREO_AISLADO */
    sbx_lee_test();                                         /* SOMA_312_WEB_AISLADA */
    sbx_progz_test();                                       /* SOMA_314_PROGZ */
    sbx_yt_test();                                          /* SOMA_315_YOUTUBE_AISLADO */
    {   /* SOMA_318_NAVEGADOR_CERRADO */
        kprint(" [navegador: si el proceso aislado no arranca (parche 318)]\n");
        ck(browser_prueba_cerrado(), "la pagina no se abre y no se cambia al motor del nucleo; la barra de estado lo dice");
    }

    kprint(" [concurrencia: 12 hilos x 4 llamadas, 8 ranuras de proceso]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    conc_ok = conc_bad = conc_done = 0;
    for (uintptr_t i = 0; i < 12; ++i) thread_create("sbxc", conc_thread, (void *)i);
    for (int w = 0; w < 3000 && conc_done < 12; ++w) thread_sleep(10);
    kprint("  correctas "); kprint_dec((uint32_t)conc_ok); kprint(", erroneas "); kprint_dec((uint32_t)conc_bad); kprint("\n");
    ck(conc_done == 12 && conc_ok == 48 && conc_bad == 0, "48 llamadas concurrentes correctas");
    leak_check(f0, "concurrencia");

    struct sbx_stats s; sbx_get_stats(&s);
    kprint("  estadisticas: "); kprint_dec((uint32_t)s.runs); kprint(" llamadas, "); kprint_dec((uint32_t)s.ok); kprint(" bien, ");
    kprint_dec((uint32_t)s.died); kprint(" murieron, "); kprint_dec((uint32_t)s.bad_reply); kprint(" respuestas rechazadas, ");
    kprint_dec((uint32_t)s.oom); kprint(" sin memoria; maximo "); kprint_dec((uint32_t)s.max_ms); kprint(" ms\n");
    kprint(bad ? "SBX: FALLO (" : "SBX: OK\n"); if (bad) { kdec64(bad); kprint(" comprobaciones)\n"); }
    return bad;
}

/* ---- prueba de extremo a extremo: el NAVEGADOR real carga por red una pagina con las imagenes de prueba (tools/sbx_check.py --web
 * la sirve desde el host); cada imagen debe llegar decodificada por el servicio aislado e identica a la referencia de host. ---- */
#include "web_page.h"
static const char *ext_of(const char *name)
{
    if (name[0] == 'p') return ".png";
    if (name[0] == 'g') return ".gif";
    if (name[0] == 'j') return ".jpg";
    if (name[0] == 'b' && name[1] == 'm') return ".bmp";
    return ".bin";
}
int sbx_webtest(const char *url)
{
    bad = 0;
    kprint("SBXWEB: navegador -> red -> servicio aislado imgdec ("); kprint(url); kprint(")\n");
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    struct wpage *p = wp_new();
    if (!p) { ck(0, "wp_new"); return bad; }
    wp_set_viewport(p, 1024, 768);
    wp_navigate(p, url, 0);
    uint64_t t0 = timer_get_uptime_ms(), idle_since = 0;
    while (timer_get_uptime_ms() - t0 < 60000) {
        wp_tick(p, timer_get_uptime_ms());
        if (p->doc && p->img_pending == 0 && p->img_ok + p->img_fail > 0) { if (!idle_since) idle_since = timer_get_uptime_ms(); else if (timer_get_uptime_ms() - idle_since > 300) break; }
        thread_sleep(5);
    }
    sbx_get_stats(&s1);
    kprint("  imagenes: "); kprint_dec((uint32_t)p->img_ok); kprint(" bien, "); kprint_dec((uint32_t)p->img_fail); kprint(" fallan; llamadas al servicio aislado: ");
    kprint_dec((uint32_t)(s1.runs - s0.runs)); kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    int expected_ok = 0;
    for (int i = 0; i < SBX_TESTIMG_N; ++i) {
        const struct sbx_testimg *t = &sbx_testimg[i];
        if (t->op != 1) continue;
        char src[48]; int k = 0;
        for (const char *q = t->name; *q && k < 40; ++q) src[k++] = *q;
        for (const char *q = ext_of(t->name); *q; ++q) src[k++] = *q;
        src[k] = 0;
        const uint32_t *px; int w = 0, h = 0;
        int got = wp_img_get(p, src, &px, &w, &h, 0, 0);
        uint32_t hs = got ? fnv(px, (size_t)w * h) : 0;
        int ok = t->exp_r == 0 ? (got && w == t->exp_w && h == t->exp_h && hs == t->exp_hash) : !got;
        if (t->exp_r == 0) expected_ok++;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(src); kprint(got ? ": " : ": no decodificada (esperado: "); 
        if (got) { kprint_dec((uint32_t)w); kprint("x"); kprint_dec((uint32_t)h); kprint(" hash=0x"); kprint_hex32(hs); kprint("\n"); }
        else kprint(t->exp_r == 0 ? "decodificada)\n" : "rechazada)\n");
        if (!ok) bad++;
    }
    ck(p->img_ok == expected_ok, "todas las imagenes validas llegaron a la pagina");
    ck(s1.runs - s0.runs >= (uint64_t)expected_ok, "y pasaron por el servicio aislado");
    ck(s1.died == s0.died, "ningun proceso de decodificacion murio");
    ck(s1.svc_calls - s0.svc_calls >= (uint64_t)expected_ok + 1, "la pagina (gzip) se inflo en el servicio zdec y las imagenes en imgdec");
    wp_free(p);
    kprint(bad ? "SBXWEB: FALLO\n" : "SBXWEB: OK\n");
    return bad;
}

/* ---- PARCHE 140: el kernel lee las fuentes COMPRIMIDAS del disco (SRCFS v02); la descompresion ocurre en el servicio zdec ---- */
#include "srcfs.h"
static int has(const char *h, const char *n) { for (; *h; ++h) { const char *a = h, *b = n; while (*a && *b && *a == *b) { ++a; ++b; } if (!*b) return 1; } return 0; }
int sbx_srcfstest(void)
{
    bad = 0;
    kprint("SBXSRC: fuentes comprimidas en disco -> servicio aislado zdec\n");
    static char out[4096];
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    int n = srcfs_ls_filter(0, out, sizeof out);
    kprint("  fuentes en el disco: "); kprint_dec((uint32_t)n); kprint("\n");
    ck(n > 300, "indice SRCFS v02 leido");
    int r = srcfs_cat("core/mem.c", 0, out, sizeof out);
    ck(r > 0 && has(out, "#include") && has(out, "siguiente OFFSET"), "src_cat core/mem.c (primera pagina)");
    int r2 = srcfs_cat("core/mem.c", (uint32_t)r, out, sizeof out);
    ck(r2 > 0, "src_cat segunda pagina (de la cache)");
    r = srcfs_grep("fs/srcfs.c", "SRCFSv02", out, sizeof out);
    ck(r > 0 && has(out, "SRCFSv02"), "src_grep encuentra texto en un archivo comprimido");
    r = srcfs_cat("fonts/vfont_data.h", 1000000, out, sizeof out);
    ck(r > 0, "tabla de datos grande (1,7 MB) tambien cabe y se lee por el final");
    sbx_get_stats(&s1);
    ck(s1.svc_calls - s0.svc_calls >= 3, "y se descomprimio en el servicio aislado");
    kprint(bad ? "SBXSRC: FALLO\n" : "SBXSRC: OK\n");
    return bad;
}

/* ---- PARCHE 142: cliente HTTP aislado (netsvc) y capacidad de red ---- */
#include "web_net.h"
#include "tcp.h"
#include "browser.h"
#include "fb.h"
static void netpat_text(char *o, uint32_t *n)       /* lo mismo que NET_TEXT de tools/sbx_check.py */
{
    uint32_t k = 0;
    for (int i = 0; i < 500; ++i) {
        const char *pre = "SOMA netsvc linea "; while (*pre) o[k++] = *pre++;
        o[k++] = (char)('0' + i / 1000); o[k++] = (char)('0' + i / 100 % 10); o[k++] = (char)('0' + i / 10 % 10); o[k++] = (char)('0' + i % 10); o[k++] = '\n';
    }
    *n = k;
}
static int fetch_eq(const char *base, const char *path, const char *want, uint32_t wn, int want_status, const char *what)
{
    char url[256]; uint32_t k = 0; for (const char *q = base; *q && k < 200; ++q) url[k++] = *q; for (const char *q = path; *q && k < 250; ++q) url[k++] = *q; url[k] = 0;
    struct wn_req rq; uint8_t *z = (uint8_t *)&rq; for (uint32_t i = 0; i < sizeof rq; ++i) z[i] = 0;
    rq.url = url; rq.timeout_ms = 8000; rq.max_body = 1u << 20; rq.want_binary = 1;
    struct wn_resp rs; uint64_t t0 = timer_get_uptime_ms();
    int r = wn_fetch_isolated(&rq, &rs);
    int ok = r == 0 && rs.status == want_status;
    if (ok && want) { ok = rs.len == wn; for (uint32_t i = 0; ok && i < wn; ++i) if (rs.body[i] != want[i]) ok = 0; }
    kprint(ok ? "  OK    " : "  FALLO "); kprint(what); kprint(": HTTP "); kprint_dec((uint32_t)rs.status); kprint(", "); kprint_dec(rs.len);
    kprint(" bytes en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms"); if (rs.err[0]) { kprint(" ("); kprint(rs.err); kprint(")"); } kprint("\n");
    if (!ok) bad++;
    wn_free_isolated(&rs);
    return ok;
}
/* SOMA_317_DESCARGAS_AISLADAS: cancelar una descarga aislada (el visor al cerrarse) */
static volatile int g_net_cancel;
static void net_cancela(void *arg) { (void)arg; thread_sleep(300); g_net_cancel = 1; }
static void net_cancel_test(const char *base)
{
    static char url[256]; uint32_t k = 0;
    for (const char *q = base; *q && k < 200; ++q) url[k++] = *q;
    for (const char *q = "/n/lento"; *q; ++q) url[k++] = *q;
    url[k] = 0;
    struct wn_req rq; uint8_t *z = (uint8_t *)&rq; for (uint32_t i = 0; i < sizeof rq; ++i) z[i] = 0;
    rq.url = url; rq.timeout_ms = 30000; rq.max_body = 1u << 20; rq.cancel = &g_net_cancel;
    static struct wn_resp rs;
    g_net_cancel = 0;
    thread_create("netcan", net_cancela, 0);
    uint64_t t0 = timer_get_uptime_ms();
    int r = wn_fetch_isolated(&rq, &rs);
    uint32_t ms = (uint32_t)(timer_get_uptime_ms() - t0);
    kprint("  descarga cancelada a los 300 ms: devuelve "); kdec64(r); kprint(" ("); kprint(rs.err); kprint(") en "); kprint_dec(ms); kprint(" ms\n");
    ck(r == -13 && !rs.body && ms < 1000, "una descarga aislada se cancela enseguida (el proceso se para)");
    wn_free_isolated(&rs);
    g_net_cancel = 1;
    r = wn_fetch_isolated(&rq, &rs);
    ck(r == -13 && !rs.body, "  y ya cancelada ni empieza");
    wn_free_isolated(&rs);
}
int sbx_nettest(const char *base)
{
    bad = 0;
    kprint("SBXNET: cliente HTTP aislado (netsvc) y capacidad de red\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    static char text[16000]; uint32_t tn; netpat_text(text, &tn);
    fetch_eq(base, "/n/text", text, tn, 200, "texto plano");
    fetch_eq(base, "/n/gz", text, tn, 200, "gzip (inflado dentro de netsvc)");
    fetch_eq(base, "/n/chunk", text, tn, 200, "chunked");
    fetch_eq(base, "/n/redir", text, tn, 200, "redireccion 302");
    fetch_eq(base, "/n/nada", 0, 0, 404, "404");
    {   /* SOMA_323_BSS: el patron de 300 KB se reserva solo al hacer la prueba (antes 292 KiB de BSS en todos los arranques) */
        const uint32_t bn = 300000u;
        char *bin = (char *)kmalloc(bn);
        if (!bin) ck(0, "memoria para el binario de prueba");
        else { for (uint32_t i = 0; i < bn; ++i) bin[i] = (char)((i * 7 + 3) % 251); fetch_eq(base, "/n/bin", bin, bn, 200, "binario de 300 KB"); kfree(bin); }
    }
    net_cancel_test(base);                                  /* SOMA_317_DESCARGAS_AISLADAS */
    /* capacidad */
    uint32_t ip_be = (10u << 24) | (0u << 16) | (2u << 8) | 2u; int port = 0;
    { const char *q = base; while (*q && !(q[0] == ':' && q[1] >= '0' && q[1] <= '9')) q++; if (*q) { q++; while (*q >= '0' && *q <= '9') port = port * 10 + (*q++ - '0'); } }
    static struct sbx_call c; int64_t code;
    sbx_prepare(&c, SBX_EVIL_NET_NOCAP, "x", 1, 65536); int r = sbx_run(&c, EVIL, 2000); code = c.exit_code; sbx_release(&c);
    ck(r == SBX_E_DIED && code == USER_EXIT_DENIED, "sin la capacidad de red, abrir un socket mata el proceso (DENIED)");
    const uint32_t caps = USER_SYS_NET_MASK | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS);
    uint8_t ip[4] = { 10, 0, 2, 2 };
    int kh = tcp_connect(ip, (uint16_t)port, 3000);                    /* conexion del KERNEL */
    ck(kh >= 0, "el kernel abre su propia conexion");
    sbx_prepare(&c, SBX_EVIL_NET_STEAL, "x", 1, 65536); c.in.arg[0] = kh; r = sbx_run_caps(&c, EVIL, 4000, caps); int32_t stolen = c.out.val[0]; sbx_release(&c);
    kprint("  operaciones sobre sockets ajenos no rechazadas: "); kprint_dec((uint32_t)stolen); kprint(" de 37\n");
    ck(r == 0 && stolen == 0, "un proceso con red no puede usar ni cerrar sockets ajenos (EBADF)");
    ck(kh >= 0 && tcp_owner(kh) != 0, "y la conexion del kernel sigue viva");
    if (kh >= 0) tcp_close(kh);
    int owned0 = 0; for (int h = 0; h < 8; ++h) if (tcp_owner(h)) owned0++;     /* (la del kernel puede seguir cerrandose: FIN_WAIT) */
    sbx_prepare(&c, SBX_EVIL_NET_FLOOD, "x", 1, 65536); c.in.arg[0] = (int32_t)ip_be; c.in.arg[1] = port; r = sbx_run_caps(&c, EVIL, 6000, caps);
    int32_t opened = c.out.val[0], fifth = c.out.val[1]; sbx_release(&c);
    kprint("  sockets abiertos de 6 pedidos: "); kprint_dec((uint32_t)opened); kprint("\n");
    ck(r == 0 && opened == USER_MAX_SOCKETS && fifth == -16, "como mucho 4 sockets por proceso (el 5o: EBUSY)");
    thread_sleep(20);
    { int left = 0; for (int h = 0; h < 8; ++h) if (tcp_owner(h)) left++;
      kprint("  sockets con dueno antes/despues del proceso: "); kprint_dec((uint32_t)owned0); kprint("/"); kprint_dec((uint32_t)left); kprint("\n");
      ck(left <= owned0, "al morir el proceso sus 4 sockets se cierran"); }
    sbx_prepare(&c, SBX_EVIL_NET_DNS, "x", 1, 65536); r = sbx_run_caps(&c, EVIL, 4000, caps); int32_t nr = c.out.val[0]; sbx_release(&c);
    ck(r == 0 && nr == 0, "nombres DNS maliciosos rechazados (EINVAL)");
    sbx_svc_stop_all();
    leak_check(f0, "red");
    kprint(bad ? "SBXNET: FALLO\n" : "SBXNET: OK\n");
    return bad;
}

/* ---- SOMA_316_RED_AISLADA: HTTPS (TLS 1.3, certificado verificado) por netsvc, comparado con el cliente del nucleo. tools/sbx_check.py
 * crea con openssl una autoridad y un certificado para 10.0.2.2, sirve /n/ por HTTPS y pasa la autoridad (opt/soma/tlsca). ---- */
#include "fs.h"
#include "tls13.h"
static int tls_get(const char *base, const char *path, int aislado, struct wn_resp *rs)
{
    static char url[256]; uint32_t k = 0;
    for (const char *q = base; *q && k < 200; ++q) url[k++] = *q;
    for (const char *q = path; *q && k < 250; ++q) url[k++] = *q;
    url[k] = 0;
    struct wn_req rq; uint8_t *z = (uint8_t *)&rq; for (uint32_t i = 0; i < sizeof rq; ++i) z[i] = 0;
    rq.url = url; rq.timeout_ms = 8000; rq.max_body = 1u << 20; rq.accept = "text/plain"; rq.direct = 1;
    return aislado ? wn_fetch_isolated(&rq, rs) : wn_fetch(&rq, rs);
}
static int tls_igual(const struct wn_resp *rs, const char *want, uint32_t wn)
{
    if (rs->status != 200 || rs->len != wn || !rs->body) return 0;
    for (uint32_t i = 0; i < wn; ++i) if (rs->body[i] != want[i]) return 0;
    return 1;
}
/* SOMA_325_LLM_AISLADO: el cliente del LLM (proveedor con clave, por HTTPS) va por netsvc. tools/sbx_check.py sirve un falso
 * /v1/chat/completions que exige la clave en Authorization y el modelo en el cuerpo. */
#include "llm.h"
#include "llm_proto.h"
static void llm_aislado_test(const char *base)
{
    kprint(" [cliente del LLM por netsvc (parche 325)]\n");
    static struct llm_conf antes, c;
    antes = *llm_conf_get();
    c = antes; c.prov = LLM_OPENAI;
    int g = llm_conf_set(&c, LLM_OPENAI, "servidor", base) | llm_conf_set(&c, LLM_OPENAI, "modelo", "modelo-prueba")
          | llm_conf_set(&c, LLM_OPENAI, "clave", "sk-prueba-325");
    ck(g == 0 && llm_conf_save(&c) == 0, "configuracion de prueba del LLM (proveedor con clave por HTTPS)");
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    static char out[512]; out[0] = 0;
    int ok = llm_chat("hola", out, sizeof out, 15000);
    sbx_get_stats(&s1);
    kprint("  respuesta: "); kprint(out[0] ? out : llm_last_error); kprint("\n");
    ck(ok == 1 && co_ibusca(out, co_len(out), "servidor falso") != 0, "la peticion llega con su clave y su modelo, y vuelve la respuesta");
    ck(s1.runs > s0.runs, "  por netsvc (un proceso aislado que termina con la peticion), no por el cliente del nucleo");
    (void)llm_conf_set(&c, LLM_OPENAI, "clave", "sk-equivocada"); (void)llm_conf_save(&c);
    out[0] = 0;
    ok = llm_chat("hola", out, sizeof out, 15000);
    kprint("  con otra clave: "); kprint(llm_last_error); kprint("\n");
    ck(ok == 0 && co_ibusca(llm_last_error, co_len(llm_last_error), "clave") != 0, "con una clave equivocada el servidor la rechaza y se dice por que");
    (void)llm_conf_save(&antes);
}
/* SOMA_326_TUNEL_TLS: el tunel TLS de ring 3 (tlstun) que usan el correo y YouTube nativo */
#include "tun_tls.h"
#include "fwcfg.h"
#include "yt_inner.h"
static int tun_lee_todo(struct tun *t, char *o, uint32_t cap, uint32_t ms)
{
    uint32_t n = 0; uint64_t t0 = timer_get_uptime_ms();
    while (n + 1 < cap && timer_get_uptime_ms() - t0 < ms) { int r = tun_recibe(t, o + n, cap - 1 - n, 200); if (r < 0) break; n += (uint32_t)r; }
    o[n] = 0; return (int)n;
}
static int tun_lee_linea(struct tun *t, char *o, uint32_t cap, uint32_t ms)
{
    uint32_t n = 0; uint64_t t0 = timer_get_uptime_ms();
    while (n + 1 < cap && timer_get_uptime_ms() - t0 < ms) { int r = tun_recibe(t, o + n, 1, 200); if (r < 0) break; if (r == 1 && o[n++] == '\n') break; }
    o[n] = 0; return (int)n;
}
static void tun_test(const char *base, int con_anclas)
{
    uint8_t ip[4] = { 10, 0, 2, 2 }; int port = 0;
    { const char *q = base; while (*q && !(q[0] == ':' && q[1] >= '0' && q[1] <= '9')) q++; if (*q) { q++; while (*q >= '0' && *q <= '9') port = port * 10 + (*q++ - '0'); } }
    static char buf[20000], m[160];
    if (!con_anclas) {
        struct tun *t = tun_abre(ip, (uint16_t)port, 1, "10.0.2.2", 8000, m, sizeof m);
        kprint("  tunel sin la autoridad: "); kprint(t ? "abierto" : m); kprint("\n");
        ck(!t && co_ibusca(m, co_len(m), "autoridad") != 0, "tunel: un certificado de una autoridad desconocida se rechaza");
        if (t) tun_cierra(t, 1);
        return;
    }
    kprint(" [tunel TLS de ring 3 (parche 326)]\n");
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    uint64_t t0 = timer_get_uptime_ms();
    struct tun *t = tun_abre(ip, (uint16_t)port, 1, "10.0.2.2", 8000, m, sizeof m);
    if (!t) { kprint("  no abre: "); kprint(m); kprint("\n"); }
    static const char rq[] = "GET /n/text HTTP/1.1\r\nHost: 10.0.2.2\r\nConnection: close\r\n\r\n";
    int n = t && tun_envia(t, rq, sizeof rq - 1) == 0 ? tun_lee_todo(t, buf, sizeof buf, 8000) : -1;
    sbx_get_stats(&s1);
    kprint("  HTTPS por el tunel: "); kdec64(n); kprint(" bytes en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    ck(n > 11500 && co_ibusca(buf, (uint32_t)n, "200 OK") != 0 && co_ibusca(buf, (uint32_t)n, "linea 0499") != 0, "tunel TLS: la pagina entera, con el certificado verificado");
    ck(s1.runs > s0.runs, "  en un proceso de ring 3");
    if (t) tun_cierra(t, 0);
    {   /* el cliente HTTP de YouTube nativo (yt_https.c) de punta a punta, por el tunel */
        static struct yh y; static char u[220]; uint32_t k = 0, tot = 0; uint64_t total = 0;
        for (const char *q = base; *q && k < 180; ++q) u[k++] = *q;
        for (const char *q = "/n/chunk"; *q; ++q) u[k++] = *q;
        u[k] = 0;
        int st = yh_open(&y, u, "GET", "User-Agent: SOMA\r\n", 0, 0, -1, &total, 8000, m, sizeof m);
        if (st == 200) for (;;) { int r = yh_read(&y, (uint8_t *)buf + tot, (uint32_t)sizeof buf - 1 - tot, 3000); if (r <= 0) break; tot += (uint32_t)r; }
        buf[tot] = 0;
        if (st >= 0) yh_close(&y, 0);
        kprint("  YouTube nativo (yh_open, chunked): HTTP "); kdec64(st); kprint(", "); kprint_dec(tot); kprint(" bytes"); if (st < 0) { kprint(" ("); kprint(m); kprint(")"); } kprint("\n");
        ck(st == 200 && tot == 11500 && co_ibusca(buf, tot, "linea 0499") != 0, "el cliente de YouTube nativo va por el tunel (chunked)");
    }
    {   /* varias a la vez: con los servicios vivos tienen que caber (16 procesos) */
        struct tun *v[6]; int ok = 0;
        for (int i = 0; i < 6; ++i) { v[i] = tun_abre(ip, (uint16_t)port, 1, "10.0.2.2", 8000, m, sizeof m); if (v[i]) ok++; }
        for (int i = 0; i < 6; ++i) if (v[i]) tun_cierra(v[i], i & 1);
        kprint("  tuneles abiertos a la vez: "); kprint_dec((uint32_t)ok); kprint(" de 6\n");
        ck(ok == 6, "seis tuneles a la vez (con los servicios aislados vivos)");
    }
    static char sp[16]; int spn = 0;
    if (fwcfg_read_file("opt/soma/starttls", sp, sizeof sp)) for (const char *q = sp; *q >= '0' && *q <= '9'; ++q) spn = spn * 10 + (*q - '0');
    if (spn) {
        struct tun *s = tun_abre(ip, (uint16_t)spn, 0, "10.0.2.2", 8000, m, sizeof m);
        char l1[64], l2[64], l3[64]; l1[0] = l2[0] = l3[0] = 0;
        int ok = s != 0;
        if (ok) { tun_lee_linea(s, l1, sizeof l1, 3000); ok = tun_envia(s, "STARTTLS\r\n", 10) == 0; }
        if (ok) { tun_lee_linea(s, l2, sizeof l2, 3000); ok = tun_starttls(s, "10.0.2.2", m, sizeof m) == 0; }
        if (ok) { ok = tun_envia(s, "hola tunel\r\n", 12) == 0; tun_lee_linea(s, l3, sizeof l3, 3000); }
        kprint("  STARTTLS: "); kprint(l1); kprint("  "); kprint(l2); kprint("  "); kprint(l3[0] ? l3 : m); kprint("\n");
        ck(ok && co_ibusca(l1, co_len(l1), "220") != 0 && co_ibusca(l2, co_len(l2), "adelante") != 0 && co_ibusca(l3, co_len(l3), "eco: hola tunel") != 0,
           "tunel STARTTLS: en claro, despues TLS en la misma conexion (como IMAP y SMTP)");
        if (s) tun_cierra(s, 0);
    } else ck(0, "falta opt/soma/starttls");
}
/* SOMA_327_RED_FUERA: lo ultimo que hablaba por red desde el nucleo: el servidor local del LLM sin clave (salud, agente y chat), el de
 * embeddings de la memoria y el servicio de YouTube de casa. tools/sbx_check.py simula los tres en el puerto HTTP de la prueba. */
#include "memoria.h"
#include "youtube.h"
#include "shell.h"
static void red_fuera_test(void)
{
    static char u[120]; int port = 0;
    if (fwcfg_read_file("opt/soma/url", u, sizeof u)) { const char *q = u + 7; while (*q && *q != ':') q++; if (*q) { q++; while (*q >= '0' && *q <= '9') port = port * 10 + (*q++ - '0'); } }
    if (!port) { ck(0, "falta opt/soma/url"); return; }
    kprint(" [nada de red en el nucleo: LLM local, embeddings y servicio de YouTube (parche 327)]\n");
    static char base[40]; uint32_t k = 0;
    for (const char *q = "http://10.0.2.2:"; *q; ++q) base[k++] = *q;
    { char d[6]; int n = 0; int v = port; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) base[k++] = d[--n]; }
    base[k] = 0;
    static struct llm_conf antes, c;
    antes = *llm_conf_get();
    c = antes; c.prov = LLM_LOCAL;
    ck((llm_conf_set(&c, LLM_LOCAL, "servidor", base) | llm_conf_set(&c, LLM_LOCAL, "clave", "") | llm_conf_set(&c, LLM_LOCAL, "modelo", "local-prueba")) == 0
       && llm_conf_save(&c) == 0, "configuracion de prueba: servidor local http://IP:puerto sin clave");
    struct sbx_stats s0, s1; static char out[512];
    sbx_get_stats(&s0);
    int h = llm_health();
    sbx_get_stats(&s1);
    kprint("  salud: "); kprint(h ? "responde" : llm_last_error); kprint("\n");
    ck(h == 1 && s1.runs > s0.runs, "salud del servidor local (GET /health) por netsvc, no por el cliente del nucleo");
    sbx_get_stats(&s0); uint64_t t0 = timer_get_uptime_ms(); out[0] = 0;
    int ok = llm_chat("hola", out, sizeof out, 20000);
    uint32_t ms = (uint32_t)(timer_get_uptime_ms() - t0);
    sbx_get_stats(&s1);
    kprint("  agente: "); kprint(out[0] ? out : llm_last_error); kprint(" ("); kprint_dec(ms); kprint(" ms)\n");
    ck(ok == 1 && co_ibusca(out, co_len(out), "LLM local") != 0 && s1.runs > s0.runs, "agente con el servidor local (lento, sin clave) por netsvc");
    {
        struct llm_msg m[1] = { { LLM_ROL_USER, "hola" } };
        sbx_get_stats(&s0); out[0] = 0;
        ok = llm_chat_hist("eres una prueba", m, 1, 0, 0, 64, out, sizeof out, 20000, 0);
        sbx_get_stats(&s1);
        kprint("  chat: "); kprint(out[0] ? out : llm_last_error); kprint("\n");
        ck(ok == 1 && co_ibusca(out, co_len(out), "LLM local") != 0 && s1.runs > s0.runs, "aplicacion de chat con el servidor local por netsvc");
        static volatile int cancela = 1;
        t0 = timer_get_uptime_ms();
        ok = llm_chat_hist("eres una prueba", m, 1, 0, 0, 64, out, sizeof out, 20000, &cancela);
        ms = (uint32_t)(timer_get_uptime_ms() - t0);
        kprint("  chat cancelado: "); kprint(llm_last_error); kprint(" ("); kprint_dec(ms); kprint(" ms)\n");
        ck(ok == 0 && ms < 1200, "cancelar el chat corta la espera al servidor local");
    }
    (void)llm_conf_set(&c, LLM_LOCAL, "servidor", "http://10.0.2.2:9"); (void)llm_conf_save(&c);
    h = llm_health();
    kprint("  servidor apagado: "); kprint(llm_last_error); kprint("\n");
    ck(h == 0 && co_ibusca(llm_last_error, co_len(llm_last_error), "no responde") != 0, "con el servidor local apagado se dice que no responde");
    (void)llm_conf_save(&antes);
    uint32_t dim = 0;
    sbx_get_stats(&s0);
    ok = memoria_prueba_emb(base, "hola memoria", &dim);
    sbx_get_stats(&s1);
    kprint("  embeddings: "); kprint(ok ? "vector de " : "fallo "); kprint_dec(dim); kprint(" dimensiones\n");
    ck(ok == 1 && dim == 16 && s1.runs > s0.runs, "embeddings de la memoria (POST /v1/embeddings) por netsvc");
    sbx_get_stats(&s0);
    int st = youtube_prueba_servicio("10.0.2.2", (uint16_t)port, "/yt/buscar?q=gatos", out, sizeof out);
    sbx_get_stats(&s1);
    kprint("  servicio YouTube: HTTP "); kdec64(st); kprint(" "); kprint(out); kprint("\n");
    ck(st == 200 && co_ibusca(out, co_len(out), "resultado falso: gatos") != 0 && s1.runs > s0.runs, "servicio de YouTube de casa por el tunel de ring 3");
    {   /* la orden curl (host puerto ruta): antes el cliente HTTP minimo del nucleo */
        static char cmd[64], co[16384]; uint32_t q = 0;
        for (const char *t = "curl 10.0.2.2 "; *t; ++t) cmd[q++] = *t;
        { char d[6]; int n = 0; int v = port; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) cmd[q++] = d[--n]; }
        for (const char *t = " /n/text"; *t; ++t) cmd[q++] = *t;
        cmd[q] = 0; co[0] = 0;
        sbx_get_stats(&s0);
        (void)dispatch_command(cmd, co, sizeof co);
        sbx_get_stats(&s1);
        kprint("  curl: "); kprint_dec(co_len(co)); kprint(" bytes\n");
        ck(co_ibusca(co, co_len(co), "linea 0") != 0 && s1.runs > s0.runs, "la orden curl por netsvc");
    }
    st = youtube_prueba_servicio("10.0.2.2", 9, "/yt/buscar?q=gatos", out, sizeof out);
    kprint("  servicio YouTube apagado: "); kprint(out); kprint("\n");
    ck(st < 0 && co_ibusca(out, co_len(out), "No hay servicio") != 0, "servicio de YouTube apagado: se dice, sin colgarse");
}
int sbx_tlstest(const char *base, const char *pem, uint32_t pn)
{
    bad = 0;
    kprint("SBXTLS: HTTPS con TLS 1.3 y certificado verificado, en netsvc (ring 3) y en el nucleo ("); kprint(base); kprint(")\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    static char text[16000]; uint32_t tn; netpat_text(text, &tn);
    static struct wn_resp ra, rn;
    ck(vfs_write("/etc/tls_confianza.pem", pem, pn) >= 0, "autoridad de prueba en /etc/tls_confianza.pem");
    static const char *const rutas[4] = { "/n/text", "/n/gz", "/n/chunk", "/n/redir" };
    static const char *const que[4] = { "texto", "gzip", "chunked", "redireccion 302" };
    for (int i = 0; i < 4; ++i) {
        uint64_t t0 = timer_get_uptime_ms();
        int a = tls_get(base, rutas[i], 1, &ra);
        uint32_t ms = (uint32_t)(timer_get_uptime_ms() - t0);
        int n = tls_get(base, rutas[i], 0, &rn);
        int ok = a == 0 && n == 0 && tls_igual(&ra, text, tn) && tls_igual(&rn, text, tn);
        kprint(ok ? "  OK    " : "  FALLO "); kprint(que[i]); kprint(" por HTTPS: netsvc HTTP "); kprint_dec((uint32_t)ra.status); kprint(" ("); kprint_dec(ra.len);
        kprint(" bytes, "); kprint_dec(ms); kprint(" ms), nucleo HTTP "); kprint_dec((uint32_t)rn.status);
        if (ra.err[0]) { kprint(" ["); kprint(ra.err); kprint("]"); }
        kprint("\n");
        if (!ok) bad++;
        wn_free_isolated(&ra); wn_free(&rn);
    }
    llm_aislado_test(base);                                  /* SOMA_325_LLM_AISLADO: con la autoridad de prueba puesta */
    tun_test(base, 1);                                       /* SOMA_326_TUNEL_TLS */
    red_fuera_test();                                        /* SOMA_327_RED_FUERA */
    ck(vfs_write("/etc/tls_confianza.pem", "", 0) >= 0, "sin la autoridad de prueba");
    {
        int a = tls_get(base, "/n/text", 1, &ra), n = tls_get(base, "/n/text", 0, &rn);
        kprint("  netsvc dice: "); kprint(ra.err); kprint("\n");
        tun_test(base, 0);                                   /* SOMA_326_TUNEL_TLS: sin la autoridad */
        ck(a != 0 && n != 0 && !ra.body && ra.err[0] == 'T', "un certificado de una autoridad desconocida se rechaza (en netsvc y en el nucleo)");
        wn_free_isolated(&ra); wn_free(&rn);
    }
    {
        int antes = tls13_verify;
        tls13_verify = 0;                                       /* lo que hace soma.tlsverificar=off */
        int a = tls_get(base, "/n/text", 1, &ra);
        ck(a == 0 && tls_igual(&ra, text, tn), "con soma.tlsverificar=off netsvc cifra sin verificar, como el nucleo");
        wn_free_isolated(&ra);
        tls13_verify = antes;
    }
    sbx_svc_stop_all();
    leak_check(f0, "TLS");
    kprint(bad ? "SBXTLS: FALLO\n" : "SBXTLS: OK\n");
    return bad;
}

/* ---- PARCHE 142: HUELLA del navegador: se dibuja la ventana entera (1024x700) en un bufer con una pagina fija (ref.html) ya cargada.
 * El parche 143 lleva el navegador a ring 3 y exige la MISMA huella: mismo resultado, otro anillo. ---- */
#define WREF_W 1024
#define WREF_H 700
int sbx_webref(const char *url)
{
    bad = 0;
    kprint("SBXWREF: huella del navegador ("); kprint(url); kprint(")\n");
    uint32_t *buf = (uint32_t *)kmalloc((size_t)WREF_W * WREF_H * 4u);
    if (!buf) { ck(0, "memoria"); return bad; }
    browser_init();
    browser_load_url(url);
    uint64_t t0 = timer_get_uptime_ms(), idle = 0;
    uint32_t h1 = 0, h2 = 0;
    while (timer_get_uptime_ms() - t0 < 30000) {
        fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end();
        if (!browser_is_loading()) { if (!idle) idle = timer_get_uptime_ms(); else if (timer_get_uptime_ms() - idle > 1500) break; } else idle = 0;
        thread_sleep(40);
    }
    fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end(); h1 = fnv(buf, (size_t)WREF_W * WREF_H);
    thread_sleep(700);
    fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end(); h2 = fnv(buf, (size_t)WREF_W * WREF_H);
    kprint("  huella 0x"); kprint_hex32(h1); kprint(" / repetida 0x"); kprint_hex32(h2); kprint(" (cargada en "); kprint_dec((uint32_t)(idle - t0)); kprint(" ms)\n");
    ck(h1 == h2, "la huella es estable (pagina quieta)");
    { uint32_t c = buf[(size_t)200 * WREF_W + 30]; kprint("  pixel (30,200) = 0x"); kprint_hex32(c); kprint("\n"); }
    kfree(buf);
    kprint(bad ? "SBXWREF: FALLO\n" : "SBXWREF: OK\n");
    return bad;
}
