#include "httpd.h"
#include "tcp.h"
#include "fs.h"
#include "sysinfo.h"
#include "thread.h"
#include "mutex.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "console.h"
#include "acct.h"
#include "idt.h"
#include "net.h"
#include "panic.h"

/* Prototipos de switch.s */
extern void switch_context(uint64_t *old_rsp, uint64_t new_rsp);
extern void thread_trampoline_asm(void);

#ifndef THREAD_STACK_BASE
#ifdef __x86_64__
#define THREAD_STACK_BASE  0x0000000080000000ULL
#else
#include "platform.h"
#define THREAD_STACK_BASE  SOMA_THREAD_STACK_BASE /* PARCHE 060: por plataforma */
#endif
#endif
#define THREAD_SLOT_SIZE   0x00010000ULL /* 64 KiB */
#define THREAD_STACK_PAGES 15            /* PARCHE 091: 60 KiB reales de pila (el agente ya corre en su propio hilo) */

/* SOMA_PARCHE_006_SCHED: las ranuras de pila se reutilizan (antes la ranura salia
 * del tid, que nunca se reciclaba: el espacio virtual crecia sin limite). */
#ifndef THREAD_MAX_SLOTS
#define THREAD_MAX_SLOTS   1024          /* 64 MiB virtuales de pilas */
#endif
static uint8_t thread_slot_used[THREAD_MAX_SLOTS];
static uint32_t dead_pending = 0;        /* hilos DEAD aun sin recolectar */

static int thread_slot_alloc(void)
{
    for (int i = 0; i < THREAD_MAX_SLOTS; ++i) {
        if (!thread_slot_used[i]) { thread_slot_used[i] = 1; return i; }
    }
    return -1;
}

static void thread_slot_release(uint64_t slot_addr)
{
    if (slot_addr < THREAD_STACK_BASE) return;
    uint64_t idx = (slot_addr - THREAD_STACK_BASE) / THREAD_SLOT_SIZE;
    if (idx < THREAD_MAX_SLOTS) thread_slot_used[idx] = 0;
}

static struct tcb *thread_list = 0;
static struct tcb *curr_thread = 0;
static struct tcb *idle_tcb = 0;
/* PARCHE 089: ciclos por dueno */
static uint64_t own_cyc[256], own_mark;
uint64_t acct_now(void);
static void acct_charge(uint64_t now) { own_cyc[kheap_owner] += now - own_mark; own_mark = now; }
static uint32_t    next_tid = 0;
static int         multitasking_active = 0;

static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

struct tcb *thread_current(void)
{
    return curr_thread;
}

void thread_trampoline_c(void)
{
    if (curr_thread && curr_thread->entry) {
        curr_thread->entry(curr_thread->arg);
    }
}

#define STACK_PAINT 0xA5A5A5A5A5A5A5A5ULL
static uint64_t allocate_thread_stack(uint64_t slot)
{

    /* 1. Desmapear la pagina de guarda (offset 0) */
    vmm_unmap_page(slot);

    /* 2. Mapear paginas de pila a partir de slot + 4096 con rollback atomico */
    for (uint64_t p = 1; p <= THREAD_STACK_PAGES; ++p) {
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) {
            /* Rollback de todas las paginas previamente asignadas 1..(p-1) */
            for (uint64_t k = 1; k < p; ++k) {
                uint64_t v = slot + k * VMM_PAGE_SIZE;
                uint64_t pa = vmm_virt_to_phys(v);
                vmm_unmap_page(v);
                if (pa) pmm_free_frame((uintptr_t)pa);
            }
            return 0;
        }

        uint64_t vaddr = slot + p * VMM_PAGE_SIZE;
        if (vmm_map_page(vaddr, (uint64_t)frame, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX) != 0) {
            pmm_free_frame(frame);
            /* Rollback de todas las paginas previamente asignadas 1..(p-1) */
            for (uint64_t k = 1; k < p; ++k) {
                uint64_t v = slot + k * VMM_PAGE_SIZE;
                uint64_t pa = vmm_virt_to_phys(v);
                vmm_unmap_page(v);
                if (pa) pmm_free_frame((uintptr_t)pa);
            }
            return 0;
        }
    }

    /* PARCHE 132: la pila se pinta con un patron para medir la marca de agua (cuanta pila se llego a usar de verdad) */
    {
        uint64_t *pp = (uint64_t *)(uintptr_t)(slot + VMM_PAGE_SIZE);
        for (uint64_t i = 0; i < (uint64_t)THREAD_STACK_PAGES * VMM_PAGE_SIZE / 8; ++i) pp[i] = STACK_PAINT;
    }
    return slot + (uint64_t)(THREAD_STACK_PAGES + 1) * VMM_PAGE_SIZE;
}

