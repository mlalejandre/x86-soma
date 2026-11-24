/* SOMA_250_YOUTUBE: ver youtube.h
 *
 * Reparto del trabajo (el planificador del nucleo es cooperativo: ningun hilo interrumpe a otro a media funcion, por eso los
 * datos compartidos de abajo no llevan cerrojos; lo unico que cruza hilos son contadores "pedido/atendido" y punteros que se
 * escriben de una vez):
 *   - la ventana (hilo del escritorio): dibuja, atiende raton y teclado y decide que se pide;
 *   - hilo ytnet: busquedas y miniaturas (peticiones cortas al servicio);
 *   - hilo ytstream: recibe el video y lo mete en la ventana de memoria (yt_ring) de la que lee el reproductor;
 *   - hilos ytia e ytnl (SOMA_251, youtube_ia.c): LLM, ficha, sonido para whisper y subtitulos;
 *   - hilo mediad (media.c): decodifica y sincroniza; aqui solo se le pide el ultimo fotograma (media_frame).
 * Saltar es pedir el video otra vez desde otro segundo: el servicio lo vuelve a emitir desde ahi. */
#include "youtube.h"
#include "yt_proto.h"
#include "fb.h"
#include "sysicon.h"
#include "textfield.h"
#include "mem.h"
#include "thread.h"
#include "idt.h"
#include "console.h"
#include "fs.h"
#include "net.h"
#include "dns.h"
#include "tcp.h"
#include "web_img.h"
#include "media.h"
#include "llm.h"            /* SOMA_251: buscador con IA, resumen y preguntas */
#include "llm_proto.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "clipboard.h"
#include "chat_md.h"        /* SOMA_286_YOUTUBE: la pestana IA en Markdown con LaTeX y minutos que se pulsan */
#include "web_font.h"
#ifdef __x86_64__
#include "voz_sys.h"        /* SOMA_251: transcripcion con whisper */
#endif

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);
extern int keyboard_last_mods(void);

#define YT_RED      0x00FF4444u
#define YT_STAGE    0x00060910u
#define CONF_PATH   "/etc/secreto/youtube.conf"
#define RING_BLOCKS 16                      /* 16 MiB de video en memoria (unos 25 s a 360p) */
#define UI_GONE_MS  4000u                   /* sin ventana durante tanto tiempo: se corta la descarga */
#define NET_CAP_250 65536u

enum { V_BROWSE = 0, V_WATCH };
enum { NS_IDLE = 0, NS_SEARCH, NS_THUMBS, NS_ERROR };
enum { PH_IDLE = 0, PH_LOADING, PH_PLAY, PH_ENDED, PH_ERROR, PH_HELD };

/* ---- configuracion ---- */
static struct yt_conf conf;
static int conf_loaded;

/* ---- ventana ---- */
static struct tfield tf; static char query[160]; static int tf_ready_, tf_focus;
static int view, scroll_px, side_scroll, hover = -1;
static char note[160];                               /* aviso de una linea bajo la barra */

/* ---- resultados (los toca solo el hilo de la ventana, salvo las miniaturas: ver ytnet) ---- */
static struct yt_item items[YT_MAX_ITEMS]; static struct wimg thumbs[YT_MAX_ITEMS]; static int nitems;
static uint32_t items_gen;                           /* busqueda a la que pertenecen */
static volatile uint32_t thumbs_seq;                 /* sube con cada miniatura lista */
static char last_query[160];

/* ---- hilo ytnet ---- */
static volatile int net_alive, net_state;
static volatile uint32_t net_req, net_done;
static char net_query[160], net_err[160];
static char net_queries[YT_AI_QMAX][YT_AI_QLEN]; static int net_nq;        /* SOMA_251: una busqueda puede ser varias (las propone el LLM) */
static struct yt_item stage[YT_MAX_ITEMS]; static volatile int stage_n; static volatile uint32_t stage_gen; static volatile int stage_ready;
static uint8_t *netbuf;

/* ---- hilo ytstream ---- */
static volatile int str_alive;
static volatile uint32_t str_req, str_done, str_live, str_errgen;   /* pedido, terminado, entregado al reproductor, fallido */
static char str_id[YT_ID_LEN + 1]; static uint32_t str_t; static char str_title[128];
static char str_err[160];
static struct yt_ring ring; static volatile int ring_eof;
static volatile uint64_t str_bytes;
static uint8_t *rxtmp;
static volatile uint64_t ui_seen_ms;

/* ---- reproduccion (hilo de la ventana) ---- */
static struct yt_item now_it; static int cur_idx = -1;
static uint32_t t0;                                  /* segundo del video en el que empieza el flujo actual */
static uint32_t last_pos;                            /* ultima posicion conocida (s) */
static int held; static uint32_t held_t;             /* parado a la espera de que el usuario pulse reproducir */
static uint32_t ended_gen;
static uint32_t last_sig;

/* --- SOMA_251: panel lateral con pestanas y caja de preguntas */
enum { T_NEXT = 0, T_INFO, T_TEXT, T_AI, T_COUNT };
static int tab;
static struct tfield tfq; static char qbuf[300]; static int tfq_ready, q_focus;
static int info_scroll, tx_scroll, chat_scroll, chat_follow = 1, tx_y0;
/* SOMA_286_YOUTUBE: busqueda pedida desde el orbe: los resultados (o el resumen de la IA) se leen en voz alta */
static volatile int voz_busqueda; static uint32_t voz_gen;
static volatile int voz_pend;                                    /* lo pide la orden (youtube_hablar): pasa a la busqueda o a la IA */
static void voz_di(const char *t);
struct ybar { int x, y, h, th, ty, max, on; int *val; };         /* SOMA_286_YOUTUBE: barras de desplazamiento que se arrastran */
enum { YB_GRID = 0, YB_SIDE, YB_INFO, YB_TEXT, YB_CHAT, YB_N };
static struct ybar yb[YB_N]; static int yb_drag = -1, yb_grab, ym_mx = -1, ym_my = -1;
static void ybar(int k, int x, int y, int h, int max, int *val);

static uint64_t ms(void) { return timer_get_uptime_ms(); }

/* ================================================================== configuracion */
static void conf_load(void)
{
    if (conf_loaded) return;
    conf_loaded = 1;
    char b[400]; int n = vfs_read(CONF_PATH, b, sizeof b);
    if (n > 0) yt_conf_parse(b, (uint32_t)n, &conf); else yt_conf_default(&conf);
}
static void conf_save(void)
{
    char b[400]; uint32_t n = yt_conf_format(&conf, b, sizeof b);
    vfs_write(CONF_PATH, b, n);
}
static void server_name(char *o, uint32_t max)
{
    uint32_t p = 0;
    conf_load();
    if (conf.host[0]) yt_put(o, max, &p, conf.host);
    else for (int i = 0; i < 4; ++i) { yt_put_dec(o, max, &p, net_gateway[i]); if (i < 3) yt_put(o, max, &p, "."); }
    yt_put(o, max, &p, ":"); yt_put_dec(o, max, &p, conf.port);
}

/* ================================================================== cliente HTTP minimo (peticiones al servicio) */
static int y_connect(char *err, uint32_t emax)
{
    uint8_t ip[4]; uint32_t p = 0;
    conf_load();
    if (!conf.host[0]) for (int i = 0; i < 4; ++i) ip[i] = net_gateway[i];
    else if (!yt_parse_ip(conf.host, ip)) {
        net_client_lock(); int ok = dns_resolve(conf.host, ip, 4000); net_client_unlock();
        if (!ok) { yt_put(err, emax, &p, "No se encuentra el equipo "); yt_put(err, emax, &p, conf.host); return -1; }
    }
    net_client_lock(); int h = tcp_connect(ip, conf.port, 5000); net_client_unlock();
    if (h < 0) {
        char sn[96]; server_name(sn, sizeof sn);
        yt_put(err, emax, &p, "No hay servicio YouTube en "); yt_put(err, emax, &p, sn);
    }
    return h;
}
static int y_send_get(int h, const char *path)
{
    char rq[1100]; uint32_t p = 0;
    yt_put(rq, sizeof rq, &p, "GET "); yt_put(rq, sizeof rq, &p, path);
    yt_put(rq, sizeof rq, &p, " HTTP/1.0\r\nHost: soma\r\nUser-Agent: SOMA-YouTube/1\r\nConnection: close\r\n\r\n");
    return tcp_send(h, rq, p) == (int)p ? 0 : -1;
}
/* Mensaje de error del servicio (texto plano corto) -> una linea limpia */
static void y_errtext(char *o, uint32_t max, const char *body, uint32_t n, int status)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < n && p + 1 < max; ++i) { char c = body[i]; if (c == '\r' || c == '\n' || c == '\t') c = ' '; if ((unsigned char)c >= 32) o[p++] = c; }
    o[p] = 0;
    if (!p) { yt_put(o, max, &p, "El servicio ha respondido con el error "); yt_put_dec(o, max, &p, (uint64_t)status); }
}
/* GET completo en netbuf. Devuelve el estado HTTP (cuerpo en *body, *len) o <0. Se abandona si cambia *gen. */
static int y_get_buf(uint8_t *nb, uint32_t ncap, const char *path, char **body, uint32_t *len, uint32_t timeout_ms, const volatile uint32_t *gen, uint32_t my, char *err, uint32_t emax)
{
    uint32_t ep = 0; err[0] = 0;
    int h = y_connect(err, emax);
    if (h < 0) return -1;
    if (y_send_get(h, path) != 0) { tcp_close(h); yt_put(err, emax, &ep, "No se pudo enviar la peticion al servicio"); return -1; }
    uint32_t total = 0; uint64_t t_start = ms(); int r = 0;
    while (total < ncap - 1) {
        if (*gen != my) { r = -2; break; }
        if (ms() - t_start > timeout_ms) { r = -3; break; }
        uint32_t room = ncap - 1 - total;
        int n = tcp_recv(h, nb + total, (uint16_t)(room > 16384 ? 16384 : room), 60);
        if (n > 0) total += (uint32_t)n;
        else if (n == TCP_EOF || n == TCP_ERR) break;
    }
    if (r) tcp_abort(h); else tcp_close(h);
    if (r == -2) return -2;
    if (r == -3) { yt_put(err, emax, &ep, "El servicio YouTube no responde (tiempo agotado)"); return -1; }
    int status = 0; uint32_t off = 0;
    if (yt_http_head((const char *)nb, total, &status, &off) != 1) { yt_put(err, emax, &ep, "Respuesta no valida del servicio YouTube"); return -1; }
    nb[total] = 0;
    *body = (char *)nb + off; *len = total - off;
    if (status != 200) y_errtext(err, emax, *body, *len > 150 ? 150 : *len, status);
    return status;
}
static int y_get(const char *path, char **body, uint32_t *len, uint32_t timeout_ms, const volatile uint32_t *gen, uint32_t my, char *err, uint32_t emax)
{
    return y_get_buf(netbuf, NET_CAP_250, path, body, len, timeout_ms, gen, my, err, emax);
}

/* ================================================================== hilo ytnet: busquedas y miniaturas */
static int ui_gone(void) { return ms() - ui_seen_ms > UI_GONE_MS; }

/* SOMA_252: modo nativo (youtube_nativo.c, incluido mas abajo) */
static int native_mode(void);
static int nat_search(const char *q, struct yt_item *out, int max, const volatile uint32_t *gen, uint32_t my, char *err, uint32_t emax);
static int nat_thumb(const char *id, uint8_t *buf, uint32_t cap, uint32_t *len, const volatile uint32_t *gen, uint32_t my);

static void ytnet_main(void *arg)
{
    (void)arg;
    uint64_t idle_since = ms();
    for (;;) {
        if (net_done == net_req) {
            if (ui_gone() && ms() - idle_since > 10000u) break;
            thread_sleep(20); continue;
        }
        uint32_t g = net_req;
        static char qs[YT_AI_QMAX][YT_AI_QLEN]; static struct yt_item part[YT_AI_QMAX][12]; int pn[YT_AI_QMAX];
        int nq = net_nq > 0 ? (net_nq > YT_AI_QMAX ? YT_AI_QMAX : net_nq) : 1;
        if (net_nq > 0) for (int i = 0; i < nq; ++i) yt_scopy(qs[i], YT_AI_QLEN, net_queries[i]);
        else yt_scopy(qs[0], YT_AI_QLEN, net_query);
        char path[900], err[160]; char *body; uint32_t len = 0;
        net_state = NS_SEARCH;
        int st = 0, n = -1, bad = 0, gone = 0; char first_err[160]; first_err[0] = 0;
        for (int k = 0; k < nq && !gone; ++k) {                  /* SOMA_251: una peticion por busqueda; luego se intercalan */
            pn[k] = 0;
            yt_path_search(path, sizeof path, &conf, nq == 1 && net_nq <= 0 ? net_query : qs[k], nq == 1 ? 12 : 8);
            int m;
            if (native_mode()) {                                 /* SOMA_252: se le pregunta a YouTube directamente */
                m = nat_search(nq == 1 && net_nq <= 0 ? net_query : qs[k], part[k], nq == 1 ? 12 : 8, &net_req, g, err, sizeof err);
                if (m == -2) { gone = 1; break; }
                st = m >= 0 ? 200 : 0;
            } else {
            st = y_get(path, &body, &len, 70000u, &net_req, g, err, sizeof err);
            if (st == -2) { gone = 1; break; }
            m = st == 200 ? yt_parse_results(body, len, part[k], 12) : -1;
            }
            if (m < 0) { bad++; if (!first_err[0]) yt_scopy(first_err, sizeof first_err, st == 200 ? "El servicio YouTube ha respondido algo inesperado" : err); }
            else pn[k] = m;
        }
        if (gone) continue;                                      /* ha llegado otra busqueda */
        if (bad < nq) {
            n = 0;
            for (int r = 0; r < 12; ++r) for (int k = 0; k < nq; ++k) if (r < pn[k]) n = yt_merge(stage, n, YT_MAX_ITEMS, &part[k][r], 1);
        }
        if (n < 0) {
            yt_scopy(net_err, sizeof net_err, first_err);
            net_state = NS_ERROR; net_done = g; idle_since = ms();
            continue;
        }
        stage_n = n; stage_gen = g; stage_ready = 1;             /* la ventana los adopta en su proximo tick */
        net_state = NS_THUMBS;
        for (int i = 0; i < n && net_req == g && !ui_gone(); ++i) {
            char id[YT_ID_LEN + 1]; yt_scopy(id, sizeof id, stage[i].id);
            yt_path_thumb(path, sizeof path, &conf, id);
            if (native_mode()) { body = (char *)netbuf; st = nat_thumb(id, netbuf, NET_CAP_250, &len, &net_req, g); }
            else st = y_get(path, &body, &len, 15000u, &net_req, g, err, sizeof err);
            if (st != 200 || len < 64) continue;
            struct wimg im; im.px = 0; im.delay = 0; im.w = im.h = im.nw = im.nh = im.frames = 0;
            if (wi_decode_fit((const uint8_t *)body, len, &im, 256, 144) != 0 || !im.px || im.w <= 0 || im.h <= 0) { wi_free(&im); continue; }
            for (int w = 0; w < 200 && net_req == g && items_gen != g; ++w) thread_sleep(10);   /* a que la ventana adopte la lista */
            if (net_req == g && items_gen == g && i < nitems && yt_seq(items[i].id, id) && !thumbs[i].px) { thumbs[i] = im; thumbs_seq++; }
            else wi_free(&im);
        }
        if (net_req == g) net_state = NS_IDLE;
        net_done = g; idle_since = ms();
    }
    net_alive = 0;
}

