/* PARCHE 037: ver apic.h. Solo lecturas (la unica escritura es el selector de registro del IOAPIC, que no tiene efectos). */
#include "apic.h"
#include "vmm.h"
#include "idt.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

static uint32_t lapic_base, io_base;                       /* direccion MMIO ya mapeada por apic_probe */
static volatile uint32_t apic_irqs;

static int map_mmio(uint32_t phys)
{
    static uint32_t done[4]; static int n;
    for (int i = 0; i < n; i++) if (done[i] == phys) return 0;
    if (phys < 0xFE000000u || (phys & 0xFFF) || n >= 4) return -1;          /* ventana MMIO estandar de la plataforma */
    if (vmm_map_page(phys, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) return -1;
    done[n++] = phys;
    return 0;
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (uint64_t)hi << 32 | lo;
}

static uint32_t io_read(uint32_t base, uint32_t reg)
{
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)base, *win = (volatile uint32_t *)(uintptr_t)(base + 0x10);
    *sel = reg;
    return *win;
}

int apic_probe(uint32_t lapic_phys, uint32_t ioapic_phys, struct apic_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    uint32_t a = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    o->cpu_has_apic = (d >> 9) & 1;
    if (!o->cpu_has_apic) return -1;
    o->msr_base = rdmsr(0x1B);
    if (map_mmio(lapic_phys) == 0) {
        lapic_base = lapic_phys;
        volatile uint32_t *l = (volatile uint32_t *)(uintptr_t)lapic_phys;
        o->lapic_id = l[0x20 / 4] >> 24; o->lapic_ver = l[0x30 / 4]; o->lapic_tpr = l[0x80 / 4]; o->lapic_svr = l[0xF0 / 4];
        o->lapic_ok = 1;
    }
    if (ioapic_phys && map_mmio(ioapic_phys) == 0) {
        o->io_id = io_read(ioapic_phys, 0) >> 24; o->io_ver = io_read(ioapic_phys, 1);
        int n = (int)((o->io_ver >> 16) & 0xFF) + 1; if (n > 24) n = 24;
        for (int i = 0; i < n; i++) o->redir[i] = (uint64_t)io_read(ioapic_phys, 0x11 + 2u * i) << 32 | io_read(ioapic_phys, 0x10 + 2u * i);
        o->nredir = n; o->ioapic_ok = 1; io_base = ioapic_phys;
    }
    return o->lapic_ok ? 0 : -1;
}

/* ---- PARCHE 038: autotest del temporizador del LAPIC ---- */
#define L_EOI 0xB0
#define L_LVT_TIMER 0x320
#define L_TIMER_INIT 0x380
#define L_TIMER_CUR 0x390
#define L_TIMER_DIV 0x3E0
#define APIC_TEST_VECTOR 64

/* SOMA_158_T480S: el LAPIC puede estar en modo x2APIC (lo deja asi el firmware si el SO lo pide, o con VT-d): entonces sus
 * registros se leen por MSR (0x800 + desplazamiento/16) y la ventana MMIO no responde. lrd/lwr sirven para los dos modos. */
static int lapic_x2;
static void wrmsr_(uint32_t msr, uint64_t v) { __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }
static inline uint32_t lrd(uint32_t off)
{
    if (lapic_x2) return (uint32_t)rdmsr(0x800u + (off >> 4));
    return *(volatile uint32_t *)(uintptr_t)(lapic_base + off);
}
static inline void lwr(uint32_t off, uint32_t v)
{
    if (lapic_x2) { wrmsr_(0x800u + (off >> 4), v); return; }
    *(volatile uint32_t *)(uintptr_t)(lapic_base + off) = v;
}

void apic_timer_irq(void)
{
    apic_irqs++;
    if (lapic_base || lapic_x2) lwr(L_EOI, 0);
}
static struct apic_sys_info sys;          /* SOMA_158: modo APIC del sistema */

