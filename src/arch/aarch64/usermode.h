#ifndef MYOS_USERMODE_H
#define MYOS_USERMODE_H
/* SOMA_PARCHE_014_RING3: en AArch64 aun no hay EL0 (requiere VBAR/GIC, Fase 5). */
int usermode_selftest(void);
#include <stdint.h>
#include <stddef.h>
int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err);
int64_t usermode_wait(int idx);
int usermode_format_table(char *out, uint32_t max, int reap);
void usermode_install_programs(void);
int usermode_oomtest_mem(void);
int usermode_oomtest(void);
int usermode_fuzz(int nproc, uint32_t iters, uint32_t seed);
#endif
