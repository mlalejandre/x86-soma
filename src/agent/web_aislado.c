/* SOMA_312_WEB_AISLADA: ver web_aislado.h. */
#include "web_aislado.h"
#include "web_lee.h"
#include "busca_web.h"

#if defined(__x86_64__) && !defined(SOMA_HOST_TEST) && !defined(WEB_HOST_TEST)
#include "sandbox.h"
#include "mem.h"

#define LEE_PLAZO_MS 10000u
#define LEE_CAP_MAX  (4u << 20)

static uint32_t largo(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }

/* Llama al servicio y deja el texto saneado en out. Devuelve bytes de texto (0 si fallo); val0 y el titulo si se piden. */
static uint32_t llama(uint32_t op, const void *in, uint32_t in_len, char *out, uint32_t cap, int32_t a1, int32_t *val0, char *tit, uint32_t tcap)
{
    if (!out || cap < 2) return 0;
    out[0] = 0;
    if (tit && tcap) tit[0] = 0;
    if (in_len > SBX_IN_MAX) return 0;
    uint32_t c2 = cap > LEE_CAP_MAX ? LEE_CAP_MAX : cap;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) return 0;
    sbx_prepare(c, op, in, in_len, 0);
    c->in.arg[0] = (int32_t)c2; c->in.arg[1] = a1;
    uint32_t n = 0;
    int r = sbx_svc_call(SBX_SVC_LEE, c, LEE_PLAZO_MS);
    if (r == 0 && c->out.status == 0 && c->out.data_len <= c2 - 1u && (c->out.data_len == 0 || sbx_copy_data(c, out, c->out.data_len) == 0)) {
        n = web_aislado_sanea(out, (uint32_t)c->out.data_len, c2);
        if (val0) *val0 = c->out.val[0];
        if (tit && tcap) {
            uint32_t k = c->out.inline_len < SBX_OUT_INLINE ? c->out.inline_len : SBX_OUT_INLINE;
            if (k > tcap - 1) k = tcap - 1;
            for (uint32_t i = 0; i < k; ++i) tit[i] = (char)c->out.inline_data[i];
            web_aislado_sanea(tit, k, tcap);
        }
    } else out[0] = 0;
    sbx_release(c);
    kfree(c);
    return n;
}

uint32_t wl_texto_aislado(const char *html, uint32_t n, char *out, uint32_t cap, char *titulo, uint32_t tcap)
{
    if (!html) { if (out && cap) out[0] = 0; return 0; }
    return llama(SBX_LEE_TEXTO, html, n, out, cap, (int32_t)(tcap > SBX_OUT_INLINE ? SBX_OUT_INLINE : tcap), 0, titulo, tcap);
}

static int busca(uint32_t op, const char *lang, const char *txt, char *out, uint32_t cap, int max)
{
    if (!txt || max <= 0) { if (out && cap) out[0] = 0; return 0; }
    if (max > 64) max = 64;
    uint32_t n = largo(txt), salto = op == SBX_LEE_WIKI ? 16u : 0u;
    char *in = (char *)kmalloc((size_t)n + salto + 1u);
    if (!in) { if (out && cap) out[0] = 0; return 0; }
    for (uint32_t i = 0; i < salto; ++i) in[i] = 0;
    for (uint32_t i = 0; lang && i + 1 < salto && lang[i]; ++i) in[i] = lang[i];
    memcpy(in + salto, txt, n);
    int32_t k = 0;
    uint32_t got = llama(op, in, n + salto, out, cap, max, &k, 0, 0);
    kfree(in);
    return got ? web_aislado_cuenta(k, max) : 0;
}

int bw_wikipedia_aislado(const char *json, const char *lang, char *out, uint32_t cap, int max) { return busca(SBX_LEE_WIKI, lang, json, out, cap, max); }
int bw_ddg_aislado(const char *html, char *out, uint32_t cap, int max) { return busca(SBX_LEE_DDG, 0, html, out, cap, max); }

#else   /* aarch64 (sin EL0) y pruebas del anfitrion: directo, como antes */

uint32_t wl_texto_aislado(const char *html, uint32_t n, char *out, uint32_t cap, char *titulo, uint32_t tcap) { return wl_texto(html, n, out, cap, titulo, tcap); }
int bw_wikipedia_aislado(const char *json, const char *lang, char *out, uint32_t cap, int max) { return bw_parse_wikipedia(json, lang, out, cap, max); }
int bw_ddg_aislado(const char *html, char *out, uint32_t cap, int max) { return bw_parse_ddg_lite(html, out, cap, max); }

#endif
