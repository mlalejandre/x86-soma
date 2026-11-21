#ifndef SOMA_YT_PROTO_H
#define SOMA_YT_PROTO_H
/* SOMA_250_YOUTUBE: la parte pura de la aplicacion YouTube (sin nucleo, sin pantalla y sin red: se prueba en el anfitrion con
 * tests/test_youtube_250.c). Todo static inline: lo incluye solo src/apps/youtube.c.
 *
 *  - protocolo con el servicio del anfitrion (tools/yt_service.py): lista de resultados, cabeceras HTTP, direcciones;
 *  - configuracion (/etc/youtube.conf);
 *  - yt_ring: la memoria del video que va llegando. Es una VENTANA sobre el flujo: guarda desde un poco antes de lo que el
 *    reproductor esta leyendo hasta lo ultimo recibido, y suelta lo ya visto para hacer sitio. Toda entrada se valida:
 *    ninguna longitud que venga de la red se usa sin acotar. */
#include <stdint.h>
#include <stddef.h>

#define YT_MAX_ITEMS   16
#define YT_ID_LEN      11
#define YT_PORT        8097

struct yt_item {
    char id[YT_ID_LEN + 1];
    uint32_t dur;                   /* segundos (0 = directo o desconocida) */
    char title[128];
    char channel[52];
    uint32_t views;
};

static inline uint32_t yt_slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static inline void yt_scopy(char *d, uint32_t max, const char *s) { uint32_t i = 0; if (!max) return; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static inline int yt_seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline int yt_starts(const char *s, const char *pre) { while (*pre) if (*s++ != *pre++) return 0; return 1; }
static inline void yt_put(char *o, uint32_t max, uint32_t *p, const char *s) { if (!max) return; while (s && *s && *p + 1 < max) o[(*p)++] = *s++; o[*p] = 0; }
static inline void yt_put_dec(char *o, uint32_t max, uint32_t *p, uint64_t v)
{
    char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    yt_put(o, max, p, b + i);
}

/* Identificador de video: exactamente 11 caracteres de [A-Za-z0-9_-]. */
static inline int yt_id_ok(const char *id)
{
    for (int i = 0; i < YT_ID_LEN; ++i) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return id[YT_ID_LEN] == 0;
}

/* Codifica un texto para la parte de consulta de una URL. Devuelve la longitud; lo que no cabe se corta en un caracter entero. */
static inline uint32_t yt_urlenc(const char *in, char *out, uint32_t max)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t o = 0;
    if (!max) return 0;
    while (in && *in) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            if (o + 2 > max) break;
            out[o++] = (char)c;
        } else {
            if (o + 4 > max) break;
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
    return o;
}

static inline uint32_t yt_num(const char *s, uint32_t n, uint32_t *used)
{
    uint64_t v = 0; uint32_t i = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (uint64_t)(s[i] - '0'); if (v > 0xFFFFFFFFu) v = 0xFFFFFFFFu; i++; }
    if (used) *used = i;
    return (uint32_t)v;
}

/* Respuesta de /yt/search:  "SOMAYT 1 <n>\n"  y luego una linea por video:  id \t segundos \t titulo \t canal \t vistas
 * Devuelve cuantos videos validos ha leido (hasta max), o -1 si no es una respuesta del servicio. */
static inline int yt_parse_results(const char *b, uint32_t len, struct yt_item *out, int max)
{
    if (len < 9 || !(b[0] == 'S' && b[1] == 'O' && b[2] == 'M' && b[3] == 'A' && b[4] == 'Y' && b[5] == 'T' && b[6] == ' ')) return -1;
    uint32_t p = 0;
    while (p < len && b[p] != '\n') p++;
    if (p >= len) return 0;
    p++;
    int n = 0;
    while (p < len && n < max) {
        uint32_t e = p;
        while (e < len && b[e] != '\n') e++;
        const char *f[5]; uint32_t fl[5]; int nf = 0; uint32_t s = p;
        for (uint32_t i = p; i <= e && nf < 5; ++i) {
            if (i == e || b[i] == '\t') { f[nf] = b + s; fl[nf] = i - s; nf++; s = i + 1; }
        }
        if (nf >= 3 && fl[0] == YT_ID_LEN) {
            struct yt_item *it = &out[n];
            for (int i = 0; i < YT_ID_LEN; ++i) it->id[i] = f[0][i];
            it->id[YT_ID_LEN] = 0;
            if (yt_id_ok(it->id)) {
                it->dur = yt_num(f[1], fl[1], 0);
                uint32_t k = 0;
                for (uint32_t i = 0; i < fl[2] && k + 1 < sizeof it->title; ++i) { char c = f[2][i]; it->title[k++] = (unsigned char)c < 32 ? ' ' : c; }
                it->title[k] = 0; k = 0;
                if (nf > 3) for (uint32_t i = 0; i < fl[3] && k + 1 < sizeof it->channel; ++i) { char c = f[3][i]; it->channel[k++] = (unsigned char)c < 32 ? ' ' : c; }
                it->channel[k] = 0;
                it->views = nf > 4 ? yt_num(f[4], fl[4], 0) : 0;
                if (!it->title[0]) yt_scopy(it->title, sizeof it->title, "(sin titulo)");
                n++;
            }
        }
        p = e + 1;
    }
    return n;
}

