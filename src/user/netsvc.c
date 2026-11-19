/* PARCHE 142: servicio aislado del cliente HTTP (web_net.c: URL, cabeceras, chunked, redirecciones, gzip, cookies de la peticion) para el
 * kernel (comando 'descargar', via src/apps/web_net_sbx.c). Ring 3 con la capacidad de red y nada mas: sin ficheros, sin pantalla.
 * Una respuesta HTTP hostil solo puede afectar a este proceso. La descompresion gzip se hace aqui mismo (web_inflate.c). */
#include "sbxrt.h"
#include "../apps/web_net.h"

uint64_t web_now_ms(void);
uint64_t timer_get_uptime_ms(void);
uint64_t web_now_ms(void) { return timer_get_uptime_ms(); }
void web_yield(void) { }

static void put(struct sbx_out *out, uint32_t *pos, const char *s)
{
    while (s && *s && *pos < SBX_OUT_INLINE - 2) out->inline_data[(*pos)++] = (uint8_t)*s++;
    out->inline_data[(*pos)++] = 0;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    if (in->op != SBX_NET_FETCH || in->len < sizeof(struct sbx_net_req)) { out->status = -1; return 0; }
    const struct sbx_net_req *q = (const struct sbx_net_req *)data;
    uint64_t need = sizeof *q + (uint64_t)q->url_len + q->method_len + q->ctype_len + q->body_len;
    if (need > in->len || q->url_len == 0 || q->url_len > 4096 || q->method_len > 16 || q->ctype_len > 128) { out->status = -1; return 0; }
    const char *p = (const char *)(q + 1);
    char *url = (char *)kmalloc(q->url_len + 1), method[17], ctype[129];
    if (!url) { out->status = -1; return 0; }
    memcpy(url, p, q->url_len); url[q->url_len] = 0; p += q->url_len;
    memcpy(method, p, q->method_len); method[q->method_len] = 0; p += q->method_len;
    memcpy(ctype, p, q->ctype_len); ctype[q->ctype_len] = 0; p += q->ctype_len;
    struct wn_req rq; memset(&rq, 0, sizeof rq);
    rq.url = url; rq.method = q->method_len ? method : 0; rq.ctype = q->ctype_len ? ctype : 0;
    rq.body = q->body_len ? p : 0; rq.body_len = q->body_len;
    rq.timeout_ms = q->timeout_ms; rq.max_body = q->max_body;
    rq.want_binary = q->flags & 1; rq.no_redirect = (q->flags >> 1) & 1; rq.via_gateway = (q->flags >> 2) & 1;
    struct wn_resp rs; memset(&rs, 0, sizeof rs);
    int r = wn_fetch(&rq, &rs);
    uint32_t pos = 0;
    put(out, &pos, rs.ctype); put(out, &pos, rs.charset); put(out, &pos, rs.final_url); put(out, &pos, rs.err);
    out->inline_len = pos;
    out->status = r == 0 ? 0 : -1;
    out->val[0] = rs.status; out->val[1] = rs.redirects; out->val[2] = rs.via_gateway;
    if (r == 0 && rs.body && rs.len) { out->data_va = (uint64_t)(uintptr_t)rs.body; out->data_len = rs.len; }
    return 0;
}