/* ================================================================== hilo ytstream: el video */
static int64_t src_read(void *c, uint64_t off, void *buf, uint32_t n) { (void)c; return yt_ring_read(&ring, off, (uint8_t *)buf, n); }
static uint64_t src_avail(void *c) { (void)c; return ring.end; }
static int src_eof(void *c) { (void)c; return ring_eof; }

static int ring_alloc(void)
{
    if (ring.nblk) return 1;
    if (!ring.hdr) ring.hdr = (uint8_t *)kmalloc(YT_HDR_KEEP);
    if (!ring.hdr) return 0;
    uint32_t n = 0;
    while (n < RING_BLOCKS) { ring.blk[n] = (uint8_t *)kmalloc(YT_BLK); if (!ring.blk[n]) break; n++; }
    if (n < 4) { while (n) kfree(ring.blk[--n]); return 0; }
    ring.nblk = n; ring.margin = n >= 8 ? YT_BLK : YT_BLK / 2;
    return 1;
}
static void ring_free(void)
{
    for (uint32_t i = 0; i < ring.nblk; ++i) { kfree(ring.blk[i]); ring.blk[i] = 0; }
    ring.nblk = 0;
    if (ring.hdr) { kfree(ring.hdr); ring.hdr = 0; }
    ring.base = ring.end = 0; ring.hdr_n = 0;
}
static void media_release(void)                      /* el reproductor deja de leer el flujo anterior */
{
    if (!media_stream_busy()) return;
    media_stop();
    for (int i = 0; i < 400 && media_stream_busy(); ++i) thread_sleep(10);
}
static void str_fail(uint32_t g, const char *m) { yt_scopy(str_err, sizeof str_err, m); str_errgen = g; }

#ifdef __x86_64__
#define SOMA_ARCH_252 "x86_64"
#else
#define SOMA_ARCH_252 "otra"
#endif
#include "youtube_nativo.c"  /* SOMA_252 */

static void stream_job(uint32_t g)
{
    char id[YT_ID_LEN + 1], title[128], path[400], err[160];
    yt_scopy(id, sizeof id, str_id); yt_scopy(title, sizeof title, str_title);
    uint32_t t = str_t;
    if (native_mode() || yt_seq(id, NAT_DIRECT_ID)) { ring_eof = 1; native_job(g, id, t); return; }      /* SOMA_252: sin servicio externo */
    nat_stop();
    media_release();
    ring_eof = 1; str_bytes = 0;
    if (!id[0]) return;                                          /* "parar": solo habia que soltar el flujo */
    if (!ring_alloc()) { str_fail(g, "Sin memoria para el video (cierra alguna aplicacion)"); return; }
    yt_ring_reset(&ring, ms()); ring_eof = 0;
    yt_path_video(path, sizeof path, &conf, id, t);
    int h = y_connect(err, sizeof err);
    if (h < 0) { ring_eof = 1; str_fail(g, err); return; }
    if (y_send_get(h, path) != 0) { tcp_close(h); ring_eof = 1; str_fail(g, "No se pudo enviar la peticion al servicio"); return; }

    /* cabeceras HTTP: el servicio no contesta hasta tener el video abierto (resolver la direccion tarda unos segundos) */
    char hd[2048]; uint32_t hn = 0; int status = 0; uint32_t off = 0, got_head = 0; uint64_t t_start = ms();
    while (!got_head) {
        if (str_req != g || ui_gone()) { tcp_abort(h); ring_eof = 1; return; }
        if (ms() - t_start > 120000u) { tcp_abort(h); ring_eof = 1; str_fail(g, "El servicio YouTube no responde (tiempo agotado)"); return; }
        if (hn >= sizeof hd - 1) { tcp_abort(h); ring_eof = 1; str_fail(g, "Respuesta no valida del servicio YouTube"); return; }
        int n = tcp_recv(h, hd + hn, (uint16_t)(sizeof hd - 1 - hn), 60);
        if (n > 0) {
            hn += (uint32_t)n;
            int r = yt_http_head(hd, hn, &status, &off);
            if (r < 0) { tcp_abort(h); ring_eof = 1; str_fail(g, "Respuesta no valida del servicio YouTube"); return; }
            got_head = r == 1;
        } else if (n == TCP_EOF || n == TCP_ERR) { tcp_close(h); ring_eof = 1; str_fail(g, "El servicio YouTube ha cortado la conexion"); return; }
    }
    if (status != 200) {
        for (int k = 0; k < 20 && hn < sizeof hd - 1; ++k) { int n = tcp_recv(h, hd + hn, (uint16_t)(sizeof hd - 1 - hn), 50); if (n > 0) hn += (uint32_t)n; else if (n < 0) break; }
        tcp_close(h); ring_eof = 1;
        y_errtext(err, sizeof err, hd + off, hn - off > 150 ? 150 : hn - off, status);
        str_fail(g, err);
        return;
    }
    if (hn > off) { yt_ring_put(&ring, (const uint8_t *)hd + off, hn - off); str_bytes = ring.end; }

    int started = 0, cut = 1; uint64_t last_data = ms(); const char *why = "fin del video";
    for (;;) {
        if (str_req != g) { why = "sustituido"; break; }
        if (ui_gone()) { why = "ventana cerrada"; break; }
        uint64_t sp = yt_ring_space(&ring);
        if (sp < 2048) {
            yt_ring_trim(&ring, ms());
            sp = yt_ring_space(&ring);
            if (sp < 2048) { thread_sleep(10); last_data = ms(); continue; }     /* memoria llena: el reproductor aun no ha avanzado */
        }
        int n = tcp_recv(h, rxtmp, (uint16_t)(sp > 16384 ? 16384 : sp), 30);
        if (n > 0) {
            yt_ring_put(&ring, rxtmp, (uint32_t)n); str_bytes = ring.end; last_data = ms();
            if (!started && ring.end >= 4096 && str_req == g) {
                static struct media_stream src;
                src.read = src_read; src.avail = src_avail; src.eof = src_eof; src.ctx = 0;
                if (media_play_stream(&src, title) != 0) { str_fail(g, "El reproductor no ha aceptado el video"); why = "reproductor ocupado"; break; }
                started = 1; str_live = g;
            }
        } else if (n == TCP_EOF) { cut = 0; break; }
        else if (n == TCP_ERR) { why = "conexion cortada"; break; }
        else if (ms() - last_data > 45000u) { if (!started) str_fail(g, "El video no llega (el servicio se ha quedado parado)"); why = "sin datos durante 45 s"; break; }
    }
    ring_eof = 1;                                                /* antes de cerrar: tcp_close cede la CPU y el reproductor debe saberlo ya */
    if (cut) tcp_abort(h); else tcp_close(h);                    /* cancelado a medias: sin esperar al servicio, que sigue enviando */
    {   /* una linea por video en la consola, como hace SOMA Media */
        char l[200]; uint32_t q = 0;
        yt_put(l, sizeof l, &q, "[youtube] "); yt_put(l, sizeof l, &q, id); yt_put(l, sizeof l, &q, " desde "); yt_put_dec(l, sizeof l, &q, t);
        yt_put(l, sizeof l, &q, " s: "); yt_put_dec(l, sizeof l, &q, ring.end / 1024u); yt_put(l, sizeof l, &q, " KiB recibidos, "); yt_put(l, sizeof l, &q, why); yt_put(l, sizeof l, &q, "\n");
        kprint(l);
    }
    if (!started && str_req == g && str_errgen != g && !ui_gone()) str_fail(g, "El servicio no ha enviado ningun video");
}

static void ytstream_main(void *arg)
{
    (void)arg;
    uint64_t idle_since = ms();
    for (;;) {
        if (str_done == str_req) {
            if (ui_gone() && ms() - idle_since > 10000u) {       /* la ventana lleva un rato cerrada: se devuelve la memoria y se acaba */
                media_release();
                if (str_done != str_req) continue;               /* han pedido algo mientras se esperaba al reproductor */
                ring_free();
                str_alive = 0;                                   /* sin ceder la CPU desde la comprobacion: no se pierde ninguna peticion */
                return;
            }
            thread_sleep(20); continue;
        }
        uint32_t g = str_req;
        stream_job(g);
        str_done = g; idle_since = ms();
    }
}

/* ================================================================== peticiones desde la ventana */
static void threads_up(void)
{
    ui_seen_ms = ms();
    if (!netbuf) netbuf = (uint8_t *)kmalloc(NET_CAP_250);
    if (!rxtmp) rxtmp = (uint8_t *)kmalloc(16384);
}
#include "youtube_ia.c"     /* SOMA_251 */

static void stream_request(const char *id, uint32_t t, const char *title)
{
    threads_up();
    if (!rxtmp) { yt_scopy(note, sizeof note, "Sin memoria"); return; }
    yt_scopy(str_id, sizeof str_id, id); yt_scopy(str_title, sizeof str_title, title); str_t = t;
    str_req++;
    if (media_stream_busy()) media_stop();                       /* lo que sonaba se calla ya, sin esperar al hilo del video */
    nat_stop();
    if (!str_alive) { str_alive = 1; if (!thread_create("ytstream", ytstream_main, 0)) { str_alive = 0; yt_scopy(note, sizeof note, "No se pudo crear el hilo del video"); } }
}
static void play_at(uint32_t t)
{
    if (!now_it.id[0]) return;
    if (now_it.dur && t >= now_it.dur) t = now_it.dur > 2 ? now_it.dur - 2 : 0;
    if (pl_seek_live(t)) { t0 = 0; last_pos = t; held = 0; note[0] = 0; ended_gen = str_req - 1; return; }      /* SOMA_252: el nativo salta sin volver a pedir el video */
    t0 = native_mode() || yt_seq(now_it.id, NAT_DIRECT_ID) ? 0 : t; last_pos = t; held = 0; note[0] = 0;
    stream_request(now_it.id, t, now_it.title);
}
static void play_item(int i)
{
    if (i < 0 || i >= nitems) return;
    now_it = items[i]; cur_idx = i; view = V_WATCH; tf_focus = 0; side_scroll = 0;
    ia_video_reset(now_it.id); info_scroll = tx_scroll = chat_scroll = 0; chat_follow = 1;
    if (tab != T_NEXT && vd_state == 0 && ia_next == J_NONE) (void)ia_start(J_INFO, 0, 0);
    play_at(0);
}
static void stop_playback(void)
{
    if (str_alive && (str_done != str_req || media_stream_busy() || nat_on)) stream_request("", 0, "");
    held = 0; now_it.id[0] = 0; cur_idx = -1;
}
static void do_search(const char *q)
{
    voz_busqueda = 0; voz_gen = 0;                               /* SOMA_286_YOUTUBE: lo escrito en la ventana no se lee en voz alta */
    while (*q == ' ') q++;
    if (!*q) return;
    conf_load();
    if (yt_starts(q, "servidor ")) {                             /* atajo: configurar sin salir de la ventana */
        char h[64]; uint16_t po;
        if (yt_host_port(q + 9, h, sizeof h, &po)) { yt_scopy(conf.host, sizeof conf.host, h); conf.port = po; conf_save(); yt_scopy(note, sizeof note, "Servicio YouTube cambiado. Escribe una busqueda."); }
        else yt_scopy(note, sizeof note, "Direccion no valida. Ejemplo: servidor 192.168.1.20:8097");
        return;
    }
    if (yt_starts(q, "clave ")) { yt_scopy(conf.key, sizeof conf.key, q + 6); conf_save(); yt_scopy(note, sizeof note, "Clave guardada."); return; }
    if (yt_seq(q, "modo nativo") || yt_seq(q, "modo servicio")) {                                   /* SOMA_252 */
        conf.native = q[5] == 'n'; conf_save(); stop_playback();
        yt_scopy(note, sizeof note, conf.native ? "Modo nativo: SOMA habla con YouTube directamente (360p)." : "Modo servicio: el video lo prepara tools/yt_service.py en otro equipo.");
        return;
    }
    if (yt_seq(q, "ia on") || yt_seq(q, "ia off")) {             /* SOMA_251 */
        conf.ai = q[4] == 'n'; conf_save();
        yt_scopy(note, sizeof note, conf.ai ? "Buscador con IA activado: describe lo que quieres ver." : "Buscador con IA desactivado: se busca el texto tal cual.");
        return;
    }
    threads_up();
    if (!netbuf) { yt_scopy(note, sizeof note, "Sin memoria"); return; }
    if (conf.ai) {                                               /* SOMA_251: el texto pasa antes por el LLM (hilo ytia) */
        yt_scopy(last_query, sizeof last_query, q); note[0] = 0;
        ai_state = AI_THINK; ai_nq = 0; ai_sum[0] = 0; yt_scopy(ai_ask, sizeof ai_ask, q);
        nl_start(q);
        view = V_BROWSE; scroll_px = 0;
        return;
    }
    if (nl_run) nl_cancel = 1;
    ai_state = AI_OFF;
    search_launch(0, 0, q);
}
/* Lanza la busqueda en el hilo ytnet: el texto tal cual (n == 0) o las n busquedas que ha propuesto el LLM. */
static void search_launch(const char q[][YT_AI_QLEN], int n, const char *shown)
{
    threads_up();
    if (!netbuf) { yt_scopy(note, sizeof note, "Sin memoria"); return; }
    net_nq = n > YT_AI_QMAX ? YT_AI_QMAX : n;
    for (int i = 0; i < net_nq; ++i) yt_scopy(net_queries[i], YT_AI_QLEN, q[i]);
    yt_scopy(net_query, sizeof net_query, shown); yt_scopy(last_query, sizeof last_query, shown);
    note[0] = 0; net_state = NS_SEARCH;
    net_req++;
    if (!net_alive) { net_alive = 1; if (!thread_create("ytnet", ytnet_main, 0)) { net_alive = 0; net_state = NS_ERROR; yt_scopy(net_err, sizeof net_err, "No se pudo crear el hilo de red"); } }
    view = V_BROWSE; scroll_px = 0;
}
void youtube_search(const char *q)
{
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    tf_set(&tf, q);
    do_search(q);
    tf_focus = 0;
    if (voz_pend) { voz_busqueda = 1; voz_pend = 0; }           /* SOMA_286_YOUTUBE: pedida desde el orbe */
}
void youtube_hablar(int on) { voz_pend = on ? 1 : 0; }
int youtube_activo(void);
int youtube_activo(void) { return now_it.id[0] && ms() - ui_seen_ms < UI_GONE_MS; }   /* SOMA_286_YOUTUBE: para el LCM (pausa el video...) */

void youtube_init(void)
{
    { extern int (*lcm_youtube_activo)(void); lcm_youtube_activo = youtube_activo; }   /* SOMA_293_MAC: el LCM pregunta si hay video abierto */
    conf_load();
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    ui_seen_ms = ms();
    if (!nitems && view == V_BROWSE) tf_focus = 1;
}

