#ifndef SOMA_WEB_SCROLL_149_H
#define SOMA_WEB_SCROLL_149_H
/* SOMA 149: planificador puro del desplazamiento de una instantanea raster.
 * Entradas enteras, aritmetica de 64 bits: nunca usar abs(INT_MIN). */
#include <stdint.h>
struct soma149_scroll_plan {
    int src_row, dst_row, copy_rows, damage_row, damage_rows;
};
static inline int soma149_make_scroll_plan(int h, int old_scroll, int new_scroll,
                                            struct soma149_scroll_plan *o)
{
    if (!o || h <= 1) return 0;
    int64_t delta = (int64_t)new_scroll - (int64_t)old_scroll;
    if (!delta || delta >= h || delta <= -((int64_t)h)) return 0;
    int d = (int)delta;
    if (d > 0) {
        o->src_row = d; o->dst_row = 0;
        o->copy_rows = h - d; o->damage_row = h - d; o->damage_rows = d;
    } else {
        o->src_row = 0; o->dst_row = -d;
        o->copy_rows = h + d; o->damage_row = 0; o->damage_rows = -d;
    }
    return 1;
}
#endif
