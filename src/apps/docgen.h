#ifndef SOMA_DOCGEN_H
#define SOMA_DOCGEN_H
/* SOMA_248_DOC: documentos paginados (informes, papers, revistas, cartas, emails, libros, presentaciones) a partir de HTML+CSS
 * (motor del navegador) o de LaTeX (motor tex.c). Maqueta a 150 % para imprimir, parte en paginas sin cortar lineas ni dejar un
 * titulo solo al final de una pagina, dibuja cada pagina en un mapa de bits (cabecera, pie y numero de pagina) y revisa el
 * resultado (lo que se sale de la pagina, imagenes que faltan, diapositivas que no caben, paginas casi vacias...).
 *
 * Formato de pagina: <meta name="soma-pagina" content="A4; margen=20mm; numeros=si; pie=Informe anual; cabecera=...">
 *   tamanos: A4, A4-apaisado, A5, carta, 16:9 (presentacion: cada .diapositiva es una pagina), email (una sola pagina larga).
 * Las clases .salto-pagina, .diapositiva y .portada (y page-break-before / break-before: page) empiezan pagina nueva.
 * No dibuja en la pantalla: usa fb_target_begin() sobre su propio bufer. Sin estado global salvo la escala del CSS mientras maqueta. */
#include <stdint.h>

#define DG_HTML 1
#define DG_TEX  2
#define DG_MAX_PAGES 200

struct dg_doc;
/* src/len: el documento (HTML o LaTeX, se detecta). zoom: escala en % (100 pantalla, 150 impresion). err: motivo si devuelve 0. */
struct dg_doc *dg_open(const char *src, uint32_t len, int zoom, char *err, uint32_t errmax);
void dg_close(struct dg_doc *d);
int  dg_npages(const struct dg_doc *d);
void dg_page_px(const struct dg_doc *d, int *w, int *h);               /* tamano de pagina en pixeles (ya escalado) */
void dg_page_pt(const struct dg_doc *d, int *w_pt, int *h_pt);         /* tamano de pagina en puntos PDF (1/72") */
int  dg_kind(const struct dg_doc *d);                                  /* DG_HTML o DG_TEX */
const char *dg_title(const struct dg_doc *d);
const char *dg_format_name(const struct dg_doc *d);                    /* "A4", "16:9", "email"... */
/* Dibuja la pagina (0 = primera) en argb (w*h del tamano de pagina). 0 = bien. */
int  dg_render_page(struct dg_doc *d, int page, uint32_t *argb);
/* Revision automatica: problemas que se ven sin mirar (uno por linea, en espanol). Devuelve cuantos. */
int  dg_check(struct dg_doc *d, char *out, uint32_t max);
/* HTML autonomo (se ve igual en cualquier navegador e imprime con el tamano de pagina): el original con la hoja de estilo de la
 * casa incrustada y una regla @page con el formato y los margenes. src/len: el HTML original. Devuelve bytes o -1 (LaTeX o no cabe). */
int  dg_export_html(const struct dg_doc *d, const char *src, uint32_t len, char *out, uint32_t max);

/* Lector de archivos para las imagenes (<img src>, \includegraphics). Lo pone la plataforma (VFS en SOMA, stdio en el anfitrion).
 * Devuelve 0 y un bufer de kmalloc (lo libera docgen con kfree). */
typedef int (*dg_load_fn)(const char *path, uint8_t **data, uint32_t *len);
void dg_set_loader(dg_load_fn f);

/* Hoja de estilo de la casa: tipografia y componentes profesionales (portada, resumen, columnas, destacados, tablas, kpi,
 * diapositivas, email...). Se anade antes de la del documento, asi que el documento puede cambiar lo que quiera. */
extern const char dg_house_css[];
#endif
