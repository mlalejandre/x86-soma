/* SOMA_248_DOC: agente de documentos con acabado profesional. Ver docagent.h.
 * Las piezas puras (tipo, extraccion, veredicto, nombre, indicaciones) se prueban en el host (tests/test_docgen_248.c);
 * el resto (LLM, archivos, pantalla) solo existe en el nucleo. */
#include "docagent.h"
#include "docgen.h"

/* ================================================================ utilidades */
static int dlen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int lowc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* compara sin mayusculas y tratando las vocales acentuadas (UTF-8) como las sin acento */
static int fold(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    int c = *p;
    if (c == 0xC3 && p[1]) {
        int d = p[1]; *pp += 2;
        switch (d) { case 0xA1: case 0x81: return 'a'; case 0xA9: case 0x89: return 'e'; case 0xAD: case 0x8D: return 'i';
                     case 0xB3: case 0x93: return 'o'; case 0xBA: case 0x9A: case 0xBC: case 0x9C: return 'u'; case 0xB1: case 0x91: return 'n'; }
        return '?';
    }
    *pp += 1;
    return lowc(c);
}
static int has(const char *h, const char *n)
{
    if (!h || !n || !*n) return 0;
    while (*h) {
        const char *a = h, *b = n; int ok = 1;
        while (*b) { if (!*a || fold(&a) != fold(&b)) { ok = 0; break; } }
        if (ok) return 1;
        h++; while ((*(const unsigned char *)h & 0xC0) == 0x80) h++;
    }
    return 0;
}
static const char *find_ci(const char *h, const char *n)
{
    for (; h && *h; h++) { int i = 0; while (n[i] && h[i] && lowc((unsigned char)h[i]) == lowc((unsigned char)n[i])) i++; if (!n[i]) return h; }
    return 0;
}

/* ================================================================ tipo de documento */
struct dkind { const char *name; const char *const *keys; const char *recipe; };
static const char *const K_PRES[] = { "presentacion", "diapositiva", "slides", "pitch", "deck", "charla", "ponencia", 0 };
static const char *const K_PAPER[] = { "paper", "articulo cientifico", "cientifico", "investigacion", "tesis", "tfg", "tfm", "latex", "ecuacion", "formula", 0 };
static const char *const K_REV[] = { "revista", "reportaje", "magazine", "noticia", "periodico", "boletin informativo", 0 };
static const char *const K_EMAIL[] = { "email", "e-mail", "correo", "newsletter", "boletin", 0 };
static const char *const K_CV[] = { "curriculum", " cv", "cv ", "hoja de vida", "resume", 0 };
static const char *const K_CARTA[] = { "carta", "solicitud formal", "instancia", "reclamacion", 0 };
static const char *const K_LIBRO[] = { "libro", "capitulo", "novela", "cuento", "relato", 0 };
static const char *const K_INF[] = { "informe", "memoria", "reporte", "report", "documento", "propuesta", "presupuesto", "manual", "guia", "dossier", "folleto", 0 };

static const struct dkind KINDS[] = {
    { "presentacion", K_PRES,
      "PRESENTACION 16:9. <meta name=\"soma-pagina\" content=\"16:9\">. Cada diapositiva es <section class=\"diapositiva\"> (una pagina cada una); "
      "entre 6 y 12. Portada con class=\"diapositiva oscura\" (.sobretitulo, h1, .subtitulo); una diapositiva = una idea, titular h2 que diga la "
      "conclusion (no 'Resultados' sino 'Las ventas crecen un 12 %'); como mucho 4 vinetas de una linea; usa .kpis, .columnas-3 con tarjetas "
      ".destacado, graficos SVG y tablas cortas (4 filas). Cierre con class=\"diapositiva acento\" con la peticion o el siguiente paso. "
      "Nada de parrafos largos: el texto debe leerse a 3 metros." },
    { "paper", K_PAPER,
      "ARTICULO CIENTIFICO A4. Si tiene formulas importantes escribe LaTeX (\\documentclass{article} ... \\end{document}, con \\title, \\author, "
      "\\maketitle, abstract, \\section, equation/align, tabular con \\hline, \\cite opcional y thebibliography) y pon en la primera linea "
      "%soma-pagina: A4. Si no, HTML: .portada con .titulo, .autores, .afiliacion; .resumen con <strong>Resumen.</strong> y .palabras-clave; "
      "secciones numeradas (1. Introduccion, 2. Metodo, 3. Resultados, 4. Discusion, 5. Conclusiones), figuras SVG con figcaption "
      "'Figura 1. ...', tablas con caption 'Tabla 1. ...', y Referencias con formato APA en una lista ol." },
    { "revista", K_REV,
      "REVISTA A4. Cabecera de cabecera (nombre de la revista, seccion, numero) en una fila con borde inferior; .sobretitulo de seccion, h1 grande, "
      ".entradilla, .firma-articulo; cuerpo en .columnas (dos columnas de texto alineado a la izquierda), ladillos h3, una .cita-grande, un "
      "recuadro .destacado o .aviso con datos ('En cifras'). pie='Revista ... · {n}'." },
    { "email", K_EMAIL,
      "EMAIL. <meta name=\"soma-pagina\" content=\"email\">. Estructura: <div class=\"email\"><div class=\"email-cabecera\">Titulo</div>"
      "<div class=\"email-cuerpo\">saludo, 2-4 parrafos cortos, datos clave en una tabla de 2 columnas si hay fechas/lugares, "
      "<p class=\"centro\"><a class=\"boton\" href=\"#\">Accion</a></p>, despedida y firma</div><div class=\"email-pie\">pie legal breve</div></div>. "
      "Tono cercano y profesional. Nada de imagenes externas." },
    { "cv", K_CV,
      "CURRICULUM A4 (1 pagina, 2 como mucho). Cabecera: nombre en .titulo, profesion en .subtitulo, contacto en .meta. Despues .lateral: "
      "columna principal con Perfil (3 lineas), Experiencia (puesto en h3, empresa y fechas en .tenue, 2-3 logros con cifras en ul) y "
      "Formacion; columna lateral en .destacado con Competencias, Idiomas (con .barra) y Herramientas. Sin foto." },
    { "carta", K_CARTA,
      "CARTA FORMAL A4. <meta name=\"soma-pagina\" content=\"A4; margen=25; numeros=no\">. .remitente (nombre en strong, direccion, contacto), "
      ".destinatario, .fecha ('Madrid, 21 de noviembre de 2026'), <p><strong>Asunto:</strong> ...</p>, saludo, 3-5 parrafos, despedida y .firma. "
      "Una sola pagina." },
    { "libro", K_LIBRO,
      "LIBRO A5. <meta name=\"soma-pagina\" content=\"A5; pie={titulo} · {n}\">. Portada .portada.pagina-completa centrada (titulo, autor). "
      "Cada capitulo: <div class=\"salto-pagina\"></div>, .capitulo ('Capitulo uno'), h1.titulo-capitulo, y el texto en <div class=\"libro\"> "
      "(parrafos con sangria, justificados). Dialogos con raya (—)." },
    { "informe", K_INF,
      "INFORME A4. <meta name=\"soma-pagina\" content=\"A4; pie=Titulo corto · Confidencial; cabecera=Titulo corto\">. .portada con .sobretitulo "
      "(departamento), .titulo, .subtitulo y .meta (autor · fecha). Resumen ejecutivo primero (3-5 lineas con la conclusion y las cifras clave), "
      "una fila .kpis con 3-4 indicadores, secciones h2 con h3, una tabla con td.num para las cifras y fila de total, un grafico SVG de barras "
      "o lineas con figcaption, recuadros .destacado (conclusion) y .aviso (riesgo), y 'Proximos pasos' en ol." },
};
#define NKINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