/* ================================================================== estado de la reproduccion */
static int phase_of(const struct media_info *mi)
{
    if (!now_it.id[0]) return PH_IDLE;
    if (held) return PH_HELD;
    if (str_errgen == str_req) return PH_ERROR;
    if (str_live != str_req) return PH_LOADING;
    if (!mi->stream) return PH_LOADING;
    if (mi->state == 5) return PH_ERROR;
    if (mi->state == 4) return PH_ENDED;
    return PH_PLAY;
}
/* ---- SOMA_286_YOUTUBE: lo que se dice en voz alta cuando la busqueda vino del orbe */
static void voz_di(const char *t)
{
#ifdef __x86_64__
    if (t && *t) (void)voz_speak_reply(t);
#else
    (void)t;
#endif
}
static void dur_hablada(char *o, uint32_t max, uint32_t *p, uint32_t s)
{
    uint32_t h = s / 3600, m = (s / 60) % 60;
    if (h) { yt_put_dec(o, max, p, h); yt_put(o, max, p, h == 1 ? " hora" : " horas"); if (m) { yt_put(o, max, p, " y "); yt_put_dec(o, max, p, m); yt_put(o, max, p, " minutos"); } return; }
    if (m) { yt_put_dec(o, max, p, m); yt_put(o, max, p, m == 1 ? " minuto" : " minutos"); return; }
    yt_put_dec(o, max, p, s); yt_put(o, max, p, " segundos");
}
static void titulo_corto(char *o, uint32_t max, uint32_t *p, const char *t)
{
    char b[80]; uint32_t n = 0; while (t[n] && n < 70) { b[n] = t[n]; n++; }
    if (t[n]) { while (n > 30 && b[n - 1] != ' ') n--; while (n && b[n - 1] == ' ') n--; }
    while (n > 0 && ((unsigned char)b[n] & 0xC0) == 0x80) n--;
    b[n] = 0; yt_put(o, max, p, b);
}
static void voz_resultados(void)
{
    if (!voz_busqueda || !voz_gen || voz_gen != items_gen) return;
    if (conf.ai && (ai_state == AI_THINK || ai_state == AI_SEARCH || ai_state == AI_SUMMING)) return;   /* se espera el resumen del LLM */
    char t[900]; uint32_t p = 0;
    if (conf.ai && ai_state == AI_DONE && ai_sum[0]) {                                                  /* LLM */
        yt_put(t, sizeof t, &p, ai_sum); yt_put(t, sizeof t, &p, " Dime cuál pongo, por ejemplo: pon el primero.");
    } else if (!nitems) {                                                                               /* plantilla (LCM) */
        yt_put(t, sizeof t, &p, "No he encontrado vídeos de "); yt_put(t, sizeof t, &p, last_query); yt_put(t, sizeof t, &p, ".");
    } else {
        static const char *const ord[3] = { "El primero es ", "El segundo, ", "Y el tercero, " };
        yt_put(t, sizeof t, &p, "He encontrado "); yt_put_dec(t, sizeof t, &p, (uint64_t)nitems); yt_put(t, sizeof t, &p, nitems == 1 ? " vídeo" : " vídeos");
        yt_put(t, sizeof t, &p, " de "); yt_put(t, sizeof t, &p, last_query); yt_put(t, sizeof t, &p, ". ");
        for (int i = 0; i < 3 && i < nitems; ++i) {
            yt_put(t, sizeof t, &p, ord[i]); yt_put(t, sizeof t, &p, "«"); titulo_corto(t, sizeof t, &p, items[i].title); yt_put(t, sizeof t, &p, "»");
            if (i == 0 && items[i].channel[0]) { yt_put(t, sizeof t, &p, ", de "); yt_put(t, sizeof t, &p, items[i].channel); }
            if (i == 0 && items[i].dur) { yt_put(t, sizeof t, &p, ", dura "); dur_hablada(t, sizeof t, &p, items[i].dur); }
            yt_put(t, sizeof t, &p, ". ");
        }
        yt_put(t, sizeof t, &p, "Dime cuál pongo, por ejemplo: pon el primero.");
    }
    voz_busqueda = 0; voz_gen = 0;
    voz_di(t);
}

static void adopt_results(void)
{
    for (int i = 0; i < YT_MAX_ITEMS; ++i) wi_free(&thumbs[i]);
    nitems = stage_n > YT_MAX_ITEMS ? YT_MAX_ITEMS : stage_n;
    for (int i = 0; i < nitems; ++i) items[i] = stage[i];
    items_gen = stage_gen; stage_ready = 0;
    scroll_px = 0; side_scroll = 0; hover = -1;
    cur_idx = -1;
    for (int i = 0; i < nitems; ++i) if (now_it.id[0] && yt_seq(items[i].id, now_it.id)) cur_idx = i;
    if (!nitems) yt_scopy(note, sizeof note, "No hay resultados para esa busqueda.");
    if (voz_busqueda) voz_gen = items_gen;                     /* SOMA_286_YOUTUBE: se leera en youtube_tick */
}

int youtube_tick(void)
{
    uint64_t t = ms();
    ui_seen_ms = t;
    int ch = 0;
    if (stage_ready) { adopt_results(); ch = 1; }
    voz_resultados();                                            /* SOMA_286_YOUTUBE */
    struct media_info mi; pl_get_info(&mi);
    int ph = phase_of(&mi);
    if (now_it.id[0] && !held) {
        if (!nat_on) media_keepalive();
        if (ph == PH_PLAY) {
            uint32_t p = t0 + (uint32_t)(mi.pos_us / 1000000u);
            if (now_it.dur && p > now_it.dur) p = now_it.dur;
            last_pos = p;
        } else if (ph == PH_ENDED && ended_gen != str_req) {
            ended_gen = str_req;
            uint32_t p = t0 + (uint32_t)(mi.pos_us / 1000000u);
            if (now_it.dur && p + 4 < now_it.dur) {              /* se ha cortado antes del final (red o servicio) */
                held = 1; held_t = p; yt_scopy(note, sizeof note, "El video se ha interrumpido. Pulsa reproducir para continuar.");
            } else if (cur_idx >= 0 && cur_idx + 1 < nitems && tab == T_NEXT && !ia_busy()) play_item(cur_idx + 1);   /* SOMA_251: leyendo la ficha, el texto o la IA de este video no se salta al siguiente */
            else { held = 1; held_t = 0; last_pos = now_it.dur; }
        } else if (str_live == str_req && str_errgen != str_req && !mi.stream) {
            /* el reproductor ya no tiene nuestro video: la ventana estuvo oculta (se para solo) o lo ha tomado SOMA Media */
            held = 1; held_t = last_pos; stream_request("", 0, "");
        }
        ph = phase_of(&mi);
    }
    uint32_t sig = mi.frames * 131u + (uint32_t)ph * 7u + last_pos * 13u + (uint32_t)net_state * 3u + thumbs_seq * 17u + (uint32_t)mi.paused * 5u + (uint32_t)mi.buffering * 11u;
    sig += ia_seq * 19u + (uint32_t)ai_state * 23u;
    if (ia_busy()) sig += (uint32_t)(t / 350u) * 1013u;
    if (ph == PH_LOADING || net_state == NS_SEARCH || mi.buffering) sig += (uint32_t)(t / 350u) * 1009u;   /* puntos animados */
    if (sig != last_sig) { last_sig = sig; ch = 1; }
    return ch;
}

/* ================================================================== dibujo */
struct ylay {
    int x, y, w, h;
    int back_w;                                      /* boton "volver" (solo viendo un video) */
    int sf_x, sf_y, sf_w, sf_h;                      /* campo de busqueda */
    int sb_cx, sb_cy, sb_r;                          /* boton buscar */
    int q_x, q_y, q_w, q_h;                          /* calidad */
    int gx, gy, gw, gh, cols, card_w, card_h, th_h, gap;      /* cuadricula */
    int vx, vy, vw, vh;                              /* video */
    int px, py, pw;                                  /* progreso */
    int cy, bcx[5], br[5];                           /* anterior, -10, reproducir, +10, siguiente */
    int ty, tw;                                      /* titulo */
    int sx, sy, sw, sh, row_h;                       /* lista lateral */
    /* SOMA_251 */
    int ai_x, ai_w;                                  /* interruptor "IA" de la barra */
    int ban_y, ban_h, ban_lines;                     /* tarjeta de la busqueda con IA (0 = no hay) */
    int desc_h;                                      /* alto extra de las tarjetas con descripcion */
    int ly, lh;                                      /* contenido del panel lateral, bajo las pestanas */
    int tb_w;                                        /* ancho de cada pestana */
    int bt_y, bt_h, bt_w;                            /* botones del panel (Texto: dos; IA: uno) */
    int qf_x, qf_y, qf_w, qf_h, qs_cx, qs_cy;        /* caja de preguntas y su boton */
};
#define BAR_H 64
static void clip_push(int32_t c[4], int x, int y, int w, int h);
static void clip_pop(const int32_t c[4]);
/* ---- SOMA_251: texto largo ajustado al ancho, con las lineas calculadas una sola vez por version del texto */
struct wline { uint32_t off; uint16_t len; uint8_t kind; };
struct wrapc { const char *src; uint32_t ver; int w, px, n, cap; struct wline *l; };
static struct wline wl_ban[16], wl_desc[700], wl_tx[4200], wl_chat[800];
static struct wrapc wc_ban = { 0, 0, 0, 0, 0, 16, wl_ban }, wc_desc = { 0, 0, 0, 0, 0, 700, wl_desc }, wc_tx = { 0, 0, 0, 0, 0, 4200, wl_tx }, wc_chat = { 0, 0, 0, 0, 0, 800, wl_chat };

static void wrap_build(struct wrapc *c, const char *s, uint32_t ver, int w, int px)
{
    if (c->src == s && c->ver == ver && c->w == w && c->px == px) return;
    c->src = s; c->ver = ver; c->w = w; c->px = px; c->n = 0;
    uint32_t p = 0;
    while (s[p] && c->n < c->cap) {
        uint32_t e = p; while (s[e] && s[e] != '\n') e++;
        uint8_t kind = 0;
        if (p < e && (unsigned char)s[p] < 4) kind = (uint8_t)s[p++];
        int bold = kind == 1 || kind == 2;
        if (p == e) { c->l[c->n].off = p; c->l[c->n].len = 0; c->l[c->n].kind = kind; c->n++; }
        while (p < e && c->n < c->cap) {
            while (p < e && s[p] == ' ') p++;
            if (p >= e) break;
            char b[240]; uint32_t fit = p, i = p;
            for (;;) {
                uint32_t j = i; while (j < e && s[j] == ' ') j++;
                while (j < e && s[j] != ' ') j++;
                if (j - p >= sizeof b - 1) { if (fit == p) fit = p + sizeof b - 2; break; }
                uint32_t k = 0; for (uint32_t q = p; q < j; ++q) b[k++] = s[q];
                b[k] = 0;
                if (si_text_w(b, px, bold) > w) {
                    if (fit == p) {                              /* una sola palabra mas ancha que la linea: se parte */
                        while (k > 1) { k--; while (k > 1 && ((unsigned char)b[k] & 0xC0) == 0x80) k--; b[k] = 0; if (si_text_w(b, px, bold) <= w) break; }
                        fit = p + k;
                    }
                    break;
                }
                fit = j; i = j;
                if (j >= e) break;
            }
            while (fit > p && fit < e && ((unsigned char)s[fit] & 0xC0) == 0x80) fit--;
            if (fit == p) fit = p + 1;
            c->l[c->n].off = p; c->l[c->n].len = (uint16_t)(fit - p); c->l[c->n].kind = kind; c->n++;
            p = fit;
        }
        p = s[e] ? e + 1 : e;
    }
}
/* Pinta las lineas visibles en [y, y+h) desplazadas scroll pixeles. */
static void wrap_draw(const struct wrapc *c, int x, int y, int h, int scroll, int lh, uint32_t col)
{
    int32_t cl[4]; clip_push(cl, x - 2, y, c->w + 6, h);
    int first = scroll / lh; if (first < 0) first = 0;
    for (int i = first; i < c->n; ++i) {
        int ly = y + i * lh - scroll;
        if (ly >= y + h) break;
        if (!c->l[i].len) continue;
        char b[244]; uint32_t n = c->l[i].len; if (n > sizeof b - 1) n = sizeof b - 1;
        for (uint32_t k = 0; k < n; ++k) b[k] = c->src[c->l[i].off + k];
        b[n] = 0;
        uint8_t kd = c->l[i].kind;
        si_text(x, ly, lh, b, c->px, kd == 1 ? 0x007CC4FFu : kd == 2 ? SI_TXT : kd == 3 ? 0x00FDA4AFu : col, kd == 1 || kd == 2);
    }
    clip_pop(cl);
}
static int wrap_clamp(const struct wrapc *c, int h, int lh, int *scroll)
{
    int maxs = c->n * lh - h; if (maxs < 0) maxs = 0;
    if (*scroll > maxs) *scroll = maxs;
    if (*scroll < 0) *scroll = 0;
    return maxs;
}
static int any_desc(void) { for (int i = 0; i < nitems; ++i) if (items[i].desc[0]) return 1; return 0; }

static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct ylay *L)
{
    L->x = (int)wx; L->y = (int)wy; L->w = (int)ww; L->h = (int)wh;
    L->back_w = view == V_WATCH ? 44 : 0;
    char qs[8]; uint32_t qp = 0; yt_put_dec(qs, sizeof qs, &qp, (uint64_t)(native_mode() ? 360 : conf.height)); yt_put(qs, sizeof qs, &qp, "p");
    L->q_w = si_chip_w(26, qs, 0); L->q_h = 26; L->q_x = L->x + L->w - 20 - L->q_w; L->q_y = L->y + 19;
    int left = L->x + 20 + L->back_w + (L->w >= 700 ? 150 : 52);
    L->ai_w = si_chip_w(26, "IA", 1); L->ai_x = L->q_x - 8 - L->ai_w;
    L->sb_r = 17; L->sb_cx = L->ai_x - 16 - L->sb_r; L->sb_cy = L->y + 32;
    L->sf_x = left; L->sf_y = L->y + 17; L->sf_h = 30; L->sf_w = L->sb_cx - L->sb_r - 10 - left;
    if (L->sf_w > 560) { L->sf_x += (L->sf_w - 560) / 2; L->sf_w = 560; L->sb_cx = L->sf_x + L->sf_w + 10 + L->sb_r; }
    if (L->sf_w < 60) L->sf_w = 60;
    /* cuadricula */
    L->gap = 16; L->gx = L->x + 20; L->gw = L->w - 40;
    if (L->gw < 120) L->gw = 120;
    L->ban_y = L->y + BAR_H + 30; L->ban_h = 0; L->ban_lines = 0;
    if (ai_state != AI_OFF && view == V_BROWSE) {                /* SOMA_251: la tarjeta de la busqueda con IA */
        if (ai_sum[0]) { wrap_build(&wc_ban, ai_sum, ai_ver, L->gw - 28, 12); L->ban_lines = wc_ban.n > 7 ? 7 : wc_ban.n; }
        L->ban_h = 34 + (L->ban_lines ? L->ban_lines * 17 + 10 : 0);
    }
    L->gy = L->y + BAR_H + 34 + (L->ban_h ? L->ban_h + 10 : 0); L->gh = L->y + L->h - L->gy;
    L->cols = (L->gw + L->gap) / (228 + L->gap); if (L->cols < 1) L->cols = 1;
    L->card_w = (L->gw - L->gap * (L->cols - 1)) / L->cols;
    L->desc_h = any_desc() ? 34 : 0;
    L->th_h = L->card_w * 9 / 16; L->card_h = L->th_h + 66 + L->desc_h;
    /* viendo un video */
    L->sw = L->w >= 920 ? 320 : (L->w >= 760 ? 250 : 0);
    if (L->w >= 1180 && (tab == T_AI || tab == T_TEXT)) L->sw = 420;          /* SOMA_286_YOUTUBE: leer y conversar con mas sitio */
    int lw = L->w - 40 - (L->sw ? L->sw + 18 : 0);
    L->vx = L->x + 20; L->vy = L->y + BAR_H + 4; L->vw = lw; L->vh = lw * 9 / 16;
    int maxvh = L->h - BAR_H - 4 - 150; if (maxvh < 90) maxvh = 90;
    if (L->vh > maxvh) L->vh = maxvh;
    L->px = L->vx; L->pw = L->vw; L->py = L->vy + L->vh + 16;
    L->cy = L->py + 40;
    static const int rr[5] = { 16, 18, 24, 18, 16 };
    for (int i = 0; i < 5; ++i) { L->br[i] = rr[i]; }
    L->bcx[2] = L->vx + 24; L->bcx[1] = L->bcx[2] + 24 + 12 + 18; L->bcx[3] = L->bcx[1] + 18 + 10 + 18;
    L->bcx[0] = L->bcx[3] + 18 + 18 + 16; L->bcx[4] = L->bcx[0] + 16 + 10 + 16;
    L->ty = L->cy + 34; L->tw = L->vw;
    L->sx = L->x + L->w - 20 - L->sw; L->sy = L->y + BAR_H + 4; L->sh = L->y + L->h - L->sy - 8; L->row_h = 78;
    L->ly = L->sy + 36; L->lh = L->sh - 36; L->tb_w = L->sw / T_COUNT;
    L->bt_y = L->ly; L->bt_h = 28; L->bt_w = (L->sw - 8) / 2;
    L->qf_h = 30; L->qf_x = L->sx; L->qf_y = L->sy + L->sh - L->qf_h - 2; L->qf_w = L->sw - 40; L->qs_cx = L->sx + L->sw - 16; L->qs_cy = L->qf_y + 15;
}

