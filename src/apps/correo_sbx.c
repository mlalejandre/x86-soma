/* SOMA_311_CORREO_AISLADO: ver correo_sbx.h. En x86_64 el correo se lee en el servicio aislado mimesvc (ring 3); en aarch64
 * (sin EL0 todavia) se lee aqui mismo, como antes. Si el servicio falla, NO se lee en el nucleo: se dice que no se ha podido. */
#include "correo_sbx.h"

#if defined(__x86_64__) && !defined(WEB_HOST_TEST)          /* en el anfitrion (pruebas): directo, no hay servicios aislados */
#include "sandbox.h"
#include "mem.h"

#define MIME_PLAZO_MS   15000u
#define FICHA_PLAZO_MS  5000u

static void mime_ilegible(struct co_mime *m, char *texto, uint32_t cap)
{
    memset(m, 0, sizeof *m);
    co_cpy(m->asunto, "(correo ilegible)", sizeof m->asunto);
    if (cap) { co_cpy(texto, CORREO_ILEGIBLE, cap); m->texto_n = co_len(texto); }
}

int correo_mime_lee(const char *raw, uint32_t n, struct co_mime *m, char *texto, uint32_t cap)
{
    if (!m || !texto || cap < 2 || !raw || n > SBX_IN_MAX) { if (m && texto) mime_ilegible(m, texto, cap); return 0; }
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) { mime_ilegible(m, texto, cap); return 0; }
    sbx_prepare(c, SBX_MIME_LEE, raw, n, 0);
    c->in.arg[0] = (int32_t)(cap > (16u << 20) ? (16u << 20) : cap);
    int r = sbx_svc_call(SBX_SVC_MIME, c, MIME_PLAZO_MS), ok = 0;
    uint64_t dl = c->out.data_len;
    uint8_t *tmp = 0;
    if (r == 0 && c->out.status == 0 && dl >= sizeof *m && dl - sizeof *m <= (uint64_t)cap - 1u
        && (tmp = (uint8_t *)kmalloc((size_t)dl)) != 0 && sbx_copy_data(c, tmp, dl) == 0) {
        uint32_t tn = (uint32_t)(dl - sizeof *m);                /* lo que de verdad ha llegado de texto */
        memcpy(m, tmp, sizeof *m);
        correo_mime_sanea(m, n, cap);
        if (m->texto_n > tn) m->texto_n = tn;
        memcpy(texto, tmp + sizeof *m, m->texto_n);
        texto[m->texto_n] = 0;
        ok = c->out.val[0] ? 1 : 0;
    } else mime_ilegible(m, texto, cap);
    if (tmp) kfree(tmp);
    sbx_release(c);
    kfree(c);
    return ok;
}

uint32_t correo_adj_saca(const char *raw, uint32_t n, const struct co_adj *a, uint8_t *out, uint32_t cap)
{
    if (!raw || !a || !out || !cap || a->off > n || a->len > n - a->off) return 0;
    uint64_t tot = (uint64_t)sizeof *a + n;
    if (tot > SBX_IN_MAX) return 0;
    uint8_t *in = (uint8_t *)kmalloc((size_t)tot);
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    uint32_t got = 0;
    if (in && c) {
        memcpy(in, a, sizeof *a);
        memcpy(in + sizeof *a, raw, n);
        sbx_prepare(c, SBX_MIME_ADJ, in, (uint32_t)tot, 0);
        c->in.arg[0] = (int32_t)(cap > 0x7FFFFFFFu ? 0x7FFFFFFFu : cap);
        int r = sbx_svc_call(SBX_SVC_MIME, c, MIME_PLAZO_MS);
        if (r == 0 && c->out.status == 0 && c->out.data_len <= cap && c->out.data_len
            && sbx_copy_data(c, out, c->out.data_len) == 0) got = (uint32_t)c->out.data_len;
        sbx_release(c);
    }
    if (in) kfree(in);
    if (c) kfree(c);
    return got;
}

void correo_ficha(struct co_msg *m, const char *cab, uint32_t cab_n, const char *cuerpo, uint32_t cuerpo_n)
{
    uint32_t uid = m->uid, fl = m->flags, tam = m->tam;
    if (!cab) { cab = ""; cab_n = 0; }
    if (!cuerpo) cuerpo_n = 0;
    uint64_t tot = (uint64_t)cab_n + cuerpo_n;
    struct sbx_call *c = tot <= SBX_IN_MAX && cab_n < 0x7FFFFFFFu && cuerpo_n < 0x7FFFFFFFu ? (struct sbx_call *)kmalloc(sizeof *c) : 0;
    uint8_t *in = c && tot ? (uint8_t *)kmalloc((size_t)tot) : 0;
    int bien = 0;
    if (c && (in || !tot)) {
        if (cab_n) memcpy(in, cab, cab_n);
        if (cuerpo_n) memcpy(in + cab_n, cuerpo, cuerpo_n);
        sbx_prepare(c, SBX_MIME_FICHA, in, (uint32_t)tot, 0);
        c->in.arg[0] = (int32_t)cab_n; c->in.arg[1] = (int32_t)cuerpo_n; c->in.arg[2] = cuerpo ? 1 : 0;
        int r = sbx_svc_call(SBX_SVC_MIME, c, FICHA_PLAZO_MS);
        if (r == 0 && c->out.status == 0 && c->out.inline_len == sizeof *m) {
            memcpy(m, c->out.inline_data, sizeof *m);
            correo_msg_sanea(m);
            bien = 1;
        }
        sbx_release(c);
    }
    if (in) kfree(in);
    if (c) kfree(c);
    if (!bien) {                                    /* sin ficha: se ve en la lista, pero sin leer nada del mensaje */
        memset(m, 0, sizeof *m);
        co_cpy(m->asunto, "(correo ilegible)", sizeof m->asunto);
    }
    m->uid = uid; m->flags = fl; m->tam = tam;
}

#else   /* aarch64: sin modo usuario todavia, se lee en el nucleo como antes */

int correo_mime_lee(const char *raw, uint32_t n, struct co_mime *m, char *texto, uint32_t cap) { return co_mime_lee(raw, n, m, texto, cap); }
uint32_t correo_adj_saca(const char *raw, uint32_t n, const struct co_adj *a, uint8_t *out, uint32_t cap) { return co_adj_saca(raw, n, a, out, cap); }
void correo_ficha(struct co_msg *m, const char *cab, uint32_t cab_n, const char *cuerpo, uint32_t cuerpo_n) { co_ficha(m, cab, cab_n, cuerpo, cuerpo_n); }

#endif
