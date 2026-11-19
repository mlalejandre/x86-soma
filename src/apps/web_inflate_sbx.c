/* PARCHE 139: wz_inflate del kernel. En x86_64 la descompresion se hace en el servicio aislado zdec (ring 3); aqui solo se copia el
 * resultado ya validado al final de `out`, con la misma semantica que antes (anade; lo parcial se conserva aunque haya error).
 * En aarch64 (sin EL0) se llama al inflate directo (web_inflate.c). */
#include "web_inflate.h"
#if defined(__x86_64__)
#include "sandbox.h"

int wz_inflate(const uint8_t *in, uint32_t n, int fmt, struct wb *out, uint32_t max_out)
{
    if (!in || !out || out->err) return -1;
    if (n > SBX_IN_MAX) return -1;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) { out->err = 1; return -1; }
    sbx_prepare(c, SBX_ZLIB_INFLATE, in, n, 0);
    c->in.arg[0] = fmt; c->in.arg[1] = (int32_t)(max_out > 0x7FFFFFFFu ? 0x7FFFFFFFu : max_out);
    int r = sbx_svc_call(SBX_SVC_ZLIB, c, 20000);
    int ret = -1;
    if (r == 0) {
        ret = c->out.val[0] < 0 ? c->out.val[0] : 0;
        uint64_t len = c->out.data_len;
        if (len > max_out) { len = max_out; ret = -1; }                     /* el servicio no puede devolver mas de lo pedido */
        if (len && wb_reserve(out, (uint32_t)len)) {
            if (sbx_copy_data(c, out->p + out->len, len) == 0) { out->len += (uint32_t)len; out->p[out->len] = 0; }
            else ret = -1;
        } else if (len) ret = -1;
    } else if (r == SBX_E_HEAPOOM || r == SBX_E_NOMEM) out->err = 1;
    sbx_release(c);
    kfree(c);
    return ret;
}
#endif