int apic_timer_selftest(struct apic_test *t)
{
    t->ticks_per_ms = t->irq_wait_ms = t->irqs = 0;
    if (sys.active && sys.timer_lapic) return -6;                 /* SOMA_158: el temporizador del LAPIC es el reloj del sistema */
    if (!lapic_base) return -1;
    uint64_t fl; __asm__ volatile ("pushfq; popq %0" : "=r"(fl));
    if (!(fl & 0x200)) return -2;
    lwr(L_LVT_TIMER, 0x10000 | APIC_TEST_VECTOR);                 /* enmascarado mientras se configura */
    lwr(L_TIMER_DIV, 0x3);                                         /* divisor 16 */
    /* calibracion: 20 ticks del PIT (1 kHz) = 20 ms; se espera a un flanco para no medir un tick parcial */
    uint64_t t0 = timer_ticks; unsigned long guard = 400000000UL;
    while (timer_ticks == t0 && --guard) __asm__ volatile ("pause");
    if (!guard) return -3;
    t0 = timer_ticks;
    lwr(L_TIMER_INIT, 0xFFFFFFFFu);
    guard = 400000000UL;
    while (timer_ticks - t0 < 20 && --guard) __asm__ volatile ("pause");
    uint32_t cur = lrd(L_TIMER_CUR);
    lwr(L_TIMER_INIT, 0);
    if (!guard) return -3;
    uint64_t elapsed = 0xFFFFFFFFu - cur;
    t->ticks_per_ms = (uint32_t)(elapsed / 20);
    if (t->ticks_per_ms < 100 || t->ticks_per_ms > 4000000u) return -4;      /* 100 kHz .. 4 GHz a divisor 16: fuera de lo creible */
    /* entrega: una interrupcion de un disparo a ~5 ms */
    apic_irqs = 0;
    lwr(L_LVT_TIMER, APIC_TEST_VECTOR);                                       /* un disparo, sin mascara */
    t0 = timer_ticks;
    lwr(L_TIMER_INIT, t->ticks_per_ms * 5);
    guard = 400000000UL;
    while (!apic_irqs && timer_ticks - t0 < 100 && --guard) __asm__ volatile ("pause");
    t->irq_wait_ms = (uint32_t)(timer_ticks - t0);
    lwr(L_LVT_TIMER, 0x10000 | APIC_TEST_VECTOR);                             /* vuelve a enmascarar */
    lwr(L_TIMER_INIT, 0);
    t->irqs = apic_irqs;
    return apic_irqs ? 0 : -5;
}

