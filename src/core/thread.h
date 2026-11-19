#ifndef MYOS_THREAD_H
#define MYOS_THREAD_H

#include <stdint.h>
#include <stddef.h>

#define THREAD_STATE_READY    0
#define THREAD_STATE_RUNNING  1
#define THREAD_STATE_SLEEPING 2
#define THREAD_STATE_DEAD     3

typedef void (*thread_func_t)(void *arg);

struct tcb {
    uint64_t        rsp;            /* Offset 0: 8 bytes */
    uint64_t        sleep_until_ms; /* Offset 8: 8 bytes */
    uint64_t        stack_slot;     /* Offset 16: 8 bytes */
    uint64_t        ticks_run;      /* Offset 24: 8 bytes */
    thread_func_t   entry;          /* Offset 32: 8 bytes */
    void           *arg;            /* Offset 40: 8 bytes */
    struct tcb     *next;           /* Offset 48: 8 bytes */
    uint32_t        tid;            /* Offset 56: 4 bytes */
    uint32_t        state;          /* Offset 60: 4 bytes */
    char            name[32];       /* Offset 64: 32 bytes -> total 96 B */

    /* PARCHE 046: estado por hilo (politica del agente, recursion del shell) */
    int             agent_mode;     /* Offset 96: 1 = ejecutando bajo politica Agente */
    int             source_depth;   /* Offset 100: anidamiento de 'source' */
    int             dispatch_depth; /* Offset 104: anidamiento de dispatch_command */
    int             pad_ext;        /* Offset 108 -> total 112 B */

    /* SOMA_PARCHE_015_PROC: proceso de usuario asociado (0 = hilo de kernel puro) */
    void           *uproc;
    uint64_t        kstack_top;     /* cima de la pila de kernel del hilo */
    /* PARCHE 089: Monitor del sistema */
    uint64_t        cyc_total;      /* ciclos en CPU */
    uint64_t        cyc_start;      /* inicio de la rodaja actual */
    uint8_t         owner;          /* dueno (acct.h) */
} __attribute__((aligned(16)));

/* Ganchos de arquitectura (los implementa usermode.c; en AArch64 son vacios):
 *  arch_thread_switch : se llama justo antes de conmutar de `prev` a `next` (cualquiera puede ser 0)
 *  arch_thread_cleanup: se llama al liberar un hilo muerto (libera lo que tuviera de modo usuario) */
void arch_thread_switch(struct tcb *prev, struct tcb *next);
void arch_thread_cleanup(struct tcb *t);

void thread_init(void);
struct tcb *thread_create(const char *name, thread_func_t entry, void *arg);
void thread_yield(void);
void thread_preempt_point(void);          /* PARCHE 072: cede la CPU si ha vencido la rodaja (sched_need_resched) */
extern volatile int sched_need_resched;
extern volatile uint32_t sched_preempt_count;
#define SCHED_SLICE_MS 10
void thread_sleep(uint32_t ms);
void thread_exit(void);
int  thread_kill(uint32_t tid);
int  thread_wake(uint32_t tid);          /* PARCHE 139: adelanta el despertar de un hilo dormido */
struct tcb *thread_current(void);
void thread_reap_dead(void);
void thread_dump(void);
int  thread_format_stacks(char *out, uint32_t max);   /* PARCHE 132: marca de agua de pila por hilo; devuelve los que pasan del 80 % */
int  thread_test_self(void);
int  thread_format_table(char *out, uint32_t max);

#endif
