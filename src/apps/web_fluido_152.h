#ifndef SOMA_WEB_FLUIDO_152_H
#define SOMA_WEB_FLUIDO_152_H
/* SOMA 152: calculos puros del navegador fluido (sin dependencias del nucleo; se prueban en el anfitrion).
 *  - anillo de la instantanea: la fila i de la vista vive en la fila (r0 + i) % h del buffer;
 *  - desplazamiento suave: cada fotograma avanza una fraccion de lo que falta (curva exponencial, ~100 ms por muesca). */
#include <stdint.h>

/* nuevo origen del anillo tras desplazar la vista d filas (d > 0: el contenido sube). h > 0. */
static inline int soma152_ring_rotate(int r0, int d, int h)
{
    if (h <= 0) return 0;
    int64_t r = ((int64_t)r0 + (int64_t)d) % (int64_t)h;
    if (r < 0) r += h;
    return (int)r;
}
/* fila del buffer que guarda la fila 'row' de la vista */
static inline int soma152_ring_row(int r0, int row, int h)
{
    return soma152_ring_rotate(r0, row, h);
}
/* paso del desplazamiento suave: d = distancia que falta (px), dt = ms desde el paso anterior.
 * Nunca se pasa del objetivo, siempre avanza al menos 1 px y llega exacto. */
static inline int soma152_smooth_step(int d, uint32_t dt)
{
    if (d == 0 || dt == 0) return 0;
    if (dt > 64) dt = 64;
    int step = (int)((int64_t)d * (int64_t)dt / (int64_t)(dt + 30));
    if (step == 0 || (int64_t)d * d <= 4) step = d;
    return step;
}
/* objetivo de la rueda acotado al documento [0, max_sy] */
static inline int soma152_clamp_target(int base, int dy, int max_sy)
{
    int64_t t = (int64_t)base + dy;
    if (t < 0) t = 0;
    if (max_sy < 0) max_sy = 0;
    if (t > max_sy) t = max_sy;
    return (int)t;
}
/* SOMA 152D: lista de bloqueo del usuario (/etc/bloqueo_web.txt). Trocea buf EN SITIO: una entrada por linea, sin
 * espacios a los lados; '#' comenta; "!x" permite x aunque la lista interna lo bloquee. Devuelve cuantas entradas. */
static inline int soma152_blocklist_parse(char *buf, const char **out, int max)
{
    int n = 0; char *c = buf;
    while (c && *c && n < max) {
        char *ls = c; while (*c && *c != '\n') c++;
        char *le = c; if (*c) c++;
        while (ls < le && (*ls == ' ' || *ls == '\t' || *ls == '\r')) ls++;
        while (le > ls && (le[-1] == ' ' || le[-1] == '\t' || le[-1] == '\r')) le--;
        *le = 0;
        if (ls < le && *ls != '#' && !(ls[0] == '!' && le - ls < 2)) out[n++] = ls;
    }
    return n;
}
static inline int soma152_has(const char *h, const char *nd)
{
    if (!*nd) return 0;
    for (; *h; h++) { const char *a = h, *b = nd; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}
/* 1 = bloquear, 0 = permitir, -1 = la lista del usuario no dice nada (decide la interna) */
static inline int soma152_blocklist_verdict(const char *url, const char *const *e, int n)
{
    for (int k = 0; k < n; k++) if (e[k][0] == '!' && soma152_has(url, e[k] + 1)) return 0;
    for (int k = 0; k < n; k++) if (e[k][0] != '!' && soma152_has(url, e[k])) return 1;
    return -1;
}
#endif
