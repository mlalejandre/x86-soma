/* SOMA_155: puertos simulados para la prueba del i8042 en el anfitrion */
#ifndef HOST155_IO_H
#define HOST155_IO_H
#include <stdint.h>
uint8_t sim_inb(uint16_t port);
void sim_outb(uint16_t port, uint8_t v);
static inline uint8_t inb(uint16_t p) { return sim_inb(p); }
static inline void outb(uint16_t p, uint8_t v) { sim_outb(p, v); }
static inline void io_wait(void) {}
#endif