static int kind_index(const char *m)
{
    for (int i = 0; i < NKINDS; i++) for (int k = 0; KINDS[i].keys[k]; k++) if (has(m, KINDS[i].keys[k])) return i;
    return NKINDS - 1;
}
const char *docagent_kind(const char *mission) { return KINDS[kind_index(mission)].name; }

int docagent_should_handle(const char *m)
{
    if (!m) return 0;
    while (*m == ' ') m++;
    if (find_ci(m, "maquetador") == m) return 1;                       /* misiones que manda el Maquetador ("maquetador: ...") */
    static const char *const verb[] = { "redacta", "escribe", "prepara", "maqueta", "disena", "crea", "genera", "haz", "elabora", "hazme", "preparame", "redactame", "escribeme", 0 };
    static const char *const fmt[] = { "pdf", "maquet", "formato", "profesional", "presentacion", "diapositiva", "paper", "revista", "folleto",
                                       "curriculum", "newsletter", "informe", "carta formal", "imprimir", "latex", "html", 0 };
    static const char *const doc[] = { "informe", "documento", "paper", "articulo", "revista", "email", "correo", "carta", "libro", "capitulo",
                                       "presentacion", "diapositivas", "curriculum", " cv", "memoria", "propuesta", "folleto", "newsletter", "manual", "dossier", 0 };
    static const char *const no[] = { ".soma", "programa", "aplicacion", "app ", "juego", "script", "codigo", "funcion", "busca en", 0 };
    for (int i = 0; no[i]; i++) if (has(m, no[i])) return 0;
    int v = 0, f = 0, d = 0;
    for (int i = 0; verb[i]; i++) if (has(m, verb[i])) v = 1;
    for (int i = 0; fmt[i]; i++) if (has(m, fmt[i])) f = 1;
    for (int i = 0; doc[i]; i++) if (has(m, doc[i])) d = 1;
    if (has(m, "documento ") && (has(m, "pdf") || has(m, "maquet"))) return 1;
    return v && d && f;
}

