#include "image.h"
#include "fb.h"
#include "mem.h"
#include "web_img.h"     /* SOMA_PARCHE_028_UI: PNG, JPEG y GIF con los decodificadores del navegador */

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int32_t read_le32s(const uint8_t *p)
{
    return (int32_t)read_le32(p);
}

void image_free(struct image *img)
{
    if (!img) return;
    if (img->pixels) {
        kfree(img->pixels);
        img->pixels = 0;
    }
    img->width = 0;
    img->height = 0;
    img->format = IMAGE_FMT_UNKNOWN;
}

int image_decode(const uint8_t *data, size_t size, struct image *img)
{
    if (!data || size < 4 || !img) return -1;
    img->pixels = 0;
    img->width = 0;
    img->height = 0;
    img->format = IMAGE_FMT_UNKNOWN;

    if (data[0] == 'B' && data[1] == 'M')
        return size < 0xFFFFFFFFu ? wi_decode_bmp_raw(data, (uint32_t)size, img) : -1;   /* PARCHE 138: aislado en x86_64 */
    {   /* SOMA_PARCHE_028_UI: el visor de imagenes y el explorador abren tambien PNG, JPEG y GIF; la transparencia se
         * compone sobre un gris oscuro (el fondo del visor) */
        struct wimg w;
        if (size < 0xFFFFFFFFu && wi_decode(data, (uint32_t)size, &w) == 0 && w.px && w.w > 0 && w.h > 0) {
            uint32_t n = (uint32_t)w.w * (uint32_t)w.h;
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t c = w.px[i], a = c >> 24;
                if (a == 255) { w.px[i] = c & 0x00FFFFFFu; continue; }
                uint32_t r = (((c >> 16) & 255) * a + 0x1E * (255 - a)) / 255, g = (((c >> 8) & 255) * a + 0x24 * (255 - a)) / 255, b = ((c & 255) * a + 0x30 * (255 - a)) / 255;
                w.px[i] = (r << 16) | (g << 8) | b;
            }
            img->pixels = w.px; img->width = (uint32_t)w.w; img->height = (uint32_t)w.h; img->format = IMAGE_FMT_BMP;
            return 0;
        }
    }
    return -1;
}

void image_render(const struct image *img, int32_t dx, int32_t dy, int32_t dw, int32_t dh)
{
    if (!img || !img->pixels || dw <= 0 || dh <= 0 || img->width == 0 || img->height == 0) return;
    fb_damage(dx, dy, dw, dh);
    volatile uint32_t *target = fb_get_buffer();
    if (!target) return;

    int32_t screen_w = (int32_t)fb_get_width();
    int32_t screen_h = (int32_t)fb_get_height();

    int32_t cx0 = 0, cy0 = 0, cx1 = screen_w, cy1 = screen_h;
    fb_get_clip(&cx0, &cy0, &cx1, &cy1);

    uint32_t step_x = ((uint32_t)img->width << 16) / (uint32_t)dw;
    uint32_t step_y = ((uint32_t)img->height << 16) / (uint32_t)dh;

    for (int32_t r = 0; r < dh; ++r) {
        int32_t py = dy + r;
        if (py < cy0 || py >= cy1) continue;

        uint32_t sy = ((uint32_t)r * step_y) >> 16;
        if (sy >= img->height) sy = img->height - 1;

        const uint32_t *src_row = img->pixels + sy * img->width;
        volatile uint32_t *dst_row = target + py * screen_w;

        uint32_t curr_x = 0;
        for (int32_t c = 0; c < dw; ++c) {
            int32_t px = dx + c;
            if (px >= cx0 && px < cx1) {
                uint32_t sx = curr_x >> 16;
                if (sx >= img->width) sx = img->width - 1;
                uint32_t pixel = src_row[sx];
                uint32_t alpha = (pixel >> 24) & 0xFF;
                if (alpha == 255) {
                    dst_row[px] = pixel;
                } else if (alpha > 0) {
                    dst_row[px] = fb_blend_color(dst_row[px], pixel);
                }
            }
            curr_x += step_x;
        }
    }
}

