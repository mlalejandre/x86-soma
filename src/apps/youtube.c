/* SOMA_250_YOUTUBE: ver youtube.h
 *
 * Reparto del trabajo (el planificador del nucleo es cooperativo: ningun hilo interrumpe a otro a media funcion, por eso los
 * datos compartidos de abajo no llevan cerrojos; lo unico que cruza hilos son contadores "pedido/atendido" y punteros que se
 * escriben de una vez):
 *   - la ventana (hilo del escritorio): dibuja, atiende raton y teclado y decide que se pide;
 *   - hilo ytnet: busquedas y miniaturas (peticiones cortas al servicio);
 *   - hilo ytstream: recibe el video y lo mete en la ventana de memoria (yt_ring) de la que lee el reproductor;
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

extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);
extern int keyboard_last_mods(void);

#define YT_RED      0x00FF4444u
#define YT_STAGE    0x00060910u
#define CONF_PATH   "/etc/secreto/youtube.conf"
#define RING_BLOCKS 16                      /* 16 MiB de video en memoria (unos 25 s a 360p) */
#define UI_GONE_MS  4000u                   /* sin ventana durante tanto tiempo: se corta la descarga */
#define NET_CAP     65536u

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
static int y_get(const char *path, char **body, uint32_t *len, uint32_t timeout_ms, const volatile uint32_t *gen, uint32_t my, char *err, uint32_t emax)
{
    uint32_t ep = 0; err[0] = 0;
    int h = y_connect(err, emax);
    if (h < 0) return -1;
    if (y_send_get(h, path) != 0) { tcp_close(h); yt_put(err, emax, &ep, "No se pudo enviar la peticion al servicio"); return -1; }
    uint32_t total = 0; uint64_t t_start = ms(); int r = 0;
    while (total < NET_CAP - 1) {
        if (*gen != my) { r = -2; break; }
        if (ms() - t_start > timeout_ms) { r = -3; break; }
        uint32_t room = NET_CAP - 1 - total;
        int n = tcp_recv(h, netbuf + total, (uint16_t)(room > 16384 ? 16384 : room), 60);
        if (n > 0) total += (uint32_t)n;
        else if (n == TCP_EOF || n == TCP_ERR) break;
    }
    if (r) tcp_abort(h); else tcp_close(h);
    if (r == -2) return -2;
    if (r == -3) { yt_put(err, emax, &ep, "El servicio YouTube no responde (tiempo agotado)"); return -1; }
    int status = 0; uint32_t off = 0;
    if (yt_http_head((const char *)netbuf, total, &status, &off) != 1) { yt_put(err, emax, &ep, "Respuesta no valida del servicio YouTube"); return -1; }
    netbuf[total] = 0;
    *body = (char *)netbuf + off; *len = total - off;
    if (status != 200) y_errtext(err, emax, *body, *len > 150 ? 150 : *len, status);
    return status;
}

