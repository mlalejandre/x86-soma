/* SOMA_259_ORBE: src/ui/gui_prompter.c compilado como en el nucleo (-ffreestanding -mgeneral-regs-only -Werror), con las
 * declaraciones que le da gui.c. Solo compila (-c): comprueba que no hay coma flotante ni avisos.
 * cc -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wno-unused-function -Isrc/ui -Isrc/apps -c tests/test_orbe_259_nucleo.c */
#include <stdint.h>
void *kmalloc(uint64_t n);
void kfree(void *p);
extern uint64_t timer_get_uptime_ms(void);
static volatile int her_agent_busy;
static uint32_t her_ans_len, her_thk_len;
static uint32_t her_col_cur = 0x007DD3FCu;
static int her_sn(uint32_t a) { return (int)(a & 1023u) - 512; }
static int her_cs(uint32_t a) { return her_sn(a + 256u); }
static uint32_t her_mix256(uint32_t c1, uint32_t c2, uint32_t k) { return k > 128 ? c2 : c1; }
static uint16_t her_cp_cell(uint32_t cp) { return (uint16_t)cp; }
static void her_draw_cell(int32_t x, int32_t y, uint16_t cell, uint32_t col, int sc);
static void her_draw_cell(int32_t x, int32_t y, uint16_t cell, uint32_t col, int sc) { (void)x; (void)y; (void)cell; (void)col; (void)sc; }
int voz_speak_state(void);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint32_t c);
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_reset_clip(void);
#include "sysicon.h"
#include "web_font.h"
static void chat_fase(const char *q) { (void)q; }                 /* SOMA_285_CHAT: el chat no esta en este banco */
static void chat_fase_orden(const char *l) { (void)l; }
#include "gui_prompter.c"
int uso(void);
int uso(void) { her_pr_turno(); her_pr_pensamiento("a"); her_pr_stream_ini(); her_pr_stream("b"); her_pr_fin("c"); her_pr_raton(1); her_pr_rueda(1); gui_fase_orden("ls"); return her_pr_dibuja(800, 600, 300); }
