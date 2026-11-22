/* Sustitutos para probar lang/vm/lexer en host: GUI, framebuffer, agentes y un VFS minimo. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "host_stubs.h"
#include "agent.h"

/* SOMA_267_DISCOS: el tamano sale de agent.h (desde el parche 260 son 384 KiB; antes se declaraba de 8 KiB aqui y no compilaba) */
#ifndef AGENT_RESP_MAX
#define AGENT_RESP_MAX 8192
#endif
char agent_last_response[AGENT_RESP_MAX];

/* vm.c los declara como simbolos debiles; el enlazador de macOS exige que existan. */
int mouse_x = 0;
int mouse_y = 0;
int mouse_buttons = 0;

void agent_run(const char *a, const char *m) { (void)a; (void)m; }

int  fb_is_active(void) { return 0; }
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void fb_present(void) { }

int  gui_soma_win_open(const char *t, int w, int h) { (void)t;(void)w;(void)h; return 0; }
int  gui_soma_win_is_active(void) { return 0; }
void gui_soma_win_close(void) { }
void gui_soma_win_clear(uint32_t c) { (void)c; }
void gui_soma_win_rect(int x, int y, int w, int h, uint32_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void gui_soma_win_pixel(int x, int y, uint32_t c) { (void)x;(void)y;(void)c; }
void gui_soma_win_line(int a, int b, int c, int d, uint32_t e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void gui_soma_win_circle(int a, int b, int r, uint32_t c) { (void)a;(void)b;(void)r;(void)c; }
void gui_soma_win_text(int x, int y, const char *s, uint32_t c) { (void)x;(void)y;(void)s;(void)c; }
void gui_soma_win_present(void) { }
void gui_soma_win_get_mouse(int *a, int *b, int *c, int *d, int *e) { *a = *b = *c = *d = *e = 0; }
int  gui_soma_win_get_key(void) { return 0; }
void gui_soma_win_get_size(int *w, int *h) { *w = 0; *h = 0; }

/* VFS minimo en memoria (tabla de 16 archivos). */
#define H_FILES 16
static struct { char name[64]; char data[4096]; uint32_t size; int used; } hf[H_FILES];

static int h_find(const char *n)
{
    for (int i = 0; i < H_FILES; ++i) if (hf[i].used && !strcmp(hf[i].name, n)) return i;
    return -1;
}
int vfs_create(const char *n, const char *d)
{
    int i = h_find(n);
    if (i < 0) for (i = 0; i < H_FILES && hf[i].used; ++i) { }
    if (i >= H_FILES) return -1;
    hf[i].used = 1;
    strncpy(hf[i].name, n, 63);
    hf[i].size = d ? (uint32_t)strnlen(d, 4095) : 0;
    memcpy(hf[i].data, d ? d : "", hf[i].size);
    hf[i].data[hf[i].size] = 0;
    return 0;
}
int vfs_write(const char *n, const char *d, uint32_t len)
{
    int i = h_find(n);
    if (i < 0) { if (vfs_create(n, "") < 0) return -1; i = h_find(n); }
    if (len > 4095) len = 4095;
    memcpy(hf[i].data, d, len); hf[i].data[len] = 0; hf[i].size = len;
    return (int)len;
}
int vfs_read(const char *n, char *out, uint32_t max)
{
    int i = h_find(n);
    if (i < 0 || !max) return -1;
    uint32_t k = hf[i].size < max - 1 ? hf[i].size : max - 1;
    memcpy(out, hf[i].data, k); out[k] = 0;
    return (int)k;
}
int vfs_size(const char *n) { int i = h_find(n); return i < 0 ? -1 : (int)hf[i].size; }   /* PARCHE 087 */
int vfs_search_bm25(const char *q, const char *p, char *out, uint32_t max)
{ (void)q; (void)p; if (max) out[0] = 0; return 0; }

/* PARCHE 098: la VM cede la CPU periodicamente */
void thread_preempt_point(void) { }



/* SOMA_235_PROGRAMADOR: reloj fijo para sys.hour() ... sys.year() en las pruebas: 20/11/2026 13:45:07 */
#include "rtc.h"
void rtc_get_datetime(struct rtc_time *t) { t->year = 2026; t->month = 11; t->day = 20; t->hour = 13; t->min = 45; t->sec = 7; }
