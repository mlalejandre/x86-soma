/* SOMA_159_T480S: nucleos secundarios en x86_64 (antes, PARCHE 082, solo un nucleo). Misma interfaz y mismo modelo que
 * src/arch/aarch64/smp.c (ver src/core/smp.h): el nucleo 0 sigue llevando todo el sistema; los demas solo calculan los
 * trozos que les manda smp_parallel y, sin trabajo, duermen con hlt.
 *
 * Arranque (como Linux, arch/x86/kernel/smpboot.c + realmode/rm/trampoline_64.S):
 *  1. Un trampolin de 16 KiB en memoria baja (< 1 MiB, RAM libre segun el firmware): 1 pagina de codigo + tablas de paginas
 *     propias (identidad del primer GiB con paginas de 2 MiB, todo ejecutable). Modo real -> protegido -> largo.
 *  2. Ya en 64 bits salta al nucleo (smp_ap_entry64), carga el CR3 de verdad, su pila y entra en smp_ap_main.
 *  3. smp_ap_main: GDT y TSS propios (cada nucleo necesita su pila IST1 para #DF/#PF), la IDT comun, CR0/CR4/EFER/PAT/XCR0
 *     iguales que el nucleo 0 (asi el framebuffer sigue siendo write-combining en todos), y el LAPIC con todo enmascarado.
 *  4. Se despierta con INIT - SIPI - SIPI, de uno en uno y con tiempo limite. Si uno no responde, no se sigue con mas.
 *
 * Trabajo: cada secundario tiene una ranura (fn, arg, numero de secuencia). El nucleo 0 la rellena, publica la secuencia y,
 * si el secundario estaba dormido, le manda una IPI al vector 0xF0 (el manejador solo hace EOI). El secundario vacia su TLB
 * antes de cada trabajo (el nucleo 0 pudo cambiar las tablas de paginas: el montón quita y pone paginas), ejecuta y publica
 * la misma secuencia como hecha. Con interrupciones cerradas salvo en el instante de "sti; hlt" (sin perdida de avisos). */
#include <stdint.h>
#include "smp.h"
#include "apic.h"
#include "hwinfo.h"
#include "console.h"
#include "idt.h"
#include "mem.h"
#include "pmm.h"
#include "bootfb.h"
#include "sysinfo.h"
#include "cpupower.h"

#define SMP_MAX     16
#define SMP_STACK   (32 * 1024)
#define SMP_IST     (16 * 1024)
#define WAKE_VECTOR 0xF0
#define SPIN_BEFORE_HLT 400            /* pausas antes de dormir: dos volcados seguidos no pagan el despertar */

