/* SOMA_252_VIDEO_NATIVO: lo que YouTube responde cuando SOMA le habla directamente (API "Innertube", JSON por HTTPS).
 * Logica pura, se prueba en el anfitrion con respuestas reales (tests/test_yt_inner_252.c). No es un lector de JSON
 * general: busca los campos que hacen falta por su nombre y descodifica las cadenas; todo acotado por las longitudes.
 *
 *   busqueda  POST /youtubei/v1/search  (cliente WEB)      -> lista de videoRenderer
 *   video     POST /youtubei/v1/player  (cliente ANDROID)  -> ficha y la direccion del formato 18 (MP4 360p, H.264 + AAC)
 *
 * Si YouTube cambia estos nombres hay que tocar SOLO este archivo (y actualizar los datos de la prueba). */
#ifndef SOMA_YT_INNER_H
#define SOMA_YT_INNER_H
#include <stdint.h>
#include "yt_proto.h"

#define YTI_HOST          "https://www.youtube.com"
#define YTI_WEB_VERSION   "2.20260708.00.00"
#define YTI_WEB_UA        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"
#define YTI_AND_VERSION   "21.26.364"
#define YTI_AND_UA        "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip"
#define YTI_HDR_WEB       "User-Agent: " YTI_WEB_UA "\r\nContent-Type: application/json\r\nX-YouTube-Client-Name: 1\r\nX-YouTube-Client-Version: " YTI_WEB_VERSION "\r\nOrigin: https://www.youtube.com\r\nAccept-Language: es\r\n"
#define YTI_HDR_AND       "User-Agent: " YTI_AND_UA "\r\nContent-Type: application/json\r\nX-YouTube-Client-Name: 3\r\nX-YouTube-Client-Version: " YTI_AND_VERSION "\r\nOrigin: https://www.youtube.com\r\nAccept-Language: es\r\n"
#define YTI_HDR_MEDIA     "User-Agent: " YTI_AND_UA "\r\n"
#define YTI_URL_MAX       2000

/* Busca la cadena "key" en [p, end). Devuelve el puntero justo despues, o 0. */
static inline const char *yti_find(const char *p, const char *end, const char *key)
{
    uint32_t kl = 0; while (key[kl]) kl++;
    if (!p || !kl) return 0;
    for (; p + kl <= end; ++p) {
        if (*p != key[0]) continue;
        uint32_t i = 1; while (i < kl && p[i] == key[i]) i++;
        if (i == kl) return p + kl;
    }
    return 0;
}
static inline int yti_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static inline void yti_put_cp(char *o, uint32_t max, uint32_t *n, uint32_t cp, int keep_nl)
{
    if (cp == '\n' && keep_nl) { if (*n + 1 < max) o[(*n)++] = '\n'; return; }
    if (cp < 32 || cp == 127) cp = ' ';
    if (cp == 0xA0) cp = ' ';
    else if (cp == 0x2018 || cp == 0x2019) cp = '\'';
    else if (cp == 0x201C || cp == 0x201D) cp = '"';
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2022) cp = '-';
    else if (cp == 0x2026) { for (int i = 0; i < 3; ++i) if (*n + 1 < max) o[(*n)++] = '.'; return; }
    if (cp >= 0x250) return;                                   /* emoji y alfabetos que las fuentes de SOMA no tienen */
    if (cp == ' ' && (*n == 0 || o[*n - 1] == ' ' || o[*n - 1] == '\n')) return;       /* sin espacios dobles ni al principio */
    if (cp < 0x80) { if (*n + 1 < max) o[(*n)++] = (char)cp; }
    else if (*n + 2 < max) { o[(*n)++] = (char)(0xC0 | (cp >> 6)); o[(*n)++] = (char)(0x80 | (cp & 63)); }
}
/* p apunta dentro de una cadena JSON (tras la comilla de apertura). La descodifica y LIMPIA en out (anade a partir de *n).
 * Devuelve el puntero tras la comilla de cierre, o 0 si la cadena no termina antes de end. */
