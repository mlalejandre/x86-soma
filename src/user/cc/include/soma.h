/* SOMA_265_CC: lo propio de SOMA para los programas en C (la misma ventana que win.* de SOMA-LANG y soma.win de Python).
 *
 *   #include <soma.h>
 *   int main(void) {
 *       if (!soma_win_create("Hola", 400, 300)) return 1;      // 0 = no hay escritorio
 *       while (soma_win_active()) {
 *           struct soma_mouse m = soma_win_mouse();            // x, y, clicked, button, inside
 *           int k = soma_win_key();                            // 0, SOMA_KEY_UP/DOWN/LEFT/RIGHT, 10 Intro, 'a'...
 *           soma_win_clear(0x101820);
 *           soma_win_rect(10, 10, 100, 40, 0x3366CC);
 *           soma_win_text(20, 22, "hola", 0xFFFFFF);
 *           soma_win_present();
 *           soma_sleep(16);
 *       }
 *       return 0;
 *   }
 * Colores 0xRRGGBB. Coordenadas dentro de la ventana. */
#ifndef _SOMA_H
#define _SOMA_H
#define SOMA_KEY_UP 16
#define SOMA_KEY_DOWN 14
#define SOMA_KEY_LEFT 2
#define SOMA_KEY_RIGHT 6
struct soma_mouse { int x, y, clicked, button, inside; };
int  soma_win_create(const char *title, int w, int h);       /* 1 abierta, 0 sin escritorio */
void soma_win_close(void);
int  soma_win_active(void);
void soma_win_size(int *w, int *h);
struct soma_mouse soma_win_mouse(void);
int  soma_win_key(void);
void soma_win_clear(unsigned color);
void soma_win_pixel(int x, int y, unsigned color);
void soma_win_rect(int x, int y, int w, int h, unsigned color);
void soma_win_line(int x0, int y0, int x1, int y1, unsigned color);
void soma_win_circle(int x, int y, int r, unsigned color);
void soma_win_text(int x, int y, const char *s, unsigned color);
void soma_win_present(void);
unsigned long soma_ms(void);                                  /* milisegundos desde el arranque */
void soma_sleep(unsigned ms);
void soma_clock(int out[6]);                                  /* anio, mes, dia, hora, minuto, segundo */
#endif
