#ifndef SOMA_HABLA_TEXTO_H
#define SOMA_HABLA_TEXTO_H
/* SOMA_250_TTS: texto de una respuesta del orbe preparado para leerlo en voz alta.
 * Fuera: bloques de codigo, codigos de color de la consola, adornos de markdown (#, *, _, `, >, |), enlaces (se leen "el enlace"),
 * rutas largas (de /workspace/docs/informe.pdf se lee "informe"), vinetas (pasan a pausas), espacios repetidos.
 * Se corta en el limite al final de una frase. Sin estado ni memoria dinamica: se prueba en el anfitrion (tests/test_tts_250.c). */
#include <stdint.h>

static inline int ht_alnum(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80; }
static inline int ht_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Devuelve los bytes escritos en out (sin contar el 0). max_chars: tope aproximado de lo que se lee. */
static inline uint32_t habla_limpia(const char *in, char *out, uint32_t cap, uint32_t max_chars)
{
    uint32_t o = 0;
    if (!out || cap < 2) return 0;
    out[0] = 0;
    if (!in) return 0;
    if (max_chars == 0 || max_chars > cap - 1) max_chars = cap - 1;
    int code = 0, line_start = 1;
#define HT_PUT(c) do { if (o + 1 < cap) out[o++] = (char)(c); } while (0)
#define HT_SP() do { if (o && out[o - 1] != ' ') HT_PUT(' '); } while (0)
    const char *p = in;
    while (*p) {
        /* bloques de codigo ``` ... ``` */
        if (p[0] == '`' && p[1] == '`' && p[2] == '`') { code = !code; p += 3; while (*p && *p != '\n' && code) p++; continue; }
        if (code) { p++; continue; }
        /* escapes ANSI */
        if (*p == '\033') { p++; if (*p == '[') { p++; while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++; if (*p) p++; } continue; }
        if (*p == '\n') {
            /* fin de linea: si la anterior no acaba en signo, una pausa */
            uint32_t k = o; while (k && out[k - 1] == ' ') k--;
            if (k && out[k - 1] != '.' && out[k - 1] != ':' && out[k - 1] != '?' && out[k - 1] != '!' && out[k - 1] != ';' && out[k - 1] != ',') { o = k; HT_PUT('.'); }
            HT_SP(); line_start = 1; p++; continue;
        }
        if (line_start) {
            while (*p == ' ' || *p == '\t') p++;
            while (*p == '#' || *p == '>' ) p++;                                         /* titulos y citas */
            if ((*p == '-' || *p == '*' || *p == '+') && p[1] == ' ') p += 2;            /* vinetas */
            else { const char *q = p; while (*q >= '0' && *q <= '9') q++; if (q > p && (*q == '.' || *q == ')') && q[1] == ' ') p = q + 2; }   /* 1. 2. */
            line_start = 0;
            continue;
        }
        /* enlaces */
        if ((p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && (p[4] == ':' || (p[4] == 's' && p[5] == ':'))) || (p[0] == 'w' && p[1] == 'w' && p[2] == 'w' && p[3] == '.')) {
            const char *e = p; while (*e && !ht_space(*e) && *e != ')') e++;
            while (e > p && (e[-1] == '.' || e[-1] == ',' || e[-1] == ';' || e[-1] == ':')) e--;   /* el punto final es de la frase */
            p = e;
            { const char *w = "el enlace"; while (*w) HT_PUT(*w++); }
            continue;
        }
        /* rutas: /algo/otra/nombre.ext -> nombre (con espacios en vez de _ y -) */
        if (*p == '/' && (o == 0 || out[o - 1] == ' ' || out[o - 1] == '(') && ht_alnum((unsigned char)p[1])) {
            const char *a = p, *e = p; int slashes = 0;
            while (*e && !ht_space(*e) && *e != ')' && *e != ',' && *e != ';') { if (*e == '/') slashes++; e++; }
            if (slashes >= 2 || (e > a + 1 && e[-1] != '.')) {
                const char *b = e; while (b > a && b[-1] != '/') b--;
                const char *dot = e; for (const char *t = b; t < e; t++) if (*t == '.') dot = t;
                if (dot == b) dot = e;
                while (e > a && (e[-1] == '.' || e[-1] == ':')) e--;
                if (dot > e) dot = e;
                for (const char *t = b; t < dot; t++) HT_PUT(*t == '_' || *t == '-' ? ' ' : *t);
                p = e; continue;
            }
        }
        /* adornos */
        if (*p == '*' || *p == '`' || *p == '|' || *p == '#' || *p == '~') { p++; continue; }
        if (*p == '_' && (o == 0 || !ht_alnum((unsigned char)out[o - 1]) || !ht_alnum((unsigned char)p[1]))) { p++; continue; }
        if (ht_space(*p)) { HT_SP(); p++; continue; }
        HT_PUT(*p); p++;
        if (o >= max_chars) break;
    }
    /* cortar al final de una frase si se paso del tope */
    if (*p && o >= max_chars) {
        uint32_t k = o; while (k > max_chars / 2 && out[k - 1] != '.' && out[k - 1] != '?' && out[k - 1] != '!') k--;
        if (k > max_chars / 2) o = k;
    }
    while (o && out[o - 1] == ' ') o--;
    /* ". ." -> "." y ": ." -> ":" */
    uint32_t w = 0;
    for (uint32_t r = 0; r < o; r++) {
        char c = out[r];
        if (c == '.' && w >= 2 && out[w - 1] == ' ' && (out[w - 2] == '.' || out[w - 2] == ':' || out[w - 2] == '?' || out[w - 2] == '!' || out[w - 2] == ',')) { w--; continue; }
        if (c == '.' && w >= 1 && (out[w - 1] == ':' || out[w - 1] == '?' || out[w - 1] == '!')) continue;
        out[w++] = c;
    }
    o = w; while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
#undef HT_PUT
#undef HT_SP
    return o;
}
#endif