static inline const char *yti_str_(const char *p, const char *end, char *out, uint32_t max, uint32_t *n, int keep_nl)
{
    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') { if (max) out[*n < max ? *n : max - 1] = 0; return p + 1; }
        if (c == '\\') {
            if (p + 1 >= end) return 0;
            char e = p[1]; p += 2;
            if (e == 'u') {
                if (p + 4 > end) return 0;
                uint32_t cp = 0; for (int i = 0; i < 4; ++i) { int h = yti_hex(p[i]); if (h < 0) return 0; cp = cp * 16 + (uint32_t)h; }
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end && p[0] == '\\' && p[1] == 'u') {      /* par subrogado */
                    uint32_t lo = 0; int ok = 1; for (int i = 0; i < 4; ++i) { int h = yti_hex(p[2 + i]); if (h < 0) ok = 0; lo = lo * 16 + (uint32_t)(h < 0 ? 0 : h); }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                }
                yti_put_cp(out, max, n, cp, keep_nl);
            } else yti_put_cp(out, max, n, e == 'n' ? '\n' : e == 't' || e == 'r' || e == 'b' || e == 'f' ? ' ' : (unsigned char)e, keep_nl);
            continue;
        }
        if (c < 0x80) { yti_put_cp(out, max, n, c, keep_nl); p++; continue; }
        uint32_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, cp = len == 4 ? (c & 7u) : len == 3 ? (c & 15u) : len == 2 ? (c & 31u) : 0xFFFD;
        if (p + len > end) return 0;
        for (uint32_t i = 1; i < len; ++i) cp = (cp << 6) | ((unsigned char)p[i] & 63u);
        yti_put_cp(out, max, n, cp, keep_nl);
        p += len;
    }
    return 0;
}
/* SOMA_315_YOUTUBE_AISLADO: out queda SIEMPRE terminada. Antes, una cadena sin cerrar (respuesta cortada) dejaba lo escrito sin
 * terminador, y quien no miraba el 0 (canal, estado, motivo, titulo y autor de la ficha) la leia hasta lo que hubiera detras. */
static inline const char *yti_str(const char *p, const char *end, char *out, uint32_t max, uint32_t *n, int keep_nl)
{
    const char *r = yti_str_(p, end, out, max, n, keep_nl);
    if (!r && max) out[*n < max ? *n : max - 1] = 0;
    return r;
}
/* Lo mismo sin limpiar (para direcciones): solo deshace los escapes. 0 si no cabe o no termina. */
static inline const char *yti_raw(const char *p, const char *end, char *out, uint32_t max)
{
    uint32_t n = 0;
    while (p < end) {
        char c = *p;
        if (c == '"') { out[n] = 0; return p + 1; }
        if (c == '\\') {
            if (p + 1 >= end) return 0;
            char e = p[1]; p += 2;
            if (e == 'u') {
                if (p + 4 > end) return 0;
                uint32_t cp = 0; for (int i = 0; i < 4; ++i) { int h = yti_hex(p[i]); if (h < 0) return 0; cp = cp * 16 + (uint32_t)h; }
                p += 4;
                if (cp < 0x21 || cp > 0x7E) return 0;          /* una direccion no lleva espacios ni nada fuera de ASCII */
                c = (char)cp;
            } else c = e;
        } else p++;
        if ((unsigned char)c < 0x21 || (unsigned char)c > 0x7E || n + 1 >= max) return 0;
        out[n++] = c;
    }
    return 0;
}
static inline uint32_t yti_digits(const char *p, const char *end)          /* "23.574.905 visualizaciones" -> 23574905 */
{
    uint64_t v = 0; int any = 0;
    for (; p < end && *p != '"'; ++p) {
        if (*p >= '0' && *p <= '9') { v = v * 10 + (uint64_t)(*p - '0'); any = 1; if (v > 0xFFFFFFFFu) return 0xFFFFFFFFu; }
        else if (*p == '.' || *p == ',' || *p == ' ' || (unsigned char)*p == 0xC2 || (unsigned char)*p == 0xA0) { if (!any) continue; }
        else if (any) break;
    }
    return (uint32_t)v;
}
static inline uint32_t yti_clock(const char *p, const char *end)           /* "1:02:03" o "10:35" -> segundos */
{
    uint32_t t = 0, cur = 0; int parts = 0;
    for (; p < end && *p != '"'; ++p) {
        if (*p >= '0' && *p <= '9') { cur = cur * 10 + (uint32_t)(*p - '0'); if (cur > 100000) return 0; }
        else if (*p == ':') { t = t * 60 + cur; cur = 0; if (++parts > 2) return 0; }
        else return 0;
    }
    return t * 60 + cur;
}