/* ================================================================== hilo ytnet: busquedas y miniaturas */
static int ui_gone(void) { return ms() - ui_seen_ms > UI_GONE_MS; }

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
        char q[160]; yt_scopy(q, sizeof q, net_query);
        char path[900], err[160]; char *body; uint32_t len = 0;
        net_state = NS_SEARCH;
        yt_path_search(path, sizeof path, &conf, q, 12);
        int st = y_get(path, &body, &len, 70000u, &net_req, g, err, sizeof err);
        if (st == -2) continue;                                  /* ha llegado otra busqueda */
        int n = st == 200 ? yt_parse_results(body, len, stage, YT_MAX_ITEMS) : -1;
        if (n < 0) {
            if (st == 200) yt_scopy(net_err, sizeof net_err, "El servicio YouTube ha respondido algo inesperado");
            else yt_scopy(net_err, sizeof net_err, err);
            net_state = NS_ERROR; net_done = g; idle_since = ms();
            continue;
        }
        stage_n = n; stage_gen = g; stage_ready = 1;             /* la ventana los adopta en su proximo tick */
        net_state = NS_THUMBS;
        for (int i = 0; i < n && net_req == g && !ui_gone(); ++i) {
            char id[YT_ID_LEN + 1]; yt_scopy(id, sizeof id, stage[i].id);
            yt_path_thumb(path, sizeof path, &conf, id);
            st = y_get(path, &body, &len, 15000u, &net_req, g, err, sizeof err);
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

static void stream_job(uint32_t g)
{
    char id[YT_ID_LEN + 1], title[128], path[400], err[160];
    yt_scopy(id, sizeof id, str_id); yt_scopy(title, sizeof title, str_title);
    uint32_t t = str_t;
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
    if (!netbuf) netbuf = (uint8_t *)kmalloc(NET_CAP);
    if (!rxtmp) rxtmp = (uint8_t *)kmalloc(16384);
}
static void stream_request(const char *id, uint32_t t, const char *title)
{
    threads_up();
    if (!rxtmp) { yt_scopy(note, sizeof note, "Sin memoria"); return; }
    yt_scopy(str_id, sizeof str_id, id); yt_scopy(str_title, sizeof str_title, title); str_t = t;
    str_req++;
    if (media_stream_busy()) media_stop();                       /* lo que sonaba se calla ya, sin esperar al hilo del video */
    if (!str_alive) { str_alive = 1; if (!thread_create("ytstream", ytstream_main, 0)) { str_alive = 0; yt_scopy(note, sizeof note, "No se pudo crear el hilo del video"); } }
}
static void play_at(uint32_t t)
{
    if (!now_it.id[0]) return;
    if (now_it.dur && t >= now_it.dur) t = now_it.dur > 2 ? now_it.dur - 2 : 0;
    t0 = t; last_pos = t; held = 0; note[0] = 0;
    stream_request(now_it.id, t, now_it.title);
}
static void play_item(int i)
{
    if (i < 0 || i >= nitems) return;
    now_it = items[i]; cur_idx = i; view = V_WATCH; tf_focus = 0; side_scroll = 0;
    play_at(0);
}
static void stop_playback(void)
{
    if (str_alive && (str_done != str_req || media_stream_busy())) stream_request("", 0, "");
    held = 0; now_it.id[0] = 0; cur_idx = -1;
}
static void do_search(const char *q)
{
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
    threads_up();
    if (!netbuf) { yt_scopy(note, sizeof note, "Sin memoria"); return; }
    yt_scopy(net_query, sizeof net_query, q); yt_scopy(last_query, sizeof last_query, q);
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
}

void youtube_init(void)
{
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
}

int youtube_tick(void)
{
    uint64_t t = ms();
    ui_seen_ms = t;
    int ch = 0;
    if (stage_ready) { adopt_results(); ch = 1; }
    struct media_info mi; media_get_info(&mi);
    int ph = phase_of(&mi);
    if (now_it.id[0] && !held) {
        media_keepalive();
        if (ph == PH_PLAY) {
            uint32_t p = t0 + (uint32_t)(mi.pos_us / 1000000u);
            if (now_it.dur && p > now_it.dur) p = now_it.dur;
            last_pos = p;
        } else if (ph == PH_ENDED && ended_gen != str_req) {
            ended_gen = str_req;
            uint32_t p = t0 + (uint32_t)(mi.pos_us / 1000000u);
            if (now_it.dur && p + 4 < now_it.dur) {              /* se ha cortado antes del final (red o servicio) */
                held = 1; held_t = p; yt_scopy(note, sizeof note, "El video se ha interrumpido. Pulsa reproducir para continuar.");
            } else if (cur_idx >= 0 && cur_idx + 1 < nitems) play_item(cur_idx + 1);
            else { held = 1; held_t = 0; last_pos = now_it.dur; }
        } else if (str_live == str_req && str_errgen != str_req && !mi.stream) {
            /* el reproductor ya no tiene nuestro video: la ventana estuvo oculta (se para solo) o lo ha tomado SOMA Media */
            held = 1; held_t = last_pos; stream_request("", 0, "");
        }
        ph = phase_of(&mi);
    }
    uint32_t sig = mi.frames * 131u + (uint32_t)ph * 7u + last_pos * 13u + (uint32_t)net_state * 3u + thumbs_seq * 17u + (uint32_t)mi.paused * 5u + (uint32_t)mi.buffering * 11u;
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
};
#define BAR_H 64
static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct ylay *L)
{
    L->x = (int)wx; L->y = (int)wy; L->w = (int)ww; L->h = (int)wh;
    L->back_w = view == V_WATCH ? 44 : 0;
    char qs[8]; uint32_t qp = 0; yt_put_dec(qs, sizeof qs, &qp, (uint64_t)conf.height); yt_put(qs, sizeof qs, &qp, "p");
    L->q_w = si_chip_w(26, qs, 0); L->q_h = 26; L->q_x = L->x + L->w - 20 - L->q_w; L->q_y = L->y + 19;
    int left = L->x + 20 + L->back_w + (L->w >= 700 ? 150 : 52);
    L->sb_r = 17; L->sb_cx = L->q_x - 16 - L->sb_r; L->sb_cy = L->y + 32;
    L->sf_x = left; L->sf_y = L->y + 17; L->sf_h = 30; L->sf_w = L->sb_cx - L->sb_r - 10 - left;
    if (L->sf_w > 560) { L->sf_x += (L->sf_w - 560) / 2; L->sf_w = 560; L->sb_cx = L->sf_x + L->sf_w + 10 + L->sb_r; }
    if (L->sf_w < 60) L->sf_w = 60;
    /* cuadricula */
    L->gap = 16; L->gx = L->x + 20; L->gy = L->y + BAR_H + 34; L->gw = L->w - 40; L->gh = L->y + L->h - L->gy;
    if (L->gw < 120) L->gw = 120;
    L->cols = (L->gw + L->gap) / (228 + L->gap); if (L->cols < 1) L->cols = 1;
    L->card_w = (L->gw - L->gap * (L->cols - 1)) / L->cols;
    L->th_h = L->card_w * 9 / 16; L->card_h = L->th_h + 66;
    /* viendo un video */
    L->sw = L->w >= 920 ? 320 : (L->w >= 760 ? 250 : 0);
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
    tf_render(&tf, L->sf_x, L->sf_y, L->sf_w, L->sf_h, tf_focus, "Buscar en YouTube", SI_TXT);
    si_round(L->sb_cx, L->sb_cy, L->sb_r, SI_SEARCH, SIB_NEUTRAL, YT_RED, 0);
    char qs[8]; uint32_t qp = 0; yt_put_dec(qs, sizeof qs, &qp, (uint64_t)conf.height); yt_put(qs, sizeof qs, &qp, "p");
    si_chip(L->q_x, L->q_y, L->q_h, qs, SI_DIM, 0);
}

