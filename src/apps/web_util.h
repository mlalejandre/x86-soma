#ifndef SOMA_WEB_UTIL_H
#define SOMA_WEB_UTIL_H
/* SOMA_PARCHE_NAVEGADOR: utilidades comunes del motor web (sin libc, sin FPU). Todo static inline. */
#include <stdint.h>
#include <stddef.h>
#include "mem.h"

/* callbacks con firma fija y utilidades opcionales: no avisar de parametros/funciones sin uso */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#define WU_UNUSED __attribute__((unused))
#define WU_FN static inline WU_UNUSED

WU_FN uint32_t wu_strlen(const char *s) { uint32_t n = 0; if (!s) return 0; while (s[n]) n++; return n; }
WU_FN int wu_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
WU_FN int wu_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
WU_FN int wu_isdigit(int c) { return c >= '0' && c <= '9'; }
WU_FN int wu_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
WU_FN int wu_isalnum(int c) { return wu_isalpha(c) || wu_isdigit(c); }
WU_FN int wu_ishex(int c) { return wu_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
WU_FN int wu_hexval(int c) { return wu_isdigit(c) ? c - '0' : (wu_lower(c) - 'a' + 10); }
WU_FN const char *wu_strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return s; if (!*s) return 0; } }
WU_FN int wu_streq(const char *a, const char *b) { if (!a || !b) return a == b; while (*a && *a == *b) { a++; b++; } return *a == *b; }
WU_FN int wu_strieq(const char *a, const char *b) { if (!a || !b) return a == b; while (*a && wu_lower(*a) == wu_lower(*b)) { a++; b++; } return wu_lower(*a) == wu_lower(*b); }
WU_FN int wu_strneq(const char *a, const char *b, uint32_t n) { for (uint32_t i = 0; i < n; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; } return 1; }
WU_FN int wu_strnieq(const char *a, const char *b, uint32_t n) { for (uint32_t i = 0; i < n; i++) { if (wu_lower(a[i]) != wu_lower(b[i])) return 0; if (!a[i]) return 1; } return 1; }
WU_FN int wu_starts(const char *s, const char *pre) { while (*pre) { if (*s++ != *pre++) return 0; } return 1; }
WU_FN int wu_startsi(const char *s, const char *pre) { while (*pre) { if (wu_lower(*s++) != wu_lower(*pre++)) return 0; } return 1; }
WU_FN const char *wu_find(const char *h, const char *n) {
    if (!h || !n) return 0;
    if (!*n) return h;
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return h; }
    return 0;
}
WU_FN const char *wu_findi(const char *h, const char *n) {
    if (!h || !n) return 0;
    if (!*n) return h;
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b && wu_lower(*a) == wu_lower(*b)) { a++; b++; } if (!*b) return h; }
    return 0;
}
/* Copia acotada: siempre termina en NUL. Devuelve longitud copiada. */
WU_FN uint32_t wu_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0; if (!max) return 0;
    while (src && src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = 0; return i;
}
WU_FN uint32_t wu_copyn(char *dst, const char *src, uint32_t n, uint32_t max) {
    uint32_t i = 0; if (!max) return 0;
    while (i < n && src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = 0; return i;
}
WU_FN int wu_atoi(const char *s) {
    int neg = 0, v = 0; while (wu_isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (wu_isdigit(*s)) { if (v < 100000000) v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}
/* Entero en texto decimal; devuelve longitud (sin NUL final si no cabe). */
WU_FN int wu_itoa(int64_t v, char *out) {
    char t[24]; int n = 0, k = 0; uint64_t u;
    if (v < 0) { out[k++] = '-'; u = (uint64_t)0 - (uint64_t)v; } else u = (uint64_t)v;
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n) out[k++] = t[--n];
    out[k] = 0; return k;
}

/* ---- UTF-8 ---- */
WU_FN uint32_t wu_utf8_next(const char *s, uint32_t *adv) {
    const uint8_t *p = (const uint8_t *)s; uint32_t c = p[0];
    if (c < 0x80) { *adv = 1; return c; }
    if (c >= 0xC2 && c < 0xE0 && (p[1] & 0xC0) == 0x80) { *adv = 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if (c >= 0xE0 && c < 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { *adv = 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); }
    if (c >= 0xF0 && c < 0xF5 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) { *adv = 4; return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); }
    *adv = 1; return 0xFFFD;
}
WU_FN int wu_utf8_put(uint32_t c, char *o) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 0x3F)); o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F)); return 4;
}

