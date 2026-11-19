#ifndef SOMA_I8042_H
#define SOMA_I8042_H
/* SOMA_155_T480S: controlador de teclado PS/2 (i8042, el que emula el EC del ThinkPad) con teclado y raton/trackpad.
 * Inicializacion al estilo de Linux (drivers/input/serio/i8042.c, keyboard/atkbd.c, mouse/psmouse-base.c, mouse/synaptics.c). */
#include <stdint.h>

void i8042_init(void);                              /* tras activar interrupciones y el reloj */
void i8042_service(void);
void i8042_irq(int irq);                            /* desde idt.c: IRQ 1 o 12 */
void i8042_tick(uint64_t ticks);                    /* desde el reloj (1 kHz): respaldo cada 2 ms */                           /* IRQ 1, IRQ 12 y respaldo desde el reloj: vacia el buffer del controlador */
int  i8042_aux_get(uint8_t *b, uint64_t *t_ms);     /* siguiente byte del raton/trackpad (1) o nada (0) */
int  i8042_aux_present(void);
int  i8042_touchpad(void);                          /* 1 si el puerto auxiliar es un trackpad Synaptics */
int  i8042_aux_wheel(void);                         /* 1 si el raton envia paquetes IntelliMouse (rueda, 4 bytes) */
void i8042_aux_stream(int on);                      /* activa/para el envio de movimientos (GUI) */
void i8042_print_info(void);                        /* orden "teclado" */

/* SOMA_167_T480S: trackpad Synaptics en modo absoluto (gestos) */
int i8042_touchpad_set_abs(int on);               /* 0 = hecho */
int i8042_touchpad_abs(void);
int i8042_syn_clickpad(void);
int i8042_syn_passthrough(void);
uint32_t i8042_syn_caps(uint32_t *ext09, uint32_t *ext0c);
#endif
