/* SOMA_175A_T480S: ver iwl_fw.h. Robusto ante archivos cortados o malformados (se prueba con datos al azar). */
#include "iwl_fw.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int iwl_fw_parse(const uint8_t *d, uint32_t len, struct iwl_fw_info *o, const char **why)
{
    const char *w = 0;
    uint8_t *z = (uint8_t *)o;
    for (uint32_t i = 0; i < sizeof *o; i++) z[i] = 0;
    if (!d || len < 88) { w = "archivo demasiado corto"; goto bad; }
    if (le32(d) != 0 || le32(d + 4) != 0x0A4C5749u) { w = "no es un firmware Intel en formato TLV (falta \"IWL\")"; goto bad; }
    for (int i = 0; i < 64; i++) { char c = (char)d[8 + i]; if (!c) break; o->human[i] = (c >= 32 && c < 127) ? c : '?'; }
    o->ver = le32(d + 72); o->build = le32(d + 76);
    uint32_t off = 88;
    while (off + 8 <= len) {
        uint32_t t = le32(d + off), l = le32(d + off + 4);
        const uint8_t *p = d + off + 8;
        if (l > len - off - 8) { w = "bloque TLV cortado (archivo incompleto)"; goto bad; }
        o->tlvs++;
        if (t == 19 || t == 20 || t == 21) {                             /* SEC_RT, SEC_INIT, SEC_WOWLAN: desplazamiento + datos */
            struct iwl_fw_img *im = &o->img[t == 19 ? IWL_IMG_RT : t == 20 ? IWL_IMG_INIT : IWL_IMG_WOWLAN];
            if (l < 4) { w = "seccion sin direccion"; goto bad; }
            if (im->n >= IWL_FW_MAX_SEC) { w = "demasiadas secciones"; goto bad; }
            im->sec[im->n].offset = le32(p); im->sec[im->n].len = l - 4; im->sec[im->n].data = p + 4; im->n++;
        } else if (t == 23 && l >= 4) o->phy_sku = le32(p);
        else if (t == 27 && l >= 4) o->num_cpu = le32(p);
        else if (t == 31 && l >= 4) o->n_scan_channels = le32(p);
        else if (t == 32 && l >= 4) o->paging_size = le32(p);
        else if (t == 22 && l >= 12) { uint32_t ut = le32(p); if (ut < 3) { o->calib_flow[ut] = le32(p + 4); o->calib_event[ut] = le32(p + 8); } }
        else if ((t == 29 || t == 30) && l >= 8) {
            uint32_t idx = le32(p), fl = le32(p + 4);
            if (idx < 4) { if (t == 29) o->api[idx] = fl; else o->capa[idx] = fl; }
        }
        uint32_t step = (l + 3u) & ~3u;
        if (step > len - off - 8) break;                                 /* el ultimo puede no tener relleno */
        off += 8 + step;
    }
    if (!o->img[IWL_IMG_INIT].n || !o->img[IWL_IMG_RT].n) { w = "faltan las imagenes INIT o RUNTIME"; goto bad; }
    for (int k = 0; k < IWL_IMG_N; k++) {
        int a, b;
        if (o->img[k].n && iwl_fw_cpu_range(&o->img[k], 1, &a, &b) != 0) { w = "imagen sin secciones para la CPU1"; goto bad; }
    }
    if (o->paging_size > (4u << 20)) { w = "paginacion de tamano imposible"; goto bad; }
    if (why) *why = 0;
    return 0;
bad:
    if (why) *why = w;
    return -1;
}

int iwl_fw_cpu_range(const struct iwl_fw_img *im, int cpu, int *first, int *end)
{
    int i = 0;
    if (cpu == 2) {
        while (i < im->n && im->sec[i].offset != IWL_SEP_CPU1_CPU2) i++;
        if (i >= im->n) { *first = *end = im->n; return 0; }               /* imagen de una sola CPU */
        i++;
    }
    *first = i;
    while (i < im->n && im->sec[i].offset != IWL_SEP_CPU1_CPU2 && im->sec[i].offset != IWL_SEP_PAGING && im->sec[i].len) i++;
    *end = i;
    return (cpu == 1 && *end == *first) ? -1 : 0;
}
