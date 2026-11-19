/* PARCHE 138: decodificador BMP separado de image.c (sin dependencias de pantalla) para poder ejecutarlo aislado en ring 3 (imgdec). */
#include "image.h"
#include "mem.h"

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

int bmp_decode(const uint8_t *data, size_t size, struct image *img)
{
    if (size < 54) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -1;

    uint32_t offset = read_le32(data + 10);
    uint32_t dib_size = read_le32(data + 14);
    int32_t  w_raw = read_le32s(data + 18);
    int32_t  h_raw = read_le32s(data + 22);
    uint16_t planes = read_le16(data + 26);
    uint16_t bpp = read_le16(data + 28);
    uint32_t clr_used = read_le32(data + 46);

    if (planes != 1) return -1;
    if (w_raw <= 0 || w_raw > 2560) return -1;
    if (h_raw == 0 || h_raw < -2560 || h_raw > 2560) return -1;

    uint32_t width = (uint32_t)w_raw;
    int top_down = 0;
    uint32_t height = 0;
    if (h_raw < 0) {
        top_down = 1;
        height = (uint32_t)(-h_raw);
    } else {
        top_down = 0;
        height = (uint32_t)h_raw;
    }

    if (offset >= size) return -1;

    size_t pixel_bytes = (size_t)width * height * sizeof(uint32_t);
    img->pixels = (uint32_t *)kmalloc(pixel_bytes);
    if (!img->pixels) return -2;

    img->width = width;
    img->height = height;
    img->format = IMAGE_FMT_BMP;

    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xFF000000;   /* PARCHE 138: antes, indices fuera de la paleta leian la pila sin inicializar */
    if (bpp <= 8) {
        uint32_t num_colors = clr_used ? clr_used : (1U << bpp);
        if (num_colors > 256) num_colors = 256;
        size_t pal_offset = 14 + dib_size;
        for (uint32_t i = 0; i < num_colors; ++i) {
            if (pal_offset + i * 4 + 4 <= size) {
                const uint8_t *e = data + pal_offset + i * 4;
                palette[i] = 0xFF000000 | ((uint32_t)e[2] << 16) | ((uint32_t)e[1] << 8) | e[0];
            } else {
                palette[i] = 0xFF000000;
            }
        }
    }

    if (bpp == 32) {
        uint32_t stride = width * 4;
        int has_alpha = 0;
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            uint32_t *drow = img->pixels + dst_y * width;
            size_t row_off = offset + (size_t)y * stride;
            if (row_off + stride > size) break;
            const uint8_t *srow = data + row_off;

            for (uint32_t x = 0; x < width; ++x) {
                uint8_t b = srow[x * 4 + 0];
                uint8_t g = srow[x * 4 + 1];
                uint8_t r = srow[x * 4 + 2];
                uint8_t a = srow[x * 4 + 3];
                if (a != 0) has_alpha = 1;
                drow[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        if (!has_alpha) {
            for (uint32_t i = 0; i < width * height; ++i) {
                img->pixels[i] |= 0xFF000000;
            }
        }
    } else if (bpp == 24) {
        uint32_t stride = ((width * 3 + 3) / 4) * 4;
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            uint32_t *drow = img->pixels + dst_y * width;
            size_t row_off = offset + (size_t)y * stride;
            if (row_off + width * 3 > size) break;
            const uint8_t *srow = data + row_off;

            for (uint32_t x = 0; x < width; ++x) {
                uint8_t b = srow[x * 3 + 0];
                uint8_t g = srow[x * 3 + 1];
                uint8_t r = srow[x * 3 + 2];
                drow[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    } else if (bpp == 16) {
        uint32_t stride = ((width * 2 + 3) / 4) * 4;
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            uint32_t *drow = img->pixels + dst_y * width;
            size_t row_off = offset + (size_t)y * stride;
            if (row_off + width * 2 > size) break;
            const uint8_t *srow = data + row_off;

            for (uint32_t x = 0; x < width; ++x) {
                uint16_t v = read_le16(srow + x * 2);
                uint32_t r = (((v >> 10) & 0x1F) * 255) / 31;
                uint32_t g = (((v >> 5)  & 0x1F) * 255) / 31;
                uint32_t b = ((v & 0x1F) * 255) / 31;
                drow[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    } else if (bpp == 8) {
        uint32_t stride = ((width + 3) / 4) * 4;
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            uint32_t *drow = img->pixels + dst_y * width;
            size_t row_off = offset + (size_t)y * stride;
            if (row_off + width > size) break;
            const uint8_t *srow = data + row_off;

            for (uint32_t x = 0; x < width; ++x) {
                drow[x] = palette[srow[x]];
            }
        }
    } else {
        kfree(img->pixels); img->pixels = 0; img->width = img->height = 0;
        return -1;
    }

    return 0;
}

