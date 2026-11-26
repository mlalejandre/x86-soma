/* SOMA_312_WEB_AISLADA: servicio aislado que LEE lo que el agente trae de Internet (ring 3, solo exit + svc_next). Lo usa el nucleo
 * desde src/agent/web_aislado.c: el texto de una pagina web cualquiera (web_lee.c, orden "investiga" y "search") y los resultados
 * de Wikipedia y DuckDuckGo (busca_web.c). El mismo codigo de antes. Sin estado entre peticiones; el nucleo sanea la respuesta.
 * SOMA_315_YOUTUBE_AISLADO: tambien la busqueda y la ficha de YouTube nativo (yt_inner.h), desde src/apps/yt_aislado.c. */
#include "sbxrt.h"
#include "../agent/web_lee.h"
#include "../agent/busca_web.h"
#include "../apps/yt_inner.h"   /* SOMA_315_YOUTUBE_AISLADO */
#include "../apps/noticias_rss.h"   /* SOMA_337_NOTICIAS */

#define LEE_CAP_MAX (4u << 20)

static char *copia(const uint8_t *d, uint32_t n)
{
    char *p = (char *)kmalloc((size_t)n + 1u);
    if (!p) return 0;
    memcpy(p, d, n); p[n] = 0;
    return p;
}

static int responde_texto(struct sbx_out *out, char *o, uint32_t n, uint32_t cap)
{
    if (n > cap - 1) n = cap - 1;
    o[n] = 0;
    out->status = 0;
    if (n) { out->data_va = (uint64_t)(uintptr_t)o; out->data_len = n; }
    return 0;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    uint32_t cap = (uint32_t)in->arg[0];
    if (cap < 2 || cap > LEE_CAP_MAX) { out->status = -1; return 0; }
    char *o = (char *)kmalloc(cap);
    if (!o) { out->status = -2; return 0; }
    o[0] = 0;
    if (in->op == SBX_LEE_TEXTO) {
        uint32_t tcap = (uint32_t)in->arg[1];
        if (tcap > SBX_OUT_INLINE) tcap = SBX_OUT_INLINE;
        char *html = copia(data, in->len);
        char *tit = tcap ? (char *)kmalloc(tcap) : 0;
        if (!html || (tcap && !tit)) { out->status = -2; return 0; }
        if (tit) tit[0] = 0;
        uint32_t n = wl_texto(html, in->len, o, cap, tit, tcap);
        if (tit) {
            uint32_t k = 0; while (k + 1 < tcap && tit[k]) { out->inline_data[k] = (uint8_t)tit[k]; k++; }
            out->inline_data[k] = 0; out->inline_len = k;
        }
        out->val[0] = (int32_t)n;
        return responde_texto(out, o, n, cap);
    }
    if (in->op == SBX_LEE_WIKI || in->op == SBX_LEE_DDG) {
        int max = in->arg[1];
        if (max < 0 || max > 64) { out->status = -1; return 0; }
        uint32_t salto = in->op == SBX_LEE_WIKI ? 16u : 0u;              /* WIKI: 16 bytes de idioma ("es") delante */
        if (in->len < salto) { out->status = -1; return 0; }
        char lang[17]; memcpy(lang, data, salto); lang[salto] = 0; lang[16] = 0;
        char *txt = copia(data + salto, in->len - salto);
        if (!txt) { out->status = -2; return 0; }
        int k = in->op == SBX_LEE_WIKI ? bw_parse_wikipedia(txt, lang, o, cap, max) : bw_parse_ddg_lite(txt, o, cap, max);
        out->val[0] = k;
        uint32_t n = 0; while (n + 1 < cap && o[n]) n++;
        return responde_texto(out, o, n, cap);
    }
    if (in->op == SBX_LEE_RSS) {                                         /* SOMA_337_NOTICIAS: canales RSS y Atom */
        int max = in->arg[1];
        if (max <= 0 || max > 200 || cap < (uint32_t)max * (uint32_t)sizeof(struct nt_item) || in->len < 64u) { out->status = -1; return 0; }
        char fdef[64]; memcpy(fdef, data, 64); fdef[63] = 0;
        struct nt_item *it = (struct nt_item *)(void *)o;
        int n = nt_rss((const char *)data + 64, in->len - 64u, it, max, fdef);
        out->val[0] = n;
        out->status = 0;
        if (n > 0) { out->data_va = (uint64_t)(uintptr_t)o; out->data_len = (uint32_t)n * (uint32_t)sizeof(struct nt_item); }
        return 0;
    }
    if (in->op == SBX_LEE_YT_BUSCA) {                                    /* SOMA_315_YOUTUBE_AISLADO */
        int max = in->arg[1];
        if (max <= 0 || max > YT_MAX_ITEMS || cap < (uint32_t)max * (uint32_t)sizeof(struct yt_item)) { out->status = -1; return 0; }
        struct yt_item *it = (struct yt_item *)(void *)o;
        const char *b = (const char *)data;
        int n = yti_parse_search(b, in->len, it, max);
        out->val[0] = n;
        out->val[1] = yti_find(b, b + in->len, "\"estimatedResults\"") != 0;
        out->status = 0;
        if (n > 0) { out->data_va = (uint64_t)(uintptr_t)o; out->data_len = (uint32_t)n * (uint32_t)sizeof(struct yt_item); }
        return 0;
    }
    if (in->op == SBX_LEE_YT_FICHA) {
        uint32_t dmax = (uint32_t)in->arg[1];
        if (dmax < 1 || dmax > cap || cap != (uint32_t)sizeof(struct yti_player) + dmax) { out->status = -1; return 0; }
        struct yti_player *pl = (struct yti_player *)(void *)o;
        char *d = o + sizeof *pl;
        out->val[0] = yti_parse_player_ex((const char *)data, in->len, pl, d, dmax, in->arg[2] != 0);
        uint32_t dn = 0; while (dn + 1 < dmax && d[dn]) dn++;
        d[dn] = 0;
        out->status = 0;
        out->data_va = (uint64_t)(uintptr_t)o; out->data_len = (uint32_t)sizeof *pl + dn + 1u;
        return 0;
    }
    out->status = -1;
    return 0;
}
