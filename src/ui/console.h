#ifndef MYOS_CONSOLE_H
#define MYOS_CONSOLE_H

#include <stdint.h>

#define CMD_LINE_MAX 4096

void kputc(char c);
unsigned console_uart_timeouts(void);   /* PARCHE 132 */
int console_uart_dead(void);
void kprint(const char *s);
void kprint_hex8(uint8_t value);
void kprint_hex16(uint16_t value);
void kprint_hex32(uint32_t value);
void kprint_dec(uint32_t value);
void kprint_mac(const uint8_t *mac);
void kprint_ip(const uint8_t *ip);

int  kgetc_ready(void);
char kgetc(void);
int  kgetline(char *buf, uint32_t max);
void console_clear(void);
extern volatile int console_muted;
/* PARCHE 116: captura de la salida de UN hilo (el que llama a begin) en un buffer, con entrada simulada para kgetline: sirve para ejecutar programas de prueba
 * sin ensuciar la pantalla ni mezclar la salida de otros hilos. `input` = lineas separadas por \n. end() devuelve los bytes capturados. abort() (panico) la corta. */
void console_capture_begin(char *buf, uint32_t cap, const char *input);
uint32_t console_capture_end(void);
void console_capture_abort(void);
int  console_capture_active(void);            /* SOMA_156B */
void keyboard_irq_handler(void);
void keyboard_inject_scancode(uint8_t sc);   /* PARCHE 047: scancode PS/2 (juego 1) desde el teclado USB (solo x86_64) */
void keyboard_reset_state(void);
/* SOMA_155_T480S: distribucion del teclado. 1 = espanol (ISO, con AltGr y teclas muertas), 0 = la de siempre (US + n/tildes). */
void keyboard_set_layout(int es);
int  keyboard_get_layout(void);
void keyboard_layout_auto(void);
void console_serial_probe(void);             /* SOMA_155B_T480S: COM1 existe? (los portatiles no tienen) */
int  console_serial_present(void);
uint32_t console_kbd_events(void);
uint32_t console_take_super(void);              /* SOMA_168B_ORBE: pulsaciones de la tecla Windows (u Option sola) */
/* SOMA_156_T480S: registro del nucleo (dmesg) */
void klog_grow(void);                        /* tras kheap_init: pasa de 16 KiB a 256 KiB */
uint32_t klog_snapshot(char *out, uint32_t max);
uint64_t klog_written(void);
void klog_pause(int on);          /* SOMA_155D: contador de pulsaciones */             /* soma.teclado=es|us; sin opcion: espanol si soma.maquina=t480s */

int  console_history_dump(char *out_buf, uint32_t max_out);
void console_history_load_from_vfs(void);
void console_history_sync_to_vfs(void);

#endif