/* SOMA_306_PILAS: si addr cae en la pagina de guarda de un hilo, su nombre ("?" si la ranura esta libre); si no, 0.
 * Lo usa el informe de panico: un #PF ahi es un desbordamiento de pila (con -fstack-clash-protection siempre cae aqui). */
const char *thread_guard_owner(uint64_t addr)
{
    if (addr < THREAD_STACK_BASE) return 0;
    uint64_t idx = (addr - THREAD_STACK_BASE) / THREAD_SLOT_SIZE;
    if (idx >= THREAD_MAX_SLOTS) return 0;
    uint64_t slot = THREAD_STACK_BASE + idx * THREAD_SLOT_SIZE;
    if (addr - slot >= VMM_PAGE_SIZE) return 0;
    struct tcb *t = thread_list;                                  /* lista circular */
    if (t) do { if (t->stack_slot == slot) return t->name; t = t->next; } while (t && t != thread_list);
    return "?";
}

/* bytes de pila realmente tocados: se busca desde el extremo bajo la primera palabra distinta del patron */
static uint32_t stack_used_bytes(uint64_t slot)
{
    if (slot < THREAD_STACK_BASE) return 0;       /* hilo 0 / idle: usan la pila del arranque, sin pintar */
    const uint64_t *pp = (const uint64_t *)(uintptr_t)(slot + VMM_PAGE_SIZE);
    uint64_t words = (uint64_t)THREAD_STACK_PAGES * VMM_PAGE_SIZE / 8, i = 0;
    while (i < words && pp[i] == STACK_PAINT) i++;
    return (uint32_t)((words - i) * 8);
}
#define STACK_TOTAL_BYTES ((uint32_t)THREAD_STACK_PAGES * VMM_PAGE_SIZE)

static void free_thread_stack(uint64_t slot)
{
    if (slot < THREAD_STACK_BASE) return;

    for (uint64_t p = 1; p <= THREAD_STACK_PAGES; ++p) {
        uint64_t vaddr = slot + p * VMM_PAGE_SIZE;
        uint64_t phys  = vmm_virt_to_phys(vaddr);
        vmm_unmap_page(vaddr);
        if (phys) {
            pmm_free_frame((uintptr_t)phys);
        }
    }
    thread_slot_release(slot);
}

struct tcb *thread_create(const char *name, thread_func_t entry, void *arg)
{
    struct tcb *t = (struct tcb *)kmalloc(sizeof(struct tcb));
    if (!t) return 0;

    memset(t, 0, sizeof(struct tcb));
    t->tid = next_tid++;
    str_copy(t->name, name ? name : "kthread", sizeof(t->name));
    t->entry = entry;
    t->arg = arg;
    t->state = THREAD_STATE_READY;
    t->ticks_run = 0;

    int slot_idx = thread_slot_alloc();
    if (slot_idx < 0) {
        kfree(t);
        return 0;
    }
    t->stack_slot = THREAD_STACK_BASE + (uint64_t)slot_idx * THREAD_SLOT_SIZE;