/* Respuesta de la busqueda -> hasta max videos (sin repetir). */
static inline int yti_parse_search(const char *b, uint32_t len, struct yt_item *it, int max)
{
    const char *end = b + len, *p = b; int n = 0;
    static const char K[] = "\"videoRenderer\":{\"videoId\":\"";
    while (n < max && (p = yti_find(p, end, K)) != 0) {
        const char *nx = yti_find(p, end, K), *re = nx ? nx - (sizeof K - 1) : end;
        struct yt_item *o = &it[n];
        int ok = p + YT_ID_LEN < re && p[YT_ID_LEN] == '"';
        for (int i = 0; ok && i < YT_ID_LEN; ++i) { char c = p[i]; ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; o->id[i] = c; }
        if (!ok) continue;
        o->id[YT_ID_LEN] = 0;
        int dup = 0; for (int k = 0; k < n && !dup; ++k) dup = yt_seq(it[k].id, o->id);
        if (dup) continue;
        uint32_t k = 0; const char *q;
        o->title[0] = o->channel[0] = o->desc[0] = 0; o->dur = 0; o->views = 0;
        q = yti_find(p, re, "\"title\":{\"runs\":[{\"text\":\"");
        if (!q || !yti_str(q, re, o->title, sizeof o->title, &k, 0) || !o->title[0]) continue;
        k = 0; q = yti_find(p, re, "\"ownerText\":{\"runs\":[{\"text\":\"");
        if (!q) q = yti_find(p, re, "\"longBylineText\":{\"runs\":[{\"text\":\"");
        if (q) (void)yti_str(q, re, o->channel, sizeof o->channel, &k, 0);
        q = yti_find(p, re, "\"lengthText\":{");
        if (q) { const char *lim = q + 400 < re ? q + 400 : re; q = yti_find(q, lim, "\"simpleText\":\""); if (q) o->dur = yti_clock(q, re); }
        q = yti_find(p, re, "\"viewCountText\":{\"simpleText\":\"");
        if (q) o->views = yti_digits(q, re);
        q = yti_find(p, re, "\"detailedMetadataSnippets\":[{\"snippetText\":{\"runs\":[");
        if (q) {
            const char *lim = yti_find(q, re, "\"snippetHoverText\""); if (!lim || lim > q + 4000) lim = q + 4000 < re ? q + 4000 : re;
            k = 0;
            while ((q = yti_find(q, lim, "{\"text\":\"")) != 0) { q = yti_str(q, lim, o->desc, sizeof o->desc, &k, 0); if (!q) break; }
            o->desc[k < sizeof o->desc ? k : sizeof o->desc - 1] = 0;
        }
        n++;
    }
    return n;
}

