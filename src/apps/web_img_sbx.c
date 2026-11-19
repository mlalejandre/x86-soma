/* PARCHE 138: puntos de entrada publicos de los decodificadores de imagen (wi_decode, wi_decode_fit, wi_decode_bmp_raw).
 *
 * x86_64: NINGUN decodificador se ejecuta en el kernel. Cada imagen se decodifica en un proceso aislado de ring 3 (servicio imgdec,
 *   src/user/imgdec.c) que no puede hacer syscalls salvo exit, con su entrada de solo lectura, monton propio bajo demanda y plazo.
 *   Aqui solo se validan las dimensiones y el tamano del resultado y se copian los pixeles a memoria del kernel.
 * aarch64: sin ring 3 todavia (EL0 pendiente): llamada directa a los decodificadores (web_img.c, bmp_dec.c). */
#include "web_img.h"
#include "image.h"
#include "mem.h"
#include "jpeg.h"

#if defined(__x86_64__)
#include "sandbox.h"

int wi_oom;
void wi_free(struct wimg *o) { if (o && o->px) kfree(o->px); if (o && o->delay) kfree(o->delay); if (o) { o->px = 0; o->w = o->h = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0; } }

#define IMG_TIMEOUT_MS 15000u

/* 0 OK (*px de w*h en memoria del kernel), -1 imagen no valida o el servicio fallo, -2 sin memoria. */
static int img_call2(uint32_t op, const uint8_t *d, uint32_t n, int a0, int a1, int a2, uint32_t **px, int v[5], uint16_t **delay, int *frames)
{
    *px = 0;
    if (!d || n == 0 || n > SBX_IN_MAX) return -1;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) return -2;
    sbx_prepare(c, op, d, n, SBX_HEAP_MAX);
    c->in.arg[0] = a0; c->in.arg[1] = a1; c->in.arg[2] = a2;
    int r = sbx_svc_call(SBX_SVC_IMG, c, IMG_TIMEOUT_MS);       /* PARCHE 139: servicio persistente (antes un proceso por imagen) */
    int ret = -1;
    if (r == SBX_E_HEAPOOM || r == SBX_E_NOMEM) ret = -2;
    else if (r == 0) {
        int32_t w = c->out.val[0], h = c->out.val[1], nf = 1;
        if (op == SBX_IMG_ANIM) { nf = c->out.val[5]; if (nf < 2 || nf > WI_ANIM_MAX_FRAMES || c->out.inline_len != (uint32_t)nf * 2u || !delay) nf = 0; }   /* SOMA_180 */
        if (c->out.status == -2) ret = -2;
        else if (c->out.status == 0 && nf > 0 && w > 0 && h > 0 && (uint64_t)w * (uint64_t)h * (uint64_t)nf <= SBX_IMG_MAX_PIXELS &&
                 c->out.data_len == (uint64_t)w * (uint64_t)h * 4u * (uint64_t)nf &&
                 c->out.val[2] >= 0 && c->out.val[2] <= 65535 && c->out.val[3] >= 0 && c->out.val[3] <= 65535) {
            uint32_t *p = (uint32_t *)kmalloc((size_t)c->out.data_len);
            if (!p) ret = -2;
            else if (sbx_copy_data(c, p, c->out.data_len) != 0) { kfree(p); ret = -1; }
            else if (nf > 1) {                                      /* SOMA_180: duraciones de la animacion */
                uint16_t *dl = (uint16_t *)kmalloc((size_t)nf * 2u);
                if (!dl) { kfree(p); ret = -2; }
                else { for (int i = 0; i < nf; ++i) { uint16_t t = (uint16_t)(c->out.inline_data[i * 2] | (c->out.inline_data[i * 2 + 1] << 8)); dl[i] = t < 20 ? 100 : t; }
                       *delay = dl; *frames = nf; *px = p; for (int i = 0; i < 5; ++i) v[i] = c->out.val[i]; ret = 0; }
            }
            else { *px = p; for (int i = 0; i < 5; ++i) v[i] = c->out.val[i]; ret = 0; }
        }
    }
    sbx_release(c);
    kfree(c);
    return ret;
}

static int img_call(uint32_t op, const uint8_t *d, uint32_t n, int a0, int a1, uint32_t **px, int v[5]) { return img_call2(op, d, n, a0, a1, 0, px, v, 0, 0); }

static int to_wimg(int r, uint32_t *px, const int v[5], struct wimg *o)
{
    o->w = o->h = 0; o->px = 0; o->nw = o->nh = 0; o->frames = 0; o->delay = 0;
    if (r != 0) { if (r == -2) wi_oom = 1; return r; }
    o->px = px; o->w = v[0]; o->h = v[1]; o->nw = v[2]; o->nh = v[3];
    return 0;
}

int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    uint32_t *px = 0; int v[5] = { 0 };
    wi_oom = 0;
    int c = img_call(SBX_IMG_DECODE, d, n, 0, 0, &px, v);
    return to_wimg(c, px, v, o) == 0 ? 0 : -1;
}

int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh)
{
    uint32_t *px = 0; int v[5] = { 0 };
    wi_oom = 0;
    int c = img_call(SBX_IMG_FIT, d, n, maxw, maxh, &px, v);
    return to_wimg(c, px, v, o);
}

