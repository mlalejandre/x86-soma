/* SOMA_202: navegador aislado, lado del nucleo (web_r3.c). */
#ifndef SOMA_WEB_R3_H
#define SOMA_WEB_R3_H
#include <stdint.h>
#include "web_r3_abi.h"
int  web_r3_start(int vw, int vh);                 /* lanza el proceso del motor; 0 = en marcha */
void web_r3_stop(void);
int  web_r3_alive(void);                           /* 0 = no hay proceso (nunca lanzado, o ha muerto y ya esta recogido) */
uint32_t web_r3_send(int op, const int32_t *a, int na, const void *data, uint32_t len);   /* encola una orden; devuelve su numero (0 = no se pudo) */
int  web_r3_wait(uint32_t seq, uint32_t timeout_ms);                                       /* 1 terminada | 0 aun no | -1 el proceso murio */
int  web_r3_call(int op, const int32_t *a, int na, const void *data, uint32_t len, void *out, uint32_t max, uint32_t timeout_ms);   /* bytes de respuesta o -1 */
void web_r3_request_stop(void);                    /* parar la carga en curso */
int  web_r3_pending(void);
const struct w3_state *web_r3_state(void);         /* ultimo estado publicado por el motor, ya validado */
void web_r3_scroll_req_done(void);
void web_r3_set_log(void (*cb)(int level, const char *msg));
uint32_t web_r3_frame_seq(void);
void web_r3_blit(int x, int y, int w, int h, int sx, int sy);
void web_r3_stats(uint32_t *starts, uint32_t *deaths, int64_t *last_exit, uint32_t *heap_pages, uint32_t *surf_pages);
/* SOMA_203 */
int  web_r3_get_image(const char *src, int want_px, const uint32_t **px, int *w, int *h, char *abs, uint32_t absmax);   /* 1 = la imagen esta */
void web_r3_image_free(void);
void web_r3_stats2(uint32_t *frames, uint32_t *paint_last, uint32_t *paint_avg, uint32_t *frame_h, uint32_t *saves, uint32_t *save_len);
int  web_r3_selftest(const char *url);             /* orden `webr3 <url>`: 0 = bien */
/* llamadas al sistema del proceso (usermode.c) */
int64_t web_r3_sys_wait(int proc_idx, int64_t ms);
int64_t web_r3_sys_post(int proc_idx, uint64_t what, uint64_t a, uint64_t b);
int64_t web_r3_sys_fs(int proc_idx, uint64_t what);
#endif
