#ifndef SOMA_WEB_RASTER_150_H
#define SOMA_WEB_RASTER_150_H
/* SOMA 150: calculos puros para optimizar raster y evitar doble reflow.
 * Ninguna dependencia del kernel ni memoria dinamica. */
#include <stdint.h>

#ifndef SOMA_WEB_IMAGE_FASTPATH
#define SOMA_WEB_IMAGE_FASTPATH 1
#endif
#ifndef SOMA_WEB_IMAGES_PER_TICK
#define SOMA_WEB_IMAGES_PER_TICK 1
#endif
#if SOMA_WEB_IMAGES_PER_TICK < 1 || SOMA_WEB_IMAGES_PER_TICK > 2
#error "SOMA_WEB_IMAGES_PER_TICK debe ser 1 o 2"
#endif

/* Componer pixeles exactamente en la misma proporcion: ni redondeo ni alpha extra. */
static inline int soma150_direct_image(int outw, int outh, int pixw, int pixh,
                                       int radius, int alpha)
{
    return outw > 0 && outh > 0 && pixw == outw && pixh == outh &&
           radius <= 0 && alpha >= 255;
}
/* Bounded scratch: max 512 KiB, salvo si una sola fila supera el limite.
 * Con 128 filas maximas se reducen los rectangulos de dano del framebuffer. */
static inline int soma150_tile_rows(int visible_w, int visible_h)
{
    if (visible_w <= 0 || visible_h <= 0) return 0;
    uint64_t row_bytes = (uint64_t)(uint32_t)visible_w * 4u;
    uint64_t n = (512u * 1024u) / row_bytes;
    if (!n) n = 1;
    if (n > 128) n = 128;
    if (n > (uint32_t)visible_h) n = (uint32_t)visible_h;
    return (int)n;
}
struct soma150_image_progress { int reflow_now, reflow_finish, finish; };
/* La maquetacion final nunca se repite despues de la maquetacion inmediata. */
static inline struct soma150_image_progress soma150_progress(int did, int pending,
                                int dirty, uint64_t elapsed_ms, uint32_t layout_ms)
{
    struct soma150_image_progress r;
    uint64_t threshold = 1500ull + (uint64_t)layout_ms * 8ull;
    r.finish = (pending == 0 || !did);
    r.reflow_now = did && (pending == 0 || elapsed_ms > threshold);
    r.reflow_finish = r.finish && dirty && !r.reflow_now;
    return r;
}
#endif
