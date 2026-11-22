/* SOMA_252_VIDEO_NATIVO: los dos decodificadores del reproductor nativo, detras de una interfaz minima.
 *   video: H.264 con libavc (third_party/libavc, Apache 2.0), un solo hilo, rutas en C generico;
 *   audio: AAC con libxaac (third_party/libxaac, Apache 2.0), salida PCM de 16 bits.
 * El audio va aparte (vp_adec.c): los tipos basicos de las dos bibliotecas chocan.
 * Se compila igual para ring 3 (vplay.elf, con vp_libc.c) y para el anfitrion (tests/vplay_host_252.c). */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "vp_abi.h"

#include "ih264_typedefs.h"
#include "iv.h"
#include "ivd.h"
#include "ih264d.h"
#include "ih264_defs.h"
#include "ih264_size_defs.h"
#include "ih264_error.h"
#include "ih264_trans_quant_itrans_iquant.h"
#include "ih264_inter_pred_filters.h"
#include "ih264_deblk_edge_filters.h"
#include "ih264d_structs.h"
#include "ih264d_function_selector.h"

/* libavc elige rutinas segun el procesador; aqui siempre las de C (funcionan en cualquier CPU y en QEMU) */
void ih264d_init_function_ptr(dec_struct_t *ps_codec) { ih264d_init_function_ptr_generic(ps_codec); }
void ih264d_init_arch(dec_struct_t *ps_codec) { ps_codec->e_processor_arch = ARCH_X86_GENERIC; }

/* ------------------------------------------------------------------ memoria alineada para libavc */
static void *vp_aalloc(void *ctx, WORD32 align, WORD32 size)
{
    (void)ctx;
    if (align < 16) align = 16;
    if (size <= 0) return 0;
    uint8_t *raw = (uint8_t *)malloc((size_t)size + (size_t)align + sizeof(void *));
    if (!raw) return 0;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (uintptr_t)align - 1) & ~((uintptr_t)align - 1);
    ((void **)a)[-1] = raw;
    return (void *)a;
}
static void vp_afree(void *ctx, void *p) { (void)ctx; if (p) free(((void **)p)[-1]); }

/* ------------------------------------------------------------------ video */
static iv_obj_t *vd;
static uint8_t *vd_bs; static uint32_t vd_bs_cap;            /* muestra convertida a Annex B */
static uint8_t *vd_out[3]; static uint32_t vd_out_sz[3];
static uint32_t vd_nal_len, vd_w, vd_h;
static uint8_t vd_hdr[1024]; static uint32_t vd_hdr_n, vd_hdr_len;      /* vd_hdr_n != 0: hay que mandar SPS/PPS con la siguiente muestra */

static int vd_ctl_params(int header_only)
{
    ih264d_ctl_set_config_ip_t ip; ih264d_ctl_set_config_op_t op;
    memset(&ip, 0, sizeof ip); memset(&op, 0, sizeof op);
    ip.s_ivd_ctl_set_config_ip_t.u4_disp_wd = 0;
    ip.s_ivd_ctl_set_config_ip_t.e_frm_skip_mode = IVD_SKIP_NONE;
    ip.s_ivd_ctl_set_config_ip_t.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    ip.s_ivd_ctl_set_config_ip_t.e_vid_dec_mode = header_only ? IVD_DECODE_HEADER : IVD_DECODE_FRAME;
    ip.s_ivd_ctl_set_config_ip_t.e_cmd = IVD_CMD_VIDEO_CTL;
    ip.s_ivd_ctl_set_config_ip_t.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
    ip.s_ivd_ctl_set_config_ip_t.u4_size = sizeof ip;
    op.s_ivd_ctl_set_config_op_t.u4_size = sizeof op;
    return ih264d_api_function(vd, &ip, &op) == IV_SUCCESS;
}
static int vd_call(const uint8_t *bs, uint32_t n, uint32_t ts, ih264d_video_decode_op_t *op)
{
    ih264d_video_decode_ip_t ip;
    memset(&ip, 0, sizeof ip); memset(op, 0, sizeof *op);
    ip.s_ivd_video_decode_ip_t.e_cmd = IVD_CMD_VIDEO_DECODE;
    ip.s_ivd_video_decode_ip_t.u4_ts = ts;
    ip.s_ivd_video_decode_ip_t.pv_stream_buffer = (void *)bs;
    ip.s_ivd_video_decode_ip_t.u4_num_Bytes = n;
    ip.s_ivd_video_decode_ip_t.u4_size = sizeof ip;
    ip.s_ivd_video_decode_ip_t.s_out_buffer.u4_num_bufs = 3;
    for (int i = 0; i < 3; ++i) { ip.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[i] = vd_out[i]; ip.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[i] = vd_out_sz[i]; }
    op->s_ivd_video_decode_op_t.u4_size = sizeof *op;
    return ih264d_api_function(vd, &ip, op) == IV_SUCCESS;
}