static void clip_push(int32_t c[4], int x, int y, int w, int h)
{
    fb_get_clip(&c[0], &c[1], &c[2], &c[3]);
    int32_t x0 = x > c[0] ? x : c[0], y0 = y > c[1] ? y : c[1], x1 = x + w < c[2] ? x + w : c[2], y1 = y + h < c[3] ? y + h : c[3];
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    fb_set_clip(x0, y0, x1 - x0, y1 - y0);
}
static void clip_pop(const int32_t c[4]) { fb_set_clip(c[0], c[1], c[2] - c[0], c[3] - c[1]); }

/* Texto en varias lineas (corta por espacios); la ultima se recorta con "...". Devuelve las lineas usadas. */
static int draw_wrap(int x, int y, int w, const char *s, int px, uint32_t col, int bold, int lines, int lh)
{
    int used = 0;
    while (*s && used < lines) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (used == lines - 1) { si_text_fit(x, y + used * lh, lh, s, w, px, col, bold); return used + 1; }
        char b[160]; int n = 0, last_fit = 0, i = 0;
        while (s[i] && n < (int)sizeof b - 1) {
            int j = i; while (s[j] && s[j] != ' ') j++;
            int k = n; for (int q = i; q < j && k < (int)sizeof b - 1; ++q) b[k++] = s[q];
            b[k] = 0;
            if (si_text_w(b, px, bold) > w && last_fit) break;
            n = k; last_fit = j; i = j;
            while (s[i] == ' ') { if (n < (int)sizeof b - 1) b[n++] = ' '; i++; }
        }
        if (!last_fit) { si_text_fit(x, y + used * lh, lh, s, w, px, col, bold); return used + 1; }
        b[n] = 0; while (n && b[n - 1] == ' ') b[--n] = 0;
        if (!s[last_fit]) { si_text_fit(x, y + used * lh, lh, b, w, px, col, bold); return used + 1; }
        si_text(x, y + used * lh, lh, b, px, col, bold);
        s += last_fit; used++;
    }
    return used;
}

static void draw_thumb(int i, int x, int y, int w, int h, int playing)
{
    fb_fill_rrect_aa(x, y, w, h, 8, 0x00141C2Au);
    if (i >= 0 && i < nitems && thumbs[i].px && thumbs[i].w > 0 && thumbs[i].h > 0) {
        int32_t c[4]; clip_push(c, x, y, w, h);
        fb_blit_scale(x, y, w, h, thumbs[i].px, thumbs[i].w, thumbs[i].h, thumbs[i].w);
        clip_pop(c);
    } else si_icon(SI_FILM, x + w / 2, y + h / 2, h / 3 < 40 ? h / 3 : 40, SI_FAINT);
    if (i >= 0 && i < nitems && items[i].dur) {
        char d[16]; yt_fmt_time(d, sizeof d, items[i].dur);
        int tw = si_text_w(d, 11, 1) + 10;
        fb_fill_rrect_aa(x + w - tw - 6, y + h - 22, tw, 17, 4, 0xE0000000u);
        si_text(x + w - tw - 1, y + h - 22, 17, d, 11, 0x00FFFFFFu, 1);
    }
    if (playing) { fb_stroke_rrect_aa(x, y, w, h, 8, YT_RED); fb_fill_rrect_aa(x + 6, y + 6, 74, 18, 4, YT_RED); si_text(x + 11, y + 6, 18, "Reproduciendo", 10, 0x00FFFFFFu, 1); }
}

static const char *dots(void) { static const char *const d[4] = { "", ".", "..", "..." }; return d[(ms() / 350u) & 3]; }

static void draw_bar(const struct ylay *L)
{
    fb_fill_rect((uint32_t)L->x, (uint32_t)L->y, (uint32_t)L->w, BAR_H, SI_SIDE);
    fb_fill_rect((uint32_t)L->x, (uint32_t)(L->y + BAR_H - 1), (uint32_t)L->w, 1, SI_LINE);
    int x = L->x + 20;
    if (L->back_w) { si_round(x + 16, L->y + 32, 16, SI_PREV, SIB_NEUTRAL, YT_RED, 0); x += L->back_w; }
    fb_fill_rrect_aa(x, L->y + 20, 34, 24, 7, YT_RED);                       /* la insignia: rectangulo rojo con el triangulo */
    si_icon(SI_PLAY, x + 18, L->y + 32, 13, 0x00FFFFFFu);
    if (L->w >= 700) si_text(x + 42, L->y + 18, 28, "YouTube", 18, SI_TXT, 1);
    tf_render(&tf, L->sf_x, L->sf_y, L->sf_w, L->sf_h, tf_focus, conf.ai ? "Describe lo que quieres ver (IA)" : "Buscar en YouTube", SI_TXT);
    si_chip(L->ai_x, L->q_y, L->q_h, "IA", conf.ai ? 0x007CC4FFu : SI_FAINT, 1);
    si_round(L->sb_cx, L->sb_cy, L->sb_r, SI_SEARCH, SIB_NEUTRAL, YT_RED, 0);
    char qs[8]; uint32_t qp = 0; yt_put_dec(qs, sizeof qs, &qp, (uint64_t)(native_mode() ? 360 : conf.height)); yt_put(qs, sizeof qs, &qp, "p");
    si_chip(L->q_x, L->q_y, L->q_h, qs, SI_DIM, 0);
}

/* SOMA_251: tarjeta de la busqueda con IA: que se ha buscado y el resumen de lo encontrado */
static void draw_banner(const struct ylay *L)
{
    fb_fill_rrect_aa(L->gx, L->ban_y, L->gw, L->ban_h, 10, SI_CARD);
    fb_fill_rrect_aa(L->gx + 10, L->ban_y + 8, 26, 18, 5, 0x007CC4FFu);
    si_text_c(L->gx + 10, L->ban_y + 8, 26, 18, "IA", 11, 0x00081018u, 1);
    char t[420]; uint32_t p = 0; uint32_t col = SI_DIM;
    if (ai_state == AI_THINK) { yt_put(t, sizeof t, &p, "Pensando que buscar"); yt_put(t, sizeof t, &p, dots()); }
    else if (ai_state == AI_PLAIN) { yt_put(t, sizeof t, &p, "Sin IA ("); yt_put(t, sizeof t, &p, ai_why); yt_put(t, sizeof t, &p, "): se ha buscado el texto tal cual"); col = SI_AMBER; }
    else {
        yt_put(t, sizeof t, &p, ai_state == AI_SEARCH ? "Buscando: " : "Busquedas: ");
        for (int i = 0; i < ai_nq; ++i) { if (i) yt_put(t, sizeof t, &p, "  -  "); yt_put(t, sizeof t, &p, ai_q[i]); }
        if (ai_state == AI_SUMMING) { yt_put(t, sizeof t, &p, "   (resumiendo lo encontrado"); yt_put(t, sizeof t, &p, dots()); yt_put(t, sizeof t, &p, ")"); }
        else if (ai_state == AI_DONE && !ai_sum[0] && ai_why[0]) { yt_put(t, sizeof t, &p, "   (sin resumen: "); yt_put(t, sizeof t, &p, ai_why); yt_put(t, sizeof t, &p, ")"); }
    }
    si_text_fit(L->gx + 44, L->ban_y + 6, 22, t, L->gw - 56, 12, col, 0);
    if (L->ban_lines) wrap_draw(&wc_ban, L->gx + 14, L->ban_y + 32, L->ban_lines * 17, 0, 17, SI_TXT);
}

static void draw_browse(const struct ylay *L)
{
    /* linea de estado bajo la barra */
    char st[220]; uint32_t p = 0; uint32_t col = SI_DIM;
    if (ai_state == AI_THINK) st[0] = 0;
    else if (net_state == NS_SEARCH) { yt_put(st, sizeof st, &p, "Buscando \""); yt_put(st, sizeof st, &p, last_query); yt_put(st, sizeof st, &p, "\""); yt_put(st, sizeof st, &p, dots()); }
    else if (net_state == NS_ERROR) { yt_put(st, sizeof st, &p, net_err); col = 0x00FDA4AFu; }
    else if (note[0]) yt_put(st, sizeof st, &p, note);
    else if (nitems) { yt_put_dec(st, sizeof st, &p, (uint64_t)nitems); yt_put(st, sizeof st, &p, " resultados de \""); yt_put(st, sizeof st, &p, last_query); yt_put(st, sizeof st, &p, "\""); }
    else st[0] = 0;
    si_text_fit(L->gx, L->y + BAR_H + 6, 22, st, L->gw, 12, col, 0);
    if (L->ban_h) draw_banner(L);

    if (!nitems) {
        int cx = L->x + L->w / 2, cy = L->gy + L->gh / 2 - 60; if (cy < L->gy + 40) cy = L->gy + 40;
        fb_fill_rrect_aa(cx - 46, cy - 32, 92, 64, 18, si_mix(SI_WIN, YT_RED, net_state == NS_ERROR ? 40 : 200));
        si_icon(net_state == NS_ERROR ? SI_CLOSE : SI_PLAY, cx + (net_state == NS_ERROR ? 0 : 3), cy, 30, 0x00FFFFFFu);
        if (net_state == NS_ERROR && native_mode()) {            /* SOMA_252: sin servicio de por medio */
            si_text_c(L->x, cy + 46, L->w, 24, "No se puede hablar con YouTube", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 76, L->w, 20, "Comprueba que SOMA tiene red (Ajustes > Redes) y que la fecha y la hora son correctas.", 12, SI_DIM, 0);
            si_text_c(L->x, cy + 96, L->w, 20, "Si el mensaje de arriba habla de un cambio en YouTube, hace falta actualizar SOMA.", 12, SI_DIM, 0);
            si_text_c(L->x, cy + 120, L->w, 20, "Alternativa: el servicio en otro equipo (escribe en la caja:  modo servicio).", 12, SI_FAINT, 0);
        } else if (net_state == NS_ERROR) {
            char sn[96]; server_name(sn, sizeof sn);
            si_text_c(L->x, cy + 46, L->w, 24, "No se puede hablar con el servicio YouTube", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 76, L->w, 20, "1. En tu Mac o PC:  python3 tools/yt_service.py --bind 0.0.0.0", 12, SI_DIM, 0);
            si_text_c(L->x, cy + 96, L->w, 20, "2. Aqui, en la caja de busqueda:  servidor <IP de ese equipo>", 12, SI_DIM, 0);
            char l3[140]; uint32_t q = 0; yt_put(l3, sizeof l3, &q, "Ahora se busca en "); yt_put(l3, sizeof l3, &q, sn);
            si_text_c(L->x, cy + 120, L->w, 20, l3, 12, SI_FAINT, 0);
        } else if (ai_state == AI_THINK) {
            si_text_c(L->x, cy + 46, L->w, 24, "La IA esta pensando que buscar", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 74, L->w, 20, "Convierte lo que has escrito en busquedas de YouTube.", 12, SI_FAINT, 0);
        } else if (net_state == NS_SEARCH) {
            si_text_c(L->x, cy + 46, L->w, 24, "Buscando en YouTube", 16, SI_TXT, 1);
        } else {
            si_text_c(L->x, cy + 46, L->w, 24, "Busca un video para empezar", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 74, L->w, 20, conf.ai ? "Describe arriba con tus palabras lo que quieres ver y pulsa Intro: la IA lo busca por ti." : "Escribe arriba y pulsa Intro. Los videos se ven aqui mismo, mientras llegan.", 12, SI_FAINT, 0);
        }
        return;
    }
    int rows = (nitems + L->cols - 1) / L->cols, total = rows * (L->card_h + L->gap);
    int maxs = total - L->gh; if (maxs < 0) maxs = 0;
    if (scroll_px > maxs) scroll_px = maxs;
    if (scroll_px < 0) scroll_px = 0;
    int32_t c[4]; clip_push(c, L->x, L->gy, L->w, L->gh);
    for (int i = 0; i < nitems; ++i) {
        int cx = L->gx + (i % L->cols) * (L->card_w + L->gap), cy = L->gy + (i / L->cols) * (L->card_h + L->gap) - scroll_px;
        if (cy + L->card_h < L->gy || cy > L->gy + L->gh) continue;
        if (i == hover) fb_fill_rrect_aa(cx - 6, cy - 6, L->card_w + 12, L->card_h + 6, 12, SI_CARD2);
        draw_thumb(i, cx, cy, L->card_w, L->th_h, now_it.id[0] && yt_seq(items[i].id, now_it.id));
        draw_wrap(cx + 2, cy + L->th_h + 6, L->card_w - 4, items[i].title, 13, SI_TXT, 1, 2, 18);
        si_text_fit(cx + 2, cy + L->th_h + 44, 18, items[i].channel, L->card_w - 4, 12, SI_DIM, 0);
        if (L->desc_h && items[i].desc[0]) draw_wrap(cx + 2, cy + L->th_h + 64, L->card_w - 4, items[i].desc, 11, SI_FAINT, 0, 2, 15);
        { char nb[8]; uint32_t np = 0; yt_put_dec(nb, sizeof nb, &np, (uint64_t)(i + 1));   /* SOMA_286_YOUTUBE: siempre numerados ("pon el segundo") */ int nw = si_text_w(nb, 11, 1) + 12; fb_fill_rrect_aa(cx + 6, cy + 6, nw, 18, 9, 0xD0000000u); si_text(cx + 12, cy + 6, 18, nb, 11, 0x00FFFFFFu, 1); }
    }
    clip_pop(c);
    (void)total;
    ybar(YB_GRID, L->x + L->w - 18, L->gy + 4, L->gh - 8, maxs, &scroll_px);    /* SOMA_286_YOUTUBE: lejos del borde de la ventana */
}

/* ================================================================== SOMA_286_YOUTUBE: barras de desplazamiento que se arrastran
 * (antes solo se pintaban, y la de la cuadricula estaba pegada al borde de la ventana, donde el clic es del redimensionado) */
