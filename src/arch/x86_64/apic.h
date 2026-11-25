#ifndef SOMA_APIC_H
#define SOMA_APIC_H
#include <stdint.h>
/* PARCHE 037: lectura (SOLO LECTURA) del LAPIC y del IOAPIC. Mapea sus paginas MMIO (sin cache) y devuelve su estado;
 * no habilita nada ni cambia el enrutado de interrupciones. Base para activar el APIC en un parche posterior. */
struct apic_info {
    int      cpu_has_apic, lapic_ok, ioapic_ok;
    uint64_t msr_base;                          /* IA32_APIC_BASE (bit 11 = habilitado, bit 8 = BSP) */
    uint32_t lapic_id, lapic_ver, lapic_svr, lapic_tpr;
    uint32_t io_id, io_ver;
    int      nredir;                            /* entradas de redireccion (max 24) */
    uint64_t redir[24];
};
int apic_probe(uint32_t lapic_phys, uint32_t ioapic_phys, struct apic_info *o);   /* 0 = LAPIC leido (IOAPIC opcional) */

/* PARCHE 038: autotest OPT-IN del temporizador del LAPIC (comando "apic test"): calibra contra el PIT, programa una
 * interrupcion de un disparo en el vector 64 y comprueba que llega. No cambia el enrutado de las IRQ del sistema.
 * Devuelve 0 si todo fue bien; <0: -1 sin LAPIC, -2 interrupciones desactivadas, -3 el PIT no avanza,
 * -4 calibracion inverosimil, -5 la interrupcion no llego. */
struct apic_test { uint32_t ticks_per_ms; uint32_t irq_wait_ms; uint32_t irqs; };
int apic_timer_selftest(struct apic_test *t);
void apic_timer_irq(void);                  /* lo llama el manejador del vector 64 */

/* PARCHE 039: enrutado de las IRQ del PIT (IRQ0 -> vector 32) y del teclado (IRQ1 -> vector 33) por el IOAPIC.
 * apic_route_on desactiva esas dos lineas en el PIC y las entrega por el IOAPIC/LAPIC; apic_route_off lo deshace
 * (todo con interrupciones desactivadas durante el cambio). Requiere apic_probe previo con el IOAPIC. */
struct apic_route_cfg { int lapic_id; int gsi[2], low[2], level[2]; };
int  apic_route_on(const struct apic_route_cfg *c);     /* 0 ok; -1 sin probe/IOAPIC; -2 ya activo */
void apic_route_off(void);
uint32_t apic_eoi_count(void);
void apic_eoi(void);                                    /* EOI del LAPIC si el enrutado esta activo; si no, no hace nada */

/* SOMA_158_T480S: modo APIC del sistema (por defecto si hay MADT con IOAPIC). Tras activar interrupciones y el i8042:
 * LAPIC encendido, teclado y trackpad por el IOAPIC, reloj por el temporizador del LAPIC (1 kHz, calibrado con el TSC) o por
 * el PIT via IOAPIC si no hay ARAT, PIC 8259 enmascarado. Si el reloj no avanza, vuelve solo al PIC.
 * 0 = activo; -1 = no se intento (ver why); -2 = se intento y se deshizo. Interruptores: soma.apic=off, soma.apic=pit. */
struct apic_sys_info {
    int active, x2apic, arat, timer_lapic;
    uint32_t lapic_id, ioapic_addr, ioapic_id, ticks_per_ms, verify_ticks;
    uint32_t gsi[3], io_index[3];          /* IRQ ISA 0 (PIT), 1 (teclado), 12 (trackpad) */
    int low[3], level[3], io_used[3];
    uint8_t pic_m, pic_s;
    const char *why;
    uint64_t tsc0, ticks0;
};
int  apic_system_start(void);
int  apic_system_active(void);
const struct apic_sys_info *apic_system_info(void);
uint64_t apic_io_redir(uint32_t idx);
void apic_clock_drift(uint64_t *ticks_ms, uint64_t *tsc_ms);                  /* entrada de redireccion del IOAPIC del sistema (orden irq) */
/* SOMA_159_T480S: arranque de los demas nucleos (smp.c) */
struct hw_acpi;
const struct hw_acpi *apic_system_acpi(void);   /* tabla MADT ya analizada; 0 si el sistema no esta en modo APIC */
int apic_lapic_x2(void);
uint32_t apic_lapic_mmio(void);
uint32_t apic_this_id(void);
int apic_send_ipi(uint32_t dest, uint32_t icr_lo);
void apic_ap_setup(void);
#endif
