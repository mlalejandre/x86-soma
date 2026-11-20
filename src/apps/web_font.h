#ifndef SOMA_WEB_FONT_H
#define SOMA_WEB_FONT_H
#include <stdint.h>
/* Texto proporcional antialiasado (DejaVu Sans incrustada) para el motor web. Anchos en 1/64 px. */
#define WF_BOLD   1
#define WF_ITALIC 2
#define WF_MONO   4
#define WF_RAW    0x40   /* PARCHE 085: sin los ajustes de fuente de la interfaz (contenido web) */
#define WF_WEB    0x80   /* SOMA_182: contenido de paginas web: usa Liberation Sans (anchos de Arial) si g_wf182_on */
#define WF_LIB    0x100  /* SOMA_184: texto con Liberation Sans aunque no sea contenido web (familia de la interfaz = Liberation) */
#define WF_ORB    0x200  /* SOMA_225: SOMA Orbital (fuente propia) */
#define WF_TEC    0x400  /* SOMA_225: SOMA Tecnica (fuente propia) */
extern int g_wf182_on;   /* SOMA_182: definido en web_css.c; lo fija web_page.c segun soma.webfont=off */
int  wf_init(void);                                             /* 1 si hay fuente */
int  wf_width64(const char *s, uint32_t n, int px, int fl);     /* ancho en 1/64 px (UTF-8) */
int  wf_width(const char *s, uint32_t n, int px, int fl);       /* ancho en px (redondeado arriba) */
int  wf_cp_adv64(uint32_t cp, int px, int fl);
void wf_draw(int32_t x, int32_t baseline_y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int alpha);
int  wf_ascent(int px);
int  wf_descent(int px);
int  wf_web_ascent(int px);                                     /* SOMA_182: como wf_ascent, para contenido web */
int  wf_web_descent(int px);
/* PARCHE 085: fuente de la interfaz (Ajustes): familia (0 sans, 1 mono) y escala en %. wf_ui_gen cambia con cada ajuste. */
void wf_ui_set(int mono, int scale);
/* SOMA_184: familia de la interfaz y los documentos: WF_FAM_SANS (DejaVu Sans), WF_FAM_MONO (DejaVu Sans Mono), WF_FAM_LIBERATION */
#define WF_FAM_SANS 0
#define WF_FAM_MONO 1
#define WF_FAM_LIBERATION 2
#define WF_FAM_ORBITAL 3      /* SOMA_225: fuentes propias */
#define WF_FAM_TECNICA 4
void wf_ui_set_family(int fam, int scale);
int  wf_ui_family(void);
int  wf_ui_mono(void);
int  wf_ui_scale(void);
uint32_t wf_ui_gen(void);
int  wf_ui_px(int px);
#endif
