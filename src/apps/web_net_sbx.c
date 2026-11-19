/* PARCHE 142: descarga HTTP para el KERNEL (comando 'descargar') a traves del servicio aislado netsvc (ring 3 con la capacidad de red):
 * el kernel no interpreta respuestas HTTP. Misma estructura de peticion y respuesta que wn_fetch; se validan tamanos y codigos. */
#include "web_net.h"
#if defined(__x86_64__)
#include "sandbox.h"

static uint32_t sl(const char *s) { uint32_t n = 0; if (s) while (s[n]) n++; return n; }
static void cpy(char *d, uint32_t max, const uint8_t *s, uint32_t avail)
{
    uint32_t i = 0; while (i + 1 < max && i < avail && s[i]) { d[i] = (char)s[i]; i++; }
    d[i] = 0;
}

int wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs)
{
    uint8_t *z = (uint8_t *)rs; for (uint32_t i = 0; i < sizeof *rs; ++i) z[i] = 0;
    uint32_t ul = sl(rq->url), ml = sl(rq->method), cl = sl(rq->ctype), bl = rq->body ? rq->body_len : 0;
    if (ul == 0 || ul > 4096 || ml > 16 || cl > 128) { cpy(rs->err, sizeof rs->err, (const uint8_t *)"peticion no valida", 64); return -1; }
    uint32_t n = (uint32_t)sizeof(struct sbx_net_req) + ul + ml + cl + bl;
    uint8_t *buf = (uint8_t *)kmalloc(n);
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!buf || !c) { if (buf) kfree(buf); if (c) kfree(c); cpy(rs->err, sizeof rs->err, (const uint8_t *)"sin memoria", 64); return -1; }
    struct sbx_net_req *q = (struct sbx_net_req *)buf;
    q->url_len = ul; q->method_len = ml; q->ctype_len = cl; q->body_len = bl;
    q->timeout_ms = rq->timeout_ms ? rq->timeout_ms : 15000; if (q->timeout_ms > 60000) q->timeout_ms = 60000;
    q->max_body = rq->max_body ? rq->max_body : (32u << 20); if (q->max_body > (128u << 20)) q->max_body = 128u << 20;
    q->flags = (rq->want_binary ? 1u : 0u) | (rq->no_redirect ? 2u : 0u) | (rq->via_gateway ? 4u : 0u);
    uint8_t *p = (uint8_t *)(q + 1);
    for (uint32_t i = 0; i < ul; ++i) *p++ = (uint8_t)rq->url[i];
    for (uint32_t i = 0; i < ml; ++i) *p++ = (uint8_t)rq->method[i];
    for (uint32_t i = 0; i < cl; ++i) *p++ = (uint8_t)rq->ctype[i];
    for (uint32_t i = 0; i < bl; ++i) *p++ = (uint8_t)rq->body[i];
    sbx_prepare(c, SBX_NET_FETCH, buf, n, 0);
    int r = sbx_svc_call(SBX_SVC_NET, c, q->timeout_ms * 3u + 5000u), ret = -1;
    if (r == 0) {
        const uint8_t *s = c->out.inline_data; uint32_t avail = c->out.inline_len, k = 0;
        const uint8_t *fld[4]; for (int f = 0; f < 4; ++f) { fld[f] = s + (k < avail ? k : avail); while (k < avail && s[k]) k++; k++; }
        cpy(rs->ctype, sizeof rs->ctype, fld[0], avail); cpy(rs->charset, sizeof rs->charset, fld[1], avail);
        cpy(rs->final_url, sizeof rs->final_url, fld[2], avail); cpy(rs->err, sizeof rs->err, fld[3], avail);
        if (c->out.status == 0 && c->out.val[0] >= 100 && c->out.val[0] <= 999 && c->out.data_len <= q->max_body) {
            rs->status = c->out.val[0]; rs->redirects = c->out.val[1] < 0 ? 0 : c->out.val[1]; rs->via_gateway = c->out.val[2] ? 1 : 0;
            rs->body = (char *)kmalloc((uint32_t)c->out.data_len + 1);
            if (rs->body && sbx_copy_data(c, rs->body, c->out.data_len) == 0) { rs->len = (uint32_t)c->out.data_len; rs->body[rs->len] = 0; ret = 0; }
            else if (rs->body) { kfree(rs->body); rs->body = 0; }
        }
    } else cpy(rs->err, sizeof rs->err, (const uint8_t *)(r == SBX_E_DIED ? "el cliente HTTP aislado fallo" : "servicio de red no disponible"), 64);
    sbx_release(c); kfree(c); kfree(buf);
    return ret;
}
void wn_free_isolated(struct wn_resp *rs) { if (rs && rs->body) { kfree(rs->body); rs->body = 0; rs->len = 0; } }
#endif