/* ================================================================ indicaciones */
static const char SYS_DOC[] =
"Eres el maquetador editorial de SOMA: redactor experto y disenador grafico. Produces documentos con acabado profesional, listos para "
"imprimir o enviar, que SOMA maqueta con su propio motor (HTML+CSS o LaTeX) y exporta a PDF.\n"
"SALIDA: UNICAMENTE el documento completo, sin explicaciones ni markdown. HTML: desde <!DOCTYPE html> hasta </html>. LaTeX: desde "
"\\documentclass hasta \\end{document}.\n"
"FORMATO DE PAGINA: <meta name=\"soma-pagina\" content=\"...\"> en <head>. Valores: A4 (por defecto), A4-apaisado, A5, carta, 16:9 "
"(presentacion), email. Opciones separadas por ';': margen=MM (20 por defecto), numeros=no, pie=texto con {n} {N} {titulo}, "
"cabecera=texto (desde la pagina 2). Pon tambien <title>.\n"
"HOJA DE ESTILO DE LA CASA (ya cargada; NO la repitas, usala con estas clases): tipografia en pt, h1-h4, p justificado, tablas con "
"cabecera oscura y filas alternas (td.num alinea cifras a la derecha; caption), blockquote/.cita, figure+figcaption. Portada: .portada "
"(o .portada.pagina-completa para una pagina entera), .sobretitulo, .titulo, .subtitulo, .autores, .meta, .afiliacion, .resumen, "
".palabras-clave. Rejillas: .columnas (2), .columnas-3, .lateral (2:1). Recuadros: .destacado, .aviso, .exito. Indicadores: "
"<div class=\"kpis\"><div class=\"kpi\"><span class=\"valor\">4,8 M€</span><span class=\"etiqueta\">Facturacion</span></div>...</div> "
"(4 por fila). Barras: <div class=\"barra\"><span style=\"width:62%\">62 %</span></div>. Revista: .entradilla, .firma-articulo, "
".cita-grande. Carta: .remitente, .destinatario, .fecha, .firma. Libro: .capitulo, .titulo-capitulo, .libro. Presentacion: "
"section.diapositiva (.oscura, .acento), .pie-diapositiva. Email: .email, .email-cabecera, .email-cuerpo, .email-pie, a.boton. "
"Utiles: .centro, .derecha, .tenue, .nota-pie, <div class=\"salto-pagina\"></div> (salto de pagina).\n"
"COLOR: elige un tema coherente con el asunto cambiando SOLO estas variables en <style>:root{...}</style>: --acento, --acento-oscuro, "
"--acento-suave (fondo muy claro), --acento-borde, --titulos, --tinta. Un acento y sus tonos; nada de arcoiris.\n"
"GRAFICOS: SVG en linea con viewBox (p. ej. viewBox=\"0 0 600 260\" width=\"600\"), rect para barras, polyline para lineas, <text> con "
"font-size 11-13 para ejes, valores y leyenda, colores del tema. Siempre con figcaption. Nada de imagenes externas ni de Internet: "
"solo las rutas que te de el usuario.\n"
"PROHIBIDO: JavaScript, fuentes o CSS externos, position:absolute/fixed, float, tamanos en vw, texto de relleno (lorem ipsum), "
"placeholders como [Nombre] (inventa datos verosimiles y coherentes si no te los dan, sin presentar como reales hechos sobre personas "
"reales).\n"
"TIPOGRAFIA Y ESTILO (espanol de Espana): comillas «», raya —, decimales con coma y miles con punto (4.820; 12,4 %), espacio antes de "
"%, €, ºC; fechas '21 de noviembre de 2026'; titulos en minuscula salvo la inicial. Jerarquia clara, parrafos de 3-6 lineas, una idea "
"por parrafo, cifras concretas, conclusiones primero. Longitud: la que pida el usuario; si no dice nada, 2-4 paginas A4 (o 8-10 "
"diapositivas). El HTML debe ocupar menos de 40 KB.";

static const char SYS_REVIEW[] =
"Eres director de arte y corrector de estilo de una editorial. Revisas un documento que SOMA ha maquetado y que se va a exportar a PDF. "
"Te doy la peticion original, el informe automatico de la maquetacion, el codigo del documento y, si puedes verlas, las paginas tal y "
"como se imprimiran. Busca: texto cortado o que se sale, solapes, paginas casi vacias o huecos grandes, viudas (un titulo solo al pie "
"de pagina), tablas o graficos ilegibles o mal proporcionados, jerarquia confusa, contraste pobre, exceso de texto en diapositivas, "
"incoherencias de datos, faltas de ortografia, comillas/decimales mal, contenido que no responde a la peticion.\n"
"Responde EXACTAMENTE asi: primera linea 'VEREDICTO: PUBLICABLE' si no hay nada importante que cambiar (y nada mas). Si hay que "
"cambiar algo: 'VEREDICTO: CORREGIR', luego 'PROBLEMAS:' con una lista breve (- ...), y despues el documento COMPLETO corregido "
"(desde <!DOCTYPE html> hasta </html>, o el LaTeX completo), con las mismas reglas de la hoja de estilo de la casa. No cambies lo que "
"esta bien.";

static const char SYS_UI[] =
"Eres disenador de interfaces. Te doy el encargo de un programa de SOMA y la captura de su ventana recien abierta. Evalua: que se vea "
"lo que pide el encargo, legibilidad (tamano y contraste del texto), alineacion y margenes, elementos cortados o solapados, "
"botones distinguibles, coherencia de colores, espacio desaprovechado. Responde 'VEREDICTO: BIEN' si esta correcto, o "
"'VEREDICTO: MEJORAR' y una lista de como mucho 6 cambios concretos y aplicables en el codigo (coordenadas, tamanos, colores en "
"hexadecimal, textos). Nada mas.";

const char *docagent_system_prompt(void) { return SYS_DOC; }