    uint64_t stack_top = allocate_thread_stack(t->stack_slot);
    if (!stack_top) {
        thread_slot_release(t->stack_slot);
        kfree(t);
        return 0;
    }

    t->kstack_top = stack_top;      /* SOMA_PARCHE_015_PROC */

    uint64_t *sp = (uint64_t *)stack_top;
#ifdef __x86_64__
    *(--sp) = (uint64_t)thread_trampoline_asm;
    *(--sp) = 0;    /* rbp */
    *(--sp) = 0;    /* rbx */
    *(--sp) = 0;    /* r12 */
    *(--sp) = 0;    /* r13 */
    *(--sp) = 0;    /* r14 */
    *(--sp) = 0;    /* r15 */
    *(--sp) = 0x202ULL;   /* rflags (IF=1) */
#else
    *(--sp) = 0; /* x20 */
    *(--sp) = 0; /* x19 */
    *(--sp) = 0; /* x22 */
    *(--sp) = 0; /* x21 */
    *(--sp) = 0; /* x24 */
    *(--sp) = 0; /* x23 */
    *(--sp) = 0; /* x26 */
    *(--sp) = 0; /* x25 */
    *(--sp) = 0; /* x28 */
    *(--sp) = 0; /* x27 */
    *(--sp) = (uint64_t)thread_trampoline_asm; /* x30 (LR) */
    *(--sp) = 0; /* x29 (FP) */
#endif

    t->rsp = (uint64_t)sp;

    if (!thread_list) {
        thread_list = t;
        t->next = t;
    } else {
        struct tcb *last = thread_list;
        while (last->next != thread_list) {
            last = last->next;
        }
        last->next = t;
        t->next = thread_list;
    }

    return t;
}

void thread_reap_dead(void)
{
    if (!thread_list) return;

    struct tcb *prev = thread_list;
    struct tcb *curr = thread_list->next;
    uint32_t left = 0;

    /* No recolectar hilo 0 (kernel_main) ni hilo 1 (idle) ni el hilo actualmente en ejecucion */
    while (curr != thread_list) {
        if (curr != curr_thread && curr->state == THREAD_STATE_DEAD) {
            struct tcb *to_free = curr;
            prev->next = curr->next;
            curr = curr->next;

            arch_thread_cleanup(to_free);          /* SOMA_PARCHE_015_PROC */
            {   /* PARCHE 132: aviso si el hilo estuvo cerca de agotar su pila (un desbordamiento solo se ve cuando ya es tarde) */
                uint32_t u = stack_used_bytes(to_free->stack_slot);
                if (u > STACK_TOTAL_BYTES / 10 * 8) { kprint("[STACK] AVISO: el hilo '"); kprint(to_free->name); kprint("' uso "); kprint_dec(u / 1024); kprint(" de "); kprint_dec(STACK_TOTAL_BYTES / 1024); kprint(" KiB de pila\n"); }
            }
            free_thread_stack(to_free->stack_slot);
            kfree(to_free);
        } else {
            if (curr->state == THREAD_STATE_DEAD) left++;   /* el actual: se recogera luego */
            prev = curr;
            curr = curr->next;
        }
    }
    dead_pending = left;
}

/* Hilo 1: Idle Task en reposo de CPU con HLT */
#include "arch.h"

static void idle_thread_func(void *arg)
{
    (void)arg;
    for (;;) {
        thread_reap_dead();
#ifdef __x86_64__
        { extern void cpu_idle(void); cpu_idle(); }   /* SOMA_166_T480S: MWAIT con estado C profundo (o HLT); sale con interrupciones */
#else
        arch_pause();
#endif
        thread_yield();
    }
}

/* Hilo 2: VirtIO-NET Daemon de recepcion en segundo plano */
/* SOMA_344_RED: con trafico (algo recibido en los ultimos 200 ms) pasa cada 1 ms; en reposo, cada 10 ms como antes (el consumo en
 * reposo no cambia). Desde que los clientes de red van en ring 3, entre una espera y otra de sus llamadas era este hilo quien vaciaba la
 * tarjeta: cada rafaga esperaba hasta 10 ms. */
