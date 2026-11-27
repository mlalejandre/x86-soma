#ifndef SOMA_WEB_WFONT_H
#define SOMA_WEB_WFONT_H
/* SOMA_348_FUENTES_WEB: fuentes de @font-face a TrueType (sfnt) que dibuja el rasterizador de SOMA (src/fonts/vfont.h).
 * Admite TrueType (.ttf), WOFF 1.0 (zlib) y WOFF 2.0 (Brotli, con las tablas glyf/loca/hmtx transformadas). Las fuentes
 * CFF (OpenType con contornos PostScript) y las colecciones no se admiten: devuelven error y la pagina usa la de respaldo.
 * Todo se comprueba contra el tamano de la entrada: los datos vienen de Internet. Solo en el navegador de ring 3. */
#include <stdint.h>

#define WFF_MAX_IN   (4u << 20)     /* archivo de fuente descargado */
#define WFF_MAX_OUT  (12u << 20)    /* fuente descomprimida */

/* 0 = bien: *out (kmalloc, lo libera quien llama) con *out_n bytes de un sfnt TrueType. <0 = error (ver wff_error). */
int wff_to_sfnt(const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *out_n);
const char *wff_error(int code);
/* 1 = el sfnt tiene contornos TrueType (glyf, loca, cmap...) */
int wff_is_truetype(const uint8_t *d, uint32_t n);
struct wb;
/* Content-Encoding: br. Anade a out (como mucho max_out bytes). 0 = bien, <0 = error. */
int wbr_decode(const uint8_t *in, uint32_t n, struct wb *out, uint32_t max_out);

#endif
