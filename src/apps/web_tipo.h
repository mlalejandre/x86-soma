#ifndef SOMA_WEB_TIPO_H
#define SOMA_WEB_TIPO_H
/* SOMA_289_VISOR: que documentos abre el navegador en su visor (PDF y LaTeX) en vez de como pagina web.
 * Logica pura (sin memoria ni E/S): la usan el motor web (web_page.c, dentro del nucleo y en el proceso aislado), el navegador
 * (browser.c) y las pruebas del anfitrion (tests/test_visor_289.c).
 *
 * El motor no dibuja estos documentos: al navegar a uno crea una pagina "marcador" cuyo titulo empieza por un espacio de anchura
 * cero y "PDF " o "TEX " seguido del nombre. El navegador ve ese titulo (tambien con el motor aislado, que solo le pasa el estado)
 * y pone el visor encima de la pagina: el historial, Atras/Adelante, Recargar y las pestanas siguen funcionando como siempre. */
#include <stdint.h>

enum { WT_NADA = 0, WT_PDF = 1, WT_TEX = 2 };
#define WT_MARCA "\342\200\213"                   /* U+200B, espacio de anchura cero (3 bytes) */

static inline int wt_low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int wt_pre_ci(const char *s, const char *p) { while (*p) { if (wt_low((unsigned char)*s) != (unsigned char)*p) return 0; s++; p++; } return 1; }
static inline uint32_t wt_len(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static inline int wt_hex(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

/* longitud de la ruta de una URL (sin ?consulta ni #fragmento) */
static inline uint32_t wt_ruta_n(const char *url)
{
    uint32_t n = 0; while (url[n] && url[n] != '?' && url[n] != '#') n++;
    return n;
}
static inline int wt_ext(const char *url, const char *ext)          /* ext en minusculas, con el punto */
{
    if (!url) return 0;
    uint32_t n = wt_ruta_n(url), e = wt_len(ext);
    if (n <= e) return 0;
    for (uint32_t i = 0; i < e; i++) if (wt_low((unsigned char)url[n - e + i]) != ext[i]) return 0;
    return url[n - e - 1] != '/';
}
/* por la direccion: .pdf, .tex, .latex, .ltx (http, https o file) */
static inline int wt_tipo_url(const char *url)
{
    if (!url || !(wt_pre_ci(url, "http://") || wt_pre_ci(url, "https://") || wt_pre_ci(url, "file:"))) return WT_NADA;
    if (wt_ext(url, ".pdf")) return WT_PDF;
    if (wt_ext(url, ".tex") || wt_ext(url, ".latex") || wt_ext(url, ".ltx")) return WT_TEX;
    return WT_NADA;
}
static inline int wt_tiene_ci(const char *h, const char *n)
{
    if (!h) return 0;
    for (; *h; h++) if (wt_pre_ci(h, n)) return 1;
    return 0;
}
/* por la respuesta: tipo de contenido o firma "%PDF-" en el primer KB */
static inline int wt_tipo_resp(const char *ctype, const uint8_t *b, uint32_t n)
{
    if (ctype && (wt_tiene_ci(ctype, "application/pdf") || wt_tiene_ci(ctype, "application/x-pdf"))) return WT_PDF;
    if (ctype && (wt_tiene_ci(ctype, "x-tex") || wt_tiene_ci(ctype, "x-latex"))) return WT_TEX;
    if (ctype && (wt_tiene_ci(ctype, "html") || wt_pre_ci(ctype, "text/") || wt_pre_ci(ctype, "image/"))) return WT_NADA;   /* la firma, solo sin tipo claro */
    if (b) { uint32_t m = n < 1024 ? n : 1024; for (uint32_t i = 0; i + 5 <= m; i++) if (b[i] == '%' && b[i + 1] == 'P' && b[i + 2] == 'D' && b[i + 3] == 'F' && b[i + 4] == '-') return WT_PDF; }
    return WT_NADA;
}
/* lo que decide el motor tras pedir una pagina http(s): por la respuesta y, si el servidor no dice que sea una pagina web, por la
 * extension (un .pdf servido como application/octet-stream). Un "x.pdf" que es una pagina web (el visor de GitHub) sigue siendo
 * una pagina. Sirve tambien sin cuerpo (la descarga fallo, p. ej. por ser demasiado grande para una pagina). */
static inline int wt_parece_html(const uint8_t *b, uint32_t n)
{
    uint32_t i = 0; if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3;
    while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) i++;
    return i + 1 < n && b[i] == '<' && (b[i + 1] == '!' || (b[i + 1] | 32) == 'h' || (b[i + 1] | 32) == 'b' || (b[i + 1] | 32) == 'd' || (b[i + 1] | 32) == 's' || (b[i + 1] | 32) == 'm');
}
static inline int wt_tipo_pagina(const char *url, const char *ctype, const uint8_t *b, uint32_t n)
{
    int r = wt_tipo_resp(ctype, b, n); if (r) return r;
    int e = wt_tipo_url(url); if (!e) return WT_NADA;
    if (ctype && (wt_tiene_ci(ctype, "html") || wt_tiene_ci(ctype, "xml") || wt_pre_ci(ctype, "image/"))) return WT_NADA;
    if (b && wt_parece_html(b, n)) return WT_NADA;
    return e;
}
/* titulo de la pagina marcador: WT_MARCA "PDF " nombre */
static inline void wt_marca(char *out, uint32_t cap, int tipo, const char *nombre)
{
    if (!cap) return;
    const char *pre = tipo == WT_TEX ? WT_MARCA "TEX " : WT_MARCA "PDF ";
    uint32_t q = 0;
    for (const char *c = pre; *c && q + 1 < cap; c++) out[q++] = *c;
    const char *c = nombre ? nombre : "";
    for (; *c && q + 1 < cap; c++) out[q++] = *c;
    if (*c) {                                                       /* recortado: sin una letra UTF-8 a medias al final */
        while (q > 0 && ((unsigned char)out[q - 1] & 0xC0) == 0x80) q--;
        if (q > 0 && ((unsigned char)out[q - 1] & 0xC0) == 0xC0) q--;
    }
    out[q] = 0;
}
static inline int wt_marca_tipo(const char *titulo)
{
    if (!titulo || !wt_pre_ci(titulo, WT_MARCA)) return WT_NADA;
    const char *t = titulo + 3;
    if (t[0] == 'P' && t[1] == 'D' && t[2] == 'F' && t[3] == ' ') return WT_PDF;
    if (t[0] == 'T' && t[1] == 'E' && t[2] == 'X' && t[3] == ' ') return WT_TEX;
    return WT_NADA;
}
static inline const char *wt_marca_nombre(const char *titulo) { return wt_marca_tipo(titulo) ? titulo + 7 : titulo; }
/* nombre del archivo de una URL: ultimo trozo de la ruta, con los %XX decodificados ("Informe%20final.pdf" -> "Informe final.pdf") */
static inline void wt_nombre(const char *url, char *out, uint32_t cap)
{
    if (!cap) return;
    out[0] = 0;
    if (!url) return;
    uint32_t n = wt_ruta_n(url), a = 0;
    while (n > 0 && url[n - 1] == '/') n--;
    for (uint32_t i = 0; i < n; i++) if (url[i] == '/') a = i + 1;
    uint32_t q = 0;
    for (uint32_t i = a; i < n && q + 1 < cap; i++) {
        int h1, h2;
        if (url[i] == '%' && i + 2 < n && (h1 = wt_hex((unsigned char)url[i + 1])) >= 0 && (h2 = wt_hex((unsigned char)url[i + 2])) >= 0) {
            char c = (char)(h1 * 16 + h2); out[q++] = (c == '/' || c == 0 || c == '\n' || c == '\r') ? '_' : c; i += 2;
        } else out[q++] = url[i] == '+' ? ' ' : url[i];
    }
    out[q] = 0;
    if (!q) { const char *d = "documento"; while (*d && q + 1 < cap) out[q++] = *d++; out[q] = 0; }
}
/* ruta del sistema de archivos de una URL file: ("file:///workspace/a%20b.pdf" -> "/workspace/a b.pdf"). 1 = era local */
static inline int wt_ruta_local(const char *url, char *out, uint32_t cap)
{
    if (!cap) return 0;
    out[0] = 0;
    if (!url || !wt_pre_ci(url, "file:")) return 0;
    const char *p = url + 5;
    if (p[0] == '/' && p[1] == '/') { p += 2; if (*p != '/') { while (*p && *p != '/') p++; } }   /* file://host/ruta: se ignora el host */
    uint32_t n = wt_ruta_n(p), q = 0;
    for (uint32_t i = 0; i < n && q + 1 < cap; i++) {
        int h1, h2;
        if (p[i] == '%' && i + 2 < n && (h1 = wt_hex((unsigned char)p[i + 1])) >= 0 && (h2 = wt_hex((unsigned char)p[i + 2])) >= 0) {
            char c = (char)(h1 * 16 + h2); if (!c) return 0; out[q++] = c; i += 2;
        } else out[q++] = p[i];
    }
    out[q] = 0;
    if (q == 0 || out[0] != '/') return 0;
    for (uint32_t i = 0; i + 2 < q + 1; i++) if (out[i] == '/' && out[i + 1] == '.' && out[i + 2] == '.' && (out[i + 3] == '/' || !out[i + 3])) return 0;   /* sin ".." */
    return 1;
}
#endif