void vd_close(void)
{
    if (vd) {
        ih264d_delete_ip_t ip; ih264d_delete_op_t op;
        memset(&ip, 0, sizeof ip); memset(&op, 0, sizeof op);
        ip.s_ivd_delete_ip_t.e_cmd = IVD_CMD_DELETE; ip.s_ivd_delete_ip_t.u4_size = sizeof ip; op.s_ivd_delete_op_t.u4_size = sizeof op;
        ih264d_api_function(vd, &ip, &op);
        vd = 0;
    }
    free(vd_bs); vd_bs = 0; vd_bs_cap = 0;
    for (int i = 0; i < 3; ++i) { free(vd_out[i]); vd_out[i] = 0; }
}

int vd_open(const uint8_t *hdr_annexb, uint32_t hdr_n, uint32_t nal_len, uint32_t w, uint32_t h)
{
    vd_close();
    if (w < 16 || h < 16 || w > VP_MAX_W || h > VP_MAX_H || hdr_n > sizeof vd_hdr) return 0;
    ih264d_create_ip_t cip; ih264d_create_op_t cop;
    memset(&cip, 0, sizeof cip); memset(&cop, 0, sizeof cop);
    cip.s_ivd_create_ip_t.e_cmd = IVD_CMD_CREATE;
    cip.s_ivd_create_ip_t.u4_share_disp_buf = 0;
    cip.s_ivd_create_ip_t.e_output_format = IV_YUV_420P;
    cip.s_ivd_create_ip_t.pf_aligned_alloc = vp_aalloc;
    cip.s_ivd_create_ip_t.pf_aligned_free = vp_afree;
    cip.s_ivd_create_ip_t.u4_size = sizeof cip;
    cop.s_ivd_create_op_t.u4_size = sizeof cop;
    if (ih264d_api_function(0, &cip, &cop) != IV_SUCCESS) return 0;
    vd = (iv_obj_t *)cop.s_ivd_create_op_t.pv_handle;
    vd->pv_fxns = (void *)ih264d_api_function; vd->u4_size = sizeof(iv_obj_t);
    {
        ih264d_ctl_set_num_cores_ip_t ip; ih264d_ctl_set_num_cores_op_t op;
        memset(&ip, 0, sizeof ip); memset(&op, 0, sizeof op);
        ip.e_cmd = IVD_CMD_VIDEO_CTL; ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T)IH264D_CMD_CTL_SET_NUM_CORES; ip.u4_num_cores = 1;
        ip.u4_size = sizeof ip; op.u4_size = sizeof op;
        ih264d_api_function(vd, &ip, &op);
    }
    /* salidas: planos Y, U, V con margen (el decodificador copia ahi el fotograma que toca mostrar) */
    uint32_t aw = (w + 63u) & ~63u, ah = (h + 63u) & ~63u;
    vd_out_sz[0] = aw * ah + 4096; vd_out_sz[1] = vd_out_sz[2] = aw * ah / 4 + 4096;
    for (int i = 0; i < 3; ++i) { vd_out[i] = (uint8_t *)malloc(vd_out_sz[i]); if (!vd_out[i]) { vd_close(); return 0; } }
    vd_bs_cap = 256u * 1024u; vd_bs = (uint8_t *)malloc(vd_bs_cap);
    if (!vd_bs) { vd_close(); return 0; }
    vd_nal_len = nal_len; vd_w = w; vd_h = h;
    memcpy(vd_hdr, hdr_annexb, hdr_n); vd_hdr_n = vd_hdr_len = hdr_n;
    if (!vd_ctl_params(0)) { vd_close(); return 0; }
    return 1;
}

/* Tras un salto: el decodificador olvida sus referencias; la siguiente muestra debe ser clave. */
void vd_reset(void)
{
    if (!vd) return;
    ivd_ctl_reset_ip_t ip; ivd_ctl_reset_op_t op;
    memset(&ip, 0, sizeof ip); memset(&op, 0, sizeof op);
    ip.e_cmd = IVD_CMD_VIDEO_CTL; ip.e_sub_cmd = IVD_CMD_CTL_RESET; ip.u4_size = sizeof ip; op.u4_size = sizeof op;
    ih264d_api_function(vd, &ip, &op);
    {
        ih264d_ctl_set_num_cores_ip_t cip; ih264d_ctl_set_num_cores_op_t cop;
        memset(&cip, 0, sizeof cip); memset(&cop, 0, sizeof cop);
        cip.e_cmd = IVD_CMD_VIDEO_CTL; cip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T)IH264D_CMD_CTL_SET_NUM_CORES; cip.u4_num_cores = 1;
        cip.u4_size = sizeof cip; cop.u4_size = sizeof cop;
        ih264d_api_function(vd, &cip, &cop);
    }
    vd_ctl_params(0);
    vd_hdr_n = vd_hdr_len;                                       /* el reinicio tambien olvida SPS y PPS */
}

