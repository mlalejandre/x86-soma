/* PARCHE 142: servicio aislado del cliente HTTP (web_net.c: URL, cabeceras, chunked, redirecciones, gzip, cookies de la peticion) para el
 * kernel (comando 'descargar', via src/apps/web_net_sbx.c). Ring 3 con la capacidad de red y nada mas: sin ficheros, sin pantalla.
 * Una respuesta HTTP hostil solo puede afectar a este proceso. La descompresion gzip se hace aqui mismo (web_inflate.c).
 * SOMA_316_RED_AISLADA: y el TLS 1.3 (tls13.c, x509.c), como el navegador aislado: el apreton de manos y los certificados de
 * cualquier servidor se analizan aqui, no en el nucleo. El nucleo manda con cada peticion el azar, la hora, si hay que verificar
 * y las autoridades del usuario; las de fabrica van en este programa (tls_roots.c). Sin estado entre peticiones: el monton se
 * vacia en cada una y las cookies no se guardan.
 * SOMA_325_LLM_AISLADO: y las cabeceras propias del cliente del LLM (la clave de la API). Cada descarga va en un proceso nuevo que
 * termina con ella (parche 316): la clave no se queda en ningun proceso. */
#include "sbxrt.h"
#include "../apps/web_net.h"
#include "../core/tls13.h"
#include "../core/x509.h"
#include "../core/tlscrypto.h"
#include "../core/tls_roots.h"

uint64_t web_now_ms(void);
uint64_t timer_get_uptime_ms(void);
uint64_t web_now_ms(void) { return timer_get_uptime_ms(); }
void web_yield(void) { }

/* ---- SOMA_316_RED_AISLADA: lo que web_net.c y tls13.c piden al sistema */
int netsvc_https_directo;                                     /* la red es real: https con TLS propio (no hay pasarela) */
static uint8_t s_semilla[32]; static uint64_t s_ctr;
void rng_bytes(void *out, size_t n)                           /* SHA-256 en modo contador sobre la semilla de la peticion */
{
    uint8_t *o = (uint8_t *)out, b[40], h[32];
    while (n) { memcpy(b, s_semilla, 32); memcpy(b + 32, &s_ctr, 8); s_ctr++; sha256(b, 40, h); size_t c = n < 32 ? n : 32; memcpy(o, h, c); o += c; n -= c; }
}
static uint64_t s_unix0, s_up0;
static uint64_t unix_now(void) { return s_unix0 ? s_unix0 + (timer_get_uptime_ms() - s_up0) / 1000u : 0; }

/* copia n bytes de s y termina; nunca pasa de cap - 1 */
static void corta(char *d, uint32_t cap, const char *s, uint32_t n)
{
    if (n > cap - 1u) n = cap - 1u;
    for (uint32_t i = 0; i < n; ++i) d[i] = s[i];
    d[n] = 0;
}

static void put(struct sbx_out *out, uint32_t *pos, const char *s)
{
    while (s && *s && *pos < SBX_OUT_INLINE - 2) out->inline_data[(*pos)++] = (uint8_t)*s++;
    out->inline_data[(*pos)++] = 0;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    if (in->op != SBX_NET_FETCH || in->len < sizeof(struct sbx_net_req)) { out->status = -1; return 0; }
    const struct sbx_net_req *q = (const struct sbx_net_req *)data;
    uint64_t need = sizeof *q + (uint64_t)q->url_len + q->method_len + q->ctype_len + q->body_len + q->accept_len + q->anclas_len + q->hdr_len;
    if (need > in->len || q->url_len == 0 || q->url_len > 4096 || q->method_len > 16 || q->ctype_len > 128
        || q->accept_len > SBX_NET_ACCEPT_MAX || q->anclas_len > SBX_NET_ANCLAS_MAX || q->hdr_len > SBX_NET_HDR_MAX) { out->status = -1; return 0; }
    const char *p = (const char *)(q + 1);
    char *url = (char *)kmalloc(q->url_len + 1), method[17], ctype[129], accept[SBX_NET_ACCEPT_MAX + 1];
    if (!url) { out->status = -1; return 0; }
    memcpy(url, p, q->url_len); url[q->url_len] = 0; p += q->url_len;
    corta(method, sizeof method, p, q->method_len); p += q->method_len;
    corta(ctype, sizeof ctype, p, q->ctype_len); p += q->ctype_len;
    const char *body = p; p += q->body_len;
    corta(accept, sizeof accept, p, q->accept_len); p += q->accept_len;
    /* SOMA_316_RED_AISLADA: azar, hora, politica y autoridades de esta peticion */
    memcpy(s_semilla, q->semilla, sizeof s_semilla); s_ctr = 0;
    s_unix0 = q->unix_time; s_up0 = timer_get_uptime_ms();
    netsvc_https_directo = (q->flags & SBX_NET_F_REAL) ? 1 : 0;
    tls13_verify = (q->flags & SBX_NET_F_VERIFICA) ? 1 : 0;
    tls13_unix_time = unix_now;
    x509_set_roots(tls_roots_blob, tls_roots_len);
    x509_clear_anchors();
    if (q->anclas_len) (void)x509_add_anchors_pem(p, q->anclas_len);
    p += q->anclas_len;
    char *hdr = 0;                                                /* SOMA_325_LLM_AISLADO: cabeceras propias (la clave del LLM) */
    if (q->hdr_len) { hdr = (char *)kmalloc(q->hdr_len + 1u); if (!hdr) { out->status = -1; return 0; } corta(hdr, q->hdr_len + 1u, p, q->hdr_len); }
    struct wn_req rq; memset(&rq, 0, sizeof rq);
    rq.url = url; rq.method = q->method_len ? method : 0; rq.ctype = q->ctype_len ? ctype : 0;
    rq.body = q->body_len ? body : 0; rq.body_len = q->body_len;
    rq.accept = q->accept_len ? accept : 0;
    rq.timeout_ms = q->timeout_ms; rq.max_body = q->max_body;
    rq.want_binary = q->flags & 1; rq.no_redirect = (q->flags >> 1) & 1; rq.via_gateway = (q->flags >> 2) & 1;
    rq.direct = (q->flags & SBX_NET_F_DIRECT) ? 1 : 0;
    rq.extra_headers = hdr;
    struct wn_resp rs; memset(&rs, 0, sizeof rs);
    int r = wn_fetch(&rq, &rs);
    if (r == -14 && wn_tls_last_error()[0]) {                    /* SOMA_316_RED_AISLADA: el motivo del fallo de TLS, para el usuario */
        static const char pre[] = "TLS: ";
        uint32_t k = 0; const char *e = wn_tls_last_error();
        for (const char *s = pre; *s && k + 1 < sizeof rs.err; ) rs.err[k++] = *s++;
        while (*e && k + 1 < sizeof rs.err) rs.err[k++] = *e++;
        rs.err[k] = 0;
    }
    uint32_t pos = 0;
    put(out, &pos, rs.ctype); put(out, &pos, rs.charset); put(out, &pos, rs.final_url); put(out, &pos, rs.err);
    out->inline_len = pos;
    out->status = r == 0 ? 0 : -1;
    out->val[0] = rs.status; out->val[1] = rs.redirects; out->val[2] = rs.via_gateway;
    if (r == 0 && rs.body && rs.len) { out->data_va = (uint64_t)(uintptr_t)rs.body; out->data_len = rs.len; }
    return 0;
}