/* Cabeceras de una respuesta HTTP: 1 si estan completas (estado y desplazamiento del cuerpo), 0 si faltan bytes, -1 si no es HTTP. */
static inline int yt_http_head(const char *b, uint32_t len, int *status, uint32_t *body_off)
{
    if (len >= 5 && !(b[0] == 'H' && b[1] == 'T' && b[2] == 'T' && b[3] == 'P' && b[4] == '/')) return -1;
    for (uint32_t i = 0; i + 3 < len; ++i) {
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
            uint32_t p = 5;
            while (p < i && b[p] != ' ') p++;
            while (p < i && b[p] == ' ') p++;
            int code = 0, d = 0;
            while (p < i && d < 3 && b[p] >= '0' && b[p] <= '9') { code = code * 10 + (b[p] - '0'); p++; d++; }
            if (d != 3) return -1;
            *status = code; *body_off = i + 4;
            return 1;
        }
    }
    return 0;
}

/* "a.b.c.d" -> 1 y la direccion; cualquier otra cosa -> 0 (sera un nombre). */
static inline int yt_parse_ip(const char *s, uint8_t ip[4])
{
    int part = 0; uint32_t v = 0; int digits = 0;
    for (;; ++s) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); if (v > 255 || ++digits > 3) return 0; }
        else if (*s == '.' || *s == 0) {
            if (!digits || part > 3) return 0;
            ip[part++] = (uint8_t)v; v = 0; digits = 0;
            if (*s == 0) break;
        } else return 0;
    }
    return part == 4;
}

/* "equipo[:puerto]" -> equipo y puerto (YT_PORT si no se da). 1 = valido. Solo letras, cifras, punto y guion en el nombre. */
static inline int yt_host_port(const char *s, char *host, uint32_t hmax, uint16_t *port)
{
    uint32_t n = 0;
    while (*s == ' ') s++;
    while (*s && *s != ':' && *s != ' ') {
        char c = *s++;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0;
        if (n + 1 >= hmax) return 0;
        host[n++] = c;
    }
    host[n] = 0;
    if (!n) return 0;
    *port = YT_PORT;
    if (*s == ':') {
        uint32_t used = 0, v = yt_num(s + 1, 6, &used);
        if (!used || v == 0 || v > 65535) return 0;
        s += 1 + used;
        *port = (uint16_t)v;
    }
    while (*s == ' ') s++;
    return *s == 0;
}

/* ---------------------------------------------------------------- configuracion: /etc/youtube.conf (clave=valor por linea) */
struct yt_conf {
    char host[64];                  /* "" = el anfitrion de QEMU (la puerta de enlace) */
    uint16_t port;
    char key[48];                   /* clave compartida con el servicio ("" = sin clave) */
    int height;                     /* 240, 360 o 480 */
    int fps;                        /* 10..30 */
};
static inline int yt_height_ok(int h) { return h == 240 || h == 360 || h == 480; }
static inline void yt_conf_default(struct yt_conf *c)
{
    c->host[0] = 0; c->port = YT_PORT; c->key[0] = 0; c->height = 360; c->fps = 15;
}
static inline void yt_conf_parse(const char *t, uint32_t len, struct yt_conf *c)
{
    yt_conf_default(c);
    uint32_t p = 0;
    while (p < len) {
        uint32_t e = p; while (e < len && t[e] != '\n') e++;
        uint32_t q = p; while (q < e && t[q] != '=') q++;
        if (q < e) {
            char k[16], v[80]; uint32_t kn = 0, vn = 0;
            for (uint32_t i = p; i < q && kn + 1 < sizeof k; ++i) if (t[i] != ' ') k[kn++] = t[i];
            for (uint32_t i = q + 1; i < e && vn + 1 < sizeof v; ++i) if (t[i] != '\r') v[vn++] = t[i];
            while (vn && v[vn - 1] == ' ') vn--;
            k[kn] = 0; v[vn] = 0;
            if (yt_seq(k, "servidor")) { char h[64]; uint16_t po; if (yt_host_port(v, h, sizeof h, &po)) { yt_scopy(c->host, sizeof c->host, h); c->port = po; } }
            else if (yt_seq(k, "clave")) yt_scopy(c->key, sizeof c->key, v);
            else if (yt_seq(k, "calidad")) { int h = (int)yt_num(v, vn, 0); if (yt_height_ok(h)) c->height = h; }
            else if (yt_seq(k, "fps")) { int f = (int)yt_num(v, vn, 0); if (f >= 10 && f <= 30) c->fps = f; }
        }
        p = e + 1;
    }
}
static inline uint32_t yt_conf_format(const struct yt_conf *c, char *o, uint32_t max)
{
    uint32_t p = 0;
    if (c->host[0]) { yt_put(o, max, &p, "servidor="); yt_put(o, max, &p, c->host); yt_put(o, max, &p, ":"); yt_put_dec(o, max, &p, c->port); yt_put(o, max, &p, "\n"); }
    if (c->key[0]) { yt_put(o, max, &p, "clave="); yt_put(o, max, &p, c->key); yt_put(o, max, &p, "\n"); }
    yt_put(o, max, &p, "calidad="); yt_put_dec(o, max, &p, (uint64_t)c->height);
    yt_put(o, max, &p, "\nfps="); yt_put_dec(o, max, &p, (uint64_t)c->fps); yt_put(o, max, &p, "\n");
    return p;
}

