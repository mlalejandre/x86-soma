/* SOMA_311_CORREO_AISLADO: servicio aislado que LEE el correo (ring 3, solo exit + svc_next). Lo usa el nucleo desde
 * src/apps/correo_sbx.c. Todo lo que interpreta bytes de un mensaje que llega de Internet (MIME, base64, quoted-printable,
 * juegos de caracteres, RFC 2047, HTML) corre aqui, con el MISMO codigo de antes (src/apps/correo_core.c).
 * Sin estado entre peticiones. Las respuestas no llevan punteros: el nucleo valida y sanea todo (correo_sbx.h). */
#include "sbxrt.h"
#include "../apps/correo_core.h"

void *co_alloc(uint32_t n) { return kmalloc(n ? n : 1); }
void  co_free(void *p) { if (p) kfree(p); }

_Static_assert(sizeof(struct co_msg) <= SBX_OUT_INLINE, "la ficha tiene que caber en la respuesta");

#define MIME_TEXTO_MAX (16u << 20)

/* El mensaje llega en la entrada (solo lectura) y sin NUL detras: se copia al monton terminado en NUL. */
static char *copia(const uint8_t *d, uint32_t n)
{
    char *p = (char *)kmalloc((size_t)n + 1u);
    if (!p) return 0;
    memcpy(p, d, n); p[n] = 0;
    return p;
}

static int lee(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    uint32_t cap = (uint32_t)in->arg[0];
    if (cap < 2 || cap > MIME_TEXTO_MAX) { out->status = -1; return 0; }
    char *raw = copia(data, in->len);
    uint8_t *buf = (uint8_t *)kmalloc(sizeof(struct co_mime) + (size_t)cap);
    if (!raw || !buf) { out->status = -2; return 0; }
    struct co_mime *m = (struct co_mime *)buf;
    char *tx = (char *)(buf + sizeof(struct co_mime));
    memset(m, 0, sizeof *m); tx[0] = 0;
    int ok = co_mime_lee(raw, in->len, m, tx, cap);
    if (m->texto_n > cap - 1) m->texto_n = cap - 1;
    out->val[0] = ok;
    out->status = 0;
    out->data_va = (uint64_t)(uintptr_t)buf;
    out->data_len = (uint64_t)sizeof(struct co_mime) + m->texto_n;
    return 0;
}

static int adjunto(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    uint32_t cap = (uint32_t)in->arg[0];
    if (in->len < sizeof(struct co_adj) || cap == 0 || cap > SBX_HEAP_MAX / 2) { out->status = -1; return 0; }
    struct co_adj a; memcpy(&a, data, sizeof a);
    uint32_t rn = in->len - (uint32_t)sizeof a;
    if (a.off > rn || a.len > rn - a.off) { out->status = -1; return 0; }
    char *raw = copia(data + sizeof a, rn);
    uint8_t *o = (uint8_t *)kmalloc(cap);
    if (!raw || !o) { out->status = -2; return 0; }
    uint32_t n = co_adj_saca(raw, rn, &a, o, cap);
    if (n > cap) n = cap;
    out->status = 0; out->val[0] = (int32_t)n;
    if (n) { out->data_va = (uint64_t)(uintptr_t)o; out->data_len = n; }
    return 0;
}

static int ficha(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    uint32_t cn = (uint32_t)in->arg[0], bn = (uint32_t)in->arg[1];
    int con_cuerpo = in->arg[2] != 0;
    if (in->arg[0] < 0 || in->arg[1] < 0 || (uint64_t)cn + bn != in->len) { out->status = -1; return 0; }
    char *cab = copia(data, cn), *cuerpo = con_cuerpo ? copia(data + cn, bn) : 0;
    if (!cab || (con_cuerpo && !cuerpo)) { out->status = -2; return 0; }
    struct co_msg m; memset(&m, 0, sizeof m);
    co_ficha(&m, cab, cn, cuerpo, bn);
    memcpy(out->inline_data, &m, sizeof m);
    out->inline_len = (uint32_t)sizeof m;
    out->status = 0;
    return 0;
}

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    if (in->op == SBX_MIME_LEE) return lee(in, data, out);
    if (in->op == SBX_MIME_ADJ) return adjunto(in, data, out);
    if (in->op == SBX_MIME_FICHA) return ficha(in, data, out);
    out->status = -1;
    return 0;
}
