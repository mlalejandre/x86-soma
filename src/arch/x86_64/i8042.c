/* SOMA_155_T480S: controlador PS/2 i8042 + teclado AT + raton PS/2 / trackpad Synaptics (Lenovo ThinkPad T480s).
 *
 * En el T480s el teclado interno, el trackpad (Synaptics, PNP LEN2xxx) y el TrackPoint cuelgan del controlador de
 * teclado que emula el EC. Linux los maneja con i8042 + atkbd + psmouse/synaptics (y, si puede, el trackpad por SMBus/RMI4).
 * Aqui se sigue la misma secuencia que Linux para dejar el controlador en un estado conocido, sin fiarse del firmware:
 *
 *   1. desactivar los dos puertos (0xAD/0xA7) y vaciar el buffer;
 *   2. reescribir el byte de control (CTR): interrupciones apagadas, relojes encendidos, traduccion a juego 1 activa;
 *   3. teclado: dejar de escanear, juego de codigos 2 (el controlador lo traduce a 1), LEDs apagados, escanear;
 *   4. raton: reset (0xFF -> AA 00), identificacion Synaptics ("E8 00 x4, E9" -> byte central 0x47), rueda IntelliMouse
 *      si no es Synaptics, 100 muestras/s, resolucion 8 cuentas/mm, escala 1:1;
 *   5. activar las interrupciones (IRQ 1 y 12) en el CTR y en el PIC.
 *
 * El trackpad se deja en modo relativo (el de fabrica): mueve el puntero, toques = clic, y el TrackPoint llega mezclado en
 * los mismos paquetes (el Synaptics reenvia su puerto "pass-through" en modo relativo). Los botones fisicos de arriba
 * (los del TrackPoint) son el boton izquierdo, central y derecho del paquete.
 *
 * Todo lo que sale del controlador pasa por i8042_service(): los bytes del teclado van al traductor de teclas y los del raton
 * a una cola que lee mouse_poll(). Se llama desde las IRQ 1 y 12 y, como respaldo, desde el reloj cada 2 ms (si en algun
 * equipo las interrupciones del 8042 no llegaran al PIC, el teclado y el trackpad seguirian funcionando). */
#include "i8042.h"
#include "io.h"
#include "idt.h"
#include "console.h"
#include "bootfb.h"

#ifdef I8042_HOST_TEST                                           /* prueba en el anfitrion (tests/test_i8042_155.c) */
#define I8042_IRQ_SAVE(fl)    ((fl) = 0)
#define I8042_IRQ_RESTORE(fl) ((void)(fl))
#else
#define I8042_IRQ_SAVE(fl)    __asm__ volatile ("pushfq; cli; popq %0" : "=r"(fl) :: "memory")
#define I8042_IRQ_RESTORE(fl) __asm__ volatile ("pushq %0; popfq" :: "r"(fl) : "memory", "cc")
#endif

#define ST_OBF   0x01      /* hay un byte para leer */
#define ST_IBF   0x02      /* el controlador aun no ha leido lo ultimo que se le escribio */
#define ST_AUX   0x20      /* el byte es del puerto auxiliar (raton) */
#define ST_TIMEOUT 0x40
#define ST_PARITY  0x80

#define CTR_KBDINT  0x01
#define CTR_AUXINT  0x02
#define CTR_SYSFLAG 0x04
#define CTR_KBDDIS  0x10
#define CTR_AUXDIS  0x20
#define CTR_XLATE   0x40

static volatile int g_busy = 1;
static int g_started;                   /* i8042_init en marcha o terminada: la IRQ 1 ya no usa el manejador antiguo */          /* durante la inicializacion y las ordenes: el servicio no toca el puerto */
static int g_present, g_kbd_ok, g_aux_ok, g_synaptics, g_wheel;
static int g_id_tries, g_id_got, g_elan_got, g_elan;               /* SOMA_168B: diagnostico de la identificacion */
static uint8_t g_id_raw[3], g_elan_raw[3];
static uint8_t g_ctr_fw, g_ctr, g_kbd_id[2], g_syn_id[3], g_aux_id;
static int g_kbd_set2, g_aux_test;
static volatile uint32_t g_kbd_bytes, g_aux_bytes, g_aux_drops, g_err_bytes;