static void draw_browse(const struct ylay *L)
{
    /* linea de estado bajo la barra */
    char st[220]; uint32_t p = 0; uint32_t col = SI_DIM;
    if (net_state == NS_SEARCH) { yt_put(st, sizeof st, &p, "Buscando \""); yt_put(st, sizeof st, &p, last_query); yt_put(st, sizeof st, &p, "\""); yt_put(st, sizeof st, &p, dots()); }
    else if (net_state == NS_ERROR) { yt_put(st, sizeof st, &p, net_err); col = 0x00FDA4AFu; }
    else if (note[0]) yt_put(st, sizeof st, &p, note);
    else if (nitems) { yt_put_dec(st, sizeof st, &p, (uint64_t)nitems); yt_put(st, sizeof st, &p, " resultados de \""); yt_put(st, sizeof st, &p, last_query); yt_put(st, sizeof st, &p, "\""); }
    else st[0] = 0;
    si_text_fit(L->gx, L->y + BAR_H + 6, 22, st, L->gw, 12, col, 0);

    if (!nitems) {
        int cx = L->x + L->w / 2, cy = L->gy + L->gh / 2 - 60; if (cy < L->gy + 40) cy = L->gy + 40;
        fb_fill_rrect_aa(cx - 46, cy - 32, 92, 64, 18, si_mix(SI_WIN, YT_RED, net_state == NS_ERROR ? 40 : 200));
        si_icon(net_state == NS_ERROR ? SI_CLOSE : SI_PLAY, cx + (net_state == NS_ERROR ? 0 : 3), cy, 30, 0x00FFFFFFu);
        if (net_state == NS_ERROR) {
            char sn[96]; server_name(sn, sizeof sn);
            si_text_c(L->x, cy + 46, L->w, 24, "No se puede hablar con el servicio YouTube", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 76, L->w, 20, "1. En tu Mac o PC:  python3 tools/yt_service.py --bind 0.0.0.0", 12, SI_DIM, 0);
            si_text_c(L->x, cy + 96, L->w, 20, "2. Aqui, en la caja de busqueda:  servidor <IP de ese equipo>", 12, SI_DIM, 0);
            char l3[140]; uint32_t q = 0; yt_put(l3, sizeof l3, &q, "Ahora se busca en "); yt_put(l3, sizeof l3, &q, sn);
            si_text_c(L->x, cy + 120, L->w, 20, l3, 12, SI_FAINT, 0);
        } else if (net_state == NS_SEARCH) {
            si_text_c(L->x, cy + 46, L->w, 24, "Buscando en YouTube", 16, SI_TXT, 1);
        } else {
            si_text_c(L->x, cy + 46, L->w, 24, "Busca un video para empezar", 16, SI_TXT, 1);
            si_text_c(L->x, cy + 74, L->w, 20, "Escribe arriba y pulsa Intro. Los videos se ven aqui mismo, mientras llegan.", 12, SI_FAINT, 0);
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
    }
    clip_pop(c);
    if (maxs > 0) {
        int th = L->gh * L->gh / total; if (th < 24) th = 24;
        fb_fill_rrect_aa(L->x + L->w - 9, L->gy + (L->gh - th) * scroll_px / maxs, 4, th, 2, SI_LINE);
    }
}

static void draw_watch(const struct ylay *L)
{
    struct media_info mi; media_get_info(&mi);
    int ph = phase_of(&mi);
    /* escenario */
    fb_fill_rrect_aa(L->vx, L->vy, L->vw, L->vh, 10, 0x00000000u);
    const uint32_t *px; uint32_t fw, fh;
    int have = (ph == PH_PLAY || ph == PH_ENDED) && media_frame(&px, &fw, &fh);
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

    /* lista lateral */
    if (L->sw) {
        si_text(L->sx, L->sy, 24, "A continuacion", 13, SI_TXT, 1);
        int ly = L->sy + 30, lh = L->sh - 30, total = nitems * L->row_h, maxs = total - lh; if (maxs < 0) maxs = 0;
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
    }
}

void youtube_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    conf_load();
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    struct ylay L; make_layout(wx, wy, ww, wh, &L);
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
    if (!L->sw || !inside(mx, my, L->sx - 6, L->sy + 30, L->sw + 12, L->sh - 30)) return -1;
    int i = (my - (L->sy + 30) + side_scroll) / L->row_h;
    return i >= 0 && i < nitems ? i : -1;
}