/* ---- hash FNV-1a ---- */
WU_FN uint32_t wu_hash(const char *s, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; } return h; }

/* ---- Arena: asignacion por bloques; wa_free_all libera todo de golpe ---- */
struct wa_chunk { struct wa_chunk *next; uint32_t size, used; };
struct wa_arena { struct wa_chunk *head; uint32_t total, limit, fail; };
#define WA_CHUNK 32768u
WU_FN void *wa_alloc(struct wa_arena *a, uint32_t n) {
    n = (n + 7u) & ~7u;
    if (a->fail) return 0;
    if (a->head && a->head->used + n <= a->head->size) { void *p = (char *)(a->head + 1) + a->head->used; a->head->used += n; return p; }
    uint32_t cs = n + sizeof(struct wa_chunk) > WA_CHUNK ? n + (uint32_t)sizeof(struct wa_chunk) : WA_CHUNK;
    if (a->limit && a->total + cs > a->limit) { a->fail = 1; return 0; }
    struct wa_chunk *c = (struct wa_chunk *)kmalloc(cs);
    if (!c) { a->fail = 1; return 0; }
    c->size = cs - (uint32_t)sizeof(struct wa_chunk); c->used = n; c->next = a->head; a->head = c; a->total += cs;
    memset(c + 1, 0, n);
    return (void *)(c + 1);
}
WU_FN void *wa_zalloc(struct wa_arena *a, uint32_t n) { void *p = wa_alloc(a, n); if (p) memset(p, 0, n); return p; }
WU_FN void wa_free_all(struct wa_arena *a) { struct wa_chunk *c = a->head; while (c) { struct wa_chunk *n = c->next; kfree(c); c = n; } a->head = 0; a->total = 0; a->fail = 0; }
WU_FN char *wa_strndup(struct wa_arena *a, const char *s, uint32_t n) { char *p = (char *)wa_alloc(a, n + 1); if (!p) return 0; memcpy(p, s, n); p[n] = 0; return p; }
WU_FN char *wa_strdup(struct wa_arena *a, const char *s) { return wa_strndup(a, s ? s : "", wu_strlen(s)); }

/* ---- Buffer creciente (heap) ---- */
struct wb { char *p; uint32_t len, cap; int err; };
/* SOMA_188: memoria del navegador segun el montón del nucleo (T480s 16 GiB: 7,8 GiB de montón). 1 = equipo con >= 2 GiB de montón */
#if defined(__x86_64__) && !defined(WEB_HOST_TEST) && !defined(SOMA_HOST_TEST)
size_t kheap_max_bytes(void);
/* SOMA 189: "memoria grande" desde 960 MiB de monton (equipos con 2 GiB de RAM o mas). Antes pedia 2 GiB justos y un QEMU
 * de 4 GiB (monton de 2047 MiB) se quedaba fuera: 20 MiB para imagenes y "sin memoria para mas imagenes" en los periodicos. */
static inline int web_big_mem(void) { return kheap_max_bytes() >= ((size_t)960 << 20); }
/* SOMA 188B: tope del monton JS = 1/4 del monton del kernel, entre 24 MiB y 1536 MiB (crece bajo demanda). El interprete no
 * tiene recolector de basura y la portada de Google gasta 260 MiB: con 1/6 y tope de 512 se quedaba corto en paginas grandes,
 * y los equipos con menos de 2 GiB de monton tenian 24 MiB fijos (QEMU con 512 MB: "memoria del script agotada"). */
