/* Sustitutos para probar thread.c en host: sin cambio de contexto real. */
#include <stdint.h>
#include <stddef.h>
#include "host_sched_cfg.h"
#include "host_stubs.h"
struct tcb;

uint8_t host_stack_arena[HOST_STACK_BYTES] __attribute__((aligned(4096)));

/* limites de .text simulados que usa panic.c en las pruebas */
uint64_t host_text_lo = 0x100000, host_text_hi = 0x200000;

/* switch_context no ejecuta el otro hilo: el planificador ya ha fijado curr_thread,
 * asi que tras la llamada thread_current() es el hilo elegido. Las pruebas actuan
 * como "el hilo actual" y comprueban las decisiones de planificacion. */
void switch_context(uint64_t *old_rsp, uint64_t new_rsp) { (void)old_rsp; (void)new_rsp; }
void thread_trampoline_asm(void) { }

/* SOMA_PARCHE_015_PROC: ganchos de arquitectura; se registran para que las pruebas los comprueben */
int host_hook_switches, host_hook_cleanups;
struct tcb *host_hook_prev, *host_hook_next, *host_hook_cleaned;
void arch_thread_switch(struct tcb *prev, struct tcb *next) { host_hook_switches++; host_hook_prev = prev; host_hook_next = next; }
void arch_thread_cleanup(struct tcb *t) { host_hook_cleanups++; host_hook_cleaned = t; }

void httpd_thread_func(void *arg) { (void)arg; }
void tcp_cleanup_for_thread(uint32_t tid) { (void)tid; }
void net_lock(void) { }
void net_unlock(void) { }
int  net_poll(void) { return 0; }
int  vfs_write(const char *n, const char *d, uint32_t len) { (void)n; (void)d; return (int)len; }
/* PARCHE 089: lo define mem.c, que estas pruebas no enlazan */
volatile unsigned char kheap_owner;


/* SOMA_192: thread.c llama a cpu_idle() desde el parche 166 (cpupower.c, que estas pruebas no enlazan): test_sched y test_mutex no enlazaban */
void cpu_idle(void) { }