/* Rutas del servicio. La clave y el texto van codificados; el identificador ya esta validado. */
static inline void yt_path_key(char *o, uint32_t max, uint32_t *p, const struct yt_conf *c)
{
    if (!c->key[0]) return;
    char e[160]; yt_urlenc(c->key, e, sizeof e);
    yt_put(o, max, p, "&k="); yt_put(o, max, p, e);
}
static inline void yt_path_search(char *o, uint32_t max, const struct yt_conf *c, const char *query, int n)
{
    char e[512]; uint32_t p = 0; yt_urlenc(query, e, sizeof e);
    yt_put(o, max, &p, "/yt/search?q="); yt_put(o, max, &p, e); yt_put(o, max, &p, "&n="); yt_put_dec(o, max, &p, (uint64_t)n);
    yt_path_key(o, max, &p, c);
}
static inline void yt_path_thumb(char *o, uint32_t max, const struct yt_conf *c, const char *id)
{
    uint32_t p = 0;
    yt_put(o, max, &p, "/yt/thumb?id="); yt_put(o, max, &p, id);
    yt_path_key(o, max, &p, c);
}
static inline void yt_path_video(char *o, uint32_t max, const struct yt_conf *c, const char *id, uint32_t t)
{
    uint32_t p = 0;
    yt_put(o, max, &p, "/yt/video?id="); yt_put(o, max, &p, id);
    yt_put(o, max, &p, "&t="); yt_put_dec(o, max, &p, t);
    yt_put(o, max, &p, "&h="); yt_put_dec(o, max, &p, (uint64_t)c->height);
    yt_put(o, max, &p, "&fps="); yt_put_dec(o, max, &p, (uint64_t)c->fps);
    yt_path_key(o, max, &p, c);
}

/* "m:ss" o "h:mm:ss" */
static inline void yt_fmt_time(char *o, uint32_t max, uint32_t s)
{
    uint32_t p = 0, h = s / 3600, m = s / 60 % 60; s %= 60;
    char two[3]; two[2] = 0;
    if (h) { yt_put_dec(o, max, &p, h); yt_put(o, max, &p, ":"); two[0] = (char)('0' + m / 10); two[1] = (char)('0' + m % 10); yt_put(o, max, &p, two); }
    else yt_put_dec(o, max, &p, m);
    yt_put(o, max, &p, ":"); two[0] = (char)('0' + s / 10); two[1] = (char)('0' + s % 10); yt_put(o, max, &p, two);
}
/* "1,2 M de visualizaciones", "34 mil visualizaciones", "512 visualizaciones" */
static inline void yt_fmt_views(char *o, uint32_t max, uint32_t v)
{
    uint32_t p = 0;
    if (v >= 1000000u) {
        yt_put_dec(o, max, &p, v / 1000000u);
        if (v < 10000000u) { char d[3] = { ',', (char)('0' + v / 100000u % 10), 0 }; yt_put(o, max, &p, d); }
        yt_put(o, max, &p, " M de visualizaciones");
    } else if (v >= 1000u) { yt_put_dec(o, max, &p, v / 1000u); yt_put(o, max, &p, " mil visualizaciones"); }
    else { yt_put_dec(o, max, &p, v); yt_put(o, max, &p, v == 1 ? " visualizacion" : " visualizaciones"); }
}

/* ---------------------------------------------------------------- yt_ring: ventana en memoria sobre el flujo de video
 * Guarda los bytes [base, end) del flujo en bloques de YT_BLK. Quien descarga anade por el final (yt_ring_put); el reproductor
 * lee donde quiere dentro de la ventana (yt_ring_read). Cuando no queda sitio, yt_ring_trim suelta lo que el reproductor ya
 * ha dejado atras: lo mas antiguo que haya leido en los ultimos segundos, menos un margen (la pausa rebobina un poco el audio).
 * Si el reproductor no lee (pausa), no se suelta nada y la descarga espera: el servicio deja de enviar.
 * Los primeros YT_HDR_KEEP bytes (la cabecera AVI) se guardan aparte y se pueden leer siempre. */