/* SOMA 189: memoria para imagenes decodificadas = 1/8 del monton, entre 20 y 192 MiB */
static inline uint32_t web_img_budget(void) { size_t m = kheap_max_bytes() / 8; if (m < ((size_t)20 << 20)) m = (size_t)20 << 20; if (m > ((size_t)192 << 20)) m = (size_t)192 << 20; return (uint32_t)m; }
static inline uint32_t web_js_heap_big(void) { size_t m = kheap_max_bytes() / 4; if (m < ((size_t)24 << 20)) m = (size_t)24 << 20; if (m > ((size_t)1536 << 20)) m = (size_t)1536 << 20; return (uint32_t)m; }
#else
static inline int web_big_mem(void) { return 1; }
static inline uint32_t web_js_heap_big(void) { return 384u << 20; }
static inline uint32_t web_img_budget(void) { return 192u << 20; }
#endif
WU_FN int wb_reserve(struct wb *b, uint32_t extra) {
    if (b->err) return 0;
    /* SOMA_201: en 64 bits. Con 32, len + extra + 1 daba la vuelta: un `extra` enorme "cabia" (y wb_addn copiaba 4 GiB) o el
     * bucle de duplicar no terminaba nunca. Ningun bufer del navegador puede pasar de 2 GiB. */
    uint64_t need = (uint64_t)b->len + extra + 1;
    if (need > 0x7FFFFFFFu) { b->err = 1; return 0; }
    if (need <= b->cap) return 1;
    uint32_t nc = b->cap ? b->cap : 256; while (nc < need) nc *= 2;
    char *np = (char *)kmalloc(nc); if (!np) { b->err = 1; return 0; }
    if (b->p) { memcpy(np, b->p, b->len); kfree(b->p); }
    b->p = np; b->cap = nc; return 1;
}
WU_FN void wb_addn(struct wb *b, const char *s, uint32_t n) { if (!wb_reserve(b, n)) return; memcpy(b->p + b->len, s, n); b->len += n; b->p[b->len] = 0; }
WU_FN void wb_add(struct wb *b, const char *s) { wb_addn(b, s, wu_strlen(s)); }
WU_FN void wb_addc(struct wb *b, char c) { wb_addn(b, &c, 1); }
WU_FN void wb_addint(struct wb *b, int64_t v) { char t[24]; int n = wu_itoa(v, t); wb_addn(b, t, (uint32_t)n); }
WU_FN void wb_free(struct wb *b) { if (b->p) kfree(b->p); b->p = 0; b->len = b->cap = 0; b->err = 0; }
WU_FN void wb_clear(struct wb *b) { b->len = 0; if (b->p) b->p[0] = 0; }

/* Cesion cooperativa periodica para bucles largos (el planificador del kernel no es expulsivo) */
/* Reloj monotono en ms (lo implementa browser.c; en pruebas de host, el arnes) */
uint64_t web_now_ms(void);
/* Cede la CPU un instante (kernel: thread_sleep/yield; host: nada) */
void web_yield(void);
static unsigned wu_ycnt WU_UNUSED;
static uint64_t wu_ylast WU_UNUSED;
/* SOMA_PARCHE_028_UI: se cede por TIEMPO (cada ~4 ms de trabajo), no por numero de pasos: asi la GUI sigue fluida
 * aunque cada paso cueste mucho (maquetar una pagina grande, decodificar una imagen...) */
WU_FN void wu_tick(void) { if ((++wu_ycnt & 3u) == 0) { uint64_t t = web_now_ms(); if (t - wu_ylast >= 4) { wu_ylast = t; web_yield(); } } }
/* Margen de pila restante del hilo actual en bytes (kernel: calculado; host: grande) */
uint32_t web_stack_left(void);

/* SOMA_190: pagina /sorry de Google (reCAPTCHA) -> URL de busqueda equivalente en DuckDuckGo HTML */
WU_FN int wu_pct_val190(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
WU_FN int wu_google_sorry_alt(const char *u, int status, char *out, uint32_t max)
{
    if (!wu_startsi(u, "https://www.google.") && !wu_startsi(u, "https://google.")) return 0;
    const char *path = u + 8; while (*path && *path != '/') path++;
    char dec[1024]; const char *src = 0;
    if (wu_startsi(path, "/sorry/")) {
        const char *c = wu_find(path, "continue=");
        if (!c) return 0;
        c += 9; uint32_t k = 0;                                   /* el valor de continue= va codificado: se decodifica una vez */
        while (*c && *c != '&' && *c != '#' && k < sizeof dec - 1) {
            if (*c == '%' && wu_pct_val190(c[1]) >= 0 && wu_pct_val190(c[2]) >= 0) { dec[k++] = (char)(wu_pct_val190(c[1]) * 16 + wu_pct_val190(c[2])); c += 3; }
            else { dec[k++] = *c == '+' ? ' ' : *c; c++; }
        }
        dec[k] = 0; src = dec;
    } else if (status == 429 && wu_startsi(path, "/search")) src = u;
    else return 0;
    const char *q = 0;
    for (const char *c = src; *c; c++) if ((c[0] == '?' || c[0] == '&') && c[1] == 'q' && c[2] == '=') { q = c + 3; break; }
    if (!q || !*q || *q == '&') return 0;
    struct wb b = {0}; wb_add(&b, "https://html.duckduckgo.com/html/?q=");
    for (; *q && *q != '&' && *q != '#'; q++) wb_addc(&b, *q);
    wu_copy(out, b.p ? b.p : "", max); wb_free(&b);
    return out[0] != 0;
}

#endif