static void toggle_play(void)
{
    struct media_info mi; media_get_info(&mi);
    int ph = phase_of(&mi);
    if (ph == PH_HELD) play_at(held_t);
    else if (ph == PH_PLAY) media_pause_toggle();
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
    conf.height = conf.height == 240 ? 360 : conf.height == 360 ? 480 : 240;
    conf_save();
    if (now_it.id[0] && view == V_WATCH && !held) play_at(last_pos);
}

int youtube_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct ylay L; make_layout(wx, wy, ww, wh, &L);
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
    if (my < L.y + BAR_H) return 0;
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
    if (tf_ready_ && tf_focus && tf_drag(&tf, mx, my, held_btn)) rd = 1;
    if (wheel && inside(mx, my, L.x, L.y, L.w, L.h)) {
        if (view == V_BROWSE) scroll_px -= wheel * 60;
        else if (L.sw && mx >= L.sx - 6) side_scroll -= wheel * 60;
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
    if (tf_focus) { tf_focus = 0; return 1; }
    if (view == V_WATCH && now_it.id[0]) { stop_playback(); view = V_BROWSE; note[0] = 0; return 1; }
    return 0;                                                    /* nada que cerrar: Esc hace lo de siempre en el escritorio */
}

int youtube_clip_op(int op)                          /* Ctrl+C / Ctrl+X / Ctrl+V del escritorio: van a la caja de busqueda */
{
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
    tf_focus = 1;
    tf_clip(&tf, op);
    return 1;
}

void youtube_handle_key(char c)
{
    int k = (uint8_t)c;
    if (!tf_ready_) { tf_init(&tf, query, sizeof query, 0); tf_ready_ = 1; }
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
    } else if (yt_seq(args, "estado")) {
        char sn[96]; server_name(sn, sizeof sn);
        struct media_info mi; media_get_info(&mi);
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
    } else return 0;                                             /* abrir la ventana (y buscar, si hay texto) */
    kprint(out); kprint("\n");
    return 1;
}