/* SOMA_180: comprobaciones baratas que si se hacen en el kernel (solo miran la cabecera; la decodificacion sigue aislada) */
int wi_is_svg(const uint8_t *d, uint32_t n)
{
    uint32_t i = 0; if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
    for (int guard = 0; guard < 64 && i < n; guard++) {
        while (i < n && (d[i] == ' ' || d[i] == '\n' || d[i] == '\r' || d[i] == '\t')) i++;
        if (i + 4 >= n || d[i] != '<') return 0;
        if (d[i + 1] == 's' && d[i + 2] == 'v' && d[i + 3] == 'g' && (d[i + 4] == ' ' || d[i + 4] == '>' || d[i + 4] == '\n' || d[i + 4] == '\r' || d[i + 4] == '\t' || d[i + 4] == '/')) return 1;
        if (d[i + 1] == '?') { while (i + 1 < n && !(d[i] == '?' && d[i + 1] == '>')) i++; i += 2; continue; }
        if (d[i + 1] == '!' && d[i + 2] == '-' && d[i + 3] == '-') { while (i + 2 < n && !(d[i] == '-' && d[i + 1] == '-' && d[i + 2] == '>')) i++; i += 3; continue; }
        if (d[i + 1] == '!') { int dep = 0; while (i < n) { if (d[i] == '[') dep++; else if (d[i] == ']') dep--; else if (d[i] == '>' && dep <= 0) break; i++; } i++; continue; }
        return 0;
    }
    return 0;
}
/* 2 = puede ser una animacion (cualquier GIF, o WebP con la marca de animacion): el servicio aislado dice si lo es de verdad */
int wi_anim_frames(const uint8_t *d, uint32_t n)
{
    if (n >= 13 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return 2;
    if (n >= 30 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P' && d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == 'X' && (d[20] & 0x02)) return 2;
    return 1;
}
int wi_decode_anim(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels)
{
    uint32_t *px = 0; int v[5] = { 0 }, nf = 0; uint16_t *dl = 0;
    wi_oom = 0;
    int c = img_call2(SBX_IMG_ANIM, d, n, (int)max_pixels, 0, 0, &px, v, &dl, &nf);
    int r = to_wimg(c, px, v, o);
    if (r == 0) { o->frames = nf; o->delay = dl; }
    return r;
}
int wi_decode_svg(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color)
{
    uint32_t *px = 0; int v[5] = { 0 };
    wi_oom = 0;
    int c = img_call2(SBX_IMG_SVG, d, n, dw, dh, (int)(color & 0xFFFFFF), &px, v, 0, 0);
    return to_wimg(c, px, v, o);
}

int wi_decode_bmp_raw(const uint8_t *d, uint32_t n, struct image *img)
{
    uint32_t *px = 0; int v[5] = { 0 };
    img->pixels = 0; img->width = img->height = 0; img->format = IMAGE_FMT_UNKNOWN;
    int r = img_call(SBX_IMG_BMPRAW, d, n, 0, 0, &px, v);
    if (r != 0) return r;
    img->pixels = px; img->width = (uint32_t)v[0]; img->height = (uint32_t)v[1]; img->format = IMAGE_FMT_BMP;
    return 0;
}

/* PARCHE 139: fotograma MJPEG del reproductor (media.c), misma interfaz que jpeg_decode. */
int jpeg_decode_isolated(const uint8_t *data, size_t len, uint32_t *out, uint32_t out_stride, uint32_t out_max_height, struct jpeg_info *info)
{
    if (!data || !out || len == 0 || len > SBX_IN_MAX || out_stride == 0 || out_stride > JPEG_MAX_DIM) return JPEG_E_SIZE;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) return JPEG_E_SIZE;
    sbx_prepare(c, SBX_IMG_JPEGRAW, data, (uint32_t)len, 0);
    c->in.arg[0] = (int32_t)out_stride; c->in.arg[1] = (int32_t)(out_max_height > JPEG_MAX_DIM ? JPEG_MAX_DIM : out_max_height);
    int r = sbx_svc_call(SBX_SVC_IMG, c, 5000), jr = JPEG_E_DATA;
    if (r == 0) {
        jr = c->out.val[0];
        uint32_t w = (uint32_t)c->out.val[1], h = (uint32_t)c->out.val[2];
        if (jr == JPEG_OK) {
            if (w == 0 || w > out_stride || h == 0 || h > out_max_height || c->out.data_len != (uint64_t)out_stride * h * 4u ||
                sbx_copy_data(c, out, c->out.data_len) != 0) jr = JPEG_E_DATA;
        } else if (jr > 0 || jr < JPEG_E_SIZE) jr = JPEG_E_DATA;              /* codigo inventado por el servicio */
        if (info) { info->width = w; info->height = h; info->ncomp = (uint8_t)c->out.val[3]; info->hmax = (uint8_t)c->out.val[4];
                    info->vmax = (uint8_t)c->out.val[5]; info->restart_interval = (uint16_t)c->out.val[6]; }
    }
    sbx_release(c);
    kfree(c);
    return jr;
}

#else   /* aarch64: sin aislamiento todavia */

int jpeg_decode_isolated(const uint8_t *data, size_t len, uint32_t *out, uint32_t out_stride, uint32_t out_max_height, struct jpeg_info *info)
{
    return jpeg_decode(data, len, out, out_stride, out_max_height, info);
}

int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o) { return wi_decode_local(d, n, o); }
int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh) { return wi_decode_fit_local(d, n, o, maxw, maxh); }
int wi_decode_bmp_raw(const uint8_t *d, uint32_t n, struct image *img) { return bmp_decode(d, n, img); }
int wi_decode_anim(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels) { return wi_decode_anim_local(d, n, o, max_pixels); }   /* SOMA_180 */
int wi_decode_svg(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color) { return wi_decode_svg_local(d, n, o, dw, dh, color); }

#endif
