/* Pruebas en host del planificador real (src/core/thread.c).
 * Se compila con -include tests/host_sched_cfg.h. switch_context es un stub: cada
 * prueba actua como "el hilo actual" y comprueba que decision toma el planificador. */
#define TSP 15 /* paginas de pila por hilo (THREAD_STACK_PAGES) */
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "thread.h"
#include "panic.h"

enum { MAXRUN = 64 };
static uint64_t T;                        /* reloj simulado (ms) */

static uint32_t cur(void) { return thread_current()->tid; }
static void tick(uint64_t ms) { T += ms; host_time_set(T); }
static void worker(void *a) { (void)a; }

/* Cede hasta que el hilo `tid` sea el actual. */
static int run_until(uint32_t tid)
{
    for (int i = 0; i < MAXRUN; ++i) {
        if (cur() == tid) return 1;
        thread_yield();
    }
    return cur() == tid;
}

static void test_estado_inicial(void)
{
    CHECK_EQ_INT(cur(), 0);
    CHECK_STR(thread_current()->name, "kernel_main");
    CHECK_EQ_INT(thread_current()->state, THREAD_STATE_RUNNING);
    CHECK_EQ_INT(host_pmm_frames_live(), 4 * TSP);    /* idle, netd, sysmon, httpd: 8 marcos cada uno */
}

static void test_round_robin(void)
{
    struct tcb *a = thread_create("A", worker, 0);
    struct tcb *b = thread_create("B", worker, 0);
    struct tcb *c = thread_create("C", worker, 0);
    uint32_t seq[35];
    int count[16] = {0}, i, idle_visto = 0;

    CHECK(a && b && c);
    CHECK_EQ_INT(a->tid, 5); CHECK_EQ_INT(b->tid, 6); CHECK_EQ_INT(c->tid, 7);
    for (i = 0; i < 35; ++i) {
        thread_yield();
        seq[i] = cur();
        if (seq[i] == 1) idle_visto = 1;
        if (seq[i] < 16) count[seq[i]]++;
    }
    /* Orden fijo: netd, sysmon, httpd, A, B, C, kernel_main, y vuelta a empezar. */
    CHECK_EQ_INT(seq[0], 2); CHECK_EQ_INT(seq[1], 3); CHECK_EQ_INT(seq[2], 4);
    CHECK_EQ_INT(seq[3], 5); CHECK_EQ_INT(seq[4], 6); CHECK_EQ_INT(seq[5], 7);
    CHECK_EQ_INT(seq[6], 0);
    for (i = 7; i < 35; ++i) CHECK_EQ_INT(seq[i], seq[i - 7]);
    CHECK_EQ_INT(idle_visto, 0);                    /* idle no corre si hay hilos listos */
    for (i = 0; i < 8; ++i) if (i != 1) CHECK_EQ_INT(count[i], 5);   /* reparto justo */
    CHECK_EQ_INT(cur(), 0);
}

static void test_dormir_y_despertar(void)
{
    int i, kmain_visto = 0;

    CHECK_EQ_INT(cur(), 0);
    thread_sleep(100);                              /* kernel_main duerme hasta T+100 */
    CHECK(cur() != 0);
    for (i = 0; i < 30; ++i) { thread_yield(); if (cur() == 0) kmain_visto = 1; }
    CHECK_EQ_INT(kmain_visto, 0);                   /* dormido: no se planifica */
    tick(99);
    for (i = 0; i < 14; ++i) { thread_yield(); if (cur() == 0) kmain_visto = 1; }
    CHECK_EQ_INT(kmain_visto, 0);                   /* a 1 ms de despertar */
    tick(1);
    CHECK(run_until(0));                            /* en T+100 vuelve */
    CHECK_EQ_INT(thread_current()->state, THREAD_STATE_RUNNING);
}

static void test_idle_solo_si_nadie_listo(void)
{
    int n = 0;

    /* Cada hilo, al ser el actual, se duerme. Al dormirse el ultimo, corre idle. */
    while (cur() != 1 && n < 20) { thread_sleep(50); n++; }
    CHECK_EQ_INT(n, 7);                             /* kernel_main, netd, sysmon, httpd, A, B, C */
    CHECK_EQ_INT(cur(), 1);
    thread_yield();
    CHECK_EQ_INT(cur(), 1);                         /* nadie listo: idle continua */
    tick(50);
    thread_yield();
    CHECK(cur() != 1);                              /* despiertan: idle cede */
    CHECK_EQ_INT(thread_current()->state, THREAD_STATE_RUNNING);
}

