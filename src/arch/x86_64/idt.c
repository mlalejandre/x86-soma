#include "bootfb.h"
#include "idt.h"
#include "smp.h"
#include "web_fault.h"
int gui_fault_recover(int vector, uint64_t fault_rip, uint64_t fault_addr, uint64_t *rip, uint64_t *rsp);   /* SOMA_221_RESCATE (gui.c) */
int gui_hang_tick(uint64_t *rip, uint64_t *rsp);
#include "console.h"
#include "io.h"
#include "panic.h"
#include "bootdiag.h"
#include "usermode.h"
#include "pic_irq.h"
#include "i8042.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idtr idtr_desc;
volatile int boot_gate_active = 0;

extern void *isr_stub_table[];
extern void isr_stub_64(void), isr_stub_255(void);
void apic_timer_irq(void);
void apic_eoi(void);

static const char *exception_names[32] = {
    "Divide-by-zero (#DE)",
    "Debug (#DB)",
    "Non-maskable Interrupt (NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 Floating-Point (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

extern uint64_t gdt64[];
static struct tss_entry kernel_tss __attribute__((aligned(16)));
static uint8_t ist1_emergency_stack[16384] __attribute__((aligned(16)));

void tss_init(void)
{
    for (uint32_t i = 0; i < sizeof(kernel_tss); ++i) {
        ((uint8_t *)&kernel_tss)[i] = 0;
    }
    uint64_t ist1_top = (uint64_t)&ist1_emergency_stack[sizeof(ist1_emergency_stack)];
    kernel_tss.ist1 = ist1_top;
    kernel_tss.iomap_base = sizeof(kernel_tss);

    uint64_t base = (uint64_t)&kernel_tss;
    uint64_t limit = sizeof(kernel_tss) - 1;

    /* Descriptor TSS 64 bits en GDT (offset 0x28: 16 bytes; 0x18/0x20 = segmentos de usuario) */
    gdt64[5] = (limit & 0xFFFFULL) |
               ((base & 0xFFFFFFULL) << 16) |
               (0x89ULL << 40) |
               (((limit >> 16) & 0x0FULL) << 48) |
               (((base >> 24) & 0xFFULL) << 56);
    gdt64[6] = (base >> 32) & 0xFFFFFFFFULL;

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
    kprint("TSS: 64-bit activo con IST1 de emergencia (16 KiB en selector 0x28)\n");
}

/* SOMA_PARCHE_014_RING3: pila a la que salta la CPU al entrar desde ring 3 (interrupciones) */
void tss_set_rsp0(uint64_t rsp0) { kernel_tss.rsp0 = rsp0; }

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero        = 0;
}

static void idt_set_gate_ist(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags, uint8_t ist)
{
    idt_set_gate(num, base, sel, flags);
    idt[num].ist = ist & 0x07;
}

static inline uint64_t read_cr0(void)
{
    uint64_t v; __asm__ volatile ("mov %%cr0, %0" : "=r"(v)); return v;
}

static inline uint64_t read_cr2(void)
{
    uint64_t v; __asm__ volatile ("mov %%cr2, %0" : "=r"(v)); return v;
}

static inline uint64_t read_cr3(void)
{
    uint64_t v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v;
}

static inline uint64_t read_cr4(void)
{
    uint64_t v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v;
}

static void print_hex64(uint64_t val)
{
    kprint("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t n = (uint8_t)((val >> shift) & 0x0F);
        kputc(n < 10 ? (char)('0' + n) : (char)('A' + n - 10));
    }
}


volatile uint64_t timer_ticks = 0;

uint64_t timer_get_uptime_ms(void)
{
    return timer_ticks;
}

volatile uint32_t timer_sleep_noirq_count;   /* PARCHE 109: esperas pedidas con las interrupciones desactivadas */