#define AUXQ 512
static volatile uint8_t  auxq[AUXQ];
static volatile uint32_t auxq_t[AUXQ];
static volatile uint32_t auxq_head, auxq_tail;

/* ------------------------------------------------------------------ acceso al controlador (con plazos en ms) */
static uint64_t now_ms(void) { return timer_get_uptime_ms(); }

static int wait_write(uint32_t ms)
{
    uint64_t t0 = now_ms();
    for (uint32_t i = 0; ; i++) {
        if (!(inb(0x64) & ST_IBF)) return 1;
        if ((i & 63) == 63 && now_ms() - t0 > ms) return 0;
        if (i > 4000000u) return 0;                 /* por si el reloj no avanzara */
        io_wait();
    }
}
static int wait_read(uint32_t ms)
{
    uint64_t t0 = now_ms();
    for (uint32_t i = 0; ; i++) {
        if (inb(0x64) & ST_OBF) return 1;
        if ((i & 63) == 63 && now_ms() - t0 > ms) return 0;
        if (i > 4000000u) return 0;
        io_wait();
    }
}
static void flush(void)
{
    for (int i = 0; i < 64; i++) {                  /* Linux: I8042_BUFFER_SIZE = 16; aqui algo mas de margen */
        if (!(inb(0x64) & ST_OBF)) {
            io_wait(); io_wait();
            if (!(inb(0x64) & ST_OBF)) break;
        }
        (void)inb(0x60);
    }
}
static int ctl_cmd(uint8_t c) { if (!wait_write(50)) return 0; outb(0x64, c); return 1; }
static int ctl_read(uint8_t c, uint8_t *v)
{
    if (!ctl_cmd(c) || !wait_read(50)) return 0;
    *v = inb(0x60);
    return 1;
}
static int ctl_write(uint8_t c, uint8_t v)
{
    if (!ctl_cmd(c) || !wait_write(50)) return 0;
    outb(0x60, v);
    return 1;
}

static int g_ready;
static void aux_enqueue(uint8_t b);
/* Lee un byte del puerto pedido. Si ya esta todo en marcha (una orden desde la GUI), lo que llegue del otro puerto se entrega
 * a su destino normal (una tecla pulsada justo entonces no se pierde); durante el arranque se descarta. */
static int dev_read(int aux, uint8_t *v, uint32_t ms)
{
    uint64_t t0 = now_ms();
    for (;;) {
        if (!wait_read(ms)) return 0;
        uint8_t st = inb(0x64);
        if (!(st & ST_OBF)) continue;
        uint8_t b = inb(0x60);
        if (((st & ST_AUX) != 0) == (aux != 0)) { *v = b; return 1; }
        if (g_ready && !(st & (ST_TIMEOUT | ST_PARITY))) {
            if (st & ST_AUX) aux_enqueue(b); else keyboard_inject_scancode(b);
        }
        if (now_ms() - t0 > ms) return 0;
    }
}
static int dev_write(int aux, uint8_t byte)
{
    if (aux && !ctl_cmd(0xD4)) return 0;
    if (!wait_write(50)) return 0;
    outb(0x60, byte);
    return 1;
}
/* Orden a un dispositivo con su ACK (0xFA). 0xFE = repetir (una vez). */
static int dev_cmd(int aux, uint8_t byte)
{
    for (int tries = 0; tries < 2; tries++) {
        if (!dev_write(aux, byte)) return 0;
        uint8_t r;
        if (!dev_read(aux, &r, aux ? 200 : 100)) return 0;
        if (r == 0xFA) return 1;
        if (r != 0xFE) return 0;
    }
    return 0;
}
static int dev_cmd_arg(int aux, uint8_t byte, uint8_t arg) { return dev_cmd(aux, byte) && dev_cmd(aux, arg); }