static void netd_thread_func(void *arg)
{
    (void)arg;
    uint64_t ultimo = 0;
    for (;;) {
        int got = 0;
        net_lock();
        while (net_poll()) got = 1;                     /* vaciar la cola de recepcion */
        net_unlock();
        uint64_t now = timer_get_uptime_ms();
        if (got) ultimo = now;
        thread_sleep(now - ultimo < 200 ? 1 : 10);
    }
}


/* Hilo 3: Demonio de Telemetria y Monitorizacion en Segundo Plano */
static void sysmon_thread_func(void *arg)
{
    (void)arg;
    static char tele_buf[256];

    for (;;) {
        thread_sleep(2000); /* Ejecutar cada 2 segundos */

        size_t f_free = 0, f_used = 0, f_total = 0;
        pmm_get_stats(&f_free, &f_used, &f_total);

        size_t h_used = 0, h_free = 0;
        kheap_stats(&h_used, &h_free);

        uint64_t uptime_s = timer_get_uptime_ms() / 1000ULL;

        /* Formatear reporte de telemetria en vivo */
        uint32_t p = 0;
        const char *hdr = "[MYOS TELEMETRY BACKGROUND LOG]\n- Uptime: ";
        while (*hdr) tele_buf[p++] = *hdr++;

        /* Segundos de uptime */
        uint64_t v = uptime_s;
        if (v == 0) tele_buf[p++] = '0';
        else { char b[12]; int n = 0; while (v) { b[n++] = (char)('0' + (v % 10)); v /= 10; } while (n) tele_buf[p++] = b[--n]; }

        const char *m2 = "s\n- RAM Libre: ";
        while (*m2) tele_buf[p++] = *m2++;
        v = (f_free * 4096ULL) / (1024ULL * 1024ULL);
        if (v == 0) tele_buf[p++] = '0';
        else { char b[12]; int n = 0; while (v) { b[n++] = (char)('0' + (v % 10)); v /= 10; } while (n) tele_buf[p++] = b[--n]; }

        const char *m3 = " MiB\n- Heap Libre: ";
        while (*m3) tele_buf[p++] = *m3++;
        v = h_free / 1024ULL;
        if (v == 0) tele_buf[p++] = '0';
        else { char b[12]; int n = 0; while (v) { b[n++] = (char)('0' + (v % 10)); v /= 10; } while (n) tele_buf[p++] = b[--n]; }

        const char *m4 = " KiB\n- Estado: 100% OPERATIVO (sysmon bg task)\n";
        while (*m4) tele_buf[p++] = *m4++;
        tele_buf[p] = '\0';

        /* Escribir en VFS de forma concurrente protegida */
        vfs_write("/sys/telemetry.txt", tele_buf, p);
    }
}

void thread_init(void)
{
    if (multitasking_active) return;

    /* Hilo 0: kernel_main */
    struct tcb *kmain = (struct tcb *)kmalloc(sizeof(struct tcb));
    memset(kmain, 0, sizeof(struct tcb));
    kmain->tid = next_tid++;
    str_copy(kmain->name, "kernel_main", sizeof(kmain->name));
    kmain->state = THREAD_STATE_RUNNING;
    kmain->next = kmain;

#ifdef __x86_64__
    uint64_t cur_rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(cur_rsp));
    kmain->rsp = cur_rsp;
#else
    uint64_t cur_sp;
    __asm__ volatile ("mov %0, sp" : "=r"(cur_sp));
    kmain->rsp = cur_sp;