/* ---- PARCHE 039: enrutado por IOAPIC ---- */
static inline void poutb_(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint8_t inb_(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static void io_write(uint32_t reg, uint32_t v)
{
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)io_base, *win = (volatile uint32_t *)(uintptr_t)(io_base + 0x10);
    *sel = reg; *win = v;
}

static int route_active, route_gsi[2];
static uint8_t pic_saved_bits;                  /* bits 0 y 1 de la mascara del PIC maestro antes de activar el enrutado */
static volatile uint32_t eoi_count;

uint32_t apic_eoi_count(void) { return eoi_count; }
void apic_eoi(void)
{
    if (!route_active) return;
    eoi_count++;
    lwr(L_EOI, 0);
}

int apic_route_on(const struct apic_route_cfg *c)
{
    if (!lapic_base || !io_base) return -1;
    if (route_active) return -2;
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    for (int i = 0; i < 2; i++) {
        uint32_t lo = (uint32_t)(32 + i) | (c->low[i] ? 1u << 13 : 0) | (c->level[i] ? 1u << 15 : 0);     /* fijo, destino fisico, sin mascara */
        route_gsi[i] = c->gsi[i];
        io_write(0x11 + 2u * (uint32_t)c->gsi[i], (uint32_t)c->lapic_id << 24);
        io_write(0x10 + 2u * (uint32_t)c->gsi[i], lo);
    }
    route_active = 1; eoi_count = 0;
    uint8_t m = inb_(0x21); pic_saved_bits = m & 3;
    poutb_(0x21, (uint8_t)(m | 3));                                  /* el PIC deja de entregar IRQ0/IRQ1 */
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
    return 0;
}

void apic_route_off(void)
{
    if (!route_active || sys.active) return;                       /* SOMA_158: el modo del sistema no se apaga desde aqui */
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    uint8_t m = inb_(0x21);
    poutb_(0x21, (uint8_t)((m & ~3u) | pic_saved_bits));
    for (int i = 0; i < 2; i++) io_write(0x10 + 2u * (uint32_t)route_gsi[i], 0x10000u | (uint32_t)(32 + i));   /* enmascarada */
    route_active = 0;
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
}

/* ===================================================================== SOMA_158_T480S: modo APIC del sistema
 * Referencias: Linux arch/x86/kernel/apic/apic.c (setup_local_APIC, calibrate_APIC_clock), io_apic.c (setup_IO_APIC_irqs).
 *
 * 1. MADT (via hw_map_phys: en UEFI esta por encima de 1 GiB): LAPIC, IOAPIC y redirecciones de las IRQ ISA.
 * 2. LAPIC: activado por software (SVR, vector espurio 255), TPR 0, LINT0 enmascarado (era la entrada ExtINT del PIC),
 *    la linea NMI que diga la MADT (LINT1 en el T480s) como NMI, errores enmascarados.
 * 3. IOAPIC: IRQ1 (teclado) -> vector 33 e IRQ12 (trackpad) -> vector 44 con su GSI, polaridad y disparo segun la MADT.
 * 4. Reloj: temporizador del LAPIC periodico a 1 kHz en el vector 32, calibrado contra el TSC. Si la CPU no garantiza que
 *    siga contando en reposo (sin ARAT, CPUID.6:EAX[2]) se usa el PIT por el IOAPIC (IRQ0 -> GSI 2 en el T480s).
 * 5. PIC 8259 enmascarado entero. Despues se comprueba que el reloj avanza; si no, se deshace todo y se vuelve al PIC. */
#include "hwinfo.h"
#include "hwscan.h"
#include "bootfb.h"
#include "sysinfo.h"
#include "console.h"
#include "fwcfg.h"

#define L_ID     0x20
#define L_TPR    0x80
#define L_SVR    0xF0
#define L_LVT_LINT0 0x350
#define L_LVT_LINT1 0x360
#define L_LVT_ERR   0x370
#define APIC_SPURIOUS_VECTOR 255

static void io_entry(uint32_t gsi_index, uint8_t vector, int low, int level, uint32_t dest, int masked)
{
    uint32_t lo = vector | (low ? 1u << 13 : 0) | (level ? 1u << 15 : 0) | (masked ? 1u << 16 : 0);   /* fijo, destino fisico */
    io_write(0x11 + 2u * gsi_index, dest << 24);
    io_write(0x10 + 2u * gsi_index, lo);
}
static uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }

/* estado del LAPIC antes de tocarlo: en modo PIC las IRQ del 8259 entran por LINT0 (ExtINT), asi que deshacer exige
 * devolverlo TODO como estaba (la prueba con fallo provocado lo demostro: sin esto el reloj quedaba parado tras volver). */
static uint32_t sav_svr, sav_tpr, sav_lint0, sav_lint1, sav_err, sav_timer, sav_div;
static void sys_rollback(void)
{
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    lwr(L_TIMER_INIT, 0);
    lwr(L_LVT_TIMER, sav_timer); lwr(L_TIMER_DIV, sav_div);
    lwr(L_LVT_LINT0, sav_lint0); lwr(L_LVT_LINT1, sav_lint1); lwr(L_LVT_ERR, sav_err);
    lwr(L_TPR, sav_tpr); lwr(L_SVR, sav_svr);
    for (int i = 0; i < 3; i++) if (sys.io_used[i]) io_entry(sys.io_index[i], (uint8_t)(32 + (i == 2 ? 12 : i)), 0, 0, 0, 1);
    poutb_(0x21, sys.pic_m); poutb_(0xA1, sys.pic_s);
    route_active = 0; sys.active = 0; sys.timer_lapic = 0;
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
}