/* ------------------------------------------------------------------ teclado (atkbd) */
static void kbd_setup(void)
{
    (void)dev_cmd(0, 0xF5);                                     /* dejar de escanear mientras se configura */
    g_kbd_set2 = dev_cmd_arg(0, 0xF0, 0x02);                     /* juego 2: el que el 8042 traduce al juego 1 */
    if (dev_cmd(0, 0xF2)) {                                      /* identificacion: AB 83 (AB 41 traducido) */
        uint8_t a = 0, b = 0;
        if (dev_read(0, &a, 100)) g_kbd_id[0] = a;
        if (dev_read(0, &b, 100)) g_kbd_id[1] = b;
    }
    (void)dev_cmd_arg(0, 0xED, 0x00);                            /* LEDs apagados (BloqMayus coincide con el estado) */
    g_kbd_ok = dev_cmd(0, 0xF4);                                 /* escanear */
    if (!g_kbd_ok) { (void)ctl_cmd(0xAE); g_kbd_ok = dev_cmd(0, 0xF4); }
}

/* ------------------------------------------------------------------ raton / trackpad (psmouse + synaptics) */
static int aux_rate(uint8_t r) { return dev_cmd_arg(1, 0xF3, r); }
/* Peticion especial de Synaptics: el argumento se codifica en 4 ordenes "fijar resolucion" (2 bits cada una). */
static int syn_query(uint8_t q, uint8_t out[3])
{
    for (int i = 3; i >= 0; i--) if (!dev_cmd_arg(1, 0xE8, (uint8_t)((q >> (i * 2)) & 3))) return 0;
    if (!dev_cmd(1, 0xE9)) return 0;
    for (int i = 0; i < 3; i++) if (!dev_read(1, &out[i], 200)) return 0;
    return 1;
}
static void aux_setup(void)
{
    g_aux_ok = 0;
    if (!dev_cmd(1, 0xFF)) {                                     /* reset: ACK, luego AA 00 (el trackpad tarda en su autotest) */
        if (!dev_cmd(1, 0xFF)) return;
    }
    uint8_t a = 0, b = 0;
    if (!dev_read(1, &a, 1500) || a != 0xAA) return;           /* sin autotest correcto no hay raton usable */
    (void)dev_read(1, &b, 300);
    g_aux_id = b;
    (void)dev_cmd(1, 0xF5);                                      /* sin envio de datos mientras se configura */
    uint8_t id[3] = { 0, 0, 0 };
    int got = 0;                                                 /* SOMA_168B: hasta 3 intentos (el trackpad puede tardar) */
    for (int t = 0; t < 3 && !(got && id[1] == 0x47); t++) {
        g_id_tries = t + 1;
        id[0] = id[1] = id[2] = 0;
        got = syn_query(0x00, id);
        if (!(got && id[1] == 0x47) && t < 2) (void)dev_cmd(1, 0xF5);
    }
    g_id_got = got; g_id_raw[0] = id[0]; g_id_raw[1] = id[1]; g_id_raw[2] = id[2];
    if (!(got && id[1] == 0x47)) {                               /* SOMA_168B: Elantech (elantech_detect de Linux: E6 x3, E9) */
        uint8_t e[3] = { 0, 0, 0 };
        (void)dev_cmd(1, 0xF6);
        if (dev_cmd(1, 0xE6) && dev_cmd(1, 0xE6) && dev_cmd(1, 0xE6) && dev_cmd(1, 0xE9) &&
            dev_read(1, &e[0], 200) && dev_read(1, &e[1], 200) && dev_read(1, &e[2], 200)) {
            g_elan_got = 1; g_elan_raw[0] = e[0]; g_elan_raw[1] = e[1]; g_elan_raw[2] = e[2];
            if (e[0] == 0x3C && e[1] == 0x03 && (e[2] == 0xC8 || e[2] == 0x00)) g_elan = 1;
        }
    }
    if (got && id[1] == 0x47) {                                  /* Synaptics: byte central 0x47 (SYN_ID_IS_SYNAPTICS) */
        g_synaptics = 1;
        g_syn_id[0] = id[0]; g_syn_id[1] = id[1]; g_syn_id[2] = id[2];
        (void)dev_cmd(1, 0xF6);                                  /* valores por defecto: modo relativo, toques activos */
    } else {
        (void)dev_cmd(1, 0xF6);
        g_wheel = 0;                                             /* IntelliMouse: tasas 200, 100, 80 y pedir ID (3 = rueda) */
        if (aux_rate(200) && aux_rate(100) && aux_rate(80) && dev_cmd(1, 0xF2)) {
            uint8_t r = 0;
            if (dev_read(1, &r, 200) && r == 3) g_wheel = 1;
        }
    }
    (void)aux_rate(100);
    (void)dev_cmd_arg(1, 0xE8, 0x03);                            /* 8 cuentas/mm */
    (void)dev_cmd(1, 0xE6);                                      /* escala 1:1 */
    g_aux_ok = 1;
}