#endif

    thread_list = kmain;
    curr_thread = kmain;
    multitasking_active = 1;

    /* Hilo 1: idle task */
    idle_tcb = thread_create("idle", idle_thread_func, 0);
    if (idle_tcb) idle_tcb->owner = ACCT_IDLE;                 /* PARCHE 089 */
    kmain->cyc_start = acct_now(); own_mark = kmain->cyc_start;

    /* Hilo 2: net daemon */
    thread_create("netd", netd_thread_func, 0);

    /* Hilo 3: sysmon daemon */
    thread_create("sysmon", sysmon_thread_func, 0);
    thread_create("httpd", httpd_thread_func, 0);

    kprint("KTHREADS: Inicializado (Hilo 0: 'kernel_main', Hilo 1: 'idle', Hilo 2: 'netd', Hilo 3: 'sysmon')\n");
}

static void schedule(void)
{
    if (!multitasking_active || !curr_thread) return;

    /* El hilo idle solo corre si nadie mas esta listo (con la GUI activa, nunca):
     * por eso los hilos muertos tambien se recolectan aqui, no solo en idle. */
    if (dead_pending) thread_reap_dead();

    uint64_t now_ms = timer_get_uptime_ms();
    watchdog_beat(now_ms);          /* SOMA_PARCHE_008_PANIC: el vigilante comprueba este latido */

    /* 1. Despertar hilos dormidos */
    struct tcb *scan = thread_list;
    if (scan) {
        do {
            if (scan->state == THREAD_STATE_SLEEPING && now_ms >= scan->sleep_until_ms) {
                scan->state = THREAD_STATE_READY;
            }
            scan = scan->next;
        } while (scan != thread_list);
    }

    /* 2. Buscar siguiente hilo READY */
    struct tcb *next = curr_thread->next;
    struct tcb *candidate = 0;

    while (next != curr_thread) {
        if (next->state == THREAD_STATE_READY && next != idle_tcb) {
            candidate = next;
            break;
        }
        next = next->next;
    }

    /* Si no hay hilos normales listos, comprobar si el actual puede continuar */
    if (!candidate) {
        if (curr_thread->state == THREAD_STATE_RUNNING && curr_thread != idle_tcb) {
            return;
        }
        /* Si nadie esta listo, ejecutar idle_tcb */
        if (idle_tcb && (idle_tcb->state == THREAD_STATE_READY || idle_tcb->state == THREAD_STATE_RUNNING)) {
            candidate = idle_tcb;
        } else {
            return;
        }
    }

    if (candidate == curr_thread) return;

    /* 3. Conmutar contexto */
    struct tcb *prev = curr_thread;
    if (prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
    }
    candidate->state = THREAD_STATE_RUNNING;
    candidate->ticks_run++;
    {   /* PARCHE 089: tiempo de CPU por hilo y por dueno */
        uint64_t now = acct_now();
        prev->cyc_total += now - prev->cyc_start;
        acct_charge(now);
        candidate->cyc_start = now;
        kheap_owner = candidate->owner;
    }
    curr_thread = candidate;

    arch_thread_switch(prev, candidate);    /* SOMA_PARCHE_015_PROC */
    switch_context(&prev->rsp, candidate->rsp);
}

void thread_yield(void)
{
    schedule();
}

/* PARCHE 072: desalojo por rodajas COOPERATIVO. La IRQ del temporizador (aarch64) solo levanta sched_need_resched cada SCHED_SLICE_MS;
 * el cambio de hilo se hace en puntos seguros (bucles de espera de drivers) llamando a thread_preempt_point(). Asi un hilo que espera
 * hardware no acapara la CPU y no hay reentrada en estructuras del nucleo sin bloqueo (cambiar de hilo dentro de la IRQ las corromperia). */
volatile int sched_need_resched;
volatile uint32_t sched_preempt_count;

void thread_preempt_point(void)
{
    if (!sched_need_resched) return;
    sched_need_resched = 0;
    if (!multitasking_active || !curr_thread) return;
    sched_preempt_count++;
    schedule();
}

