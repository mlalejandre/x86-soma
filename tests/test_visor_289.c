/* SOMA_289_VISOR: pruebas en el anfitrion de la decision del navegador (src/apps/web_tipo.h): que se abre en el visor de
 * documentos (PDF, LaTeX) y que sigue siendo una pagina web, la pagina marcador, el nombre del archivo y la ruta local. */
#include <stdio.h>
#include <string.h>
#include "web_tipo.h"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

int main(void)
{
    /* por la direccion */
    chk(wt_tipo_url("https://arxiv.org/x/informe.pdf") == WT_PDF, "https .pdf");
    chk(wt_tipo_url("http://a.com/b/INFORME.PDF?descarga=1#page=2") == WT_PDF, "mayusculas, consulta y fragmento");
    chk(wt_tipo_url("file:///workspace/paper.tex") == WT_TEX, "file .tex");
    chk(wt_tipo_url("https://a.com/x.latex") == WT_TEX && wt_tipo_url("https://a.com/x.ltx") == WT_TEX, ".latex y .ltx");
    chk(wt_tipo_url("https://a.com/pdf") == WT_NADA, "sin punto no es extension");
    chk(wt_tipo_url("https://a.com/x.pdf.html") == WT_NADA, ".pdf.html es una pagina");
    chk(wt_tipo_url("https://a.com/?f=x.pdf") == WT_NADA, "la extension en la consulta no cuenta");
    chk(wt_tipo_url("https://a.com/.pdf") == WT_NADA, "un archivo que solo es la extension");
    chk(wt_tipo_url("ftp://a.com/x.pdf") == WT_NADA && wt_tipo_url("javascript:x.pdf") == WT_NADA && wt_tipo_url(0) == WT_NADA, "otros esquemas no");
    chk(wt_tipo_url("https://a.com/texto") == WT_NADA && wt_tipo_url("https://a.com/x.text") == WT_NADA, ".text no es .tex");
    /* por la respuesta */
    chk(wt_tipo_resp("application/pdf", 0, 0) == WT_PDF, "application/pdf");
    chk(wt_tipo_resp("Application/X-PDF; charset=binary", 0, 0) == WT_PDF, "application/x-pdf");
    chk(wt_tipo_resp("text/x-tex", 0, 0) == WT_TEX && wt_tipo_resp("application/x-latex", 0, 0) == WT_TEX, "tipos de LaTeX");
    { static const uint8_t pdf[] = "\n%PDF-1.7\n%\342\343\317\323\n1 0 obj"; chk(wt_tipo_resp("application/octet-stream", pdf, sizeof pdf - 1) == WT_PDF, "firma %PDF- con tipo generico"); chk(wt_tipo_resp("", pdf, sizeof pdf - 1) == WT_PDF, "firma sin tipo"); }
    { static const uint8_t html[] = "<html><body>Ejemplo: %PDF-1.4 en el texto</body></html>"; chk(wt_tipo_resp("text/html", html, sizeof html - 1) == WT_NADA, "una pagina que menciona %PDF- sigue siendo pagina"); }
    { uint8_t big[2000]; memset(big, 'a', sizeof big); memcpy(big + 1500, "%PDF-", 5); chk(wt_tipo_resp("", big, sizeof big) == WT_NADA, "la firma solo cuenta en el primer KB"); }
    chk(wt_tipo_resp("image/png", (const uint8_t *)"%PDF-", 5) == WT_NADA, "una imagen no es un PDF");
    /* la decision completa del motor */
    { static const uint8_t pdf[] = "%PDF-1.4 ..."; chk(wt_tipo_pagina("https://arxiv.org/pdf/2401.00001", "application/pdf", pdf, sizeof pdf - 1) == WT_PDF, "PDF sin extension (arXiv)"); }
    { static const uint8_t html[] = "\n  <!DOCTYPE html><html><head><title>informe.pdf</title>"; chk(wt_tipo_pagina("https://github.com/u/r/blob/main/informe.pdf", "text/html; charset=utf-8", html, sizeof html - 1) == WT_NADA, "el visor de GitHub de un .pdf es una pagina"); chk(wt_tipo_pagina("https://x.com/a.pdf", "", html, sizeof html - 1) == WT_NADA, ".pdf que resulta ser HTML sin tipo"); }
    { static const uint8_t tex[] = "\\documentclass{article}\n\\begin{document}Hola\\end{document}"; chk(wt_tipo_pagina("https://raw.githubusercontent.com/u/r/main/paper.tex", "text/plain; charset=utf-8", tex, sizeof tex - 1) == WT_TEX, ".tex servido como text/plain"); }
    chk(wt_tipo_pagina("https://a.com/grande.pdf", "application/octet-stream", 0, 0) == WT_PDF, "sin cuerpo (demasiado grande): por la extension");
    chk(wt_tipo_pagina("https://a.com/grande.pdf", "text/html", 0, 0) == WT_NADA, "sin cuerpo pero el servidor dice HTML");
    chk(wt_tipo_pagina("https://a.com/index.html", "text/html", (const uint8_t *)"<html>", 6) == WT_NADA, "una pagina normal");
    { static const uint8_t bom[] = "\357\273\277<html>"; chk(wt_parece_html(bom, sizeof bom - 1), "HTML con BOM"); }
    chk(!wt_parece_html((const uint8_t *)"\\section{<b>}", 13) && !wt_parece_html((const uint8_t *)"<", 1), "LaTeX no parece HTML; un '<' suelto tampoco");
    /* pagina marcador */
    char t[64];
    wt_marca(t, sizeof t, WT_PDF, "informe.pdf");
    chk(wt_marca_tipo(t) == WT_PDF && !strcmp(wt_marca_nombre(t), "informe.pdf"), "marcador PDF");
    wt_marca(t, sizeof t, WT_TEX, "paper.tex");
    chk(wt_marca_tipo(t) == WT_TEX && !strcmp(wt_marca_nombre(t), "paper.tex"), "marcador TEX");
    chk(wt_marca_tipo("PDF informe.pdf") == WT_NADA && wt_marca_tipo("Mi documento") == WT_NADA && wt_marca_tipo(0) == WT_NADA, "un titulo normal no es marcador");
    chk(wt_marca_tipo(WT_MARCA "Otra cosa") == WT_NADA, "solo la marca no basta");
    chk(!strcmp(wt_marca_nombre("Titulo normal"), "Titulo normal"), "nombre de un titulo normal: el mismo");
    { char c[14]; wt_marca(c, sizeof c, WT_PDF, "\303\241\303\251\303\255.pdf"); chk(strlen(c) < sizeof c && wt_marca_tipo(c) == WT_PDF, "marcador recortado"); const char *n = wt_marca_nombre(c); int okc = 1; size_t L = strlen(n); if (L && ((unsigned char)n[L - 1] & 0xC0) == 0xC0) okc = 0; chk(okc, "sin una letra UTF-8 a medias"); }
    /* nombre del archivo */
    char n[64];
    wt_nombre("https://a.com/docs/Informe%20final%20(v2).pdf?x=1#p=3", n, sizeof n); chk(!strcmp(n, "Informe final (v2).pdf"), "nombre con %20 y parentesis");
    wt_nombre("https://a.com/dir/", n, sizeof n); chk(!strcmp(n, "dir"), "barra final");
    wt_nombre("https://a.com", n, sizeof n); chk(!strcmp(n, "a.com"), "solo dominio");
    wt_nombre("", n, sizeof n); chk(!strcmp(n, "documento"), "vacio: documento");
    wt_nombre("https://a.com/a%2Fb%00c.pdf", n, sizeof n); chk(!strchr(n, '/') && strlen(n) == 9, "sin barras ni nulos decodificados");
    wt_nombre("https://a.com/espa%C3%B1a.tex", n, sizeof n); chk(!strcmp(n, "espa\303\261a.tex"), "UTF-8 decodificado");
    { char s4[5]; wt_nombre("https://a.com/larguisimo.pdf", s4, sizeof s4); chk(strlen(s4) == 4, "respeta el tope"); }
    /* ruta local */
    char r[128];
    chk(wt_ruta_local("file:///workspace/Mis%20papers/a.pdf", r, sizeof r) && !strcmp(r, "/workspace/Mis papers/a.pdf"), "file:/// con %20");
    chk(wt_ruta_local("file:/workspace/b.tex#sec", r, sizeof r) && !strcmp(r, "/workspace/b.tex"), "file:/ y fragmento");
    chk(wt_ruta_local("file://localhost/workspace/c.pdf", r, sizeof r) && !strcmp(r, "/workspace/c.pdf"), "file://host/");
    chk(!wt_ruta_local("https://a.com/x.pdf", r, sizeof r), "http no es local");
    chk(!wt_ruta_local("file:///workspace/../etc/clave.pdf", r, sizeof r) && !wt_ruta_local("file:///a/%2E%2E/b.pdf", r, sizeof r), "sin ..");
    chk(!wt_ruta_local("file:///a%00b.pdf", r, sizeof r), "sin nulos");
    chk(wt_ruta_local("file:///workspace/..algo.pdf", r, sizeof r), "'..algo' es un nombre, no subir");
    printf("visor 289: %d/%d correctas\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