/* ------------------------------------------------------------------ SOMA_167_T480S: modo absoluto (gestos) */
static uint32_t g_syn_caps, g_syn_ext09, g_syn_ext0c;
static int g_syn_abs;
/* Orden de modo / pass-through de Synaptics: el byte va en 4 "fijar resolucion" y despues una tasa especial
 * (0x14 = fijar el byte de modo; 0x28 = enviar el byte al dispositivo del pass-through, el TrackPoint). */
static int syn_special(uint8_t v, uint8_t rate)
{
    for (int i = 3; i >= 0; i--) if (!dev_cmd_arg(1, 0xE8, (uint8_t)((v >> (i * 2)) & 3))) return 0;
    return aux_rate(rate);
}
static void syn_read_caps(void)
{
    uint8_t c[3];
    g_syn_caps = g_syn_ext09 = g_syn_ext0c = 0;
    if (!syn_query(0x02, c)) return;
    g_syn_caps = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    if (!(g_syn_caps & 0x800000u)) return;                       /* sin capacidades extendidas */
    uint32_t nreq = (g_syn_caps >> 20) & 7;
    if (nreq >= 1 && syn_query(0x09, c)) g_syn_ext09 = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    if (nreq >= 4 && syn_query(0x0C, c)) g_syn_ext0c = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
}
int i8042_syn_clickpad(void) { return (g_syn_ext0c & 0x100000u) || (g_syn_ext0c & 0x000100u); }
int i8042_syn_passthrough(void) { return (g_syn_caps & 0x80u) != 0; }
int i8042_touchpad_abs(void) { return g_syn_abs; }
uint32_t i8042_syn_caps(uint32_t *ext09, uint32_t *ext0c) { if (ext09) *ext09 = g_syn_ext09; if (ext0c) *ext0c = g_syn_ext0c; return g_syn_caps; }
/* on = 1: absoluto + W + 80 paquetes/s + sin gestos propios del trackpad (los hace SOMA) y TrackPoint por pass-through.
 * on = 0: valores de fabrica (modo relativo, como hasta el 166). Devuelve 0 si quedo como se pidio. */
