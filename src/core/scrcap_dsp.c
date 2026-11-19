/* SOMA_179_T480S: piezas de la captura de pantalla sin dependencias del kernel (se prueban en host). Ver scrcap.h */
#include <stdint.h>
#include "scrcap.h"

#define OUT_RATE 48000u

void scrcap_rs_reset(struct scrcap_rs *r, uint32_t rate)
{
    r->rate = rate; r->opos = 0; r->ic = 0; r->last = 0;
    r->step = rate ? ((uint64_t)rate << 16) / OUT_RATE : 65536u;
    if (!r->step) r->step = 1;
}

uint32_t scrcap_rs_run(struct scrcap_rs *r, const int16_t *st, uint32_t frames, int16_t *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t m = ((int32_t)st[2u * i] + (int32_t)st[2u * i + 1u]) / 2;
        uint64_t here = r->ic << 16;
        while (r->opos <= here) {
            if (n >= cap) { r->last = m; r->ic++; return n; }       /* sin sitio: se descarta el resto (no deberia pasar) */
            int64_t t = (int64_t)(r->opos + 65536u) - (int64_t)here;  /* 0..65536 desde la muestra anterior */
            if (t < 0) { t = 0; }
            if (t > 65536) { t = 65536; }
            int32_t v = r->last + (int32_t)(((int64_t)(m - r->last) * t) >> 16);
            out[n++] = (int16_t)v;
            r->opos += r->step;
        }
        r->last = m; r->ic++;
    }
    return n;
}

void scrcap_box(const uint32_t *src, uint32_t stride, uint32_t ow, uint32_t oh, uint32_t div, uint32_t *dst)
{
    if (div <= 1) {
        for (uint32_t y = 0; y < oh; y++) for (uint32_t x = 0; x < ow; x++) dst[(uint64_t)y * ow + x] = src[(uint64_t)y * stride + x] & 0x00FFFFFFu;
        return;
    }
    uint32_t n = div * div;
    for (uint32_t y = 0; y < oh; y++) {
        for (uint32_t x = 0; x < ow; x++) {
            uint32_t r = 0, g = 0, b = 0;
            for (uint32_t j = 0; j < div; j++) {
                const uint32_t *p = src + (uint64_t)(y * div + j) * stride + (uint64_t)x * div;
                for (uint32_t i = 0; i < div; i++) { uint32_t c = p[i]; r += (c >> 16) & 255u; g += (c >> 8) & 255u; b += c & 255u; }
            }
            dst[(uint64_t)y * ow + x] = ((r + n / 2) / n) << 16 | ((g + n / 2) / n) << 8 | ((b + n / 2) / n);
        }
    }
}
