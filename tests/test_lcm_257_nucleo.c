/* SOMA_257_LCM: lcm_soma.inc compila como en el nucleo (freestanding, sin coma flotante, sin libc).
 * cc -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Isrc/agent -c tests/test_lcm_257_nucleo.c -o /tmp/x.o */
#include <stdint.h>
void kprint(const char *s);
extern volatile int console_muted;
char agent_last_response[4096];
static int delegation_depth;
int shell_agent_mode(void);
int dispatch_command(const char *c, char *out, uint32_t max);
int shell_command_info(const char *n, uint32_t *f, const char **h);
int vfs_read(const char *p, char *b, uint32_t n);
int vfs_write(const char *p, const char *b, uint32_t n);
static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s) { while (*s && *pos + 1 < max) out[(*pos)++] = *s++; if (max) out[*pos < max ? *pos : max - 1] = 0; }
static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v) { char b[24]; int n = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23); while (n) { char c[2] = { b[--n], 0 }; fb_puts(out, max, pos, c); } }
static char mayordomo_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int mayordomo_has(const char *t, const char *n) { for (; *t; ++t) { const char *a = t, *b = n; while (*a && *b && *a == *b) { ++a; ++b; } if (!*b) return 1; } return 0; }
static int str_eq_ci(const char *a, const char *b) { while (*a && *b) { if (mayordomo_lower(*a) != mayordomo_lower(*b)) return 0; ++a; ++b; } return *a == *b; }
static void mayordomo_store_reply(const char *r, int pr) { (void)r; (void)pr; }
#include "lcm_ordenes.inc"
#include "lcm_soma.inc"
int lcm_prueba_nucleo(void) { return lcm_soma("hola") + lcm_mayordomo("ls") + (int)delegation_depth; }