static struct hw_acpi acpi;                                  /* SOMA_159_T480S: a nivel de archivo (smp.c lee la lista de CPU) */
int apic_system_start(void)
{
    for (unsigned i = 0; i < sizeof sys; i++) ((uint8_t *)&sys)[i] = 0;
    if (bootinfo_disabled("apic")) { sys.why = "desactivado (soma.apic=off o modo seguro)"; return -1; }
    if (route_active) { sys.why = "enrutado de prueba (orden apic) activo"; return -1; }
    uint32_t a = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (!((d >> 9) & 1)) { sys.why = "la CPU no tiene APIC"; return -1; }
    uint32_t ecx1 = c;                                     /* SOMA_159: bit 21 = x2APIC */
    a = 0; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (a >= 6) { a = 6; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d)); sys.arat = (int)((a >> 2) & 1); }
    uint64_t rsdp = hw_find_rsdp();
    if (!rsdp || hw_acpi_parse(rsdp, hw_map_phys, 0, &acpi) != 0 || !acpi.has_madt || !acpi.nioapic) {
        sys.why = "sin tabla MADT o sin IOAPIC"; return -1;
    }
    uint64_t base = rdmsr(0x1B);
    if (!(base & (1ULL << 11))) { wrmsr_(0x1B, base | (1ULL << 11)); base = rdmsr(0x1B); }   /* LAPIC habilitado globalmente */
    {   /* SOMA_159_T480S: soma.x2apic=on (o fw_cfg opt/soma/x2apic en QEMU) pasa el LAPIC a x2APIC aunque el firmware lo dejara en
         * xAPIC. Sirve para probar ese camino en el emulador; en el portatil solo hace falta si el firmware ya lo entrega asi. */
        static char fx[4];
        if (!(base & (1ULL << 10)) && ((ecx1 >> 21) & 1) &&
            (bootinfo_has_option("soma.x2apic=on") || fwcfg_read_file("opt/soma/x2apic", fx, sizeof fx))) {
            wrmsr_(0x1B, base | (1ULL << 10)); base = rdmsr(0x1B);
        }
    }
    lapic_x2 = (int)((base >> 10) & 1);
    sys.x2apic = lapic_x2;
    if (!lapic_x2) {
        uint32_t lp = acpi.lapic_addr ? acpi.lapic_addr : (uint32_t)(base & 0xFFFFF000u);
        if (map_mmio(lp) != 0) { sys.why = "no se pudo proyectar el LAPIC"; return -1; }
        lapic_base = lp;
    }
    sys.lapic_id = lapic_x2 ? lrd(L_ID) : lrd(L_ID) >> 24;
    /* rutas ISA: 0 = PIT, 1 = teclado, 12 = trackpad */
    const uint8_t isa[3] = { 0, 1, 12 };
    int ioi = -1;
    for (int i = 0; i < 3; i++) {
        hw_isa_irq_route(&acpi, isa[i], &sys.gsi[i], &sys.low[i], &sys.level[i]);
        int k = hw_ioapic_for_gsi(&acpi, sys.gsi[i]);
        if (k < 0 || (ioi >= 0 && k != ioi)) { sys.why = "las IRQ ISA no estan en un unico IOAPIC"; return -1; }
        ioi = k;
    }
    if (map_mmio(acpi.ioapic_addr[ioi]) != 0) { sys.why = "no se pudo proyectar el IOAPIC"; return -1; }
    io_base = acpi.ioapic_addr[ioi];
    sys.ioapic_addr = io_base; sys.ioapic_id = acpi.ioapic_id[ioi];
    uint32_t nent = ((io_read(io_base, 1) >> 16) & 0xFF) + 1;
    for (int i = 0; i < 3; i++) {
        uint32_t idx = sys.gsi[i] - acpi.ioapic_gsi[ioi];
        if (idx >= nent) { sys.why = "GSI fuera del IOAPIC"; return -1; }
        sys.io_index[i] = idx;
    }

    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    sys.pic_m = inb_(0x21); sys.pic_s = inb_(0xA1);
    /* El PIC se enmascara LO PRIMERO (como Linux: legacy_pic->mask_all antes de tocar el LAPIC). Si se enmascarase LINT0
     * antes, una IRQ que el PIC ya tuviera pedida (el PIT dispara cada ms) quedaba colgada sin nadie que la atendiera: en
     * QEMU llegaba como vector -1 (#GP con codigo 0xFFFFFFFA); asi lo encontro la prueba del pendrive. */
    poutb_(0x21, 0xFF); poutb_(0xA1, 0xFF);
    sav_svr = lrd(L_SVR); sav_tpr = lrd(L_TPR); sav_lint0 = lrd(L_LVT_LINT0); sav_lint1 = lrd(L_LVT_LINT1);
    sav_err = lrd(L_LVT_ERR); sav_timer = lrd(L_LVT_TIMER); sav_div = lrd(L_TIMER_DIV);
    /* LAPIC */
    lwr(L_SVR, 0x100u | APIC_SPURIOUS_VECTOR);
    lwr(L_TPR, 0);
    int nmi = acpi.nmi_lint >= 0 ? acpi.nmi_lint : 1;
    lwr(L_LVT_LINT0, nmi == 0 ? 0x400u : 0x10000u);                  /* 0x400 = entrega NMI */
    lwr(L_LVT_LINT1, nmi == 1 ? 0x400u : 0x10000u);
    lwr(L_LVT_ERR, 0x10000u | 0xFE);
    lwr(L_EOI, 0);
    /* reloj: temporizador del LAPIC (con ARAT) calibrado contra el TSC durante 10 ms */
    if (sys.arat && tsc_ticks_per_ms > 100000u && !bootinfo_has_option("soma.apic=pit")) {
        lwr(L_LVT_TIMER, 0x10000 | 32);
        lwr(L_TIMER_DIV, 0x3);                                         /* divisor 16 */
        lwr(L_TIMER_INIT, 0xFFFFFFFFu);
        uint64_t t0 = rdtsc_(), lim = tsc_ticks_per_ms * 10u;
        while (rdtsc_() - t0 < lim) __asm__ volatile ("pause");
        uint32_t cur = lrd(L_TIMER_CUR);
        lwr(L_TIMER_INIT, 0);
        uint32_t tpm = (uint32_t)((0xFFFFFFFFu - cur) / 10u);
        if (tpm >= 100 && tpm <= 4000000u) {
            sys.ticks_per_ms = tpm;
            lwr(L_LVT_TIMER, 0x20000u | 32);                           /* periodico, vector 32 (el del reloj de siempre) */
            lwr(L_TIMER_INIT, tpm);
            sys.timer_lapic = 1;
        }
    }
    uint32_t dest = sys.lapic_id;
    if (!sys.timer_lapic) { io_entry(sys.io_index[0], 32, sys.low[0], sys.level[0], dest, 0); sys.io_used[0] = 1; }   /* PIT */
    io_entry(sys.io_index[1], 33, sys.low[1], sys.level[1], dest, 0); sys.io_used[1] = 1;                            /* teclado */
    io_entry(sys.io_index[2], 44, sys.low[2], sys.level[2], dest, 0); sys.io_used[2] = 1;                            /* trackpad */
    route_active = 1; eoi_count = 0; sys.active = 1;                   /* (el PIC 8259 ya esta enmascarado desde el principio) */
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
    /* comprobacion: el reloj debe avanzar (30 ms medidos con el TSC) */
    if (fl & 0x200) {
        uint64_t t0 = timer_ticks, s0 = rdtsc_(), lim = tsc_ticks_per_ms * 30u;
        while (rdtsc_() - s0 < lim) __asm__ volatile ("pause");
        uint64_t got = timer_ticks - t0;
        sys.verify_ticks = (uint32_t)got;
        if (got < 3) { sys_rollback(); sys.why = "el reloj no avanzaba por el APIC: vuelta al PIC"; return -2; }   /* 0 si no funciona; en emulacion lenta puede contar ~10 */
    }
    sys.why = "activo";
    sys.tsc0 = rdtsc_(); sys.ticks0 = timer_ticks;                    /* para medir la deriva del reloj (orden irq) */
    return 0;
}
/* SOMA_158: ms contados por el reloj y ms segun el TSC desde que se activo el APIC (deben coincidir en hardware real) */
void apic_clock_drift(uint64_t *ticks_ms, uint64_t *tsc_ms)
{
    *ticks_ms = sys.active ? timer_ticks - sys.ticks0 : 0;
    *tsc_ms = (sys.active && tsc_ticks_per_ms) ? (rdtsc_() - sys.tsc0) / tsc_ticks_per_ms : 0;
}
int apic_system_active(void) { return sys.active; }
uint64_t apic_io_redir(uint32_t idx)
{
    if (!io_base) return 0;
    return (uint64_t)io_read(io_base, 0x11 + 2u * idx) << 32 | io_read(io_base, 0x10 + 2u * idx);
}
const struct apic_sys_info *apic_system_info(void) { return &sys; }