/* ---------------------------------------------------------------- trampolin (se copia a memoria baja; nunca corre aqui) */
__asm__(
    ".pushsection .rodata.smp_tramp, \"a\"\n"
    ".balign 16\n"
    ".global smp_tramp_start, smp_tramp_end, smp_tr_jmp32, smp_tr_cr3, smp_tr_jmp64, smp_tr_entry, smp_tr_gdt, smp_tr_gdtbase, smp_tr_pm32, smp_tr_lm64\n"
    ".code16\n"
    "smp_tramp_start:\n"
    "    cli\n"
    "    cld\n"
    "    movw %cs, %ax\n"
    "    movw %ax, %ds\n"
    "    lgdtl smp_tr_gdtdesc - smp_tramp_start\n"
    "    movl %cr0, %eax\n"
    "    orl $1, %eax\n"
    "    movl %eax, %cr0\n"
    "    .byte 0x66, 0xEA\n"                       /* ljmpl $0x08, $(T + pm32) */
    "smp_tr_jmp32: .long 0\n"
    "    .word 0x08\n"
    ".code32\n"
    "smp_tr_pm32:\n"
    "    movl %cr0, %eax\n"
    "    andl $0x9FFFFFFF, %eax\n"                /* tras INIT la cache esta apagada (CD/NW): encenderla */
    "    movl %eax, %cr0\n"
    "    movw $0x10, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %ss\n"
    "    movl %cr4, %eax\n"
    "    orl $0x20, %eax\n"                        /* PAE */
    "    movl %eax, %cr4\n"
    "    .byte 0xB8\n"                             /* movl $(T + 0x1000), %eax */
    "smp_tr_cr3: .long 0\n"
    "    movl %eax, %cr3\n"
    "    movl $0xC0000080, %ecx\n"
    "    rdmsr\n"
    "    orl $0x900, %eax\n"                       /* LME + NXE */
    "    wrmsr\n"
    "    movl %cr0, %eax\n"
    "    orl $0x80000001, %eax\n"                  /* PG + PE */
    "    movl %eax, %cr0\n"
    "    .byte 0xEA\n"                             /* ljmp $0x18, $(T + lm64) */
    "smp_tr_jmp64: .long 0\n"
    "    .word 0x18\n"
    ".code64\n"
    "smp_tr_lm64:\n"
    "    .byte 0x48, 0xB8\n"                       /* movabsq $smp_ap_entry64, %rax */
    "smp_tr_entry: .quad 0\n"
    "    jmpq *%rax\n"
    ".balign 8\n"
    "smp_tr_gdt:\n"
    "    .quad 0\n"
    "    .quad 0x00CF9A000000FFFF\n"               /* 0x08 codigo 32 */
    "    .quad 0x00CF92000000FFFF\n"               /* 0x10 datos 32 */
    "    .quad 0x00AF9A000000FFFF\n"               /* 0x18 codigo 64 */
    "smp_tr_gdtdesc:\n"
    "    .word 31\n"
    "smp_tr_gdtbase: .long 0\n"
    "smp_tramp_end:\n"
    ".popsection\n"

    /* Entrada en 64 bits (ya dentro del nucleo): CR3 de verdad, pila propia, a C. */
    ".pushsection .text\n"
    ".code64\n"
    ".global smp_ap_entry64\n"
    "smp_ap_entry64:\n"
    "    movq smp_ap_cr3(%rip), %rax\n"
    "    movq %rax, %cr3\n"
    "    movq smp_ap_stack(%rip), %rsp\n"
    "    andq $-16, %rsp\n"
    "    xorq %rbp, %rbp\n"
    "    movq smp_ap_idx(%rip), %rdi\n"
    "    call smp_ap_main\n"
    "1:  cli\n"
    "    hlt\n"
    "    jmp 1b\n"

    /* IPI de despertar: solo EOI (el trabajo lo mira el bucle de smp_ap_main al volver de hlt). */
    ".global smp_wake_stub\n"
    "smp_wake_stub:\n"
    "    pushq %rax\n"
    "    pushq %rcx\n"
    "    pushq %rdx\n"
    "    cmpb $0, smp_eoi_x2(%rip)\n"
    "    jne 2f\n"
    "    movq smp_eoi_mmio(%rip), %rax\n"
    "    movl $0, (%rax)\n"
    "    jmp 3f\n"
    "2:  movl $0x80B, %ecx\n"
    "    xorl %eax, %eax\n"
    "    xorl %edx, %edx\n"
    "    wrmsr\n"
    "3:  popq %rdx\n"
    "    popq %rcx\n"
    "    popq %rax\n"
    "    iretq\n"
    ".popsection\n"
);

extern const uint8_t smp_tramp_start[], smp_tramp_end[], smp_tr_jmp32[], smp_tr_cr3[], smp_tr_jmp64[], smp_tr_entry[];
extern const uint8_t smp_tr_gdt[], smp_tr_gdtbase[], smp_tr_pm32[], smp_tr_lm64[];
extern void smp_ap_entry64(void), smp_wake_stub(void);
extern uint64_t gdt64[];
void smp_ap_main(uint64_t idx);

/* leidos por el ensamblador de arriba */
volatile uint64_t smp_ap_cr3, smp_ap_stack, smp_ap_idx;
volatile uint8_t smp_eoi_x2;
volatile uint64_t smp_eoi_mmio;

struct tss64 {
    uint32_t r0; uint64_t rsp0, rsp1, rsp2; uint64_t r1; uint64_t ist[7]; uint64_t r2; uint16_t r3, iomap;
} __attribute__((packed));
struct apcpu { uint64_t gdt[7]; struct tss64 tss; } __attribute__((aligned(16)));

