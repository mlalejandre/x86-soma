/* SOMA_353_CACHE_HTTP: cache de subrecursos en memoria (ver web_hcache.h).
 * En el portatil SOMA habla TLS directamente y no habia ninguna cache: cada pagina volvia a bajar todo su CSS, JavaScript,
 * fuentes e imagenes, tambien al volver atras o al abrir otra noticia del mismo sitio. Solo se guardan respuestas que el
 * servidor declara frescas (web_net.c: wn_cache_secs). La navegacion principal (el HTML de la pagina) nunca sale de aqui. */
#include "web_hcache.h"
#include "web_util.h"

#define HC_N 512
#define HC_MAX_ONE (4u << 20)            /* una respuesta de mas de 4 MiB no se guarda */
#define HC_MAX_SECS (7u * 86400u)

struct hc_ent { uint32_t h; uint8_t bin; char *url, *final_url, *body; uint32_t len; char ctype[96], charset[24]; uint64_t exp, used; };
static struct hc_ent hc_tab[HC_N];
static uint32_t hc_used_bytes, hc_n, hc_budget_set;

static uint32_t hc_budget(void)
{
    if (hc_budget_set) return hc_budget_set;
#if defined(SOMA_WEBSVC)
    return web_big_mem() ? (48u << 20) : (12u << 20);
#else
    return 12u << 20;
#endif
}
static char *hc_dup(const char *s) { uint32_t n = wu_strlen(s); char *p = (char *)kmalloc(n + 1u); if (p) memcpy(p, s, n + 1u); return p; }
static uint32_t hc_cost(const struct hc_ent *e) { return e->len + 1u + wu_strlen(e->url) + wu_strlen(e->final_url) + 2u + (uint32_t)sizeof *e; }
static void hc_drop(struct hc_ent *e)
{
    if (!e->url) return;
    hc_used_bytes -= hc_cost(e); hc_n--;
    kfree(e->url); kfree(e->final_url); kfree(e->body); memset(e, 0, sizeof *e);
}
static struct hc_ent *hc_find(const char *url, uint32_t h, int bin)
{
    for (int i = 0; i < HC_N; i++) { struct hc_ent *e = &hc_tab[i]; if (e->url && e->h == h && e->bin == (uint8_t)bin && wu_streq(e->url, url)) return e; }
    return 0;
}
int whc_get(const char *url, int bin, uint32_t max_body, struct wn_resp *rs)
{
    if (!url || !rs) return 0;
    struct hc_ent *e = hc_find(url, wu_hash(url, wu_strlen(url)), bin != 0);
    if (!e) return 0;
    uint64_t now = web_now_ms();
    if (now >= e->exp) { hc_drop(e); return 0; }                         /* caducada: se pide de nuevo */
    if (max_body && e->len > max_body) return 0;                         /* quien pide ahora acepta menos */
    char *b = (char *)kmalloc(e->len + 1u); if (!b) return 0;
    memcpy(b, e->body, e->len); b[e->len] = 0;
    memset(rs, 0, sizeof *rs); rs->status = 200; rs->body = b; rs->len = e->len;
    wu_copy(rs->ctype, e->ctype, sizeof rs->ctype); wu_copy(rs->charset, e->charset, sizeof rs->charset); wu_copy(rs->final_url, e->final_url, sizeof rs->final_url);
    e->used = now;
    return 1;
}
void whc_put(const char *url, int bin, const struct wn_resp *rs)
{
    if (!url || !rs || rs->cache_s <= 0 || rs->status != 200 || !rs->body) return;
    uint32_t bud = hc_budget(); if (rs->len > HC_MAX_ONE || rs->len > bud / 4) return;
    uint32_t h = wu_hash(url, wu_strlen(url));
    struct hc_ent *old = hc_find(url, h, bin != 0); if (old) hc_drop(old);
    struct hc_ent n; memset(&n, 0, sizeof n);
    n.url = hc_dup(url); n.final_url = hc_dup(rs->final_url[0] ? rs->final_url : url); n.body = (char *)kmalloc(rs->len + 1u);
    if (!n.url || !n.final_url || !n.body) { kfree(n.url); kfree(n.final_url); kfree(n.body); return; }
    memcpy(n.body, rs->body, rs->len); n.body[rs->len] = 0; n.len = rs->len; n.h = h; n.bin = (uint8_t)(bin != 0);
    wu_copy(n.ctype, rs->ctype, sizeof n.ctype); wu_copy(n.charset, rs->charset, sizeof n.charset);
    uint64_t now = web_now_ms(); uint32_t secs = (uint32_t)rs->cache_s; if (secs > HC_MAX_SECS) secs = HC_MAX_SECS;
    n.exp = now + (uint64_t)secs * 1000u; n.used = now;
    for (;;) {   /* sitio: se echa la menos usada hasta que quepa */
        int fr = -1, lru = -1;
        for (int i = 0; i < HC_N; i++) { if (!hc_tab[i].url) { if (fr < 0) fr = i; } else if (lru < 0 || hc_tab[i].used < hc_tab[lru].used) lru = i; }
        if (fr >= 0 && hc_used_bytes + hc_cost(&n) <= bud) { hc_tab[fr] = n; hc_used_bytes += hc_cost(&n); hc_n++; return; }
        if (lru < 0) { kfree(n.url); kfree(n.final_url); kfree(n.body); return; }
        hc_drop(&hc_tab[lru]);
    }
}
void whc_set_budget(uint32_t bytes) { hc_budget_set = bytes; }
void whc_clear(void) { for (int i = 0; i < HC_N; i++) hc_drop(&hc_tab[i]); }
uint32_t whc_bytes(void) { return hc_used_bytes; }
uint32_t whc_count(void) { return hc_n; }