void thread_sleep(uint32_t ms)
{
    if (!curr_thread) return;
    curr_thread->sleep_until_ms = timer_get_uptime_ms() + (uint64_t)ms;
    curr_thread->state = THREAD_STATE_SLEEPING;
    schedule();
}

void thread_exit(void)
{
    if (!curr_thread) return;
    tcp_cleanup_for_thread(curr_thread->tid);
    kmutex_release_all_for_thread(curr_thread);
    curr_thread->state = THREAD_STATE_DEAD;
    dead_pending++;
    schedule();

    for (;;) {
        arch_halt();
    }
}

/* PARCHE 139: despierta YA a un hilo dormido (thread_sleep): pasa a listo en la siguiente vuelta del planificador.
 * Devuelve 1 si estaba durmiendo. No cede la CPU. */
int thread_wake(uint32_t tid)
{
    if (!multitasking_active || !thread_list) return 0;
    struct tcb *t = thread_list;
    do {
        if (t->tid == tid) {
            if (t->state != THREAD_STATE_SLEEPING) return 0;
            t->sleep_until_ms = 0;
            return 1;
        }
        t = t->next;
    } while (t && t != thread_list);
    return 0;
}

int thread_kill(uint32_t tid)
{
    if (!multitasking_active || !thread_list) return 0;
    /* Blindaje: hilos criticos del sistema (0: kmain, 1: idle, 2: netd, 3: sysmon) */
    if (tid <= 3) return 0;

    struct tcb *t = thread_list;
    do {
        if (t->tid == tid) {
            if (t->state != THREAD_STATE_DEAD) {
                tcp_cleanup_for_thread(tid);
                kmutex_release_all_for_thread(t);
                t->state = THREAD_STATE_DEAD;
                dead_pending++;
                if (t == curr_thread) {
                    schedule();
                    for (;;) { arch_halt(); }
                }
                return 1;
            }
            return 0;
        }
        t = t->next;
    } while (t != thread_list);

    return 0;
}

void thread_dump(void)
{
    kprint("\nTABLA DE HILOS (KTHREADS):\n");
    kprint("  rodajas cedidas por desalojo cooperativo: "); kprint_dec(sched_preempt_count); kprint("\n");
    kprint("------------------------------------------------------------------\n");
    kprint(" TID  Nombre           Estado     Ticks CPU  RSP Virtual  Pila Slot\n");
    kprint("------------------------------------------------------------------\n");

    if (!thread_list) {
        kprint("  (sin hilos)\n");
        return;
    }

    struct tcb *t = thread_list;
    do {
        kprint("  ");
        kprint_dec(t->tid);
        if (t->tid < 10) kprint("  "); else kprint(" ");

        kprint(t->name);
        uint32_t nl = 0; while (t->name[nl]) nl++;
        for (uint32_t s = nl; s < 17; ++s) kputc(' ');

        if (t->state == THREAD_STATE_RUNNING)  kprint("RUNNING    ");
        else if (t->state == THREAD_STATE_READY)    kprint("READY      ");
        else if (t->state == THREAD_STATE_SLEEPING) kprint("SLEEPING   ");
        else if (t->state == THREAD_STATE_DEAD)     kprint("DEAD       ");
        else                                        kprint("UNKNOWN    ");

        kprint_dec((uint32_t)t->ticks_run);
        uint32_t tl = (t->ticks_run < 10) ? 1 : ((t->ticks_run < 100) ? 2 : ((t->ticks_run < 1000) ? 3 : 5));
        for (uint32_t s = tl; s < 11; ++s) kputc(' ');
        kprint("0x"); kprint_hex32((uint32_t)t->rsp);
        kprint("   0x"); kprint_hex32((uint32_t)t->stack_slot);
        kprint("\n");

        t = t->next;
    } while (t != thread_list);

    kprint("------------------------------------------------------------------\n");
    kprint("Hilo activo: TID ");
    kprint_dec(curr_thread ? curr_thread->tid : 0);
    kprint(" ('"); kprint(curr_thread ? curr_thread->name : "none"); kprint("')\n\n");
}