static void ybar(int k, int x, int y, int h, int max, int *val)
{
    struct ybar *b = &yb[k];
    b->x = x; b->y = y; b->h = h; b->max = max; b->val = val; b->on = max > 0 && h > 30;
    if (!b->on) return;
    b->th = h * h / (h + max); if (b->th < 28) b->th = 28; if (b->th > h) b->th = h;
    int v = *val; if (v > max) v = max; if (v < 0) v = 0;
    b->ty = y + (h - b->th) * v / max;
    int ov = yb_drag == k || (ym_mx >= x - 6 && ym_mx < x + 10 && ym_my >= y && ym_my < y + h);
    fb_fill_rrect_aa(ov ? x - 2 : x, b->ty, ov ? 8 : 5, b->th, 3, ov ? 0xFF56637Au : 0xFF2C3850u);
}
static int ybar_hit(int32_t mx, int32_t my)
{
    for (int k = 0; k < YB_N; ++k) { struct ybar *b = &yb[k]; if (b->on && mx >= b->x - 6 && mx < b->x + 10 && my >= b->y && my < b->y + b->h) return k; }
    return -1;
}
static void ybar_move(int k, int32_t my)
{
    struct ybar *b = &yb[k];
    if (!b->on || b->h <= b->th) return;
    int t = my - yb_grab - b->y; if (t < 0) t = 0; if (t > b->h - b->th) t = b->h - b->th;
    *b->val = (int)((int64_t)t * b->max / (b->h - b->th));
    if (k == YB_CHAT) chat_follow = *b->val >= b->max;
}
static void ybar_press(int k, int32_t my)
{
    struct ybar *b = &yb[k];
    yb_drag = k; yb_grab = (my >= b->ty && my < b->ty + b->th) ? my - b->ty : b->th / 2;
    ybar_move(k, my);
}

/* ================================================================== SOMA_286_YOUTUBE: la conversacion de la pestana IA en Markdown */
#define YM_OPS  9000
#define YM_POOL (256u * 1024u)
#define YM_ZON  600
static struct md_op *ym_ops; static char *ym_pool; static struct md_zone *ym_z; static void *ym_scr;
static struct md_out ym_o; static uint32_t ym_ver = 0xFFFFFFFFu; static int ym_w = -1, ym_h, ym_ok;
static int ym_ox, ym_oy, ym_ay0, ym_ay1, ym_hz = -1;            /* origen pintado, zona visible, zona bajo el raton */
static const struct md_theme YM_TEMA = {
    0x00E5EAF2u, 0x008592A8u, 0x00FFFFFFu, 0x007DD3FCu, 0x00263044u,
    0x00070B12u, 0x00111826u, 0x00212B3Bu, 0x00E6EDF3u, 0x001B2536u, 0x00FDA4AFu, 0x00F43F5Eu, 0x00574A12u,
    0x00151E2Eu, 0x000F1622u, 0x00F43F5Eu,
    0x00FF7B72u, 0x00A5D6FFu, 0x0079C0FFu, 0x006E7681u, 0x00D2A8FFu, 0x00FFA657u, 0x00FDA4AFu };
static int ym_fl(int fl) { return fl | WF_RAW | ((fl & WF_MONO) ? 0 : WF_LIB); }
static int ym_mw(const char *s, int n, int px, int fl, void *u) { (void)u; return wf_width(s, (uint32_t)n, px, ym_fl(fl)); }
static int ym_ma(int px, void *u) { (void)u; return wf_ascent(px); }
static int ym_md(int px, void *u) { (void)u; return wf_descent(px); }
static const struct md_metrics YM_MET = { ym_mw, ym_ma, ym_md, 0, 0 };
static uint32_t ym_put(char *d, uint32_t cap, uint32_t p, const char *s, uint32_t n) { for (uint32_t i = 0; i < n && p + 1 < cap; ++i) d[p++] = s[i]; return p; }
/* [m:ss] o [h:mm:ss] -> enlace "#t=segundos" (el usuario lo pulsa para saltar ahi) */
static uint32_t ym_linea(char *d, uint32_t cap, uint32_t p, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; ) {
        if (s[i] == '[') {
            uint32_t j = i + 1, a = 0, b = 0, c = 0, nd = 0, partes = 0, v = 0; int ok = 1;
            while (j < n && j < i + 10) {
                if (s[j] >= '0' && s[j] <= '9') { v = v * 10 + (uint32_t)(s[j] - '0'); nd++; }
                else if (s[j] == ':' && nd) { if (partes == 0) a = v; else b = v; partes++; v = 0; nd = 0; }
                else break;
                j++;
            }
            if (j < n && s[j] == ']' && partes >= 1 && partes <= 2 && nd == 2 && !(j + 1 < n && s[j + 1] == '(')) {
                uint32_t sec = partes == 1 ? a * 60 + v : (a * 60 + b) * 60 + v; (void)c;
                char num[16]; uint32_t q = 0; yt_put_dec(num, sizeof num, &q, sec);
                p = ym_put(d, cap, p, s + i, j - i + 1); p = ym_put(d, cap, p, "(#t=", 4); p = ym_put(d, cap, p, num, q); p = ym_put(d, cap, p, ")", 1);
                i = j + 1; continue;
            }
            (void)ok;
        }
        if (p + 1 < cap) d[p++] = s[i];
        i++;
    }
    return p;
}
static void ym_build(int w)
{
    if (ym_ok && ym_ver == chat_ver && ym_w == w) return;
    if (!ym_ops) {
        ym_ops = (struct md_op *)kmalloc(YM_OPS * sizeof(struct md_op)); ym_pool = (char *)kmalloc(YM_POOL);
        ym_z = (struct md_zone *)kmalloc(YM_ZON * sizeof(struct md_zone)); ym_scr = kmalloc(MD_SCRATCH);
        if (!ym_ops || !ym_pool || !ym_z || !ym_scr) { if (ym_ops) kfree(ym_ops); if (ym_pool) kfree(ym_pool); if (ym_z) kfree(ym_z); if (ym_scr) kfree(ym_scr); ym_ops = 0; ym_ok = 0; return; }
    }
    uint32_t cap = chat_len * 2 + 4096; char *d = (char *)kmalloc(cap);
    if (!d) { ym_ok = 0; return; }
    uint32_t p = 0, i = 0;
    while (i < chat_len) {
        uint32_t e = i; while (e < chat_len && chat[e] != '\n') e++;
        char k = chat[i];
        if (k == 1) { p = ym_put(d, cap, p, "\n> **T\303\272** \302\267 ", 16); p = ym_put(d, cap, p, chat + i + 1, e - i - 1); p = ym_put(d, cap, p, "\n\n", 2); }
        else if (k == 2) { p = ym_put(d, cap, p, "\n#### ", 6); p = ym_put(d, cap, p, chat + i + 1, e - i - 1); p = ym_put(d, cap, p, "\n\n", 2); }
        else if (k == 3) { p = ym_put(d, cap, p, "\n> [!CAUTION]\n> ", 16); p = ym_put(d, cap, p, chat + i + 1, e - i - 1); p = ym_put(d, cap, p, "\n\n", 2); }
        else { p = ym_linea(d, cap, p, chat + i, e - i); p = ym_put(d, cap, p, "\n", 1); }
        i = e + 1;
    }
    d[p < cap ? p : cap - 1] = 0;
    ym_o.ops = ym_ops; ym_o.cap = YM_OPS; ym_o.pool = ym_pool; ym_o.poolcap = YM_POOL; ym_o.z = ym_z; ym_o.zcap = YM_ZON;
    ym_h = md_layout(d, p, w, wf_ui_px(13), &YM_MET, &YM_TEMA, &ym_o, ym_scr);
    kfree(d);
    ym_ver = chat_ver; ym_w = w; ym_ok = 1;
}
static void ym_paint(int ox, int oy, int y0, int y1)
{
    for (int i = 0; i < ym_o.nops; i++) {
        const struct md_op *q = &ym_o.ops[i];
        int hov = q->zone >= 0 && q->zone == ym_hz;
        switch (q->type) {
        case MD_TEXT: { int y = oy + q->y; if (y + q->px < y0 || y - q->px > y1) break; wf_draw(ox + q->x, y, ym_o.pool + q->s, q->n, q->px, hov ? 0x00FFFFFFu : q->rgb, ym_fl(q->fl), 255); break; }
        case MD_RECT: { if (oy + q->y > y1 || oy + q->y + q->h < y0) break; uint32_t a = q->rgb >> 24; if (!a) a = 255; fb_fill_rrect_aa(ox + q->x, oy + q->y, q->w, q->h, q->px, (a << 24) | (q->rgb & 0xFFFFFFu)); break; }
        case MD_STROKE: if (oy + q->y > y1 || oy + q->y + q->h < y0) break; fb_stroke_rrect_aa(ox + q->x, oy + q->y, q->w, q->h, q->px, 0xFF000000u | q->rgb); break;
        case MD_LINE: { int ya = oy + q->y / 16; if (ya < y0 - 4 || ya > y1 + 4) break; uint32_t a = q->rgb >> 24; if (!a || hov) a = 255;
            fb_line_aa(ox * 16 + q->x, oy * 16 + q->y, ox * 16 + q->x + q->w, oy * 16 + q->y + q->h, q->px, (a << 24) | (hov ? 0xFFFFFFu : (q->rgb & 0xFFFFFFu))); break; }
        default: break;
        }
    }
}
static int ym_zone_at(int32_t mx, int32_t my)
{
    if (!ym_ok || my < ym_ay0 || my >= ym_ay1) return -1;
    for (int k = ym_o.nz - 1; k >= 0; --k) { const struct md_zone *z = &ym_o.z[k]; if (mx >= ym_ox + z->x && my >= ym_oy + z->y && mx < ym_ox + z->x + z->w && my < ym_oy + z->y + z->h) return k; }
    return -1;
}

static void draw_side_list(const struct ylay *L)
{
    int ly = L->ly, lh = L->lh, total = nitems * L->row_h, maxs = total - lh; if (maxs < 0) maxs = 0;
    if (side_scroll > maxs) side_scroll = maxs;
    if (side_scroll < 0) side_scroll = 0;
    int32_t c[4]; clip_push(c, L->sx - 6, ly, L->sw + 12, lh);
    for (int i = 0; i < nitems; ++i) {
        int y = ly + i * L->row_h - side_scroll;
        if (y + L->row_h < ly || y > ly + lh) continue;
        int on = i == cur_idx;
        if (on || i == hover) fb_fill_rrect_aa(L->sx - 6, y - 4, L->sw + 12, L->row_h - 2, 10, on ? si_mix(SI_WIN, YT_RED, 36) : SI_CARD2);
        draw_thumb(i, L->sx, y, 120, 67, 0);
        draw_wrap(L->sx + 130, y, L->sw - 132, items[i].title, 12, on ? 0x00FFFFFFu : SI_TXT, 1, 2, 16);
        si_text_fit(L->sx + 130, y + 36, 16, items[i].channel, L->sw - 132, 11, SI_DIM, 0);
    }
    clip_pop(c);
    ybar(YB_SIDE, L->sx + L->sw + 4, ly, lh, maxs, &side_scroll);      /* SOMA_286_YOUTUBE */
}
static void scrollbar(const struct ylay *L, int k, int y, int h, int *scroll, int maxs)      /* SOMA_286_YOUTUBE: se arrastra */
{
    ybar(k, L->sx + L->sw + 4, y, h, maxs, scroll);
}
/* Linea de estado de la IA del video (lo que esta haciendo o el ultimo error). Devuelve el alto usado. */
static int draw_ia_status(const struct ylay *L, int y)
{
    int busy = ia_run >= J_INFO;
    if (!ia_status[0] || (!busy && !ia_err && !yt_seq(ia_status, "Cancelado"))) return 0;
    char t[260]; uint32_t p = 0;
    yt_put(t, sizeof t, &p, ia_status);
    if (busy && !ia_err) {
        char el[16]; yt_fmt_time(el, sizeof el, (uint32_t)((ms() - ia_t0) / 1000u));
        yt_put(t, sizeof t, &p, dots()); yt_put(t, sizeof t, &p, "  "); yt_put(t, sizeof t, &p, el);
    }
    return draw_wrap(L->sx, y, L->sw, t, 11, ia_err ? 0x00FDA4AFu : SI_AMBER, 0, 4, 15) * 15 + 6;
}
static void draw_side_info(const struct ylay *L)
{
    int y = L->ly;
    if (vd_state == 0) { char t[60]; uint32_t p = 0; yt_put(t, sizeof t, &p, "Cargando la ficha del video"); yt_put(t, sizeof t, &p, dots()); si_text(L->sx, y, 20, t, 12, SI_DIM, 0); return; }
    if (vd_state == 2) { draw_wrap(L->sx, y, L->sw, vd_desc, 12, 0x00FDA4AFu, 0, 5, 17); return; }
    char t[200]; uint32_t p = 0; char vv[64];
    if (vd_info.date[0]) yt_put(t, sizeof t, &p, vd_info.date);
    if (vd_info.views || now_it.views) { yt_fmt_views(vv, sizeof vv, vd_info.views ? vd_info.views : now_it.views); if (p) yt_put(t, sizeof t, &p, "  -  "); yt_put(t, sizeof t, &p, vv); }
    if (p) { si_text_fit(L->sx, y, 18, t, L->sw, 11, SI_DIM, 1); y += 20; }
    p = 0; yt_put(t, sizeof t, &p, vd_info.subs[0] ? "Subtitulos en YouTube: " : "Sin subtitulos en YouTube"); yt_put(t, sizeof t, &p, vd_info.subs);
    si_text_fit(L->sx, y, 18, t, L->sw, 11, SI_FAINT, 0); y += 24;
    int h = L->ly + L->lh - y;
    if (!vd_desc[0]) { si_text(L->sx, y, 20, "Este video no tiene descripcion.", 12, SI_FAINT, 0); return; }
    wrap_build(&wc_desc, vd_desc, vd_ver, L->sw - 4, 12);
    int maxs = wrap_clamp(&wc_desc, h, 17, &info_scroll);
    wrap_draw(&wc_desc, L->sx, y, h, info_scroll, 17, SI_TXT);
    scrollbar(L, YB_INFO, y, h, &info_scroll, maxs);
}
static int text_top(const struct ylay *L) { return L->bt_y + L->bt_h + 10; }
static void draw_side_text(const struct ylay *L)
{
    int working = ia_run == J_WHISPER || ia_run == J_SUBS;
    if (working) si_button(L->sx, L->bt_y, L->sw, L->bt_h, SI_CLOSE, "Cancelar", SIB_NEUTRAL, YT_RED, 0);
    else {
        si_button(L->sx, L->bt_y, L->bt_w, L->bt_h, SI_WAVE, "Transcribir", SIB_ACCENT, YT_RED, 0);
        si_button(L->sx + L->bt_w + 8, L->bt_y, L->bt_w, L->bt_h, SI_COMMENT, "Subtitulos", SIB_NEUTRAL, YT_RED, 0);
    }
    int y = text_top(L);
    y += draw_ia_status(L, y);
    int h = L->ly + L->lh - y;
    if (!tx_len) {
        if (!working) draw_wrap(L->sx, y, L->sw, "Transcribir pasa el sonido del video por whisper en este equipo (tarda; mientras, el video puede ir a saltos). "
                                "Subtitulos pide a YouTube los que tenga el video: es inmediato. Con el texto, en la pestana IA puedes pedir un resumen y preguntar por lo que se dice.", 12, SI_FAINT, 0, 9, 17);
        return;
    }
    char t[160]; uint32_t p = 0; yt_put(t, sizeof t, &p, "Fuente: "); yt_put(t, sizeof t, &p, tx_src); yt_put(t, sizeof t, &p, ". Pulsa una linea para ir a ese momento.");
    y += draw_wrap(L->sx, y, L->sw, t, 11, SI_FAINT, 0, 2, 15) * 15 + 6; h = L->ly + L->lh - y;
    tx_y0 = y;
    wrap_build(&wc_tx, tx, tx_ver, L->sw - 4, 12);
    int maxs = wrap_clamp(&wc_tx, h, 17, &tx_scroll);
    wrap_draw(&wc_tx, L->sx, y, h, tx_scroll, 17, SI_TXT);
    scrollbar(L, YB_TEXT, y, h, &tx_scroll, maxs);
}
/* SOMA_286_YOUTUBE: atajos de la pestana IA */
static const char *const YCHIP_T[3] = { "Puntos clave", "Este momento", "\302\277De qu\303\251 trata?" };
static int ychip_x[3], ychip_y, ychip_w[3], ychip_h;
static void ychip_pregunta(int k, char *q, uint32_t max)
{
    uint32_t p = 0;
    if (k == 0) yt_put(q, max, &p, "Enumera los puntos clave del video, cada uno con su minuto [m:ss].");
    else if (k == 1) { char t[16]; yt_fmt_time(t, sizeof t, held ? held_t : last_pos); yt_put(q, max, &p, "Que se esta explicando ahora, alrededor del minuto ["); yt_put(q, max, &p, t); yt_put(q, max, &p, "]? Explicamelo con detalle."); }
    else yt_put(q, max, &p, "De que trata este video? Respondeme en un parrafo y dime si merece la pena verlo entero.");
}
static void draw_side_ai(const struct ylay *L)
{
    int working = ia_run == J_SUM || ia_run == J_ASK;
    if (working) si_button(L->sx, L->bt_y, L->sw, L->bt_h, SI_CLOSE, "Cancelar", SIB_NEUTRAL, YT_RED, 0);
    else si_button(L->sx, L->bt_y, L->sw, L->bt_h, SI_LIST, tx_len ? "Resumir el video" : "Resumir el video (busca subtitulos)", SIB_ACCENT, YT_RED, 0);
    int y = text_top(L);
    y += draw_ia_status(L, y);
    /* atajos */
    ychip_h = 24; ychip_y = L->qf_y - 34;
    { int x = L->sx;
      for (int k = 0; k < 3; ++k) {
          int w = si_text_w(YCHIP_T[k], 11, 1) + 18; ychip_x[k] = x; ychip_w[k] = w;
          if (x + w > L->sx + L->sw) { ychip_w[k] = 0; continue; }
          int ov = ym_mx >= x && ym_mx < x + w && ym_my >= ychip_y && ym_my < ychip_y + ychip_h;
          fb_fill_rrect_aa(x, ychip_y, w, ychip_h, 12, working ? 0xFF111826u : ov ? 0xFF2A3346u : 0xFF1B2536u);
          fb_stroke_rrect_aa(x, ychip_y, w, ychip_h, 12, 0xFF2C3850u);
          si_text_c(x, ychip_y, w, ychip_h, YCHIP_T[k], 11, working ? SI_FAINT : SI_TXT, 1);
          x += w + 6;
      } }
    int h = ychip_y - 8 - y;
    ym_ay0 = ym_ay1 = 0;
    if (!chat_len) {
        draw_wrap(L->sx, y, L->sw, tx_len ? "Pregunta abajo lo que quieras sobre el video: las respuestas salen de su transcripcion y sus minutos se pueden pulsar."
                  : "Pregunta abajo lo que quieras sobre el video. Si no hay transcripcion, SOMA intenta traer sus subtitulos; si no, usa el titulo y la descripcion.", 12, SI_FAINT, 0, 6, 17);
    } else if (h > 20) {
        ym_build(L->sw - 6);
        if (ym_ok) {
            int maxs = ym_h + 8 - h; if (maxs < 0) maxs = 0;
            if (chat_follow) chat_scroll = maxs;
            if (chat_scroll > maxs) chat_scroll = maxs;
            if (chat_scroll < 0) chat_scroll = 0;
            int32_t c[4]; clip_push(c, L->sx - 2, y, L->sw + 4, h);
            ym_ox = L->sx; ym_oy = y - chat_scroll; ym_ay0 = y; ym_ay1 = y + h;
            ym_paint(ym_ox, ym_oy, y, y + h);
            clip_pop(c);
            ybar(YB_CHAT, L->sx + L->sw + 4, y, h, maxs, &chat_scroll);
        } else {
            wrap_build(&wc_chat, chat, chat_ver, L->sw - 4, 12);
            if (chat_follow) chat_scroll = 1 << 28;
            int maxs = wrap_clamp(&wc_chat, h, 17, &chat_scroll);
            wrap_draw(&wc_chat, L->sx, y, h, chat_scroll, 17, SI_TXT);
            scrollbar(L, YB_CHAT, y, h, &chat_scroll, maxs);
        }
    }
    if (!tfq_ready) { tf_init(&tfq, qbuf, sizeof qbuf, 0); tfq_ready = 1; }
    tf_render(&tfq, L->qf_x, L->qf_y, L->qf_w, L->qf_h, q_focus, "Pregunta sobre el video", SI_TXT);
    si_round(L->qs_cx, L->qs_cy, 14, SI_NEXT, SIB_ON, YT_RED, 0);
}

