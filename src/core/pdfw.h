#ifndef SOMA_PDFW_H
#define SOMA_PDFW_H
/* SOMA_248_DOC: escritor de PDF minimo y correcto (PDF 1.4). Cada pagina es una imagen RGB comprimida con Flate y predictor PNG
 * (los mismos datos que el IDAT de un PNG: png_encode_rgb hace el trabajo), a su tamano real en puntos. Titulo en el diccionario
 * Info (UTF-16BE, admite acentos). Lo abren el visor de PDF de SOMA, los navegadores, Acrobat, Preview, Evince e imprentas.
 * Sin estado global; memoria con kmalloc. Pruebas en el host: tests/test_docgen_248.c. */
#include <stdint.h>
#include <stddef.h>

struct pdfw;
struct pdfw *pdfw_new(const char *title_utf8, const char *creator);
/* argb: w*h pixeles; w_pt/h_pt: tamano de la pagina en puntos (1/72"). 0 = bien, <0 = sin memoria o error de codificacion. */
int  pdfw_page(struct pdfw *p, const uint32_t *argb, int w, int h, int w_pt, int h_pt);
/* Cierra el documento: devuelve el PDF (kmalloc, lo libera quien llama) y su longitud; libera p. 0 si falla. */
uint8_t *pdfw_finish(struct pdfw *p, uint32_t *len);
void pdfw_free(struct pdfw *p);
/* pieza suelta (pruebas): saca los datos zlib concatenados de los IDAT de un PNG. Devuelve bytes o 0. */
size_t pdfw_png_idat(const uint8_t *png, size_t n, uint8_t *out, size_t cap);
#endif
