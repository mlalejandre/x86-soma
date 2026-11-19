#ifndef SOMA_PDFVIEW_H
#define SOMA_PDFVIEW_H
#include <stdint.h>
/* PARCHE 088: lector de PDF con la estetica del explorador */
void pdfview_init(void);
int  pdfview_open_file(const char *path);        /* 0 = abierto */
void pdfview_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
int  pdfview_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void pdfview_handle_key(char c);
int  pdfview_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held);   /* 1 = repintar */
int  pdfview_clip_op(int op);
/* SOMA_213_PDF: varias ventanas (como notepad_inst_*) */
int  pdfview_inst_new(void);
void pdfview_select(int slot);
void pdfview_inst_drop(int slot);
int  pdfview_inst_find(const char *path);
const char *pdfview_inst_name(int slot);
#endif