static void draw_watch(const struct ylay *L)
{
    struct media_info mi; pl_get_info(&mi);
    int ph = phase_of(&mi);
    /* escenario */
    fb_fill_rrect_aa(L->vx, L->vy, L->vw, L->vh, 10, 0x00000000u);
    const uint32_t *px; uint32_t fw, fh;
    int have = (ph == PH_PLAY || ph == PH_ENDED) && pl_frame(&px, &fw, &fh);
    if (have) {
        uint32_t dw = (uint32_t)L->vw, dh = (uint32_t)((uint64_t)L->vw * fh / fw);
        if (dh > (uint32_t)L->vh) { dh = (uint32_t)L->vh; dw = (uint32_t)((uint64_t)L->vh * fw / fh); }
        int dx = L->vx + (L->vw - (int)dw) / 2, dy = L->vy + (L->vh - (int)dh) / 2;
        fb_blit_scale(dx, dy, (int32_t)dw, (int32_t)dh, px, (int32_t)fw, (int32_t)fh, (int32_t)fw);
    }
    else if (cur_idx >= 0 && cur_idx < nitems && thumbs[cur_idx].px && yt_seq(items[cur_idx].id, now_it.id)) {
        /* aun sin fotogramas: la miniatura del video, oscurecida, hace de cartel */
        int tw = thumbs[cur_idx].w, th = thumbs[cur_idx].h;
        int dw = L->vw, dh = L->vw * th / tw;
        if (dh > L->vh) { dh = L->vh; dw = L->vh * tw / th; }
        fb_blit_scale(L->vx + (L->vw - dw) / 2, L->vy + (L->vh - dh) / 2, dw, dh, thumbs[cur_idx].px, tw, th, tw);
        fb_fill_rrect_aa(L->vx, L->vy, L->vw, L->vh, 0, 0xA0000000u);
    }
    int cx = L->vx + L->vw / 2, cy = L->vy + L->vh / 2;
    if (ph == PH_LOADING || (ph == PH_PLAY && (mi.buffering || mi.state == 1 || !have))) {
        char m[40]; uint32_t q = 0; yt_put(m, sizeof m, &q, "Cargando"); yt_put(m, sizeof m, &q, dots());
        int tw = si_text_w("Cargando...", 14, 1);
        fb_fill_rrect_aa(cx - tw / 2 - 18, cy - 18, tw + 36, 36, 18, 0xC0000000u);
        si_text(cx - tw / 2, cy - 18, 36, m, 14, 0x00FFFFFFu, 1);
    } else if (ph == PH_ERROR) {
        si_icon(SI_CLOSE, cx, cy - 22, 44, si_mix(YT_STAGE, SI_RED, 190));
        const char *em = str_errgen == str_req ? str_err : (mi.msg && mi.msg[0] ? mi.msg : "No se pudo reproducir el video");
        int32_t c[4]; clip_push(c, L->vx + 10, L->vy, L->vw - 20, L->vh);
        draw_wrap(L->vx + 24, cy + 12, L->vw - 48, em, 13, 0x00FDA4AFu, 1, 3, 20);
        clip_pop(c);
    } else if (ph == PH_HELD || (ph == PH_PLAY && mi.paused)) {
        fb_fill_rrect_aa(cx - 34, cy - 34, 68, 68, 34, 0xB0000000u);
        si_icon(ph == PH_HELD && held_t == 0 && last_pos && now_it.dur && last_pos >= now_it.dur ? SI_RESTART : SI_PLAY, cx + 3, cy, 30, 0x00FFFFFFu);
    }
    /* progreso */
    uint32_t pos = ph == PH_HELD ? held_t : last_pos, dur = now_it.dur;
    if (ph == PH_HELD && held_t == 0 && dur && last_pos >= dur) pos = dur;
    fb_fill_rrect_aa(L->px, L->py - 2, L->pw, 5, 2, SI_LINE);
    if (dur) {
        if (pos > dur) pos = dur;
        int fx = (int)((uint64_t)L->pw * pos / dur);
        if (fx > 0) fb_fill_rrect_aa(L->px, L->py - 2, fx < 5 ? 5 : fx, 5, 2, YT_RED);
        int kx = L->px + fx; if (kx < L->px + 6) kx = L->px + 6;
        if (kx > L->px + L->pw - 6) kx = L->px + L->pw - 6;
        fb_fill_rrect_aa(kx - 6, L->py - 6, 13, 13, 6, YT_RED);
    }
    /* controles */
    int playing = ph == PH_PLAY && !mi.paused;
    si_round(L->bcx[2], L->cy, L->br[2], playing ? SI_PAUSE : SI_PLAY, SIB_ON, YT_RED, 0);
    si_round(L->bcx[1], L->cy, L->br[1], SI_REWIND, SIB_NEUTRAL, YT_RED, 0);
    si_round(L->bcx[3], L->cy, L->br[3], SI_FFWD, SIB_NEUTRAL, YT_RED, 0);
    si_round(L->bcx[0], L->cy, L->br[0], SI_SKIP_PREV, SIB_GHOST, YT_RED, cur_idx <= 0);
    si_round(L->bcx[4], L->cy, L->br[4], SI_SKIP_NEXT, SIB_GHOST, YT_RED, cur_idx < 0 || cur_idx + 1 >= nitems);
    {
        char a[16], b[16], tt[40]; uint32_t q = 0;
        yt_fmt_time(a, sizeof a, pos); yt_put(tt, sizeof tt, &q, a);
        if (dur) { yt_fmt_time(b, sizeof b, dur); yt_put(tt, sizeof tt, &q, " / "); yt_put(tt, sizeof tt, &q, b); }
        else yt_put(tt, sizeof tt, &q, "  (directo)");
        si_text(L->bcx[4] + 30, L->cy - 12, 24, tt, 13, SI_DIM, 1);
        if (L->vw >= 560 && (ph == PH_PLAY || ph == PH_ENDED) && mi.width) {
            char inf[64]; q = 0;
            yt_put_dec(inf, sizeof inf, &q, mi.width); yt_put(inf, sizeof inf, &q, "x"); yt_put_dec(inf, sizeof inf, &q, mi.height);
            if (mi.dropped) { yt_put(inf, sizeof inf, &q, "  -  "); yt_put_dec(inf, sizeof inf, &q, mi.dropped); yt_put(inf, sizeof inf, &q, " saltados"); }
            si_text_r(L->vx + L->vw, L->cy - 12, 24, inf, 11, SI_FAINT, 0);
        }
    }
    /* titulo y canal */
    int used = draw_wrap(L->vx, L->ty, L->tw, now_it.title, 17, SI_TXT, 1, 2, 24);
    char sub[200]; uint32_t q = 0; char vv[64];
    yt_put(sub, sizeof sub, &q, now_it.channel);
    if (now_it.views) { yt_fmt_views(vv, sizeof vv, now_it.views); if (q) yt_put(sub, sizeof sub, &q, "  -  "); yt_put(sub, sizeof sub, &q, vv); }
    si_text_fit(L->vx, L->ty + used * 24 + 4, 20, sub, L->tw, 12, SI_DIM, 0);
    if (note[0]) si_text_fit(L->vx, L->ty + used * 24 + 26, 20, note, L->tw, 12, SI_AMBER, 0);

    /* panel lateral (SOMA_251: con pestanas) */
    if (L->sw) {
        static const char *const tn[T_COUNT] = { "Lista", "Info", "Texto", "IA" };
        for (int i = 0; i < T_COUNT; ++i) {
            int tx0 = L->sx + i * L->tb_w;
            si_text_c(tx0, L->sy, L->tb_w, 26, tn[i], 12, i == tab ? SI_TXT : SI_DIM, 1);
            if (i == tab) fb_fill_rrect_aa(tx0 + 8, L->sy + 26, L->tb_w - 16, 3, 1, YT_RED);
            else if ((i == T_TEXT && tx_len) || (i == T_AI && chat_len) || ((i == T_TEXT || i == T_AI) && ia_run >= J_SUM))
                fb_fill_rrect_aa(tx0 + L->tb_w / 2 + si_text_w(tn[i], 12, 1) / 2 + 4, L->sy + 8, 6, 6, 3, ia_run >= J_SUM ? SI_AMBER : SI_GREEN);
        }
        fb_fill_rect((uint32_t)L->sx, (uint32_t)(L->sy + 29), (uint32_t)L->sw, 1, SI_LINE);
        if (tab == T_NEXT) draw_side_list(L);
        else if (tab == T_INFO) draw_side_info(L);
        else if (tab == T_TEXT) draw_side_text(L);
        else draw_side_ai(L);
    }
}

void youtube_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    conf_load();
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    struct ylay L; make_layout(wx, wy, ww, wh, &L);
    for (int k = 0; k < YB_N; ++k) yb[k].on = 0;                  /* SOMA_286_YOUTUBE: las vuelve a poner quien las pinta */
    int32_t c[4]; clip_push(c, L.x, L.y, L.w, L.h);
    fb_fill_rect(wx, wy, ww, wh, SI_WIN);
    draw_bar(&L);
    if (view == V_WATCH && now_it.id[0]) draw_watch(&L); else draw_browse(&L);
    clip_pop(c);
}

/* ================================================================== raton y teclado */
static int inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
static int in_round(int32_t x, int32_t y, int cx, int cy, int r) { int dx = x - cx, dy = y - cy; r += 4; return dx * dx + dy * dy <= r * r; }

static int item_at(const struct ylay *L, int32_t mx, int32_t my)
{
    if (view == V_BROWSE) {
        if (!inside(mx, my, L->gx, L->gy, L->gw, L->gh)) return -1;
        for (int i = 0; i < nitems; ++i) {
            int cx = L->gx + (i % L->cols) * (L->card_w + L->gap), cy = L->gy + (i / L->cols) * (L->card_h + L->gap) - scroll_px;
            if (inside(mx, my, cx, cy, L->card_w, L->card_h)) return i;
        }
        return -1;
    }
    if (!L->sw || tab != T_NEXT || !inside(mx, my, L->sx - 6, L->ly, L->sw + 12, L->lh)) return -1;
    int i = (my - L->ly + side_scroll) / L->row_h;
    return i >= 0 && i < nitems ? i : -1;
}

static void toggle_play(void)
{
    struct media_info mi; pl_get_info(&mi);
    int ph = phase_of(&mi);
    if (ph == PH_HELD) play_at(held_t);
    else if (ph == PH_PLAY) pl_pause_toggle();
    else if (ph == PH_ENDED || ph == PH_ERROR) play_at(ph == PH_ERROR ? last_pos : 0);
}
static void seek_rel(int d)
{
    if (!now_it.id[0]) return;
    int64_t t = (int64_t)(held ? held_t : last_pos) + d;
    if (t < 0) t = 0;
    play_at((uint32_t)t);
}
static void cycle_quality(void)
{
    if (native_mode()) { yt_scopy(note, sizeof note, "En modo nativo YouTube solo entrega 360p. Mas calidad: con el servicio externo (youtube modo servicio)."); return; }
    conf.height = conf.height == 240 ? 360 : conf.height == 360 ? 480 : 240;
    conf_save();
    if (now_it.id[0] && view == V_WATCH && !held) play_at(last_pos);
}

