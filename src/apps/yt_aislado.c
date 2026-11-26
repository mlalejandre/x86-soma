/* SOMA_315_YOUTUBE_AISLADO: ver yt_aislado.h. */
#include "yt_aislado.h"

#if defined(__x86_64__) && !defined(SOMA_HOST_TEST) && !defined(WEB_HOST_TEST)
#include "sandbox.h"
#include "mem.h"

#define YTA_PLAZO_MS 10000u

/* Llama a leesvc con la respuesta de YouTube; deja en dst exactamente tam bytes (los que tiene que devolver). 0 = bien. */
static int llama(uint32_t op, const char *b, uint32_t len, int32_t a1, int32_t a2, void *dst, uint32_t cap, uint32_t *tam, int32_t *v0, int32_t *v1)
{
    if (!b || len > SBX_IN_MAX || cap < 2) return -1;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) return -1;
    sbx_prepare(c, op, b, len, 0);
    c->in.arg[0] = (int32_t)cap; c->in.arg[1] = a1; c->in.arg[2] = a2;
    int ret = -1;
    int r = sbx_svc_call(SBX_SVC_LEE, c, YTA_PLAZO_MS);
    if (r == 0 && c->out.status == 0 && c->out.data_len <= cap && (c->out.data_len == 0 || sbx_copy_data(c, dst, c->out.data_len) == 0)) {
        *tam = (uint32_t)c->out.data_len; *v0 = c->out.val[0]; *v1 = c->out.val[1];
        ret = 0;
    }
    sbx_release(c);
    kfree(c);
    return ret;
}

int yti_busca_aislado(const char *b, uint32_t len, struct yt_item *it, int max, int *estimado)
{
    *estimado = 0;
    if (max <= 0) return 0;
    if (max > YT_MAX_ITEMS) max = YT_MAX_ITEMS;
    uint32_t tam = 0; int32_t n = 0, est = 0;
    if (llama(SBX_LEE_YT_BUSCA, b, len, max, 0, it, (uint32_t)max * (uint32_t)sizeof *it, &tam, &n, &est) != 0) return -1;
    if (n < 0 || n > max || tam != (uint32_t)n * (uint32_t)sizeof *it) return -1;          /* tiene que cuadrar exacto */
    *estimado = est ? 1 : 0;
    return yta_sanea_lista(it, n, max);
}

int yti_ficha_aislada(const char *b, uint32_t len, struct yti_player *o, char *desc, uint32_t dmax, int any_host)
{
    o->playable = 0; o->status[0] = o->reason[0] = o->url[0] = o->title[0] = o->author[0] = 0; o->dur = o->views = 0;
    if (desc && dmax) desc[0] = 0; else dmax = 0;
    uint32_t cap = (uint32_t)sizeof *o + (dmax ? dmax : 1u);
    char *tmp = (char *)kmalloc(cap);
    if (!tmp) return -1;
    uint32_t tam = 0; int32_t ok = 0, nada = 0;
    int r = llama(SBX_LEE_YT_FICHA, b, len, (int32_t)(dmax ? dmax : 1u), any_host ? 1 : 0, tmp, cap, &tam, &ok, &nada);
    if (r != 0 || tam < (uint32_t)sizeof *o + 1u || tam > cap) { kfree(tmp); return -1; }
    memcpy(o, tmp, sizeof *o);
    if (dmax) {
        uint32_t dn = tam - (uint32_t)sizeof *o;                        /* la descripcion, con su terminador */
        if (dn > dmax) dn = dmax;
        memcpy(desc, tmp + sizeof *o, dn);
        desc[dn - 1] = 0;
    }
    kfree(tmp);
    yta_sanea_ficha(o, desc, dmax, any_host);
    return ok ? 1 : 0;
}

#else   /* aarch64 (sin EL0) y pruebas del anfitrion: directo, como antes */

int yti_busca_aislado(const char *b, uint32_t len, struct yt_item *it, int max, int *estimado)
{
    int n = yti_parse_search(b, len, it, max);
    *estimado = yti_find(b, b + len, "\"estimatedResults\"") != 0;
    return n;
}
int yti_ficha_aislada(const char *b, uint32_t len, struct yti_player *o, char *desc, uint32_t dmax, int any_host)
{
    return yti_parse_player_ex(b, len, o, desc, dmax, any_host);
}

#endif