struct slot {
    volatile smp_fn fn; void *volatile arg; volatile int idx, n;
    volatile uint32_t seq, done;
    volatile int online, sleeping; uint32_t apic_id;
    volatile uint64_t jobs, busy, wakes;
    struct apcpu *pc;
} __attribute__((aligned(64)));
static struct slot g_slot[SMP_MAX];
static int g_ncpu = 1, g_inited, g_enabled = 1, g_found = 1, g_want;
static volatile int g_busy;
static const char *g_method = "un solo nucleo (x86_64)";
static const char *g_why = "";
static uint64_t g_cr0, g_cr4, g_efer, g_pat, g_xcr0;
static uint32_t g_tramp;
static uint32_t g_boot_ms[SMP_MAX];

static inline uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
static inline uint64_t rdmsr_(uint32_t m) { uint32_t lo, hi; __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m)); return (uint64_t)hi << 32 | lo; }
static inline void wrmsr_(uint32_t m, uint64_t v) { __asm__ volatile ("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }
static uint64_t us_ticks(uint64_t us) { uint64_t t = tsc_ticks_per_ms ? tsc_ticks_per_ms : 2000000; return t * us / 1000; }
static void delay_us(uint64_t us) { uint64_t e = rdtsc_() + us_ticks(us); while (rdtsc_() < e) __asm__ volatile ("pause"); }

static void tlb_flush_all(void)
{
    uint64_t c4; __asm__ volatile ("mov %%cr4, %0" : "=r"(c4));
    if (c4 & (1ULL << 7)) {                                    /* PGE: quitarlo y ponerlo vacia tambien las globales */
        __asm__ volatile ("mov %0, %%cr4" : : "r"(c4 & ~(1ULL << 7)) : "memory");
        __asm__ volatile ("mov %0, %%cr4" : : "r"(c4) : "memory");
    } else {
        uint64_t c3; __asm__ volatile ("mov %%cr3, %0" : "=r"(c3)); __asm__ volatile ("mov %0, %%cr3" : : "r"(c3) : "memory");
    }
}

/* ---------------------------------------------------------------- el secundario */
void smp_ap_main(uint64_t idx)
{
    struct slot *s = &g_slot[idx];
    struct apcpu *pc = s->pc;
    struct { uint16_t lim; uint64_t base; } __attribute__((packed)) gd = { (uint16_t)(sizeof pc->gdt - 1), (uint64_t)(uintptr_t)pc->gdt };
    __asm__ volatile ("lgdt %0" : : "m"(gd) : "memory");
    __asm__ volatile ("pushq $0x08\n leaq 1f(%%rip), %%rax\n pushq %%rax\n lretq\n 1:" ::: "rax", "memory");
    __asm__ volatile ("movw $0x10, %%ax\n movw %%ax, %%ds\n movw %%ax, %%es\n movw %%ax, %%ss\n xorw %%ax, %%ax\n movw %%ax, %%fs\n movw %%ax, %%gs" ::: "rax", "memory");
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
    idt_load();
    __asm__ volatile ("mov %0, %%cr0" : : "r"(g_cr0) : "memory");
    __asm__ volatile ("mov %0, %%cr4" : : "r"(g_cr4) : "memory");
    if (g_cr4 & (1ULL << 18)) __asm__ volatile ("xsetbv" : : "c"(0), "a"((uint32_t)g_xcr0), "d"((uint32_t)(g_xcr0 >> 32)));
    wrmsr_(0xC0000080, g_efer);
    wrmsr_(0x277, g_pat);
    __asm__ volatile ("fninit");
    apic_ap_setup();
    s->apic_id = apic_this_id();
    __atomic_store_n(&s->online, 1, __ATOMIC_RELEASE);
    uint32_t last = 0;
    for (;;) {
        uint32_t q = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (q == last) {
            for (int i = 0; i < SPIN_BEFORE_HLT && q == last; i++) { __asm__ volatile ("pause"); q = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE); }
            if (q == last) {
                __atomic_store_n(&s->sleeping, 1, __ATOMIC_SEQ_CST);      /* Dekker con smp_parallel_n: o ve la secuencia o manda IPI */
                __asm__ volatile ("mfence" ::: "memory");
                if (__atomic_load_n(&s->seq, __ATOMIC_ACQUIRE) == last) { cpu_idle_ap(&s->seq, last); s->wakes++; }   /* SOMA_166: MWAIT (C6) o HLT */
                __atomic_store_n(&s->sleeping, 0, __ATOMIC_RELEASE);
                continue;
            }
        }
        tlb_flush_all();
        uint64_t t0 = timer_ticks;
        s->fn(s->idx, s->n, s->arg);
        __asm__ volatile ("sfence" ::: "memory");                       /* escrituras write-combining (pantalla) antes de "hecho" */
        s->busy += timer_ticks - t0; s->jobs++;
        last = q;
        __atomic_store_n(&s->done, q, __ATOMIC_RELEASE);
    }
}

/* ---------------------------------------------------------------- el nucleo 0 */
static int find_tramp(void)
{
    for (uint32_t t = 0x8000; t + 0x4000 <= 0x9F000; t += 0x1000)
        if (pmm_range_is_ram(t, 0x4000)) { g_tramp = t; return 0; }
    return -1;
}

static void build_tramp(void)
{
    uint8_t *T = (uint8_t *)(uintptr_t)g_tramp;
    uint32_t len = (uint32_t)(smp_tramp_end - smp_tramp_start);
    for (uint32_t i = 0; i < 0x4000; i++) T[i] = 0;
    for (uint32_t i = 0; i < len; i++) T[i] = smp_tramp_start[i];
#define OFF(l) ((uint32_t)((l) - smp_tramp_start))
    *(uint32_t *)(T + OFF(smp_tr_jmp32)) = g_tramp + OFF(smp_tr_pm32);
    *(uint32_t *)(T + OFF(smp_tr_cr3))   = g_tramp + 0x1000;
    *(uint32_t *)(T + OFF(smp_tr_jmp64)) = g_tramp + OFF(smp_tr_lm64);
    *(uint64_t *)(T + OFF(smp_tr_entry)) = (uint64_t)(uintptr_t)smp_ap_entry64;
    *(uint32_t *)(T + OFF(smp_tr_gdtbase)) = g_tramp + OFF(smp_tr_gdt);
#undef OFF
    uint64_t *pml4 = (uint64_t *)(T + 0x1000), *pdpt = (uint64_t *)(T + 0x2000), *pd = (uint64_t *)(T + 0x3000);
    pml4[0] = (uint64_t)(g_tramp + 0x2000) | 3;
    pdpt[0] = (uint64_t)(g_tramp + 0x3000) | 3;
    for (uint64_t i = 0; i < 512; i++) pd[i] = (i << 21) | 0x83;    /* P | RW | PS: 2 MiB, ejecutable */
    __asm__ volatile ("wbinvd" ::: "memory");                       /* el secundario arranca sin cache coherente aun: a RAM */
}

static int parse_smp_option(void)
{
    const char *c = bootinfo_cmdline();
    for (; c && *c; c++) {
        const char *k = "soma.smp=", *p = c; int i = 0;
        while (k[i] && p[i] == k[i]) i++;
        if (k[i]) continue;
        p += i;
        if (*p < '0' || *p > '9') return -1;
        int v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        return v;
    }
    return -1;
}

static int boot_one(int idx, uint32_t apic_id)
{
    struct slot *s = &g_slot[idx];
    uint8_t *stk = (uint8_t *)kmalloc(SMP_STACK), *ist = (uint8_t *)kmalloc(SMP_IST);
    struct apcpu *pc = (struct apcpu *)kmalloc(sizeof *pc + 16);
    if (!stk || !ist || !pc) { g_why = "sin memoria para las pilas"; return -1; }
    pc = (struct apcpu *)(((uintptr_t)pc + 15) & ~(uintptr_t)15);
    for (unsigned i = 0; i < sizeof *pc; i++) ((uint8_t *)pc)[i] = 0;
    for (int i = 0; i < 5; i++) pc->gdt[i] = gdt64[i];
    pc->tss.ist[0] = (uint64_t)(uintptr_t)(ist + SMP_IST) & ~15ULL;
    pc->tss.iomap = (uint16_t)sizeof pc->tss;
    uint64_t base = (uint64_t)(uintptr_t)&pc->tss, limit = sizeof pc->tss - 1;
    pc->gdt[5] = (limit & 0xFFFFULL) | ((base & 0xFFFFFFULL) << 16) | (0x89ULL << 40) | (((limit >> 16) & 0x0FULL) << 48) | (((base >> 24) & 0xFFULL) << 56);
    pc->gdt[6] = (base >> 32) & 0xFFFFFFFFULL;
    s->pc = pc; s->apic_id = apic_id;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(smp_ap_cr3));
    smp_ap_stack = (uint64_t)(uintptr_t)(stk + SMP_STACK);
    smp_ap_idx = (uint64_t)idx;
    __asm__ volatile ("wbinvd" ::: "memory");
    uint64_t t0 = rdtsc_();
    if (apic_send_ipi(apic_id, 0x4500) != 0) { g_why = "la IPI INIT no se entrego"; return -1; }     /* INIT, nivel activo */
    delay_us(10000);
    for (int k = 0; k < 2 && !__atomic_load_n(&s->online, __ATOMIC_ACQUIRE); k++) {
        if (apic_send_ipi(apic_id, 0x4600 | (g_tramp >> 12)) != 0) { g_why = "la IPI SIPI no se entrego"; return -1; }
        delay_us(k ? 1000 : 300);
    }
    uint64_t lim = rdtsc_() + us_ticks(500000);
    while (!__atomic_load_n(&s->online, __ATOMIC_ACQUIRE) && rdtsc_() < lim) __asm__ volatile ("pause");
    if (!s->online) { g_why = "un nucleo no respondio a INIT-SIPI-SIPI"; return -1; }
    g_boot_ms[idx] = (uint32_t)((rdtsc_() - t0) / (tsc_ticks_per_ms ? tsc_ticks_per_ms : 1));
    return 0;
}