/* Auto-test: hilo efimero con recoleccion de basura */
static volatile int ephemeral_executed = 0;

static void ephemeral_func(void *arg)
{
    (void)arg;
    ephemeral_executed = 1;
    thread_sleep(10);
    /* Sale implicitamente por thread_trampoline -> thread_exit() */
}

int thread_test_self(void)
{
    kprint("\n[KTHREAD REAPER TEST] Verificando ciclo de vida de hilo efimero...\n");

    size_t f_before = 0, u_before = 0, t_before = 0;
    pmm_get_stats(&f_before, &u_before, &t_before);

    ephemeral_executed = 0;
    struct tcb *ephemeral = thread_create("transient_task", ephemeral_func, 0);
    if (!ephemeral) {
        kprint("  FALLO: No se pudo crear hilo efimero\n");
        return 0;
    }

    uint64_t ep_slot = ephemeral->stack_slot;

    /* Esperar a que ejecute y termine */
    uint64_t start = timer_get_uptime_ms();
    while (!ephemeral_executed && (timer_get_uptime_ms() - start < 1000)) {
        thread_yield();
    }

    if (!ephemeral_executed) {
        kprint("  FALLO: Hilo efimero no ejecuto\n");
        return 0;
    }

    /* Dar tiempo para que pase a DEAD */
    thread_sleep(20);

    /* Forzar recoleccion de basura */
    thread_reap_dead();

    /* Verificar que la pila fue desmapeada */
    if (vmm_virt_to_phys(ep_slot + 4096) != 0) {
        kprint("  FALLO: La pila del hilo muerto no fue desmapeada\n");
        return 0;
    }

    size_t f_after = 0, u_after = 0, t_after = 0;
    pmm_get_stats(&f_after, &u_after, &t_after);

    if (f_after != f_before) {
        kprint("  FALLO: Fuga de memoria física detectada en el ciclo de vida del hilo\n");
        return 0;
    }

    kprint("  Hilo efimero ejecutado, terminado y recolectado OK (0 fugas PMM)\n");
    kprint("[KTHREAD REAPER TEST] SUPERADO CON EXITO (Gestion de Recursos OK).\n\n");
    return 1;
}


int thread_format_table(char *out, uint32_t max)
{
    if (!out || max == 0) return 0;
    uint32_t pos = 0;
    const char *hdr = "TID   NOMBRE          ESTADO     TICKS      RSP\n";
    while (*hdr && pos < max - 1) out[pos++] = *hdr++;

    if (!thread_list) {
        out[pos] = '\0';
        return (int)pos;
    }

    struct tcb *t = thread_list;
    do {
        char line[80];
        for (int i = 0; i < 79; ++i) line[i] = ' ';

        /* TID col 0 */
        char tb[10]; int tn = 0; uint32_t v = t->tid;
        if (v == 0) tb[tn++] = '0';
        else while (v) { tb[tn++] = (char)('0' + v % 10); v /= 10; }
        for (int k = 0; k < tn; ++k) line[k] = tb[tn - 1 - k];

        /* Nombre col 6 */
        for (int k = 0; t->name[k] && k < 14; ++k) line[6 + k] = t->name[k];

        /* Estado col 22 */
        const char *st = (t->state == THREAD_STATE_RUNNING) ? "RUNNING" :
                         (t->state == THREAD_STATE_READY)   ? "READY" :
                         (t->state == THREAD_STATE_SLEEPING)? "SLEEP" : "DEAD";
        for (int k = 0; st[k]; ++k) line[22 + k] = st[k];

        /* Ticks col 33 */
        tn = 0; v = (uint32_t)t->ticks_run;
        if (v == 0) tb[tn++] = '0';
        else while (v) { tb[tn++] = (char)('0' + v % 10); v /= 10; }
        for (int k = 0; k < tn; ++k) line[33 + k] = tb[tn - 1 - k];

        /* RSP col 44 */
        const char hex[] = "0123456789ABCDEF";
        uint32_t rsp32 = (uint32_t)t->rsp;
        line[44] = '0'; line[45] = 'x';
        for (int sh = 28; sh >= 0; sh -= 4) line[46 + (7 - sh/4)] = hex[(rsp32 >> sh) & 0xF];

        int end_col = 54;
        line[end_col++] = '\n';
        line[end_col] = '\0';

        for (int k = 0; k < end_col && pos < max - 1; ++k) out[pos++] = line[k];

        t = t->next;
    } while (t != thread_list);

    out[pos] = '\0';
    return (int)pos;
}