int i8042_touchpad_set_abs(int on)
{
    if (!g_ready || !g_aux_ok || !g_synaptics) return -1;
    g_busy = 1;
    (void)dev_cmd(1, 0xF5);                                      /* parar el envio mientras se cambia */
    flush();
    int ok;
    if (on) {
        if (!g_syn_caps) syn_read_caps();
        int wmode = (g_syn_caps & 0x800000u) != 0;               /* W necesita las capacidades extendidas */
        uint8_t mode = (uint8_t)(0x80 | 0x40 | 0x04 | (wmode ? 0x01 : 0));
        ok = wmode && syn_special(mode, 0x14);
        if (ok && i8042_syn_passthrough()) (void)syn_special(0xF4, 0x28);   /* TrackPoint: activar su envio */
        if (!ok) { (void)dev_cmd(1, 0xF6); }
    } else {
        ok = dev_cmd(1, 0xF6) ? 1 : 0;                           /* valores por defecto = modo relativo */
        (void)aux_rate(100);
        (void)dev_cmd_arg(1, 0xE8, 0x03);
        (void)dev_cmd(1, 0xE6);
    }
    g_syn_abs = on && ok;
    flush();
    auxq_tail = auxq_head;
    (void)dev_cmd(1, 0xF4);
    g_busy = 0;
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ PIC */
static void pic_unmask(int irq)
{
    if (irq < 8) { outb(0x21, (uint8_t)(inb(0x21) & ~(1u << irq))); return; }
    outb(0x21, (uint8_t)(inb(0x21) & ~(1u << 2)));               /* cascada */
    outb(0xA1, (uint8_t)(inb(0xA1) & ~(1u << (irq - 8))));
}

void i8042_init(void)
{
    g_busy = 1;
    g_started = 1;
    uint8_t st = inb(0x64);
    if (st == 0xFF) {                                           /* no hay controlador (bus flotante) */
        kprint("PS/2: no hay controlador i8042\n");
        return;
    }
    g_present = 1;
    flush();
    (void)ctl_cmd(0xAD);                                         /* puertos desactivados */
    (void)ctl_cmd(0xA7);
    flush();
    if (!ctl_read(0x20, &g_ctr_fw)) {
        kprint("PS/2: el controlador no responde (sin teclado PS/2)\n");
        g_present = 0;
        g_busy = 0;
        return;
    }
    g_ctr = (uint8_t)((g_ctr_fw | CTR_XLATE | CTR_SYSFLAG) & ~(CTR_KBDINT | CTR_AUXINT | CTR_KBDDIS | CTR_AUXDIS));
    (void)ctl_write(0x60, g_ctr);
    uint8_t t = 0xFF;
    g_aux_test = ctl_read(0xA9, &t) ? t : 0xFF;                  /* prueba del puerto auxiliar: 0 = correcto */

    (void)ctl_cmd(0xAE);                                         /* puerto del teclado */
    kbd_setup();
    (void)ctl_cmd(0xA8);                                         /* puerto auxiliar */
    aux_setup();
    flush();

    g_ctr |= CTR_KBDINT;
    if (g_aux_ok) g_ctr |= CTR_AUXINT;
    (void)ctl_write(0x60, g_ctr);
    pic_unmask(1);
    if (g_aux_ok) pic_unmask(12);
    g_ready = 1;
    g_busy = 0;

    kprint("PS/2: teclado "); kprint(g_kbd_ok ? "listo" : "SIN RESPUESTA");
    if (g_kbd_ok) { kprint(" (id "); kprint_hex8(g_kbd_id[0]); kprint(" "); kprint_hex8(g_kbd_id[1]); kprint(g_kbd_set2 ? ", juego 2 traducido)" : ")"); }
    kprint(" | puntero: ");
    if (!g_aux_ok) kprint("no detectado");
    else if (g_synaptics) { kprint("trackpad Synaptics v"); kprint_dec(g_syn_id[2] & 0x0F); kprint("."); kprint_dec(g_syn_id[0]); kprint(" + TrackPoint (modo relativo)"); }
    else if (g_elan) kprint("trackpad Elan (protocolo aun no soportado: modo raton)");
    else kprint(g_wheel ? "raton PS/2 con rueda" : "raton PS/2");
    kprint("\n");
}

/* ------------------------------------------------------------------ servicio (IRQ / reloj) */
static void aux_enqueue(uint8_t b)
{
    g_aux_bytes++;
    uint32_t nx = (auxq_head + 1) % AUXQ;
    if (nx == auxq_tail) { g_aux_drops++; return; }
    auxq[auxq_head] = b; auxq_t[auxq_head] = (uint32_t)timer_get_uptime_ms(); auxq_head = nx;
}
void i8042_service(void)
{
    if (!g_ready || g_busy) return;
    unsigned long fl;
    I8042_IRQ_SAVE(fl);
    for (int n = 0; n < 32; n++) {
        uint8_t st = inb(0x64);
        if (!(st & ST_OBF)) break;
        uint8_t b = inb(0x60);
        if (st & (ST_TIMEOUT | ST_PARITY)) { g_err_bytes++; continue; }   /* byte corrupto: Linux tambien lo descarta */
        if (st & ST_AUX) {
            aux_enqueue(b);
        } else {
            g_kbd_bytes++;
            keyboard_inject_scancode(b);
        }
    }
    I8042_IRQ_RESTORE(fl);
}

static volatile uint32_t g_irq_count;   /* SOMA_155D: si llegan IRQ 1/12 no hace falta leer el puerto en cada consulta */
void i8042_irq(int irq)
{
    if (g_ready) { g_irq_count++; i8042_service(); return; }
    if (!g_started && irq == 1) keyboard_irq_handler();          /* antes de i8042_init: como siempre */
}
void i8042_tick(uint64_t ticks)
{
    if (!g_ready) return;
    if (g_irq_count ? (ticks % 20u) == 0 : (ticks & 1u) == 0) i8042_service();   /* con IRQ: red de seguridad cada 20 ms */
}

int i8042_aux_get(uint8_t *b, uint64_t *t_ms)
{
    if (!g_irq_count) i8042_service();                           /* sin IRQ: se lee aqui (y en el reloj cada 2 ms) */
    if (auxq_tail == auxq_head) return 0;
    *b = auxq[auxq_tail];
    if (t_ms) *t_ms = auxq_t[auxq_tail];
    auxq_tail = (auxq_tail + 1) % AUXQ;
    return 1;
}

int i8042_aux_present(void) { return g_aux_ok; }
int i8042_touchpad(void) { return g_synaptics; }
int i8042_aux_wheel(void) { return g_wheel; }

void i8042_aux_stream(int on)
{
    if (!g_ready || !g_aux_ok) return;
    g_busy = 1;
    (void)dev_cmd(1, on ? 0xF4 : 0xF5);
    if (!on) auxq_tail = auxq_head;
    g_busy = 0;
}

void i8042_print_info(void)
{
    kprint("Controlador PS/2 (i8042): "); kprint(g_present ? "presente" : "ausente");
    if (g_present) { kprint(" | CTR firmware "); kprint_hex8(g_ctr_fw); kprint(" -> "); kprint_hex8(g_ctr);
        kprint(" | prueba puerto aux "); kprint_hex8((uint8_t)g_aux_test); }
    kprint("\n");
    kprint("  Teclado : "); kprint(g_kbd_ok ? "activo" : "sin respuesta");
    kprint(" (id "); kprint_hex8(g_kbd_id[0]); kprint(" "); kprint_hex8(g_kbd_id[1]); kprint(g_kbd_set2 ? ", juego 2 + traduccion)\n" : ")\n");
    kprint("  Puntero : ");
    if (!g_aux_ok) kprint("no detectado\n");
    else if (g_synaptics) {
        kprint("Synaptics TouchPad, version "); kprint_dec(g_syn_id[2] & 0x0F); kprint("."); kprint_dec(g_syn_id[0]);
        kprint(" (id "); kprint_hex8(g_syn_id[0]); kprint(" "); kprint_hex8(g_syn_id[1]); kprint(" "); kprint_hex8(g_syn_id[2]);
        kprint(g_syn_abs ? "), modo absoluto (gestos)\n" : "), modo relativo: toque = clic, TrackPoint + boton central = desplazamiento\n");
    } else { kprint(g_elan ? "trackpad Elan (protocolo aun no soportado: modo raton)" : g_wheel ? "raton PS/2 con rueda (IntelliMouse)" : "raton PS/2 basico"); kprint(" (id "); kprint_hex8(g_aux_id); kprint(")\n"); }
    if (g_aux_ok) {                                              /* SOMA_168B: lo que contesto el puntero al identificarse */
        kprint("  Identificacion: Synaptics "); kprint(g_id_got ? "respondio " : "sin respuesta ");
        kprint_hex8(g_id_raw[0]); kprint(" "); kprint_hex8(g_id_raw[1]); kprint(" "); kprint_hex8(g_id_raw[2]);
        kprint(" ("); kprint_dec((uint32_t)g_id_tries); kprint(" intentos)");
        if (!g_synaptics) { kprint(" | Elan "); if (g_elan_got) { kprint_hex8(g_elan_raw[0]); kprint(" "); kprint_hex8(g_elan_raw[1]); kprint(" "); kprint_hex8(g_elan_raw[2]); } else kprint("sin respuesta"); }
        kprint("\n");
    }
    kprint("  IRQ 1/12: "); kprint(g_irq_count ? "llegan (" : "NO llegan: se usa el respaldo del reloj ("); kprint_dec(g_irq_count); kprint(")\n");
    kprint("  Bytes   : teclado "); kprint_dec(g_kbd_bytes); kprint(", puntero "); kprint_dec(g_aux_bytes);
    kprint(", perdidos "); kprint_dec(g_aux_drops); kprint(", con error "); kprint_dec(g_err_bytes); kprint("\n");
}
