/* SOMA_273_ARCHIVOS: edicion de una linea de texto para los dialogos del explorador (renombrar, nueva carpeta...).
 * Puro (sin libc ni kernel) para poder probarlo en el host. Cursor, seleccion, Supr/Retroceso, Inicio/Fin y UTF-8 (una tilde o una n con virgulilla
 * se borran enteras). La seleccion es el tramo [s0, s1) del texto; al teclear se sustituye. */
#ifndef FM_EDIT_H
#define FM_EDIT_H
#include <stdint.h>

struct fm_ed { char *t; uint32_t cap, len, caret, s0, s1; };

static inline void fm_ed_set(struct fm_ed *e, char *buf, uint32_t cap, const char *txt)
{
    e->t = buf; e->cap = cap; e->len = 0;
    while (txt && txt[e->len] && e->len + 1 < cap) { buf[e->len] = txt[e->len]; ++e->len; }
    buf[e->len] = 0; e->caret = e->len; e->s0 = e->s1 = 0;
}
static inline int fm_ed_has_sel(const struct fm_ed *e) { return e->s1 > e->s0; }
static inline int fm_ed_cont(char c) { return ((uint8_t)c & 0xC0) == 0x80; }
static inline void fm_ed_cut(struct fm_ed *e, uint32_t a, uint32_t b)         /* quita [a, b) */
{
    if (b > e->len) b = e->len;
    if (a >= b) return;
    for (uint32_t i = b; i <= e->len; ++i) e->t[i - (b - a)] = e->t[i];
    e->len -= b - a;
    if (e->caret > b) e->caret -= b - a; else if (e->caret > a) e->caret = a;
}
static inline void fm_ed_unsel(struct fm_ed *e) { e->s0 = e->s1 = 0; }
static inline void fm_ed_del_sel(struct fm_ed *e)
{
    if (!fm_ed_has_sel(e)) return;
    uint32_t a = e->s0, b = e->s1; fm_ed_unsel(e); fm_ed_cut(e, a, b); e->caret = a;
}
/* un byte de entrada: 1 si se inserto */
static inline int fm_ed_insert(struct fm_ed *e, char c)
{
    fm_ed_del_sel(e);
    if (e->len + 1 >= e->cap) return 0;
    for (uint32_t i = e->len + 1; i > e->caret; --i) e->t[i] = e->t[i - 1];
    e->t[e->caret++] = c; e->len++;
    return 1;
}
static inline void fm_ed_back(struct fm_ed *e)
{
    if (fm_ed_has_sel(e)) { fm_ed_del_sel(e); return; }
    if (!e->caret) return;
    uint32_t a = e->caret - 1; while (a > 0 && fm_ed_cont(e->t[a])) --a;
    fm_ed_cut(e, a, e->caret); e->caret = a;
}
static inline void fm_ed_supr(struct fm_ed *e)
{
    if (fm_ed_has_sel(e)) { fm_ed_del_sel(e); return; }
    if (e->caret >= e->len) return;
    uint32_t b = e->caret + 1; while (b < e->len && fm_ed_cont(e->t[b])) ++b;
    fm_ed_cut(e, e->caret, b);
}
static inline void fm_ed_left(struct fm_ed *e)
{
    if (fm_ed_has_sel(e)) { e->caret = e->s0; fm_ed_unsel(e); return; }
    if (!e->caret) return;
    --e->caret; while (e->caret > 0 && fm_ed_cont(e->t[e->caret])) --e->caret;
}
static inline void fm_ed_right(struct fm_ed *e)
{
    if (fm_ed_has_sel(e)) { e->caret = e->s1; fm_ed_unsel(e); return; }
    if (e->caret >= e->len) return;
    ++e->caret; while (e->caret < e->len && fm_ed_cont(e->t[e->caret])) ++e->caret;
}
static inline void fm_ed_home(struct fm_ed *e) { fm_ed_unsel(e); e->caret = 0; }
static inline void fm_ed_end(struct fm_ed *e) { fm_ed_unsel(e); e->caret = e->len; }
static inline void fm_ed_select_all(struct fm_ed *e) { e->s0 = 0; e->s1 = e->len; e->caret = e->len; }
/* al renombrar se selecciona el nombre sin la extension (se teclea el nombre nuevo y la extension se conserva) */
static inline void fm_ed_select_base(struct fm_ed *e, int is_dir)
{
    uint32_t dot = e->len;
    if (!is_dir) for (uint32_t i = e->len; i > 1; --i) if (e->t[i - 1] == '.') { dot = i - 1; break; }
    e->s0 = 0; e->s1 = dot; e->caret = dot;
    if (e->s1 == 0) fm_ed_unsel(e);
}
/* caracteres que un nombre no puede llevar (exFAT/FAT y la ruta) */
static inline int fm_ed_char_ok(char c)
{
    uint8_t u = (uint8_t)c;
    if (u < 32 || u == 127) return 0;
    switch (c) { case '/': case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|': return 0; default: return 1; }
}
/* nombre valido: no vacio, sin caracteres prohibidos, sin punto/espacio final, no empieza por punto, no es solo espacios */
static inline int fm_ed_name_ok(const char *n)
{
    if (!n[0] || n[0] == '.') return 0;
    uint32_t len = 0, blank = 1;
    for (; n[len]; ++len) { if (!fm_ed_char_ok(n[len])) return 0; if (n[len] != ' ') blank = 0; }
    if (blank || n[len - 1] == ' ' || n[len - 1] == '.') return 0;
    return 1;
}
/* Mi unidad: el explorador solo entra en /usuario y sus carpetas; sin "..", sin otra raiz */
static inline int fm_ed_ruta_ok(const char *p)
{
    static const char *r = "/usuario", *u = "/usb";            /* SOMA_363_ACCESO: y los medios USB externos */
    if (!p) return 0;
    uint32_t i = 0; for (; r[i]; ++i) if (p[i] != r[i]) break;
    if (r[i]) { for (i = 0; u[i]; ++i) if (p[i] != u[i]) return 0; }
    if (p[i] && p[i] != '/') return 0;
    for (const char *q = p; *q; ++q)
        if (q[0] == '.' && q[1] == '.' && (q == p || q[-1] == '/') && (q[2] == '/' || q[2] == '\0')) return 0;
    return 1;
}
#endif