/* ================================================================== PARCHE 089: contabilidad para el Monitor del sistema */
uint64_t acct_now(void) { return arch_cycle_counter(); }
uint8_t acct_set_owner(uint8_t o)
{
    uint8_t old = kheap_owner;
    if (o == old) return old;
    acct_charge(acct_now());
    kheap_owner = o;
    if (curr_thread) curr_thread->owner = o;
    return old;
}
uint64_t acct_owner_cycles(uint8_t o)
{
    if (o == kheap_owner) { uint64_t now = acct_now(); return own_cyc[o] + (now - own_mark); }
    return own_cyc[o];
}
void thread_set_owner(void *tcb, uint8_t o) { struct tcb *t = (struct tcb *)tcb; if (t) { t->owner = o; if (t == curr_thread) kheap_owner = o; } }
int thread_snapshot(struct thread_info *out, int max)
{
    if (!thread_list || max <= 0) return 0;
    int n = 0; uint64_t now = acct_now();
    struct tcb *t = thread_list;
    do {
        if (n >= max) break;
        struct thread_info *o = &out[n++];
        o->tid = t->tid; o->state = t->state; str_copy(o->name, t->name, sizeof o->name);
        o->cycles = t->cyc_total + (t == curr_thread ? now - t->cyc_start : 0);
        o->switches = t->ticks_run; o->stack_kb = (uint32_t)(THREAD_STACK_PAGES * 4); o->owner = t->owner; o->user = t->uproc ? 1 : 0;
        t = t->next;
    } while (t && t != thread_list);
    return n;
}

/* PARCHE 132: orden `stacks`: uso maximo de pila por hilo (marca de agua). Devuelve cuantos hilos pasan del 80 %. */
int thread_format_stacks(char *out, uint32_t max)
{
    uint32_t pos = 0; int hot = 0;
    #define PUT(str) do { const char *_s = (str); while (*_s && pos + 1 < max) out[pos++] = *_s++; } while (0)
    #define PUTN(v) do { char _b[12]; uint32_t _n = (v), _k = 0; if (!_n) _b[_k++] = '0'; while (_n) { _b[_k++] = (char)('0' + _n % 10); _n /= 10; } while (_k && pos + 1 < max) out[pos++] = _b[--_k]; } while (0)
    if (max) out[0] = 0;
    if (!thread_list) { PUT("(sin hilos)"); if (max) out[pos] = 0; return 0; }
    struct tcb *t = thread_list;
    do {
        uint32_t u = stack_used_bytes(t->stack_slot);
        uint32_t pct = u * 100 / STACK_TOTAL_BYTES;
        PUTN(t->tid); PUT("  "); PUT(t->name); PUT("  "); PUTN(u); PUT("/"); PUTN(STACK_TOTAL_BYTES); PUT(" bytes ("); PUTN(pct); PUT("%)");
        if (pct >= 80) { PUT("  <-- CERCA DEL LIMITE"); hot++; }
        PUT("\n");
        t = t->next;
    } while (t != thread_list);
    if (max) out[pos < max ? pos : max - 1] = 0;
    #undef PUT
    #undef PUTN
    return hot;
}
