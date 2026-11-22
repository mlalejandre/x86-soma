/* SOMA_252_VIDEO_NATIVO: YouTube sin ningun equipo externo. Este archivo se incluye desde youtube.c.
 *
 *   busqueda y ficha   SOMA habla con YouTube por HTTPS (yt_https.c) y lee sus respuestas (yt_inner.h);
 *   miniaturas         https://i.ytimg.com/vi/<id>/mqdefault.jpg (JPEG normal);
 *   video              el formato 18 (MP4 de 360p, H.264 + AAC), que se descarga por trozos segun se ve y lo
 *                      decodifica el reproductor nativo de ring 3 (vplay_sys.c, SOMA_252).
 * Solo en x86_64 (el reproductor nativo no existe en aarch64: alli sigue haciendo falta el servicio).
 * Tambien reproduce cualquier MP4 de una direccion http(s):  youtube mp4 <direccion>. */
#include "yt_inner.h"
#ifdef __x86_64__
#include "../user/vplay/vp_abi.h"
#endif

#define NAT_DIRECT_ID "_mp4direct_"                              /* "video" que no es de YouTube: una direccion de MP4 */
static char nat_direct_url[YTI_URL_MAX];
static char nat_desc[6200]; static char nat_desc_id[YT_ID_LEN + 1]; static uint32_t nat_views;      /* ficha del ultimo video pedido */
static volatile int nat_on;                                      /* lo que suena (o se esta abriendo) es nativo */
#ifdef __x86_64__
static char nat_id[YT_ID_LEN + 1];                              /* el video que tiene abierto el reproductor nativo */
#endif
static volatile uint32_t nat_seek_ms;                            /* salto pendiente al arrancar (reanudar a media pelicula) */

static int native_mode(void)
{
#ifdef __x86_64__
    return conf.native;
#else
    return 0;
#endif
}

/* ---- busqueda y miniaturas (hilo ytnet) */
static int nat_search(const char *q, struct yt_item *out, int max, const volatile uint32_t *gen, uint32_t my, char *err, uint32_t emax)
{
    const uint32_t CAP = 900u * 1024u;
    uint8_t *b = (uint8_t *)kmalloc(CAP); char body[900]; uint32_t len = 0, ep = 0;
    if (!b) { yt_put(err, emax, &ep, "Sin memoria para la busqueda"); return -1; }
    uint32_t bl = yti_body_search(body, sizeof body, q);
    char url[160]; uint32_t up = 0;
    yt_put(url, sizeof url, &up, conf.api[0] ? conf.api : YTI_HOST); yt_put(url, sizeof url, &up, "/youtubei/v1/search?prettyPrint=false");
    int st = yh_fetch(url, "POST", YTI_HDR_WEB, body, bl, b, CAP, &len, 25000u, gen, my, err, emax);
    int n = -1;
    if (st == -3) n = -2;
    else if (st == 200) {
        n = yti_parse_search((const char *)b, len, out, max);
        if (n == 0 && !yti_find((const char *)b, (const char *)b + len, "\"estimatedResults\"")) { yt_put(err, emax, &ep, "YouTube ha respondido algo que SOMA no entiende (puede haber cambiado: hace falta actualizar)"); n = -1; }
    } else if (st > 0) { yt_put(err, emax, &ep, "YouTube ha rechazado la busqueda (HTTP "); yt_put_dec(err, emax, &ep, (uint64_t)st); yt_put(err, emax, &ep, ")"); }
    kfree(b);
    return n;
}
static int nat_thumb(const char *id, uint8_t *buf, uint32_t cap, uint32_t *len, const volatile uint32_t *gen, uint32_t my)
{
    char url[200], err[120];
    if (conf.api[0]) { uint32_t up = 0; yt_put(url, sizeof url, &up, conf.api); yt_put(url, sizeof url, &up, "/vi/"); yt_put(url, sizeof url, &up, id); yt_put(url, sizeof url, &up, "/mqdefault.jpg"); }
    else yti_thumb_url(url, sizeof url, id);
    return yh_fetch(url, "GET", "User-Agent: " YTI_WEB_UA "\r\n", 0, 0, buf, cap, len, 12000u, gen, my, err, sizeof err);
}