/* ---- SOMA_159_T480S: lo que necesita smp.c para despertar los demas nucleos ---- */
#define L_ICR_LO 0x300
#define L_ICR_HI 0x310
const struct hw_acpi *apic_system_acpi(void) { return sys.active ? &acpi : 0; }
int apic_lapic_x2(void) { return lapic_x2; }
uint32_t apic_lapic_mmio(void) { return lapic_base; }
uint32_t apic_this_id(void) { return lapic_x2 ? lrd(L_ID) : lrd(L_ID) >> 24; }
/* Envia una IPI (ICR bajo = vector | modo | nivel...) a un LAPIC por su ID. 0 = enviada; -1 = el LAPIC no la dio por entregada. */
int apic_send_ipi(uint32_t dest, uint32_t icr_lo)
{
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
    int r = 0;
    if (lapic_x2) {
        __asm__ volatile ("mfence; lfence" ::: "memory");      /* la escritura del ICR x2APIC no serializa (igual que Linux) */
        wrmsr_(0x830, (uint64_t)dest << 32 | icr_lo);
    } else {
        lwr(L_ICR_HI, dest << 24);
        lwr(L_ICR_LO, icr_lo);
        r = -1;
        for (uint32_t i = 0; i < 2000000u; i++) { if (!(lrd(L_ICR_LO) & (1u << 12))) { r = 0; break; } __asm__ volatile ("pause"); }
    }
    if (fl & 0x200) __asm__ volatile ("sti" ::: "memory");
    return r;
}
/* Prepara el LAPIC del nucleo que la llama (un secundario): mismo modo que el principal, todo enmascarado salvo las IPI. */
void apic_ap_setup(void)
{
    uint64_t base = rdmsr(0x1B);
    if (!(base & (1ULL << 11))) { base |= 1ULL << 11; wrmsr_(0x1B, base); }
    if (lapic_x2 && !(base & (1ULL << 10))) wrmsr_(0x1B, base | (1ULL << 10));
    lwr(L_TPR, 0);
    lwr(L_LVT_TIMER, 0x10000);
    lwr(L_LVT_LINT0, 0x10000);
    lwr(L_LVT_LINT1, 0x10000);
    lwr(L_LVT_ERR, 0x10000 | 0xFE);
    lwr(L_SVR, 0x1FF);
    lwr(L_EOI, 0);
}