static void test_kill(void)
{
    uint32_t t;
    int i;

    CHECK_EQ_INT(thread_kill(0), 0);                /* hilos criticos blindados */
    CHECK_EQ_INT(thread_kill(1), 0);
    CHECK_EQ_INT(thread_kill(2), 0);
    CHECK_EQ_INT(thread_kill(3), 0);
    CHECK_EQ_INT(thread_kill(9999), 0);             /* inexistente */

    CHECK(run_until(0));
    t = 5;                                          /* hilo "A" */
    CHECK_EQ_INT(thread_kill(t), 1);
    CHECK_EQ_INT(thread_kill(t), 0);                /* ya muerto, aun sin recolectar */
    for (i = 0; i < 30; ++i) { thread_yield(); CHECK(cur() != t); }   /* nunca se planifica */
    CHECK_EQ_INT(thread_kill(t), 0);                /* ya recolectado: no existe */
}

/* Antes del parche 006, un hilo muerto no se liberaba mientras el hilo principal siguiera
 * ocupado (idle, que era quien recolectaba, no corre con hilos listos, como con la GUI). */
static void test_recoleccion_con_hilo_ocupado(void)
{
    int base = host_pmm_frames_live();
    struct tcb *w = thread_create("W", worker, 0);
    uint32_t tid;
    int i;

    CHECK(w != 0);
    tid = w->tid;
    CHECK_EQ_INT(host_pmm_frames_live(), base + TSP);
    CHECK_EQ_INT(thread_kill(tid), 1);
    thread_yield();                                 /* un solo planificado basta */
    CHECK_EQ_INT(host_pmm_frames_live(), base);     /* pila liberada */
    for (i = 0; i < 20; ++i) { thread_yield(); CHECK(cur() != tid); }
}

static void test_dormido_y_matado_no_despierta(void)
{
    int base = host_pmm_frames_live(), i;
    struct tcb *s = thread_create("S", worker, 0);
    uint32_t tid = s->tid;

    CHECK(run_until(tid));
    thread_sleep(10);                               /* S duerme */
    CHECK_EQ_INT(thread_kill(tid), 1);
    tick(1000);                                     /* pasa su hora de despertar */
    for (i = 0; i < 30; ++i) { thread_yield(); CHECK(cur() != tid); }
    CHECK_EQ_INT(host_pmm_frames_live(), base);
}

/* Con 48 ranuras y 4 en uso, 300 ciclos solo son posibles si las ranuras se reciclan. */
static void test_reutiliza_ranuras(void)
{
    int base = host_pmm_frames_live(), i, fallos = 0, fuera = 0;
    uint32_t last_tid = 0;
    uint64_t first_slot = 0;
    int mismo_slot = 0;

    for (i = 0; i < 300; ++i) {
        struct tcb *t = thread_create("ciclo", worker, 0);
        if (!t) { fallos++; continue; }
        if (t->tid <= last_tid) fallos++;           /* el tid sigue siendo unico y creciente */
        last_tid = t->tid;
        if (t->stack_slot < THREAD_STACK_BASE ||
            t->stack_slot >= THREAD_STACK_BASE + HOST_STACK_BYTES) fuera++;
        if (i == 0) first_slot = t->stack_slot;
        else if (t->stack_slot == first_slot) mismo_slot++;
        thread_kill(t->tid);
        thread_yield();
    }
    CHECK_EQ_INT(fallos, 0);
    CHECK_EQ_INT(fuera, 0);
    CHECK(mismo_slot > 250);                        /* se reutiliza la misma ranura */
    CHECK_EQ_INT(host_pmm_frames_live(), base);
}

static int llenar(struct tcb **out, int max)
{
    int n = 0;
    while (n < max && (out[n] = thread_create("lleno", worker, 0)) != 0) n++;
    return n;
}

static void vaciar(struct tcb **hs, int n)
{
    uint32_t tids[HOST_STACK_SLOTS];
    int i;
    for (i = 0; i < n; ++i) tids[i] = hs[i]->tid;
    for (i = 0; i < n; ++i) thread_kill(tids[i]);
    thread_yield();
}

static void test_agotar_ranuras_y_fallo_de_pmm(void)
{
    int base = host_pmm_frames_live();
    struct tcb *hs[HOST_STACK_SLOTS + 4];
    int n1, n2, n3;

    n1 = llenar(hs, HOST_STACK_SLOTS + 4);
    CHECK(n1 > 0 && n1 < HOST_STACK_SLOTS);         /* hay un limite y se alcanza */
    CHECK_EQ_INT(host_pmm_frames_live(), base + TSP * n1);
    CHECK(thread_create("de_mas", worker, 0) == 0); /* lleno: NULL, sin colgarse */
    CHECK_EQ_INT(host_pmm_frames_live(), base + TSP * n1);
    vaciar(hs, n1);
    CHECK_EQ_INT(host_pmm_frames_live(), base);

    /* Fallo del PMM a mitad de la pila: sin marcos ni ranura perdidos. */
    host_pmm_fail_after(3);
    CHECK(thread_create("sin_marcos", worker, 0) == 0);
    host_pmm_fail_after(-1);
    CHECK_EQ_INT(host_pmm_frames_live(), base);
    n2 = llenar(hs, HOST_STACK_SLOTS + 4);
    CHECK_EQ_INT(n2, n1);                           /* la misma capacidad: ninguna ranura se perdio */
    vaciar(hs, n2);

    for (int r = 0; r < 5; ++r) {                   /* repetir llenar/vaciar no degrada */
        n3 = llenar(hs, HOST_STACK_SLOTS + 4);
        CHECK_EQ_INT(n3, n1);
        vaciar(hs, n3);
    }
    CHECK_EQ_INT(host_pmm_frames_live(), base);
}

