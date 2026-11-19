/* SOMA_164_T480S: ACPI de verdad con uACPI (third_party/uacpi, licencia MIT). Este archivo es su capa de adaptacion
 * (las funciones uacpi_kernel_* que pide la biblioteca) y lo que SOMA usa de ella:
 *  - arranque del interprete AML: tablas, espacio de nombres (DSDT/SSDT), _INI/_STA, modelo de interrupciones (\_PIC);
 *  - controlador embebido (EC) de ThinkPad por los puertos de la tabla ECDT (o 0x62/0x66): sin el, los metodos de bateria,
 *    tapa y termica no pueden leer nada;
 *  - eventos (SCI): en vez de enrutar la interrupcion por nivel del SCI (una tormenta de interrupciones colgaria el sistema
 *    si algo fallase), un hilo llama al manejador de uACPI cada 100 ms. Para la tapa, el boton y la bateria basta;
 *  - apagado S5 y reinicio por el registro de la FADT.
 * Interruptor: soma.acpi=off (o el modo seguro). Si algo falla, SOMA sigue como antes: el interprete queda apagado. */
#include <stdint.h>
#include <uacpi/uacpi.h>
#include <uacpi/kernel_api.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <uacpi/opregion.h>
#include <uacpi/sleep.h>
#include <uacpi/event.h>
#include <uacpi/utilities.h>
#include <uacpi/namespace.h>
#include <uacpi/notify.h>
#include "acpi_sys.h"
#include "console.h"
#include "mem.h"
#include "thread.h"
#include "vmm.h"
#include "pmm.h"
#include "hwscan.h"
#include "bootfb.h"
#include "sysinfo.h"
#include "apic.h"
#include "io.h"
#include "fb.h"
#include "backlight.h"                                /* SOMA_168_T480S */

static struct acpi_sys_info g_info;
static volatile uint32_t g_warn, g_err;

/* ------------------------------------------------------------------ tiempo */
static inline uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
static uint64_t tpm(void) { return tsc_ticks_per_ms ? tsc_ticks_per_ms : 2000000; }
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) { return rdtsc_() * 1000000ULL / tpm(); }
void uacpi_kernel_stall(uacpi_u8 usec) { uint64_t e = rdtsc_() + tpm() * usec / 1000; while (rdtsc_() < e) __asm__ volatile ("pause"); }
void uacpi_kernel_sleep(uacpi_u64 msec) { if (msec > 0xFFFFFFFFULL) msec = 0xFFFFFFFFULL; thread_sleep((uint32_t)msec); }

/* ------------------------------------------------------------------ registro */
void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *s)
{
    if (lvl == UACPI_LOG_ERROR) g_err++;
    else if (lvl == UACPI_LOG_WARN) g_warn++;
    else return;                                                   /* INFO/TRACE/DEBUG: demasiado para la pantalla */
    kprint(lvl == UACPI_LOG_ERROR ? "ACPI error: " : "ACPI aviso: ");
    kprint(s);                                                     /* uACPI ya termina la linea */
}

/* ------------------------------------------------------------------ memoria */
void *uacpi_kernel_alloc(uacpi_size n) { return kmalloc(n ? n : 1); }
void *uacpi_kernel_alloc_zeroed(uacpi_size n) { uint8_t *p = kmalloc(n ? n : 1); if (p) for (uacpi_size i = 0; i < n; i++) p[i] = 0; return p; }
void uacpi_kernel_free(void *p, uacpi_size sz) { (void)sz; if (p) kfree(p); }

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out)
{
    uint64_t r = hw_find_rsdp();
    if (!r) return UACPI_STATUS_NOT_FOUND;
    *out = r;
    return UACPI_STATUS_OK;
}

/* Memoria fisica del firmware (tablas, regiones de operacion SystemMemory) en identidad y con escritura. El primer GiB ya
 * esta en identidad. Por encima: la RAM normal (en el mapa del firmware) con cache, el resto (MMIO, ACPI NVS) sin cache. */
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    if (!len) len = 1;
    if (addr + len < addr || addr + len > (1ULL << 46)) return UACPI_MAP_FAILED;
    uint64_t a = addr & ~0xFFFULL, e = (addr + len + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t va = a; va < e; va += 4096) {
        if (va < 0x40000000ULL) continue;
        if (va >= FB_VIRT_ADDR && va < FB_VIRT_ADDR + 0x4000000ULL) return UACPI_MAP_FAILED;
        if (va >= 0x0000004000000000ULL && va < 0x0000008000000000ULL) return UACPI_MAP_FAILED;   /* ventana del montón */
        uint64_t cur = vmm_virt_to_phys(va);
        if (cur != 0 && cur != va) return UACPI_MAP_FAILED;                                       /* ocupada por otra cosa */
        uint64_t fl = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
        if (!pmm_range_is_ram(va, 4096)) fl |= (1ULL << 3) | (1ULL << 4);                        /* PWT | PCD */
        if (vmm_map_page(va, va, fl) != 0) return UACPI_MAP_FAILED;
    }
    return (void *)(uintptr_t)addr;
}
void uacpi_kernel_unmap(void *p, uacpi_size len) { (void)p; (void)len; }        /* se queda proyectada (como hw_map_phys) */
void *acpi_sys_map_mmio(uint64_t phys, uint64_t len)   /* SOMA_168_T480S */
{
    void *p = uacpi_kernel_map(phys, len);
    return p == UACPI_MAP_FAILED ? (void *)0 : p;
}