void timer_sleep_ms(uint32_t ms)
{
    uint64_t fl; __asm__ volatile ("pushfq; popq %0" : "=r"(fl));
    if (!(fl & 0x200)) {
        /* PARCHE 109: con IF=0 el reloj no avanza y 'hlt' dormiria para siempre (cuelgue total). Espera acotada por iteraciones. */
        timer_sleep_noirq_count++;
        for (uint64_t i = 0, n = (uint64_t)ms * 20000u; i < n; ++i) __asm__ volatile ("pause");
        return;
    }
    uint64_t target = timer_ticks + (uint64_t)ms;
    while (timer_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void pic_remap(void)
{
    /* ICW1: Iniciar inicializacion en modo cascada */
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    /* ICW2: Vector base Master = 0x20 (32), Slave = 0x28 (40) */
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    /* ICW3: Conectar Master con Slave en IRQ2 */
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();

    /* ICW4: Modo 8086 */
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    /* Desenmascarar solo IRQ0 (bit 0 = 0 en Master). Resto enmascarado por ahora */
    outb(0x21, 0xFC); /* Desenmascarar IRQ0 (Timer) e IRQ1 (Teclado) */
    outb(0xA1, 0xFF);
}

void timer_init(uint32_t freq_hz)
{
    pic_remap();

    uint32_t divisor = 1193182 / freq_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Canal 0, lobyte/hibyte, Modo 2 (generador de frecuencia), binario */
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

volatile uint64_t irq_spurious_count;   /* PARCHE 108: IRQ espurias del PIC absorbidas (7 y 15) */
volatile uint64_t irq_unhandled_count;  /* PARCHE 108: IRQ reales sin manejador (confirmadas para no bloquear la linea) */

static uint8_t pic_read_isr(uint16_t cmd_port)
{
    outb(cmd_port, 0x0B);               /* OCW3: la proxima lectura devuelve ISR */
    return inb(cmd_port);
}

/* SOMA_158_T480S: contadores por vector (orden "irq") */
static volatile uint64_t irq_vec_count[48];
static volatile uint64_t irq_lapic_spurious;
uint64_t idt_irq_count(int vector)
{
    if (vector == 255) return irq_lapic_spurious;
    return (vector >= 32 && vector < 48) ? irq_vec_count[vector] : 0;
}

static void irq_dispatch(struct trap_frame *tf)
{
    irq_vec_count[tf->vector & 47]++;
    if (tf->vector == 32) {
        timer_ticks++;
        outb(0x20, 0x20);
        apic_eoi();                                                /* PARCHE 039: no hace nada si la IRQ viene del PIC */
        watchdog_tick(timer_ticks, tf->rip, tf->rbp, tf->rsp);   /* SOMA_PARCHE_008_PANIC */
        if ((tf->cs & 3) == 0 && (tf->rflags & 0x200) && gui_hang_tick(&tf->rip, &tf->rsp))   /* SOMA_221_RESCATE: aplicacion colgada */
            tf->rflags = (tf->rflags | 0x200) & ~0x40500ull;
        usermode_on_tick(timer_ticks, tf->cs);                     /* SOMA_PARCHE_014_RING3 */
        i8042_tick(timer_ticks);                                   /* SOMA_155_T480S: respaldo del teclado/trackpad PS/2 */
        return;
    }

    if (tf->vector == 33) {
        i8042_irq(1);                                              /* SOMA_155_T480S (antes keyboard_irq_handler) */
        outb(0x20, 0x20);
        apic_eoi();                                                /* PARCHE 039 */
        return;
    }

    if (tf->vector == 44) {                                        /* SOMA_155_T480S: IRQ 12 = raton/trackpad PS/2 */
        i8042_irq(12);
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        apic_eoi();
        return;
    }

    /* PARCHE 108: el resto de IRQ del PIC (2..15) no tiene manejador. Se distingue la espuria de la real con el registro ISR. */
    uint8_t im = tf->vector == 39 ? pic_read_isr(0x20) : 0, is = tf->vector == 47 ? pic_read_isr(0xA0) : 0;
    switch (pic_irq_action((unsigned)tf->vector, im, is)) {
    case PIC_IRQ_SPURIOUS_NO_EOI:         irq_spurious_count++; break;
    case PIC_IRQ_SPURIOUS_ACK_MASTER:     irq_spurious_count++; outb(0x20, 0x20); break;
    case PIC_IRQ_ACK_BOTH:                irq_unhandled_count++; outb(0xA0, 0x20); outb(0x20, 0x20); break;
    default:                              irq_unhandled_count++; outb(0x20, 0x20); break;
    }
}

void isr_exception_handler(struct trap_frame *tf)
{
    if (tf->vector == 255) { irq_lapic_spurious++; return; }   /* PARCHE 038: espurio del LAPIC: se ignora (sin EOI) */
    if (tf->vector == 64) { apic_timer_irq(); return; }  /* PARCHE 038: autotest del temporizador del LAPIC */
    if (tf->vector >= 32 && tf->vector < 48) {
        irq_dispatch(tf);
        return;
    }
    /* SOMA_166_T480S: rdmsr/wrmsr "seguros" (msr_read_safe/msr_write_safe): si el registro no existe la CPU da #GP; se
     * salta la instruccion y se devuelve error en RSI (tabla msr_fix: direccion que falla -> direccion de continuacion). */
    if (tf->vector == 13 && (tf->cs & 3) == 0) {
        extern const uint64_t __start_msr_fix[], __stop_msr_fix[];
        for (const uint64_t *f = __start_msr_fix; f + 1 < __stop_msr_fix; f += 2)
            if (f[0] == tf->rip) { tf->rip = f[1]; tf->rsi = 1; return; }
    }
    /* SOMA_198: un fallo del motor del navegador (su hilo de trabajo, en ring 0) cierra la pagina, no el sistema (web_fault.h) */
    if ((tf->cs & 3) == 0 && tf->vector < 32 && web_fault_recover((int)tf->vector, tf->rip, tf->vector == 14 ? read_cr2() : 0, &tf->rip, &tf->rsp)) {
        tf->rflags = (tf->rflags | 0x200) & ~0x40500ull;           /* con interrupciones; sin DF, TF ni AC */
        return;
    }
    /* SOMA_221_RESCATE: un fallo dentro de una aplicacion del escritorio cierra esa ventana, no el sistema */
    if ((tf->cs & 3) == 0 && tf->vector < 32 && gui_fault_recover((int)tf->vector, tf->rip, tf->vector == 14 ? read_cr2() : 0, &tf->rip, &tf->rsp)) {
        tf->rflags = (tf->rflags | 0x200) & ~0x40500ull;
        return;
    }
    /* SOMA_PARCHE_014_RING3: una excepcion en ring 3 mata el proceso de usuario, no el kernel */
    if ((tf->cs & 3) == 3 && tf->vector < 32 && smp_user_fault(tf->vector, tf->rip, read_cr2())) return;   /* SOMA_244_HILOS (no vuelve) */
    if ((tf->cs & 3) == 3 && usermode_active()) {
        if (tf->vector == 14 && usermode_demand_fault(read_cr2(), tf->error_code)) return;   /* PARCHE 138: region bajo demanda */
        usermode_on_fault(tf->vector, tf->rip, read_cr2());
    }
    {   /* PARCHE 109: excepcion DURANTE el informe de panico: no reentrar (desbordaria la pila); detener de forma observable */
        int nivel = panic_enter();
        if (nivel >= 1) {
            if (nivel == 1) kprint("\n[PANICO ANIDADO] otra excepcion durante el informe: sistema detenido\n");
            qemu_exit(0x23);
            for (;;) __asm__ volatile ("cli; hlt");
        }
    }
    console_muted = 0; /* un panic dentro de una redireccion '>' debe ser visible */
    console_capture_abort(); /* PARCHE 116: ni dentro de una captura de pruebas */
    uint64_t cr2 = read_cr2();
    uint64_t cr0 = read_cr0();
    uint64_t cr3 = read_cr3();
    uint64_t cr4 = read_cr4();

    kprint("\n\n========================================================================\n");
    bootfb_console_panic();        /* SOMA_154_T480S: en el portatil, el panico se ve en pantalla aunque la GUI estuviera activa */
    kprint("!!! KERNEL PANIC: EXCEPCION CPU DETECTADA !!!\n");
    kprint("========================================================================\n");
    bootdiag_panic_hint();         /* SOMA_228_ARRANQUE: si cae durante el arranque, en que etapa y como saltarla */

    kprint("Excepcion: [Vector ");
    kprint_dec((uint32_t)tf->vector);
    kprint("] ");
    if (tf->vector < 32) {
        kprint(exception_names[tf->vector]);
    } else {
        kprint("Interrupcion Desconocida");
    }
    kprint("\nCodigo Error: ");
    print_hex64(tf->error_code);
    kprint("\n\nREGISTROS DE EJECUCION:\n");
    kprint("  RIP: "); print_hex64(tf->rip);
    kprint("   CS:  "); print_hex64(tf->cs);
    kprint("\n  RSP: "); print_hex64(tf->rsp);
    kprint("   SS:  "); print_hex64(tf->ss);
    kprint("\n  RFLAGS: "); print_hex64(tf->rflags);
    kprint("\n\nREGISTROS DE CONTROL Y MEMORIA:\n");
    if (tf->vector == 14) {
        kprint("  CR2 (Direccion fallida): "); print_hex64(cr2); kprint("  <-- FALLO DE PAGINA\n");
    }
    kprint("  CR0: "); print_hex64(cr0);
    kprint("   CR3 (PML4): "); print_hex64(cr3);
    kprint("\n  CR4: "); print_hex64(cr4);
    kprint("\n\nREGISTROS GENERALES:\n");
    kprint("  RAX: "); print_hex64(tf->rax); kprint("   RBX: "); print_hex64(tf->rbx); kprint("\n");
    kprint("  RCX: "); print_hex64(tf->rcx); kprint("   RDX: "); print_hex64(tf->rdx); kprint("\n");
    kprint("  RSI: "); print_hex64(tf->rsi); kprint("   RDI: "); print_hex64(tf->rdi); kprint("\n");
    kprint("  RBP: "); print_hex64(tf->rbp); kprint("   R8:  "); print_hex64(tf->r8);  kprint("\n");
    kprint("  R9:  "); print_hex64(tf->r9);  kprint("   R10: "); print_hex64(tf->r10); kprint("\n");
    kprint("  R11: "); print_hex64(tf->r11); kprint("   R12: "); print_hex64(tf->r12); kprint("\n");
    kprint("  R13: "); print_hex64(tf->r13); kprint("   R14: "); print_hex64(tf->r14); kprint("\n");
    kprint("  R15: "); print_hex64(tf->r15); kprint("\n");

    panic_print_context(tf->rip, 0, tf->rbp, tf->rsp);   /* SOMA_PARCHE_008_PANIC */

    kprint("========================================================================\n");
    kprint("Sistema detenido por proteccion (HLT).\n");
    if (boot_gate_active) {
        kprint("[PUERTA DE ARRANQUE] Fallo critico durante prueba canaria. Abortando QEMU...\n");
        qemu_exit(0x22);
    }
    qemu_exit(0x23); /* Salida con codigo 0x47 observable por el host */

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void idt_init(void)
{
    tss_init();

    for (int i = 0; i < 48; ++i) {                                /* PARCHE 108: 0..47 (antes 0..33: IRQ 2..15 sin puerta) */
        idt_set_gate((uint8_t)i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);
    }

    idt_set_gate(64,  (uint64_t)isr_stub_64,  0x08, 0x8E);        /* PARCHE 038 */
    idt_set_gate(255, (uint64_t)isr_stub_255, 0x08, 0x8E);

    /* Blindaje anti-Triple Fault: #DF (8) y #PF (14) conmutan a pila IST1 */
    idt_set_gate_ist(8,  (uint64_t)isr_stub_table[8],  0x08, 0x8E, 1);
    idt_set_gate_ist(14, (uint64_t)isr_stub_table[14], 0x08, 0x8E, 1);

    idtr_desc.limit = (uint16_t)(sizeof(idt) - 1);
    idtr_desc.base  = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr_desc));
    kprint("IDT: 32 excepciones + 16 IRQ del PIC registradas (IST1 activo en #DF/#PF)\n");
    /* PARCHE 108: autoprueba. Un 'int' de software a los vectores 39 y 47 recorre la misma ruta que una IRQ espuria del 8259 (ISR sin
     * bit activo): debe absorberse sin panico y quedar contada. Antes de este parche habria acabado en #NP (puerta ausente). */
    uint64_t antes = irq_spurious_count;
    __asm__ volatile ("int $39" : : : "memory");
    __asm__ volatile ("int $47" : : : "memory");
    kprint(irq_spurious_count == antes + 2 ? "IDT: IRQ espuria 7/15 absorbida sin panico: OK\n"
                                           : "IDT: IRQ espuria 7/15: FALLO (no se contabilizo)\n");
}

/* SOMA_159_T480S: los nucleos secundarios usan la misma IDT; smp.c pone su vector de despertar (0xF0) */
void idt_load(void) { __asm__ volatile ("lidt %0" : : "m"(idtr_desc)); }
void idt_set_vector(uint8_t vec, void (*stub)(void)) { idt_set_gate(vec, (uint64_t)(uintptr_t)stub, 0x08, 0x8E); }