#ifndef YT_BLK
#define YT_BLK        (1u << 20)
#endif
#ifndef YT_HDR_KEEP
#define YT_HDR_KEEP   65536u
#endif
#define YT_MAX_BLK    32
#define YT_NO_READ    (~(uint64_t)0)
#define YT_TRIM_WINDOW_MS 2000u

struct yt_ring {
    uint8_t *blk[YT_MAX_BLK];
    uint32_t nblk;
    uint64_t base, end;             /* bytes del flujo que hay en memoria */
    uint64_t lo_cur, lo_prev;       /* lo mas antiguo leido en la ventana de tiempo actual y en la anterior */
    uint64_t rot_ms;
    uint32_t margin;                /* bytes que se conservan por detras de lo mas antiguo leido */
    uint8_t *hdr; uint32_t hdr_n;   /* copia del principio del flujo */
};

static inline uint64_t yt_ring_cap(const struct yt_ring *r) { return (uint64_t)r->nblk * YT_BLK; }
static inline uint64_t yt_ring_space(const struct yt_ring *r) { return yt_ring_cap(r) - (r->end - r->base); }
static inline void yt_ring_reset(struct yt_ring *r, uint64_t now_ms)
{
    r->base = r->end = 0; r->lo_cur = r->lo_prev = YT_NO_READ; r->rot_ms = now_ms; r->hdr_n = 0;
}
static inline void yt_ring_copy(const struct yt_ring *r, uint64_t off, uint8_t *dst, const uint8_t *src, uint32_t n)
{   /* dst != 0: lee del anillo; src != 0: escribe en el */
    while (n) {
        uint32_t b = (uint32_t)((off / YT_BLK) % r->nblk), o = (uint32_t)(off % YT_BLK), k = YT_BLK - o;
        if (k > n) k = n;
        if (dst) { for (uint32_t i = 0; i < k; ++i) dst[i] = r->blk[b][o + i]; dst += k; }
        else { for (uint32_t i = 0; i < k; ++i) r->blk[b][o + i] = src[i]; src += k; }
        off += k; n -= k;
    }
}
/* Anade n bytes al final. Devuelve los que ha aceptado (los que caben). */
static inline uint32_t yt_ring_put(struct yt_ring *r, const uint8_t *d, uint32_t n)
{
    if (!r->nblk) return 0;
    uint64_t sp = yt_ring_space(r);
    if (n > sp) n = (uint32_t)sp;
    if (r->hdr && r->end < YT_HDR_KEEP) {
        uint32_t k = YT_HDR_KEEP - (uint32_t)r->end; if (k > n) k = n;
        for (uint32_t i = 0; i < k; ++i) r->hdr[(uint32_t)r->end + i] = d[i];
        r->hdr_n = (uint32_t)r->end + k;
    }
    yt_ring_copy(r, r->end, 0, d, n);
    r->end += n;
    return n;
}
/* Lee n bytes desde off: n si estan todos, -1 si no (ya soltados o aun no recibidos). */
static inline int64_t yt_ring_read(struct yt_ring *r, uint64_t off, uint8_t *buf, uint32_t n)
{
    if (off + n < off) return -1;
    if (r->hdr && off + n <= r->hdr_n) { for (uint32_t i = 0; i < n; ++i) buf[i] = r->hdr[(uint32_t)off + i]; }
    else {
        if (!r->nblk || off < r->base || off + n > r->end) return -1;
        yt_ring_copy(r, off, buf, 0, n);
    }
    if (off < r->lo_cur) r->lo_cur = off;
    return (int64_t)n;
}
/* Suelta lo que el reproductor ya ha dejado atras. Devuelve los bytes liberados. */
static inline uint64_t yt_ring_trim(struct yt_ring *r, uint64_t now_ms)
{
    if (now_ms - r->rot_ms >= YT_TRIM_WINDOW_MS) { r->lo_prev = r->lo_cur; r->lo_cur = YT_NO_READ; r->rot_ms = now_ms; }
    uint64_t low = r->lo_prev < r->lo_cur ? r->lo_prev : r->lo_cur;
    if (low == YT_NO_READ || r->lo_prev == YT_NO_READ) return 0;       /* aun no hay dos ventanas de lecturas: no se sabe que sobra */
    uint64_t nb = low > r->margin ? low - r->margin : 0;
    if (nb > r->end) nb = r->end;
    if (nb <= r->base) return 0;
    uint64_t freed = nb - r->base;
    r->base = nb;
    return freed;
}

#endif