/* YUV 4:2:0 (BT.601, rango de video) -> XRGB, con enteros */
static void yuv_to_xrgb(const uint8_t *y, uint32_t ys, const uint8_t *u, const uint8_t *v, uint32_t cs, uint32_t w, uint32_t h, uint32_t *dst, uint32_t dstride)
{
    for (uint32_t j = 0; j < h; ++j) {
        const uint8_t *py = y + (size_t)j * ys, *pu = u + (size_t)(j >> 1) * cs, *pv = v + (size_t)(j >> 1) * cs;
        uint32_t *d = dst + (size_t)j * dstride;
        for (uint32_t i = 0; i < w; ++i) {
            int c = (int)py[i] - 16, dd = (int)pu[i >> 1] - 128, e = (int)pv[i >> 1] - 128;
            int yy = 298 * c + 128;
            int r = (yy + 409 * e) >> 8, g = (yy - 100 * dd - 208 * e) >> 8, b = (yy + 516 * dd) >> 8;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            d[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

/* Decodifica una muestra del MP4 (NAL con longitud delante; n == 0 = vaciar lo que quede dentro al final).
 * ts viaja con el fotograma: cuando sale uno (*got = 1), *out_ts es el ts de la muestra a la que pertenece.
 * Si dst != 0 el fotograma se convierte a XRGB (dstride pixeles por fila). 0 = error del flujo (se puede seguir). */
int vd_decode(const uint8_t *s, uint32_t n, uint32_t ts, uint32_t *dst, uint32_t dstride, int *got, uint32_t *out_ts, uint32_t *ow, uint32_t *oh)
{
    *got = 0;
    if (!vd) return 0;
    uint32_t o = 0;
    if (vd_hdr_n) { memcpy(vd_bs, vd_hdr, vd_hdr_n); o = vd_hdr_n; }          /* SPS y PPS delante de la primera muestra */
    if (n) {
        uint32_t p = 0;
        if ((uint64_t)n + n / 2 + o + 64 > vd_bs_cap) {
            uint32_t nc = n + n / 2 + o + 4096; uint8_t *nb = (uint8_t *)malloc(nc);
            if (!nb) return 0;
            free(vd_bs); vd_bs = nb; vd_bs_cap = nc;
            if (vd_hdr_n) memcpy(vd_bs, vd_hdr, vd_hdr_n);
        }
        while (p + vd_nal_len <= n) {
            uint32_t l = 0; for (uint32_t k = 0; k < vd_nal_len; ++k) l = (l << 8) | s[p + k];
            p += vd_nal_len;
            if (l > n - p) break;                                             /* muestra truncada o corrupta */
            if (o + 4 + l > vd_bs_cap) break;
            vd_bs[o++] = 0; vd_bs[o++] = 0; vd_bs[o++] = 0; vd_bs[o++] = 1;
            memcpy(vd_bs + o, s + p, l); o += l; p += l;
        }
        if (o == 0) return 0;
    } else {
        ivd_ctl_flush_ip_t ip; ivd_ctl_flush_op_t op;
        memset(&ip, 0, sizeof ip); memset(&op, 0, sizeof op);
        ip.e_cmd = IVD_CMD_VIDEO_CTL; ip.e_sub_cmd = IVD_CMD_CTL_FLUSH; ip.u4_size = sizeof ip; op.u4_size = sizeof op;
        ih264d_api_function(vd, &ip, &op);
        o = 0;
    }
    vd_hdr_n = 0;
    ih264d_video_decode_op_t op;
    uint32_t done = 0; int ok = 1, guard = 0;
    do {                                                                       /* una llamada consume un NAL o un fotograma */
        int r = vd_call(vd_bs + done, o - done, ts, &op);
        uint32_t used = op.s_ivd_video_decode_op_t.u4_num_bytes_consumed;
        if (op.s_ivd_video_decode_op_t.u4_output_present && !*got) {
            *got = 1; *out_ts = op.s_ivd_video_decode_op_t.u4_ts;
            iv_yuv_buf_t *f = &op.s_ivd_video_decode_op_t.s_disp_frm_buf;
            uint32_t w = f->u4_y_wd, h = f->u4_y_ht;
            if (w > vd_w) w = vd_w;
            if (h > vd_h) h = vd_h;
            *ow = w; *oh = h;
            if (dst && w && h && w <= dstride) yuv_to_xrgb((const uint8_t *)f->pv_y_buf, f->u4_y_strd, (const uint8_t *)f->pv_u_buf, (const uint8_t *)f->pv_v_buf, f->u4_u_strd, w, h, dst, dstride);
        }
        if (!r) { ok = 0; if (!used) break; }
        if (!n) break;
        done += used ? used : (o - done);
    } while (done < o && ++guard < 64);
    return ok;
}