struct yti_player {
    int playable;                       /* 1 = "OK" */
    char status[40], reason[200];
    char url[YTI_URL_MAX];              /* formato 18, o "" si YouTube no lo ha dado */
    char title[128], author[52];
    uint32_t dur, views;
};
/* 1 si url es https://<algo>.googlevideo.com/... (lo unico que se descarga como video) */
static inline int yti_media_url_ok(const char *u)
{
    if (!yt_starts(u, "https://")) return 0;
    const char *h = u + 8; uint32_t n = 0;
    while (h[n] && h[n] != '/' && h[n] != ':' && h[n] != '?' && h[n] != '@') n++;
    static const char suf[] = ".googlevideo.com";
    if (h[n] != '/' || n <= sizeof suf - 1) return 0;
    for (uint32_t i = 0; i < sizeof suf - 1; ++i) if (h[n - (sizeof suf - 1) + i] != suf[i]) return 0;
    return 1;
}
/* Respuesta del reproductor. desc recibe la descripcion (con saltos de linea). 1 = la respuesta es de YouTube (mira playable y url). */
static inline int yti_parse_player_ex(const char *b, uint32_t len, struct yti_player *o, char *desc, uint32_t dmax, int any_host)
{
    const char *end = b + len, *q; uint32_t k;
    o->playable = 0; o->status[0] = o->reason[0] = o->url[0] = o->title[0] = o->author[0] = 0; o->dur = o->views = 0;
    if (dmax) desc[0] = 0;
    const char *ps = yti_find(b, end, "\"playabilityStatus\":{");
    if (!ps) return 0;
    const char *pe = ps + 3000 < end ? ps + 3000 : end;
    q = yti_find(ps, pe, "\"status\":\""); k = 0;
    if (q) (void)yti_str(q, pe, o->status, sizeof o->status, &k, 0);
    o->playable = yt_seq(o->status, "OK");
    q = yti_find(ps, pe, "\"reason\":\""); k = 0;
    if (q) (void)yti_str(q, pe, o->reason, sizeof o->reason, &k, 0);
    const char *f = yti_find(b, end, "\"formats\":[");
    if (f) {
        const char *fe = yti_find(f, end, "\"adaptiveFormats\""); if (!fe) fe = f + 20000 < end ? f + 20000 : end;
        q = yti_find(f, fe, "\"itag\":18,");
        if (q) {
            const char *lim = q + 3000 < fe ? q + 3000 : fe;
            const char *u = yti_find(q, lim, "\"url\":\"");
            if (u && (!yti_raw(u, fe, o->url, sizeof o->url) || !(yti_media_url_ok(o->url) || (any_host && (yt_starts(o->url, "http://") || yt_starts(o->url, "https://")))))) o->url[0] = 0;
        }
    }
    const char *vd = yti_find(b, end, "\"videoDetails\":{");
    if (vd) {
        const char *ve = vd + 40000 < end ? vd + 40000 : end;
        q = yti_find(vd, ve, "\"title\":\""); k = 0; if (q) (void)yti_str(q, ve, o->title, sizeof o->title, &k, 0);
        q = yti_find(vd, ve, "\"author\":\""); k = 0; if (q) (void)yti_str(q, ve, o->author, sizeof o->author, &k, 0);
        q = yti_find(vd, ve, "\"lengthSeconds\":\""); if (q) o->dur = yti_digits(q, ve);
        q = yti_find(vd, ve, "\"viewCount\":\""); if (q) o->views = yti_digits(q, ve);
        q = yti_find(vd, ve, "\"shortDescription\":\""); k = 0;
        if (q && dmax) { (void)yti_str(q, ve, desc, dmax, &k, 1); desc[k < dmax ? k : dmax - 1] = 0; }
    }
    return 1;
}

static inline int yti_parse_player(const char *b, uint32_t len, struct yti_player *o, char *desc, uint32_t dmax) { return yti_parse_player_ex(b, len, o, desc, dmax, 0); }