/* ------------------------------------------------------------------ puertos */
struct ioh { uint64_t base, len; };
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out)
{
    if (base > 0xFFFF || base + len > 0x10000) return UACPI_STATUS_INVALID_ARGUMENT;
    struct ioh *h = kmalloc(sizeof *h); if (!h) return UACPI_STATUS_OUT_OF_MEMORY;
    h->base = base; h->len = len; *out = h;
    return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle h) { kfree(h); }
#define IOCHK(sz) struct ioh *io = (struct ioh *)h; if (off + (sz) > io->len) return UACPI_STATUS_INVALID_ARGUMENT; uint16_t port = (uint16_t)(io->base + off)
uacpi_status uacpi_kernel_io_read8(uacpi_handle h, uacpi_size off, uacpi_u8 *v) { IOCHK(1); *v = inb(port); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_read16(uacpi_handle h, uacpi_size off, uacpi_u16 *v) { IOCHK(2); *v = inw(port); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_read32(uacpi_handle h, uacpi_size off, uacpi_u32 *v) { IOCHK(4); *v = inl(port); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write8(uacpi_handle h, uacpi_size off, uacpi_u8 v) { IOCHK(1); outb(port, v); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write16(uacpi_handle h, uacpi_size off, uacpi_u16 v) { IOCHK(2); outw(port, v); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write32(uacpi_handle h, uacpi_size off, uacpi_u32 v) { IOCHK(4); outl(port, v); return UACPI_STATUS_OK; }

/* ------------------------------------------------------------------ PCI (mecanismo 1, segmento 0) */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address a, uacpi_handle *out)
{
    if (a.segment != 0 || a.device > 31 || a.function > 7) return UACPI_STATUS_UNIMPLEMENTED;
    *out = (uacpi_handle)(uintptr_t)(0x80000000u | (uint32_t)a.bus << 16 | (uint32_t)a.device << 11 | (uint32_t)a.function << 8);
    return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle h) { (void)h; }
static uint16_t pci_sel(uacpi_handle h, uacpi_size off) { outl(0xCF8, (uint32_t)(uintptr_t)h | (uint32_t)(off & 0xFC)); return (uint16_t)(0xCFC + (off & 3)); }
#define PCICHK if (off > 255) return UACPI_STATUS_INVALID_ARGUMENT; uint64_t fl_ = irq_off()
static uint64_t irq_off(void) { uint64_t f; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory"); return f; }
static void irq_on(uint64_t f) { if (f & 0x200) __asm__ volatile ("sti" ::: "memory"); }
uacpi_status uacpi_kernel_pci_read8(uacpi_handle h, uacpi_size off, uacpi_u8 *v) { PCICHK; *v = inb(pci_sel(h, off)); irq_on(fl_); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_pci_read16(uacpi_handle h, uacpi_size off, uacpi_u16 *v) { PCICHK; *v = inw(pci_sel(h, off)); irq_on(fl_); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_pci_read32(uacpi_handle h, uacpi_size off, uacpi_u32 *v) { PCICHK; *v = inl(pci_sel(h, off)); irq_on(fl_); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_pci_write8(uacpi_handle h, uacpi_size off, uacpi_u8 v) { PCICHK; outb(pci_sel(h, off), v); irq_on(fl_); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_pci_write16(uacpi_handle h, uacpi_size off, uacpi_u16 v) { PCICHK; outw(pci_sel(h, off), v); irq_on(fl_); return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_pci_write32(uacpi_handle h, uacpi_size off, uacpi_u32 v) { PCICHK; outl(pci_sel(h, off), v); irq_on(fl_); return UACPI_STATUS_OK; }

/* ------------------------------------------------------------------ sincronizacion (el ACPI solo corre en el nucleo 0) */
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) { return (uacpi_interrupt_state)irq_off(); }
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state s) { irq_on(s); }
uacpi_thread_id uacpi_kernel_get_thread_id(void) { void *t = thread_current(); return t ? t : (void *)1; }

struct sem { volatile int64_t n; };
uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 init) { struct sem *s = kmalloc(sizeof *s); if (s) s->n = init; return s; }
void uacpi_kernel_free_semaphore(uacpi_handle h) { kfree(h); }
uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle h, uacpi_u16 timeout)
{
    struct sem *s = h; uint64_t t0 = rdtsc_(), lim = (uint64_t)timeout * tpm();
    for (;;) {
        uint64_t f = irq_off();
        if (s->n > 0) { s->n--; irq_on(f); return UACPI_STATUS_OK; }
        irq_on(f);
        if (timeout != 0xFFFF && rdtsc_() - t0 >= lim) return UACPI_STATUS_TIMEOUT;
        thread_sleep(1);
    }
}
void uacpi_kernel_signal_semaphore(uacpi_handle h) { struct sem *s = h; uint64_t f = irq_off(); s->n++; irq_on(f); }

struct spin { volatile int v; };
uacpi_handle uacpi_kernel_create_spinlock(void) { struct spin *s = kmalloc(sizeof *s); if (s) s->v = 0; return s; }
void uacpi_kernel_free_spinlock(uacpi_handle h) { kfree(h); }
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h)
{
    struct spin *s = h; uint64_t f = irq_off();
    while (__sync_lock_test_and_set(&s->v, 1)) __asm__ volatile ("pause");
    return (uacpi_cpu_flags)f;
}
void uacpi_kernel_unlock_spinlock(uacpi_handle h, uacpi_cpu_flags f) { struct spin *s = h; __sync_lock_release(&s->v); irq_on(f); }

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *r)
{
    if (r->type == UACPI_FIRMWARE_REQUEST_TYPE_FATAL) { kprint("ACPI: el firmware pidio un error fatal (Fatal); se ignora\n"); g_err++; }
    return UACPI_STATUS_OK;
}

/* ------------------------------------------------------------------ trabajo diferido (GPE, notificaciones) */
struct work { uacpi_work_handler fn; uacpi_handle ctx; struct work *next; int queued; };
static struct work *wq_head, *wq_tail;
static volatile int wq_running;
uacpi_handle uacpi_kernel_create_work_item(void) { struct work *w = kmalloc(sizeof *w); if (w) { w->fn = 0; w->ctx = 0; w->next = 0; w->queued = 0; } return w; }
void uacpi_kernel_free_work_item(uacpi_handle h) { kfree(h); }
void uacpi_kernel_schedule_work(uacpi_work_type t, uacpi_handle item, uacpi_work_handler fn, uacpi_handle ctx)
{
    (void)t;
    struct work *w = item; if (!w) return;
    uint64_t f = irq_off();
    w->fn = fn; w->ctx = ctx;
    if (!w->queued) { w->queued = 1; w->next = 0; if (wq_tail) wq_tail->next = w; else wq_head = w; wq_tail = w; }
    irq_on(f);
}
static void wq_drain(void)
{
    for (;;) {
        uint64_t f = irq_off();
        struct work *w = wq_head;
        if (w) { wq_head = w->next; if (!wq_head) wq_tail = 0; w->queued = 0; wq_running++; }
        irq_on(f);
        if (!w) return;
        w->fn(w->ctx);
        __sync_fetch_and_sub(&wq_running, 1);
    }
}
uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
    wq_drain();
    for (int i = 0; i < 5000 && wq_running; i++) thread_sleep(1);
    return UACPI_STATUS_OK;
}

/* ------------------------------------------------------------------ SOMA_165_T480S: boton de encendido, tapa, bateria */
static volatile int pb_pending;
static volatile uint32_t pb_count, lid_events, bat_events;
static void (*pb_action)(void);
void acpi_set_power_button_action(void (*fn)(void)) { pb_action = fn; }
static void pwr_check(void)
{
    if (!pb_pending) return;
    pb_pending = 0;
    kprint("ACPI: boton de encendido: apagando\n");
    if (pb_action) pb_action();
}

static void pwr_refresh(int force);
static void ec_poll_queries(void);
static void hkey_drain(void);
static void lid_check(void);
/* ------------------------------------------------------------------ SCI por sondeo */
static uacpi_interrupt_handler sci_fn;
static uacpi_handle sci_ctx;
static volatile uint64_t sci_polls, sci_handled;
static void sci_thread(void *a)
{
    (void)a;
    for (;;) {
        thread_sleep(100);
        if (!sci_fn) continue;
        uint64_t f = irq_off();
        uacpi_interrupt_ret r = sci_fn(sci_ctx);
        irq_on(f);
        sci_polls++; if (r & UACPI_INTERRUPT_HANDLED) sci_handled++;
        ec_poll_queries();                                           /* SOMA_168_T480S: teclas Fn, avisos del EC */
        wq_drain();
        hkey_drain();
        lid_check();
        pwr_check();
        pwr_refresh(0);
    }
}
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler fn, uacpi_handle ctx, uacpi_handle *out)
{
    g_info.sci_irq = irq;
    sci_ctx = ctx; sci_fn = fn;
    *out = (uacpi_handle)(uintptr_t)1;
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler fn, uacpi_handle h) { (void)fn; (void)h; sci_fn = 0; return UACPI_STATUS_OK; }

/* ------------------------------------------------------------------ controlador embebido (EC) */
static uint16_t ec_data = 0x62, ec_cmd = 0x66;
static volatile uint64_t ec_ops, ec_timeouts;
static int ec_wait(int obf_full)           /* obf_full: esperar OBF=1; si no, esperar IBF=0 */
{
    uint64_t e = rdtsc_() + tpm() * 20;                                /* 20 ms */
    for (;;) {
        uint8_t st = inb(ec_cmd);
        if (obf_full ? (st & 1) : !(st & 2)) return 0;
        if (rdtsc_() > e) { ec_timeouts++; return -1; }
        __asm__ volatile ("pause");
    }
}
static int ec_read(uint8_t addr, uint8_t *v)
{
    if (ec_wait(0)) return -1;
    outb(ec_cmd, 0x80);                                               /* RD_EC */
    if (ec_wait(0)) return -1;
    outb(ec_data, addr);
    if (ec_wait(1)) return -1;
    *v = inb(ec_data);
    return 0;
}
static int ec_write(uint8_t addr, uint8_t v)
{
    if (ec_wait(0)) return -1;
    outb(ec_cmd, 0x81);                                               /* WR_EC */
    if (ec_wait(0)) return -1;
    outb(ec_data, addr);
    if (ec_wait(0)) return -1;
    outb(ec_data, v);
    return ec_wait(0);
}
static uacpi_handle ec_lock;
static uacpi_status ec_handler(uacpi_region_op op, uacpi_handle data)
{
    if (op == UACPI_REGION_OP_ATTACH || op == UACPI_REGION_OP_DETACH) return UACPI_STATUS_OK;
    if (op != UACPI_REGION_OP_READ && op != UACPI_REGION_OP_WRITE) return UACPI_STATUS_INVALID_ARGUMENT;
    uacpi_region_rw_data *rw = data;
    if (rw->offset > 0xFF || rw->offset + rw->byte_width > 0x100) return UACPI_STATUS_INVALID_ARGUMENT;
    uacpi_cpu_flags f = uacpi_kernel_lock_spinlock(ec_lock);
    int bad = 0;
    if (op == UACPI_REGION_OP_READ) {
        uint64_t v = 0;
        for (uint8_t i = 0; i < rw->byte_width && !bad; i++) { uint8_t b = 0; bad = ec_read((uint8_t)(rw->offset + i), &b); v |= (uint64_t)b << (8 * i); }
        rw->value = v;
    } else {
        for (uint8_t i = 0; i < rw->byte_width && !bad; i++) bad = ec_write((uint8_t)(rw->offset + i), (uint8_t)(rw->value >> (8 * i)));
    }
    uacpi_kernel_unlock_spinlock(ec_lock, f);
    ec_ops++;
    return bad ? UACPI_STATUS_HARDWARE_TIMEOUT : UACPI_STATUS_OK;
}
static void ec_setup(void)
{
    uacpi_table t;
    if (uacpi_table_find_by_signature("ECDT", &t) == UACPI_STATUS_OK) {
        struct acpi_ecdt *e = t.ptr;
        if (e->ec_control.address && e->ec_control.address < 0x10000 && e->ec_data.address && e->ec_data.address < 0x10000) {
            ec_cmd = (uint16_t)e->ec_control.address; ec_data = (uint16_t)e->ec_data.address; g_info.ec_from_ecdt = 1;
        }
        uacpi_table_unref(&t);
    }
    g_info.ec_cmd = ec_cmd; g_info.ec_data = ec_data;
    if (inb(ec_cmd) == 0xFF) { g_info.ec_present = 0; return; }                 /* nada en ese puerto (QEMU) */
    ec_lock = uacpi_kernel_create_spinlock();
    if (uacpi_install_address_space_handler(uacpi_namespace_root(), UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_handler, 0) == UACPI_STATUS_OK)
        g_info.ec_present = 1;
}

void acpi_power_setup(void);
/* ------------------------------------------------------------------ arranque */
static void fail(const char *paso, uacpi_status st) { g_info.why = paso; g_info.status = uacpi_status_to_string(st); }

int acpi_sys_init(void)
{
    g_info.why = "";
    if (bootinfo_disabled("acpi")) { g_info.why = "desactivado (soma.acpi=off o modo seguro)"; return -1; }
    uint64_t t0 = rdtsc_();
    uacpi_status st = uacpi_initialize(0);
    if (st != UACPI_STATUS_OK) { fail("tablas", st); return -1; }
    st = uacpi_namespace_load();
    if (st != UACPI_STATUS_OK) { fail("cargar DSDT/SSDT", st); return -1; }
    ec_setup();
    st = uacpi_set_interrupt_model(apic_system_active() ? UACPI_INTERRUPT_MODEL_IOAPIC : UACPI_INTERRUPT_MODEL_PIC);
    if (st != UACPI_STATUS_OK) g_warn++;
    st = uacpi_namespace_initialize();
    if (st != UACPI_STATUS_OK) { fail("inicializar dispositivos", st); return -1; }
    st = uacpi_finalize_gpe_initialization();
    if (st != UACPI_STATUS_OK) g_warn++;
    g_info.active = 1; g_info.why = "activo";
    g_info.init_ms = (uint32_t)((rdtsc_() - t0) / tpm());
    acpi_power_setup();                                              /* SOMA_165_T480S */
    thread_create("acpisci", sci_thread, 0);
    return 0;
}

const struct acpi_sys_info *acpi_sys_info(void)
{
    g_info.warnings = g_warn; g_info.errors = g_err; g_info.sci_polls = sci_polls; g_info.sci_handled = sci_handled;
    g_info.ec_ops = ec_ops; g_info.ec_timeouts = ec_timeouts;
    return &g_info;
}

/* Apagado S5: prepara (\_PTS, \_S5) con interrupciones abiertas y entra con ellas cerradas. Solo vuelve si fallo. */
int acpi_poweroff(void)
{
    if (!g_info.active) return -1;
    if (uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5) != UACPI_STATUS_OK) return -2;
    uint64_t f = irq_off();
    uacpi_status st = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    irq_on(f);
    return st == UACPI_STATUS_OK ? 0 : -3;
}
int acpi_reboot(void)
{
    if (!g_info.active) return -1;
    return uacpi_reboot() == UACPI_STATUS_OK ? 0 : -2;
}

/* Dispositivos con _HID (orden acpi). cb(ruta, hid, sta) por cada uno. */
struct walk { void (*cb)(const char *, const char *, uint32_t, void *); void *u; int n; };
static uacpi_iteration_decision walk_cb(void *u, uacpi_namespace_node *node, uacpi_u32 depth)
{
    (void)depth;
    struct walk *w = u;
    uacpi_id_string *id = 0;
    if (uacpi_eval_hid(node, &id) != UACPI_STATUS_OK || !id) return UACPI_ITERATION_DECISION_CONTINUE;
    uacpi_u32 sta = 0x0F; (void)uacpi_eval_sta(node, &sta);
    const uacpi_char *path = uacpi_namespace_node_generate_absolute_path(node);
    w->cb(path ? path : "?", id->value, sta, w->u); w->n++;
    if (path) uacpi_free_absolute_path(path);
    uacpi_free_id_string(id);
    return UACPI_ITERATION_DECISION_CONTINUE;
}
int acpi_for_each_device(void (*cb)(const char *path, const char *hid, uint32_t sta, void *u), void *u)
{
    if (!g_info.active) return -1;
    struct walk w = { cb, u, 0 };
    uacpi_namespace_for_each_child_simple(uacpi_namespace_root(), walk_cb, &w);
    return w.n;
}

/* ------------------------------------------------------------------ SOMA_165_T480S: bateria, cargador, tapa */
#define MAXBAT 2
static uacpi_namespace_node *bat_node[MAXBAT], *ac_node, *lid_node;
static int nbat;
static uacpi_namespace_node *ec_node, *hkey_node, *video_out;          /* SOMA_168_T480S */
static volatile int hkey_pending;
static uacpi_iteration_decision find_one(void *u, uacpi_namespace_node *n, uacpi_u32 d)
{
    (void)d; uacpi_namespace_node **p = u; if (!*p) *p = n; return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_bat(void *u, uacpi_namespace_node *n, uacpi_u32 d)
{
    (void)d; (void)u; if (nbat < MAXBAT) bat_node[nbat++] = n; return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_status notify_cb(uacpi_handle ctx, uacpi_namespace_node *n, uacpi_u64 v)
{
    int kind = (int)(uintptr_t)ctx;
    if (kind == 1) { lid_events++; }
    else if (kind == 2) { bat_events++; }
    else if (kind == 3 && v == 0x80) { pb_count++; pb_pending = 1; }
    else if (kind == 4 && v == 0x80) { hkey_pending = 1; }                  /* SOMA_168: el HKEY tiene eventos (MHKP) */
    else if (kind == 5 && (v == 0x86 || v == 0x87)) {                       /* SOMA_168: aviso de video: brillo +/- */
        uacpi_namespace_node *x = 0;
        if (uacpi_namespace_node_find(n, "_BCM", &x) == UACPI_STATUS_OK && x) { hotkey_note_source(3, (uint32_t)v); hotkey_brightness(3, v == 0x86); }
    }
    return UACPI_STATUS_OK;
}
static uacpi_interrupt_ret pb_fixed(uacpi_handle ctx) { (void)ctx; pb_count++; pb_pending = 1; return UACPI_INTERRUPT_HANDLED; }
static uacpi_iteration_decision pb_dev(void *u, uacpi_namespace_node *n, uacpi_u32 d)
{
    (void)u; (void)d; uacpi_install_notify_handler(n, notify_cb, (uacpi_handle)(uintptr_t)3); return UACPI_ITERATION_DECISION_CONTINUE;
}
static void hotkeys_setup(void);
void acpi_power_setup(void)
{
    if (!g_info.active) return;
    uacpi_find_devices("PNP0C0A", find_bat, 0);
    uacpi_find_devices("ACPI0003", find_one, &ac_node);
    uacpi_find_devices("PNP0C0D", find_one, &lid_node);
    for (int i = 0; i < nbat; i++) uacpi_install_notify_handler(bat_node[i], notify_cb, (uacpi_handle)(uintptr_t)2);
    if (lid_node) uacpi_install_notify_handler(lid_node, notify_cb, (uacpi_handle)(uintptr_t)1);
    if (uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, pb_fixed, 0) == UACPI_STATUS_OK) g_info.pb_fixed = 1;
    uacpi_find_devices("PNP0C0C", pb_dev, 0);
    g_info.nbat = nbat; g_info.has_ac = ac_node != 0; g_info.has_lid = lid_node != 0;
    hotkeys_setup();                                                 /* SOMA_168_T480S */
}

/* ------------------------------------------------------------------ SOMA_168_T480S: teclas del ThinkPad, EC y brillo */
static int eval_args(uacpi_namespace_node *n, const char *m, int nargs, uint64_t a0, uint64_t a1, uint64_t *ret)
{
    uacpi_object *o[2] = { 0, 0 };
    for (int i = 0; i < nargs; i++) { o[i] = uacpi_object_create_integer(i ? a1 : a0); if (!o[i]) { nargs = i; break; } }
    uacpi_object_array arr = { o, (uacpi_size)nargs };
    uacpi_object *r = 0;
    uacpi_status st = uacpi_eval(n, m, nargs ? &arr : 0, ret ? &r : 0);
    if (ret) { uacpi_u64 v = 0; if (st == UACPI_STATUS_OK && r) uacpi_object_get_integer(r, &v); *ret = v; }
    if (r) uacpi_object_unref(r);
    for (int i = 0; i < nargs; i++) if (o[i]) uacpi_object_unref(o[i]);
    return st == UACPI_STATUS_OK ? 0 : -1;
}
static uacpi_iteration_decision find_bcl(void *u, uacpi_namespace_node *n, uacpi_u32 d)
{
    (void)u; (void)d;
    uacpi_object_name nm = uacpi_namespace_node_name(n);
    if (!video_out && nm.text[0] == '_' && nm.text[1] == 'B' && nm.text[2] == 'C' && nm.text[3] == 'L') video_out = uacpi_namespace_node_parent(n);
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static void hotkeys_setup(void)
{
    uacpi_find_devices("PNP0C09", find_one, &ec_node);
    g_info.ec_node = ec_node != 0;
    /* Video: el panel (la salida con _BCL); el firmware avisa con 0x86/0x87. _DOS(4): el firmware no cambia el brillo por
     * su cuenta (lo hace el sistema), como acpi_video de Linux. El aviso se recoge con un manejador global (raiz). */
    uacpi_namespace_for_each_child_simple(uacpi_namespace_root(), find_bcl, 0);
    if (video_out) {
        g_info.video_bcl = 1;
        uacpi_namespace_node *bus = uacpi_namespace_node_parent(video_out), *dos = 0;
        if (bus && uacpi_namespace_node_find(bus, "_DOS", &dos) == UACPI_STATUS_OK && dos) g_info.dos_ok = eval_args(bus, "_DOS", 1, 4, 0, 0) == 0;
    }
    uacpi_install_notify_handler(uacpi_namespace_root(), notify_cb, (uacpi_handle)(uintptr_t)5);
    /* HKEY del ThinkPad (thinkpad_acpi de Linux): version, teclas que tiene, activar (MHKC 1) y cada tecla (MHKM n 1) */
    uacpi_find_devices("LEN0268", find_one, &hkey_node);
    if (!hkey_node) uacpi_find_devices("LEN0068", find_one, &hkey_node);
    if (!hkey_node) uacpi_find_devices("IBM0068", find_one, &hkey_node);
    if (!hkey_node || bootinfo_disabled("hkey")) return;
    uint64_t ver = 0, mask = 0;
    eval_args(hkey_node, "MHKV", 0, 0, 0, &ver);
    if ((ver >> 8) == 2) { if (eval_args(hkey_node, "MHKA", 1, 1, 0, &mask)) eval_args(hkey_node, "MHKA", 0, 0, 0, &mask); }
    else if (eval_args(hkey_node, "MHKA", 0, 0, 0, &mask)) eval_args(hkey_node, "MHKA", 1, 1, 0, &mask);
    g_info.hkey_ver = (uint32_t)ver; g_info.hkey_mask = (uint32_t)mask;
    uacpi_install_notify_handler(hkey_node, notify_cb, (uacpi_handle)(uintptr_t)4);   /* antes de activarlo: no se pierde nada */
    /* SOMA_172B: no se llama a MHKM (una vez por tecla): en el T480s hace Decrement(Arg0) y uACPI lo rechaza (93 errores al
     * arrancar). La mascara que deja el firmware ya incluye las teclas Fn que se usan (probado en el portatil). */
    if (eval_args(hkey_node, "MHKC", 1, 1, 0, 0) != 0) return;
    g_info.hkey = 1;
}
/* Consultas del EC: SCI_EVT (bit 5 del estado) = tiene un evento; QR_EC (0x84) da su numero y el firmware lo atiende en
 * el metodo _Qxx del EC (Fn+brillo, bateria, temperatura...). Por sondeo, como el resto del SCI (cada 100 ms). */
static void ec_poll_queries(void)
{
    if (!g_info.ec_present || !ec_node || !ec_lock) return;
    for (int k = 0; k < 8; k++) {
        if (!(inb(ec_cmd) & 0x20)) return;
        uint8_t q = 0; int bad;
        uacpi_cpu_flags f = uacpi_kernel_lock_spinlock(ec_lock);
        bad = ec_wait(0);
        if (!bad) { outb(ec_cmd, 0x84); bad = ec_wait(1); }
        if (!bad) q = inb(ec_data);
        uacpi_kernel_unlock_spinlock(ec_lock, f);
        if (bad || !q) return;
        g_info.ec_queries++;
        hotkey_note_source(1, q);
        static const char hx[] = "0123456789ABCDEF";
        char m[5] = { '_', 'Q', hx[q >> 4], hx[q & 15], 0 };
        uacpi_eval(ec_node, m, 0, 0);
    }
}
static void hkey_drain(void)
{
    if (!hkey_pending || !hkey_node) return;
    hkey_pending = 0;
    for (int k = 0; k < 16; k++) {
        uint64_t ev = 0;
        if (eval_args(hkey_node, "MHKP", 0, 0, 0, &ev) || !ev) return;
        hotkey_note_source(2, (uint32_t)ev);
        if (ev == 0x1010) hotkey_brightness(2, 1);
        else if (ev == 0x1011) hotkey_brightness(2, 0);
        else if (ev == 0x101B) hotkey_post(HK_MIC_MUTE);
        else hotkey_post(HK_OTHER);
    }
}
/* Tapa: cerrada apaga la luz del panel, abierta la devuelve (soma.tapa=off lo desactiva) */
static void lid_check(void)
{
    static uint32_t seen;
    if (lid_events == seen) return;
    seen = lid_events;
    if (bootinfo_disabled("tapa")) return;
    int o = acpi_lid_open();
    if (o == 0) { backlight_blank(1); g_info.lid_actions++; }
    else if (o == 1) { backlight_blank(0); g_info.lid_actions++; }
}

static uint32_t pkg_u32(uacpi_object_array *a, uacpi_size i)
{
    uacpi_u64 v = 0xFFFFFFFF;
    if (i < a->count) uacpi_object_get_integer(a->objects[i], &v);
    return (uint32_t)v;
}
static void pkg_str(uacpi_object_array *a, uacpi_size i, char *out, int max)
{
    out[0] = 0;
    uacpi_data_view dv;
    if (i >= a->count || uacpi_object_get_string_or_buffer(a->objects[i], &dv) != UACPI_STATUS_OK) return;
    int k = 0;
    for (uacpi_size j = 0; j < dv.length && k < max - 1; j++) { char c = dv.const_text[j]; if (!c) break; if (c >= 32 && c < 127) out[k++] = c; }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}
int acpi_battery_read(int i, struct acpi_battery *b)
{
    for (unsigned k = 0; k < sizeof *b; k++) ((uint8_t *)b)[k] = 0;
    if (!g_info.active || i < 0 || i >= nbat) return -1;
    uacpi_u32 sta = 0;
    if (uacpi_eval_sta(bat_node[i], &sta) == UACPI_STATUS_OK && !(sta & 0x10)) return 0;    /* bit 4: bateria colocada */
    b->present = 1;
    uacpi_object *o = 0; uacpi_object_array a;
    int ok = 0;
    if (uacpi_eval_simple_package(bat_node[i], "_BIX", &o) == UACPI_STATUS_OK && uacpi_object_get_package(o, &a) == UACPI_STATUS_OK && a.count >= 16) {
        b->unit_ma = pkg_u32(&a, 1) == 1; b->design = pkg_u32(&a, 2); b->full = pkg_u32(&a, 3); b->design_mv = pkg_u32(&a, 5);
        b->cycles = pkg_u32(&a, 8); pkg_str(&a, 16, b->model, sizeof b->model); if (a.count > 19) pkg_str(&a, 19, b->oem, sizeof b->oem);
        ok = 1;
    }
    if (o) { uacpi_object_unref(o); o = 0; }
    if (!ok && uacpi_eval_simple_package(bat_node[i], "_BIF", &o) == UACPI_STATUS_OK && uacpi_object_get_package(o, &a) == UACPI_STATUS_OK && a.count >= 9) {
        b->unit_ma = pkg_u32(&a, 0) == 1; b->design = pkg_u32(&a, 1); b->full = pkg_u32(&a, 2); b->design_mv = pkg_u32(&a, 4);
        b->cycles = 0xFFFFFFFF; pkg_str(&a, 9, b->model, sizeof b->model); if (a.count > 12) pkg_str(&a, 12, b->oem, sizeof b->oem);
        ok = 1;
    }
    if (o) { uacpi_object_unref(o); o = 0; }
    if (uacpi_eval_simple_package(bat_node[i], "_BST", &o) == UACPI_STATUS_OK && uacpi_object_get_package(o, &a) == UACPI_STATUS_OK && a.count >= 4) {
        uint32_t st = pkg_u32(&a, 0);
        b->discharging = st & 1; b->charging = (st >> 1) & 1; b->critical = (st >> 2) & 1;
        b->rate = pkg_u32(&a, 1); b->remaining = pkg_u32(&a, 2); b->voltage = pkg_u32(&a, 3);
        b->have_state = 1;
    }
    if (o) uacpi_object_unref(o);
    if (!b->have_state) return 1;
    uint32_t full = (b->full && b->full != 0xFFFFFFFF) ? b->full : b->design;
    if (full && full != 0xFFFFFFFF && b->remaining != 0xFFFFFFFF) {
        uint64_t p = (uint64_t)b->remaining * 100 / full; b->percent = p > 100 ? 100 : (uint32_t)p;
    } else b->percent = 0xFFFFFFFF;
    b->minutes = 0xFFFFFFFF;
    if (b->rate && b->rate != 0xFFFFFFFF && b->remaining != 0xFFFFFFFF) {
        if (b->discharging) b->minutes = (uint32_t)((uint64_t)b->remaining * 60 / b->rate);
        else if (b->charging && full != 0xFFFFFFFF && full > b->remaining) b->minutes = (uint32_t)((uint64_t)(full - b->remaining) * 60 / b->rate);
    }
    b->health = 0xFFFFFFFF;                                          /* capacidad actual frente a la de fabrica */
    if (b->design && b->design != 0xFFFFFFFF && b->full && b->full != 0xFFFFFFFF) b->health = (uint32_t)((uint64_t)b->full * 100 / b->design);
    return 1;
}
int acpi_ac_online(void)
{
    if (!g_info.active || !ac_node) return -1;
    uacpi_u64 v; return uacpi_eval_simple_integer(ac_node, "_PSR", &v) == UACPI_STATUS_OK ? (int)(v & 1) : -1;
}
int acpi_lid_open(void)
{
    if (!g_info.active || !lid_node) return -1;
    uacpi_u64 v; return uacpi_eval_simple_integer(lid_node, "_LID", &v) == UACPI_STATUS_OK ? (int)(v & 1) : -1;
}
void acpi_event_counts(uint32_t *pb, uint32_t *lid, uint32_t *bat) { *pb = pb_count; *lid = lid_events; *bat = bat_events; }

/* Copia de la bateria para la interfaz (que no debe esperar al EC): se renueva cada 10 s o con un aviso del firmware. */
static struct acpi_battery bat_cache[MAXBAT];
static volatile int ac_cache = -1, lid_cache = -1, cache_ok;
static uint32_t last_bat_events, low_warned;
static uint64_t last_refresh;
static void pwr_refresh(int force)
{
    if (!g_info.active || (!nbat && !ac_node && !lid_node)) return;
    uint64_t now = rdtsc_() / tpm();
    if (!force && cache_ok && now - last_refresh < 10000 && last_bat_events == bat_events) return;
    last_refresh = now; last_bat_events = bat_events;
    for (int i = 0; i < nbat; i++) { struct acpi_battery b; acpi_battery_read(i, &b); bat_cache[i] = b; }
    ac_cache = acpi_ac_online(); lid_cache = acpi_lid_open(); cache_ok = 1;
    struct acpi_power_state ps; acpi_power_state(&ps);
    if (ps.percent != 0xFFFFFFFF && ps.discharging && ps.percent <= 10 && !low_warned) {
        low_warned = 1; kprint("ACPI: BATERIA BAJA: "); kprint_dec(ps.percent); kprint(" % (enchufa el cargador)\n");
    }
    if (ps.percent != 0xFFFFFFFF && ps.percent > 15) low_warned = 0;
}
/* Resumen de todas las baterias (el T480s tiene dos: interna y extraible) */
void acpi_power_state(struct acpi_power_state *o)
{
    o->valid = cache_ok; o->ac = ac_cache; o->lid = lid_cache; o->nbat = 0;
    o->percent = 0xFFFFFFFF; o->minutes = 0xFFFFFFFF; o->charging = 0; o->discharging = 0;
    uint64_t rem = 0, full = 0, rate = 0; int any = 0;
    for (int i = 0; i < nbat; i++) {
        const struct acpi_battery *b = &bat_cache[i];
        if (!b->present || !b->have_state) continue;
        o->nbat++;
        uint32_t f = (b->full && b->full != 0xFFFFFFFF) ? b->full : b->design;
        if (b->remaining == 0xFFFFFFFF || f == 0xFFFFFFFF || !f) continue;
        rem += b->remaining; full += f; any = 1;
        if (b->rate != 0xFFFFFFFF) rate += b->rate;
        o->charging |= b->charging; o->discharging |= b->discharging;
    }
    if (any && full) { uint64_t p = rem * 100 / full; o->percent = p > 100 ? 100 : (uint32_t)p; }
    if (any && rate) {
        if (o->discharging) o->minutes = (uint32_t)(rem * 60 / rate);
        else if (o->charging && full > rem) o->minutes = (uint32_t)((full - rem) * 60 / rate);
    }
}
void acpi_power_refresh_now(void) { pwr_refresh(1); }