/* ---- SOMA_251: acciones del panel */
static void ia_note_busy(void) { ia_set("Espera a que termine lo que esta en marcha (o pulsa Cancelar).", 1); }
static void do_transcribe(int subs)
{
    if (!now_it.id[0]) return;
    ia_video_reset(now_it.id);
    if (ia_heavy() || ia_next >= J_SUM) { ia_note_busy(); return; }
    tx_scroll = 0;
    ia_set(subs ? "Pidiendo los subtitulos a YouTube" : "Preparando la transcripcion", 0);
    (void)ia_start(subs ? J_SUBS : J_WHISPER, 0, 0);
}
static void do_summary(void)
{
    if (!now_it.id[0]) return;
    ia_video_reset(now_it.id);
    if (ia_heavy() || ia_next >= J_SUM) { ia_note_busy(); return; }
    chat_follow = 1; ia_set("Preparando el resumen", 0);
    (void)ia_start(J_SUM, 0, 0);
}
static void do_ask(const char *q)
{
    while (*q == ' ') q++;
    if (!*q || !now_it.id[0]) return;
    ia_video_reset(now_it.id);
    if (ia_heavy() || ia_next >= J_SUM) { ia_note_busy(); return; }
    chat_add(1, q); chat_follow = 1; ia_set("Pensando", 0);
    (void)ia_start(J_ASK, q, 0);
    if (tfq_ready) tf_set(&tfq, "");
}
static void set_tab(int t)
{
    tab = t; hover = -1;
    if (t != T_AI) q_focus = 0;
    if (t != T_NEXT && now_it.id[0]) {
        ia_video_reset(now_it.id);
        if (vd_state == 2 && ia_run == J_NONE) { vd_state = 0; vd_desc[0] = 0; vd_ver++; }          /* fallo antes: se reintenta */
        if (vd_state == 0 && ia_run != J_INFO && ia_next == J_NONE) (void)ia_start(J_INFO, 0, 0);
    }
}
/* clic en la transcripcion: va al momento del parrafo "[m:ss] ..." pulsado */
static void tx_click(const struct ylay *L, int32_t my)
{
    (void)L;
    int y = tx_y0;                                               /* donde la pinto draw_side_text */
    if (my < y || !tx_len || wc_tx.src != tx) return;
    int i = (my - y + tx_scroll) / 17;
    if (i < 0 || i >= wc_tx.n) return;
    uint32_t o = wc_tx.l[i].off;
    while (o && tx[o - 1] != '\n') o--;
    if (tx[o] != '[') return;
    uint32_t u = 0, a = yt_num(tx + o + 1, 6, &u), sec = 0; const char *c = tx + o + 1 + u;
    if (!u || *c != ':') return;
    uint32_t b = yt_num(c + 1, 2, &u); c += 1 + u;
    if (u != 2) return;
    sec = a * 60 + b;
    if (*c == ':') { uint32_t d = yt_num(c + 1, 2, &u); if (u == 2) sec = sec * 60 + d; }
    play_at(sec);
}
static int side_click(const struct ylay *L, int32_t mx, int32_t my)       /* 1 = el clic era del panel */
{
    if (!L->sw || view != V_WATCH || !now_it.id[0] || mx < L->sx - 6) return 0;
    if (inside(mx, my, L->sx, L->sy, L->sw, 30)) { int t = (mx - L->sx) / L->tb_w; if (t >= 0 && t < T_COUNT) set_tab(t); return 1; }
    if (tab == T_NEXT) return 0;
    if (tab == T_TEXT) {
        if (inside(mx, my, L->sx, L->bt_y, L->sw, L->bt_h)) {
            if (ia_run == J_WHISPER || ia_run == J_SUBS) ia_stop();
            else do_transcribe(mx >= L->sx + L->bt_w + 4);
        } else if (my > L->bt_y + L->bt_h) tx_click(L, my);
        return 1;
    }
    if (tab == T_AI) {
        if (inside(mx, my, L->sx, L->bt_y, L->sw, L->bt_h)) { if (ia_run == J_SUM || ia_run == J_ASK) ia_stop(); else do_summary(); }
        else if (in_round(mx, my, L->qs_cx, L->qs_cy, 14)) do_ask(qbuf);
        else if (my >= ychip_y && my < ychip_y + ychip_h) {                     /* SOMA_286_YOUTUBE: atajos */
            for (int k = 0; k < 3; ++k) if (ychip_w[k] && mx >= ychip_x[k] && mx < ychip_x[k] + ychip_w[k]) {
                if (ia_run == J_SUM || ia_run == J_ASK) { ia_note_busy(); break; }
                char q[200]; ychip_pregunta(k, q, sizeof q); do_ask(q); break;
            }
        } else {                                                                 /* SOMA_286_YOUTUBE: minutos y botones de copiar */
            int z = ym_zone_at(mx, my);
            if (z >= 0) {
                const struct md_zone *q = &ym_o.z[z];
                const char *u = ym_o.pool + q->s;
                if (q->kind == MDZ_LINK && q->n > 3 && u[0] == '#' && u[1] == 't' && u[2] == '=') { uint32_t k2 = 0; play_at(yt_num(u + 3, 7, &k2)); }
                else if (q->kind == MDZ_COPY) { (void)clip_set_text(u, q->n); yt_scopy(note, sizeof note, "Codigo copiado al portapapeles."); }
                else if (q->kind == MDZ_LINK) { char l[200]; uint32_t n = q->n < 150 ? q->n : 150, pp = 0; yt_put(l, sizeof l, &pp, "Enlace copiado: "); for (uint32_t k3 = 0; k3 < n; ++k3) l[pp++] = u[k3]; l[pp] = 0; (void)clip_set_text(u, q->n); yt_scopy(note, sizeof note, l); }
            }
        }
        return 1;
    }
    return 1;
}

int youtube_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct ylay L; make_layout(wx, wy, ww, wh, &L);
    { int k = ybar_hit(mx, my); if (k >= 0) { ybar_press(k, my); return 0; } }    /* SOMA_286_YOUTUBE: arrastrar una barra */
    if (view == V_WATCH && tab == T_AI && tfq_ready && L.sw && tf_hit(&tfq, mx, my)) {     /* SOMA_251: la caja de preguntas */
        q_focus = 1; tf_focus = 0; tf_press(&tfq, mx, my, keyboard_shift_held());
        return 0;
    }
    q_focus = 0;
    if (tf_hit(&tf, mx, my)) {
        int was = tf_focus;
        tf_focus = 1; tf_press(&tf, mx, my, keyboard_shift_held());
        if (!was) tf_select_all(&tf);                            /* primer clic: lo que se escriba sustituye a la busqueda anterior */
        return 0;
    }
    tf_focus = 0;
    if (L.back_w && in_round(mx, my, L.x + 36, L.y + 32, 16)) { stop_playback(); view = V_BROWSE; note[0] = 0; return 0; }
    if (in_round(mx, my, L.sb_cx, L.sb_cy, L.sb_r)) { do_search(query); return 0; }
    if (inside(mx, my, L.q_x - 4, L.q_y - 4, L.q_w + 8, L.q_h + 8)) { cycle_quality(); return 0; }
    if (inside(mx, my, L.ai_x - 4, L.q_y - 4, L.ai_w + 8, L.q_h + 8)) {                    /* SOMA_251: interruptor de la IA */
        conf.ai = !conf.ai; conf_save();
        yt_scopy(note, sizeof note, conf.ai ? "Buscador con IA activado: describe lo que quieres ver." : "Buscador con IA desactivado: se busca el texto tal cual.");
        return 0;
    }
    if (my < L.y + BAR_H) return 0;
    if (side_click(&L, mx, my)) return 0;
    int it = item_at(&L, mx, my);
    if (it >= 0) { play_item(it); return 0; }
    if (view != V_WATCH || !now_it.id[0]) return 0;
    if (now_it.dur && inside(mx, my, L.px, L.py - 10, L.pw, 20)) { play_at((uint32_t)((uint64_t)now_it.dur * (uint32_t)(mx - L.px) / (uint32_t)L.pw)); return 0; }
    if (in_round(mx, my, L.bcx[2], L.cy, L.br[2])) { toggle_play(); return 0; }
    if (in_round(mx, my, L.bcx[1], L.cy, L.br[1])) { seek_rel(-10); return 0; }
    if (in_round(mx, my, L.bcx[3], L.cy, L.br[3])) { seek_rel(10); return 0; }
    if (in_round(mx, my, L.bcx[0], L.cy, L.br[0])) { if (cur_idx > 0) play_item(cur_idx - 1); return 0; }
    if (in_round(mx, my, L.bcx[4], L.cy, L.br[4])) { if (cur_idx >= 0 && cur_idx + 1 < nitems) play_item(cur_idx + 1); return 0; }
    if (inside(mx, my, L.vx, L.vy, L.vw, L.vh)) { toggle_play(); return 0; }
    return 0;
}

int youtube_mouse(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int wheel, int held_btn)
{
    struct ylay L; make_layout(wx, wy, ww, wh, &L);
    int rd = 0;
    /* SOMA_286_YOUTUBE: barras que se arrastran, resaltado de barras y de los minutos de la pestana IA */
    if (yb_drag >= 0) { if (held_btn) { ybar_move(yb_drag, my); rd = 1; } else { yb_drag = -1; rd = 1; } }
    if (mx != ym_mx || my != ym_my) {
        int ob = ybar_hit(ym_mx, ym_my), nb = ybar_hit(mx, my);
        int oc = ym_my >= ychip_y && ym_my < ychip_y + ychip_h, nc = my >= ychip_y && my < ychip_y + ychip_h;
        ym_mx = mx; ym_my = my;
        int z = (view == V_WATCH && tab == T_AI) ? ym_zone_at(mx, my) : -1;
        if (z != ym_hz || ob != nb || oc || nc) { ym_hz = z; rd = 1; }
    }
    if (tf_ready_ && tf_focus && tf_drag(&tf, mx, my, held_btn)) rd = 1;
    if (tfq_ready && q_focus && tf_drag(&tfq, mx, my, held_btn)) rd = 1;
    if (wheel && inside(mx, my, L.x, L.y, L.w, L.h)) {
        if (view == V_BROWSE) scroll_px -= wheel * 60;
        else if (L.sw && mx >= L.sx - 6) {
            if (tab == T_NEXT) side_scroll -= wheel * 60;
            else if (tab == T_INFO) info_scroll -= wheel * 51;
            else if (tab == T_TEXT) tx_scroll -= wheel * 51;
            else { chat_follow = 0; if (chat_scroll > (1 << 27)) chat_scroll = ym_ok ? ym_h : wc_chat.n * 17; chat_scroll -= wheel * 51; if (wheel < 0 && yb[YB_CHAT].on && chat_scroll >= yb[YB_CHAT].max) chat_follow = 1; }
        }
        rd = 1;
    }
    int h = my >= L.y + BAR_H ? item_at(&L, mx, my) : -1;
    if (h != hover) { hover = h; rd = 1; }
    return rd;
}

void youtube_close(void)
{
    if (!now_it.id[0] || held) return;
    held = 1; held_t = last_pos;
    stream_request("", 0, "");                                   /* para el reproductor ya y corta la descarga */
}

int youtube_escape(void)
{
    if (q_focus) { q_focus = 0; return 1; }
    if (tf_focus) { tf_focus = 0; return 1; }
    if (view == V_WATCH && now_it.id[0]) { stop_playback(); view = V_BROWSE; note[0] = 0; return 1; }
    return 0;                                                    /* nada que cerrar: Esc hace lo de siempre en el escritorio */
}

int youtube_clip_op(int op)                          /* Ctrl+C / Ctrl+X / Ctrl+V del escritorio: van a la caja de busqueda */
{
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    if (q_focus && tfq_ready) { tf_clip(&tfq, op); return 1; }   /* SOMA_251: la caja de preguntas */
    if (!tf_focus && op == CLIP_OP_COPY && view == V_WATCH && now_it.id[0]) {              /* copiar lo que se esta leyendo en el panel */
        if (tab == T_TEXT && tx_len) { clip_set_text(tx, tx_len); clip_toast("Transcripcion copiada"); return 1; }
        if (tab == T_INFO && vd_desc[0]) { clip_set_text(vd_desc, 0); clip_toast("Descripcion copiada"); return 1; }
        if (tab == T_AI && chat_len) {
            char *b = (char *)kmalloc(chat_len + 1);
            if (b) { uint32_t n = 0; for (uint32_t i = 0; i < chat_len; ++i) if ((unsigned char)chat[i] >= 4) b[n++] = chat[i]; b[n] = 0; clip_set_text(b, n); kfree(b); clip_toast("Conversacion copiada"); }
            return 1;
        }
    }
    tf_focus = 1;
    tf_clip(&tf, op);
    return 1;
}

void youtube_handle_key(char c)
{
    int k = (uint8_t)c;
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    if (q_focus && tfq_ready) {                                  /* SOMA_251: escribiendo una pregunta */
        int md = keyboard_last_mods();
        int r = tf_key(&tfq, c, (md & 1) || keyboard_ctrl_held(), (md & 2) || keyboard_shift_held());
        if (r == TF_ENTER) do_ask(qbuf);
        else if (r == TF_ESC || r == TF_TAB) q_focus = 0;
        return;
    }
    if (tf_focus) {
        int md = keyboard_last_mods();
        int r = tf_key(&tf, c, (md & 1) || keyboard_ctrl_held(), (md & 2) || keyboard_shift_held());
        if (r == TF_ENTER) { do_search(query); tf_focus = 0; }
        else if (r == TF_ESC || r == TF_TAB) tf_focus = 0;
        return;
    }
    if (view == V_WATCH && now_it.id[0]) {
        if (k == ' ' || k == 'k') { toggle_play(); return; }
        if (k == 2 || k == ',' || k == 'j') { seek_rel(-10); return; }
        if (k == 6 || k == '.' || k == 'l') { seek_rel(10); return; }
        if (k == 'n') { if (cur_idx >= 0 && cur_idx + 1 < nitems) play_item(cur_idx + 1); return; }
        if (k == 'p') { if (cur_idx > 0) play_item(cur_idx - 1); return; }
        if (k == 'q') { cycle_quality(); return; }
        if (k >= '1' && k <= '4') { set_tab(k - '1'); return; }                                    /* SOMA_251: pestanas del panel */
        if (k == 'i' || k == '?') { set_tab(T_AI); if (!tfq_ready) { tf_init(&tfq, qbuf, sizeof qbuf, 0); tfq_ready = 1; } q_focus = 1; return; }
        if (k == 8 || k == 127) { stop_playback(); view = V_BROWSE; note[0] = 0; return; }       /* Retroceso: volver a los resultados (Esc: youtube_escape) */
        if (k == '/' || k == '\t') { tf_focus = 1; tf_select_all(&tf); return; }
        return;
    }
    if (k == '\r' || k == '\n') { if (hover >= 0) play_item(hover); else if (nitems) play_item(0); return; }
    if (k == '\t' || k == '/') { tf_focus = 1; tf_select_all(&tf); return; }
    if (k >= 32 && k != 127) {                                   /* escribir sin haber pulsado en la caja: va a la busqueda */
        tf_focus = 1;
        int md = keyboard_last_mods();
        tf_key(&tf, c, (md & 1) || keyboard_ctrl_held(), (md & 2) || keyboard_shift_held());
    }
}