/* schedule() emite el latido que vigila el watchdog: sin el, avisaria en falso (o nunca). */
static void test_latido_del_vigilante(void)
{
    tick(7);
    thread_yield();
    CHECK_EQ_INT(watchdog_last_beat_ms(), T);
    tick(250);
    thread_sleep(1);                                /* dormir tambien pasa por schedule() */
    CHECK_EQ_INT(watchdog_last_beat_ms(), T);
    tick(1);
    thread_yield();
    CHECK_EQ_INT(watchdog_last_beat_ms(), T);
}

/* SOMA_PARCHE_015_PROC: los ganchos de arquitectura que usa el modo usuario */
extern int host_hook_switches, host_hook_cleanups;
extern struct tcb *host_hook_prev, *host_hook_next, *host_hook_cleaned;

static void test_ganchos_de_arquitectura(void)
{
    struct tcb *w = thread_create("G", worker, 0);
    CHECK(w != 0);
    /* la cima de la pila de kernel queda registrada: 1 pagina de guarda + 8 de pila por encima de la ranura */
    CHECK_EQ_INT(w->kstack_top, w->stack_slot + (TSP + 1) * 4096);
    CHECK(w->uproc == 0);

    struct tcb *antes = thread_current();
    int n0 = host_hook_switches;
    CHECK(run_until(w->tid));
    CHECK(host_hook_switches > n0);                 /* hubo al menos un cambio */
    CHECK(host_hook_next == w);                     /* el ultimo cambio fue hacia G */
    CHECK(host_hook_prev != w);
    (void)antes;

    CHECK(run_until(0));
    int n1 = host_hook_switches;
    thread_yield();                                 /* con un unico hilo listo no hay cambio: sin gancho */
    int cl0 = host_hook_cleanups;
    uint32_t tid = w->tid;
    CHECK_EQ_INT(thread_kill(tid), 1);
    thread_yield();                                 /* recoge al muerto */
    CHECK_EQ_INT(host_hook_cleanups, cl0 + 1);      /* exactamente una vez por hilo liberado */
    CHECK(host_hook_cleaned != 0);
    (void)n1;
}

/* SOMA_306_PILAS: un fallo en la pagina de guarda de un hilo se atribuye a ese hilo, y solo esa pagina */
static void test_pagina_de_guarda(void)
{
    struct tcb *g = thread_create("guardado", worker, 0);
    CHECK(g != 0);
    if (!g) return;
    uint64_t slot = g->stack_slot;
    CHECK(thread_guard_owner(slot) != 0 && strcmp(thread_guard_owner(slot), "guardado") == 0);
    CHECK(thread_guard_owner(slot + 4095) != 0 && strcmp(thread_guard_owner(slot + 4095), "guardado") == 0);
    CHECK(thread_guard_owner(slot + 4096) == 0);                       /* primera pagina de la pila de verdad */
    CHECK(thread_guard_owner(slot + 0x10000 - 8) == 0);                 /* cima de la pila */
    CHECK(thread_guard_owner(THREAD_STACK_BASE - 1) == 0);              /* fuera de la zona de pilas */
    CHECK(thread_guard_owner(THREAD_STACK_BASE + (uint64_t)THREAD_MAX_SLOTS * 0x10000ULL) == 0);
    CHECK_EQ_INT(thread_kill(g->tid), 1);
    thread_yield();                                                     /* se recolecta: la ranura queda libre */
    CHECK(thread_guard_owner(slot) == 0 || strcmp(thread_guard_owner(slot), "guardado") != 0);
}

int main(void)
{
    thread_init();
    test_estado_inicial();
    test_round_robin();
    test_dormir_y_despertar();
    test_idle_solo_si_nadie_listo();
    test_kill();
    test_recoleccion_con_hilo_ocupado();
    test_dormido_y_matado_no_despierta();
    test_reutiliza_ranuras();
    test_agotar_ranuras_y_fallo_de_pmm();
    test_latido_del_vigilante();
    test_ganchos_de_arquitectura();
    test_pagina_de_guarda();
    TEST_MAIN_END("sched");
}