void smp_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    int want = -1;
    g_slot[0].online = 1;
    g_method = "un solo nucleo (x86_64)";
    if (bootinfo_disabled("smp")) { g_why = "desactivado (soma.smp=off o modo seguro)"; goto out; }
    const struct hw_acpi *a = apic_system_acpi();
    if (!a) { g_why = "hace falta el modo APIC (parche 158)"; goto out; }
    g_slot[0].apic_id = apic_this_id();
    want = parse_smp_option();
    g_want = want;
    if (want == 0 || want == 1) { g_why = "limitado a 1 por soma.smp"; goto out; }
    g_found = 0;
    for (int i = 0; i < a->nlapic; i++) if (a->lapic[i].enabled) g_found++;
    if (g_found < 1) g_found = 1;
    if (g_found == 1) { g_why = "el firmware solo declara un procesador"; goto out; }
    if (find_tramp() != 0) { g_why = "no hay 16 KiB libres bajo 1 MiB para el trampolin"; goto out; }
    if ((uint32_t)(smp_tramp_end - smp_tramp_start) > 0x1000) { g_why = "trampolin demasiado grande"; goto out; }
    __asm__ volatile ("mov %%cr0, %0" : "=r"(g_cr0));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(g_cr4));
    g_efer = rdmsr_(0xC0000080); g_pat = rdmsr_(0x277);
    if (g_cr4 & (1ULL << 18)) { uint32_t lo, hi; __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0)); g_xcr0 = (uint64_t)hi << 32 | lo; }
    smp_eoi_x2 = (uint8_t)apic_lapic_x2();
    smp_eoi_mmio = (uint64_t)apic_lapic_mmio() + 0xB0;
    idt_set_vector(WAKE_VECTOR, smp_wake_stub);
    build_tramp();
    int n = 1;
    for (int i = 0; i < a->nlapic && n < SMP_MAX; i++) {
        if (!a->lapic[i].enabled || a->lapic[i].apic_id == g_slot[0].apic_id) continue;
        if (want > 1 && n >= want) break;
        if (a->lapic[i].apic_id > 0xFF && !apic_lapic_x2()) continue;          /* sin x2APIC no se puede nombrar */
        if (boot_one(n, a->lapic[i].apic_id) != 0) break;                       /* si uno falla no se sigue: ver arriba */
        n++;
    }
    g_ncpu = n;
    if (n > 1) { g_method = apic_lapic_x2() ? "INIT-SIPI por x2APIC" : "INIT-SIPI por xAPIC"; if (!*g_why) g_why = "activo"; }