/* ================================================================== orden de shell
 * youtube                      abre la ventana (devuelve 0: la abre shell_apps.c)
 * youtube <texto>              abre la ventana y busca
 * youtube servidor <ip[:pto]>  donde esta tools/yt_service.py    | youtube servidor auto  (la puerta de enlace: QEMU)
 * youtube clave <texto>        clave compartida con el servicio   | youtube clave -        (sin clave)
 * youtube calidad 240|360|480  | youtube fps 10..30 | youtube estado */
int youtube_command(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0;
    static char big[1500]; char *uo = out; uint32_t um = max;    /* SOMA_251: "estado" ya no cabe en el bufer de la orden */
    int ret = 1;
    out = big; max = sizeof big; big[0] = 0;
    while (*args == ' ') args++;
    conf_load();
    if (yt_starts(args, "servidor ") || yt_seq(args, "servidor")) {
        const char *v = args + 8; while (*v == ' ') v++;
        char h[64]; uint16_t po;
        if (yt_seq(v, "auto")) { conf.host[0] = 0; conf.port = YT_PORT; conf_save(); yt_put(out, max, &p, "youtube: se usara el equipo anfitrion (puerta de enlace), puerto 8097"); }
        else if (*v && yt_host_port(v, h, sizeof h, &po)) { yt_scopy(conf.host, sizeof conf.host, h); conf.port = po; conf_save(); yt_put(out, max, &p, "youtube: servicio en "); yt_put(out, max, &p, h); yt_put(out, max, &p, ":"); yt_put_dec(out, max, &p, po); }
        else yt_put(out, max, &p, "uso: youtube servidor <ip o nombre>[:puerto] | youtube servidor auto");
    } else if (yt_starts(args, "clave ")) {
        const char *v = args + 6; while (*v == ' ') v++;
        if (yt_seq(v, "-")) conf.key[0] = 0; else yt_scopy(conf.key, sizeof conf.key, v);
        conf_save(); yt_put(out, max, &p, conf.key[0] ? "youtube: clave guardada" : "youtube: sin clave");
    } else if (yt_starts(args, "calidad ")) {
        int h = (int)yt_num(args + 8, 4, 0);
        if (yt_height_ok(h)) { conf.height = h; conf_save(); yt_put(out, max, &p, "youtube: calidad "); yt_put_dec(out, max, &p, (uint64_t)h); yt_put(out, max, &p, "p"); }
        else yt_put(out, max, &p, "uso: youtube calidad 240|360|480");
    } else if (yt_starts(args, "fps ")) {
        int f = (int)yt_num(args + 4, 3, 0);
        if (f >= 10 && f <= 30) { conf.fps = f; conf_save(); yt_put(out, max, &p, "youtube: "); yt_put_dec(out, max, &p, (uint64_t)f); yt_put(out, max, &p, " fotogramas por segundo"); }
        else yt_put(out, max, &p, "uso: youtube fps 10..30");
    } else if (yt_starts(args, "modo ") || yt_seq(args, "modo")) {                            /* SOMA_252 */
        const char *v = args + 4; while (*v == ' ') v++;
        if (yt_seq(v, "nativo") || yt_seq(v, "servicio")) {
            conf.native = v[0] == 'n'; conf_save(); stop_playback();
            yt_put(out, max, &p, conf.native ? "youtube: modo nativo (SOMA habla con YouTube directamente; 360p)" : "youtube: modo servicio (tools/yt_service.py en otro equipo)");
            if (conf.native && !native_mode()) yt_put(out, max, &p, "\n  AVISO: en este procesador no hay reproductor nativo: se seguira usando el servicio");
        } else { yt_put(out, max, &p, "uso: youtube modo nativo|servicio   (ahora: "); yt_put(out, max, &p, conf.native ? "nativo)" : "servicio)"); }
    } else if (yt_starts(args, "api ") || yt_seq(args, "api")) {                              /* SOMA_252: base de la API (pruebas o un espejo propio) */
        const char *v = args + 3; while (*v == ' ') v++;
        struct yh_url u;
        if (yt_seq(v, "auto") || yt_seq(v, "youtube")) { conf.api[0] = 0; conf_save(); yt_put(out, max, &p, "youtube: se habla con https://www.youtube.com"); }
        else if (yh_parse_url(v, &u) && yt_slen(v) < sizeof conf.api && yt_seq(u.path, "/")) { yt_scopy(conf.api, sizeof conf.api, v); conf_save(); yt_put(out, max, &p, "youtube: la busqueda y los videos se piden a "); yt_put(out, max, &p, conf.api); }
        else { yt_put(out, max, &p, "uso: youtube api auto | http(s)://equipo[:puerto]   (ahora: "); yt_put(out, max, &p, conf.api[0] ? conf.api : "https://www.youtube.com"); yt_put(out, max, &p, ")"); }
    } else if (yt_starts(args, "mp4 ")) {                                                     /* SOMA_252: un MP4 de cualquier direccion */
        const char *v = args + 4; while (*v == ' ') v++;
        struct yh_url u;
        if (!yh_parse_url(v, &u) || yt_slen(v) >= sizeof nat_direct_url) yt_put(out, max, &p, "uso: youtube mp4 http(s)://servidor/video.mp4   (H.264 + AAC, hasta 854x480)");
        else if (!native_mode() && !yt_seq("x86_64", SOMA_ARCH_252)) yt_put(out, max, &p, "youtube: el reproductor nativo solo existe en x86_64");
        else {
            yt_scopy(nat_direct_url, sizeof nat_direct_url, v);
            for (uint32_t i = 0; i < sizeof now_it; ++i) ((uint8_t *)&now_it)[i] = 0;
            yt_scopy(now_it.id, sizeof now_it.id, NAT_DIRECT_ID); yt_scopy(now_it.title, sizeof now_it.title, u.path); yt_scopy(now_it.channel, sizeof now_it.channel, u.host);
            cur_idx = -1; view = V_WATCH; tab = T_NEXT; ia_video_reset(now_it.id);
            play_at(0);
            if (uo && um) uo[0] = 0;
            return 2;                                            /* hay que abrir la ventana, sin buscar nada */
        }
    } else if (yt_starts(args, "ia ") || yt_seq(args, "ia")) {                                /* SOMA_251 */
        const char *v = args + 2; while (*v == ' ') v++;
        if (yt_seq(v, "on") || yt_seq(v, "si")) conf.ai = 1; else if (yt_seq(v, "off") || yt_seq(v, "no")) conf.ai = 0;
        else { yt_put(out, max, &p, "uso: youtube ia on|off   (ahora: "); yt_put(out, max, &p, conf.ai ? "on)" : "off)"); }
        if (out[0] != 'u') { conf_save(); yt_put(out, max, &p, conf.ai ? "youtube: el buscador entiende lenguaje natural (usa el LLM del sistema)" : "youtube: el buscador busca el texto tal cual"); }
    } else if (yt_starts(args, "idioma ")) {
        const char *v = args + 7; while (*v == ' ') v++;
        if (yt_lang_ok(v)) { yt_scopy(conf.lang, sizeof conf.lang, v); conf_save(); yt_put(out, max, &p, "youtube: idioma de la transcripcion: "); yt_put(out, max, &p, conf.lang); }
        else yt_put(out, max, &p, "uso: youtube idioma auto|es|en|fr|...");
    } else if (yt_starts(args, "minutos ")) {
        int m = (int)yt_num(args + 8, 4, 0);
        if (m >= 1 && m <= 120) { conf.max_min = m; conf_save(); yt_put(out, max, &p, "youtube: whisper transcribe como mucho "); yt_put_dec(out, max, &p, (uint64_t)m); yt_put(out, max, &p, " min de cada video"); }
        else yt_put(out, max, &p, "uso: youtube minutos 1..120");
    } else if (yt_starts(args, "contexto ")) {
        uint32_t c = yt_num(args + 9, 7, 0);
        if (c >= 2000 && c <= 120000) { conf.ctx = (int)c; conf_save(); yt_put(out, max, &p, "youtube: se pasan al LLM hasta "); yt_put_dec(out, max, &p, c); yt_put(out, max, &p, " caracteres de transcripcion"); }
        else yt_put(out, max, &p, "uso: youtube contexto 2000..120000   (caracteres de transcripcion que caben en tu modelo)");
    } else if (yt_starts(args, "ver ")) {
        int n = (int)yt_num(args + 4, 3, 0);
        if (n == 0 && (yt_seq(args + 4, "ultimo") || yt_seq(args + 4, "último"))) n = nitems;     /* SOMA_286_YOUTUBE */
        voz_pend = 0;
        if (n >= 1 && n <= nitems) { play_item(n - 1); yt_put(out, max, &p, "youtube: "); yt_put(out, max, &p, now_it.title); ret = 3; }
        else yt_put(out, max, &p, nitems ? "youtube: no hay un resultado con ese numero" : "youtube: no hay resultados: busca antes (youtube <busqueda>)");
    } else if (yt_seq(args, "pausa") || yt_seq(args, "sigue") || yt_seq(args, "siguiente") || yt_seq(args, "anterior")) {   /* SOMA_286_YOUTUBE: para la voz */
        voz_pend = 0;
        if (!now_it.id[0]) yt_put(out, max, &p, "youtube: no hay ningun video abierto");
        else {
            struct media_info mi; pl_get_info(&mi); int ph = phase_of(&mi);
            if (args[0] == 'p') { if (ph == PH_PLAY && !mi.paused) pl_pause_toggle(); yt_put(out, max, &p, "youtube: en pausa"); }
            else if (yt_seq(args, "sigue")) { if (ph == PH_PLAY && mi.paused) pl_pause_toggle(); else if (ph != PH_PLAY) toggle_play(); yt_put(out, max, &p, "youtube: sigue"); }
            else if (yt_seq(args, "siguiente")) { if (cur_idx >= 0 && cur_idx + 1 < nitems) { play_item(cur_idx + 1); yt_put(out, max, &p, "youtube: "); yt_put(out, max, &p, now_it.title); } else yt_put(out, max, &p, "youtube: no hay siguiente"); }
            else { if (cur_idx > 0) { play_item(cur_idx - 1); yt_put(out, max, &p, "youtube: "); yt_put(out, max, &p, now_it.title); } else yt_put(out, max, &p, "youtube: no hay anterior"); }
            ret = 3;
        }
    } else if (yt_seq(args, "transcribir") || yt_seq(args, "subtitulos") || yt_seq(args, "resumen") || yt_starts(args, "pregunta ")) {
        if (!now_it.id[0]) { voz_pend = 0; yt_put(out, max, &p, "youtube: antes hay que abrir un video (youtube ver <n>)"); }
        else {
            ia_voz = (args[0] == 'r' || args[0] == 'p') ? voz_pend : 0; voz_pend = 0;       /* SOMA_286_YOUTUBE: la respuesta, en voz alta */
            if (args[0] == 't') { tab = T_TEXT; do_transcribe(0); }
            else if (args[0] == 's') { tab = T_TEXT; do_transcribe(1); }
            else if (args[0] == 'r') { tab = T_AI; do_summary(); }
            else { tab = T_AI; do_ask(args + 9); }
            if (ia_err) ia_voz = 0;
            yt_put(out, max, &p, ia_err ? ia_status : "youtube: en marcha (mira la ventana o 'youtube estado')");
            ret = 3;
        }
    } else if (yt_seq(args, "estado")) {
        char sn[96]; server_name(sn, sizeof sn);
        struct media_info mi; pl_get_info(&mi);
        yt_put(out, max, &p, "youtube: servicio "); yt_put(out, max, &p, sn);
        yt_put(out, max, &p, conf.key[0] ? " (con clave), " : ", "); yt_put_dec(out, max, &p, (uint64_t)conf.height); yt_put(out, max, &p, "p a ");
        yt_put_dec(out, max, &p, (uint64_t)conf.fps); yt_put(out, max, &p, " fps\n  resultados: "); yt_put_dec(out, max, &p, (uint64_t)nitems);
        yt_put(out, max, &p, ", video: "); yt_put(out, max, &p, now_it.id[0] ? now_it.id : "ninguno");
        yt_put(out, max, &p, ", recibido "); yt_put_dec(out, max, &p, str_bytes / 1024u); yt_put(out, max, &p, " KiB, en memoria ");
        yt_put_dec(out, max, &p, (ring.end - ring.base) / 1024u); yt_put(out, max, &p, " KiB de "); yt_put_dec(out, max, &p, yt_ring_cap(&ring) / 1024u);
        yt_put(out, max, &p, "\n  reproductor: fotogramas "); yt_put_dec(out, max, &p, mi.frames); yt_put(out, max, &p, ", saltados "); yt_put_dec(out, max, &p, mi.dropped);
        yt_put(out, max, &p, mi.buffering ? ", esperando datos" : "");
        if (str_errgen == str_req && str_err[0]) { yt_put(out, max, &p, "\n  ultimo error: "); yt_put(out, max, &p, str_err); }
        if (net_state == NS_ERROR) { yt_put(out, max, &p, "\n  busqueda: "); yt_put(out, max, &p, net_err); }
        yt_put(out, max, &p, "\n  modo: "); yt_put(out, max, &p, native_mode() ? "nativo (sin servicio externo)" : conf.native ? "servicio (este procesador no tiene reproductor nativo)" : "servicio");
        if (nat_on) { struct media_info m2; pl_get_info(&m2); yt_put(out, max, &p, ", nativo: estado "); yt_put_dec(out, max, &p, (uint64_t)m2.state); yt_put(out, max, &p, ", "); yt_put_dec(out, max, &p, m2.frames); yt_put(out, max, &p, " fotogramas, "); yt_put_dec(out, max, &p, m2.dropped); yt_put(out, max, &p, " tarde, "); yt_put_dec(out, max, &p, m2.width); yt_put(out, max, &p, "x"); yt_put_dec(out, max, &p, m2.height); }
        yt_put(out, max, &p, "\n  IA: buscador "); yt_put(out, max, &p, conf.ai ? "on" : "off"); yt_put(out, max, &p, ", idioma "); yt_put(out, max, &p, conf.lang);
        yt_put(out, max, &p, ", "); yt_put_dec(out, max, &p, (uint64_t)conf.max_min); yt_put(out, max, &p, " min, contexto "); yt_put_dec(out, max, &p, (uint64_t)conf.ctx);
        yt_put(out, max, &p, "\n  transcripcion: "); if (tx_len) { yt_put_dec(out, max, &p, tx_len); yt_put(out, max, &p, " caracteres ("); yt_put(out, max, &p, tx_src); yt_put(out, max, &p, ")"); } else yt_put(out, max, &p, "no");
        yt_put(out, max, &p, ", ficha: "); yt_put(out, max, &p, vd_state == 1 ? "si" : "no"); yt_put(out, max, &p, ", conversacion: "); yt_put_dec(out, max, &p, chat_len);
        yt_put(out, max, &p, ", trabajo: "); yt_put_dec(out, max, &p, (uint64_t)ia_run);
        if (ia_status[0]) { yt_put(out, max, &p, "\n  "); yt_put(out, max, &p, ia_status); }
        if (ai_state != AI_OFF) { yt_put(out, max, &p, "\n  busqueda IA ("); yt_put_dec(out, max, &p, (uint64_t)ai_state); yt_put(out, max, &p, "):"); for (int i = 0; i < ai_nq; ++i) { yt_put(out, max, &p, " ["); yt_put(out, max, &p, ai_q[i]); yt_put(out, max, &p, "]"); } }
    } else return 0;                                             /* abrir la ventana (y buscar, si hay texto) */
    if (uo && p < um) yt_scopy(uo, um, out);                     /* lo escribe quien ha llamado (el terminal o un agente) */
    else { if (uo && um) uo[0] = 0; kprint(out); kprint("\n"); }
    return ret;                                                  /* SOMA_286_YOUTUBE: 3 = hecho, y hay que traer la ventana delante */
}
