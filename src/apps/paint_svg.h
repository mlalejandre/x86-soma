#ifndef SOMA_PAINT_SVG_H
#define SOMA_PAINT_SVG_H
/* SOMA_292_PAINT_IA: dibujar en Paint con el modelo de lenguaje. El modelo responde con un SVG; Paint lo pinta en el lienzo POR PASOS
 * (primero el fondo, despues cada objeto), como si lo dibujara a mano, con el rasterizador SVG del navegador (web_svg.inc).
 * Logica pura (sin memoria ni E/S): se prueba en el anfitrion con el rasterizador de verdad (tests/test_paint_ia_292.c). */
#include <stdint.h>

#define PS_PASOS_MAX 14

/* El SVG de la respuesta del modelo: sin razonamiento (<think>), sin ```svg, sin texto alrededor. Si viene cortado (sin </svg>),
 * se cierra tras la ultima etiqueta completa. 0 = no hay SVG. */
uint32_t ps_extrae(const char *txt, char *out, uint32_t cap);
/* Fin (desplazamiento tras su '>') de cada elemento de primer nivel dentro de <svg>: fondo, grupos, formas. Devuelve cuantos. */
int  ps_trozos(const char *svg, uint32_t n, uint32_t *fin, int max);
/* Cuantos elementos se ven en el paso i (1..pasos) de un dibujo de n elementos en 'pasos' pasos */
int  ps_corte(int n, int pasos, int i);
int  ps_pasos(int n);
/* svg[0, corte) + "</svg>" en out. Devuelve la longitud (0 si no cabe). */
uint32_t ps_prefijo(const char *svg, uint32_t corte, char *out, uint32_t cap);
/* dst = argb (con su alfa) sobre base; los tres de npx pixeles; dst sale opaco (0x00RRGGBB, como el lienzo de Paint) */
void ps_mezcla(uint32_t *dst, const uint32_t *base, const uint32_t *argb, uint32_t npx);
uint32_t ps_cubiertos(const uint32_t *argb, uint32_t npx);   /* pixeles con algo de color */
/* La peticion sin "en paint", "dibujame", "pinta"...: "dibujame un faro en paint" -> "un faro" (para el titulo y para el modelo) */
void ps_tema(const char *peticion, char *out, uint32_t cap);
#endif
