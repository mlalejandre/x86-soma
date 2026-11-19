#ifndef SOMA_WEB_REGEX_H
#define SOMA_WEB_REGEX_H
/* Motor de expresiones regulares tipo ECMAScript (retroceso) sobre UTF-8. */
#include <stdint.h>
#include "web_util.h"
#define WRE_I 1
#define WRE_G 2
#define WRE_M 4
#define WRE_S 8
#define WRE_U 16
#define WRE_Y 32
#define WRE_MAXG 255                     /* grupos de captura maximos */
struct wre;
/* NULL si hay error (mensaje en err). El programa vive en el arena. */
struct wre *wre_compile(struct wa_arena *ar, const char *pat, uint32_t n, int flags, char *err, uint32_t errmax);
int  wre_ngroups(const struct wre *re);
const char *wre_group_name(const struct wre *re, int i);        /* NULL si el grupo no tiene nombre */
int  wre_flags(const struct wre *re);
/* caps debe tener 2*(ngroups+1) elementos; valores en bytes, -1 = grupo sin participar.
   Devuelve 1 coincide, 0 no, -1 presupuesto de pasos agotado. wre_exec busca desde start
   (si el flag Y esta activo solo prueba en start); wre_match_at prueba solo en pos. */
int  wre_exec(const struct wre *re, const char *s, uint32_t len, uint32_t start, int32_t *caps);
int  wre_match_at(const struct wre *re, const char *s, uint32_t len, uint32_t pos, int32_t *caps);
/* limite inferior de pila (opcional): si la pila baja de aqui el emparejamiento aborta */
extern const char *wre_stack_lo;
#endif