/* ---- ficha y direccion del video: 1 = bien (o->url puede venir vacia: mirar o->playable y o->reason) */
static int nat_player(const char *id, struct yti_player *o, char *err, uint32_t emax, const volatile uint32_t *gen, uint32_t my)
{
    const uint32_t CAP = 320u * 1024u;
    uint8_t *b = (uint8_t *)kmalloc(CAP); char body[700]; uint32_t len = 0, ep = 0;
    if (!b) { yt_put(err, emax, &ep, "Sin memoria"); return 0; }
    uint32_t bl = yti_body_player(body, sizeof body, id);
    char url[160]; uint32_t up = 0;
    yt_put(url, sizeof url, &up, conf.api[0] ? conf.api : YTI_HOST); yt_put(url, sizeof url, &up, "/youtubei/v1/player?prettyPrint=false");
    int st = yh_fetch(url, "POST", YTI_HDR_AND, body, bl, b, CAP, &len, 25000u, gen, my, err, emax);
    int ok = 0;
    if (st == 200) {
        static char d[6200];
        ok = yti_parse_player_ex((const char *)b, len, o, d, sizeof d, conf.api[0] != 0);      /* con una base propia (pruebas) el video puede venir de cualquier sitio */
        if (!ok) yt_put(err, emax, &ep, "YouTube ha respondido algo que SOMA no entiende (puede haber cambiado: hace falta actualizar)");
        else { yt_scopy(nat_desc, sizeof nat_desc, d); yt_scopy(nat_desc_id, sizeof nat_desc_id, id); nat_views = o->views; }
    } else if (st > 0) { yt_put(err, emax, &ep, "YouTube ha rechazado la peticion del video (HTTP "); yt_put_dec(err, emax, &ep, (uint64_t)st); yt_put(err, emax, &ep, ")"); }
    kfree(b);
    return ok;
}

#ifdef __x86_64__
/* ---- de donde saca el reproductor los bytes: una conexion que se mantiene mientras la lectura sea seguida */
struct nsrc { char url[YTI_URL_MAX]; const char *hdr; struct yh c; int open; uint64_t pos, total; };

static int64_t nsrc_fetch(void *ctx, uint64_t off, uint8_t *buf, uint32_t max, uint64_t *total, char *err, uint32_t emax)
{
    struct nsrc *s = (struct nsrc *)ctx; uint32_t ep = 0;
    if (s->total && off >= s->total) { *total = s->total; return 0; }
    if (!s->open || s->pos != off) {
        if (s->open) { yh_close(&s->c, 1); s->open = 0; }
        uint64_t tot = 0;
        int st = yh_open(&s->c, s->url, "GET", s->hdr, 0, 0, (int64_t)off, &tot, 20000u, err, emax);
        if (st < 0) return -1;
        if (st == 416) { yh_close(&s->c, 1); if (!s->total) s->total = off; *total = s->total; return 0; }
        if ((st != 206 && st != 200) || (st == 200 && off)) {
            yh_close(&s->c, 1);
            yt_put(err, emax, &ep, st == 403 ? "YouTube ha rechazado la descarga del video (403: la direccion ha caducado o no vale para este equipo)" : "El servidor del video ha respondido HTTP ");
            if (st != 403) yt_put_dec(err, emax, &ep, (uint64_t)st);
            return -1;
        }
        s->open = 1; s->pos = off;
        if (tot) s->total = tot;
    }
    uint32_t got = 0; uint64_t t0 = ms();
    while (got < max) {
        int r = yh_read(&s->c, buf + got, max - got, 120);
        if (r > 0) { got += (uint32_t)r; if (got >= 65536u || ms() - t0 > 400u) break; }
        else if (r == -1) { yh_close(&s->c, 0); s->open = 0; if (!s->total) s->total = s->pos + got; break; }
        else if (r == -2) { yh_close(&s->c, 1); s->open = 0; if (!got) { yt_put(err, emax, &ep, "La descarga del video se ha cortado"); return -1; } break; }
        else if (got) break;
        else if (ms() - t0 > 25000u) { yh_close(&s->c, 1); s->open = 0; yt_put(err, emax, &ep, "El video no llega (la red se ha quedado parada)"); return -1; }
    }
    s->pos += got; *total = s->total;
    return (int64_t)got;
}
static void nsrc_close(void *ctx) { struct nsrc *s = (struct nsrc *)ctx; if (s->open) yh_close(&s->c, 1); kfree(s); }

