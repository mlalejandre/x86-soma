/* PARCHE 142: descarga HTTP para el KERNEL (comando 'descargar') a traves del servicio aislado netsvc (ring 3 con la capacidad de red):
 * el kernel no interpreta respuestas HTTP. Misma estructura de peticion y respuesta que wn_fetch; se validan tamanos y codigos.
 * SOMA_316_RED_AISLADA: tambien TLS (netsvc lleva tls13.c y x509.c) y las peticiones del agente (investiga y search): se pasan
 * Accept y direct, y con cada peticion el azar, la hora, si se verifican los certificados y las autoridades del usuario.
 * Cada descarga va en un proceso netsvc NUEVO que termina con ella (no el servicio persistente): si una respuesta hostil llegara a
 * tomar el proceso, no veria ni podria alterar las descargas siguientes. Cuesta unos milisegundos, nada al lado de la red.
 * SOMA_317_DESCARGAS_AISLADAS: rq->cancel tambien vale aqui: si se pone a 1, el proceso se para y la descarga devuelve -13
 * ("cancelada"), como wn_fetch.
 * SOMA_325_LLM_AISLADO: y rq->extra_headers (la clave del LLM). El bufer de la peticion se borra antes de liberarlo. */
#include "web_net.h"
#if defined(__x86_64__) && !defined(WEB_HOST_TEST) && !defined(SOMA_HOST_TEST)
#include "sandbox.h"
#include "tls13.h"
#include "tls_trust.h"
#include "rng.h"
#include "fs.h"
#include "usermode.h"
extern const uint8_t _binary_netsvc_elf_start[], _binary_netsvc_elf_end[];

static uint32_t sl(const char *s) { uint32_t n = 0; if (s) while (s[n]) n++; return n; }
/* SOMA_325_LLM_AISLADO: la peticion puede llevar la clave del LLM: se borra antes de devolverla al monton */
static void borra_y_libera(uint8_t *b, uint32_t n) { volatile uint8_t *v = b; for (uint32_t i = 0; i < n; ++i) v[i] = 0; kfree(b); }
static void cpy(char *d, uint32_t max, const uint8_t *s, uint32_t avail)
{
    uint32_t i = 0; while (i + 1 < max && i < avail && s[i]) { d[i] = (char)s[i]; i++; }
    d[i] = 0;
}

int wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs)
{
    uint8_t *z = (uint8_t *)rs; for (uint32_t i = 0; i < sizeof *rs; ++i) z[i] = 0;
    uint32_t ul = sl(rq->url), ml = sl(rq->method), cl = sl(rq->ctype), bl = rq->body ? rq->body_len : 0, al = sl(rq->accept), hl = sl(rq->extra_headers);
    if (ul == 0 || ul > 4096 || ml > 16 || cl > 128 || al > SBX_NET_ACCEPT_MAX || bl > SBX_IN_MAX / 2u || hl > SBX_NET_HDR_MAX
        || rq->referer) {                                               /* SOMA_316: lo que netsvc no sabe hacer no se pide aqui */
        cpy(rs->err, sizeof rs->err, (const uint8_t *)"peticion no valida", 64); return -1;
    }
    if (rq->cancel && *rq->cancel) { cpy(rs->err, sizeof rs->err, (const uint8_t *)"cancelada", 64); return -13; }   /* SOMA_317 */
    /* SOMA_316_RED_AISLADA: la confianza del TLS del nucleo, para netsvc. Las autoridades del usuario se leen en un bufer propio */
    tls_trust_setup();
    uint32_t pl = 0;
    char *pem = vfs_read_alloc("/etc/tls_confianza.pem", &pl);
    if (!pem) pl = 0;
    if (pl > FS_DATA_MAX) pl = FS_DATA_MAX;                              /* como tls_trust.c */
    uint32_t n = (uint32_t)sizeof(struct sbx_net_req) + ul + ml + cl + bl + al + pl + hl;
    uint8_t *buf = (uint8_t *)kmalloc(n);
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!buf || !c) { if (buf) kfree(buf); if (c) kfree(c); if (pem) kfree(pem); cpy(rs->err, sizeof rs->err, (const uint8_t *)"sin memoria", 64); return -1; }
    struct sbx_net_req *q = (struct sbx_net_req *)buf;
    for (uint32_t i = 0; i < (uint32_t)sizeof *q; ++i) buf[i] = 0;
    q->url_len = ul; q->method_len = ml; q->ctype_len = cl; q->body_len = bl;
    q->accept_len = al; q->anclas_len = pl; q->hdr_len = hl;
    q->unix_time = tls13_unix_time ? tls13_unix_time() : 0;
    rng_bytes(q->semilla, sizeof q->semilla);
    q->timeout_ms = rq->timeout_ms ? rq->timeout_ms : 15000; if (q->timeout_ms > 600000) q->timeout_ms = 600000;   /* SOMA_327: hasta 10 min (un modelo local tarda en responder) */
    q->max_body = rq->max_body ? rq->max_body : (32u << 20); if (q->max_body > (128u << 20)) q->max_body = 128u << 20;
    q->flags = (rq->want_binary ? 1u : 0u) | (rq->no_redirect ? 2u : 0u) | (rq->via_gateway ? 4u : 0u)
             | (rq->direct ? SBX_NET_F_DIRECT : 0u) | (wn_https_directo() ? SBX_NET_F_REAL : 0u) | (tls13_verify ? SBX_NET_F_VERIFICA : 0u);
    uint8_t *p = (uint8_t *)(q + 1);
    for (uint32_t i = 0; i < ul; ++i) *p++ = (uint8_t)rq->url[i];
    for (uint32_t i = 0; i < ml; ++i) *p++ = (uint8_t)rq->method[i];
    for (uint32_t i = 0; i < cl; ++i) *p++ = (uint8_t)rq->ctype[i];
    for (uint32_t i = 0; i < bl; ++i) *p++ = (uint8_t)rq->body[i];
    for (uint32_t i = 0; i < al; ++i) *p++ = (uint8_t)rq->accept[i];
    for (uint32_t i = 0; i < pl; ++i) *p++ = (uint8_t)pem[i];
    if (pem) kfree(pem);
    for (uint32_t i = 0; i < hl; ++i) *p++ = (uint8_t)rq->extra_headers[i];      /* SOMA_325_LLM_AISLADO */
    sbx_prepare(c, SBX_NET_FETCH, buf, n, SBX_HEAP_MAX);
    const uint32_t caps = USER_SYS_NET_MASK | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS);          /* lo mismo que el servicio netsvc */
    int r = sbx_run_caps_cancel(c, _binary_netsvc_elf_start, (size_t)(_binary_netsvc_elf_end - _binary_netsvc_elf_start), q->timeout_ms * 3u + 5000u, caps, rq->cancel), ret = -1;
    if (rq->cancel && *rq->cancel) {                                   /* SOMA_317: cancelada (aunque llegara a terminar) */
        sbx_release(c); kfree(c); borra_y_libera(buf, n);
        cpy(rs->err, sizeof rs->err, (const uint8_t *)"cancelada", 64);
        return -13;
    }
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
    sbx_release(c); kfree(c); borra_y_libera(buf, n);
    return ret;
}
void wn_free_isolated(struct wn_resp *rs) { if (rs && rs->body) { kfree(rs->body); rs->body = 0; rs->len = 0; } }
/* SOMA_327_RED_FUERA: GET (body = 0) o POST JSON por netsvc con el contrato del cliente minimo (http_get / http_post_json): cuerpo
 * en buf (con NUL, recortado a cap - 1) y r->body apuntando a el. Devuelve el estado HTTP o < 0 sin respuesta. */
#include "http.h"
int wn_http_simple(const char *url, const char *body, char *buf, uint32_t cap, struct http_response *r, uint32_t ms, const volatile int *cancel)
{
    r->body = 0; r->body_len = 0; r->status_code = 0;
    if (!buf || cap < 2) return -1;
    buf[0] = 0;
    struct wn_req rq; memset(&rq, 0, sizeof rq);
    struct wn_resp *rs = (struct wn_resp *)kmalloc(sizeof *rs);
    if (!rs) return -1;
    memset(rs, 0, sizeof *rs);
    rq.url = url; rq.method = body ? "POST" : 0; rq.body = body; rq.body_len = sl(body); rq.ctype = body ? "application/json" : 0;
    rq.timeout_ms = ms; rq.max_body = 32u << 20; rq.no_redirect = 1; rq.direct = 1; rq.cancel = cancel;
    int st = -1;
    if (wn_fetch_isolated(&rq, rs) == 0) {
        uint32_t n = rs->len < cap - 1 ? rs->len : cap - 1;
        if (rs->body && n) memcpy(buf, rs->body, n);
        buf[n] = 0;
        st = rs->status; r->body = buf; r->body_len = n; r->status_code = st;
    }
    wn_free_isolated(rs); kfree(rs);
    return st;
}
#else   /* SOMA_316_RED_AISLADA: aarch64 (sin EL0) y pruebas del anfitrion: el cliente de siempre */
int wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs) { return wn_fetch(rq, rs); }
void wn_free_isolated(struct wn_resp *rs) { wn_free(rs); }
#endif