/* ================================================================ piezas puras */
uint32_t docagent_extract(const char *r, char *out, uint32_t cap, int *is_tex)
{
    if (is_tex) *is_tex = 0;
    if (!r || !out || cap < 2) return 0;
    const char *a = find_ci(r, "<!DOCTYPE"), *e = 0;
    if (!a) a = find_ci(r, "<html");
    if (a) { e = find_ci(a, "</html>"); e = e ? e + 7 : a + dlen(a); }
    else if ((a = find_ci(r, "\\documentclass"))) {
        const char *pre = a;                                     /* %soma-pagina: justo antes */
        const char *sp = find_ci(r, "%soma-pagina:"); if (sp && sp < a) pre = sp;
        a = pre; e = find_ci(a, "\\end{document}"); e = e ? e + 14 : a + dlen(a);
        if (is_tex) *is_tex = 1;
    } else if ((a = find_ci(r, "<body")) || (a = find_ci(r, "<section")) || (a = find_ci(r, "<div")) || (a = find_ci(r, "<h1"))) {
        e = a + dlen(a);                                         /* trozo de HTML sin cabecera: se acepta */
    } else return 0;
    const char *f = find_ci(a, "```"); if (f && f < e) e = f;
    uint32_t n = (uint32_t)(e - a); if (n >= cap) n = cap - 1;
    for (uint32_t i = 0; i < n; i++) out[i] = a[i];
    out[n] = 0;
    return n;
}
int docagent_verdict(const char *r)
{
    const char *v = r ? find_ci(r, "VEREDICTO") : 0;
    if (!v) return -1;
    v += 9; while (*v == ':' || *v == ' ' || *v == '*') v++;
    if (find_ci(v, "PUBLICABLE") == v || find_ci(v, "BIEN") == v || find_ci(v, "OK") == v) return 1;
    return 0;
}
void docagent_slug(const char *t, char *out, int max)
{
    int n = 0, us = 1;
    const char *p = t ? t : "";
    while (*p && n < max - 1) {
        int c = fold(&p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[n++] = (char)c; us = 0; }
        else if (!us && n) { out[n++] = '_'; us = 1; }
    }
    if (*p && n >= max - 1) {                                             /* cortado a media palabra: hasta la anterior */
        const char *q = p; int c = fold(&q);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { int k = n; while (k > 0 && out[k - 1] != '_') k--; if (k > 6) n = k; }
    }
    while (n && out[n - 1] == '_') n--;
    if (!n) { const char *d = "documento"; while (*d && n < max - 1) out[n++] = *d++; }
    out[n] = 0;
}

#ifndef WEB_HOST_TEST
/* ================================================================ nucleo: LLM, archivos, pantalla */
#include "llm.h"
#include "llm_proto.h"
#include "fs.h"
#include "mem.h"
#include "console.h"
#include "gui.h"
#include "png_enc.h"
#include "pdfw.h"
#include "rtc.h"
#include "thread.h"

#define DOC_MAX     (96u * 1024u)
#define REPLY_MAX   (160u * 1024u)
#define DIR_DOCS    "/workspace/docs/"
#define LLM_TIMEOUT 300000u

static void (*g_prog)(const char *);
void docagent_set_progress(void (*cb)(const char *msg)) { g_prog = cb; }
static void prog(const char *m)
{
    kprint("\033[1;35m[MAQUETADOR]\033[0m "); kprint(m); kprint("\n");
    if (g_prog) g_prog(m);
    gui_thought_update(m);
}
static void put(char *o, uint32_t max, uint32_t *p, const char *s) { while (s && *s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void putn(char *o, uint32_t max, uint32_t *p, int v) { char t[12]; int k = 0; if (v < 0) { put(o, max, p, "-"); v = -v; } do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11); char r[12]; for (int i = 0; i < k; i++) r[i] = t[k - 1 - i]; r[k] = 0; put(o, max, p, r); }
static void scpy(char *d, const char *s, uint32_t max) { uint32_t i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } if (max) d[i] = 0; }
static int ends(const char *s, const char *e) { int a = dlen(s), b = dlen(e); if (a < b) return 0; for (int i = 0; i < b; i++) if (lowc((unsigned char)s[a - b + i]) != lowc((unsigned char)e[i])) return 0; return 1; }

/* escribir sin que la GUI abra el archivo en el Bloc de notas (lo abre el maquetador al final) */
static int wr(const char *path, const void *d, uint32_t n)
{
    void (*h)(const char *, const char *, uint32_t) = vfs_live_hook;
    vfs_live_hook = 0;
    int r = vfs_write(path, (const char *)d, n);
    vfs_live_hook = h;
    return r;
}
static int img_loader(const char *url, uint8_t **data, uint32_t *len)
{
    char full[128]; uint32_t q = 0;
    if (url[0] != '/') put(full, sizeof full, &q, DIR_DOCS);
    put(full, sizeof full, &q, url);
    char *b = vfs_read_alloc(full, len);
    if (!b && url[0] != '/') { q = 0; put(full, sizeof full, &q, "/workspace/"); put(full, sizeof full, &q, url); b = vfs_read_alloc(full, len); }
    if (!b) return -1;
    *data = (uint8_t *)b; return 0;
}

/* abrir/maquetar con turno de pantalla (las fuentes y el destino de dibujo son compartidos con la GUI). dg_close tambien: libera
 * imagenes que la cache de fuentes no toca, pero asi todo el trabajo con el motor queda serializado con la GUI. */
static struct dg_doc *open_doc(const char *src, uint32_t n, int zoom, char *err, uint32_t emax)
{
    dg_set_loader(img_loader);
    int tok = gui_turn_begin();
    struct dg_doc *d = dg_open(src, n, zoom, err, emax);
    gui_turn_end(tok);
    return d;
}
static int render(struct dg_doc *d, int page, uint32_t *px) { int tok = gui_turn_begin(); int r = dg_render_page(d, page, px); gui_turn_end(tok); return r; }

static int review_zoom(const char *src, uint32_t n)
{
    int tok_slides = 0;
    for (uint32_t i = 0; i + 4 < n && i < 4096; i++) if (src[i] == '1' && src[i + 1] == '6' && src[i + 2] == ':' && src[i + 3] == '9') { tok_slides = 1; break; }
    return tok_slides ? 70 : 100;
}

