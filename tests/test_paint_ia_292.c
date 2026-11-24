/* SOMA_292_PAINT_IA: dibujar en Paint con el modelo. Pruebas en el anfitrion de src/apps/paint_svg.c con el rasterizador SVG de
 * verdad (web_svg.inc, por web_img.c): sacar el SVG de la respuesta del modelo (con razonamiento, con ```svg, cortado), trocearlo
 * en elementos de primer nivel, pintarlo por pasos (cada paso cubre al menos lo del anterior y el ultimo es el dibujo entero),
 * mezclarlo sobre el lienzo y sacar el tema de la peticion. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_img.h"
#include "paint_svg.h"
#include "agent_ruta.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) { }

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

static const char DIBUJO[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\">\n"
    "<defs><linearGradient id=\"cielo\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1e3a8a\"/><stop offset=\"1\" stop-color=\"#f97316\"/></linearGradient></defs>\n"
    "<!-- fondo -->\n<rect width=\"640\" height=\"480\" fill=\"url(#cielo)\"/>\n"
    "<!-- sol -->\n<circle cx=\"500\" cy=\"120\" r=\"60\" fill=\"#facc15\" stroke=\"#78350f\" stroke-width=\"3\"/>\n"
    "<!-- mar -->\n<rect x=\"0\" y=\"330\" width=\"640\" height=\"150\" fill=\"#0e7490\"/>\n"
    "<!-- faro -->\n<g stroke=\"#1f2937\" stroke-width=\"3\"><polygon points=\"250,330 290,140 330,330\" fill=\"#f8fafc\"/>"
    "<rect x=\"270\" y=\"200\" width=\"40\" height=\"25\" fill=\"#dc2626\"/><path d=\"M 280 140 Q 290 110 300 140 Z\" fill=\"#dc2626\"/></g>\n"
    "<style>.x{fill:red}</style>\n"
    "<ellipse cx=\"120\" cy=\"400\" rx=\"60\" ry=\"14\" fill=\"#164e63\"/>\n"
    "<line x1=\"0\" y1=\"330\" x2=\"640\" y2=\"330\" stroke=\"#f8fafc\" stroke-width=\"2\"/>\n"
    "</svg>";

static int render(const char *s, uint32_t n, struct wimg *im) { memset(im, 0, sizeof *im); return wi_decode_svg((const uint8_t *)s, n, im, 640, 480, 0); }

int main(void)
{
    static char out[65536];
    /* 1. extraer */
    uint32_t n = ps_extrae(DIBUJO, out, sizeof out);
    chk(n == strlen(DIBUJO) && !strcmp(out, DIBUJO), "SVG tal cual");
    {   static char r[70000]; snprintf(r, sizeof r, "<think>Voy a pensar en <svg> y en el faro...</think>\nAqui tienes:\n```svg\n%s\n```\nEspero que te guste.", DIBUJO);
        n = ps_extrae(r, out, sizeof out); chk(n == strlen(DIBUJO) && !strcmp(out, DIBUJO), "sin razonamiento, sin ```svg ni texto alrededor"); }
    {   char r[400] = "<?xml version=\"1.0\"?><svg viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\" fill=\"red\"/><circle cx=\"5\" cy=\"5\" r=\"3\" fi";
        n = ps_extrae(r, out, sizeof out); chk(n && strstr(out, "<rect") && !strstr(out, "<circle") && !strcmp(out + n - 6, "</svg>"), "cortado: se cierra tras la ultima etiqueta completa"); }
    chk(ps_extrae("No puedo dibujar eso.", out, sizeof out) == 0 && out[0] == 0, "sin SVG: 0");
    chk(ps_extrae("<svgx>no</svgx>", out, sizeof out) == 0, "<svgx> no es <svg>");
    chk(ps_extrae(DIBUJO, out, 40) == 0, "no cabe: 0");
    {   char r[300] = "<svg viewBox='0 0 10 10'><rect/></svg> y otra <svg viewBox='0 0 5 5'><circle/></svg>"; n = ps_extrae(r, out, sizeof out); chk(n && strstr(out, "<circle"), "varios: del primero al ultimo cierre"); }
    /* 2. trozos */
    uint32_t fin[64];
    int k = ps_trozos(DIBUJO, (uint32_t)strlen(DIBUJO), fin, 64);
    chk(k == 8, "8 elementos de primer nivel (defs, fondo, sol, mar, faro, style, roca, linea)");
    chk(k >= 5 && !strncmp(DIBUJO + fin[0] - 7, "</defs>", 7) && !strncmp(DIBUJO + fin[4] - 4, "</g>", 4), "cortes al final de cada elemento");
    {   int sube = 1; for (int i = 1; i < k; i++) if (fin[i] <= fin[i - 1]) sube = 0; chk(sube, "cortes crecientes"); }
    {   uint32_t f2[64];
        chk(ps_trozos("<svg/>", 6, f2, 64) == 0 && ps_trozos("nada", 4, f2, 64) == 0 && ps_trozos(DIBUJO, (uint32_t)strlen(DIBUJO), f2, 3) == 3, "vacio, sin svg y tope");
        const char *q = "<svg><g title=\"a>b\"><rect/></g><!-- <rect/> --><![CDATA[ <x> ]]><circle r=\"1\"/></svg>";
        chk(ps_trozos(q, (uint32_t)strlen(q), f2, 64) == 2, "comillas con '>', comentarios y CDATA no cuentan"); }
    /* 3. pasos */
    chk(ps_pasos(0) == 0 && ps_pasos(5) == 5 && ps_pasos(100) == PS_PASOS_MAX, "numero de pasos");
    {   int bien = 1; for (int N = 1; N <= 60; N++) { int P = ps_pasos(N), prev = 0; for (int i = 1; i <= P; i++) { int c = ps_corte(N, P, i); if (c < prev || c < 1 || c > N) bien = 0; prev = c; } if (prev != N) bien = 0; } chk(bien, "cortes de cada paso: crecientes y el ultimo es todo"); }
    /* 4. por pasos con el rasterizador de verdad */
    static char pre[70000];
    struct wimg im, full;
    chk(render(DIBUJO, (uint32_t)strlen(DIBUJO), &full) == 0 && full.w == 640 && full.h == 480, "el dibujo entero se rasteriza a 640x480");
    uint32_t cub_full = ps_cubiertos(full.px, 640 * 480);
    chk(cub_full == 640u * 480u, "con el fondo, todo el lienzo cubierto");
    int P = ps_pasos(k); uint32_t prev = 0; int crece = 1, iguales = 0;
    for (int i = 1; i <= P; i++) {
        uint32_t L = ps_prefijo(DIBUJO, fin[ps_corte(k, P, i) - 1], pre, sizeof pre);
        if (!L || render(pre, L, &im) != 0) { crece = 0; break; }
        uint32_t c = ps_cubiertos(im.px, 640 * 480); if (c < prev) crece = 0; prev = c;
        if (i == P) iguales = !memcmp(im.px, full.px, 640u * 480u * 4u);
        wi_free(&im);
    }
    chk(crece, "cada paso se rasteriza y cubre al menos lo del anterior");
    chk(iguales, "el ultimo paso es exactamente el dibujo entero");
    {   uint32_t L = ps_prefijo(DIBUJO, fin[1], pre, sizeof pre);   /* defs + fondo */
        int rr = L ? render(pre, L, &im) : -9; uint32_t cc = rr == 0 ? ps_cubiertos(im.px, 640 * 480) : 0;
        chk(L && rr == 0 && cc == 640u * 480u, "el primer paso ya tiene el fondo (con el degradado de defs)");
        uint32_t c = im.px[120 * 640 + 500]; int es_sol = ((c >> 16) & 255) > 200 && ((c >> 8) & 255) > 180 && (c & 255) < 120;
        chk(!es_sol, "en el primer paso aun no hay sol"); wi_free(&im);
        uint32_t s = full.px[120 * 640 + 500]; chk(((s >> 16) & 255) > 200 && ((s >> 8) & 255) > 180 && (s & 255) < 120, "en el dibujo entero, el sol amarillo"); }
    chk(ps_prefijo(DIBUJO, 100, pre, 50) == 0, "prefijo que no cabe: 0");
    /* 5. mezcla sobre el lienzo */
    {   uint32_t base[4] = { 0x00FFFFFF, 0x00FFFFFF, 0x00000000, 0x00102030 }, src[4] = { 0x00FF0000, 0xFF00FF00, 0x800000FF, 0x00000000 }, d[4];
        ps_mezcla(d, base, src, 4);
        chk(d[0] == 0x00FFFFFF && d[1] == 0x0000FF00 && d[3] == 0x00102030, "transparente deja el lienzo; opaco lo tapa");
        chk(((d[2] & 255) > 120 && (d[2] & 255) < 135) && (d[2] >> 24) == 0, "semitransparente: mezcla a medias y sale opaco (sin alfa)"); }
    ps_mezcla(full.px, full.px, full.px, 640 * 480); chk(1, "mezcla en el sitio sin fallos");
    wi_free(&full);
    /* 6. el tema de la peticion */
    char t[120];
    ps_tema("dib\303\272jame un faro al atardecer en Paint", t, sizeof t); chk(!strcmp(t, "un faro al atardecer"), "dibujame ... en Paint");
    ps_tema("\302\277puedes pintar un gato naranja en el paint, por favor?", t, sizeof t); chk(!strcmp(t, "un gato naranja"), "puedes pintar ... por favor");
    ps_tema("haz un dibujo de una casa con jard\303\255n", t, sizeof t); chk(!strcmp(t, "una casa con jard\303\255n"), "haz un dibujo de (con tildes)");
    ps_tema("abre paint y dibuja un cohete", t, sizeof t); chk(!strcmp(t, "un cohete"), "abre paint y dibuja");
    ps_tema("dibuja", t, sizeof t); chk(!strcmp(t, "un dibujo"), "sin tema: un dibujo");
    ps_tema("dibuja un mapa de Espa\303\261a", t, sizeof t); chk(!strcmp(t, "un mapa de Espa\303\261a"), "conserva la enye");
    ps_tema("dibuja algo que me guste", t, sizeof t); chk(!strcmp(t, "algo que me guste"), "'me' en medio se queda");
    {   char c6[6]; ps_tema("dibuja un \303\241rbol", c6, sizeof c6); int bien = 1; size_t L = strlen(c6); if (L && ((unsigned char)c6[L - 1] & 0xC0) == 0xC0) bien = 0; chk(bien && L < sizeof c6, "tope sin letras partidas"); }
    /* 7. que peticiones del orbe son dibujos (ar_dibujo) */
    static const char *const si[] = { "dib\303\272jame un faro al atardecer", "pinta un gato naranja en paint", "\302\277puedes dibujar en paint una casa?",
        "haz un dibujo de un drag\303\263n", "quiero que dibujes un robot en Paint", "pinta una flor", "hazme un dibujo", "dibuja un paisaje de monta\303\261a", 0 };
    static const char *const no[] = { "abre paint", "pinta la pared de azul", "dibuja un gr\303\241fico de ventas", "c\303\263mo se dibuja un caballo",
        "dibuja un diagrama de flujo", "expl\303\255came c\303\263mo pintar al \303\263leo", "dibuja el \303\241rbol de archivos", "crea un programa que dibuje c\303\255rculos",
        "dibuja una tabla con los gastos", "hola", "", 0 };
    for (int i = 0; si[i]; i++) chk(ar_dibujo(si[i]), si[i]);
    for (int i = 0; no[i]; i++) { char m2[160]; snprintf(m2, sizeof m2, "no es un dibujo: %s", no[i]); chk(!ar_dibujo(no[i]), m2); }
    chk(!ar_dibujo(0), "nulo");
    printf("paint ia 292: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
