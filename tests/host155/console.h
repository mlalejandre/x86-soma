#ifndef HOST155_CONSOLE_H
#define HOST155_CONSOLE_H
#include <stdint.h>
void kprint(const char *s);
void kprint_hex8(uint8_t v);
void kprint_dec(uint32_t v);
void keyboard_inject_scancode(uint8_t sc);
void keyboard_irq_handler(void);
#endif