/* ---- cuerpos de las peticiones */
static inline void yti_json_esc(char *o, uint32_t max, uint32_t *p, const char *s)
{
    static const char hx[] = "0123456789abcdef";
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { if (*p + 2 < max) { o[(*p)++] = '\\'; o[(*p)++] = (char)c; } }
        else if (c < 32) { if (*p + 6 < max) { o[(*p)++] = '\\'; o[(*p)++] = 'u'; o[(*p)++] = '0'; o[(*p)++] = '0'; o[(*p)++] = hx[c >> 4]; o[(*p)++] = hx[c & 15]; } }
        else if (*p + 1 < max) o[(*p)++] = (char)c;
    }
    if (max) o[*p < max ? *p : max - 1] = 0;
}
static inline uint32_t yti_body_search(char *o, uint32_t max, const char *query)
{
    uint32_t p = 0;
    yt_put(o, max, &p, "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"" YTI_WEB_VERSION "\",\"hl\":\"es\",\"gl\":\"ES\",\"timeZone\":\"UTC\",\"utcOffsetMinutes\":0}},\"query\":\"");
    yti_json_esc(o, max, &p, query);
    yt_put(o, max, &p, "\"}");
    return p;
}
static inline uint32_t yti_body_player(char *o, uint32_t max, const char *id)
{
    uint32_t p = 0;
    yt_put(o, max, &p, "{\"context\":{\"client\":{\"clientName\":\"ANDROID\",\"clientVersion\":\"" YTI_AND_VERSION "\",\"androidSdkVersion\":30,\"osName\":\"Android\",\"osVersion\":\"11\","
                       "\"userAgent\":\"" YTI_AND_UA "\",\"hl\":\"es\",\"gl\":\"ES\",\"timeZone\":\"UTC\",\"utcOffsetMinutes\":0}},\"videoId\":\"");
    yti_json_esc(o, max, &p, id);
    yt_put(o, max, &p, "\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\"}}}");
    return p;
}
static inline void yti_thumb_url(char *o, uint32_t max, const char *id)
{
    uint32_t p = 0;
    yt_put(o, max, &p, "https://i.ytimg.com/vi/"); yt_put(o, max, &p, id); yt_put(o, max, &p, "/mqdefault.jpg");
}

/* ================================================================== cliente HTTP propio (src/apps/yt_https.c)
 SOMA_252_VIDEO_NATIVO: cliente HTTP/1.1 minimo sobre TCP o TLS 1.3 (el TLS del sistema, con el certificado verificado)
 * para la aplicacion YouTube nativa. A diferencia de wn_fetch (web_net.c) lee el cuerpo segun llega (un video de decenas
 * de megas no cabe en memoria), deja elegir User-Agent y devuelve el tamano total de Content-Range. */

struct tun;
struct yh {
    int h; struct tun *tn; int tls_on;      /* SOMA_326_TUNEL_TLS: la conexion es un tunel (src/apps/tun_tls.c); h >= 0 = abierta */
    uint8_t *rb; uint32_t rn, rp;           /* bufer de lectura */
    int chunked, done; uint64_t left;        /* cuerpo: por trozos / bytes que quedan (~0 = hasta que cierren) */
    uint32_t chunk_left; int chunk_state;
};
/* Abre url ("http://" o "https://"), envia la peticion y lee las cabeceras (sigue hasta 4 redirecciones).
 * range_from >= 0 anade "Range: bytes=<from>-". headers = lineas "Nombre: valor\r\n" (User-Agent incluido) o 0.
 * Devuelve el estado HTTP (200, 206, 403...) o < 0 si no se pudo (err explica). *total = tamano del recurso si se sabe, o 0. */
int  yh_open(struct yh *c, const char *url, const char *method, const char *headers, const char *body, uint32_t blen,
             int64_t range_from, uint64_t *total, uint32_t timeout_ms, char *err, uint32_t emax);
/* Cuerpo: > 0 bytes leidos, 0 = nada aun (timeout_ms agotado), -1 = fin, -2 = error. */
int  yh_read(struct yh *c, uint8_t *buf, uint32_t max, uint32_t timeout_ms);
void yh_close(struct yh *c, int abort_now);
/* Peticion entera en memoria (respuestas JSON). Devuelve el estado HTTP o < 0; *len = bytes del cuerpo (truncado a cap - 1, con 0 final).
 * Si stop != 0 y *stop deja de valer stop_val, se abandona. */
int  yh_fetch(const char *url, const char *method, const char *headers, const char *body, uint32_t blen, uint8_t *out, uint32_t cap, uint32_t *len,
              uint32_t timeout_ms, const volatile uint32_t *stop, uint32_t stop_val, char *err, uint32_t emax);

/* --- puro (se prueba en el anfitrion) */
struct yh_url { int tls; char host[128]; uint16_t port; const char *path; };
int yh_parse_url(const char *url, struct yh_url *u);                        /* 1 = bien */
struct yh_head { int status; int chunked; uint64_t clen; int has_clen; uint64_t range_total; char location[1400]; };
int yh_parse_head(const char *b, uint32_t n, struct yh_head *h);            /* b = cabeceras completas (hasta la linea vacia). 1 = bien */
#endif