void image_render_alpha(const struct image *img, int32_t dx, int32_t dy, int32_t dw, int32_t dh)
{
    image_render(img, dx, dy, dw, dh);
}

int image_create_demo_bmp(uint8_t *out, size_t max_size, uint32_t w, uint32_t h)
{
    if (w != 80 || h != 40) { w = 80; h = 40; }
    uint32_t stride = ((w * 3 + 3) / 4) * 4;
    size_t total_size = 54 + (size_t)h * stride;
    if (max_size < total_size) return 0;

    memset(out, 0, total_size);

    out[0] = 'B'; out[1] = 'M';
    out[2] = (uint8_t)(total_size & 0xFF);
    out[3] = (uint8_t)((total_size >> 8) & 0xFF);
    out[4] = (uint8_t)((total_size >> 16) & 0xFF);
    out[5] = (uint8_t)((total_size >> 24) & 0xFF);
    out[10] = 54;
    out[14] = 40;
    out[18] = (uint8_t)(w & 0xFF);
    out[19] = (uint8_t)((w >> 8) & 0xFF);
    out[22] = (uint8_t)(h & 0xFF);
    out[23] = (uint8_t)((h >> 8) & 0xFF);
    out[26] = 1;
    out[28] = 24;
    uint32_t img_bytes = h * stride;
    out[34] = (uint8_t)(img_bytes & 0xFF);
    out[35] = (uint8_t)((img_bytes >> 8) & 0xFF);

    uint8_t *pix = out + 54;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *row = pix + y * stride;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = (uint8_t)(10 + (x * 40) / w);
            uint8_t g = (uint8_t)(20 + (y * 90) / h);
            uint8_t b = (uint8_t)(80 + ((x + y) * 160) / (w + h));

            if (x < 2 || x >= w - 2 || y < 2 || y >= h - 2) {
                r = 0x38; g = 0xBD; b = 0xF8;
            } else if (x >= 20 && x <= 60 && y >= 14 && y <= 26) {
                r = 0xF5; g = 0x9E; b = 0x0B;
            } else if ((x == 30 || x == 50) && (y >= 6 && y <= 34)) {
                r = 0x10; g = 0xB9; b = 0x81;
            }

            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    return (int)total_size;
}

int image_create_chip_bmp(uint8_t *out, size_t max_size, uint32_t w, uint32_t h)
{
    if (w != 64 || h != 32) { w = 64; h = 32; }
    uint32_t stride = ((w * 3 + 3) / 4) * 4;
    size_t total_size = 54 + (size_t)h * stride;
    if (max_size < total_size) return 0;

    memset(out, 0, total_size);

    out[0] = 'B'; out[1] = 'M';
    out[2] = (uint8_t)(total_size & 0xFF);
    out[3] = (uint8_t)((total_size >> 8) & 0xFF);
    out[4] = (uint8_t)((total_size >> 16) & 0xFF);
    out[5] = (uint8_t)((total_size >> 24) & 0xFF);
    out[10] = 54;
    out[14] = 40;
    out[18] = (uint8_t)(w & 0xFF);
    out[19] = (uint8_t)((w >> 8) & 0xFF);
    out[22] = (uint8_t)(h & 0xFF);
    out[23] = (uint8_t)((h >> 8) & 0xFF);
    out[26] = 1;
    out[28] = 24;
    uint32_t img_bytes = h * stride;
    out[34] = (uint8_t)(img_bytes & 0xFF);
    out[35] = (uint8_t)((img_bytes >> 8) & 0xFF);

    uint8_t *pix = out + 54;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *row = pix + y * stride;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = 0x0F, g = 0x17, b = 0x2A;

            /* Pines dorados superior e inferior */
            if ((y < 4 || y >= h - 4) && (x % 6 < 4)) {
                r = 0xF5; g = 0x9E; b = 0x0B;
            } else if (x >= 8 && x < w - 8 && y >= 6 && y < h - 6) {
                /* Núcleo de silicio */
                r = 0x06; g = 0x4E; b = 0x3B;
                if (x >= 24 && x < 40 && y >= 12 && y < 20) {
                    r = 0x34; g = 0xD3; b = 0x99;
                }
            }

            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    return (int)total_size;
}