static void nat_stop(void) { if (vps_active()) vps_stop(); nat_on = 0; }

/* El trabajo del hilo ytstream cuando el modo es nativo: abre el video y se queda vigilando hasta que piden otra cosa. */
static void native_job(uint32_t g, const char *id, uint32_t t)
{
    char err[200]; err[0] = 0;
    media_release();
    vps_stop();
    nat_on = 1; nat_id[0] = 0;
    if (!id[0]) { nat_on = 0; return; }
    struct nsrc *s = (struct nsrc *)kmalloc(sizeof *s);
    if (!s) { str_fail(g, "Sin memoria"); return; }
    s->open = 0; s->pos = 0; s->total = 0; s->url[0] = 0; s->hdr = YTI_HDR_MEDIA;
    if (yt_seq(id, NAT_DIRECT_ID)) { yt_scopy(s->url, sizeof s->url, nat_direct_url); s->hdr = "User-Agent: SOMA-Video/1\r\n"; }
    else {
        static struct yti_player pl;
        if (!nat_player(id, &pl, err, sizeof err, &str_req, g)) { kfree(s); if (str_req == g) str_fail(g, err[0] ? err : "No se pudo pedir el video a YouTube"); return; }
        if (str_req != g) { kfree(s); return; }
        if (!pl.url[0]) {
            uint32_t p = 0;
            if (!pl.playable) { yt_put(err, sizeof err, &p, "YouTube no deja ver este video: "); yt_put(err, sizeof err, &p, pl.reason[0] ? pl.reason : pl.status); }
            else yt_put(err, sizeof err, &p, "YouTube no ha dado este video en un formato que SOMA sepa pedir (360p directo). Con el servicio externo si se puede: youtube modo servicio");
            kfree(s); str_fail(g, err); return;
        }
        yt_scopy(s->url, sizeof s->url, pl.url);
        if (yt_seq(now_it.id, id)) { if (!now_it.dur && pl.dur) now_it.dur = pl.dur; if (!now_it.views) now_it.views = pl.views; }
    }
    struct vps_source src; src.fetch = nsrc_fetch; src.close = nsrc_close; src.ctx = s;
    if (!vps_start(&src, err, sizeof err)) { str_fail(g, err); return; }       /* vps_start ya ha cerrado la fuente */
    nat_seek_ms = t * 1000u;
    yt_scopy(nat_id, sizeof nat_id, id);
    str_live = g;
    uint64_t t0 = ms(); int seeked = nat_seek_ms == 0; const char *why = "sustituido";
    while (str_req == g) {
        if (ui_gone()) { why = "ventana cerrada"; break; }
        struct vps_info vi; vps_get_info(&vi);
        if (vi.state == VPS_ERROR) { str_fail(g, vi.err[0] ? vi.err : "El video no se puede reproducir"); why = "error"; break; }
        if (vi.state == VPS_IDLE) { why = "parado"; break; }
        if (!seeked && vi.state == VPS_PLAYING) { vps_seek(nat_seek_ms); seeked = 1; }
        if (vi.state == VPS_LOADING && ms() - t0 > 60000u) { str_fail(g, "El video no arranca (la red va muy lenta o YouTube no responde)"); why = "sin datos"; break; }
        thread_sleep(40);
    }
    {
        struct vps_info vi; vps_get_info(&vi);
        char l[220]; uint32_t q = 0;
        yt_put(l, sizeof l, &q, "[youtube] nativo "); yt_put(l, sizeof l, &q, id); yt_put(l, sizeof l, &q, " desde "); yt_put_dec(l, sizeof l, &q, t);
        yt_put(l, sizeof l, &q, " s: "); yt_put_dec(l, sizeof l, &q, vi.have_bytes / 1024u); yt_put(l, sizeof l, &q, " KiB, "); yt_put_dec(l, sizeof l, &q, vi.frames_shown);
        yt_put(l, sizeof l, &q, " fotogramas ("); yt_put_dec(l, sizeof l, &q, vi.frames_late); yt_put(l, sizeof l, &q, " tarde, "); yt_put_dec(l, sizeof l, &q, vi.errors);
        yt_put(l, sizeof l, &q, " errores), "); yt_put(l, sizeof l, &q, why); yt_put(l, sizeof l, &q, "\n");
        kprint(l);
    }
    vps_stop();
}
#else
static void nat_stop(void) { nat_on = 0; }
static void native_job(uint32_t g, const char *id, uint32_t t) { (void)id; (void)t; str_fail(g, "El modo nativo solo existe en x86_64"); }
#endif

