/* SOMA_292_PAINT_IA: ver paint_svg.h */
#include "paint_svg.h"

static int lw(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int pre_ci(const char *s, const char *e, const char *p) { while (*p) { if (s >= e || lw((unsigned char)*s) != *p) return 0; s++; p++; } return 1; }
static const char *busca_ci(const char *s, const char *e, const char *p) { for (; s < e; s++) if (pre_ci(s, e, p)) return s; return 0; }
static uint32_t len(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }

uint32_t ps_extrae(const char *txt, char *out, uint32_t cap)
{
    if (cap) out[0] = 0;
    if (!txt || cap < 16) return 0;
    const char *s = txt, *e = txt + len(txt);
    { const char *t = 0, *c = s; while ((c = busca_ci(c, e, "</think>")) != 0) { t = c + 8; c = t; } if (t) s = t; }   /* tras el ultimo </think> */
    const char *a = busca_ci(s, e, "<svg");
    while (a && !(a[4] == ' ' || a[4] == '>' || a[4] == '\n' || a[4] == '\r' || a[4] == '\t')) a = busca_ci(a + 4, e, "<svg");
    if (!a) return 0;
    const char *z = 0, *c = a; while ((c = busca_ci(c, e, "</svg>")) != 0) { z = c + 6; c = z; }
    uint32_t q = 0; int cierra = 0;
    if (!z) {                                                    /* cortado: hasta la ultima etiqueta completa */
        const char *u = e; while (u > a && u[-1] != '>') u--;
        if (u <= a) return 0;
        z = u; cierra = 1;
    }
    if ((uint32_t)(z - a) + 8 >= cap) return 0;
    for (const char *p = a; p < z; p++) out[q++] = *p;
    if (cierra) { const char *f = "</svg>"; while (*f) out[q++] = *f++; }
    out[q] = 0;
    return q;
}

/* fin de la etiqueta que empieza en p ('<'), respetando las comillas; e si no termina */
static const char *fin_etiqueta(const char *p, const char *e)
{
    char qu = 0;
    for (p++; p < e; p++) { if (qu) { if (*p == qu) qu = 0; } else if (*p == '"' || *p == '\'') qu = *p; else if (*p == '>') return p + 1; }
    return e;
}
int ps_trozos(const char *svg, uint32_t n, uint32_t *fin, int max)
{
    if (!svg || !n || max <= 0) return 0;
    const char *s = svg, *e = svg + n, *a = busca_ci(s, e, "<svg");
    if (!a) return 0;
    const char *p = fin_etiqueta(a, e);
    if (p >= e || p[-2] == '/') return 0;                        /* <svg .../> vacio */
    int prof = 0, k = 0;
    while (p < e && k < max) {
        if (*p != '<') { p++; continue; }
        if (pre_ci(p, e, "<!--")) { const char *c = busca_ci(p + 4, e, "-->"); p = c ? c + 3 : e; continue; }
        if (pre_ci(p, e, "<![cdata[")) { const char *c = busca_ci(p + 9, e, "]]>"); p = c ? c + 3 : e; continue; }
        if (p + 1 < e && (p[1] == '?' || p[1] == '!')) { p = fin_etiqueta(p, e); continue; }
        if (p + 1 < e && p[1] == '/') {
            if (prof == 0) break;                                /* </svg> */
            p = fin_etiqueta(p, e); prof--;
            if (prof == 0) fin[k++] = (uint32_t)(p - s);
            continue;
        }
        const char *t = fin_etiqueta(p, e);
        if (t >= e && t[-1] != '>') break;                       /* etiqueta sin cerrar al final */
        int sola = t - p >= 2 && t[-2] == '/';
        if (!sola && (pre_ci(p, e, "<style") || pre_ci(p, e, "<script"))) {   /* su contenido puede llevar '<' */
            const char *c = busca_ci(t, e, pre_ci(p, e, "<style") ? "</style" : "</script");
            t = c ? fin_etiqueta(c, e) : e; sola = 1;
        }
        p = t;
        if (sola) { if (prof == 0) fin[k++] = (uint32_t)(p - s); }
        else prof++;
    }
    return k;
}
int ps_pasos(int n) { return n <= 0 ? 0 : n < PS_PASOS_MAX ? n : PS_PASOS_MAX; }
int ps_corte(int n, int pasos, int i)
{
    if (n <= 0 || pasos <= 0) return 0;
    if (i >= pasos) return n;
    if (i <= 0) return 0;
    int c = (n * i + pasos - 1) / pasos; return c < 1 ? 1 : c > n ? n : c;
}
uint32_t ps_prefijo(const char *svg, uint32_t corte, char *out, uint32_t cap)
{
    if (!svg || cap < corte + 8) { if (cap) out[0] = 0; return 0; }
    uint32_t q = 0; for (uint32_t i = 0; i < corte; i++) out[q++] = svg[i];
    const char *f = "</svg>"; while (*f) out[q++] = *f++;
    out[q] = 0; return q;
}
void ps_mezcla(uint32_t *dst, const uint32_t *base, const uint32_t *argb, uint32_t npx)
{
    for (uint32_t i = 0; i < npx; i++) {
        uint32_t s = argb[i], a = s >> 24, b = base[i] & 0x00FFFFFFu;
        if (a == 0) { dst[i] = b; continue; }
        if (a == 255) { dst[i] = s & 0x00FFFFFFu; continue; }
        uint32_t r = (((s >> 16) & 255) * a + ((b >> 16) & 255) * (255 - a) + 127) / 255;
        uint32_t g = (((s >> 8) & 255) * a + ((b >> 8) & 255) * (255 - a) + 127) / 255;
        uint32_t bl = ((s & 255) * a + (b & 255) * (255 - a) + 127) / 255;
        dst[i] = (r << 16) | (g << 8) | bl;
    }
}
uint32_t ps_cubiertos(const uint32_t *argb, uint32_t npx) { uint32_t k = 0; for (uint32_t i = 0; i < npx; i++) if (argb[i] >> 24) k++; return k; }

/* ---------------------------------------------------------------- el tema de la peticion */
/* palabra normalizada: minusculas ASCII y vocales con tilde sin ella (UTF-8 de dos bytes) */
static uint32_t norm(const char *a, uint32_t n, char *o, uint32_t cap)
{
    uint32_t q = 0;
    for (uint32_t i = 0; i < n && q + 1 < cap; i++) {
        unsigned char c = (unsigned char)a[i];
        if (c == 0xC3 && i + 1 < n) {
            unsigned char d = (unsigned char)a[++i]; char r = '?';
            switch (d) { case 0xA1: case 0x81: r = 'a'; break; case 0xA9: case 0x89: r = 'e'; break; case 0xAD: case 0x8D: r = 'i'; break;
                         case 0xB3: case 0x93: r = 'o'; break; case 0xBA: case 0x9A: case 0xBC: r = 'u'; break; case 0xB1: case 0x91: r = 'n'; break; default: break; }
            o[q++] = r;
        } else o[q++] = (char)lw(c);
    }
    o[q] = 0; return q;
}
static int eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
void ps_tema(const char *m, char *out, uint32_t cap)
{
    if (!cap) return;
    out[0] = 0; if (!m) return;
    enum { NW = 48 };
    const char *wa[NW]; uint32_t wl[NW]; char wn[NW][20]; int nw = 0, quita[NW];
    for (const char *p = m; *p && nw < NW; ) {
        while (*p == ' ' || *p == ',' || *p == '.' || *p == '?' || *p == '!' || *p == ':' || *p == ';' || *p == '"'
               || ((unsigned char)p[0] == 0xC2 && ((unsigned char)p[1] == 0xBF || (unsigned char)p[1] == 0xA1))) p += (unsigned char)*p == 0xC2 ? 2 : 1;
        if (!*p) break;
        const char *a = p;
        while (*p && *p != ' ' && *p != ',' && *p != '.' && *p != '?' && *p != '!' && *p != ':' && *p != ';' && *p != '"'
               && !((unsigned char)p[0] == 0xC2 && ((unsigned char)p[1] == 0xBF || (unsigned char)p[1] == 0xA1))) p++;
        wa[nw] = a; wl[nw] = (uint32_t)(p - a); norm(a, wl[nw], wn[nw], sizeof wn[0]); quita[nw] = 0; nw++;
    }
    /* frases que se quitan (en palabras normalizadas) */
    static const char *const F[][5] = {
        { "en", "el", "paint", 0 }, { "en", "paint", 0 }, { "con", "paint", 0 }, { "en", "el", "lienzo", 0 }, { "en", "la", "aplicacion", "de", "dibujo" },
        { "haz", "un", "dibujo", "de", 0 }, { "hazme", "un", "dibujo", "de", 0 }, { "crea", "un", "dibujo", "de", 0 }, { "haz", "un", "dibujo", 0 },
        { "hazme", "un", "dibujo", 0 }, { "un", "dibujo", "de", 0 }, { "por", "favor", 0 }, { "abre", "paint", "y", 0 },
        { "dibuja", 0 }, { "dibujame", 0 }, { "dibujar", 0 }, { "dibujes", 0 }, { "pinta", 0 }, { "pintame", 0 }, { "pintar", 0 }, { "ilustra", 0 },
        { "ilustrame", 0 }, { "puedes", 0 }, { "podrias", 0 }, { "quiero", "que", 0 }, { "me", 0 }, { "porfa", 0 }, { "soma", 0 }, { 0 } };
    for (int f = 0; F[f][0]; f++) {
        int nf = 0; while (nf < 5 && F[f][nf]) nf++;
        for (int k = 0; k + nf <= nw; k++) {
            int ok = 1; for (int j = 0; j < nf && ok; j++) ok = !quita[k + j] && eq(wn[k + j], F[f][j]);
            if (!ok) continue;
            if (nf == 1 && eq(F[f][0], "me") && k > 0 && !quita[k - 1]) continue;   /* "me" solo al principio (tras lo quitado) */
            for (int j = 0; j < nf; j++) quita[k + j] = 1;
        }
    }
    uint32_t q = 0;
    for (int k = 0; k < nw; k++) {
        if (quita[k]) continue;
        if (q && q + 1 < cap) out[q++] = ' ';
        for (uint32_t z = 0; z < wl[k] && q + 1 < cap; z++) out[q++] = wa[k][z];
    }
    if (q + 1 >= cap) { while (q > 0 && ((unsigned char)out[q - 1] & 0xC0) == 0x80) q--; if (q > 0 && ((unsigned char)out[q - 1] & 0xC0) == 0xC0) q--; }   /* sin letra a medias */
    out[q] = 0;
    if (!q) { const char *d = "un dibujo"; while (*d && q + 1 < cap) out[q++] = *d++; out[q] = 0; }
}