out:
    kprint("SMP: "); kprint_dec((uint32_t)g_ncpu); kprint(g_ncpu == 1 ? " nucleo en marcha" : " nucleos en marcha");
    kprint(" de "); kprint_dec((uint32_t)g_found); kprint(" ("); kprint(g_ncpu > 1 ? g_method : g_why);
    if (g_ncpu > 1) { kprint("; trampolin en 0x"); kprint_hex32(g_tramp); }
    kprint("; el 0 lleva el sistema, el resto calcula en paralelo: 'cpus')\n");
    if (g_ncpu > 1 && g_ncpu < g_found && want < 0) { kprint("SMP: aviso: "); kprint(g_why); kprint("\n"); }
}

int smp_cpus(void) { return g_enabled ? g_ncpu : 1; }
int smp_enabled(void) { return g_enabled; }
void smp_set_enabled(int on) { g_enabled = on ? 1 : 0; }
const char *smp_method(void) { return g_method; }
const char *smp_why(void) { return g_why; }
int smp_found(void) { return g_found; }

int smp_parallel_n(smp_fn fn, void *arg, int n)
{
    int cpus = smp_cpus();
    if (n > cpus) n = cpus;
    if (n < 1) n = 1;
    if (n == 1 || __atomic_exchange_n(&g_busy, 1, __ATOMIC_ACQUIRE)) { fn(0, 1, arg); return 1; }   /* sin anidar */
    uint32_t seq[SMP_MAX];
    for (int c = 1; c < n; c++) {
        struct slot *s = &g_slot[c];
        s->fn = fn; s->arg = arg; s->idx = c; s->n = n;
        seq[c] = s->seq + 1;
        __atomic_store_n(&s->seq, seq[c], __ATOMIC_SEQ_CST);
    }
    __asm__ volatile ("mfence" ::: "memory");
    for (int c = 1; c < n; c++) if (__atomic_load_n(&g_slot[c].sleeping, __ATOMIC_SEQ_CST)) apic_send_ipi(g_slot[c].apic_id, WAKE_VECTOR);
    uint64_t t0 = timer_ticks;
    fn(0, n, arg);
    g_slot[0].jobs++; g_slot[0].busy += timer_ticks - t0;
    for (int c = 1; c < n; c++) {
        uint64_t warn = rdtsc_() + us_ticks(2000000);
        while (__atomic_load_n(&g_slot[c].done, __ATOMIC_ACQUIRE) != seq[c]) {
            __asm__ volatile ("pause");
            if (warn && rdtsc_() > warn) { kprint("SMP: el nucleo "); kprint_dec((uint32_t)c); kprint(" tarda mas de 2 s en un trabajo\n"); warn = 0; }
        }
    }
    __atomic_store_n(&g_busy, 0, __ATOMIC_RELEASE);
    return n;
}
void smp_parallel(smp_fn fn, void *arg) { (void)smp_parallel_n(fn, arg, SMP_MAX); }
int smp_busy(void) { return __atomic_load_n(&g_busy, __ATOMIC_ACQUIRE) != 0; }   /* SOMA_221_RESCATE */

int smp_core_info(int i, struct smp_core_info *o)
{
    if (i < 0 || i >= SMP_MAX) return -1;
    if (i >= g_ncpu && i > 0) return -1;
    o->online = g_slot[i].online; o->mpidr = g_slot[i].apic_id; o->jobs = g_slot[i].jobs; o->busy_ticks = g_slot[i].busy;
    return 0;
}
uint64_t smp_core_wakes(int i) { return (i > 0 && i < g_ncpu) ? g_slot[i].wakes : 0; }
uint32_t smp_core_boot_ms(int i) { return (i > 0 && i < g_ncpu) ? g_boot_ms[i] : 0; }
uint32_t smp_tramp_addr(void) { return g_tramp; }
