/* PARCHE 139: servicio aislado de descompresion (ring 3, solo exit + svc_next). Lo usa el kernel desde web_inflate_sbx.c:
 * cuerpos HTTP con Content-Encoding gzip/deflate y flujos FlateDecode de los PDF. Un inflate malicioso (bomba de descompresion,
 * tablas Huffman corruptas...) solo puede matar este proceso o agotar SU monton. */
#include "sbxrt.h"
#include "../apps/web_inflate.h"

uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    if (in->op != SBX_ZLIB_INFLATE) { out->status = -1; return 0; }
    struct wb o; memset(&o, 0, sizeof o);
    uint32_t max_out = (uint32_t)in->arg[1];
    int r = wz_inflate(data, in->len, in->arg[0], &o, max_out);
    out->val[0] = r; out->val[1] = o.err;
    out->status = 0;
    if (o.p && o.len) { out->data_va = (uint64_t)(uintptr_t)o.p; out->data_len = o.len; }
    return 0;
}