/* paginas en PNG para el revisor (las primeras y la ultima, hasta 4) */
struct shots { uint8_t *png[4]; uint32_t len[4]; int page[4]; int n; };
static void shots_free(struct shots *s) { for (int i = 0; i < s->n; i++) if (s->png[i]) kfree(s->png[i]); s->n = 0; }
static void take_shots(struct dg_doc *d, struct shots *s)
{
    s->n = 0;
    int np = dg_npages(d), w, h; dg_page_px(d, &w, &h);
    if (np <= 0 || w <= 0 || h <= 0) return;
    if (h > 2600) h = 2600;                                         /* email muy largo: basta con la parte de arriba */
    int pick[4], k = 0;
    for (int i = 0; i < np && k < 3; i++) pick[k++] = i;
    if (np > 3) pick[k++] = np - 1;
    int fw, fh; dg_page_px(d, &fw, &fh);
    uint32_t *px = (uint32_t *)kmalloc((size_t)fw * (size_t)fh * 4u);
    if (!px) return;
    size_t cap = (size_t)w * h * 3u + 65536u, wk = png_work_size(w, h);
    void *work = kmalloc(wk);
    for (int i = 0; i < k && work; i++) {
        if (render(d, pick[i], px) != 0) continue;
        uint8_t *png = (uint8_t *)kmalloc(cap); if (!png) break;
        size_t pn = png_encode_rgb(px, w, h, png, cap, work);
        if (!pn) { kfree(png); continue; }
        s->png[s->n] = png; s->len[s->n] = (uint32_t)pn; s->page[s->n] = pick[i] + 1; s->n++;
    }
    if (work) kfree(work);
    kfree(px);
}

static int issues_of(struct dg_doc *d, char *out, uint32_t max) { return dg_check(d, out, max); }

