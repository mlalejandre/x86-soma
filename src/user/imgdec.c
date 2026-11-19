/* PARCHE 138: servicio aislado de decodificacion de imagenes (ring 3, solo SYS_EXIT). Lo usa el kernel desde web_img_sbx.c.
 * Contiene TODO el codigo que interpreta bytes de imagen: PNG (inflate), GIF (LZW), JPEG base/progresivo/CMYK, BMP.
 * Un fallo aqui (desbordamiento, bucle, agotar memoria) mata este proceso; el kernel recibe un error y sigue. */
#include "sbxrt.h"
#include "../apps/web_img.h"
#include "../apps/image.h"
#include "../core/jpeg.h"

/* los decodificadores (web_img.c, web_jpeg.c, web_inflate.c, jpeg.c, bmp_dec.c) se compilan aparte con las opciones de usuario y se enlazan
 * con este fichero (ver IMGDEC_SRCS en el Makefile) */
extern int wi_oom;

/* en el kernel cooperativo los decodificadores cedian la CPU a mano; en ring 3 el reloj expulsa el proceso cada 10 ms */
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }

int sbx_main(const struct sbx_in *in, const uint8_t *data, struct sbx_out *out)
{
    struct wimg im; memset(&im, 0, sizeof im);
    int r;
    if (in->op == SBX_IMG_BMPRAW) {
        struct image b; memset(&b, 0, sizeof b);
        r = bmp_decode(data, in->len, &b);
        if (r == 0) { im.px = b.pixels; im.w = (int)b.width; im.h = (int)b.height; }
        else r = (r == -2) ? -2 : -1;
    } else if (in->op == SBX_IMG_JPEGRAW) {                  /* PARCHE 139: fotograma MJPEG para el reproductor */
        struct jpeg_info ji; memset(&ji, 0, sizeof ji);
        uint32_t stride = (uint32_t)in->arg[0], maxh = (uint32_t)in->arg[1];
        int jr = jpeg_get_info(data, in->len, &ji);
        if (jr == JPEG_OK && (stride == 0 || stride > JPEG_MAX_DIM || ji.width > stride || ji.height > maxh || ji.height == 0)) jr = JPEG_E_SIZE;
        uint32_t *buf = 0;
        if (jr == JPEG_OK) { buf = (uint32_t *)kmalloc((size_t)stride * ji.height * 4u); if (!buf) jr = JPEG_E_SIZE; }
        if (jr == JPEG_OK) jr = jpeg_decode(data, in->len, buf, stride, ji.height, &ji);
        out->val[0] = jr; out->val[1] = (int32_t)ji.width; out->val[2] = (int32_t)ji.height; out->val[3] = ji.ncomp;
        out->val[4] = ji.hmax; out->val[5] = ji.vmax; out->val[6] = ji.restart_interval;
        out->status = jr == JPEG_OK ? 0 : -1;
        if (jr == JPEG_OK) { out->data_va = (uint64_t)(uintptr_t)buf; out->data_len = (uint64_t)stride * ji.height * 4u; }
        return 0;
    } else if (in->op == SBX_IMG_FIT) {
        r = wi_decode_fit_local(data, in->len, &im, in->arg[0], in->arg[1]);
    } else if (in->op == SBX_IMG_ANIM) {                    /* SOMA_180 */
        r = wi_decode_anim_local(data, in->len, &im, (uint32_t)in->arg[0]);
        if (r == 0 && (im.frames < 2 || im.frames > WI_ANIM_MAX_FRAMES || !im.delay)) r = -1;
    } else if (in->op == SBX_IMG_SVG) {                     /* SOMA_180 */
        r = wi_decode_svg_local(data, in->len, &im, in->arg[0], in->arg[1], (uint32_t)in->arg[2]);
    } else if (in->op == SBX_IMG_DECODE) {
        wi_oom = 0;
        r = wi_decode_local(data, in->len, &im);
        if (r != 0 && wi_oom) r = -2;
    } else r = -1;
    out->status = r == 0 ? 0 : (r == -2 ? -2 : -1);
    out->val[4] = wi_oom;
    if (r == 0 && im.px && im.w > 0 && im.h > 0) {
        out->val[0] = im.w; out->val[1] = im.h; out->val[2] = im.nw; out->val[3] = im.nh;
        out->data_va = (uint64_t)(uintptr_t)im.px;
        out->data_len = (uint64_t)im.w * (uint64_t)im.h * 4u;
        if (in->op == SBX_IMG_ANIM) {
            out->val[5] = im.frames; out->data_len *= (uint64_t)im.frames;
            memcpy(out->inline_data, im.delay, (size_t)im.frames * 2u); out->inline_len = (uint32_t)im.frames * 2u;
        }
    } else if (out->status == 0) out->status = -1;
    return 0;
}