/* ---- lo que la ventana le pregunta al reproductor, sea el nativo o SOMA Media */
static void pl_get_info(struct media_info *mi)
{
#ifdef __x86_64__
    if (nat_on) {
        static char emsg[128];
        struct vps_info vi; vps_get_info(&vi);
        mi->stream = vi.state != VPS_IDLE; mi->buffering = vi.buffering; mi->paused = vi.paused;
        mi->state = vi.state == VPS_LOADING ? 1 : vi.state == VPS_PLAYING ? (vi.paused ? 3 : 2) : vi.state == VPS_ENDED ? 4 : vi.state == VPS_ERROR ? 5 : 0;
        mi->pos_us = (uint64_t)vi.pos_ms * 1000u; mi->dur_us = (uint64_t)vi.dur_ms * 1000u;
        mi->frames = vi.frame_seq; mi->dropped = vi.frames_late; mi->width = vi.w; mi->height = vi.h;
        yt_scopy(emsg, sizeof emsg, vi.err); mi->msg = emsg;
        if (vi.state == VPS_LOADING) mi->buffering = 0;
        if (now_it.id[0] && !now_it.dur && vi.dur_ms) now_it.dur = (vi.dur_ms + 500u) / 1000u;      /* un MP4 suelto: la duracion la dice el archivo */
        return;
    }
#endif
    media_get_info(mi);
}
static int pl_frame(const uint32_t **px, uint32_t *w, uint32_t *h)
{
#ifdef __x86_64__
    if (nat_on) { uint32_t seq; const uint32_t *p = vps_frame(w, h, &seq); if (!p) return 0; *px = p; return 1; }
#endif
    return media_frame(px, w, h);
}
static void pl_pause_toggle(void)
{
#ifdef __x86_64__
    if (nat_on) { struct vps_info vi; vps_get_info(&vi); vps_pause(!vi.paused); return; }
#endif
    media_pause_toggle();
}
/* salto dentro del video que ya esta abierto en el reproductor nativo: 1 = hecho (no hace falta volver a pedirlo) */
static int pl_seek_live(uint32_t t)
{
#ifdef __x86_64__
    if (nat_on && str_live == str_req && str_errgen != str_req && yt_seq(nat_id, now_it.id)) {
        struct vps_info vi; vps_get_info(&vi);
        if (vi.state == VPS_PLAYING || vi.state == VPS_ENDED) { vps_seek(t * 1000u); if (vi.paused) vps_pause(0); return 1; }
    }
#endif
    (void)t;
    return 0;
}