static void today(char *o, uint32_t max)
{
    static const char *const M[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    struct rtc_time t; rtc_get_datetime(&t);
    uint32_t p = 0; o[0] = 0;
    if (t.year < 2024 || t.month < 1 || t.month > 12) return;
    putn(o, max, &p, t.day); put(o, max, &p, " de "); put(o, max, &p, M[t.month - 1]); put(o, max, &p, " de "); putn(o, max, &p, t.year);
}

/* ---- una ronda de revision: devuelve 1 si ha cambiado el documento */
static int review_round(const char *mission, char *doc, uint32_t *dlen_, int *is_tex, char *reply, char *log, uint32_t logmax, uint32_t *lp, int round)
{
    char err[160];
    struct dg_doc *d = open_doc(doc, *dlen_, review_zoom(doc, *dlen_), err, sizeof err);
    if (!d) { put(log, logmax, lp, "No se pudo maquetar: "); put(log, logmax, lp, err); put(log, logmax, lp, "\n"); return 0; }
    static char chk[4096]; int nchk = issues_of(d, chk, sizeof chk);
    int np = dg_npages(d);
    struct shots s; s.n = 0;
    int vision = !llm_vision_unsupported();
    if (vision) take_shots(d, &s);
    dg_close(d);

    uint32_t cap = *dlen_ + 8192u;
    char *prompt = (char *)kmalloc(cap);
    if (!prompt) { shots_free(&s); return 0; }
    uint32_t p = 0;
    put(prompt, cap, &p, "PETICION ORIGINAL:\n"); put(prompt, cap, &p, mission);
    put(prompt, cap, &p, "\n\nMAQUETACION: "); putn(prompt, cap, &p, np); put(prompt, cap, &p, np == 1 ? " pagina." : " paginas.");
    put(prompt, cap, &p, nchk ? "\nINFORME AUTOMATICO:\n" : "\nINFORME AUTOMATICO: sin problemas medibles.\n"); if (nchk) put(prompt, cap, &p, chk);
    if (s.n) { put(prompt, cap, &p, "\nTe adjunto las paginas "); for (int i = 0; i < s.n; i++) { if (i) put(prompt, cap, &p, ", "); putn(prompt, cap, &p, s.page[i]); } put(prompt, cap, &p, " renderizadas tal y como saldran en el PDF.\n"); }
    else put(prompt, cap, &p, "\n(No puedes ver las paginas: juzga por el informe y el codigo.)\n");
    put(prompt, cap, &p, "\nDOCUMENTO:\n"); put(prompt, cap, &p, doc);

    struct llm_img im[4]; for (int i = 0; i < s.n; i++) { im[i].data = s.png[i]; im[i].len = s.len[i]; im[i].mime = "image/png"; }
    char m[64]; uint32_t q = 0; put(m, sizeof m, &q, s.n ? "Revisando las paginas con vision (ronda " : "Revisando la maquetacion (ronda "); putn(m, sizeof m, &q, round); put(m, sizeof m, &q, ")...");
    prog(m);
    int ok = llm_chat_ex(SYS_REVIEW, prompt, im, s.n, 16000, reply, REPLY_MAX, LLM_TIMEOUT);
    if (ok == -2 && s.n) {                                          /* el modelo no ve imagenes: misma revision sin ellas */
        prog("El modelo no admite imagenes: reviso solo con el informe de la maquetacion");
        p = 0; put(prompt, cap, &p, "");                           /* se reconstruye sin la frase de las paginas */
        put(prompt, cap, &p, "PETICION ORIGINAL:\n"); put(prompt, cap, &p, mission);
        put(prompt, cap, &p, "\n\nMAQUETACION: "); putn(prompt, cap, &p, np); put(prompt, cap, &p, " paginas.\n");
        put(prompt, cap, &p, nchk ? "INFORME AUTOMATICO:\n" : "INFORME AUTOMATICO: sin problemas medibles.\n"); if (nchk) put(prompt, cap, &p, chk);
        put(prompt, cap, &p, "\n(No puedes ver las paginas: juzga por el informe y el codigo.)\n\nDOCUMENTO:\n"); put(prompt, cap, &p, doc);
        ok = llm_chat_ex(SYS_REVIEW, prompt, 0, 0, 16000, reply, REPLY_MAX, LLM_TIMEOUT);
    }
    shots_free(&s);
    kfree(prompt);
    if (ok != 1) { put(log, logmax, lp, "Revision sin respuesta del modelo: "); put(log, logmax, lp, llm_last_error); put(log, logmax, lp, "\n"); return 0; }
    int v = docagent_verdict(reply);
    put(log, logmax, lp, "Ronda "); putn(log, logmax, lp, round); put(log, logmax, lp, v == 1 ? ": publicable.\n" : ": hay que corregir.\n");
    const char *pr = find_ci(reply, "PROBLEMAS");
    if (pr) { const char *e = find_ci(pr, "<!DOCTYPE"); if (!e) e = find_ci(pr, "<html"); if (!e) e = find_ci(pr, "\\documentclass"); if (!e) e = find_ci(pr, "%soma-pagina"); uint32_t k = 0; while (pr + k < (e ? e : pr + 1500) && pr[k] && k < 1500) k++; for (uint32_t i = 0; i < k && *lp + 1 < logmax; i++) log[(*lp)++] = pr[i]; log[*lp] = 0; put(log, logmax, lp, "\n"); }
    if (v == 1) return 0;
    /* documento corregido: se acepta solo si se maqueta y no empeora */
    static char cand[DOC_MAX]; int ctex = 0;
    uint32_t cn = docagent_extract(reply, cand, sizeof cand, &ctex);
    if (cn < 200) { put(log, logmax, lp, "La revision no trajo el documento corregido.\n"); return 0; }
    struct dg_doc *d2 = open_doc(cand, cn, 100, err, sizeof err);
    if (!d2) { put(log, logmax, lp, "La version corregida no se maqueta; se conserva la anterior.\n"); return 0; }
    static char chk2[4096]; int nchk2 = issues_of(d2, chk2, sizeof chk2); int np2 = dg_npages(d2);
    dg_close(d2);
    if (nchk2 > nchk || np2 <= 0) { put(log, logmax, lp, "La version corregida tiene mas problemas; se conserva la anterior.\n"); return 0; }
    for (uint32_t i = 0; i <= cn; i++) doc[i] = cand[i];
    *dlen_ = cn; *is_tex = ctex;
    return 1;
}

/* ---- exportar */
static int export_pdf(const char *src, uint32_t n, const char *dst, char *err, uint32_t emax, int *pages, uint32_t *bytes)
{
    int z = review_zoom(src, n) == 70 ? 150 : 200;                   /* 16:9 a 1920x1080; A4 a 192 ppp */
    struct dg_doc *d = open_doc(src, n, z, err, emax);
    if (!d) return 0;
    int w, h, wp, hp; dg_page_px(d, &w, &h); dg_page_pt(d, &wp, &hp);
    uint32_t *px = (uint32_t *)kmalloc((size_t)w * (size_t)h * 4u);
    struct pdfw *pdf = pdfw_new(dg_title(d), "SOMA Maquetador");
    int ok = px && pdf;
    for (int i = 0; ok && i < dg_npages(d); i++) {
        if (render(d, i, px) != 0 || pdfw_page(pdf, px, w, h, wp, hp) != 0) ok = 0;
        thread_sleep(0);
    }
    if (pages) *pages = dg_npages(d);
    dg_close(d);
    if (px) kfree(px);
    if (!ok) { if (pdf) pdfw_free(pdf); scpy(err, "sin memoria para el PDF", emax); return 0; }
    uint32_t len = 0; uint8_t *b = pdfw_finish(pdf, &len);
    if (!b) { scpy(err, "no se pudo cerrar el PDF", emax); return 0; }
    int r = wr(dst, b, len);
    kfree(b);
    if (bytes) *bytes = len;
    if (r < 0) { scpy(err, "no se pudo guardar el PDF (sin espacio?)", emax); return 0; }
    return 1;
}
static int export_html(const char *src, uint32_t n, const char *dst, char *err, uint32_t emax)
{
    struct dg_doc *d = open_doc(src, n, 100, err, emax);
    if (!d) return 0;
    uint32_t cap = n + 16384u; char *o = (char *)kmalloc(cap);
    int k = o ? dg_export_html(d, src, n, o, cap) : -1;
    dg_close(d);
    if (k <= 0) { if (o) kfree(o); scpy(err, "solo se exportan a HTML los documentos HTML", emax); return 0; }
    int r = wr(dst, o, (uint32_t)k); kfree(o);
    if (r < 0) { scpy(err, "no se pudo guardar", emax); return 0; }
    return 1;
}
static int export_png(const char *src, uint32_t n, const char *base, char *err, uint32_t emax, int *pages)
{
    struct dg_doc *d = open_doc(src, n, 150, err, emax);
    if (!d) return 0;
    int w, h; dg_page_px(d, &w, &h);
    uint32_t *px = (uint32_t *)kmalloc((size_t)w * (size_t)h * 4u);
    size_t cap = (size_t)w * h * 3u + 65536u, wk = png_work_size(w, h);
    uint8_t *png = (uint8_t *)kmalloc(cap); void *work = kmalloc(wk);
    int ok = px && png && work, np = dg_npages(d);
    for (int i = 0; ok && i < np; i++) {
        if (render(d, i, px) != 0) { ok = 0; break; }
        size_t pn = png_encode_rgb(px, w, h, png, cap, work);
        char nm[64]; uint32_t q = 0; put(nm, sizeof nm, &q, base); put(nm, sizeof nm, &q, "-"); putn(nm, sizeof nm, &q, i + 1); put(nm, sizeof nm, &q, ".png");
        if (!pn || wr(nm, png, (uint32_t)pn) < 0) ok = 0;
    }
    if (pages) *pages = np;
    dg_close(d);
    if (px) kfree(px);
    if (png) kfree(png);
    if (work) kfree(work);
    if (!ok) scpy(err, "no se pudieron escribir las imagenes", emax);
    return ok;
}
static void base_of(const char *path, char *o, uint32_t max)
{
    scpy(o, path, max);
    int n = dlen(o);
    for (int i = n - 1; i >= 0 && o[i] != '/'; i--) if (o[i] == '.') { o[i] = 0; break; }
}

int docagent_export(const char *path, const char *fmt, const char *dst, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    uint32_t n = 0; char *src = vfs_read_alloc(path, &n);
    if (!src) { put(out, max, &p, "No encuentro "); put(out, max, &p, path); return 0; }
    char base[64]; base_of(path, base, sizeof base);
    char d[64]; char err[160] = ""; int ok = 0, pages = 0; uint32_t bytes = 0;
    if (has(fmt, "pdf")) { if (dst && *dst) scpy(d, dst, sizeof d); else { uint32_t q = 0; d[0] = 0; put(d, sizeof d, &q, base); put(d, sizeof d, &q, ".pdf"); }
        prog("Exportando a PDF..."); ok = export_pdf(src, n, d, err, sizeof err, &pages, &bytes); }
    else if (has(fmt, "htm")) { if (dst && *dst) scpy(d, dst, sizeof d); else { uint32_t q = 0; d[0] = 0; put(d, sizeof d, &q, base); put(d, sizeof d, &q, ends(path, ".html") ? "-web.html" : ".html"); }
        ok = export_html(src, n, d, err, sizeof err); }
    else if (has(fmt, "png") || has(fmt, "imagen")) { scpy(d, dst && *dst ? dst : base, sizeof d); ok = export_png(src, n, d, err, sizeof err, &pages); }
    else { put(out, max, &p, "Formatos: pdf, html, png"); kfree(src); return 0; }
    kfree(src);
    if (!ok) { put(out, max, &p, "No se pudo exportar: "); put(out, max, &p, err); return 0; }
    put(out, max, &p, "Exportado: "); put(out, max, &p, d);
    if (has(fmt, "png")) put(out, max, &p, "-N.png");
    if (pages) { put(out, max, &p, " ("); putn(out, max, &p, pages); put(out, max, &p, pages == 1 ? " pagina" : " paginas"); if (bytes) { put(out, max, &p, ", "); putn(out, max, &p, (int)(bytes / 1024)); put(out, max, &p, " KiB"); } put(out, max, &p, ")"); }
    return 1;
}

/* ---- escribir un documento */
int docagent_run(const char *mission, char *out, uint32_t max, char *path_out, uint32_t path_max)
{
    uint32_t op = 0; if (max) out[0] = 0;
    if (path_out && path_max) path_out[0] = 0;
    int ki = kind_index(mission);
    char *reply = (char *)kmalloc(REPLY_MAX);
    char *doc = (char *)kmalloc(DOC_MAX);
    char *prompt = (char *)kmalloc(8192);
    static char log[6144]; uint32_t lp = 0; log[0] = 0;
    if (!reply || !doc || !prompt) { put(out, max, &op, "Sin memoria para el maquetador"); goto fin0; }
    {
        char hoy[48]; today(hoy, sizeof hoy);
        uint32_t p = 0;
        put(prompt, 8192, &p, "PETICION DEL USUARIO:\n"); put(prompt, 8192, &p, mission);
        put(prompt, 8192, &p, "\n\nTIPO DE DOCUMENTO Y RECETA: "); put(prompt, 8192, &p, KINDS[ki].recipe);
        if (hoy[0]) { put(prompt, 8192, &p, "\n\nFECHA DE HOY: "); put(prompt, 8192, &p, hoy); }
        { uint32_t bn = 0; char *bb = vfs_read_alloc("/agent/blackboard.txt", &bn);                /* notas del buscador, si las hay */
          if (bb) { if (bn > 3000) bb[3000] = 0; put(prompt, 8192, &p, "\n\nNOTAS DE INVESTIGACION (usalas si vienen al caso; no copies basura):\n"); put(prompt, 8192, &p, bb); kfree(bb); } }
        put(prompt, 8192, &p, "\n\nEscribe ahora el documento completo.");
        prog("Redactando y maquetando el documento...");
        int ok = llm_chat_ex(SYS_DOC, prompt, 0, 0, 16000, reply, REPLY_MAX, LLM_TIMEOUT);
        if (ok != 1) { put(out, max, &op, "El modelo no ha respondido: "); put(out, max, &op, llm_last_error); goto fin0; }
    }
    int is_tex = 0;
    uint32_t n = docagent_extract(reply, doc, DOC_MAX, &is_tex);
    if (n < 100) { put(out, max, &op, "La respuesta del modelo no trae un documento HTML ni LaTeX."); wr("/agent/doc_respuesta.txt", reply, (uint32_t)dlen(reply)); goto fin0; }
    {
        char err[160];
        struct dg_doc *d = open_doc(doc, n, 100, err, sizeof err);
        if (!d) { put(out, max, &op, "El documento no se pudo maquetar: "); put(out, max, &op, err); wr("/agent/doc_respuesta.txt", doc, n); goto fin0; }
        char title[128]; scpy(title, dg_title(d), sizeof title);
        dg_close(d);
        if (!title[0]) scpy(title, KINDS[ki].name, sizeof title);
        /* revision (1-2 rondas: la segunda solo si la primera cambio algo) */
        for (int r = 1; r <= 2; r++) if (!review_round(mission, doc, &n, &is_tex, reply, log, sizeof log, &lp, r)) break;
        char slug[23]; docagent_slug(title, slug, sizeof slug);   /* el VFS admite nombres de 47 caracteres */
        char path[64]; uint32_t q = 0; put(path, sizeof path, &q, DIR_DOCS); put(path, sizeof path, &q, slug); put(path, sizeof path, &q, is_tex ? ".tex" : ".html");
        if (wr(path, doc, n) < 0) { put(out, max, &op, "No se pudo guardar "); put(out, max, &op, path); goto fin0; }
        char pdf[64]; q = 0; put(pdf, sizeof pdf, &q, DIR_DOCS); put(pdf, sizeof pdf, &q, slug); put(pdf, sizeof pdf, &q, ".pdf");
        prog("Exportando a PDF...");
        int pages = 0; uint32_t bytes = 0;
        int pok = export_pdf(doc, n, pdf, err, sizeof err, &pages, &bytes);
        wr("/agent/doc_revision.txt", log, lp);
        put(out, max, &op, "Documento listo: "); put(out, max, &op, title);
        put(out, max, &op, "\n  Fuente: "); put(out, max, &op, path);
        if (pok) { put(out, max, &op, "\n  PDF:    "); put(out, max, &op, pdf); put(out, max, &op, " ("); putn(out, max, &op, pages); put(out, max, &op, pages == 1 ? " pagina, " : " paginas, "); putn(out, max, &op, (int)(bytes / 1024)); put(out, max, &op, " KiB)"); }
        else { put(out, max, &op, "\n  PDF: no se pudo crear ("); put(out, max, &op, err); put(out, max, &op, ")"); }
        put(out, max, &op, "\n  Revision: "); put(out, max, &op, lp ? "ver /agent/doc_revision.txt" : "sin cambios");
        put(out, max, &op, llm_vision_unsupported() ? " (el modelo no ve imagenes: revision por informe)" : "");
        put(out, max, &op, "\n  Mas formatos: documento exportar "); put(out, max, &op, path); put(out, max, &op, " html|png");
        if (path_out) scpy(path_out, path, path_max);
        kfree(reply); kfree(doc); kfree(prompt);
        return 1;
    }
fin0:
    if (reply) kfree(reply);
    if (doc) kfree(doc);
    if (prompt) kfree(prompt);
    return 0;
}

int docagent_review(const char *path, const char *wishes, char *out, uint32_t max)
{
    uint32_t op = 0; if (max) out[0] = 0;
    uint32_t n = 0; char *src = vfs_read_alloc(path, &n);
    if (!src) { put(out, max, &op, "No encuentro "); put(out, max, &op, path); return 0; }
    char *doc = (char *)kmalloc(DOC_MAX), *reply = (char *)kmalloc(REPLY_MAX);
    if (!doc || !reply || n + 1 > DOC_MAX) { put(out, max, &op, n + 1 > DOC_MAX ? "Documento demasiado grande para revisarlo" : "Sin memoria"); goto fin; }
    for (uint32_t i = 0; i < n; i++) doc[i] = src[i];
    doc[n] = 0;
    {
        int is_tex = ends(path, ".tex");
        static char log[6144]; uint32_t lp = 0; log[0] = 0;
        char mission[600]; uint32_t q = 0;
        put(mission, sizeof mission, &q, "Revisar y mejorar el acabado profesional del documento ");
        put(mission, sizeof mission, &q, path);
        if (wishes && *wishes) { put(mission, sizeof mission, &q, ". Indicaciones del usuario: "); put(mission, sizeof mission, &q, wishes); }
        int changed = review_round(mission, doc, &n, &is_tex, reply, log, sizeof log, &lp, 1);
        wr("/agent/doc_revision.txt", log, lp);
        if (changed) {
            char bak[64]; base_of(path, bak, sizeof bak); uint32_t b = (uint32_t)dlen(bak); put(bak, sizeof bak, &b, "-ant"); put(bak, sizeof bak, &b, is_tex ? ".tex" : ".html");
            wr(bak, src, dlen(src));
            wr(path, doc, n);
            put(out, max, &op, "Documento mejorado y guardado (la version anterior queda en "); put(out, max, &op, bak); put(out, max, &op, ").\n");
        } else put(out, max, &op, "Sin cambios: el revisor lo da por bueno o su propuesta no mejoraba.\n");
        put(out, max, &op, log);
    }
    kfree(src); kfree(doc); kfree(reply);
    return 1;
fin:
    kfree(src); if (doc) kfree(doc); if (reply) kfree(reply);
    return 0;
}

int docagent_ui_review(const char *mission, const uint8_t *png, uint32_t len, char *out, uint32_t max)
{
    if (max) out[0] = 0;
    if (llm_vision_unsupported()) return -2;
    char prompt[1200]; uint32_t p = 0;
    put(prompt, sizeof prompt, &p, "ENCARGO DEL PROGRAMA:\n"); put(prompt, sizeof prompt, &p, mission);
    put(prompt, sizeof prompt, &p, "\n\nTe adjunto la captura de su ventana.");
    struct llm_img im = { png, len, "image/png" };
    static char reply[8192];
    int ok = llm_chat_ex(SYS_UI, prompt, &im, 1, 1500, reply, sizeof reply, 120000);
    if (ok != 1) return ok == -2 ? -2 : -1;
    int v = docagent_verdict(reply);
    if (v == 1) return 0;
    const char *l = find_ci(reply, "VEREDICTO"); if (l) { while (*l && *l != '\n') l++; } else l = reply;
    while (*l == '\n' || *l == '\r') l++;
    scpy(out, l, max);
    return out[0] ? 1 : 0;
}
#endif
